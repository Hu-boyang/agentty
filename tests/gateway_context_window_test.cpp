// 内容 -window 网关的解析—— "my 1M models show 200k" 虫子,你这混蛋
//
// 报告人 LiteLLM 用户:代理支持背后的模型~1M,
// agentty 显示200k,他们要求设置一个环境来提升它。
// IS 一项( ^W 模型中 选择面板 周期到 1M/2M 因此,
// 报告实际上有两个失败:
//
//   1. agentty ⊿Τ KNOW 窗口——网关的/ v1/models 行载
//      没有大小,所以每个模型都掉到 kDefaultContextWindow 上.
//   2. 国家 UI ⊿Τ SAY 不知——Ctx栏是空白的,
//      读作 "not applicable" 而不是说 "guessing" ,所以,
//      我说 ^W 从未找到过 200k看起来像一个盖子。
//
// 钉住它 解析 合同(1)形状。 解析 梯子是网关的答案被
// 恢复的单一地点,所以每个带窗口的方言都必须被它恢复,
// 每个形状都能够恢复 不 必须决心做到0 -- --
// "unknown" - 而不是向一个无人申报的数字。
// 将这两条线合起来就是整个错误:0是使探测器起火的原因,
// 柱子说"auto",以及超载物质.

#include "agtest.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "agentty/provider/openai/transport.hpp"
#include "agentty/domain/session.hpp"
#include "agentty/domain/catalog.hpp"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

// 电话 真实 解析器 ,而不是复制本。早先的草稿在这里照了梯子,
// 所以测试不需要输出,但反射合同不是合同:删除该模型中的
// 浮标分支。 传输层 将文件绿色保存到 LiteLLM
// 16385.0 默默地变成了 "unknown" 测试必须进行
// 调用 什么样的船。
int advertised(const json& m) {
    return agentty::provider::openai::advertised_context_window(m);
}

}  // 命名空间结束。

TEST_CASE("context: llama.cpp router rows read the window from launch args") {
    // 来,来,来 UNLOADED 路由器模型没有 `meta` ;大小只生活在 argv 中,
    // 路由器将用( server-models.cpp,
    // get_router_models: status.args。该源代码复制的形状。
    using agentty::provider::openai::router_declared_window;
    auto row = [](std::string args) {
        return json::parse(R"({"id":"m","status":{"value":"unloaded","args":)"
                           + args + "}}");
    };

    // 默认 --parallel 是自动的: 4个插槽, 统一 KV ,
    // 因此每个槽都可以使用整个 池.
    CHECK(router_declared_window(row(R"(["llama-server","--ctx-size","65536"])")) == 65'536);
    // 明确的时间档,不统一 KV 拆分该 池.
    CHECK(router_declared_window(row(
        R"(["llama-server","--ctx-size","65536","--parallel","2"])")) == 32'768);
    // 明确时间档 WITH 统一 KV 别
    CHECK(router_declared_window(row(
        R"(["llama-server","--ctx-size","65536","--parallel","2","--kv-unified"])")) == 65'536);
    // 人数 -slot 盖子绑住。
    CHECK(router_declared_window(row(
        R"(["llama-server","--ctx-size","65536","--kv-unified-per-slot","16384"])")) == 16'384);
    // 没有 -c: 池 大小从每架 -slot 盖子。
    CHECK(router_declared_window(row(
        R"(["llama-server","--parallel","1","--kv-unified-per-slot","32768"])")) == 32'768);
    // 没有 -c / -c 0 表示 "trained size" ,这些词组没有告诉我们。
    CHECK(router_declared_window(row(R"(["llama-server","-m","x.gguf"])")) == 0);
    CHECK(router_declared_window(row(R"(["llama-server","--ctx-size","0"])")) == 0);
    // 垃圾和缺失的形状是"unknown",从不计数.
    CHECK(router_declared_window(row(R"(["llama-server","--ctx-size","lots"])")) == 0);
    CHECK(router_declared_window(json::parse(R"({"id":"m"})")) == 0);
    CHECK(router_declared_window(json::parse(R"({"id":"m","status":"loaded"})")) == 0);
}

TEST_CASE("context: every gateway dialect that declares a window is recovered") {
    // 每个真实的堆栈一个案例。每个案例都是堆栈在 /
    // v1/models 上实际释放的形状, 所以这里的
    // 回归就是对部署的回归。

    // OpenRouter 单位:每名 -DEPLOYMENT 窗口,
    // 它比模型的通用性要高 —— 两个提供者后面的同名可以使用两个大小的版本。
    CHECK(advertised(json::parse(
        R"({"id":"x","context_length":200000,
             "top_provider":{"context_length":1000000}})")) == 1'000'000);

    // 维基月球在线解说- max_model_len 是那个 运行时 上限( --max-model-len).
    CHECK(advertised(json::parse(R"({"id":"x","max_model_len":262144})")) == 262'144);

    // LiteLLM 平面 : max_input_tokens ,从自己的成本图中解决。
    CHECK(advertised(json::parse(R"({"id":"x","max_input_tokens":1048576})")) == 1'048'576);

    // ...还有 LiteLLM 发作为 FLOAT (观察:
    // 16385.0) 严格整数读取将窗口静悄悄地放下,
    // 模型又回到默认,这就是这个整个文件存在的失败模式.
    CHECK(advertised(json::parse(R"({"id":"x","max_input_tokens":1048576.0})")) == 1'048'576);

    // 嵌入到 model_info (LiteLLM /v1/model/info 排行,一些代词).
    CHECK(advertised(json::parse(
        R"({"id":"x","model_info":{"max_input_tokens":200000}})")) == 200'000);

    // llama.cpp: n_ctx 服务器是什么 STARTED 与;
    // n_ctx_train 建筑天花板,只是一个 回退.
    CHECK(advertised(json::parse(R"({"id":"x","n_ctx":32768})")) == 32'768);
    CHECK(advertised(json::parse(R"({"id":"x","n_ctx_train":131072})")) == 131'072);
    CHECK(advertised(json::parse(
        R"({"id":"x","n_ctx":8192,"n_ctx_train":131072})")) == 8'192);

    // 字符串元数据仍然算作声明的代理。
    CHECK(advertised(json::parse(R"({"id":"x","context_length":"131072"})")) == 131'072);
}

TEST_CASE("context: the OpenAI-compatible universe beyond Claude and GPT") {
    // 上面的梯子是用我们接到的报告 堆积起来的
    // 但现在的生态环境大部分是"OpenAI-compatible",
    // 每个卖家都选择了自己的拼写,拼写相同。 我们不读的
    // 拼写并不是一个小错 — — 它默默地将模型降为200k默认值,
    // 而这正是这个文件被打开的准确错误,只是针对一个卖家,
    // 而还没有人提交过。
    //
    // 因此:每个主要东道主有一例 OpenAI-compatible
    // API ,使用字段,每个字段实际返回 / v1/models.
    // 这些是用户通过网关或直接基地联系到的提供者 -url 超音速 超音调

    // Mistral(api.mistral.ai). 其自己的拼写,没有其他匹配.
    CHECK(advertised(json::parse(
        R"({"id":"mistral-large-latest","max_context_length":131072})"))
        == 131'072);

    // Together.ai 烟花, DeepSeek , 困惑—— 全部 `context_length`.
    CHECK(advertised(json::parse(
        R"({"id":"meta-llama/Llama-3.3-70B","context_length":131072})"))
        == 131'072);
    CHECK(advertised(json::parse(
        R"({"id":"deepseek-chat","context_length":65536})")) == 65'536);

    // Groq在它的模范行上拼写了`context_window`.
    CHECK(advertised(json::parse(
        R"({"id":"llama-3.3-70b-versatile","context_window":131072})"))
        == 131'072);

    // 科赫尔是相近的层层 巢下 `context_length` 内置每架
    // -model 对象相同 LiteLLM 巢 窝 model_info.
    CHECK(advertised(json::parse(
        R"({"id":"command-r-plus","model_info":{"context_window":128000}})"))
        == 128'000);

    // 文/ 书 DashScope 以及中国的几个供应商分别
    // 报告输入的一半。 max_input_tokens 是
    // 一个带宽的编号 提示词 ,这就是上下文测量值。
    CHECK(advertised(json::parse(
        R"({"id":"qwen-max","max_input_tokens":30720})")) == 30'720);
}

TEST_CASE("context: an output cap is never mistaken for a context window") {
    // 危险在附近 -miss。许多行都携带 两者 上下文窗口和最大值
    // -output 盖,输出盖总是小得多(4k–32k
    // 对128k–1M).
    //
    // 读错的不会大声失败:它产生一个合理的小数字,
    // 计数器读到~10x的真实用法,
    // 自动 -compaction 几乎立即开火——所以
    // 模型被交给一个非常适合的对话摘要。用户看到一个代理商,
    // "forgets" 不断在巨大的窗户上
    //
    // `max_output_tokens` / `max_completion_tokens`
    // / `max_tokens` 现场 TOP 等值是输出盖,绝不应读作窗口。
    CHECK(advertised(json::parse(
        R"({"id":"x","max_output_tokens":8192})")) == 0);
    CHECK(advertised(json::parse(
        R"({"id":"x","max_completion_tokens":16384})")) == 0);

    // 当两者出现时, WINDOW 赢,谁先赢谁都不行
    CHECK(advertised(json::parse(
        R"({"id":"x","max_output_tokens":8192,"context_length":200000})"))
        == 200'000);
    CHECK(advertised(json::parse(
        R"({"id":"x","max_context_length":131072,"max_output_tokens":4096})"))
        == 131'072);
}

TEST_CASE("context: a silent row is UNKNOWN (0), never a guess") {
    // 合同的另一半, 以及更重要的。 这些形状没有宣布, 必须确定为 0 ,
    // 这样呼叫者才能从 "nobody knows" 中告诉
    // "the gateway said 200k"。 返回这里的默认
    // 值正是使 200k 看起来像一个盖子: 探测器永远不开火,
    // 选择面板 栏显示一个自信的号码, 而 ^W 看上去是毫无意义的。
    for (const char* body : {
            R"({"id":"x","object":"model"})",                    // 赤色( 浅色) OpenAI
            R"({"id":"qwen3:8b","object":"model","owned_by":"library"})", // ⁇ /v1
            R"({"id":"qwen/qwen3-coder-30b","object":"model"})", // LM 工作室
            R"({"id":"x","object":"model","owned_by":"openai"})" // 库存 LiteLLM
        }) {
        CHECK(advertised(json::parse(body)) == 0);
    }

    // 垃圾也绝不能变成窗口——申报内容不正确仍然是"unknown",
    // 而不是数字。
    CHECK(advertised(json::parse(R"({"id":"x","context_length":null})")) == 0);
    CHECK(advertised(json::parse(R"({"id":"x","context_length":"many"})")) == 0);
    CHECK(advertised(json::parse(R"({"id":"x","model_info":"not-an-object"})")) == 0);
    CHECK(advertised(json::parse(R"({"id":"x","top_provider":42})")) == 0);
    // 零和消极都是虚无的宣示,而不是微小的窗口.
    CHECK(advertised(json::parse(R"({"id":"x","max_model_len":0})")) == 0);
}

TEST_CASE("context: the deployment's own figure outranks the generic one") {
    // 梯子内优先。当一个门网代表一个模型时,
    // 它就会报告两个门网。 上游 窗口与部署服务不同
    // ——只有网关知道哪些适用于我们即将发出的请求,
    // 所以具体的请求获胜。
    CHECK(advertised(json::parse(
        R"({"id":"x","context_length":8192,"max_model_len":262144})")) == 262'144);
    CHECK(advertised(json::parse(
        R"({"id":"x","max_input_tokens":8192,
             "top_provider":{"context_length":1000000}})")) == 1'000'000);
}

// ── 一个不宣布任何信息的私人网关: 文件记录的最后一个网关 ────
//
// 实际设置( O) gitlab.com/r3xxar/fw16-ai-inference
// : 双人前方8090处的比夫罗斯特 llama.cpp 服务器。它/ v1/models
// 行赤——代号和物,别无别物——和模样代号( ⁇ ).
// `igpu/laguna-xs-2.1` ) 公众都不认识 目录 建筑工程。
//
// 所以每个自动运行答0, 也就是CORRECT: 在
// 这里发明一个窗口是真正的262k模型如何被困在无人
// 选择的默认中。 但0 的后果值得一试,因为它是沉默的:
// StreamState::compaction_threshold ()当 context_max
// ⁇  0,所以自动 -compaction 绝不 FIRES 和长 线程 增长到
// 服务器拒绝它。
//
// AGENTTY_MAX_CONTEXT_TOKENS 是逃生舱门( 3) docs/CONTEXT_WINDOW.md).
// 探测结果是诚实的 并且
// 重置武器
TEST_CASE("context: a bare private-gateway row declares nothing") {
    // 这正是比弗罗斯为当地服务 -hosted 模型 模型 模式
    CHECK(advertised(json::parse(
        R"({"id":"igpu/laguna-xs-2.1","object":"model","owned_by":"bifrost"})"))
        == 0);
    CHECK(advertised(json::parse(
        R"({"id":"dgpu/granite-4.2-3b","object":"model","owned_by":"bifrost"})"))
        == 0);
}

TEST_CASE("context: an unknown window disarms compaction, a known one arms it") {
    // 后果,直接声明。这是用户无法看到的部分 :
    // 显示器没有显示最大值,也没有警告,所以第一个症状
    // 是从服务器深处到长会话的400个.
    agentty::StreamState s;

    s.context_max = 0;                       // 没有人宣布过
    CHECK(s.compaction_threshold() == 0);    // ^ 自动-compaction 永不开火

    s.context_max = 262'144;                 // AGENTTY_MAX_CONTEXT_TOKENS=262144
    const int thr = s.compaction_threshold();
    CHECK(thr > 0);                          // □ 武装
    CHECK(thr < s.context_max);              // 留有空间回答
}

TEST_CASE("context: id inference only speaks for families it knows") {
    // 径4号 resolve_context_window
    // iD 推论。 上方 洞穴逃出舱门 保守的默认,
    // 所以一个数字返回沉默 两者—— 这使得一个自信的错误答案
    // 这里比没有答案更糟糕。
    //
    // 曾经还200千元 KNOWN FAMILY ,
    // 和 Family::Gpt 双亲都是已知的家族。所以,
    // gpt-5.4(实际上400k)和gpt-4.1(实际上1M)都
    // 推断出200k。 目录 还没有 已加载 然而,测量仪读取了真实使用量
    // 和自动的两倍 -compaction 大约一半的子弹被击中 线程
    // 本来应该有——默默地抛弃模型仍然能看到的背景,没有任何地方这样说。
    //
    // 规则:仅在id真正决定窗口时才会推断.
    using MC = agentty::ModelCapabilities;

    // Claude : 200k是每代出版的基数,
    // `[1m]` 也就是知识,所以它能说话。
    CHECK(MC::from_id("claude-sonnet-5").context_window()      == 200'000);
    CHECK(MC::from_id("claude-opus-5").context_window()        == 200'000);
    CHECK(MC::from_id("claude-haiku-4-5").context_window()     == 200'000);
    CHECK(MC::from_id("claude-sonnet-5[1m]").context_window()  == 1'000'000);

    // GPT : 窗口为 不 号码0表示未知,谁把问题交给
    // models.dev / env 覆盖 / 默认 ——
    // 存在 的 rung 正好适合此。
    CHECK(MC::from_id("gpt-5.4").context_window()  == 0);
    CHECK(MC::from_id("gpt-4.1").context_window()  == 0);
    CHECK(MC::from_id("gpt-5-codex").context_window() == 0);

    // 并且一个来自没有我们认识的家属的ID 保持0,就像它总是。
    CHECK(MC::from_id("gemini-2.5-pro").context_window()   == 0);
    CHECK(MC::from_id("some-private-model").context_window() == 0);
}

TEST_CASE("context: every path into available_models bakes the window") {
    // bake_context_window () 存在时如此 一个 数字达到两个 选择
    // 面板 的 ctx 列和状态栏( Q) PR #39),这只能维持,只要每一条路
    // 充满 `available_models` 运行它。
    //
    // 刷新路径已经实现。 SEED 路径 - init.cpp ,
    // 您开始的行——没有:在设置均匀之前运行 已加载 ,
    // 因此,一个按住上下文窗口的用户,每行的针头都被忽略,
    // 直到第一个提供者刷新。 不变量 除了你
    // 真正进入的状态之外 到处都存在
    // -check.
    //
    // 这是源扫描,因为错误是 MISSING
    // CALL SITE 行为测试必须构建
    // 完整的内涵路径,而且仍然只覆盖两个 调用
    // 而不是明年的第三个网站。
    const std::filesystem::path root{AGENTTY_SRC_ROOT};

    struct Site { const char* file; const char* why; };
    static constexpr Site kSites[] = {
        {"src/runtime/app/init.cpp",
         "the SEEDED rows — the catalog you start with, before any refresh. "
         "Bakes right after load_settings(), because the ladder needs them."},
        {"src/runtime/app/update/models.cpp",
         "the REFRESH rows — rebuilt when a provider lists its models."},
    };

    for (const auto& s : kSites) {
        const auto path = root / s.file;
        INFO("site = " << s.file);
        REQUIRE(std::filesystem::exists(path));

        std::ifstream in(path);
        REQUIRE(in);
        std::string src;
        {
            std::string line;
            while (std::getline(in, line)) {
                // 脱下注释行。 这些文件中每一个
                // EXPLAINS 编号 不变量 并进行扫描,
                // 以匹配解释通过时的 调用 已经消失,
                // 这正是这项考验本身所抓住的。
                const auto first = line.find_first_not_of(" \t");
                if (first != std::string::npos
                    && line.compare(first, 2, "//") == 0) continue;
                src += line;
                src += '\n';
            }
        }

        // A CALL ,不提:名后有开放的帕伦.
        CHECK_MESSAGE(src.find("bake_context_window(") != std::string::npos,
            s.file << " fills available_models without baking the context "
                      "window. " << s.why
                   << "  Unbaked rows carry the raw catalog figure, so the "
                      "picker's ctx column and the status bar disagree about "
                      "the same model, and a user's pinned window is "
                      "silently ignored. Call ui::bake_context_window(row, "
                      "provider_id, settings) on every row.");
    }
}
