// settings_add_effect_test ——设置 面板 添加时 -mode 创建是一个 副作用.
//
// 在插件(或Commands/Agents)中提交"name command",
// 加上提示词写入磁盘:一个条目在mcp.json,或一个启动文件。这个条目曾经运行
// INSIDE,即状态更新函数。现在,状态更新函数只描述它(cmd::settings_add)
// 和归并的答案(SettingsAddDone)。
//   1. 提交返回a Cmd 写 无内容 中切换到磁盘 归并.
//   2. SettingsAddDone 用于插件烤面包 "… — connecting…" 开始
//      a 插件重新装入; 失败时会向信件敬酒, 并重新装入任何内容。

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/settings/list.hpp"

namespace fs = std::filesystem;
using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

void type(Model& m, const std::string& s) {
    for (char c : s)
        (void)app::update(m, msg::SettingsListMsg{SettingsListChar{static_cast<char32_t>(c)}});
}
}  // 命名空间结束。

int main() {
    std::printf("=== settings_add_effect_test ===\n");
    const fs::path home = fs::temp_directory_path() / "agentty_settings_add_effect";
    fs::remove_all(home);
    fs::create_directories(home);
#if defined(_WIN32)
    _putenv_s("AGENTTY_HOME", (home / ".agentty").string().c_str());
#else
    ::setenv("AGENTTY_HOME", (home / ".agentty").c_str(), 1);
    ::setenv("HOME", home.c_str(), 1);
#endif
    const fs::path mcp = home / ".agentty" / "mcp.json";

    Model m;
    pn::SettingsList o;
    o.concern = settings::Category::Plugins;
    m.ui.panel.descend(std::move(o));

    (void)app::update(m, msg::SettingsListMsg{SettingsListAddStart{}});
    type(m, "demo /bin/true");
    auto cmd = app::update(m, msg::SettingsListMsg{SettingsListSubmitInput{}});
    check(!cmd.is_none(), "submit returns an effect");
    check(!fs::exists(mcp), "submit wrote nothing during the fold");
    const auto* after = m.ui.panel.get<pn::SettingsList>();
    check(after && !after->input_active && after->input.empty(),
          "submit closes the add prompt");

    // 电话 副作用 '答曰,折叠.
    m.ui.plugins_loading = false;
    auto ok_cmd = app::update(m, msg::SettingsListMsg{
        SettingsAddDone{settings::Category::Plugins, true, "added demo"}});
    check(!ok_cmd.is_none(), "a successful plugin add starts a reload");
    check(m.ui.plugins_loading, "the pane shows the reload in flight");
    check(m.s.status.find("added demo") != std::string::npos
              && m.s.status.find("connecting") != std::string::npos,
          "the toast says what happened and that it is connecting");

    m.ui.plugins_loading = false;
    (void)app::update(m, msg::SettingsListMsg{
        SettingsAddDone{settings::Category::Plugins, false, "no such script: x.py"}});
    check(!m.ui.plugins_loading, "a failed add reloads nothing");
    check(m.s.status.find("no such script") != std::string::npos,
          "a failed add toasts its reason");

    fs::remove_all(home);
    std::printf(g_fail ? "FAILED (%d)\n" : "PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
