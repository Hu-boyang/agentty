// TERM 编辑 终端 自己的颜色——第37期.
//
// 报告是 "horrible look and feel in non-black terminals, almost unusable" ,
// 从带有灯光的用户中 -grey 加上三个安静的抱怨: TERM
// 不受尊重,没有黑人 -and-white 选项,
// 以及 agentty 假设一个小精灵 -terminal-ish
// 模拟器。
//
// 颜色半部分由 theme::native 成为 DEFAULT
// ——它表示完全没有文字颜色,所以新装的涂料带有: SGR
// 39/49 (无) 终端 墨水和背景)和16个 NAMED ansi 插槽,
// 是调色板中的索引 USER 配置。 无 maya 选了
//
// 电话 TERM 一半需要真正的修复: `TERM=dumb` 被颜色等级所
// 尊崇 而在别的地方,所以 agentty 单色的,然后包起来 帧 输入 DEC
// 私人模式2026 反正。 "Unknown private modes are no-ops"
// 真实的,一个 终端 带 DEC 解析器 ; 一个哑巴 终端 没有,
// 所以 字节 土地是垃圾

#include "agtest.hpp"

#include "agentty/domain/ui_theme.hpp"

#include <maya/style/schemes.hpp>
#include <maya/style/schemes.hpp>
#include <maya/style/theme.hpp>
#include <maya/terminal/ansi.hpp>

#include <cstdlib>
#include <print>
#include <string>

namespace {

// 范围为setenv/unsetenv,因此一个案件不能漏入下一个案件.
struct ScopedEnv {
    std::string key;
    bool had_old = false;
    std::string old;
    ScopedEnv(const char* k, const char* v) : key(k) {
        if (const char* prev = std::getenv(k)) { had_old = true; old = prev; }
        if (v) ::setenv(k, v, 1); else ::unsetenv(k);
    }
    ~ScopedEnv() {
        if (had_old) ::setenv(key.c_str(), old.c_str(), 1);
        else         ::unsetenv(key.c_str());
    }
};

}  // 命名空间结束。

TEST_CASE("issue 37: the default theme states no colours of its own") {
    // 核心,每个位置都必须决心 终端 默认值( N) SGR
    // 39/49 或 或 a 或 类型 NAMED ansi指数
    // ——从不字面 RGB 作者是别人的背景。
    //
    // 单硬码的胡伊是整个bug: 选择#565F89的
    // 哑音文本, 它在黑色上读,在浅灰色上消失,
    // 这恰恰是这个问题上的截图。
    int rgb_slots = 0;
    int checked = 0;
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(maya::ThemeSlot::Overlay); ++i) {
        const auto slot = static_cast<maya::ThemeSlot>(i);
        const maya::LitColor c =
            maya::theme::native.resolve(maya::Color::slot(slot));
        // 命名和默认均为 "the user's own palette".
        // Rgb/Indexed 这将是 maya 强制颜色。
        if (c.kind() == maya::ColorKind::Rgb || c.kind() == maya::ColorKind::Indexed)
            ++rgb_slots;
        ++checked;
    }
    CHECK(checked == static_cast<int>(maya::kThemeSlotCount));
    CHECK(rgb_slots == 0);
}

TEST_CASE("issue 37: native paints no background, so a light terminal stays light") {
    // owns_canvas 是一个主机用于决定是否填充
    // 可视区域。当地人必须回答 NO 填充它就是灯光
    // 终端 变成一个暗的矩形,上面有无法读取的文字。
    CHECK(!maya::theme::owns_canvas(maya::theme::native));
    CHECK(maya::theme::native.background.kind() == maya::ColorKind::Default);

    // 允许用户 PICKED 拥有画布的图案: 他们要求它,
    // 它会说明一个匹配的前景。 区别是同意。
    const maya::Theme* dracula = nullptr;
    for (const auto& s : maya::theme::schemes)
        if (std::string_view{s.name} == "Dracula") dracula = s.theme;
    REQUIRE(dracula != nullptr);
    CHECK(maya::theme::owns_canvas(*dracula));
}

TEST_CASE("issue 37: TERM=dumb means no escape sequences, not just no colour") {
    // 就是这样 "doesn't respect TERM" 颜色等级
    // 被尊崇的哑巴,其他的都一样。
    {
        ScopedEnv term{"TERM", "dumb"};
        CHECK(maya::theme::terminal_is_dumb());
        CHECK(maya::theme::detect_tier(/*tty=*/true) == maya::theme::ColorTier::Mono);
        // 哑巴 终端 无法支持 DEC 私人模式,
        // 不管其他环境说法如何。
        CHECK(!maya::ansi::env_supports_synchronized_output());
    }
    // ..还有一个 过期 部队 标志 不得为明确要求纯
    // 文本的用户复活逃避输出。
    {
        ScopedEnv term{"TERM", "dumb"};
        ScopedEnv force{"MAYA_FORCE_SYNC", "1"};
        CHECK(!maya::ansi::env_supports_synchronized_output());
    }
    // 正常的终端不受任何这种情况的影响。
    {
        ScopedEnv term{"TERM", "xterm-256color"};
        CHECK(!maya::theme::terminal_is_dumb());
    }
}

TEST_CASE("issue 37: a black-and-white terminal is a supported request") {
    // 如果需要黑白终端怎么办？
    // ——问法有三种,所有的方法都必须到达莫诺,因为答案不能
    // 取决于用户知道哪一个拼写.
    {
        ScopedEnv term{"TERM", "dumb"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Mono);
    }
    {
        ScopedEnv term{"TERM", "xterm-256color"};
        ScopedEnv no_color{"NO_COLOR", "1"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Mono);
    }
    {
        ScopedEnv term{"TERM", "xterm-256color"};
        ScopedEnv forced{"MAYA_COLOR", "none"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Mono);
    }
    // 管道到文件是第四个:重定向日志应该是文字,
    // 而不是逃生汤.
    {
        ScopedEnv term{"TERM", "xterm-256color"};
        ScopedEnv no_color{"NO_COLOR", nullptr};
        ScopedEnv forced{"MAYA_COLOR", nullptr};
        // 自我的爱 -identified 东道主( 5) Windows
        // 终端 套装 WT_SESSION 故意超过手柄探测器的分数
        // 这件案子是直接重定向的 所以把宿主的名字从画面中取出来
        ScopedEnv wt{"WT_SESSION", nullptr};
        ScopedEnv conemu{"ConEmuANSI", nullptr};
        ScopedEnv prog{"TERM_PROGRAM", nullptr};
        ScopedEnv clif{"CLICOLOR_FORCE", nullptr};
        CHECK(maya::theme::detect_tier(/*tty=*/false) == maya::theme::ColorTier::Mono);
    }
}

TEST_CASE("issue 37: capability is inferred conservatively, never assumed richest") {
    // rxvt、不同版本的 xterm、GNOME 终端和 Konsole 都存在细节差异。
    // maya不能参考tercap,所以问题在于
    // 其推断是否为每个家庭的DEFENSIBLE,
    // 而不是是否一致的悲观.
    ScopedEnv ct{"COLORTERM", nullptr};
    ScopedEnv no_color{"NO_COLOR", nullptr};
    ScopedEnv forced{"MAYA_COLOR", nullptr};
    ScopedEnv prog{"TERM_PROGRAM", nullptr};
    ScopedEnv wt{"WT_SESSION", nullptr};

    // 赤裸裸的 VT-era TERM 得到十六种颜色 终端 从那时起,我们一直以来
    // VT100 其后代——和 无内容 更进一步。记者明确点名的情况就是这样(第1段)。
    // "setting TERM=vt100 usually solves problems" 因此,
    // 它不能升级。
    {
        ScopedEnv term{"TERM", "vt100"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Ansi16);
    }
    // 电话 xterm/screen/tmux/rxvt/linux
    // 家人会得到 INDEXED 颜色,而非正色。已索引 SGR 在那里,
    // 普遍安全,将16人计算成灰人,他们就会崩溃,变成初选,但38人;
    // 2 将是一个家庭不支持的说法。
    for (const char* t : {"rxvt", "xterm", "linux", "screen"}) {
        ScopedEnv term{"TERM", t};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Ansi256);
    }
    // A 终端 说 IS 相信真彩色的名声 因为这样
    // 的能力才能在 COLORTERM 才不会呢
    {
        ScopedEnv term{"TERM", "konsole"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::TrueColor);
    }
    // 明确的能力主张总是胜诉——主张胜过猜测.
    {
        ScopedEnv term{"TERM", "xterm-256color"};
        ScopedEnv claim{"COLORTERM", "truecolor"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::TrueColor);
    }
    // 不论等级, USER '超能力是最重要的, 全部。
    {
        ScopedEnv term{"TERM", "konsole"};
        ScopedEnv override_{"MAYA_COLOR", "16"};
        CHECK(maya::theme::detect_tier(true) == maya::theme::ColorTier::Ansi16);
    }
}

TEST_CASE("issue 37: a low colour tier falls back to native, not a bad approximation") {
    // 一个命名的图案状态为 RGB , 包括背景。
    // 量化为 16 种颜色, 成为泥土 —— 和背景泥土正是
    // 无法读取的截图。 低于 256 种颜色的 诚实答案是
    // 用户自己的调色板。
    agentty::ui_prefs::Prefs p;
    p.theme = "Dracula";
    p.tier  = agentty::ui_prefs::ColorTier::Ansi16;
    const auto r = agentty::ui_prefs::resolve(p, agentty::ui_prefs::detect(true));
    CHECK(r.theme == &maya::theme::native);
}

TEST_CASE("theming: every scheme in the picker is findable by name") {
    // find_scheme() 二进制程序-searches 方案[],
    // 只有在表格排序时才正确。如果不是,则搜索 MISSES 一种
    // 存在的办法——症状不是崩溃,
    // 而是 "unknown scheme — using native" 对于用户可以在前面看到的主题。
    //
    // schemes.hpp static_asserts 命令,
    // 所以这是行为的一半: 选择面板 可以显示必须环绕 -trip 通过
    // 搜索 选择面板 使用时。
    int checked = 0;
    for (const auto& s : maya::theme::schemes) {
        const maya::Theme* found = agentty::ui_prefs::find_scheme(s.name);
        if (found != s.theme) {
            CHECK(std::string_view{s.name} == "<round-tripped>");  // 说吧
            break;
        }
        ++checked;
    }
    CHECK(checked == static_cast<int>(std::size(maya::theme::schemes)));
    CHECK(checked > 600);

    // 名字没有错,没有邻居,草率 二进制程序 搜索
    // 返回插入点的条目,该条目将默默地将字母顺序递回
    // -adjacent 主题。
    CHECK(agentty::ui_prefs::find_scheme("Dracul")  == nullptr);
    CHECK(agentty::ui_prefs::find_scheme("Draculaa") == nullptr);
    CHECK(agentty::ui_prefs::find_scheme("")         == nullptr);
    CHECK(agentty::ui_prefs::find_scheme("zzzzzzz")  == nullptr);
}

TEST_CASE("theming: the Background preference actually does something") {
    // 它过去完全没有做任何事情。极性被检测、解决、
    // 坚持和显示——一个完整的往返旅行——而没有任何消费者阅读,
    // 设置行的帮助声称它是"consulted by schemes that have both",
    // 而不是一个方案。一个改变什么的设置比一个
    // 缺失的设置更糟糕:用户认为他们想要的
    // 东西是不可能的。
    agentty::ui_prefs::Prefs p;
    p.theme    = "Dracula";                        // 暗图
    p.tier     = agentty::ui_prefs::ColorTier::TrueColor;
    p.polarity = agentty::ui_prefs::Polarity::Light;

    const auto r = agentty::ui_prefs::resolve(p, agentty::ui_prefs::detect(true));
    REQUIRE(r.theme != nullptr);
    CHECK(agentty::ui_prefs::scheme_is_light(*r.theme) == std::optional{false});
    CHECK(!agentty::ui_prefs::theme_override_reason(p, r).empty());

    // A NOTE ,而不是一个覆盖:
    // 用户选择了此操作 终端 并允许它被指。
    // 面板 用于避免。
    CHECK(r.theme != &maya::theme::native);

    // 匹配极性是无足轻重的,而且什么也说不出来.
    agentty::ui_prefs::Prefs q = p;
    q.polarity = agentty::ui_prefs::Polarity::Dark;
    const auto rq = agentty::ui_prefs::resolve(q, agentty::ui_prefs::detect(true));
    CHECK(agentty::ui_prefs::theme_override_reason(q, rq).empty());

    // 和光线上的光线计 终端 也一样安静——检查是对称的,
    // 而不是 "dark is suspicious".
    agentty::ui_prefs::Prefs l;
    l.theme    = "Catppuccin Latte";               // 轻度计划
    l.tier     = agentty::ui_prefs::ColorTier::TrueColor;
    l.polarity = agentty::ui_prefs::Polarity::Light;
    const auto rl = agentty::ui_prefs::resolve(l, agentty::ui_prefs::detect(true));
    REQUIRE(rl.theme != nullptr);
    CHECK(agentty::ui_prefs::scheme_is_light(*rl.theme) == std::optional{true});
    CHECK(agentty::ui_prefs::theme_override_reason(l, rl).empty());
}

// 原生状态 a 默认背景 —— SGR 4,第49页, 终端 自己的画布
// 因此,它已经 NO 自身极性,且不得主张:它曾经通过
// 默认投射 to_rgb (, 白化, 并报告自己为
// 光亮1.0的光度方案。 任何将它与检测到的极性相比较
// 的呼叫者, 都会在每暗处得到一个自信的错答。 终端.
TEST_CASE("issue 37: native claims no polarity, because it paints no canvas") {
    CHECK(!agentty::ui_prefs::scheme_is_light(maya::theme::native).has_value());

    // 所以它不会触发错配音符, 无论是极性。
    for (auto pol : {agentty::ui_prefs::Polarity::Light,
                     agentty::ui_prefs::Polarity::Dark}) {
        agentty::ui_prefs::Prefs p;
        p.theme    = "";            // 没有名为 QQ 的本地方案
        p.tier     = agentty::ui_prefs::ColorTier::TrueColor;
        p.polarity = pol;
        const auto r = agentty::ui_prefs::resolve(p, agentty::ui_prefs::detect(true));
        CHECK(agentty::ui_prefs::theme_override_reason(p, r).empty());
    }
}
