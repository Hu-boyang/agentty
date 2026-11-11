// web_search_policy.cpp ——过程 -wide web_search 政策档次。

#include "agentty/tool/web_search_policy.hpp"

#include "agentty/io/persistence.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/snapshot.hpp"

#include <exception>

namespace agentty::tools::web_search_policy {

namespace {

util::AtomicSnapshot<web_search_cfg::Config>& slot() {
    static util::AtomicSnapshot<web_search_cfg::Config> s;
    return s;
}

} // 命名空间结束。

void seed(Io io) {
    // 从 settings.json ,一次,通过一个可能读取磁盘的入口点。
    // 从不覆盖一个值 面板 已经安装。
    if (slot().load()) return;
    web_search_cfg::Config c;
    try {
        c = persistence::load_settings(io).web_search;   // 应用内存覆盖
    } catch (const std::exception& e) {
        util::dbglog("web_search_policy.seed", e.what());
    } catch (...) {
        util::dbglog("web_search_policy.seed", "non-std exception");
    }
    // 原子: a 面板 在读盘运行时安装该区域。
    (void)slot().store_if_empty(std::move(c));
}

web_search_cfg::Config current() {
    const auto s = slot().load();
    return s ? *s : web_search_cfg::Config{};
}

void install(web_search_cfg::Config c) {
    slot().store(std::move(c));
}

} // namespace agentty::tools::web_search_policy
