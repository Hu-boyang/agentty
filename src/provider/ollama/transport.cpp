// agentty::provider::ollama —本地Ollama服务器的本机/api/chat 传输层。
// 有关基本原理，请参阅 transport.hpp。以 zed-industries/zed
// crates/ollama/src/ollama.rs 为模型：NDJSON 数据流、结构化 message.tool_calls、
// object-form 工具参数、keep_alive + options.num_predict。
//
// 我们更喜欢Ollama的结构化tool_calls通道，但弱本地模型（qwen2.5
// -coder、codellama等）通常会将工具调用泄漏为`content`中的裸
// JSON。因此，内容在 START 处保存为 -then-classified
// 响应：如果能解析为工具调用对象或数组，或带 ```json / <tool_call>
// 包装器)命名广告工具，我们将其打捞成结构化的调用；
// 否则它就会像普通散文一样泛滥。一旦任何结构化的 tool_calls
// 帧 到达，抢救将被禁用（模型可以驱动真实通道）。

#include "agentty/provider/ollama/transport.hpp"
#include "agentty/domain/reasoning_off.hpp"
#include "agentty/version.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/stream_scaffold.hpp"
#include "agentty/provider/usage.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/provider/wire.hpp"
#include "agentty/provider/debug.hpp"
#include "agentty/provider/openai/transport.hpp"   // local_model_action_rules (#91)
#include "agentty/provider/wire_supersede.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // util::workspace_root/project_root — AGENTS.md锚定+步行开始
#include "agentty/util/base64.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::provider::ollama {

using json = nlohmann::json;
using auth::AuthHeader;

namespace {

// ── UTF-8 洗涤剂 ─────────────────────────────────────────────────────────────
// 工具输出或粘贴的斑点可以携带无效的UTF-8； nlohmann 的
// dump() 扔在上面。将每个格式错误的 字节 替换为 U+FFFD，
// 这样请求就会生成，而不是回合因 JSON type_error 而终止。
// 共享严格实现（拒绝超长编码+代理）——参见
// wire::scrub_utf8。
using wire::scrub_utf8;

http::Headers build_request_headers(const AuthHeader& auth) {
    http::Headers h;
    h.push_back({"accept", "application/json"});
    h.push_back({"content-type", "application/json"});
    h.push_back({"user-agent", ::agentty::user_agent()});
    // Ollama默认不需要认证，但是远程/代理实例可以位于不记名密钥后面
    // ——以与OpenAI系列相同的方式发出它（
    // auth::bearer_token是用于提取的单一事实来源）。
    if (std::string tok = auth::bearer_token(auth); !tok.empty())
        h.push_back({"authorization", "Bearer " + tok});
    return h;
}

bool is_assistant_with_results(const Message& m) {
    return wire::is_assistant_with_results(m);
}

// ── 数据流 状态 ─────────────────────────────────────────────────────────────
struct StreamCtx {
    EventSink   sink;
    // 镜像req.show_reasoning —门是否吞下<think>…
    // </think>文本被表面化为StreamThinkingDelta (
    // 可见推理）或简化为StreamHeartbeat (仅活性；
    // 没有隐藏捕获使消息+会话文件膨胀）。本机`thinking`字段已被门控
    // 请求-side（`think:true`仅在show_reasoning
    // 时发送）。默认为 true，因此测试工具保持捕获语义。
    bool        show_reasoning = true;
    // NDJSON 行框架（缓冲区 + 读取游标 + 压缩）位于共享
    // wire::LineFramer 中 — 请参阅 include/agentty/provider/wire.hpp.
    // Ollama 使用比 SSE 默认值更大的压缩阈值，
    // 因为其本机 帧（整个消息对象）运行得比 SSE 增量更大。
    wire::LineFramer framer{256 * 1024};
    int         tool_seq = 0; // 唯一化合成工具-call id
    StopReason  stop_reason = StopReason::Unspecified;
    bool        terminated  = false;

    // ── 漏损的-tool-call 救助(当地型号微弱) ────────────────────
    // 内容帧被保留，而响应COULD仍然是裸工具-call JSON。
    // 一旦我们决定这是散文，我们就冲洗 + 停止持有；
    // 一旦结构化的 tool_calls 帧 到达，打捞就会被禁用。
    std::string text_hold;                 // 可疑的缓冲内容
    bool        holding         = true;    // 仍在缓冲潜在工具JSON
    bool        salvage_eligible = true;   // 仍可工具调用（启动）
    bool        any_text_flushed = false;
    bool        any_structured_tool = false;
    int         salvage_seq     = 0;       // 统一回收的 调用 id
    std::vector<std::string> known_tools;  // 我们可以挽救的工具名称
    bool allow_remember_salvage = false;
    bool allow_forget_salvage = false;
    bool allow_wipe_salvage = false;
    std::string full_content;              // 每条内容字节（救援扫描）

    // JSON-protocol模式（代理-zero风格，非常弱的模式）。
    // 没有发送本机工具数组； 整个 响应预计是一个
    // `{"thoughts":[...],"tool_name":"...","tool_args":{...}}`
    // 对象（或用于聊天 -only 回合的简单散文）。启用宽恕第一-`{`..
    // 终端回收中的最后`}`提取，
    // 因此对象周围的leading/trailing叙述不会击败工具调用。
    bool        json_protocol   = false;

    // ── 理由说明 -model <think> 剥离 ───────────────────────────────
    // 通过 Ollama 的本机 `thinking` 字段驱动 不 的推理模型（
    // deepseek-r1、qwen3、…）将它们的 chain-of-thought
    // 转储到包裹在 <think>…</think> 中的 `content` 中。
    // 我们必须将 不 作为用户可见的散文发出，否则前导 <think> 块将击败
    // could_be_tool_json 保留（它不以 `{` 开头），
    // 并将推理作为文本刷新在真实工具 调用 之前。在 `in_think` 期间，
    // 我们吞咽内容直到 </think> 结束，然后在接下来的任何内容中
    // 恢复正常的 hold/classify。
    bool        in_think        = false;
    bool        think_seen      = false;  // 本回合曾进入思考区块
    // 部分-tag保留：<think>/</think>标签可以跨NDJSON
    // 帧 ("<thi" + "nk>")分割。 filter_think 阻止
    // chunk-final 字节 运行，该运行是标签的正确前缀，
    // 并将其重新-prepends 到下一个块；如果标签从未完成，则在 `done` 处刷新为文字内容。
    std::string think_carry;

    // ── 渐进式 `response` 流式传输 (JSON-protocol 聊天回复) ───
    // 在json_protocol模式下，普通聊天回复作为语法-forced对象{
    // "tool_name":"response","tool_args":{
    // "text":"…"}}流式char-by-char到达。旧路径持有 整个 对象，
    // 并在 `done` 处发出一个 StreamTextDelta 中的文本 -
    // 因此 正文 出现并定居在单个 帧 中，使 maya 的 reveal_fx
    // 没有任何动画（没有打字机），并强制 one-frame 冻结切换（重复的
    // 正文）。相反，我们检测 tool_name=="response"
    // mid-hold，找到回复字段的值字符串，并且 数据流 的 DECODED
    // 字符随着 传输 的传递而递增——就像本机文本 数据流 一样。
    // resp_active 控制路径； resp_scan_pos 是我们在
    // text_hold 中扫描值 -string 开口的距离；
    // resp_in_value 表示我们在字符串内部，resp_value_pos 是
    // 下一个未解码的 字节 的索引； resp_esc 跟踪 待处理 反斜杠转义。
    bool        resp_active   = false;  // 确认tool_name=="response"
    bool        resp_done     = false;  // 流式传输收盘报价
    bool        resp_in_value = false;  // 光标位于值字符串内部
    bool        resp_esc      = false;  // 最后的字节是未消耗的'\\'
    std::size_t resp_scan_pos = 0;      // 定位值时扫描光标
    std::size_t resp_value_pos = 0;     // 接下来字节要解码里面的值
    std::string resp_emitted;           // 解码文本已经流式传输

};

// ── 救护人员(从openai/transport.cpp起) ──────────────────

// Arg-key修复弱局部模型。他们通常会使用 WRONG 参数键发出 RIGHT
// 工具 - `bash` 使用 {"cmd":"ls"} 而不是 {
// "command":"ls"}，`read` 使用 {"file":
// "x"} 而不是 {"path":"x"}，`grep` 使用 {"query":
// "foo"} 而不是{"pattern":"foo"}。然后，该工具发现没有必需的参数、
// 错误，并且弱模型循环 re-emitting 相同的损坏的 调用。
// 我们将一小组well-known别名重新映射到它们的规范键PER TOOL
// - 但是当规范键不存在并且别名存在时，会映射到ONLY，因此永远不会触及正确的模型。
// 保守：表中没有的工具保留为 -is。
void repair_arg_keys(const std::string& tool, json& args) {
    if (!args.is_object()) return;
    // （规范，{别名...}）每个工具。第一个找到的别名获胜。
    struct Alias { const char* canon; std::vector<const char*> from; };
    auto remap = [&](const std::vector<Alias>& table) {
        for (const auto& a : table) {
            if (args.contains(a.canon)) continue;          // 已经正确
            for (const char* f : a.from) {
                if (args.contains(f)) {
                    args[a.canon] = args[f];
                    args.erase(f);
                    break;
                }
            }
        }
    };
    // `bash` 对于 `shell` 保留为 LEGACY
    // ALIAS：该工具注册为 `shell`，但名称 `bash`
    // 在模型训练数据中非常常见，弱本地模型会发出它 - 在这里接受它，
    // 这样它的参数仍然会被规范化（分派 将其解析为 下游）。
    if (tool == "shell" || tool == "bash" || tool == "diagnostics") {
        remap({{"command", {"cmd", "shell", "script", "run", "cmdline"}}});
    } else if (tool == "read" || tool == "list_dir" || tool == "find_definition") {
        remap({{"path", {"file", "filepath", "file_path", "filename", "dir",
                          "directory", "target"}}});
    } else if (tool == "write") {
        remap({{"file_path", {"path", "file", "filepath", "filename", "target"}},
               {"content",   {"text", "body", "data", "contents", "code"}}});
    } else if (tool == "edit") {
        remap({{"path",     {"file", "filepath", "file_path", "filename", "target"}},
               {"old_text", {"old", "old_string", "search", "find", "from"}},
               {"new_text", {"new", "new_string", "replace", "replacement", "to"}}});
    } else if (tool == "grep") {
        remap({{"pattern", {"query", "q", "regex", "search", "text", "term"}},
               {"path",    {"dir", "directory", "root", "file"}}});
    } else if (tool == "glob") {
        remap({{"pattern", {"query", "q", "glob", "pat", "match"}},
               {"path",    {"dir", "directory", "root"}}});
    } else if (tool == "web_fetch") {
        remap({{"url", {"uri", "link", "address", "href"}}});
    } else if (tool == "web_search") {
        remap({{"query", {"q", "search", "term", "text", "prompt"}}});
    } else if (tool == "search_docs") {
        remap({{"query", {"q", "search", "term", "text", "question"}}});
    }
}

// `s`有可能是泄露工具-call的START吗？仅在响应开始时有意义 (
// salvage_eligible)。共享分类器 — 请参阅
// wire::could_be_tool_json（单一事实来源；在此处和 openai
// 传输层 中重复）。
using wire::could_be_tool_json;

// 剥离 <tool_call>…</tool_call> 和 ```json…``` 包裹以及周围空白。
[[nodiscard]] std::string_view strip_tool_call_wrappers(std::string_view sv) noexcept {
    auto ltrim = [](std::string_view& s) {
        while (!s.empty() && (s.front()==' '||s.front()=='\t'
                              ||s.front()=='\n'||s.front()=='\r')) s.remove_prefix(1);
    };
    auto rtrim = [](std::string_view& s) {
        while (!s.empty() && (s.back()==' '||s.back()=='\t'
                              ||s.back()=='\n'||s.back()=='\r')) s.remove_suffix(1);
    };
    ltrim(sv); rtrim(sv);
    if (sv.starts_with("<tool_call>")) {
        sv.remove_prefix(std::string_view{"<tool_call>"}.size());
        if (auto p = sv.rfind("</tool_call>"); p != std::string_view::npos)
            sv = sv.substr(0, p);
        ltrim(sv); rtrim(sv);
    }
    if (sv.starts_with("```")) {
        sv.remove_prefix(3);
        // 跳过栅栏线上的可选语言标签（JSON、sh、bash、
        // tool_code、……）直到第一个换行符。弱模型将泄露的
        // 工具 调用 包裹在他们想要的任何围栏中。
        if (auto nl = sv.find('\n'); nl != std::string_view::npos) {
            std::string_view first = sv.substr(0, nl);
            bool word = !first.empty();
            for (char c : first)
                if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c=='-')) { word=false; break; }
            if (word) sv.remove_prefix(nl + 1);   // 删除 lang 标记行
        } else if (sv.starts_with("json")) {
            sv.remove_prefix(4);
        }
        ltrim(sv);
        if (auto p = sv.rfind("```"); p != std::string_view::npos)
            sv = sv.substr(0, p);
        rtrim(sv);
    }
    return sv;
}

// 举行的缓冲区像工具-call对象一样打开，但不解析(传输被切断)，
// 或者是包装器-only残余物/空`{}`——丢弃它而不是显示垃圾。
[[nodiscard]] bool hold_is_truncated_tool_json(std::string_view sv) noexcept {
    std::string_view raw = sv;
    sv = strip_tool_call_wrappers(sv);
    if (sv.empty() && raw != sv) return true;
    if (sv == "json" || sv == "```" || sv == "`") return true;
    if (sv == "{}") return true;
    if (sv.empty() || sv.front() != '{') return false;
    try { auto _ = json::parse(sv); (void)_; return false; }
    catch (...) { return true; }
}

// 命名 UNADVERTISED 工具的完整 tool-call-shaped 对象 — 将其删除。
[[nodiscard]] bool hold_is_unknown_tool_call(
        std::string_view sv, const std::vector<std::string>& known) noexcept {
    if (known.empty()) return false;
    sv = strip_tool_call_wrappers(sv);
    if (sv.empty() || sv.front() != '{') return false;
    json j;
    try { j = json::parse(sv); } catch (...) { return false; }
    if (!j.is_object()) return false;
    std::string name;
    if (j.contains("name") && j["name"].is_string())
        name = j["name"].get<std::string>();
    else if (j.contains("function") && j["function"].is_string())
        name = j["function"].get<std::string>();
    else if (j.contains("tool_name") && j["tool_name"].is_string())
        name = j["tool_name"].get<std::string>();
    else if (j.contains("tool") && j["tool"].is_string())
        name = j["tool"].get<std::string>();
    else
        return false;
    if (!j.contains("arguments") && !j.contains("parameters")
            && !j.contains("tool_args") && !j.contains("args")) return false;
    // 在已知的-tool检查之前去除任何`tool:action`后缀。
    if (auto colon = name.find(':'); colon != std::string::npos)
        name = name.substr(0, colon);
    // `response`伪-tool是JSON-protocol聊天逃生舱口——它是
    // known_tools中的NOT（我们从不宣传它），但它的载荷是用户-visible回复，
    // 因此必须将NEVER放在这里。 rescue_json_protocol 将其文本
    // 展开为散文；将其报告为 "not an unknown tool call"。
    if (name == "response") return false;
    for (const auto& t : known) if (t == name) return false;
    return true;
}

// 从 JSON 对象发出一个回收工具 调用。如果被消耗则返回 true（
// 发射 OR 故意作为 never-salvage 足枪工具被吞下）。
//
// 接受弱模型为工具请求产生的每个形状：
//   ·native-ish:{"name":"bash","arguments":{...}}(或"function")
//   ·代理-zero:{"tool_name":"bash","tool_args":{...}}
//   ·松散：{"tool":"bash"，"args":{...}}
//   ·动作后缀：{"tool_name":"x:run"，...}→args.action="run"
// 这镜像了agent-zero的normalize_tool_request：模型
// 仅必须发出SOME可识别的{name，args}对象，而不是确切的本机模式定义。
[[nodiscard]] bool emit_salvaged_tool(StreamCtx& ctx, std::string_view sv) {
    json j;
    try { j = json::parse(sv); } catch (...) { return false; }
    if (!j.is_object()) return false;
    std::string name;
    if (j.contains("name") && j["name"].is_string())
        name = j["name"].get<std::string>();
    else if (j.contains("function") && j["function"].is_string())
        name = j["function"].get<std::string>();
    else if (j.contains("tool_name") && j["tool_name"].is_string())
        name = j["tool_name"].get<std::string>();
    else if (j.contains("tool") && j["tool"].is_string())
        name = j["tool"].get<std::string>();
    else
        return false;

    // `tool_name`可以携带`tool:action`后缀（代理-zero方法分派)。
    // 将其拆分；该操作成为工具读取的参数。
    std::string action_suffix;
    if (auto colon = name.find(':'); colon != std::string::npos
            && colon > 0 && colon + 1 < name.size()) {
        action_suffix = name.substr(colon + 1);
        name = name.substr(0, colon);
    }

    // 遗留别名：该工具被注册为`shell`，但`bash`在
    // 模型训练数据中如此常见，以至于弱本地模型会发出它。
    // 如果广告中的模型说 `bash` 和 `shell`，请规范化名称，
    // 以便回收的 调用 瞄准真正的工具（下游 看到 `shell`）。
    if (name == "bash") {
        for (const auto& t : ctx.known_tools)
            if (t == "shell") { name = "shell"; break; }
    }

    bool known = false;
    for (const auto& t : ctx.known_tools) if (t == name) { known = true; break; }
    if (!known) return false;
    // 内存 JSON仅对于最新用户明确请求的确切操作是安全的。
    // 这保留了主动提供的 -mutation 保护，
    // 同时使 remember/forget 在没有本机工具 调用 的模型上工作。
    if (name == "remember" && !ctx.allow_remember_salvage) return true;
    if (name == "forget" && !ctx.allow_forget_salvage) return true;
    if (name == "wipe_memory" && !ctx.allow_wipe_salvage) return true;
    if (name == "skill") return true;

    // 任何已接受键下的参数；默认为 {}。
    json args_obj = json::object();
    bool have_args = false;
    auto take_args = [&](const char* key) {
        if (have_args || !j.contains(key)) return;
        const auto& a = j[key];
        if (a.is_string()) {
            try { args_obj = json::parse(a.get<std::string>()); have_args = true; }
            catch (const std::exception& e) { util::dbglog("ollama.salvage_args.parse", e.what()); }
            catch (...) { util::dbglog("ollama.salvage_args.parse", "non-std exception"); }
        } else if (a.is_object()) {
            args_obj = a; have_args = true;
        } else if (!a.is_null()) {
            args_obj = a; have_args = true;
        }
    };
    take_args("arguments");
    take_args("tool_args");
    take_args("args");
    take_args("parameters");
    if (!action_suffix.empty() && args_obj.is_object()
            && !args_obj.contains("action"))
        args_obj["action"] = action_suffix;
    // 在工具看到它们之前修复常见的错误参数键（cmd→命令、文件→路径、
    // 查询→模式……）——在泄漏的调用最终被挽救后导致#1weak
    // -model循环原因，但将其所需的参数命名为错误。
    repair_arg_keys(name, args_obj);
    std::string args = args_obj.is_object() || args_obj.is_array()
        ? args_obj.dump() : "{}";

    std::string call_id = "call_salvaged_" + std::to_string(ctx.salvage_seq++);
    ctx.sink(StreamToolUseStart{ToolCallId{call_id}, ToolName{name}});
    ctx.sink(StreamToolUseDelta{ToolCallId{call_id}, args});
    ctx.sink(StreamToolUseEnd{ToolCallId{call_id}});
    ctx.stop_reason = StopReason::ToolUse;
    return true;
}

// 从 JSON 数组 [{...}，{...}] 发出回收的 调用。如果有则返回 true。
[[nodiscard]] bool emit_salvaged_tool_array(StreamCtx& ctx, std::string_view sv) {
    json arr;
    try { arr = json::parse(sv); } catch (...) { return false; }
    if (!arr.is_array()) return false;
    bool any = false;
    for (const auto& item : arr)
        if (item.is_object() && emit_salvaged_tool(ctx, item.dump())) any = true;
    return any;
}

// 尝试将所保存的内容作为工具调用（对象或数组，在剥离包装器之后）进行抢救。
// 如果 调用 是 emitted/consumed.，则返回 true
[[nodiscard]] bool try_salvage_hold(StreamCtx& ctx) {
    std::string_view inner = strip_tool_call_wrappers(ctx.text_hold);
    if (inner.empty()) return false;
    if (inner.front() == '[') return emit_salvaged_tool_array(ctx, inner);
    if (inner.front() == '{') return emit_salvaged_tool(ctx, inner);
    return false;
}

// 将保持的内容刷新为散文（丢弃leaked/garbage工具JSON)
// 并停止保持。一旦我们确定该保留不是可挽救的工具，就会调用 调用。
void flush_text_hold(StreamCtx& ctx) {
    ctx.holding = false;
    if (ctx.text_hold.empty()) return;
    if (hold_is_truncated_tool_json(ctx.text_hold)) {
        AGT_LOG(Model, Warn, "salvage.dropped_truncated",
                "dialect=ollama bytes={} head={}", ctx.text_hold.size(),
                std::string_view{ctx.text_hold}.substr(0, 200));
        ctx.text_hold.clear(); return;
    }
    if (hold_is_unknown_tool_call(ctx.text_hold, ctx.known_tools)) {
        AGT_LOG(Model, Warn, "salvage.dropped_unknown_tool",
                "dialect=ollama bytes={} head={}", ctx.text_hold.size(),
                std::string_view{ctx.text_hold}.substr(0, 200));
        ctx.text_hold.clear(); return;
    }
    // Whitespace-only 保留不是真正的回复——将其留给 never
    // -blank-turn 网络到 渲染 一个显式的 `(empty response)`，
    // 而不是发出一个空白-looking 文本增量。
    if (ctx.text_hold.find_first_not_of(" \t\r\n") == std::string::npos) {
        ctx.text_hold.clear(); return;
    }
    ctx.sink(StreamTextDelta{ctx.text_hold});
    ctx.any_text_flushed = true;
    ctx.text_hold.clear();
}

// Last-resort:弱模型叙述散文AND将泄露工具调用埋藏在
// 回复中途出现的 ```…``` 围栏块，因此此前未在开头暂存。此时
// 终端关闭，如果NOTHING被抢救或结构化，
// 则扫描full_content以查找其正文是广告工具
// -call对象的FIRST围栏块并抢救它。保守：仅在没有其他 调用 发生时触发，
// 仅在围栏块上，仅针对广告工具。如果 调用 被触发，则返回 true。
bool rescue_tool_from_prose(StreamCtx& ctx) {
    if (ctx.any_structured_tool) return false;
    if (ctx.stop_reason == StopReason::ToolUse) return false;  // 已打捞
    std::string_view sv{ctx.full_content};
    std::size_t pos = 0;
    while ((pos = sv.find("```", pos)) != std::string_view::npos) {
        std::size_t open_end = sv.find('\n', pos + 3);
        if (open_end == std::string_view::npos) break;
        std::size_t close = sv.find("```", open_end + 1);
        if (close == std::string_view::npos) break;
        std::string_view body = sv.substr(open_end + 1, close - open_end - 1);
        // 修剪
        while (!body.empty() && (body.front()==' '||body.front()=='\t'
               ||body.front()=='\n'||body.front()=='\r')) body.remove_prefix(1);
        while (!body.empty() && (body.back()==' '||body.back()=='\t'
               ||body.back()=='\n'||body.back()=='\r')) body.remove_suffix(1);
        if (!body.empty() && (body.front()=='{' || body.front()=='[')) {
            bool ok = body.front()=='['
                ? emit_salvaged_tool_array(ctx, body)
                : emit_salvaged_tool(ctx, body);
            if (ok) return true;
        }
        pos = close + 3;
    }
    return false;
}

// 在 `sv` 中查找第一个 BALANCED top-level JSON 对象子字符串，
// 遵循字符串文字 + 转义（因此引用值内的 `}` 不会提前关闭对象）。返回[第一个
// '{' .. 匹配 '}'] 范围，如果不存在平衡对象则返回空。这是agent
// -zero的extract_json_root_string：JSON
// -protocol模式下的弱模型将他们的`{tool_name,tool_args}`
// 对象包装在杂散叙述中（"Sure! {"tool_name":...}  Let me know!"）；
// 将平衡物体拉出可使 调用 免受噪音影响。
[[nodiscard]] std::string_view extract_first_json_object(std::string_view sv) noexcept {
    std::size_t start = sv.find('{');
    if (start == std::string_view::npos) return {};
    int depth = 0;
    bool in_str = false, esc = false;
    for (std::size_t i = start; i < sv.size(); ++i) {
        char c = sv[i];
        if (in_str) {
            if (esc)            esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"')      in_str = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return sv.substr(start, i - start + 1);
        }
    }
    return {};  // 从未平衡（传输 削减中期-object）
}

// JSON-protocol 终端救援（极弱型号，特工-zero款式）。
// 整个响应应该是 一个 {thoughts, tool_name, tool_args} 对象。
// 将第一个平衡物体从full_content中拉出并尝试将其作为工具调用发射。
// 如果它带有 `thoughts`/`headline` 字段但没有可用的工具，
// 请将其显示为散文，以便用户仍然可以看到模型的推理。如果工具 调用 被触发，
// 则返回 true。
bool rescue_json_protocol(StreamCtx& ctx) {
    if (!ctx.json_protocol) return false;
    if (ctx.any_structured_tool) return false;
    if (ctx.stop_reason == StopReason::ToolUse) return false;
    std::string_view obj = extract_first_json_object(ctx.full_content);
    if (obj.empty()) return false;
    json j;
    try { j = json::parse(obj); } catch (...) { return false; }
    if (!j.is_object()) return false;

    // Plain-chat逃生舱口：tool_name=="response"意味着
    // 模型选择说话，而不是行动。将 tool_args 和 数据流 中的文本展开为散文。
    // (Grammar-forced JSON否则会向用户转储原始对象。)
    auto name_of = [&](const char* k) -> std::string {
        return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>()
                                                   : std::string{};
    };
    std::string tname = name_of("tool_name");
    if (tname.empty()) tname = name_of("tool");
    if (tname.empty()) tname = name_of("name");
    if (tname == "response") {
        std::string text;
        const json* args = nullptr;
        if (j.contains("tool_args") && j["tool_args"].is_object()) args = &j["tool_args"];
        else if (j.contains("args") && j["args"].is_object())      args = &j["args"];
        if (args) {
            for (const char* k : {"text", "response", "content", "message", "answer"}) {
                if (args->contains(k) && (*args)[k].is_string()) {
                    text = (*args)[k].get<std::string>();
                    break;
                }
            }
        }
        // 回退到顶部-level键，某些型号使用而不是嵌套。
        if (text.empty()) {
            for (const char* k : {"response", "content", "message", "answer"}) {
                if (j.contains(k) && j[k].is_string()) { text = j[k].get<std::string>(); break; }
            }
        }
        // 最后的手段：模型选择`response`，但将文本留空，
        // 并将其实际单词放入`thoughts`（与小型模型上的语法
        // -forced输出常见）。将想法表达为散文——用户必须看到 SOMETHING，
        // 而不是空白（Aider 的纪律：永远不要放弃非
        // -empty 模型回复）。
        if (text.empty() && j.contains("thoughts")) {
            const auto& th = j["thoughts"];
            if (th.is_array()) {
                for (const auto& t : th)
                    if (t.is_string()) {
                        if (!text.empty()) text += '\n';
                        text += t.get<std::string>();
                    }
            } else if (th.is_string()) {
                text = th.get<std::string>();
            }
        }
        if (!text.empty()) {
            ctx.sink(StreamTextDelta{text});
            ctx.any_text_flushed = true;
            ctx.stop_reason = StopReason::EndTurn;
        }
        // 无论哪种方式都消耗所持有的缓冲区：该对象是整个回复，
        // 因此它也必须由调用者将NOT刷新为原始-JSON散文。
        ctx.text_hold.clear();
        ctx.holding = false;
        return false;  // 不是工具调用;让 终端 继续冲洗
    }

    // 工具请求？发射它。
    const bool has_tool = !tname.empty();
    if (has_tool && emit_salvaged_tool(ctx, obj)) return true;
    return false;
}

// 表面吞噬推理文本：当用户展示推理时可见StreamThinkingDelta，
// 否则为活跃性-only StreamHeartbeat（无隐藏捕获——参见
// StreamCtx::show_reasoning）。
void emit_reasoning(StreamCtx& ctx, const std::string& text) {
    if (text.empty()) return;
    if (ctx.show_reasoning) ctx.sink(StreamThinkingDelta{text, {}});
    else                    ctx.sink(StreamHeartbeat{});
}

// 最终安全网-NEVER让助手将渲染变成空白（助手的
// `if not received_content`纪律）。在 salvage/flush.
// 之后的每个 终端 路径上调用如果轮流产生 NO 文本和 NO 工具 调用，
// 则显示模型实际发送的任何内容，以便用户始终看到回复：
//   ·JSON-protocol对象→其`response`/`text`/`thoughts` 载荷，
//     剥离协议脚手架（extract_first_json_object
//     +同野外行走救援用途）；
//   ·否则原始捕获的内容，逐字；
//   ·如果即使是空的，则清晰的`(empty response)`标记（Aider的
//     精确的占位符），因此回合不是无声的空虚。
void flush_unhandled_content(StreamCtx& ctx) {
    // 从未完成的部分<think>-tag保留是文字内容——
    // 归并它返回到下面的salvage/flush决定之前。 （done-frame
    // 路径急切地刷新它；这覆盖了 `done` 之前的 传输 切割。）
    if (!ctx.think_carry.empty()) {
        std::string tail = std::move(ctx.think_carry);
        ctx.think_carry.clear();
        if (ctx.in_think)      emit_reasoning(ctx, tail);
        else if (ctx.holding)  ctx.text_hold += tail;
        else                   ctx.full_content += tail;
    }
    if (ctx.any_text_flushed) return;
    if (ctx.any_structured_tool) return;
    if (ctx.stop_reason == StopReason::ToolUse) return;

    auto emit = [&](std::string s) {
        // 修剪 leading/trailing 空白，这样单独的换行符就不算数。
        std::size_t a = 0, b = s.size();
        while (a < b && (s[a]==' '||s[a]=='\t'||s[a]=='\n'||s[a]=='\r')) ++a;
        while (b > a && (s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\n'||s[b-1]=='\r')) --b;
        s = s.substr(a, b - a);
        if (s.empty()) return false;
        ctx.sink(StreamTextDelta{s});
        ctx.any_text_flushed = true;
        if (ctx.stop_reason != StopReason::ToolUse)
            ctx.stop_reason = StopReason::EndTurn;
        return true;
    };

    // 首先尝试从 JSON-protocol 对象中提取人工回复，以便用户看到文字，
    // 而不是 {tool_name:response,...} 脚手架。
    std::string_view obj = extract_first_json_object(ctx.full_content);
    if (!obj.empty()) {
        json j;
        try { j = json::parse(obj); } catch (...) { j = json{}; }
        if (j.is_object()) {
            std::string text;
            const json* args = nullptr;
            if (j.contains("tool_args") && j["tool_args"].is_object()) args = &j["tool_args"];
            else if (j.contains("args") && j["args"].is_object())      args = &j["args"];
            if (args)
                for (const char* k : {"text","response","content","message","answer"})
                    if (args->contains(k) && (*args)[k].is_string())
                        { text = (*args)[k].get<std::string>(); break; }
            if (text.empty())
                for (const char* k : {"response","content","message","answer","text"})
                    if (j.contains(k) && j[k].is_string())
                        { text = j[k].get<std::string>(); break; }
            if (text.empty() && j.contains("thoughts")) {
                const auto& th = j["thoughts"];
                if (th.is_array()) {
                    for (const auto& t : th) if (t.is_string()) {
                        if (!text.empty()) text += '\n';
                        text += t.get<std::string>();
                    }
                } else if (th.is_string()) text = th.get<std::string>();
            }
            if (emit(std::move(text))) return;
        }
    }

    // 没有JSON 载荷（或者它是空的）——逐字显示原始内容，
    // UNLESS是被故意吞下的工具-call-shaped对象（像
    // `remember`这样的枪式工具、未广告的工具或被截断的
    // 工具JSON）。暴露原始数据会向用户泄漏
    // `{"name":"remember",…}`——这正是回收路径要隐藏的垃圾。
    std::string_view rc{ctx.full_content};
    const bool rc_blank = rc.find_first_not_of(" \t\r\n") == std::string_view::npos;
    const bool swallowed_tool_json =
        !rc_blank && (hold_is_truncated_tool_json(rc) ||
                      hold_is_unknown_tool_call(rc, ctx.known_tools) ||
                      could_be_tool_json(rc));
    if (!swallowed_tool_json && emit(ctx.full_content)) return;

    // Deliberately-swallowed工具JSON：该轮次设计没有
    // user-facing文本——保持沉默，不显示占位符。
    if (swallowed_tool_json) return;

    // 事实上什么也没返回。 Mirror Aider 的占位符让转弯永远
    // 不会是一片寂静的空白。
    ctx.sink(StreamTextDelta{"(empty response)"});
    ctx.any_text_flushed = true;
    if (ctx.stop_reason != StopReason::ToolUse)
        ctx.stop_reason = StopReason::EndTurn;
}

// 从流式内容块中过滤推理-model <think>…</think>，
// 跟踪ctx中跨帧的open/close状态。仅返回可见（非​​-think）文本。
// 本机 `thinking` 字段是干净路径；这是 回退，适用于将 CoT
// 内联到 `content` 的型号。我们还接受 <thinking>
// …</thinking>（某些模板使用长格式）。
[[nodiscard]] std::string filter_think(StreamCtx& ctx, std::string_view chunk) {
    // Re-prepend来自前一个块的任何hold-back部分标签——
    // 标签跨token-sized NDJSON 帧 ("<thi" +
    // "nk>")分割，并且普通的per-chunk find()会将片段泄漏为
    // 可见散文(或在分割关闭标签上吞下答案)。
    std::string joined;
    std::string_view s = chunk;
    if (!ctx.think_carry.empty()) {
        joined = std::move(ctx.think_carry);
        ctx.think_carry.clear();
        joined.append(chunk);
        s = joined;
    }
    std::string out;
    out.reserve(s.size());
    // 推理从 <think>…</think> 这个块内部被吞噬——通过
    // StreamThinkingDelta 浮出水面（相同 统一了 事件
    // 本机 `thinking` 字段和所有其他提供程序使用）而不是被丢弃，
    // 因此 inline-CoT 本地模型 渲染 推理也在共享块中。
    std::string reasoning;
    // 最长的块-final后缀是open/close标签的PROPER前缀—
    // —被保留到think_carry中而不是发出。 npos = 无。
    auto partial_tag_at = [](std::string_view text, bool closing) {
        const std::size_t max_tag = closing ? 11 : 10;  // </thinking>/<thinking>
        const std::size_t lo = text.size() > max_tag ? text.size() - max_tag + 1 : 0;
        for (std::size_t p = lo; p < text.size(); ++p) {
            if (text[p] != '<') continue;
            std::string_view suf = text.substr(p);
            auto is_pref = [&](std::string_view tag) {
                return suf.size() < tag.size()
                    && tag.substr(0, suf.size()) == suf;
            };
            if (closing ? (is_pref("</think>") || is_pref("</thinking>"))
                        : (is_pref("<think>")  || is_pref("<thinking>")))
                return p;
        }
        return std::string_view::npos;
    };
    std::size_t i = 0;
    while (i < s.size()) {
        if (ctx.in_think) {
            // 寻找结束标签。
            std::size_t close = s.find("</think>", i);
            std::size_t close2 = s.find("</thinking>", i);
            std::size_t c = std::min(close, close2);
            if (c == std::string_view::npos) {
                // 仍然在思考——剩下的就是推理，
                // 减去为下一个块保留的可能的部分结束标签。
                std::string_view rem = s.substr(i);
                if (auto p = partial_tag_at(rem, /*关闭=*/true);
                    p != std::string_view::npos) {
                    ctx.think_carry.assign(rem.substr(p));
                    rem = rem.substr(0, p);
                }
                reasoning.append(rem);
                emit_reasoning(ctx, reasoning);
                return out;
            }
            reasoning.append(s.substr(i, c - i));
            ctx.in_think = false;
            i = c + (c == close ? 8 : 11);
        } else {
            std::size_t open = s.find("<think>", i);
            std::size_t open2 = s.find("<thinking>", i);
            std::size_t o = std::min(open, open2);
            if (o == std::string_view::npos) {
                // 无开放标签——剩余部分是散文，
                // 减去为下一个块保留的可能的部分开放标签。
                std::string_view rem = s.substr(i);
                if (auto p = partial_tag_at(rem, /*关闭=*/false);
                    p != std::string_view::npos) {
                    ctx.think_carry.assign(rem.substr(p));
                    rem = rem.substr(0, p);
                }
                out.append(rem);
                break;
            }
            out.append(s.substr(i, o - i));
            ctx.in_think   = true;
            ctx.think_seen = true;
            i = o + (o == open ? 7 : 10);
        }
    }
    emit_reasoning(ctx, reasoning);
    return out;
}

// JSON-protocol聊天回复的渐进`response` 流式传输。当保持 缓冲区
// 累积语法-forced {"tool_name":"response",
// "tool_args":{"text":"…"}}对象 char-by-char 时，
// 它会解码并发出回复文本 INCREMENTALLY，因此 maya 的 reveal_fx
// 会像普通文本 数据流 一样对其进行动画处理 - 而不是旧的行为（保持整个对象，
// 在 `done` 处发出一个巨大的 StreamTextDelta，这使得打字机没有任何动画效果，
// 并强制执行 one-frame 冻结切换，从而在 回滚缓冲区 中复制了 正文）。
//
// ctx.text_hold 上的状态机（仅增长）：
//   1.尚未激活：扫描`"tool_name"`:`"response"`。直到我们SEE
//      我们无法提交 tool_name 值 — 该模型可能会发出真实的工具 调用。
//      如果我们确实看到非 "response" tool_name，
//      则放弃（永远返回 false）并让正常的抢救路径处理它。
//   2.活动：定位回复字段的开头引用（文本|响应|内容|
//      message|answer)，然后 数据流 解码该字符串值的字符，
//      直到其结束（未转义）引用，每个块发出 StreamTextDelta。
//
// 一旦获取了响应的所有权（活动的），则返回真，
// 因此调用者停止将text_hold馈送至could_be_tool_json/刷新。保守：
// 在值字符串实际打开之前不发出任何信号，并在结束引号处
// 干净地停止（忽略尾随对象 字节 `"}}`）。
bool try_progressive_response(StreamCtx& ctx) {
    if (ctx.resp_done) return true;
    std::string_view hold{ctx.text_hold};

    // ── 第一阶段:确认 tool_name == "response" ───────────────────
    if (!ctx.resp_active) {
        // 找到tool_name键。接受 "tool_name" / "tool" / "name"。
        // 成功时，resp_scan_pos被设置为刚好经过tool_name 值，
        // 因此阶段2的回复-key搜索绝不会越过作为tool_name值本身出现的文字
        // "response"。
        auto find_key_val = [&](std::string_view key) -> int {
            // 返回：1==响应，0==其他工具（中止），-1==未知
            std::size_t k = hold.find(key);
            if (k == std::string_view::npos) return -1;
            std::size_t p = k + key.size();
            // 跳过ws+':'+ws
            while (p < hold.size() && (hold[p]==' '||hold[p]=='\t'
                   ||hold[p]=='\n'||hold[p]=='\r')) ++p;
            if (p >= hold.size()) return -1;
            if (hold[p] != ':') return -1;   // 不是我们想要的key:value
            ++p;
            while (p < hold.size() && (hold[p]==' '||hold[p]=='\t'
                   ||hold[p]=='\n'||hold[p]=='\r')) ++p;
            if (p >= hold.size()) return -1;
            if (hold[p] != '"') return -1;   // 值串尚未打开
            std::size_t vs = p + 1;
            std::size_t ve = hold.find('"', vs);
            if (ve == std::string_view::npos) return -1;  // 值不完整
            std::string_view val = hold.substr(vs, ve - vs);
            ctx.resp_scan_pos = ve + 1;  // 刚刚过去的收盘价报价
            return val == "response" ? 1 : 0;
        };
        int r = find_key_val("\"tool_name\"");
        if (r == -1) r = find_key_val("\"tool\"");
        if (r == -1) r = find_key_val("\"name\"");
        if (r == 0)  { ctx.resp_done = true; return false; }  // 真实工具调用
        if (r != 1)  return false;                            // 未定
        ctx.resp_active = true;
    }

    // ── 阶段2: 定位回复值字符串的开口引号 ──────────
    // 仅搜索AFTER和tool_name值(resp_scan_pos)，
    // 因此作为tool_name值的文字"response"/"name"不会被误
    // 认为是回复键。
    if (!ctx.resp_in_value) {
        std::string_view tail = ctx.resp_scan_pos < hold.size()
            ? hold.substr(ctx.resp_scan_pos) : std::string_view{};
        std::size_t best = std::string_view::npos;
        for (std::string_view key : {"\"text\"", "\"response\"", "\"content\"",
                                     "\"message\"", "\"answer\""}) {
            std::size_t k = tail.find(key);
            if (k == std::string_view::npos) continue;
            std::size_t p = k + key.size();
            while (p < tail.size() && (tail[p]==' '||tail[p]=='\t'
                   ||tail[p]=='\n'||tail[p]=='\r')) ++p;
            if (p >= tail.size() || tail[p] != ':') continue;
            ++p;
            while (p < tail.size() && (tail[p]==' '||tail[p]=='\t'
                   ||tail[p]=='\n'||tail[p]=='\r')) ++p;
            if (p >= tail.size() || tail[p] != '"') continue;  // 尚未开放
            if (k < best) {
                best = k;
                ctx.resp_value_pos = ctx.resp_scan_pos + p + 1;
            }
        }
        if (best == std::string_view::npos) return true;  // 有效，值待处理
        ctx.resp_in_value = true;
    }

    // ── 阶段3:解码+新发射 -arrived 价值 字节 ────────────────
    // 从 resp_value_pos 开始，尊重 JSON 字符串转义；
    // 停在第一个未转义的收盘报价处。 缓冲区 解码输出并在每个 调用（每个
    // 传输 帧）一个 StreamTextDelta 中发出它，
    // 因此 逐步显示 光标平滑前进。
    std::string out;
    std::size_t i = ctx.resp_value_pos;
    bool closed = false;
    while (i < hold.size()) {
        char c = hold[i];
        if (ctx.resp_esc) {
            ctx.resp_esc = false;
            switch (c) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case '/':  out += '/';  break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                case 'u': {
                    // \uXXXX — 需要另外 4 个可用的十六进制数字。
                    // 如果还没有全部完成，请保留 resp_esc 设置并停止；
                    // 一旦数字到达，就恢复下一个 帧。光标停留在 'u' 上方。
                    if (i + 4 >= hold.size()) {
                        ctx.resp_esc = true;
                        ctx.resp_value_pos = i;  // re-read 'u' (和数字)下一个 帧
                        // 我们在-escape中期：i点AT 'u'。坚持并保释。
                        goto done_decode;
                    }
                    auto hexval = [](char h) -> int {
                        if (h>='0'&&h<='9') return h-'0';
                        if (h>='a'&&h<='f') return h-'a'+10;
                        if (h>='A'&&h<='F') return h-'A'+10;
                        return -1;
                    };
                    int h0=hexval(hold[i+1]),h1=hexval(hold[i+2]),
                        h2=hexval(hold[i+3]),h3=hexval(hold[i+4]);
                    if (h0<0||h1<0||h2<0||h3<0) { out += 'u'; break; }
                    unsigned cp = (h0<<12)|(h1<<8)|(h2<<4)|h3;
                    // 将BMP代码点编码为UTF-8(代理对
                    // 在聊天回复中很少见；单独的代理通过作为
                    // 替换-ish 字节，对于显示无害)。
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    i += 4;
                    break;
                }
                default:   out += c;    break;
            }
            ++i;
            continue;
        }
        if (c == '\\') {
            // 逃生引线。如果是 最后 字节，请在此处停止，
            // 将光标 AT 保留为反斜杠，以便转义完成下一个 帧。
            if (i + 1 >= hold.size()) { ctx.resp_value_pos = i; goto done_decode; }
            ctx.resp_esc = true;
            ++i;
            continue;
        }
        if (c == '"') { closed = true; ++i; break; }
        out += c;
        ++i;
    }
    // 光标位于最后一个完全-decoded 字节（或收盘价）之后。
    ctx.resp_value_pos = i;
done_decode:
    if (!out.empty()) {
        ctx.resp_emitted += out;
        ctx.sink(StreamTextDelta{out});
        ctx.any_text_flushed = true;
    }
    if (closed) {
        ctx.resp_done       = true;
        ctx.holding         = false;
        ctx.salvage_eligible = false;
        ctx.stop_reason     = StopReason::EndTurn;
        ctx.text_hold.clear();
    }
    return true;
}

// 处理来自 NDJSON 帧 的一个本机 `message` 对象。
void handle_message(StreamCtx& ctx, const json& message) {
    // 本机推理：当我们发送 think:true 时，Ollama 将模型的 chain
    // -of-thought 路由到结构化的 `thinking` 字段（流式内容）。
    // 将其发出为 StreamThinkingDelta — 相同 统一 事件、
    // Anthropic (thinking_delta)、OpenAI-compat
    // (reasoning_content) 和 ChatGPT/Codex (
    // reasoning_summary) 传输层 发出 — 因此本地推理 渲染 与所有
    // 其他提供者位于同一块中。此 传输 上没有签名。在长时间推理暂停期间兼作活跃心跳。
    if (auto it = message.find("thinking");
        it != message.end() && it->is_string()) {
        const auto& think = it->get_ref<const std::string&>();
        emit_reasoning(ctx, think);
    }
    // 结构化工具调用。 Ollama 在一个 帧 中完全返回它们-formed（不是流式
    // 传输的 char-by-char），因此连续发出 Start+Delta+End。
    if (message.contains("tool_calls") && message["tool_calls"].is_array()
        && !message["tool_calls"].empty()) {
        int idx = 0;
        for (const auto& tc : message["tool_calls"]) {
            std::string name, args = "{}";
            std::string call_id;
            if (tc.contains("id") && tc["id"].is_string())
                call_id = tc["id"].get<std::string>();
            if (tc.contains("function") && tc["function"].is_object()) {
                const auto& fn = tc["function"];
                if (fn.contains("name") && fn["name"].is_string())
                    name = fn["name"].get<std::string>();
                if (fn.contains("arguments")) {
                    const auto& a = fn["arguments"];
                    if (a.is_string())     args = a.get<std::string>();
                    else if (!a.is_null()) args = a.dump();
                }
            }
            if (name.empty()) continue;
            if (call_id.empty())
                call_id = "call_ollama_" + std::to_string(ctx.tool_seq++)
                        + "_" + std::to_string(idx);
            ++idx;
            // 即使在 NATIVE 通道上也修复常见的错误参数键 —
            // 获得结构化路径的弱模型仍然可以将 `command`
            // 命名为 `cmd` 等。仅在规范键不存在时才重新映射。
            if (!args.empty() && args != "{}") {
                try {
                    json aj = json::parse(args);
                    if (aj.is_object()) {
                        repair_arg_keys(name, aj);
                        args = aj.dump();
                    }
                } catch (...) { /* 在 解析 失败时将参数保留为 -is */ }
            }
            ctx.sink(StreamToolUseStart{ToolCallId{call_id}, ToolName{name}});
            ctx.sink(StreamToolUseDelta{ToolCallId{call_id}, args});
            ctx.sink(StreamToolUseEnd{ToolCallId{call_id}});
            ctx.stop_reason = StopReason::ToolUse;
            ctx.any_structured_tool = true;
            ctx.salvage_eligible    = false;  // 模型驱动真实渠道
        }
        // 真正结构化的调用到来——任何持有的内容都是在它之前
        // 的真实散文；现在冲洗它。
        if (ctx.holding) flush_text_hold(ctx);
    }

    // 助理散文。保留-and-classified，而抢救仍然符合条件，
    // 因此可以恢复泄漏的工具-call JSON，而不是转储为文本。
    if (message.contains("content") && message["content"].is_string()) {
        const auto& raw = message["content"].get_ref<const std::string&>();
        if (raw.empty()) return;
        // 在到达 hold/classify 路径或用户之前，剥离任何内联 <think>
        // …</think> 推理（deepseek-r1、qwen3，
        // 当未通过本机 `thinking` 字段驱动时）。 Ollama 的本机
        // `thinking` 字段（message.thinking）单独处理，从不输入内容。
        std::string s = filter_think(ctx, raw);
        if (s.empty()) return;  // 整个块都在思考块内
        ctx.full_content += s;
        // Progressive `response` 已经直播+关闭了本回合的回复：
        // 任何进一步的内容帧只是对象的尾部结构(`"}}`/pretty
        // -print空白)。吞掉它们——将它们作为文本发送，
        // 将 ` }` 附加到回复上（尾随 -brace 泄漏）。
        if (ctx.resp_active && ctx.resp_done) return;
        if (!ctx.holding) {
            ctx.sink(StreamTextDelta{s});
            ctx.any_text_flushed = true;
            return;
        }
        ctx.text_hold += s;
        // JSON-protocol聊天回复？ 数据流 和 `response`
        // 文本在到达时递增，因此 reveal_fx 会对其进行动画处理（并且冻结
        // 切换是渐进的，而不是重复 正文 的单个 -frame 转储）。
        // 一旦取得 所有权，它就拥有剩余的所有权；保释出来，这样 字节 就不会像下面的
        // 散文那样 ALSO classified/flushed 了。
        if (ctx.json_protocol && !ctx.any_structured_tool) {
            if (try_progressive_response(ctx)) return;
        }
        // 还可能是工具调用吗？继续持有，直到 `done` 做出决定。
        if (ctx.salvage_eligible && could_be_tool_json(ctx.text_hold)) return;
        // 决定它是散文——发布 缓冲区 和 数据流 其余部分。
        flush_text_hold(ctx);
    }
}

void dispatch_line(StreamCtx& ctx, std::string_view line) {
    if (line.empty()) return;
    json j;
    try { j = json::parse(line); } catch (...) { return; }

    if (j.contains("error")) {
        std::string msg = j["error"].is_string()
            ? j["error"].get<std::string>() : j["error"].dump();
        ctx.sink(StreamError{msg, std::nullopt});
        ctx.terminated = true;
        return;
    }
    if (j.contains("message") && j["message"].is_object())
        handle_message(ctx, j["message"]);

    if (j.value("done", false)) {
        // 刷新任何持有的-back部分<think>-tag
        // 字节——标签从未完成，因此它们是文字内容：如果我们
        // 仍在思考块内则推理，否则散文。
        if (!ctx.think_carry.empty()) {
            std::string tail = std::move(ctx.think_carry);
            ctx.think_carry.clear();
            if (ctx.in_think) {
                emit_reasoning(ctx, tail);
            } else if (ctx.holding) {
                ctx.text_hold += tail;
            } else if (!(ctx.resp_active && ctx.resp_done)) {
                ctx.sink(StreamTextDelta{tail});
                ctx.any_text_flushed = true;
            }
        }
        // 如果渐进式-response流媒体已经获取了本轮回复的所有权（
        // json_protocol聊天回复增量地流式传输），则文本已经被发出。
        // 不要让 rescue_json_protocol re-emit
        // 它（即 -streamed 和 正文 的两倍 — 重复错误）。
        // 如果值字符串从未关闭（传输 切到 -reply 中间），则刷新解码后
        // 的 字节 和 待处理 并完成。
        if (ctx.resp_active) {
            if (!ctx.resp_done) {
                // 排出任何剩余的可解码字节（未看到结束报价）。
                try_progressive_response(ctx);
                ctx.resp_done = true;
                ctx.holding   = false;
                ctx.text_hold.clear();
                if (ctx.stop_reason != StopReason::ToolUse)
                    ctx.stop_reason = StopReason::EndTurn;
            }
            if (auto su = usage::from_ollama(j)) ctx.sink(*su);
            flush_unhandled_content(ctx);
            return;
        }
        // JSON-protocol (非常弱的模型）：ENTIRE回复
        // 意味着是一个{想法，tool_name，tool_args}对象。
        // 尝试从完整内容 首次 中提取工具 调用（在将其作为散文冲刷之前），
        // 这样像 "Sure, I'll do that:" 这样
        // 的主导旁白就不会在动作之前发出，并且在视觉上与动作竞争。
        bool jp_fired = false;
        if (ctx.json_protocol) {
            jp_fired = rescue_json_protocol(ctx);
            if (jp_fired) { ctx.text_hold.clear(); ctx.holding = false; }
        }
        // 对任何 still-held 内容的最终决定：尝试将其作为泄露的
        // 工具 调用 进行抢救，否则作为散文刷新（丢弃垃圾 JSON）。
        if (!jp_fired && ctx.holding && !ctx.text_hold.empty()) {
            if (!try_salvage_hold(ctx)) flush_text_hold(ctx);
            else { ctx.text_hold.clear(); ctx.holding = false; }
        } else if (ctx.holding) {
            ctx.holding = false;
        }
        // 正文中间埋藏的工具调用，包括叙述文字与 ```fenced``` JSON。
        rescue_tool_from_prose(ctx);
        // Never-blank-turn网络：如果没有发出任何内容，则显示模型的内容
        // （或显式的`(empty response)`）。
        flush_unhandled_content(ctx);
        if (auto su = usage::from_ollama(j)) ctx.sink(*su);
        auto reason = j.value("done_reason", std::string{"stop"});
        if (ctx.stop_reason != StopReason::ToolUse)
            ctx.stop_reason = (reason == "length") ? StopReason::MaxTokens
                                                   : StopReason::EndTurn;
    }
}

void feed_ndjson(StreamCtx& ctx, const char* data, std::size_t len) {
    ctx.framer.feed(data, len, [&](std::string_view line) {
        dispatch_line(ctx, line);
    });
}

// ── CLAUDE.md 内存 等级( 用户) -authored ,简洁) ─────────────────────────
// AGENTS.md (AAIF标准，项目-scoped)通过
// wire::agents_md_block前置，因此标准化公共项目指南出现在个人
// CLAUDE.md层的BEFORE中——与Anthropic 提示词使用的排序相同。
// Empty/missing AGENTS.md 删除该块。
std::string memory_blocks() {
    return wire::agents_md_block(
               "Project guidance following the open AGENTS.md standard "
               "(agents.md, stewarded by the Agentic AI Foundation under "
               "the Linux Foundation). Treat as authoritative public "
               "project conventions.",
               tools::util::workspace_root(),
               tools::util::project_root(),
               wire::resolve_global_agents_md())
         + wire::claude_md_blocks(
               "Project-specific guidance the user has authored. Treat these as "
               "persistent context for THIS workspace and user.");
}

} // 命名空间结束。

// ── 信件阵列( 内在形状) ───────────────────────────────────────────
// Ollama想要工具参数作为JSON OBJECT(不是序列化字符串)并且工具
// 结果作为角色："tool"和`tool_name`。
//
// `json_protocol`：当 true 时，模型 绝不 显示了一个本机 `tools`
// 数组 - 它被教导（json_protocol_addendum）发出
// `{tool_name,tool_args}` JSON 对象和
// "wait for its result in the next message." 以
// NATIVE 形状（assistant.tool_calls[] + 角色："tool"）反馈历史记录
// 打破了这个契约：一个只知道散文-JSON的小模型协议不知道角色："tool" 消息是什么，
// 因此在 multi-step 任务中它会丢失 线程，重新 -issues 它已经运行的 调用，或者产生幻觉结果。
// 在 JSON-protocol 模式中，我们因此将 渲染 模型的 自身 教授
// 词汇中的 round-trip：助手转弯变成它发出的字面{想法？，
// tool_name，tool_args}对象，并且结果作为简单的 USER
// 消息（"TOOL RESULT (name): …"）返回。现在，
// 循环完全按照系统 提示词 所承诺的形状闭合。
json build_messages(const std::vector<Message>& msgs, bool json_protocol) {
    json arr = json::array();
    // 共享age-tiered 传输预算的新近度排名（0=最新工具结果）。
    // 过期 成功的转储逐渐变得紧张+末尾，因此从许多 调用 之前读取的大量内容
    // 停止完整重播 - 这里具有双重价值，其中本地模型具有 SMALLEST
    // 上下文窗口。 JSON-protocol "TOOL RESULT (name):"
    // 用户轮次和本机角色："tool" 轮次共享相同的计数器。
    // 参见 wire::cap_tool_result_aged。
    int total_tool_results = 0;
    for (const auto& m : msgs)
        if (is_assistant_with_results(m))
            total_tool_results += static_cast<int>(m.tool_calls.size());
    int tool_results_emitted = 0;
    const auto superseded = wire::superseded_read_ids(msgs);
    // THIS请求的Per-side图像上限(参见wire::wire_image_count)：
    // Many-image规则对整个请求中的块进行计数，
    // 因此上限在这里导出一次而不是针对每个消息导出。
    const unsigned max_side = util::wire_max_side(wire::wire_image_count(msgs));
    for (const auto& m : msgs) {
        const bool has_text  = !m.text.empty();
        const bool has_tools = is_assistant_with_results(m);
        const bool has_images = wire::has_wire_message_image(m, max_side);

        // JSON 协议往返：助手工具调用渲染为
        //    模型自身的{tool_name，tool_args}对象，作为用户转动的结果。
        if (json_protocol && has_tools) {
            // 助手与 调用 一起发出的散文（在 JP 模式中很少见，
            // 但保留它，这样推理就不会丢失）。
            if (has_text) {
                arr.push_back({{"role", "assistant"},
                               {"content", scrub_utf8(m.text)}});
            }
            for (const auto& tc : m.tool_calls) {
                const int recency_rank = total_tool_results - 1 - tool_results_emitted;
                ++tool_results_emitted;
                json obj;
                obj["tool_name"] = tc.name.value;
                obj["tool_args"] = tc.args.is_null() ? json::object() : tc.args;
                // 回显调用作为助理的散文-JSON回复。
                arr.push_back({{"role", "assistant"},
                               {"content", scrub_utf8(obj.dump())}});
                // 结果作为USER轮，模型被教导接下来读取。
                std::string out = tc.output();
                bool is_error = !tc.is_terminal() || tc.is_failed() || tc.is_rejected();
                if (out.empty()) {
                    if (tc.is_rejected())       out = "(rejected by user)";
                    else if (!tc.is_terminal()) out = "(no output)";
                } else if (!is_error && superseded.count(tc.id.value)) {
                    out = std::string{wire::kSupersededReadPointer};
                } else {
                    out = wire::cap_tool_result_aged(out, recency_rank, is_error);
                }
                std::string body = "TOOL RESULT (" + tc.name.value + "):\n" + out;
                arr.push_back({{"role", "user"},
                               {"content", scrub_utf8(body)}});
            }
            continue;   // 已完全处理该消息
        }

        if (has_text || has_images || has_tools) {
            json msg;
            msg["role"] = (m.role == Role::User) ? "user" : "assistant";
            std::string wire_text = m.attachments.empty()
                ? m.text : attachment::expand(m.text, m.attachments);
            msg["content"] = scrub_utf8(wire_text);
            if (has_images) {
                json imgs = json::array();
                for (const auto* imgp : wire::wire_message_images(m, max_side))
                    imgs.push_back(imgp->base64());
                if (!imgs.empty()) msg["images"] = std::move(imgs);
            }
            if (has_tools) {
                json calls = json::array();
                for (const auto& tc : m.tool_calls) {
                    calls.push_back({
                        {"id", tc.id.value},
                        {"function", {
                            {"name", tc.name.value},
                            {"arguments", tc.args.is_null() ? json::object()
                                                            : tc.args},
                        }},
                    });
                }
                msg["tool_calls"] = std::move(calls);
            }
            arr.push_back(std::move(msg));
        }
        if (has_tools) {
            for (const auto& tc : m.tool_calls) {
                const int recency_rank = total_tool_results - 1 - tool_results_emitted;
                ++tool_results_emitted;
                std::string out = tc.output();
                bool is_error = !tc.is_terminal() || tc.is_failed() || tc.is_rejected();
                if (out.empty()) {
                    if (tc.is_rejected())       out = "(rejected by user)";
                    else if (!tc.is_terminal()) out = "(no output)";
                } else if (!is_error && superseded.count(tc.id.value)) {
                    out = std::string{wire::kSupersededReadPointer};
                } else {
                    out = wire::cap_tool_result_aged(out, recency_rank, is_error);
                }
                json tool_msg{
                    {"role", "tool"},
                    {"tool_name", tc.name.value},
                    {"content", scrub_utf8(out)},
                };
                // 愿景tool_result:ollama的/api/chat接受关于ANY消息的
                // `images`阵列，包括工具结果。与其他方言相同的
                // SSOT 选择器； base64 阵列形状为 ollama
                // -specific。
                json tool_imgs = json::array();
                for (const auto* imgp : wire::wire_tool_result_images(tc, max_side))
                    tool_imgs.push_back(imgp->base64());
                if (!tool_imgs.empty()) tool_msg["images"] = std::move(tool_imgs);
                arr.push_back(std::move(tool_msg));
            }
        }
    }
    return arr;
}

namespace {
json build_tools(const std::vector<provider::ToolSpec>& tools) {
    // SSOT:与OpenAI-compat 传输层共享——参见wire::openai_chat_tools。
    return wire::openai_chat_tools(tools);
}

// JSON-protocol附录(agent-zero样式)用于非常弱的模型。
// 附加到发送本机 `tools` 数组的系统 提示词 INSTEAD。
// 内联工具 目录（名称 + 描述 + 参数名称）并将确切的 single
// -object 响应格式固定到提取器 解析。保持简洁 - 小模型淹没
// 在长模式定义中，所以我们给出名称+一个one-line参数提示，而不是完整的
// JSON 模式定义。
std::string json_protocol_addendum(const std::vector<provider::ToolSpec>& tools) {
    std::string s;
    s += "\n\n## How to act (IMPORTANT — read carefully)\n";
    s += "You do NOT have a function-calling API. To use a tool you MUST reply "
         "with ONE single JSON object and NOTHING else — no prose before or "
         "after it, no markdown fences. The JSON object has exactly these "
         "fields:\n";
    s += "  - \"thoughts\": array of short strings, your reasoning\n";
    s += "  - \"tool_name\": the EXACT name of one tool from the list below\n";
    s += "  - \"tool_args\": an object of arguments for that tool\n\n";
    s += "Example (run a shell command):\n";
    s += "{\"thoughts\":[\"I need to list files\"],\"tool_name\":\"bash\","
         "\"tool_args\":{\"command\":\"ls -la\"}}\n\n";
    s += "Rules:\n";
    s += "- Output the JSON object ALONE, valid JSON, double quotes.\n";
    s += "- Use ONE tool per reply, then wait for its result in the next "
         "message before the next step.\n";
    s += "- The tool's result comes back as a user message beginning "
         "`TOOL RESULT (toolname):` — read it, then emit your NEXT JSON object "
         "(another tool call, or a \"response\" object when the task is done).\n";
    s += "- `tool_name` must be one of the listed names, never an action verb "
         "like read/write/run.\n";
    s += "- Tools act on LOCAL files and commands only. `read` opens a local "
         "file by path — it does NOT fetch a URL. To get a web page use "
         "`web_fetch` with a `url`; to search the web use `web_search`. Never "
         "pass an http(s):// address to `read`.\n";
    s += "- If a tool result is an ERROR, do NOT re-issue the same call — it "
         "will fail again. Fix the arguments (a path that doesn't exist, or "
         "the wrong tool), or answer the user with what you already have.\n";
    s += "- If you do NOT need a tool (a greeting, or a question you can answer "
         "from the conversation), set \"tool_name\" to \"response\" and put your "
         "reply text in \"tool_args\": {\"text\": \"...\"}.\n\n";
    s += "## Available tools\n";
    for (const auto& t : tools) {
        s += "- " + t.name;
        if (!t.description.empty()) {
            // 描述的第一句/第一行保持简短。
            std::string d = t.description;
            if (auto nl = d.find('\n'); nl != std::string::npos) d = d.substr(0, nl);
            if (d.size() > 160) d = d.substr(0, 160);
            s += ": " + d;
        }
        // 参数名称提示来自JSON、模式定义、`properties`。
        if (t.input_schema.is_object() && t.input_schema.contains("properties")
                && t.input_schema["properties"].is_object()) {
            std::string params;
            for (auto it = t.input_schema["properties"].begin();
                 it != t.input_schema["properties"].end(); ++it) {
                if (!params.empty()) params += ", ";
                params += it.key();
            }
            if (!params.empty()) s += "  (args: " + params + ")";
        }
        s += "\n";
    }
    return s;
}

// 语法-constrained解码模式定义 (Ollama `format`字段)。
// Ollama 编译 a JSON 模式定义 转换为 GBNF 语法，并在采样期间屏蔽每
// 个非 -conforming next-token，强制模型发出与此形状匹配的可解析对象，
// 无论大小如何。 tool_name 是广告工具的 ENUM 加上 "response"
// 伪 -tool（plain-chat 逃生舱口：模型将其散文放在 tool_args.text 中，
// rescue_json_protocol 将其展开为文本增量）。
json json_protocol_schema(const std::vector<provider::ToolSpec>& tools) {
    json tool_names = json::array();
    for (const auto& t : tools) tool_names.push_back(t.name);
    tool_names.push_back("response");

    json thoughts_prop;
    thoughts_prop["type"] = "array";
    thoughts_prop["items"]["type"] = "string";

    json name_prop;
    name_prop["type"] = "string";
    name_prop["enum"] = std::move(tool_names);

    json args_prop;
    args_prop["type"] = "object";

    json props;
    props["thoughts"]  = std::move(thoughts_prop);
    props["tool_name"] = std::move(name_prop);
    props["tool_args"] = std::move(args_prop);

    json schema;
    schema["type"]       = "object";
    schema["properties"] = std::move(props);
    schema["required"]   = json::array({"tool_name", "tool_args"});
    return schema;
}
} // 命名空间结束。

// 构建Ollama `options`对象：num_ctx、num_predict，并采样。
// 这是本地型号中最高的 -leverage 鲁棒性杠杆。暴露（non-anonymous），
// 因此 单元测试 可以在没有网络 round-trip 的情况下断言所选值。
// 读取 AGENTTY_OLLAMA_NUM_CTX / _NUM_PREDICT 覆盖。
int effective_num_ctx(int advertised) noexcept {
    // 确定服务窗口的ONE处。 build_options()
    // 低于 调用（用于 传输），而 运行时 调用（用于仪表），
    // 因此两者不能漂移 - 他们确实做到了，漂移了四倍。
    constexpr int kAgentCtxFloor   = 8192;    // 未知-window 默认
    constexpr int kAgentCtxCeiling = 32768;   // 不要OOM本地KV 缓存

    // power-user逃生舱口超越一切：设置此的人比
    // 我们选择的天花板更了解他们的机器。在这里受到尊敬，
    // 而不仅仅是在 build_options 中，
    // 否则仪表会忽略它，并为那些足够关心配置它的用户
    // 重新引入相同的差异。
    if (const char* v = std::getenv("AGENTTY_OLLAMA_NUM_CTX")) {
        char* end = nullptr;
        const long n = std::strtol(v, &end, 10);
        if (end && *end == '\0' && n > 0 && n <= std::numeric_limits<int>::max())
            return static_cast<int>(n);
    }

    if (advertised <= 0) return kAgentCtxFloor;
    if (advertised < kAgentCtxFloor)   return kAgentCtxFloor;
    if (advertised > kAgentCtxCeiling) return kAgentCtxCeiling;
    return advertised;
}

json build_options(const Request& req) {
    auto env_int = [](const char* name) -> int {
        if (const char* v = std::getenv(name)) {
            int n = 0;
            for (const char* p = v; *p >= '0' && *p <= '9'; ++p) n = n * 10 + (*p - '0');
            return n;
        }
        return 0;
    };

    // ── num_ctx (文本窗口) \\\\fn微软雅黑\\\fs18\b1\bord1\shad1\3cH2F2F2}翻译: 该 对代理人的关键选项 ───────────
    //
    // Ollama 的默认 num_ctx 是模型真实窗口的微小 2048（
    // 较新版本为 4096）、INDEPENDENT。由于 agentty 从未设置过它，
    // 所以每个请求都默默地被截断到该层 - 经过几轮工具 -trips 后，
    // 系统 提示词 + 工具 目录 + 早期对话从前面滚动，模型 "forgets"
    // 及其 tools/instructions mid-session。
    // 这是导致 Ollama 上的weak-model 代理失败的第一大原因。
    //
    // 优先级：
    //   1.AGENTTY_OLLAMA_NUM_CTX环境覆盖（power-user逃生舱口）。
    //   2、从/api/show model_info探测模型的真实上下文窗口
    //      (req.context_window)，固定到理智的代理上限，
    //      因此我们不会尝试在笔记本电脑上分配 128k-token KV 缓存
    //      和 OOM Ollama 服务器（这将使整个请求失败）。
    //   3.当窗口未知时，安全代理-sized默认(8192) — 4x
    //      Ollama的底板足以容纳提示词+一些工具转动，
    //      在适度的硬件上足够小至加载。
    //
    // 所有三个现在都位于 effective_num_ctx() 中，
    // 因为上下文 GAUGE 需要相同的答案：它用于对未夹紧的窗口进行评分，
    // 而 传输 发送夹紧的窗口，因此条形图读取 131k 本地模型
    // 上真实使用情况的四分之一。
    const int num_ctx = effective_num_ctx(req.context_window);

    // ── num_predict(最大输出符) ────────────────────────────────
    //
    // Ollama的默认值是低的〜128，其截断了真实答案。但盲目发送
    // Hosted-model 上限（16384）在本地也是错误的：
    // num_predict 代币被保留为 OUT OF num_ctx，
    // 因此 8k 窗口上的 16k num_predict 几乎没有留给 提示词 的空间。
    // 将 num_predict 限制在上下文窗口的一半处，以便 提示词 始终有空间，
    // 下限为 2048（足以用于实质性答案或工具 调用 的参数），
    // 并遵循显式覆盖/请求自己的 max_tokens 作为上限。
    int num_predict = env_int("AGENTTY_OLLAMA_NUM_PREDICT");
    if (num_predict <= 0) {
        num_predict = req.max_tokens > 0 ? req.max_tokens : 4096;
        const int half_ctx = num_ctx / 2;
        if (num_predict > half_ctx) num_predict = half_ctx;
        if (num_predict < 2048)     num_predict = std::min(2048, num_ctx);
    }

    json opts;
    opts["num_ctx"]     = num_ctx;
    opts["num_predict"] = num_predict;

    // ── 抽样 确定性 -leaning 工具的 -calling 可靠性 ───────
    //
    // 弱模型在低温下发出更多可解析的工具调用——高温使得7B
    // 模型即兴创作出畸形的`{"tool":...}`或徘徊
    // 在散文中期-call中。较低（非零）温度可以保持
    // 聊天的多样性，同时大幅减少 tool-JSON 损坏。
    // 仅当模型位于weak/json-protocol路径时设置；有能力的模型保留其模型
    // 文件默认值。通过 AGENTTY_OLLAMA_TEMPERATURE 覆盖。
    if (req.json_protocol) {
        opts["temperature"] = 0.2;
        opts["top_p"]       = 0.9;
    }
    if (const char* t = std::getenv("AGENTTY_OLLAMA_TEMPERATURE")) {
        try { opts["temperature"] = std::stod(t); }
        catch (const std::exception& e) { util::dbglog("ollama.env_temperature.parse", e.what()); }
        catch (...) { util::dbglog("ollama.env_temperature.parse", "non-std exception"); }
    }

    return opts;
}

std::string system_prompt() {
    std::string cwd;
    try { cwd = std::filesystem::current_path().string(); }
    catch (const std::exception& e) { util::dbglog("ollama.system_prompt.cwd", e.what()); }
    catch (...) { util::dbglog("ollama.system_prompt.cwd", "non-std exception"); }

#if defined(_WIN32)
    const char* os_name = "Windows";
    const char* shell   = "cmd.exe";
#elif defined(__APPLE__)
    const char* os_name = "macOS";
    const char* shell   = "sh";
#else
    const char* os_name = "Linux";
    const char* shell   = "sh";
#endif

    std::string out;
    out += "You are agentty, a terminal coding assistant. You are helpful, "
           "direct, and act on requests instead of asking which option to "
           "pick. Keep replies concise.\n\n";

    out += "CONVERSATION MEMORY\n"
           "- The full conversation so far is in the messages above. ALWAYS "
           "use earlier messages to answer follow-up questions (names, files, "
           "decisions the user already gave you).\n"
           "- If the user told you a fact earlier (e.g. their name), recall it "
           "from the conversation; never say you don't have it.\n\n";

    out += "TOOLS\n"
           "- Tools let you read/edit files and run commands. Call a tool ONLY "
           "when the task needs it. For greetings, chit-chat, or questions you "
           "can answer from the conversation, reply in plain text \xE2\x80\x94 "
           "do NOT call a tool.\n"
           "- When a task DOES need an action (rename/move/delete a file, run a "
           "shell command, read or edit code), you MUST actually call the tool. "
           "NEVER describe the command in prose or a code block and claim it "
           "ran \xE2\x80\x94 that does nothing. NEVER say a file was created, "
           "renamed, or deleted unless a tool you called returned that result.\n"
           "- To run a shell command (mv, rm, mkdir, git, etc.) call the `bash` "
           "tool with a `command` argument. There is NO `git` or `mv` tool \xE2\x80\x94 "
           "use `bash`. To edit an existing file use `edit`; use `write` only to "
           "create a new file.\n"
           "- Emit tool calls through the tool-call channel, NOT as JSON or a "
           "```code block``` in your reply.\n"
           "- Make ONE tool call at a time and wait for its result. Never "
           "invent a tool result.\n"
           "- Never call remember/forget/wipe_memory unless the user asks you "
           "to remember or forget something.\n"
           "- For questions about the user's OWN docs, manuals, specs, or "
           "notes (anything you can't reliably answer from general knowledge), "
           "call `search_docs` FIRST to retrieve the relevant passages, then "
           "answer from what it returns. Do NOT guess from memory when the "
           "answer should come from their documents.\n\n";

    // 与 OpenAI-compat 本地 提示词 共享（来源之一，#91）。
    out += openai::local_model_action_rules();

    out += "OUTPUT\n"
           "- Output is rendered as GitHub-flavoured markdown in a terminal. "
           "Use fenced code blocks for code. Keep tables small. LaTeX math "
           "renders too: `$…$` inline, `$$…$$` or a ```math fence for "
           "display (fractions, roots, sums, matrices).\n\n";

    out += "ENVIRONMENT\n";
    out += "- os: ";    out += os_name; out += "\n";
    out += "- shell: "; out += shell;   out += "\n";
    if (!cwd.empty()) { out += "- cwd: "; out += cwd; out += "\n"; }

    out += memory_blocks();
    return out;
}

void set_memory_salvage_intent(StreamCtx& ctx, const Request& req) {
    std::string text;
    for (auto it = req.messages.rbegin(); it != req.messages.rend(); ++it) {
        if (it->role == Role::User && !it->is_proactive_context()) {
            text = it->text;
            break;
        }
    }
    std::ranges::transform(text, text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](std::string_view s) { return text.find(s) != std::string::npos; };
    ctx.allow_remember_salvage = has("remember") || has("don't forget")
        || has("do not forget") || has("keep in mind") || has("from now on");
    ctx.allow_forget_salvage = has("forget") || has("remove the memory")
        || has("remove memory") || has("drop the memory");
    ctx.allow_wipe_salvage = has("wipe memory") || has("wipe your memory")
        || has("forget everything") || has("clean slate");
}

// ── 流式传输 切入点 ────────────────────────────────────────────────────
provider::StreamResult run_stream_sync(Request req, EventSink sink, http::CancelTokenPtr cancel) {
    StreamCtx ctx;
    ctx.sink = std::move(sink);
    ctx.show_reasoning = req.show_reasoning;
    set_memory_salvage_intent(ctx, req);
    // Salvage可能仅将调用合成为我们实际宣传的工具。
    ctx.known_tools.reserve(req.tools.size());
    for (const auto& t : req.tools) ctx.known_tools.push_back(t.name);
    ctx.json_protocol = req.json_protocol && !req.tools.empty();
    // 没有广告宣传工具 → 没有任何可挽救的东西；将所有内容视为散文。
    if (req.tools.empty()) { ctx.holding = false; ctx.salvage_eligible = false; }
    // JSON-protocol：整个回复是一个工具-request对象，
    // 所以始终按住+停留抢救-eligible（没有原生通道到达）。
    if (ctx.json_protocol) { ctx.holding = true; ctx.salvage_eligible = true; }

    // ── 建 构 /api/chat 正文 ────────────────────────────────────────────────
    json body;
    body["model"]  = req.model;
    body["stream"] = true;
    // 使模型在回合之间保持驻留，以便下一个提示词是即时的。
    body["keep_alive"] = "10m";
    body["options"] = build_options(req);

    // 本机推理：问Ollama为模型链布线-of-thought进入结构化的
    // `message.thinking`字段（而不是内联
    // <think>…</think>在内容中）当用户选择可见推理（^R)
    // AND我们不在JSON-protocol模式（弱模型无法推理，
    // 语法会与思维领域发生冲突）。Ollama忽略`think`在不
    // 支持它的型号上，因此可以安全地广泛发送。流处理程序发出
    // message.thinking as StreamThinkingDelta
    // - 这SAME每个其他提供者都使用统一事件 - 因此本地推理在
    // 同一个块中呈现。默认关闭保持死亡状态-air-free线不变。
    // 原生思维。 Ollama 的 `think` 接受 BOOLEAN 或
    // 分级级别（"low"|"medium"|"high"，
    // 较新的版本还有 "max"）； GPT-OSS 特别是 REQUIRES
    // 是调整迹线长度的级别。将与模型无关的工作层映射到其上，以便本地推理
    // 模型像托管模型一样遵循 选择面板 的阶梯：
    //   努力设置 → think:"<level>" (最小 归并 到低 —
    //                 ollama 拒绝 "minimal"；xhigh/max
    //                 归并 到 "high"，因为 "max" 尚未通用 —
    //   参见 ollama#15831) 努力关闭 → think:true
    //                 仅当用户显示推理时（没有层级的可见思维），否则省略。
    // Ollama 在无法推理的模型上忽略 `think`，因此广泛发送它是安全的。
    // 以 JSON-protocol 模式关闭（弱模型；语法会与思维领域作斗争）。
    // 数据流 处理函数 将 message.thinking 发出为
    // StreamThinkingDelta — 每个其他提供商都
    // 使用 相同 统一的 事件。
    if (!ctx.json_protocol) {
        if (!req.effort.empty()) {
            std::string level = req.effort;
            if (level == "minimal")                    level = "low";
            else if (level == "xhigh" || level == "max") level = "high";
            body["think"] = level;
        } else if (req.show_reasoning) {
            body["think"] = true;
        } else if ((req.no_reasoning || req.quick)
                   && req.reasoning_off
                          != static_cast<std::uint8_t>(::agentty::ReasoningOff::Impossible)) {
            // Off是指令，而不是遗漏：思维模型(qwen3、
            // deepseek)在`think`不存在时进行思考。
            body["think"] = false;
        }
    }

    json messages = json::array();
    std::string sys = req.system_prompt;
    // JSON-protocol:将工具目录+single-object响应规范附加到系统提示词，
    // 并且NOT发送下面的本机`tools`数组。
    if (ctx.json_protocol)
        sys += json_protocol_addendum(req.tools);
    if (!sys.empty())
        messages.push_back({{"role", "system"},
                            {"content", scrub_utf8(sys)}});
    for (auto& m : build_messages(req.messages, ctx.json_protocol))
        messages.push_back(std::move(m));
    body["messages"] = std::move(messages);

    // 当不处于 JSON-protocol 模式时，本机工具阵列 仅。
    // 弱模型会得到内联 目录（原生 模式定义 会让它们感到困惑）。
    if (!req.tools.empty() && !ctx.json_protocol)
        body["tools"] = build_tools(req.tools);

    // 弱模型的语法-constrained解码：`format`中的JSON 模式
    // 定义使Ollama 编译成为GBNF语法，并在采样时屏蔽每个非
    // -conforming next-token——无论模型大小如何，
    // 回复都是FORCED到可解析的{thoughts，tool_name，
    // tool_args}对象。下面的 salvage/rescue 路径保留作为
    // 截断的 数据流 的后备（Ollama 约束 per-token 但不验证完整响应）。
    if (ctx.json_protocol)
        body["format"] = json_protocol_schema(req.tools);

    std::string body_str;
    try {
        body_str = body.dump();
    } catch (const nlohmann::json::exception& e) {
        ctx.sink(StreamError{std::string{"request build failed (invalid UTF-8): "}
                             + e.what()});
        ctx.sink(StreamFinished{StopReason::Unspecified});
        return provider::StreamResult::failed("request build failed: invalid UTF-8");
    }

    // ── HTTP 请求 ─────────────────────────────────────────────────────────
    http::Request hreq;
    hreq.method    = http::HttpMethod::Post;
    hreq.host      = req.endpoint.host;
    hreq.port      = req.endpoint.port;
    hreq.path      = "/api/chat";
    hreq.plaintext = !req.endpoint.use_tls;
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    hreq.headers = build_request_headers(req.auth);
    hreq.body    = std::move(body_str);

    // 共享脚手架：status/Retry-After捕获、心跳、
    // 缓冲-wait（这里之前是MISSING——缓冲发送显示为死气沉沉）、
    // 传输转储、上限错误正文。只有 NDJSON 饲料是我们的。
    provider::StreamScaffold sc;
    sc.dialect = "ollama-native";
    sc.sink    = ctx.sink;
    sc.feed    = [&](std::string_view chunk) {
        feed_ndjson(ctx, chunk.data(), chunk.size());
        return true;
    };
    http::StreamHandler handler = sc.handler();

    // 具有两个local-server偏差的标准梯：idle=120s
    // （令牌之间本地生成缓慢）和total=0/unbounded
    // （大模型可以合法地数据流比任何托管上限更长）。
    http::Timeouts tos = provider::local_stream_timeouts(
        std::chrono::milliseconds(120'000));

    // 保留取消令牌的副本：移动到数据流 调用中，
    // 但是finish_stream需要它来区分用户取消和post
    // -loop处的传输层错误。
    http::CancelTokenPtr cancel_for_end = cancel;
    auto result = http::default_client(::agentty::IoAccess::grant()).stream(hreq, std::move(handler),
                                                tos, std::move(cancel));

    // 经由脚手架的统一的末端-of-turn对——该传输层先前在数据流
    // 末端记录了NOTHING(与每个同级相比的漂移)。
    sc.log_result(bool(result),
                  result ? std::string_view{} : result.error().render());

    // 整个帖子-loop到SHARED尾声：一个终端 事件，正确的优先级，
    // 与每个其他提供者相同。 before_finish（仅成功）执行 Ollama
    // 的 终端 salvage/flush — 数据流 可能会在没有 `done`
    // 帧（传输 剪切）的情况下结束，而内容仍然保留，因此 drain/rescue。
    return provider::finish_stream({
        .terminated  = ctx.terminated,
        .sink        = ctx.sink,
        .result_ok   = bool(result),
        .http_status = sc.http_status,
        .non_replayable = !result && result.error().non_replayable,
        .cancel      = cancel_for_end,
        .stop        = ctx.stop_reason,
        .http_error_message = [&]() -> std::string {
            std::string msg = "HTTP " + std::to_string(sc.http_status);
            try {
                auto j = json::parse(sc.error_body);
                if (j.contains("error") && j["error"].is_string())
                    msg += ": " + j["error"].get<std::string>();
                else if (!sc.error_body.empty())
                    msg += ": " + sc.error_body.substr(0, 300);
            } catch (...) {
                if (!sc.error_body.empty()) msg += ": " + sc.error_body.substr(0, 300);
            }
            if (sc.http_status == 404)
                msg += "  (model not loaded — run 'ollama pull " + req.model + "')";
            return msg;
        },
        .retry_after = sc.retry_after(),
        .transport_error_message = [&]() -> std::string {
            std::string msg = std::string{"http: "} + result.error().render();
            msg += "  (is Ollama running? start it with 'ollama serve', or check "
                   "--provider host:port)";
            return msg;
        },
        .before_finish = [&ctx]() {
            // Progressive-response已经拥有回合——只需耗
            // 尽任何待处理解码的字节；永远不会 re-run 救援（会重复）。
            if (ctx.resp_active) {
                if (!ctx.resp_done) {
                    try_progressive_response(ctx);
                    ctx.resp_done = true; ctx.holding = false; ctx.text_hold.clear();
                    if (ctx.stop_reason != StopReason::ToolUse)
                        ctx.stop_reason = StopReason::EndTurn;
                }
            } else {
                bool jp_fired = false;
                if (ctx.json_protocol) {
                    jp_fired = rescue_json_protocol(ctx);
                    if (jp_fired) { ctx.text_hold.clear(); ctx.holding = false; }
                }
                if (!jp_fired && ctx.holding && !ctx.text_hold.empty()) {
                    if (!try_salvage_hold(ctx)) flush_text_hold(ctx);
                    else { ctx.text_hold.clear(); ctx.holding = false; }
                }
                rescue_tool_from_prose(ctx);
            }
            flush_unhandled_content(ctx);
        },
    });
}

std::vector<Msg> parse_ndjson_for_test(std::string_view ndjson_bytes,
                                       std::vector<std::string> known_tools,
                                       bool json_protocol,
                                       bool allow_memory_salvage,
                                       bool show_reasoning) {
    std::vector<Msg> out;
    StreamCtx ctx;
    ctx.show_reasoning = show_reasoning;
    ctx.sink = [&out](Msg m) { out.push_back(std::move(m)); };
    ctx.known_tools = std::move(known_tools);
    ctx.allow_remember_salvage = allow_memory_salvage;
    ctx.allow_forget_salvage = allow_memory_salvage;
    ctx.allow_wipe_salvage = allow_memory_salvage;
    ctx.json_protocol = json_protocol && !ctx.known_tools.empty();
    if (ctx.known_tools.empty()) { ctx.holding = false; ctx.salvage_eligible = false; }
    if (ctx.json_protocol) { ctx.holding = true; ctx.salvage_eligible = true; }
    feed_ndjson(ctx, ndjson_bytes.data(), ndjson_bytes.size());
    // 镜像run_stream_sync的终端齐平，以便测试看到实时序列。
    if (!ctx.terminated) {
        if (ctx.resp_active) {
            if (!ctx.resp_done) {
                try_progressive_response(ctx);
                ctx.resp_done = true; ctx.holding = false; ctx.text_hold.clear();
                if (ctx.stop_reason != StopReason::ToolUse)
                    ctx.stop_reason = StopReason::EndTurn;
            }
        } else {
            bool jp_fired = false;
            if (ctx.json_protocol) {
                jp_fired = rescue_json_protocol(ctx);
                if (jp_fired) { ctx.text_hold.clear(); ctx.holding = false; }
            }
            if (!jp_fired && ctx.holding && !ctx.text_hold.empty()) {
                if (!try_salvage_hold(ctx)) flush_text_hold(ctx);
                else { ctx.text_hold.clear(); ctx.holding = false; }
            }
            rescue_tool_from_prose(ctx);
        }
        flush_unhandled_content(ctx);
        ctx.sink(StreamFinished{ctx.stop_reason});
    }
    return out;
}

} // namespace agentty::provider::ollama
