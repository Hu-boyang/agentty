// 内 -app 登录 弹出面板.
//
// 5个子 -states 键入关卡 `ui::login::State`:
//
//   选择 OAuth (1) 或 API 键 (2)
//   OAuthCode 浏览器打开; 用户粘贴 回调 代码。
//                    授权的URL获得一个专用的边框+`[c] copy URL`
//                    /`[o] open in browser again`
//                    付得起,因此URL是从用户的剪贴板上键入
//                    一个键盘的——不需要终端 鼠标-select.
//   交换权限 HTTP POST 飞行中。
//   ApiKeyInput 粘贴 SK -ant- ...钥匙。
//   选择面板上方失败。
//
// 大小是响应的:外包装器设置了 `min_width` 楼层和楼层
// 覆盖面板 部件的默认缩放使其能填充可用的宽度( 减去 2) -col
// 边缘粘贴。 每个文本节点都使用 TextWrap::Wrap 如此长的
// URL / API 键盘标签重流而不是切换。 选择面板 铬(见
// view/pickers.cpp's wrap_picker).

#include "agentty/runtime/view/login.hpp"

#include <string>
#include <variant>
#include <vector>

#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/view/palette.hpp"

#include <maya/widget/panel.hpp>

namespace agentty::ui {

using namespace maya;
using namespace maya::dsl;
// `login::` 决定 `agentty::ui::login::`
// 不属于这一范围,没有别名——和 MSVC 拒绝一个名字阴影着
// 现有巢命名空间的别名,所以不要写一个.

namespace {

// 共享密钥 -hint 页脚。 每个子 -state 将上下文
// 中有意义的密钥传递给用户,这样用户就总是看到完整的地图。
// 包装如此长的脚印在窄的终端上重流.
Element key_hints(std::initializer_list<std::pair<std::string, std::string>> hints) {
    // 平整成一个 TextElement 画家
    // 可以用笔划的笔划 画得很窄 终端 之前的施塔
    // 克方法 一直没有包起来
    std::string content;
    std::vector<StyledRun> runs;
    auto push_run = [&](std::string_view s, Style sty) {
        if (s.empty()) return;
        runs.push_back(StyledRun{
            .byte_offset = content.size(),
            .byte_length = s.size(),
            .style       = sty,
        });
        content.append(s);
    };
    const Style key_sty = Style{}.with_fg(fg).with_bold();
    const Style sep_sty = fg_dim(muted);
    const Style val_sty = fg_dim(muted);
    bool first = true;
    for (const auto& [k, v] : hints) {
        if (!first) push_run("   \xc2\xb7   ", sep_sty);   //   ·
        first = false;
        push_run(k, key_sty);
        push_run(" ", sep_sty);
        push_run(v, val_sty);
    }
    return Element{TextElement{
        .content = std::move(content),
        .style   = Style{}.with_fg(fg),
        .wrap    = TextWrap::Wrap,
        .runs    = std::move(runs),
    }};
}

// 环绕 -mode 正文 文本——长的 URL/ 解释行重
// 流而不是溢出 弹出面板.
Element body_text(std::string_view content, Style sty) {
    return Element{TextElement{
        .content = std::string{content},
        .style   = sty,
        .wrap    = TextWrap::Wrap,
    }};
}

// 边框,包装-modeXX "URL panel":
// URL在窄的终端上填充弹出面板宽度和横行回流,
// 因此用户可以不横向滚动阅读每个字符.
// 背景-styled因此读作专用文物(即要复制的东西),
// 而不是内线的散文.
Element url_panel(std::string_view url) {
    auto url_text = Element{TextElement{
        .content = std::string{url},
        .style   = Style{}.with_fg(code_path),   // bright_cyan — "this is a path/identifier"
        .wrap    = TextWrap::Wrap,
    }};
    return (v(url_text)
            | padding(0, 1)
            | border(BorderStyle::Round)
            | bcolor(code_path)).build();
}

Element panel_picking(std::string_view provider,
                      bool failed, std::string_view fail_msg) {
    // 此提供方是否允许选择认证方式？
    // 是一个 注册表 能力( method_menu ),而不是名字——即:
    // 状态更新函数 门读的一样 标志 ,所以视图和行为不能漂移。
    const auto* prow = provider::preset_for(provider);
    const bool anthropic_only = prow && prow->method_menu;
    std::vector<Element> rows;
    rows.push_back(text(anthropic_only
                            ? "Add a " + std::string{prow->label} + " account"
                            : std::string{"Sign in to agentty"},
                        fg_bold(fg)));
    rows.push_back(body_text(
        anthropic_only
            ? "Choose how to authenticate this new Anthropic account."
            : "Bring your own model. Pick how you want to connect — you can "
              "change this any time from the provider picker.",
        fg_dim(muted)));
    rows.push_back(text(""));
    if (failed) {
        // 环绕 -mode 危险烤面包,所以一个长长的错误
        // 信息 留在里面 弹出面板 而不是溢入铬中。
        rows.push_back(body_text(
            std::string{"\xE2\x9A\xA0 "} + std::string{fail_msg},
            fg_of(danger)));
        rows.push_back(text(""));
    }
    // 编号项目是shrow-left,与头文件 /
    // 描述 / 提示行共享列。标题坐在数字徽章旁边;
    // 字幕续缩缩为确切的徽章宽度("1) " =
    // 3个字符),所以它视像挂在标题下,而不是数字下.
    //
    // API 键被显示 首次 :这是毫不含糊的,提供者 -neutral 路径( 任何)
    // Anthropic/OpenAI-family ),而“在““““”一词之后,“““A”字“字”字“““E”字“)
    // ”“““P””““A”(“A”“A””“A”)“A”和“A” 事件订阅 欧奥特是第三个 -party-client
    // 路径中,一些用户宁愿避免。在键的引导下,不保存 -controversial 选项一个键键键退出。
    rows.push_back(h(text("1) ", fg_bold(highlight)),
                     text("Paste an API key", fg_bold(fg))).build());
    rows.push_back(h(text("   ", fg_of(fg)),
                     body_text("Anthropic sk-ant-…, or any provider's key",
                               fg_dim(muted))).build());
    rows.push_back(text(""));
    rows.push_back(h(text("2) ", fg_bold(highlight)),
                     text("OAuth via claude.ai", fg_bold(fg))).build());
    rows.push_back(h(text("   ", fg_of(fg)),
                     body_text("use your Claude Pro / Max subscription",
                               fg_dim(muted))).build());
    rows.push_back(text(""));
    if (!anthropic_only) {
        rows.push_back(h(text("3) ", fg_bold(highlight)),
                         text("Sign in with ChatGPT", fg_bold(fg))).build());
        rows.push_back(h(text("   ", fg_of(fg)),
                         body_text("use your ChatGPT Plus / Pro (GPT-5 Codex)",
                                   fg_dim(muted))).build());
        rows.push_back(text(""));
        rows.push_back(h(text("4) ", fg_bold(highlight)),
                         text("Custom OpenAI-compatible host", fg_bold(fg)),
                         text("  \xe2\x80\x94 llama.cpp, vLLM, LM Studio, Ollama",
                                   fg_dim(muted))).build());
        rows.push_back(text(""));
        rows.push_back(h(text("5) ", fg_bold(highlight)),
                         text("Sign in with GitHub Copilot", fg_bold(fg))).build());
        rows.push_back(h(text("   ", fg_of(fg)),
                         body_text("use your GitHub Copilot subscription",
                                   fg_dim(muted))).build());
        rows.push_back(text(""));
        rows.push_back(h(text("6) ", fg_bold(highlight)),
                         text("Sign in with Kimi", fg_bold(fg))).build());
        rows.push_back(h(text("   ", fg_of(fg)),
                         body_text("use your Kimi plan (K2 models)",
                                   fg_dim(muted))).build());
        rows.push_back(text(""));
    }
    rows.push_back(key_hints({{anthropic_only ? "1/2" : "1\xe2\x80\x93" "6", "choose"},
                              {"Esc", "close"}}));
    return v(std::move(rows)).build();
}

// 一个 -item 单人面板 -field 输入状态。字段是 真实
// 面板项目——秘密(计数) -only 遮盖,类型 -level 没
// 办法 -leak 保证)或文本( 光标 窗口,所以在下方有一个长的粘贴
// 卷轴 光标 而不是从边缘走开 Prose的骑马是 头文件 线条;
// Enter SUBMITS 在此,所以所得出的编辑提示被覆盖。
Element input_panel(std::string title, std::vector<Element> prose,
                    maya::PanelControl control, std::string note,
                    std::string editing_note,
                    std::vector<Element> footer = {}) {
    maya::Panel::Config cfg;
    cfg.title     = std::move(title);
    cfg.accent    = accent;
    cfg.min_width = 48;
    cfg.selected  = 0;
    for (auto& p : prose) cfg.header.push_back(std::move(p));
    cfg.header.push_back(text(""));
    maya::Panel::Item it;
    it.leading = "\xe2\x80\xba";                    // ⁇  – 提示词 标记
    it.leading_style = fg_bold(accent);
    it.control = std::move(control);
    // 提示词-style :输入拥抱 QQ而不是右 -aligning
    // 屏幕 -width 离那部经典而去,(这行没有标签栏),
    it.value_primary = true;
    cfg.items.push_back(std::move(it));
    cfg.footer       = std::move(footer);
    cfg.note         = std::move(note);
    cfg.editing_note = std::move(editing_note);
    return maya::Panel{std::move(cfg)}.build();
}

Element panel_oauth_code(const login::OAuthCode& s) {
    std::vector<Element> prose;
    prose.push_back(body_text(
        "  Step 1 \xe2\x80\x94 open this URL and authorize agentty:",
        fg_dim(muted)));
    prose.push_back(text(""));
    prose.push_back(url_panel(s.authorize_url));
    prose.push_back(text(""));
    prose.push_back(body_text(
        "  Step 2 \xe2\x80\x94 paste the callback code below:",
        fg_dim(muted)));
    // 代码是 凭据 中途: 秘密, 计数 -only.
    maya::panel::Secret sec;
    sec.filled      = s.code_input.size();
    sec.caret       = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, s.cursor)), s.code_input.size());
    sec.placeholder = "paste the code from claude.ai";
    std::vector<Element> footer;
    footer.push_back(text(""));
    footer.push_back(key_hints({
        {"c",   "copy URL"},
        {"o",   "open browser"},
        {"^Y",  "copy"},
        {"^O",  "open"},
    }));
    return input_panel(" OAuth via claude.ai ", std::move(prose),
                       std::move(sec),
                       "\xe2\x86\xb5 submit code \xc2\xb7 esc cancel",
                       "\xe2\x86\xb5 submit code \xc2\xb7 esc cancel",
                       std::move(footer));
}

Element panel_oauth_exchanging() {
    std::vector<Element> rows;
    rows.push_back(text("Exchanging authorization code\xE2\x80\xA6",   // …
                        fg_bold(fg)));
    rows.push_back(text(""));
    rows.push_back(body_text(
        "Talking to platform.claude.com — this should take a second.",
        fg_dim(muted)));
    rows.push_back(text(""));
    rows.push_back(key_hints({{"Esc", "cancel"}}));
    return v(std::move(rows)).build();
}

Element panel_chatgpt_waiting(const login::ChatGptWaiting& s) {
    std::vector<Element> rows;
    if (s.device_auth) {
        rows.push_back(text(s.user_code.empty()
                                ? "Requesting ChatGPT device code\xE2\x80\xA6"
                                : "Sign in to ChatGPT",
                            fg_bold(fg)));
        rows.push_back(text(""));
        if (s.user_code.empty()) {
            rows.push_back(body_text(
                "This SSH session uses device authorization. Waiting for "
                "OpenAI to issue a one-time code\xE2\x80\xA6",
                fg_dim(muted)));
        } else {
            rows.push_back(body_text(
                "Open this link in a browser on any device:", fg_dim(muted)));
            rows.push_back(text(""));
            rows.push_back(url_panel(s.authorize_url));
            rows.push_back(text(""));
            rows.push_back(body_text("Then enter this one-time code:",
                                     fg_dim(muted)));
            rows.push_back(text(s.user_code, fg_bold(fg)));
            rows.push_back(text(""));
            rows.push_back(body_text(
                "The code expires in 15 minutes. Continue only because you "
                "started this login in agentty.", fg_dim(muted)));
        }
    } else {
        rows.push_back(text("Waiting for ChatGPT\xE2\x80\xA6", fg_bold(fg)));
        rows.push_back(text(""));
        rows.push_back(body_text(
            "A browser tab has opened at chatgpt.com \xE2\x80\x94 authorize agentty "
            "there. The sign-in completes automatically over a local callback "
            "(http://localhost:1455); you don't need to paste anything.",
            fg_dim(muted)));
        rows.push_back(text(""));
        if (!s.authorize_url.empty()) {
            rows.push_back(body_text(
                "Browser didn't open? Visit this URL manually:", fg_dim(muted)));
            rows.push_back(text(""));
            rows.push_back(url_panel(s.authorize_url));
            rows.push_back(text(""));
        }
    }
    rows.push_back(text(""));
    rows.push_back(key_hints({{"Esc", "cancel"}}));
    return v(std::move(rows)).build();
}

Element panel_device_waiting(const login::DeviceWaiting& s) {
    const std::string label = s.provider_label.empty() ? "your provider"
                                                       : s.provider_label;
    std::vector<Element> rows;
    rows.push_back(text(s.user_code.empty()
                            ? "Requesting device code\xE2\x80\xA6"
                            : "Sign in with " + label,
                        fg_bold(fg)));
    rows.push_back(text(""));
    if (s.user_code.empty()) {
        rows.push_back(body_text(
            "Waiting for " + label + " to issue a one-time code\xE2\x80\xA6",
            fg_dim(muted)));
    } else {
        rows.push_back(body_text(
            "Open this URL to approve the device (it already includes your",
            fg_dim(muted)));
        rows.push_back(body_text(
            "code). On SSH, press [u] to copy it, or scan/type it on any device:",
            fg_dim(muted)));
        rows.push_back(text(""));
        rows.push_back(url_panel(s.authorize_url));
        rows.push_back(text(""));
        rows.push_back(h(text("   if the page asks for a code, enter: ", fg_dim(muted)),
                         text(s.user_code, fg_bold(highlight))).build());
        rows.push_back(text(""));
        rows.push_back(body_text(
            "After signing in to " + label + ", approve the device. The code "
            "expires in ~15 minutes; no API key is needed.", fg_dim(muted)));
        if (s.provider == "copilot") {
            // GitHub 显示此同意页为 `GitHub Copilot Plugin` ,
            // 没有 `Agentty` . 这是故意的,而且 不 可替换:
            // 符号 -exchange 仅接受结束点 VS 代码 GitHub
            // 应用代号( $ `Iv1.b507a08c87ecfe98`),
            // 和 副驾驶服务器按键其模式 允许清单 使用不同的App
            // id 将反弹 代号交换 * 或 * 返回一个剥离的模型列表。
            // 见 docs/design/copilot-provider.md
            // 第二节 调用 明确显示用户不会因为自己点击错误而
            // 中止 链接.
            rows.push_back(text(""));
            rows.push_back(body_text(
                "Note: GitHub will label the app \"GitHub Copilot Plugin\" on "
                "the consent page. That is expected \xe2\x80\x94 agentty signs "
                "in as VS Code's Copilot client because the API's model "
                "allowlist is keyed to that identity.",
                fg_dim(muted)));
        }
        rows.push_back(text(""));
        rows.push_back(key_hints({
            {"c",   "copy code"},
            {"u",   "copy URL"},
            {"o",   "open browser"},
            {"Esc", "cancel"},
        }));
        return v(std::move(rows)).build();
    }
    rows.push_back(text(""));
    rows.push_back(key_hints({{"Esc", "cancel"}}));
    return v(std::move(rows)).build();
}

Element panel_api_key(const login::ApiKeyInput& s) {
    const bool anthropic = s.provider.empty();
    const bool custom = !anthropic && !provider::preset_for(s.provider);
    const bool optional = custom && !s.key_required;
    std::vector<Element> prose;
    prose.push_back(body_text(
        anthropic
            ? "  Paste an sk-ant-\xe2\x80\xa6 key. It's saved encrypted under "
              "~/.agentty/credentials."
        : optional
            ? "  Paste this host's API key, or leave it empty if the server "
              "needs none. A key is saved encrypted under ~/.agentty/credentials."
        : custom
            ? "  This host answered 401: it needs an API key. Paste it here; "
              "it's saved encrypted under ~/.agentty/credentials."
            : "  Paste your " + s.provider_label + " API key to switch to "
              "it. It's saved encrypted under ~/.agentty/credentials so you "
              "won't be asked again.",
        fg_dim(muted)));
    // 电话 KEY 从未到达部件... panel::Secret 带一个数。
    maya::panel::Secret sec;
    sec.filled      = s.key_input.size();
    sec.caret       = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, s.cursor)), s.key_input.size());
    sec.placeholder = anthropic ? "sk-ant-\xe2\x80\xa6"
                    : optional  ? "API key (optional)\xe2\x80\xa6"
                                : "paste API key\xe2\x80\xa6";
    const char* back = login::has_parent(s.origin) ? "esc back" : "esc cancel";
    const std::string enter = optional && s.key_input.empty()
        ? "\xe2\x86\xb5 continue without a key \xc2\xb7 "
        : "\xe2\x86\xb5 submit \xc2\xb7 ";
    return input_panel(
        " " + (anthropic ? std::string{"Anthropic"} : s.provider_label) + " API key ",
        std::move(prose), std::move(sec), enter + back, enter + back);
}

// 飞行中连接探测器: 指定主机名称和正在尝试的是什么,
// 短等读作 WORK 而不是绞刑 Esc 取消输入。
Element panel_host_probing(const login::HostProbing& s) {
    std::vector<Element> rows;
    rows.push_back(text("Checking " + s.spec + "\xe2\x80\xa6", fg_bold(fg)));
    rows.push_back(text(""));
    rows.push_back(body_text(
        "Dialing the server's model list \xe2\x80\x94 the configured path, "
        "then /v1/models, then Ollama's /api/tags \xe2\x80\x94 to detect what "
        "it speaks and confirm it's alive before switching.",
        fg_dim(muted)));
    rows.push_back(text(""));
    rows.push_back(key_hints({{"Esc", "cancel"}}));
    return v(std::move(rows)).build();
}

Element panel_custom_host(const login::CustomHostInput& s) {
    std::vector<Element> prose;
    prose.push_back(body_text(
        "  Enter a host or host:port for any server that speaks the OpenAI "
        "chat API \xe2\x80\x94 llama.cpp, vLLM, LM Studio, a proxy, or a "
        "remote box. A non-443 port uses plain HTTP (the local-server "
        "convention); a bare host uses HTTPS on 443 and defaults to the "
        "/v1 API prefix. For a server on a different prefix, paste the "
        "full URL (https://host/path) and the prefix is honoured verbatim. "
        "Append #name to keep several accounts on the SAME endpoint "
        "(e.g. \xe2\x80\xa6/v1#work and \xe2\x80\xa6/v1#personal \xe2\x80\x94 "
        "each keeps its own API key and model).",
        fg_dim(muted)));
    // 文本,而不是秘密——一个主机不是 凭据 ,
    // 并显示 光标 窗口表示粘贴时间长 URL 滚动 光标.
    maya::panel::Text t;
    t.value       = s.host_input;
    t.caret       = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, s.cursor)), s.host_input.size());
    t.placeholder = "localhost:8080";
    const char* back = login::has_parent(s.origin) ? "esc back" : "esc cancel";
    std::vector<Element> footer;
    footer.push_back(text(""));
    footer.push_back(body_text(
        "  Examples:  localhost:8080  \xc2\xb7  https://ollama.com/v1#work  "
        "\xc2\xb7  inference.example.com  \xc2\xb7  https://inference.example.com/api",
        fg_dim(muted)));
    return input_panel(
        " Custom OpenAI-compatible host ", std::move(prose), std::move(t),
        std::string{"\xe2\x86\xb5 connect \xc2\xb7 "} + back,
        std::string{"\xe2\x86\xb5 connect \xc2\xb7 "} + back,
        std::move(footer));
}

// 账户切换器作为真实的maya::Panel——每选择
// 面板使用相同的部件,因此账户列表会获得面板的光标栏,
// 徽章栏,可视区域滚动和模式铬,而不是一个hand
// -rolled逆-video列表(面板将其它地方都删除).
//
// 项目映射:
//   徽章上显示 活动 账户(accent)——删除时是武装的领先标签( "#work",
//   "personal…" ) 跟踪
//   "press d again to confirm" 持枪者,
//           SECONDARY ,所以标签存活
//   的狭窄 终端 + 添加另一个账户... 以显示为最后一个项目, 暗到选中
Element panel_account_list(const login::AccountList& s,
                           maya::ScrollState* scroll,
                           std::string_view active_error) {
    maya::Panel::Config cfg;
    cfg.title     = " " + s.provider_label + " accounts ";
    cfg.accent    = accent;
    cfg.min_width = 48;
    // 滚动像每个列表面板一样: 在一个提供者上的许多账户必须
    // 在内部窗口中 弹出面板 而不是成长过 终端 边缘。
    cfg.scroll     = scroll;
    cfg.viewport_h = 14;
    const int n = static_cast<int>(s.rows.size());
    cfg.selected  = std::min(s.cursor, n);   // 最后一个项目= 添加-new

    if (s.rows.empty()) {
        cfg.header.push_back(body_text(
            "  No saved accounts. Add one to sign in without leaving agentty.",
            fg_dim(muted)));
        cfg.header.push_back(text(""));
    }

    for (const auto& r : s.rows) {
        maya::Panel::Item it;
        const bool confirming = (s.confirm_remove == r.label);
        it.leading = r.label;
        it.active  = r.active;
        if (confirming) {
            it.badge           = "\xe2\x9a\xa0";           // ⚠
            it.badge_style     = fg_of(danger);
            it.leading_style   = fg_bold(danger);
            it.trailing        = "press d again to confirm";
            it.trailing_style  = fg_of(danger);
        } else if (r.active && !active_error.empty()) {
            // 主机在最后的请求中拒绝了此密钥: 请在这里说,
            // 用户可以添加一个工作密钥。
            it.badge         = "\xe2\x9a\xa0";           // ⚠
            it.badge_style   = fg_of(warn);
            it.leading_style = fg_bold(warn);
            it.trailing       = "rejected by the host";
            it.trailing_style = fg_of(warn);
        } else if (r.active) {
            it.badge         = "\xe2\x9c\x93";             // ✓
            it.badge_style   = fg_of(accent);
            it.leading_style = fg_bold(accent);
            it.trailing       = "active";
            it.trailing_style = fg_dim(muted);
        }
        it.trailing_secondary = true;
        cfg.items.push_back(std::move(it));
    }
    {
        maya::Panel::Item add;
        add.leading       = "+ Add another account\xe2\x80\xa6";
        add.leading_style = fg_dim(muted);
        cfg.items.push_back(std::move(add));
    }

    cfg.note = s.confirm_remove.empty()
                   ? "\xe2\x86\x91\xe2\x86\x93 move \xc2\xb7 \xe2\x86\xb5 select \xc2\xb7 d remove \xc2\xb7 esc close"
                   : "d confirm remove \xc2\xb7 any other key cancels";
    return maya::Panel{std::move(cfg)}.build();
}

} // 命名空间结束。

Element login_modal(const Model& m) {
    if (!login::is_open(m.ui.login)) return nothing();

    Element body = std::visit([&m](const auto& s) -> Element {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::same_as<T, login::Closed>) {
            return nothing();
        } else if constexpr (std::same_as<T, login::Picking>) {
            return panel_picking(s.provider, false, "");
        } else if constexpr (std::same_as<T, login::OAuthCode>) {
            return panel_oauth_code(s);
        } else if constexpr (std::same_as<T, login::OAuthExchanging>) {
            return panel_oauth_exchanging();
        } else if constexpr (std::same_as<T, login::ChatGptWaiting>) {
            return panel_chatgpt_waiting(s);
        } else if constexpr (std::same_as<T, login::DeviceWaiting>) {
            return panel_device_waiting(s);
        } else if constexpr (std::same_as<T, login::ApiKeyInput>) {
            return panel_api_key(s);
        } else if constexpr (std::same_as<T, login::CustomHostInput>) {
            return panel_custom_host(s);
        } else if constexpr (std::same_as<T, login::HostProbing>) {
            return panel_host_probing(s);
        } else if constexpr (std::same_as<T, login::AccountList>) {
            // 活动提供者的最后一个接头说,它的密钥被拒绝了.
            std::string_view key_err;
            const bool auth_err = m.s.models_error.find("HTTP 401") != std::string::npos
                               || m.s.models_error.find("HTTP 403") != std::string::npos;
            const auto& sel = m.d.selection;
            const std::string active_id = !sel.provider_id().empty()
                ? std::string{sel.provider_id()}
                : (sel.kind == provider::Kind::OpenAI ? sel.openai_endpoint.label
                                                      : std::string{});
            if (auth_err && !active_id.empty() && s.provider == active_id)
                key_err = m.s.models_error;
            return panel_account_list(s, &m.ui.account_list_scroll, key_err);
        } else if constexpr (std::same_as<T, login::Failed>) {
            return panel_picking("", true, s.message);
        }
    }, m.ui.login);

    // 小组讨论会 -BUILT 状态(输入、账户列表) 帧 ,
    // 标题和注释线——再次包装它们将筑起两个边界。
    // -built 传教士(选派,等待) 加载动画 ,
    // 失败)仍然得到通用 "Sign in" 帧.
    const bool self_framed =
        std::holds_alternative<login::OAuthCode>(m.ui.login)
        || std::holds_alternative<login::ApiKeyInput>(m.ui.login)
        || std::holds_alternative<login::CustomHostInput>(m.ui.login)
        || std::holds_alternative<login::AccountList>(m.ui.login);
    if (self_framed) return body;

    // 响应大小 : `min_width` 地板 弹出面板
    // 微小的可读宽度 终端 ; 缩写 覆盖面板 默认延伸使其增大,
    // 以填充所有宽度的列 终端 (减去 覆盖面板's 2-col 边缘垫。
    // 无最大封顶 – 每个文本节点都使用 TextWrap::Wrap ,
    // 所以URL和传言会自然地回流到任意宽度的 终端 空
    // 的96个cols 左转50个col 终端 典型的150
    // 个 -col 窗口中选择一个窗口。
    return vstack()
        .padding(1, 2)
        .min_width(Dimension::fixed(48))
        .border(BorderStyle::Round)
        .border_color(accent)
        .border_text(" Sign in to agentty ",
                     BorderTextPos::Top, BorderTextAlign::Center)
        (std::move(body));
}

} // namespace agentty::ui
