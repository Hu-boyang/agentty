#pragma once
// agentty::ThreadDelta-线程的一个保存，如状态更新函数所描述的。
//
// 保存用于按值承载整个线程。在长 线程 上，该副本（每个工具 调用 的参数
// JSON 源码树，每个输出）每轮工具花费循环 线程 ~20 毫秒。
// 增量仅包含自上次 保存 和 状态更新函数 描述以来可能发生的更改：由
// persistence::describe_save 构建，
// 由 persistence::save_thread_delta 运行。

#include <cstddef>
#include <cstdint>
#include <vector>

#include "agentty/domain/conversation.hpp"

namespace agentty {

struct ThreadDelta {
    Thread                     header;   // 除 `messages` 之外的每个字段
    std::size_t                from = 0; // 可能不同的第一持久消息
    std::vector<Message>       tail;     // 持久消息[来自，结束)
    std::vector<std::uint64_t> fps;      // 每个持久消息的指纹
};

} // namespace agentty
