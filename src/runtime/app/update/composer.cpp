// composer_update — 状态更新函数 费用 `msg::ComposerMsg`。
// 作曲者纯粹是本地的 UI 状态(文本、光标、扩展) 标志 ,队列项目、附件,
// undo/redo 堆栈,历史索引; 这里的手臂无法伸入网络 / 流式传输 /工具。 / 工具。
// ComposerEnter / ComposerSubmit 路径通过
// detail::submit_message 处理范围更广的 "kick a new turn" 流动。

#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>

#include <maya/terminal/tmux.hpp>
#include <maya/terminal/ansi.hpp>   // env_supports_osc5522 - 发现猫咪
#include <maya/core/overload.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"   // cmd::refresh_oauth
#include "agentty/auth/auth.hpp"                  // oauth_proactive_refresh_token
#include "agentty/io/clipboard.hpp"
#include "agentty/util/logx.hpp"          // AGT_LOG - 贴面 -path 追踪
#include "agentty/provider/selection.hpp"   // prewarm_active_provider
#include "agentty/util/env.hpp"
#include "agentty/util/image_dims.hpp"
#include "agentty/runtime/panel/palette.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/panel/mention.hpp"
#include "agentty/runtime/panel/symbol.hpp"
#include "agentty/runtime/slash.hpp"
#include "agentty/workspace/files.hpp"
#include "agentty/workspace/symbols.hpp"
#include "agentty/runtime/view/helpers.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

// 图像 -paste 检测 终端 括号 -paste 交付 UTF
// -8文本只使用——附加一个图像,用户将文件的*path*(drag
// - onto)丢弃. -terminal,
// "copy as path" 随你怎么说,我接受它,如果 载荷 是一个单线条,
// 命名一个常规文件 工作区 '文件系统, 并开始一个公认的
// 图像魔法 -byte 前缀。
const char* detect_image_media_type(std::string_view bytes) noexcept {
    auto u = [&](std::size_t i){ return static_cast<unsigned char>(bytes[i]); };
    // PNG:89 50 4E 47 0D 0A 1A 0A
    if (bytes.size() >= 8 && u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E
        && u(3) == 0x47 && u(4) == 0x0D && u(5) == 0x0A && u(6) == 0x1A
        && u(7) == 0x0A) return "image/png";
    // JPEG: FF D8 FF
    if (bytes.size() >= 3 && u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF)
        return "image/jpeg";
    // 加纳工业联合会87a/
    if (bytes.size() >= 6
        && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F'
        && bytes[3] == '8' && (bytes[4] == '7' || bytes[4] == '9')
        && bytes[5] == 'a') return "image/gif";
    // WEBP: "RIFF" .... "WEBP"
    if (bytes.size() >= 12
        && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F'
        && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
        return "image/webp";
    return nullptr;
}


namespace {

constexpr std::size_t kUndoDepth = 64;

// 快照 当前的作曲家 载荷 进入撤消堆栈并清除重做堆栈。
// 附件为附件 -only 现场 运行时 级别( 后空格
// 大于 a) 标签 删除占位符 字节 发件人 `text`
// 但它将附件对象留在向量中——孤儿是 GC 以编曲
// 清晰的方式进行,所以只存储矢量的 * 大小*。
// 快照 时间足够恢复到逆向状态。
//
// `coalesce` : 当真实( 运行普通的自我)
// -inserting 之前的操作是: ALSO 输入可连接的编辑,
// 我们跳过按新键 快照 所以一个 Ctrl +Z 倒转整个
// word/run 而不是一个单一字符。 -coalescing 执行
// 部分第1段(paste, 删除, 标签 插入,光标跳转,
// 提交)总是会断开运行,因此结构编辑会保持单个可撤销性——标准编辑器语义.
void push_undo(ComposerState& cs, bool coalesce = false) {
    if (coalesce && cs.undo_coalescing && !cs.undo_stack.empty()) {
        // 继续当前打字运行: 快照 已经放在堆栈的
        // 顶部是预 -run 状态我们要恢复到。
        // 只要保持重做堆栈的清除( 这仍然是一个新的编辑)。
        cs.redo_stack.clear();
        return;
    }
    if (cs.undo_stack.size() >= kUndoDepth) {
        // 丢掉最古老的 快照。 开始抹去的 是O( N)
        // 在一个矢量上, 但 N =  64 这里,
        // 所以成本与 换乘 的 代用 相比是微不足道的。
        cs.undo_stack.erase(cs.undo_stack.begin());
    }
    ComposerState::Snapshot s;
    s.text   = cs.text;
    s.cursor = cs.cursor;
    s.attachments = cs.attachments;
    cs.undo_stack.push_back(std::move(s));
    cs.redo_stack.clear();
    cs.undo_coalescing = coalesce;
}

// 回到原点 LIVE 草稿和丢弃回合 -trip 快照.
//
// 这两种功能——reset_history和
// reset_queue_peek——只有它们设定到-1的哨兵才有所不同;
// 两者都清除了快照. 随着浏览到变体,有一个"stop browsing"过渡,
// 所以有一个功能.
//
// 为什么任何文本编辑都会触发它：当作曲家显示拉出的-up历史项目或查看的队列槽时，
// 编辑仍在编辑THAT项目。但一旦用户只是简单地打字，
// 提交的 "re-queue the peeked slot, removing its original"
// 合同就不再符合他们的心理模型——他们认为自己
// 正在撰写一条新消息。返回到“实时”会使编辑后
// 的 ​​字节 成为实时草稿，因此杂乱的击键无法在
// 提交时默默地删除错误的队列条目。
void reset_browsing(ComposerState& cs) {
    cs.browsing = ComposerState::Live{};
    cs.draft_save.reset();
    cs.draft_save_attachments.clear();
}

// begin_edit用于STRUCTURAL编辑（粘贴、
// 删除、标签插入）：推送从不与邻居合并的独立撤消快照。
void begin_edit(ComposerState& cs) {
    push_undo(cs);
    reset_browsing(cs);
    reset_browsing(cs);
}

// begin_edit用于self-inserting击键：将连续
// 键入合并到单个撤消单元中（参见push_undo)。
void begin_typing_edit(ComposerState& cs) {
    push_undo(cs, /*合并=*/true);
    reset_browsing(cs);
    reset_browsing(cs);
}

// Word-boundary光标移动。边界是一系列空白； 标签 占位符算作
// 单个导航单元（委托给 ui::chip_prev / chip_next）。
// 反映了 `vim`/`bash` Ctrl+W 的想法：
// 跳过空白，然后跳过单词字符。
bool is_word_char(unsigned char c) noexcept {
    return std::isalnum(c) || c == '_';
}

int word_left(std::string_view s, int pos) noexcept {
    if (pos <= 0) return 0;
    // 如果光标位于芯片的右边缘，则跨过芯片。
    int chip = ui::chip_prev(s, pos);
    if (chip != pos - 1
        || (pos > 0 && static_cast<unsigned char>(s[pos - 1]) == 0x01))
        return chip;
    int p = pos;
    // 跳过尾随空白。
    while (p > 0 && std::isspace(static_cast<unsigned char>(s[p - 1]))) --p;
    // 跳过一系列单词字符。
    while (p > 0 && is_word_char(static_cast<unsigned char>(s[p - 1]))) --p;
    // 如果我们没有移过任何单词-like，则跳过RUN
    // 标点符号（"))))"是一个单位，
    // 匹配word/whitespace运行的消耗方式），因此光标前进一个连贯的标记。
    if (p == pos) {
        while (p > 0
               && !is_word_char(static_cast<unsigned char>(s[p - 1]))
               && !std::isspace(static_cast<unsigned char>(s[p - 1]))
               && static_cast<unsigned char>(s[p - 1]) != 0x01)
            --p;
    }
    return p;
}

int word_right(std::string_view s, int pos) noexcept {
    int n = static_cast<int>(s.size());
    if (pos >= n) return n;
    int chip = ui::chip_next(s, pos);
    if (chip != pos + 1
        || (pos < n && static_cast<unsigned char>(s[pos]) == 0x01))
        return chip;
    int p = pos;
    while (p < n && is_word_char(static_cast<unsigned char>(s[p]))) ++p;
    while (p < n && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    // 没有消耗word/whitespace——跳过标点符号的RUN作为
    // 一个单元（与word_left对称）。
    if (p == pos) {
        while (p < n
               && !is_word_char(static_cast<unsigned char>(s[p]))
               && !std::isspace(static_cast<unsigned char>(s[p]))
               && static_cast<unsigned char>(s[p]) != 0x01)
            ++p;
    }
    return p;
}

// 该粘贴可能是放置的图像路径吗？仅进行廉价的词法检查
// （修剪一行，不要太长）；文件读取 工作线程。
// 保持每个普通文本粘贴同步。
bool may_be_image_path(std::string_view text) noexcept {
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    const auto t = text.substr(a, b - a);
    if (t.empty() || t.size() > 4096) return false;
    if (t.find('\n') != std::string_view::npos) return false;
    // 路径具有分隔符或扩展名；粘贴的单词两者都没有。
    return t.find_first_of("/\\.") != std::string_view::npos;
}

// 在活动线程中构建用户-message文本引用的反向-chronological列表。
// 用于↑/↓历史行走。我们故意不进行重复数据
// 删除（用户 带类型的 "y" 三次到 重
// 试 期望所有三个都可以访问 - 即使文本相同，
// 每个的编辑上下文也不同）。
//
// 返回对(文本、附件)的引用。每个用户消息将其文本保留在 标签-form
// （占位符 + `attachments` 向量）中；恢复两者都会保留
// round-trip 非-destructive - 召回消息
// 渲染 为 标签，编辑为 标签，re-submits 为 标签。
struct HistoryEntryRef {
    const std::string*             text;
    const std::vector<Attachment>* attachments;
};
std::vector<HistoryEntryRef> previous_user_texts(const Model& m) {
    std::vector<HistoryEntryRef> out;
    out.reserve(m.d.current.messages.size() / 2);
    for (auto it = m.d.current.messages.rbegin();
         it != m.d.current.messages.rend(); ++it) {
        // 主动<retrieved-context>块是模型视为参考
        // 的合成用户消息(Role::User+proactive_context)，
        // 而不是用户的话语。它们必须 绝不 出现在作曲家的 ↑
        // /↓ 历史回忆中 - 回忆会粘贴原始围栏块，
        // 就好像用户拥有 带类型的 一样。
        if (it->role == Role::User && !it->is_proactive_context() && !it->fork_note && !it->text.empty())
            out.push_back({&it->text, &it->attachments});
    }
    return out;
}

void apply_history_entry(ComposerState& cs, const HistoryEntryRef& entry) {
    cs.text        = *entry.text;
    cs.attachments = *entry.attachments;
    cs.cursor      = static_cast<int>(cs.text.size());
    if (cs.text.find('\n') != std::string::npos) cs.expanded = true;
}

// 通过richpaste从剪贴板粘贴：通过ssh从本地显示器或
// 笔记本电脑的代理复制文件，或者图像，或者文本。由 Ctrl+V
// / Alt+V 和空括号粘贴（吞下图像的 终端）使用。
// 读取在 工作线程 上运行并返回为 ClipboardRead。
Cmd smart_paste_from_clipboard(Model&) { return cmd::read_clipboard(); }

// 一张图像进入作曲家作为标签。如果太大而无法发送，则返回一个 toast。
Cmd insert_image(Model& m, std::string bytes, std::string media_type,
                 std::string where) {
    {
        begin_edit(m.ui.composer);
        // 从头文件读取真实像素尺寸。 hi-DPI 屏幕截图是一个很小的文件，
        // 但可以有 3000+ px 宽，Anthropic 在 Many
        // -image 请求中拒绝它 - 传输 会丢弃它，因此警告 当前 而不是
        // 让转弯默默地丢失图像（或者，pre-gate，直接 400）。
        const auto dims = util::image_dimensions(bytes);
        const bool oversized = dims.known()
                            && dims.longest() > util::kMaxWireImageSide;
        Attachment att;
        att.kind       = Attachment::Kind::Image;
        att.path       = std::move(where);
        att.media_type = std::move(media_type);
        att.byte_count = bytes.size();
        att.body.set_bytes(std::move(bytes));
        std::size_t idx = m.ui.composer.attachments.size();
        m.ui.composer.attachments.push_back(std::move(att));
        auto placeholder = attachment::make_placeholder(idx);
        m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
        m.ui.composer.cursor += static_cast<int>(placeholder.size());
        m.ui.composer.expanded = true;
        const auto& mt = m.ui.composer.attachments.back().media_type;
        if (mt != "image/png" && mt != "image/jpeg"
            && mt != "image/gif" && mt != "image/webp") {
            return set_status_toast(
                m, "Image is " + mt + " — models only accept PNG, JPEG, GIF "
                   "or WebP, so it won't be sent. Convert it first.",
                std::chrono::seconds{8});
        }
        if (oversized) {
            return set_status_toast(
                m, "Image is " + std::to_string(dims.w) + "\xc3\x97"
                + std::to_string(dims.h) + " px — over the "
                + std::to_string(util::kMaxWireImageSide)
                + " px limit, it won't be sent. Resize it first.",
                std::chrono::seconds{8});
        }
        return Cmd::none();
    }
}

// 将剪贴板项目按顺序放入编辑器中。文本通过 ComposerPaste
// 臂返回（换行清理，标签）； path_checked，
// 因此不会再次嗅探粘贴的路径。
Cmd insert_items(Model& m, std::vector<ClipboardItem> items, std::string_view where) {
    std::vector<Cmd> cmds;
    for (auto& it : items) {
        if (it.kind == ClipboardItem::Kind::Image)
            cmds.push_back(insert_image(m, std::move(it.bytes), std::move(it.mime),
                                        std::string{where}));
        else if (!it.bytes.empty())
            cmds.push_back(composer_update(
                m, ComposerPaste{std::move(it.bytes), /*path_checked=*/true,
                                 /*文字=*/it.saved_path}));
    }
    Cmd out = Cmd::none();
    for (auto& c : cmds) out = Cmd::batch(std::move(out), std::move(c));
    return out;
}

Cmd on_clipboard_read(Model& m, ClipboardRead& r) {
    m.ui.clipboard_walk = {};
    if (!r.items.empty())
        return insert_items(m, std::move(r.items), "<clipboard>");
    AGT_LOG(Ui, Info, "paste.clipboard_empty", "miss={} why={}", static_cast<int>(r.miss), r.error);

    const auto first_line = [&] { return r.error.substr(0, r.error.find('\n')); };
    switch (r.miss) {
        case ClipboardMiss::AskTerminal: break;   // 下面
        case ClipboardMiss::None:
        case ClipboardMiss::Empty:
            return set_status_toast(m, "clipboard is empty", std::chrono::seconds{4});
        case ClipboardMiss::TooBig:
            return set_status_toast(m, "clipboard: " + first_line(), std::chrono::seconds{10});
        case ClipboardMiss::Refused:
            // 终端在那里并且会应答，只是不允许这样做。
            return set_status_toast(
                m,
                "clipboard: your terminal blocks clipboard reads \xe2\x80\x94 kitty: add "
                "`clipboard_control write-clipboard read-clipboard` to kitty.conf",
                std::chrono::seconds{12});
        case ClipboardMiss::NoRoute:
            // 不需要设置的路线都尝试过了；说那一个。
            return set_status_toast(
                m,
                m.env.ssh ? "clipboard: couldn't reach your laptop's clipboard \xe2\x80\x94 run "
                            "`agentty paste setup` there once, or for an image use agentty.org/paste"
                          : "clipboard: " + first_line(),
                std::chrono::seconds{10});
    }

    // 步行的下一个路线是终端，其甚至通过ssh在用户的机器上运行。
    // Ask 通过我们自己的输入，以步行选择的一种方言（OSC 5522
    // 表示小猫：图像和文本；其他 OSC 52：文本）。
    // 回复作为 PasteEvent 返回到 ComposerPaste 臂中；
    // 沉默恢复了下一条路线的行走（例如 tmux 的 缓冲区）。
    {
        const bool images = r.ask == TerminalAsk::Images;
        m.ui.clipboard_walk = std::move(r.walk);
        auto toast = set_status_toast(
            m, "reading clipboard from your terminal\xE2\x80\xA6",
            std::chrono::seconds{3});
        const std::uint64_t seq = ++m.ui.clipboard_query_seq;
        // 只有读取的图像-capable才可以预期带来图像，
        // 因此只有这样文本-only答案才可以自我解释。
        m.ui.clipboard_wanted_image = images;
        // 拒绝（小猫的 EPERM）会移动此计数器：不同的修复。
        m.ui.clipboard_refused_mark =
            maya::clipboard_read_refused().load(std::memory_order_relaxed);
        // 距离"the terminal never answered"还有多久。
        // 回复的第一个 字节 在一个往返中返回；一旦 字节 着陆，
        // 超时 臂就会继续伸展，因此缓慢的 链接 上的大图像仍然会完成。
        // 它每 150 毫秒检查一次，因此拒绝会立即继续。
        const auto wait = std::chrono::milliseconds{m.env.ssh ? 2500 : 1200};
        m.ui.clipboard_query_deadline = m.now + wait;
        m.ui.clipboard_rx_mark =
            maya::clipboard_rx_bytes().load(std::memory_order_relaxed);
        AGT_LOG(Ui, Info, "paste.ask_terminal", "seq={} dialect={}", seq, images ? "osc5522" : "osc52");
        return Cmd::batch(
                    cmd::query_clipboard(images ? maya::ClipboardDialect::images
                                                : maya::ClipboardDialect::text),
                    std::move(toast),
                    Cmd::after(std::chrono::milliseconds{150},
                                          Msg{ClipboardQueryTimeout{seq}}));
    }
}

} // 命名空间结束。

using maya::overload;

Cmd composer_update(Model& m, msg::ComposerMsg cm) {
    // 在 任意 作曲家消息上标记最后的 -interaction
    // 时钟（击键、编辑、光标移动、粘贴、历史记录）。
    // maya 合成器小部件中的空闲眨眼-stop 会关闭此功能：
    // 最后一次交互后 15 秒，绘制的光标变稳定并停止请求 帧，
    // 因此空闲的 agentty 停止驱动 终端 合成器。
    // 此处的标记（所有作曲家消息的单一入口点）覆盖了每条路径，
    // 而无需接触每条手臂。
    m.ui.composer.last_edit_ms = maya::anim::default_clock().now_ms();

    // ── LOOP 模式使作曲家 READ-ONLY ───────────────────────
    // 当^B装备有DISPLAYS箱时，提示词每轮都被重新-sent。
    // 该显示必须与 loop_text 保持相同，
    // 或者用户正在阅读一件事，而 agentty 发送另一件事
    // - 因此每条变异消息都会被丢弃，而不是允许它发散。
    //
    // ComposerToggleLoop是故意的例外：
    // 出路必须从锁定状态内部继续工作，否则该模式是陷阱。
    // (光标移动是无害的，但在冻结的 缓冲区 上毫无意义，
    // 并且放下它们会保留规则一行而不是腐烂的allow-list。)
    // Esc-to-cancel 和每个全局和弦都位于该
    // 状态更新函数 之外并且不受影响。
    if (m.ui.composer.looping()
        && !std::holds_alternative<ComposerToggleLoop>(cm)) {
        return Cmd::none();
    }
    // ── 闲置 -lapse 连接再 -warm ──────────────────────────
    // 池的热套接字在大约90秒空闲（http idle_ttl）后终止，
    // 因此任何更长的暂停后的提交都会在TTFT之上支付冷TCP+TLS+
    // H2拨号（大约150-400毫秒）。打字是最早可靠的
    // "a request is coming" 信号——在温暖窗口消失后，
    // 对 首次 作曲家 事件 进行机会主义预热。
    //
    // 节流阀存在于MODEL (m.s.last_prewarm_at)中，
    // 而不是在函数-local静态中：静态状态更新可以看到Model不拥有，
    // 因此重放和一个进程中的两个应用程序实例都会出错。时钟仍然在此处读
    // 取 - 请参阅 elm_purity lint，了解将 `now`
    // 标记到每条消息的 Model 上的计划。
    Cmd prewarm = Cmd::none();
    {
        const auto now = m.now;
        const bool wire_stale =
            m.s.last_wire_at.time_since_epoch().count() == 0
            || now - m.s.last_wire_at > std::chrono::seconds(85);
        const bool warm_throttled =
            m.s.last_prewarm_at.time_since_epoch().count() != 0
            && now - m.s.last_prewarm_at < std::chrono::seconds(85);
        if (wire_stale && !warm_throttled && !m.s.active()) {
            m.s.last_prewarm_at = now;
            prewarm = cmd::prewarm_provider(m);
        }
    }
    // ── 主动OAuth 刷新 ────────────────────────────────
    // 上述套接字re-warm的同级。启动时有效的令牌可能会在用户读取输出/撰写时失效
    // WHILE；第一次提交将返回 401，停放 数据流，刷新，然后 重试 —
    // 可见为first-message 滞后。键入的是 "a request is coming"
    // 信号：如果 on-disk OAuth 令牌在到期前约 5 分钟内（或已经
    // 过去）并带有 refresh_token，则启动 相同 后台刷新
    // init() 和 401 处理函数 使用，因此新的持有者在 Enter
    // 之前就位。在 oauth_refresh_in_flight 上门控（
    // 无 double-fire；TokenRefreshed 清除它）并进行限制，
    // 因此 key-repeat 无法向令牌端点发送垃圾邮件。
    Cmd proactive_refresh = Cmd::none();
    {
        const auto now = m.now;
        const bool probe_throttled =
            m.s.last_refresh_probe_at.time_since_epoch().count() != 0
            && now - m.s.last_refresh_probe_at < std::chrono::seconds(30);
        // 令牌是Anthropic的；在任何其他提供商上，没有任何内容可以刷新，
        // 并且 in-flight 标志 将保留下一次发送。
        if (!probe_throttled && !m.s.active() && !m.s.oauth_refresh_in_flight
            && m.d.selection.kind == provider::Kind::Anthropic) {
            m.s.last_refresh_probe_at = now;
            // 工作线程读取令牌并仅在其到期时刷新。
            m.s.oauth_refresh_in_flight = true;
            proactive_refresh = cmd::refresh_oauth_if_due();
        }
    }
    Cmd step = std::visit(overload{
        [&](ComposerCharInput e) -> Cmd {
            // '/'为普通文本。作曲家上方的 `/` 菜单是根据文本 +
            // 光标 (slash::menu_query) 自行设计的，
            // 因此此处不会打开任何内容，并且 带类型的 名称始终保留在作曲家中。
            // '@'打开提及选择面板的文件。与 '/' 不同，
            // 这是允许在 -prose 中出现的（"ping @alice tomorrow"
            // 是一个很好的英语句子），因此我们不需要空的 缓冲区 ——
            // 我们只要求前一个字符是单词边界（start-of-string
            // 或空格），因此 URL/电子邮件/e.g。
            // "alice@example.com" 不触发。
            auto at_word_boundary = [&]{
                if (m.ui.composer.cursor == 0) return true;
                char prev = m.ui.composer.text[
                    static_cast<std::size_t>(m.ui.composer.cursor) - 1];
                return prev == ' ' || prev == '\t' || prev == '\n';
            };
            if (e.ch == U'@' && at_word_boundary()) {
                // 没有提前加载 快照：选择面板在第一次读取时拉出一个，
                // 并且如果索引稍后发布则自动加满，因此冷打开是免费的并且永远
                // 不会让面板滞留在"indexing…"上（
                // 参见panel/filtered_picker.hpp)。
                m.ui.panel.descend(pn::Mention{mention::Open{}});

                // 刷新git信号，
                // 以便工作-set排名反映自启动以来所做的编辑。
                //
                // 像所有其他后台作业一样，通过Cmd调度接口边界 — NOT一手
                // -rolled 线程。这个处理函数用于调用一个裸露的
                // `run_isolated_detached`，因此持有`@`会
                // 产生一个线程和两个8s-timeout `git`子进程PER KEYSTROKE，
                // 所有这些都竞相发布到同一张地图中。
                //
                // 合并存在于 MODEL 中，这使得它成为 Elm，
                // 而不是隐藏在 工作区 层中的静态锁存器：
                // 状态更新函数可以查看刷新是否已经在进行中，
                // 因此它根本不发出第二个Cmd。一个 标志，
                // 一处，测试可见。
                Cmd git_cmd;
                if (!m.ui.git_refresh_inflight) {
                    m.ui.git_refresh_inflight = true;
                    git_cmd = Cmd::task_isolated(
                        [](maya::Sink<Msg> out, std::stop_token) {
                            refresh_git_signals();
                            out.send(Msg{GitSignalsRefreshed{}});
                        });
                }
                return git_cmd;
            }
            // '#'打开符号 选择面板-镜子'@'。非-blocking：
            // 快照仅当（并行）符号扫描已经着陆时；
            // 否则以空的 快照 + "indexing…" 提示
            // 打开并在第一次击键时填充。切勿在扫描时阻挡 UI。
            if (e.ch == U'#' && at_word_boundary()) {
                m.ui.panel.descend(pn::Symbol{symbol::Open{}});
                return Cmd::none();
            }
            // 将连续输入合并到一个撤消单元中，
            // 但在空白处中断运行，因此Ctrl+Z逐字倒回
            // （不是一次整个段落）。一个空格开始
            // 一个新的快照；下面的单词合并到它上面。
            const bool is_ws = (e.ch == U' ' || e.ch == U'\t');
            if (is_ws) begin_edit(m.ui.composer);
            else       begin_typing_edit(m.ui.composer);
            auto utf8 = ui::utf8_encode(e.ch);
            m.ui.composer.text.insert(m.ui.composer.cursor, utf8);
            m.ui.composer.cursor += static_cast<int>(utf8.size());
            return Cmd::none();
        },
        [&](ComposerBackspace) -> Cmd {
            if (m.ui.composer.cursor > 0 && !m.ui.composer.text.empty()) {
                begin_edit(m.ui.composer);
                // 如果光标位于一个占位符的右边缘，
                // 则 chip_prev 跳过整个占位符 — 退
                // 格键然后在一次击键中擦除整个 标签 标记，
                // 这是用户的心智模型：
                // 应删除整个附件，而非只删除末尾哨兵字节。
                // 附件对象按索引保留在向量中（
                // 重新编号会破坏指向后面索引的其他占位符）；
                // 当作曲家下次清除时，
                // 孤儿会得到 GC。
                int p = ui::chip_prev(m.ui.composer.text, m.ui.composer.cursor);
                m.ui.composer.text.erase(p, m.ui.composer.cursor - p);
                m.ui.composer.cursor = p;
            }
            return Cmd::none();
        },
        [&](ComposerEnter)  { return submit_message(m); },
        [&](ComposerSubmit) { return submit_message(m); },
        [&](ComposerNewline) -> Cmd {
            begin_edit(m.ui.composer);
            m.ui.composer.text.insert(m.ui.composer.cursor, "\n");
            m.ui.composer.cursor += 1;
            m.ui.composer.expanded = true;
            return Cmd::none();
        },
        [&](ComposerToggleExpand) -> Cmd {
            m.ui.composer.expanded = !m.ui.composer.expanded;
            return Cmd::none();
        },
        [&](ComposerToggleLoop) -> Cmd {
            auto& c = m.ui.composer;
            // 已经循环→撤防。故意将 不 取消 转为 -flight：
            // 你说的是 "stop after this one"，
            // 这是非 -destructive 读数。 Esc 仍然取消回合本身。
            if (c.loop_armed) {
                const int n = c.loop_iterations;
                c.loop_armed = false;
                c.loop_text.clear();
                c.loop_attachments.clear();
                c.loop_iterations = 0;
                auto toast = set_status_toast(
                    m, n > 0 ? "loop off \xc2\xb7 " + std::to_string(n) + " sent"
                             : std::string{"loop off"});
                return toast;
            }
            // 武装需要重复一些事情。一个空的作曲家会用没有 载荷 的
            // 循环来武装一个循环——使其变得不可代表，
            // 而不是稍后默默地 no-op。
            if (c.text.empty()) {
                auto toast = set_status_toast(
                    m, "loop: type a message first");
                return toast;
            }
            // 快照为载荷，然后提交。 快照（不是现场
            // 作曲家）是重复的，因此用户可以继续输入。
            c.loop_armed       = true;
            c.loop_text        = c.text;
            c.loop_attachments = c.attachments;
            c.loop_iterations  = 0;
            auto sub_cmd = submit_message(m);
            // 提交排水作曲家；放置武装 载荷 BACK，
            // 以便盒子不断显示正在重复的内容。循环时，
            // 作曲家是 read-only（请参阅编辑守卫），
            // 因此这是武装 提示词 的显示，而不是可编辑的草稿 - 如果没有它，
            // 用户会看到一个空盒子 auto-send 他们看不到的东西。
            auto& sc = m.ui.composer;
            sc.text        = sc.loop_text;
            sc.attachments = sc.loop_attachments;
            sc.cursor      = static_cast<int>(sc.text.size());
            return sub_cmd;
        },
        [&](ComposerCursorLeft) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = ui::chip_prev(m.ui.composer.text, m.ui.composer.cursor);
            return Cmd::none();
        },
        [&](ComposerCursorRight) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = ui::chip_next(m.ui.composer.text, m.ui.composer.cursor);
            return Cmd::none();
        },
        [&](ComposerCursorHome) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = 0;
            return Cmd::none();
        },
        [&](ComposerCursorEnd) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
            return Cmd::none();
        },
        [&](ComposerCursorWordLeft) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = word_left(m.ui.composer.text, m.ui.composer.cursor);
            return Cmd::none();
        },
        [&](ComposerCursorWordRight) -> Cmd {
            m.ui.composer.undo_coalescing = false;
            m.ui.composer.cursor = word_right(m.ui.composer.text, m.ui.composer.cursor);
            return Cmd::none();
        },
        [&](ComposerKillToEndOfLine) -> Cmd {
            const auto& s = m.ui.composer.text;
            int n = static_cast<int>(s.size());
            int p = m.ui.composer.cursor;
            if (p >= n) return Cmd::none();
            int q = p;
            while (q < n && s[q] != '\n') ++q;
            // 标准读取行：一行文本上的Ctrl+K删除至EOL，
            // 不包括'\n'；在空行上，
            // 它会删除换行符本身（将空行折叠起来）。
            if (q == p && q < n && s[q] == '\n') ++q;
            begin_edit(m.ui.composer);
            m.ui.composer.text.erase(p, q - p);
            return Cmd::none();
        },
        [&](ComposerKillToBeginningOfLine) -> Cmd {
            const auto& s = m.ui.composer.text;
            int p = m.ui.composer.cursor;
            if (p <= 0) return Cmd::none();
            int q = p;
            while (q > 0 && s[q - 1] != '\n') --q;
            if (q == p) return Cmd::none();
            begin_edit(m.ui.composer);
            m.ui.composer.text.erase(q, p - q);
            m.ui.composer.cursor = q;
            return Cmd::none();
        },
        [&](ComposerDeleteWordBack) -> Cmd {
            // Ctrl+W——读取行unix-word-rubout。
            // Delete 从上一个字边界到光标。
            // 重用 标签-aware word_left 边界，
            // 因此附件 标签 右边缘的 Ctrl+W 一笔删除整个标记（
            // 与 标签-aware Backspace 相同的心理模型）。
            int p = m.ui.composer.cursor;
            if (p <= 0) return Cmd::none();
            int q = word_left(m.ui.composer.text, p);
            if (q >= p) return Cmd::none();
            begin_edit(m.ui.composer);
            m.ui.composer.text.erase(static_cast<std::size_t>(q),
                                     static_cast<std::size_t>(p - q));
            m.ui.composer.cursor = q;
            return Cmd::none();
        },
        [&](ComposerDeleteWordForward) -> Cmd {
            // Alt+D——读取行kill-word。 Delete 从光标处
            // 一直到下一个字边界；光标保持原状。通过 word_right 与
            // Ctrl+W 和 标签-aware 对称。
            const auto& s = m.ui.composer.text;
            int p = m.ui.composer.cursor;
            if (p >= static_cast<int>(s.size())) return Cmd::none();
            int q = word_right(s, p);
            if (q <= p) return Cmd::none();
            begin_edit(m.ui.composer);
            m.ui.composer.text.erase(static_cast<std::size_t>(p),
                                     static_cast<std::size_t>(q - p));
            return Cmd::none();
        },
        [&](ComposerUndo) -> Cmd {
            if (m.ui.composer.undo_stack.empty()) return Cmd::none();
            ComposerState::Snapshot cur;
            cur.text   = std::move(m.ui.composer.text);
            cur.cursor = m.ui.composer.cursor;
            cur.attachments = std::move(m.ui.composer.attachments);
            auto prev = std::move(m.ui.composer.undo_stack.back());
            m.ui.composer.undo_stack.pop_back();
            if (m.ui.composer.redo_stack.size() >= kUndoDepth)
                m.ui.composer.redo_stack.erase(m.ui.composer.redo_stack.begin());
            m.ui.composer.redo_stack.push_back(std::move(cur));
            m.ui.composer.text        = std::move(prev.text);
            m.ui.composer.cursor      = prev.cursor;
            m.ui.composer.attachments = std::move(prev.attachments);
            m.ui.composer.undo_coalescing = false;
            reset_browsing(m.ui.composer);
            reset_browsing(m.ui.composer);
            return Cmd::none();
        },
        [&](ComposerRedo) -> Cmd {
            if (m.ui.composer.redo_stack.empty()) return Cmd::none();
            ComposerState::Snapshot cur;
            cur.text   = std::move(m.ui.composer.text);
            cur.cursor = m.ui.composer.cursor;
            cur.attachments = std::move(m.ui.composer.attachments);
            auto next = std::move(m.ui.composer.redo_stack.back());
            m.ui.composer.redo_stack.pop_back();
            if (m.ui.composer.undo_stack.size() >= kUndoDepth)
                m.ui.composer.undo_stack.erase(m.ui.composer.undo_stack.begin());
            m.ui.composer.undo_stack.push_back(std::move(cur));
            m.ui.composer.text        = std::move(next.text);
            m.ui.composer.cursor      = next.cursor;
            m.ui.composer.attachments = std::move(next.attachments);
            m.ui.composer.undo_coalescing = false;
            reset_browsing(m.ui.composer);
            reset_browsing(m.ui.composer);
            return Cmd::none();
        },
        [&](ComposerHistoryPrev) -> Cmd {
            auto texts = previous_user_texts(m);
            if (texts.empty()) return Cmd::none();
            auto& cs = m.ui.composer;
            // 我们现在所处的位置：浏览历史记录，或其他任何地方（实时，
            // 或查看队列——↑离开该队列并进入历史记录）。
            const auto cur = cs.history_index();
            int next_idx = cur.value_or(-1) + 1;
            if (next_idx >= static_cast<int>(texts.size()))
                next_idx = static_cast<int>(texts.size()) - 1;
            // 实时草稿中的第一个 ↑ — 快照 无论用户拥有
            // 带类型的，所以 ↓ 一直恢复它。
            if (!cur) cs.draft_save = cs.text;
            cs.browsing = ComposerState::History{next_idx};
            cs.undo_coalescing = false;
            apply_history_entry(cs, texts[static_cast<std::size_t>(next_idx)]);
            // 历史行走确实NOT推动撤消：仅↑↓是非
            // -destructive（它们使draft_save完好无损）。
            // 用户编辑后，begin_edit 会触发 reset_browsing，
            // 并且行走的文本将成为新的实时草稿。
            return Cmd::none();
        },
        [&](ComposerHistoryNext) -> Cmd {
            auto& cs = m.ui.composer;
            const auto cur = cs.history_index();
            if (!cur) return Cmd::none();   // 不走历史
            cs.undo_coalescing = false;
            auto texts = previous_user_texts(m);
            const int next_idx = *cur - 1;
            if (next_idx < 0) {
                // 一路走回现场选秀。
                cs.browsing = ComposerState::Live{};
                cs.text = cs.draft_save.value_or(std::string{});
                cs.cursor = static_cast<int>(cs.text.size());
                cs.draft_save.reset();
                return Cmd::none();
            }
            cs.browsing = ComposerState::History{next_idx};
            if (next_idx < static_cast<int>(texts.size()))
                apply_history_entry(cs, texts[static_cast<std::size_t>(next_idx)]);
            return Cmd::none();
        },
        [&](ComposerImagePasteFromClipboard) -> Cmd {
            // 带括号的粘贴仅传送UTF-8文本；对于图像 - on-clipboard，
            // 我们直接询问 OS 剪贴板（有关 per-OS 的实现，
            // 请参阅 io/clipboard.cpp）。
            // 同步 — 帮助程序立即在 no-image 剪贴板上退出。
            // 与Alt+V触发器和空括号-paste检测相同的代码路径
            // （Windows 终端吞噬Ctrl+V，
            // 我们的两个回退降落在这里）。
            return smart_paste_from_clipboard(m);
        },
        [&](ComposerPaste& e) -> Cmd {
            // 终端剪贴板回复(OSC 5522)其ESC 字节在途中被
            // 剥离(一些tmux/ssh设置)，以纯文本形式到达：
            // "]5522;type=read:status=DATA:..."。
            // 它是剪贴板，而不是用户 带类型的 的东西：将其收集到看不见的地方，
            // 直到它的 DONE 数据包，然后粘贴它所携带的内容。
            {
                auto& pend = m.ui.clipboard_leaked_reply;
                if (!pend.empty() || leaked_reply(e.text) != LeakedReply::None) {
                    if (pend.size() + e.text.size() > (64u << 20)) {   // 暴走：扔掉它
                        pend.clear();
                        return Cmd::none();
                    }
                    pend += e.text;
                    if (leaked_reply(pend) != LeakedReply::Complete) return Cmd::none();
                    std::string why;
                    auto items = decode_leaked_reply(pend, &why);
                    AGT_LOG(Ui, Info, "paste.leaked_reply", "bytes={} items={} err={}", pend.size(), items.size(), why);
                    pend.clear();
                    if (items.empty()) return Cmd::none();
                    const bool needs_disk = std::ranges::any_of(items, [](auto& it) {
                        return it.kind == ClipboardItem::Kind::File;
                    });
                    if (needs_disk) return cmd::settle_clipboard(std::move(items));
                    return insert_items(m, std::move(items), "<clipboard>");
                }
            }

            // 成块到达的粘贴（不带方括号的粘贴的 终端，
            // 逐个读取）正好落在其之前的块的 标签 之后。
            // 种植 标签 而不是开始新的，所以一张粘贴就是
            // 一张 标签。
            //
            // 作为仍在到达的文本粘贴的图像将每一块都占据直到其完整，
            // 甚至是在暂停足够长的时间之后看起来像新
            // 粘贴的图像：缓慢的链接在-line中间暂停。
            // 不能再多的文本（除了 base64 和空格之外的任何内容）
            // 将被保留。
            auto& cs = m.ui.composer;
            const auto arriving_idx = [&]() -> std::size_t {
                if (!m.d.persisted.paste.decode_text) return cs.attachments.size();
                for (std::size_t i = cs.attachments.size(); i-- > 0;) {
                    const auto& a = cs.attachments[i];
                    if (a.kind == Attachment::Kind::Paste && !a.stalled && armored_partial(a.body.bytes())) return i;
                }
                return cs.attachments.size();
            }();
            const bool more_of_it = arriving_idx < cs.attachments.size() && !e.text.empty()
                && std::ranges::all_of(e.text, [](unsigned char c) {
                       return std::isalnum(c) || c == '+' || c == '/' || c == '=' || c == '-' || c == '_'
                           || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c >= 0x80;
                   });
            if (e.continues_paste || more_of_it) {
                const auto cur = static_cast<std::size_t>(std::clamp(cs.cursor, 0, static_cast<int>(cs.text.size())));
                const auto len = attachment::placeholder_len_ending_at(cs.text, cur);
                const auto at_cursor = len > 0 ? attachment::placeholder_index(cs.text, cur - len) : cs.attachments.size();
                const auto idx = more_of_it ? arriving_idx : at_cursor;
                AGT_LOG(Ui, Debug, "paste.piece", "bytes={} grows={} chip_bytes={}", e.text.size(),
                        idx < cs.attachments.size() && cs.attachments[idx].kind == Attachment::Kind::Paste,
                        idx < cs.attachments.size() ? cs.attachments[idx].byte_count : 0);
                if (idx < cs.attachments.size() && cs.attachments[idx].kind == Attachment::Kind::Paste) {
                    auto& prev = cs.attachments[idx];
                    std::string body = prev.body.bytes();
                    body += e.text;
                    // 这些片段可以加起来形成作为文本发送的图像。
                    // 一旦块完成，将 标签 交换为它解码的内容；
                    // 在那之前，继续种植 标签。
                    if (m.d.persisted.paste.decode_text && has_armored(body) && !armored_partial(body)) {
                        std::string why;
                        auto items = decode_armored(body, &why);
                        if (!items.empty()) {
                            // 放下该标签的占位符（无论它位于何处）并将其
                            // 解码到的内容放在其位置上。
                            const auto ph = attachment::make_placeholder(idx);
                            if (auto at = cs.text.find(ph); at != std::string::npos) {
                                cs.text.erase(at, ph.size());
                                cs.cursor = static_cast<int>(at);
                            }
                            prev.body.set_bytes(std::string{});
                            prev.byte_count = prev.line_count = 0;
                            const bool needs_disk = std::ranges::any_of(items, [](auto& it) {
                                return it.kind == ClipboardItem::Kind::File;
                            });
                            if (needs_disk) return cmd::settle_clipboard(std::move(items));
                            return insert_items(m, std::move(items), "<paste>");
                        }
                    }
                    std::erase(body, '\r');
                    prev.line_count = std::ranges::count(body, '\n') + (!body.empty() && body.back() == '\n' ? 0 : 1);
                    prev.byte_count = body.size();
                    const bool image_text = m.d.persisted.paste.decode_text && armored_partial(body);
                    prev.body.set_bytes(std::move(body));
                    prev.stalled = false;
                    if (prev.line_count > 1) cs.expanded = true;
                    if (!image_text) return Cmd::none();
                    // 仍然到来：如果不再到来，就这样说，
                    // 而不是永远"Receiving"。 maya 在 1 秒
                    // 安静后结束浆糊爆发；给慢速 链接 两倍。
                    return Cmd::after(std::chrono::milliseconds{2500},
                                      Msg{ArmoredPasteStall{++m.ui.armored_paste_gen}});
                }
                // 没有 标签 生长（第一块变成图像、
                // 路径或文本）：将这块视为其自己的粘贴。
                e.continues_paste = false;
            }

            // ANY粘贴(带括号，OSC 52回复，OSC 5522图像回复)
            // 满足in-flight escape-based剪贴板查询-取消
            // 待处理 no-reply诊断。
            //
            // 我们只用一种方言询问（步行选择了它），
            // 所以第一个回复就是答案：kitty的OSC 5522
            // 回复携带图像（如果有的话），否则携带文本。
            // 第二个相同的回复（终端 回复两次）被丢弃。
            if (m.ui.clipboard_query_done == m.ui.clipboard_query_seq
                && m.ui.clipboard_query_seq != 0 && !e.text.empty()
                && e.text.size() == m.ui.clipboard_last_reply_size
                && std::hash<std::string>{}(e.text) == m.ui.clipboard_last_reply_hash) {
                AGT_LOG(Ui, Info, "paste.duplicate_reply", "bytes={}", e.text.size());
                return Cmd::none();
            }
            const bool wanted_image = m.ui.clipboard_wanted_image;
            if (m.ui.clipboard_query_done != m.ui.clipboard_query_seq && !e.continues_paste && !e.text.empty()) {
                m.ui.clipboard_last_reply_size = e.text.size();
                m.ui.clipboard_last_reply_hash = std::hash<std::string>{}(e.text);
                m.ui.clipboard_query_done = m.ui.clipboard_query_seq;
                m.ui.clipboard_wanted_image = false;
                m.ui.clipboard_walk = {};
                m.ui.clipboard_text_fallback.clear();
            }
            // 空括号粘贴 → Windows 终端 签名用于
            // 用户按了 Ctrl+V，但剪贴板没有文本内容。
            // 终端吞下Ctrl+V来运行自己的粘贴动作；
            // 当 CF_UNICODETEXT 不存在时（e.g。
            // 剪贴板保存来自 Win+Shift+S 的图像），
            // 该操作发送 `\x1b[200~ \x1b[201~`，
            // 中间没有任何内容。通过 Alt+V 使用的相同路径进行路由，
            // 以便用户无需学习备用快捷方式即可获取图像。
            if (e.text.empty())
                return smart_paste_from_clipboard(m);

            // 检测粘贴路径。此会话的六轮时间用于猜测 原因，
            // 100 KB 图像显示为 4 KB，
            // 因为这里没有留下任何痕迹：相同的症状适合截断
            // 解析器、吞咽的延续块和 per-chunk 附件，
            // 并且只有日志可以区分它们。每个粘贴一行
            // 包含大小和所采取的分支，
            // 使下一个报告可以从日志而不是屏幕截图中进行回答。
            AGT_LOG(Ui, Info, "paste.recv",
                    "bytes={} wanted_image={} magic={} first16={:.16}",
                    e.text.size(), wanted_image ? 1 : 0,
                    detect_image_media_type(e.text)
                        ? detect_image_media_type(e.text) : "-",
                    e.text);

            // 原始图像 字节 的 Bracketed-paste —
            // 极其罕见（大多数 终端 擦掉了 二进制程序 的粘贴），
            // 但带有 `--allow-passthrough`-style
            // 选项的 Kitty 和 Wezterm 将逐字地向我们提供 字节。
            // 通过魔法前缀检测并摄取为图像。在 path-detection
            // 之前进行了测试，因此恰好以 字节 开头的 PNG 可以
            // 将 解析 作为路径采用正确的分支。
            if (auto* mt = detect_image_media_type(e.text); mt != nullptr) {
                AGT_LOG(Ui, Info, "paste.image_bytes",
                        "media_type={} bytes={}", mt, e.text.size());
                begin_edit(m.ui.composer);
                Attachment att;
                att.kind       = Attachment::Kind::Image;
                att.path       = "<paste>";
                att.media_type = mt;
                att.byte_count = e.text.size();
                att.body.set_bytes(std::move(e.text));
                std::size_t idx = m.ui.composer.attachments.size();
                m.ui.composer.attachments.push_back(std::move(att));
                auto placeholder = attachment::make_placeholder(idx);
                m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
                m.ui.composer.cursor += static_cast<int>(placeholder.size());
                m.ui.composer.expanded = true;
                return Cmd::none();
            }

            // `richpaste:1:`行：在笔记本电脑（
            // `agentty paste howto`)上作为文本复制的图像或文件，
            // 作为普通粘贴跨越ssh。解码是纯粹的，所以在这里运行。
            // 到目前为止，只有装甲块的一部分（其余部分有
            // 更多碎片）：它变成标签，碎片增长，完成时解码。
            const bool arriving = !e.path_checked && m.d.persisted.paste.decode_text
                               && armored_partial(e.text);
            if (arriving) {
                // 接下来的碎片成长为标签。如果他们从来没有来过，
                // 摊位检查上就会这么说。
                e.path_checked = true;
                Cmd pasted = composer_update(m, std::move(e));
                return Cmd::batch(std::move(pasted),
                                  Cmd::after(std::chrono::milliseconds{2500},
                                             Msg{ArmoredPasteStall{++m.ui.armored_paste_gen}}));
            }
            if (!e.path_checked && m.d.persisted.paste.decode_text && has_armored(e.text)) {
                std::string why;
                auto items = decode_armored(e.text, &why);
                AGT_LOG(Ui, Info, "paste.armored", "items={} err={}", items.size(), why);
                if (!items.empty()) {
                    // 除了图像和文本之外的文件需要保存到磁盘，
                    // 这是工作线程的工作。
                    const bool needs_disk = std::ranges::any_of(items, [](auto& it) {
                        return it.kind == ClipboardItem::Kind::File;
                    });
                    if (needs_disk) return cmd::settle_clipboard(std::move(items));
                    return insert_items(m, std::move(items), "<paste>");
                }
                // 每个块都是坏的：按原样粘贴文本，并说出原因。
                // （仍然到达的块永远不会到达这里：参见`arriving`。）
                if (!why.empty()) {
                    e.path_checked = true;   // 跳过路径嗅探：它不是路径
                    Cmd toast = set_status_toast(m, "paste: " + why, std::chrono::seconds{6});
                    return Cmd::batch(std::move(toast), composer_update(m, std::move(e)));
                }
            }

            // Image-path粘贴：命名真实图像文件的单个
            // -line路径成为图像附件。检查是否读取文件，
            // 因此可能是路径的粘贴会转到 工作线程，
            // 并以 ImagePathSniffed 返回；如果不是图像，
            // 则回复re-sends此处的文本并设置path_checked。
            if (!e.path_checked && may_be_image_path(e.text))
                return cmd::sniff_image_path(std::move(e.text));

            // 空粘贴（剪贴板管理器打嗝，终端掉落了二进制
            // 程序剪贴板）——无事可做。
            if (e.text.empty()) return Cmd::none();

            // 标准化行结尾：一些 终端 （和 ssh tty 熟模式）
            // 在括号粘贴中翻译 \n → \r，这样 字节 看起来
            // 就像用户在每一行都按下了 Enter。 Maya 的
            // word_wrap 仅在 \n 上分割，
            // 因此将 \r 留在 正文 中会将整个粘贴折叠成一个
            // 基于宽度换行的逻辑行 - 可见为带有杂散 渲染 的
            // 巨大单个 -row 用户消息，*不是*用户期望的行
            // -by-line 渲染。 Canvas::write_text
            // 跳过 \r 作为控制字符，因此 存储单元 也不会获得
            // 托架 -return 字节；如果没有这种规范化，
            // 粘贴看起来都会错误 和 会导致父布局失效（布局会看到一条换行，
            // 但 cached/measured 高度不匹配会
            // 在用户转弯和助理转弯之间留下可见的间隙）。
            // 剥离杂散的 \r 并将 \r-only / \r\n
            // 转换为 \n； pure-\n 输入不变。
            {
                std::string norm;
                norm.reserve(e.text.size());
                for (std::size_t i = 0; i < e.text.size(); ++i) {
                    char c = e.text[i];
                    if (c == '\r') {
                        norm.push_back('\n');
                        if (i + 1 < e.text.size() && e.text[i + 1] == '\n')
                            ++i;
                    } else {
                        norm.push_back(c);
                    }
                }
                e.text = std::move(norm);
            }

            // 粘贴到空编辑器中的短的one-line
            // `/name args`是带类型的文本，而不是标签：它必须
            // 保留菜单和解析器可以读取的命令（标签将斜线隐藏在令牌后面）。
            // 路径（`/tmp/x.png`，第一个字中的第二个'
            // /'）仍然走标签方式，因此丢弃的图像仍然是图像。
            const bool slash_name = [&] {
                if (!m.ui.composer.text.empty() || !e.text.starts_with('/')) return false;
                if (e.text.size() > 512 || e.text.find('\n') != std::string::npos) return false;
                const auto word_end = e.text.find_first_of(" \t");
                const std::string_view word = std::string_view{e.text}.substr(1, word_end == std::string::npos ? std::string::npos : word_end - 1);
                return !word.empty() && word.find('/') == std::string_view::npos
                    && word.find('.') == std::string_view::npos;
            }();
            if (slash_name || e.literal) {
                begin_edit(m.ui.composer);
                auto& cs = m.ui.composer;
                if (e.literal) {
                    // 已保存文件的路径：带类型的到光标处的文本。
                    const auto at = static_cast<std::size_t>(std::clamp(cs.cursor, 0, static_cast<int>(cs.text.size())));
                    std::string ins = e.text;
                    if (at > 0 && cs.text[at - 1] != ' ' && cs.text[at - 1] != '\n') ins.insert(0, 1, ' ');
                    ins.push_back(' ');
                    cs.text.insert(at, ins);
                    cs.cursor = static_cast<int>(at + ins.size());
                    return Cmd::none();
                }
                cs.text   = std::move(e.text);
                cs.cursor = static_cast<int>(cs.text.size());
                return Cmd::none();
            }

            // Always-chip：每个粘贴，无论大小，
            // 都成为附件+内联占位符。 Single-line 粘贴
            // 会获得预览标题 ("Pasted: hello world")，
            // 以便保持清晰； multi-line 粘贴获取
            // lines/bytes 摘要。 Inline
            // -as-text 是任何具有结构的粘贴的 UX 回归
            // - 之前的阈值-based 崩溃混合了两个演示文稿，
            // 并使作曲家身高随着粘贴大小而抽搐。
            const std::size_t lines =
                std::ranges::count(e.text, '\n')
                + (e.text.back() == '\n' ? 0 : 1);

            begin_edit(m.ui.composer);
            Attachment att;
            att.kind       = Attachment::Kind::Paste;
            att.line_count = lines;
            att.byte_count = e.text.size();
            att.body.set_bytes(std::move(e.text));
            std::size_t idx = m.ui.composer.attachments.size();
            m.ui.composer.attachments.push_back(std::move(att));
            auto placeholder = attachment::make_placeholder(idx);
            m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
            m.ui.composer.cursor += static_cast<int>(placeholder.size());
            if (lines > 1) m.ui.composer.expanded = true;
            // 终端回答了TEXT。是否值得警告完全
            // 取决于剪贴板上的内容，我们从这里看不到
            // 这一点 - 对文本剪贴板的文本回复是完全成功的，
            // 而不是降级的图像粘贴。
            //
            // 这用于在 SSH 上的每个 Ctrl+V 上发射。
            // `wanted_image` 设置为 任意 粘贴（Ctrl
            // +V 是唯一的粘贴键），并且在 SSH 上，本地探测总是失败，
            // 因此每个普通文本粘贴都到达此臂并遇到
            // "kitty needs clipboard READ permission"（kitty 需要剪贴板读取权限。）
            // - 建议既是错误的，而且一旦用户已经授予它，就会令人困惑。
            // 当没有任何问题时发出的警告会训练人们忽略重要的
            // 警告。
            //
            // 仅当 终端 CANNOT 完全传送图像时才大声说
            // 出来（没有 OSC 5522），因为那时真正的图像
            // 粘贴是默默地不可能的并且用户需要知道。一只回复文字
            // 的小猫只是在剪贴板上写了文字：什么也不说。
            const bool terminal_can_do_images =
                maya::ansi::env_supports_osc5522();
            if (wanted_image && !terminal_can_do_images) {
                // 到达这里意味着终端根本没有图像方言—
                // —上面通过保持安静来处理OSC
                // 5522分支，因为用文本回复的小猫
                // 只是被要求提供文本。所以有一件事要说，
                // 那就是能力，而不是权限。
                auto toast = set_status_toast(
                    m,
                    "pasted text \xe2\x80\x94 for images over ssh run "
                    "`agentty paste setup` on your laptop once",
                    std::chrono::seconds{9});
                return toast;
            }
            return Cmd::none();
        },
        [&](ArmoredPasteStall& e) -> Cmd {
            // 作为文本粘贴的图像停止到达。较新一代的 re-armed
            // 是后来的作品，因此只有最后一次检查才有效。
            if (e.generation != m.ui.armored_paste_gen) return Cmd::none();
            bool any = false;
            for (auto& a : m.ui.composer.attachments) {
                if (a.kind != Attachment::Kind::Paste || a.stalled) continue;
                if (!armored_partial(a.body.bytes())) continue;
                a.stalled = true;
                any = true;
            }
            if (!any) return Cmd::none();
            AGT_LOG(Ui, Info, "paste.armored_stalled", "gen={}", e.generation);
            return set_status_toast(
                m,
                "image didn't arrive whole \xe2\x80\x94 delete the chip and copy it again "
                "(big images: agentty.org/paste keeps them small)",
                std::chrono::seconds{8});
        },
        [&](ClipboardQueryTimeout& e) -> Cmd {
            // escape-based剪贴板读取没有得到答复。 过期守卫：
            // 仅诊断THIS查询是否仍然是最新的并且同时
            // 没有粘贴到达。
            if (e.seq != m.ui.clipboard_query_seq
                || m.ui.clipboard_query_done >= e.seq)
                return Cmd::none();
            const bool refused =
                maya::clipboard_read_refused().load(std::memory_order_relaxed)
                    != m.ui.clipboard_refused_mark;
            // 终端可能是ANSWERING，只是还没有完成：
            // 在远程链接上，图像回复在数秒内是兆字节的
            // base64和数据流。在 字节 主动登陆时诊断
            // "no answer"，导致大屏幕截图变得无法粘贴。
            // 如果计数器在我们武装后移动，则在新标记上重新
            // -arm并让它完成；只有真正的沉默才能做出下面的诊断。
            // 受传输本身限制——当 字节 停止时，
            // 下一个 超时 不会看到任何运动并如实报告。
            if (!refused) {
                const std::uint64_t now =
                    maya::clipboard_rx_bytes().load(std::memory_order_relaxed);
                if (now != m.ui.clipboard_rx_mark) {
                    m.ui.clipboard_rx_mark = now;
                    const auto again = std::chrono::milliseconds{1500};
                    return Cmd::after(again,
                                Msg{ClipboardQueryTimeout{e.seq}});
                }
                // 仍在等待中：很快再次查看，
                // 因此拒绝（作为计数器到达，而不是事件）立即继续前进，
                // 而不是在整个等待之后。
                if (m.now < m.ui.clipboard_query_deadline) {
                    return Cmd::after(std::chrono::milliseconds{150},
                                      Msg{ClipboardQueryTimeout{e.seq}});
                }
            }
            // 该查询已失效。清除 image-intent 锁存器，
            // 这样它就不会泄漏到不相关的后续粘贴中并贴错标签。
            m.ui.clipboard_wanted_image = false;

            // 终端 保持安静（或拒绝）：那是一条路线，而不是粘贴。
            // 继续走到下一条路线（tmux 的 缓冲区，
            // 当 终端 不回答我们时，它仍然可以获取文本）。
            // 快速输入的文本回复就是答案，因此改为使用它。
            if (m.ui.clipboard_walk.active() && m.ui.clipboard_text_fallback.empty()) {
                m.ui.clipboard_query_done = m.ui.clipboard_query_seq;
                AGT_LOG(Ui, Info, "paste.terminal_quiet", "refused={} resuming at route {}", refused, m.ui.clipboard_walk.next + 1);
                return cmd::resume_clipboard(std::move(m.ui.clipboard_walk),
                                             refused ? "the terminal refused the read (kitty: add read-clipboard "
                                                       "to clipboard_control in kitty.conf)"
                                                     : "the terminal didn't answer",
                                             refused);
            }
            m.ui.clipboard_walk = {};

            // 读取的终端 REFUSED——它回答为EPERM。
            //
            // 首先检查，在文本 回退 之前，
            // 因为这是我们确切地知道出了什么问题以及确切
            // 地哪一行修复了它的情况。下面的每条消息都是
            // 根据间接证据进行的猜测；这是终端告诉我们的。
            if (refused) {
                std::string txt = std::move(m.ui.clipboard_text_fallback);
                m.ui.clipboard_text_fallback.clear();
                m.ui.clipboard_query_done = m.ui.clipboard_query_seq;
                auto toast = set_status_toast(
                    m,
                    "clipboard: your terminal refused the image read \xe2\x80\x94 "
                    "in kitty.conf add `clipboard_control write-clipboard "
                    "write-primary read-clipboard read-primary` and restart "
                    "kitty (it is write-only by default)",
                    std::chrono::seconds{12});
                if (txt.empty()) return toast;
                return Cmd::batch(
                    composer_update(m, ComposerPaste{std::move(txt)}),
                    std::move(toast));
            }

            // 如果在我们等待图像时收到了 TEXT 回复（tmux
            // 竞态：tmux 从 缓冲区 回答了 OSC 52，
            // 而 OSC 5522 仍在飞向小猫），现在就使用它，
            // 而不是诊断 "no answer" — 终端 DID 答案，
            // 只是不是我们的那种类型想要的，并且在用户的文本被隐藏
            // 时向用户显示错误比显示文本更糟糕。
            if (!m.ui.clipboard_text_fallback.empty()) {
                std::string txt = std::move(m.ui.clipboard_text_fallback);
                m.ui.clipboard_text_fallback.clear();
                m.ui.clipboard_query_done = m.ui.clipboard_query_seq;
                auto toast = set_status_toast(
                    m,
                    "pasted text \xe2\x80\x94 for images over SSH, kitty needs "
                    "clipboard_control read-clipboard in kitty.conf (restart "
                    "kitty after adding it)",
                    std::chrono::seconds{8});
                return Cmd::batch(
                    composer_update(m, ComposerPaste{std::move(txt)}),
                    std::move(toast));
            }
            // 说出用户的EXACT情况和最短路径——未答复的查询
            // 决不能在沉默中死掉-end。解决这个问题会遍历 /
            // proc 并询问 tmux，因此它在 工作线程 上运行。
            return cmd::diagnose_clipboard(m.env.ssh);
        },
        [&](ImagePathSniffed& e) -> Cmd {
            if (e.path.empty())
                return composer_update(
                    m, ComposerPaste{std::move(e.text), /*path_checked=*/true});
            begin_edit(m.ui.composer);
            Attachment att;
            att.kind       = Attachment::Kind::Image;
            att.path       = std::move(e.path);
            att.media_type = std::move(e.media_type);
            att.byte_count = e.body.size();
            att.body.set_bytes(std::move(e.body));
            std::size_t idx = m.ui.composer.attachments.size();
            m.ui.composer.attachments.push_back(std::move(att));
            auto placeholder = attachment::make_placeholder(idx);
            m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
            m.ui.composer.cursor += static_cast<int>(placeholder.size());
            m.ui.composer.expanded = true;
            return Cmd::none();
        },
        [&](ClipboardRead& e) -> Cmd { return on_clipboard_read(m, e); },
        [&](ClipboardDiagnosed& e) -> Cmd {
            return set_status_toast(m, std::move(e.message),
                                    std::chrono::seconds{8});
        },
        [&](ComposerRecallQueued) -> Cmd {
            // No-op当没有什么可回忆的时候——呼叫者（Up
            // -arrowkeymap) 仅当队列非时才会发出
            // 此消息-empty，但谓词跨框架是活跃的，
            // 因此要采取防御措施。
            if (m.ui.composer.queued.empty()) return Cmd::none();

            // 将队列排入由 '\n' 加入的作曲家，并将任何前
            // -existing 作曲家文本附加在另一个 '\n' 之后。
            // 镜像 Claude 代码的 `Lc_`（二进制程序
            // 偏移量 76303220）：按一次 ↑ 将 整个 可编辑队列
            // 排入一个作曲家 加载 — 没有 per-item 循环。
            // Multi-line排队项目保留其换行符，
            // 因此成为排队消息的粘贴在召回轮
            // -trip中幸存下来。
            //
            // 每个排队的槽可以携带其自己的附件[]，
            // 其文本中具有0-based占位符索引。
            // 我们合并附件向量并重写每个槽的占位符
            // 以在连接时指向新的（合并的）索引。
            std::string             recalled;
            std::vector<Attachment> merged_atts;
            auto append_with_remap = [&](std::string_view text,
                                         std::vector<Attachment>& atts) {
                // base =索引，在我们推送它们之后，
                // 该槽的附件将落在merged_atts中。
                std::size_t base = merged_atts.size();
                for (std::size_t i = 0; i < text.size(); ) {
                    if (static_cast<unsigned char>(text[i]) == attachment::kSentinel) {
                        auto len = attachment::placeholder_len_at(text, i);
                        if (len > 0) {
                            auto local_idx = attachment::placeholder_index(text, i);
                            // 删除无法解析的占位符（腐败/过期索引）
                            // ——与attachment::
                            // expand相同的防御策略。
                            if (local_idx < atts.size())
                                recalled += attachment::make_placeholder(base + local_idx);
                            i += len;
                            continue;
                        }
                    }
                    recalled.push_back(text[i++]);
                }
                for (auto& a : atts) merged_atts.push_back(std::move(a));
            };
            for (std::size_t i = 0; i < m.ui.composer.queued.size(); ++i) {
                if (i > 0) recalled.push_back('\n');
                append_with_remap(m.ui.composer.queued[i].text,
                                  m.ui.composer.queued[i].attachments);
            }
            // 光标落在调用的文本和用户的 pre-existing 作曲家
            // 输入之间的边界处——正是他们想要开始编辑或附加的地方。
            // （Claude代码的接口边界是`O.join("\n").length + 1 + _`；
            // 我们使用相同的想法：如果后面有任何内容，
            // 则end-of-recalled + 1，
            // 否则end-of-recalled。）
            int boundary = static_cast<int>(recalled.size());
            if (!m.ui.composer.text.empty()) {
                // 用户的前
                // -existing作曲家文本可能将ALSO携带占位符放入composer.attachments中；
                // 最后合并它们并在拼接之前重新映射。
                recalled.push_back('\n');
                ++boundary;
                append_with_remap(m.ui.composer.text, m.ui.composer.attachments);
            }
            begin_edit(m.ui.composer);
            m.ui.composer.text        = std::move(recalled);
            m.ui.composer.attachments = std::move(merged_atts);
            m.ui.composer.cursor      = boundary;
            // Multi-line内容→翻转展开，因此作曲家的
            // `expanded`上限（16行）采用副作用，而不是8-row未展开的上限。
            // 与 ComposerPaste 相同的触发器。
            if (m.ui.composer.text.find('\n') != std::string::npos)
                m.ui.composer.expanded = true;
            // 破坏性召回：排队的项目现在存在于作曲家缓冲区中的ONLY中。
            // Re-submit re-queues 位于 末尾（新的
            // 末尾 位置）。清除作曲家会丢弃它们。与 Claude 代码
            // 相同的 trade-off — 保持数据模型简单（无
            // "soft-deleted, recallable" 中间状态）。
            m.ui.composer.queued.clear();
            return Cmd::none();
        },
        [&](ComposerQueuePeekPrev) -> Cmd {
            // Alt+↑ —进一步进入INTO队列。顺序：队列[最后
            // 一个]（最-recently排队，
            // 最接近"the one I just typed"）→队列
            // [last-1]→…→队列[0]。因此，首先按 加载
            // 和 末尾 项，这是用户在纠正最后一条排队消息中的拼写
            // 错误时通常想要的。
            if (m.ui.composer.queued.empty()) return Cmd::none();
            auto& cs = m.ui.composer;
            cs.undo_coalescing = false;
            const int n = static_cast<int>(cs.queued.size());
            int next_idx;
            // 从历史进入队列现在只是状态改变：
            // 分配QueuePeek REPLACES历史，
            // 因此曾经坐在这里的
            // 两者互斥，应放弃当前历史消息选择。
            // 块消失了。 快照 仍然需要删除，因为屏幕上的文本 WAS 是历史
            // 选择并保存它会将其与实时草稿混为一谈。
            if (const auto peek = cs.queue_peek_index()) {
                // 已经在查看——在继续之前将用户的编辑提交
                // 回到他们来自的队列槽中。如果没有这个，
                // Alt+↑→类型→Alt+↑将默默地放弃带
                // 类型的更正。
                auto& slot = cs.queued[static_cast<std::size_t>(*peek)];
                slot.text        = std::move(cs.text);
                slot.attachments = std::move(cs.attachments);
                next_idx = *peek - 1;
                if (next_idx < 0) next_idx = 0;   // 夹钳，无缠绕
            } else {
                if (!cs.is_live()) {
                    // 来自历史选择：放弃其快照。
                    cs.draft_save.reset();
                    cs.draft_save_attachments.clear();
                } else {
                    // 第一次按实时草稿 — 快照 文本 + 附件，
                    // 因此 Alt+↓ 经过 末尾 即可恢复它们。
                    cs.draft_save             = cs.text;
                    cs.draft_save_attachments = cs.attachments;
                }
                next_idx = n - 1;
            }
            cs.browsing = ComposerState::QueuePeek{next_idx};
            // 将槽移动到现场作曲家中（
            // 我们将在下一个周期/提交时将其写回）。
            cs.text        = cs.queued[static_cast<std::size_t>(next_idx)].text;
            cs.attachments = cs.queued[static_cast<std::size_t>(next_idx)].attachments;
            cs.cursor = static_cast<int>(cs.text.size());
            // Peek 不撤消 快照 (round-trip non-destructive)。
            // Multi-line 偷看内容 → 荣誉扩展上限。
            if (cs.text.find('\n') != std::string::npos)
                cs.expanded = true;
            return Cmd::none();
        },
        [&](ComposerQueuePeekNext) -> Cmd {
            // Alt+↓ — 向实时选秀方向走回队列的 OUT。
            // 不偷看时No-op。
            auto& cs = m.ui.composer;
            const auto peek = cs.queue_peek_index();
            if (!peek) return Cmd::none();
            cs.undo_coalescing = false;
            const int n = static_cast<int>(cs.queued.size());
            // 首先将当前编辑提交回到其槽中。
            if (*peek < n) {
                auto& slot = cs.queued[static_cast<std::size_t>(*peek)];
                slot.text        = std::move(cs.text);
                slot.attachments = std::move(cs.attachments);
            }
            const int next_idx = *peek + 1;
            if (next_idx >= n) {
                // 走过末尾——恢复实时草稿(文本
                // +标签)并离开查看模式。
                cs.browsing   = ComposerState::Live{};
                cs.text        = cs.draft_save.value_or(std::string{});
                cs.attachments = std::move(cs.draft_save_attachments);
                cs.draft_save_attachments.clear();
                cs.cursor = static_cast<int>(cs.text.size());
                cs.draft_save.reset();
                return Cmd::none();
            }
            cs.browsing    = ComposerState::QueuePeek{next_idx};
            cs.text        = cs.queued[static_cast<std::size_t>(next_idx)].text;
            cs.attachments = cs.queued[static_cast<std::size_t>(next_idx)].attachments;
            cs.cursor      = static_cast<int>(cs.text.size());
            if (cs.text.find('\n') != std::string::npos)
                cs.expanded = true;
            return Cmd::none();
        },
        [&](ComposerSlashMove& e) -> Cmd {
            const auto cat  = slash::catalog_of(m);
            const auto rows = slash::menu_rows(m, cat);
            if (rows.empty()) return Cmd::none();
            m.ui.composer.slash.index =
                ui::step(m.ui.composer.slash.index, e.delta, static_cast<int>(rows.size()));
            return Cmd::none();
        },
        [&](ComposerSlashComplete) -> Cmd {
            const auto cat  = slash::catalog_of(m);
            const auto rows = slash::menu_rows(m, cat);
            if (rows.empty()) return Cmd::none();
            const int i = std::clamp(m.ui.composer.slash.index, 0,
                                     static_cast<int>(rows.size()) - 1);
            begin_edit(m.ui.composer);
            auto done = slash::complete(m.ui.composer.text, *rows[static_cast<std::size_t>(i)].entry);
            m.ui.composer.text   = std::move(done.text);
            m.ui.composer.cursor = done.cursor;
            m.ui.composer.slash  = {};
            return Cmd::none();
        },
        [&](ComposerSlashDismiss) -> Cmd {
            if (const auto q = slash::menu_query(m.ui.composer.text, m.ui.composer.cursor))
                m.ui.composer.slash.dismissed = std::string{q->query};
            return Cmd::none();
        },
        [&](SlashPromptRendered& e) -> Cmd {
            m.ui.composer.slash_pending = false;
            // 用户同时编辑了该行：他们的新文本获胜。
            if (m.ui.composer.text != e.typed) return Cmd::none();
            if (e.text.empty())
                return set_status_toast(m, "that prompt's server didn't answer");
            // 缺少必需的参数：说出要输入的内容并保留草稿，
            // 因此只需按一下键即可修复，并且无需花费任何时间。
            if (e.usage)
                return set_status_toast(m, std::move(e.text),
                                        std::chrono::seconds{6});
            begin_edit(m.ui.composer);
            m.ui.composer.text   = std::move(e.text);
            m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
            return submit_message(m);
        },
        [&](ComposerQueuePopLast) -> Cmd {
            // Alt+Backspace 在没有活动的窥视的空作曲家
            // 上 —"undo queue":删除最近排队的项目。
            // 当您发出一条消息而您立即后悔而代理仍然忙碌时，
            // 这很有用。弹出的 字节 被删除（不会
            // 恢复到作曲家），因此这是纯粹的删除，
            // 反映了真实的 Backspace 删除字符的方式。
            // 如果你想编辑它，Alt+↑。
            if (m.ui.composer.queued.empty()) return Cmd::none();
            m.ui.composer.queued.pop_back();
            // 如果查看索引指向或超过被删除的末尾，
            // 则使其无效。 （订阅这个 Msg 作曲家的直播，
            // 但要保持警惕。）
            if (const auto peek = m.ui.composer.queue_peek_index();
                peek && *peek >= static_cast<int>(m.ui.composer.queued.size())) {
                m.ui.composer.browsing = ComposerState::Live{};
                m.ui.composer.draft_save.reset();
                m.ui.composer.draft_save_attachments.clear();
            }
            return Cmd::none();
        },
    }, cm);

    // `/`菜单的选择属于一个查询。新的查询从顶部开始；
    // 解雇仅在其令牌未更改时有效。
    {
        auto& sm = m.ui.composer.slash;
        const auto q = slash::menu_query(m.ui.composer.text, m.ui.composer.cursor);
        const std::string now = q ? std::string{q->query} : std::string{};
        if (!q || now != sm.query) sm.index = 0;
        if (sm.dismissed && (!q || *sm.dismissed != now)) sm.dismissed.reset();
        sm.query = now;
    }

    // 归并在上面武装的机会主义副作用中（预热、
    // 主动OAuth刷新），而不干扰匹配臂产生的任何Cmd。
    // none()短-circuits是常见情况——几乎每次击键
    // ——零开销。
    if (prewarm.is_none() && proactive_refresh.is_none()) return step;
    return Cmd::batch(std::move(step), std::move(prewarm),
                      std::move(proactive_refresh));
}

} // namespace agentty::app::detail
