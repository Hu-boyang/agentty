// src/rag/vector_cache.cpp — 参见 agentty/rag/vector_cache.hpp.

#include "agentty/rag/vector_cache.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace agentty::rag {

std::string VectorCache::key(std::string_view model, std::string_view text) {
    std::string k;
    k.reserve(model.size() + 1 + text.size());
    k.append(model);
    k.push_back('\0');   // 不能出现在模型id中，所以(a,bc)!=(ab,c)
    k.append(text);
    return k;
}

bool VectorCache::get(std::string_view model, std::string_view text, ::rag::Vector& out) const {
    const auto m = map_.current();
    if (!m) return false;
    const auto it = m->find(key(model, text));
    if (it == m->end()) return false;
    out = it->second.v;
    return true;
}

void VectorCache::merge(std::vector<std::pair<std::string, ::rag::Vector>> fresh) const {
    if (fresh.empty()) return;
    const std::uint64_t g = gen_.fetch_add(1, std::memory_order_relaxed) + 1;
    // Copy-on-write:从当前地图构建下一个地图，
    // 并且仅在没有其他预取同时发布的情况下发布它；否则从他们的开始，
    // 所以两者都不会失去其向量。
    for (;;) {
        const auto cur = map_.current();
        auto next = std::make_shared<Map>(cur ? *cur : Map{});
        for (auto& [k, v] : fresh) (*next)[k] = Entry{v, g};
        if (next->size() > capacity_) {
            // 将最老的几代丢弃直至低于上限。
            std::vector<std::pair<std::uint64_t, const std::string*>> by_age;
            by_age.reserve(next->size());
            for (const auto& [k, e] : *next) by_age.emplace_back(e.gen, &k);
            std::ranges::sort(by_age, {}, &std::pair<std::uint64_t, const std::string*>::first);
            std::vector<std::string> drop;
            for (std::size_t i = 0; i < next->size() - capacity_; ++i) drop.push_back(*by_age[i].second);
            for (const auto& k : drop) next->erase(k);
        }
        if (map_.publish_if(cur, std::shared_ptr<const Map>(std::move(next)))) return;
    }
}

::rag::Result<void> VectorCache::prefetch(const maya::unlocked& cap, const ::rag::dense::AnyEmbedder& real,
                                          std::span<const std::string> texts, std::size_t batch) const {
    const std::string model{real.identity()};
    // 尚未缓存的内容，每个文本一次。
    std::vector<std::string> todo;
    {
        const auto m = map_.current();
        std::unordered_set<std::string_view> seen;
        for (const auto& t : texts) {
            if (!seen.insert(t).second) continue;
            if (m && m->contains(key(model, t))) continue;
            todo.push_back(t);
        }
    }
    if (todo.empty()) return {};

    const std::size_t bs = std::max<std::size_t>(batch, 1);
    const std::size_t nb = (todo.size() + bs - 1) / bs;
    std::vector<std::optional<std::vector<::rag::Vector>>> out(nb);
    std::vector<std::optional<::rag::Error>> errs(nb);
    auto run = [&](std::size_t b) {
        const std::size_t off = b * bs, end = std::min(off + bs, todo.size());
        auto r = real.embed(std::span<const std::string>(todo).subspan(off, end - off));
        if (!r) { errs[b] = r.error(); return; }
        out[b] = std::move(*r);
    };
    // 后端表示它想要多少个正在运行的请求（托管的API 8、
    // 本地Ollama 4、in-process模型1）。这里没有保存 锁，
    // 因此批次可以真正并行运行。
    const std::size_t width = std::clamp<std::size_t>(real.max_concurrency(), 1, nb);
    maya::parallel_for(cap, nb, width, run);

    std::vector<std::pair<std::string, ::rag::Vector>> fresh;
    fresh.reserve(todo.size());
    for (std::size_t b = 0; b < nb; ++b) {
        if (!out[b]) continue;
        const std::size_t off = b * bs;
        for (std::size_t j = 0; j < out[b]->size() && off + j < todo.size(); ++j) {
            // 正是真实嵌入器返回的内容：缓存 必须是它的
            // drop-in，并且语料库之后进行自己的标准化。
            fresh.emplace_back(key(model, todo[off + j]), std::move((*out[b])[j]));
        }
    }
    prefetched_.fetch_add(fresh.size(), std::memory_order_relaxed);
    const bool any = !fresh.empty();
    merge(std::move(fresh));
    if (!any)
        for (auto& e : errs)
            if (e) return std::unexpected(std::move(*e));
    return {};
}

VectorCache::Stats VectorCache::stats() const noexcept {
    const auto m = map_.current();
    return {hits_.load(std::memory_order_relaxed), misses_.load(std::memory_order_relaxed),
            prefetched_.load(std::memory_order_relaxed), m ? m->size() : 0};
}

::rag::Result<std::vector<::rag::Vector>> CachedEmbedder::embed(std::span<const std::string> texts) const {
    std::vector<::rag::Vector> out(texts.size());
    std::vector<std::size_t> miss;
    const std::string_view model = real_.identity();
    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (cache_ && cache_->get(model, texts[i], out[i])) cache_->note_hit();
        else miss.push_back(i);
    }
    if (miss.empty()) return out;
    // 未预取（新文本在分段后进入）：询问真正的嵌入器。
    // 正确答案，只是速度较慢，并且经过计数，因此可以看到回归。
    if (cache_)
        for (std::size_t n = 0; n < miss.size(); ++n) cache_->note_miss();
    std::vector<std::string> rest;
    rest.reserve(miss.size());
    for (auto i : miss) rest.push_back(texts[i]);
    auto r = real_.embed(rest);
    if (!r) return std::unexpected(r.error());
    for (std::size_t j = 0; j < miss.size() && j < r->size(); ++j) out[miss[j]] = std::move((*r)[j]);
    return out;
}

}  // namespace agentty::rag
