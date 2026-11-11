#pragma once
// agentty::catalog —该 用于每个提供商的单一捆绑模型地板。
//
// 历史上，每个提供商的离线"floor"列表位于不同的地方：seed_models
// ()(init.cpp)、人类传输层的内联回退、selection.cpp的bundled_models_for()
// 和per-provider bundled_models()(
// chatgpt/copilot/kimi)。四个以上的 hand-maintained
// 列表可以（并且确实）漂移 - 一个模型列出，但另一个模型没有列出，这正是新的
// -shipped 旗舰在一个 选择面板 表面上显示的方式，而不是融合的表面。
//
// 这是它们所有人的 唯一 SOURCE。每个需要楼层调用 catalog::bundled
// (provider_id)的站点； live /v1/models
// fetch 仍然取代它（地板是地板，而不是天花板）。功能与 id (
// ModelCapabilities::from_id) 保持 派生 — 该文件仅拥有
// 对应的 id，离线存在，永远无法执行任何操作。
//
// Newest-first:当用户未给出-m时，front()是合理的默认值。
// 真实目录太大/用户-defined无法猜测的提供商
// （开放路由器、自定义主机、本地）故意返回空。

#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/catalog.hpp"

namespace agentty::catalog {

// 为每个后缀 -capable 型号添加 `[1m]` long-context 同伴，
// 就在其基行之后。由捆绑楼层和 live-fetch 路径共享，
// 因此种子和 目录 一致。权利 self-heals 下游（如果帐户
// 不能 数据流 1M，则 context_1m_blocked 会删除行）。
inline void add_1m_variants(std::vector<ModelInfo>& v) {
    std::vector<ModelInfo> out;
    out.reserve(v.size() * 2);
    for (auto& mi : v) {
        const bool suffixable =
            ModelCapabilities::from_id(mi.id.value).supports_1m_suffix()
            && mi.id.value.find("[1m]") == std::string::npos;
        out.push_back(mi);
        if (suffixable) {
            ModelInfo one_m = mi;
            one_m.id = ModelId{mi.id.value + "[1m]"};
            one_m.display_name = mi.display_name + " (1M context)";
            one_m.context_window = 1'000'000;
            one_m.favorite = false;
            out.push_back(std::move(one_m));
        }
    }
    v = std::move(out);
}

// `provider_id`(==注册表 id/端点标签)的离线楼层，
// 最新-first。当没有合理的猜测时就为空。
[[nodiscard]] inline std::vector<ModelInfo> bundled(std::string_view provider_id) {
    auto mk = [&](const char* id, const char* name = nullptr,
                  int ctx = 200000, bool fav = false,
                  std::optional<bool> tools = std::nullopt) {
        return ModelInfo{ .id = ModelId{id},
                          .display_name = name ? name : id,
                          .provider = std::string{provider_id},
                          .context_window = ctx,
                          .favorite = fav,
                          .supports_tools = tools };
    };

    std::vector<ModelInfo> v;

    if (provider_id == "anthropic") {
        // 旗舰车道（Fable/Mythos 5)位于Opus上方； 4.5线是GA楼层。
        // 下面的 add_1m_variants() 提供 `[1m]` 同伴。
        v = { mk("claude-fable-5",  "Claude Fable 5",  200000, true),
              mk("claude-opus-4-5",   "Claude Opus 4.5",   200000, true),
              mk("claude-sonnet-4-5", "Claude Sonnet 4.5", 200000, true),
              mk("claude-haiku-4-5",  "Claude Haiku 4.5",  200000, false) };
        add_1m_variants(v);
    } else if (provider_id == "chatgpt") {
        // 仅在 /responses 上接受 Codex-enabled ChatGPT 帐户。
        v = { mk("gpt-5", "GPT-5", 272000, false, true) };
    } else if (provider_id == "copilot") {
        v = { mk("gpt-4o"), mk("gpt-4.1"), mk("o4-mini"),
              mk("claude-sonnet-4"), mk("gemini-2.5-pro") };
    } else if (provider_id == "kimi") {
        // Kimi K2 运送 256k 窗口。明确说明是因为
        // Kimi 的 /models 载荷 没有上下文长度，
        // 所以稍后不会覆盖它 - 将其保留在 200k 默认值上，
        // 在 -reported 窗口下默默地保持 56k，并使上下文量规（
        // 和压缩阈值）在每次 Kimi 回合时尽早触发。
        constexpr int k256 = 262144;
        v = { mk("kimi-k2-turbo-preview", nullptr, k256),
              mk("kimi-k2-0905-preview", nullptr, k256),
              mk("kimi-k2-0711-preview", nullptr, k256) };
    } else if (provider_id == "xai") {
        v = { mk("grok-4.6"), mk("grok-4"), mk("grok-code-fast-1"),
              mk("grok-3"), mk("grok-3-mini") };
    } else if (provider_id == "mistral") {
        v = { mk("mistral-large-latest"), mk("magistral-medium-latest"),
              mk("codestral-latest"), mk("mistral-medium-latest"),
              mk("mistral-small-latest") };
    } else if (provider_id == "gemini") {
        v = { mk("gemini-2.5-pro"), mk("gemini-2.5-flash"),
              mk("gemini-2.5-flash-lite"), mk("gemini-2.0-flash") };
    } else if (provider_id == "fireworks") {
        v = { mk("accounts/fireworks/models/kimi-k2-instruct"),
              mk("accounts/fireworks/models/deepseek-v3"),
              mk("accounts/fireworks/models/qwen3-235b-a22b"),
              mk("accounts/fireworks/models/llama-v3p3-70b-instruct") };
    } else if (provider_id == "deepseek") {
        v = { mk("deepseek-chat"), mk("deepseek-reasoner"),
              mk("deepseek-v4-pro"), mk("deepseek-v4-flash") };
    } else if (provider_id == "groq") {
        v = { mk("llama-3.3-70b-versatile"), mk("moonshotai/kimi-k2-instruct"),
              mk("qwen/qwen3-32b"), mk("llama-3.1-8b-instant") };
    } else if (provider_id == "cerebras") {
        v = { mk("llama-3.3-70b"), mk("qwen-3-235b-a22b-instruct-2507"),
              mk("llama3.1-8b") };
    } else if (provider_id == "together") {
        v = { mk("deepseek-ai/DeepSeek-V3"),
              mk("meta-llama/Llama-3.3-70B-Instruct-Turbo"),
              mk("Qwen/Qwen3-235B-A22B-Instruct-2507-tput") };
    }
    // openrouter、自定义主机和本地没有底线——
    // 太大/user-defined无法猜测；它们保持空状态，直到实时获取落地。
    return v;
}

} // namespace agentty::catalog
