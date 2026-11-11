#include "agentty/runtime/app/deps.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/registry.hpp"

#include <maya/runtime.hpp>
#include <stdexcept>
#include <variant>

namespace agentty::app {

namespace {
Deps* g_deps = nullptr;
// 现场认证头文件。由主机的 InstallAuth 副作用 在循环
// 线程 上写入，由 工作线程 读取（子代理，目录 获取，
// 每个 数据流）。 快照，而不是 锁：它的读取次数远远多于写入次数，
// 并且来自其他 锁' 正文（提供者 缓存），因此这里的 锁 需要
// 高于所有这些的排名。 锁-free，它没有排名，也不能死锁。
maya::snapshot<auth::AuthHeader>& g_auth() {
    static maya::snapshot<auth::AuthHeader> a;
    return a;
}
}

auth::AuthHeader live_auth() { return g_auth().get(); }

const Deps& deps(Io) {
    if (!g_deps) throw std::logic_error("agentty::app::deps() called before install_deps()");
    return *g_deps;
}

void install_deps(Deps d) {
    static Deps storage;
    g_auth().store(d.auth);
    storage = std::move(d);
    g_deps = &storage;
}

auth::AuthHeader auth_snapshot(Io io, const provider::Selection& sel) {
    // 通过中央凭据层从给定提供者解析，因此凭据永远不会偏离它
    // 所针对的提供者。这是 "what auth goes on the wire"
    // 的唯一事实来源：如果交换机更改了活动的
    // provider/model 但代码路径忘记重新安装
    // 头文件，这仍然返回 RIGHT 提供商的 凭据（
    // 这类缺陷曾使 Anthropic 的 OAuth 令牌被发给 Mistral，导致 401。
    //
    // Anthropic和hosted-key/custom-host提供商在此解析真实的头文件；
    // oauth_native (ChatGPT/Copilot/Kimi) 和本地
    // 解析为空，它们的 传输层 提供令牌 - 与 update_auth 的 缓存 相同，
    // 我们将其保留为 fast/fallback 路径。
    const std::string pid =
        sel.kind == provider::Kind::OpenAI ? sel.openai_endpoint.label
                                           : std::string{provider::default_provider_id()};
    auto resolved = provider::credentials::resolve(io, pid);
    if (!auth::bearer_token(resolved).empty()
        || std::holds_alternative<auth::BearerHeader>(resolved))
        return resolved;
    // 空解析(oauth_native/本地)
    // -回退到安装的login/switch流的缓存的头文件(由那些提供商的传输层使用)。
    return live_auth();
}

// 脱离循环(没有Model的工作线程):针对选择的PUBLISHED
// 副本进行解析，其中分派 接口边界保持等于Model的副本。
auth::AuthHeader auth_snapshot(Io io) {
    return auth_snapshot(io, provider::active());
}

void update_auth(Io io, auth::AuthHeader auth) {
    g_auth().store(auth);
    tools::subagent::set_auth(io, std::move(auth));
}

} // namespace agentty::app
