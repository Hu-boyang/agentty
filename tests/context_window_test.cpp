// context_window_test ——模型上下文窗口如何解决.
//
// 窗口驱动 Ctx-% 计, 自动 -compaction
// 触发和压缩片天花板,两种失败模式不是 对称性 :
// 猜测过高让前缀运行过终点接受的,
// 而转弯则在传输上消亡,猜测太低的紧凑比它必须的要早。
// 所以解析命令很重要,它被固定在这里。
//
// 错误是用来:运行LiteLLM后面的1M
// -token型号的用户看到agentty将其封顶在200k,
// 无法改变. TWO原因,都涵盖在下面——
// v1/models解析器忽略了每个网关可以登上广告窗口的字段,并且根本没有用户的覆盖.

#include "agtest.hpp"

#include "agentty/domain/catalog.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/store/store.hpp"

#include <cstdlib>   // setenv/unsetenv
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using agentty::store::Settings;
namespace ui = agentty::ui;

// 电话 解析器 试验中的生命 传输层 TU ;
// 内容是 -declared 因为它是执行细节
// list_models 测试是唯一的 其它的呼叫者。
namespace agentty::provider::openai::detail {
[[nodiscard]] int advertised_context_window(const nlohmann::json& m);
[[nodiscard]] bool is_local_endpoint(const agentty::provider::openai::Endpoint& ep);
}
namespace det = agentty::provider::openai::detail;

TEST_CASE("context: a bare OpenAI /v1/models row advertises nothing") {
    // OpenAI 属于自己的 模式定义 是{id, 对象, 创建, owned_by - 没有窗户。
    // 0 表示 "nobody said" ,必须区别于 "200k".
    const auto row = json::parse(R"({
        "id": "gpt-4o", "object": "model",
        "created": 1234567890, "owned_by": "openai"
    })");
    CHECK(det::advertised_context_window(row) == 0);
}

TEST_CASE("context: LiteLLM model_info is read") {
    // LiteLLM 传递一个配置 `model_info:` 挡住,直接穿过
    const auto row = json::parse(R"({
        "id": "my-1m-model",
        "model_info": {"max_input_tokens": 1000000, "max_output_tokens": 8192}
    })");
    CHECK(det::advertised_context_window(row) == 1000000);

    // max_tokens 旧的拼写和有效 回退.
    const auto older = json::parse(R"({
        "id": "x", "model_info": {"max_tokens": 128000}
    })");
    CHECK(det::advertised_context_window(older) == 128000);
}

TEST_CASE("context: OpenRouter's per-deployment window wins") {
    // 连着模特儿的 目录 窗口 和。
    // 它们通常不同意(供应商可在128k提供1M型号),
    // SERVING 将拒绝一个报应者,
    // -long 提示词.
    const auto row = json::parse(R"({
        "id": "anthropic/claude-3.5-sonnet",
        "context_length": 1000000,
        "top_provider": {"context_length": 128000}
    })");
    CHECK(det::advertised_context_window(row) == 128000,
          "the serving endpoint's window outranks the catalog figure");
}

TEST_CASE("context: vLLM max_model_len is read") {
    const auto row = json::parse(R"({"id": "m", "max_model_len": 32768})");
    CHECK(det::advertised_context_window(row) == 32768);
}

TEST_CASE("context: a stringified window still parses") {
    // 一些代理字符串化了数字元数据。 丢弃这些元数据会静静
    // 地压制模型, 这正是这个守护器的整个失败。
    const auto row = json::parse(R"({"id": "m", "context_length": "262144"})");
    CHECK(det::advertised_context_window(row) == 262144);
}

TEST_CASE("context: garbage never throws or yields a bogus window") {
    for (const char* src : {R"({"id":"m","context_length":null})",
                            R"({"id":"m","context_length":"not a number"})",
                            R"({"id":"m","model_info":"not an object"})",
                            R"({"id":"m","context_length":-5})",
                            R"({"id":"m"})"}) {
        const auto row = json::parse(src);
        CHECK(det::advertised_context_window(row) == 0, src);
    }
}

TEST_CASE("context: llama-server reports the window inside meta, not at the top") {
    // 逐字记录形状 llama.cpp tools/server/server-context.cpp (
    // get_res_model_info). n_ctx 是那个 SERVED 窗口( 窗口) slot_n_ctx, i.e.
    // 什麽 -c 分配的); n_ctx_train 是建筑天花板,是建筑天花
    //
    // 第49期:平面梯级已经列出n_ctx/n_ctx_train,
    // 但llama.cpp从未将它们排在一排的顶层——仅位于
    // `meta`之内——因此每个ellama
    // -server模型都掉入200k默认. 声称200k的8k模型表示
    // 收缩永远不会起火,服务器则将提示词切换.
    const auto row = json::parse(R"({
        "id": "qwen2.5-coder-7b",
        "object": "model",
        "owned_by": "llamacpp",
        "meta": {
            "vocab_type": 2, "n_vocab": 152064,
            "n_ctx": 8192, "n_ctx_train": 32768,
            "n_embd": 3584, "n_params": 7615616512,
            "size": 4431389696, "ftype": 15
        }
    })");
    // 电话 SERVED 窗口胜过列车 -time 上限:
    // 8192是请求必须实际包含的内容。
    CHECK(det::advertised_context_window(row) == 8192);
}

TEST_CASE("context: llama-server falls back to n_ctx_train when n_ctx is absent") {
    const auto row = json::parse(R"({
        "id": "m", "meta": { "n_ctx_train": 32768 }
    })");
    CHECK(det::advertised_context_window(row) == 32768);
}

TEST_CASE("context: a meta block that says nothing is not a window") {
    // `meta` 存在于每 llama.cpp 排行,因此,不能仅仅将其存在
    // 视为一个答案——0必须留下。 "nobody said".
    const auto row = json::parse(R"({
        "id": "m", "meta": { "n_vocab": 152064, "ftype": 15 }
    })");
    CHECK(det::advertised_context_window(row) == 0);
}

TEST_CASE("context: LM Studio's loaded instance config is a window") {
    // 人均数 -instance 配置块从 LM 工作室的原生/ / 工作室
    // api/v1/models. 探测器读了每个 loaded_instances [].
    // 通过这个配置 解析器 ,所以,我说 `context_length` 一定要解决那里。
    const auto cfg = json::parse(R"({
        "context_length": 16384,
        "eval_batch_size": 512,
        "flash_attention": true
    })");
    CHECK(det::advertised_context_window(cfg) == 16384);
}

TEST_CASE("context: an architectural maximum is not the loaded window") {
    // LM 工作室的/v1shim报告 max_context_length - 什么
    // 样的模型 ARCHITECTURE 支持。一个能够 128k 的模型 已加载 16k
    // 仍然拒绝,16k。
    //
    // 两者 解析 数字,这就是陷阱:行看 "known" ,
    // 所以端点探测器(单靠它就能看到 已加载 从未运行。
    // 解析器 右侧返回此处已声明的值——这是在模型
    // 之前唯一可用的约束值 加载 - 但是,我说,
    // list_models 必须将较小的被探测值视为权威值。
    const auto row = json::parse(R"({
        "id": "qwen/qwen3-coder-30b", "max_context_length": 262144
    })");
    CHECK(det::advertised_context_window(row) == 262144);

    const auto loaded = json::parse(R"({ "context_length": 16384 })");
    CHECK(det::advertised_context_window(loaded) == 16384);
    // 探测器的整个点覆盖: 运行时 数字较小,而数字较小
    // 提示词 一定要尊重你,你必须尊重我
    CHECK(det::advertised_context_window(loaded)
            < det::advertised_context_window(row));
}

// ── 哪个终点得到 运行时-window 探测器 ────────────────────────
//
// 由你决定 NETWORK BEHAVIOUR。
// 真假需要多几轮 -trips 每一次刷新,是唯一的方法a 运行
// 时 窗口; 假表示端点保留着任何其/ v1/models 都已经宣布了,
// 两个错误都沉默了 所以才把分类放在这里
namespace {
agentty::provider::openai::Endpoint host_ep(const char* h) {
    agentty::provider::openai::Endpoint ep;
    ep.host = h;
    return ep;
}
}

TEST_CASE("probe: a server on your own network counts as local") {
    // 仅检查回路的第一个版本。 数小时内报告 # 49 :
    // llama-server 确实部署在另一台机器，并非回环地址。
    // 将模型服务器放在盒子上 GPU 是那个
    // NORMAL 运行本地模型的方法,而那个设置则
    // 留下了永久错误的上下文窗口.
    CHECK(det::is_local_endpoint(host_ep("localhost")));
    CHECK(det::is_local_endpoint(host_ep("127.0.0.1")));
    CHECK(det::is_local_endpoint(host_ep("127.0.0.2")));      // 127/8
    CHECK(det::is_local_endpoint(host_ep("::1")));
    CHECK(det::is_local_endpoint(host_ep("host.docker.internal")));

    // RFC1918 - 电话 LAN 报告里的案件。
    CHECK(det::is_local_endpoint(host_ep("192.168.1.50")));
    CHECK(det::is_local_endpoint(host_ep("10.0.0.7")));
    CHECK(det::is_local_endpoint(host_ep("172.16.0.1")));
    CHECK(det::is_local_endpoint(host_ep("172.31.255.254")));

    // 链接-local, CGNAT (尾部的手伸出来) 100.64/10 ),
    // mDNS,和一张裸体单曲 -label 无法在公共互联网上解决的主机名。
    CHECK(det::is_local_endpoint(host_ep("169.254.1.1")));
    CHECK(det::is_local_endpoint(host_ep("100.84.22.21")));
    CHECK(det::is_local_endpoint(host_ep("gpubox.local")));
    CHECK(det::is_local_endpoint(host_ep("gpubox")));

    // IPv6 独有 -local 财务报告和已审计财务报表 链接-local.
    CHECK(det::is_local_endpoint(host_ep("fd00::1")));
    CHECK(det::is_local_endpoint(host_ep("fe80::1")));
}

TEST_CASE("probe: a hosted API is never probed") {
    // 另一半,通过这些测试,每个用户每个刷新一个
    // 404个路由, 永远。
    CHECK_FALSE(det::is_local_endpoint(host_ep("api.openai.com")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("api.anthropic.com")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("openrouter.ai")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("api.mistral.ai")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("")));

    // 172.x OUTSIDE 16-31是公开的(172.15和172.32是routable),
    // 17x是苹果. a prefix match on. "172." 或 或 时 间 "17." 都会吞下两个。
    CHECK_FALSE(det::is_local_endpoint(host_ep("172.15.0.1")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("172.32.0.1")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("17.253.144.10")));
    // 100.x 在CGNAT范围以外,也是公众的。
    CHECK_FALSE(det::is_local_endpoint(host_ep("100.1.2.3")));
}

TEST_CASE("probe: a learned host is treated as self-hosted from then on") {
    // 添加 -host 流动探测器 每个 新主机,不管地址。 答录器 运行时
    // 路径(/道具,/) api/ps, loaded_instances
    // ) 证明了它是什么——没有托管人 API ——所以人们会记住它,
    // 以后的每一次更新探测器都会被它所利用.
    //
    // 这就是替换一个设置切换:服务器已经告诉我们
    // 它是什么,所以要求用户确认就是要求他们重复
    // 我们得到的答案.
    CHECK_FALSE(det::is_local_endpoint(host_ep("llm.example.com")));

    agentty::provider::openai::install_probe_hosts({"llm.example.com"});
    CHECK(det::is_local_endpoint(host_ep("llm.example.com")));
    // 确切的东道主——学习一个东道主决不能选择在它的邻国.
    CHECK_FALSE(det::is_local_endpoint(host_ep("other.example.com")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("evil-llm.example.com")));

    // 安装取代批发,所以一个从设置中掉下来的主机
    // 停止被探测,而不是停留在过期集中.
    agentty::provider::openai::install_probe_hosts({});
    CHECK_FALSE(det::is_local_endpoint(host_ep("llm.example.com")));
}

TEST_CASE("probe: AGENTTY_PROBE_HOSTS opts a public host in") {
    // 服务器的逃生舱门到达了 VPN 拥有自己的 DNS ,或者穿过隧道,
    // 没有休眠症就无法分辨。没有这个答案,
    // "my server is at llm.example.com" 谓
    // "unsupported" ,而窗口永远是错的——探测器是 仅 如何是学它的方法.
    CHECK_FALSE(det::is_local_endpoint(host_ep("llm.example.com")));

    ::setenv("AGENTTY_PROBE_HOSTS", "llm.example.com", 1);
    CHECK(det::is_local_endpoint(host_ep("llm.example.com")));
    // 精确匹配只——后缀不能选择整个域.
    CHECK_FALSE(det::is_local_endpoint(host_ep("evil-llm.example.com")));

    // 逗号 -separated 空间被容忍。
    ::setenv("AGENTTY_PROBE_HOSTS", "a.example.com, b.example.com", 1);
    CHECK(det::is_local_endpoint(host_ep("a.example.com")));
    CHECK(det::is_local_endpoint(host_ep("b.example.com")));
    CHECK_FALSE(det::is_local_endpoint(host_ep("c.example.com")));

    ::unsetenv("AGENTTY_PROBE_HOSTS");
    CHECK_FALSE(det::is_local_endpoint(host_ep("a.example.com")));
}

TEST_CASE("context: a GGUF arch-prefixed context_length is a window") {
    // 窗口如何在模型自己的元数据中拼写,以及Ollama的CLI所读
    // 的准确(cmd/cmd.go showInfo). 拱前缀每个
    // 家族都不同,因此它有后缀的对应.
    const auto row = json::parse(R"({
        "model_info": {
            "general.architecture": "qwen2",
            "qwen2.context_length": 32768,
            "qwen2.embedding_length": 3584
        }
    })");
    CHECK(det::advertised_context_window(row) == 32768);
}

TEST_CASE("context: a FLOAT context_length still counts") {
    // Ollama 本身发送一个纯整数—— 对照一个真实的 0. 34.2 守护进程
    // 进行验证, 它会回答 "qwen3.context_length" : 40960.
    // 不过 ModelInfo 谓 带类型的 任何地图,所以协议中没有任何保证,
    // 和 LiteLLM 已经以浮点字段(16385.0)发出此类字段。
    //
    // 严格地对待整个窗口,如果它是错误的,那么它就意味着不
    // 买任何东西。
    const auto row = json::parse(R"({
        "model_info": { "qwen2.context_length": 32768.0 }
    })");
    CHECK(det::advertised_context_window(row) == 32768);

    // 平拼法也采用浮字——LiteLLM 输出16385.0.
    const auto flat = json::parse(R"({ "context_length": 16385.0 })");
    CHECK(det::advertised_context_window(flat) == 16385);
}

TEST_CASE("context: embedding_length is not a context window") {
    // 两种键并存于此 model_info 并两者均以 "_length".
    // 过于松散的匹配会报告一个3584-token窗口,用于32k型号.
    const auto row = json::parse(R"({
        "model_info": { "qwen2.embedding_length": 3584 }
    })");
    CHECK(det::advertised_context_window(row) == 0);
}

TEST_CASE("context: resolution order is override > advertised > id") {
    Settings s;
    const char* prov = "litellm";
    const char* model = "my-model";

    // 3. 任何地方都不知 —— 保守的默认,从不猜测.
    CHECK(ui::resolve_context_window(prov, model, 0, s)
              == ui::kDefaultContextWindow);

    // 2. 网关广告的广告比猜的要好。
    CHECK(ui::resolve_context_window(prov, model, 1'000'000, s) == 1'000'000);

    // 一个KNOWN id仍然输给了一个来自端点的活体数字,
    // 这个点将服务于请求——两个网关后面的同名可能有所不同.
    CHECK(ui::resolve_context_window(prov, "claude-sonnet-4-5", 400'000, s)
              == 400'000,
          "advertised outranks id inference");

    // 1. 用户的超载比一切要好,它们配置了网关。
    s.context_overrides[ui::context_override_key(prov, model)] = 2'000'000;
    CHECK(ui::resolve_context_window(prov, model, 1'000'000, s) == 2'000'000,
          "an override a heuristic can overrule is not a setting");
}

TEST_CASE("context: overrides are per provider AND model") {
    // 两个网关后面的同一型号ID可以用两个不同的窗口来服务,
    // 因此一个裸型号ID不能作为足够密钥.
    Settings s;
    s.context_overrides[ui::context_override_key("litellm", "shared")] = 1'000'000;

    CHECK(ui::resolve_context_window("litellm", "shared", 0, s) == 1'000'000);
    CHECK(ui::resolve_context_window("other", "shared", 0, s)
              == ui::kDefaultContextWindow,
          "an override on one provider must not leak to another");
}

TEST_CASE("context: a known family still resolves without a catalog") {
    Settings s;
    // Claude 身份证上写着他们的窗户,不需要广告。
    CHECK(ui::resolve_context_window("anthropic", "claude-opus-4-5", 0, s)
              == 200'000);
    // [1m]后缀迫使宽窗.
    CHECK(ui::resolve_context_window("anthropic", "claude-sonnet-4-5[1m]", 0, s)
              == 1'000'000);
}

TEST_CASE("context: labels read the way the picker prints them") {
    CHECK(ui::context_window_label(0)         == "auto");
    CHECK(ui::context_window_label(128'000)   == "128k");
    CHECK(ui::context_window_label(200'000)   == "200k");
    CHECK(ui::context_window_label(1'000'000) == "1M");
    CHECK(ui::context_window_label(1'048'576) == "1M+");
}

TEST_CASE("context: the override ladder starts at auto and covers the range") {
    // "auto" 一定要跑0 所以一圈整圈的 ^W 总是返回到
    // "let agentty decide" ——控制本身就已经清空了.
    REQUIRE(std::size(ui::kContextLadder) > 1);
    CHECK(ui::kContextLadder[0] == 0, "auto is the first rung");
    // 严格地上升,所以踏步是两方向的单调递增的.
    for (std::size_t i = 1; i < std::size(ui::kContextLadder); ++i)
        CHECK(ui::kContextLadder[i] > ui::kContextLadder[i - 1],
              "the ladder ascends");
    // 报道记者实际需要的
    bool has_1m = false;
    for (int w : ui::kContextLadder) if (w == 1'000'000) has_1m = true;
    CHECK(has_1m, "1M is reachable — the window this feature was asked for");
}
