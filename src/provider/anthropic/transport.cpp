#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/version.hpp"
#include "agentty/provider/prompt.hpp"
#include "agentty/provider/anthropic/sse.hpp"


#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/stream_scaffold.hpp"
#include "agentty/provider/wire.hpp"
#include "agentty/provider/debug.hpp"
#include "agentty/provider/wire_supersede.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::provider::anthropic {

namespace {

// 带-and-suspenders UTF-8洗涤器。 注册表 已在捕获边界
// （GetConsoleOutputCP / CP_ACP 枢轴）处转换子进程输出，
// 但到达 json::dump() 的任何字符串都必须是有效的 UTF-8，
// 否则 API 调用 会因 `type_error.316` 而终止。
// 在已经-valid字符串上运行成本低廉，并保护未来的调用站点，
// 这些站点从多个片段（e.g.错误后缀+部分输出）组装工具输出，其中字节边界可以
// 分割UTF-8序列。将无效的 字节 运行替换为 U+FFFD。
//
// UTF-8验证+清理是传输 头文件中共享的、严格的事实来源
// ——每个传输层进行相同的清理（在400下一个请求之前拒绝过长
// 的编码和代理）。参见 wire::scrub_utf8。
using wire::is_valid_utf8;
using wire::scrub_utf8;

// 将索引备份到其所在的 UTF-8 代码点的开头。
// UTF-8边界助手+工具-result 字节-budget上限现在位于共享传输 头文件
// (include/agentty/provider/wire.hpp)中，
// 因此每个传输层大小工具输出相同——参见wire::cap_tool_result_aged。

} // 命名空间结束。

using json = nlohmann::json;

// 金属丝-format身份。agentty表明自己的身份HONESTLY——确实
// 如此NOT冒充官方Claude代码CLI。用户-agent和 x-app宣布"agentty";
// 我们只发送标头Anthropic's API实际上需要发挥作用（人为-version
// + 人择-beta我们使用的门控功能的标志，包括用于订阅令牌的 oauth-2025-
// 04-20）。我们故意这样做NOT欺骗Claude代码的用户-agent，
// 这Anthropic JS SDK's x-stainless-* 指纹，
// 其BetaToolRunner辅助标签，或Claude-Code-shaped
// metadata.user_id. If Anthropic的边缘永远坚硬
// -requires第一次-party客户签名，这是一个ToS我们向用户展示的边界（
// "use an API key or another provider"），而不是我们通过伪装悄悄规避的。
namespace headers {
    inline constexpr const char* anthropic_version = "2023-06-01";
    // 诚实身份——agentty说它是agentty(::agentty::user_agent())。
    inline constexpr const char* x_app             = "agentty";

    // Beta ID是FEATURE 标志，而不是身份欺骗——每个都解锁
    // agentty真正使用的能力(OAuth-token接受、1M上下文、
    // 上下文管理、提示词-cache范围、fine-grained工具流式传输)。
    // 由 select_betas() 根据 model/auth. 组成
    inline constexpr const char* beta_claude_code            = "claude-code-20250219";
    inline constexpr const char* beta_oauth                  = "oauth-2025-04-20";
    inline constexpr const char* beta_context_1m             = "context-1m-2025-08-07";
    inline constexpr const char* beta_context_management     = "context-management-2025-06-27";
    inline constexpr const char* beta_prompt_cache_scope     = "prompt-caching-scope-2026-01-05";
    // 扩展缓存 TTL (1-hour) opt-in。 无需 这个测试版位于 头
    // 文件 集合中，发送 `cache_control:{ttl:"1h"}` 使
    // Anthropic 的边缘 SILENTLY DROP 成为整个断点（缓存
    // 错过每个回合 + 节流层）。 WITH 它，稳定的前缀断点（系统/工具/对话锚点）
    // 在 1-hour 空闲窗口中生存，而不是默认的 5-minute —
    // 因此暂停阅读、思考或离开不再强制在下一回合对整个 提示词 前缀
    // 进行完整的 -price 缓存-creation re-write。
    // 这是Claude代码的`Dt6`扩展-TTL路径。
    // 我们将最新消息上的 ROLLING 断点保留在默认值 5-minute
    // （它每轮都会变化，因此长 TTL 不会买任何东西），并且仅在内容跨
    // 轮稳定的情况下才使用 1-hour TTL。
    inline constexpr const char* beta_extended_cache_ttl     = "extended-cache-ttl-2025-04-11";
    inline constexpr const char* beta_thinking_binding       = "thinking-binding-controls-2026-08-01";
    // Per-tool `eager_input_streaming: true` 在 Claude
    // 4.6 (GA) 上没有测试版 头文件 的情况下被授予荣誉。对于较旧的型号（haiku
    // -4-5、claude-3.x），边缘需要此 头文件 — 在 4.6+ 上发送它是 no-op，
    // 因此当请求中的任何工具选择加入时，我们始终包含它。
    inline constexpr const char* beta_fine_grained_streaming = "fine-grained-tool-streaming-2025-05-14";
    // 交错的(可见的)思维。让模型在内容块 和 之间进行计划——至
    // 关重要的是，当我们不执行 时，也会发送 redact
    // -thinking——在 传输 上显示思维增量，而不是对其进行编辑。
    // 当用户选择进入 "show reasoning" (^R)且努力等级开启
    // 时添加了 仅；默认情况下完全不思考（没有 dead-air）。
    inline constexpr const char* beta_interleaved_thinking   = "interleaved-thinking-2025-05-14";
} // namespace headers

namespace {

// SSE 解析器 —StreamCtx、dispatch_event、feed_sse、
// simdjson content_block_delta快速路径以及SSE 事件-kind闭合总和—
// 移至anthropic/sse.cpp(StreamCtx+feed_sse+共享dbg/debug_log记录器在中声明sse.hpp）。
// 下面的 run_stream_sync 通过 feed_sse 驱动 数据流。
// 行为由anthropic_sse_golden_test守护。

json tool_spec_to_json(const ToolSpec& s) {
    json j;
    j["name"] = s.name;
    j["description"] = s.description;
    j["input_schema"] = s.input_schema;
    // Anthropic的fine-grained工具流式传输 opt-in(每个-tool字段)。
    // 仅当为 true 时才发出，因此 缓存-key 形状与 CC 的工具块匹配，
    // 用于不使用它的工具。 GA 在 Claude 4.6+ 上；
    // 在旧型号上由 `fine-grained-tool-streaming-2025-05-14`
    // beta 头文件 控制（我们无条件发送该 beta，因此该领域始终受到尊重）。
    if (s.eager_input_streaming) j["eager_input_streaming"] = true;
    if (s.defer_loading) j["defer_loading"] = true;
    return j;
}

// 选择 /v1/messages 的 anthropic-beta 值列表与 firstParty
// 路径的 Mirrors Claude Code v2.1.113 的 `fR8(model)`
// 鸡尾酒构建器的方式完全相同，MINUS 是两个思考测试版。为什么我们偏离 CC：
//
// `interleaved-thinking-2025-05-14`让模型
// 在内容块之间规划；与 `redact-thinking-2026-02-12`
// （抑制 传输 的思维增量）相结合，长 write/edit 调用 的结果
// 是 tool_use 的 `display_description`
// 与其 `content` 字段之间有 20-30 秒的 dead-air
// — 该模型生成了我们从未见过的经过编辑的思维标记，然后将整个 `content`
// 正文 转储到一爆发。 CC 用 "Thinking…" 加载动画 对此进行了论文；
// agentty 的 TUI 没有，所以它看起来只是冻结了。
// 删除两个 beta 会强制模型立即开始发出 `content`。
// 如果您想要 渲染 思维块，只需删除编辑块并在 UI 中显示可见的思维增量。
std::string select_betas(std::string_view model, bool is_oauth,
                         bool any_eager_streaming = false,
                         bool show_reasoning = false) {
    // 用于所有 model-id 内省的单一解码站点 — 请参阅
    // ModelCapabilities::from_id 了解为什么
    // 我们标记化而不是 substring-match。
    // 在此处添加针对新家庭/一代的新测试版； transport.cpp 解析 型号
    // 字符串中没有其他内容。
    const auto caps = ModelCapabilities::from_id(model);

    std::vector<std::string_view> b;
    if (!caps.is_haiku())              b.emplace_back(headers::beta_claude_code);          // !q
    if (is_oauth)                      b.emplace_back(headers::beta_oauth);                // Hq()
    if (caps.extended_context_1m)      b.emplace_back(headers::beta_context_1m);           // AL(H)
    if (caps.generation_4_or_later)    b.emplace_back(headers::beta_context_management);   // eU(提供商) && CR4(H)
    b.emplace_back(headers::beta_prompt_cache_scope);                                      // _（fa() — 始终为真 firstParty）
    // 1-hour扩展缓存 TTL。始终开启：agentty 使用
    // ttl:"1h" 固定稳定前缀（系统/工具/对话锚），
    // 因此长时间空闲窗口不会强制执行 full-price re-cache。
    // 当没有断点使用长 TTL 时无害（边缘只是忽略未使用的功能）。
    b.emplace_back(headers::beta_extended_cache_ttl);
    if (any_eager_streaming)           b.emplace_back(headers::beta_fine_grained_streaming);
    // 模型生成对交错思维（推理BETWEEN
    // 工具调用)的要求不同：
    //   • 自适应型号（Opus 4.6+/4.7/4.8，旗舰 5)：交错
    //     思路是AUTOMATIC——没有beta 头文件，加一个就是no-op。
    //   ·传统启用的-mode型号（Opus/Sonnet 4.5及更早版本）：
    //     interleaved-thinking-2025-05-14
    //     beta 允许模型在 tool_use 块之间进行规划。
    //     我们在该路径上添加 仅，并在用户选择可见推理 (^R)
    //     时添加 仅。我们从不添加 redact-thinking，
    //     因此思维增量达到 传输，推理块具有到 渲染 的真实文本。
    //     默认关闭→无思考测试版→dead-air-free默认传输不变。
    if (show_reasoning && !caps.uses_adaptive_thinking())
        b.emplace_back(headers::beta_interleaved_thinking);
    if (caps.binds_thinking_to_prefix()) b.emplace_back(headers::beta_thinking_binding);

    std::string out;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i) out.push_back(',');
        out.append(b[i]);
    }
    return out;
}

// 构建小写HTTP/2 头文件集合。 agentty 发送 仅、头文件、Anthropic
// API 需要：身份验证 头文件、API 版本、测试版功能 标志 以及其自己的诚实用户
// -agent / x-app。它故意让 不 发送 Anthropic JS SDK 的
// x-stainless-* 平台 指纹 或 CLI 的 x
// -stainless-helper=BetaToolRunner 标签 - 这些标签的存在只是为了让
// 流量首先看到 -party，而 agentty 不是。
//
// AuthHeader为已结束总和(ApiKeyHeader | BearerHeader)；
// std::visit 在类型级别分派到右侧的 头文件 NAME。
// 无法在 `x-api-key:` 下发送 OAuth 令牌，反之亦然 - 变体
// 手臂将其命名为 头文件。
http::Headers build_request_headers(const AuthHeader& auth,
                                    std::string_view beta_value,
                                    int timeout_seconds,
                                    bool streaming = false,
                                    int retry_count = 0) {
    (void)timeout_seconds;
    (void)retry_count;
    http::Headers h;
    h.push_back({"accept", streaming ? "text/event-stream"
                                      : "application/json"});
    h.push_back({"content-type",   "application/json"});
    if (streaming) {
        // 公司网关通常buffer/compress
        // SSE直到积累大的正文。这些标准指令保留增量 帧。
        http::append_sse_no_buffer(h);
    }
    h.push_back({"user-agent",     ::agentty::user_agent()});
    h.push_back({"x-app",          headers::x_app});
    h.push_back({"anthropic-version", headers::anthropic_version});
    h.push_back({"anthropic-dangerous-direct-browser-access", "true"});
    if (!beta_value.empty())
        h.push_back({"anthropic-beta", std::string{beta_value}});
    // 变体臂指示头文件。 "Bearer " 前缀归此站点
    // 所有 - 调用者向我们提供原始令牌，而不是带前缀的字符
    // 串 - 因此 API 密钥也不会意外地带有不记名前缀。
    std::visit([&](const auto& a) {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, ApiKeyHeader>) {
            h.push_back({"x-api-key", a.value});
        } else if constexpr (std::is_same_v<T, BearerHeader>) {
            h.push_back({"authorization", "Bearer " + a.token});
        }
    }, auth);
    return h;
}

// 每-machine十六进制ID稳定。 Anthropic 的 `metadata.user_id`
// 是一个合法的、记录在案的字段，用于滥用信号 - 每台机器保留 STABLE 使得
// agentty 的流量看起来像一个一致的用户，而不是每次都出现一群新会话，
// 这是诚实的（IS 是一台机器上的一个用户）。我们将 不 塑造成 Claude 代码
// 的 `user_<hash>_account_<uuid>_...` 标识符；
// 它只是一个派生的-from-machine-id不透明的十六进制字符串。
std::string machine_id_hex(int nibbles) {
    static std::string cached;
    static std::once_flag once;
    maya::once(once, [] {
        std::string seed;
        for (auto path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
            std::ifstream f(path);
            if (f) { std::getline(f, seed); if (!seed.empty()) break; }
        }
        if (seed.empty()) {
            if (const char* h = std::getenv("HOSTNAME")) seed = h;
        }
        if (seed.empty()) seed = "agentty-anonymous";
        // FNV-1a 64-bit，两次以不同的偏移量填充至 128 位。
        auto fnv = [](std::string_view s, uint64_t off) {
            uint64_t h = off;
            for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ull; }
            return h;
        };
        uint64_t a = fnv(seed, 0xcbf29ce484222325ull);
        uint64_t b = fnv(seed, 0x84222325cbf29ce4ull);
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                      (unsigned long long)a, (unsigned long long)b);
        cached.assign(buf, 32);
    });
    return cached.substr(0, std::min<size_t>(nibbles, cached.size()));
}

std::string make_user_id() {
    // metadata.user_id is a JSON目的
    // `{device_id, session_id}`：稳定的每-machine device_id
    // 加每-process session_id（铸造一次，因此在一轮中保持不变agentty跑步）。
    // 这是agentty自己诚实的客户身份Anthropic的虐待-signal装桶——
    // NOT的副本Claude代码的`T7H()`形状好，无假货account_uuid.
    static std::string cached;
    static std::once_flag once;
    maya::once(once, []{
        auto device_id = machine_id_hex(32);
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        uint64_t s1 = 0xcbf29ce484222325ull;
        for (int i = 0; i < 8; ++i) { s1 ^= (now >> (i*8)) & 0xff; s1 *= 0x100000001b3ull; }
        uint64_t s2 = s1 ^ 0x9e3779b97f4a7c15ull;
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                      (unsigned long long)s1, (unsigned long long)s2);
        auto session_id = std::string(buf, 32);
        cached = nlohmann::json{
            {"device_id",  device_id},
            {"session_id", session_id},
        }.dump();
    });
    return cached;
}

} // 命名空间结束。

// ── 流式传输 字符串-backed 消息-array 撰稿人 ─────────────────────────
//
// messages_json_string() / build_messages() 及其 json_write_
// * 帮助程序移至 anthropic/wire_body.cpp — request-body 序列
// 化程序是此文件 高频路径 的最大块，并且干净地提取出来（没有 StreamCtx，没有 SSE）。
// 通过 transport.hpp 声明，run_stream_sync() 位于 调用
// messages_json_string() 之下。 字节-identity 由 wire_golden_test 守护。

// ----------------------------------------------------------------------------

provider::StreamResult run_stream_sync(Request req, EventSink sink, http::CancelTokenPtr cancel) {
    if (is_empty(req.auth)) {
        sink(StreamError{"not authenticated — run 'agentty login' or set ANTHROPIC_API_KEY"});
        return provider::StreamResult::failed("not authenticated");
    }

    // 在 `sink` 被移动到下面的 `ctx.sink` 中之后，
    // emit_terminal 在错误路径上运行——通过 `ctx.sink` 调度是唯一的实时句柄。
    // 之前的版本通过引用捕获`sink`并在每个非-happy-path终端上调用移动的-from
    // std::function，其在UI中作为
    // "stream backend: bad_function_call"出现。
    // 整个post-loop现在贯穿于末尾处的provider::finish_stream。

    const bool is_oauth = std::holds_alternative<BearerHeader>(req.auth);

    json body;
    // `[1m]` 选择面板标记选择1M窗口+上下文测试版，但NOT是真实模型ID——将其剥离，
    // 以便传输携带`claude-sonnet-5`，而不是`claude-sonnet-5[1m]`
    // （API 404s）。下面的 select_betas() 仍然会看到
    // ORIGINAL req.model，因此它会检测到标记并发送 context
    // -1m beta。
    body["model"]      = wire_model_id(req.model);
    body["max_tokens"] = req.max_tokens;
    body["stream"]     = true;

    // 具有1-HOUR扩展TTL的稳定-prefix 缓存断点。
    // 系统 提示词 是请求中最稳定的东西 - 它仅在用户编辑 CLAUDE.md
    // 或 `remember`/`forget` 触发时发生变化 - 因此它是
    // 度过漫长 TTL 的理想场所。我们无条件发送
    // `extended-cache-ttl-2025-04-11` 测试版（参见
    // select_betas），这就是 `ttl:"1h"` 坚持下来而不是被默默丢弃的原因。
    // 这是Claude代码的`Dt6`扩展-cache路径。
    // 结果：长达一个小时的暂停不再强制在下一回合对整个系统前缀进行 full
    // -price re-cache。滚动消息断点保留 5 分钟默认值 (
    // messages_json_string)，因为它们无论如何都会改变。
    const json kCacheCtl1h = {{"type", "ephemeral"}, {"ttl", "1h"}};

    // 系统始终作为 content-block 数组发送，因此无论身份验证方式如何，
    // 我们都可以附加 cache_control。 OAuth 另外在前面添加了
    // 不可变 Claude 代码前导码（cli.js 行 ~5641），
    // 以便 Anthropic 的边缘接受 OAuth 令牌； API-key 呼叫者会跳过该序言。
    {
        json sys = json::array();
        if (is_oauth) {
            sys.push_back({
                {"type", "text"},
                {"text", "You are Claude Code, Anthropic's official CLI for Claude."}
            });
        }
        // 也擦洗系统提示词。与消息文本（通过 messages_json_string
        // 中的 scrub_utf8 运行）不同，系统 提示词 直接放入 `body` 中，
        // 并由下面的 body.dump() 序列化 — 这会在 首次 格式错误的
        // UTF-8 字节 上抛出 type_error.316。 提示词 现在携带
        // 任意用户 字节（CLAUDE.md 层 + `remember` 记录，
        // 可以从粘贴的工具输出中舍入 -trip truncated/broken
        // multi-byte 序列）。一个坏的 字节 导致 每个 与
        // "invalid UTF-8 in conversation" 一起打开每个
        // 线程 失败；无论来源如何，这里的一次擦洗都可以使 传输 很好地-formed。
        sys.push_back({
            {"type", "text"},
            {"text", scrub_utf8(req.system_prompt)},
            {"cache_control", kCacheCtl1h}
        });
        body["system"] = std::move(sys);
    }

    // 将消息数组直接构建为字符串缓冲区。 缓存 断点（最后一个 + 第二个
    // -to-last 消息，每个消息的最后一个块）在写入期间内联 — 请参阅
    // messages_json_string。我们将其拼接到下面转储的
    // 正文 中，而不是经过 `body["messages"] = json::parse(...)`，
    // 这会将 -parse 字符串重新转换回 JSON 源码树，
    // 以便 body.dump() 可以再次重新 -serialize。
    // 对于具有 1 MiB 内容的 write-tool 回合，
    // round-trip 是主要请求-build 成本。
    // 仅当该请求本身启用思考（努力）时，才重放存储的思考块。
    // 通过思考，忽略它们——它们只被思考所需要，
    // 并且对思考有效-enabled请求。
    std::string messages_str = messages_json_string(
        Thread{ThreadId{""}, "", req.messages, {}, {}},
        /*include_thinking=*/!req.effort.empty());
    if (!req.tools.empty()) {
        json tools_j = json::array();
        for (const auto& t : req.tools) tools_j.push_back(tool_spec_to_json(t));
        // 工具缓存断点继续在LAST工具上——模式定义阵列按顺序序列化，
        // 并且Anthropic的边缘缓存是前缀，
        // 直到并包括标记的块。匹配 cli.js，其中每个会话构建
        // 一次工具列表，最后一个条目带有 cache_control。
        // 工具是session-stable（每轮都相同的目录），
        // 因此它们在系统提示词旁边得到1-hour TTL——它们一起形成了
        // long-lived前缀，可以在空闲间隙中生存。
        //
        // 延迟工具不能承载cache_control (400)，
        // 因此引脚位于未延迟的最后一个工具上。每当有任何事情被推迟时，
        // tool_search 都是最后一个，所以这通常是最后一个工具。
        for (auto it = tools_j.rbegin(); it != tools_j.rend(); ++it)
            if (!it->value("defer_loading", false)) {
                (*it)["cache_control"] = kCacheCtl1h;
                break;
            }
        body["tools"] = std::move(tools_j);
    }
    body["metadata"] = json{{"user_id", make_user_id()}};
    // 推理努力+适应性思维。 req.effort 是 -clamped 之前的
    // launch_stream 的模型能力； non-empty 表示用户在模型 选择面板
    // 中选择了一个思考层。将 output_config.effort 与适应性思维配
    // 对 — GA 方式为 Opus 4.6+/4.7/ 4.8 打开推理（budget_tokens
    // 在 4.7/4.8 上被删除；在那里输入："enabled" 400s）。
    // 当努力结束时完全省略，保留默认的 no-thinking、
    // dead-air-free 传输。模型响应时发出的辅助思维块由
    // messages_json_string 捕获并重放（见下文），
    // 因此 tool_use 转弯不会因为掉落的思维块而变为 400。
    // 首先声明(/v1/models)并了解事实，id规则为回退。
    const auto off_caps = resolved_caps(wire_model_id(req.model), "anthropic");
    if (!req.effort.empty()) {
        // 认为MODE是修订-gated(
        // Anthropic改变了4.6/4.7边界处的接口)：
        //   • Opus/Sonnet 4.5 和 EARLIER（修订版 <= 5）：仅限
        //     思考:{type:"enabled",budget_tokens:N}被接受；
        //     型号："adaptive" 400s (
        //     "adaptive thinking is not supported on this model")。（此模型不支持自适应思考。）
        //   ·4.6:两者都有效。
        //   • 4.7 / 4.8 / Claude 5+：仅类型："adaptive" + output_config.effort
        //     (budget_tokens被移除；类型："enabled" 400s)。
        // 我们从 传输 型号 ID 中解码修订版本并相应地进行选择。
        const auto& tcaps = off_caps;
        const bool legacy_enabled = !tcaps.uses_adaptive_thinking();

        if (legacy_enabled) {
            // 将工作量层映射到思考令牌预算（> = 1024分钟）。
            // 随层缩放；上限低于 max_tokens，
            // 所以答案仍然有空间。
            int budget = 8000;
            if      (req.effort == "low")    budget = 4000;
            else if (req.effort == "medium") budget = 8000;
            else if (req.effort == "high")   budget = 16000;
            else if (req.effort == "xhigh")  budget = 24000;
            else if (req.effort == "max")    budget = 32000;
            // 将预算严格保持在max_tokens以下(Anthropic
            // 要求启用路径上的budget_tokens < max_tokens；
            // 满足或超过400秒的预算)。留下舒适的应答楼层。
            if (req.max_tokens > 0) {
                const int ceiling = std::max(1024, req.max_tokens - 8000);
                if (budget > ceiling) budget = ceiling;
            }
            json thinking = json{{"type", "enabled"},
                                 {"budget_tokens", budget}};
            // 显示工作在BOTH模式；在启用的路径上，
            // 默认情况下思维是可见的（总结），但当用户要求
            // 查看推理时明确设置它，以便在 传输 上意图明确。
            if (req.show_reasoning)
                thinking["display"] = "summarized";
            body["thinking"] = std::move(thinking);
            // Opus 4.5是ALSO向output_config.effort
            // 致敬的一款扩展-thinking-only型号，其中COMPOSES
            // 与budget_tokens（努力引导深度，预算限制它）。
            // 当模型支持工作时发送它，这样 选择面板 的等级就不会默默下降。
            if (tcaps.supports_effort())
                body["output_config"] = json{{"effort", req.effort}};
        } else {
            json thinking = json{{"type", "adaptive"}};
            // VISIBLE推理：自适应思维REDACTS其文本默认放在
            // 传输上（thinking_delta.thinking到达为空）。
            // 当用户打开 "show reasoning" (^R) 时，
            // 请求总结思维 数据流，以便我们能够获得模型的实际推理增量。
            if (req.show_reasoning)
                thinking["display"] = "summarized";
            body["thinking"]      = std::move(thinking);
            body["output_config"] = json{{"effort", req.effort}};
        }
    } else if (off_caps.needs_explicit_thinking_off()) {
        // 努力是关闭的，但该模型会思考，除非被告知不要这样做。
        // 省略 `thinking` 会导致用户关闭账单。
        body["thinking"] = json{{"type", "disabled"}};
    } else if (off_caps.supports_between_tools_thinking()) {
        // 无法禁用；这是其最低设置。
        body["thinking"] = json{{"type", "between_tools"}};
    } else if (off_caps.thinking_always_on()) {
        // 不能被禁用，并且让它按其默认努力思考，这可以在任何
        // 文本之前花费所有max_tokens。
        body["thinking"]      = json{{"type", "adaptive"}};
        body["output_config"] = json{{"effort", "low"}};
    }
    // 当系统提示词或工具改变（内存、MCP、模式切换）时，
    // 旧思维块转到过期。 Ask API 掉落它们，而不是 400。
    if (off_caps.binds_thinking_to_prefix() && body.contains("thinking")
        && body["thinking"].value("type", "") == "adaptive")
        body["thinking"]["block_binding"] =
            json{{"prefix_mismatch_behavior", "drop_block"}};
    // 消息阵列的拼接标记。 nlohmann 给出了转储表格
    // `"messages":<unique-string>"`；
    // 我们将 -replace 占位符与 messages_json_string
    // 串起来。选择了一个不能出现在合法的 -escaped JSON 字符串中的标记，
    // 因此 find() 即使在奇怪的 载荷 上也是明确的。
    constexpr std::string_view kMessagesPlaceholder =
        "\x01__agentty_messages_splice__\x01";
    body["messages"] = std::string{kMessagesPlaceholder};

    // Last-line-of-defence:如果请求源码树中的任何字符串仍然
    // 携带non-UTF-8 字节(绕过清理的工具，新的代码路径)，则下面的dump
    // ()抛出type_error.316。我们曾经是terminate()；
    // 现在我们显示了 StreamError，以便 状态更新函数 可以恢复，
    // 并且用户会看到转向失败，而不是进程在 -stream 中期死亡。
    std::string body_str;
    try {
        body_str = body.dump();
    } catch (const nlohmann::json::exception& e) {
        sink(StreamError{std::string{"request build failed (invalid UTF-8 in conversation): "} + e.what()});
        sink(StreamFinished{StopReason::Unspecified});
        return provider::StreamResult::failed("request build failed: invalid UTF-8");
    }
    // 将转储的占位符字符串替换为原始消息JSON。
    // nlohmann将std::string值发出为JSON字符串（引用+转义）。
    // 控件 字节 \x01 round-trip 为 ``，因此转储的形式为
    // `"__agentty_messages_splice__"`
    // — 查找并替换它。
    {
        constexpr std::string_view kDumpedPlaceholder =
            "\"\\u0001__agentty_messages_splice__\\u0001\"";
        auto pos = body_str.find(kDumpedPlaceholder);
        if (pos == std::string::npos) {
            sink(StreamError{"request build failed: messages placeholder not found in dumped body"});
            sink(StreamFinished{StopReason::Unspecified});
            return provider::StreamResult::failed("request build failed: placeholder");
        }
        body_str.replace(pos, kDumpedPlaceholder.size(), messages_str);
    }

    AGT_LOG(Wire, Trace, "anthropic.request.body", "bytes={} raw={}", body_str.size(), ::agentty::logx::body(body_str));

    StreamCtx ctx;
    ctx.sink = std::move(sink);

    http::Request hreq;
    hreq.method  = http::HttpMethod::Post;
    hreq.host    = "api.anthropic.com";
    hreq.port    = 443;
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    // `?beta=true`与SDK中的`beta.messages.create`匹配(
    // cli.js第393行)——Anthropic的边缘门与测试头文件所针对的路径相同。
    hreq.path    = "/v1/messages?beta=true";
    // 300 秒匹配 cli.js mb1()：API_TIMEOUT_MS 环境覆盖或本地
    // 默认 300 秒（CLAUDE_CODE_REMOTE 为 120 秒）。
    // x-stainless-timeout 是广告，而不是强制执行——我们实际的
    // 数据流 是无界的，而 取消 在 帧 边界进行轮询。
    const bool any_eager = std::ranges::any_of(req.tools,
        [](const auto& t){ return t.eager_input_streaming; });
    hreq.headers = build_request_headers(req.auth,
                                         select_betas(req.model, is_oauth, any_eager,
                                                      req.show_reasoning),
                                         /*timeout_seconds=*/300,
                                         /*流式传输=*/true,
                                         /*retry_count=*/req.retry_count);
    hreq.body    = std::move(body_str);

    // 所有per-turn 处理函数脚手架——状态捕获、错误路径上的重试
    // -After（Anthropic在429/529上将其作为整数秒发出）、
    // 心跳、缓冲-wait、传输转储、上限错误-body累积——是共享
    // StreamScaffold。只有 解析器 饲料是我们的。
    provider::StreamScaffold sc;
    auto arm = [&] {
        sc = provider::StreamScaffold{};
        sc.dialect = "anthropic-messages";
        sc.sink    = ctx.sink;
        sc.feed    = [&](std::string_view chunk) {
            feed_sse(ctx, chunk.data(), chunk.size());
            return true;
        };
    };
    arm();

    // 标准 流式传输 阶梯 — 基本原理与 stream_timeouts() 一致。
    const http::Timeouts tos = provider::stream_timeouts();
    http::CancelTokenPtr cancel_for_end = cancel;
    auto& client = http::default_client(::agentty::IoAccess::grant());
    auto result  = client.stream(hreq, sc.handler(), tos, cancel);

    // 我们认为需要"disabled"的型号可能仍然是400就可以了。
    // 400 数据流 什么都没有，所以再次发送 API 所要求的：between_tools
    // 当它命名时（sonnet-5-5 风格），否则最低的努力（opus-5-5）。
    constexpr std::string_view kDisabled = R"("thinking":{"type":"disabled"})";
    constexpr std::string_view kLow      = R"("output_config":{"effort":"low"})";
    constexpr std::string_view kBetween  = R"("thinking":{"type":"between_tools"})";
    // 用于学习的模型的思维类型如下：声明的，
    // 否则id规则假设的。
    using MC = ModelCapabilities;
    const std::uint8_t known_types = off_caps.thinking_declared()
        ? off_caps.thinking_types
        : static_cast<std::uint8_t>(MC::kThinkDeclared
              | (off_caps.uses_adaptive_thinking() ? MC::kThinkAdaptive : MC::kThinkEnabled)
              | (off_caps.needs_explicit_thinking_off() ? MC::kThinkDisabled : 0)
              | (off_caps.supports_between_tools_thinking() ? MC::kThinkBetweenTools : 0));
    if (result && sc.http_status == 400) {
        const bool wants_between =
            sc.error_body.find("between_tools") != std::string::npos;
        const bool rejects_disabled = wants_between
            || sc.error_body.find("thinking.type.disabled") != std::string::npos;
        const bool rejects_between =
            sc.error_body.find("thinking.type.between_tools") != std::string::npos;
        if (auto p = hreq.body.find(kDisabled);
            rejects_disabled && p != std::string::npos) {
            AGT_LOG(Wire, Warn, "anthropic.thinking_disabled_rejected",
                    "model={} retrying with {}", req.model,
                    wants_between ? "between_tools" : "effort=low");
            // 记住它，以便以后轮流第一次发送正确的东西。
            learn_anthropic_thinking(wire_model_id(req.model), known_types,
                                     MC::kThinkDisabled,
                                     wants_between ? MC::kThinkBetweenTools : 0);
            hreq.body.replace(p, kDisabled.size(), wants_between ? kBetween : kLow);
            arm();
            result = client.stream(hreq, sc.handler(), tos, cancel);
        } else if (auto q = hreq.body.find(kBetween);
                   rejects_between && q != std::string::npos) {
            AGT_LOG(Wire, Warn, "anthropic.thinking_between_rejected",
                    "model={} retrying with effort=low", req.model);
            learn_anthropic_thinking(wire_model_id(req.model), known_types,
                                     MC::kThinkBetweenTools, 0);
            hreq.body.replace(q, kBetween.size(), kLow);
            arm();
            result = client.stream(hreq, sc.handler(), tos, cancel);
        }
    }

    // 经由支架的统一末端-of-turn对；附加了 thinking_deltas，
    // 因此 "reasoning not showing" 报告将 server-vs-client 从日志中拆分出来。
    sc.log_result(bool(result),
                  result ? std::string_view{} : result.error().render(),
                  std::format("thinking_deltas={}", ctx.thinking_deltas));

    // 整个帖子-loop到SHARED尾声：对出口进行分类并以正确的优先级恰好
    // 发出一个终端 事件。 on_any_end 在 两者 成功和错误时关闭打开
    // 的工具块（对端 可能会在 content_block_stop 之前切断
    // mid-tool-use），因此 状态更新函数 的救援路径始终运行。
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
                if (j.contains("error") && j["error"].contains("message"))
                    msg += ": " + j["error"]["message"].get<std::string>();
                else if (j.contains("message"))
                    msg += ": " + j["message"].get<std::string>();
                else
                    msg += ": " + sc.error_body.substr(0, 300);
            } catch (...) {
                if (!sc.error_body.empty()) msg += ": " + sc.error_body.substr(0, 300);
            }
            if (sc.http_status == 401 || sc.http_status == 403)
                msg += "  (run 'agentty login' to re-authenticate)";
            return msg;
        },
        .retry_after = sc.retry_after(),
        // 网络/TLS/nghttp2-level错误——从未产生完整的SSE 数据流。
        // 带类型的 HttpError 的 render() 是嵌入的，
        // 因此 下游 error_class 子字符串嗅探仍然会路由它。
        .transport_error_message = [&]() -> std::string {
            return std::string{"http: "} + result.error().render();
        },
        .on_any_end = [&ctx]() {
            // 数据流在服务器没有关闭其块的情况下结束——中止、
            // 传输层错误、被截断的响应。关闭所有仍打开的窗口；
            // 工具 调用 离开 待处理 此处楔入转弯。
            // 采取的是批次，所以没有顺序出错的情况。
            for (const auto& b : ctx.tool_blocks)
                ctx.sink(StreamToolUseEnd{ToolCallId{b.id}});
            ctx.tool_blocks.clear();
        },
        // Anthropic总是以message_stop结束回复。
        .expects_terminal_event = true,
    });
}

std::vector<Msg> parse_sse_for_test(
    const std::vector<std::pair<std::string, std::string>>& events) {
    std::vector<Msg> out;
    StreamCtx ctx;
    ctx.sink = [&out](Msg m) { out.push_back(std::move(m)); };
    // 重建确切的SSE 传输形式(`event: <name>\ndata: <json>\n\n`)
    // 并将其推过feed_sse —现场on_chunk使用的SAME成
    // 帧器+dispatch_event路径 —因此发出的Msg序列与
    // 携带这些帧的真实数据流相同。
    for (const auto& [name, data] : events) {
        std::string frame;
        frame.reserve(name.size() + data.size() + 16);
        frame += "event: ";
        frame += name;
        frame += "\ndata: ";
        frame += data;
        frame += "\n\n";
        feed_sse(ctx, frame.data(), frame.size());
    }
    return out;
}

// 来自/v1/models行的capabilities.thinking.types/capabilities.effort。
// 当该行没有说明时为零字段（较旧的API，剥离它的代理）。
AnthropicDecl parse_capabilities(const json& model) {
    AnthropicDecl d;
    const auto caps = model.find("capabilities");
    if (caps == model.end() || !caps->is_object()) return d;
    auto supported = [](const json& o, const char* key) {
        const auto it = o.find(key);
        if (it == o.end()) return false;
        if (it->is_boolean()) return it->get<bool>();
        return it->is_object() && it->value("supported", false);
    };
    if (const auto th = caps->find("thinking"); th != caps->end() && th->is_object()) {
        if (const auto ty = th->find("types"); ty != th->end() && ty->is_object()) {
            using MC = ModelCapabilities;
            d.thinking = MC::kThinkDeclared;
            if (supported(*ty, "enabled"))       d.thinking |= MC::kThinkEnabled;
            if (supported(*ty, "adaptive"))      d.thinking |= MC::kThinkAdaptive;
            if (supported(*ty, "disabled"))      d.thinking |= MC::kThinkDisabled;
            if (supported(*ty, "between_tools")) d.thinking |= MC::kThinkBetweenTools;
        } else if (!th->value("supported", true)) {
            // 根本没有思考："off"只是离开了这个领域。
            d.thinking = ModelCapabilities::kThinkDeclared;
        }
    }
    if (const auto ef = caps->find("effort"); ef != caps->end() && ef->is_object()) {
        for (Effort e : {Effort::Minimal, Effort::Low, Effort::Medium, Effort::High,
                         Effort::Xhigh, Effort::Max})
            if (supported(*ef, std::string{effort_wire(e)}.c_str()))
                d.effort |= effort_bit(e);
    }
    return d;
}

std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth) {
    const bool is_oauth = std::holds_alternative<BearerHeader>(auth);

    // Claude代码针对每个后缀-capable型号呈现1M-context
    // `[1m]` VARIANT。我们通过 catalog::add_1m_variants
    // （单一共享 impl — 捆绑地板使用的相同）进行镜像，应用于下面的实时 目录。
    // 权利 self-heals 下游（如果 1M 请求 400 秒，
    // context_1m_blocked 会剥离行），因此广泛提供该优惠是安全的。

    // Built-in地板=单个捆绑的目录(基本行+它们的`[1m]`同伴)。
    // 与 seed_models() 共享一个源，因此种子和下面
    // 的实时 /v1/models 目录 永远不会漂移。
    // 仅当网络探测确实没有产生任何结果（离线/瞬态非 200）时才返回，
    // 因此 选择面板 永远不会空着；直播名单就是天花板。
    auto seed = [] { return catalog::bundled("anthropic"); };

    std::vector<ModelInfo> result;
    if (is_empty(auth)) return seed();

    http::Request hreq;
    hreq.method  = http::HttpMethod::Get;
    hreq.host    = "api.anthropic.com";
    hreq.port    = 443;
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    hreq.path    = "/v1/models?limit=100";
    // /v1/models不需要流式传输 betacocktail——仅
    // 需要oauth门(当适用时)，匹配cli.js调用模型-listing端点。
    hreq.headers = build_request_headers(auth,
                                         is_oauth ? headers::beta_oauth : "",
                                         /*timeout_seconds=*/10);
    // /v1/models是一个小列表(在典型的目录尺寸下约30个KB)。
    // 严格限制，以便行为不当的代理/重放循环无法在例行启动探测中将
    // 数据流 引入 OOM。
    hreq.max_body_bytes = 1ull * 1024 * 1024;

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(5'000);
    tos.total   = std::chrono::milliseconds(10'000);

    auto resp = http::default_client(io).send(hreq, tos);
    if (!resp || resp->status != 200) return seed();

    try {
        auto j = json::parse(resp->body);
        std::vector<std::pair<std::string, AnthropicDecl>> decls;
        for (const auto& m : j.value("data", json::array())) {
            auto id = m.value("id", "");
            auto name = m.value("display_name", id);
            if (id.empty()) continue;
            // 该账户的实时数字；排名超过 models.dev。
            if (const auto mt = m.find("max_tokens");
                mt != m.end() && mt->is_number_integer())
                set_catalog_max_output("anthropic/" + id, mt->get<int>());
            if (auto d = parse_capabilities(m); d.thinking != 0 || d.effort != 0)
                decls.emplace_back(id, d);
            result.push_back(ModelInfo{
                .id = ModelId{id},
                .display_name = name,
                .provider = "anthropic",
                .context_window = 200000,
            });
        }
        // 在我们交错 1M 变体 之前，将原始 /v1/models 列表排序成一个干净的、
        // 可预测的分组——参见 catalog.hpp 的
        // model_picker_less（单一事实来源：强度-first，从来不是一个固定
        // 的家庭桶，所以 Fable/Mythos 永远不会因为 alphabetical/positional
        // 运气不好而沉入 Opus/Sonnet/ 俳句以下）。
        std::stable_sort(result.begin(), result.end(), model_picker_less);
        set_anthropic_decls(std::move(decls));
        // 为每个后缀-capable模型呈现`[1m]`伴侣，
        // 将其插入到其基本模型之后，以便配对是相邻的（共享实现）。
        catalog::add_1m_variants(result);
    } catch (const std::exception& e) {
        util::dbglog("anthropic.list_models.parse", e.what());
    } catch (...) {
        util::dbglog("anthropic.list_models.parse", "non-std exception");
    }

    // 网络说200，但我们解析出任何可用的东西——回退到种子，
    // 因此在提供商切换之后选择面板永远不会留空。
    if (result.empty()) return seed();
    return result;
}

} // namespace agentty::provider::anthropic
