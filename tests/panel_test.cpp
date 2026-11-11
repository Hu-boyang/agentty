// maya::Panel ——堕落的病例.
//
// 面板现在 仅 覆盖面板 部件: 每一个 选择面板 和应用程序中的
// 所有设置 渲染 让它变得边缘行为 加载-bearing 在某种程度上,
// 一个 -use 部件从来就不是——一个给它带空列表的呼叫者,
// 一个出局者 -of-range 光标,或一个 -row 终端 仍然必须
// 获得可用性 帧 ,因为替代方案是 覆盖面板 开着呢 弹出面板 和隐形。
//
// 这些是呼叫者可以合法生成的输入(空匹配集、
// 从更长的列表中留下的光标、调整大小) 终端 ) 加上只产生
// 一个错误的。两者都必须 渲染.

#include "agtest.hpp"

#include <maya/print.hpp>
#include <maya/widget/panel.hpp>

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using maya::Panel;

Panel::Item text_row(const char* label, const char* value = "") {
    Panel::Item r;
    r.leading = label;
    r.trailing = value;
    return r;
}

Panel::Item header_row(const char* label) {
    Panel::Item r;
    r.leading   = label;
    r.control   = maya::panel::Header{};
    return r;
}

// 渲染 面板在 `width`.
//
// COLUMNS 设置到相同的宽度,因为面板从“从
// “从”中获取其内容预算。 终端 预算必须和它实际的
// 宽度有关。 渲染 70岁,而环境 终端 说200
// 是无法在应用中发生的状态, 覆盖面板 伸缩至
// 终端 ——它使一个窗口值看起来没有被接受.
std::string render(Panel::Config c, int width = 60) {
    std::string saved;
    const bool had = std::getenv("COLUMNS") != nullptr;
    if (had) saved = std::getenv("COLUMNS");
    setenv("COLUMNS", std::to_string(width).c_str(), /*覆盖=*/1);

    auto out = maya::render_to_string(Panel{std::move(c)}.build(), width);

    if (had) setenv("COLUMNS", saved.c_str(), /*覆盖=*/1);
    else     unsetenv("COLUMNS");
    return out;
}

// 是否为 帧 带有上下边框 ? 无法绘制边框的面板是
// "open but invisible" 失败—— 覆盖面板 拥有键盘,
// 而未显示任何内容。
bool framed(const std::string& out) {
    return out.find("\xe2\x95\xad") != std::string::npos    // ╭
        && out.find("\xe2\x95\xb0") != std::string::npos;   // ╰
}

int count_of(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (std::size_t i = hay.find(needle); i != std::string::npos;
         i = hay.find(needle, i + needle.size())) ++n;
    return n;
}

// 排行图中故意有分歧的高度列表: 一些带行帮助( 哪些)
// 渲染 仅列在 FOCUSED 列,所以内容高度取决于光标所在之处,
// 有些错误,有些错误 头文件.
std::vector<Panel::Item> mixed_rows(int n) {
    std::vector<Panel::Item> rows;
    for (int i = 0; i < n; ++i) {
        Panel::Item r;
        r.leading = "row" + std::to_string(i);
        if (i % 3 == 0)  r.help    = "help for row " + std::to_string(i);
        if (i % 5 == 0)  r.error   = "bad value";
        if (i % 11 == 0) r.control = maya::panel::Header{};   // 头文件 都比这要高
        rows.push_back(std::move(r));
    }
    return rows;
}

} // 命名空间结束。

TEST_CASE("panel: an empty body still renders a frame") {
    // A 选择面板 其查询与无异。它必须绘制其边界, 其 头文件
    // 及其页脚——用户需要到某处看 "no matches" 并提醒
    // 人们: Esc 出口 门关上
    Panel::Config c;
    c.title  = " Empty ";
    c.footer.push_back(maya::Element{maya::TextElement{.content = "  Esc close"}});
    const auto out = render(std::move(c));

    CHECK(framed(out));
    CHECK(out.find("Empty") != std::string::npos);
    CHECK(out.find("Esc close") != std::string::npos);
}

TEST_CASE("panel: an empty body with a scroll state does not collapse") {
    // 危险的组合: 滚动 ON ,没有卷轴。 可视区域 按分钟计算(
    // N) viewport_h , 0)为零行,为零行 -height
    // 滚动区域会崩溃面板到它的边界。
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Empty ";
    c.scroll     = &s;
    c.viewport_h = 10;
    const auto out = render(std::move(c));

    CHECK(framed(out));
    CHECK(s.y == 0);
}

TEST_CASE("panel: an out-of-range cursor focuses nothing") {
    // 过期 索引 — 查询缩小了列表, 但光标尚未移动。
    // 它不能将焦点画在任意行上, 也不能崩溃。
    for (int sel : {-1, 5, 99, -99}) {
        Panel::Config c;
        c.title = " Stale ";
        c.items.push_back(text_row("Alpha"));
        c.items.push_back(text_row("Beta"));
        c.selected = sel;
        const auto out = render(std::move(c));

        CHECK(framed(out));
        CHECK(out.find("Alpha") != std::string::npos);
        // 任何地方都没有光标栏:超出范围意味着没有焦点.
        CHECK(out.find("\xe2\x96\x8e") == std::string::npos);   // ▎
    }
}

TEST_CASE("panel: the cursor never lands on a header") {
    // 头文件 显示 `selected` 其中一个是呼叫器错误,
    // 部件拒绝而不是 渲染 用户无法采取行动的焦点
    // ——一个似乎无所作为的箭头键。
    Panel::Config c;
    c.title = " Grouped ";
    c.items.push_back(header_row("Section"));
    c.items.push_back(text_row("Alpha"));
    c.selected = 0;                       // 编号 头文件
    const auto out = render(std::move(c));

    CHECK(framed(out));
    CHECK(out.find("SECTION") != std::string::npos);
    CHECK(out.find("\xe2\x96\x8e") == std::string::npos);   // 无光标栏
}

TEST_CASE("panel: a menu only opens on a Choice row") {
    // `menu_row` 指着一个 切换 是一个呼叫器错误。
    // 渲染 选项列表将让一个无法使用它的控制下下拉。
    Panel::Config c;
    c.title = " Mismatch ";
    Panel::Item t;
    t.leading = "Toggle";
    t.control = maya::panel::Toggle{true};
    c.items.push_back(std::move(t));
    c.selected = 0;

    Panel::Menu m;
    m.options = {"One", "Two"};
    c.menu     = m;
    c.menu_row = 0;                        // 不 a 选择行

    const auto out = render(std::move(c));
    CHECK(framed(out));
    CHECK(out.find("One") == std::string::npos);
    CHECK(out.find("Two") == std::string::npos);
}

TEST_CASE("panel: a menu on a Choice row renders its options") {
    // 正面的个案,因此上面的警卫不能通过失效菜单通过.
    Panel::Config c;
    c.title = " Choice ";
    Panel::Item ch;
    ch.leading = "Backend";
    ch.control = maya::panel::Choice{"One"};
    c.items.push_back(std::move(ch));
    c.selected = 0;

    Panel::Menu m;
    m.options     = {"One", "Two"};
    m.current     = 0;
    m.highlighted = 1;
    c.menu     = m;
    c.menu_row = 0;

    const auto out = render(std::move(c));
    CHECK(out.find("One") != std::string::npos);
    CHECK(out.find("Two") != std::string::npos);
    // QQ标为COMMITTED值,QQ标光标——两个事实,两个标记.
    CHECK(out.find("\xe2\x97\x89") != std::string::npos);   // ◉
    CHECK(out.find("\xe2\x9d\xaf") != std::string::npos);   // ❯
}

TEST_CASE("panel: a menu with fewer hints than options is safe") {
    // `hints` 是可选的, 可能很短。 用突出显示的选项索引它,
    // 而无需边框检查会读过结尾。
    Panel::Config c;
    c.title = " Hints ";
    Panel::Item ch;
    ch.leading = "Pick";
    ch.control = maya::panel::Choice{"A"};
    c.items.push_back(std::move(ch));
    c.selected = 0;

    Panel::Menu m;
    m.options     = {"A", "B", "C"};
    m.hints       = {"only the first"};    // 短于选项
    m.highlighted = 2;                     // 超过提示的结尾
    c.menu     = m;
    c.menu_row = 0;

    const auto out = render(std::move(c));
    CHECK(framed(out));
    CHECK(out.find("C") != std::string::npos);
}

TEST_CASE("panel: highlight offsets outside the label are ignored") {
    // 模糊 -match 位置来自计分器对一个字符串运行,
    // 该字符串可能自此被切换或替换。 Out -of-range 缩
    // 进不能通过标签索引。
    Panel::Config c;
    c.title = " Highlight ";
    Panel::Item r = text_row("abc");
    r.highlight = {0, 2, 99, -1};          // 99和1 已经超出范围
    c.items.push_back(std::move(r));
    c.selected = 0;

    const auto out = render(std::move(c));
    CHECK(framed(out));
    CHECK(out.find("abc") != std::string::npos);
}

TEST_CASE("panel: an empty label with highlights is safe") {
    // 变形配对:突出部分与任何文本都抵消。
    Panel::Config c;
    c.title = " Empty label ";
    Panel::Item r;
    r.highlight = {0, 1};
    c.items.push_back(std::move(r));
    c.selected = 0;

    CHECK(framed(render(std::move(c))));
}

TEST_CASE("panel: a tiny viewport still shows a row") {
    // 短篇小说 终端。该词 可视区域 地板是至少保持
    // 一行和 帧 可见,而不是崩溃到边框。
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Tiny ";
    c.scroll     = &s;
    c.viewport_h = 0;                      // 堕落
    for (int i = 0; i < 20; ++i)
        c.items.push_back(text_row("Row"));
    c.selected = 19;

    const auto out = render(std::move(c));
    CHECK(framed(out));
    CHECK(out.find("Row") != std::string::npos);
}

TEST_CASE("panel: a narrow terminal keeps the frame closed") {
    // min_width 下方有一层: 帧 面板
    // 就不再是盒子了。
    Panel::Config c;
    c.title     = " N ";
    c.min_width = 1;                       // 堕落
    c.items.push_back(text_row("Alpha", "value"));
    c.selected = 0;

    const auto out = render(std::move(c), 24);
    CHECK(framed(out));
}

TEST_CASE("panel: the cursor row's help is kept in view when scrolled") {
    // 一个字段的帮助行属于它的行。 单向行滚动就
    // 留下了一条经过底边的描述—— 它在您箭头
    // 射到行上读取时就消失了。
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Help ";
    c.scroll     = &s;
    c.viewport_h = 6;
    for (int i = 0; i < 12; ++i) c.items.push_back(text_row("Row"));
    Panel::Item last = text_row("Last");
    last.help = "the description of the last row";
    c.items.push_back(std::move(last));
    c.selected = 12;                       // 最后一行

    const auto out = render(std::move(c));
    CHECK(out.find("the description of the last row") != std::string::npos);
}

TEST_CASE("panel: rows stay inside the border at any width") {
    // 版式是弹性的,而不是算术的。以前每个版本都
    // 根据一个猜到的储备量度行并加进,每个猜想从真实
    // 的夹片中漂移——值在卷轴栏下滑动,并离开帧。
    maya::ScrollState s;
    for (int w : {30, 40, 60, 100}) {
        Panel::Config c;
        c.title      = " Width ";
        c.scroll     = &s;
        c.viewport_h = 4;
        for (int i = 0; i < 10; ++i)
            c.items.push_back(text_row(
                "a fairly long label that will not fit",
                "and a fairly long value too"));
        c.selected = 0;

        const auto out = render(std::move(c), w);
        // 每条线路完全为涂装的存储单元的`w`列:
        // 帧每行两侧关闭.
        std::size_t start = 0;
        while (start <= out.size()) {
            const std::size_t nl = out.find('\n', start);
            const std::string line = out.substr(start, nl - start);
            if (!line.empty())
                CHECK(maya::string_width(line) <= w);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }
}

TEST_CASE("panel: the caret stays visible in a value longer than its column") {
    // 将 光标 和您打过的所有字都从短线椭圆后面
    // 的右边缘打出。 您仍然可以编辑;
    // 您只是看不到它。 现在的值在 光标 下水平滚动,
    // 与每个 -line 编辑器一样。
    const std::string v =
        "a-very-long-endpoint-name-that-will-not-fit-in-the-column-at-all";

    for (std::size_t at : {std::size_t{0}, v.size() / 2, v.size()}) {
        Panel::Config c;
        c.title = " Caret ";
        Panel::Item r;
        r.leading = "Host";
        r.control = maya::panel::Text{v, at};
        c.items.push_back(std::move(r));
        c.selected = 0;

        const auto out = render(std::move(c), 70);
        // 光标在每一个位置上都显示在屏幕上.
        CHECK(out.find("\xe2\x96\x88") != std::string::npos);   // █
    }
}

TEST_CASE("panel: a scrolled value says which way it is scrolled") {
    // ⁇ / ⁇  标一个水平卷轴——故意不... TruncateEnd 使用,所以,
    // "there is more text this way" 与《公约》第2条不同
    // "this was cut off".
    const std::string v =
        "a-very-long-endpoint-name-that-will-not-fit-in-the-column-at-all";

    Panel::Config c;
    c.title = " Caret ";
    Panel::Item r;
    r.leading = "Host";
    r.control = maya::panel::Text{v, v.size()};   // 光标 现场 END
    c.items.push_back(std::move(r));
    c.selected = 0;

    const auto out = render(std::move(c), 70);
    CHECK(out.find("\xe2\x80\xb9") != std::string::npos);   // 左转更多
}

TEST_CASE("panel: the text caret is not the row cursor bar") {
    // 两者都使用了 U+258E,所以一行编辑后,在0栏和中段显示相同
    // 的格言 -value - 电话 光标 读作第二个光标。
    // 光标 现在已经是整块的,不能与季度混淆 -block 边缘标记。
    Panel::Config c;
    c.title = " Distinct ";
    Panel::Item r;
    r.leading = "Host";
    r.control = maya::panel::Text{"abc", 1};
    c.items.push_back(std::move(r));
    c.selected = 0;

    const auto out = render(std::move(c));
    CHECK(out.find("\xe2\x96\x88") != std::string::npos);   // 光标 国 家
    CHECK(count_of(out, "\xe2\x96\x8e") == 1);              // 完全一次 一次 一次
}

TEST_CASE("panel: an empty field being edited says so") {
    // 孤独 光标 值栏中与 a 无法区分 渲染 艺术。
    // 一个空的编辑字段保留其占位符,放在 光标.
    Panel::Config c;
    c.title = " Empty edit ";
    Panel::Item t;
    t.leading = "Host";
    t.control = maya::panel::Text{"", 0, "hostname"};
    c.items.push_back(std::move(t));
    Panel::Item s;
    s.leading = "Key";
    s.control = maya::panel::Secret{0, 0};
    c.items.push_back(std::move(s));
    c.selected = 0;

    const auto out = render(std::move(c));
    CHECK(out.find("hostname") != std::string::npos);
    CHECK(out.find("empty")    != std::string::npos);
}

TEST_CASE("panel: dropdown options align with the value column") {
    // 选项列表取代它所属的值, 因此它必须坐在同一列。
    // $ -column 边距( 数组使用 2) 将列表右向调整,
    // 因此打开一个下移似乎可以移动列。
    Panel::Config c;
    c.title = " Align ";
    Panel::Item ch;
    ch.leading = "Backend";
    ch.control = maya::panel::Choice{"Auto"};
    c.items.push_back(std::move(ch));
    c.selected = 0;

    Panel::Menu m;
    m.options = {"Auto", "Ollama"};
    m.current = 0;
    c.menu     = m;
    c.menu_row = 0;

    const auto out = render(std::move(c), 60);

    // 查找闭锁控制器的切夫龙行和选项行的右边缘;
    // 它们必须在同一列结束.
    const auto right_edge = [&](const std::string& needle) -> std::size_t {
        std::size_t start = 0;
        while (start <= out.size()) {
            const std::size_t nl = out.find('\n', start);
            const std::string line = out.substr(start, nl - start);
            if (line.find(needle) != std::string::npos) {
                const auto last = line.find_last_not_of(" \xe2\x94\x82");
                return last;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return std::string::npos;
    };
    const auto opt_edge = right_edge("Ollama");
    CHECK(opt_edge != std::string::npos);
}

TEST_CASE("panel: a frame settles in a single render") {
    // 运行循环 -renders 任何时候 渲染器 员额
    // -layout 回写不同意视图从( ) 中创建
    // 的滚动状态。 detail::scroll_writeback_dirty。
    // 所以,任何数量面板和 渲染器 两者 计算是等待
    // 发生的一个暂时性错误:第一个 帧 复写方式取代了
    // 渲染器 和整个 覆盖面板 重新绘制。
    //
    // `max_y` 这正是。 scrollbar_y
    // 大小的拇指从它,但 渲染器 仅按版式写回,
    // 因此栏从 PREVIOUS 帧 内容高度, 改变高度的
    // 箭头按下( 焦点行的帮助线出现) 需要2 个 渲染
    // 有一些手插在中间明显错的拇指上
    //
    // 小组测量正文,因此在布局运行前就知道了范围.
    // 它出版, 并回信 然后同意而不是仲裁。
    // 这项试验主张协议: 渲染 编号 相同 两次输入必须
    // 使状态在第二次时保持未受影响。
    constexpr int kRows = 60;
    constexpr int kVh   = 8;

    maya::ScrollState s;
    const auto frame = [&](int sel) {
        Panel::Config c;
        c.title      = " Nav ";
        c.viewport_h = kVh;
        c.scroll     = &s;
        c.items       = mixed_rows(kRows);
        c.selected   = sel;
        return render(std::move(c), 72);
    };

    frame(0);   // 建立稳定状态;

    for (int sel = 0; sel < kRows; ++sel) {
        const auto first = frame(sel);
        const int  y = s.y, max_y = s.max_y;

        // Re-rendering 相同的输入必须是 no-op : 相同的像素,
        // 相同的卷轴几何。 如果不是, 用户会看到第一个帧 ,
        // 然后是修正—— 闪存器, 和双倍的输入- > photon。
        maya::detail::scroll_writeback_dirty = false;
        const auto second = frame(sel);

        CHECK(second == first);
        CHECK(s.y == y);
        CHECK(s.max_y == max_y);
        CHECK_FALSE(maya::detail::scroll_writeback_dirty);
    }
}

TEST_CASE("panel: the scrollbar is sized from the CURRENT frame's content") {
    // scrollbar_y 通过读取大小 s.max_y 时间
    // BUILD 时间,但 渲染器 只写 max_y 返回时 之后。
    // 一张离开的面板 max_y 编号 渲染器 因此,
    // 它从任何方面画出它的栏杆, PREVIOUS 帧 留下; 回写后通知,
    // 模糊滚动状态, 运行循环绘制整个 覆盖面板 重新纠正它。
    //
    // 证据必须抽样 BUILD 时间 渲染 读取
    // s.max_y 证明什么也没有,到那时回信已经改正了,
    // 这恰恰是第二个 渲染 用于预防。
    // 所以: build() 检查国家 无需 渲染。
    // 随你便 max_y 此时此刻,是卷轴条的大小。
    for (int rows : {20, 60, 200}) {
        for (int vh : {4, 8, 14}) {
            maya::ScrollState s;          // 冷,就像一个 覆盖面板 打开
            Panel::Config c;
            c.title      = " Cold ";
            c.viewport_h = vh;
            c.scroll     = &s;
            c.selected   = 0;
            for (int i = 0; i < rows; ++i) {
                Panel::Item r;
                r.leading = "row" + std::to_string(i);
                c.items.push_back(std::move(r));
            }

            const maya::Element frame = Panel{std::move(c)}.build();
            (void)frame;
            // 平面行是各一行,所以范围是精确的算术——这里,
            // 在任何布局运行之前,它就已经知道.
            CHECK(s.max_y == rows - vh);
        }
    }

    // A 正文 说 FITS 发布无可滚动范围, 因此栏画一个
    // 完整的 -height 而不是一个误导人的滑稽
    {
        maya::ScrollState s;
        Panel::Config c;
        c.title      = " Fits ";
        c.viewport_h = 10;
        c.scroll     = &s;
        c.selected   = 0;
        for (int i = 0; i < 4; ++i) {
            Panel::Item r;
            r.leading = "row" + std::to_string(i);
            c.items.push_back(std::move(r));
        }
        const maya::Element frame = Panel{std::move(c)}.build();
        (void)frame;
        CHECK(s.max_y == 0);
    }
}

TEST_CASE("panel: independent panels do not interfere across threads") {
    // 面板是一个纯值转换( 包含, 元素输出) , 完全共享一块 可变 状态:
    // BORROWED ScrollState*. maya 滚动簿记(
    // 直播) -state 注册表 和回信时的记述, -dirty 标志
    // )是(这是) thread_local ,所以不同的面板 线程 与
    // 不同州必须完全独立。
    //
    // 值得坚持而不是假设 因为现在的小组 WRITES
    // s.max_y 共享的 注册表 本来会让 竞态
    // 时值 覆盖面板 渲染 将锁在牢里,让它安全
    constexpr int kThreads = 8;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    // render()将COLUMNS设定在每调用左右,Setenv
    // 设定在8线程XX竞态. 每线程使用宽度72,所以在此设定一次.
    setenv("COLUMNS", "72", 1);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            maya::ScrollState s;                  // 百分比 -thread ,未共享
            const int rows = 40 + t * 10;         // 每线程 个不同大小
            for (int sel = 0; sel < rows; ++sel) {
                Panel::Config c;
                c.title      = " T ";
                c.viewport_h = 8;
                c.scroll     = &s;
                c.selected   = sel;
                for (int i = 0; i < rows; ++i) {
                    Panel::Item r;
                    r.leading = "row" + std::to_string(i);
                    c.items.push_back(std::move(r));
                }
                if (maya::render_to_string(Panel{std::move(c)}.build(), 72).empty()) ++failures;
            }
            // 每一个 线程 '状态必须描述 ITS 自身 内容。
            // 共享 注册表 或脏 标志 以邻居的境界
            // 登陆这里
            if (s.max_y != rows - 8) ++failures;
        });
    }
    for (auto& th : threads) th.join();
    unsetenv("COLUMNS");

    CHECK(failures.load() == 0);
}

TEST_CASE("panel: a built Element outlives the Config it came from") {
    // 行数 渲染 改为推迟 ComponentElements
    // (本节) 头文件 如果其中一人在“配置”中找到参考,
    // 则指“组合”中的任何一种。 `this` ——
    // 捕捉在临时小组死亡时纠缠,坠机降落在 PAINT 时间,
    // 远离制造它的建筑,在范围内,在外涂漆。
    maya::Element e = [] {
        Panel::Config c;
        c.title = " Scoped ";
        c.items.push_back(header_row("SECTION"));
        for (int i = 0; i < 12; ++i)
            c.items.push_back(text_row("field", "value"));
        c.selected = 1;
        return Panel{std::move(c)}.build();
    }();

    const auto out = maya::render_to_string(e, 60);
    CHECK(framed(out));
    CHECK(out.find("SECTION") != std::string::npos);
}

TEST_CASE("panel: landing on a header does not move the view") {
    // A 头文件 无法集中焦点——它不带任何动作,所以部件拒绝 渲染
    // 以光标为主。 "no row is focused" 谓 不
    // 与第2款相同,但该款适用于第2款。
    // "the cursor is on line 0" 使他们相聚,
    // -in-view 将卷轴 0 夹在视图中: 将长组列表下移到光标穿过某一
    // 区域时折回顶端 头文件 ,完全失去用户位置.
    //
    // 缺勤或离职也一样 -of-range 选择,
    // 即如何 选择面板 没有匹配 渲染 时,用户仍在滚动。
    constexpr int kRows = 60;
    constexpr int kVh   = 8;

    const auto scrolled_state = [&](maya::ScrollState& s) {
        // 走到下面足够远 风景真的卷了起来
        for (int sel = 0; sel < 40; ++sel) {
            Panel::Config c;
            c.title = " Nav "; c.viewport_h = kVh; c.scroll = &s;
            c.items = mixed_rows(kRows); c.selected = sel;
            (void)render(std::move(c), 72);
        }
    };
    const auto render_at = [&](maya::ScrollState& s, int sel) {
        Panel::Config c;
        c.title = " Nav "; c.viewport_h = kVh; c.scroll = &s;
        c.items = mixed_rows(kRows); c.selected = sel;
        (void)render(std::move(c), 72);
    };

    // mixed_rows 使每11行的一行 头文件.
    {
        maya::ScrollState s;
        scrolled_state(s);
        const int before = s.y;
        REQUIRE(before > 0);
        render_at(s, 44);                     // a header
        CHECK(s.y == before);
    }
    // 完全没有选择权。
    {
        maya::ScrollState s;
        scrolled_state(s);
        const int before = s.y;
        REQUIRE(before > 0);
        render_at(s, -1);
        CHECK(s.y == before);
    }
    // 长名单中留下的选题
    {
        maya::ScrollState s;
        scrolled_state(s);
        const int before = s.y;
        REQUIRE(before > 0);
        render_at(s, 9999);
        CHECK(s.y == before);
    }
}

TEST_CASE("panel: multi-row items scroll in row space") {
    // Config::items 呼叫者( C) -built 要件和可能 MULTI
    // -ROW : 待办事宜 选择面板 推一个 PlanView 覆盖每个任务。
    // 用一个线来测量它们,每个线都放自动 -scroll 夹在 INDEX 空间,
    // 所以最后的四 3 -row 项-从第9行开始,而不是第3行——滚动到0,
    // 并删除所选内容 -screen 完全没有。
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Items ";
    c.viewport_h = 4;
    c.scroll     = &s;
    for (int i = 0; i < 4; ++i) {
        const std::string n = std::to_string(i);
        c.prebuilt.push_back(maya::dsl::v(
            maya::dsl::text("item" + n + "-a"),
            maya::dsl::text("item" + n + "-b"),
            maya::dsl::text("item" + n + "-c")).build());
    }
    c.selected = 3;

    const auto out = render(std::move(c), 50);

    // 四 3 3 4 3 6 6 3 5 6 4 6 5 3 7 6 7 3 10 6 10 3 11 6 11 3 16 6 16 3 15 3 14 3 13 3
    // -row 12行, 可视区域 4,QQ最后一行结束,所以抵消必须为8. Index space would have said 0.
    CHECK(s.y == 8);
    CHECK(out.find("item3-c") != std::string::npos);
}

TEST_CASE("panel: the dropdown reads as one attached block") {
    // 下拉的工作是看起来像下拉 — — 属于上面一行的列表 —
    // 而不是像另外四行恰巧坐落在附近的面板。有两个属性带有它,
    // 而且当每个选项都正确的时候,两者都不存在。
    // -aligned 自行决定:
    //
    //   1. 无线电台共用a COLUMN 独立正确 -aligned 线条设置
    //      并且无线电组的无线电组不排队 无法
    //      作为一个组读出来 无线电组的频率是每组的
    //   2. 区块有BOUNDARY,所以选择开始和停止的地点是
    //      引出而不是推论。
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Menu ";
    c.min_width  = 60;
    c.viewport_h = 12;
    c.scroll     = &s;
    for (int i = 0; i < 3; ++i) {
        Panel::Item r;
        r.leading = "field" + std::to_string(i);
        if (i == 1) r.control = maya::panel::Choice{.label = "beta"};
        c.items.push_back(std::move(r));
    }
    Panel::Menu m;
    // 故意乱划:最短和最长者大不相同,即每架直升机的准确时间。
    // -line 调头分散了无线电
    m.options     = {"a", "beta", "a considerably longer option label"};
    m.highlighted = 1;
    m.current     = 1;
    m.viewport    = 3;
    c.menu     = m;
    c.menu_row = 1;
    c.selected = 1;

    const auto out = render(std::move(c), 72);

    // 每个选项行的无线电格言栏,所有三个必须同意。
    std::vector<std::size_t> radio_cols;
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t nl = out.find('\n', start);
        const std::string line =
            out.substr(start, nl == std::string::npos ? std::string::npos
                                                      : nl - start);
        // QQ( 选中) 或 ○( 未选中)
        std::size_t at = line.find("\xe2\x97\x89");
        if (at == std::string::npos) at = line.find("\xe2\x97\x8b");
        if (at != std::string::npos) {
            // 计数 DISPLAY 栏,不 字节 :前方的切夫龙是多面的
            // -byte ,所以一个 字节 建筑工程的每个项目抵消额将有所不同。
            radio_cols.push_back(
                static_cast<std::size_t>(maya::string_width(line.substr(0, at))));
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }

    REQUIRE(radio_cols.size() == 3);
    CHECK(radio_cols[0] == radio_cols[1]);
    CHECK(radio_cols[1] == radio_cols[2]);

    // 且块上下方各置于括号内.
    CHECK(out.find("\xe2\x95\xad") != std::string::npos);   // ╭ opens it
    CHECK(out.find("\xe2\x95\xb0") != std::string::npos);   // ╰ closes it
    // 最高规则与排行榜的切夫龙有联系.
    CHECK(out.find("\xe2\x94\xb4") != std::string::npos);   // ┴
}

TEST_CASE("panel: a vertical list has no horizontal scroll extent") {
    // 一节 头文件 PAINTS 规则是,装饰可以扩展成空间,
    // 而不是满足要求的空间。
    // ComponentElement::measure
    // 回答 "I need all the width you have" ,
    // 测量时提供的宽度不是 终端 宽度: 滚动 可视区域
    // 调查其子孙与 UNBOUNDED 宽度以发现内容范围,
    // 所以 头文件 索赔的~2^24栏。
    //
    // 两种结果,两者都是真实的。面板报告自己横向可
    // 滚动的1600万列,这就是滚动条和轮子的所在。
    // -routing 假数字来自宽度,所以 CHANGED
    // 显示每次调整大小时—— 将滚动状态弄脏,
    // 并进行循环运行 重绘 整个 覆盖面板 a 第二次为
    // 25度31宽。
    //
    // 面板为垂直列表. max_x为0,每宽,
    // 任意组合行.
    for (int width : {40, 60, 80, 120, 160}) {
        maya::ScrollState s;
        Panel::Config c;
        c.title      = " Wide ";
        c.viewport_h = 10;
        c.scroll     = &s;
        c.items       = mixed_rows(50);      // 包括头文件、帮助、错误
        c.selected   = 1;
        (void)render(std::move(c), width);
        CHECK(s.max_x == 0);
    }
}

TEST_CASE("panel: resizing settles in a single render") {
    // 调整键盘的对等器大小已固定 -point 测试。
    // 终端 宽度不能让滚动状态脏:没有关于面板的
    // CONTENT 高度取决于其宽度( 数列为一行,
    // 帮助和错误为 NoWrap 因此,一个不同意的回文是报告几何学,
    // 视图被错误而不是真正的回流。
    maya::ScrollState s;
    const auto frame = [&](int width) {
        Panel::Config c;
        c.title      = " Resize ";
        c.viewport_h = 10;
        c.scroll     = &s;
        c.items       = mixed_rows(200);
        c.selected   = 100;
        return render(std::move(c), width);
    };

    frame(60);                              // 建立稳定状态;

    int dirty = 0, unstable = 0;
    for (int width = 40; width <= 160; width += 4) {
        maya::detail::scroll_writeback_dirty = false;
        const auto f1 = frame(width);
        const int max_y = s.max_y, y = s.y;
        if (maya::detail::scroll_writeback_dirty) ++dirty;

        const auto f2 = frame(width);       // 相同宽度的两次:固定点
        if (f1 != f2 || s.max_y != max_y || s.y != y) ++unstable;
    }
    CHECK(dirty == 0);
    CHECK(unstable == 0);
}

TEST_CASE("panel: a Secret is never rendered in the clear") {
    // 控制器带有一个 LENGTH ,不是字符串——部件输入中没有简写,
    // 所以没有 渲染 路径可以漏出一个。
    Panel::Config c;
    c.title = " Secret ";
    Panel::Item r;
    r.leading = "API key";
    r.control = maya::panel::Secret{11};
    c.items.push_back(std::move(r));
    c.selected = 0;

    const auto out = render(std::move(c));
    CHECK(out.find("*") != std::string::npos);
    CHECK(count_of(out, "*") == 11);
}

// ============================================================================
//  虚拟化
// ============================================================================
//
// 小组 MEASURES 编号 正文 (计算行数,无建置) 决定哪个
// 行 可视区域 交叉,然后 渲染 只有那些... ... . .
// 线程 列表为 14 后面的 数千行 -row 窗口,并绘制其他4986
// 个成本~67ms a 帧.
//
// 将一线知识分成两段:一线多高,一线多长。如果它们按线漂移,
// 跳过的人 -row 空间器不再符合它们所代表的行,
// 一个字段静静地滚动了它自己的帮助文本。这些测试将它们
// 固定在一起。 THROUGH 该 RENDERER ,
// 这是唯一一个看到两者的裁判: max_y 由实际的
// 布局计算 -painted 子,而卷轴夹则使用测量的总数.
// 如果两者意见不一致,max_y不同意下面的算术.

namespace {

// 文档规则, 独立于部件重排 : 一行是一行,
// 加上 FOCUSED 行的帮助, 加上一个错误。
// A 头文件 总是一个
int expected_lines(const Panel::Item& r, bool focused) {
    if (r.is_header()) return 1;
    return 1 + (focused && !r.help.empty() ? 1 : 0) + (!r.error.empty() ? 1 : 0);
}

} // 命名空间结束。

TEST_CASE("panel: the measured body is exactly as tall as the painted body") {
    // 将光标翻过混合列表 -height 横行。
    // 每站内容范围 LAYOUT 涂鸦儿童计算数必须等于
    // MEASURE 预言的通过——平等是实现平等的全部前提。
    // 渲染 只有一个窗口。
    constexpr int kRows = 60;
    constexpr int kVh   = 8;

    for (int sel = 0; sel < kRows; ++sel) {
        maya::ScrollState s;
        Panel::Config c;
        c.title      = " Mixed ";
        c.viewport_h = kVh;
        c.scroll     = &s;
        c.items       = mixed_rows(kRows);
        c.selected   = sel;

        int total = 0;
        for (int i = 0; i < kRows; ++i)
            total += expected_lines(c.items[static_cast<std::size_t>(i)],
                                    i == sel);

        (void)render(std::move(c));
        // 内容 = 可视区域 + max_y ,由 渲染器 自己定义的
        CHECK(s.max_y == total - kVh);
    }
}

TEST_CASE("panel: an open menu is measured at the height it draws") {
    // 菜单是单行所能贡献的高度最高的东西,
    // 其高度是有条件的三种方式(一个焦点选项的提示,
    // 一个位置计数器,只在窗口打开时,一个空-list占位符).
    //
    // 断言是VISIBILITY,
    // 不是程度:滚动将焦点行的整个跨度保持在屏幕上,
    // 而跨度是计算量。如果计量比单行数少,
    // 滚动会短短一行,选项列表的底部 — —
    // 显示更多,或者光标下解释选项的提示 —
    // — 仅坐落在底边。 测量所绘的跨度
    // 不会证明什么:它是从所画的中计算出来的,
    // 因此,与绘图相提并论的度量会取消。
    const auto frame = [](int opts, int viewport, int hi, bool with_hints,
                          int vh) -> std::string {
        maya::ScrollState s;
        Panel::Config c;
        c.title      = " Menu ";
        c.viewport_h = vh;
        c.scroll     = &s;
        for (int i = 0; i < 12; ++i) {
            Panel::Item r;
            r.leading = "field" + std::to_string(i);
            if (i == 3) r.control = maya::panel::Choice{.label = "b"};
            c.items.push_back(std::move(r));
        }
        Panel::Menu m;
        for (int i = 0; i < opts; ++i)
            m.options.push_back("option" + std::to_string(i));
        if (with_hints)
            for (int i = 0; i < opts; ++i)
                m.hints.push_back("about" + std::to_string(i));
        m.highlighted = hi;
        m.current     = 0;
        m.viewport    = viewport;
        c.menu     = m;
        c.menu_row = 3;
        c.selected = 3;
        return render(std::move(c), 60);
    };

    // 带有提示的窗口列表: 行+ 块的两条规则+
    // 3 选项+ 提示+ "2/5". 每一条线必须
    // 同时在屏幕上,这就是测量通过必须预言的卷轴
    // 才能保持组团.
    {
        const auto out = frame(5, 3, 1, true, 8);
        CHECK(out.find("field3")  != std::string::npos);
        CHECK(out.find("option0") != std::string::npos);
        CHECK(out.find("option2") != std::string::npos);
        CHECK(out.find("about1")  != std::string::npos);   // 提示
        CHECK(out.find("2/5")     != std::string::npos);   // 计数器
    }
    // 无提示:然后是计数器是组的最后一行.
    {
        const auto out = frame(5, 3, 1, false, 7);
        CHECK(out.find("option2") != std::string::npos);
        CHECK(out.find("2/5")     != std::string::npos);
        CHECK(out.find("about1")  == std::string::npos);
    }
    // 适配:完全没有计数器,最后选项是最后一行.
    {
        const auto out = frame(3, 5, 1, false, 6);
        CHECK(out.find("option2") != std::string::npos);
        CHECK(out.find("3/3")     == std::string::npos);
    }
    // 空选项集:占位符仍然得到其行.
    {
        const auto out = frame(0, 5, 0, false, 2);
        CHECK(out.find("(no options)") != std::string::npos);
    }
}

TEST_CASE("panel: a row is rendered whenever any part of it is on screen") {
    // 窗口选择从 LINE 以缩放方式,所以必须先建起
    // 横跨任意边的行 — 滚动轴抵消然后剪贴。
    // -visible 线条是虚拟化如何在上或下方显示
    // 空白条条 可视区域.
    constexpr int kRows = 40;
    constexpr int kVh   = 6;

    maya::ScrollState s;
    for (int sel = 0; sel < kRows; ++sel) {
        Panel::Config c;
        c.title      = " Window ";
        c.viewport_h = kVh;
        c.scroll     = &s;          // 共享: 这是真正的 key-nav 步
        c.items       = mixed_rows(kRows);
        c.selected   = sel;

        const bool is_header = c.items[static_cast<std::size_t>(sel)].is_header();
        const std::string label = "row" + std::to_string(sel);
        const std::string help  = c.items[static_cast<std::size_t>(sel)].help;

        const auto out = render(std::move(c));

        CHECK(framed(out));
        // 你上的那排总是在屏幕上...
        if (!is_header) CHECK(out.find(label) != std::string::npos);
        // . . 它拥有的帮助也是一样, 因为滚动可以让整个
        // 行的长度都保持视线。 -line 测量错误的断开。
        if (!is_header && !help.empty())
            CHECK(out.find(help) != std::string::npos);
    }
}

TEST_CASE("panel: a huge list renders only its viewport") {
    // 锻炼的点. 5000行,14可见:帧
    // 必须包含光标周围的行,其余的则不包含.
    maya::ScrollState s;
    Panel::Config c;
    c.title      = " Huge ";
    c.viewport_h = 14;
    c.scroll     = &s;
    c.items.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        Panel::Item r;
        r.leading = "entry-" + std::to_string(i);
        c.items.push_back(std::move(r));
    }
    c.selected = 2500;

    const auto out = render(std::move(c), 60);

    CHECK(framed(out));
    CHECK(out.find("entry-2500") != std::string::npos);
    // 名单的两端都离窗户很近
    CHECK(out.find("entry-0\n") == std::string::npos);
    CHECK(out.find("entry-4999") == std::string::npos);
    // 滚动栏仍然知道列表的真实大小.
    CHECK(s.max_y == 5000 - 14);
}

