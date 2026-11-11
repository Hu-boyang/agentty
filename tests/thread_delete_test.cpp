// thread_delete_test - 两者 -press 线程-picker 删除( ThreadListDelete).
//
// ^D 输入 ^J 线程 选择面板 删除 a 线程 ,
// 镜像 SettingsListRemove / AccountRemove 双
// -press 将合同固定在安全的地方:
//   1. ARM 那样 COMMIT ——第一台按机只标出行号( ⁇ ). confirm_remove
//      设置,没有删除); SAME行的第二张按键会输入。
//   2. DISARM 移动中——两家新闻机构之间的任何导航都使
//      待处理 状态,所以一个流浪 ^D 永远不能删除 线程.
//   3. 活动-thread 删除开始 a FRESH 线程 通过同一核心
//      NewThread 用途:相位投放到Idle(中位) -stream 删除
//      不能离开 传输 与死人对决 线程 和。 和 简 况 current.id 变化。
//   4. NON-ACTIVE 删除叶 m.d.current 无人知晓 选择面板 打开。
//   5. 无用-after-erase:土司引用删除的标题正确
//      纵横元素在中点被销毁 -reducer.

#include "agentty/runtime/app/update/internal.hpp"
#include "agtest_fx.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/panel/common.hpp"

#include <optional>
#include <print>
#include <string>
#include <vector>

namespace pn = agentty::ui::panel;

using namespace agentty;
namespace detail = agentty::app::detail;
namespace pick = agentty::ui::pick;

namespace {

int g_failed = 0;
void check(bool cond, const std::string& msg) {
    if (!cond) { std::println("  FAIL: {}", msg); ++g_failed; }
}

int g_new_ids = 0;
std::vector<std::string> g_deleted;   // 退回的DeleteThread 副作用的编号

void install_stub_deps() {
    g_deleted.clear();
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const agentty::ThreadId& id) { g_deleted.push_back(id.value); },
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"fresh-" + std::to_string(++g_new_ids)}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .auth          = {},
    });
}

// A 选择面板 打开时间 `index` ,由三个 线程 t0/t1/t2 ,
// 与 `current` 设置到任何通过 ID 的。 线程 最新订购 -first
// 象真正的名单一样; 身份证是稳定的,标题是不同的,这样我们就可以坚持祝酒词。
Model make_model(int cursor, const std::string& current_id) {
    Model m;
    for (int i = 0; i < 3; ++i) {
        Thread t;
        t.id = ThreadId{"t" + std::to_string(i)};
        t.title = "thread " + std::to_string(i);
        m.d.threads.push_back(std::move(t));
    }
    m.d.current.id = ThreadId{current_id};
    m.d.current.title = "active";
    m.ui.panel.descend(pn::ThreadList{{cursor}});
    return m;
}

// 传一个消息 PERFORM 随其所求.
//
// 删除 a 线程 是一个 副作用 现在: 状态更新函数 返回一个
// 字串 "remove this id" 并且由主办方执行。
// 这些检查坚持 `g_deleted`, i.e .在到达商店的方面,
// 所以测试必须发挥宿主的作用——这是重点,因为如果手臂真的还回去,
// 只删除它就降落在那里.
Model step(Model m, msg::ThreadListMsg tm) {
    auto [next, cmd] =
        detail::step(detail::thread_list_update, std::move(m), std::move(tm));
    agtest::fx::Store store;
    agtest::fx::run(cmd, store);
    for (const auto& id : store.deleted_threads) g_deleted.push_back(id.value);
    return std::move(next);
}

const pick::OpenAt* picker(const Model& m) {
    return m.ui.panel.get<pn::ThreadList>();
}

} // 命名空间结束。

int main() {
    install_stub_deps();

    // ── 1. 然后武装进行 ────────────────────────────────────────────────
    {
        // t1( a) 上的光标 NON-active 列, 电流是单独的 线程.
        Model m = make_model(/*光标=*/1, /*时态=*/"other");

        // 初按:臂,去无.
        m = step(std::move(m), ThreadListDelete{});
        check(g_deleted.empty(), "first press deletes nothing");
        check(picker(m) && picker(m)->confirm_remove == "t1",
              "first press arms confirm_remove on the focused row id");
        check(m.d.threads.size() == 3, "first press leaves the list intact");

        // 第二媒体 相同 排: 承诺。
        m = step(std::move(m), ThreadListDelete{});
        check(g_deleted.size() == 1 && g_deleted[0] == "t1",
              "second press commits delete of the focused thread");
        check(m.d.threads.size() == 2, "row removed from the list");
        check(picker(m) && picker(m)->confirm_remove.empty(),
              "confirm_remove cleared after commit");
        bool gone = true;
        for (const auto& t : m.d.threads) if (t.id.value == "t1") gone = false;
        check(gone, "the deleted id is no longer in the list");
        // 选择面板仍然开放,当前没有变化(t1没有活动).
        check(picker(m) != nullptr, "picker stays open after a non-active delete");
        check(m.d.current.id.value == "other", "current thread untouched");
    }

    // ── 2. 动中解除武装 ─────────────────────────────────────────────────
    {
        install_stub_deps();
        Model m = make_model(1, "other");
        m = step(std::move(m), ThreadListDelete{});            // 臂 臂 转1
        check(picker(m)->confirm_remove == "t1", "armed");
        m = step(std::move(m), ThreadListMove{+1});            // 移动裁军
        check(picker(m)->confirm_remove.empty(), "move clears the pending delete");
        m = step(std::move(m), ThreadListDelete{});            // 现在刚刚是re-arms
        check(g_deleted.empty(), "a d after a move re-arms, does not delete");
    }

    // ── 3. 删除下列内容: 活动 线程 开始新 + 重排阶段 ─────────
    {
        install_stub_deps();
        // t0上的光标是 ALSO 活动 线程.
        Model m = make_model(/*光标=*/0, /*时态=*/"t0");
        // 模拟中间 -stream 删除: 相位 流式传输 进去了
        m.s.phase = phase::Streaming{};
        m = step(std::move(m), ThreadListDelete{});   // 手臂
        m = step(std::move(m), ThreadListDelete{});   // 承诺

        check(g_deleted.size() == 1 && g_deleted[0] == "t0", "active thread deleted");
        // A BRAND-NEW id,不是被删除的,也不是任何幸存的行.
        //
        // 以身份而不是以身份为主 "fresh-" 前缀从根部 :
        // new_thread_id 离开 Deps 接口边界 - 纯粹的功能,
        // 从不需要去除,所以 状态更新函数 调用
        // persistence::new_id ()直接和根子不再看到.
        // 重要的是财产, 这一点是明确的。
        check(!m.d.current.id.value.empty()
                  && m.d.current.id.value != "t0"
                  && m.d.current.id.value != "t1"
                  && m.d.current.id.value != "t2",
              "deleting the active thread starts a brand-new thread");
        check(m.d.current.messages.empty(), "the fresh thread is empty");
        check(std::holds_alternative<phase::Idle>(m.s.phase),
              "a mid-stream active-thread delete drops phase back to Idle");
        check(!m.ui.panel.is<agentty::ui::panel::ThreadList>(),
              "the picker closes on an active-thread delete (full reset_inline swap)");
    }

    // ── 4. 只向以下地区开火: 相同 行( 编号) -keyed 已确认) ───────────
    {
        install_stub_deps();
        Model m = make_model(0, "other");
        m = step(std::move(m), ThreadListDelete{});   // 臂转臂
        check(picker(m)->confirm_remove == "t0", "armed t0");
        // 跳转到 End (t2),然后按键 ^D : 不同的一行,所以是 -arms
        // t2 而不是删除 t0.
        m = step(std::move(m), ThreadListJump{ThreadListJump::Where::End});
        check(picker(m)->confirm_remove.empty(), "jump disarms t0");
        m = step(std::move(m), ThreadListDelete{});
        check(g_deleted.empty(), "no delete: the armed row lost focus before commit");
        check(picker(m)->confirm_remove == "t2", "^D on the new row arms it instead");
    }

    if (g_failed == 0) std::println("thread_delete_test: OK");
    return g_failed == 0 ? 0 : 1;
}
