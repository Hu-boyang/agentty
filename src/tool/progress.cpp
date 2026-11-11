// progress.cpp — tools::CallContext 等着吧
//
// 环境本身是一个纯价值 分派器 建设和手到 ToolDef::execute ;
// 这里没有任何信息 -thread.

#include "agentty/tool/registry.hpp"

#include <functional>
#include <memory>
#include <stop_token>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

namespace agentty::tools {

std::shared_ptr<std::stop_source> CallContext::merged_stop() const {
    // 来源和 回调 那次旅行 它一起生活 一起死去
    struct Merged {
        std::stop_source src;
        std::vector<std::unique_ptr<std::stop_callback<std::function<void()>>>> cbs;
    };
    auto m = std::make_shared<Merged>();
    for (const auto& t : cancel)
        m->cbs.push_back(std::make_unique<std::stop_callback<std::function<void()>>>(
            t, std::function<void()>{[s = &m->src] { s->request_stop(); }}));
    return std::shared_ptr<std::stop_source>(m, &m->src);
}

bool CallContext::wait(std::chrono::milliseconds d, std::stop_token also) const {
    auto merged = merged_stop();
    std::optional<std::stop_callback<std::function<void()>>> also_cb;
    if (also.stop_possible())
        also_cb.emplace(also, std::function<void()>{[merged] { merged->request_stop(); }});
    return maya::delay_for(capability(), merged->get_token(), d) || cancelled();
}

} // namespace agentty::tools
