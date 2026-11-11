#pragma once
// agentty::ui::step——移动列表光标的唯一规则。
//
// 单步(↑/↓，j/k)环绕：最后一行上的↓落在第一行上，
// 并且第一行上的↑落在最后一行上。更大的步长（PgUp/PgDn
// 以 ±N 形式发送）会在末尾停止，因为从底部跳到顶部会丢失您的位置。
// 每个列表都使用这个，所以它们都有相同的感觉。

#include <algorithm>

namespace agentty::ui {

[[nodiscard]] constexpr int step(int index, int delta, int n) noexcept {
    if (n <= 0) return 0;
    if (delta == 1 || delta == -1) return ((index + delta) % n + n) % n;
    return std::clamp(index + delta, 0, n - 1);
}

// 同样，跳过行`skip(i)`表示无法保持光标（头文件部分，
// 信息行）。如果没有其他行符合条件，则保持不变。
template <class Skip>
[[nodiscard]] constexpr int step_over(int index, int delta, int n, Skip skip) noexcept {
    if (n <= 0) return 0;
    if (delta != 1 && delta != -1) {
        int i = std::clamp(index + delta, 0, n - 1);
        const int back = delta > 0 ? -1 : 1;   // 落在靠近边缘的真实行上
        for (int g = 0; g < n && skip(i); ++g) {
            const int next = i + back;
            if (next < 0 || next >= n) break;
            i = next;
        }
        return skip(i) ? index : i;
    }
    int i = index;
    for (int g = 0; g < n; ++g) {
        i = ((i + delta) % n + n) % n;
        if (!skip(i)) return i;
    }
    return index;
}

} // namespace agentty::ui
