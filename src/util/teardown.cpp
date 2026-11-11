// teardown.cpp ——过程 -exit 加入( E) 注册表。见 teardown.hpp.

#include "agentty/util/teardown.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/util/logx.hpp"
#include "agentty/util/callback.hpp"

namespace agentty::util::teardown {

namespace {

struct Entry {
    Token                         token;
    std::string                   name;
    util::Callback<void(const maya::unlocked&)> fn;   // 援引,从未重新分配
};

// 复制 -on-write: register/cancel 以比较方式发布新列表 -and-swap.
// 条目是关闭它们拆掉的(通常通过参考)任何内容,
// 因此它们不是可以发送的值,无法通过锁;
// 公布的列表是诚实的形状。注册是罕见的,列表很短。
struct Registry {
    std::vector<Entry> actions;
    Token               next_token = 1;
};

// 函数 -local 静态: 无静态 -init-order
// 依赖任何可能从自己的静态构造器中注册的子系统。
// 在静态破坏中,由于延迟注册而泄漏,仍然发现它还活着。
maya::published<const Registry>& registry() {
    static auto* r = [] {
        auto* p = new maya::published<const Registry>;
        p->publish(std::make_shared<const Registry>());
        return p;
    }();
    return *r;
}

// 应用 `edit` 以复制本清单并公布, 重试 如果先
// 有其他人发布,则返回 `edit` 返回时,
template <class F>
auto update(F edit) {
    for (;;) {
        auto cur  = registry().current();
        auto next = std::make_shared<Registry>(*cur);
        auto out  = edit(*next);
        if (registry().publish_if(cur, std::move(next))) return out;
    }
}

} // 命名空间结束。

Token on_shutdown(std::string name, teardown::Action action) {
    if (!action) return 0;
    return update([&](Registry& r) {
        const Token token = r.next_token++;
        r.actions.push_back(Entry{token, name, {action}});
        return token;
    });
}

void cancel(Token token) noexcept {
    if (token == 0) return;
    try {
        (void)update([token](Registry& r) {
            return std::erase_if(r.actions, [token](const Entry& a) { return a.token == token; });
        });
    } catch (...) { /* 复制短名单的 内存 中:留下它 */ }
}

void run(const maya::unlocked& cap) noexcept {
    // 换入空列表(保留符牌计数器),然后运行:一个动作可能
    // 注册另一个动作,或者通过某种破坏器再次到达run(),
    // 并且不等待任何东西.
    std::vector<Entry> actions;
    try {
        actions = update([](Registry& r) { return std::exchange(r.actions, {}); });
    } catch (...) { return; }

    // LIFO — 一个后来注册的子系统可能依赖于早期的子系统,
    // 所以它首先被撕毁. 和静态破坏一样的纪律.
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        try {
            if (it->fn) it->fn(cap);
        } catch (const std::exception& e) {
            AGT_LOG(General, Warn, "teardown",
                    "{} threw during shutdown: {}", it->name, e.what());
        } catch (...) {
            AGT_LOG(General, Warn, "teardown",
                    "{} threw a non-std exception during shutdown", it->name);
        }
    }
}

std::size_t pending() noexcept {
    return registry().current()->actions.size();
}

} // namespace agentty::util::teardown
