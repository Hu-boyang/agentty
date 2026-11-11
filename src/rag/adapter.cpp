// agentty::rag适配器——检索引擎，由rag-cpp (rag::Engine)提供动力。
//
// 这是包括<rag/rag.hpp>的agentty中的ONLY 编译单元。
// 它将 agentty 的紧凑检索边界 (rag_adapter.hpp) 映射到 rag-cpp
// 的生产混合引擎上：上下文分块、BM25 + dense/HNSW、RRF 融合、重新排序、
// GraphRAG 和 .ragdb 持久化 — 因此 `search_docs`、
// `search_code` 和主动 pre-turn 路径"just work" 并保持快速。
//
// 知识源被统一到 一个 引擎中，并通过 URI 前缀进行区分，
// 因此来源保留 search()：
//     docs://<rel-path> 文档/知识文件夹（code-aware 分块）
//     Skill://<name> 安装了 Agent-Skills
//     正文 内存://<id> 学到的事实（JSONL 内存 存储）
//     mcp://<uri> 此会话的 MCP 资源（opt-in）

#include "agentty/rag/rag_adapter.hpp"
#include "agentty/util/callback.hpp"
#include "agentty/rag/rag_host.hpp"
#include "agentty/rag/vector_cache.hpp"
#include "agentty/io/shared_file.hpp"

#include "agentty/util/background.hpp"
#include "agentty/util/sendable.hpp"
#include "agentty/util/logx.hpp"

// agentty的检索引擎是rag-cpp。在 rag-cpp 尚未可构建的平台上（
// MSVC — 请参阅 CMakeLists.txt），AGENTTY_HAS_RAGCPP
// 为 0，并且此 TU 编译 是公共边界的一个微小的 no-op 实现，
// 因此 agentty 链接 的其余部分和每个非 -retrieval 功能都可以工作。完整的发动机装
// 在 Linux/macOS 二进制程序 中。
#ifndef AGENTTY_HAS_RAGCPP
#  define AGENTTY_HAS_RAGCPP 1
#endif

#if AGENTTY_HAS_RAGCPP

#include <maya/runtime.hpp>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rag/rag.hpp>

#include "agentty/dirs/dirs.hpp"
#include "agentty/config/inventory.hpp"   // kRagSpec——唯一声明
#include "agentty/io/http.hpp"
#include "agentty/mcp/client.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/memory_store.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/lock_levels.hpp"

namespace fs = std::filesystem;

namespace agentty::rag {
namespace {

// ── 持续指数所居之处 ─────────────────────────────────────
//
// Root::Project，因此存储遵循CODE并且锚定到项目标记而不是外壳的cwd。
// 旧的 `fs::current_path()` 形式为 `cd src/ && agentty`
// 提供了第二个 82 MB 索引 (#61)。
//
// $AGENTTY_RAG_DIR重新安置所有三个。相对值解析为 <anchor>/.
// agentty，而不是 cwd，因此它为每个启动目录命名一个目录
// 而不是不同的目录 - 规则 dirs 继承自 user_root。
//
// 可重建：.ragdb是导出数据。删除一个会花费re-index，
// 而不是历史，这就是让#62的横扫变得激进的原因，而blobs::
// gc必须小心。 keep_last=1 保留前一个嵌入器的索引，
// 因此 A/B-ing 两个后端不会强制在每个交换机上完全
// 重建 - 将身份标记放入文件名的全部意义。
//
// 扫描本身是#62并且尚未接线；这些声明了意图，
// 因此策略位于路径旁边，而不是在以后编写的任何代码中。
//
// 在 config/inventory.hpp (kRagSpec) 中声明
// 一次 并在此处使用别名，因此 `agentty config rag` 报告
// 实际使用的相同根、覆盖和生命周期——没有第二个定义偏离它。
constexpr ::agentty::dirs::Spec kDocsIndexSpec = ::agentty::config::kRagSpec;

constexpr ::agentty::dirs::Spec kCodeIndexSpec = kDocsIndexSpec;

// 检索反馈：学习循环的计数。与索引相同的根和覆盖（它是
// 相同类型的派生项目-scoped数据），但NOT可重建
// ——它是从实际使用中积累的，没有任何东西可以重新生成它，
// 所以没有扫描可能会收集它。这种区别正是 Lifecycle
// 字段的存在，使之变得明确而不是隐含。
constexpr ::agentty::dirs::Spec kFeedbackSpec = ::agentty::config::kFeedbackSpec;

// 解析索引所在的目录。
//
// 当 $AGENTTY_RAG_DIR 为 不 时，设置为 <anchor>/.
// agentty，并且位于项目内部本身就是将两个结帐分开的原因。
//
// 覆盖移除该属性：将两个项目指向一个目录并且它们落在相同的rag_docs.
// <tag>.ragdb上。元的 `root` 防护意味着这
// 不是 CORRUPTION——每个项目都会拒绝对方的索引
// 并重建——但它是一个在每个交换机上默默地花费完整 re-index
// 的鞭打，这比它本来要 保存 的磁盘更糟糕。
//
// 因此，被覆盖的位置获得per-project子目录。
// 基本名称使其可读；绝对锚点的哈希值使其保持唯一。
// 与嵌入标签的推理相同：机器身份确保正确性，
// 人类前缀让你可以知道你在看什么。
[[nodiscard]] fs::path rag_store_dir(const ::agentty::dirs::Spec& spec) {
    auto d = ::agentty::dirs::resolve(spec);
    if (!d) return {};

    // 指数曾经在<project>/.agentty中持平；
    // 他们现在住在cache/下。移动现有的内容，而不是孤立一个无人能命名的 38
    // MB 索引，这就是 #62 的目的。
    std::error_code mec;
    if (!d->root.empty() && fs::is_directory(d->path, mec)) {
        for (const auto& e : fs::directory_iterator(d->root, mec)) {
            if (mec) break;
            if (!e.is_regular_file(mec) || mec) continue;
            const auto name = e.path().filename().string();
            if (!name.starts_with("rag_")) continue;
            std::error_code rec;
            fs::rename(e.path(), d->path / name, rec);
        }
    }
    return d->path;
}

// Sweep取代了我们刚刚重写的索引的变体。
//
// 将 之后 称为成功的 保存，这是唯一已知的实时
// 文件名的时刻——在启动时进行扫描就必须猜测，
// 并且猜测是如何删除你将要 加载 的索引。
//
// `include_untagged`开启：遗留的`rag_code.ragdb`
// （无嵌入器标签）不能由当前代码路径命名，因此它是不可读的自重而不是
// 某人可能仍然想要的文件。那是#62 中的 47 MB。
void sweep_variants(const ::agentty::dirs::Spec& spec,
                    std::string_view stem,
                    const fs::path& live_db) {
    if (!spec.life.sweeps() || live_db.empty()) return;

    const std::string live = live_db.filename().string();
    const std::array<std::string, 1> keep{live};
    static constexpr std::array<std::string_view, 1> kSidecars{".meta.json"};

    ::agentty::dirs::SweepRequest req{
        .spec = spec,
        .stem = stem,
        .suffix = ".ragdb",
        .keep = keep,
        .sidecar_suffixes = kSidecars,
        .include_untagged = true,
    };
    (void)::agentty::dirs::sweep(req);
}

bool truthy_default_on(const char* var) {
    const char* v = std::getenv(var);
    if (!v || !v[0]) return true;                 // 未设置 ⇒ ON
    return !(v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N');
}

// 索引仅在检索器自己的工作线程和工具线程上运行，从不在状态更新函数中运行（RAG
// 面板通过cmd::read_rag_embed_status读取它）。
inline Io index_io() { return IoAccess::grant(); }

bool truthy_default_off(const char* var) {
    const char* v = std::getenv(var);
    if (!v || !v[0]) return false;                // 未设置 ⇒ OFF
    return !(v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N');
}

float env_float(const char* var, float dflt) {
    if (const char* v = std::getenv(var); v && v[0]) {
        try { return std::stof(v); } catch (...) {}
    }
    return dflt;
}

fs::path resolve_docs_root(const std::string& configured) {
    // Env 获胜并且是 re-read 每个 调用:AGENTTY_DOCS_DIR
    // 可以在检索（测试或重新指向它的用户）之间改变，并且缓存的根
    // 将默默地提供错误的语料库。
    if (const char* d = std::getenv("AGENTTY_DOCS_DIR"); d && d[0])
        return fs::path{d};
    if (!configured.empty()) return fs::path{configured};
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (ec) return {};
    auto docs = cwd / "docs";
    if (fs::is_directory(docs, ec)) return docs;
    auto kb = cwd / ".agentty" / "knowledge";
    if (fs::is_directory(kb, ec)) return kb;
    return {};
}

// 将"docs://path" uri拆分回(source-tag，bare-path)。
std::pair<std::string, std::string> split_uri(const std::string& uri) {
    auto pos = uri.find("://");
    if (pos == std::string::npos) return {"docs", uri};
    return {uri.substr(0, pos), uri.substr(pos + 3)};
}

std::size_t retrieval_output_budget() {
    std::size_t value = 12 * 1024; // 整个结果大约有 3k 个令牌
    if (const char* v = std::getenv("AGENTTY_RAG_OUTPUT_BYTES"); v && v[0]) {
        try { value = std::clamp<std::size_t>(std::stoull(v), 2048, 64 * 1024); }
        catch (...) {}
    }
    return value;
}

// TOP分数的分数，低于该分数的末尾段落被丢弃而不是被压缩到上下文中。
// 无论如何，低-confidence 末尾是该模型忽略的东西，
// 因此为其支付旗舰输入价格纯粹是浪费。仅当 s >= floor_frac *
// top_score 时，得分 s 的命中才会存活。 0 禁用楼层（保留每次命中）；
// 默认情况下会修剪明显的 末尾 而不触及良好检索的实质。
// 可通过 AGENTTY_RAG_RELEVANCE_FLOOR 进行调整。
float relevance_floor_frac() {
    float f = 0.30f;
    if (const char* v = std::getenv("AGENTTY_RAG_RELEVANCE_FLOOR"); v && v[0]) {
        try { f = std::clamp(std::stof(v), 0.0f, 0.95f); } catch (...) {}
    }
    return f;
}

// Water-filling预算分配器:按照其得分^gamma的比例将
// `total` 字节分摊到各个段落中(伽马>1将预算集中在自信的头上)，
// 但每次段落决不会低于`floor`，因此即使保留的末尾命中也仍然是清晰的摘录。
// 这取代了固定的 total/n 分割，后者为 0.11 置信度的 8 级
// 命中提供了与 0.88 的 1 级命中相同的 字节 容差 — i.e。
// 在信号和噪声上花费了相同的代币。针对每个输入分数返回一项津贴，总和 <= 总计。
// 可通过 AGENTTY_RAG_BUDGET_GAMMA 进行调整。
std::vector<std::size_t> allocate_budget(const std::vector<double>& scores,
                                         std::size_t total,
                                         std::size_t floor = 768) {
    const std::size_t n = scores.size();
    std::vector<std::size_t> out(n, 0);
    if (n == 0 || total == 0) return out;
    float gamma = 1.5f;
    if (const char* v = std::getenv("AGENTTY_RAG_BUDGET_GAMMA"); v && v[0]) {
        try { gamma = std::clamp(std::stof(v), 0.5f, 4.0f); } catch (...) {}
    }
    // 如果每个通道都不能到达地板，则返回到均匀分布，
    // 这样我们就不会因为第一个通道而使后面的通道挨饿。
    if (total < floor * n) {
        std::size_t each = total / n;
        for (auto& a : out) a = each;
        return out;
    }
    std::vector<double> weight(n);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double s = std::clamp(scores[i], 0.0, 1.0);
        weight[i] = std::pow(s + 1e-3, static_cast<double>(gamma));
        sum += weight[i];
    }
    if (sum <= 0.0) { for (auto& w : weight) w = 1.0; sum = static_cast<double>(n); }
    // 为所有人保留席位，剩余的按重量分配。
    const std::size_t reserved = floor * n;
    const std::size_t extra = total - reserved;
    for (std::size_t i = 0; i < n; ++i)
        out[i] = floor + static_cast<std::size_t>(
                             (weight[i] / sum) * static_cast<double>(extra));
    return out;
}

// 通过检索置信度缩放TOTAL 字节预算。勉强清除相关性栏（
// conf ~0.3）的检索不应保留与 slam-dunk（
// conf ~0.9）相同的 ~3k 标记 - 它将花费预算的边缘
// 段落正是模型最不可能使用的那些。我们将预算从conf=
// floor_conf的下限分数线性增加到conf=1.0的100%，
// 因此弱检索注入了一个紧凑、廉价的块，而强检索则获得了完整的空间。
// conf<0（未知）保留全部预算——我们不会惩罚从未评分的路径。
// 可通过 AGENTTY_RAG_CONF_BUDGET_FLOOR 进行调整（最小分数，默认 0.45）。
std::size_t confidence_scaled_budget(std::size_t total, double conf) {
    if (conf < 0.0) return total;                 // 未分级 → 全部预算
    float floor_frac = 0.45f;
    if (const char* v = std::getenv("AGENTTY_RAG_CONF_BUDGET_FLOOR"); v && v[0]) {
        try { floor_frac = std::clamp(std::stof(v), 0.1f, 1.0f); } catch (...) {}
    }
    const double c = std::clamp(conf, 0.0, 1.0);
    // frac=floor+(1-floor)*c，夹紧到[floor，1]。
    const double frac = std::clamp(
        static_cast<double>(floor_frac) + (1.0 - floor_frac) * c,
        static_cast<double>(floor_frac), 1.0);
    return std::max<std::size_t>(2048,
        static_cast<std::size_t>(static_cast<double>(total) * frac));
}

std::vector<std::string> query_terms(std::string_view query) {
    std::vector<std::string> terms;
    std::string word;
    auto flush = [&] {
        if (word.size() >= 2
            && std::find(terms.begin(), terms.end(), word) == terms.end())
            terms.push_back(word);
        word.clear();
    };
    for (unsigned char c : query) {
        if (std::isalnum(c)) word.push_back(static_cast<char>(std::tolower(c)));
        else flush();
    }
    flush();
    return terms;
}

std::size_t utf8_boundary(std::string_view text, std::size_t at) {
    at = std::min(at, text.size());
    while (at > 0 && at < text.size()
           && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
    return at;
}

// Re-read是来自现场磁盘的精确的1-based包含行范围[开始，结束]。
// 加强RAG结果验证：索引可以滞后于编辑，但磁盘始终是最新的——
// 因此search_code重新-reads引用的行并服务于磁盘真相。
// 如果出现任何错误（缺少文件、out-of-range），
// 则返回 ""，以便调用者退回到索引文本。
std::string read_disk_lines(const std::string& path, int start, int end) {
    if (start < 1 || end < start) return {};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string line, out;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (n < start) continue;
        if (n > end) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out += line;
        out += '\n';
    }
    return (n >= start) ? out : std::string{};
}

std::string compress_passage(std::string_view query, std::string_view text,
                             std::size_t budget) {
    if (text.size() <= budget) return std::string{text};
    const auto terms = query_terms(query);

    // PROSE的提取句子选择(model-free LLMLingua-style)。
    // 通过rare-term-weighted查询重叠对每个句子进行评分，
    // 贪婪地保留最高的-scoring直到预算填满，然后按原始顺序发出它们。
    // 这会删除不相关的填充物 BETWEEN 相关句子 - 这是下面的连续行
    // -window 无法做到的 - 因此相同的预算包含更多的答案-bearing 文本。
    // 我们只对natural-language段落采用这条路径（相
    // 对于长度而言，很少有换行符）；代码和配置保留连续的窗口路径，
    // 其中 inter-line 连续性为 加载-bearing 以提高可读性。
    // 通过 AGENTTY_RAG_EXTRACTIVE=0 禁用。
    auto extractive_enabled = [] {
        const char* v = std::getenv("AGENTTY_RAG_EXTRACTIVE");
        return !(v && (v[0] == '0' || v[0] == 'f' || v[0] == 'F'));
    };
    const std::size_t newline_count =
        static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    const bool prose_shaped =
        !terms.empty() && text.size() > 0
        && newline_count * 40 < text.size();   // 平均行 > 40 个字符 ⇒ 散文
    if (extractive_enabled() && prose_shaped) {
        // 分成关于.、!、?的句子和硬换行符。
        // 保留分隔符及其句子，以便重新汇编时读起来自然。
        struct Sent { std::size_t begin, end; };
        std::vector<Sent> sents;
        std::size_t s = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            const bool boundary =
                (c == '.' || c == '!' || c == '?' || c == '\n')
                && (i + 1 >= text.size()
                    || text[i + 1] == ' ' || text[i + 1] == '\n'
                    || text[i + 1] == '\t');
            if (boundary) { sents.push_back({s, i + 1}); s = i + 1; }
        }
        if (s < text.size()) sents.push_back({s, text.size()});

        if (sents.size() > 2) {
            // 越罕见的查询术语更具区分性：通过1/(1+语料库
            // -frequency-in-this-passage)对术语进行加权。
            // 便宜的 in-passage IDF 代理 — 不需要全局统计数据，
            // 并且它可以阻止常用术语（'the'、'code'）主导分数。
            std::vector<std::size_t> tf(terms.size(), 0);
            std::string lowered{text};
            for (char& c : lowered)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (std::size_t ti = 0; ti < terms.size(); ++ti) {
                std::size_t at = 0;
                while ((at = lowered.find(terms[ti], at)) != std::string::npos) {
                    ++tf[ti]; at += terms[ti].size();
                }
            }
            auto sent_score = [&](const Sent& se) -> double {
                double sc = 0.0;
                std::string_view seg = text.substr(se.begin, se.end - se.begin);
                std::string low{seg};
                for (char& c : low)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (std::size_t ti = 0; ti < terms.size(); ++ti)
                    if (low.find(terms[ti]) != std::string::npos)
                        sc += 1.0 / (1.0 + static_cast<double>(tf[ti]));
                // 长度-normalise轻，因此长句子不能仅在表面积上取胜，
                // 但不要超过-penalise(sqrt)。
                return sc / std::sqrt(static_cast<double>(seg.size()) + 1.0);
            };
            std::vector<std::size_t> order(sents.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(),
                [&](std::size_t a, std::size_t b) {
                    return sent_score(sents[a]) > sent_score(sents[b]);
                });
            // 贪婪地接纳最上面的句子，直到预算填满。
            // 如果最上面的句子得分为 0（任何地方都没有查询重叠），
            // 则保留到连续路径 - 提取没有可用的信号。
            if (sent_score(sents[order[0]]) > 0.0) {
                std::vector<char> keep(sents.size(), false);
                std::size_t used = 0;
                const std::size_t sep = 1;   // 保留句子之间的换行符
                for (std::size_t k = 0; k < order.size(); ++k) {
                    const auto& se = sents[order[k]];
                    std::size_t len = se.end - se.begin;
                    if (used + len + sep > budget) {
                        if (used == 0) break;   // 首先已经超出预算
                        continue;               // 跳过，尝试较小的一个
                    }
                    keep[order[k]] = true;
                    used += len + sep;
                }
                std::string out;
                out.reserve(used + 8);
                bool gap = false;
                for (std::size_t i = 0; i < sents.size(); ++i) {
                    if (!keep[i]) { gap = true; continue; }
                    if (!out.empty()) {
                        if (gap) { out += " … "; gap = false; }
                        else if (out.back() != '\n' && out.back() != ' ')
                            out += ' ';
                    }
                    std::string_view seg = text.substr(sents[i].begin,
                                                        sents[i].end - sents[i].begin);
                    // 修剪裂口可能留下的前导空间。
                    while (!seg.empty() && (seg.front() == ' ' || seg.front() == '\n'))
                        seg.remove_prefix(1);
                    out.append(seg);
                }
                if (!out.empty()) {
                    if (out.size() > budget) out.resize(utf8_boundary(out, budget));
                    return out;
                }
            }
        }
    }

    // ── 毗连线 -window 回退 (代码 / 配置 / 无) -signal 说词). ──
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    for (std::size_t start = 0; start < text.size();) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size(); else ++end;
        lines.emplace_back(start, end);
        start = end;
    }
    auto line_score = [&](std::size_t i) {
        std::string lower{text.substr(lines[i].first, lines[i].second - lines[i].first)};
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::size_t score = 0;
        for (const auto& term : terms) if (lower.find(term) != std::string::npos) ++score;
        return score;
    };
    std::size_t best = 0, best_score = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (auto score = line_score(i); score > best_score) { best = i; best_score = score; }

    std::size_t first = best, last = best + 1;
    while (true) {
        bool grew = false;
        if (first > 0 && lines[last - 1].second - lines[first - 1].first <= budget) {
            --first; grew = true;
        }
        if (last < lines.size() && lines[last].second - lines[first].first <= budget) {
            ++last; grew = true;
        }
        if (!grew) break;
    }
    std::size_t begin = lines[first].first;
    std::size_t end = lines[last - 1].second;
    if (end - begin > budget) {
        // minified/generated单行：将摘录集中
        // 在第一个查询项上，而不是盲目地保留不相关的前缀。
        std::string lower{text};
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::size_t hit = std::string::npos;
        for (const auto& term : terms) {
            hit = lower.find(term);
            if (hit != std::string::npos) break;
        }
        if (hit == std::string::npos) hit = begin;
        begin = hit > budget / 3 ? hit - budget / 3 : 0;
        end = std::min(text.size(), begin + budget);
    }
    begin = utf8_boundary(text, begin);
    end = utf8_boundary(text, end);
    std::string out;
    if (begin > 0) out += "…\n";
    out.append(text.substr(begin, end - begin));
    if (end < text.size()) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += "…";
    }
    if (out.size() > budget) out.resize(utf8_boundary(out, budget));
    return out;
}

// ── FeedbackStore ─────────────────────────────────────────────────────────
//   学习循环成为现实。单个进程-wide存储支持BOTH写入侧（feedback::note_surfaced
//   / note_file_opened自由功能）和读取侧（Retriever::Impl::feedback_boost）。
//   它坚持将`use`/`win` 事件的-only TSV追加到<project>
//   /.agentty/rag_feedback.tsv和归并 Beta-smoothed
//   per-path win-rate回到排名作为BOUNDED助推。
//
//   信号语义（匹配docs/website/retrieval.md):
//     使用 —search_docs 浮现这条路径（分母）。
//     获胜——代理然后`read`出现一条路径，
//           即RECENTLY（分子）。读取 never-surfaced
//           文件不是 的胜利：它不带有有关检索的相关性判断。
//
//   微调是[1-kMax，1+kMax]中以1.0为中心的乘法因子，
//   通过平滑后的win-rate距中性先验的距离以及通过随着样本计数而增长的置信项来缩放。
//   没有历史的路径不会受到影响，
//   因此循环只有在有证据时才能提供帮助。
// 计数和会话的表面集合，作为一个受保护的值。
// 下面的操作是对其的自由函数，因此锁定区域中的
// 任何内容都只能到达它所传递的数据。
struct FeedbackState {
    struct Tally { double uses = 0.0; double wins = 0.0; };
    bool loaded = false;
    std::string loaded_for;                    // 锚定TSV为已加载
    std::unordered_map<std::string, Tally> counts;
    std::unordered_set<std::string> recent;    // 本次会议浮出水面
};

class FeedbackStore {
public:
    static FeedbackStore& instance() {
        static FeedbackStore s;
        return s;
    }

    // 锁顺序：存储是LEAF（其正文仅更新计数；TSV追加在其后运行）。
    // boost() 从索引 正文 内的排名中调用（held<rag_index>
    // 转换：叶排名在其上方）； note_* 写入端通过
    // feedback::note_* 从工具 正文 运行。

    // `path`的排名微调，大约在[0.85，1.15]。
    // 1.0（中性）当学习停止、路径看不见或证据太少时。
    float boost(const maya::below<maya::leaf>& cap, const std::string& path) {
        return st_.with(cap, [](FeedbackState& st, std::string p, std::string tsv) {
            ensure_loaded(st, tsv);
            auto it = st.counts.find(p);
            if (it == st.counts.end()) return 1.0f;
            const double uses = it->second.uses;
            const double wins = it->second.wins;
            if (uses <= 0.0) return 1.0f;
            // Beta(1，1)平滑：(wins+1)/(uses+2)-
            // 有界win-rate，其从0.5先验开始并且仅随着证据增加而移动。
            const double rate = (wins + 1.0) / (uses + 2.0);
            // 对估计的置信度随着样本数量的增加而增加（饱和）。
            const double conf = uses / (uses + kPriorN);
            const double delta = (rate - 0.5) * 2.0;         // → [-1, 1]
            double factor = 1.0 + kMax * delta * conf;
            if (factor < 1.0 - kMax) factor = 1.0 - kMax;
            if (factor > 1.0 + kMax) factor = 1.0 + kMax;
            return static_cast<float>(factor);
        }, path, tsv_path().string());
    }

    // 记录这些路径是通过检索出现的（每个路径是"use")，
    // 并将它们记住为最近-surfaced，因此接下来的读取可以是胜利。
    void note_surfaced(const maya::below<maya::leaf>& cap, const std::vector<std::string>& paths) {
        const std::string tsv = tsv_path().string();
        auto to_write = st_.with(cap,
            [](FeedbackState& st, std::vector<std::string> ps, std::string t) {
                ensure_loaded(st, t);
                std::vector<std::string> out;
                for (auto& p : ps) {
                    if (p.empty()) continue;
                    st.counts[p].uses += 1.0;
                    st.recent.insert(p);
                    out.push_back(std::move(p));
                }
                return out;
            },
            paths, tsv);
        append(tsv, "use", to_write);
    }

    // 记录`path`被打开。仅当它（或其基本名称）最近出现时，
    // 它才是 WIN - 否则读取内容不会告诉我们任何有关
    // 检索质量的信息，并且会被忽略（也不会出现分母膨胀）。
    void note_opened(const maya::below<maya::leaf>& cap, const std::string& path) {
        if (path.empty()) return;
        const std::string tsv = tsv_path().string();
        auto matched = st_.with(cap,
            [](FeedbackState& st, std::string opened, std::string t) {
                ensure_loaded(st, t);
                std::string m = match_recent(st, opened);
                if (!m.empty()) st.counts[m].wins += 1.0;
                return m;
            },
            path, tsv);
        if (!matched.empty()) append(tsv, "win", std::vector<std::string>{matched});
    }

private:
    static constexpr double kMax    = 0.15;   // 最大±15%排名微调
    static constexpr double kPriorN = 4.0;    // 〜半置信度所需的用途

    maya::guarded<FeedbackState> st_;

    static fs::path tsv_path() {
        auto dir = rag_store_dir(kFeedbackSpec);
        if (dir.empty()) return {};
        return dir / "rag_feedback.tsv";
    }

    // 加载 (每PROJECT一次)持续计数，因此轻推在重新启动后仍然存在。
    //
    // 键入已解析的目录，而不是cwd。仅当路径为 cwd-derived 时，
    // 键入 cwd 才是正确的；现在存储已锚定到项目标记，
    // `cd src/` 解析为 相同 文件，并且 cwd 键将在每个
    // chdir 上重新加载它 — 丢弃 in-memory `recent`
    // 集以及此会话的获胜信号。
    static void ensure_loaded(FeedbackState& st, const std::string& tsv) {
        const fs::path p{tsv};
        const std::string key = tsv.empty() ? std::string{} : p.parent_path().string();
        if (st.loaded && st.loaded_for == key) return;
        st.counts.clear();
        st.loaded = true;
        st.loaded_for = key;
        if (tsv.empty()) return;
        std::ifstream f(p);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            // <epoch>\t<use|win>\t<path>
            auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            std::string kind = line.substr(t1 + 1, t2 - t1 - 1);
            std::string path = line.substr(t2 + 1);
            if (path.empty()) continue;
            if (kind == "use") st.counts[path].uses += 1.0;
            else if (kind == "win") st.counts[path].wins += 1.0;
        }
    }

    // 追加在锁之外运行：每一行在ONE write(2)中与O_APPEND
    // 一起出去，因此并发agentty进程从不拼接mid-line。
    static void append(const std::string& tsv, const char* kind,
                       const std::vector<std::string>& paths) {
        if (paths.empty() || tsv.empty()) return;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
        std::string lines;
        for (const auto& path : paths)
            lines += std::to_string(now) + '\t' + kind + '\t' + path + '\n';
        (void)persistence::append_atomic(::agentty::IoAccess::grant(), tsv, lines);
    }

    // 如果精确路径是最近的，或者如果最近的路径以相同的基本名称结束（docs:
    // //foo/bar.md浮出，代理读取foo/bar.md或绝对.../foo/bar.md），
    // 则读取与浮出的段落匹配。
    static std::string match_recent(const FeedbackState& st, const std::string& opened) {
        if (st.recent.contains(opened)) return opened;
        auto slash = opened.find_last_of("/\\");
        std::string base = slash == std::string::npos ? opened
                                                       : opened.substr(slash + 1);
        if (base.empty()) return {};
        for (const auto& r : st.recent) {
            if (r.size() >= base.size() &&
                r.compare(r.size() - base.size(), base.size(), base) == 0) {
                // 防止较长名称上的 bare-basename 错误匹配。
                if (r.size() == base.size() ||
                    r[r.size() - base.size() - 1] == '/' ||
                    r[r.size() - base.size() - 1] == '\\')
                    return r;
            }
        }
        return {};
    }
};

} // 命名空间结束。

Config Config::from_env() {
    Config c;
    if (const char* d = std::getenv("AGENTTY_DOCS_DIR"); d && d[0]) c.docs_root = d;
    // 嵌入器选择+端点，包括旧的AGENTTY_OLLAMA_HOST/AGENTTY_EMBED_MODEL名称。
    // 解析 的一位拥有者。
    ::agentty::rag::embed::apply_env(index_io(), c.embed);
    c.skills        = truthy_default_on("AGENTTY_RAG_SKILLS");
    c.memory        = truthy_default_on("AGENTTY_RAG_MEMORY");
    c.mcp_resources = truthy_default_off("AGENTTY_RAG_MCP");

    c.contextual  = truthy_default_on("AGENTTY_RAG_CONTEXTUAL");
    c.mmr         = truthy_default_on("AGENTTY_RAG_MMR");
    c.mmr_lambda  = env_float("AGENTTY_RAG_MMR_LAMBDA", 0.65f);
    c.stitch      = truthy_default_on("AGENTTY_RAG_STITCH");
    c.dedup       = truthy_default_on("AGENTTY_RAG_DEDUP");
    c.dedup_threshold = env_float("AGENTTY_RAG_DEDUP_THRESHOLD", 0.92f);
    c.autocut     = truthy_default_on("AGENTTY_RAG_AUTOCUT");
    c.autocut_sensitivity = env_float("AGENTTY_RAG_AUTOCUT_SENSITIVITY", 2.0f);
    if (const char* f = std::getenv("AGENTTY_RAG_FUSION"); f && f[0]) {
        std::string fv{f};
        for (char& ch : fv) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        // 仅接受我们实施的两种模式；其他任何东西都会
        // 保持凸默认值，而不是默默地禁用融合。
        if (fv == "rrf" || fv == "convex") c.fusion = fv;
    }
    c.adaptive_fusion = truthy_default_on("AGENTTY_RAG_ADAPTIVE");
    c.prf         = truthy_default_off("AGENTTY_RAG_PRF");
    c.corrective  = truthy_default_off("AGENTTY_RAG_CORRECT");
    c.graph       = truthy_default_off("AGENTTY_RAG_GRAPH");
    // 即使当其模型是本地的时，生成查询扩展也具有可测量的等待时间成本。
    // 这是一种明确的权力-mode，而不是每回合都征税。
    c.expand      = truthy_default_off("AGENTTY_RAG_EXPAND");
    c.hyde        = truthy_default_off("AGENTTY_RAG_HYDE");
    if (const char* g = std::getenv("AGENTTY_RAG_GEN_MODEL"); g && g[0]) c.gen_model = g;
    c.persist     = truthy_default_on("AGENTTY_RAG_PERSIST");
    c.learn       = truthy_default_off("AGENTTY_RAG_LEARN");
    // per-stage检索轨迹是引擎的"show your work"：它为工具
    // 卡渲染的漏斗提供动力，因此默认为ON。 AGENTTY_RAG_TRACE=0
    // 抑制它（仅限紧凑型 one-line 模式 头文件）。
    c.trace       = truthy_default_on("AGENTTY_RAG_TRACE");
    c.dense_weight = env_float("AGENTTY_RAG_DENSE_WEIGHT", 1.0f);
    c.bm25_weight  = env_float("AGENTTY_RAG_BM25_WEIGHT", 1.0f);
    // 主动/"fork"行为（由工具后端消耗）。
    // Pre-turn注入是EXPLICIT opt-in：它在每次知识
    // -shaped转动之前花费model-context代币而无需用户询问，
    // 因此它默认OFF（匹配文档和提交门，其读取Model::env /
    // m.d.persisted.rag）。
    // 用户通过 Ctrl+K → RAG 选择面板 （持续，设置configured=true，
    // 因此此环境默认值不再起作用）或 AGENTTY_RAG_PROACTIVE
    // =1（对于 one-off 运行）打开它。
    c.proactive          = truthy_default_off("AGENTTY_RAG_PROACTIVE");
    c.proactive_min_conf = static_cast<double>(env_float("AGENTTY_RAG_PROACTIVE_MIN", 0.35f));
    c.proactive_bytes    = static_cast<int>(env_float("AGENTTY_RAG_PROACTIVE_BYTES", 6144.0f));
    return c;
}

// ─────────────────────────────────────────────────────────────────────────
// Dense-embedder可用性。三种状态，拼写为三种类型：
//
//   未探测——当前配置尚未运行探测。
//   就绪——嵌入器应答；携带 MEASURED 维度、
//                 向量-space 身份和观察到的延迟。
//   不可用 — 带有 原因。这就是重点：在没有 user-visible
//                 信号的情况下，检索默默地降级到 BM25 是
//                 该子系统最严重的故障模式。
namespace dense {
struct Unprobed {};
struct Ready {
    std::uint64_t identity   = 0;
    std::uint32_t dim        = 0;
    int           latency_ms = 0;
};
struct Unavailable { std::string reason; };
}  // namespace dense
using DenseState = std::variant<dense::Unprobed, dense::Ready, dense::Unavailable>;

// ─────────────────────────────────────────────────────────────────────────
// 检索读取或重建的所有内容。存在于 Impl::index 中，
// 一个 maya::guarded 中：下面的每个方法都在 index.with
// () 中运行，因此 锁 是由接收者隐含的。有意在整个重建过程中举行；
// UI 的状态行使用 try_read，因此 帧 永远不会等待状态行。
//
// 锁顺序：索引排名为lock_levels::rag_index。它的正文通过
// memory_fingerprint()和reindex()读取已
// 学习的-memory存储（memory_store，排在它上面）。每个 正文
// 都会获取它所收到的 `held<rag_index>`，并将其地址存放在
// `held_` 中，持续 正文 的持续时间（IndexBody，如下），
// 并且两个 内存 读取会使用它。 held_ 仅在 锁 内部设置并在释放之前清除，
// 因此功能永远不会比证明它的 正文 寿命更长。
struct Index {
    using held_t = maya::held<lock_levels::rag_index>;
    const held_t* held_ = nullptr;
    [[nodiscard]] const held_t& held() const { return *held_; }

    // 已预热的工作线程的取消 标志，由Impl拥有（在锁之外，
    // 因此关闭可以在重建保持索引时使其脱扣）。 refresh_docs()
    // 和 reindex() 在每个循环边界轮询它，以便 multi
    // -second 嵌入传递立即展开。首先声明：下面的 ctor 需要设置它。
    const std::atomic<bool>* warm_stop = nullptr;

    Config cfg = Config::from_env();

    ::rag::Engine engine;
    bool   embedder_ready = false;
    // 密集-embedder状态机。是 `ollama_probed` + `ollama_ready`：
    // 两个布尔值编码三种状态，不变量仅存在于注释中。现在是 和类型，
    // 其故障臂带有原因字符串 选择面板 和
    // `agentty diagnostics` 渲染。
    DenseState dense{dense::Unprobed{}};
    // 文档索引的新鲜度：(root，指纹)它是为其构建的。
    std::string indexed_root;
    std::uint64_t indexed_fp = 0;
    bool docs_initialized = false;
    std::unordered_map<std::string, std::uint64_t> docs_files;
    std::string persist_checked_for;
    // 内容指纹，而不是项目计数：编辑技能或
    // 替换内存记录而不改变基数必须使检索无效。
    std::uint64_t skills_gen = 0;
    std::uint64_t memory_gen = 0;
    std::uint64_t mcp_gen = 0;

    // 用于延迟-budgeted主动检索的Skills/memory-only引擎。
    // 它是故意分开的，因此冷的 Warm-only 查询永远无法遍历、
    // 重建或丢弃文档语料库。
    ::rag::Engine warm_engine;
    bool warm_initialized = false;
    std::uint64_t warm_skills_gen = 0;
    std::uint64_t warm_memory_gen = 0;

    [[nodiscard]] bool warm_cancelled() const noexcept {
        return warm_stop && warm_stop->load(std::memory_order_relaxed);
    }


    // search_code (cwd 源码树)的独立引擎，
    // 具有其自己的编辑-drift 指纹。与文档引擎分开，因此文档重新
    // 索引永远不会干扰代码搜索和vice-versa。
    ::rag::Engine code_engine{::rag::index::CorpusConfig{}};
    bool          code_embedder_ready = false;
    bool          code_initialized = false;
    std::string code_root;
    std::unordered_map<std::string, std::uint64_t> code_files;

    explicit Index(const std::atomic<bool>* stop) : warm_stop(stop), engine(make_engine_config()) {
        probe_embedder();
        // 探测器可能已经选择了设置前缀的模型（自动）。
        if (!auto_model.empty()) engine = ::rag::Engine(make_engine_config());
        attach_embedder();
        apply_pipeline(engine);
    }

    // 为 HyDE/multi-query 安装 ZERO-COST 本地生成器：
    // 本地 Ollama 上的微型模型。没有云令牌，没有身份验证，没有提供商管道。
    // 如果 Ollama 没有启动，调用 会快速失败，而 HyDE/expand
    // no-op（普通混合动力仍然运行）——不可用时免费，存在时召回获胜。
    // 显式的 set_generator() 会覆盖它。
    //
    // 注意，这是故意的嵌入后端的INDEPENDENT：您
    // 可以通过企业端点嵌入，同时本地Ollama（如果有）
    // 进行查询扩展。仅当该端点本身是 Ollama 时，
    // 它才会拨打嵌入端点，否则为历史本地主机默认值。
    [[nodiscard]] static Retriever::Generator default_generator(const Config& cfg) {
        namespace eb = ::agentty::rag::embed;
        const bool embed_is_ollama =
            cfg.embed.backend == eb::Backend::Ollama
            || cfg.embed.backend == eb::Backend::Auto;
        std::string   host  = embed_is_ollama ? cfg.embed.host : std::string{"127.0.0.1"};
        std::uint16_t port  = embed_is_ollama ? cfg.embed.port : std::uint16_t{11434};
        std::string   model = cfg.gen_model;
        return [host, port, model](const std::string& prompt, int n)
                        -> std::vector<std::string> {
            std::vector<std::string> outs;
            const int want = n > 0 ? n : 1;
            try {
                for (int i = 0; i < want; ++i) {
                    ::rag::plugin::Json body = {
                        {"model", model},
                        {"prompt", prompt},
                        {"stream", false},
                        {"options", {{"temperature", i == 0 ? 0.0 : 0.7},
                                      {"num_predict", 160}}},
                    };
                    ::agentty::http::Request req;
                    req.method    = ::agentty::http::HttpMethod::Post;
                    req.host      = host;
                    req.port      = port;
                    req.path      = "/api/generate";
                    req.plaintext = true;   // 本地Ollama讲普通话HTTP/1.1
                    req.headers.push_back({"content-type", "application/json"});
                    req.body      = body.dump();
                    req.max_body_bytes = 512 * 1024;

                    ::agentty::http::Timeouts to;
                    to.connect = std::chrono::milliseconds(600);   // Ollama缺席 → 快速失败
                    to.total   = std::chrono::milliseconds(4000);  // 每个假设的硬上限
                    auto res = ::agentty::http::default_client(::agentty::IoAccess::grant()).send(req, to);
                    if (!res || res->status != 200) break;   // Ollama下来→放弃
                    auto j = ::rag::plugin::Json::parse(res->body, nullptr, false);
                    if (j.is_discarded() || !j.contains("response")) continue;
                    std::string text = j["response"].get<std::string>();
                    if (!text.empty()) outs.push_back(std::move(text));
                }
            } catch (...) { /* 最佳-effort;空→普通混合 */ }
            return outs;
        };
    }

    ::rag::index::CorpusConfig make_engine_config(bool source_code = false) {
        ::rag::index::CorpusConfig cc;
        // 索引正文在构建和搜索时持有rag_index，
        // 并且在锁下加入助手线程是jaal所禁止的（#112:
        // thread_root 在 -retrieve 中期中止）。
        // 所以这个引擎的工作是内联运行的。成本是一次完成一批冷嵌入；
        // 温暖的索引（持续的或已经建立的）不需要支付任何费用。
        cc.split      = ::rag::util::Splitter{};
        cc.contextual = cfg.contextual;
        std::tie(cc.query_prefix, cc.doc_prefix) = ::agentty::rag::embed::prefixes(effective_embed());
        if (source_code) {
            cc.chunking = ::rag::index::CorpusConfig::Chunking::source;
            // 关于代码使用字的问题("clipboard read")；
            // 该代码使用标识符 (on_clipboard_read)。两者都索引。
            cc.tokenize.split_identifiers = !(std::getenv("AGENTTY_RAG_SPLIT_IDENT")
                                              && std::getenv("AGENTTY_RAG_SPLIT_IDENT")[0] == '0');
        }
        return cc;
    }

    // 根据配置组成FULL-POWER检索管道agentty驱动器。
    // 这镜像了rag-cpp自己的Pipeline::best()漏斗，适应于agentty的per
    // -stage 切换：
    //   [PRF展开]→混合(自适应-convex融合)→过滤器
    //   →特征-rerank→[去重]→[MMR多样性]→[父缝合]→[
    //   自动剪切]→top
    // -k凸(TM2C2)融合是rag-cpp在RRF上测量的-better默认值；
    // 自适应 变体 还会针对每个检索器更有信心的每次查询 re-weights。
    // 对 归并 附近 -duplicate 段落进行重复数据删除，
    // 并自动剪切修剪低 -relevance 末尾 — 均直接来自 best()。
    void apply_pipeline(::rag::Engine& eng) {
        namespace pl = ::rag::pipeline;
        pl::HybridRetrieveConfig hy;
        hy.split        = ::rag::util::Splitter{};
        hy.candidate_k  = 60;
        hy.bm25_weight  = std::max(0.0f, cfg.bm25_weight);
        hy.dense_weight = std::max(0.0f, cfg.dense_weight);
        if (cfg.fusion == "rrf") {
            // 加权倒数-rank融合——尊重公共bm25/dense权重的ONLY模式
            // （凸面在设计上忽略它们）。
            hy.fusion = pl::HybridRetrieveConfig::Fusion::rrf;
        } else {
            // 凸(TM2C2)是默认值：保留RRF丢弃的得分分布。
            // 自适应将每个查询的 alpha 值向具有更尖锐
            // （更自信）的分数曲线的检索器移动。
            hy.fusion            = pl::HybridRetrieveConfig::Fusion::convex;
            hy.convex.adaptive   = cfg.adaptive_fusion;
        }

        pl::Pipeline p;
        if (cfg.prf)
            p.add(std::make_shared<pl::PrfExpandStage>(pl::ExpandConfig{}));
        p.add(std::make_shared<pl::HybridRetrieveStage>(hy));
        p.add(std::make_shared<pl::FilterStage>());
        // 内置-in standard()/quality()/context()
        // 管道使用的精确精度-preserving特征重新排序（确定性，无模型）。
        p.add(pl::make_feature_rerank_stage());
        // 归并接近-duplicate段落BEFORE diversity/stitch，
        // 因此LLM上下文窗口不用于重新-reading解释相同内容的副本。
        if (cfg.dedup) {
            ::rag::rerank::DedupConfig dc;
            dc.threshold = cfg.dedup_threshold;
            p.add(::rag::rerank::make_dedup_stage(dc));
        }
        if (cfg.mmr)
            p.add(::rag::rerank::make_mmr_stage(cfg.mmr_lambda));
        if (cfg.stitch)
            p.add(std::make_shared<pl::ParentStitchStage>(1));
        // 自动剪切在 FINAL 相关性顺序上运行，就在 top-k
        // 之前：它在分数拐点处修剪 low-relevance 末尾，
        // 因此具有三个强答案的查询返回三个，而不是用弱匹配填充 k。
        if (cfg.autocut) {
            ::rag::rerank::AutocutConfig ac;
            ac.sensitivity = cfg.autocut_sensitivity;
            p.add(::rag::rerank::make_autocut_stage(ac));
        }
        p.add(std::make_shared<pl::TopKStage>());
        eng.with_pipeline(std::move(p));
    }

    ::rag::plugin::Json embedder_spec() const {
        const auto s = ::agentty::rag::embed::spec_json(effective_embed());
        return s ? ::rag::plugin::Json::parse(*s) : ::rag::plugin::Json{};
    }

    // 嵌入配置现在应该是DIALLED：已配置的配置，
    // 其中选择了模型汽车的探针（如果它选择了另一个）。
    std::string auto_model;
    [[nodiscard]] ::agentty::rag::embed::EmbedConfig effective_embed() const {
        auto e = cfg.embed;
        if (!auto_model.empty()) e.model = auto_model;
        return e;
    }

    // 每个配置探测配置的嵌入器一次。替换旧的 `ollama_probed`
    // /`ollama_ready` 布尔对：结果是 和类型，
    // 即 CARRIES ITS REASON，因此可以在 UI
    // 中显示故障，而不是永远默默地将检索降级到 BM25（该子系统具有
    // 的单一最坏故障模式）。
    void probe_embedder() {
        if (!std::holds_alternative<dense::Unprobed>(dense)) return;
        apply_probe(run_probe(cfg.embed));   // 汽车重新选型
    }

    // 探针分成两半。 run_probe 拨打网络端点（或 内存-maps
    // 模型文件）并且不触及任何索引状态，因此 apply_config
    // 并重新使用索引 UNLOCKED 运行它：将 锁 跨过 multi
    // -second 超时 会停止每个读取器。 apply_probe
    // 将结果写入 锁 下。
    static ::agentty::rag::embed::ProbeResult
    run_probe(const ::agentty::rag::embed::EmbedConfig& ec) {
        namespace eb = ::agentty::rag::embed;
        if (ec.backend == eb::Backend::Disabled)
            return eb::ProbeErr{"embeddings disabled"};
        return eb::probe(ec);
    }

    void apply_probe(const ::agentty::rag::embed::ProbeResult& r) {
        namespace eb = ::agentty::rag::embed;
        if (const auto* ok = std::get_if<eb::ProbeOk>(&r)) {
            // MEASURED维度是权威的——固定它，
            // 以便每个后续规范（和身份哈希）都携带它。
            cfg.embed.dim = ok->dim;
            // Auto可能选择了比配置的模型更好的模型。
            // 与cfg.embed分开，因此apply_config仍然
            // 比较用户配置的内容。
            const std::string picked = ok->model != cfg.embed.model ? ok->model : std::string{};
            const bool switched = picked != auto_model;
            auto_model = picked;
            dense = dense::Ready{eb::identity(effective_embed()), ok->dim, ok->latency_ms};
            // 另一种模型意味着其他向量和前缀：冷启动文档引擎（
            // reset_embedder处理代码并热启动）。
            if (switched && docs_initialized) {
                engine = ::rag::Engine(make_engine_config());
                embedder_ready = false;
                docs_initialized = false;
                indexed_root.clear();
                indexed_fp = 0;
                docs_files.clear();
                persist_checked_for.clear();
            }
            ::agentty::util::dbglog("rag.embed",
                "ready: " + eb::describe(effective_embed()) + " ("
                + std::to_string(ok->dim) + "d)");
        } else {
            dense = dense::Unavailable{std::get<eb::ProbeErr>(r).why};
            ::agentty::util::dbglog("rag.embed",
                "unavailable: " + std::get<dense::Unavailable>(dense).reason
                + "; using BM25");
        }
    }

    [[nodiscard]] bool dense_ready() const noexcept {
        return std::holds_alternative<dense::Ready>(dense);
    }

    // 嵌入索引锁外部的向量(VectorCache，#112)。
    // 引擎的嵌入器从中读取；检索器在每次锁定构建或搜索之前填充它。
    // shared_ptr<const>：缓存 自行同步，
    // 因此索引可以将其从 锁 正文 中取出。
    std::shared_ptr<const ::agentty::rag::VectorCache> vectors =
        std::make_shared<const ::agentty::rag::VectorCache>();
    // 当前配置的真实嵌入器，用于预取到调用。
    // 与发动机一起建造；当密集关闭时为空。
    std::optional<::rag::dense::AnyEmbedder> real_embedder;

    // 将 缓存-backed 嵌入器给予 `eng`。所有三个引擎
    // 都放在一处，因此默认情况下它们都不能在 锁 下执行网络 IO。
    bool attach_cached_embedder(::rag::Engine& eng) {
        if (!dense_ready()) return false;
        if (!real_embedder) {
            auto e = ::agentty::rag::registries().make_embedder(embedder_spec());
            if (!e) return false;
            real_embedder = std::move(*e);
        }
        eng.with_embedder(::rag::dense::AnyEmbedder{::agentty::rag::CachedEmbedder{*real_embedder, vectors}});
        return true;
    }

    void attach_embedder() {
        embedder_ready = false;
        if (!dense_ready()) return;
        embedder_ready = attach_cached_embedder(engine);
    }

    // 嵌入配置改变(apply_config，
    // re-probe)：重建真正的嵌入器并启动新的缓存。
    // 后端的身份只是其模型名称，因此另一台主机上的相同名称不得获得旧向量。
    // 不调用普通重建：检索的传递1重新索引并且
    // 需要获取向量传递0。
    void reset_embedder() {
        real_embedder.reset();
        // 就地清除，而不是替换：每个引擎的CachedEmbedder
        // 都持有相同的缓存，因此新对象将使旧引擎读取缓存，
        // 不再填充任何东西。
        vectors->clear();
        attach_embedder();
        // 热引擎和代码引擎将旧后端保存在它们的 CachedEmbedder
        // 中：给它们新的后端，并将它们标记为冷的，以便在新模型的
        // 空间中下一个 调用 re-embeds。
        if (warm_initialized) {
            warm_initialized = false;
            warm_engine = ::rag::Engine(make_engine_config());
        }
        if (code_initialized) {
            code_initialized = false;
            code_root.clear();
            code_files.clear();
            code_engine = ::rag::Engine(make_engine_config(/*source_code=*/true));
        }
        attach_code_embedder();
    }

    // 锁和网络如何保持分离(#112)。检索最多运行
    // 其锁定的 正文 两次：
    //
    //   pass 0 照常做一切，但是在它嵌入的地方（带有 缓存
    //           无法应答的块的构建，或者查询向量），
    //           它记录 `wanted` 中的文本并跳过 build/search，
    //           因此在 锁 下不会发生网络 IO。
    //   (解锁)检索器将 `wanted` 嵌入到 缓存 中，
    //           并根据后端的需要发送尽可能多的请求。
    //   第 1 遍再次执行所有操作；现在每个向量都是 缓存 命中。
    //           它永远不会延迟：任何仍然丢失的东西（后端批量
    //           失败）都会以缓慢的方式获取，因此结果总是正确的。
    //
    // 第 1 遍重复廉价工作（文件遍历、分块：毫秒），
    // 而不是嵌入（秒）。温暖的 调用 不需要向量并且可以通过一次。
    std::vector<std::string> wanted;
    int pass = 0;
    // HyDE/multi-query：提示词通道0想要生成（
    // 空：无），并且在通道1上生成器回答的内容已经嵌入。
    // 生成器的型号为 调用，因此与嵌入器一样，它可以解锁运行。
    std::string              gen_prompt;
    int                      gen_n = 1;
    std::vector<std::string> gen_outputs;

    // 当第 0 遍应该在需要嵌入这些文本的工作之前停止时为真（
    // 已在 `wanted` 中注明它们）。 缓存 命中不算数。
    [[nodiscard]] bool defer_for(std::vector<std::string> texts) {
        if (pass != 0 || !real_embedder) return false;
        const std::string_view model = real_embedder->identity();
        ::rag::Vector scratch;
        std::erase_if(texts, [&](const std::string& t) { return vectors->get(model, t, scratch); });
        if (texts.empty()) return false;
        wanted.insert(wanted.end(), std::make_move_iterator(texts.begin()),
                      std::make_move_iterator(texts.end()));
        return true;
    }
    // 同样，对于大约build()的发动机。
    [[nodiscard]] bool defer_build(const ::rag::Engine& eng) {
        return eng.corpus().has_embedder() && defer_for(eng.corpus().pending_texts());
    }

    static void hash_text(std::uint64_t& h, std::string_view value) {
        for (unsigned char c : value) { h ^= c; h *= 1099511628211ull; }
        h ^= 0xff; h *= 1099511628211ull;
    }

    std::uint64_t skills_fingerprint() const {
        std::uint64_t h = 1469598103934665603ull;
        if (!cfg.skills) return h;
        for (const auto& s : tools::skills::all(index_io())) {
            hash_text(h, s.name);
            hash_text(h, s.body);
        }
        return h;
    }

    std::uint64_t memory_fingerprint() const {
        std::uint64_t h = 1469598103934665603ull;
        if (!cfg.memory) return h;
        for (auto scope : {tools::memory::Scope::User, tools::memory::Scope::Project})
            for (const auto& r : tools::memory::load_all(held(), scope)) {
                hash_text(h, r.id);
                hash_text(h, r.text);
            }
        return h;
    }

    std::uint64_t mcp_fingerprint() const {
        std::uint64_t h = 1469598103934665603ull;
        if (!cfg.mcp_resources) return h;
        for (const auto& r : ::agentty::mcp::mcp_resources()) {
            hash_text(h, r.uri);
            hash_text(h, r.title);
            hash_text(h, r.description);
            hash_text(h, r.mime_type);
        }
        return h;
    }

    void ensure_warm_index() {
        const auto sfp = skills_fingerprint();
        const auto mfp = memory_fingerprint();
        if (warm_initialized
            && sfp == warm_skills_gen && mfp == warm_memory_gen) return;
        warm_engine = ::rag::Engine(make_engine_config());
        (void)attach_cached_embedder(warm_engine);
        if (cfg.skills)
            for (const auto& s : tools::skills::all(index_io()))
                if (!s.body.empty())
                    (void)warm_engine.add("skill://" + s.name, s.body,
                                          {{"kind", "skill"}}, s.name);
        if (cfg.memory)
            for (auto scope : {tools::memory::Scope::User, tools::memory::Scope::Project})
                for (const auto& r : tools::memory::load_all(held(), scope))
                    if (!r.text.empty())
                        (void)warm_engine.add("memory://" + r.id, r.text,
                            {{"kind", "memory"},
                             {"scope", std::string(tools::memory::to_string(scope))}});
        // 通过要嵌入的块的0：将warm_initialized
        // 保留为假，因此通过1重建该引擎并找到缓存的每个向量。
        if (defer_build(warm_engine)) return;
        (void)warm_engine.build();
        apply_pipeline(warm_engine);
        warm_skills_gen = sfp;
        warm_memory_gen = mfp;
        warm_initialized = true;
    }

    // 针对当前源集从头开始重建整个引擎。在 `mu` 下调用。 `skip_docs`
    // 表示 "don't WALK the docs folder" — 但已为
    // `indexed_root` 编入索引的任何文档都将被保留（从该文件夹中重新获取
    // -loaded，无需进行新鲜度遍历），因此 warm/proactive 路径永远不会
    // 丢失文档语料库。廉价路径（未更改 fp/gen）通过 needs_reindex
    // () 提前返回。
    void reindex(const fs::path& root, bool skip_docs) {
        engine = ::rag::Engine(make_engine_config());
        embedder_ready = false;
        attach_embedder();

        // Docs/知识文件夹。在温暖路径 (skip_docs) 上，
        // 我们仍然索引 最后 KNOWN 文档根（如果有
        // 的话）——我们只是跳过会检测漂移的 指纹 行走。
        // 这使得文档可以主动检索，而无需支付新鲜度扫描费用。
        fs::path docs_root = root;
        if (skip_docs && docs_root.empty() && !indexed_root.empty())
            docs_root = fs::path{indexed_root};
        if (!docs_root.empty()) {
            ::rag::loaders::DirOptions opts = docs_dir_options();
            auto docs = ::rag::loaders::load_directory(docs_root, opts);
            if (docs) {
                for (auto& d : *docs) {
                    std::string rel = d.meta.count("rel") ? d.meta["rel"] : d.uri;
                    rel = fs::path{rel}.generic_string();
                    d.meta["rel"] = rel;
                    (void)engine.add("docs://" + rel, std::move(d.text), d.meta, d.title);
                }
            }
        }

        // 技能。
        if (cfg.skills) {
            for (const auto& s : tools::skills::all(index_io())) {
                if (s.body.empty()) continue;
                (void)engine.add("skill://" + s.name, s.body, {{"kind", "skill"}}, s.name);
            }
        }

        // 学习了内存(两个范围)。
        if (cfg.memory) {
            for (auto scope : {tools::memory::Scope::User, tools::memory::Scope::Project}) {
                for (const auto& r : tools::memory::load_all(held(), scope)) {
                    if (r.text.empty()) continue;
                    (void)engine.add("memory://" + r.id, r.text,
                               {{"kind", "memory"}, {"scope", std::string(tools::memory::to_string(scope))}});
                }
            }
        }

        // 连接的MCP资源被显式地opt-in，
        // 因为读取它们可能涉及服务器I/O.，每个资源的故障是隔离的。
        if (cfg.mcp_resources) {
            for (const auto& r : ::agentty::mcp::mcp_resources()) {
                if (warm_cancelled()) return;
                std::string err;
                auto text = ::agentty::mcp::mcp_read_resource(r.uri, err);
                if (!text || text->empty()) continue;
                (void)engine.add("mcp://" + r.uri, std::move(*text),
                    {{"kind", "mcp"}, {"server", r.server},
                     {"mime", r.mime_type}}, r.title);
            }
        }

        // 下面的嵌入过程在大型语料库上可能需要几秒钟；在这里保释，
        // 以便进程拆卸/re-seated 温暖不会阻止它。
        if (warm_cancelled()) return;
        // 传递0：首先在锁之外获取向量，然后传递1再次
        // 重新索引并从缓存构建（尚未记录任何内容）。
        if (defer_build(engine)) return;
        auto built = engine.build();
        if (!built)
            ::agentty::util::dbglog("rag.build", std::string(::rag::to_string(built.error().code)));
        apply_pipeline(engine);

        if (!skip_docs) {
            indexed_root = root.string();
            ::rag::loaders::DirOptions opts = docs_dir_options();
            docs_files = file_manifest(root, opts);
            indexed_fp = manifest_fingerprint(docs_files);
            docs_initialized = true;
        } else if (indexed_root.empty() && !root.empty()) {
            indexed_root = root.string();
        }
        skills_gen = skills_fingerprint();
        memory_gen = memory_fingerprint();
        mcp_gen = mcp_fingerprint();

        // 保留构建的语料库和source/config清单。
        // 该清单可防止热打开为另一个根、源生成、
        // 分块配置文件或嵌入模型构建的索引提供服务。
        persist_index();
    }

    // 原子地将文本发布到`dest`：写入同级临时文件、刷新并重命名目标。
    // rename(2) 在同一文件系统上是原子的，因此读者永远不会观察到
    // half-written .meta.json，并且 mid-write
    // 崩溃会使前一个好文件（或没有）完好无损 - 永远不会出现一个会导致
    // 元与其 .ragdb 不同步的截断文件。
    // 派生的缓存，因此没有fsync：断电需要重建，而不是损坏的文件。
    static bool atomic_write(const fs::path& dest, const std::string& body) {
        if (dest.empty()) return false;
        return persistence::write_atomic(::agentty::IoAccess::grant(), dest, body,
                                         {.durability = persistence::Durability::Atomic});
    }

    // 持久化文档索引所在的位置（在工作区 .agentty/下）。
    // 持久化索引路径携带EMBEDDER IDENTITY。
    // 有两个原因，其中之一是修复了正确性错误：
    //
    //  1..ragdb只能由生成SAME向量的嵌入器重用
    //     几何学。用于记录 `embed_model` 的清单 - 一个 NAME，
    //     一旦后端可插入，它就停止识别空间（"nomic-embed-text"
    //     由 Ollama 提供服务，与加载到 -process 中，
    //     因为 GGUF 与 pooling/normalization 不同）。
    //     重新打开一个与另一个会默默地给出错误的余弦：没有崩溃，没有警告，只是糟糕的检索。
    //
    //  2.因为它是在FILENAME中而不是仅仅在舱单中，
    //     在热索引之间切换后端 SWITCHES，
    //     而不是使本地模型和端点之间的空闲 A/B 无效。
    //
    // WHERE它所在的是dirs::Root::Project，
    // 而不是cwd。旧的 `fs::current_path() / ".agentty"`
    // 意味着 `cd src/ && agentty` 构建了 第二 索引
    // （#61），因为路径随 shell 移动。相反，锚点会走到项目标记处，
    // 因此无论您从哪个子目录启动，一次签出都有一个索引。
    //
    // 请注意，这确实 不 迁移由旧版本编写的索引：元
    // 携带语料库 `root` 并在不同时拒绝 加载，
    // 因此旧位置的 过期 文件无论如何都会被拒绝，
    // 而不是默默地重用。它留在磁盘上供 #62 的扫描
    // 收集。
    fs::path ragdb_path() {
        auto dir = rag_store_dir(kDocsIndexSpec);
        if (dir.empty()) return {};
        return dir
             / ("rag_docs." + ::agentty::rag::embed::identity_tag(effective_embed()) + ".ragdb");
    }

    fs::path ragmeta_path() {
        auto p = ragdb_path();
        return p.empty() ? fs::path{} : fs::path{p.string() + ".meta.json"};
    }

    void persist_index() {
        if (!cfg.persist || cfg.mcp_resources) return;
        auto db = ragdb_path();
        auto meta = ragmeta_path();
        if (db.empty() || meta.empty()) return;
        std::error_code ec;
        fs::create_directories(db.parent_path(), ec);
        auto saved = engine.save(db.string());
        if (!saved) return;
        ::rag::plugin::Json j = {
            {"version", 3}, {"root", indexed_root}, {"docs_fp", indexed_fp},
            // Per-file 标记，因此稍后打开可以仅更新
            // 改变的文件而不是重建。
            {"files", docs_files},
            {"skills_fp", skills_gen}, {"memory_fp", memory_gen},
            {"contextual", cfg.contextual},
            // VECTOR SPACE的身份，而不是型号的名称。
            // 请参阅 ragdb_path() 了解名称不够的原因。
            {"embed_identity", ::agentty::rag::embed::identity(effective_embed())},
            {"dense", dense_ready()},
        };
        atomic_write(meta, j.dump());
        sweep_variants(kDocsIndexSpec, "rag_docs.", db);
    }

    bool try_load_persisted(const fs::path& root) {
        if (!cfg.persist || cfg.mcp_resources) return false;
        const std::string root_s = root.string();
        if (persist_checked_for == root_s) return false;
        persist_checked_for = root_s;
        auto db = ragdb_path();
        auto meta = ragmeta_path();
        if (db.empty() || meta.empty()) return false;
        try {
            std::ifstream in(meta);
            if (!in) return false;
            ::rag::plugin::Json j;
            in >> j;
            ::rag::loaders::DirOptions opts = docs_dir_options();
            auto current_files = file_manifest(root, opts);
            const auto current_docs = manifest_fingerprint(current_files);
            const auto current_skills = skills_fingerprint();
            const auto current_memory = memory_fingerprint();
            // 改变的文档文件不拒绝索引：我们采用其存储的文件
            // 列表并且refresh_docs更新不同的文件列表。
            // 如果没有存储列表（旧元），指纹 必须匹配。
            auto stored = j.value("files", FileManifest{});
            const bool docs_ok = !stored.empty()
                || j.value("docs_fp", std::uint64_t{}) == current_docs;
            if (j.value("version", 0) != 3
                || j.value("root", std::string{}) != root_s
                || !docs_ok
                || j.value("skills_fp", std::uint64_t{}) != current_skills
                || j.value("memory_fp", std::uint64_t{}) != current_memory
                || j.value("contextual", false) != cfg.contextual
                || j.value("embed_identity", std::uint64_t{})
                       != ::agentty::rag::embed::identity(effective_embed())
                || j.value("dense", false) != dense_ready())
                return false;
            auto opened = ::rag::Engine::open(db.string());
            if (!opened) return false;
            engine = std::move(*opened);
            // load()无法恢复分路器；这是我们的，不是文件的。
            engine.corpus().set_parallelism(::rag::util::Splitter{}, nullptr);
            embedder_ready = false;
            attach_embedder();
            apply_pipeline(engine);
            indexed_root = root_s;
            if (stored.empty()) {
                docs_files = std::move(current_files);
            } else {
                docs_files = std::move(stored);
            }
            indexed_fp = manifest_fingerprint(docs_files);
            docs_initialized = true;
            skills_gen = current_skills;
            memory_gen = current_memory;
            // 持久索引从不保存MCP资源(cfg.mcp_resources跳过持久化)，
            // 因此这是空的指纹。保留为 0 会使 needs_reindex
            // () 为真，并且每个热打开都会重建。
            mcp_gen = mcp_fingerprint();
            ::agentty::util::dbglog("rag.persist", "opened warm index " + db.string());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool needs_reindex(const fs::path& root, bool skip_docs) {
        // 改变的文档根ALWAYS强制重建——即使在热路径上
        // ——因此在重新指向文件夹之后主动检索永远不能服务
        // 于过期语料库。
        if (!root.empty() && indexed_root != root.string()) return true;
        if (!skip_docs) {
            ::rag::loaders::DirOptions opts = docs_dir_options();
            if (manifest_fingerprint(file_manifest(root, opts)) != indexed_fp) return true;
        }
        if (skills_gen != skills_fingerprint()) return true;
        if (memory_gen != memory_fingerprint()) return true;
        if (mcp_gen != mcp_fingerprint()) return true;
        return false;
    }

    using FileManifest = std::unordered_map<std::string, std::uint64_t>;

    // 有界，为文档/知识语料库构建-aware DirOptions。
    //
    // rag-cpp DEFAULT DirOptions 是 UNBOUNDED
    // (max_files = 0)，每个 -file 上限为 4 MB，
    // 并且排除列表仅知道常见的 VCS/package 目录。当 AGENTTY_DOCS_DIR
    // 指向一个大的 仓库 根（受支持的配置）时，默认行走和 加载 整个 源码树 —
    // 在 _deps/ 下供应的 deps，生成 CMakeFiles/，
    // 构建输出，.agentty 缓存 — 进入 HNSW 密集存储，其支持向量增长
    // 到 n_chunks * 暗淡。在非常大的 代码库 上，分配 就是 OS
    // OOM-kills，而 OOM-kill 不能被守卫每个检索入口点的
    // try/catch 捕获。限制路径（文件 + 字节）并跳过 build/vendor 目录，
    // 以便文档路径与 search_code 路径一样有界。
    static ::rag::loaders::DirOptions docs_dir_options() {
        ::rag::loaders::DirOptions opts;   // 继承rag-cpp include_ext
        opts.extractors.run = ::agentty::rag::converter_runner();
        opts.exclude_dirs = {
            ".git",".hg",".svn","node_modules","build","dist","out",
            "target","venv",".venv","__pycache__",".cache","_deps",
            "CMakeFiles",".agentty","vendor","third_party"};
        opts.max_file_bytes = 1 * 1024 * 1024;   // 1 MB：文档，而不是 blob
        opts.max_files = 6000;                    // 语料库大小的硬上限
        return opts;
    }

    FileManifest file_manifest(const fs::path& root,
                               const ::rag::loaders::DirOptions& opts) {
        FileManifest out;
        if (root.empty()) return out;
        std::unordered_set<std::string> wanted(opts.include_ext.begin(), opts.include_ext.end());
        std::unordered_set<std::string> skipped(opts.exclude_dirs.begin(), opts.exclude_dirs.end());
        std::error_code ec;
        auto flags = fs::directory_options::skip_permission_denied;
        if (opts.follow_symlinks) flags |= fs::directory_options::follow_directory_symlink;
        for (fs::recursive_directory_iterator it(root, flags, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const auto& e = *it;
            if (e.is_directory(ec)) {
                if (skipped.contains(e.path().filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!e.is_regular_file(ec)) continue;
            auto ext = e.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!wanted.contains(ext)) continue;
            auto size = e.file_size(ec);
            if (ec || size > opts.max_file_bytes) { ec.clear(); continue; }
            if (opts.max_files > 0 && out.size() >= opts.max_files) {
                // 这么说吧。上限是一个真正的决定（在 monorepo
                // 上无界行走是一个启动停滞），但是一个语料库默默地
                // 丢失了 仓库 的大部分内容，这使得检索看起来是破碎
                // 的而不是有界的——用户询问一个 IS 存在的文件，
                // 但什么也没有得到，没有办法告诉为什么。
                //
                // 记录在UI中而不是浮出水面：与超大的SKILL.md或惰性
                // 钩子不同，这里没有BROKEN，
                // 并且修复是"narrow the root or raise the cap"
                // 而不是"edit this file"。诊断线的重量合适。
                AGT_LOG(Persist, Warn, "rag",
                        "corpus hit the {} file ceiling under {} — indexing "
                        "only part of it", opts.max_files, root.string());
                break;
            }
            auto mtime = e.last_write_time(ec).time_since_epoch().count();
            if (ec) { ec.clear(); continue; }
            auto rel = fs::relative(e.path(), root, ec).generic_string();
            if (ec) { ec.clear(); continue; }
            std::uint64_t stamp = 1469598103934665603ull;
            hash_text(stamp, rel);
            stamp ^= static_cast<std::uint64_t>(size); stamp *= 1099511628211ull;
            stamp ^= static_cast<std::uint64_t>(mtime); stamp *= 1099511628211ull;
            out.emplace(std::move(rel), stamp);
        }
        return out;
    }

    static std::uint64_t manifest_fingerprint(const FileManifest& files) {
        std::vector<std::pair<std::string, std::uint64_t>> ordered(files.begin(), files.end());
        std::sort(ordered.begin(), ordered.end());
        std::uint64_t h = 1469598103934665603ull;
        for (const auto& [path, stamp] : ordered) {
            hash_text(h, path);
            h ^= stamp; h *= 1099511628211ull;
        }
        return h;
    }

    // 与文档索引相同的身份规则——参见ragdb_path()，
    // 这也解释了为什么这是dirs-resolved而不是cwd-relative。
    fs::path code_ragdb_path() const {
        auto dir = rag_store_dir(kCodeIndexSpec);
        if (dir.empty()) return {};
        return dir
             / ("rag_code." + ::agentty::rag::embed::identity_tag(effective_embed()) + ".ragdb");
    }

    fs::path code_meta_path() const {
        auto db = code_ragdb_path();
        return db.empty() ? fs::path{} : fs::path{db.string() + ".meta.json"};
    }

    bool try_load_code_index(const fs::path& root) {
        if (!cfg.persist) return false;
        auto db = code_ragdb_path();
        auto meta = code_meta_path();
        if (db.empty() || meta.empty()) return false;
        try {
            std::ifstream in(meta);
            if (!in) return false;
            ::rag::plugin::Json j;
            in >> j;
            const auto stored = j.value("files", FileManifest{});
            // 3:用分割标识符对代码进行索引。
            // 之前编写的索引仅包含整个标识符的记录：重建。
            // 改变的文件不拒绝索引：我们采用其存储的文件列表
            // 并且retrieve_code仅更新不同的文件。
            if (j.value("version", 0) != 3
                || j.value("root", std::string{}) != root.string()
                || stored.empty()
                || j.value("contextual", false) != cfg.contextual
                || j.value("embed_identity", std::uint64_t{})
                       != ::agentty::rag::embed::identity(effective_embed())
                || j.value("dense", false) != dense_ready())
                return false;
            auto opened = ::rag::Engine::open(db.string());
            if (!opened) return false;
            code_engine = std::move(*opened);
            code_engine.corpus().set_parallelism(::rag::util::Splitter{}, nullptr);
            attach_code_embedder();
            apply_pipeline(code_engine);
            code_root = root.string();
            code_files = stored;
            code_initialized = true;
            return true;
        } catch (...) { return false; }
    }

    void persist_code_index() {
        if (!cfg.persist || !code_initialized) return;
        auto db = code_ragdb_path();
        auto meta = code_meta_path();
        if (db.empty() || meta.empty()) return;
        std::error_code ec;
        fs::create_directories(db.parent_path(), ec);
        if (!code_engine.save(db.string())) return;
        ::rag::plugin::Json j = {
            {"version", 3}, {"root", code_root}, {"files", code_files},
            {"contextual", cfg.contextual},
            {"embed_identity", ::agentty::rag::embed::identity(effective_embed())},
            {"dense", dense_ready()},
        };
        atomic_write(meta, j.dump());
        sweep_variants(kCodeIndexSpec, "rag_code.", db);
    }

    void refresh_docs(const fs::path& root) {
        ::rag::loaders::DirOptions opts = docs_dir_options();
        auto manifest = file_manifest(root, opts);
        const auto fp = manifest_fingerprint(manifest);
        const bool non_doc_changed = skills_gen != skills_fingerprint()
                                  || memory_gen != memory_fingerprint()
                                  || mcp_gen != mcp_fingerprint();
        if (!docs_initialized || indexed_root != root.string()
            || non_doc_changed) {
            reindex(root, /*skip_docs=*/false);
            return;
        }
        if (fp == indexed_fp) return;

        std::size_t changed = 0;
        for (const auto& [path, stamp] : manifest) {
            auto old = docs_files.find(path);
            if (old == docs_files.end() || old->second != stamp) ++changed;
        }
        for (const auto& [path, _] : docs_files)
            if (!manifest.contains(path)) ++changed;
        if (changed > std::max<std::size_t>(32, docs_files.size() / 3)) {
            reindex(root, /*skip_docs=*/false);
            return;
        }

        auto remove_uri = [&](const std::string& rel) {
            // O(1)URI→id查找而不是对语料库中的每个文档进行
            // 线性扫描——这使得增量文档更新O(改变×
            // corpus_size)并且在大型语料库上是病态的。
            auto& corpus = engine.corpus();
            if (auto id = corpus.find_by_uri("docs://" + rel))
                (void)corpus.remove_document(*id);
        };
        for (const auto& [path, old_stamp] : docs_files) {
            auto now = manifest.find(path);
            if (now == manifest.end() || now->second != old_stamp) remove_uri(path);
        }
        for (const auto& [path, stamp] : manifest) {
            if (warm_cancelled()) return;
            auto old = docs_files.find(path);
            if (old != docs_files.end() && old->second == stamp) continue;
            auto loaded = ::rag::loaders::load_file(root / fs::path{path}, docs_dir_options().extractors);
            if (!loaded) continue;
            loaded->meta["rel"] = path;
            // upsert_document自动地用该uri替换任何实时文档，
            // 因此即使上面的remove_uri错过了逻辑删除条目，
            // 改变的文件也不能留下过期重复项。
            (void)engine.corpus().upsert_document("docs://" + path,
                             std::move(loaded->text),
                             loaded->meta, loaded->title);
        }
        if (warm_cancelled()) return;
        // 通过0：嵌入之前停止。 docs_files 尚未更新，
        // 因此第 1 遍看到已更改的相同文件并再次插入
        // 它们（用相同的文档替换每个文档），然后此构建会
        // 找到 缓存 中的每个向量。
        if (defer_build(engine)) return;
        auto built = engine.build();
        if (!built) {
            reindex(root, /*skip_docs=*/false);
            return;
        }
        apply_pipeline(engine);
        docs_files = std::move(manifest);
        indexed_fp = fp;
        persist_index();
    }

    void attach_code_embedder() {
        code_embedder_ready = false;
        if (!dense_ready()) return;
        code_embedder_ready = attach_cached_embedder(code_engine);
    }
};


// 已预热的工作线程 与检索器共享的内容：索引，加上（在其 锁
// 之外）one-at-a-time 门和 取消 标志 在重建
// 保存索引时关闭跳闸。
// 将正文的持有<rag_index>停放在索引上持续正文的持续时间，
// 因此读取内存存储（排名在索引上方）的索引方法可以通过它。
// 仅独占 正文：共享（读取）正文 永远不会设置它。
struct IndexBody {
    Index& ix;
    // `pass`是两个-pass的运行，调用是(Index::defer_for)。
    IndexBody(Index& i, const Index::held_t& h, int pass = 1) : ix(i) {
        ix.held_ = std::addressof(h);
        ix.pass = pass;
        ix.wanted.clear();
        ix.gen_prompt.clear();
    }
    // gen_outputs由调用者在第1遍之前设置并在第1遍期间使用；
    // 之后清除，这样它就不会泄漏到下一次检索中。
    ~IndexBody() { ix.held_ = nullptr; ix.pass = 1; ix.gen_outputs.clear(); }
    IndexBody(const IndexBody&)            = delete;
    IndexBody& operator=(const IndexBody&) = delete;
};

// 当 0正文 需要解锁完成工作时，它从 锁 中发出了多么好的通行证：
// 要嵌入的文本，并且可能是要从(HyDE/multi-query)生成的提示词。
// 两者都不是：`result`是最终的。
template <class R>
struct Pass {
    R                        result{};
    std::vector<std::string> texts;
    std::string              gen_prompt;
    int                      gen_n = 1;
    [[nodiscard]] bool done() const noexcept { return texts.empty() && gen_prompt.empty(); }
};

// 在正文运行之后：打包结果，加上它推迟的内容（仅传递0）。
template <class R>
Pass<R> finish_pass(Index& ix, R r) {
    Pass<R> p{std::move(r), {}, {}, 1};
    if (ix.pass == 0 && !ix.wanted.empty() && ix.real_embedder) p.texts = std::move(ix.wanted);
    if (ix.pass == 0 && !ix.gen_prompt.empty()) {
        p.gen_prompt = std::move(ix.gen_prompt);
        p.gen_n      = ix.gen_n;
    }
    return p;
}

// 真实嵌入器和从索引读取的缓存用于解锁预取。
// 两者都是自行同步的共享句柄。
struct EmbedHandles {
    std::optional<::rag::dense::AnyEmbedder>           embedder;
    std::shared_ptr<const ::agentty::rag::VectorCache> cache;
};

} // namespace agentty::rag
// AnyEmbedder是对来自任何线程的调用安全的后端的共享
// 不可变句柄（rag-cpp的契约：embed()是常量且可重入的），
// 并且缓存自身同步。将它们从 锁 中取出并共享任何可写内容。
MAYA_SENDABLE(agentty::rag::EmbedHandles);
namespace agentty::rag {

// 两个-pass 调用 (Index::defer_for)的解锁中间：
// 将`texts`嵌入到缓存中，其中与后端所处理的请求数量一样多。
// 没有保存 锁，因此其他检索、已预热的工作线程 和 UI 继续进行。
void prefetch_unlocked(const maya::unlocked& cap, maya::guarded<Index, lock_levels::rag_index>& index,
                       const std::vector<std::string>& texts) {
    const auto h = index.read(cap, [](const Index& ix) {
        return EmbedHandles{ix.real_embedder, ix.vectors};
    });
    if (!h.embedder || !h.cache) return;
    const auto t0 = std::chrono::steady_clock::now();
    auto r = h.cache->prefetch(cap, *h.embedder, texts);
    const auto st = h.cache->stats();
    ::agentty::util::dbglog("rag.prefetch",
        std::to_string(texts.size()) + " texts in "
        + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0).count()) + " ms"
        + "; cache " + std::to_string(st.entries) + " vectors, " + std::to_string(st.hits) + " hits, "
        + std::to_string(st.misses) + " misses so far"
        + (r ? std::string{} : " (error: " + std::string(::rag::to_string(r.error().code)) + ")"));
}

// 检索器的公共API是从工具正文、
// 主动pre-turn路径和RAG设置工作线程调用的——绝不会从agentty
// 锁 正文内部调用(这些对于它们自己的值是无法捕获的并且不能到达检索器)。
// 它的方法也位于测试和 rag 主机使用的稳定公共接口后面。因此，
// 这里的每个 re-states "no lock held"、
// 检索器的审查根 (tests/lint/allowlist.txt)
// 以及它下面的所有内容（索引 锁、内存 存储、已预热的工作线程）
// 都会根据它进行检查。
template <class F>
auto api_root(F&& f) -> decltype(maya::thread_root(std::forward<F>(f))) {
    return maya::thread_root(std::forward<F>(f));
}

struct WarmState {
    std::atomic<bool>    warming{false};
    std::atomic<bool>    warm_stop{false};
    maya::guarded<Index, lock_levels::rag_index> index{std::in_place, &warm_stop};
};
using WarmRef = maya::co_owned<WarmState>;

// 已发布的HyDE/multi-query生成器：被调用，从未被重新分配。
using GeneratorFn = util::Callback<std::vector<std::string>(const std::string&, int)>;

struct Retriever::Impl {
    WarmRef               warm = WarmRef::make();
    std::atomic<bool>&    warming   = warm->warming;
    std::atomic<bool>&    warm_stop = warm->warm_stop;
    maya::guarded<Index, lock_levels::rag_index>& index = warm->index;

    // 可选的LLM 接口边界用于HyDE/multi-query。
    // 在索引之外：它是一个可能拨出的闭包，因此 retrieve() 会取
    // 其中的 快照 而不会是 调用 用户代码，它无法看穿 锁 自己的规则。
    maya::published<const GeneratorFn> generator;

    Impl() {
        generator.publish(std::make_shared<const GeneratorFn>(GeneratorFn{
            Index::default_generator(api_root([&](const maya::unlocked& c) {
                return index.read(c, [](const Index& ix) { return ix.cfg; }); }))}));
    }

    // LAST数据成员ON PURPOSE:成员以相反的顺序销毁，因此组的析构函数
    // (屏障)在~Impl中运行FIRST并且已预热的工作线程永远不能看到半
    // -destroyed Impl。留到最后。 WorkerGroup，
    // 因为 warm_async() 和 shutdown() 从不同的 线程 到达它。
    util::WorkerGroup warmer{"rag.warm"};

    ~Impl() {
        // stop()通过令牌请求停止，该令牌跳闸warm_stop，
        // 因此飞行中的温暖在其下一批中逃逸。
        // 该组自己的析构函数(最后一个成员)停止并加入它。
    }
};

Retriever::Retriever() : impl_(std::make_unique<Impl>()) {}
Retriever::~Retriever() = default;

void Retriever::set_generator(Generator g) {
    impl_->generator.publish(std::make_shared<const GeneratorFn>(GeneratorFn{std::move(g)}));
}

// 当前HyDE生成器上的共享句柄，被移交给索引锁。
// 生成器为用户关闭：默认拨打本地Ollama，
// 不碰agentty 锁； set_generator() 是调用者的，
// 并且始终在索引 锁 下调用，因此这会保持该合同不变。
struct GeneratorRef { std::shared_ptr<const GeneratorFn> fn; };
} // namespace agentty::rag
MAYA_SENDABLE(agentty::rag::GeneratorRef);
namespace agentty::rag {

namespace {
// retrieve()的正文，索引锁定运行。
Retrieval retrieve_locked(Index& ix, const std::string& query, int kk, bool skip_docs,
                          const GeneratorRef& gref) {
    const auto* generator = gref.fn.get();
    Retrieval out;
    auto root = skip_docs ? fs::path{}
                          : resolve_docs_root(ix.cfg.docs_root);

    if (skip_docs) {
        ix.ensure_warm_index();
    } else {
        if (ix.engine.corpus().chunk_count() == 0)
            (void)ix.try_load_persisted(root);
        ix.refresh_docs(root);
    }

    auto& active_engine = skip_docs ? ix.warm_engine : ix.engine;
    // 通道0推迟了构建：在此停止，检索器获取锁
    // 之外的向量并再次运行它。
    if (!ix.wanted.empty()) return out;
    const bool any = active_engine.corpus().chunk_count() > 0;
    if (!any) {
        out.error = "no knowledge configured. Set AGENTTY_DOCS_DIR to a "
                    "folder of docs, put files under ./docs, install skills, "
                    "or store memories to give search_docs something to find.";
        return out;
    }

    // HyDE/multi-query:生成器是型号调用并且其输出被嵌入，
    // 因此两者都运行OUTSIDE和锁 (Index::defer_for)。
    // Pass 0记录的是提示词； Retriever 生成、嵌入输出，并将 1 只手
    // rag-cpp 传递给一个生成器，生成器仅返回它们。 warm/proactive
    // 路径 (skip_docs) 上的 SKIPPED：该路径是 Latency
    // -budgeted（前 -turn 对冲），并且不得等待生成。
    const bool use_llm = !skip_docs && generator && *generator && (ix.cfg.hyde || ix.cfg.expand);
    const int gen_n = ix.cfg.expand ? 3 : 1;
    if (use_llm && ix.pass == 0) {
        ix.gen_prompt = ix.cfg.expand
            ? "Generate 3 alternative phrasings of the following question, one per line.\n\nQuestion: " + query
            : "Write a short passage that directly answers the question.\n\nQuestion: " + query + "\nPassage:";
        ix.gen_n = gen_n;
        (void)ix.defer_for({active_engine.corpus().query_text(query)});   // 其向量随之而来
        return out;
    }
    // 查询自己的向量也是网络调用：在锁之外获取它。
    if (active_engine.corpus().has_embedder()
        && ix.defer_for({active_engine.corpus().query_text(query)})) return out;

    // ── RETRIEVE ──────────────────────────────────────────────────
    // 1.通过全流程的基础混合检索（PRF→凸
    //    融合→过滤器→特征-rerank→MMR→parent-stitch→top-k)。
    //    可选地跟踪模式头文件的每个阶段。
    std::vector<std::string> trace;
    std::vector<std::string>* tracep = ix.cfg.trace ? &trace : nullptr;
    const std::size_t want = static_cast<std::size_t>(kk);

    std::vector<::rag::SearchResult> hits;
    std::string retriever_mode = active_engine.corpus().has_embedder()
                               ? "hybrid+ctx" : "bm25+ctx";

    // 2. 当生成器处于运行状态时，LLM-assisted 检索（HyDE / multi-query）
    //    有线AND启用——关闭查询↔文档不对称间隙并
    //    提升召回率。不存在时优雅地降级为普通搜索。
    //    （在此通道之前生成，在 锁 之外：见上文。）
    bool used_llm = false;
    if (use_llm) {
        ::rag::query::Generator gen =
            [&](std::string_view) -> ::rag::Result<std::vector<std::string>> {
                // 解锁步骤为此查询生成了什么（并且
                // 已经嵌入）。当模型无法访问时为空：
                // HyDE然后回到普通查询，如之前一样。
                return ix.gen_outputs;
            };
        ::rag::Result<std::vector<::rag::Hit>> lh =
            std::unexpected(::rag::Error{});
        if (ix.cfg.expand)
            lh = ::rag::query::multi_query_search(active_engine.corpus(), query, want, gen, gen_n);
        else
            lh = ::rag::query::hyde_search(active_engine.corpus(), query, want, gen);
        if (lh && !lh->empty()) {
            for (const auto& h : *lh) hits.push_back(active_engine.corpus().resolve(h));
            used_llm = true;
            retriever_mode += ix.cfg.expand ? "+multiquery" : "+hyde";
        }
    }

    if (!used_llm) {
        auto res = active_engine.search(query, want, {}, tracep);
        if (!res) { out.error = "retrieval failed"; return out; }
        hits = std::move(*res);
    }

    // 3.可选配GraphRAG扩展。按倒数等级熔断，因为图
    // 和基础分数具有不同的尺度；
    // 在已经-k的基本列表之后追加使得图表命中要么不正确地占主导地位，
    // 要么在没有出现的情况下被修剪。
    if (ix.cfg.graph) {
        try {
            auto g = active_engine.graph_local(query, want);
            if (g && !g->empty()) {
                std::unordered_map<std::string, std::size_t> positions;
                auto key_of = [](const ::rag::SearchResult& r) {
                    return r.uri + "\n" + std::to_string(r.start_line);
                };
                for (std::size_t i = 0; i < hits.size(); ++i) {
                    hits[i].score.value = 1.0f / static_cast<float>(60 + i + 1);
                    positions.emplace(key_of(hits[i]), i);
                }
                for (std::size_t i = 0; i < g->size(); ++i) {
                    const float add = 1.0f / static_cast<float>(60 + i + 1);
                    auto key = key_of((*g)[i]);
                    if (auto it = positions.find(key); it != positions.end()) {
                        hits[it->second].score.value += add;
                    } else {
                        (*g)[i].score.value = add;
                        positions.emplace(std::move(key), hits.size());
                        hits.push_back(std::move((*g)[i]));
                    }
                }
                std::stable_sort(hits.begin(), hits.end(),
                    [](const auto& a, const auto& b) {
                        return a.score.value > b.score.value;
                    });
                retriever_mode += "+graph";
            }
        } catch (...) { /* 图可选 */ }
    }

    // 4.CRAG校正分级：型号-free检索评估器，
    //    删除分级不相关的段落并产生对[0，1]的真实置信度。
    //    这会将 "always inject whatever came back" 的
    //    检索变成 self-checking 步骤。
    double crag_conf = -1.0;
    if (ix.cfg.corrective && !hits.empty()) {
        try {
            std::vector<::rag::Hit> raw;
            raw.reserve(hits.size());
            for (const auto& h : hits) raw.push_back(::rag::Hit{h.chunk, h.score});
            ::rag::crag::CragConfig cc;
            cc.strips = want;
            cc.drop_irrelevant = false;
            auto corr = ::rag::crag::correct(active_engine.corpus(), query, raw, cc);
            crag_conf = static_cast<double>(corr.confidence);
            if (!corr.kept.empty()) {
                // Re-resolve仅保留的块，保留CRAG的顺序。
                std::vector<::rag::SearchResult> kept;
                for (const auto& h : corr.kept)
                    kept.push_back(active_engine.corpus().resolve(h));
                if (!kept.empty()) { hits = std::move(kept); retriever_mode += "+crag"; }
            }
        } catch (...) { /* 分级可选 */ }
    }

    if (hits.empty()) {
        out.error = "no relevant passages (retrieval graded low-confidence)";
        return out;
    }
    if (hits.size() > want) hits.resize(want);

    // 5.学习-loop阅读面：通过其历史推动每个命中
    //    Beta-smoothed win-rate (限制为±15%)，
    //    然后是re-sort，因此在THIS 工作区中反复证明有用的段落
    //    领先于near-tied中无用的段落。对于看不见的路径为中性（
    //    ×1.0），因此这只能细化（而不是发明）排名。关闭与
    //    AGENTTY_RAG_LEARN=0.
    bool learned = false;
    if (ix.cfg.learn) {
        auto& fb = FeedbackStore::instance();
        for (auto& h : hits) {
            auto [src, path] = split_uri(h.uri);
            (void)src;
            float b = fb.boost(ix.held(), path);
            if (b != 1.0f) {
                h.score.value *= b;
                learned = true;
            }
        }
        if (learned)
            std::stable_sort(hits.begin(), hits.end(),
                             [](const ::rag::SearchResult& a,
                                const ::rag::SearchResult& b) {
                                 return a.score.value > b.score.value;
                             });
    }

    double top = 0.0;
    std::vector<std::string> surfaced;
    surfaced.reserve(hits.size());

    // 相关性下限：在花费任何代币之前降低低-confidence 末尾。
    // 最热门的作品总是能生存下来；仅当其得分在顶部的 floor_frac
    // 范围内时，后面的命中才会幸存。这就是节省代币的来源——
    // 模型忽略了 0.1-confidence 段落，
    // 因此我们不需要付费来注入它们。
    {
        double hi = 0.0;
        for (const auto& r : hits) hi = std::max(hi, static_cast<double>(r.score.value));
        const double floor = hi * relevance_floor_frac();
        if (hi > 0.0 && floor > 0.0) {
            std::size_t keep = 1; // 永远保持最好的
            while (keep < hits.size()
                   && static_cast<double>(hits[keep].score.value) >= floor)
                ++keep;
            if (keep < hits.size()) hits.resize(keep);
        }
    }

    // Score-proportional (water-filling)
    // 预算：有信心的段落有完成的空间；保留 末尾 段落得到紧凑的摘录 -
    // 而不是旧的扁平 total/n 分割，在信号和噪声上花费相同的标记。
    // TOTAL 首先按 CRAG 置信度缩小，
    // 因此勉强 -passing 检索注入了一个便宜的块，而不是完整的约 3k tok。
    const std::size_t total_budget =
        confidence_scaled_budget(retrieval_output_budget(), crag_conf);
    std::vector<double> pre_scores;
    pre_scores.reserve(hits.size());
    for (const auto& r : hits) {
        double s = std::clamp(static_cast<double>(r.score.value), 0.0, 1.0);
        pre_scores.push_back(s);
    }
    const std::vector<std::size_t> allowances =
        allocate_budget(pre_scores, total_budget);
    std::size_t remaining_budget = total_budget;
    std::size_t idx = 0;
    for (const auto& r : hits) {
        auto [src, path] = split_uri(r.uri);
        Passage p;
        p.source     = src;
        p.path       = path;
        p.line_start = static_cast<int>(r.start_line);
        p.line_end   = static_cast<int>(r.end_line);
        double s = static_cast<double>(r.score.value);
        if (s < 0.0) s = 0.0;
        if (s > 1.0) s = 1.0;
        p.score      = s;
        std::string raw = r.context.empty() ? r.text : (r.context + "\n" + r.text);
        std::size_t want_bytes = idx < allowances.size() ? allowances[idx] : 768;
        const std::size_t allowance = std::min(want_bytes, remaining_budget);
        ++idx;
        if (allowance < 256) break;
        p.text = compress_passage(query, raw, allowance);
        remaining_budget -= std::min(remaining_budget, p.text.size());
        top = std::max(top, p.score);
        surfaced.push_back(path);
        out.passages.push_back(std::move(p));
    }
    // 学习-loop写方（分母）：记录我们所浮现的内容，
    // 以便其中一个稍后的`read`可以得分为胜利。
    if (ix.cfg.learn && !surfaced.empty())
        FeedbackStore::instance().note_surfaced(ix.held(), surfaced);
    // 优选CRAG的校准置信度（当可用时）；否则得分最高。
    out.confidence = crag_conf >= 0.0 ? crag_conf : top;

    // ── 检索漏斗:显示引擎实际工作 ──────
    // `mode`是工具卡渲染。我们构建一个紧凑的 one-line
    // 标题（检索器 + 融合 + 置信度），然后是 OPTIONAL
    // multi-line "funnel"，引导候选集完成其
    // 实际经历的每个阶段，并记录 rag-cpp 本身的计数。
    // 漏斗是UX的核心：用户（和模型）可以SEE检索到47个候选者，
    // 将其重新排名为30，删除重复数据为24，
    // 并自动剪切为8——而不是信任不透明的黑匣子。
    std::string headline = retriever_mode;
    headline += ix.cfg.fusion == "rrf"
                  ? ", rrf-fusion"
                  : (ix.cfg.adaptive_fusion ? ", convex-fusion(adaptive)"
                                                : ", convex-fusion");
    if (learned) headline += ", learned-boost";
    if (!root.empty() && !skip_docs)
        headline += ", docs=" + root.filename().string();
    char buf[48];
    std::snprintf(buf, sizeof buf, ", confidence %.2f", out.confidence);
    headline += buf;

    std::string m = headline;
    if (ix.cfg.trace && !trace.empty()) {
        // rag-cpp发出两种跟踪线：携带计数的诊断线（
        // "hybrid: 47 candidates"，
        // "dedup 30 -> 24")和仅回显阶段名称的裸阶段
        // 标记（"→ dedup")。保留前者——它们显示漏斗变窄—
        // —并放弃后者，一旦诊断出现，后者就会成为噪音。
        std::vector<std::string> steps;
        steps.reserve(trace.size());
        for (const auto& t : trace) {
            if (t.empty()) continue;
            // 裸露的"→ stagename"标记以箭头开始；跳过它们。
            if (t.rfind("→ ", 0) == 0 || t.rfind("-> ", 0) == 0) continue;
            steps.push_back(t);
        }
        if (!steps.empty()) {
            // 渲染作为锯齿漏斗，卡片逐字显示。每个梯级都是一个阶段；
            // 箭头前缀读取 top-to-bottom
            // 作为沿着管道流动的查询。
            m += "\n  funnel:";
            for (const auto& s : steps)
                m += "\n    ↳ " + s;
            m += "\n    ↳ top-" + std::to_string(out.passages.size());
        }
    }
    out.mode = std::move(m);
    return out;
}
} // 命名空间结束。

Retrieval Retriever::retrieve(const std::string& query, int k, bool skip_docs) {
    Retrieval out;
    if (query.empty()) { out.error = "empty query"; return out; }
    const int kk = k > 0 ? k : 6;
    try {
        out = api_root([&](const maya::unlocked& cap) {
            constexpr auto locked = [](const Index::held_t& h, Index& ix, int pass, std::string query,
                                       int kk, bool skip_docs, GeneratorRef g,
                                       std::vector<std::string> generated) {
                IndexBody body{ix, h, pass};
                ix.gen_outputs = std::move(generated);
                return finish_pass(ix, retrieve_locked(ix, query, kk, skip_docs, g));
            };
            const GeneratorRef g{impl_->generator.current()};
            // 传递0，然后（仅当它询问时）没有锁的慢速部分：
            // HyDE/multi-query生成，并嵌入第二遍将查找的所有内容。
            // 然后传递1（Index::defer_for）。
            auto p = impl_->index.with(cap, locked, 0, query, kk, skip_docs, g, std::vector<std::string>{});
            if (p.done()) return std::move(p.result);
            std::vector<std::string> generated;
            if (!p.gen_prompt.empty() && g.fn && *g.fn) {
                try {
                    generated = (*g.fn)(p.gen_prompt, p.gen_n);
                } catch (...) {
                    generated.clear();   // 无法到达的模型：普通混合型，如前所述
                }
                p.texts.insert(p.texts.end(), generated.begin(), generated.end());
                p.texts.push_back(query);   // HyDE也融合了原始查询
            }
            if (!p.texts.empty()) prefetch_unlocked(cap, impl_->index, p.texts);
            return impl_->index.with(cap, locked, 1, query, kk, skip_docs, g, std::move(generated)).result;
        });
    } catch (const std::exception& e) {
        out.error = std::string("retrieval error: ") + e.what();
    } catch (...) {
        out.error = "retrieval error";
    }
    // 安静地检索没有返回任何内容，
    // 看起来与"the docs have no answer"相同，
    // 因此损坏的索引/丢失的嵌入器/无法到达的Ollama读取为模型失败。记录结果以及
    // 产生该结果的管道。
    AGT_LOGL(Rag, out.error.empty() ? ::agentty::logx::Level::Debug
                                    : ::agentty::logx::Level::Warn,
             "rag.retrieve", "k={} passages={} mode={} err={}",
             k, out.passages.size(),
             out.mode.empty() ? std::string{"-"} : out.mode,
             out.error.empty() ? std::string{"-"} : out.error);
    return out;
}

namespace {

// 查询的小写单词，减去填充符，用于与路径和定义
// 名称匹配。
std::vector<std::string> query_words(std::string_view q) {
    static const std::unordered_set<std::string> filler = {
        "the", "a", "an", "of", "to", "in", "on", "for", "and", "or", "is", "are", "how",
        "what", "where", "when", "why", "does", "do", "it", "its", "that", "this", "with",
        "by", "from", "be", "as", "at", "which", "who", "can", "into", "out", "if", "so"};
    std::vector<std::string> out;
    std::string w;
    auto flush = [&] {
        if (w.size() >= 3 && !filler.contains(w)) out.push_back(w);
        w.clear();
    };
    for (char c : q) {
        if (std::isalnum(static_cast<unsigned char>(c))) w.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else flush();
    }
    flush();
    return out;
}

// 按照寻找实现的人的方式重新排序 search_code 的 over-fetched
// 候选者，然后保留 `k`。在 tests/rag_eval 上测量（关于
// 此 仓库 的 40 个问题）；每个信号都在那里，因为 eval 是这样说的：
//
//   *作用：关于代码的问题想要代码。测试、文档和构建
//     文件已关闭-weighted，除非问题要求它们（"test"、"doc"、
//     "cmake"...）。 头文件 的排名仅低于其 .cpp。
//   *路径：文件名或目录中的查询字（"clipboard"->
//     src/io/clipboard.cpp)是强有力的证据，就像一个人读取源码树的方式
//     一样。
//   * 传播：文件的第二个块计数较少，第三个块计数又较少，
//     因此八个结果覆盖了多个文件而不是一个文件八次。
//   *对：池中的头文件将其.cpp提升到其正上方。
std::vector<::rag::SearchResult> code_rerank(const std::string& query, std::vector<::rag::SearchResult> in,
                                             std::size_t k) {
    if (in.empty()) return in;
    // AGENTTY_RAG_CODE_RERANK=0:引擎的顺序按原样(对于A/B评估)。
    if (const char* e = std::getenv("AGENTTY_RAG_CODE_RERANK"); e && e[0] == '0') {
        if (in.size() > k) in.resize(k);
        return in;
    }
    const auto words = query_words(query);
    auto asks = [&](std::initializer_list<std::string_view> ws) {
        for (const auto& w : words)
            for (auto x : ws)
                if (w.starts_with(x)) return true;
        return false;
    };
    const bool wants_tests = asks({"test", "spec", "fixture", "regression"});
    const bool wants_docs  = asks({"doc", "readme", "guide", "explain", "design"});
    const bool wants_build = asks({"cmake", "build", "compile", "link", "packag", "ci"});

    float hi = 0.f, lo = std::numeric_limits<float>::max();
    for (const auto& r : in) { hi = std::max(hi, r.score.value); lo = std::min(lo, r.score.value); }
    const float range = hi > lo ? hi - lo : 1.f;

    struct Scored { ::rag::SearchResult r; double s; std::string file; };
    std::vector<Scored> v;
    v.reserve(in.size());
    for (auto& r : in) {
        auto [src, path] = split_uri(r.uri);
        (void)src;
        std::string lp = path;
        std::ranges::transform(lp, lp.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        double s = (r.score.value - lo) / range;   // 引擎的相关性，0..1

        const bool is_test  = lp.starts_with("tests/") || lp.contains("/tests/") || lp.contains("_test.")
                           || lp.contains("test_") || lp.contains(".test.");
        const bool is_doc   = lp.ends_with(".md") || lp.ends_with(".rst") || lp.ends_with(".txt")
                           || lp.starts_with("docs/");
        const bool is_build = lp.ends_with(".cmake") || lp.ends_with("cmakelists.txt") || lp.starts_with("packaging/")
                           || lp.starts_with(".github/") || lp.ends_with(".rb") || lp.ends_with(".yml");
        const bool is_header = lp.ends_with(".hpp") || lp.ends_with(".h");
        if (is_test && !wants_tests) s *= 0.55;
        if (is_doc && !wants_docs) s *= 0.6;
        if (is_build && !wants_build) s *= 0.5;
        if (is_header) s *= 0.9;

        // 路径中的查询词：文件名最多，目录较少。
        const auto slash = lp.rfind('/');
        const std::string_view base = slash == std::string::npos ? std::string_view{lp} : std::string_view{lp}.substr(slash + 1);
        double path_hits = 0;
        for (const auto& w : words) {
            const std::string stem = w.size() > 5 ? w.substr(0, w.size() - 1) : w;   // "clipboards"~"clipboard"
            if (base.contains(stem)) path_hits += 1.0;
            else if (std::string_view{lp}.contains(stem)) path_hits += 0.4;
        }
        s += 0.25 * std::min(path_hits, 2.0);
        v.push_back({std::move(r), s, path});
    }

    // 对：排名良好的头文件将其自己的.cpp提升到其上方，
    // 因为问题通常是关于实现的。锁在裸露的茎上：
    // 头文件和源通常位于不同的目录中(auth/与io/)。
    auto stem_of = [](std::string_view p) {
        const auto sl = p.rfind('/');
        if (sl != std::string_view::npos) p = p.substr(sl + 1);
        return std::string{p.substr(0, p.rfind('.'))};
    };
    {
        std::unordered_map<std::string, double> hdr;
        for (const auto& x : v)
            if (x.file.ends_with(".hpp") || x.file.ends_with(".h")) {
                auto& h = hdr[stem_of(x.file)];
                h = std::max(h, x.s);
            }
        for (auto& x : v)
            if (x.file.ends_with(".cpp") || x.file.ends_with(".cc") || x.file.ends_with(".c"))
                if (auto it = hdr.find(stem_of(x.file)); it != hdr.end())
                    x.s = std::max(x.s, it->second * 1.02);
    }
    std::ranges::stable_sort(v, std::greater{}, &Scored::s);

    // 传播：按顺序行走，从文件中折扣每个进一步的块，
    // 并且re-sort一次。 （一张通行证就足够了：折扣只会降低。）
    std::unordered_map<std::string, int> seen;
    for (auto& x : v) {
        const int n = seen[x.file]++;
        if (n == 1) x.s *= 0.7;
        else if (n >= 2) x.s *= 0.45;
    }
    std::ranges::stable_sort(v, std::greater{}, &Scored::s);

    std::vector<::rag::SearchResult> out;
    out.reserve(std::min(k, v.size()));
    for (auto& x : v) {
        if (out.size() >= k) break;
        // 将其余代码读取的分数保持在相同的0..1范围内。
        x.r.score = ::rag::Score{static_cast<float>(std::clamp(x.s, 0.0, 1.0))};
        out.push_back(std::move(x.r));
    }
    return out;
}

// retrieve_code()的正文，索引锁定运行。
Retrieval retrieve_code_locked(Index& ix, const std::string& query, int kk) {
    Retrieval out;
    std::error_code ec;
    auto root = fs::current_path(ec);
    if (ec) { out.error = "search_code: cannot resolve cwd"; return out; }

    ::rag::loaders::DirOptions opts;
    opts.include_ext = {
        ".c",".cc",".cpp",".cxx",".h",".hh",".hpp",".hxx",".inl",
        ".py",".js",".jsx",".ts",".tsx",".mjs",".go",".rs",".java",
        ".kt",".swift",".rb",".php",".cs",".scala",".sh",".bash",
        ".zig",".lua",".sql",".proto",".cmake",".md"};
    opts.exclude_dirs = {
        ".git",".hg",".svn","node_modules","build","dist","out",
        "target","venv",".venv","__pycache__",".cache","_deps",
        "CMakeFiles",".agentty","vendor","third_party"};
    opts.max_file_bytes = 256 * 1024;
    opts.max_files = 4000;

    auto manifest = ix.file_manifest(root, opts);
    if (!ix.code_initialized)
        (void)ix.try_load_code_index(root);
    const bool cold = !ix.code_initialized
                   || ix.code_root != root.string();

    std::size_t changed = 0;
    if (!cold) {
        for (const auto& [path, stamp] : manifest) {
            auto it = ix.code_files.find(path);
            if (it == ix.code_files.end() || it->second != stamp) ++changed;
        }
        for (const auto& [path, _] : ix.code_files)
            if (!manifest.contains(path)) ++changed;
    }
    const bool rebuild = cold
        || changed > std::max<std::size_t>(64, ix.code_files.size() / 3);

    if (rebuild) {
        ix.code_engine = ::rag::Engine(
            ix.make_engine_config(/*source_code=*/true));
        ix.attach_code_embedder();
        auto files = ::rag::loaders::load_directory(root, opts);
        if (files) {
            for (auto& d : *files) {
                std::string rel = d.meta.count("rel") ? d.meta["rel"] : d.uri;
                rel = fs::path{rel}.generic_string();
                d.meta["rel"] = rel;
                (void)ix.code_engine.add("code://" + rel,
                                             std::move(d.text), d.meta, d.title);
            }
        }
        // 通过0:在嵌入之前停止(参见Index::defer_for)。
        if (ix.defer_build(ix.code_engine)) return out;
        auto built = ix.code_engine.build();
        if (!built) { out.error = "search_code: index build failed"; return out; }
        ix.apply_pipeline(ix.code_engine);
    } else if (changed > 0) {
        auto remove_uri = [&](const std::string& rel) {
            // O(1)URI→id查找，不是完整的-corpus线性扫描。
            auto& corpus = ix.code_engine.corpus();
            if (auto id = corpus.find_by_uri("code://" + rel))
                (void)corpus.remove_document(*id);
        };

        for (const auto& [path, old_stamp] : ix.code_files) {
            auto it = manifest.find(path);
            if (it == manifest.end() || it->second != old_stamp)
                remove_uri(path);
        }
        for (const auto& [path, stamp] : manifest) {
            auto old = ix.code_files.find(path);
            if (old != ix.code_files.end() && old->second == stamp) continue;
            auto loaded = ::rag::loaders::load_file(root / fs::path{path});
            if (!loaded) continue;
            loaded->meta["rel"] = path;
            // upsert = dedup-safe 替换（参见文档路径）。
            (void)ix.code_engine.corpus().upsert_document("code://" + path,
                std::move(loaded->text), loaded->meta, loaded->title);
        }
        // 通过0：嵌入之前停止。 code_files 未更新，
        // 因此第 1 遍再次插入相同的文件并从 缓存 构建。
        if (ix.defer_build(ix.code_engine)) return out;
        auto built = ix.code_engine.build();
        if (!built) { out.error = "search_code: incremental update failed"; return out; }
        ix.apply_pipeline(ix.code_engine);
    }
    ix.code_root = root.string();
    ix.code_files = std::move(manifest);
    ix.code_initialized = true;
    if (rebuild || changed > 0) ix.persist_code_index();

    if (ix.code_engine.corpus().chunk_count() == 0) {
        out.error = "search_code: no source files found under " + root.string();
        return out;
    }

    std::vector<std::string> code_trace;
    std::vector<std::string>* code_tracep = ix.cfg.trace ? &code_trace : nullptr;
    // 查询的向量也是网络调用：在锁之外获取它。
    if (ix.code_engine.corpus().has_embedder()
        && ix.defer_for({ix.code_engine.corpus().query_text(query)}))
        return out;
    // Over-fetch，然后用代码问题实际想要的重新
    // 排名（参见code_rerank)：仅引擎的排名将测试、
    // 文档和一个文件的三个块置于问题所涉及的实现之上。
    const std::size_t pool = static_cast<std::size_t>(std::max(kk * 4, 32));
    auto res = ix.code_engine.search(query, pool, {}, code_tracep);
    if (!res) { out.error = "search_code failed"; return out; }
    // AGENTTY_RAG_DUMP_POOL=<file>:将候选池附加
    // 为JSON行，用于离线重排序实验。
    if (const char* dp = std::getenv("AGENTTY_RAG_DUMP_POOL"); dp && *dp) {
        auto pooled = code_rerank(query, *res, res->size());
        ::rag::plugin::Json j;
        j["q"] = query;
        j["c"] = ::rag::plugin::Json::array();
        for (const auto& r : pooled)
            j["c"].push_back({{"path", split_uri(r.uri).second}, {"score", r.score.value},
                              {"text", r.text}});
        (void)persistence::append_atomic(::agentty::IoAccess::grant(), dp, j.dump() + "\n");
    }
    double top = 0.0;
    // 相关性下限+score-proportional预算（与文档
    // 路径相同的基本原理）：不为low-confidence 末尾
    // 支付代币，并给予自信的空间来完成。
    std::vector<::rag::SearchResult> ranked = code_rerank(query, std::move(*res), static_cast<std::size_t>(kk));
    {
        double hi = 0.0;
        for (const auto& r : ranked) hi = std::max(hi, static_cast<double>(r.score.value));
        const double floor = hi * relevance_floor_frac();
        if (hi > 0.0 && floor > 0.0 && !ranked.empty()) {
            std::size_t keep = 1;
            while (keep < ranked.size()
                   && static_cast<double>(ranked[keep].score.value) >= floor)
                ++keep;
            if (keep < ranked.size()) ranked.resize(keep);
        }
    }
    const std::size_t total_budget = confidence_scaled_budget(
        retrieval_output_budget(),
        ranked.empty() ? -1.0
                       : std::clamp(static_cast<double>(ranked.front().score.value),
                                    0.0, 1.0));
    std::vector<double> pre_scores;
    pre_scores.reserve(ranked.size());
    for (const auto& r : ranked)
        pre_scores.push_back(std::clamp(static_cast<double>(r.score.value), 0.0, 1.0));
    const std::vector<std::size_t> allowances =
        allocate_budget(pre_scores, total_budget);
    std::size_t remaining_budget = total_budget;
    std::size_t idx = 0;
    for (const auto& r : ranked) {
        auto [src, path] = split_uri(r.uri);
        (void)src;
        Passage p;
        p.source     = "code";
        p.path       = path;
        p.line_start = static_cast<int>(r.start_line);
        p.line_end   = static_cast<int>(r.end_line);
        p.score      = static_cast<double>(r.score.value);
        std::size_t want_bytes = idx < allowances.size() ? allowances[idx] : 768;
        const std::size_t allowance = std::min(want_bytes, remaining_budget);
        ++idx;
        if (allowance < 256) break;
        // ── 磁盘验证:RAG提议,直播磁盘处理. ────────
        // 索引可以滞后于未索引的编辑； re-read 引用的行范围
        // 来自磁盘上的 当前 文件并提供该服务（事实）。仅当磁盘读取
        // 失败时才回退到索引块文本（自遍历后的文件 moved/deleted）。
        // 保持 search_code 结果正确-on-bytes，
        // 而不仅仅是大约-ranked。
        std::string verified = read_disk_lines(
            (root / fs::path{path}).string(), p.line_start, p.line_end);
        const std::string& source_text = !verified.empty() ? verified : r.text;
        p.text = compress_passage(query, source_text, allowance);
        remaining_budget -= std::min(remaining_budget, p.text.size());
        top = std::max(top, p.score);
        out.passages.push_back(std::move(p));
    }
    out.confidence = top;
    std::string cm = std::string(ix.code_embedder_ready ? "code:hybrid" : "code:bm25")
                   + (ix.cfg.fusion == "rrf" ? ", rrf-fusion"
                      : (ix.cfg.adaptive_fusion ? ", convex-fusion(adaptive)"
                                                    : ", convex-fusion"))
                   + ", " + std::to_string(ix.code_engine.corpus().chunk_count())
                   + " chunks from " + root.filename().string();
    {
        char cbuf[48];
        std::snprintf(cbuf, sizeof cbuf, ", confidence %.2f", out.confidence);
        cm += cbuf;
    }
    if (ix.cfg.trace && !code_trace.empty()) {
        std::vector<std::string> steps;
        steps.reserve(code_trace.size());
        for (const auto& t : code_trace) {
            if (t.empty()) continue;
            if (t.rfind("→ ", 0) == 0 || t.rfind("-> ", 0) == 0) continue;
            steps.push_back(t);
        }
        if (!steps.empty()) {
            cm += "\n  funnel:";
            for (const auto& s : steps) cm += "\n    ↳ " + s;
            cm += "\n    ↳ top-" + std::to_string(out.passages.size());
        }
    }
    out.mode = std::move(cm);
    return out;
}
} // 命名空间结束。

Retrieval Retriever::retrieve_code(const std::string& query, int k) {
    Retrieval out;
    if (query.empty()) { out.error = "empty query"; return out; }
    const int kk = k > 0 ? k : 6;
    try {
        out = api_root([&](const maya::unlocked& cap) {
            constexpr auto locked = [](const Index::held_t& h, Index& ix, int pass, std::string query, int kk) {
                IndexBody body{ix, h, pass};
                return finish_pass(ix, retrieve_code_locked(ix, query, kk));
            };
            // 与retrieve()相同的两次通过：嵌入
            // 冷源码树并行地发生在锁外部。
            auto p = impl_->index.with(cap, locked, 0, query, kk);
            if (p.done()) return std::move(p.result);
            prefetch_unlocked(cap, impl_->index, p.texts);
            return impl_->index.with(cap, locked, 1, query, kk).result;
        });
    } catch (const std::exception& e) {
        out.error = std::string("search_code failed: ") + e.what();
    } catch (...) {
        out.error = "search_code failed";
    }
    return out;
}

bool Retriever::warm() const {
    return api_root([&](const maya::unlocked& cap) {
      return impl_->index.with(cap, [](const Index::held_t& h, Index& ix) {
        IndexBody body{ix, h};
        auto root = resolve_docs_root(ix.cfg.docs_root);
        if (root.empty()) return true;
        if (ix.engine.corpus().chunk_count() == 0)
            (void)ix.try_load_persisted(root);
        return !ix.needs_reindex(root, /*skip_docs=*/false);
      });
    });
}

bool Retriever::code_warm() const {
    return api_root([&](const maya::unlocked& cap) {
      return impl_->index.read(cap, [](const Index& ix) {
        if (ix.code_initialized) return true;
        // 来自先前会话的持久代码索引算作热的：加载它是Engine::
        // open()+清单stat-walk，而不是embed-everything构建。
        // 它的缺失意味着机会主义查询将支付全部冷构建费用——拒绝；
        // 只有明确的 search_code 调用 才应该。
        std::error_code ec;
        return fs::exists(ix.code_ragdb_path(), ec) && !ec;
      });
    });
}

void Retriever::warm_async() {
    bool expected = false;
    if (!impl_->warming.compare_exchange_strong(expected, true)) return;
    // CAS允许一次加热一个LOGICAL。不再需要句柄切换来
    // 同步：旧的 jthread 必须在 mu 下加入和重新安装，
    // 并且该组的 post() 本身不受任何 线程 的影响。
    // 新热传递：从先前的工作线程中清除任何过期 取消。上面的 CAS
    // 意味着没有其他热机正在运行，因此还没有任何内容读取此内容。
    impl_->warm_stop.store(false, std::memory_order_relaxed);
    impl_->warmer.post([](const maya::unlocked& cap, std::stop_token st, WarmRef state) {
        // 将组的stop_token桥接至深度索引功能轮询的标志，
        // 因此关闭会中断嵌入传递。
        std::stop_callback on_stop(st, [&state] {
            state->warm_stop.store(true, std::memory_order_relaxed);
        });
        // 清除门，然而该出口；该组的包装器捕获一次投掷，
        // 但仅在 帧 展开之后。
        struct ClearWarming {
            WarmState& s;
            ~ClearWarming() { s.warming.store(false); }
        } clear{*state};
        // 像retrieve()这样的两个通道：冷文档文件夹的嵌入在
        // 它们之间运行，没有保留锁，因此当已预热的工作线程与嵌入器对话时，
        // search_docs 调用 (或UI)不会保持等待。
        constexpr auto locked = [](const Index::held_t& h, Index& ix, int pass) {
            IndexBody body{ix, h, pass};
            auto root = resolve_docs_root(ix.cfg.docs_root);
            if (ix.engine.corpus().chunk_count() == 0)
                (void)ix.try_load_persisted(root);
            ix.refresh_docs(root);
            return finish_pass(ix, 0);
        };
        auto p = state->index.with(cap, locked, 0);
        if (p.done() || state->warm_stop.load(std::memory_order_relaxed)) return;
        prefetch_unlocked(cap, state->index, p.texts);
        if (state->warm_stop.load(std::memory_order_relaxed)) return;
        (void)state->index.with(cap, locked, 1);
    }, impl_->warm);
}

// 立即停止任何in-flight热并回收工作线程。在进程拆卸的
// 早期调用，因此 ^C 是即时的。 不 故意接触索引：已预热的
// 工作线程 在整个运行过程中都保留它，因此在这里等待它会阻止拆卸。
// 只有 标志 和 工作线程 组，两者都不受任何 线程 的影响。
void Retriever::shutdown() {
    // 1. 使合作社 标志 跳闸，以便 refresh_docs()/reindex() 保释在他们的
    //    下一个循环边界（并且在昂贵的engine.build()之前）。
    impl_->warm_stop.store(true, std::memory_order_relaxed);
    // 2.停止组：请求停止（也触发stop_callback）并且
    //    等待。在实践中受到限制，因为 工作线程 检查网络批次
    //    之间的 标志，因此最坏的情况是 -flight 批次中的一个。
    //    对于任何 线程 和 幂等 都是安全的，具有 ~Impl 自己的停止。
    api_root([&](const maya::unlocked& cap) { impl_->warmer.stop(cap); });
}

// 替换实时配置。任何改变 CORPUS 形状的东西（源、上下文分块、
// 嵌入模型、持久化、融合、管道阶段）都必须使构建的索引无效，以便下一个
// retrieve() 从新配置重建；纯 ranking/labelling
// 切换（学习、跟踪、主动*）在下一个 调用 上使用 副作用，无需重建。
// 我们采取保守路线：如果 任意 corpus-affecting 字段不同，
// 则将所有三个引擎恢复为冷状态，以便 refresh_docs/retrieve_code
// 干净地重建。保留文档根目录（docs_root 不是 选择面板-exposed）。
// EMBEDDER 现在是 选择面板-owned，
// 因此与 docs_root 不同，它是从传入配置中获取的，对其进行更改
// 会强制重新进行 re-probe + 冷重建。
void Retriever::apply_config(const Config& cfg) {
  api_root([&](const maya::unlocked& cap) {
    try {
        // 步骤1，锁定：安装新的配置，
        // 并报告嵌入器是否改变（其探针必须解锁运行）以及语料
        // 库形状是否改变。
        struct Change { bool embedder = false; bool corpus = false;
                        ::agentty::rag::embed::EmbedConfig embed; };
        const Change ch = impl_->index.with(cap, [](Index& ix, Config cfg) {
            const Config& old = ix.cfg;
            Change c;
            // 不同的嵌入器意味着不同的VECTOR
            // SPACE：每个持久向量都是不可比较的，
            // 因此这始终是冷重建（并且.ragdb路径随着身份而改变，
            // 因此当用户切换回来时旧索引在磁盘上保留）。
            c.embedder = ::agentty::rag::embed::identity(old.embed)
                      != ::agentty::rag::embed::identity(cfg.embed);
            c.corpus =
                   old.skills        != cfg.skills
                || old.memory        != cfg.memory
                || old.mcp_resources != cfg.mcp_resources
                || old.contextual    != cfg.contextual
                || old.dedup         != cfg.dedup
                || old.mmr           != cfg.mmr
                || old.stitch        != cfg.stitch
                || old.autocut       != cfg.autocut
                || old.fusion        != cfg.fusion
                || old.adaptive_fusion != cfg.adaptive_fusion
                || old.dense_weight  != cfg.dense_weight
                || old.bm25_weight   != cfg.bm25_weight
                || old.persist       != cfg.persist
                || c.embedder;
            // 仅保留 选择面板 真正从未触及的部分。
            cfg.docs_root = old.docs_root;
            ix.cfg = std::move(cfg);
            if (c.embedder) ix.dense = dense::Unprobed{};
            c.embed = ix.cfg.embed;   // 探测配置了什么；汽车改装-picks
            return c;
        }, cfg);

        // 步骤2，UNLOCKED：探针拨打网络端点（或映射型号）。
        // 在 multi-second 超时 上保存索引是让
        // 每个读者（包括 UI 的状态行）停滞在其后面的原因。
        // 读者看到Unprobed直到结果出现。
        std::optional<::agentty::rag::embed::ProbeResult> probed;
        if (ch.embedder) probed = Index::run_probe(ch.embed);

        // 步骤3、锁定：将探头着陆，然后复位或re-pipeline。
        impl_->index.with(cap, [](Index& ix, Change c,
                             std::optional<::agentty::rag::embed::ProbeResult> pr) {
            if (pr) ix.apply_probe(*pr);
            if (c.corpus) {
                // Cold-start每台发动机；管道是在下一个 reindex/refresh.
                // 上从新的 切换 重建的
                ix.engine = ::rag::Engine(ix.make_engine_config());
                ix.embedder_ready = false;
                ix.docs_initialized = false;
                ix.indexed_root.clear();
                ix.indexed_fp = 0;
                ix.docs_files.clear();
                ix.persist_checked_for.clear();
                ix.warm_initialized = false;
                ix.warm_engine = ::rag::Engine(ix.make_engine_config());
                ix.code_initialized = false;
                ix.code_root.clear();
                ix.code_files.clear();
                ix.reset_embedder();
            } else {
                // Ranking-only变化：re-apply管道就位，
                // 因此便宜的re-labels（融合标签）刷新而无需重建。
                ix.apply_pipeline(ix.engine);
            }
        }, ch, std::move(probed));
        // 默认生成器在为Ollama时拨打嵌入主机，
        // 因此新的嵌入器意味着新的默认生成器。
        if (ch.embedder)
            impl_->generator.publish(std::make_shared<const GeneratorFn>(
                GeneratorFn{Index::default_generator(snapshot_config())}));
    } catch (...) { /* 最佳-effort;在失败时保持旧配置不变 */ }
  });
}

Config Retriever::snapshot_config() const {
    return api_root([&](const maya::unlocked& cap) { return impl_->index.read(cap, [](const Index& ix) { return ix.cfg; }); });
}

Retriever::EmbedStatus Retriever::embed_status() const {
    EmbedStatus out;

    // NEVER块。这从 UI 线程 到 渲染 称为状态行，
    // 索引在整个重建过程中保持不变。这里的阻塞读取意味着
    // UI 冻结（包括动画），只要重建花费了很长的时间。
    //
    // 状态读数本质上是建议性的：如果检索器正忙于重新配置，
    // 则 "reconfiguring" IS 是真实答案，
    // 并且这样说不需要任何成本。下一个 帧 拾取真实状态。
    auto now = api_root([&](const maya::unlocked& cap) { return impl_->index.try_read(cap, [](const Index& ix) {
        EmbedStatus st_out;
        st_out.describe = ::agentty::rag::embed::describe(ix.effective_embed());
        std::visit([&st_out](const auto& st) {
            using T = std::decay_t<decltype(st)>;
            if constexpr (std::is_same_v<T, dense::Ready>) {
                st_out.state      = EmbedStatus::State::Ready;
                st_out.dim        = st.dim;
                st_out.latency_ms = st.latency_ms;
            } else if constexpr (std::is_same_v<T, dense::Unavailable>) {
                st_out.state  = EmbedStatus::State::Unavailable;
                st_out.reason = st.reason;
            } else {
                st_out.state = EmbedStatus::State::Unprobed;
            }
        }, ix.dense);
        return st_out;
    }); });
    if (!now) {
        out.state    = EmbedStatus::State::Unprobed;
        out.describe = "reconfiguring\xe2\x80\xa6";
        return out;
    }
    return *now;
}

Retriever::EmbedStatus Retriever::reprobe_embedder() {
    // 与apply_config相同的规则：探测器本身运行UNLOCKED，
    // 因为它拨打网络端点。只有state write和re-attach走锁，
    // 而且都很便宜。
    return api_root([&](const maya::unlocked& cap) {
    const auto ec = impl_->index.with(cap, [](Index& ix) {
        ix.dense = dense::Unprobed{};
        return ix.cfg.embed;
    });
    auto r = Index::run_probe(ec);
    impl_->index.with(cap, [](Index& ix, ::agentty::rag::embed::ProbeResult pr) {
        ix.apply_probe(pr);
        ix.reset_embedder();
    }, std::move(r));
    return embed_status();
    });
}

// ── 学习循环( 写侧) ─────────────
// 两个半部都委托给进程-wide FeedbackStore，该进程拥有TSV和
// read-side微移。最佳-effort；从不抛出。
namespace feedback {
void note_surfaced(const maya::below<maya::leaf>& cap, const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    try {
        if (truthy_default_off("AGENTTY_RAG_LEARN"))
            FeedbackStore::instance().note_surfaced(cap, paths);
    } catch (...) { /* 最佳-effort */ }
}
void note_file_opened(const maya::below<maya::leaf>& cap, const std::string& path) {
    // 最近一段所指向的文件的`read`是IMPLICIT相关性判断。
    // 如果该路径最近通过检索出现，则 FeedbackStore 会记入
    // "win" 仅 — 读取 never-surfaced 文件不是一个
    // 信号并且会被丢弃，因此 win-rate 仍可归因。
    if (path.empty()) return;
    try {
        if (truthy_default_off("AGENTTY_RAG_LEARN"))
            FeedbackStore::instance().note_opened(cap, path);
    } catch (...) { /* 最佳-effort */ }
}
} // namespace feedback

// ── CLI: `agentty rag-bench <root>` ────────────────────────────────────
// 你无法测量的管道是一堆振动。 rag-bench 评估检索漏斗
// ON YOUR 自身 CORPUS，离线，以毫秒为单位 - 无 LLM，
// 无网络，完全确定性。
//
// 方法（已知-item检索）：
//   1.对语料库进行索引（BM25 +本地哈希-dense 回退，如果Ollama已关闭）。
//   2.样本块；对于每个，从其最接近的位置合成一个查询
//      DISCRIMINATIVE 条款（最高 BM25 条款贡献，通过 Corpus::explain）。
//      生成查询的块是该查询的已知GOLD答案。
//   3.通过检索LADDER运行每个查询——每个梯级添加一个阶段：
//        bm25-only→混合+prf→+feature
//      -rerank→+mmr并报告每个梯级的recall@k、MRR和nDCG@10。
//      因为每个梯级恰好添加一个阶段，所以梯级上的 DROPS 表示值得调整的
//      阶段 — 并且每个 AGENTTY_RAG_* 旋钮都可以根据数字进行设置。
namespace bench {
namespace {

struct Query {
    std::string text;
    std::uint32_t gold;   // 源块的ChunkId值
};

struct Metrics {
    double recall = 0.0, mrr = 0.0, ndcg = 0.0;
    std::size_t n = 0;
    void add(int rank /* 1-based，0 = 未命中 */, std::size_t k) {
        ++n;
        if (rank >= 1 && static_cast<std::size_t>(rank) <= k) {
            recall += 1.0;
            mrr    += 1.0 / rank;
        }
        // 单个相关文档 ⇒ IDCG = 1;进入前 10 名时，DCG = 1/log2(排名+1)。
        if (rank >= 1 && rank <= 10)
            ndcg += 1.0 / std::log2(static_cast<double>(rank) + 1.0);
    }
    void finalize() { if (n) { recall /= n; mrr /= n; ndcg /= n; } }
};

// 黄金块在命中列表中的排名为1-based，或者如果不存在则为0。
int rank_of(const std::vector<::rag::Hit>& hits, std::uint32_t gold, std::size_t k) {
    for (std::size_t i = 0; i < hits.size() && i < k; ++i)
        if (hits[i].chunk.value == gold) return static_cast<int>(i) + 1;
    return 0;
}

// 将块的文本分割成不同的字母数字术语（确定性顺序）。
std::vector<std::string> distinct_terms(const std::string& text) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    std::string cur;
    auto flush = [&] {
        if (cur.size() >= 3 && seen.insert(cur).second) out.push_back(cur);
        cur.clear();
    };
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else flush();
    }
    flush();
    return out;
}

} // 命名空间结束。

int run(const std::string& root) {
    fs::path r = resolve_docs_root(root);
    if (r.empty()) {
        std::fprintf(stderr, "rag-bench: no docs root (pass a folder or set AGENTTY_DOCS_DIR)\n");
        return 2;
    }
    try {
        auto cfg = Config::from_env();
        ::rag::Engine engine;
        bool have_dense = false;
        // 适配器使用相同的 SSOT — 该站点曾经创建自己的 {
        // "type":"ollama"} 文字，并且已经偏离了它。
        if (const auto spec = ::agentty::rag::embed::spec_json(cfg.embed);
            spec && engine.with_embedder_spec(::rag::plugin::Json::parse(*spec), ::agentty::rag::registries())) {
            auto probe = engine.corpus().embed_text("rag benchmark availability probe");
            have_dense = probe.has_value() && !probe->empty();
        }
        if (!have_dense) engine = ::rag::Engine{};

        auto t0 = std::chrono::steady_clock::now();
        ::rag::loaders::DirOptions opts;
        // 匹配交互式文档路径：跳过build/vendor目录并限制语料库，
        // 使得巨大的源码树上的`rag-bench`不能像无界行走
        // 那样OOM。
        opts.exclude_dirs = {
            ".git",".hg",".svn","node_modules","build","dist","out",
            "target","venv",".venv","__pycache__",".cache","_deps",
            "CMakeFiles",".agentty","vendor","third_party"};
        opts.max_file_bytes = 1 * 1024 * 1024;
        opts.max_files = 6000;
        auto docs = ::rag::loaders::load_directory(r, opts);
        std::size_t n = 0;
        if (docs) for (auto& d : *docs) { (void)engine.add(d.uri, std::move(d.text), d.meta, d.title); ++n; }
        (void)engine.build();
        auto t1 = std::chrono::steady_clock::now();
        auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        const auto& corpus = engine.corpus();
        std::size_t nchunks = corpus.chunk_count();
        std::printf("rag-bench: indexed %zu docs, %zu chunks from %s in %lld ms "
                    "(dense: %s)\n",
                    n, nchunks, r.string().c_str(),
                    static_cast<long long>(build_ms),
                    have_dense ? "ollama" : "bm25-only");
        if (nchunks == 0) { std::fprintf(stderr, "rag-bench: empty corpus\n"); return 1; }

        // 2.合成known-item查询。均匀采样约 200 个块，以便
        //    大型语料库仍然以毫秒为单位并且可重复地运行。
        const std::size_t kQueryBudget = 200;
        const std::size_t stride = nchunks > kQueryBudget ? nchunks / kQueryBudget : 1;
        std::vector<Query> queries;
        {
            auto lease = corpus.chunks();
            for (std::size_t i = 0; i < lease.size(); i += stride) {
                const auto& ch = lease[i];
                auto terms = distinct_terms(ch.text);
                if (terms.size() < 3) continue;
                // 根据术语对 此 块的 BM25 贡献对术语进行排名，
                // 保留前几个最具辨别力的 — 已知的 -item 查询。
                std::vector<std::pair<float, std::string>> scored;
                scored.reserve(terms.size());
                for (const auto& t : terms) {
                    auto ex = corpus.explain(t, ch.id);
                    scored.emplace_back(ex.lexical_score, t);
                }
                std::sort(scored.begin(), scored.end(),
                          [](auto& a, auto& b) { return a.first > b.first; });
                std::string q;
                for (std::size_t j = 0; j < scored.size() && j < 5; ++j) {
                    if (scored[j].first <= 0.0f) break;
                    if (!q.empty()) q += ' ';
                    q += scored[j].second;
                }
                if (q.empty()) continue;
                queries.push_back({std::move(q), ch.id.value});
            }
        }
        if (queries.empty()) { std::fprintf(stderr, "rag-bench: no queries synthesized\n"); return 1; }

        // 3.组装梯子——每梯级添加一级。
        namespace pl = ::rag::pipeline;
        auto hybrid_cfg = [&](bool dense) {
            pl::HybridRetrieveConfig hy;
            hy.candidate_k  = 60;
            hy.bm25_weight  = 1.0f;
            hy.dense_weight = dense ? 1.0f : 0.0f;
            // 凸融合IGNORES每个-retriever权重（它使用
            // convex.alpha)，因此为了实际上隔离第一个梯级的BM25，
            // 我们回退到加权RRF，这遵循dense_weight = 0。
            hy.fusion = dense ? pl::HybridRetrieveConfig::Fusion::convex
                              : pl::HybridRetrieveConfig::Fusion::rrf;
            return hy;
        };
        struct Rung { const char* name; pl::Pipeline pipe; };
        std::vector<Rung> ladder;
        {   // bm25-only
            pl::Pipeline p;
            p.add(std::make_shared<pl::HybridRetrieveStage>(hybrid_cfg(false)));
            p.add(std::make_shared<pl::TopKStage>());
            ladder.push_back({"bm25-only", std::move(p)});
        }
        {   // 混合+prf
            pl::Pipeline p;
            p.add(std::make_shared<pl::PrfExpandStage>(pl::ExpandConfig{}));
            p.add(std::make_shared<pl::HybridRetrieveStage>(hybrid_cfg(true)));
            p.add(std::make_shared<pl::TopKStage>());
            ladder.push_back({"hybrid+prf", std::move(p)});
        }
        {   // + 特征-rerank
            pl::Pipeline p;
            p.add(std::make_shared<pl::PrfExpandStage>(pl::ExpandConfig{}));
            p.add(std::make_shared<pl::HybridRetrieveStage>(hybrid_cfg(true)));
            p.add(pl::make_feature_rerank_stage());
            p.add(std::make_shared<pl::TopKStage>());
            ladder.push_back({"+feature-rerank", std::move(p)});
        }
        {   // + mmr (完整质量漏斗，PRF-prepended)
            pl::Pipeline p = pl::Pipeline::quality_with(hybrid_cfg(true), cfg.mmr_lambda);
            ladder.push_back({"+mmr", std::move(p)});
        }

        const std::size_t k = 10;
        std::printf("\n  %-16s  recall@%zu   MRR    nDCG@10   ms/query\n",
                    "stage", k);
        std::printf("  %-16s  --------  ------  -------  --------\n", "----------------");
        for (auto& rung : ladder) {
            Metrics m;
            auto s0 = std::chrono::steady_clock::now();
            for (const auto& q : queries) {
                auto hits = rung.pipe.run(corpus, q.text, k);
                if (!hits) { m.add(0, k); continue; }
                m.add(rank_of(*hits, q.gold, k), k);
            }
            auto s1 = std::chrono::steady_clock::now();
            double per = std::chrono::duration_cast<std::chrono::microseconds>(s1 - s0).count()
                         / 1000.0 / static_cast<double>(queries.size());
            m.finalize();
            std::printf("  %-16s   %.3f   %.3f   %.3f     %6.2f\n",
                        rung.name, m.recall, m.mrr, m.ndcg, per);
        }
        std::printf("\n  %zu known-item queries over %zu chunks. "
                    "A metric that DROPS at a rung is the stage to tune.\n",
                    queries.size(), nchunks);

        // 4.TOKEN-COST测量（opt-in经由AGENTTY_RAG_MEASURE = 1)。运行
        //    通过REAL Retriever::retrieve()输出路径
        //    合成查询——该输出路径应用相关性下限、water-fill、
        //    confidence-scaled预算和提取压缩——
        //    并统计实际注入到模型回合中的bytes/tokens。
        //    运行 二进制程序 两次（通过 env 打开或关闭控制杆）并比较以查看真正的节省。
        //    代币估算：bytes/4（Claude/GPT 英文启发式）。
        if (const char* mv = std::getenv("AGENTTY_RAG_MEASURE"); mv && mv[0] == '1') {
            Retriever ret;
            std::size_t total_bytes = 0, total_passages = 0, empty = 0;
            std::size_t sampled = 0;
            const std::size_t kMax = std::min<std::size_t>(queries.size(), 80);
            const std::size_t qstride = queries.size() > kMax ? queries.size() / kMax : 1;
            auto q0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < queries.size(); i += qstride) {
                auto rr = ret.retrieve(queries[i].text, /*k=*/8, /*skip_docs=*/false);
                ++sampled;
                if (rr.passages.empty()) { ++empty; continue; }
                total_passages += rr.passages.size();
                for (const auto& p : rr.passages) total_bytes += p.text.size();
            }
            auto q1 = std::chrono::steady_clock::now();
            double per_ms = std::chrono::duration_cast<std::chrono::microseconds>(q1 - q0).count()
                            / 1000.0 / static_cast<double>(std::max<std::size_t>(sampled, 1));
            const double est_tokens = static_cast<double>(total_bytes) / 4.0;
            std::printf("\n  ── TOKEN COST (real retrieve() output path) ──\n");
            std::printf("  queries measured : %zu (%zu returned nothing)\n", sampled, empty);
            std::printf("  passages injected: %zu total, %.2f avg/query\n",
                        total_passages,
                        static_cast<double>(total_passages)
                            / static_cast<double>(std::max<std::size_t>(sampled, 1)));
            std::printf("  output bytes     : %zu total, %.0f avg/query\n",
                        total_bytes,
                        static_cast<double>(total_bytes)
                            / static_cast<double>(std::max<std::size_t>(sampled, 1)));
            std::printf("  est. tokens      : %.0f total, %.0f avg/query (bytes/4)\n",
                        est_tokens,
                        est_tokens / static_cast<double>(std::max<std::size_t>(sampled, 1)));
            std::printf("  retrieve latency : %.2f ms/query\n", per_ms);
            // 发出用于脚本化before/after比较的machine-readable行。
            std::printf("  MEASURE\ttokens=%.0f\tbytes=%zu\tpassages=%zu\tqueries=%zu\n",
                        est_tokens, total_bytes, total_passages, sampled);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rag-bench: %s\n", e.what());
        return 1;
    }
}

// 使用正斜杠将通道路径标准化为仓库-relative，
// 因此可以将其与评估的黄金路径进行比较。
static std::string rel_path(const std::string& p, const fs::path& root) {
    std::string s = p;
    for (const char* pre : {"code://", "docs://"})
        if (s.starts_with(pre)) s.erase(0, std::strlen(pre));
    std::error_code ec;
    fs::path fp{s};
    if (fp.is_absolute()) {
        auto r = fs::relative(fp, root, ec);
        if (!ec) fp = r;
    }
    return fp.generic_string();
}

int eval(const std::string& eval_file, int k) {
    std::ifstream in(eval_file);
    if (!in) {
        std::fprintf(stderr, "rag-eval: can't read %s\n", eval_file.c_str());
        return 2;
    }
    ::rag::plugin::Json cases;
    try { in >> cases; } catch (...) {
        std::fprintf(stderr, "rag-eval: %s is not JSON\n", eval_file.c_str());
        return 2;
    }
    // 裸阵列为search_code例； {"tool":"search_docs","cases":
    // [...]} 运行 search_docs，具有相对于文档根目录的黄金路径。
    const bool docs = cases.is_object() && cases.value("tool", std::string{}) == "search_docs";
    if (cases.is_object()) {
        ::rag::plugin::Json inner = cases.value("cases", ::rag::plugin::Json::array());
        cases = std::move(inner);
    }
    const fs::path root = docs ? resolve_docs_root({}) : fs::current_path();
    if (root.empty()) {
        std::fprintf(stderr, "rag-eval: no docs root (set AGENTTY_DOCS_DIR)\n");
        return 2;
    }
    Retriever ret;
    auto search = [&](const std::string& q, int kk) {
        return docs ? ret.retrieve(q, kk) : ret.retrieve_code(q, kk);
    };
    const auto t0 = std::chrono::steady_clock::now();
    // 首先调用建立索引；与查询分开计时。
    (void)search("warm up", 1);
    const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    int n = 0, hit1 = 0, hitk = 0;
    double mrr = 0;
    std::vector<std::string> misses;
    // AGENTTY_RAG_EVAL_ALL=1打印每个案例的排名和每个未命中的结果。
    const char* all_env = std::getenv("AGENTTY_RAG_EVAL_ALL");
    const std::size_t show = (all_env && all_env[0] == '1') ? SIZE_MAX : 3;
    const auto q0 = std::chrono::steady_clock::now();
    for (const auto& c : cases) {
        const std::string q = c.value("q", std::string{});
        std::unordered_set<std::string> gold;
        for (const auto& g : c.value("gold", std::vector<std::string>{})) gold.insert(g);
        if (q.empty() || gold.empty()) continue;
        ++n;
        const auto r = search(q, k);
        // 文件-level排名：任何黄金文件的第一段。
        int rank = 0;
        std::vector<std::string> got;
        for (std::size_t i = 0; i < r.passages.size(); ++i) {
            const auto rp = rel_path(r.passages[i].path, root);
            if (got.size() < show) got.push_back(rp);
            if (!rank && gold.contains(rp)) rank = static_cast<int>(i) + 1;
        }
        if (rank == 1) ++hit1;
        if (show == SIZE_MAX) {
            std::printf("  rank %d  %s\n", rank, q.c_str());
            if (rank != 1) {
                std::printf("        top:");
                for (std::size_t i = 0; i < got.size() && i < 3; ++i) std::printf(" %s", got[i].c_str());
                std::printf("\n");
            }
        }
        if (rank >= 1) { ++hitk; mrr += 1.0 / rank; }
        else {
            std::string m = "  MISS  " + q + "\n        got:";
            for (const auto& g : got) m += " " + g;
            if (!r.error.empty()) m += " (error: " + r.error + ")";
            misses.push_back(std::move(m));
        }
    }
    const auto q_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - q0).count();
    std::printf("rag-eval: %d cases, k=%d, index %lld ms, %lld ms/query\n", n, k,
                static_cast<long long>(build_ms), n ? static_cast<long long>(q_ms / n) : 0LL);
    std::printf("  hit@1   %.3f\n  hit@%d  %.3f\n  MRR     %.3f\n", n ? double(hit1) / n : 0.0, k,
                n ? double(hitk) / n : 0.0, n ? mrr / n : 0.0);
    for (const auto& m : misses) std::printf("%s\n", m.c_str());
    return 0;
}
} // namespace bench

} // namespace agentty::rag

#else  // !AGENTTY_HAS_RAGCPP  ─────────────────────────────────────────────

// 在该平台上无法检索(e.g.MSVC，直到rag-cpp为Windows-portable)。
// 来自 rag_adapter.hpp 的每个公共 符号 在这里被定义为 no-op，
// 因此 agentty 链接 并运行； search_docs/search_code
// 显示清晰的 "unavailable" 消息，而不是崩溃或返回 过期 数据。

#include <cstdio>
#include <cstdlib>

namespace agentty::rag {

Config Config::from_env() {
    // 默认-constructed配置即可；在 no-op 构建中没有任何内容读取它，
    // 但测试和调用者仍然可以构建一个。
    return Config{};
}

// Impl是空的——头文件存储不透明的指针，我们简单地将其保留为空。
struct Retriever::Impl {};

Retriever::Retriever() : impl_(nullptr) {}
Retriever::~Retriever() = default;

void Retriever::set_generator(Generator /*g*/) {}

static Retrieval unavailable_() {
    Retrieval r;
    r.mode  = "retrieval unavailable on this platform";
    r.error = "retrieval engine (rag-cpp) is not built on this platform";
    r.confidence = 0.0;
    return r;
}

Retrieval Retriever::retrieve(const std::string& /*查询*/, int /*k*/,
                              bool /*skip_docs*/) {
    return unavailable_();
}

Retrieval Retriever::retrieve_code(const std::string& /*查询*/, int /*k*/) {
    return unavailable_();
}

bool Retriever::warm() const { return true; }  // 无事可建→永远温暖
void Retriever::warm_async() {}
void Retriever::shutdown() {}
void Retriever::apply_config(const Config& /*cfg*/) {}
Config Retriever::snapshot_config() const { return Config{}; }

Retriever::EmbedStatus Retriever::embed_status() const {
    EmbedStatus s;
    s.state  = EmbedStatus::State::Unavailable;
    s.reason = "retrieval engine (rag-cpp) is not built on this platform";
    return s;
}
Retriever::EmbedStatus Retriever::reprobe_embedder() { return embed_status(); }

namespace feedback {
void note_surfaced(const std::vector<std::string>& /*路径*/) {}
void note_file_opened(const maya::below<maya::leaf>&, const std::string& /*路径*/) {}
}  // namespace feedback

namespace bench {
int run(const std::string& /*根*/) {
    std::fprintf(stderr,
                 "rag-bench: the retrieval engine is not built on this "
                 "platform (rag-cpp is not yet Windows-portable).\n");
    return 2;
}
int eval(const std::string& /*eval_file*/, int /*k*/) { return run({}); }
}  // namespace bench

}  // namespace agentty::rag

#endif  // AGENTTY_HAS_RAGCPP
