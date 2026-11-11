#pragma once
// agentty::ui——由视图模块共享的小型纯助手。

#include <chrono>
#include <string>
#include <string_view>

#include <maya/style/color.hpp>
#include <maya/style/style.hpp>
#include <maya/dsl.hpp>
#include <maya/terminal/ansi.hpp>

#include "agentty/domain/ui_live.hpp"   // 思维偏好
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 线程 SHOW是否是该消息的推理块？
//
// 三个消费者必须对此达成一致，
// 并且他们处于三个不同的层：渲染块的视图、
// 要求动画帧的事件订阅以及不得重复它的活动指示器。
// 当他们不同意时，我们得到了一个可以绘制但从不动画的块，
// 以及一个 hex-tape "still working" 行堆叠在
// 已经在说同样的事情的推理块下。
[[nodiscard]] inline bool reasoning_shown(const Message& msg) noexcept {
    return ui_prefs::current().thinking != ui_prefs::Thinking::Hidden
        && !msg.reasoning_display_text().empty();
}

// 枚举反射——委托给agentty::to_string()。
[[nodiscard]] inline std::string_view profile_label(Profile p) noexcept { return to_string(p); }
[[nodiscard]] maya::Color profile_color(Profile p) noexcept;
[[nodiscard]] inline std::string_view phase_label(const Phase& p) noexcept { return to_string(p); }

// Status-bar样式——当前阶段的字形+动词形式，
// 并且选择终端颜色以一目了然地传达紧急情况。
[[nodiscard]] std::string_view phase_glyph(const Phase& p) noexcept;
[[nodiscard]] std::string_view phase_verb(const Phase& p) noexcept;
[[nodiscard]] maya::Color      phase_color(const Phase& p) noexcept;

[[nodiscard]] std::string timestamp_hh_mm(std::chrono::system_clock::time_point tp);

// 完整时间戳 — 选择面板 行的 "Mon DD HH:MM" (e.g."Jan 14 09:15")。
// 假设每个可见行都来自今年，则省略年份；通过 `%Y %b %d %H:%M`
// if/when 线程 添加它超过一年 -long 历史成为一个
// 真正值得关注的问题。
[[nodiscard]] std::string timestamp_full(std::chrono::system_clock::time_point tp);

// ── 原始字型 ────────────────────────────────────────────────
// 字母-spaced大写("S E C T I O N")-缺乏真正的small
// -caps的CLI工具中"this is a section header"的印刷简写。
// ASCII-only; non-ASCII 字节 不变地通过。
// 仅对短标签有用 - 长字符串会很快变宽（每个字符都会增加一个空格）。
[[nodiscard]] std::string small_caps(std::string_view s);

// Right-aligned固定-width整数。
// 用于任何就地更新的数字（令牌计数、持续时间、百分比、计数器），
// 以便周围的行永远不会水平跳舞。如果 `n` 比 `width` 宽，
// 则恢复到自然宽度。
[[nodiscard]] std::string tabular_int(int n, int width);

// 紧凑型、始终-5-display-column已过-time格式化程序。
// 选择幅度的最佳单位：
//     0.0–9.9 s → " 4.2s"（前导空格）
//   10.0–99.9 s  →  "12.3s"
//      100–599 秒 → " 234s"（整秒）
//        ≥600 s  →  " 9m05s"  (m/s)
//        ≥3600 s →  " >1hr"
// 稳定的宽度是重点——落入任何always-on指示器
// （标签阶段已过、工具持续时间等），并且周围的布局
// 不会随着值的变化而改变。
[[nodiscard]] std::string format_elapsed_5(float secs);

// 用于非-fixed-width表面的Variable-width紧凑的elapsed-time格式化程序
// (转动元、时间线title/footer)：
//   < 1 s  → "234ms"
//   < 60 s → "4.2s"
//   else →
// "1m20s" 在不需要精确宽度且 "234ms"/"4.2s"
// 比填充形式读取更好的任何非 -status-bar 表面中使用它。
[[nodiscard]] std::string format_duration_compact(float secs);

// 将任意模型id标准化为短的人类标签。
//
// THIN DELEGATE在域SSOT_`model_name::decode(id).full()`上。
// 保留为命名助手，因为它是视图层读入的词汇表，但它没有自己
// 的逻辑：族表、版本提取、date/`:latest`
// /`[1m]`处理和颜色策略都存在于
// `domain/model_name.hpp`中，
// 在那里得到证明并与转弯头文件、状态标签和欢迎屏幕共享。 不是否在此处添加
// 一个特殊情况 - 将其添加到解码器，每个表面都会获取它。
//
//   codellama:最新 → "Codellama" qwen2.5
//   -coder:7b → "Qwen2.5 Coder 7b"
//   openai/gpt-4o-mini → "GPT 4o Mini"
//   claude-sonnet-4-5[1m] → "Sonnet 4.5 · 1M"
//
// 注意最后一行：型号名称带有NO供应商前缀("Claude")
// 并且`[1m]`标记成为可见注释而不是被默默地丢弃。
// Provider 标识由提供者 标签 从 注册表 行渲染一次
// — 请参阅 model_name.hpp 了解为什么
// 供应商和提供者是独立的轴，不能混为一谈。
[[nodiscard]] std::string pretty_model_label(std::string_view model_id);

// 模型行的 该 规范人类标签 — 由 per-provider 选择面板、
// 融合的 "all providers" 选择面板、和 他们的 fuzzy
// -match 锚点使用，所以你的 SEE 是被匹配和突出显示的，
// 并且每个提供者 渲染 在两个视图中都是相同的。
//
// 又瘦代表：`model_name::decode(id, display_name).full()`。
//
// 这两个标签源被证明是不一致的：`id`是结构化且统一的("gpt-5.3-chat-latest")，
// 但是服务器`display_name`的范围从干净("Claude Sonnet 4.5")
// 到raw-cased("Hy-MT2-30B-A3B"，
// "gpt-image-1.5")到cruft-bearing(
// "GPT-5.3 Chat (latest)")——并且有时是与id(
// "Nano Banana Pro")无关的营销别名。解码器规范化它信任的任何来源，
// 首选 id-derived 形式，除非服务器名称携带 id 无法重建的信号。
[[nodiscard]] std::string model_display_label(std::string_view id,
                                              std::string_view display_name);

// 如上所述，但是当id具有1("igpu/Laguna Xs 2.1")时
// 以路由命名空间为前缀。仅对于 选择面板 的消歧路径 - 请参阅
// update/models.cpp. 中的碰撞注释
[[nodiscard]] std::string model_display_label_qualified(
    std::string_view id, std::string_view display_name);

// 给定模型id的上下文窗口大小。默认为 200 K，但当模型 ID 带有
// agentty-internal `[1m]` 标签（这会触发 传输 上的
// `context-1m-2025-08-07` beta）时，会升至 1 M。
// 由 status-bar ctx % 计算使用，因此在切换到带有旧 200
// K 上限的 1 M-window 模型后，条形图不会读取 "180 %"。
// 横档^W在模型选择面板中步进。 0 == "auto"（无覆盖；
// 回退到提供商广告或 ID 暗示的内容），并且故意设置为 首次，
// 因此循环从 "let agentty decide" 开始，
// 并且一整圈始终返回那里。
//
// 这些大小是网关实际服务的大小，而不是为了它们本身而取整的数字：32k/64k本地构建、
// 128k通用vLLM和OpenAI-compat默认、
// 200k Claude-class、272k Codex、400k GPT-
// 5-class、1M宽层LiteLLM用户最常触及2M的Gemini上限。
inline constexpr int kContextLadder[] = {
    0, 32'000, 64'000, 128'000, 200'000, 272'000, 400'000, 1'000'000, 2'000'000,
};

// 窗口的人工标签，匹配选择面板自己的格式（"auto"、
// "128k"、"1M")。
[[nodiscard]] std::string context_window_label(int tokens);

// 当NOTHING已知模型时假定的窗口——无用户覆盖、
// 无广告人物、无识别的id系列。
//
// 200k是刻意保守而不是乐观。这两种故障模式不是对称
// 的：猜测太 HIGH 让前缀增长超过端点接受的内容，
// 并且转弯在 传输 mid-conversation 上终止，
// 而猜测太 LOW 只会早于严格必要的时间进行压缩。
// 因此，未知情况采用可恢复错误。
inline constexpr int kDefaultContextWindow = 200'000;

// ── 背景窗口解析 ──────────────────────────────────
//
// 窗口驱动 ctx-% 读数、
// auto-compaction 触发器和压缩切片上限，
// 因此在两个方向上出错都是昂贵的：太小会默默地截断可以保存
// 整个转录本的模型，太大会让前缀超过端点接受的内容，
// 并且在 传输 上转向失败。
//
// FIVE来源，最-specific在前。
// 分层是整个设计——单一来源不可能适合每个部署：
//
//   1.用户的per-model覆盖（他们配置了网关）
//   2.什么提供商ADVERTISED(探测/api/show，或者
//                                             /v1/models 行中的窗口）
//   3. models.dev声明(limit.context来自缓存的
//                                             社区快照;
//                                             当端点什么也没说时
//                                             填补空白）
//   4.型号id含义（Claude/GPT系列、`[1m]`）
//   5.保守默认（一无所知）
//
// 注意2和3的顺序：来自将服务于请求的端点的LIVE
// 数字超过了根据id猜测的排名。两个网关后面的
// 相同名称可以由两个不同的窗口提供服务，并且只有网关
// 知道哪个。
//
// 当没有报告任何内容时，`advertised`为0——这就是为什么
// ModelInfo的context_window默认为0而不是数字的原因：
// "unknown"和"200k"不能是相同的值，或者真正的1M模型被无人选择的
// 默认值所限制。
//
// `env_max`是在启动时解析的AGENTTY_MAX_CONTEXT_TOKENS (Model::Env::
// max_context_tokens，0=未设置）：默认值之前的最后一个梯级。
// 通过了所以梯子上没有读取任何环境。
[[nodiscard]] int resolve_context_window(std::string_view provider_id,
                                        std::string_view model_id,
                                        int advertised,
                                        const store::Settings& settings,
                                        int env_max = 0) noexcept;

// 解析 AGENTTY_MAX_CONTEXT_TOKENS。当未设置或不是正整数
// 时为 0（"1M"、"abc"、"-5" 被拒绝而不是作为小窗口读取）。
[[nodiscard]] int max_context_tokens_from_env() noexcept;

// 存储覆盖的复合键。公开以便设置编写
// 者和解析者不能对拼写有异议。
[[nodiscard]] std::string context_override_key(std::string_view provider_id,
                                               std::string_view model_id);

// 用将使用 ACTUALLY 的窗口标记 目录 行。
//
// 选择面板的ctx列读取为ModelInfo::context_window；状态栏 调用
// resolve_context_window()。两条路线通向同一个号码，但他们却偏离了方向：
// 其网关未做广告的一行保持 context_window
// ==0 并呈现 "auto"，而状态栏——运行完整的梯子
// ——显示同一模型的 models.dev 数字。
// 用户看到一个屏幕与另一个屏幕关于他们试图验证的数字相互矛盾。
//
// 因此，装载机烘烤通过 此处，并且 `row.context_window` 是
// 根据梯子的构造而设计的。要求是 sail3r 的（PR #39）：
// 选择器上下文列与状态栏的数值由结构保证一致。
// 一个功能，所以未来调用网站都无法实现一半
// of it.
//
// 入口处的`row.context_window`是ADVERTISED
// 数字（0 =端点什么也没说），这正是梯子的梯级2想要的。
void bake_context_window(ModelInfo& row,
                         std::string_view provider_id,
                         const store::Settings& settings,
                         int env_max = 0) noexcept;

[[nodiscard]] int context_max_for_model(std::string_view model_id) noexcept;

// UTF-8助手。
[[nodiscard]] std::string utf8_encode(char32_t cp);
[[nodiscard]] int utf8_prev(std::string_view s, int byte_pos) noexcept;
[[nodiscard]] int utf8_next(std::string_view s, int byte_pos) noexcept;

// 标签-aware 变体:与utf8_prev/utf8_next相同，
// 但将任何作曲家附件占位符(\x01ATT:N
// \x01)视为单个导航单元。
// 光标从右侧进入关闭哨兵会跳转到打开哨兵之前；
// 从左侧进入开始哨兵会跳转到结束哨兵之后。
[[nodiscard]] int chip_prev(std::string_view s, int byte_pos) noexcept;
[[nodiscard]] int chip_next(std::string_view s, int byte_pos) noexcept;

// ── Model 列表顺序 ──────────────────────────────────────
// 提供者以 catalog/wire 顺序交还模型——有效地随机给读者。
// `model_order_less(a, b)` 为未查询的单一 -provider
// 列表提供了已融合的 "all providers" 视图的整洁、
// 人工排序。它不是 普通词法排序：版本号打破了这一点（"claude-opus-4-10"
// 对 之前 "...-4-8" 进行排序，因为 '1' < '8'；
// "gpt-10" 在 "gpt-2" 之前）。相反，它是所示标签上的
// NATURAL 比较器：
//   1. group by FAMILY——该标签的领先非-numeric运行（“Claude
//      Sonnet", "GPT”），case-folded — 因此所有十四行诗/所有
//      GPT-4 坐在一起，与 all-providers 视图的分类外观相匹配；
//   2.在一个族内，NEWEST 首次-数字块比较为
//      整数，降序，因此 4.6 ▷ 4.5 ▷ 4 和 10 ▷ 2（每个
//      "sort models by release" 请求背后的意图 — 当前
//      模型在顶部 — 在没有发布日期的情况下实现，local/custom
//      端点不携带发布日期）；
//   3.全案-folded标为决赛，稳定抢七。
// `a`/`b`是作为SHOWN的标签(pretty_model_label
// 输出或提供者display_name)。运行时的呼叫者 之前 会提升
// 收藏夹 — 这是一个故意的用户信号，优先于最近的信号。
[[nodiscard]] bool model_order_less(std::string_view a,
                                    std::string_view b) noexcept;

// 用于覆盖面板的实时搜索输入的硬件-caret 存储单元（
// 命令调色板/型号选择面板/融合选择面板查询行）。一个隐藏的 █，
// 其风格带有 caret_anchor meta-bit：maya
// 的内联序列化器将 真实 终端 光标移到其上，并将其显示为闪烁
// 条 - 因此 光标 会随着焦点移动（作曲家 → 调色板 → 返回），
// 而不是在 覆盖面板 打开时消失。 IME 候选窗口也锚定在
// 搜索字段中。直接将其附加在查询文本之后。 `accent`
// 通过 OSC 12 为 光标 着色（仅限 RGB；
// 在其他地方忽略 - 无害）。
// 序列化器中的Bottom-most-anchor-wins，
// 并且当任何覆盖面板打开时（composer_config中的panel::top门），
// 作曲家停止发射其锚点，因此每个帧恰好存在一个锚点。
//
// 形状1（BLINKING块）是有意且无条件的：搜索字段是空闲文本输入，
// 因此其光标完全像作曲家的空闲光标一样闪烁——在任何地方都相同，
// 包括在tmux下，并且不管终端自己的默认光标样式如何。
// 眨眼是 TERMINAL 的 (DECSCUSR)，
// 所以它的动画 帧 成本为零。无 tmux 特殊-case：尾声
// 停在右侧边缘并重置装饰，因此 tmux 的 copy-mode
// 光标落在空白填充上，而不是 mid-content（请参阅
// maya 的 emit_caret_epilogue）。一条代码路径，
// 一看，无处不在。
[[nodiscard]] inline auto query_caret(maya::Color tint) {
    return maya::dsl::text("\xe2\x96\x88",
                           maya::Style{}
                               .with_conceal()
                               .with_caret_anchor()
                               .with_caret_shape(1)   // 闪烁块
                               .with_fg(tint));
}

} // namespace agentty::ui
