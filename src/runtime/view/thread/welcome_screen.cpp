#include "agentty/runtime/view/thread/welcome_screen.hpp"

#include <maya/core/render_context.hpp>
#include <maya/widget/model_badge.hpp>

#include "agentty/domain/model_name.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui {

maya::WelcomeScreen::Config welcome_screen_config(const Model& m) {
    // 欢迎屏幕本身有整台终端,因此显示最丰富的投影——
    // `full()`保持`· 1M`注解宽度
    // -tight状态将减少标签.
    const auto name = model_name::decode(m.d.model_id.value);
    maya::ModelBadge mb{{.label = name.full(), .color = name.color}};

    maya::WelcomeScreen::Config cfg;
    cfg.sigil_color = role_brand_alt;                   // bright_magenta 旗舰西吉尔
    cfg.tagline     = "a calm middleware between you and the model";
    cfg.model_badge    = mb.build();
    cfg.profile_label  = std::string{profile_label(m.d.profile)};
    cfg.profile_color  = profile_color(m.d.profile);
    // 启动卡——显示 仅 第一次真正运行: 线程 历史记录已装入
    // 完成( E) `!threads_loading`
    // ) 并且没有 已保存 谈话至今( A) `threads.empty()`。
    // 历史的返回用户从未看到过它,所以欢迎是干净的;
    // 占位符建议读作着陆 -page 工具一知道就填满,
    // 但品牌 -new 用户盯着一个空的作曲家,不知道先打什么—
    // —这三种混凝土,真实的 提示词 教三件事,你教三件事吗?
    // agentty 用于(理解a) 代码库 改变一下 run/fix 因此,
    // 第一个信息是有用的东西的复制件,而不是空白的。
    const bool first_run = !m.s.threads_loading && m.d.threads.empty();
    if (first_run) {
        cfg.starters_title = "New here? Try one of these";
        cfg.starters = {
            "Explain what this project does and how it's structured",
            "Find and fix the bug in <file> — it <symptom>",
            "Add a <feature> and run the tests",
        };
    } else {
        cfg.starters_title = {};
        cfg.starters       = {};
    }
    cfg.hint_intro     = "type to begin";
    // 完整的按键绑定地图(以前在底部状态栏的ShortcutRow,
    // 已经退役——现在状态栏成了一个高的-visibility土司槽).
    // per-key颜色表示每个绑定的一览:选择面板(
    // palette/threads/ todo/models)
    // 分享他们的动作-surfacehue,
    // profile/new/quit获得不同的语义色彩.
    cfg.hints          = {{"^K",    " palette", code_path},      // 青色——行动
                          {"^J",    " threads", role_info},      // 蓝色——导航
                          {"^\xe2\x86\x90\xe2\x86\x92", " cycle", role_info},   // 蓝色——导航(^←→)
                          {"^T",    " todo",    warn},           // 黄色——规划
                          {"S-Tab", " profile", role_brand},     // 红宝石——身份
                          {"^/",    " models",  role_brand_alt}, // 亮色红红色
                          {"^P",    " provider",code_path},      // 青色——行动
                          {"^N",    " new",     success},        // 绿色 – 创建
                          {"^C",    " quit",    danger}};        // 红色——破坏性
    cfg.accent_color   = role_brand;
    cfg.text_color     = fg;
    // 可视区域 夹子,闲的 帧 欢迎的周围是~11行的铬(
    // AppLayout 外部贴板 2 + 作曲 6 + 状态
    // 栏 3 ; 欢迎封顶 at term_rows -11岁,
    // 所以... 整个 闲置 帧 符合 终端. maya
    // 级 -drops 空白分隔符,然后将像素西吉尔折叠成一个
    // -row 字签。溢出欢迎词将上行输入本地 回滚缓冲区 开始时,
    // 第一次欢迎 / 对话互换 将它们拖到那里 ,
    // 字签的启动风味 -duplication 腐败。
    // available_height ()读取大小 RenderContext
    // ui::view 安装整个建构阶段(真正的ioctl dims,
    // 或带模拟的dims)—— do 不 ioctl在这里,
    // 它会覆盖测试的 带钉一个模拟几何。
    // 预备队是 SPELLED OUT 而不是一个
    // 魔法号码,因为数字是没有人能检查的。它比10的真
    // 铬要低11倍,所以欢迎的预算是短一行的预算,
    // 而且 帧 结束于 终端 '底边——作曲家下面的缺口。
    // 下面的每个术语都是一个可见的带,可以在屏幕上计数:
    constexpr int kOuterPadding = 1;   // AppLayout 垫片(1),仅顶端
    constexpr int kComposerRows = 5;   // 边框 + 输入 + 规则 + 元 + 边框
    constexpr int kStatusRows   = 3;   // 规则 + 状态线 + 规则
    constexpr int kWelcomeGap   = 1;   // 欢迎和作曲之间空行
    constexpr int kIdleChromeRows =
        kOuterPadding + kComposerRows + kStatusRows + kWelcomeGap;
    const int term_rows = maya::available_height();
    if (term_rows > 0)
        cfg.max_rows = std::max(4, term_rows - kIdleChromeRows);
    return cfg;
}

} // namespace agentty::ui
