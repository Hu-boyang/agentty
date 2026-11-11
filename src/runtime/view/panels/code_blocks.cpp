// code_blocks.cpp - 电话 ^G 代码 -block 选择面板 + 运行 -result 纸牌。
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {


// Ctrl +G — 最新助手回复的代码块。 行= 1
// -style 索引 + 语言标记 + 首 -line 预览
// + 行数。 密钥中的位数快捷键 处理函数 地图 1 -based ,
// 所以领先的数字是教授快速路径的支付能力。
Element code_blocks_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::CodeBlocks>();
    if (!o) return nothing();

    Panel::Config cfg;
    cfg.title      = " Run Code Block ";
    cfg.accent     = success;
    cfg.min_width  = kPanelWide;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.code_blocks_scroll;
    cfg.selected   = o->blocks.empty() ? -1 : o->index;

    cfg.items.reserve(o->blocks.size());
    // 第一通道宽度:徽标列被加到一个通用宽度,所以每行的预
    // 览文本都在同一列开始(一个QQ运行徽标和一个`python`
    // 标记徽标否则会错配). Mirrors 工具
    // 输出查看器的徽标对齐.
    int badge_w = 0;
    for (const auto& b : o->blocks) {
        const bool runnable = code_blocks::is_shell_language(b.language);
        const std::string lang = b.language.empty() ? std::string{"sh"} : b.language;
        const std::string bd = runnable ? std::string{" \xe2\x96\xb6 "}
                                        : " " + lang + " ";
        badge_w = std::max(badge_w, string_width(bd));
    }
    for (int i = 0; i < static_cast<int>(o->blocks.size()); ++i) {
        const auto& b = o->blocks[static_cast<std::size_t>(i)];
        // 20 正文 的第一个非-blank 线作为预览 —
        // — 用户在屏幕上相对于回复的视觉匹配。
        // 跳过前导空白意味着一个以 comment/newline
        // 打开的块仍然预览它的第一个真实命令而不是空行。
        std::string_view body{b.body};
        while (!body.empty() && (body.front() == '\n' || body.front() == '\r'))
            body.remove_prefix(1);
        auto eol = body.find('\n');
        std::string preview{body.substr(0, eol == std::string_view::npos
                                             ? body.size() : eol)};
        const bool runnable = code_blocks::is_shell_language(b.language);
        const std::string lang = b.language.empty() ? std::string{"sh"}
                                                    : b.language;

        Panel::Item row;
        // 徽章 = 跑钱,稳定彩色锚( 不 被选中的行上暗化,
        // 所以 "which of these actually runs"
        // 阅读一览:a ` ▶ ` 在成功区域中为可运行块播放格莱弗,
        // 或者在哑音中只为一个我们可操作的语言标记 edit/copy.
        if (runnable) {
            row.badge       = " \xe2\x96\xb6 ";           // ▶
            row.badge_style = Style{}.with_fg(success).with_bold();
        } else {
            row.badge       = " " + lang + " ";
            row.badge_style = fg_dim(muted);
        }
        if (int bw = string_width(row.badge); bw < badge_w)
            row.badge.append(static_cast<std::size_t>(badge_w - bw), ' ');
        // 领先:1 -9快 -path 数字( 教快捷键的付价)
        // + 第一个 -line 预览。
        row.leading = std::to_string(i + 1) + "  " + preview;
        row.leading_style  = runnable ? fg_of(fg) : fg_dim(muted);
        // 拖曳: 语言 \ 线数。 对于一个可运行的块,
        // 语言会移动到意义( QQ徽章) , 因此这里的标记只是方言;
        // 对于一个非-runnable 的标记已经携带它,
        // 所以只显示大小, 以避免重复标记。
        row.trailing = (runnable ? lang + " \xc2\xb7 " : std::string{})
                     + std::to_string(b.line_count)
                     + (b.line_count == 1 ? " line" : " lines");
        row.trailing_style = fg_dim(muted);
        cfg.items.push_back(std::move(row));
    }

    cfg.footer.push_back(text(""));
    cfg.footer.push_back(key_hints({
        {"↑↓", "move", 5},        // ↑↓
        {"Enter/1-9", "run", 3},
        {"e", "edit", 4},
        {"y", "copy", 4},
        {"Esc", "close", 4},
    }));

    return Panel{std::move(cfg)}.build();
}

// 员额 -run 结果卡。用户已经看到全部输出在真实
// 中实时 终端 (而且它仍然留在原生的) 回滚缓冲区
// 上方为 TUI );此卡显示摘要——命令,退出代码,
// 大小——加 最后 少行(错误位于结尾)和决定键。
Element code_block_result_panel(const Model& m) {
    auto* r = m.ui.panel.get<pn::CodeBlockResult>();
    if (!r) return nothing();

    const bool ok_exit = !r->timed_out && r->exit_code == 0;

    Panel::Config cfg;
    cfg.title      = " Run Result ";
    cfg.accent     = ok_exit ? success : danger;
    cfg.min_width  = kPanelWide;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.code_blocks_scroll;
    cfg.selected   = -1;   // 改为-only - 无光标行

    // 头文件: "$ command" +状态线.
    {
        std::string cmd_line = r->command;
        // 仅限一行——多行 -line 块读取差于 a 头文件.
        if (auto eol = cmd_line.find('\n'); eol != std::string::npos) {
            cmd_line.resize(eol);
            cmd_line += " \xe2\x80\xa6";   // …
        }
        cfg.header.push_back(h(text("$ ", fg_bold(cfg.accent)),
                               text(std::move(cmd_line), fg_of(fg))).build());
        std::size_t lines = r->output.empty() ? 0 : 1;
        for (char c : r->output) if (c == '\n') ++lines;
        std::string status = r->timed_out
            ? "timed out"
            : "exit " + std::to_string(r->exit_code);
        status += " \xc2\xb7 " + std::to_string(lines) + " lines \xc2\xb7 ";
        status += (r->output.size() >= 1024)
            ? std::to_string(r->output.size() / 1024) + " KB"
            : std::to_string(r->output.size()) + " B";
        cfg.header.push_back(text("  " + status,
            ok_exit ? fg_of(muted) : fg_bold(danger)));
        cfg.header.push_back(sep);
    }

    // 完全捕获, 行 -numbered 类似工具输出查看器: 右
    // 对齐的沟槽 + 一个状态 -hued ⁇ 管(红在一
    // failed/timed-out 数字很容易说
    // "line 42 is where it broke" 并匹配 Ctrl +
    // O查看器,所以两个输出表面读取相同.
    //
    // 手动窗口( 纪律与 tool_output_viewer :
    // 捕获封顶为2 MB ——多达数万行——将每行元素建在一个卷轴容器中,
    // 以铺设 全部 每一个键盘都会被扣上 而不是被扣上 按需加
    // 载 线条 一次 变成静态 缓存 由输出键入 字节 ,
    // 然后只将可见的[y, y+vh]切片输入 选择面板 (
    // cfg.scroll = nullptr ),所以一个 帧
    // 费用 O( 可视区域 )无论捕获大小.
    struct ResultBodyCache {
        const void*          bytes_key = nullptr;
        std::size_t          bytes_len = 0;
        bool                 ok        = false;
        std::vector<Element> rows;
    };
    static ResultBodyCache cache;   // UI 线程 仅与选择面板的静态相同学科.

    const void* bytes_key = static_cast<const void*>(r->output.data());
    if (cache.bytes_key != bytes_key || cache.bytes_len != r->output.size()
        || cache.ok != ok_exit) {
        cache.bytes_key = bytes_key;
        cache.bytes_len = r->output.size();
        cache.ok        = ok_exit;
        cache.rows.clear();

        std::string_view out{r->output};
        if (out.empty()) {
            cache.rows.push_back(
                text("  (no output captured)", fg_italic(muted))
                | height(1) | overflow(Overflow::Hidden));
        } else {
            const Color pipe_hue = ok_exit ? muted : danger;
            std::vector<std::string_view> lines;
            {
                std::size_t pos = 0;
                while (pos <= out.size()) {
                    std::size_t eol = out.find('\n', pos);
                    std::size_t len =
                        (eol == std::string_view::npos ? out.size() : eol) - pos;
                    lines.push_back(out.substr(pos, len));
                    if (eol == std::string_view::npos) break;
                    pos = eol + 1;
                }
            }
            const int gutter_w = static_cast<int>(
                std::to_string(std::max<std::size_t>(1, lines.size())).size());
            cache.rows.reserve(lines.size());
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::string num = std::to_string(i + 1);
                if (static_cast<int>(num.size()) < gutter_w)
                    num.insert(0, gutter_w - num.size(), ' ');
                cache.rows.push_back(
                    hstack().width(Dimension::percent(100))(
                      text("  " + num + " ", fg_dim(warn)),
                      text("\xe2\x94\x82 ", fg_dim(pipe_hue)),   // │
                      text(std::string{lines[i]},
                           ok_exit ? fg_of(muted) : fg_of(fg))
                          | clip | grow(1.0f) | shrink(1.0f)
                    ).build()
                    | height(1) | overflow(Overflow::Hidden));
            }
        }
    }

    // 窗口缓存行到 可视区域 ; 在此保持滚动边框,
    // 因此 状态更新函数 夹子保持正确, 只输入可见的切片。
    const int total_rows = static_cast<int>(cache.rows.size());
    const int vh = std::max(1, cfg.viewport_h);
    auto& sc = m.ui.code_blocks_scroll;
    sc.max_y = std::max(0, total_rows - vh);
    sc.y     = std::clamp(sc.y, 0, sc.max_y);
    cfg.scroll = nullptr;
    const int first = sc.y;
    const int last  = std::min(total_rows, first + vh);
    for (int i = first; i < last; ++i)
        cfg.prebuilt.push_back(cache.rows[static_cast<std::size_t>(i)]);

    cfg.footer.push_back(text(""));
    // 滚动 -position 读出: 屏幕上显示的是哪几行抓取
    // —— 手动窗口没有滚动条, 所以这是显示输出量增加的支付率。
    // above/below (语法与工具查看器相同).
    if (total_rows > vh) {
        cfg.footer.push_back(text(
            "  " + std::to_string(first + 1) + "\xe2\x80\x93"   // –
                 + std::to_string(last) + " / "
                 + std::to_string(total_rows) + " lines",
            fg_dim(muted)));
    }
    cfg.footer.push_back(key_hints({
        {"a", "attach to composer", 6},
        {"y", "copy", 4},
        {"Esc", "discard", 4},
    }));

    return Panel{std::move(cfg)}.build();
}

// Ctrl +O 工具 -output 观众,一个阶段内的两个阶段 选择面板 铬:
//
//   LIST ——每套结算工具一行 调用 (最新第一):分类彩色工具
//           徽章( "Read", "Bash", "Edit" 和
//           笔录卡一样 详细线 还有 "ok · 1.2s · 48 KB" 尾随。
//           Enter 打开 正文.
//   BODY - 电话 完整 所选的存储输出
//           调用 时间卡 elides 长 正文
//           直接跳到中间生活的地方 previous/next 产出;
//           Esc 返回列表中。
//
// 来,来,来 覆盖面板 - 不在,没有在 -place 扩展卡片—
// —因为记录片的 承诺行不是可变 本地语言 回滚缓冲区 ;
// 在那里种一张牌会重写承诺的行( ) HardReset 腐败阶级。
// 覆盖面板 画画严格地在现场 可视区域 ,与其他所有类型相同 选择面板.

} // namespace agentty::ui
