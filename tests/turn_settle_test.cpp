// turn_settle_test.cpp — 字节-conservation +
// 结算 -shape 不变量 横跨 TURN-ENDING 过渡( 主机) FSM
// 边缘 逐步显示 审计发现错误在 ). fsm_test 涵盖通用型国家机器,
// stream_liveness_test 覆盖 流式传输 ⁇   ⁇ 光标 边,
// 但过渡在哪里 字节 移动到消息的三 缓冲区 — pending_stream
// (画 传输) → streaming_text (开车)
// -committed 案文(已解决) NO 直接 单元测试。每个审计错误都住在这里:
// a 解决时损失的字节, 积压粘贴, 部件留下动画。
//
// 电话 不变量 被钉住, 完毕 finalize_turn(EndTurn / MaxTokens):
//
//   1. CONSERVATION ——在最后确定后,解决 `text` 包含每个
//      字节 文本中写着: streaming_text ∪ pending_stream 之前,
//      顺序,和两个刮伤 缓冲区 空的。没有 字节 被删除(
//      "invisible in-flight bytes at message_stop" 而不
//      重复(双倍) -commit ) (中文(简体) ).
//
//   2. IDEMPOTENCE - 打电话 finalize_turn 两次是拒绝 -op 第二届
//      时间: 文本不变, 无 re -appended 后缀。 A retry/cancel
//      路径 -enters 定稿不得加倍 -commit.
//
//   3. SETTLE READINESS ——在最后敲定+耗尽延期结算后,
//      live_tail_reveal_settled( m) 变为真实( 冰封门打开 ) :
//      转弯实际上达到一个固定的形状, 它不楔形活体。
//
// 纯 ⁇  状态更新函数 调用 手 ⁇  -built Model;无网,无景.

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"

#include <maya/core/anim_clock.hpp>

#include <cstdlib>
#include <optional>
#include <print>
#include <string>
#include <vector>

using namespace agentty;
namespace detail = agentty::app::detail;

namespace {


// finalize_turn 触摸 deps() (store/provider
// 接口边界 即使这个测试从未运行返回的 Cmd。安装一次惰性根根——没有网络,没有 IO.
void install_stub_deps() {
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const agentty::ThreadId&) {},
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"stub"}; },
        .title_from    = [](std::string_view) { return std::string{}; },
        .auth          = {},
    });
}

// 构建一个 Model 活下来的 末尾 是一个助理信件,
// 带有 字节 分三块 缓冲区 中间 -stream 信息确实如此。
Model make_model_midstream(std::string settled, std::string streaming,
                           std::string pending) {
    Model m;
    m.d.current.id = ThreadId{"settle"};

    Message u;
    u.role = Role::User;
    u.text = "prompt";
    m.d.current.messages.push_back(std::move(u));

    Message a;
    a.role            = Role::Assistant;
    a.text            = std::move(settled);
    a.streaming_text  = std::move(streaming);
    a.pending_stream  = std::move(pending);
    m.d.current.messages.push_back(std::move(a));
    return m;
}

// 将环境强制于交互(滑翔)政策,以便测试练习 request_finalize 路径,
// 而不是 SSH 即时 -finish 路径——但
// CONSERVATION 不变量 一定要坚持下去 两者 ,
// 所以我们运行 第二次通过与滑翔机残疾。
void set_glide(bool on) {
    if (on) { ::unsetenv("AGENTTY_NO_REVEAL_GLIDE"); ::unsetenv("SSH_TTY");
              ::unsetenv("SSH_CONNECTION"); ::unsetenv("SSH_CLIENT"); }
    else    { ::setenv("AGENTTY_NO_REVEAL_GLIDE", "1", 1); }
}

// 将推迟的结算推向完成: finalize_turn 武装 逐步显示
// 斜坡( 交互) 或立即结束( 斜坡); 选择方式之一,
// 继续勾选动画时钟+ 投票 live_tail_reveal_settled
// 直到部件打开 关闭, 关闭。
void drain_settle(Model& m) {
    for (int i = 0; i < 400 && !detail::live_tail_reveal_settled(m); ++i) {
        // 观点的构建是推进 逐步显示 光标; 直接安放,
        // 等坡道落地后再进行。 在此我们只需提前时间和时间。
        // -check - 部件自己最后的斜坡
        // 排水沟 build() 冷冻门会咨询
        auto& msg = m.d.current.messages.back();
        auto* cache = m.ui.view_cache.peek(m.d.current.id, msg.id);
        if (cache && cache->streaming) (void)cache->streaming->build();
        maya::testing::advance_anim_clock_ms(33);
    }
}

void conservation(bool glide) {
    std::println("--- conservation (glide={}) ---", glide);
    set_glide(glide);
    maya::testing::freeze_anim_clock(0);

    const std::string settled   = "Settled prose from a prior sub-turn. ";
    const std::string streaming = "Drip-committed streaming text. ";
    const std::string pending   = "Raw wire bytes not yet drained.";
    const std::string expected  = settled + streaming + pending;

    Model m = make_model_midstream(settled, streaming, pending);
    (void)detail::finalize_turn(m, StopReason::EndTurn);

    auto& a = m.d.current.messages.back();
    // #1 保护: 每一个 字节 以顺序显示; 抓取 缓冲区 空无所着.
    check(a.text == expected,
          "settled text == settled ∪ streaming ∪ pending, in order (got '"
          + a.text + "')");
    check(a.streaming_text.empty(), "streaming_text drained to empty");
    check(a.pending_stream.empty(), "pending_stream drained to empty");

    // #2 独断专行:第二次定稿( retry/cancel 内容 -entry )是无 -op.
    const std::string after_first = a.text;
    (void)detail::finalize_turn(m, StopReason::EndTurn);
    check(a.text == after_first,
          "second finalize_turn does not double-commit (got '" + a.text + "')");

    maya::testing::unfreeze_anim_clock();
    std::println("PASS\n");
}

void empty_pending_is_safe() {
    std::println("--- empty_pending_is_safe ---");
    set_glide(false);
    maya::testing::freeze_anim_clock(0);
    // 已经滴水的转弯 -committed 全部都说: 待处理 空的,
    // 内含着 流式传输 空的, 仅是已解决的文本。 定稿必须是干净的 否
    // -op-ish 和解,不附加空后缀或丢失文本。
    Model m = make_model_midstream("Only settled text, nothing in flight.",
                                   "", "");
    const std::string before = m.d.current.messages.back().text;
    (void)detail::finalize_turn(m, StopReason::EndTurn);
    check(m.d.current.messages.back().text == before,
          "settled-only turn survives finalize unchanged");
    maya::testing::unfreeze_anim_clock();
    std::println("PASS\n");
}

void reaches_settled_shape() {
    std::println("--- reaches_settled_shape ---");
    set_glide(false);   // 路径稀少:finish()是同步的,没有斜坡等待
    maya::testing::freeze_anim_clock(0);
    Model m = make_model_midstream("Prefix. ", "middle ", "tail bytes.");
    (void)detail::finalize_turn(m, StopReason::EndTurn);
    drain_settle(m);
    // #3转弯到达一个固定的形状(冰盖门打开)——它不会
    // 将部件楔形永久地存活.
    check(detail::live_tail_reveal_settled(m),
          "turn reaches settled shape after finalize + drain (freeze gate "
          "opens; a wedged ramp would keep this false)");
    maya::testing::unfreeze_anim_clock();
    std::println("PASS\n");
}

} // 命名空间结束。

TEST_CASE("turn settle") {
    std::println("=== turn_settle_test ===");
    install_stub_deps();
    conservation(/*滑翔机=( 滑翔)*/true);
    conservation(/*滑翔机=( 滑翔)*/false);
    empty_pending_is_safe();
    reaches_settled_shape();
}
