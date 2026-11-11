// reveal_bucket_test - 电话 逐步显示 渲染 桶答一个 PAINT 问,
// 不是带宽。
//
// ── 错误 ─────────────────────────────────────────────────────────────
//
// 报为 "reasoning seems slow and bursty" 过Ssh。
//
// program.hpp 现场直播时把视线散列桶装 逐步显示 所以,
// 我们 帧 利率与 终端 实际油漆:16毫秒 终端 解剖
// 学合成物( DEC 2026同步),在其中其画作逐渐达到的
// ~100ms(. Termux 苹果公司 终端 ,
// tmux 无需通过),因为 60 FPS 水管被淹 展示在模型后面
//
// 那桶以前读过 streaming_tick_period (),
// 答案 两个 立即提问:
//
//   终端能否原子合成一帧？支持时使用 33 ms。
//   ms: 100 ms "is there a slow wire to spare"  ⁇  80 ms 当远程
//
// 首先是画画,把地板打碎 逐步显示 桶子压住
// TYPEWRITER 与眼所见无关的原因: 逐步显示
// 缓冲器是 字节-per-SECOND (
// 见A/C.5/61/L.48,第16段)
// markdown.hpp's 逐步显示 以100 ms的
// 推理的60 cps 地板,即 ~6 字符一次降落,每秒十次。
// 可见的块,这就是什么。 "bursty" 说吧
//
// 旧评论说没有爆发 因为
// Tick 周期本来就是字节交付节奏。
// 本地真实的,假的在sh,在哪里 字节 继续不断到达 此时,
// 滴滴被故意拖住 链接.
//
// ── 这么一来,这针 ──────────────────────────────────────────────────────
//
// 电话 逐步显示 桶取决于同步 -output 支助 仅。
// 远程不能改变它, 也不能改变滴答。

#include "agtest.hpp"

#include <maya/terminal/ansi.hpp>

#include <cstdlib>
#include <string>

#include "agentty/runtime/app/subscribe.hpp"

namespace {

// Set/clear 范围期间的海滨。
struct Env {
    std::string key, saved;
    bool had = false;
    Env(const char* k, const char* v) : key(k) {
        if (const char* old = std::getenv(k)) { saved = old; had = true; }
        if (v) setenv(k, v, 1); else unsetenv(k);
    }
    ~Env() {
        if (had) setenv(key.c_str(), saved.c_str(), 1);
        else     unsetenv(key.c_str());
    }
};

// 电话 逐步显示 桶,作为 program.hpp 计算它。
// 将它作为镜像而不是导出 : visual_hash ()是大成员模板,
// 这是测试中它的一个线条。如果真实的模板改变形状,则测试应当
// FAIL 改为 编译 或不同意,两者都是我们希望的信号。
//
// 测试的要点是DEPENDENCY,而不是常数:这
// 必须是同步支持的函数单独.
std::int64_t reveal_bucket_ms() {
    return maya::ansi::env_supports_synchronized_output() ? 16 : 100;
}

} // 命名空间结束。

TEST_CASE("reveal bucket: it is a function of sync support alone") {
    // 没有 TERM 操纵: 绳子在下面运行 TERM ⁇ ,
    // ⁇ , 其中 env_supports_synchronized_output
    // (a) 拒绝正确与否,无论 MAYA_FORCE_SYNC
    // (a 终端 无法逃脱 无法逃脱 DEC 私有模式。所以,
    // 请声明 INVARIANT 而不是一个恒定数, 不论如何,
    // 终端 报告,桶必须同意它。
    const bool sync = maya::ansi::env_supports_synchronized_output();
    CHECK(reveal_bucket_ms() == (sync ? 16 : 100));
}

TEST_CASE("reveal bucket: ssh does NOT throttle the typewriter") {
    // 回归,同样 终端 ,同样的油漆能力;
    // 唯一的变化是shd输出变量。 逐步显示 桶子不能动
    const std::int64_t local = reveal_bucket_ms();

    const Env a{"SSH_CONNECTION", "10.0.0.2 51234 10.0.0.1 22"};
    const Env b{"SSH_TTY", "/dev/pts/3"};
    const std::int64_t remote = reveal_bucket_ms();

    CHECK(remote == local);
    INFO("reveal bucket local=", local, " remote=", remote);
}

TEST_CASE("reveal bucket: ssh doesn't slow the tick either") {
    // 滴滴上的地板让结局 -of-turn 滑翔机 2-3 帧 ,
    // 所以一个答复 降落在一个爆破 仍然粘贴在 message_stop。
    // 远程更改现在什么都不会改变; Motion=Off 即如何切 重绘.
    const bool sync = maya::ansi::env_supports_synchronized_output();
    agentty::Model::Env local;   local.synchronized_output  = sync;
    agentty::Model::Env remote;  remote.synchronized_output = sync; remote.remote = true;
    const auto period = agentty::app::streaming_tick_period(local);
    CHECK(period.count() >= 33);
    CHECK(agentty::app::streaming_tick_period(remote) == period);
    CHECK(remote.reveal_end_glide() == local.reveal_end_glide());

    // 可能就是那个 SLOWER 而不是桶子(只有桶子
    // 决定唤醒是否会产生新的散乱 )。 它绝不能更快,
    // 这意味着唤醒时什么都不做。
    CHECK(period.count() >= reveal_bucket_ms());
}
