#pragma once
// agentty::ui::nav-覆盖面板的声明性导航语法。
//
// 每个列表-style 覆盖面板回答相同的关键词汇：
//
//   Esc → close/back Enter → 选择↑/↓ (+ j/k)
//   → 移动±1 Home/End/PgUp/PgDn → 跳转Backspace
//   → 滤镜擦除可打印 → 滤镜类型（或vim键，无滤镜时）
//   <open-chord re-press> → 切换关闭
//
// subscribe.cpp用于将-copy该表转换为15+`on_X(ev)`开关
// 功能——`on_thread_list`和`on_checkpoint_picker`
// 是字节-level克隆，并且每个新的覆盖面板重新-implemented（或者
// 忘记：缺少j/k、缺少PageUp、缺少切换-close）语法又一个时间。
//
// NavSpec将一个覆盖面板的ENTIRE键映射表示为声明性值：
// 共享语法被翻译为 一次 (在 `translate` 中)，
// 每个 覆盖面板 仅通过 `extra` 提供其 Msg 构造
// 函数+其真正唯一的键。新的 选择面板 通过填充结构来获得一致、
// 完整的导航 - 语法不能再随 -overlay 漂移。
//
// 设计说明：
//   • Msg 工厂是 std::function<Msg()> 等，按照 覆盖面板 绑定
//     subscribe.cpp。空工厂= "this overlay doesn't do that verb"
//     （e.g。叉子选择面板没有过滤器，待办事项弹出面板没有光标）。
//   · `vim_nav` — j/k (以及可选的 h/l) 作为 Move/Step 同义词。 ON 为
//     读取-only列表； OFF，其中可打印内容键入过滤器
//     查询（j/k 必须保留可键入文本）。
//   · `toggle_close_chord` — 覆盖面板 自己的开放和弦（^K、^J、^T…）
//     再次关闭它。 Open/close 作为数据对称，而不是每个 处理函数
//     中的 re-derived 特殊情况。
//   ·`extra`运行FIRST，因此覆盖面板可以覆盖
//     共享语法（diff-review的Enter = AcceptHunk，
//     结果卡的Enter =丢弃）而不分叉整个表。

#include <functional>
#include <optional>

#include <maya/maya.hpp>

#include "agentty/runtime/msg.hpp"

namespace agentty::ui::nav {

// 每个寻呼机共享的跳转目标。 覆盖面板 与 `Jump` 消息映射此
// enum onto their own `Where`; overlays without one map Page* onto ±N Move.
enum class Jump { Home, End, PageUp, PageDown };

struct NavSpec {
    // ── 共享语法动词(null = 不受此覆盖面板支持). ────
    std::function<Msg()>            close;       // Esc（和q，其中vim_nav）
    std::function<Msg()>            select;      // Enter
    std::function<Msg(int)>         move;        // ↑=-1 ↓=+1 (j/k 当 vim_nav 时)
    std::function<Msg(Jump)>        jump;        // Home/End/PgUp/PgDn
    std::function<Msg(char32_t)>    filter_ch;   // 可打印→查询追加
    std::function<Msg()>            filter_bs;   // Backspace→查询擦除

    // vim 的 j/k 对应移动，q 对应关闭。
    // RETIRED (保留为字段，所以旧的 调用 站点 编译；translate()
    // 忽略它)。裸露的可打印内容要么输入过滤器，要么不执行任何操作；
    // 每个操作键都是 ^chord、Enter、Esc 或箭头。一种语法，没有方言。
    bool vim_nav = false;

    // Bare-letter是该覆盖面板的和弦动词的别名。
    //
    // 关键策略说可打印的TYPES或做NOTHING——其
    // 存在是因为具有文本输入的面板无法区分命令和字符。
    // PURE LIST 没有这样的歧义：这里没有任何类型，
    // 所以字母是明确的，和弦是纯粹的摩擦（更糟糕的是：
    // 终端复用器吃和弦——^A是tmux的默认前缀，
    // ^B是屏幕的，所以第一次按下根本不会到达我们）。
    //
    // 由 覆盖面板 填充 仅，没有 filter_ch 且没有文本字段：
    // {信，消息}。 translate() 在和弦传递后应用它们，
    // 当 `filter_ch` 为空时应用 仅 - 因此将
    // 一个连接到过滤面板是 no-op 而不是吃掉查询的击键。
    std::vector<std::pair<char32_t, std::function<Msg()>>> letter_verbs;

    // 将分页表示为大移动(无跳转工厂)的覆盖面板的页面粒度：
    // PgUp/PgDn成为移动(∓page_step)。
    int page_step = 10;

    // 覆盖面板自己的开放和弦，如ctrl-normalised字母('k'代表^K，
    // 'j'代表^J，…，'/'代表^/)。 re-press 关闭 覆盖面板。 0 = 无。
    char32_t toggle_close_chord = 0;

    // 覆盖面板-specific键，尝试了BEFORE共享语法，以便它们可以
    // 覆盖任何动词。收到下面的 ctrl-normalised 事件 视图。
    std::function<std::optional<Msg>(const maya::KeyEvent&)> extra;
};

// CharKey的标准化视图：原始控制字节 (0x01..
// 0x1A-传统终端使用NO ctrl 标志发送Ctrl-<letter>)被
// 折叠成具有ctrl=true的字母，因此每个消费者都与一种规范形式进行比较。
struct CharView {
    char32_t c    = 0;      // 归一化代码点（原始ctrl→字母）
    char32_t raw  = 0;      // 原始码点
    bool     ctrl = false;  // ev.mods.ctrl OR 原始控制 字节
};

[[nodiscard]] inline std::optional<CharView> char_view(const maya::KeyEvent& ev) noexcept {
    const auto* ck = std::get_if<maya::CharKey>(&ev.key);
    if (!ck) return std::nullopt;
    CharView v;
    v.raw = ck->codepoint;
    v.c   = v.raw;
    const bool raw_ctrl = (v.raw >= 0x01 && v.raw <= 0x1A);
    if (raw_ctrl) v.c = U'a' + (v.raw - 1);
    v.ctrl = ev.mods.ctrl || raw_ctrl;
    // ctrl+/和ctrl+_是相同的字节 (0x1F)。一旦应用程序要求 modifyOtherKeys，
    // tmux re-encodes 就按 ctrl+_，因此 归并 都到 ctrl+/.
    if (v.raw == 0x1F || (v.ctrl && v.raw == U'_')) { v.c = U'/'; v.ctrl = true; }
    return v;
}

// 通过规范翻译一键事件。当该键对此 覆盖面板 没有
// 任何意义时，返回 nullopt（调用者决定该键是
// 落入全局处理还是被吞噬）。
[[nodiscard]] inline std::optional<Msg> translate(const NavSpec& s,
                                                  const maya::KeyEvent& ev) {
    using maya::SpecialKey;

    // 1.首先是覆盖面板-specific键——它们可以覆盖共享动词。
    if (s.extra)
        if (auto r = s.extra(ev)) return r;

    // 2.特殊键——语法的统一核心。
    if (const auto* sk = std::get_if<SpecialKey>(&ev.key)) {
        switch (*sk) {
            case SpecialKey::Escape:
                if (s.close) return s.close();
                break;
            case SpecialKey::Enter:
                if (s.select) return s.select();
                break;
            case SpecialKey::Up:
                if (s.move) return s.move(-1);
                break;
            case SpecialKey::Down:
                if (s.move) return s.move(+1);
                break;
            case SpecialKey::Backspace:
                if (s.filter_bs) return s.filter_bs();
                break;
            case SpecialKey::Home:
                if (s.jump) return s.jump(Jump::Home);
                if (s.move) return s.move(-1000000);
                break;
            case SpecialKey::End:
                if (s.jump) return s.jump(Jump::End);
                if (s.move) return s.move(+1000000);
                break;
            case SpecialKey::PageUp:
                if (s.jump) return s.jump(Jump::PageUp);
                if (s.move) return s.move(-s.page_step);
                break;
            case SpecialKey::PageDown:
                if (s.jump) return s.jump(Jump::PageDown);
                if (s.move) return s.move(+s.page_step);
                break;
            default: break;
        }
        return std::nullopt;
    }

    // 3.字符键。
    const auto v = char_view(ev);
    if (!v) return std::nullopt;

    // Open-chord re-press → 切换 关闭（在任何东西都可以输入之前）。
    if (v->ctrl && s.toggle_close_chord && s.close) {
        const char32_t lo = s.toggle_close_chord;
        const char32_t up = (lo >= U'a' && lo <= U'z')
                          ? lo - (U'a' - U'A') : lo;
        // ^/作为原始0x1F到达，没有控制标志； char_view 不属于 归并
        // （它不在 0x01..0x1A 中），因此也与原始 字节 匹配。
        if (v->c == lo || v->c == up
            || (lo == U'/' && v->raw == 0x1F))
            return s.close();
    }

    // 过滤器胜过vim nav:可打印类型进入查询。
    if (s.filter_ch && !v->ctrl && v->raw >= 0x20)
        return s.filter_ch(v->raw);

    // Bare-letter 动词——仅纯列表（通过提前返回来保护上面的！
    // filter_ch，并且再次在这里，因此规则是本地的）。
    // 不区分大小写：列表命令不应关心 Shift。
    if (!s.filter_ch && !v->ctrl && !s.letter_verbs.empty()) {
        const char32_t lo = (v->c >= U'A' && v->c <= U'Z')
                          ? v->c + (U'a' - U'A') : v->c;
        for (const auto& [key, make] : s.letter_verbs)
            if (key == lo && make) return make();
    }

    // 没有其他裸-letter动词：可打印TYPES（上面过滤，
    // 形成文本行）或NOTHING——一个关键策略，没有方言。
    return std::nullopt;
}

} // namespace agentty::ui::nav

namespace agentty::ui::panel {

// 关闭此类覆盖面板的消息。
//
// BACKSTOP为逃生保证(见subscribe.cpp)。每个专有的
// 覆盖面板 都会吞下无人认领的密钥 - 这就是它成为 弹出面板 的原因 - 因此，
// 无法回答 Esc 的 处理函数 会让用户无路可走，应用程序看起来会冻结。
// Retrieval 面板 正好带来了这个错误。
//
// 分派器 不是相信 15 个 处理函数 中的每一个都能永远正确地获得 Esc，
// 而是退回到这一点。 弹出面板 可以是有缺陷的、半 -built 或全新的，
// 而 仍然 永远不会困住用户。
//
// 详尽的种类(-Wswitch)，因此添加覆盖面板而不
// 命名其关闭消息是编译警告，而不是你不能离开的弹出
// 面板——保证不能依赖于任何人记住它。
[[nodiscard]] inline Msg close_msg(Kind k) noexcept {
    switch (k) {
        case Kind::Login:           return Msg{CloseLogin{}};
        case Kind::Permission:      return Msg{PermissionReject{}};
        case Kind::Palette:  return Msg{ClosePalette{}};
        case Kind::Mention:         return Msg{CloseMention{}};
        case Kind::Symbol:          return Msg{CloseSymbol{}};
        case Kind::CodeBlocks:      return Msg{CloseCodeBlocks{}};
        case Kind::CodeBlockResult: return Msg{CloseCodeBlocks{}};
        case Kind::ToolOutput:      return Msg{CloseToolOutput{}};
        case Kind::Checkpoints:     return Msg{CloseCheckpoints{}};
        case Kind::Rag:     return Msg{CloseRag{}};
        case Kind::Stats:           return Msg{CloseStats{}};
        case Kind::Skills:          return Msg{CloseSkills{}};
        case Kind::SettingsList:    return Msg{CloseSettingsList{}};
        case Kind::PluginEdit:      return Msg{ClosePluginEdit{}};
        case Kind::Appearance:      return Msg{CloseAppearance{}};
        case Kind::Sandbox:         return Msg{CloseSandbox{}};
        // 关闭列表编辑器COMMITS，这就是为什么这不是
        // CloseSandboxList：表单中的Esc意味着
        // "leave this field, keeping it"，并且在Esc
        // 上丢弃您的条目的列表将是面板中离开失去工作的一个地方。
        case Kind::SandboxList:     return Msg{SandboxListClose{}};
        case Kind::Fork:            return Msg{CloseFork{}};
        case Kind::Models:     return Msg{CloseModels{}};
        case Kind::Providers:  return Msg{CloseProviders{}};
        case Kind::ThreadList:      return Msg{CloseThreadList{}};
        case Kind::SmartMode:       return Msg{CloseSmartMode{}};
        case Kind::WebSearch:       return Msg{CloseWebSearch{}};
        case Kind::PasteSettings:   return Msg{ClosePasteSettings{}};
        case Kind::DiffReview:      return Msg{CloseDiffReview{}};
        // 环境：从不吞咽，因此它永远不需要救援。
        case Kind::Todo:            return Msg{NoOp{}};
        case Kind::None:            return Msg{NoOp{}};
    }
    return Msg{NoOp{}};
}

} // namespace agentty::ui::panel
