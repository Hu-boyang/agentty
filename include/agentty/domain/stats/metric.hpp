#pragma once
// agentty::stats — 度量和部分：行词汇。
//
// 指标是一个数字加上表示如何拼写的单位加上分母，
// 即份额 OF。三个字段，因为面板中的每个统计
// 数据都需要这三个字段，并且没有统计数据需要
// 第四个字段。
//
// `of` 是 CARRIED，而不是在视图中重新计算。
// 两个独立分配的消费者在最后一位数字上意见不一致，而一个小组的
// 份额之和不等于一，那么这个小组就不会被任何人二次信任。

#include <cstdint>
#include <string>
#include <vector>

#include "agentty/domain/stats/unit.hpp"

namespace agentty::stats {

struct Facts;   // 归并 输入；提取器读取它

struct Metric {
    // 一个字符串字段，不是 string_view + 所有者。真正的标签是
    // 模型 ID 和工具名称 - 少于 23 个字符，因此 SSO
    // 意味着用于分割它们的 "allocation" 参数从来都不是真实的，
    // 描述一个名称的两个字段是这个 代码库 不断删除的形状。
    std::string label;
    Unit        unit  = Unit::Count;
    double      value = 0;
    double      of    = 0;      // 0 = 无共享，无栏
    // 尾随暗淡的音符。它在工作表中拥有自己的列，
    // 因此注释可以对齐，而不是在不同宽度的值后面参差不齐地拖尾。
    std::string detail;
    // 乐队或甜甜圈部分的片段：整体的各个部分。
    //
    // `hue` 索引 一个 OF 两个 调色板，
    // 哪个是部件含义的属性，而不是部件数量的属性。
    // STATE（缓存 命中/未命中、完成/失败）具有值得编码的
    // 语义 - 绿色是好的，红色不是。 NAME（工具、模型）没有，
    // 将 `grep` 涂成红色会表明它失败了。
    // `categorical` 表示提取器正在索引哪个表，
    // 因此视图永远不必根据值进行猜测。
    struct Part { std::string label; double value = 0; int hue = 0; };
    std::vector<Part> parts;
    bool categorical = false;
    // Spark / Plot 部分的示例。
    std::vector<double> series;

    [[nodiscard]] double share() const noexcept {
        return of > 0 ? value / of : -1.0;
    }
};

// 如何绘制剖面。这命名了提取器填充的 FIELDS，
// 而不是调用了哪个小部件 - 它们中的每一个都位于相同的
// maya::StatSheet 中，这就是保持行彼此对齐的原因。
enum class Viz : std::uint8_t {
    Kv,     // 标签 + 值（无栏）
    Bars,   // 标签+价值+分享栏（排名）
    Spark,  // 标签+值+趋势条（内联，一行）
    Band,   // 一个完整的-width组合栏+图例
    // 与RING相同的组成。带状和甜甜圈在相反的方向上失败，
    // 这就是两者存在的原因：带状是精确和紧凑的，
    // 但 1-column 条子很容易被错过，
    // 而环则使眼睛与 ANGLES 相比，其中一个薄楔子
    // 与圆圈相对应。使用环当作文 IS 选项卡的答案，
    // 环当它是几个事实之一。
    Donut,
    Plot,   // 多-row braille/block图
    Hero,   // 标题图及其回答的句子
    // DISTRIBUTION：每个占用的延迟桶占一行，
    // 因此 SHAPE 是可见的，而不是它的两个分位数。 95 个快速
    // 调用和 5 个慢速调用与 100 个中等调用具有相同的平均值，
    // 并且意味着完全不同的东西 - 这在 "p50 / p95" 中是不可见的，
    // 但在绘制桶时就很明显。
    Dist,
    // 与VERTICAL直方图相同的分布：每个桶一列而不是一行。
    // Dist 是精确的并且永远滚动，这在桶标签值得阅读时是正确的；
    // Hist 是眼睛立即看到的单一形状，
    // 当问题是 "what does this distribution look like"
    // 而不是 "how many landed in 64-128ms" 时，
    // 这是正确的。
    Hist,
};

struct Section {
    std::string_view heading;   // 空 = 无标题行
    Viz              viz;
    // 写入调用者-owned临时向量，因此稳定的-state 帧
    // 不分配任何内容。
    void (*extract)(const Facts&, std::vector<Metric>& out);
};

}  // namespace agentty::stats
