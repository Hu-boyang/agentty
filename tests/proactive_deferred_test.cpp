// proactive_deferred_test - 电话 DEFERRED 相同 -turn 地面定位 状态更新函数 路径。
//
// 主动的预告 -turn RAG "retrieve-then-launch"
// 设计: 当同步套期在 submit_message 错失(a) large/slow
// 密探的体 -embed 圆形 -trip 无法清理小预算, submit_message
// 离开阶段进入 流式传输 {ctx}(所以状态) -bar 加载动画 活的—— UI 从来没有
// 感觉挂),和 HOLDS 编号 数据流 在一个孤立的检索任务后面发射。当它降落时,
// 它会发出 ProactiveContextReady ,其名称为 状态更新函数 臂 (双臂)
// stream.cpp ——必须:
//
//   1. 注射该物质 <retrieved-context> 块进入 此 转身,就在前方
//      跟踪空助手占位符( 传输 顺序 :
//      用户(问题) —— 用户(文稿) ——助理(reply)),
//   2. 不 冻结活的占位符(仅插入上下文 msg),
//   3. 清除"retrieving context…"状态,
//   4. 返回一个非 -none Cmd (举行) -back launch_stream ),以及
//   5. DROP a 过期 如果用户到达 已取消 暂时不动
//
// 这驱动了 真实 stream_update 状态更新函数 的 stub dips(
// 没有 I/O 的提供者;返回的 Cmd 被检查过,从未执行).

#include <optional>
#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ThreadId;
using agentty::ModelId;
namespace phase = agentty::phase;
namespace msg   = agentty::msg;
namespace store = agentty::store;
namespace provider = agentty::provider;

static int g_checks = 0;

// A Model 形状完全为 submit_message 把它丢在树篱上 MISS
// : 用户转动 + 后面的空助手占位符, 相位 流式传输 ,
// 并显示 "retrieving context…" 状态粘着。
static Model make_deferred_model() {
    Model m;
    m.d.current.id = ThreadId{"deferred"};
    m.d.model_id   = ModelId{"claude-opus-4-5"};

    Message user;
    user.role = Role::User;
    user.text = "how does the retry backoff work in the http client";
    m.d.current.messages.push_back(std::move(user));

    Message placeholder;   // 现场助理 末尾 submit_message 推开
    placeholder.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(placeholder));

    phase::Active ctx;
    ctx.awaiting_retrieval = m.s.retrieval_seq = 7;
    m.s.phase        = phase::Streaming{std::move(ctx)};
    m.s.status       = "retrieving context\xE2\x80\xA6";
    m.s.status_until = {};
    return m;
}

TEST_CASE("proactive deferred") {
    // 软垫... launch_stream (在建筑内部) 处理函数 )
    // 读作 deps() 并走工具 注册表 ; 它不能触摸网络。
    // 返回 Cmd 被检查,永远不要跑。
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

    const std::string block =
        "<retrieved-context>\n[docs:http/retry.md:12]\nexponential backoff "
        "with full jitter, capped at 30s\n</retrieved-context>";

    // ── 1. 快乐路径:一个真正的块注入相同的-turn+发射 ────────
    {
        Model m = make_deferred_model();
        const std::size_t before = m.d.current.messages.size();

        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m), msg::StreamMsg{agentty::ProactiveContextReady{block, 0.82, 7}});

        auto& msgs = m2.d.current.messages;
        check(msgs.size() == before + 1,
              "deferred: block inserted as one new message");

        // 命令必须是用户(问题) + 用户( 内容) +助理( 位置持有人)
        check(msgs.size() >= 3, "deferred: at least 3 messages");
        const auto& ctx = msgs[msgs.size() - 2];
        const auto& tail = msgs.back();
        check(ctx.role == Role::User && ctx.is_proactive_context(),
              "deferred: context msg is a proactive User message");
        check(ctx.text.find("retrieved-context") != std::string::npos,
              "deferred: context msg carries the retrieved block");
        check(ctx.proactive && ctx.proactive->confidence
           && *ctx.proactive->confidence > 0.81
           && *ctx.proactive->confidence < 0.83,
              "deferred: retrieval confidence threaded onto the message");
        check(tail.role == Role::Assistant && tail.text.empty(),
              "deferred: trailing placeholder preserved (live tail)");

        // 现场占位者不得 被冻结( 数量) 数据流 逐步显示 将予封印。
        // frozen_through 计算被冻结的前缀;
        // 它必须在占位符之前停止。
        check(m2.ui.frozen_through < msgs.size(),
              "deferred: placeholder left unfrozen");
        check(m2.ui.frozen_through == msgs.size() - 1,
              "deferred: frozen prefix covers the inserted context msg");

        // 状况已清理完毕;发布了Cmd号发射。
        check(m2.s.status.empty(),
              "deferred: 'retrieving context…' status cleared");
        check(!cmd.is_none(),
              "deferred: a stream-launch Cmd is returned");
        // 阶段仍然活跃(launch_stream需要活的ctx).
        check(!m2.s.is_idle(), "deferred: phase stays active for launch");
    }

    // ── 2. 空块(清除了无信任栏) ──────
    //    发射转弯——只是没有注入上下文。
    {
        Model m = make_deferred_model();
        const std::size_t before = m.d.current.messages.size();

        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m), msg::StreamMsg{agentty::ProactiveContextReady{std::string{}, -1.0, 7}});

        check(m2.d.current.messages.size() == before,
              "deferred(empty): no message injected");
        check(m2.s.status.empty(),
              "deferred(empty): status cleared");
        check(!cmd.is_none(),
              "deferred(empty): stream still launches (no grounding, no hang)");
    }

    // ── 3. 过期 抵达后 取消 : 用户点击 Esc ~ 闲着时, ────
    //    检索在飞行中 一定是 DROPPED ,没有发射。
    {
        Model m = make_deferred_model();
        m.s.phase = phase::Idle{};   // 用户 已取消
        const std::size_t before = m.d.current.messages.size();

        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m), msg::StreamMsg{agentty::ProactiveContextReady{block, 0.5, 7}});

        check(m2.d.current.messages.size() == before,
              "deferred(stale): no injection after cancel-to-Idle");
        check(cmd.is_none(),
              "deferred(stale): no stream launched after cancel");
        check(m2.s.is_idle(),
              "deferred(stale): phase stays Idle");
    }

    // 4. 旧结果抵达更新轮次：先 Esc，再提交，再收到
    //    不得降落或发动新的转弯
    {
        Model m = make_deferred_model();
        auto* c = agentty::active_ctx(m.s.phase);
        c->awaiting_retrieval = m.s.retrieval_seq = 8;   // 新的转弯
        const std::size_t before = m.d.current.messages.size();

        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m), msg::StreamMsg{agentty::ProactiveContextReady{block, 0.9, 7}});

        check(m2.d.current.messages.size() == before,
              "deferred(old id): no injection into the newer turn");
        check(cmd.is_none(),
              "deferred(old id): no second launch");
        check(agentty::active_ctx(m2.s.phase)->awaiting_retrieval == 8,
              "deferred(old id): newer turn still waits for its own result");
    }

    // ── 5. A duplicate delivery launches once.
    {
        Model m = make_deferred_model();
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m), msg::StreamMsg{agentty::ProactiveContextReady{std::string{}, -1.0, 7}});
        check(!cmd.is_none(), "deferred(dup): first delivery launches");
        auto [m3, cmd2] = ::agentty::app::detail::step(agentty::app::detail::stream_update, std::move(m2), msg::StreamMsg{agentty::ProactiveContextReady{std::string{}, -1.0, 7}});
        check(cmd2.is_none(), "deferred(dup): second delivery is dropped");
    }
}
