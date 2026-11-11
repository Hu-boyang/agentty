#pragma once
// agentty::rag-检索适配器。
//
// agentty的RAG引擎是外部rag-cpp 库 (rag::Engine:
// 上下文BM25+可选的dense/HNSW检索、加权RRF、MMR重新排名、opt-in
// CRAG/ HyDE/GraphRAG以及验证的.ragdb 持久化)。
// 这个 头文件 是 agentty 其余部分看到的 仅 表面：它将每个 rag:: type 隐藏在紧凑、
// 稳定的 API 后面，因此应用程序永远不会依赖于引擎的内部结构。
//
// 边界故意很小——应用程序需要三件事：
//
//   1.检索器-build/refresh来自文件夹的文档索引，将其融合
//                           具有技能+学到的-memory+MCP
//                           -resource知识源，并用排名、压缩的段落回答查询。
//                           支持 `search_docs` 工具和 pre
//                           -turn 主动-retrieval 路径。
//   2. feedback::note_file_opened——学习循环的写入端：`read`
//                           文件中指向的段落算作"win"。
//   3.bench::run——`agentty rag-bench`CLI子命令。
//
// 下面的所有内容都是std类型+POD；没有 rag:: 类型泄漏。重型引擎位于 .
// cpp (src/rag/adapter.cpp) 中。

#include <cstdint>

#include "agentty/rag/embed_backend.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <maya/runtime.hpp>

namespace agentty::rag {

// 一篇检索到的段落，针对应用程序进行了扁平化。镜像 mcp::tools::DocPassage
// 字段-for-field，以便后端将其映射到一个简单的副本。
struct Passage {
    std::string   source;      // 出处："docs"/"skills"/"memory"/"mcp:<uri>"
    std::string   path;        // 文件路径或虚拟uri (技能://…，内存://…)
    int           line_start = 0;
    int           line_end   = 0;
    double        score      = 0.0;
    std::string   text;        // 段落正文(已压缩)
};

// 检索的结果：排序的段落+人类-readable模式标签
// （引擎配置+置信度）工具外壳渲染，+置信度信号，
// 使得主动路径可以对其进行门控。
struct Retrieval {
    std::vector<Passage> passages;
    std::string          mode;            // e.g。 "hybrid+ctx, reranked, confidence 0.62"
    double               confidence = 0.0;
    std::string          error;           // non-empty⇒失败（无知识等）
};

// 旋钮，全部默认从环境解析（from_env()）。保留为
// 结构体，让测试可以确定性地驱动适配器。每次 rag-cpp
// 质量特征agentty驱动器这里有切换。
struct Config {
    std::string  docs_root;               // AGENTTY_DOCS_DIR (或 ./docs、./.agentty/knowledge)

    // 对应的 嵌入器，以及如何到达它。三个松散字段硬连线到本地 Ollama 守护进程；
    // 现在，用户可以从 TUI 中配置一个值（Ctrl+K -> RAG -> 嵌入）。
    // 默认值完全重现旧行为：Auto -> Ollama on
    // 127.0.0.1:11434 with nomic-embed-text。
    //
    // NOTE `embed.dim` 是 embed::probe() 的 派生，而不是用户的 带类型的：
    // rag-cpp的HNSW默默地丢弃其大小与索引不一致的向量，
    // 因此猜测的维度产生空索引并且没有错误。
    embed::EmbedConfig embed;

    bool         skills   = true;         // AGENTTY_RAG_SKILLS
    bool         memory   = true;         // AGENTTY_RAG_MEMORY
    bool         mcp_resources = false;   // AGENTTY_RAG_MCP，显式 opt-in

    // 保守的生产默认：每个可选阶段在启用之前
    // 必须在用户的语料库上获得其延迟和输出成本。
    bool     contextual = true;   // index-time定位上下文
    bool     mmr        = true;   // 多样性最终候选池
    float    mmr_lambda = 0.65f;
    bool     stitch     = true;
    // 近-duplicate重复数据删除+相关性自动剪切——rag
    // -cpp的Pipeline::best()的细化阶段。 dedup 归并
    // paraphrase/boilerplate 副本，因此 LLM 上下文窗口
    // 不会用于 re-reading 相同的段落；自动剪切会在分数拐点处修剪低
    // -relevance 末尾。两者都很便宜并且适合接地生成，但可以将结果缩短到
    // k 以下，因此默认情况下但可以单独切换。
    bool     dedup      = true;   // AGENTTY_RAG_DEDUP
    float    dedup_threshold = 0.92f;
    bool     autocut    = true;   // AGENTTY_RAG_AUTOCUT
    float    autocut_sensitivity = 2.0f;
    bool     prf        = false;  // 可以漂移查询；基准测试后选择加入
    bool     corrective = false; // 词汇代理拒绝语义匹配
    bool     graph      = false; // 二次图构建；显式电源模式
    // LLM查询生成提高了对困难研究问题的召回率，
    // 但增加了一个或多个模型往返。在确定性混合路径上
    // 保持正常编码轮次；用户可以明确启用任一功能。
    bool     expand     = false;  // AGENTTY_RAG_EXPAND—多-query/RAG-Fusion
    bool     hyde       = false;  // AGENTTY_RAG_HYDE—HyDE
    // HyDE/multi-query需要LLM。启用后，agentty 在嵌入的
    // 相同 Ollama 上使用 SMALL LOCAL 模型（零云成本/令牌）。
    // 通过AGENTTY_RAG_GEN_MODEL覆盖模型。
    std::string gen_model = "qwen2.5:0.5b";   // Ollama 上微小、快速、无处不在
    bool     persist    = true;   // AGENTTY_RAG_PERSIST — .agentty/下的.ragdb 缓存
    bool     learn      = false;  // 隐式文件-open反馈是opt-in直到
                                  // 每个源类型都有一个可归属的信号
    bool     trace      = false;  // AGENTTY_RAG_TRACE —归并 per-stage 跟踪进入模式
    // 融合。 rag-cpp 测量凸（TM2C2）组合为在 NDCG 上
    // 击败 RRF，因此它是 agentty 的默认值；
    // ADAPTIVE 变体 还会将每-query 的权重转向对THAT 查询
    // 更有信心的猎犬（一条尖锐的顶级-heavy 得分曲线）。
    // 设置 AGENTTY_RAG_FUSION=rrf 以回退到加权倒数-rank 融合，
    // 这是唯一遵循下面 bm25/dense 权重的模式（凸面忽略它们）。
    std::string fusion = "convex";   // AGENTTY_RAG_FUSION:凸| rrf
    bool     adaptive_fusion = true; // AGENTTY_RAG_ADAPTIVE (仅凸面)
    // 加权RRF;两个公共权重都直接影响融合（仅限 rrf 模式）。
    float    dense_weight = 1.0f;
    float    bm25_weight  = 1.0f;

    // 主动/"fork"行为——pre-turn active-RAG路径（由工具后端读取，
    // 而不是检索漏斗）。 `proactive` 控制整个 pre-turn 注射；
    // proactive_min_conf 是要清除的 CRAG 条（值 > 1.0 是合法的
    // "never inject" 开关）； proactive_bytes 盖住注入的
    // <retrieved-context> 块。默认值镜像环境读取 (AGENTTY_RAG_PROACTIVE
    // / _MIN / _BYTES)。
    bool     proactive          = true;
    double   proactive_min_conf = 0.35;
    int      proactive_bytes    = 6144;

    [[nodiscard]] static Config from_env();
};

// 猎犬。一个 long-lived 实例支持 search_docs
// + 主动检索（后端保留函数 -local 静态）。
// 线程-safe：retrieve()可能会被并发调用；文档索引受到内部保护。
class Retriever {
public:
    Retriever();
    ~Retriever();
    Retriever(const Retriever&) = delete;
    Retriever& operator=(const Retriever&) = delete;

    // 检索`query`的最多k个段落。 `skip_docs`
    // 使用独立技能+内存索引，无法遍历、重建或丢弃文档语料库。
    [[nodiscard]] Retrieval retrieve(const std::string& query, int k,
                                     bool skip_docs = false);

    // SEMANTIC CODE SEARCH (支持search_code):
    // 在当前工作目录下索引源文件(代码-aware分块)并用排名的
    // 段落回答`query`。 Edit-aware：一个便宜的指纹，
    // 比行走的源码树在漂移上重建。独立于文档索引。从不抛出。
    [[nodiscard]] Retrieval retrieve_code(const std::string& query, int k);

    // OPTIONAL LLM 接口边界 用于 HyDE + multi-query
    // / RAG-Fusion。给定 提示词，返回一个或多个完成。设置后（且
    // AGENTTY_RAG_HYDE / AGENTTY_RAG_EXPAND
    // 开启），检索将使用 LLM 来缩小查询-文档不对称差距并提高召回率。不存在 →
    // 这些功能会优雅地降级为普通混合搜索（rag-cpp 的合同）。 agentty 在
    // 这里连接自己的提供商，因此 RAG 可以使用相同的型号连接 "think"。
    using Generator =
        std::function<std::vector<std::string>(const std::string& prompt, int n)>;
    void set_generator(Generator g);

    // Non-blocking：文档索引是否为当前根构建且
    // 新鲜（或者是否没有文档根，在这种情况下检索总是热的）？
    [[nodiscard]] bool warm() const;

    // Non-blocking:retrieve_code()可以回答WITHOUT冷索引构建吗？
    // 当代码索引存在于 内存 中或磁盘上存在持久索引时为真（加
    // 载它很便宜；只有 from-scratch 构建很昂贵）。
    // 盖茨 OPPORTUNISTIC 检索 — e.g。 search_structural
    // 的 Zero-hit 语义领先 — 因此 side-effect 查询永远不会触发冷构建。
    [[nodiscard]] bool code_warm() const;

    // 踢出独立的背景指数构建，以便未来的转变是温暖的。
    // 单-flight;立即返回。
    void warm_async();

    // 停止任何in-flight背景暖PROMPTLY并加入它。
    // 调用 是早期进程拆卸（静态销毁之前），因此 ^C 不会在 multi
    // -second 嵌入过程中停滞。 幂等；如果没有暖机运行，则安全。
    void shutdown();

    // 替换实时配置。 Sources/pipeline/fusion
    // 更改会改变语料库或持久化 -index 身份，强制重建下一个
    // retrieve(); pure ranking toggles take effect immediately. Thread-safe;
    // 从不抛出。支持 RAG 设置 选择面板 的 "apply" 路径。
    void apply_config(const Config& cfg);

    // 当前-live配置(针对选择面板的初始状态/round-trip)。
    [[nodiscard]] Config snapshot_config() const;

    // 直播dense-embedder状态，为RAG、
    // 选择面板的状态行和`agentty diagnostics`。
    // 镜像内部 DenseState：当嵌入器不可用时，
    // 它会携带 REASON，因此配置错误的端点是一条可见消息，
    // 而不是永远悄悄地回退到 BM25。
    //
    // 绝不 BLOCKS。从任何 帧 上的 UI 线程 到 调用
    // 都是安全的：如果检索器处于 mid-reconfiguration
    // （apply_config re-probes 嵌入器并在
    // 持有 锁 的同时重建三个引擎），则此报告未探测到而不是等待。
    // 可能使 渲染 循环停止的状态读数比短暂模糊的状态读数更糟糕。
    struct EmbedStatus {
        enum class State : std::uint8_t { Unprobed, Ready, Unavailable };
        State         state      = State::Unprobed;
        std::uint32_t dim        = 0;   // MEASURED，从未使用过-supplied
        int           latency_ms = 0;
        std::string   reason;           // 当不可用时填充
        std::string   describe;         // 通过 Ollama 127.0.0.1:11434 使用 nomic-embed-text。
    };
    [[nodiscard]] EmbedStatus embed_status() const;

    // Re-run针对CURRENT配置进行可用性探测并采用测量的维度。
    // 阻塞； 调用 关闭 UI 线程。这就是 选择面板 的
    // "Test connection" 行所驱动的。
    EmbedStatus reprobe_embedder();

private:
    struct Impl;
    // 拥有。 unique_ptr，其析构函数在 adapter.cpp
    // 中定义（其中 Impl 是完整的）——类型系统携带 所有权 而不是注释；
    // 头文件 仍然没有拉入 rag:: 类型。
    std::unique_ptr<Impl> impl_;
};

// ── 学习循环( 写侧) ─────────────────────────────────────────
// 具有两半的闭合反馈环路：
//   note_surfaced(路径)-search_docs 调用这与它刚刚
//                          出现的文件路径有关。每个都被记录为 "use"（分母），
//                          并被记住为最近的 -surfaced 候选者。
//   note_file_opened(路径)-当代理`read`创建文件时
//                          工具接口边界 调用。当最近通过检索发现该路径时，
//                          它算作 "win"（分子）仅 — i.e。
//                          这段话指出了值得采取行动的地方。然后，
//                          Beta-smoothed win/use 比率会推动该路径
//                          的未来排名（在 Retriever 中读取侧面生活）。
// 两者都是best-effort并且从不抛出。 AGENTTY_RAG_LEARN=0 禁用循环。
namespace feedback {
void note_surfaced(const maya::below<maya::leaf>&, const std::vector<std::string>& paths);
void note_file_opened(const maya::below<maya::leaf>&, const std::string& path);
}

// 命令行入口：`agentty rag-bench <root>`。
namespace bench {
int run(const std::string& root);
// 判断的eval:每个案例是{q，gold:[paths]}。从当前目录运行
// search_code（确切的刀具路径）并打印文件 -level recall@k、
// MRR 和未命中数。 `eval_file` 是 JSON 数组。总是退出 0（这是一个报告）。
int eval(const std::string& eval_file, int k);
}

} // namespace agentty::rag
