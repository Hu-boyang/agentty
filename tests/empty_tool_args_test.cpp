// empty_tool_args_test — "the model sent no arguments" 不是,是"我们",不是"我们"
// "the arguments failed to parse".（参数解析失败。）
//
// ── 本测试固定验证的缺陷。 ────────────────────────────────────────────────────
//
// 一种所有参数都是可选的工具—— `git_status`, `list_dir`,
// `repo_map` ——合法地称为没有争论。 OpenAI
// -compatible 传输 说 调用 以"一"字为名 tool_calls
// [] 条目 `function.arguments` 缺席, `""` ,或 ,
// 或 `"{}"` : 0 参数三角体到达解码器,所以累积 缓冲区 结尾处为空 调用.
//
// 两种代理循环,然后决定什么 `ToolUse::args` 坚持住,主力 状态更新函数
// 种子 `json::object()` 时间 StreamToolUseStart ,
// 所以一个空 缓冲区 保留有效 `{}` 和工具运行。子代理环没有种子任何,
// 所以 `args` 留在Nlohmann的默认... `null` ——和 分派器 读作无效
// `args` 作为其 解析-failure 信号 :
//
//     "tool args failed to parse — re-emit the call with complete, valid JSON arguments"（工具参数解析失败，应以完整、合法的 JSON 参数重新发出调用。）
//
//
// 信息回到模型中作为一个 tool_result。模式。
// -emits 同样的无争议 调用 ,得到同样的错误, 并燃烧它的
// 转动预算在一个 调用 第一次是没错 日志上没写
// "argumentless" ; 它读作不能产生有效的模型 JSON.
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
//     ABSENT 参数是一个空对象.
//     仅a FAILED 解析 可能使参数无效。
//
// 无效的哨兵必须保持完全的一件事的意义,
// 因为分派器的错误消息是在它确实存在这个假设的前提下写出来的.
//
// ── 原因 A SOURCE SCAN, 不 A BEHAVIOURAL TEST ────────────────────────────
//
// 行为版本需要完全的子代理转折:一个提供者,数据流,
// 分派器,一个工具注册表. 该测试今天会通过,
// 不会注意到THIRD循环——ACP适配器,无头的`run`
// 路径——重复同样的省略,因为它只练习了存在的两个.
//
// 形状就是重现的:一个建立 ToolUse 调自 a
// StreamToolUseStart 永远不要种子 `args` 因此,
// 这个断言: 不变量 在每个这样的地点, 道路 attribution_discipline_test
// 声称没有解码器选择工具 调用 主人专横地。

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 从测试二进制程序的CWD开始走起来寻找仓库根.
fs::path repo_root() {
    auto dir = fs::current_path();
    for (int up = 0; up < 6; ++up) {
        if (fs::exists(dir / "src" / "provider") && fs::exists(dir / "tests"))
            return dir;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

// 每个网站 按需加载 a ToolUse 调自 a 数据流 '工具' 工具 -use START.
// 如果出现新的代理环路,请在此添加——列表就是点.
const char* kToolUseConstructionSites[] = {
    "src/runtime/app/update/stream.cpp",   // 主体 状态更新函数
    "src/tool/mcp_tools_backends.cpp",     // 子代理环
};

}  // 命名空间结束。

TEST_CASE("empty tool args: nlohmann's default really is null") {
    // 整个bug 都基于这个。 如果 nlohmann
    // 默认为空对象, 种子将是多余的, 而 分派器 的无效
    // 检查是死代码 —— 所以请说明这个假设而不是依赖它。
    agentty::ToolUse tc;
    CHECK(tc.args.is_null());
    CHECK(!tc.args.is_object());
}

TEST_CASE("empty tool args: a blank buffer is distinguishable from bad JSON") {
    // 这两起案件是: 分派器 必须在每一圈对累积的
    // 论据作出的决定的层次上加以区分 缓冲区.
    auto decide = [](std::string_view buf) -> nlohmann::json {
        // "No arguments at all" ——缺,空,
        // 或白空。提供商都为同一东西发送三台,所以必须一起降落.
        if (buf.find_first_not_of(" \t\r\n") == std::string_view::npos)
            return nlohmann::json::object();
        try {
            return nlohmann::json::parse(buf);
        } catch (...) {
            return nlohmann::json{};  // 无效 = 真实无法解析
        }
    };

    // 缺 / 空 / 白空 / 显然的空 物: 全部为一件事。
    CHECK(decide("").is_object());
    CHECK(decide("   ").is_object());
    CHECK(decide("\n").is_object());
    CHECK(decide("{}").is_object());

    // 一个真实的 载荷 活下来了
    CHECK(decide(R"({"path":"a.txt"})")["path"] == "a.txt");

    // 也只有真爱 解析 失败产生无信使——短号
    // -mid-stream 案件编号 分派器 '其文为写.
    CHECK(decide(R"({"path":"a.tx)").is_null());
    CHECK(decide("not json at all").is_null());
}

TEST_CASE("empty tool args: every ToolUse construction site seeds an object") {
    const auto root = repo_root();
    REQUIRE_MESSAGE(!root.empty(),
                    "could not locate the repo root from the test's CWD");

    for (const char* rel : kToolUseConstructionSites) {
        const auto path = root / rel;
        const auto src  = read_file(path);
        REQUIRE_MESSAGE(!src.empty(), "unreadable test source: " << rel);

        // 网站建立了一个 ToolUse 从工具 -use 开始...
        const bool constructs =
            src.find("StreamToolUseStart") != std::string::npos;
        if (!constructs) continue;

        // ...所以它必须用对象来种子词。两种拼写都使用(
        // `tc.args = json::object()` 创建本地 ToolUse;
        // `args = nlohmann::json::object()`
        // 如果一个字被写满,那末,你应当遵守这个律例。
        const bool seeds =
            src.find("args   = json::object()") != std::string::npos
            || src.find("args = json::object()") != std::string::npos
            || src.find("args = nlohmann::json::object()") != std::string::npos;

        CHECK_MESSAGE(seeds,
            rel << ": builds a ToolUse from StreamToolUseStart but never "
                   "seeds `args` with an object. An argumentless tool call "
                   "(git_status, list_dir) then leaves args null, which the "
                   "dispatcher reports to the model as \"tool args failed to "
                   "parse\" \u2014 a correct call rejected as malformed. Seed "
                   "`tc.args = json::object()` at the start site; leave null "
                   "to mean a FAILED parse and nothing else.");
    }
}
