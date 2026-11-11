#pragma once
// agentty/util/lock_levels.hpp-agentty的锁被采纳的顺序，
// 如TYPES和编译器检查。
//
// 每个maya::guarded<T, Rank>命名其等级。采取它需要有能力证明每个 锁
// 线程 都拥有 LOWER (jaal core/capability.hpp) 的排名：
//
//     const maya::unlocked&线程 没有任何内容 const
//     maya::held<R>&a 锁 正文 排名 R const
//     maya::below<R>& 参数表明：调用方只持有等级小于 R 的锁。
//
// 采用 锁 等级 R (直接或通过任何它 调用) 的函数声明
// `const maya::below<R>&`，并且其调用者
// 必须能够产生一个。因此，从排名 >= R 的 锁 内部
// 生成的 调用 不会是 编译，无论它位于 调用 图中的哪个位置。
// 下表为顺序；添加嵌套边缘意味着将内部的 锁 放置在外部的上方，
// 并且 编译器 检查每个调用者。
//
// 大多数 锁 是叶子（maya::leaf，默认）：它们的 正文 不带任何东西。
// 读取次数远多于其改变的值（UI首选项、目录事实、
// 主动选择）是maya::snapshot：没有锁，没有等级，
// 可以从任何地方读取，包括视图和锁 正文。
//
// 最外层第一。数字之间有间隔，以便新的排名适合在两个之间。

#include <maya/runtime.hpp>

namespace agentty::lock_levels {

// 序列化整个MCP连接+发布轮次；一轮读取 OAuth 令牌、
// 服务器列表、里面的工具 目录 和 持久化。
using mcp_connecting = maya::rank<10, "mcp.connecting">;

// 副驾驶的 Auto-session 缓存。通过 /models/session
// 探针举行，因此并发回合共享一个探针，并且该探针需要一个新的代理令牌。
using copilot_auto = maya::rank<12, "copilot.auto_session">;

// 每个提供商（Claude、Copilot、Kimi、Codex、
// MCP OAuth）一次进行一次 凭据 刷新。它的 正文 读取和写入
// 凭据 存储（叶子）并进行网络刷新。
using auth_refresh = maya::rank<15, "auth.refresh">;

// 一个凭据文件的read-modify-write通过网络刷新保持，
// 因此其他进程采用该令牌而不是再次刷新。
// 从提供商的刷新通道内部进入，因此它的排名高于提供商。
using cred_file = maya::rank<17, "auth.cred_file">;

// RAG索引：保存了整个retrieve/refresh，
// 其读取里面的技能目录、内存存储以及其他缓存。
using rag_index = maya::rank<20, "rag.index">;

// 内存范围文件的read-modify-write（获取内部的id rng）。
// 通过 RAG 重新索引读取，因此它排名高于索引。
using memory_store = maya::rank<25, "memory.store">;

// 一个线程的日志(.jsonl/.ofs/.meta.json)，
// 同时漏极工作线程写入它。 线程索引的泳道（file_lane）被取入内部，因此排名靠后。
using thread_log = maya::rank<28, "persistence.thread_log">;

// 共享-file通道：两个agentty实例共享的文件的一次读取
// -modify-write（线程索引、设置）。它的正文采用cross
// -process文件锁并且可以读取持久化 缓存。
using file_lane = maya::rank<30, "persistence.file_lane">;

// 背景工作线程的槽：其正文触及其工作线程耗尽的状态。
using worker_slot = maya::rank<35, "worker_slot">;

// 说明正文接触的车道或槽位，其本身就是外部锁，
// 除了离开之外什么也没有。
using writer_state   = maya::rank<40, "persistence.writer_state">;
using settings_cache = maya::rank<40, "settings_cache.state">;

// 参数类型，拼写一次。具有 R 级（或高于 R 级的任何
// 值）的函数采用 `below_R`。
using below_mcp_connecting = maya::below<mcp_connecting>;
using below_copilot_auto   = maya::below<copilot_auto>;
using below_auth_refresh   = maya::below<auth_refresh>;
using below_cred_file      = maya::below<cred_file>;
using below_rag_index      = maya::below<rag_index>;
using below_memory_store   = maya::below<memory_store>;
using below_thread_log     = maya::below<thread_log>;
using below_file_lane      = maya::below<file_lane>;
using below_worker_slot    = maya::below<worker_slot>;
using below_writer_state   = maya::below<writer_state>;
using below_settings_cache = maya::below<settings_cache>;
// 仅采用叶子：可从除叶子之外的任何 锁 正文 调用。
using below_leaf           = maya::below<maya::leaf>;

}  // namespace agentty::lock_levels

namespace agentty {
// 签名的短拼写。
using maya::unlocked;
using maya::held;
using maya::below;
namespace locks = lock_levels;
}  // namespace agentty
