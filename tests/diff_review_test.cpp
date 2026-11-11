// diff_review_test ——审查 -before-apply 特性,
// 结尾为结束。 -wired 流程:工具的 FileChange 土地在
// m.d.pending_changes ,且接受/拒绝 状态更新函数 坚持 RIGHT
// 内容到磁盘( 接受保留工具所写的, 拒绝返回原样)。
//
// 背景:人口途径(第12条) ToolExecOutput.change →
// apply_tool_output → pending_changes ) 很久以前又被退回,
// 留下整个diff -review 特性已死。此选项 锁 复活的线条 让它不能再静静
// 地死去

#include "agentty/runtime/app/update/internal.hpp"
#include "agtest_fx.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/diff/diff.hpp"

#include <map>
#include <optional>
#include <print>
#include <string>

namespace pn = agentty::ui::panel;

using namespace agentty;
namespace detail = agentty::app::detail;
namespace pick = agentty::ui::pick;

namespace {
int g_fail = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::println("  FAIL: {}", what); ++g_fail; }
}

// 抓取每个 write_file 编号 状态更新函数 ASKS FOR ,
// 这样我们就可以在不接触真实文件系统的情况下,断言什么会击中磁盘.
//
// 请求而非执行: 写入文件是 副作用 现在——
// 状态更新函数 返回描述写和主机执行的值。
// 这些实例坚持 g_writes ,所以他们必须扮演主机的角色,
// 这也是重点: 只有在手臂真正返回时,才出现一个字母
// it.
std::map<std::string, std::string> g_writes;

// 跑步,走步,走一步,走一步 跑步 副作用 输入 g_writes.
template <class S>
S&& apply_fx(S&& step) {
    agtest::fx::Store store;
    agtest::fx::run(step.second, store);
    for (auto& [path, contents] : store.written_files)
        g_writes[path] = contents;
    return std::forward<S>(step);
}

void install_stub_deps() {
    g_writes.clear();
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .write_file    = [](const std::string& p, const std::string& c) { g_writes[p] = c; },
        .auth          = {},
    });
}

// 一个有活工具的模型 调用 ,所以,我说 apply_tool_output 就能解决它。
Model with_live_tool(const char* id) {
    Model m;
    Message a; a.role = Role::Assistant;
    ToolUse tc; tc.id = ToolCallId{id}; tc.name = ToolName{"edit"};
    tc.status = ToolUse::Running{};
    a.tool_calls.push_back(std::move(tc));
    m.d.current.messages.push_back(std::move(a));
    return m;
}

FileChange make_change(const char* path, const std::string& before,
                       const std::string& after) {
    auto fc = diff::compute(path, before, after);
    fc.original_contents = before;
    fc.new_contents = after;
    return fc;
}
} // 命名空间结束。

int main() {
    const std::string before = "line one\nline two\nline three\n";
    const std::string after  = "line one\nline TWO\nline three\n";

    // ── 人口:工具的 FileChange 土地在 pending_changes ──────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"edited login.cpp"},
            make_change("login.cpp", before, after));
        check(m.d.pending_changes.size() == 1, "tool FileChange queued for review");
        check(!m.d.pending_changes.empty()
              && m.d.pending_changes[0].path == "login.cpp",
              "queued change carries the path");
        check(!m.d.pending_changes[0].hunks.empty(), "change has structured hunks");
    }

    // ── 解调: 两次编辑到 相同 文件崩溃到一个净条目 ──────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"e1"},
            make_change("f.txt", before, after));
        // 编辑的第二版基于第一个版本的输出
        const std::string after2 = "line ONE\nline TWO\nline three\n";
        Model m2 = with_live_tool("t2");
        m2.d.pending_changes = m.d.pending_changes;   // 随身携带队列
        detail::apply_tool_output(m2, ToolCallId{"t2"},
            std::expected<std::string, tools::ToolError>{"e2"},
            make_change("f.txt", after, after2));
        check(m2.d.pending_changes.size() == 1,
              "same-file edits dedup to one review entry");
        check(m2.d.pending_changes[0].original_contents == before,
              "dedup keeps the ORIGINAL first-seen contents");
        check(m2.d.pending_changes[0].new_contents == after2,
              "dedup reflects the newest contents");
    }

    // ── FAILED 工具队列无任何 ─────────────────────────────────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::unexpected(tools::ToolError::io("disk full")),
            make_change("nope.txt", before, after));
        check(m.d.pending_changes.empty(), "a failed tool queues no change");
    }

    // ── REJECT 全部 将每个文件返回磁盘上的原文件 ────────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("a.txt", before, after));
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        // 两位 -press 警卫(承诺7498bf3f):从 OPEN
        // 面板 第一次 第一次 ^X 臂(无写),第二个执行。
        // 调色板是 "Reject all" 发送 RejectAllChanges {
        // 确认=真}并绕过警卫 – 明确路径已经是故意的多路 -step.
        auto armed = apply_fx(detail::step(detail::diff_review_update, std::move(m), RejectAllChanges{}));
        check(g_writes.count("a.txt") == 0,
              "first reject-all press only arms, no write yet");
        check(armed.first.ui.panel.get<pn::DiffReview>()
                  && armed.first.ui.panel.get<pn::DiffReview>()->confirm_reject_all,
              "first reject-all press arms the confirm flag");
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(armed.first), RejectAllChanges{}));
        check(g_writes.count("a.txt") == 1, "reject-all wrote the file");
        check(g_writes["a.txt"] == before,
              "reject-all reverted the file to ORIGINAL contents");
        check(s.first.d.pending_changes.empty(), "queue cleared after reject-all");
        check(!s.first.ui.panel.is<agentty::ui::panel::DiffReview>(),
              "pane closes after reject-all");
    }

    // ── ACCEPT 全部 保留工具的写入( 不返回磁盘) ───────────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("b.txt", before, after));
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), AcceptAllChanges{}));
        check(g_writes.empty(), "accept-all writes nothing (tool already wrote)");
        check(s.first.d.pending_changes.empty(), "queue cleared after accept-all");
    }

    // ── 百分比 -hunk REJECT 然后关闭返回到 差异块 ────────────────
    {
        install_stub_deps();
        // 两个独立 差异块 在一个文件中—— 相隔甚远的更改,
        // 从而 diff 引擎会发出不同的 差异块 (需要 > 3 个没有变化的行).
        const std::string b =
            "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n";
        const std::string a =
            "ONE\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\nFIFTEEN\n";
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("c.txt", b, a));
        auto& fc0 = m.d.pending_changes[0];
        check(fc0.hunks.size() >= 2, "distinct edits produce >=2 hunks");
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        // 接受第一个差异块,拒绝第二个,然后关闭。
        auto s1 = apply_fx(detail::step(detail::diff_review_update, std::move(m), AcceptHunk{}));
        auto s2 = apply_fx(detail::step(detail::diff_review_update, std::move(s1.first), RejectHunk{}));
        auto s3 = apply_fx(detail::step(detail::diff_review_update, std::move(s2.first), CloseDiffReview{}));
        check(g_writes.count("c.txt") == 1, "close persisted the mixed decision");
        // 写入文件保存已接受 差异块 (一个 却使被否认的复返于(它),
        // FIFTEEN ——15):从 'ONE' ,结尾为 '15'.
        const std::string& w = g_writes["c.txt"];
        check(w.rfind("ONE", 0) == 0, "accepted hunk kept (ONE at top)");
        check(w.find("FIFTEEN") == std::string::npos, "rejected hunk reverted (no FIFTEEN)");
        check(w.find("\n15\n") != std::string::npos, "rejected line restored to 15");
    }

    // ── 百分比 -hunk ACCEPT 自动调试 最后 差异块 自动 -closes 编号 面板 ──────────
    // 转回 旋转 -pane 流动:带一个 差异块 左边 y/Enter 执行 和
    // 关闭一个新闻机——无额外 Esc 广告中的含义在 "apply" 财务报告
    // 和已审计财务报表 "keep rest".
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("d.txt", before, after));
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), AcceptHunk{}));
        // ACCEPTING 写什么也没有——工具已经将内容放入磁盘,
        // 所以 persist() 跳过文件( E)
        // "nothing to revert; disk is correct"
        // 接受合同的相同合同 -all 此处曾要求书面写出 "did it commit?"
        // 无法回答所有问题 -accepted 复议后,将测试永久保留为红色.
        // 调 -writing 相同 字节 也会对用户自工具运行以来
        // 所改变的任何东西进行拼写。
        check(g_writes.count("d.txt") == 0,
              "accepting the last hunk needs no write — disk already matches");
        check(s.first.d.pending_changes.empty(), "queue cleared on auto-close");
        check(!s.first.ui.panel.is<agentty::ui::panel::DiffReview>(),
              "pane closed itself after the last hunk was decided");
    }

    // ── 百分比 -hunk REJECT 最后一个 差异块 自动 -closes 也一样 ────────────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("e.txt", before, after));
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), RejectHunk{}));
        check(g_writes.count("e.txt") == 1 && g_writes["e.txt"] == before,
              "last-hunk reject reverted the file (auto-closed)");
        check(!s.first.ui.panel.is<agentty::ui::panel::DiffReview>(),
              "pane closed itself after the last reject");
    }

    // ── 中点 -review ,每张 -hunk 裁决确实如此 不 关闭 面板 ─────────
    {
        install_stub_deps();
        const std::string b = "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n";
        const std::string a = "ONE\n2\n3\n4\n5\n6\n7\n8\n9\nTEN\n";
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("m.txt", b, a));
        m.ui.panel.descend(agentty::ui::panel::DiffReview{{0, 0}});
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), AcceptHunk{}));
        check(s.first.ui.panel.is<agentty::ui::panel::DiffReview>(),
              "pane stays open while pending hunks remain");
        check(s.first.d.pending_changes.size() == 1, "queue kept mid-review");
    }

    // ── X 带 面板 CLOSED : 2个 -press 通过域名守护 ─────────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("x1.txt", before, after));
        check(m.ui.panel.get<pn::DiffReview>() == nullptr, "pane closed");
        auto armed = apply_fx(detail::step(detail::diff_review_update, std::move(m), RejectAllChanges{}));
        check(g_writes.count("x1.txt") == 0, "closed-pane X first press arms only");
        check(armed.first.d.reject_all_armed, "domain guard armed by first X");
        auto done = apply_fx(detail::step(detail::diff_review_update, std::move(armed.first), RejectAllChanges{}));
        check(g_writes.count("x1.txt") == 1 && g_writes["x1.txt"] == before,
              "second closed-pane X reverts the file");
        check(!done.first.d.reject_all_armed, "guard consumed after execution");
        check(!done.first.ui.panel.is<agentty::ui::panel::DiffReview>(),
              "reject-all from outside leaves no pane behind");
    }

    // ── a CONFIRMED 拒绝 -all (调色板行)跳过警卫 ────────
    // 调色板选择已经打开。。 Enter 准备在仪式上登场
    // MESSAGE 而不是电话前 -writing
    // 编号 状态更新函数 自己的守备状态, 这就是为什么
    // 它执行的第一步从 两者 面板 州级。
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("x6.txt", before, after));
        check(m.ui.panel.get<pn::DiffReview>() == nullptr, "pane closed");
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m),
                                       RejectAllChanges{/*确认=*/true}));
        check(g_writes.count("x6.txt") == 1 && g_writes["x6.txt"] == before,
              "confirmed reject-all reverts on the FIRST press");
        check(!s.first.d.reject_all_armed,
              "confirmed reject-all leaves no armed guard behind");
        check(s.first.d.pending_changes.empty(), "queue cleared");
    }

    // 除触发待确认的按键外，其他任何审查消息都解除确认保护。
    // (已关闭的表格) -pane X 守备跟随打开 -pane ^X 规则: 首个按臂,
    // 任意 其他 diff 语句 -review 信息——公开审查、接受、
    // 导航——解除武装,所以半个 -armed 破坏键无法在接下来的动作中存活下来.
    // 唯一一个关闭的 -pane 信件为 open/accept/reject ,
    // 所有这一切都应该清除或解决警卫。 )
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("x2.txt", before, after));
        m.d.reject_all_armed = true;
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), DiffReviewScroll{3}));
        check(!s.first.d.reject_all_armed,
              "any non-arming diff-review message disarms the closed-pane guard");
        check(g_writes.count("x2.txt") == 0, "no write on stray navigation");
        check(!s.first.d.pending_changes.empty(), "queue untouched");
    }

    // ── AcceptAll 消耗武装警卫(无) 过期 守护到下一个回合) ──
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("x3.txt", before, after));
        m.d.reject_all_armed = true;
        auto s = apply_fx(detail::step(detail::diff_review_update, std::move(m), AcceptAllChanges{}));
        check(!s.first.d.reject_all_armed, "accept-all consumed the armed guard");
        check(s.first.d.pending_changes.empty(), "accept-all cleared the queue");
    }

    // ── 新的呈文也清除了武装警卫(没有过期警卫) ─────
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("x5.txt", before, after));
        m.d.reject_all_armed = true;
        m.d.model_id = ModelId{"claude-sonnet-4-5"};  // 提交需要一个模型
        m.ui.composer.text = "next question";
        auto s = apply_fx(detail::step(detail::submit_message, std::move(m)));
        check(!s.first.d.reject_all_armed,
              "submitting clears the armed guard (implicit accept)");
        check(s.first.d.pending_changes.empty(), "submit cleared the queue");
    }

    // ── 提交新信件清除审查窗口 ──────────────
    // 脱衣舞 一个 窗口( 代理) -edits ——你的下
    // 一个消息;前进的暗示接受,所以排队不得横跨转弯.
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"ok"},
            make_change("z.txt", before, after));
        check(!m.d.pending_changes.empty(), "queued before submit");
        m.d.model_id = ModelId{"claude-sonnet-4-5"};  // 提交需要一个模型
        m.ui.composer.text = "next question";
        auto s = detail::step(detail::submit_message, std::move(m));
        check(s.first.d.pending_changes.empty(),
              "submitting a new message clears the pending-changes queue");
    }

    // ── 空模型 id: 提交必须NOT 启动转弯 ────────────────────
    // 当地人员返回 -preset "dead loop when prompted" :
    // 一个新选中的 llama.cpp/Ollama 已经 model_id="" 直到他们找到土地。
    // 发送 "model":"" 被服务器拒绝,然后重新 -fires
    // 编号 重试 机器永远。 submit_message 一定要
    // 拒绝 保留作曲的文字 不 推动用户消息/助理占位符。
    {
        install_stub_deps();
        Model m;
        m.d.model_id = ModelId{""};             // 尚未解决模式
        m.ui.composer.text = "hello local model";
        const std::size_t before_n = m.d.current.messages.size();
        auto s = detail::step(detail::submit_message, std::move(m));
        check(s.first.d.current.messages.size() == before_n,
              "empty-model submit pushes NO message (no dead-loop turn)");
        check(s.first.ui.composer.text == "hello local model",
              "empty-model submit keeps the composer text (nothing lost)");
    }

    // 完整状态更新路径：ToolExecOutput → tool_update → pending_changes。
    // (我的其他测试) 调用 apply_tool_output 直接; 这证明 状态更新函数
    // 手臂实际向前 e.change, i.e。该 分派-site 线条牵引).
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        ToolExecOutput e{
            ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"edited"},
            make_change("wired.cpp", before, after)};
        auto s = detail::step(detail::tool_update, std::move(m), msg::ToolMsg{std::move(e)});
        check(s.first.d.pending_changes.size() == 1,
              "ToolExecOutput through the reducer queues the change");
        check(!s.first.d.pending_changes.empty()
              && s.first.d.pending_changes[0].path == "wired.cpp",
              "the reducer forwarded the FileChange, not dropped it");
    }

    // ── 多个 -file 编辑( 替换) 队列 每个 已触摸的文件供审查 ───
    {
        install_stub_deps();
        Model m = with_live_tool("t1");
        std::vector<FileChange> multi;
        multi.push_back(make_change("a.ts", before, after));
        multi.push_back(make_change("b.ts", before, after));
        multi.push_back(make_change("c.ts", before, after));
        detail::apply_tool_output(m, ToolCallId{"t1"},
            std::expected<std::string, tools::ToolError>{"replaced across 3 files"},
            std::nullopt, std::move(multi));
        check(m.d.pending_changes.size() == 3,
              "a multi-file replace queues all 3 files");
        bool a=false,b=false,c=false;
        for (auto& fc : m.d.pending_changes) {
            if (fc.path == "a.ts") a = true;
            if (fc.path == "b.ts") b = true;
            if (fc.path == "c.ts") c = true;
        }
        check(a && b && c, "every replaced file is in the review queue");
    }

    if (g_fail == 0) std::println("diff_review_test: OK");
    return g_fail ? 1 : 0;
}
