#pragma once
// agentty::i18n——启动：选择语言并加载目录。
//
// 从 i18n.hpp 分离，因此核心不受 getenv 和 IO 的影响，
// 因此测试可以使用显式输入驱动 resolve_language()，
// 而不是通过改变进程环境。

#include <string_view>

#include "agentty/i18n/i18n.hpp"

namespace agentty::i18n {

// 给定标志和已保存设置，使用哪种语言。
//
// 优先级，最高优先：
//
//   1. --lang用户带类型的 THIS运行什么
//   2. settings.json ui.lang，当非-empty时(空="auto")
//   3.环境LC_ALL、LC_MESSAGES、LANGUAGE、LANG
//   4. en
//
// Pure：将其输入作为参数而不是读取设置，
// 因此可以在没有文件系统或环境的情况下测试优先级。
[[nodiscard]] Lang resolve_language(std::string_view cli_lang,
                                    std::string_view saved);

// 加载嵌入目录并激活解析语言。
//
// 仅当 ENGLISH 目录 未能达到 解析 时才返回
// false，这是构建缺陷而不是 运行时 条件——没有
// 目录 的请求语言不是错误，它会默默地回退到英语（选择
// 面板 显示完整性，因此在可操作的地方可见间隙）。
bool init(std::string_view cli_lang, std::string_view saved);

}  // namespace agentty::i18n
