#pragma once
// agentty::web_search_form —设置 → 网页搜索为 form::Form。
//
// 面板是设置注册表的薄投影：每一行都是web_search.
// *注册表行，因此其标签、帮助、控制种类、范围、
// 环境锁和"default"出处都来自该表。这里只有 面板
// 的框架和从行到配置的 write-back。
//
// 纯粹：没有IO，没有全局变量。 add_rows 内部的 env-lock
// 检查读取进程环境，这就是为什么它存在于 panel/（与其他表单构建器）
// 下而不是在 状态更新函数 中的原因。

#include "agentty/domain/web_search_config.hpp"
#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/settings_registry.hpp"   // EnvSnapshot

#include <functional>
#include <string>
#include <string_view>

namespace agentty::web_search_form {

// 服务的API密钥现在来自哪里："stored"、"env: NAME"或
// ""（当没有时）。 面板 只知道 该 存在一个密钥，而不知道它是什么。
// 传入而不是查找，因此构建 面板 不会触及密钥库。
using KeyOrigin = std::function<std::string(std::string_view service)>;

// 行ID超出注册表的。服务的拨号盘和按键挂在为其命名的插
// 槽下方（仅当两个服务命名相同的服务时，仅第一个插槽）。
//   web_search.dial.<service>.<dial> 选择（值 id
//                                      "" =服务默认值），或者免费-text拨号的文本
//                                      （空=默认）
//   web_search.key.<service> 秘密；始终为空，请参阅.cpp
inline constexpr std::string_view kDialPrefix = "web_search.dial.";
inline constexpr std::string_view kKeyPrefix  = "web_search.key.";

// 面板代表`c`。光标位于第一行（on/off 开关）。 `key_origin`
// 可能为空，在这种情况下，每个键行都读取 "not set" 并且对键开放。
// 持有（"stored"或"env: NAME"）锁其行并在值列中如此表示的键；
// 参见 add_service_rows。
[[nodiscard]] form::Form build_form(const web_search_cfg::Config& c,
                                    const settings::registry::EnvSnapshot& env,
                                    const KeyOrigin& key_origin = {});

// Write每个解锁行放回到`c`中。锁定的行（环境覆盖）
// 将被跳过，因此导出永远不会被 面板 显示的内容覆盖。
void apply_form(const form::Form& f, web_search_cfg::Config& c);

// 关键行所属的服务(""，如果`row_id`不是关键行)。
[[nodiscard]] std::string_view key_row_service(std::string_view row_id) noexcept;
// "Remove key"操作行所属的服务(""，如果不是的话)。
[[nodiscard]] std::string_view key_clear_service(std::string_view row_id) noexcept;


} // namespace agentty::web_search_form
