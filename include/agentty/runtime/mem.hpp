#pragma once
// 内存-pressure释放助手。
//
// 长会话积累数百个MiB的小分配:每个工具结果字符串、
// 视图缓存中的每个Element-tree节点、
// 每个划痕JSON解析出传输。  当用户采取释放“
// 大”连续块的操作时——压缩对话（删除所有先前的工具输出）、
// 切换到/启动新的 线程（释放之前的 线程 的
// 视图 缓存）——字节 返回到 分配器 的空闲列表，
// 但不一定返回到内核。  尽管程序在逻辑上要小得多，
// 但 RSS 仍保持高电平。
//
// `release_to_kernel()`是明确的轻推。
// 它是有意限定范围的：我们不会在每个 Tick 上对其进行
// 调用（会破坏堆），而仅在用户刚刚执行了释放兆字节会话
// 状态的操作的几个粗略-grained 边界处。
//
//   ·mimalloc→mi_collect(真):强制收集并要求OS
//                    回收未使用的页面。这是默认的获取路径。
//   ·glibc→malloc_trim(0):当mimalloc被禁用时回退。
//   ·其他→在没有便携式装饰API的系统分配器上没有-op。
//
// 该函数被声明为`inline`并且存在于该头文件中，
// 因此平台分支根据TU被解析在编译-time处；
// 无 链接-time 分派 开销。

#if defined(AGENTTY_USE_MIMALLOC)
#  include <mimalloc.h>
#elif defined(__GLIBC__)
#  include <malloc.h>
#endif

#include <cstddef>
#include <cstdlib>
#include <optional>

namespace agentty {

inline void release_to_kernel() noexcept {
#if defined(AGENTTY_USE_MIMALLOC)
    mi_collect(true);
#elif defined(__GLIBC__)
    ::malloc_trim(0);
#else
    // musl/macOS/Windows上的No-op。
#endif
}

// 调用 启动时一次。 mimalloc 将释放的页面保留 10 毫秒，
// 然后再归还，并且 工作线程 线程 上释放的页面会等待 线
// 程 再次分配，因此突发工作（较长的 线程 的 回滚缓冲区
// 重播）会驻留。根据 帧 测量，立即归还它们的速度并不慢。
// MIMALLOC_PURGE_DELAY 仍然覆盖它。
inline void tune_allocator() noexcept {
#if defined(AGENTTY_USE_MIMALLOC)
    if (!std::getenv("MIMALLOC_PURGE_DELAY"))
        mi_option_set(mi_option_purge_delay, 0);
#endif
}

// mimalloc通过callback/printing API而不是
// 稳定的带类型的 快照公开统计数据，因此保持该便携式助手明确不可用。
struct memory_stats_unavailable_t {};
inline std::optional<memory_stats_unavailable_t> memory_stats() noexcept {
    return std::nullopt;
}

} // namespace agentty
