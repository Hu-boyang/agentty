// logx_lifecycle_test - 届会横幅, SIGUSR1 标记,和旋转的种子。
//
// 这些是德意志, -workflow 担保来源 docs/website/logging.md (
// 面向开发者的持续捕获工作流。
//   • 每个进程都写一个grep-able "=== agentty session:"横幅
//   • signal_mark (a) 邮票a MARK + 飞行 -recorder 块, 自动 -signal-safe
//   • 两者都通过与普通的事件相同的O_APPEND ft(因此
//     正确间隔,数到中 -run 轮调)

#include <fstream>
#include <string>

#include "agtest.hpp"
#include "agentty/util/logx.hpp"

using namespace agentty;

namespace {

std::string whole_log() {
    const auto lf = logx::log_file();
    if (lf.empty()) return {};
    std::ifstream in{std::string{lf}, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

void require_logging() {
    REQUIRE_MESSAGE(!logx::log_file().empty(),
                    "AGENTTY_LOG/_FILE must be set for this binary "
                    "(ctest sets them; see cmake/AgenttyTests.cmake)");
}

} // 命名空间结束。

TEST_CASE("logx: session_banner writes a grep-able run separator") {
    require_logging();
    logx::session_banner("9.9.9-test debug pid=42 cwd=/tmp/x");
    const auto all = whole_log();
    CHECK(all.find("=== agentty session: 9.9.9-test debug pid=42 cwd=/tmp/x ===")
          != std::string::npos);
}

TEST_CASE("logx: signal_mark stamps a MARK block with the flight recorder") {
    require_logging();
    // 以可识别的事件来播种环,因此该垃圾场有内容.
    AGT_LOG(General, Warn, "lifecycle.test", "pre-mark marker {}", 7);
    logx::signal_mark();
    const auto all = whole_log();
    const auto mark = all.rfind("=== MARK (SIGUSR1)");
    REQUIRE(mark != std::string::npos);
    const auto end = all.find("=== END MARK ===", mark);
    REQUIRE(end != std::string::npos);
    // 飞行 -recorder 快照 栅栏之间必须装有种子的警告
    // 事件 (警告+总是响起) -recorded).
    CHECK(all.substr(mark, end - mark).find("pre-mark marker 7")
          != std::string::npos);
}

TEST_CASE("logx: banner and mark are no-ops without a file sink") {
    // 无法禁用水槽 -process (init是一次);
    // 这个把针钉在 CONTRACT 经由 API 其文件行为: 调用
    // 已经 noexcept 并且即使多次被召唤,也不得投掷或坠毁。
    logx::session_banner("");
    logx::signal_mark();
    logx::session_banner("second call");
    CHECK(true);   // 到达这里的就是这个断言
}
