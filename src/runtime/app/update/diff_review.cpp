// diff_review_update — 状态更新函数 对于 `msg::DiffReviewMsg`。
// 两个-axis 弹出面板超过（file_index，hunk_index）；突变 = per-hunk
// Accepted/Rejected 状态翻转； AcceptAll / RejectAll 立即迷恋每一
// 次 待处理 的变化。通过 no-change 路径上的 set_status_toast 发出状态 toast，
// 因此empty-state Enter 不会感到沉默。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/app/deps.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace pick = agentty::ui::pick;
using maya::overload;

Cmd diff_review_update(Model& m, msg::DiffReviewMsg dm) {
    // 将一个文件的REVIEW DECISION保留到磁盘。该工具在运行时已经
    // 写入了该文件，因此 `new_contents` 就是现在磁盘上的内容。
    // 接受=保留；拒绝=回复。 diff::apply_accepted() 仅将
    // 接受的 差异块 应用于原始文件来重建文件 - 因此，任何被拒绝的 差异块
    // 的文件都会被重写，并且 all-accepted 文件将保留为 -is。
    // 待处理(未决定)差异块被视为在关闭时被接受(改变
    // 已经生效；不触及它保留它)。
    // 将写入返回为VALUE；调用者将其批处理到其结果中，
    // 然后主机执行它。 none() 当文件不需要重写时。
    auto persist = [](const FileChange& fc) -> Cmd {
        bool any_reject = false;
        for (const auto& hk : fc.hunks)
            if (hk.status == Hunk::Status::Rejected) { any_reject = true; break; }
        if (!any_reject) return Cmd::none();     // 没有什么可恢复的；磁盘正确
        return Cmd(WriteFile{fc.path, diff::apply_accepted(fc)});
    };
    // 将光标前进到当前文件中的下一个 still-PENDING 差异块，
    // 以便决策向前传递给审阅者（如接受 git add -p）。
    // 如果该文件中没有剩余内容，则跳至下一个带有 待处理 差异块 的文件；包裹。
    auto advance = [&](pick::OpenAtCell* c) -> bool {
        const int nfiles = static_cast<int>(m.d.pending_changes.size());
        for (int fo = 0; fo < nfiles; ++fo) {
            int fi = (c->file_index + fo) % nfiles;
            const auto& hunks = m.d.pending_changes[static_cast<std::size_t>(fi)].hunks;
            int start = (fo == 0) ? c->hunk_index : 0;
            for (int ho = 0; ho < static_cast<int>(hunks.size()); ++ho) {
                int hi = (start + ho) % static_cast<int>(hunks.size());
                if (hunks[static_cast<std::size_t>(hi)].status == Hunk::Status::Pending) {
                    c->file_index = fi; c->hunk_index = hi;
                    c->body_scroll = 0;   // 新鲜差异块→顶
                    return true;
                }
            }
        }
        // 任何地方都没有待处理——所有差异块都已决定。
        // 调用者提交并关闭，而不是将光标停放在已经-decided
        // 差异块上。
        return false;
    };

    // 提交每一个决定并关闭面板。当确定最后一个 待处理 差异块 时，
    // 由 CloseDiffReview (Esc/^R) 和 per-hunk 处理
    // 函数' auto-close 共享 — 流程不得依赖于用户知道哪个击键恰好是
    // 最后一个击键。 `done` 选择 toast 动词："review complete"，
    // 当每个 差异块 被决定时 (auto-close)，而 "review closed"，
    // 当用户在 -review 中保留其余部分时 (Esc/
    // ^R)。
    auto commit_and_close = [&](bool done) -> Cmd {
        int reverted = 0, kept = 0;
        std::vector<Cmd> writes;
        for (const auto& fc : m.d.pending_changes) {
            for (const auto& hk : fc.hunks) {
                if (hk.status == Hunk::Status::Rejected) ++reverted;
                else ++kept;   // 已接受 OR 待处理 — 均保持有效
            }
            writes.push_back(persist(fc));
        }
        m.d.pending_changes.clear();
        m.d.reject_all_armed = false;   // 审查已解决 → 防护消耗
        ascend(m);   // 作出上述决定； Esc 降落在您来自的地方
        auto cmd = set_status_toast(m,
            reverted == 0
                ? (done ? "review complete — all " : "review closed — all ")
                    + std::to_string(kept)
                    + (kept == 1 ? " change" : " changes") + " kept"
                : (done ? "review complete — " : "review closed — ")
                    + std::to_string(reverted)
                    + " undone, " + std::to_string(kept) + " kept");
        writes.push_back(std::move(cmd));
        return Cmd::batch(std::move(writes));
    };

    // 将可能-stale的光标夹在CURRENT变更集上。当 面板 打开时，
    // 差异块 计数可能会缩小：对同一路径的新工具编辑会折叠到其现有审阅条目中
    // (*it = diff::compute(原始，最新))，
    // 这可能会产生比光标索引更少的 差异块。 渲染 的视野处于防御状态；
    // 状态更新函数 也必须如此，否则 fc.hunks[c->hunk_index]
    // 会在下一个 y/n/Enter. 上出现 -of-bounds UB
    auto clamp_cursor = [&](pick::OpenAtCell* c) -> FileChange* {
        if (!c || m.d.pending_changes.empty()) return nullptr;
        const int nfiles = static_cast<int>(m.d.pending_changes.size());
        if (c->file_index >= nfiles) c->file_index = nfiles - 1;
        if (c->file_index < 0)       c->file_index = 0;
        auto& fc = m.d.pending_changes[static_cast<std::size_t>(c->file_index)];
        const int nh = static_cast<int>(fc.hunks.size());
        if (c->hunk_index >= nh) c->hunk_index = nh > 0 ? nh - 1 : 0;
        if (c->hunk_index < 0)   c->hunk_index = 0;
        return &fc;
    };

    // 除了确认第二次按下之外，在 任意 diff-review 动作上解除两个
    // -press 防护装置 - 误行的第一次按下不得在 j/k 或卷轴后面留下活
    // 的 "next X/^X nukes everything" 陷阱。两名警卫：
    // 面板内部(confirm_reject_all在存储单元上)和其
    // 外部(域上的reject_all_armed，对于当面板关闭时的X)。
    const bool arming =
        std::holds_alternative<RejectAllChanges>(dm);
    if (!arming) {
        if (auto* c = m.ui.panel.get<pn::DiffReview>())
            c->confirm_reject_all = false;
        m.d.reject_all_armed = false;
    }

    return std::visit(overload{
        [&](OpenDiffReview) -> Cmd {
            // 告诉用户什么时候没有什么可查看的，
            // 而不是默默地什么也不做——打开一个空的
            // 面板只会使屏幕闪烁并使他们对他们的击键
            // 是否注册感到困惑。
            if (m.d.pending_changes.empty()) {
                auto cmd = set_status_toast(m, "no pending changes to review");
                return cmd;
            }
            m.ui.panel.descend(pn::DiffReview{{0, 0}});
            return Cmd::none();
        },
        [&](CloseDiffReview) -> Cmd {
            // 在退出时保留每个文件的决定，然后清除队列——
            // 关闭面板提交审查。说 内容 关闭意味着：未
            // 决定的 差异块 被保留（更改已经存在于磁盘上），
            // 除非我们宣布它，否则这是不可见的。
            return commit_and_close(/*完成=*/false);
        },
        [&](DiffReviewMove& e) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            auto* fc = clamp_cursor(c);
            if (!fc) return Cmd::none();
            int sz = static_cast<int>(fc->hunks.size());
            if (sz == 0) return Cmd::none();
            c->hunk_index = (c->hunk_index + e.delta + sz) % sz;
            c->body_scroll = 0;   // 新的-focused差异块从其顶部开始
            return Cmd::none();
        },
        [&](DiffReviewScroll& e) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            auto* fc = clamp_cursor(c);
            if (!fc || fc->hunks.empty()) return Cmd::none();
            const auto& hk =
                fc->hunks[static_cast<std::size_t>(c->hunk_index)];
            // 差异块的正文行的上限：其补丁行计数。
            // 视图精确地钳制解析的行计数；
            // 这只是防止偏移无限制地逃跑。
            const int max_rows = static_cast<int>(
                std::count(hk.patch.begin(), hk.patch.end(), '\n')) + 1;
            c->body_scroll = std::clamp(c->body_scroll + e.delta,
                                        0, std::max(0, max_rows - 1));
            return Cmd::none();
        },
        [&](DiffReviewNextFile) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            if (!c || m.d.pending_changes.empty()) return Cmd::none();
            int sz = static_cast<int>(m.d.pending_changes.size());
            c->file_index = (c->file_index + 1) % sz;
            c->hunk_index = 0;
            c->body_scroll = 0;
            return Cmd::none();
        },
        [&](DiffReviewPrevFile) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            if (!c || m.d.pending_changes.empty()) return Cmd::none();
            int sz = static_cast<int>(m.d.pending_changes.size());
            c->file_index = (c->file_index - 1 + sz) % sz;
            c->hunk_index = 0;
            c->body_scroll = 0;
            return Cmd::none();
        },
        [&](AcceptHunk) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            if (auto* fc = clamp_cursor(c)) {
                if (!fc->hunks.empty())
                    fc->hunks[static_cast<std::size_t>(c->hunk_index)].status =
                        Hunk::Status::Accepted;
                // 最后做出的决定 → 现在提交并关闭 面板。没有下一个
                // 差异块 可以登陆，因此要求额外的 Esc（其在非
                // -final 按键上的其他含义是 "reject the rest"）
                // 会将两个相反的功能训练到一个键中。
                if (!advance(c)) return commit_and_close(/*完成=*/true);
            }
            return Cmd::none();
        },
        [&](RejectHunk) -> Cmd {
            auto* c = m.ui.panel.get<pn::DiffReview>();
            if (auto* fc = clamp_cursor(c)) {
                if (!fc->hunks.empty())
                    fc->hunks[static_cast<std::size_t>(c->hunk_index)].status =
                        Hunk::Status::Rejected;
                if (!advance(c)) return commit_and_close(/*完成=*/true);
            }
            return Cmd::none();
        },
        [&](AcceptAllChanges) -> Cmd {
            if (m.d.pending_changes.empty()) {
                auto cmd = set_status_toast(m, "no pending changes to accept");
                return cmd;
            }
            // 接受=保留工具已经写入的内容；没有什么可以坚持的。
            int hunks = 0;
            for (auto& fc : m.d.pending_changes)
                for (auto& h : fc.hunks) { h.status = Hunk::Status::Accepted; ++hunks; }
            m.d.pending_changes.clear();
            m.d.reject_all_armed = false;   // 任何决议都会消耗守卫
            ascend(m);   // 作出上述决定； Esc 降落在您来自的地方
            auto cmd = set_status_toast(m,
                "accepted " + std::to_string(hunks)
                + (hunks == 1 ? " hunk" : " hunks"));
            return cmd;
        },
        [&](RejectAllChanges rm) -> Cmd {
            if (m.d.pending_changes.empty()) {
                auto cmd = set_status_toast(m, "no pending changes to reject");
                return cmd;
            }
            // 两个-PRESS 关键和弦的保护。在 面板 (^X) 内，
            // 第一个压力机装备 存储单元 标志，第二个压力机执行；
            // 从更改条（^X，面板 关闭）开始，相同的协议在
            // m.d.reject_all_armed 上运行。无论哪种方式，
            // 单次按键都不能恢复每个触摸的文件。镜像线程 选择面板 的two
            // -press 删除。
            //
            // `rm.confirmed`绕过守卫：调色板的
            // "Reject all"行已经打开→选择→Enter，
            // 因此要求第二次按下将会有仪式。意图依赖于 MESSAGE，
            // 而不是由调用者将 -written 预置到 Model 中 —
            // 状态更新函数 仍然是其自身保护状态的唯一写入者。
            if (!rm.confirmed) {
                if (auto* c = m.ui.panel.get<pn::DiffReview>()) {
                    // 在面板内部：在第一次按下时装备存储单元 标志。
                    if (!c->confirm_reject_all) {
                        c->confirm_reject_all = true;
                        auto cmd = set_status_toast(m,
                            "press ^X again to revert ALL changes — any other key cancels");
                        return cmd;
                    }
                } else if (!m.d.reject_all_armed) {
                    // 在面板之外：在第一次按下时布防域标志。
                    // 守卫会被任何审核决议（^X、^A、^R、
                    // new-turn 提交）消耗，因此它永远无法
                    // 生存到稍后的回合并在不相关的键上开火。
                    m.d.reject_all_armed = true;
                    auto cmd = set_status_toast(m,
                        "press ^X again to revert ALL changes");
                    return cmd;
                }
            }
            // 拒绝ALL=将每个触及的文件恢复到其在磁盘上的原始内容（工具已经
            // 写入了新版本，因此这将撤消它们）。 apply_accepted
            // () 与每个 差异块 被拒绝都会准确地产生
            // original_contents。
            int hunks = 0, files = 0;
            std::vector<Cmd> writes;
            for (auto& fc : m.d.pending_changes) {
                for (auto& h : fc.hunks) { h.status = Hunk::Status::Rejected; ++hunks; }
                writes.push_back(Cmd(WriteFile{fc.path, fc.original_contents}));
                ++files;
            }
            m.d.pending_changes.clear();
            m.d.reject_all_armed = false;   // 处决→守卫消耗
            ascend(m);   // 作出上述决定； Esc 降落在您来自的地方
            auto cmd = set_status_toast(m,
                "reverted " + std::to_string(hunks)
                + (hunks == 1 ? " hunk" : " hunks")
                + " across " + std::to_string(files)
                + (files == 1 ? " file" : " files"));
            writes.push_back(std::move(cmd));
            return Cmd::batch(std::move(writes));
        },
    }, dm);
}

} // namespace agentty::app::detail
