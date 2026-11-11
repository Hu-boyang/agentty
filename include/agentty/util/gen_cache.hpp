#pragma once
// agentty/util/gen_cache.hpp — 被清除
// 后慢速填充无法覆盖的缓存值。
//
// 获取在进入网络之前读取gen()并用store_if(值，
// gen)存储。如果 clear() 在两者之间运行，则存储将被删除，
// 因此在失效之前获取的列表永远不会落在其之后。

#include <cstdint>
#include <utility>

#include <maya/runtime.hpp>

namespace agentty::util {

template <class T>
class GenCache {
public:
    [[nodiscard]] T get() const { return slot_.load()->value; }
    [[nodiscard]] std::uint64_t gen() const { return slot_.load()->gen; }

    // Store `v` 仅当自读取 `seen` 以来没有任何内容清除 缓存 时。
    // 返回是否着陆。
    bool store_if(T v, std::uint64_t seen) {
        // update()可能是重试和正文，所以结果来自于它最终
        // 存储的存储单元，而不是来自于正文内设置的标志。
        auto stored = slot_.update([](const Cell& cur, const T& next, std::uint64_t g) {
            return cur.gen == g ? Cell{next, g, true} : Cell{cur.value, cur.gen, false};
        }, std::move(v), seen);
        return stored->landed;
    }

    // 清空缓存并且使已经在进行中的每个提取无效。
    void clear() {
        slot_.update([](const Cell& cur) { return Cell{T{}, cur.gen + 1, false}; });
    }

private:
    struct Cell { T value{}; std::uint64_t gen = 0; bool landed = false; };
    maya::snapshot<Cell> slot_;
};

}  // namespace agentty::util
