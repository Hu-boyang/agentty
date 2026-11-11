#pragma once
// agentty/rag/vector_cache.hpp — 嵌入计算了 OUTSIDE 和
// RAG 索引 锁，在其中读取。
//
// 索引是为整个检索或重建而保存的maya::guarded，
// 并且rag-cpp向其嵌入器询问来自该正文内部的向量：build()
// 上的每个待处理块，即搜索查询。嵌入器是网络 调用（本地 Ollama，
// 托管 API）。在 锁 下执行这些 调用 意味着要么一次一批，
// 要么在 锁 下与助手 线程 一起扇出，而 jaal 禁止这样
// 做（#112 正是在这一点上中止）。
//
// 因此，该工作被分成三部分，并且只有便宜的部分持有锁：
//
//   1.阶段（锁定）计算出哪些文本将需要向量：
//                           Corpus::pending_texts()，查询
//   2.预取（解锁）将它们并行批量地嵌入到缓存中
//   3.如前完成（锁定）build/search；语料库的嵌入器是
//                           CachedEmbedder，所以每次查找都是命中
//
// 步骤3中的失误（中间有新的东西）会回落到真正的嵌入器，
// 所以答案仍然是正确的，只是速度较慢；未命中次数会被计算在内，
// 因此回归会显示在 stats() 中。
//
// 存储是被替换、从未编辑的maya::published
// 映射：读取器（锁定的嵌入器，无法获取锁的库虚拟）
// 用一个原子对其加载，并且预取发布添加了新向量的新映射。
// 密钥携带嵌入者的身份，因此切换模型永远
// 不会提供来自旧空间的向量。

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <maya/runtime.hpp>
#include <rag/dense/embedder.hpp>

namespace agentty::rag {

class VectorCache {
public:
    // 最多保留向量。 768-d 浮点向量为 3 KB，
    // 因此外部约为 200 MB；典型的项目只使用一小部分。
    // 超过上限时，最老一代的条目将排在第一位。
    explicit VectorCache(std::size_t capacity = 64 * 1024) : capacity_(capacity) {}

    VectorCache(const VectorCache&)            = delete;
    VectorCache& operator=(const VectorCache&) = delete;

    // (模型，文本)的缓存向量。 锁-free；从来没有 IO。
    [[nodiscard]] bool get(std::string_view model, std::string_view text, ::rag::Vector& out) const;

    // 嵌入尚未缓存的每个文本，`width`批次正在运行，
    // 并发布它们。加入助手 线程，因此需要 `unlocked`：
    // 无法从 锁 正文 调用它。部分失败会保留有效的内容（
    // 锁定的构建会回退到真正的嵌入器以完成其余部分）；
    // 仅当无法嵌入任何内容时才会返回错误。
    ::rag::Result<void> prefetch(const maya::unlocked& cap, const ::rag::dense::AnyEmbedder& real,
                                 std::span<const std::string> texts, std::size_t batch = 32) const;

    struct Stats {
        std::uint64_t hits = 0, misses = 0, prefetched = 0, entries = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;
    // 删除每个向量（嵌入配置改变）。 锁-free，就像发布一样。
    void clear() const noexcept { map_.publish(nullptr); }
    void note_hit() const noexcept { hits_.fetch_add(1, std::memory_order_relaxed); }
    void note_miss() const noexcept { misses_.fetch_add(1, std::memory_order_relaxed); }

private:
    struct Entry {
        ::rag::Vector v;
        std::uint64_t gen = 0;   // 添加时；最年长的先越过上限
    };
    using Map = std::unordered_map<std::string, Entry>;
    static std::string key(std::string_view model, std::string_view text);
    void merge(std::vector<std::pair<std::string, ::rag::Vector>> fresh) const;

    std::size_t                        capacity_;
    // 地图被整个替换，从未被编辑，因此读者不需要锁并且缓存
    // 可以作为常量共享：添加向量是发布，其是原子的（此处的
    // 可变仅出于这个原因）。 publish_if 重试 如果
    // 两次预取 竞态，那么两者都不会丢失其向量。
    mutable maya::published<const Map> map_;
    mutable std::atomic<std::uint64_t> gen_{0};
    mutable std::atomic<std::uint64_t> hits_{0}, misses_{0}, prefetched_{0};
};

// 锁定引擎使用的嵌入器：来自 缓存 的答案，并且仅在错过
// 调用 时才是真正的答案（缓慢，但正确）。与真实嵌入器
// 具有相同的身份和维度，因此持久索引仍然与其模型匹配。
class CachedEmbedder {
public:
    CachedEmbedder(::rag::dense::AnyEmbedder real, std::shared_ptr<const VectorCache> cache)
        : real_(std::move(real)), cache_(std::move(cache)) {}

    [[nodiscard]] std::size_t dimension() const { return real_.dimension(); }
    [[nodiscard]] std::string_view identity() const { return real_.identity(); }
    // 这里的批次是查找，并且这在锁内运行，
    // 其中助手线程不能被加入：一次一个。
    [[nodiscard]] std::size_t max_concurrency() const noexcept { return 1; }
    [[nodiscard]] ::rag::Result<std::vector<::rag::Vector>> embed(std::span<const std::string> texts) const;

private:
    ::rag::dense::AnyEmbedder          real_;
    std::shared_ptr<const VectorCache> cache_;
};

}  // namespace agentty::rag

MAYA_SYNC(agentty::rag::VectorCache);
