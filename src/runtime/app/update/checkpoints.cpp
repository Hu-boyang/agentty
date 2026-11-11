// checkpoint_update-状态更新函数用于倒带检查点选择面板。
//
// 每个用户在 git 仓库 内提交的轮流都会在其上方固定一个工作树 快照 和
// 渲染 一个检查点分隔符；这个 选择面板 使这些点中的 全部 可以达到（调
// 色板的旧 "Rewind" 只能达到最新的）。 Open 在每个检查点回合
// 构建一个条目，然后启动异步 per-entry 差异摘要 (
// 从此处开始，工作树发生了哪些变化？
// 以便该行可以显示 "N files · +A −D"，而不会阻塞大
// 仓库 上的打开。 Select 将突出显示的条目映射到现有的
// RestoreCheckpoint 流程 (meta.cpp)
// - 破坏性的工作树 + 脚本倒带，未更改。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/checkpoints.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/workspace/checkpoint.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace cp = agentty::checkpoints;
using maya::overload;

namespace {

// 展平 提示词 的第一行以进行行预览：标签 占位符成为其可见
// 标签（原始 \x01ATT:N\x01 标记将 渲染 作为垃圾），
// 然后剪辑到第一个换行符。反映了 CheckpointRestored
// 中作曲家的笔芯扁平化，因此预览和笔芯一致。
std::string preview_of(const Message& msg) {
    std::string flat;
    flat.reserve(msg.text.size());
    for (std::size_t i = 0; i < msg.text.size(); ) {
        if (static_cast<unsigned char>(msg.text[i]) == attachment::kSentinel) {
            auto len = attachment::placeholder_len_at(msg.text, i);
            if (len > 0) {
                auto idx = attachment::placeholder_index(msg.text, i);
                if (idx < msg.attachments.size()) {
                    flat.push_back('[');
                    flat.append(attachment::chip_label(msg.attachments[idx]));
                    flat.push_back(']');
                }
                i += len;
                continue;
            }
        }
        char c = msg.text[i++];
        if (c == '\n') break;   // 仅第一行
        flat.push_back(c);
    }
    // 修剪前导空白，使得以 chip/newline 开头的 提示词
    // 仍然可以清晰地读取。
    std::size_t start = flat.find_first_not_of(" \t");
    if (start == std::string::npos) return "(no prompt text)";
    flat = flat.substr(start);
    // 限制预览，使得墙-of-text 提示词不能占据该行。
    // maya的选择面板无论如何都会将前导存储单元剪辑到行宽度，
    // 但是这里的硬字符上限使条目列表的布局成本低廉，
    // 并且甚至在宽泛的范围内为异步差异统计尾部存储单元提供了空间。
    // UTF-8-safe：在附加省略号之前备份任何延续字节，
    // 因此我们永远不会将多字节代码点切成两半。
    constexpr std::size_t kMaxPreview = 96;
    if (flat.size() > kMaxPreview) {
        std::size_t cut = kMaxPreview;
        while (cut > 0 && (static_cast<unsigned char>(flat[cut]) & 0xC0) == 0x80)
            --cut;
        flat.erase(cut);
        flat += "\xe2\x80\xa6";   // …
    }
    return flat;
}

// 从当前线程的检查点用户轮次、最旧的-first
// (最新的区域在底部、最接近作曲家)构建条目列表。
std::vector<cp::Entry> build_entries(const Model& m) {
    std::vector<cp::Entry> out;
    int user_turn = 0;
    for (const auto& msg : m.d.current.messages) {
        if (msg.role == Role::User) ++user_turn;
        if (msg.role != Role::User || !msg.checkpoint_id) continue;
        cp::Entry e;
        e.id      = *msg.checkpoint_id;
        e.turn    = user_turn;
        e.preview = preview_of(msg);
        e.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.timestamp.time_since_epoch()).count();
        out.push_back(std::move(e));
    }
    return out;
}

// 每个条目一个Cmd:从UI 线程和分派 CheckpointDiffLoaded
// 后面计算其差异摘要。 checkpoint_summary shells
// git，因此它绝不能在 状态更新函数 上运行。对隔离的 池 上的
// 所有条目 加载 进行并行批处理。
Cmd load_all_diffs(const std::vector<cp::Entry>& entries) {
    std::vector<Cmd> parts;
    parts.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        parts.push_back(Cmd::task_isolated(
            [](maya::Sink<Msg> out, std::stop_token, int i, std::string id) {
                auto d = workspace::checkpoint_summary(id);
                CheckpointDiffLoaded ev;
                ev.index         = i;
                ev.ok            = d.valid;
                ev.files_changed = d.files_changed;
                ev.insertions    = d.insertions;
                ev.deletions     = d.deletions;
                out.send(Msg{std::move(ev)});
            },
            i, entries[static_cast<std::size_t>(i)].id.value));
    }
    return parts.empty() ? Cmd::none() : Cmd::batch(std::move(parts));
}

} // 命名空间结束。

Cmd checkpoint_update(Model& m, msg::CheckpointMsg cm) {
    return std::visit(overload{
        [&](OpenCheckpoints) -> Cmd {
            // 倒带重写工作树+转录本——仅从已确定的会话提供
            // 它（镜像RestoreCheckpoint门）。
            if (!m.s.is_idle() || m.s.compacting || m.s.thread_loading) {
                return set_status_toast(m, "cannot rewind while the agent is working");
            }
            if (!m.ui.git_repo) {
                return set_status_toast(m, "still checking for a git repo");
            }
            if (!*m.ui.git_repo) {
                return set_status_toast(m, "checkpoints need a git repo");
            }
            auto entries = build_entries(m);
            if (entries.empty()) {
                return set_status_toast(m, "no checkpoints in this thread yet");
            }
            // 在最新的（最后的）条目上打开——最常见的倒带目标，
            // 并且是旧的单一-shot路径始终采用的目标。
            const int last = static_cast<int>(entries.size()) - 1;
            auto diffs = load_all_diffs(entries);
            m.ui.panel.descend(pn::Checkpoints{{std::move(entries), last}});
            m.ui.checkpoints_scroll = Model::UI::routed_scroll();
            return diffs;
        },
        [&](CloseCheckpoints) -> Cmd {
            ascend(m);   // Esc：返回打开此的调色板，或关闭
            return Cmd::none();
        },
        [&](CheckpointsMove& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Checkpoints>();
            if (!o || o->entries.empty()) return Cmd::none();
            const int n = static_cast<int>(o->entries.size());
            o->index = ui::step(o->index, e.delta, n);
            return Cmd::none();
        },
        [&](LibraryLoaded& e) -> Cmd {
            m.ui.library = std::move(e.library);
            return Cmd::none();
        },
        [&](RepoProbed& e) -> Cmd {
            m.ui.git_repo = e.in_repo;
            return Cmd::none();
        },
        [&](TerminalScrollbackProbed& e) -> Cmd {
            m.ui.terminal_scrollback = {e.rows, e.unlimited, e.exact, std::move(e.source)};
            return Cmd::none();
        },
        [&](CheckpointDiffLoaded& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Checkpoints>();
            if (!o) return Cmd::none();   // 选择面板 中期 -load 关闭
            if (e.index < 0 || e.index >= static_cast<int>(o->entries.size()))
                return Cmd::none();
            auto& en = o->entries[static_cast<std::size_t>(e.index)];
            if (!e.ok) {
                en.diff_state = cp::Entry::DiffState::Failed;
                return Cmd::none();
            }
            en.diff_state    = cp::Entry::DiffState::Ready;
            en.files_changed = e.files_changed;
            en.insertions    = e.insertions;
            en.deletions     = e.deletions;
            en.clean         = (e.files_changed == 0);
            return Cmd::none();
        },
        [&](CheckpointsSelect) -> Cmd {
            auto* o = m.ui.panel.get<pn::Checkpoints>();
            if (!o || o->entries.empty()
                || o->index < 0 || o->index >= static_cast<int>(o->entries.size())) {
                m.ui.panel.close<pn::Checkpoints>();
                return Cmd::none();
            }
            auto id = o->entries[static_cast<std::size_t>(o->index)].id;
            m.ui.panel.close<pn::Checkpoints>();
            // 移交给现有的，battle-tested 倒带：它在空闲时重新
            // -gates，恢复孤立的工作线程 上的文件，然后截断转录+在
            // CheckpointRestored 中重新填充作曲家。
            // RestoreCheckpoint生活在META域中，而不是这个域。
            return meta_update(m, msg::MetaMsg{RestoreCheckpoint{std::move(id)}});
        },
    }, cm);
}

} // namespace agentty::app::detail
