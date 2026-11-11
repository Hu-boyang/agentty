#pragma once
// 用于Claude工具-use参数的鲁棒读取器。容忍 流式传输
// 模型在 JSON 漂移时发出的常见形状：
//   ·缺失字段→默认
//   ·空值→默认或空选择
//   ·字符串槽中的numbers/bools→通过dump()强制
//   • int 槽中的 "42" → 解析 为 42
//   • 字符串槽中的字符串数组 → newline-join（write-tool 习惯用法）
// 目标是避免可恢复模型错误的红色错误卡，
// 同时仍将 "genuinely required field missing" 显示为显式错误。

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace agentty::tools::util {

class ArgReader {
public:
    explicit ArgReader(const nlohmann::json& args) noexcept : args_(args) {}

    [[nodiscard]] bool is_object() const noexcept { return args_.is_object(); }

    // Alias-aware:当模型发送`file_text`、`text`、`body`等时，
    // `has("content")`为真。必须匹配`raw()`/`str()`语义，
    // 或者在`has`上门控的调用者(e.g.写入所需的-content检查)将虚假地
    // 拒绝有效的别名-shaped 调用。
    [[nodiscard]] bool has(std::string_view key) const {
        return raw(key) != nullptr;
    }

    // 缺失/空->定义。 number/bool -> 强制。 array ->
    // newline-join 元素（匹配 write-tool 的习惯用法）。
    // 当发生强制转换时，`note` 设置为非 -empty human-readable
    // 字符串，因此调用者可以将其回显到模型而不显示错误。
    [[nodiscard]] std::string str(std::string_view key,
                                  std::string def = {},
                                  std::string* note = nullptr) const;

    // 与 str() 类似，但如果键缺失、为 null，
    // 或者在强制转换后生成空字符串，则返回 nullopt。对于真正需要
    // 的领域。
    [[nodiscard]] std::optional<std::string> require_str(std::string_view key) const;

    // 解析 "42" 字符串，截断双精度数。当 missing/null 或 解析
    // 失败时返回 def。
    [[nodiscard]] int integer(std::string_view key, int def) const;

    // 接受"true"/"false"/"1"/"0"字符串。
    [[nodiscard]] bool boolean(std::string_view key, bool def) const;

    [[nodiscard]] const nlohmann::json* raw(std::string_view key) const noexcept;

private:
    const nlohmann::json& args_;
};

} // namespace agentty::tools::util
