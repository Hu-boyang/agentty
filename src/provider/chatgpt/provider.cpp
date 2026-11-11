#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/stream_epilogue.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "agentty/provider/wire.hpp"

#include <maya/runtime.hpp>

namespace agentty::provider::chatgpt {

// Codex提供者是一个瘦的、本机ChatGPT路径：它使用reverse-engineered
// OAuth登录直接与OpenAI响应后端对话(运行`agentty login`
// →ChatGPT)。 运行时 处有 NO `codex` 二进制程序，
// 并且没有 app-server 子进程 - 身份验证、令牌刷新和 流式传输 都发生在
// -process 中，与 Anthropic OAuth 路径完全相同。

struct ChatGptProvider::Impl {};

ChatGptProvider::ChatGptProvider() : impl_(nullptr) {}
ChatGptProvider::~ChatGptProvider() = default;

provider::StreamResult ChatGptProvider::stream(const maya::unlocked& cap, provider::Request req,
                                               provider::EventSink sink) {
    if (!responses_available()) {
        sink(StreamStarted{});
        sink(StreamError{
            "Not signed in to ChatGPT. Run `agentty login` and choose "
            "ChatGPT to authenticate, then retry."});
        return provider::StreamResult::failed("not signed in to ChatGPT");
    }
    return stream_responses(cap, std::move(req), std::move(sink));
}

// ── Model 发现 ───────────────────────────────────────────────
//
// 镜像代码x-rs：签名的-in ChatGPT账户对于其将接受哪个
// 模型段具有权威性，因此我们FETCH实时`/models` 目录 (
// fetch_models)并精确地表面那些——从来不是硬编码的猜测。
// 旧的 Baked-in 列表（gpt-5.1-codex …）是破坏的
// 原因：服务器停止提供这些 slugs 并拒绝了请求。 目录 已为进程缓存，
// 因此 选择面板 在第一次获取后保持即时状态；一个小捆绑的 回退
// 涵盖了离线/not-signed-in 情况，因此模型始终是可选的（第一回
// 合然后会显示上面的 "run `agentty login`" 提示）。
namespace {

std::vector<ModelInfo> bundled_models() {
    // 单捆目录。 必须 仅包含 Codex-enabled ChatGPT
    // 帐户在 /responses 上接受的段头；直播的目录才是真正的天花板。
    return catalog::bundled("chatgpt");
}

// 清空直到第一次成功的实时获取。
using Models = std::vector<ModelInfo>;
maya::snapshot<Models>& models_cache() { static maya::snapshot<Models> c; return c; }

} // 命名空间结束。

std::vector<ModelInfo> list_models_cached() {
    return models_cache().get();
}

std::vector<ModelInfo> list_models(Io io, const maya::unlocked& cap) {
    if (auto hit = list_models_cached(); !hit.empty()) return hit;

    // Ask该账户为其真实的目录(阻塞，短超时；
    // 任何失败就为空)。仅当我们实际拥有 凭据 时才尝试。
    std::vector<ModelInfo> resolved;
    if (responses_available()) {
        auto catalog = fetch_models(io, cap);
        // 服务器列出默认的第一个(或标志 is_default)；重新排序，
        // 以便帐户的默认值为索引 0 — 这是 选择面板 预先选择的。
        std::stable_sort(catalog.begin(), catalog.end(),
            [](const CatalogModel& a, const CatalogModel& b) {
                return a.is_default && !b.is_default;
            });
        for (const auto& cm : catalog) {
            resolved.push_back(ModelInfo{
                ModelId{cm.slug}, cm.display_name, "chatgpt",
                cm.context_window, false, true});
        }
    }

    if (resolved.empty()) resolved = bundled_models();
    else   // 缓存仅真实，server-confirmed列出
        models_cache().store(resolved);
    return resolved;
}

// 账户的默认模型段(目录索引0)，或者当无法到达目录时安全的回退。
// 这是 selection/defaulting 应该使用的，而不是任何硬
// 编码的 "gpt-5.1-codex"。
std::string default_model(Io io, const maya::unlocked& cap) {
    auto ms = list_models(io, cap);
    return ms.empty() ? std::string{"gpt-5"} : ms.front().id.value;
}

} // namespace agentty::provider::chatgpt
