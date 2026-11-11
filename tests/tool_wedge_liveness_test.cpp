// tool_wedge_liveness_test - 绞刑 -syscall 网格
// 测量活度 最后 PROGRESS 事件 而不是从发射。
//
// 退后目标: 楔形网 update/meta.cpp (tick)
// 失败 任意 运行工具 330s 之后 STARTED ——甚至
// 健康地长期地—— 抛弃了它的工作。 -runner 积极推动取得进展。
// `task` 代理代理代理进行全方位 -minute 审计(
// 通过工具后继工具,释放进展) 快照 将获得力量 -failed
// 平顶帽上,整个报告都丢掉了。观察者:平行探险家,两个断头台在
// 330秒左右,而第三个恰巧完成的则在更早的时候幸存下来。
//
// 固定: 运行载体 last_progress_at ,在每张标签上盖章
// ToolExecProgress 快照 ; 楔形测量法 -- -- 最大( 最大)
// started_at, last_progress_at ) 进步工具会不断重置上限;
// 一个工具真的挂在了一个阻塞的系统调用上,所以净值的运行完全如前。
//
// 这驱动器 真实 meta_update 状态更新函数 附有 " 勾选 " ,并声称:
//   A. 很久以前开始的 BUT 刚进步 工具继续运行
//   B. 早在发射后AND号就已停机 —— 工具已失效(已拖动)。
//   C. 取得的进展 ITSELF 年长于 □ 仍在织布 过期 进步无法挽救
//      一个 -gone-silent 工具),将使用该工具。

#include <chrono>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

using namespace agentty;
using std::chrono::steady_clock;
using std::chrono::seconds;

namespace {


// 每一个 归并 在此运行于此固定时间; 工具时间是相对它设定的。
constexpr steady_clock::time_point kNow = app::kTestEpoch;

// 有个模特停在了里面 ExecutingTool 与一个运行工具,其启动和最后 -progress 时,
// 我们控制。 `progress_ago` < 0 表示数 "no progress yet".
Model model_with_running_tool(seconds started_ago, seconds progress_ago) {
    Model m;
    m.s.phase = phase::ExecutingTool{phase::Active{}};

    Message asst;
    asst.role = Role::Assistant;
    ToolUse tc;
    tc.id   = ToolCallId{"tc-1"};
    tc.name = ToolName{"task"};
    tc.args = nlohmann::json{{"prompt", "exhaustive audit"}};

    const auto now = kNow;
    ToolUse::Running run;
    run.started_at = now - started_ago;
    if (progress_ago.count() >= 0) {
        run.progress_text     = "step 12: reading spec/rcp-1.0.md";
        run.last_progress_at  = now - progress_ago;
    }
    tc.status = run;
    asst.tool_calls.push_back(std::move(tc));
    m.d.current.messages.push_back(std::move(asst));
    return m;
}

// 等待用户批准的工具 `waited` 并且
// 刚刚获得批准—— 执行开始 当前。
// 模型发行#40:当模型发射时,卡片被创建 调用
// 人类花时间 问题是 思考时间是否与工具相对立
Model model_just_approved_after(seconds waited) {
    Model m;
    m.s.phase = phase::ExecutingTool{phase::Active{}};

    Message asst;
    asst.role = Role::Assistant;
    ToolUse tc;
    tc.id   = ToolCallId{"tc-1"};
    tc.name = ToolName{"shell"};
    tc.args = nlohmann::json{{"command", "make -j8"}};

    const auto now = kNow;
    ToolUse::Running run;
    // 牌诞生于模型释放 调用 — `waited` 刚才
    run.started_at = now - waited;
    // 执行行动刚刚开始,在批准后, 在修复之前,
    // 没有地方可以记录这个, IS 虫子,你这混蛋
    run.executing_since = now;
    tc.status = run;
    asst.tool_calls.push_back(std::move(tc));
    m.d.current.messages.push_back(std::move(asst));
    return m;
}

bool tool_running(const Model& m) {
    for (const auto& msg : m.d.current.messages)
        for (const auto& tc : msg.tool_calls)
            if (tc.id == ToolCallId{"tc-1"}) return tc.is_running();
    return false;
}
bool tool_failed(const Model& m) {
    for (const auto& msg : m.d.current.messages)
        for (const auto& tc : msg.tool_calls)
            if (tc.id == ToolCallId{"tc-1"})
                return std::holds_alternative<ToolUse::Failed>(tc.status);
    return false;
}

Model tick(Model m) {
    m.now = kNow;   // 编号 归并 时间
    auto [next, _] = ::agentty::app::detail::step(::agentty::app::detail::meta_update, std::move(m), msg::MetaMsg{Tick{}});
    return std::move(next);
}

} // 命名空间结束。

TEST_CASE("tool wedge liveness") {
    // 楔形路径 调用 kick_pending_tools ,达到 deps()。安装无 -op
    // Deps 所以,我们 状态更新函数 运行时没有真实 Provider/Store.
    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-test"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    // ── A. 长 -running 不过 ACTIVELY 进展 ______________________ 不 结了婚 ──────────
    // 20分钟前推出,但进展 快照 之前降落的,
    // 这是以前被误杀的准确案例
    {
        Model m = model_with_running_tool(seconds{1200}, seconds{1});
        m = tick(std::move(m));
        check(tool_running(m), "A: healthy long-runner with fresh progress stays Running");
        check(!tool_failed(m), "A: it is NOT force-failed by the wedge net");
    }

    // ── B. 长 子 -running 财务报告和已审计财务报表 SILENT 发射以来 —— 楔形。 ───────────────
    // 400多年前发射的,从未释放出进步。 -syscall 形状。
    {
        Model m = model_with_running_tool(seconds{400}, seconds{-1});
        m = tick(std::move(m));
        check(tool_failed(m), "B: a tool silent since launch past the cap is failed");
        check(!tool_running(m), "B: it no longer shows as Running");
    }

    // ── C. 进展虽存在,但本身就是进展 STALE 过了盖子 结了婚 ───
    // 开始于700多年前;生命最后的标志 400多年前(>330s)。
    // 过期 进步不得 保持一个自 -dead 工具活着。
    {
        Model m = model_with_running_tool(seconds{700}, seconds{400});
        m = tick(std::move(m));
        check(tool_failed(m), "C: a tool whose last progress is older than the cap is failed");
    }

    // ── D. 刚刚到顶端,没有进展 —— 仍在运行。 ─────────
    // 300多年前( < 330s)发射,没有进展,网络不能提前。
    {
        Model m = model_with_running_tool(seconds{300}, seconds{-1});
        m = tick(std::move(m));
        check(tool_running(m), "D: below the cap, a silent tool is left alone");
    }

    // ── E. 人类批准时间为NOT 工具时间(第40期) ──
    //
    // 工具尚未获批时超时计时器就已经开始计时，
    // 因此用户批准后，工具可能立即因
    // 耗时过长失败，尽管实际执行尚未
    // 开始。
    //
    // started_at 模型时盖章 EMITS
    // 编号 调用 ,故意:卡片显示一个活过的时间器,
    // 并在加时冷却 -streaming 读作卡住。
    // 但工具等待 权限 并且从同一个领域测得的楔形网
    // ——时间是 HUMAN 花费在330s的预算里。
    // 离开6分钟, 批准, 而第一个Tick杀死一个
    // 没有执行任何指令的命令。
    //
    // 两个钟回答不同的问题,不能是一个领域:
    //   started_at      — "how long has this card existed?" (播放) (
    //   播放) executing_since — "how long has this been RUNNING?" (寿命)
    {
        Model m = model_just_approved_after(seconds{400});
        m = tick(std::move(m));
        check(tool_running(m),
              "E: a just-approved tool is not wedged by the time spent "
              "waiting for approval");
        check(!tool_failed(m),
              "E: approving after a long think does not instantly fail it");
    }

    // ── F. 但一次 . . .. 但有一次 . EXECUTING 帽子从那一刻起就一直适用 ──
    // 固定决不能解除网络:一个早已批准、此后一直保持沉默的工具
    // 仍然是一个挂在嘴边的工具。
    {
        Model m = model_just_approved_after(seconds{900});
        // 400,000多年前批准( > 上限),而不是刚刚批准。
        for (auto& msg : m.d.current.messages)
            for (auto& tc : msg.tool_calls)
                if (auto* r = std::get_if<ToolUse::Running>(&tc.status))
                    r->executing_since = kNow - seconds{400};
        m = tick(std::move(m));
        check(tool_failed(m),
              "F: silent for longer than the cap SINCE EXECUTION began is "
              "still wedged");
    }
}
