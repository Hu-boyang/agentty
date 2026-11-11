#pragma once
// agentty::provider::wire-交叉-transport message/prompt帮助者。
//
// 每 传输层 (Anthropic、OpenAI-compat、Ollama、
// ChatGPT/Codex)用于逐字重新-implement的小型、无状态构建块：
// "assistant turn carries tool results"谓词、
// home-dir解析器、所读取的上限文件和CLAUDE.md user/project/local
// 内存-block包装纸。挂在这里，以便修复（新的 内存 层、Windows 路径怪癖、
// 64 KiB 上限）落地一次。 字节 输出受到 per-transport 传输 黄金测试的保护。
//
// 区别于wire.hpp(字节-level SSE取帧，故意域-light):
// 这合法地需要域消息+文件系统。

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/provider.hpp"   // provider::ToolSpec
#include "agentty/util/image_dims.hpp"      // util::image_within_wire_limits
#include "agentty/util/logx.hpp"            // AGT_LOG — 命名已删除的图像
#include "agentty/util/user_root.hpp"      // 单个每-user根(~/.agentty)

namespace agentty::provider::wire {

// 工具的用于`parameters`/`input_schema`槽的JSON
// 模式定义，受到保护：声明没有模式定义的工具仍然需要在传输上有效的空-object
// 模式定义，或者严格的后端（OpenAI响应，一些代理）400。一个保护，
// 由每个OpenAI-family 工具编码器，因此行为不会漂移。
[[nodiscard]] inline nlohmann::json tool_schema_or_empty(const nlohmann::json& schema) {
    if (schema.is_null())
        return nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
    return schema;
}

// OpenAI*聊天完成*`tools`阵列形状：
//   [{"type":"function","function":{名称，描述，
// 参数}}]由 OpenAI-compat 传输层 和 和 Ollama 本机 传输层
// 逐字共享（它们是 字节-identical 副本）。响应 传输 使用 FLATTER
// 形状并保留其自己的编码器，但重复使用上面的 tool_schema_or_empty()，
// 因此 null-schema 保护是 single-source。
[[nodiscard]] inline nlohmann::json openai_chat_tools(
    const std::vector<provider::ToolSpec>& tools) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tools) {
        if (t.defer_loading) continue;   // Anthropic-only概念；从来没有在这里
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name", t.name},
                {"description", t.description},
                {"parameters", tool_schema_or_empty(t.input_schema)},
            }},
        });
    }
    return arr;
}

// 每当辅助消息携带ANY tool_calls时为真。 Anthropic（和
// OpenAI-shaped 线）要求每个 tool_use/tool_call
// 与以下消息中的 tool_result 配对，无论 终端 与否 — 发送 调用
// 而无需配对 400，并楔入重播的转录本。因此，这推动了每个 传输层 的
// "emit the follow-up tool-result turn" 决策。
[[nodiscard]] inline bool is_assistant_with_results(const Message& m) noexcept {
    return m.role == Role::Assistant && !m.tool_calls.empty();
}

// ── 传输 IMAGE 政策——真相的唯一来源 ──────────────────────────
//
// 每个 传输 方言（Anthropic 消息、OpenAI 响应、
// OpenAI-chat/NDJSON)都以其 自身 JSON 形状对图像进行编码——该差异是
// 合法的并且在 -dialect 中保持不变。 不 必须分歧的是 字节 周围的 POLICY：
// message/tool贡献的图像、空-bytes跳过以及媒体-type默认。
// 这些决定（糟糕地）以三个文件中的 copy-pasted 行的形式存在，
// 这正是 tool-result 图像在 Anthropic 传输
// 上传送的方式，但默默地不是其他两个。这些助手曾经就是那个政策。
//
//   wire_media_type — 要发出的 media_type ('' → 安全 image/png
//   默认值) wire_message_images — USER 消息贡献的图像
//   wire_tool_result_images — COMPLETED 工具贡献的图像
//                             (读取图像文件 → ToolUse::Done.images)
//
// 指针（从不复制），因此 multi-MiB 屏幕截图每回合不会重复。
// 忘记工具-result图像的方言现在是VISIBLE遗漏（它不是调用
// wire_tool_result_images）而不是无声的漂移。

[[nodiscard]] inline std::string_view wire_media_type(const ImageContent& img) noexcept {
    return img.media_type.empty() ? std::string_view{"image/png"}
                                  : std::string_view{img.media_type};
}

// 正确，当且仅当图像具有真实的字节 AND符合传输的像素-dimension限制。
// 一个空的-bytes ImageContent（泄漏到线程中的一个已耗尽的草稿附件）
// 序列化为一个空的base64 "data"，整个请求需要400秒；超大图像 (>
// 2000 px/side) 400 秒 MANY-image 以相同方式请求 (
// "image dimensions exceed max allowed size")。（图像尺寸超过允许的最大值。）
// 两者都从这里的 传输 中删除——每种方言的图像选择器都通过 一个 门——所以一个坏
// 图像不能杀死否则的 -valid 回合。 48 KB 的屏幕截图仍然可以是 3000
// + px 宽，因此文件大小并不能代表； image_dimensions() 从
// 头文件 字节 读取真实像素。
[[nodiscard]] inline bool wire_image_sendable(
    const ImageContent& img, unsigned max_side = util::kMaxWireImageSide) noexcept {
    // 在此处丢弃图像是通过构造实现的INVISIBLE：散文标记（
    // "[image: <paste>]")仍然熄灭，因此模型被告知
    // 图像存在并且没有显示任何内容，并且用户看到一个默默地忽略他们的
    // 屏幕截图的转弯。每次删除都指定一次原因，以便日志回答
    // "why didn't it see my image"，而不会平分管道。
    if (img.bytes().empty()) {
        AGT_LOG(Wire, Warn, "wire.image_dropped",
                "reason=empty_bytes media_type={}", wire_media_type(img));
        return false;
    }
    // 每个提供商都准确地接受 png/jpeg/gif/webp.
    // 任何其他内容（macOS 剪贴板 TIFF、HEIC）400 秒的 整个
    // 请求 - 并且由于该图像存在于历史中，因此每一次轮回都会楔入 线程。
    {
        const auto mt = wire_media_type(img);
        if (mt != "image/png" && mt != "image/jpeg"
            && mt != "image/gif" && mt != "image/webp") {
            AGT_LOG(Wire, Warn, "wire.image_dropped",
                    "reason=unsupported_media_type media_type={}", mt);
            return false;
        }
    }
    if (!util::image_within_wire_limits(img.bytes(), max_side)) {
        const auto d = util::image_dimensions(img.bytes());
        AGT_LOG(Wire, Warn, "wire.image_dropped",
                "reason=oversize dims={}x{} max_side={} bytes={}",
                d.w, d.h, max_side, img.bytes().size());
        return false;
    }
    return true;
}

// 整个对话将放在传输上的图像块的数量，以CANDIDATE术语表示（非
// -empty 字节，在尺寸门之前）。这是 util::wire_max_side
// 的输入，因此每种方言都以相同的方式导出其 per-request 上限，
// 而不是各自发明一个计数。图像块的两个来源都包含在内 - user
// -turn 图像 和 tool_result 图像 - 因为提供商
// 的 Many-image 限制在请求中计算 BLOCKS，
// 而不仅仅是用户粘贴的图像。通过工具读取二十个屏幕截图的会话与粘贴二十个
// 屏幕截图的会话达到了相同的上限。
[[nodiscard]] inline std::size_t
wire_image_count(const std::vector<Message>& msgs) noexcept {
    std::size_t n = 0;
    for (const auto& m : msgs) {
        if (m.role == Role::User)
            for (const auto& img : m.images)
                if (!img.bytes().empty()) ++n;
        for (const auto& tc : m.tool_calls)
            for (const auto& img : tc.done_images())
                if (!img.bytes().empty()) ++n;
    }
    return n;
}

// USER消息贡献给传输（跳过空）的图像。
[[nodiscard]] inline std::vector<const ImageContent*>
wire_message_images(const Message& m, unsigned max_side = util::kMaxWireImageSide) {
    std::vector<const ImageContent*> out;
    if (m.role != Role::User) return out;
    for (const auto& img : m.images)
        if (wire_image_sendable(img, max_side)) out.push_back(&img);
    return out;
}

// COMPLETED工具为其tool_result（跳过空）
// 贡献的图像。仅当工具显示图片（在图像上读取）时才为非-empty。
[[nodiscard]] inline std::vector<const ImageContent*>
wire_tool_result_images(const ToolUse& tc, unsigned max_side = util::kMaxWireImageSide) {
    std::vector<const ImageContent*> out;
    for (const auto& img : tc.done_images())
        if (wire_image_sendable(img, max_side)) out.push_back(&img);
    return out;
}

// tool_search结果(#98)。该工具每次命中都会写入一行 `loaded: <name>` 行；
// 在 Anthropic 传输 上，这些变成 tool_reference 块，
// 其余文本是注释。与tools::plan::kLoadedMarker保持同步。
inline constexpr std::string_view kToolSearchName = "tool_search";
inline constexpr std::string_view kToolSearchLoaded = "loaded: ";

[[nodiscard]] inline std::vector<std::string>
tool_search_references(const ToolUse& tc) {
    std::vector<std::string> out;
    if (tc.name.value != kToolSearchName || !tc.is_done()) return out;
    std::string_view text = tc.output();
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        if (line.starts_with(kToolSearchLoaded)) {
            line.remove_prefix(kToolSearchLoaded.size());
            if (const auto sp = line.find(' '); sp != std::string_view::npos)
                line = line.substr(0, sp);
            if (!line.empty()) out.emplace_back(line);
        }
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return out;
}

// 正确，当且仅当消息具有至少一个可发送的USER图像——每种方言
// 用于将-code打开为`has_images`循环的消息发射门。
[[nodiscard]] inline bool has_wire_message_image(
    const Message& m, unsigned max_side = util::kMaxWireImageSide) noexcept {
    if (m.role != Role::User) return false;
    for (const auto& img : m.images)
        if (wire_image_sendable(img, max_side)) return true;
    return false;
}

// 用户主目录，可移植。 HOME (POSIX) 第一，USERPROFILE (
// Windows) 第二；当两者均未设置时为空路径。
[[nodiscard]] inline std::filesystem::path home_dir() noexcept {
    if (const char* h = std::getenv("HOME"); h && *h)
        return std::filesystem::path{h};
#if defined(_WIN32)
    if (const char* h = std::getenv("USERPROFILE"); h && *h)
        return std::filesystem::path{h};
#endif
    return {};
}

// 读取文本文件，吞掉任何 I/O 错误（丢失/不可读 → 空）。
// 被截断为`cap` 字节，因此一个流氓multi-MB
// CLAUDE.md不能在每一回合毒害系统提示词。尾随空白被修剪，
// 因此包装标签永远不会被空行堵塞。
//
// 返回的文本中截断的文件SAYS SO。这比其他任何地方都更重要，
// 代码库 限制了读取：AGENTS.md 是 INSTRUCTIONS，
// 读取器就是型号。在 64 KiB 处默默切割意味着在底部附近写了
// 一条规则 - "never delete files without asking"
// 正是人们放在最后的那种东西 - 根本没有到达模型，
// 而作者看着他们文件的顶部采取了 副作用 并合理地得出了
// 整个事情的结论。
//
// 标记被寻址到模型而不是被记录，因为模型是需要
// 它的人：它是被要求遵循它仅具有一部分的指令的模型，
// 并且当丢失的部分重要时它可以这样说。
// 日志行在重要的时刻不会被任何人阅读。
[[nodiscard]] inline std::string read_capped_file(
    const std::filesystem::path& p, std::size_t cap = 64u * 1024u) {
    std::error_code ec;
    if (p.empty() || !std::filesystem::is_regular_file(p, ec) || ec) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
    const bool truncated = s.size() > cap;
    if (truncated) s.resize(cap);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'
                          || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    if (truncated)
        s += "\n\n[truncated at " + std::to_string(cap / 1024)
           + " KB — the rest of this file was NOT read]";
    return s;
}

// 解析全局AGENTS.md——适用于所有项目的用户-level文件，
// 类似于Codex的~/.codex/AGENTS.md和OpenCode的~/.config/opencode/AGENTS.md.。
// 不是已发布的agents.md规范的一部分（
// 仅是项目-scoped），而是主要工具实现的
// 实用扩展。
//
// 按照优先顺序对两个候选进行检查；第一个非 -empty 文件
// 获胜：
//   1.~/.agentty/AGENTS.md —agentty-specific全球指导
//   2.~/.agents/AGENTS.md-任何代理工具的共享全球指南
//   3. ~/.codex/AGENTS.md — Codex的全局文件($CODEX_HOME如果设置的话)，所以
//                              Codex用户的规则继承为-is
//
// 返回第一个现有的非-empty文件的路径，或者当不存在
// 候选时返回空路径。
[[nodiscard]] inline std::filesystem::path resolve_global_agents_md() noexcept {
    const auto home = home_dir();
    const auto root = ::agentty::util::user_root();
    std::filesystem::path codex;
    if (const char* c = std::getenv("CODEX_HOME"); c && *c) codex = c;
    else if (!home.empty()) codex = home / ".codex";

    const std::filesystem::path candidates[] = {
        root.empty() ? std::filesystem::path{} : root / "AGENTS.md",
        home.empty() ? std::filesystem::path{} : home / ".agents" / "AGENTS.md",
        codex.empty() ? std::filesystem::path{} : codex / "AGENTS.md",
    };
    for (const auto& p : candidates) {
        if (p.empty()) continue;
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec) && !ec) {
            // Non-empty通过read_capped_file检查（比stat+open两次便宜）。
            if (!read_capped_file(p).empty())
                return p;
        }
    }
    return {};
}

// CLAUDE.md层所在的位置，匹配Claude代码：
//   用户~/.claude/CLAUDE.md ($CLAUDE_CONFIG_DIR如果设置）；
//           ~/CLAUDE.md对于较旧的设置项目，当缺少该内容时仍会读取./CLAUDE.md，
//   和./.claude/CLAUDE.md（两者均按此顺序）
//   本地./CLAUDE.local.md
[[nodiscard]] inline std::filesystem::path resolve_user_claude_md() noexcept {
    const auto home = home_dir();
    std::filesystem::path dir;
    if (const char* c = std::getenv("CLAUDE_CONFIG_DIR"); c && *c) dir = c;
    else if (!home.empty()) dir = home / ".claude";
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_regular_file(dir / "CLAUDE.md", ec))
        return dir / "CLAUDE.md";
    return home.empty() ? std::filesystem::path{} : home / "CLAUDE.md";
}
inline constexpr const char* kProjectClaudeMd[] = {"CLAUDE.md", ".claude/CLAUDE.md"};

// 将两个工程文件加入；两者都可以为空。
[[nodiscard]] inline std::string join_project_claude_md(std::string a, std::string b) {
    if (a.empty()) return b;
    if (!b.empty()) a += "\n\n" + b;
    return a;
}

// CLAUDE.md 内存层次结构-本地-model 提示词共享的"lite"
// 包装器(仅user/project/local层，没有学习-memory/技能)。
// 路径如上。 Empty/missing 等级已被省略； all-empty
// 返回 ""。介绍行是一个参数，因此调用者可以保持其准确的措辞（字节
// -identity 很重要 - 这会在 缓存 断点后面为系统 提示词
// 提供数据）。
[[nodiscard]] inline std::string claude_md_blocks(std::string_view intro) {
    const auto user_path      = resolve_user_claude_md();
    const std::string user    = user_path.empty() ? std::string{} : read_capped_file(user_path);
    const std::string project = join_project_claude_md(
        read_capped_file(std::filesystem::path{kProjectClaudeMd[0]}),
        read_capped_file(std::filesystem::path{kProjectClaudeMd[1]}));
    const std::string local   = read_capped_file(std::filesystem::path{"CLAUDE.local.md"});
    if (user.empty() && project.empty() && local.empty()) return {};

    std::string m = "\n\n<memory>\n";
    m += intro;
    m += "\n";
    if (!user.empty())    m += "<user-memory>\n"    + user    + "\n</user-memory>\n";
    if (!project.empty()) m += "<project-memory>\n" + project + "\n</project-memory>\n";
    if (!local.empty())   m += "<local-memory>\n"   + local   + "\n</local-memory>\n";
    m += "</memory>";
    return m;
}

// AGENTS.md —项目-scoped代理指导的开放标准，由Linux基金会下的
// Agentic AI基金会(AAIF)管理。请参阅 https://agents.md.
// 已发布的规范仅限于项目-scoped（无用户层，无本地层）。然而，像 Codex
// (~/.codex/AGENTS.md) 和 OpenCode (
// ~/.config/opencode/AGENTS.md) 这样的主要工具实现了
// global/user 范围作为实用的扩展。 agentty 也是如此：当
// `global_path` 是非 -empty（由调用者通过 wire::resolve_global_agents_md
// () 解析）时，其内容在单独的 <agents-md-global> 块 之前
// 和项目-level <agents-md> 块中发出，因此项目指导会覆盖全局。
//
// 该规范还描述了子包的嵌套 monorepo 文件：
// 在每个包中放置 AGENTS.md，代理自动读取目录树中最近的文件，以距离最近者为准。
// 当 `search_from` 是 `workspace_root`
// 的子目录（i.e。代理的 cwd 在包内）时，我们向上寻找第二个
// AGENTS.md，它*不是*根文件。如果找到，
// 其内容将在单独的 <agents-md-package>
// 块中发出，以便模型可以区分 root-level 指导和 package
// -specific 指导并正确应用优先级（最近的获胜）。
// 步行在 workspace_root 停止 - 永远不会逃脱
// 工作区 边界。
//
// `workspace_root`被传入（而不是在这里解析），因此该助手保持
// self-contained:msg_shared.hpp NOT拉入util/fs_helpers机械，
// 使传输层脱离ToolError/registry表面。
// 调用者(provider/prompt.cpp、openai/transport.cpp、
// ollama/transport.cpp)已经链接 util并从那里解析workspace_root()/project_root()。
//
// 当 全部 层（全局+根）是 missing/empty 时，返回 ""，
// 因此调用者省略该块而不发出空包装标签。与 CLAUDE.md 相同的 64 KiB 帽
// + 尾部 -whitespace 装饰，通过共享 wire::read_capped_file。
//
// 传输形状(优先级低→高)：
//   <agents-md-global>…</agents-md-global>(可选，
//   ~/.agentty/AGENTS.md) <agents-md>…</agents-md>(根，
//   <workspace_root>/AGENTS.md)
// <agents-md-package>…</agents-md-package>(可选，
// 最近嵌套) 当最近的AGENTS.md是根文件(相同规范路径)时，
// 跳过嵌套块以避免重复。保持标准化公共项目指南在视觉上与个人 CLAUDE.md
// 笔记不同，可以让模型区分它们并正确应用优先级。
//
// `read_file`是用于EVERY层（全局、根、嵌套）的文件-reader。
// 它默认为 uncapped-cache read_capped_file (对于很少
// 重建的 local-model 提示词 来说很好)。 Anthropic 路径
// （每次都会重建系统 提示词）会注入其 mtime-cached 读取器，
// 因此 per-turn 的成本是一些 stat() + 缓存的 正文 的 memcpy，
// 而不是 N 个完整文件读取。无论哪种方式，读者都必须应用 64 KiB 上限和尾部
// -whitespace 修剪，以便跨提供商保持 字节-identity。
using agents_md_reader = std::string (*)(const std::filesystem::path&);

// 默认读取器：read_capped_file具有第二个（默认）cap参数，
// 因此其地址是不会绑定到agents_md_reader的2-arg函数指针。
// 这个单一-arg转发器确实——它应用了标准的64KiB上限。
[[nodiscard]] inline std::string read_capped_default(
    const std::filesystem::path& p) {
    return read_capped_file(p);
}

[[nodiscard]] inline std::string agents_md_block(
    std::string_view               intro,
    const std::filesystem::path&   workspace_root,
    const std::filesystem::path&   search_from = {},
    const std::filesystem::path&   global_path = {},
    agents_md_reader               read_file   = &read_capped_default) {
    // ── 全球范围(用户-level,适用于所有项目) ──
    const std::string global_content =
        global_path.empty() ? std::string{} : read_file(global_path);

    const std::string content = read_file(workspace_root / "AGENTS.md");
    if (content.empty() && global_content.empty()) return {};

    std::string m;

    // 全局块优先（最低优先级）。
    if (!global_content.empty()) {
        m += "\n\n<agents-md-global>\n";
        m += "Global guidance from ~/.agentty/AGENTS.md (or ~/.agents/AGENTS.md). "
             "Applies across all projects; overridden by project-level guidance "
             "below.\n";
        m += global_content;
        m += "\n</agents-md-global>";
    }

    // 根项目块。
    if (!content.empty()) {
        m += "\n\n<agents-md>\n";
        m += intro;
        m += "\n";
        m += content;
        m += "\n</agents-md>";
    }

    // ── 巢穴单列坡步行:在workspace_root下方找到最近的AGENTS.md ──
    // 从`search_from`向上走(通常代理的cwd通过
    // project_root()夹在工作区内)寻找作为根文件的NOT的AGENTS.md。
    // 在 workspace_root 处停止 — 永远不会逃出
    // 工作区 边界。
    if (!search_from.empty()) {
        std::error_code ec;
        auto root_canon = std::filesystem::weakly_canonical(workspace_root, ec);
        if (ec) root_canon = workspace_root;
        auto root_agents = root_canon / "AGENTS.md";

        auto dir = std::filesystem::weakly_canonical(search_from, ec);
        if (ec) dir = search_from;

        while (dir.has_parent_path()
           && dir != root_canon
           && std::filesystem::is_directory(dir, ec) && !ec) {
            auto candidate = dir / "AGENTS.md";
            auto candidate_canon = std::filesystem::weakly_canonical(candidate, ec);
            if (!ec && std::filesystem::is_regular_file(candidate, ec) && !ec
                && candidate_canon != root_agents) {
                const std::string nested = read_file(candidate);
                if (!nested.empty()) {
                    m += "\n\n<agents-md-package>\n";
                    m += "Package-specific guidance from the nearest AGENTS.md "
                         "(https://agents.md). The closest AGENTS.md to the "
                         "working directory wins; treat this as overriding the "
                         "root-level guidance above where they conflict.\n";
                    m += nested;
                    m += "\n</agents-md-package>";
                }
                break;  // 最近发现——停止行走
            }
            dir = dir.parent_path();
        }
    }

    return m;
}

} // namespace agentty::provider::wire
