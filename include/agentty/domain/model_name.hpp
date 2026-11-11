#pragma once
// agentty::model_name —关于如何将模型ID呈现给人类的
// SINGLE事实来源。
//
// ── 为什么有这种东西 ──────────────────────────────────────────────────────
//
// 问题"what do we call this model on screen?"
// 曾经由六个独立的解析器来回答：
//
//   1.ui::pretty_model_label在'-_ :/'→"Claude Opus 4.8"上代币化
//   2. ui::model_display_label 协调的服务器名称与 ID
//   3.maya::ModelBadge::resolve()SUBSTRING查找("opus")→{"Opus"，洋红色}
//   4.maya::ModelBadge::extract_version()数字-run扫描→"4.8"
//   5.ui::speaker_style_for自身goto-based版本扫描
//   6.ModelCapabilities::from_id位置分词器→系列枚举
//
// 一个问题的三个系列分类器和三个版本提取器。
// 他们不同意，并且分歧是用户-visible:作曲家标签说"Opus"，而轮次头文件
// 说"Opus 4.8"，而选择面板说"Claude Opus 4.8"——相同型号，
// 三根弦。 `[1m]` 扩展上下文从徽章中消失。 Haiku 在一个表面上是绿色的，
// 而 bright_cyan 在另一个表面上是绿色的。 Fable/Mythos
// - 在 catalog.hpp 中宣布为旗舰​​车道 - 完全从 speaker_style
// 的 if-chain 中脱落，并使用供应商前缀进行渲染，没有使用其他
// 任何东西。
//
// 根本原因是LAYERING INVERSION。
// ARCHITECTURE.md 表示主机构建配置值，
// maya 拥有每个像素；反之亦然——maya 不得做出 SEMANTIC 决定。
// 原始 ID 中的小部件 解析 供应商分类法无法访问 目录，
// 没有保守证明，并且在新系列发布时没有理由进行更新。它总是会漂移。
// PROVIDER_HETEROGENEITY.md已经诊断出传输的这种确切疾病：
// "a model fact in a transport is a drift bomb"。 WIDGET 中的
// 模型事实是同一枚炸弹。
//
// ── 设计 ───────────────────────────────────────────────────────────
//
// ONE解码器→结构化值→呈现-only投影。
//
//   decode(id, server_name) -> ModelName{ family, version, qualifier,
//                                         annotation, color }
//
// 以及 DIFFER 仅 IN HOW MUCH THEY DROP 的三个预测：
//
//   full() "Opus 4.8 · 1M" 选择面板行，
//   调色板medium() "Opus 4.8"转头文件，
//   作曲家标签 tiny() "Opus"最窄棚横档
//
// 有两个属性使其稳健而不仅仅是"one more function"：
//
//   ·将DELEGATES分类法添加到ModelCapabilities::from_id而不是添加
//     第七解析器。 from_id 已经拥有位置标记器、家族 枚举（包括
//     Fable/Mythos）、generation/revision 和
//     [1m] 标记。此 头文件 消耗 `caps` 并且仅添加
//     DISPLAY 关注点。添加一个族涉及 from_id 和一个颜色表 -
//     不是一个小部件，也不是一个视图。
//
//   ·棚梯是一个解码值的PROJECTION，而不是不同的
//     代码路径。旧的紧凑型徽章通过从不同的分支中掉出而放弃了版本，
//     这就是信息丢失的隐藏方式。删除字段的投影会在一个可见的、
//     可测试的位置执行此操作 - 并且引脚 tiny() ⊑
//     medium() ⊑ full() 下方的校样，因此没有任何梯级
//     可以与更丰富的梯级相矛盾。
//
// ── 强健:我们从未见过的模型 ─────────────────────────────
//
// 硬性要求不是当今 ID 的正确性——测试语料库固定这些。
// 我们看到 不 的 id 降级为相同的 SHAPE，
// 因此在此构建之后启动的模型看起来是原生的，而不是损坏的。
// 用户遇到品牌 -new 旗舰产品是 UI 开始发出原始
// 传输 id 或不一致的排版的最糟糕时刻。
//
// 因此，解码器具有产生相同结构的TWO路径：
//
//   已知家族 → 家族表中的名称，版本来自 from_id 未知家族 →
//   name/version/qualifier 从 id 本身恢复，绝大多数遵循
//                    `[vendor-]name-version [-qualifier…]`
//
//
// 两者都剥离供应商前缀，删除 快照 日期和别名指针，并填充相同的三个字段——
// 因此 `claude-quasar-6` 读取 "Quasar 6" 与
// `claude-opus-4-8` 读取 "Opus 4.8" 完全相同，
// 比任何人将 Quasar 添加到 枚举 早几个月。将一个系列添加到 `Family`
// 就是 OPTIMISATION（它购买了车道颜色和功能门），而不是修复损坏标签的错误。
//
// `decode`是TOTAL：每个输入，包括对抗性输入（"---"、":::"、
// "[1m]"、"4-5")，产生非-empty可渲染名称并且从不抛出。
// 空模型 标签 读取为错误，而不是 "unknown model"。
//
// ── id是NOT信任的, 以"20世纪"为例, ─────────────────────────────────────────────
//
// Id来自提供商API和用户配置。每当端点是（自定义主机、受损聚合器、
// 打字错误-squatted代理）时，`/v1/models`响应
// 就是attacker-controlled，并且它直接流向终端，
// 即INTERPRETS所传递的内容。在此边界上有两个保证，
// 因此 下游 不必重新-check，并且 调用 站点也不会忘记：
//
//   ·任何输出字段中的NO CONTROL BYTES。包含 "\x1b[31m" 的 ID
//     否则将发出实时转义序列——重新着色 UI、
//     清除屏幕或从用户从未见过的字符串中破坏 帧
//     带类型的。那是终端注射，不是外观缺陷。
//   ·BOUNDEDLENGTH。 4 KB "name" 是针对所有服务的拒绝服务
//     宽度-shed梯子下游。截断是可见的（UTF-8 边界上的省略号），
//     因此奇怪的标签读取为 "too long" 而不是 "wrong"。
//
// 这两条规则都是故意愚蠢的——丢弃字节、帽长度——并且恰好应用于一个阻塞点。
// Multi-byte UTF-8 未受影响地通过。
//
// ── 何谓是故意的 不 此处:供应商 ────────────────────────────────
//
// 较早的草案携带来自id的`vendor`字段("Claude"，
// "GPT")re-derived。这是错误的，其原因值得说明，
// 因为这是练习的重点。
//
// PROVIDER(服务于字节——registry.hpp行："Anthropic"、"Groq"、
// "Copilot")和VENDOR(训练模型——Anthropic产生Claude)是
// INDEPENDENT轴。 bundled_catalog.hpp 证明了这一点：Copilot
// 服务于 gpt-4o，claude-sonnet-4 和 gemini-2.5-pro。
// 它们仅在 `anthropic` 行上重合，这就是为什么很容易错过这种区别的原因。
//
// 来自 id 的供应商字符串 re-derived 将是
// FOURTH 事实来源，位于已经来自 注册表 的提供商标签旁边，
// 没有任何东西强迫他们同意 — 正是该文件消除的漂移。
// 所以：没有供应商字段。 "Claude " 前缀被简单地删除。
// 副驾驶下的"Opus 4.8" 标签是明确和诚实的；副驾驶 标签 下的
// "Claude Opus 4.8" 主动邀请误读您正在与
// Anthropic 交谈。 Provider 身份由提供商 标签 从 注册
// 表 行呈现为 一次 — 从未从模型 ID 推断出来。

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <maya/style/color.hpp>

#include "agentty/domain/catalog.hpp"

namespace agentty::model_name {

// ── 家庭颜色表 ──────────────────────────────────────────────────
//
// ONE表，因此俳句实际上不能在一个表面上是绿色而在另一个表面上是青色。
// 值是调色板的 ROLE 标记（持久化 身份），而不是 STATUS 标记 -
// palette.hpp 的 "one hue = one axis" 规则
// 意味着绿色意味着 "ok"，并且不能同时意味着 "Haiku"（这种冲突就是
// 俳句首先移出绿色的原因）。
//
// 开关是 EXHAUSTIVE，没有默认值：添加系列枚举器失败到 编译，
// 直到它被赋予色调。这是替换 "remember to update the widget too"
// 的 编译-time 证明。
[[nodiscard]] constexpr maya::Color color_of(
    ModelCapabilities::Family f) noexcept {
    using F = ModelCapabilities::Family;
    using S = maya::ThemeSlot;
    // SLOTS，不是文字。家庭色调是 ROLE（"flagship"、
    // "workhorse"、"fast"），角色正是主题分配的颜色。
    // 从字面上看，这是屏幕上最后一个仍在 all-RGB 方案上以
    // 名为 ANSI 绘制的东西 — 一个洋红色徽章，无论用户选择什么，
    // 它都保持洋红色。
    //
    // 槽还使该文件对其层保持诚实：域代码现在可以在
    // 不触及像素的情况下命名角色，并且分辨率在绘制
    // 时发生在maya中。
    switch (f) {
        // 旗舰巷——强调色调，主题为"this is special"。
        case F::Opus:   return maya::Color::slot(S::Accent);
        case F::Fable:  return maya::Color::slot(S::Accent);
        case F::Mythos: return maya::Color::slot(S::Accent);
        // 主力——主要色调。
        case F::Sonnet: return maya::Color::slot(S::Primary);
        // Fast/agile-链接，每个方案中的青色-family槽。
        // 不 成功：绿色是 status_ok 并与 ✓ 完成图标碰撞。
        case F::Haiku:  return maya::Color::slot(S::Link);
        // OpenAI gpt-5.x 响应行。
        case F::Gpt:    return maya::Color::slot(S::Info);
        // 未知家族(本地模型、聚合器id、我们还不知道的行)。
        // 中性信息——它仍然是一个模型，只是我们不能将其
        // 放置在车道上。
        case F::Unknown: return maya::Color::slot(S::Info);
    }
    return maya::Color::slot(S::Info);   // 不可达；开关是详尽的
}

// 已知家族的规范显示拼写。未知为空 - 调用者会
// 退回到 id-derived 标签，这是应该查询
// 名称的原始 id 的 仅 路径。
[[nodiscard]] constexpr std::string_view family_label(
    ModelCapabilities::Family f) noexcept {
    using F = ModelCapabilities::Family;
    switch (f) {
        case F::Haiku:   return "Haiku";
        case F::Sonnet:  return "Sonnet";
        case F::Opus:    return "Opus";
        case F::Fable:   return "Fable";
        case F::Mythos:  return "Mythos";
        case F::Gpt:     return "GPT";
        case F::Unknown: return "";
    }
    return "";
}

// ── ModelName — 解码值 ────────────────────────────────────────
//
// 每个字段都是PRESENTATION-READY：已经是标题-cased，
// 已经去除了快照日期/：最新/量化噪声。没有消费者 re-parses。
struct ModelName {
    // 家庭，或者当家庭未知时，id-derived姓名。
    // "Opus"·"Sonnet"·"GPT"·"Qwen2.5 Coder"·"Nano Banana Pro"。
    // 对于非-empty ID，NEVER为空。
    std::string name;

    // 标准化点版本-"4.8"、"5.6"、"2.5"。
    // 当 id 不包含任何内容或仅包含 快照 日期（出处，
    // 而不是身份）时为空。
    std::string version;

    // 遵循系列+版本的产品限定符："Codex Max"、"Chat"、
    // "Sol"。普通 变体 为空。这是 IDENTITY，
    // 而不是装饰 — gpt-5.1 和 gpt-5.1-codex-max
    // 是不同的产品，将两者折叠为 "GPT 5.1" 会使 选择面板 行
    // 模糊不清您将要支付的费用。
    std::string qualifier;

    // context/variant注释改变了你的BUYING，
    // 而不仅仅是你所称的："1M"/"2M"扩展上下文。
    // 空为普通变体。这就是为什么 选择面板 必须能够区分地显示
    // 两个 -identical 行。
    std::string annotation;

    // 网关在id前面放置的路由命名空间：`igpu/laguna-xs-2.1`
    // 中的"igpu"。当 id 不带任何内容时为空。
    //
    // 通常装饰 — `openrouter/anthropic/claude-3-5-haiku`
    // 是相同的产品，但您到达它时，显示路径会在每一行中产生噪音。
    // 但 self-hosted 网关使用前缀作为 LANE：两台 llama.cpp
    // 服务器前面的 Bifrost 将
    // `dgpu/granite-4.0-h-tiny` 路由到
    // 8GB 离散 GPU，将 `utils/granite-4.0-h-tiny`
    // 路由到 CPU。相同的重量，不同的机器，截然不同的速度 - 并且前缀将
    // 渲染 都删除为 "Granite 4.0 H Tiny"，因此 选择面板
    // 显示两行相同的行，您无法判断要在哪一行上运行。
    //
    // 因此，它被保留为 FACT，并且 full() 仅当它是唯一区分两
    // 行时才显示它（参见 needs_namespace / full()）。
    // 这样，常见的情况保持干净，模糊的情况保持诚实——与上面的
    // `annotation` 的推理相同。
    std::string ns;

    // 家族色调来自上表。主题 SLOT，就像 color_of()
    // 返回的所有内容一样 - 这里的默认值是文字，会悄悄地将任何 un
    // -decoded 模型固定到一个调色板。
    maya::Color color = maya::Color::slot(maya::ThemeSlot::Info);

    // 当 ID 解码为 KNOWN 系列时为真。当为 false 时，
    // `name` 来自 id/server-name 标准化器而不是族表，
    // 并且 `version` 为空（在未知 id 中运行的版本是不
    // 可靠的 - 它可能是尺寸标签、日期或定量级别）。
    bool family_known = false;

    // ── 预测 ──────────────────────────────────────────────────────
    // 棚梯。每个梯级的下降幅度都严格大于最后一个梯级；
    // 引脚 tiny() ⊑ medium() ⊑ full() 下方的
    // 保守证明，因此狭窄的表面永远不能陈述与宽表面相矛盾的东西。

    // "Opus 4.8 · 1M"——我们所知道的一切。 选择面板 行，命令面板。
    [[nodiscard]] std::string full() const;

    // "igpu/Laguna Xs 2.1" — full()，以路由命名空间为前缀。
    //
    // 对于必须消除歧义的一个表面：选择面板列出了多个目录，
    // 其中两个条目解码为相同的标签。知道自己的行是唯一的调用
    // 者应该使用 full()；存在这种情况是为了使不明确的
    // 情况具有拼写，而不是默认的拼写。
    [[nodiscard]] std::string qualified() const;

    // "Opus 4.8"/"GPT 5.1 Codex Max"-
    // 仅删除注释。转头文件，作曲标签。版本和限定符都保留：单独的
    // "Opus" 无法区分 4.5 和 4.8（原始 bug），
    // 单独的 "GPT 5.1" 无法区分 Codex 产品和基础产品。
    [[nodiscard]] std::string medium() const;

    // "Opus" / "GPT 5.1" — 也删除了限定符。
    // 最窄的横档，用于棚子状态栏，长名称会将行推到边缘。
    [[nodiscard]] std::string tiny() const;

    [[nodiscard]] bool operator==(const ModelName&) const = default;
};

// ── 解码器 ──────────────────────────────────────────────────────────
//
// `id` wire/selection型号id，可能携带agentty `[1m]`/`[2m]`标记、
//                提供者命名空间（"openrouter/ anthropic/claude-opus-4")
//                或Ollama `:tag`。
// `server_name`是供应商在发送时针对该型号自己的display_name。
//                使用 仅 恢复 id 无法重建的营销别名（
//                "Nano Banana Pro" 为
//                Gemini-3-pro-image）；仅是 id 的
//                re-cased 拼写形式的名称将被忽略，取而代之的是 id
//                -derived 形式，因此同一模型在不同的提供者中的读取方式相同。
//
// 总计：对于非-empty ID，从不抛出、从不返回空的`name`。
[[nodiscard]] ModelName decode(std::string_view id,
                               std::string_view server_name = {});

// ── 证据 ───────────────────────────────────────────────────────────────
//
// 与tool/spec.hpp和provider/registry.hpp相同的习惯用法：
// 保持表诚实的不变量是由编译器检查的，而不是通过记忆来检查的。
namespace proofs {

inline constexpr std::array<ModelCapabilities::Family, 7> kAllFamilies{
    ModelCapabilities::Family::Unknown, ModelCapabilities::Family::Haiku,
    ModelCapabilities::Family::Sonnet,  ModelCapabilities::Family::Opus,
    ModelCapabilities::Family::Fable,   ModelCapabilities::Family::Mythos,
    ModelCapabilities::Family::Gpt,
};

// 每个KNOWN家族都有非-empty标签。忘记拼写的新
// 枚举器会导致构建失败，而不是 渲染 变成 ""。
consteval bool every_known_family_is_labelled() {
    for (auto f : kAllFamilies) {
        if (f == ModelCapabilities::Family::Unknown) continue;
        if (family_label(f).empty()) return false;
    }
    return true;
}
static_assert(every_known_family_is_labelled(),
              "a Family enumerator has no display label — add it to family_label()");

// 任何已知的家庭都不能借用STATUS槽。 palette.hpp的学科是"one hue = one axis"；
// success/warning/error 表示 ok/warn/error，仅此而已。
// 这是Haiku-as-green所违反的规则。
//
// 现在比较SLOTS，color_of()返回它们。
// 如果将其保留为文字，则此检查将悄然变得空虚——没有插槽等于文字，
// 因此无论如何它都会通过，这比没有它更糟糕。
consteval bool no_family_uses_a_status_hue() {
    const maya::Color banned[] = {
        maya::Color::slot(maya::ThemeSlot::Success),
        maya::Color::slot(maya::ThemeSlot::Warning),
        maya::Color::slot(maya::ThemeSlot::Error),
    };
    for (auto f : kAllFamilies)
        for (const auto& b : banned)
            if (color_of(f) == b) return false;
    return true;
}
static_assert(no_family_uses_a_status_hue(),
              "a family colour collides with a status slot (success=ok / "
              "warning=warn / error=error) — pick a role slot instead");

// 族标签是成对不同的，因此两个泳道决不能将渲染视为同
// 一个词。
consteval bool family_labels_are_unique() {
    for (std::size_t i = 0; i < kAllFamilies.size(); ++i) {
        const auto a = family_label(kAllFamilies[i]);
        if (a.empty()) continue;
        for (std::size_t j = i + 1; j < kAllFamilies.size(); ++j)
            if (a == family_label(kAllFamilies[j])) return false;
    }
    return true;
}
static_assert(family_labels_are_unique(),
              "two Family enumerators share a display label");

} // namespace proofs

} // namespace agentty::model_name
