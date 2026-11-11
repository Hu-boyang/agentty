// 嵌入后端边界的测试——让检索停止被硬线连接到本地的
// Ollama守护进程上的层.
//
// 这里有两个地产比其他地产更值钱 因为两者都失败了
// SILENTLY 生产中:
//
//  1. identity()必须分离矢量空格。
//     由同样几何的嵌入器重新使用,以及 rag-cpp's HNSW
//     DROPS 匹配的矢量没有出错( Q) hnsw.cpp:
//     `if (vec.size() != dim_) return;`。
//     所以一个 过期 索引在不同的后端重新打开, 产生空结果或垃圾圈, 没有任何日志。
//
//  2. api_key 不得 参加身份调查。 凭据 不是,是"我们",不是"我们"
//     几何学的变化;如果它被散开,
//     每个键旋转都会默地丢掉一个完全好的指数.

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "agentty/rag/embed_backend.hpp"

using namespace agentty::rag::embed;

namespace {

EmbedConfig ollama_cfg() {
    EmbedConfig c;
    c.backend = Backend::Ollama;
    c.model   = "nomic-embed-text";
    c.host    = "127.0.0.1";
    c.port    = 11434;
    return c;
}

EmbedConfig openai_cfg() {
    EmbedConfig c;
    c.backend = Backend::OpenAI;
    c.model   = "nomic-embed-text";
    c.host    = "gateway.internal";
    c.port    = 8080;
    return c;
}

} // 命名空间结束。

TEST_CASE("embed: defaults reproduce the historical Ollama behaviour") {
    // 从未打开过此选项的升级用户 选择面板 必须遵守 NO 变化。
    EmbedConfig c;
    CHECK(c.backend == Backend::Auto);
    CHECK(c.model == "nomic-embed-text");
    CHECK(c.host == "127.0.0.1");
    CHECK(c.port == 11434);

    // 自动拨打Ollama直到有别的说法.
    const auto spec = spec_json(c);
    REQUIRE(spec.has_value());
    const auto j = nlohmann::json::parse(*spec);
    CHECK(j.at("type") == "ollama");
    CHECK(j.at("model") == "nomic-embed-text");
    CHECK(j.at("port") == 11434);
}

TEST_CASE("embed: identity separates vector spaces") {
    // 同样的型号 NAME ,不同的后端 QQ 不同的几何。
    // 这是旧的运单(存储) `embed_model` )无法表达,
    // 一旦后端可以插上,一个名字就不是一个身份的原因.
    auto a = ollama_cfg();
    auto b = openai_cfg();
    CHECK(identity(a) != identity(b));

    // 端点是空间的一部分:两个主机在同一标签下可能
    // 服务不同的权重.
    auto c = openai_cfg();
    c.host = "other.internal";
    CHECK(identity(b) != identity(c));

    auto d = openai_cfg();
    d.port = 9090;
    CHECK(identity(b) != identity(d));

    // 测量的维度也有所区别(经运行的-embedding端点)。
    auto e = openai_cfg();
    e.dim = 768;
    auto f = openai_cfg();
    f.dim = 1536;
    CHECK(identity(e) != identity(f));

    // Model名称在一个后端内仍然很重要.
    auto g = ollama_cfg();
    g.model = "mxbai-embed-large";
    CHECK(identity(a) != identity(g));
}

TEST_CASE("embed: models trained with prefixes get them") {
    auto n = ollama_cfg();
    n.model = "nomic-embed-text";
    CHECK(prefixes(n).first == "search_query: ");
    CHECK(prefixes(n).second == "search_document: ");
    auto e = openai_cfg();
    e.model = "intfloat/multilingual-e5-large";
    CHECK(prefixes(e).first == "query: ");
    auto plain = openai_cfg();
    plain.model = "text-embedding-3-small";
    CHECK(prefixes(plain).first.empty());
    CHECK(prefixes(plain).second.empty());
}

TEST_CASE("embed: identity ignores the credential") {
    // 旋转API键决不能使温指数失效——它认证,
    // 它没有定义几何.
    auto a = openai_cfg();
    auto b = openai_cfg();
    a.api_key = "sk-first";
    b.api_key = "sk-second-rotated";
    CHECK(identity(a) == identity(b));
    CHECK(identity_tag(a) == identity_tag(b));
}

TEST_CASE("embed: identity is stable and short") {
    const auto c = ollama_cfg();
    CHECK(identity(c) == identity(c));          // 决定性
    CHECK(identity_tag(c).size() == 8);         // 文件名-safe
    for (char ch : identity_tag(c))
        CHECK(((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')));
}

TEST_CASE("embed: validation catches what the picker must refuse") {
    // Auto/Off 自己完成的 自己完成 自己完成
    EmbedConfig auto_cfg;
    CHECK(is_valid(auto_cfg));
    EmbedConfig off;
    off.backend = Backend::Disabled;
    CHECK(is_valid(off));

    auto bad_host = openai_cfg();
    bad_host.host.clear();
    CHECK_FALSE(is_valid(bad_host));

    auto bad_port = openai_cfg();
    bad_port.port = 0;
    CHECK_FALSE(is_valid(bad_port));

    auto no_model = ollama_cfg();
    no_model.model.clear();
    CHECK_FALSE(is_valid(no_model));

    // In-process后端需要文件,而不是终点.
    EmbedConfig gguf;
    gguf.backend = Backend::Gguf;
    CHECK_FALSE(is_valid(gguf));
    gguf.model_path = "/models/nomic.gguf";
    CHECK(is_valid(gguf));

    // 一条路一定看起来像一条路。
    auto bad_path = openai_cfg();
    bad_path.path = "v1/embeddings";   // 缺少主斜线
    CHECK_FALSE(is_valid(bad_path));
    bad_path.path = "/v1/embeddings";
    CHECK(is_valid(bad_path));

    // 点点 -number 主机必须是完整的IPv4地址. "127.0.0"
    // 曾经是 已保存 从 选择面板 一次默默地左转搜索关键字 -only.
    auto ip = ollama_cfg();
    for (const char* h : {"127.0.0", "127.0.0.1.5", "300.0.0.1", "127..0.1", "1234.0.0.1"}) {
        ip.host = h;
        CHECK_FALSE(is_valid(ip));
    }
    for (const char* h : {"127.0.0.1", "10.0.0.255", "localhost", "gpu-box.lan"}) {
        ip.host = h;
        CHECK(is_valid(ip));
    }
}

TEST_CASE("embed: an invalid config mints no spec") {
    // spec_json 唯一一个地方是建立 光谱,所以拒绝在这里是停止
    // 半 -typed 到达终点 rag-cpp.
    auto bad = openai_cfg();
    bad.host.clear();
    CHECK_FALSE(spec_json(bad).has_value());

    EmbedConfig off;
    off.backend = Backend::Disabled;
    CHECK_FALSE(spec_json(off).has_value());   // exical-only 不是嵌入器
}

TEST_CASE("embed: dimension is only pinned once measured") {
    // A GUESSED 暗是无声指数 -corruption 脚枪
    // : rag-cpp's HNSW 忽略了不同意它的矢量。
    // 所以在探测器测量到一个时,光谱会完全省略密钥。
    auto c = openai_cfg();
    CHECK(c.dim == 0);
    auto j = nlohmann::json::parse(*spec_json(c));
    CHECK_FALSE(j.contains("dim"));

    c.dim = 768;
    j = nlohmann::json::parse(*spec_json(c));
    REQUIRE(j.contains("dim"));
    CHECK(j.at("dim") == 768);
}

TEST_CASE("embed: specs carry each backend's own keys") {
    // OpenAI-compatible : 案件编号 KhazAkar 实际要求的 —— 任意的端点,
    // 带有用户 -provided 模型 模型 模式
    auto oa = openai_cfg();
    oa.tls     = true;
    oa.path    = "/v1/embeddings";
    oa.api_key = "sk-test";
    const auto j = nlohmann::json::parse(*spec_json(oa));
    CHECK(j.at("type") == "openai");
    CHECK(j.at("host") == "gateway.internal");
    CHECK(j.at("tls") == true);
    CHECK(j.at("path") == "/v1/embeddings");
    CHECK(j.at("api_key") == "sk-test");

    // 内 -process : 无 host/port 完全不,
    // 这是 "cannot run a daemon at work" 答案,一个偏差的终点键 将会是一个谎言。
    EmbedConfig g;
    g.backend    = Backend::Gguf;
    g.model_path = "/models/nomic.gguf";
    const auto gj = nlohmann::json::parse(*spec_json(g));
    CHECK(gj.at("type") == "gguf");
    CHECK(gj.at("model_path") == "/models/nomic.gguf");
    CHECK_FALSE(gj.contains("host"));
    CHECK_FALSE(gj.contains("port"));
    CHECK_FALSE(gj.contains("api_key"));
}

TEST_CASE("embed: backend ids round-trip") {
    for (Backend b : kBackends) {
        CHECK(backend_from_id(id_of(b)) == b);
        CHECK_FALSE(label_of(b).empty());
        CHECK_FALSE(id_of(b).empty());
    }
    // 未知的IDs倒回到安全默认,而不是扔.
    CHECK(backend_from_id("nonsense") == Backend::Auto);
    CHECK(backend_from_id("") == Backend::Auto);
}

TEST_CASE("embed: capability predicates match the spec keys") {
    // 这些按行 选择面板 显示的驱动器; 错配意味着用户
    // 可以输入字段, 字段会忽略这个字段。
    CHECK(is_in_process(Backend::Gguf));
    CHECK(is_in_process(Backend::Onnx));
    CHECK_FALSE(is_in_process(Backend::Ollama));

    CHECK(needs_endpoint(Backend::Ollama));
    CHECK(needs_endpoint(Backend::OpenAI));
    CHECK(needs_endpoint(Backend::LlamaCpp));
    CHECK_FALSE(needs_endpoint(Backend::Gguf));

    // 只有偏远的HTTPXXXX API 使用无记名符号——
    // Ollama和llama.cpp服务器在我们每次目标部署中都未经认证.
    CHECK(needs_api_key(Backend::OpenAI));
    CHECK_FALSE(needs_api_key(Backend::Ollama));
    CHECK_FALSE(needs_api_key(Backend::LlamaCpp));
    CHECK_FALSE(needs_api_key(Backend::Gguf));

    CHECK(needs_model_path(Backend::Gguf));
    CHECK_FALSE(needs_model_path(Backend::OpenAI));
}

TEST_CASE("embed: describe names the transport honestly") {
    CHECK(describe(ollama_cfg()).find("ollama") != std::string::npos);
    CHECK(describe(ollama_cfg()).find("11434") != std::string::npos);

    EmbedConfig g;
    g.backend    = Backend::Gguf;
    g.model_path = "/models/nomic.gguf";
    const auto d = describe(g);
    CHECK(d.find("in-process") != std::string::npos);
    CHECK(d.find("nomic.gguf") != std::string::npos);   // 碱性名称, 不全路径

    EmbedConfig off;
    off.backend = Backend::Disabled;
    CHECK(describe(off).find("BM25") != std::string::npos);
}
