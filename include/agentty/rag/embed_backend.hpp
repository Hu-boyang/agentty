#pragma once
// agentty::rag::embed——嵌入BACKEND边界。
//
// 检索过去被硬连线到本地Ollama守护进程：`{"type":"ollama"}`
// 在adapter.cpp的四个站点处按字面拼写(文档附加、代码附加、
// 热探针、rag-bench)，并且唯一的调整是AGENTTY_OLLAMA_HOST。
// 这是一个固执己见的默认值，价值 KEEPING — 但它
// 也是唯一可访问的选项，锁 排除了任何无法运行后台守护程序（锁定
// -down 公司计算机）或已经拥有内部嵌入网关的人。
//
// 这个头文件使后端成为VALUE。 rag-cpp 已经注册了我们需要的每个
// 工厂（参见 third_party/rag-cpp/src/plugin/builtins.cpp）；
// agentty 的工作只是命名一个并交出其钥匙。所以这个文件故意变薄：
//
//   * `Backend` — agentty 公开的封闭后端集。
//   * `EmbedConfig`——用户-facing旋钮，一个结构。
//   * `spec_for()`-EmbedConfig->rag-cpp插件JSON。 唯一 放置一个
//                     规格已铸造； adapter.cpp 不再包含任何文字。
//   * `identity()` —定义 VECTOR SPACE 的所有内容的散列。
//   * `validate()`——纯前提条件检查，由选择面板呈现。
//
// ── 为什么 `dim` 这儿不在这里 ────────────────────────────────────────────────
// 用户-supplied尺寸是已加载脚枪。 rag-cpp 的 HNSW 索引删除
// 不匹配的向量 SILENTLY：
//
//     无效 HnswIndex::add(std::uint32_t id，std::span<const float> 向量){
//         if (dim_ == 0) dim_ = vec.size();
//         if (vec.size() != dim_ || dim_ == 0) return;   // ignored!
//
// 因此，错误的暗淡会产生空索引，没有错误，并且检索会悄悄地
// 永远降级为BM25。因此，维度为 派生 — probe()
// 嵌入一个字符串并读取向量的大小。它是一个结果，而不是一个输入，
// 类型系统反映了这一点：没有 `dim` 字段需要设置。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include "agentty/util/io.hpp"

namespace agentty::rag::embed {

// ── 后端的agentty曝光 ─────────────────────────────────────────
// 特意设置了一个小封闭集，而不是注册表表。每个都映射到已注册的工厂 rag-cpp。
// `Auto` 不是后端——它是 spec_for() 扩展的
// POLICY（首选 Ollama，降级为 BM25）。
enum class Backend : std::uint8_t {
    Auto,        // 自以为是的默认值：如果应答则本地Ollama，否则BM25
    Ollama,      // 显式 local/remote Ollama 守护进程
    OpenAI,      // 任意 OpenAI-compatible /v1/embeddings 主机
    LlamaCpp,    // llama.cpp 服务器/嵌入（也是 llamafile 提供的服务）
    Gguf,        // in-process GGUF — 无守护程序、无网络、无安装
    Onnx,        // 在-process ONNX 运行时
    Disabled,    // 仅词汇：BM25/keyword，从不嵌入
};

inline constexpr int kBackendCount = 7;
inline constexpr Backend kBackends[kBackendCount] = {
    Backend::Auto,   Backend::Ollama, Backend::OpenAI, Backend::LlamaCpp,
    Backend::Gguf,   Backend::Onnx,   Backend::Disabled,
};

[[nodiscard]] constexpr std::string_view id_of(Backend b) noexcept {
    switch (b) {
        case Backend::Auto:     return "auto";
        case Backend::Ollama:   return "ollama";
        case Backend::OpenAI:   return "openai";
        case Backend::LlamaCpp: return "llamacpp";
        case Backend::Gguf:     return "gguf";
        case Backend::Onnx:     return "onnx";
        case Backend::Disabled: return "off";
    }
    return "auto";
}

[[nodiscard]] constexpr std::string_view label_of(Backend b) noexcept {
    switch (b) {
        case Backend::Auto:     return "Auto";
        case Backend::Ollama:   return "Ollama";
        case Backend::OpenAI:   return "Custom endpoint";
        case Backend::LlamaCpp: return "llama.cpp server";
        case Backend::Gguf:     return "GGUF (in-process)";
        case Backend::Onnx:     return "ONNX (in-process)";
        case Backend::Disabled: return "Off";
    }
    return "Auto";
}

[[nodiscard]] constexpr std::string_view blurb_of(Backend b) noexcept {
    switch (b) {
        case Backend::Auto:     return "local Ollama if reachable, else keyword-only";
        case Backend::Ollama:   return "Ollama daemon, local or remote";
        case Backend::OpenAI:   return "any OpenAI-compatible /v1/embeddings host";
        case Backend::LlamaCpp: return "llama.cpp / llamafile server endpoint";
        case Backend::Gguf:     return "no daemon — model file loaded in-process";
        case Backend::Onnx:     return "no daemon — ONNX model loaded in-process";
        case Backend::Disabled: return "keyword search only (BM25)";
    }
    return "";
}

[[nodiscard]] constexpr Backend backend_from_id(std::string_view id) noexcept {
    for (Backend b : kBackends)
        if (id_of(b) == id) return b;
    return Backend::Auto;
}

// 该后端是否到达网络/守护进程？驱动 选择面板 中的
// "no daemon required" 徽章 — 单一事实 KhazAkar
// 的请求实际上已开启。
[[nodiscard]] constexpr bool is_in_process(Backend b) noexcept {
    return b == Backend::Gguf || b == Backend::Onnx;
}
[[nodiscard]] constexpr bool needs_endpoint(Backend b) noexcept {
    return b == Backend::Ollama || b == Backend::OpenAI || b == Backend::LlamaCpp;
}
[[nodiscard]] constexpr bool needs_model_path(Backend b) noexcept {
    return b == Backend::Gguf || b == Backend::Onnx;
}
// 仅远程HTTP API获取不记名令牌。在我们针对的每个部署中，
// Ollama 和 llama.cpp 服务器都未经身份验证。
[[nodiscard]] constexpr bool needs_api_key(Backend b) noexcept {
    return b == Backend::OpenAI;
}
[[nodiscard]] constexpr bool needs_model_name(Backend b) noexcept {
    return b == Backend::Ollama || b == Backend::OpenAI;
}

// ── 用户-facing配置 ────────────────────────────────────────
// 一个结构体，全部可选。默认值重现今天的行为 恰好：
// 127.0.0.1:11434上的Backend::Auto+nomic-embed-text，
// 因此从不打开选择面板的现有用户观察不到任何变化。
struct EmbedConfig {
    Backend      backend = Backend::Auto;
    std::string  model   = "nomic-embed-text";

    // 端点、分割而不是URL字符串，因此选择面板可以独立地验证各部分，
    // 因此端口保持带类型的。 `tls` 和 `path` 仅对
    // Backend::OpenAI 重要。
    std::string   host = "127.0.0.1";
    std::uint16_t port = 11434;
    bool          tls  = false;
    std::string   path;            // 空⇒后端默认(/v1/embeddings)

    // 在-process后端。
    std::string model_path;        // .gguf/.onnx
    std::string tokenizer_path;    // 仅ONNX

    // 凭据。 绝不 一直存在于 settings.json 中，
    // 并且从来不是 identity() 的一部分——它进行身份验证，但不定义向量空间。
    std::string api_key;

    // 由 probe() 导出，绝不由用户导出 带类型的。 0 ⇒ 未知；
    // 该规范完全省略了密钥，因此 rag-cpp 使用模型的原生尺寸。
    std::uint32_t dim = 0;

    [[nodiscard]] bool operator==(const EmbedConfig&) const = default;
};

// ── 验证 ───────────────────────────────────────────────────────────
// 值，而不是回调：选择面板 渲染 `Invalid::why`内联
// 并且拒绝提交。纯⇒单元-testable，没有UI。
struct Invalid { std::string why; };
using Validity = std::variant<std::monostate, Invalid>;

[[nodiscard]] Validity validate(const EmbedConfig& c);
[[nodiscard]] inline bool is_valid(const EmbedConfig& c) {
    return std::holds_alternative<std::monostate>(validate(c));
}

// ── 矢量空间的识别 ─────────────────────────────────────────
// THE正确性原语。持久化的 .ragdb 只能由生成 相同
// 几何图形的嵌入器重用。单独的 Model NAME 无法表达后端可
// 插拔：Ollama 提供的 "nomic-embed-text"
// 以及 -process 中与 GGUF 相同的权重 已加载
// 在池化和归一化方面有所不同，因此它们的余弦不具有可比性。
// 使用另一个嵌入器重新打开一个索引会产生无声的错误检索——没有崩溃，
// 没有警告，只有错误的答案。
//
// 因此，清单以{后端、模型、端点、路径、暗淡}的散列为关键，
// 并且它还被拼接到.ragdb FILENAME中——这将
// 修复升级为一项功能：在热索引之间切换后端SWITCHES
// 而不是使一个失效。
//
// 排除api_key (凭据，不是几何形状）和每个排名-only旋钮。
[[nodiscard]] std::uint64_t identity(const EmbedConfig& c);

// {查询前缀，文档前缀}训练模型（nomic:
// "search_query: " / "search_document: ")，对于没有它们的型号为空。
[[nodiscard]] std::pair<std::string, std::string> prefixes(const EmbedConfig& c);

// 文件名中使用的短十六进制形式。
[[nodiscard]] std::string identity_tag(const EmbedConfig& c);

// ── 薄荷丝 ─────────────────────────────────────────────────────────
// ONE放置了嵌入器规格。当后端禁用（或无效）时返回 nullopt，
// 调用者读取为 "lexical only"。
//
// `resolved_auto` 选择 Auto 扩展为：
// 已经探测过的调用者通过具体后端。没有探测结果的自动映射到
// Ollama，因此第一个探测仍然拨打历史默认值。
//
// 返回类型是JSON字符串而不是rag::plugin::Json，
// 因为这个头文件一定不能将rag-cpp拉入每个仅仅读取配置的TU中；
// adapter.cpp 仍然是唯一包含 <rag/rag.hpp> 的地方。
[[nodiscard]] std::optional<std::string> spec_json(const EmbedConfig& c);

// Human-readable one-liner用于状态行/诊断，e.g。
//   通过 Ollama 127.0.0.1:11434 使用 nomic-embed-text。
//   进程内运行 bge-small，使用 GGUF。
[[nodiscard]] std::string describe(const EmbedConfig& c);

// ── 检测结果 ─────────────────────────────────────────────────────────
// `Test connection`返回什么，以及填充`dim`。
struct ProbeOk {
    std::uint32_t dim        = 0;
    int           latency_ms = 0;
    // 回答的模型。与仅在“自动”下的配置不同，
    // 当用户保留默认值时，“自动”会首先尝试更好的模型。
    std::string   model;
};
struct ProbeErr { std::string why; };
using ProbeResult = std::variant<ProbeOk, ProbeErr>;

// 阻塞single-shot探针：构建嵌入器，嵌入一根短串，
// 测量。从不抛出。从 UI 线程 运行（状态更新函数 将其包裹在
// Cmd 中）。返回 派生 维度 - 唯一值得信赖的来源。
[[nodiscard]] ProbeResult probe(const EmbedConfig& c);

// ── 环境 ──────────────────────────────────────────────────────────
// 旧版AGENTTY_OLLAMA_HOST/AGENTTY_EMBED_MODEL继续工作；
// 如果两者均已设置，则新的 AGENTTY_EMBED_* 名称获胜。
// 应用在 `base` 之上，因此持久的 选择面板 配置可以分层在 env 默认值之上。
void apply_env(Io, EmbedConfig& c);

} // namespace agentty::rag::embed
