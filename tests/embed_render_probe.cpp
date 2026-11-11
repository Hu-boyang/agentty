// 嵌入式视觉探测器 面板。不是测试,而是打印出真正的 帧 人类可以看它,
// 和人类一样
// palette_render_probe.cpp.
//
//   ./embed_render_probe 关闭的下拉
//   ./embed_render_probe --menu 打开

#include <cstdio>
#include <cstdlib>
#include <string>

#include <maya/print.hpp>
#include <maya/widget/panel.hpp>

#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/view/form_panel.hpp"
#include "agentty/runtime/view/palette.hpp"

int main(int argc, char** argv) {
    namespace eb = agentty::rag::embed;
    namespace rs = agentty::rag_settings;
    namespace sf = agentty::smart_form;

    const std::string arg = argc > 1 ? argv[1] : "";

    // 智能模式,用于在第二面板上对共享形式层进行眼球.
    // --smart-advanced 也显示路径 -policy 行数( ^A ),"在应用中".
    if (arg == "--smart" || arg == "--smart-advanced") {
        sf::Inputs in;
        in.enabled        = true;
        in.strategic      = {"Claude Opus 4.5", true};
        in.implementation = {"Claude Sonnet 4.5", false};
        in.utility        = {"Claude Haiku 4.5", false};
        in.advanced       = (arg == "--smart-advanced");
        auto f = sf::build_form(in);
        const auto out = maya::render_to_string_ansi(
            maya::Panel{agentty::ui::form_config(f, agentty::ui::success)}.build(), 84);
        std::printf("%s\n", out.c_str());
        return 0;
    }

    const bool open_menu = (arg == "--menu");
    const bool show_auto = (arg == "--auto");

    eb::EmbedConfig c;
    if (show_auto) {
        c.backend = eb::Backend::Auto;
    } else {
        c.backend = eb::Backend::OpenAI;
        c.host    = "gateway.internal";
        c.port    = 8443;
        c.tls     = true;
        c.api_key = "sk-not-printed";
    }
    c.model = "nomic-embed-text";

    const bool advanced = (arg == "--advanced" || arg == "--scroll");
    const bool edit     = (arg == "--edit");
    agentty::store::Settings settings;
    auto form = rs::build_form(c, agentty::store::RagMode::On, settings, {}, advanced);
    form.subtitle = eb::describe(c);
    form.note     = "unsaved \xe2\x80\x94 ^T test, ^S save";
    form.cursor   = 0;

    // --scroll : 一个短 可视区域 光标位于
    // END ,这就是屏幕上实际失败的情况(即: 面板
    // 画过底边,最后一行无法到达。
    maya::ScrollState scroll;
    int viewport = 0;
    if (arg == "--scroll") {
        viewport = 12;
        form.cursor = static_cast<int>(form.fields.size()) - 1;
    }

    if (open_menu) {
        auto a = agentty::form::keys::Action{agentty::form::keys::Intent::Activate};
        (void)agentty::form::keys::apply(form, a);
    }

    // --edit : 光标在 Model 行(一个文本字段),中 -edit
    // — 渲染 编号 光标 编辑 -hued 边栏和注释行中模式提示。
    if (edit) {
        for (std::size_t i = 0; i < form.fields.size(); ++i)
            if (form.fields[i].is_text_like() && !form.fields[i].locked) {
                form.cursor = static_cast<int>(i);
                break;
            }
        auto a = agentty::form::keys::Action{agentty::form::keys::Intent::Activate};
        (void)agentty::form::keys::apply(form, a);
    }

    // --width=N 渲染 以该列计数,
    // 所以可以以算术的狭小尺寸验证弹性布局 -based 版本已崩溃。
    int width = 84;
    for (int i = 1; i < argc; ++i) {
        const std::string a{argv[i]};
        if (a.rfind("--width=", 0) == 0) width = std::atoi(a.c_str() + 8);
    }

    const auto out = maya::render_to_string_ansi(
        maya::Panel{agentty::ui::form_config(form, agentty::ui::info,
                                            viewport > 0 ? &scroll : nullptr,
                                            viewport,
                                            width)}.build(), width);
    std::printf("%s\n", out.c_str());
    return 0;
}
