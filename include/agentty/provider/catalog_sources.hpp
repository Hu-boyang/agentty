#pragma once
// agentty::provider-"things that have models"的ONE枚举。
//
// 融合模型选择面板从用户实际可以数据流的每个后端提取：他们登录的内置-in预设，
// 以及已保存自定义主机（原始"host:port"、Ollama、
// llama.cpp、私有网关）。这份清单从未被写下来。
// `refresh_fused_sources` re-derived 它内联，
// 并且很长一段时间它枚举了 仅 预设 - 因此 已保存 自定义主机没有 目录，
// 其实时 /v1/models 获取落在 `available_models` 中，
// 而 选择面板 没有显示任何内容。对于这些用户来说，菜单只是空白。
//
// 提供者PICKER已经吸取了这个教训：provider_rows.hpp说
// 合并列表本身是一个值。
// 构建一次，因此状态更新函数和视图不能对存在哪些行存在分歧。
// CATALOG 层从未得到同样的待遇，这正是为什么源可能被遗忘
// 并且没有人注意到发布的原因。
//
// 所以：在此声明一次。添加后端类型意味着编辑这个文件，而不是其他任何
// 东西——每个消费者都会自动遵循，
// "we forgot to enumerate X" 不再是一类错误，而是一个需要记住的东西。
//
// ── 为什么不重新使用ProviderRow? ──────────────────────────────────────
// ProviderRow回答了一个不同的问题：提供者PICKER
// 显示了什么，其中包括ACP代理（没有要列出的模型——它们
// 拥有自己的模型选择）和"Custom host…"哨兵（
// 根本不是后端）。 目录 源是可以回答模型查询的子集。
// 两个问题，两种类型，一种对它们的共同部分的共同推导。

#include <string>
#include <vector>

#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/store/store.hpp"

namespace agentty::provider {

// fused 选择面板可以列出其型号的后端。
struct CatalogSource {
    // 身份稳定，每ProviderCatalog都存储有密钥。
    // 对于预设，这是其 注册表 id ("anthropic")；对于自定义主机，它是用户 带
    // 类型的（"api.my-gw.com"、"localhost:11434"）的规范。
    std::string id;
    // 用户所看到的。预设的标签，或主机的已解析
    // 显示名称。
    std::string label;
    // 当这来自 注册表 时确实如此。需要行本身的消费者可以使用
    // preset_for(id) 进行查找 - 保留为 标志 而不是指针，
    // 以便结构保持可复制和 生命周期-free。
    bool is_preset = false;
    // Un-authed预设显示为QUERY-GATED标志-in提供而不是目录：
    // 在没有帐户的情况下键入"mistral"应该提供标志-in而不是死胡同。
    // 自定义主机永远不会处于这种状态 - 持有为其进行身份
    // 验证的 已保存 密钥 IS。
    bool needs_signin = false;
};

// fused 选择面板可以列出型号的所有内容，按显示顺序：
// 首先是注册表预设（注册表顺序），然后是已保存自定义主机（已排序）。
//
// 采用由 saved_custom_hosts() 处理，
// 它将 RESOLVES 的任何规范丢弃到预设行上 —
// "api.githubcopilot.com" 是副驾驶预设，而不是其旁边的第二个后端。
// 如果没有这一点，消费者在此处为每个元素构建一个条目将为一个端点生成两个
// 目录 并显示其模型两次。
[[nodiscard]] inline std::vector<CatalogSource> catalog_sources(
    const store::Settings& settings, const auth::AuthView& view) {
    std::vector<CatalogSource> out;
    const auto presets = providers();
    out.reserve(presets.size() + settings.provider_keys.size());

    for (const auto& p : presets) {
        out.push_back(CatalogSource{
            .id           = std::string{p.id},
            .label        = std::string{p.label},
            .is_preset    = true,
            .needs_signin = auth_source(p, settings, view) == AuthSource::None,
        });
    }
    for (const auto& spec : saved_custom_hosts(settings.provider_keys)) {
        out.push_back(CatalogSource{
            .id           = spec,
            .label        = provider_display_name(parse_selection(spec)),
            .is_preset    = false,
            .needs_signin = false,
        });
    }
    return out;
}

} // namespace agentty::provider
