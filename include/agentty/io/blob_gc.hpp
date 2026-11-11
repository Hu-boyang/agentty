#pragma once
// agentty::blobs::gc-回收载荷不再有线程引用。
//
// 原因 此 IS 不 REFCOUNTING
//
// Blob是内容-addressed：粘贴到两个线程
// 中的相同屏幕截图或相同的工具输出是从两者引用的ONE文件。
// 在真实商店上测量：188个活动斑点中的9个具有多个引用者。
// 所以 "delete this thread's blobs when the thread is deleted"
// 是错误的——它会默默地清空 线程 中仍然打开的图像，
// 并且在有人滚动回到该回合之前，损坏是不可见的。
//
// 引用计数原则上会解决这个问题，但实际上会更糟：它
// 必须在每个 保存 上进行事务性更新、重写、分叉、
// 压缩和删除，并且单个错过的减量会永远泄漏，
// 而单个虚假的减量会破坏数据。该商店的整体吸引力在于，
// blob 是一个以其内容命名的 不可变 文件，
// 其他任何内容都不需要与任何内容保持一致。
//
// 所以：标记和清除。走过每一个线程，收集每一个参考资料，
// 删除剩下的。 O（语料库），因此不能在 高频路径
// 上运行 - 但它是唯一一个设计，其中 WRONG
// 关于参考消耗磁盘而不是历史。
//
// 原因 IT IS NEEDED AT 全部
//
// 缩短线程孤儿斑点的每条路径：delete_thread、压缩、
// 消息编辑、分叉和日志的rewrite()。使用一天后在真实商店中
// 进行测量，471 个斑点中有 283 个（10.5 MB，
// 15.5 MB）已经未被引用。这占了商店的 60%，而且还在不断增长。
//
// 该 SAFETY RULE
//
// 仅当 NO 线程 文件提到斑点时，扫描才会删除该斑点。
// 如果 walk 无法读取 线程，则它不知道 线程
// 引用了什么，因此它根本拒绝删除任何内容。
// 无法读取的 线程 文件正是用户最需要完好无损的 载荷 的时候。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>

#include <maya/runtime.hpp>

namespace agentty::blobs {

struct GcStats {
    std::size_t   scanned_threads = 0;
    std::size_t   unreadable      = 0;  // >0 ⇒ 没有删除任何内容
    std::size_t   total_blobs     = 0;
    std::size_t   referenced      = 0;
    std::size_t   deleted         = 0;
    std::size_t   dangling        = 0;  // 引用但缺失——一个真正的错误
    std::uintmax_t bytes_freed    = 0;
    bool          ran             = false;  // 假⇒中止，参见`unreadable`

    [[nodiscard]] std::size_t orphans() const noexcept {
        return total_blobs - referenced;
    }
};

// 标记并清除blob存储。 `dry_run` 报告 WOULD
// 被删除而不触及任何内容，这就是在任何人信任它与真正的
// 商店之前应该如何执行的操作。
//
// 不从任何高频路径自动调用。回收磁盘从来都不够紧急，
// 不足以证明在 线程 切换期间进行 O(corpus) 遍历是合理的。
[[nodiscard]] GcStats collect(bool dry_run = false);

// `min_age`:仅回收其mtime至少是这个旧的未引用的blob。
// 保存 将其 blob 之前 写入引用它们的 线程 文件（另
// 一个 agentty 进程可能是 mid-save），
// 因此新的未引用 blob 可能即将被引用。 0 = 没有恩典。
// 文件之间检查`stop`；停止行走不会删除任何东西。
[[nodiscard]] GcStats collect_in(const std::filesystem::path& threads_dir,
                                 bool dry_run = false,
                                 std::chrono::seconds min_age =
                                     std::chrono::seconds{0},
                                 std::stop_token stop = {});

// Background-safe内务处理：以24小时宽限窗口运行收集，
// 每天最多一次（blob目录中的标记文件）。当因为最近运行而
// 跳过时或当 已取消 时返回 nullopt。
[[nodiscard]] std::optional<GcStats> collect_if_due(const maya::unlocked& cap,
                                                    std::stop_token stop = {});

// 在短暂的延迟之后在其自己的线程上运行collect_if_due()，
// 因此它永远不会与启动竞争并且根本不会在立即退出的进程中运行（管道-EOF冒烟测试、
// `--version`-style快速路径）。 线程 由 join_background_gc
// () 连接，拆解前为 main() 调用；当 CRT 释放它们时，
// 独立行走仍然接触 logx/persistence 静力学，这是一个 use
// -after-free（CI 管道烟雾测试中的 Windows 0xC0000005）。
void start_background_gc();
// 取消 (合作地，在文件之间检查）并加入。 幂等，如果 start_background_gc
// () 从未运行过，则为 no-op。加入，所以需要`unlocked`。
void join_background_gc(const maya::unlocked& cap) noexcept;

} // namespace agentty::blobs
