// attribution_discipline_test ——解码器可能不会发明工具. 调用 主人的吗?
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
// 当流出的块不说哪个工具时 调用 属于它,
// 解码器必须 FAIL 而不是选择一个。
//
// Picking 生产有效的 JSON. 将 调用 的 字
// 节 附加到另一个 解析 , 传递 模式定义 验证和发送
// —— 所以 `edit` 运行的路径属于 `shell`,
// 并且没有在它写完之前看似不对。 失败产生一个明显的断掉的转弯。
// 严格来说, 更好, 并且这是解码器错误时唯一保持
// 诚实的结果。
//
// ── 原因 A SOURCE SCAN ────────────────────────────────────────────────────
//
// 我们把这个错误运送了四次, 穿过两个解码器( 请参看 )
// docs/TOOL_CALL_ATTRIBUTION.md。最后一个幸存下来的审查,
// 因为 回退 曾经是 NAMED 说得好: `latest_tool_item` 诵读如意,
// `*open_tool_items.begin()` 打开一个 unordered_set
// — "latest" 指无论大麻是哪一种
//
// 行为测试无法抓住下一个 因为下一个将是新的 回退
// 在一个尚未存在的解码器中添加一个新名称。
// CAN 被抓住是,我们 SHAPE : 引用未
// 订购容器的第一个要素,以表明身份。
//
// 扫描提供方的形状和方式 theme_discipline_test
// 扫描颜色字面。这是一个粗网, 假阳性的成本是 1。
// `// attribution-ok:` 注释解释
// 为什么一个案件是安全的,而丢失的代价是随另一个案件
// 运行的工具 调用 争吵

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "agtest.hpp"

namespace fs = std::filesystem;

namespace {

// 阅读未订购容器的第一个元素是第-undefined号订单.
// 使用它作为ANSWER("which call is this?")是虫子;
// 使用它来排出一组是细的,所以扫描会寻找调试形状,
// 让排水点自己给它做注释.
[[nodiscard]] bool looks_like_arbitrary_pick(const std::string& line) {
    // 便宜的预过滤器首先:下面的regex是昂贵的部分,
    // 绝大多数的线条都不含这些内容.
    const bool candidate = line.find(".begin()") != std::string::npos
                        || line.find(".back()")  != std::string::npos
                        || line.find(".front()") != std::string::npos;
    if (!candidate) return false;
    // 跳过评论。 此文件本身的规则在它所守护的解码器
    // 中用旁白来讨论, 并标出一个错误的解释,
    // 因为它会训练人们忽略扫描仪。
    const auto first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line.compare(first, 2, "//") == 0)
        return false;

    // `*thing.begin()`——无序容器的任意元素.
    static const std::regex deref_begin{
        R"(\*\s*[A-Za-z_][A-Za-z0-9_.>-]*\.begin\(\))"};
    if (std::regex_search(line, deref_begin)) return true;

    // calls.back() / tool_slots.front() 表示最近一次调用。
    //
    // 匹配于( M) COLLECTION-OF-CALLS 名字,
    // 没有包含 "tool". back()/front() 弦乐很普通,
    // 缓冲区 财务报告和已审计财务报表 JSON 数组 -
    // `tools_j.back()["cache_control"]` 正在构建请求 正文 而不是
    // 选择一个主人 和一个向那些开火的扫描仪 被关闭, 这比没有一个更糟糕。
    //
    // 类型层已经删除了INDEXING拼写(
    // ToolCallTracker::all()不是随机的-access),
    // 因此这只需要覆盖残片:可能通过两层的-sounding
    // "latest"选择。
    static const std::regex ends_pick{
        R"(\b(calls|tool_calls|tool_slots|slots|open_tools|open_tool_items)\s*\.(back|front)\(\))",
        std::regex::icase};
    return std::regex_search(line, ends_pick);
}

// 明确且经过审查的豁免。 评论必须写为WHY,
// 以便下一个读者可以判断而不是相信它。
[[nodiscard]] bool exempted(const std::string& line) {
    return line.find("attribution-ok:") != std::string::npos;
}

}  // 命名空间结束。

TEST_CASE("attribution: no decoder picks an arbitrary call as an owner") {
    const fs::path root{AGENTTY_SRC_ROOT};
    const fs::path dir = root / "src" / "provider";
    REQUIRE(fs::exists(dir));

    std::vector<std::string> offenders;
    int scanned = 0;

    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;

        std::ifstream in(e.path());
        REQUIRE(in);
        ++scanned;

        std::string line;
        std::string prev;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            if (!looks_like_arbitrary_pick(line)) { prev = line; continue; }
            // 标点符可能坐落在线本身或上面的线上:
            // 真正的解释通常是一句,
            // 而后继的评论,长读比前一句更糟糕。
            if (exempted(line) || exempted(prev)) { prev = line; continue; }
            offenders.push_back(
                fs::relative(e.path(), root).generic_string() + ":"
                + std::to_string(lineno) + "  " + line);
            prev = line;
        }
    }

    // 扫描过的东西,或者测试是空虚的绿色。
    CHECK(scanned > 5);

    for (const auto& o : offenders) MESSAGE(o);
    CHECK_MESSAGE(offenders.empty(),
        "a provider decoder dereferences the first element of an unordered "
        "container. If that value answers \"which tool call do these bytes "
        "belong to?\", it is a coin flip that corrupts one call with "
        "another's arguments — and the result still parses, so nothing "
        "notices until a tool runs. Return an explicit failure instead (see "
        "wire::ToolCallTracker / AttributionError::Ambiguous). If the site "
        "is genuinely safe — draining a set, or a single-element container "
        "checked above — append `// attribution-ok: <why>`.");
}

TEST_CASE("attribution: every transport states how it attributes") {
    // 上面的扫描会捕获任意选择的解码器。它无法捕获从未面对过问题的解码器,
    // 因为它会保留 一个 时间档和路线,那里的一切——这就是
    // Anthropic SSE 解析器 数年未完成(
    // `current_tool_id` ,每个 input_json_delta
    // 发送到最后一个打开的区块中。
    //
    // 那个密码是安全的,但是安全 BY LUCK: Claude
    // 一次发出一个内容块,所以 "the last one opened"
    // 碰巧是对的,对服务器的排气订单下注不是财产,输的一方是一
    // 调用 附加在他人的论据中——仍然有效 JSON ,默默发送,
    // 错误的工具。
    //
    // 所以每个 传输层 必须 NAME 其策略,
    // 名称必须是真正的机制。
    //
    //   键入 —— 以 传输 身份键入的每 -call 位值 (Chat's
    //                wire::ToolCallTracker, Response'
    //                output_index + OpenCalls,
    //   Anthropic 的内容 -block 指数)原子 —— 协议在一个 帧 中发送了 调用 整条 ,
    //                因此 Start/Delta/End 被放回 -to-back ,
    //                两个 调用 从未在飞行 (Ollama NDJSON XX message.tool_calls
    //                及其救生路径)
    //
    // A 传输层 与两者的形状 总是回来。
    struct Transport {
        const char* path;
        const char* evidence;   // 证明策略的标志
        const char* why;
    };
    const Transport kTransports[] = {
        {"openai/transport.cpp",   "ctx.tools.attribute",
         "Chat Completions interleaves parallel calls; keyed on (id, index) "
         "jointly via wire::ToolCallTracker, which returns Ambiguous rather "
         "than guessing."},
        {"responses/codec.cpp",    "addressed_item",
         "Responses addresses frames by output_index (a proxy cannot rewrite "
         "it without renumbering output[]), then a validated item_id, then "
         "OpenCalls::sole() which answers only when there is exactly one."},
        {"anthropic/sse.cpp",     "tool_at",
         "Anthropic puts an index on every content_block frame; that index "
         "is the block's identity for start, every delta, and stop."},
        {"ollama/transport.cpp",  "StreamToolUseEnd",
         "Ollama's NDJSON carries whole calls in one frame, so each is "
         "emitted Start->Delta->End atomically and nothing is ever in "
         "flight to misattribute."},
    };

    const fs::path root{AGENTTY_SRC_ROOT};
    for (const auto& t : kTransports) {
        const auto path = root / "src" / "provider" / t.path;
        INFO("transport = " << t.path);
        REQUIRE(fs::exists(path));

        std::ifstream in(path);
        REQUIRE(in);
        const std::string src((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());

        CHECK_MESSAGE(src.find(t.evidence) != std::string::npos,
            t.path << " no longer contains `" << t.evidence
                   << "`, the mechanism its attribution rests on. " << t.why
                   << " If the strategy changed, update this table with the "
                      "new mechanism — do not delete the row. A transport "
                      "with no named strategy is one that routes bytes "
                      "somewhere plausible, and plausible corruption still "
                      "parses.");
    }
}

TEST_CASE("attribution: no transport keeps a single current-call slot") {
    // 拼写方式 Anthropic 臭虫, 名字禁止使用。
    //
    // `current_tool_id` / `active_tool` /
    // `last_tool` 被读作簿记,事实上是一个归属决定: 每个三角洲
    // 都属于那个。 调用。此文件中的扫描仪不会 标志 已经没有了
    // `*begin()` ,没有 `.back()` 没有什么像一个小插曲,
    // 它看起来像一个变数
    static constexpr std::string_view kBanned[] = {
        "current_tool_id", "current_tool_name",
        "active_tool_id",  "last_tool_id",
        "latest_tool_item",
    };

    const fs::path root{AGENTTY_SRC_ROOT};
    const fs::path dir = root / "src" / "provider";
    REQUIRE(fs::exists(dir));

    std::vector<std::string> offenders;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;

        std::ifstream in(e.path());
        REQUIRE(in);
        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            // 解释禁令的评论不是违反禁令的——
            // 解码员记录了这一规则,并标出解释来
            // 训练人们忽略扫描仪.
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos
                && line.compare(first, 2, "//") == 0) continue;
            if (exempted(line)) continue;
            for (auto banned : kBanned) {
                if (line.find(banned) == std::string::npos) continue;
                offenders.push_back(
                    fs::relative(e.path(), root).generic_string() + ":"
                    + std::to_string(lineno) + "  " + line);
            }
        }
    }

    for (const auto& o : offenders) MESSAGE(o);
    CHECK_MESSAGE(offenders.empty(),
        "a provider decoder keeps a single 'current tool' slot. That is an "
        "attribution decision wearing the clothes of bookkeeping: every "
        "delta goes to that one call, so the moment the server interleaves "
        "two, one call's arguments land on the other. It parses, it "
        "dispatches, and nothing notices. Key on the identity the wire "
        "already provides (content_block index, tool_calls[].index, "
        "output_index) and resolve through ONE named lookup.");
}

TEST_CASE("attribution: the scanner recognises the shape it exists for") {
    // 一个不能失败的纪律测试是装饰。 这些是两个
    // 错误的精确线条, 所以未来的匹配者必须不断重
    // 写它们。
    CHECK(looks_like_arbitrary_pick(
        "        ctx.latest_tool_item = *ctx.open_tool_items.begin();"));
    CHECK(looks_like_arbitrary_pick(
        "    return *calls.begin();"));
    CHECK(looks_like_arbitrary_pick(
        "  auto id = * open_items.begin() ;"));

    // 并且不要向普通的重复开火, 即最 `.begin()` 使用时。
    CHECK(!looks_like_arbitrary_pick(
        "    for (auto it = m.begin(); it != m.end(); ++it) {"));
    CHECK(!looks_like_arbitrary_pick(
        "    std::vector<std::string> ids(s.begin(), s.end());"));
    CHECK(!looks_like_arbitrary_pick(
        "    std::sort(v.begin(), v.end());"));
    // 也有关描述bug的流言——解码器在评论中解释
    // 这个规则,并标出解释的标志来训练人们忽略
    // 扫描仪.
    CHECK(!looks_like_arbitrary_pick(
        "// it was refreshed from `*open_tool_items.begin()` on a set,"));

    // ── TYPE 无法清除的残留物 ──
    //
    // ToolCallTracker::all ()故意的不是随机的 -access ,
    // 所以,我说 `[i]` 是一个 编译 错误。 back() 仍然合法,
    // 在双向范围内, "the most recent call" 完全可以证明
    // -sounding 拼写,否则会滑过两层。
    CHECK(looks_like_arbitrary_pick("    auto& c = tracker.calls.back();"));
    CHECK(looks_like_arbitrary_pick("    return calls.back().id;"));
    CHECK(looks_like_arbitrary_pick("    auto& s = tool_slots.front();"));

    // 不过 不 常规用途的 back()/front() ,
    // 这些在任何地方的字符串和 缓冲区 扫描仪向那些
    // 开火时会关机, 更糟糕的是没有。
    CHECK(!looks_like_arbitrary_pick("    if (buf.back() == '\\n') buf.pop_back();"));
    CHECK(!looks_like_arbitrary_pick("    if (sv.front() != '{') return false;"));
    CHECK(!looks_like_arbitrary_pick("    tools_j.back()[\"cache_control\"] = x;"));

    // 豁免是受尊重的,并且只有标记。
    CHECK(exempted("  auto id = *s.begin();  // attribution-ok: size()==1 above"));
    CHECK(!exempted("  auto id = *s.begin();  // fine, probably"));
}
