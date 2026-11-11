#pragma once
// agentty::util::toml_lite — 一个小型 TOML
// 读取器，用于导入其他工具的配置（Codex 的 config.toml）。
// 涵盖表、点分键、带引号的键、表数组、内联表、数组、所有四种字符串形式、
// 整数、浮点数和布尔值。日期以字符串形式返回。不是验证者。

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace agentty::util::toml_lite {

// 解析 `text`变成JSON对象。如果出现语法错误，则返回 false 并将 `error`
// 设置为 "line N: what"。
[[nodiscard]] bool parse(std::string_view text, nlohmann::json& out, std::string& error);

}  // namespace agentty::util::toml_lite
