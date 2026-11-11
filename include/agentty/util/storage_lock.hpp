#pragma once
// agentty::util::storage_lock：检查 agentty 当前是否正在使用存储。
//
// 每个正在运行的agentty在其整个生命周期中都在<user root>/
// agentty.running上持有SHARED 锁。重写每个人 (
// `agentty config move`) 下的存储的东西会使用 EXCLUSIVE，
// 只有在没有实例运行时才会成功。当进程终止时，内核会删除 锁，
// 因此崩溃永远不会留下 过期 "running"，这与 pid 文件不同。

#include <maya/runtime.hpp>

#include <optional>

namespace agentty::util {

// 召开会议。当文件系统不能锁时为空；这仅意味着
// `config move` 无法证明商店闲置，这里永远不会失败。
[[nodiscard]] std::optional<maya::platform::native_file_lock> hold_store_shared();

// 独家，无需等待。当另一个 agentty 正在运行时 nullopt（
// 或者 加锁 不受支持，`config move` 也以同样的方式处理：拒绝）。
[[nodiscard]] std::optional<maya::platform::native_file_lock> try_store_exclusive();

}  // namespace agentty::util
