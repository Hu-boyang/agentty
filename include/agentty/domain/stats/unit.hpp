#pragma once
// agentty::stats-单位和format():ONE处的数字变成文本。
//
// 面板中的每个统计量都是双加一个单位。这个文件拥有翻译，
// 拥有它一次就是重点：没有它，令牌计数在一个选项卡上拼写为 "12.4k"，
// 在下一个选项卡上拼写为 "12400"，持续时间在这里是
// "3.2s"，那里是 "3200ms"，第十一个统计数据以第
// 十二个拼写出现。这种偏差在审查中是看不见的——每个网站本身看起来
// 都是正确的——并且在使用中很明显。
//
// 小部件(maya::StatSheet)故意使NOT这样做。
// 它需要字符串，因为单位属于拥有数字的域；
// 每当主机学习一个新单位时，知道千代币是什么的小部件
// 就需要一个新的枚举器。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace agentty::stats {

enum class Unit : std::uint8_t {
    Count,    // 1, 47, 1.2k — 圈数，调用，错误
    Tokens,   // 847、12.4k、1.3M — 与 Count 相同的坡道，根据意图命名
    Bytes,    // 512B，4.1KB — 1024-based
    Millis,   // 340ms、3.2s、4m12s-读取为持续时间，而不读取为数字
    Ratio,    // 0.62 → "62%"
    Rate,     // 1.2k/s
    Usd,      // $0.42
};

namespace detail {

// 由计数/令牌/速率共享的数千个斜坡。
//
// 10以下一位小数且没有一位小数以上："1.2k"和
// "12k"都适合四列，因此它们的表保持对齐而无需填充，
// 并且无论如何都不会读取在12k处下降的精度。
[[nodiscard]] inline std::string si(double v) {
    char buf[32];
    const double a = v < 0 ? -v : v;
    if (a < 1000.0) {
        // 整数打印为整数。 "47.0 turns" 表示
        // 计数不具有精度。
        if (a == std::floor(a)) {
            std::snprintf(buf, sizeof buf, "%.0f", v);
        } else {
            std::snprintf(buf, sizeof buf, "%.1f", v);
        }
    } else if (a < 1'000'000.0) {
        const double k = v / 1000.0;
        std::snprintf(buf, sizeof buf, (k < 10.0 && k > -10.0) ? "%.1fk" : "%.0fk", k);
    } else if (a < 1'000'000'000.0) {
        const double mm = v / 1'000'000.0;
        std::snprintf(buf, sizeof buf, (mm < 10.0 && mm > -10.0) ? "%.1fM" : "%.0fM", mm);
    } else {
        std::snprintf(buf, sizeof buf, "%.1fB", v / 1'000'000'000.0);
    }
    return buf;
}

// 持续时间读作持续时间。 214000 是一个数字；
// "3m34s"是一个关于你等了多久的事实，读者不应该有分歧。
[[nodiscard]] inline std::string duration(double ms) {
    char buf[32];
    if (ms < 0) ms = 0;
    if (ms < 1000.0) {
        std::snprintf(buf, sizeof buf, "%.0fms", ms);
    } else if (ms < 60'000.0) {
        const double s = ms / 1000.0;
        std::snprintf(buf, sizeof buf, s < 10.0 ? "%.1fs" : "%.0fs", s);
    } else if (ms < 3'600'000.0) {
        const int total = static_cast<int>(ms / 1000.0 + 0.5);
        std::snprintf(buf, sizeof buf, "%dm%02ds", total / 60, total % 60);
    } else {
        const int total = static_cast<int>(ms / 60'000.0 + 0.5);
        std::snprintf(buf, sizeof buf, "%dh%02dm", total / 60, total % 60);
    }
    return buf;
}

[[nodiscard]] inline std::string bytes(double b) {
    char buf[32];
    const double a = b < 0 ? -b : b;
    if (a < 1024.0)             { std::snprintf(buf, sizeof buf, "%.0f B", b); }
    else if (a < 1024.0 * 1024) { std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0); }
    else if (a < 1024.0 * 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
    } else {
        std::snprintf(buf, sizeof buf, "%.2f GB", b / (1024.0 * 1024 * 1024));
    }
    return buf;
}

}  // namespace detail

// 单一格式化器。面板显示的每个数字都经过这里。
[[nodiscard]] inline std::string format(Unit u, double v) {
    switch (u) {
        case Unit::Count:
        case Unit::Tokens:
            return detail::si(v);
        case Unit::Bytes:
            return detail::bytes(v);
        case Unit::Millis:
            return detail::duration(v);
        case Unit::Ratio: {
            char buf[16];
            // 整数百分比。份额被解读为比例，"61.7%" 花了一个
            // 专栏来讨论没人采取行动的精度。但非 -zero 份额
            // 永远不会打印为 "0%" — 与不允许舍入为空的条形
            // 规则相同："almost none" 和 "none"
            // 是不同的读数。
            const double pct = v * 100.0;
            if (pct > 0.0 && pct < 1.0) return "<1%";
            if (pct < 0.0 && pct > -1.0) return ">-1%";
            std::snprintf(buf, sizeof buf, "%.0f%%", pct);
            return buf;
        }
        case Unit::Rate:
            return detail::si(v) + "/s";
        case Unit::Usd: {
            char buf[24];
            const double a = v < 0 ? -v : v;
            // Sub-cent金额在廉价型号上是真实的，并且将它们四舍
            // 五入到"$0.00"报告了并非免费的免费转弯。
            std::snprintf(buf, sizeof buf, a < 0.01 && a > 0 ? "$%.4f" : "$%.2f", v);
            return buf;
        }
    }
    return detail::si(v);
}

}  // namespace agentty::stats
