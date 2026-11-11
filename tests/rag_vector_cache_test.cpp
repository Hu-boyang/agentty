// VectorCache / CachedEmbedder : 嵌入在
// RAG 指数 锁 (#112)并读其内. 纯:假嵌 ⁇ ,无网.

#include "agtest.hpp"
#include "agtest_cap.hpp"

#include "agentty/rag/vector_cache.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

using agentty::rag::CachedEmbedder;
using agentty::rag::VectorCache;

namespace {

// 计数 调用 ; 文本的矢量是{len, 第一 字节}.
// `fail_from` 使每个批次的第一稿都是 失败的,测试部分失败。
struct FakeEmbedder {
    std::string id = "fake-a";
    std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> texts = std::make_shared<std::atomic<int>>(0);
    std::string fail_from;
    std::size_t conc = 4;
    [[nodiscard]] std::size_t dimension() const { return 2; }
    [[nodiscard]] std::string_view identity() const { return id; }
    [[nodiscard]] std::size_t max_concurrency() const { return conc; }
    [[nodiscard]] rag::Result<std::vector<rag::Vector>> embed(std::span<const std::string> in) const {
        calls->fetch_add(1);
        texts->fetch_add(static_cast<int>(in.size()));
        if (!fail_from.empty() && !in.empty() && in.front() >= fail_from)
            return std::unexpected(rag::Error{rag::Errc::transport_error, "down"});
        std::vector<rag::Vector> out;
        for (const auto& t : in)
            out.push_back({static_cast<float>(t.size()), t.empty() ? 0.f : static_cast<float>(t[0])});
        return out;
    }
};

}  // 命名空间结束。

TEST_CASE("vector cache: prefetch once, then every lookup is a hit") {
    FakeEmbedder fe;
    rag::dense::AnyEmbedder real{fe};
    auto cache = std::make_shared<const VectorCache>();
    std::vector<std::string> texts;
    for (int i = 0; i < 100; ++i) texts.push_back("text-" + std::to_string(i));

    REQUIRE(cache->prefetch(agtest::cap(), real, texts, /*批次=(单位:千美元)*/8).has_value());
    CHECK(fe.texts->load() == 100);
    CHECK(fe.calls->load() == 13);   // ceil( 100/8) 数据
    CHECK(cache->stats().entries == 100);

    // 同样的文本的第二个预示 并不花费什么。
    REQUIRE(cache->prefetch(agtest::cap(), real, texts, 8).has_value());
    CHECK(fe.texts->load() == 100);

    // 锁定的侧面:所有命中,没有调用.
    CachedEmbedder ce{real, cache};
    auto r = ce.embed(texts);
    REQUIRE(r.has_value());
    CHECK(r->size() == 100);
    const rag::Vector want7{6.f, static_cast<float>('t')};
    CHECK((*r)[7] == want7);
    CHECK(fe.calls->load() == 13);
    CHECK(cache->stats().hits == 100);
    CHECK(cache->stats().misses == 0);
}

TEST_CASE("vector cache: a miss falls back to the real embedder and is counted") {
    FakeEmbedder fe;
    rag::dense::AnyEmbedder real{fe};
    auto cache = std::make_shared<const VectorCache>();
    std::vector<std::string> some{"a", "bb"};
    REQUIRE(cache->prefetch(agtest::cap(), real, some).has_value());
    const int calls = fe.calls->load();

    CachedEmbedder ce{real, cache};
    std::vector<std::string> mixed{"a", "new-one", "bb"};
    auto r = ce.embed(mixed);
    REQUIRE(r.has_value());
    const rag::Vector va{1.f, static_cast<float>('a')};
    const rag::Vector vn{7.f, static_cast<float>('n')};
    const rag::Vector vb{2.f, static_cast<float>('b')};
    CHECK((*r)[0] == va);
    CHECK((*r)[1] == vn);
    CHECK((*r)[2] == vb);
    CHECK(fe.calls->load() == calls + 1);   // 单曲 调用 ,为一位小姐
    CHECK(cache->stats().misses == 1);
    CHECK(cache->stats().hits == 2);
}

TEST_CASE("vector cache: a different model never gets another model's vector") {
    FakeEmbedder a;
    FakeEmbedder b;
    b.id = "fake-b";
    auto cache = std::make_shared<const VectorCache>();
    std::vector<std::string> t{"same text"};
    REQUIRE(cache->prefetch(agtest::cap(), rag::dense::AnyEmbedder{a}, t).has_value());
    rag::Vector v;
    CHECK(cache->get("fake-a", "same text", v));
    CHECK(!cache->get("fake-b", "same text", v));
    // 而密钥边界不能伪造:("fake-a","x")对("fake-ax","").
    CHECK(!cache->get("fake-a" "same", " text", v));
    (void)b;
}

TEST_CASE("vector cache: partial failure keeps what worked; total failure says why") {
    FakeEmbedder fe;
    fe.fail_from = "z";
    rag::dense::AnyEmbedder real{fe};
    auto cache = std::make_shared<const VectorCache>();
    std::vector<std::string> texts{"a1", "a2", "z1", "z2"};
    CHECK(cache->prefetch(agtest::cap(), real, texts, /*批次=(单位:千美元)*/2).has_value());
    rag::Vector v;
    CHECK(cache->get("fake-a", "a1", v));
    CHECK(!cache->get("fake-a", "z1", v));

    std::vector<std::string> only_bad{"z3", "z4"};
    auto bad = cache->prefetch(agtest::cap(), real, only_bad, 2);
    CHECK(!bad.has_value());
}

TEST_CASE("vector cache: over capacity, the oldest entries go first") {
    FakeEmbedder fe;
    rag::dense::AnyEmbedder real{fe};
    auto cache = std::make_shared<const VectorCache>(/*容量=*/4);
    std::vector<std::string> first{"1", "2", "3"};
    std::vector<std::string> second{"4", "5", "6"};
    REQUIRE(cache->prefetch(agtest::cap(), real, first).has_value());
    REQUIRE(cache->prefetch(agtest::cap(), real, second).has_value());
    CHECK(cache->stats().entries == 4);
    rag::Vector v;
    // 每个新的矢量都存活下来;剩下的一个槽到一个旧的槽.
    CHECK(cache->get("fake-a", "4", v));
    CHECK(cache->get("fake-a", "5", v));
    CHECK(cache->get("fake-a", "6", v));
    const int old_kept = cache->get("fake-a", "1", v) + cache->get("fake-a", "2", v) + cache->get("fake-a", "3", v);
    CHECK(old_kept == 1);
}

TEST_CASE("vector cache: batches run side by side, up to the backend's width") {
    // 慢假: 每一个 调用 睡觉,如此重叠 调用 可见。
    struct Slow {
        std::shared_ptr<std::atomic<int>> now  = std::make_shared<std::atomic<int>>(0);
        std::shared_ptr<std::atomic<int>> peak = std::make_shared<std::atomic<int>>(0);
        [[nodiscard]] std::size_t dimension() const { return 1; }
        [[nodiscard]] std::string_view identity() const { return "slow"; }
        [[nodiscard]] std::size_t max_concurrency() const { return 4; }
        [[nodiscard]] rag::Result<std::vector<rag::Vector>> embed(std::span<const std::string> in) const {
            const int n = now->fetch_add(1) + 1;
            int p = peak->load();
            while (n > p && !peak->compare_exchange_weak(p, n)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds{40});
            now->fetch_sub(1);
            return std::vector<rag::Vector>(in.size(), rag::Vector{1.f});
        }
    } slow;
    auto cache = std::make_shared<const VectorCache>();
    std::vector<std::string> texts;
    for (int i = 0; i < 16; ++i) texts.push_back(std::to_string(i));
    REQUIRE(cache->prefetch(agtest::cap(), rag::dense::AnyEmbedder{slow}, texts, /*批次=(单位:千美元)*/2).has_value());
    CHECK(slow.peak->load() >= 2);
    CHECK(slow.peak->load() <= 4);
}
