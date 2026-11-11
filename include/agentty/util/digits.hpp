// agentty/util/digits.hpp——附加一位十进制数字而不溢出。
//
// 我们从文本中获取的数字解析（工具输出、差异头文件、git numstat、
// 版本字符串、模型ID）来自外部。长数字运行中的 `v = v * 10 + d`
// 溢出有符号整数，即 UB。取而代之的是饱和。
#pragma once

#include <limits>
#include <type_traits>

namespace agentty::util {

template <class T>
[[nodiscard]] constexpr T push_digit(T v, char c) noexcept {
    static_assert(std::is_integral_v<T>);
    constexpr T kMax = std::numeric_limits<T>::max();
    const T d = static_cast<T>(c - '0');
    if (v > (kMax - d) / 10) return kMax;
    return static_cast<T>(v * 10 + d);
}

static_assert(push_digit(12, '3') == 123);
static_assert(push_digit(std::numeric_limits<int>::max() / 10, '9') == std::numeric_limits<int>::max());
static_assert(push_digit(std::numeric_limits<int>::max(), '0') == std::numeric_limits<int>::max());

}  // namespace agentty::util
