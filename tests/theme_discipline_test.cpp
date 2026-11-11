// theme_discipline_test — agentty 拼写其自己的颜色。
//
// 该 RULE
// ========
// agentty 没有 UI 层中。每个颜色决定都存在于 maya ,通过主题达到:
// agentty 名称一个语义 TOKEN (ui::accent,
// ui::status_ok 或 或 a 或 类型 ThemeSlot ,和 maya 确定
// 油漆时间的形状
//
// 任何一处的文字 agentty 以某种方式打破了它 其它没有捕获。
// 它只被评估一次,没有主题的范围,所以它默默地
// 将一个元素钉在它编译的调色板上——它保持了
// 渲染 继续寻找好开发者 终端 ,
// 并且只有在用户选择一个方案而部分屏幕忽略时才会出现.
// 这只虫子的每颗都长成这样
//
// 所以每个建筑都用机械方式 对照源头进行检查
#include "agtest.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// 允许拼写颜色的ONE文件:语义调色板本身.
//
// palette.hpp 在哪里是 7 个 agentty 符号被定义,
// 每个都是一个主题 -slot 改为( AGENTTY_THEME_SLOT )
// 而不是字面——所以它可以免于扫描,但不能免于规则.
const std::vector<std::string> kAllowed = {
    "include/agentty/runtime/view/palette.hpp",
};

// 允许写RAWXXSGR逃逸的文档.
//
// 代码 -block 运行时释放颜色 maya 谓 SUSPENDED
// 煮的Tty,(笑声) TUI 拆掉,没有画布或样式 池 ——
// 所以它真的不能通过主题。 maya 是否释放颜色的分级检测,
// 用户可以观察到的部分( ) NO_COLOR / TERM ⁇ ,
// ⁇ , ⁇ ).
const std::vector<std::string> kAllowedRawSgr = {
    "src/runtime/app/cmd_code_block_run.cpp",
};

[[nodiscard]] bool listed(const std::vector<std::string>& list,
                          const std::string& rel) {
    for (const auto& a : list)
        if (rel == a) return true;
    return false;
}

[[nodiscard]] bool allowed(const std::string& rel) {
    return listed(kAllowed, rel);
}

// ── 过滤器 ───────────────────────────────────────────────────────
//
// libstdc++ 的双倍 std::regex 是一个
// 没有字面前缀优化的解译后跟踪器,所以它走每行的每个位置,
// 即使线条不可能匹配。在117k行x3位图案上,它花费7.4美元。
// SECONDS 在调试中构建实际运行的测试——使这个grep
// 成为第三个 -slowest 测试在套房,
// 比 渲染 测量:7515ms - > 24ms,一个310x加速,
// 先问一个便宜的底弦问题。
//
// CORRECTNESS RULE: 每个预过滤器必须严格地是
// WEAKER 而不是它的 regex —— 它可能通过 regex
// 拒绝的线, 而不是反向。 过滤出一个真实命中的预过滤器是一个静音孔,
// 这是这个文件存在的一个防止的失败。 因此, 在下方每个测试一个字面的
// 子字符串, 由 rgex PROVABLY 要求:
//
//   点亮需求 "Color::" 或 或 时 间 "AnsiColor::" 或 或 时 间
//               "fg_"/"bg_" — 每个 其八个备选方案之一以其中之一为起点。
//               (注: AnsiColor * 和 fg_/bg_ 预
//               设:此过滤器的较早草稿仅测试于 * hex/ * rgb
//               并且会悄悄地把大部分规则切换掉,必须对照它前面的图案检查前过滤器,
//               代之以其他方法——这就是这一技术的整个风险。
//               ) raw_sgr 需求 "\x1b[" / "\033["
//   / "\e[" (其字面前缀) channel_read 需求
//   ".r()" / ".g()" / ".b()" (该词 IS 即该字节集).
//
// find() 是个小姑娘 -backed 并在第一次命中时停止,
// 所以常见的病例(无匹配)需要一线扫描而不是每个位置扫描一次.
[[nodiscard]] inline bool may_be_literal(const std::string& l) noexcept {
    return l.find("Color::") != std::string::npos   // 涵盖 AnsiColor: 也:
        || l.find("fg_")     != std::string::npos
        || l.find("bg_")     != std::string::npos;
}

[[nodiscard]] inline bool may_be_raw_sgr(const std::string& l) noexcept {
    return l.find("\\x1b[") != std::string::npos
        || l.find("\\033[") != std::string::npos
        || l.find("\\e[")   != std::string::npos;
}

[[nodiscard]] inline bool may_read_channel(const std::string& l) noexcept {
    return l.find(".r()") != std::string::npos
        || l.find(".g()") != std::string::npos
        || l.find(".b()") != std::string::npos;
}

// ── 这个频道有看管吗? ───────────────────────────────────────
//
// `has_channels()`确立了一个事实,
// 即第RESTXXOFTHEENCLOSINGBLOCK,
// 不是一条线。 第-scoped线检查强迫守军标记在算术线本身上,
// 这是不可读的(第`... + bg.b())  // has_channels`),
// 教错上游如何工作,在有人写自然事物的时刻悄悄地
// 不打开:
//
//     if (!bg.has_channels()) return std::nullopt;
//     return 0.2126 * bg.r() + ...;        // flagged, though it is correct
//
// 反之,轨道的支撑深度。在深度d看到的警卫覆盖
// 了每一行,直到深度下降回到d以下,这正是编译器
// 同意事实仍然存在的范围。
//
// 这是一个热度,而不是解析器:字符串字元内的套接字或评论
// 会扭曲深度。这是故意的BIASEDXXTOWARDFALSEPOSITIVES
// (一个Skew使警卫提前到期,标志成为安全线),
// 因为这次测试的失败模式是一种烦恼,而缺失一个真实
// 命中的失败模式是再次的#45航运。
class GuardScope {
public:
    // 按顺序为文件的每行提供饲料,BEFORE测试.
    void observe(const std::string& line) {
        // 警卫是 CODE。纪念在A. COMMENT 并非细节:
        // 接近此危险的每一函数都会有一个注释来解释它,
        // 因此读注释就可以在最需要它的文件中解开扫描器。
        // maya's anim::lerp 坐在一个评论标题下
        // "Why this checks has_channels()"
        // 并挥舞通过, 并删除了它的警戒。
        const auto first = line.find_first_not_of(" \t");
        const bool comment =
            first != std::string::npos
            && (line.compare(first, 2, "//") == 0
                || line.compare(first, 2, "/*") == 0
                || line.compare(first, 1, "*") == 0);

        // has_channels ()是名字,但已经证明
        // Kind::Rgb 远处所陈述的同样事实,以及 SGR
        // 排放路径使用它,因为不管怎样,它们必须切换到这种。
        // r()/g()/b() 痷 ARE 频道。
        const bool establishes =
            line.find("has_channels") != std::string::npos
            || line.find("ColorKind::Rgb") != std::string::npos
            || line.find("Kind::Rgb") != std::string::npos;

        if (!comment && establishes) guarded_depth_ = depth_;
        if (comment) return;   // 传言中的支架也不在范围之内

        for (char c : line) {
            if (c == '{') ++depth_;
            else if (c == '}') {
                --depth_;
                // 警卫挡板关闭:事实不再坚持.
                if (guarded_depth_ >= 0 && depth_ < guarded_depth_)
                    guarded_depth_ = -1;
            }
        }
    }

    [[nodiscard]] bool guarded() const noexcept { return guarded_depth_ >= 0; }

    void reset() noexcept { depth_ = 0; guarded_depth_ = -1; }

private:
    int depth_ = 0;
    int guarded_depth_ = -1;
};

}  // 命名空间结束。

TEST_CASE("theme discipline: the guard tracker is block-scoped") {
    // 扫描仪本身就是密码,一个默默停止守卫的警卫比没有更糟糕
    // ——它永远读作绿色。 -scoped 版本完全失败 :
    // 它只接受一个标记 ON 算术线,所以每一个正确
    // -written 卫队是虚伪的,是明显的固定的。
    // "append // has_channels" ) 教错
    // 上游如何工作的模型。 将范围规则固定, 使其不能腐烂。
    auto scan = [](std::initializer_list<const char*> lines) {
        GuardScope s;
        std::vector<bool> guarded;
        for (const char* l : lines) {
            const std::string line{l};
            s.observe(line);
            guarded.push_back(s.guarded());
        }
        return guarded;
    };

    // 将丑陋的内线标记塞进 ui_theme.hpp : 警卫必须掩护
    // REST OF ITS BLOCK 不只是它自己的线。
    {
        const auto g = scan({
            "bool f(LitColor c) {",
            "    if (!c.has_channels()) return false;",
            "    return c.r() + c.g() > 10;",
            "}",
        });
        CHECK(!g[0], "no guard before has_channels is seen");
        CHECK(g[1],  "the guard line itself counts");
        CHECK(g[2],  "the guard must survive to the next line");
        CHECK(!g[3], "the guard expires when its block closes");
    }

    // 窝内警卫不会泄露给兄弟姐妹
    {
        const auto g = scan({
            "void f() {",
            "    if (x) {",
            "        if (c.has_channels()) {}",
            "    }",
            "    return c.r() * 2;",
            "}",
        });
        CHECK(g[2],  "guarded inside the nested block");
        CHECK(!g[4], "a sibling statement is NOT covered by it");
    }

    // 故障-open案例:一个职能的守卫不能覆盖下一个.
    {
        const auto g = scan({
            "int a(LitColor c) {",
            "    if (!c.has_channels()) return 0;",
            "    return c.r() / 2;",
            "}",
            "int b(LitColor c) {",
            "    return c.r() / 2;",
            "}",
        });
        CHECK(g[2],  "the first function is guarded");
        CHECK(!g[5], "the next function must NOT inherit the guard");
    }

    // 一个警卫在... COMMENT 这个人不是警卫 加载-bearing
    // : 接近这个危险值的每个函数都记录它, 因此遵守注释就可以在
    // 需要的文件中将扫描仪解开。 maya's anim::lerp 坐在下面,
    // 我看见了 "// Why this checks has_channels()"
    // 被警察自己删除了
    {
        const auto g = scan({
            "// Why this checks has_channels() ...",
            "LitColor lerp(LitColor a, LitColor b, double t) {",
            "    return LitColor::rgb(mix(a.r(), b.r()));",
            "}",
        });
        CHECK(!g[0], "a comment mentioning has_channels establishes nothing");
        CHECK(!g[2], "so the body is UNGUARDED and must be flagged");
    }

    // 一个证明 Kind::Rgb 分支是同样的事实,
    // 它所描述的漫长的道路是: SGR 排放路径在这种路径上切换,
    // 并且在这个分支中, 字节 真正的渠道。
    {
        const auto g = scan({
            "switch (c.kind()) {",
            "    case ColorKind::Rgb:",
            "        return write(p, c.r(), c.g(), c.b());",
            "}",
        });
        CHECK(g[1], "case ColorKind::Rgb establishes channels");
        CHECK(g[2], "and covers the emission inside it");
    }
}

TEST_CASE("theme discipline: prefilters never reject a real hit") {
    // 预过滤器是一种可以默默地进行优化的 DISABLE 规则:如果
    // 它拒绝一条线, 将匹配的正则线, 支票会悄悄地停止检查,
    // 套房将永远绿化。 起草这些我得到的 `lit` 第一次尝试时
    // 有错误—— "::hex("/"::rgb" 并错过
    // Color::black(), AnsiColor * 和
    // fg_/bg_ 预设,是图案的八个替代品中的五个.
    //
    // 因此,直接断言其含义,即选择的字符串:regex
    // 匹配QQ预过滤器通过。反转是好的,也是预期的(预
    // 过滤器可能超过-admit;只有成本时间 )。
    const std::regex lit{
        R"(Color::(black|red|green|yellow|blue|magenta|cyan|white|bright_\w+)\(\))"
        R"(|Color::hex\(0x)"
        R"(|Color::rgb\(\s*[0-9])"
        R"(|Color::rgb\(\s*0x)"
        R"(|Color::hsl\(\s*[0-9])"
        R"(|Color::indexed\(\s*[0-9])"
        R"(|AnsiColor::)"
        R"(|\b(fg|bg)_(black|red|green|yellow|blue|magenta|cyan|white)\b)"};
    const std::regex raw_sgr{R"((\\x1b|\\033|\\e)\[[0-9;]*m)"};
    const std::regex channel_read{R"(\.[rgb]\(\))"};

    // 每一个替代品都有一个探测器,加上近距离的探测器 -misses 一定要这样 不 相匹配,对吗?
    const std::vector<std::string> corpus = {
        "auto c = Color::black();",
        "auto c = Color::bright_magenta();",
        "auto c = Color::hex(0x282A36);",
        "auto c = Color::rgb(12, 34, 56);",
        "auto c = Color::rgb(0xFF, 0, 0);",
        "auto c = Color::hsl(210, 0.5f, 0.4f);",
        "auto c = Color::indexed(42);",
        "AnsiColor::BrightBlack",
        "Style{}.fg_red()",
        "auto s = bg_blue;",
        R"(out += "\x1b[31m";)",
        R"(out += "\033[0m";)",
        R"(out += "\e[1;32m";)",
        "return c.r() + c.g();",
        "put_u8(o, c.b());",
        "return LitColor::rgb(mix(a.r(), b.r()));",
        // 近-misses: 任何一层都没有匹配的预期值。
        "auto c = Color::slot(ThemeSlot::Accent);",
        "int red = 5;",
        "// just a comment about rgb",
    };

    for (const auto& line : corpus) {
        if (std::regex_search(line, lit)) {
            const std::string m =
                "may_be_literal rejected a line `lit` matches: " + line;
            CHECK(may_be_literal(line), m);
        }
        if (std::regex_search(line, raw_sgr)) {
            const std::string m =
                "may_be_raw_sgr rejected a line `raw_sgr` matches: " + line;
            CHECK(may_be_raw_sgr(line), m);
        }
        if (std::regex_search(line, channel_read)) {
            const std::string m =
                "may_read_channel rejected a channel read: " + line;
            CHECK(may_read_channel(line), m);
        }
    }
}

TEST_CASE("theme discipline: agentty names tokens, never colours") {
    const fs::path root{AGENTTY_SRC_ROOT};
    REQUIRE(fs::exists(root));

    // 赤色字色(变种). Color::slot 即点,且算出;
    // Color::rgb (expr)是算术的——两者都很好,
    // 硬码通道值不是.
    const std::regex lit{
        R"(Color::(black|red|green|yellow|blue|magenta|cyan|white|bright_\w+)\(\))"
        R"(|Color::hex\(0x)"
        R"(|Color::rgb\(\s*[0-9])"
        R"(|Color::rgb\(\s*0x)"
        R"(|Color::hsl\(\s*[0-9])"
        R"(|Color::indexed\(\s*[0-9])"
        R"(|AnsiColor::)"
        // maya的字面样式预设是另一个名字的同一种注.
        R"(|\b(fg|bg)_(black|red|green|yellow|blue|magenta|cyan|white)\b)"};

    // 一只手,你来帮忙 -written SGR 全部逃逸的颜色,
    // 所以上面的正则无法看到它。 这是最关键的逃避: 它对每
    // 一种类型都看不见。 -level 警卫 和 ,
    // 它是如何爬回到 颜色,一旦明显的路径关闭。
    const std::regex raw_sgr{R"((\\x1b|\\033|\\e)\[[0-9;]*m)"};

    // 彩色频道字节上的算术.
    //
    // 解析槽后产生 a LitColor ,这是 PAINTABLE
    // 但不一定要这样 NUMERIC 仅 Kind::Rgb 带有频道。
    // 命名和索引保存一个 PALETTE INDEX 数字 字节 与
    // g/b 零,默认完全无任何——和 theme::native
    // 将每个槽都刻意指定为命名或默认,因此用户自己的调色板到达屏幕。
    //
    // 乘以或异于这些 字节 因此,将调色板索引读作彩色通道。
    // 这就是第45期: bright_black (名称8)
    // 混入rgb(8,0,0),并涂在每一个推理块的
    // 每行上——暗处隐形. 终端 ,并且对任何开发者来说都是隐形的,
    // 因为他们都运行着一个计划而不是本地的.
    //
    // 任意 频道读取——不只是一个与操作员相邻的频道.
    //
    // 第一个版本需要有一个[- ⁇ /]在读数旁边。 感觉很精确,
    // 毫无用处: 原始的# 45 bug 将频道传递到一个 FUNCTION
    // — `LitColor::rgb(mix(a.r(), b.r()), ...)`
    // ——所以没有接线员在眼前,扫描仪走过了它写下的线条.
    // for. Flag every read; the two legitimate shapes (index() for palette
    // 排放,一个has_channels/Kind:Rgb分支用于真实的
    // 算术)已经明确,并说成如此.
    const std::regex channel_read{R"(\.[rgb]\(\))"};

    std::vector<std::string> offenders;
    int scanned = 0;

    for (const fs::path& sub : {fs::path{"src"}, fs::path{"include"}}) {
        const fs::path dir = root / sub;
        if (!fs::exists(dir)) continue;
        for (const auto& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            const auto ext = e.path().extension();
            if (ext != ".cpp" && ext != ".hpp") continue;

            const std::string rel =
                fs::relative(e.path(), root).generic_string();
            // NOTE : kallowed 免除文件 COLOUR
            // LITERAL 规则只允许。它不能跳过整个文件——
            // 一个规则默默地给予另一个规则一个例外是警卫如何腐烂。
            // 以下频道检查在各地都适用,无例外:
            // palette.hpp读了主题槽,
            // 所以它手头有颜色,正是混合可以出现的地方.
            const bool lit_exempt = allowed(rel);
            ++scanned;

            std::ifstream in{e.path()};
            std::string line;
            int n = 0;
            GuardScope scope;
            while (std::getline(in, line)) {
                ++n;
                scope.observe(line);
                const auto first = line.find_first_not_of(" \t");
                if (first != std::string::npos
                    && (line.compare(first, 2, "//") == 0
                        || line.compare(first, 1, "*") == 0)) continue;
                if (!lit_exempt && may_be_literal(line)
                    && std::regex_search(line, lit))
                    offenders.push_back(rel + ":" + std::to_string(n)
                                        + "  " + line);
                if (!listed(kAllowedRawSgr, rel) && may_be_raw_sgr(line)
                    && std::regex_search(line, raw_sgr))
                    offenders.push_back(rel + ":" + std::to_string(n)
                                        + "  (raw SGR)  " + line);
                if (may_read_channel(line) && !scope.guarded()
                    && std::regex_search(line, channel_read))
                    offenders.push_back(rel + ":" + std::to_string(n)
                                        + "  (channel read on a colour that "
                                          "may have none)  " + line);
            }
        }
    }

    // 守卫:走一步,找不到任何扫描,
    // 会完全因为错误的理由而通过.
    CHECK(scanned > 100);

    if (!offenders.empty()) {
        std::string msg = std::to_string(offenders.size())
            + " colour literal(s) in agentty. Colour belongs in maya:\n";
        for (std::size_t i = 0; i < offenders.size() && i < 25; ++i)
            msg += "   " + offenders[i] + "\n";
        msg += "\nUse a ui:: token (palette.hpp) or Color::slot(ThemeSlot::X)\n"
               "so the colour follows the user's theme.";
        FAIL_CHECK(msg);
    }
    CHECK(offenders.empty());
}
