#pragma once
// agentty::util::Callback-仅被调用的已发布钩子。
//
// 一个子系统发布一个设置观察者、一个list-changed钩子、一个teardown
// 动作一次、其他线程只有调用它。普通的 std::function 不会被冻结（
// 闭包可能会达到任何目标），
// 因此 maya::published<const std::function<..>> 被拒绝。下面列出了一种可行的情况：
// 被调用，从未被重新分配，并且闭包到达的是其所有者的以进行同步。
// 这里有一个 opt-in，而不是每个 调用 站点都有一个，并且 回调 的
// grep 会找到每个这样的钩子。

#include <functional>
#include <type_traits>
#include <utility>

#include <maya/runtime.hpp>

namespace agentty::util {

template <class Sig>
struct Callback {
    std::function<Sig> fn;

    Callback() = default;
    // 隐式的，如std::function的：回调落入其中。
    template <class F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, Callback>
                 && std::is_constructible_v<std::function<Sig>, F>)
    Callback(F&& f) : fn(std::forward<F>(f)) {}

    template <class... A>
    decltype(auto) operator()(A&&... a) const { return fn(std::forward<A>(a)...); }
    explicit operator bool() const noexcept { return static_cast<bool>(fn); }
};

} // namespace agentty::util

template <class Sig> MAYA_SENDABLE_T(agentty::util::Callback<Sig>);
template <class Sig> MAYA_FROZEN_T(agentty::util::Callback<Sig>);
