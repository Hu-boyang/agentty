#pragma once
// agentty::app::detail-流式传输-tool-args机器的共享叶助手。
// 这些是 (JSON / std::string / std::span) 上的纯函数，
// 没有 Model 或 状态更新函数 依赖性；它们以前是 stream.cpp 私有的，
// 但现在由 两者、live-preview 解码器 (stream_preview.cpp)
// 和 finalize/salvage 路径 (stream.cpp) 使用，
// 因此它们住在这里以避免重复或 ODR 冲突。
//
// `inline`(不是`static`)，因此两个编译单元共享
// 一个定义——地址是不相关的(没有人获取他们的指针)并且内联保持
// 热预览路径分配-free。

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "agentty/tool/spec.hpp"
#include "agentty/tool/util/partial_json.hpp"

namespace agentty::app::detail {

// 键模型有时会代替我们的规范字段名称发出。
//
// 这些早于下面的命令族。 mcp-cpp 的 ArgReader 现在有自己的别名表（
// src/tools/util/arg_reader.cpp），并且 read/grep/shell
// 在那里整理了更多形状；这些列表是 agentty 的主机-side 容差，
// 由 canonify、salvage_args 和下面所需的-field 防护使用。
//
// WIDENING 一个 OF THESE HAS 两个 CONSUMERS，
// 没有一个。 required-field 仅保护 READS 表（别名满足检查并且
// 不会重写任何内容），但 salvage_args CANONIFIES 通过它 -
// `pick("old_string", kOldStrAliases)` 将它
// 找到的第一个别名复制到规范键上。今天这是安全的，原因值得写下而不是重新发现：抢救
// 仅在 TRUNCATED 数据流 上运行，ended_inside_string
// () 首先拒绝 mid-string 情况，并且每个 分派器 无论如何都会读取两种拼写，
// 因此重写是 副作用 中的 no-op。添加一个别名，其规范孪生在
// 某处按字面意思读取，但事实并非如此。
inline constexpr std::string_view kPathAliases[]    = {"path", "file_path", "filepath", "filename"};
// old_text/new_text是成绩单-evidenced（Laguna S 2.1、
// 2026年10月3日的agentty会话、do_edit.md跟踪）：编辑工具的编辑
// []条目被键入old_text/new_text（参见edit_schema），
// 并且模型将该拼写泄漏到TOP-LEVEL old_string/new_string槽中。
// parse_edit_args 接受两个级别的两种拼写（顶级探测 `old_string`，
// 然后是 `old_text`；edits[] 探测相反），所以 分派 从来都不是
// 失败——守卫才是。 `old`/`search`/`find`/`from` 被
// 故意列出在 不 中：没有记录显示发出它们的模型，并且与 _text 形式不同，
// 它们不是 模式定义 自己的该工具词汇表。
inline constexpr std::string_view kOldStrAliases[]  = {"old_string", "old_text",
                                                       "old_str", "oldStr"};
inline constexpr std::string_view kNewStrAliases[]  = {"new_string", "new_text",
                                                       "new_str", "newStr"};
inline constexpr std::string_view kContentAliases[] = {"content", "file_text", "text",
                                                        "file_content", "contents",
                                                        "body", "data"};
// 为 shell 工具的 `command` 参数发出的键模型。 Laguna S 2.1（以及采用
// OpenAI-style 规格的 cross-trained 型号）可靠地发送 `cmd`。
//
// 不 镜像自 mcp-cpp 的 ArgReader，尽管上面的评论是关于旧列表的。
// ArgReader 是一个普通的键控读取器——str(key)、
// require_str(key)、raw(key)——根本没有别名表；
// 附近唯一的 "cmd" 是 util/child_process.hpp.
// 中的 cmd.exe 检测，所以这里的规范化并不是重复 分派 的便利，
// 它是 该 机制：在没有规范化的情况下放松警惕，`cmd`-shaped
// 调用 停止大声失败，并开始到达读取 `command` 的 分派器，
// 什么也没找到，并运行一个空字符串。一起守卫和册封船只，或者根本不守卫
// 和封圣船只。
//
// 故意保留SHORT。每个条目都是一个键，我们将其值传递给 shell，
// 因此错误的猜测不会导致失败的 调用 —— 它会执行错误的字符串。由于这个原因，
// `shell` 和 `run` 被删除：`shell` 作为工具 NAMED
// shell 上的键是不明确的，而不是 "the command" 的拼写，
// 而 `run` 是一个真正的 agentty 子命令，
// 模型可以合理地发送其他含义的命令（特别是发送给 process_start）。
// 仅添加别名和显示实际发出它的模型的转录本。
inline constexpr std::string_view kCommandAliases[] = {"command", "cmd",
                                                       "shell_command",
                                                       "script", "cmdline"};
inline constexpr std::string_view kDisplayDescription = "display_description";

// 流式传输期间显示的实时内容预览的硬上限。该小部件仅 渲染 `content` 的
// 前 6 行，而型号为 mid-write； re-extracting / re
// -laying-out a multi-KB 正文 8x/sec 是导致大写入
// "feel" 陷入困境的原因，即使 字节 到达也是如此。 4 KiB 覆盖了约 50
// 条宽线 — 远远超过小部件显示的范围 — 以及每个 -tick 工作的范围。
inline constexpr std::size_t kStreamingPreviewCap = 4 * 1024;

// 依次尝试每个别名键，返回从原始 流式传输 缓冲区 中嗅出的
// 第一个字段。 `partial` 选择渐进式嗅探器（容忍
// 未闭合值）而不是严格嗅探器。
[[nodiscard]] inline std::optional<std::string>
sniff_any(const std::string& raw,
          std::span<const std::string_view> keys,
          bool partial) {
    for (auto k : keys) {
        auto v = partial ? agentty::tools::util::sniff_string_progressive(raw, k)
                         : agentty::tools::util::sniff_string(raw, k);
        if (v) return v;
    }
    return std::nullopt;
}

// 在解析的args对象中将别名键重命名为其规范名称IN PLACE：
// 对于对象中存在的第一个别名`canon`（`canon`本身
// 不存在），复制`canon`下的值。原始条目保留下来，
// 因此型号 re-reading 其自己的 调用 仍然可以找到它发送的内容。
// 当任何重命名发生时返回真（调用者使用它到mark_args_dirty）。
[[nodiscard]] inline bool canonify_alias_keys(nlohmann::json& args,
                                              std::string_view canon,
                                              std::span<const std::string_view> keys) {
    if (!args.is_object()) return false;
    bool changed = false;
    for (auto k : keys) {
        if (k == canon) continue;
        auto it = args.find(std::string{k});
        if (it == args.end() || !it->is_string()) continue;
        if (args.contains(std::string{canon})) break;   // 规范胜
        args[std::string{canon}] = *it;                 // 副本：别名条目保持不变
        changed = true;
    }
    return changed;
}

// 规范化给定的built-in工具已知接收到的别名键就位到它们的规范名称中。
// 目前涵盖 `command` 系列（外壳/诊断/测试/process_start）：
// Laguna S 2.1 和型号 cross-trained 在
// OpenAI-style 规格上为外壳发出 `cmd`。
//
// 这些工具现在自己读取一些别名(mcp-cpp ArgReader)，
// 但是卡、权限 提示词和防护读取规范密钥，
// 因此重命名在这里仍然重要。
//
// 当任何内容被重命名时返回真。
[[nodiscard]] inline bool canonify_tool_args(std::string_view tool_name,
                                             nlohmann::json& args) {
    if (tool_name == "shell" || tool_name == "diagnostics"
        || tool_name == "test" || tool_name == "process_start")
        return canonify_alias_keys(args, "command", kCommandAliases);
    return false;
}

// 尝试通过部分 -JSON 闭合器 解析 流式传输 缓冲区。
// 当结果是可解析对象时返回一个对象，否则为 nullopt。
// 严格上比正则表达式嗅探器更强大 - 处理嵌套
// 对象（edits[].old_text）和转义引号 - 但
// 调用者仍然会回退到嗅探器来获取部分关闭器尚未公开的字段
// （e.g。当当前字段的值是稍后才会关闭的部分字符串时）。
[[nodiscard]] inline std::optional<nlohmann::json>
try_parse_partial(const std::string& raw) {
    if (raw.empty()) return std::nullopt;
    try {
        auto closed = agentty::tools::util::close_partial_json(raw);
        auto parsed = nlohmann::json::parse(closed, /*cb=*/nullptr,
                                            /*allow_exceptions=*/false);
        if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

// 第一别名键作为字符串存在于(partial-parsed)对象中。
[[nodiscard]] inline std::optional<std::string>
get_string_any(const nlohmann::json& obj,
               std::span<const std::string_view> keys) {
    for (auto k : keys) {
        auto it = obj.find(std::string{k});
        if (it == obj.end()) continue;
        if (it->is_string()) return it->get<std::string>();
    }
    return std::nullopt;
}

// 截断防护：在 数据流 parses/salvages 工具参数之后，
// 验证目标工具实际需要的最小字段。常见的故障模式是 传输 在
// `display_description` 的结束 `"` 和随后
// 的 `"content":` 之间下降 - close_partial_json
// 然后剥离悬空的 `,` 并生成一个 well-formed 但 content
// -less 对象。运行该工具会默默地生成一个空文件，并且模型将在神秘的
// "content required" 循环上出现 重试。返回第一个缺少
// 的必填字段的名称，如果全部存在（或者该工具没有/未知），则返回 {}。
[[nodiscard]] inline std::string_view
missing_required_field(std::string_view tool_name, const nlohmann::json& args) {
    if (!args.is_object()) return "(args)";
    auto is_nonempty_string_any = [&](std::span<const std::string_view> keys) {
        for (auto k : keys) {
            auto it = args.find(std::string{k});
            if (it == args.end() || !it->is_string()) continue;
            if (!it->get_ref<const std::string&>().empty()) return true;
        }
        return false;
    };
    auto is_nonempty_string = [&](std::string_view key) {
        auto it = args.find(std::string{key});
        return it != args.end() && it->is_string()
            && !it->get_ref<const std::string&>().empty();
    };

    // 通过 spec::Kind 关闭 -set 分派。不在 目录
    // 中的工具（kind_of 返回 nullopt）将被视为
    // "no required fields"，因此未知的未来工具不会被此
    // 防护阻止 - 运行时 分派器 将首先拒绝未知名称并出现 带类型的 错误。
    auto kind = tools::spec::kind_of(tool_name);
    if (!kind) return {};

    using K = tools::spec::Kind;
    switch (*kind) {
        case K::Write:
            if (!is_nonempty_string_any(kPathAliases))    return "path";
            if (!is_nonempty_string_any(kContentAliases)) return "content";
            return {};
        case K::Edit: {
            if (!is_nonempty_string_any(kPathAliases))    return "path";
            auto it = args.find("edits");
            if (it != args.end() && it->is_array() && !it->empty()) return {};
            if (!is_nonempty_string_any(kOldStrAliases))  return "old_string";
            if (!is_nonempty_string_any(kNewStrAliases))  return "new_string";
            return {};
        }
        case K::Bash:
        case K::Diagnostics:
        case K::ProcessStart: {
            if (is_nonempty_string_any(kCommandAliases)) return {};
            // shell还运行作为argv列表发送的命令。
            auto it = args.find("command");
            if (it != args.end() && it->is_array() && !it->empty()) return {};
            return "command";
        }
        case K::ProcessPoll:
        case K::ProcessStop:
            if (!is_nonempty_string("id")) return "id";
            return {};
        case K::Move:
            if (!is_nonempty_string("source")) return "source";
            if (!is_nonempty_string("destination")) return "destination";
            return {};
        case K::Remove:
            if (!is_nonempty_string_any(kPathAliases)) return "path";
            return {};
        case K::Grep:
            // 工具读取相同的拼写（mcp-cpp ArgReader的模式别名）。
            for (const char* k : {"pattern", "query", "q", "regex", "search", "term", "match", "pat"})
                if (is_nonempty_string(k)) return {};
            return "pattern";
        case K::FindDefinition:
            if (!is_nonempty_string("symbol")) return "symbol";
            return {};
        case K::SearchDocs:
            if (!is_nonempty_string("query")) return "query";
            return {};
        case K::WebFetch:
            if (!is_nonempty_string("url")) return "url";
            return {};
        case K::GitCommit:
            if (!is_nonempty_string("message")) return "message";
            return {};
        case K::GitBlame:
            if (!is_nonempty_string_any(kPathAliases)) return "path";
            return {};
        case K::Remember:
            // 记住需要`text` 正文。 `scope` 是可选的。
            if (!is_nonempty_string("text")) return "text";
            return {};
        case K::Forget:
            // 忘记接受`id`或`substring`——该工具
            // 自己的解析器用比该防护更丰富的消息来显示"need one of"错误，
            // 所以不要在这里设门。
            return {};
        case K::Wipe:
            // wipe_memory需要`scope`，
            // 但是该工具自己的解析器产生比该防护更丰富的错误消息。
            // 将门推迟到工具层； `confirm` 和 -step
            // 无论如何都能防止意外擦拭。
            return {};
        case K::Task:
            // 任务需要子代理的`prompt`/`description`。
            if (!is_nonempty_string("prompt")) return "prompt";
            return {};
        case K::Skill:
            // 技能要求技能的`name`为加载。
            if (!is_nonempty_string("name")) return "name";
            return {};
        case K::Read:
            // 没有路径的`read`只会失败，
            // 因此将其视为截断严格来说比让它分派要好。
            //
            // 这个案例曾经属于下面的 no-required 组，认为该工具自己
            // 的 "[invalid args] path required"
            // 保留了 带类型的 ToolError 链。最近挖掘 40 个 线程
            // 驳斥了它：3323 个读取中有 44 个完全以这种方式死亡，
            // 其中 33 个带有参数 `{}`。 带类型的 错误是真实的，
            // 但在这里毫无用处——它会跳过透明的 重试，烧掉转弯，并告诉模型它忘记
            // 了一个参数，而事实是 传输 切割 mid-arguments。
            if (is_nonempty_string_any(kPathAliases)) return {};
            // read 还采用这些（mcp-cpp ArgReader 的路径别名）和
            // paths:[...] 数组。
            for (const char* k : {"file", "filePath", "dir", "directory", "target", "pathname"})
                if (is_nonempty_string(k)) return {};
            for (const char* k : {"paths", "files", "file_paths"})
                if (auto it = args.find(k); it != args.end() && it->is_array() && !it->empty())
                    return {};
            return "path";
        case K::Glob:
            // 相同的推理：模式IS的查询，与没有任何一的glob没有
            // 任何匹配。 （`path` 确实默认为 cwd。）
            if (!is_nonempty_string("pattern")) return "pattern";
            return {};
        // `path` 是很好的-to-have，但对于这些来说不是严格要求的：
        // list_dir默认为cwd，并且git工具使用比该防护
        // 可以产生的更丰富的消息来验证per-action。
        case K::ListDir:
        case K::GitDiff:
        case K::GitLog:
        case K::GitStatus:
        case K::GitShow:
        case K::GitBranch:   // `name`要求是动作-dependent;的
                             // 工具自身的解析器给出了更丰富的错误。
        case K::GitStash:    // 动作-gated; 解析器 验证每-action。
        case K::GitRebase:   // 上游仅对于action=onto需要。
        case K::GitCherryPick: // 仅对于action=pick需要提交。
        case K::Test:
        case K::WebSearch:
        case K::RepoMap:
        case K::SearchCode:
        case K::Todo:
            return {};
    }
    // 无默认值：-切换对 Kind 而言是详尽的，因此这里使用新工具 Kind re
    // -triggers -Wswitch (-Werror 下的构建错误)，
    // 而不是默默地落入 "no required fields"。 DESIGN.md 规则。
    return {};   // 无法接通
}

} // namespace agentty::app::detail
