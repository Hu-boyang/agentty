#pragma once
// agentty::LazyBytes——知道如何获取自身的载荷。
//
// 消息携带的两个东西是大的、不透明的并且几乎无人读取：图像
// 字节和附件正文。两者都位于 content
// -addressed blob 存储中，两者在历史上都是 base64 内联，
// 并且两者都在同一个路径上读取 - 当消息为 re-sent 时，
// 即 传输。 渲染器 从来不接触任何一个：图像从其
// 媒体类型和尺寸中提取，附件从其名称和 字节 计数中提取。
//
// 因此，在加载时间对它们进行解码纯粹是浪费，并且是昂贵的类型。
// 在真实商店中测量：一个 29 MB 线程 携带 17 MB 的图像 载荷，
// 26 个捕获的-output 附件携带 4.2 MB 的
// base64 — 所有这些在每次打开时都被解码，产生无人查看的 字节。
//
// 因此，已加载 载荷 作为 `Source` (斑点名称，或 base64
// 文本）开始，并且在第一个 `bytes()` 调用 上以 按需加载 开始。
// 新捕获的 载荷（粘贴、屏幕截图、工具结果）是在 字节 存在的情况下构建的，
// 并且从未触及 惰性 路径。
//
// CORRECTNESS，因为"lazy"是bug隐藏的地方：
//   -bytes()是ONLY读取器。没有公共原始字段，所以没有
//     调用者可能会意外地观察到未实现的载荷为空。
//   -按需加载是幂等并且值-preserving:相同的字节
//     在每个调用上，COPY复制源，因此该副本解析为
//     相同的字节而不是什么都没有。
//   - 无法解析的源（已删除的 blob、损坏的 base64）
//     产生空的字节——正是提前加载加载器针对相同输入产生的内容，
//     并且每个消费者已经处理了空的载荷。
//   -线程-safe。 bytes() 是 const，从 工作线程 线程 调用
//     (提供者传输层在UI 线程上构建传输 正文，并且持久化编写者在
//     其自己的线程上构建保存)。它曾经在没有同步的情况下写入两个
//     `mutable` 字段，因此读取相同未解析的 载荷 的两个 线
//     程 都可以写入 bytes_：可以撕裂 std::string
//     的数据 竞态。现在，载荷 位于共享的 不可变 存储单元 和 按需
//     加载 中，恰好位于 std::call_once 下。
//     副本共享 存储单元，因此它们在它们之间解析一次（并复制 线程
//     以获得不再深的 -copies 图像 字节）。 set_bytes
//     () 永远不会触及共享的 存储单元：它为此句柄提供一个新句柄，
//     因此其他副本保留其值。
//
// 原因 ITS 自身 HEADER：conversation.hpp
// (ImageContent)和composer_attachment.hpp
// (附件)都需要它，并且conversation.hpp已经包括composer_attachment.hpp
// ——在任一中定义它都将是一个循环。它也非常通用：它对图像
// 或附件一无所知，只知道可能还不存在的 字节。
//
// 解析器是由持久化层安装一次的函数指针，
// 因此该头文件不了解blob目录或base64的
// 任何知识。

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <maya/runtime.hpp>   // maya::once

namespace agentty {

class LazyBytes {
public:
    // 首次需要时如何获取字节。空 `blob` 表示 载荷 内联
    // base64 在 `b64` 中。
    struct Source {
        std::string blob;   // content-addressed blob 名称（首选）
        std::string b64;    // 传统内联 base64 回退
        [[nodiscard]] bool empty() const noexcept {
            return blob.empty() && b64.empty();
        }
    };

    LazyBytes() = default;
    explicit LazyBytes(std::string raw)
        : cell_(std::make_shared<const Cell>(Cell::resolved_tag{}, std::move(raw))) {}

    [[nodiscard]] static LazyBytes lazy(Source src) {
        LazyBytes v;
        if (!src.empty())                  // 没有什么需要解决 => 保持空白
            v.cell_ = std::make_shared<const Cell>(std::move(src));
        return v;
    }
    [[nodiscard]] static LazyBytes from_blob(std::string name) {
        return lazy(Source{.blob = std::move(name), .b64 = {}});
    }
    [[nodiscard]] static LazyBytes from_base64(std::string b64) {
        return lazy(Source{.blob = {}, .b64 = std::move(b64)});
    }

    // 原始的字节、按需加载是他们第一次使用的。同时从任何副本上的
    // 任何 线程 到 调用 都是安全的。
    [[nodiscard]] const std::string& bytes() const {
        return cell_ ? cell_->bytes() : empty_string();
    }

    // 彻底替换载荷(剪贴板捕获、工具结果、
    // 提交时读取的@file)。赋予该句柄自己的 存储单元；
    // 之前制作的副本保留旧的 载荷。
    void set_bytes(std::string raw) {
        cell_ = std::make_shared<const Cell>(Cell::resolved_tag{}, std::move(raw));
    }

    // 待处理源，对于作者来说：从未实现的载荷可以是re-persisted
    // BY REFERENCE，没有解码也没有re-encode。
    // 对于从原始 字节 或 set_bytes() 构建的 载荷 为空。
    // 呼叫者先检查materialised()；和以前一样，
    // 在 按需加载 之后，源代码本身仍然可读。
    [[nodiscard]] const Source& source() const noexcept {
        return cell_ ? cell_->source : empty_source();
    }
    [[nodiscard]] bool materialised() const noexcept {
        return !cell_ || cell_->done.load(std::memory_order_acquire);
    }

    // "Is there nothing here?"
    // 而不强制进行blob读取：具有源的未物化的载荷不为空，
    // 无论源解析为什么。
    [[nodiscard]] bool empty() const noexcept {
        if (!cell_) return true;
        return materialised() ? cell_->bytes().empty() : cell_->source.empty();
    }

    // 由持久化层在启动时安装一次。空解析器（一个 单元测试，
    // 永远不会从磁盘中获取 加载）使每个 惰性 载荷 解析为空，
    // 这与丢失的 blob 相同。原子性，因为 工作线程 读取了它；
    // 它在任何 工作线程 存在之前被写入一次。
    using Resolver = std::string (*)(const Source&);
    static void set_resolver(Resolver r) noexcept {
        resolver_.store(r, std::memory_order_release);
    }

private:
    // 不可变一旦共享，除了一个-time 按需加载，
    // std::call_once使竞态-free：每个线程
    // 要么运行解析，要么等待它，然后都看到完成的字节。
    struct Cell {
        struct resolved_tag {};
        explicit Cell(Source s) : source(std::move(s)) {}
        Cell(resolved_tag, std::string raw) : bytes_(std::move(raw)) {
            done.store(true, std::memory_order_relaxed);
        }

        const std::string& bytes() const {
            if (!done.load(std::memory_order_acquire)) {
                maya::once(once_, [this] {
                    auto r  = resolver_.load(std::memory_order_acquire);
                    bytes_  = r ? r(source) : std::string{};
                    done.store(true, std::memory_order_release);
                });
            }
            return bytes_;
        }

        const Source              source;
        mutable std::atomic<bool> done{false};
    private:
        mutable std::once_flag    once_;
        mutable std::string       bytes_;     // 写一次，里面call_once
    };

    static const std::string& empty_string() noexcept {
        static const std::string s;
        return s;
    }
    static const Source& empty_source() noexcept {
        static const Source s{};
        return s;
    }

    inline static std::atomic<Resolver> resolver_{nullptr};

    std::shared_ptr<const Cell> cell_;    // null=空，已解决
};

} // namespace agentty
