#pragma once
// RAG模式选择面板-一个决定：主动(pre-turn)检索如何表现。
//
// 从命令选项板(Ctrl+K→"RAG")打开它。三种模式：
//
//   在 每个 转动之前注入检索到的上下文 仅在 线
//   程 的第一转动时注入（接地，然后关闭） 关闭 无主动注入（
//   search_docs/search_code 仍然工作）
//
// ↑↓移动，Enter/Space/←→选择，Esc关闭。
// 该选择持续到 settings.json 并实时应用。高级检索旋钮不再位于
// UI 中 - 它们保持默认设置（高级用户为 env-tunable）。
//
// 仅UI-state； update/rag_settings.cpp 中的 状态更新函数，在 view/pickers.cpp. 中查看

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "agentty/rag/embed_backend.hpp"
#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/settings_registry.hpp"
#include "agentty/store/store.hpp"   // store::RagMode

namespace agentty {

namespace rag_settings {

// ── 嵌入子-form ────────────────────────────────────────
// 检索过去被硬连线到本地Ollama守护进程，
// 该守护进程锁排除任何不能运行守护进程（锁定的-down工作机器）
// 或已经具有内部嵌入网关的人。后端现在是 user-chosen，
// 并且根据所有内容均可从 TUI 进行配置的要求，
// 选择 此处 而不仅仅是通过环境变量。
//
// 面板仅拥有form::Form：导航、编辑、
// 下拉菜单和键映射均来自共享组件层，并且maya::
// Form拥有每个字形。这里剩下的是唯一真正嵌入
// -specific的知识——后端需要WHICH行，以及行如何映射
// 回EmbedConfig。
//
// 行是来自所选后端的 派生（参见 rows_for）：自定义端点显示
// host/port/model/key，in-process 后端显示 model
// -file 路径。该表单永远不会显示不能影响所选后端的行，因此不存在
// "which of these actually applies?" 歧义。
//
// 探针是加载-bearing行。嵌入维度是 派生，
// 绝不是 带类型的：rag-cpp 的 HNSW 默默地丢弃
// 大小与索引不一致的向量，因此猜测的维度会产生一个空索引，
// 根本没有错误。因此，测试连接位于有效配置的路径上。
struct EmbedForm {
    rag::embed::EmbedConfig cfg;

    // 行+光标+焦点+脏，全部来自共享模型。
    form::Form form;

    // 探针生命周期。保持 面板 关注点而不是表单关注
    // 点：这是通用表单无法表达的 一个 事物，
    // 因为它的结果反馈到配置（测量的维度）中。
    struct Idle {};
    struct Testing {};
    struct Ok   { std::uint32_t dim; int latency_ms; };
    struct Failed { std::string why; };
    using Probe = std::variant<Idle, Testing, Ok, Failed>;
    Probe probe{Idle{}};

    // 下一个TestDone可以回答哪个探头LAUNCH。
    // 探针在 工作线程 上运行；在启动和完成之间，
    // 用户可以 EDIT 配置（这会使探测器无效）或再次启动。
    // 代不匹配的完成是关于不再存在的配置的 过期 答案 -
    // 如果没有标记，它将覆盖 `dim` 并针对探测器从未见过的
    // 字节 标记 面板 已验证 -Ok。与登录的 attempt_id
    // 形状相同。
    std::uint64_t probe_gen = 0;

    [[nodiscard]] bool dirty() const noexcept { return form.dirty; }
};

// 字段ID。命名常量是因为 状态更新函数 按 id 寻址
// 行（当后端更改行集时，行索引会发生变化）。
inline constexpr const char* kFieldMode      = "mode";
inline constexpr const char* kFieldBackend   = "backend";
inline constexpr const char* kFieldModel     = "model";
inline constexpr const char* kFieldHost      = "host";
inline constexpr const char* kFieldPort      = "port";
inline constexpr const char* kFieldTls       = "tls";
inline constexpr const char* kFieldPath      = "path";
inline constexpr const char* kFieldModelPath = "model_path";
inline constexpr const char* kFieldTokenizer = "tokenizer";
inline constexpr const char* kFieldApiKey    = "api_key";
inline constexpr const char* kFieldTest      = "test";

// 构建配置后端的表单。纯 — 单元 -testable，不含 UI。
// `mode`是主动-retrieval策略，呈现为第一行：它和嵌入
// 器是一个问题（"how does retrieval behave here?")的两半，
// 并且将它们分成两个覆盖面板使得第二个问题
// 不可发现。
//
// `rag`提供管道旋钮，它们是来自settings_registry::
// kSettings的GENERATED，而不是在此处列出——添加旋
// 钮就是添加表行，并且该面板在不进行编辑的情况下拾取它。
// `advanced`揭示了用户无法轻易判断其副作用的行。
[[nodiscard]] form::Form build_form(const rag::embed::EmbedConfig& c,
                                    store::RagMode mode,
                                    const store::Settings& settings = {},
                                    const ::agentty::settings::registry::EnvSnapshot& env = {},
                                    bool advanced = false);

// 将行读回到配置中（build_form的逆）。
[[nodiscard]] rag::embed::EmbedConfig config_from_form(
    const rag::embed::EmbedConfig& base, const form::Form& f);

// 读回模式行。
[[nodiscard]] store::RagMode mode_from_form(const form::Form& f,
                                            store::RagMode fallback);

// 将 注册表-generated 行读回到 RagConfig 中。
// 遍历 build_form 遍历的同一个表，因此无法写入从未读取过的行。
void apply_form_to_settings(const form::Form& f, store::Settings& settings);

struct Closed {};
struct Open {
    // 光标是MODE本身，而不是kMode的索引。存储索引意味
    // 着每次读取都是 `kModes[i]` — 一个未经检查的
    // 数组下标，如果列表重新排序或调整大小，其含义会悄然改变。
    store::RagMode cursor = store::RagMode::On;
    store::RagMode active = store::RagMode::On;   // 持久化模式（行标记）

    // 面板的形式。 不 可选：没有表单的表单 -backed 覆盖
    // 面板 拥有键盘，无法回答任何问题，当应用程序冻结时，它会在屏幕上读取。
    // 该状态是可达的，而 `embed` 是由延迟的 Cmd 填充的
    // 可选状态；现在是在施工中建造的，所以"open but inert"
    // 不具有代表性。
    EmbedForm embed;

    // 显示Tier::Advanced行——用户一眼无法判断其
    // 副作用的旋钮(数字调整、路由策略)。被 DEFAULT 隐藏，
    // 并非不存在：将它们埋在环境变量中是使它们一开始就无法
    // 被发现的原因，而列出 30 个滑块的设置屏幕本身就是
    // 不可用的。用 ^A 切换； 面板 重建了它的形状。
    bool advanced = false;
};

// 行，按照显示顺序——1:1与store::RagMode。
// 一个 位置记录了布局：视图在其中移动，光标在其中移动。
inline constexpr store::RagMode kModes[] = {
    store::RagMode::On, store::RagMode::FirstTurnOnly, store::RagMode::Off,
};
inline constexpr int kModeCount =
    static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));

// 光标移动结束于枚举——没有 调用 站点拥有模数。
[[nodiscard]] constexpr store::RagMode next_mode(store::RagMode mmode,
                                                 int delta) noexcept {
    int i = 0;
    for (int k = 0; k < kModeCount; ++k)
        if (kModes[k] == mmode) { i = k; break; }
    const int n = kModeCount;
    return kModes[((i + delta) % n + n) % n];
}

} // namespace rag_settings

using RagState =
    std::variant<rag_settings::Closed, rag_settings::Open>;

[[nodiscard]] inline bool rag_settings_is_open(const RagState& s) noexcept {
    return std::holds_alternative<rag_settings::Open>(s);
}
[[nodiscard]] inline rag_settings::Open*
rag_settings_opened(RagState& s) noexcept {
    return std::get_if<rag_settings::Open>(&s);
}
[[nodiscard]] inline const rag_settings::Open*
rag_settings_opened(const RagState& s) noexcept {
    return std::get_if<rag_settings::Open>(&s);
}

} // namespace agentty
