// embed_backend.cpp —嵌入后端边界的实现。
//
// 有意从adapter.cpp拆分：adapter.cpp是TU，
// 其中包括ENGINE的<rag/rag.hpp>，并且已经是2300行。
// 该文件仅拥有 config->spec 映射、身份哈希、验证和探测，
// 因此 picker/reducer/tests 可以针对边界 链接，
// 而无需拖入整个检索引擎。
//
// probe()确实需要rag-cpp（它构建了一个嵌入器来测量它），
// 因此它是在rag-cpp不可用的平台上编译出来的——文件的其余
// 部分是纯粹的并且总是构建。

#include "agentty/rag/embed_backend.hpp"
#include "agentty/rag/rag_host.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

#ifndef AGENTTY_HAS_RAGCPP
#  define AGENTTY_HAS_RAGCPP 1
#endif

#if AGENTTY_HAS_RAGCPP
#  include <rag/rag.hpp>
#endif

namespace agentty::rag::embed {

namespace {

using json = nlohmann::json;

// FNV-1a。 adapter.cpp 的明显指纹使用
// 相同的原语，因此身份哈希读取与检索层的其余部分一致。
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime  = 1099511628211ull;

void hash_text(std::uint64_t& h, std::string_view v) noexcept {
    for (unsigned char c : v) { h ^= c; h *= kFnvPrime; }
    h ^= 0xFFu; h *= kFnvPrime;   // 字段分隔符："ab"+"c"!="a"+"bc"
}
void hash_u64(std::uint64_t& h, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFFu; h *= kFnvPrime; }
}

[[nodiscard]] std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

[[nodiscard]] const char* env_or_null(const char* key) {
    const char* v = std::getenv(key);
    return (v && v[0]) ? v : nullptr;
}

// 将"host:port"/"http://host:port"分割成多个部分。故意容忍：
// 用户粘贴 URL，并且拒绝它们作为方案前缀是敌对的。
struct Endpoint { std::string host; std::uint16_t port = 0; bool tls = false; bool have_port = false; };

[[nodiscard]] Endpoint parse_endpoint(std::string raw) {
    Endpoint e;
    raw = trim(std::move(raw));
    if (raw.rfind("https://", 0) == 0) { e.tls = true;  raw.erase(0, 8); }
    else if (raw.rfind("http://", 0) == 0) { e.tls = false; raw.erase(0, 7); }
    if (auto slash = raw.find('/'); slash != std::string::npos) raw.erase(slash);
    if (auto colon = raw.rfind(':'); colon != std::string::npos) {
        const std::string port_s = raw.substr(colon + 1);
        bool digits = !port_s.empty()
            && std::all_of(port_s.begin(), port_s.end(),
                           [](unsigned char c) { return std::isdigit(c); });
        if (digits) {
            try {
                const int p = std::stoi(port_s);
                if (p > 0 && p <= 65535) {
                    e.port = static_cast<std::uint16_t>(p);
                    e.have_port = true;
                    raw.erase(colon);
                }
            } catch (...) { /* 保持主机完好无损 */ }
        }
    }
    e.host = std::move(raw);
    return e;
}

// 在探测任何内容之前自动拨号：历史默认值。
[[nodiscard]] constexpr Backend concrete(Backend b) noexcept {
    return b == Backend::Auto ? Backend::Ollama : b;
}

} // 命名空间结束。

// ── 验证 ───────────────────────────────────────────────────────────
Validity validate(const EmbedConfig& c) {
    if (c.backend == Backend::Disabled || c.backend == Backend::Auto)
        return std::monostate{};   // 没什么可出错的

    if (needs_endpoint(c.backend)) {
        if (trim(c.host).empty())
            return Invalid{"host is empty"};
        if (c.port == 0)
            return Invalid{"port must be 1-65535"};
        if (c.host.find(' ') != std::string::npos)
            return Invalid{"host contains a space"};
        // 所有数字和点表示IPv4地址：需要0-255的四个部分。
        // 捕获 "127.0.0"，这解决不了任何有用的问题。
        if (std::all_of(c.host.begin(), c.host.end(),
                        [](unsigned char ch) { return std::isdigit(ch) || ch == '.'; })) {
            int parts = 0;
            bool ok = true;
            std::size_t at = 0;
            while (ok && at <= c.host.size()) {
                const auto dot = c.host.find('.', at);
                const auto part = c.host.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
                ok = !part.empty() && part.size() <= 3 && std::stoi(part) <= 255;
                ++parts;
                if (dot == std::string::npos) break;
                at = dot + 1;
            }
            if (!ok || parts != 4)
                return Invalid{"host \"" + c.host + "\" is not a valid IPv4 address"};
        }
    }
    if (needs_model_name(c.backend) && trim(c.model).empty())
        return Invalid{"model name is required"};
    if (needs_model_path(c.backend)) {
        if (trim(c.model_path).empty())
            return Invalid{"model file path is required"};
    }
    if (c.backend == Backend::OpenAI && !c.path.empty() && c.path.front() != '/')
        return Invalid{"path must start with '/'"};
    return std::monostate{};
}

// ── 身份 ─────────────────────────────────────────────────────────────
std::pair<std::string, std::string> prefixes(const EmbedConfig& c) {
    // AGENTTY_EMBED_PREFIX=0 将其关闭（对于 A/B）。
    if (const char* e = std::getenv("AGENTTY_EMBED_PREFIX"); e && e[0] == '0') return {};
    if (!needs_model_name(concrete(c.backend))) return {};
    std::string m = c.model;
    for (auto& ch : m) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (m.find("nomic-embed") != std::string::npos)
        return {"search_query: ", "search_document: "};
    // qwen3-embedding 被保留：其指令前缀在 tests/rag_eval 上得分更
    // 差（held-out MRR .77 vs .80）。
    const bool e5 = m.starts_with("e5-") || m.find("/e5-") != std::string::npos
                 || m.find("-e5-") != std::string::npos;
    if (e5 && m.find("instruct") == std::string::npos)
        return {"query: ", "passage: "};
    if (m.find("mxbai-embed") != std::string::npos)
        return {"Represent this sentence for searching relevant passages: ", ""};
    if (m.find("snowflake-arctic-embed") != std::string::npos)
        return {"Represent this sentence for searching relevant passages: ", ""};
    return {};
}

std::uint64_t identity(const EmbedConfig& c) {
    std::uint64_t h = kFnvOffset;
    // 后端ID是几何的一部分：-process中的Ollama与已加载提供的相同权重在pooling
    // /normalization.中不同
    hash_text(h, id_of(concrete(c.backend)));
    if (needs_model_name(concrete(c.backend))) hash_text(h, c.model);
    if (needs_endpoint(concrete(c.backend))) {
        hash_text(h, c.host);
        hash_u64(h, c.port);
        hash_u64(h, c.tls ? 1u : 0u);
        hash_text(h, c.path);
    }
    if (needs_model_path(concrete(c.backend))) {
        hash_text(h, c.model_path);
        hash_text(h, c.tokenizer_path);
    }
    hash_u64(h, c.dim);
    // 前缀改变每个存储的向量，因此它们是空间的一部分。
    if (const auto [q, d] = prefixes(c); !q.empty() || !d.empty()) {
        hash_text(h, q);
        hash_text(h, d);
    }
    // 故意对 不 进行哈希处理：api_key。旋转 凭据 一定
    // 不能丢弃完全有效的索引。
    return h;
}

std::string identity_tag(const EmbedConfig& c) {
    static constexpr char kHex[] = "0123456789abcdef";
    const std::uint64_t h = identity(c);
    std::string out(8, '0');
    for (int i = 0; i < 8; ++i) out[7 - i] = kHex[(h >> (i * 4)) & 0xFu];
    return out;
}

// ── 薄荷丝 ─────────────────────────────────────────────────────────
std::optional<std::string> spec_json(const EmbedConfig& c) {
    if (c.backend == Backend::Disabled) return std::nullopt;
    if (!is_valid(c)) return std::nullopt;

    const Backend b = concrete(c.backend);
    json j;
    j["type"] = std::string{id_of(b)};

    switch (b) {
        case Backend::Ollama:
            j["model"] = c.model;
            j["host"]  = c.host;
            j["port"]  = c.port;
            // Ollama 加载是首次使用的模型（对于nomic
            // -embed-text需要1-2秒，在繁忙的GPU上更多）
            // 并且大批量也需要一段时间。第一次探测超时 1.2 秒，
            // 然后 agentty 在整个会话中运行 BM25-only。
            // 死端点仍然会快速失败：连接被拒绝，而不是超时。
            j["timeout_ms"] = 30000;
            break;
        case Backend::OpenAI:
            j["model"] = c.model;
            j["host"]  = c.host;
            j["port"]  = c.port;
            j["tls"]   = c.tls;
            if (!c.path.empty())    j["path"]    = c.path;
            if (!c.api_key.empty()) j["api_key"] = c.api_key;
            j["timeout_ms"] = 8000;   // 远程：允许WAN延迟
            break;
        case Backend::LlamaCpp:
            j["host"] = c.host;
            j["port"] = c.port;
            if (!c.path.empty()) j["path"] = c.path;
            j["timeout_ms"] = 4000;
            break;
        case Backend::Gguf:
            j["model_path"] = c.model_path;
            j["normalize"]  = true;
            break;
        case Backend::Onnx:
            j["model_path"] = c.model_path;
            if (!c.tokenizer_path.empty()) j["tokenizer_path"] = c.tokenizer_path;
            j["normalize"] = true;
            break;
        case Backend::Auto:
        case Backend::Disabled:
            break;   // 无法访问：concrete() 映射自动，禁用返回
    }
    // 仅当尺寸为 MEASURED 时才固定尺寸。万万没想到：错误的
    // 暗淡会使 rag-cpp 的 HNSW 默默地丢弃每个向量。
    if (c.dim > 0) j["dim"] = c.dim;
    return j.dump();
}

std::string describe(const EmbedConfig& c) {
    if (c.backend == Backend::Disabled) return "keyword only (BM25)";
    const Backend b = concrete(c.backend);
    std::string out;
    if (needs_model_name(b)) out += c.model.empty() ? "(no model)" : c.model;
    else if (needs_model_path(b)) {
        const auto slash = c.model_path.find_last_of("/\\");
        out += c.model_path.empty()
             ? "(no model file)"
             : c.model_path.substr(slash == std::string::npos ? 0 : slash + 1);
    } else out += "llama.cpp";

    if (is_in_process(b)) {
        out += " in-process (";
        out += id_of(b);
        out += ")";
    } else {
        out += " via ";
        out += id_of(b);
        out += ' ';
        out += c.host;
        out += ':';
        out += std::to_string(c.port);
    }
    return out;
}

// ── 环境 ──────────────────────────────────────────────────────────
void apply_env(Io, EmbedConfig& c) {
    // 旧名称首先出现，因此当两者都存在时新名称获胜。
    if (const char* m = env_or_null("AGENTTY_EMBED_MODEL")) c.model = m;
    if (const char* h = env_or_null("AGENTTY_OLLAMA_HOST")) {
        const auto e = parse_endpoint(h);
        if (!e.host.empty()) c.host = e.host;
        if (e.have_port)     c.port = e.port;
    }

    if (const char* b = env_or_null("AGENTTY_EMBED_BACKEND")) {
        std::string id{b};
        std::transform(id.begin(), id.end(), id.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        // 接受同一行的几个友好别名。
        if (id == "none" || id == "disabled" || id == "bm25") id = "off";
        if (id == "llamafile" || id == "llama.cpp")           id = "llamacpp";
        if (id == "openai-compatible" || id == "custom")      id = "openai";
        c.backend = backend_from_id(id);
    }
    if (const char* e = env_or_null("AGENTTY_EMBED_ENDPOINT")) {
        const auto ep = parse_endpoint(e);
        if (!ep.host.empty()) c.host = ep.host;
        if (ep.have_port)     c.port = ep.port;
        c.tls = ep.tls;
        // 当URL省略之一时，默认来自该方案的端口。
        if (!ep.have_port && ep.tls) c.port = 443;
    }
    if (const char* p = env_or_null("AGENTTY_EMBED_PATH"))       c.path           = p;
    if (const char* p = env_or_null("AGENTTY_EMBED_MODEL_PATH")) c.model_path     = p;
    if (const char* p = env_or_null("AGENTTY_EMBED_TOKENIZER"))  c.tokenizer_path = p;
    if (const char* k = env_or_null("AGENTTY_EMBED_API_KEY"))    c.api_key        = k;
}

// ── 探测 ────────────────────────────────────────────────────────────────
#if AGENTTY_HAS_RAGCPP

namespace {
// 当模型保留为默认值时，Auto的第一选择：在tests/rag_eval上测量得更好，
// 仅当它已经被拉动时才使用。
constexpr std::string_view kAutoPreferred = "qwen3-embedding:0.6b";

ProbeResult probe_one(const EmbedConfig& c) {
    if (c.backend == Backend::Disabled)
        return ProbeErr{"embeddings are disabled"};
    if (auto v = validate(c); auto* bad = std::get_if<Invalid>(&v))
        return ProbeErr{bad->why};

    // 具有尺寸UNPINNED的探针：我们来这里是为了学习它，
    // 并且固定过期值将掩盖我们想要检测的非常不匹配。
    EmbedConfig probe_cfg = c;
    probe_cfg.dim = 0;
    const auto spec = spec_json(probe_cfg);
    if (!spec) return ProbeErr{"no embedder for this configuration"};

    try {
        const auto t0 = std::chrono::steady_clock::now();
        ::rag::Engine engine;
        if (!engine.with_embedder_spec(::rag::plugin::Json::parse(*spec), ::agentty::rag::registries()))
            return ProbeErr{"backend '" + std::string{id_of(concrete(c.backend))}
                            + "' could not be constructed"};
        auto vector = engine.corpus().embed_text("agentty retrieval availability probe");
        const auto t1 = std::chrono::steady_clock::now();
        if (!vector)
            return ProbeErr{"embed failed — endpoint unreachable or model missing"};
        if (vector->empty())
            return ProbeErr{"backend returned an empty vector"};

        ProbeOk ok;
        ok.dim = static_cast<std::uint32_t>(vector->size());
        ok.latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        ok.model = c.model;
        return ok;
    } catch (const std::exception& e) {
        return ProbeErr{e.what()};
    } catch (...) {
        return ProbeErr{"unknown error while probing the embedder"};
    }
}
}  // 命名空间结束。

ProbeResult probe(const EmbedConfig& c) {
    if (c.backend == Backend::Auto && c.model == EmbedConfig{}.model) {
        EmbedConfig better = c;
        better.model = std::string{kAutoPreferred};
        // 未被拉出的模型立即失败（404)，因此当它不存在
        // 时这不需要任何成本。
        if (auto r = probe_one(better); std::holds_alternative<ProbeOk>(r)) return r;
    }
    return probe_one(c);
}

#else

ProbeResult probe(const EmbedConfig&) {
    return ProbeErr{"retrieval engine (rag-cpp) is not built on this platform"};
}

#endif

} // namespace agentty::rag::embed
