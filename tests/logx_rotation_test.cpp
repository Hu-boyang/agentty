// logx_rotation_test 中间 -run 旋转不能丢失线条或
// 使作者持有的ft 无效。
//
// 旋转是一对对数 接口边界 带有货币危险: 写手读取汇 ft
// 无需 旋转 互斥量 (诊断不得 锁 打电话者 高频路径 ),
// 所以旋转可以 CLOSES 旧的描述符并发布新的描述符,
// 并让每个持有内核已回收的号码的赛车作家都留下一个新描述符。
// 写法成功, 进入了打开的下个任意。 最多只能是
// 无声的日志丢失和交叉。 -fd 最坏的腐败,
// 和它隐匿在单一 -threaded 测试。
//
// 修理是dup2: 重装 -point 编号 相同 在新
// 文件中的描述符, 因此所持有的值在旋转中保持有效,
// 并且没有窗口可以重新使用。 这些测试将双半的契约标注在其中。
//
// 达到轮换需要降低门槛:在航运方面 32 MB 测试
// 必须写32 MB (测量:超过3分钟) TU 链
// 接到 logx.cpp 直接地,它设置了内部的把手,
// 而不是需要一个内脏——伐木保留着无人配置的东西。

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "agtest.hpp"
#include "agentty/util/logx.hpp"

using namespace agentty;

// 内部轮换门槛( 3) src/util/logx.cpp )不公开
// API 唯一能触摸到它的就是这个测试
namespace agentty::logx::detail {
extern std::atomic<std::int64_t> g_rotate_bytes;
}

namespace {

// 旋转 64 KB 之后, 接口边界 可以以毫秒的速度到达。
// 在任何日志之前设定: 初始使用时的沉积拉链。
constexpr long long kRotateBytes = 64 * 1024;

struct SetRotateBytes {
    SetRotateBytes() {
        logx::detail::g_rotate_bytes.store(kRotateBytes,
                                           std::memory_order_relaxed);
    }
} const g_set_rotate_bytes;

long long rotate_limit() { return kRotateBytes; }

std::string read_file(const std::string& p) {
    std::ifstream in{p, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

// 日志员仍然拥有的一切:活文件加上它所保存的旋转生成。
// 在其中之一的线条就是幸存下来的线条。
std::string live_plus_rotated() {
    const auto lf = std::string{logx::log_file()};
    if (lf.empty()) return {};
    return read_file(lf + ".old") + read_file(lf);
}

void require_logging() {
    REQUIRE_MESSAGE(!logx::log_file().empty(),
                    "AGENTTY_LOG/_FILE must be set for this binary "
                    "(ctest sets them; see cmake/AgenttyTests.cmake)");
}

// A 载荷 足够宽的几百行 KB-scale 阈值,
// 因此每个测试实际上都旋转,而不是仅仅附加.
constexpr const char* kPad =
    "----------------------------------------------------------------";

// 粗糙的 字节 磁盘上每条已发射的行: 载荷
// 加上logx 的时间戳、 关卡、 频道和站点前缀。
// 仅用于 SIZING 以真实内容为根据,
// 绝不反对这一估计。
constexpr long long kLineCost = 64 + 80;

long long live_size() {
    return static_cast<long long>(read_file(std::string{logx::log_file()}).size());
}

#if defined(__linux__)
// 进程当前打开的 fd 编号 `path` ,或 -1.
// 阅读/ proc/self/fd 是唯一能从外部对流观察
// 水槽描述符而不扩大其公共范围的方法 API 测试的好处。
int fd_pointing_at(const std::string& path) {
    int found = -1;
    for (const auto& e : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code ec;
        const auto target = std::filesystem::read_symlink(e.path(), ec);
        if (ec) continue;                       // 弗德在我们下面关闭了,很好
        if (target.string() != path) continue;
        const int fd = std::atoi(e.path().filename().string().c_str());
        // directory_iterator本身持有fd;
        // 日志是唯一能够指向这条路径的,所以第一对齐获胜.
        if (found < 0) found = fd;
    }
    return found;
}
#endif

}  // 命名空间结束。

TEST_CASE("logx: mid-run rotation keeps the sink writable") {
    require_logging();
    const long long limit = rotate_limit();
    // Write 顺利地超过了极限,无论文件现在在哪里 这些测试案例
    // 共享一个日志,所以 "how much is enough"
    // 取决于以前的案件已经写过什么。
    const long long need = 2 * limit + limit / 2;
    for (long long w = 0; w < need; w += kLineCost)
        AGT_LOG(General, Warn, "rotation.test", "single {} {}", w, kPad);

    const auto all = live_plus_rotated();
    // 写最后一行必须存在: 如果旋转使描述符无效,
    // 在交换后写会消失。
    CHECK(all.find("single ") != std::string::npos);
    // 而旋转必定真的发生了——否则这个测试
    // 只是默默地行使了附加路径.
    CHECK_MESSAGE(!read_file(std::string{logx::log_file()} + ".old").empty(),
                  "expected a rotated .old generation");
}

TEST_CASE("logx: concurrent writers lose nothing across a rotation") {
    require_logging();

    // 真正从测试中获取能量 有两个错误的开始 都值得
    // 记录 因为两者看起来都是证据 而不是:
    //
    //   * "assert every line ever written survives"——错误,因为logx
    //     保留 一个 旋转生成。 几倍于阈值的爆破
    //     SUPPOSED 以放弃最古老的线条。
    //   * "use a tiny threshold so it rotates constantly"——错误
    //     更微妙的原因: 线程 在不同的时间完成,
    //     所以 线程 早期完成的最后一行被旋转所驱离 LATER
    //     线程 触发器,对固定的对冲器也失败了 i.e
    //     测量的是保留量,而不是错误。
    //
    // 两者的区别在于会计,每个大小 ROUND 所以
    // 最多一次就越过了门槛:然后,圆形的线条被分割成.
    // old和活文件,没有被拆卸的东西,每个都被拆掉。
    // 必须。任何缺位都是在写法下沉淀无效的
    // – 实际缺陷。重复多次回合,
    // 使许多旋转窗口能够 竞态 反对。
    const long long limit = rotate_limit();
    REQUIRE(limit > 0);

    constexpr int kThreads = 8;
    constexpr int kRounds  = 12;

    long rotations_seen = 0;
    for (int round = 0; round < kRounds; ++round) {
        // 大小 此 回合对当前文件实际坐落的位置,
        // 而不是对空文件:这些案例共享一个日志,
        // 所以固定的突起可以跨越两个阈值,并驱逐自己的
        // 早期行——这种合法的掉落看起来完全像错误.
        const long long before  = live_size();
        const long long headroom = limit - before;
        // 仅靠剩下的主厅: 保证一个通道, 而不是两个。
        // 如果文件已经超过极限, 下个发射会旋转,
        // 所以一个小突突仍然会给出一个窗口。
        const long long budget  = (headroom > 0 ? headroom : 0) + limit / 4;
        const int per_thread =
            static_cast<int>(budget / kLineCost) / kThreads + 1;
        REQUIRE(per_thread > 1);

        std::vector<std::thread> ws;
        ws.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            ws.emplace_back([t, round, per_thread] {
                for (int i = 0; i < per_thread; ++i)
                    AGT_LOG(General, Warn, "rotation.test",
                            "r={} t={} i={} {}", round, t, i, kPad);
            });
        }
        for (auto& w : ws) w.join();

        const auto all = live_plus_rotated();
        if (live_size() < before) ++rotations_seen;

        // 每行图 此 圆形必须存在——一个
        // 旋转窗口内一个圆形的圆形不可能被驱离。
        int missing = 0, first_missing_t = -1, first_missing_i = -1;
        for (int t = 0; t < kThreads; ++t) {
            for (int i = 0; i < per_thread; ++i) {
                const std::string key = "r=" + std::to_string(round) +
                                        " t=" + std::to_string(t) +
                                        " i=" + std::to_string(i) + " ";
                if (all.find(key) == std::string::npos) {
                    if (missing == 0) { first_missing_t = t; first_missing_i = i; }
                    ++missing;
                }
            }
        }
        REQUIRE_MESSAGE(missing == 0,
                        "round " << round << ": " << missing
                        << " line(s) lost across rotation, first t="
                        << first_missing_t << " i=" << first_missing_i);
    }

    // 如果没有任何东西旋转,测试证明没有旋转。
    CHECK_MESSAGE(rotations_seen > 0,
                  "no rotation observed across " << kRounds << " rounds");
}

TEST_CASE("logx: rotation re-points the sink fd instead of replacing it") {
    require_logging();
#if !defined(__linux__)
    MESSAGE("fd-identity check needs /proc; skipped on this platform");
#else
    // 该 加载-bearing 测试Dup2修复, 和唯一一个在
    // 这里 有真正的力量对抗虫。
    //
    // 线条 -accounting 试验通过时 BROKEN 日志器也 :
    // 近( oldfd) 和赛车手的write() 之间的 竞态
    // 窗口是宽的纳米秒, 因此损失太少, 无法通过写行和计数来捕捉。
    // 从统计角度来说, 它将购买一个缓慢的、 片面的测试,
    // 测试在绿色运行中仍然无法证明任何结果。
    //
    // 所以,坚持 PROPERTY 使我们 竞态 无法,
    // 而不是它所产生的症状。 作者读取沉淀的 fd 不旋转 锁 ;
    // 只有在旋转从未使所持数字无效的情况下才发出声音。
    // dup2 re -points 现有描述符,所以该数字在
    // 进程上稳定 生命周期 ; 互换 -and-close
    // 发布新的版本,并释放旧的用于再利用。在旋转的fd中进行比较,
    // 可以确定这两个执行部分——每次运行,不 竞态 需要的,
    // 请说明。
    const std::string path = std::string{logx::log_file()};

    const int before = fd_pointing_at(path);
    REQUIRE_MESSAGE(before >= 0, "could not find the log fd in /proc/self/fd");

    // 强制至少一次旋转.
    const long long limit = rotate_limit();
    const long long need  = limit - live_size() + limit / 4;
    for (long long w = 0; w < need; w += kLineCost)
        AGT_LOG(General, Warn, "rotation.test", "fdid {} {}", w, kPad);
    REQUIRE_MESSAGE(!read_file(path + ".old").empty(),
                    "expected a rotated .old generation");

    const int after = fd_pointing_at(path);
    REQUIRE_MESSAGE(after >= 0, "log fd vanished after rotation");
    CHECK_MESSAGE(before == after,
                  "rotation replaced the sink fd (" << before << " -> "
                  << after << "); writers reading it unlocked would be "
                  "holding a closed, reusable descriptor");

    // 和那个 -pointed 福德必须到达 LIVE 文件,
    // 不是旋转 -away 一,dup2在错误的目标上会保持
    // 数字稳定,同时默默地写入.old 永远.
    AGT_LOG(General, Warn, "rotation.test", "post-rotation sentinel {}", 1);
    CHECK(read_file(path).find("post-rotation sentinel 1") != std::string::npos);
#endif
}

TEST_CASE("logx: rotation bounds the live file") {
    require_logging();
    // 旋转的要点是, 活文件保持小块。 给它一个宽大的
    // 天花板( 门槛 + 1 burke) ,
    // 这样它就可以在不敏感地 到达 的 交叉 位置 下 保证。
    const long long limit = rotate_limit();

    for (long long w = 0; w < 4 * limit; w += kLineCost)
        AGT_LOG(General, Warn, "rotation.test", "bound {} {}", w, kPad);

    const auto live = read_file(std::string{logx::log_file()});
    CHECK_MESSAGE(static_cast<long long>(live.size()) <= limit * 4,
                  "live log grew to " << live.size()
                  << " bytes against a " << limit
                  << "-byte rotation threshold");
}
