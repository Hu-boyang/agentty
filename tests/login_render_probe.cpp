// login_render_probe — 渲染 每个小组 -converted 登录状态
// 如此人(和 CI )可以看它:
//   ./agentty_standalone_tests login_render_probe 账户列表
//   ./agentty_standalone_tests login_render_probe --confirm 删除武装
//   ./agentty_standalone_tests login_render_probe --key      API
//   关键输入 ./agentty_standalone_tests login_render_probe --key-full 键部分
//   带类型的 ./agentty_standalone_tests login_render_probe --host 自定义主机输入
//   ./agentty_standalone_tests login_render_probe --oauth OAuth 代码条目
#include <cstdio>
#include <cstring>
#include <string>

#include <maya/print.hpp>
#include <maya/core/render_context.hpp>

#include "agentty/runtime/login.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/login.hpp"

using namespace agentty;
namespace lg = agentty::ui::login;

int main(int argc, char** argv) {
    const std::string arg = argc > 1 ? argv[1] : "";

    Model m;
    if (arg == "--key" || arg == "--key-full") {
        lg::ApiKeyInput s;
        if (arg == "--key-full") {
            s.key_input = "sk-ant-api03-abcdefghij";
            s.cursor    = static_cast<int>(s.key_input.size());
        }
        m.ui.login = std::move(s);
    } else if (arg == "--host") {
        lg::CustomHostInput s;
        s.host_input = "gateway.internal:8443/v1#work";
        s.cursor     = static_cast<int>(s.host_input.size());
        m.ui.login = std::move(s);
    } else if (arg == "--oauth") {
        lg::OAuthCode s;
        s.authorize_url = "https://claude.ai/oauth/authorize?code=true&client_id=abcdef";
        m.ui.login = std::move(s);
    } else {
        lg::AccountList s;
        s.provider_label = "Anthropic";
        s.rows = {{"anthropic", "work",     true},
                  {"anthropic", "personal", false},
                  {"anthropic", "team-eu",  false}};
        s.cursor = 1;
        if (arg == "--confirm") s.confirm_remove = "personal";
        m.ui.login = std::move(s);
    }

    maya::RenderContext ctx{86, 40, maya::render_generation(), true};
    maya::RenderContextGuard g(ctx);
    auto el  = ui::login_modal(m);
    auto out = maya::render_to_string(el, 86);
    std::fputs(out.c_str(), stdout);
    return 0;
}
