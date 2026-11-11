// web_search_plan.cpp - 看见了 头文件.

#include "agentty/tool/web_search_plan.hpp"

#include "agentty/tool/web_search_policy.hpp"
#include "agentty/tool/web_search_secret.hpp"

#include <algorithm>
#include <chrono>

namespace agentty::tools::web_search_plan {

mcp::tools::WebSearchPlan build_plan(const web_search_cfg::Config& c,
                                  const KeyLookup& key_of) {
    ::mcp::tools::WebSearchPlan plan;
    // 两个下-case字母或无:值最终是请求.
    if (c.language.size() == 2 && c.language[0] >= 'a' && c.language[0] <= 'z'
        && c.language[1] >= 'a' && c.language[1] <= 'z')
        plan.language = c.language;

    for (const std::string* id : {&c.primary, &c.secondary, &c.fallback}) {
        if (id->empty() || *id == "none") continue;
        const auto* svc = ::mcp::tools::find_web_search_service(*id);
        if (!svc) continue;
        const bool dup = std::any_of(plan.slots.begin(), plan.slots.end(),
            [&](const ::mcp::tools::WebSearchSlot& s) { return s.service == *id; });
        if (dup) continue;

        ::mcp::tools::WebSearchSlot slot;
        slot.service = *id;
        if (svc->needs_key && key_of) slot.api_key = key_of(*id);
        const std::string prefix = *id + ".";
        for (const auto& [k, v] : c.dials)
            if (k.size() > prefix.size() && k.compare(0, prefix.size(), prefix) == 0)
                slot.dials.emplace_back(k.substr(prefix.size()), v);
        plan.slots.push_back(std::move(slot));
    }
    // 无可用(所有三个未确定或未知): 回到密钥上 -less
    // 默认而非离开 web_search 无法回答。
    if (plan.slots.empty()) {
        auto d = ::mcp::tools::default_web_search_plan();
        d.language = plan.language;
        return d;
    }
    return plan;
}

namespace {

struct LiveSource final : ::mcp::tools::WebSearchPlanSource {
    ::mcp::tools::WebSearchPlan plan() const override {
        auto p = build_plan(web_search_policy::current(),
                            [](std::string_view s) { return web_search_secret::load(s); });
        p.now = std::chrono::system_clock::now();   // mcp-cpp 读作无时钟
        return p;
    }
};

} // 命名空间结束。

std::shared_ptr<::mcp::tools::WebSearchPlanSource> make_plan_source() {
    return std::make_shared<LiveSource>();
}

} // namespace agentty::tools::web_search_plan
