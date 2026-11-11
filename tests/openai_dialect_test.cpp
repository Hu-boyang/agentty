// tests/openai_dialect_test.cpp 生态系统没有的生物体
//
// "OpenAI-compatible"没有规格(见lens.hpp),
// 因此无法遵循,因此不能断言符合——只有OBSERVED.
// 这个文件是替代:从实际终点中捕获的真块,
// 在dialect.hpp的镜头表上重放.
//
// 下方的固定图都取自实际反应。当一个提供者被探测到这个
// 套房的现场时,日期会被记录下来。当一个提供者改变拼写时,
// 这里的固定图就会开始失败——这是这个生态系统能够
// 产生的最接近于某种破坏的特性。

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "agentty/provider/openai/dialect.hpp"
#include "agentty/provider/openai/lens.hpp"

using json = nlohmann::json;
namespace oa = agentty::provider::openai;
namespace dx = agentty::provider::openai::dialect;

// ═══ 代数 ═════════════════════════════════════════════════════════
// 连环是替代物,也许。这些定律是固定的,因为下面的每
// 个偏差表都是闭合的表达式——如果 `|` 不属于关联
// 或 `fail` 并不是它的身份,一个表 意味着的东西,
// 而不是它读作。

TEST_CASE("lens: Alternative laws") {
    const json j = json::parse(R"({"b":"from-b","c":"from-c"})");

    const auto a = oa::key<std::string>("a");   // 从未观察到
    const auto b = oa::key<std::string>("b");
    const auto c = oa::key<std::string>("c");

    // 左身份/ 右身份: 失败是| 的单位
    CHECK((oa::fail<std::string>() | b)(j) == b(j));
    CHECK((b | oa::fail<std::string>())(j) == b(j));

    // 关联性: parens的形状不能改变答案
    CHECK(((a | b) | c)(j) == (a | (b | c))(j));

    // 选择选择按书面顺序选择 首次。
    CHECK((b | c)(j).value() == "from-b");
    CHECK((c | b)(j).value() == "from-c");

    // 镜片,没有观察任何事物是无声的, 从来没有扔过
    CHECK(!a(j).has_value());
    CHECK(!a(json::parse("[]")).has_value());     // 错误的根类型
    CHECK(!a(json::parse("null")).has_value());
}

TEST_CASE("lens: a wrong-typed member falls through, it does not throw") {
    // 发送的网关 `reasoning: null`
    // 旁边有人居住 `reasoning_content` 扔到这里会
    // 让现场流产 数据流 以一个甚至不需要存在的字段。
    const json j = json::parse(R"({"reasoning":null,"reasoning_content":"think"})");
    CHECK(dx::reasoning_delta()(j).value() == "think");

    const json wrong = json::parse(R"({"content":42})");
    CHECK(!dx::content_delta()(wrong).has_value());   // 数字, 不为字符串
}

// ═══ 推理:有三种拼写的字段 ═══════════════════════════

TEST_CASE("dialect: reasoning — DeepSeek / vLLM spelling") {
    // reasoning_content 是什么 说吧 DeepSeek 引入和 vLLM 复制。
    const json j = json::parse(R"({"reasoning_content":"step one"})");
    CHECK(dx::reasoning_delta()(j).value() == "step one");
}

TEST_CASE("dialect: reasoning — bare `reasoning` spelling") {
    // OpenAI 答复 API ,一些 ,有点 OpenRouter 传球,还有尤洛 -Auto.
    // CAPTURED LIVE 2026-09-23 电话: https://yolo-auto.com/v1 (中文(简体) ). -flash,
    // system_fingerprint vllm-0.29.1rc1):该机型为: 数据流 BARE `reasoning`.
    // 仅检查客户端 reasoning_content 渲染 什么都没有。
    const json j = json::parse(
        R"({"reasoning":"The user wants me to count from 1"})");
    CHECK(dx::reasoning_delta()(j).value() ==
          "The user wants me to count from 1");
}

TEST_CASE("dialect: reasoning — empty first key must fall through") {
    // 该 REGRESSION 此 FILE EXISTS FOR.
    //
    // Τ OpenRouter 排放的通过正常化 两者 键,
    // 与 reasoning_content 空 打破了第一个钥匙,
    // 而这个钥匙只是... EXISTS 放下每一个推理符号,
    // 沉默——没有错误,没有警告,只是一个看起来沉默中思考的模型.
    const json j = json::parse(
        R"({"reasoning_content":"","reasoning":"actual thinking"})");
    CHECK(dx::reasoning_delta()(j).value() == "actual thinking");

    // ...对称的箱子仍然更喜欢更具体的钥匙
    const json k = json::parse(
        R"({"reasoning_content":"specific","reasoning":"generic"})");
    CHECK(dx::reasoning_delta()(k).value() == "specific");
}

TEST_CASE("dialect: reasoning — absent is not an error") {
    const json j = json::parse(R"({"content":"just prose"})");
    CHECK(!dx::reasoning_delta()(j).has_value());
}

// ═══ 内容: 字符串对结构化部分 ═════════════════════════════════

TEST_CASE("dialect: content — the ordinary string form") {
    const json j = json::parse(R"({"content":"hello"})");
    CHECK(dx::content_delta()(j).value() == "hello");
}

TEST_CASE("dialect: content — Mistral's structured parts") {
    // 被迷惑所发现 -small-latest 与
    // reasoning_effort =高。在操作此形状之前,
    // 整个阵列都默默无声 DROPPED :推理隐形,传言缺失,
    // 在漫长推理过程中没有活的心跳——这令摊位饿死. 看门狗 并读作绞.
    const json part_text = json::parse(R"({"type":"text","text":"answer"})");
    CHECK(dx::content_part_text()(part_text).value() == "answer");

    const json part_think = json::parse(
        R"({"type":"thinking","thinking":[{"type":"text","text":"a"},
                                          {"type":"text","text":"b"}]})");
    const auto segs = dx::content_part_thinking_nested()(part_think);
    REQUIRE(segs.has_value());
    CHECK(segs->size() == 2);
    CHECK((*segs)[0] == "a");
    CHECK((*segs)[1] == "b");

    // 平顶楼 变体 中期计划和方案预算工作组
    const json flat = json::parse(R"({"type":"thinking","text":"flat"})");
    CHECK(dx::content_part_thinking_flat()(flat).value() == "flat");

    // a `text` 须部分 不 被解读为思维,
    // 或者在推理通道中传言土地,而答案从未出现
    CHECK(!dx::content_part_thinking_flat()(part_text).has_value());
    CHECK(!dx::content_part_text()(part_think).has_value());
}

// ═══ 工具调用 ══════════════════════════════════════════════════════════

TEST_CASE("dialect: tool-call index is optional, and absence is meaningful") {
    // OpenAI 发送 `index` 碎片在块块之间重新组装
    const json with = json::parse(R"({"index":2,"function":{"name":"f"}})");
    CHECK(dx::tool_call_index()(with).value() == 2);

    // 许多克隆人忽略了它. Yolo-Auto的工具调用(captured live
    // 2026-09-23)在一个元素中完整地到达,完全没有索引.
    const json without = json::parse(
        R"({"id":"chatcmpl-tool-98fc71679a8b1fc3","type":"function",
            "function":{"name":"get_weather","arguments":"{\"city\": \"Paris\"}"}})");
    CHECK(!dx::tool_call_index()(without).has_value());
    // 打电话者必须回到 ARRAY POSITION。
    // 默认到0在这里会使两个不同的 调用 ' 论点为一个不正确问题。
}

// ═══ 错误: 一个东西有三个信封 ═══════════════════════════════

TEST_CASE("dialect: every error envelope resolves to a message") {
    // OpenAI 形状,以及究竟什么是优娄频螺旋形 -Auto 返回时 401/403
    // (抓获直播2026-09-23).
    const json nested = json::parse(
        R"({"error":{"message":"Your current plan does not include the 'yolo' model.",
                     "type":"invalid_request_error","code":"invalid_api_key"}})");
    CHECK(dx::error_message()(nested).value().starts_with("Your current plan"));

    // 被一些网关压扁了
    const json flat = json::parse(R"({"error":"rate limited"})");
    CHECK(dx::error_message()(flat).value() == "rate limited");

    // llama.cpp 赤裸的形态,没有 `error` 包装
    const json bare = json::parse(R"({"code":500,"message":"context overflow"})");
    CHECK(dx::error_message()(bare).value() == "context overflow");

    // 成功块不能看起来像一个错误,或者每个数据流报告
    // 失败.
    const json ok = json::parse(R"({"choices":[{"delta":{"content":"hi"}}]})");
    CHECK(!dx::error_message()(ok).has_value());
}

// ═══ 模型元数据 ══════════════════════════════════════════════════════

TEST_CASE("dialect: context window is a vLLM extension, absent on OpenAI") {
    // CAPTURED LIVE 2026-09-23 电话: https://yolo-auto.com/v1/models.
    // 两种钥匙都在场。 -token 提示词 返回( E) HTTP 200
    // 块在一 FREE 键,所以数字是真实的,而不是营销的上限。
    //
    // NOTE 这是 NARROW 平面 -key 镜头,全梯子——~15个拼写横跨.
    // top_provider / model_info /元和 GGUF
    // 拱门 -prefixed 键 - 住进
    // detail::advertised_window_tokens (,即 SSOT。
    // 这里的第二个部分列表比没有更糟糕:它漏掉的拼写会悄悄地掉到默认窗口,
    // 计数器会误读,自动 -compaction 将发射错误点(问题49)。
    const json yolo = json::parse(
        R"({"id":"qwen3.8-flash","object":"model","owned_by":"yolo-auto",
            "context_length":131072,"max_model_len":131072})");
    CHECK(dx::model_context_window_flat()(yolo).value() == 131072);

    // 单凭钥匙就够了
    CHECK(dx::model_context_window_flat()(json::parse(R"({"max_model_len":8192})"))
              .value() == 8192);

    // OpenAI 船舶 NEITHER ——这个领域完全没有形状.
    // 缺席必须是代表性的,这就是观察的本意。
    const json openai = json::parse(
        R"({"id":"gpt-4o","object":"model","owned_by":"openai"})");
    CHECK(!dx::model_context_window_flat()(openai).has_value());
}

// ═══ 观察<T>:索赔不是观察 ══════════════════════════

TEST_CASE("Observed: a declared hint cannot impersonate a probed fact") {
    using Ctx = oa::Observed<int>;

    const auto probed   = Ctx::probed(131072);
    const auto declared = Ctx::declared(131072);
    const auto absent   = Ctx::absent();

    // 只有探测器才有权威
    CHECK(probed.confirmed());
    CHECK(!declared.confirmed());      // < - 整个点
    CHECK(!absent.confirmed());

    // 如果错误是便宜的,提示仍然可用。
    CHECK(declared.believed());
    CHECK(probed.believed());
    CHECK(!absent.believed());

    // 缺勤带有可用默认值,而不必假装是测量的。
    CHECK(absent.or_else(8192) == 8192);
    CHECK(probed.or_else(8192) == 131072);
}

TEST_CASE("Observed: the PR-53 failure mode, as a type") {
    // PR #53提议 注册表 线条 DECLARING 提供商服务模式 `yolo`
    // 财务报告和已审计财务报表 `yolo-small`。用真钥匙铺设,
    // / v1/models 两者均不列出,并每一份免费账户均返回403。
    //
    // 在旧的形状下,要求是普通的布勒或弦,
    // 没有东西把它与测量区分开来。这里的出处是类型,
    // 所以需要调用者 FACT 无法意外接受 CLAIM -
    // 它必须要求 `confirmed()` ,声明回答否。
    using Models = oa::Observed<std::vector<std::string>>;

    const auto row_claims = Models::declared({"yolo", "yolo-small"});
    const auto probe_saw  = Models::probed({"qwen3.8-flash", "qwen3.8-27b"});

    CHECK(!row_claims.confirmed());        // a 注册表 行不能断言这一点
    CHECK(probe_saw.confirmed());          // 只有传输可以

    // 而两者真正分歧,这是事实的要紧.
    CHECK(row_claims.value != probe_saw.value);
}

// ── 已宣布的能力:每名拼写"this model can reason" ──────
//
// 是否决定 agentty 提供一个努力阶梯和发送 `reasoning_effort`
// 一点都没有 resolved_caps () 解决为:
//
//     per-model 覆盖 > env > 实时 目录 > id 推断
//
// 这些固定装置是填补 CATALOG 运行为本地服务器。
// 没有他们 llama.cpp / LM 工作室模型通过id
// 推论——一个热力化而不是主机命名( `:7b` 标记,已知的家族前缀),
// 不能分类为 GGUF 文件名或文件名 `publisher/model` 键,
// 所以推理支持最终由文件名决定。

TEST_CASE("Observed: Mistral declares reasoning as a bare bool") {
    // /v1/models 列, Mistral(读取器的形状最初是用来写成的)。
    // 两种极性都很重要 : `false` 是一个真正的宣言,
    // 模式 CANNOT 理由,不得读作 "absent".
    CHECK(oa::dialect::declared_reasoning()(json::parse(R"({"reasoning":true})"))
              == std::optional<bool>{true});
    CHECK(oa::dialect::declared_reasoning()(json::parse(R"({"reasoning":false})"))
              == std::optional<bool>{false});
}

TEST_CASE("Observed: LM Studio declares reasoning as an OBJECT") {
    // GET /api/v1/models，使用 LM Studio 原生 API，见其 REST
    // 参考文档。不能推理的模型会完全省略该键，因此
    // 对象是否存在本身就是能力声明，无需读取内部字段
    // 来回答是否支持的问题。
    //
    // 该 BUG 此 PINS : 读者过去要求 is_boolean
    // (并跳过其他任何,所以每一个) LM 工作室模型悄悄地 被推到
    // ID推论。
    const auto on = json::parse(
        R"({"vision":true,"trained_for_tool_use":true,
            "reasoning":{"allowed_options":["off","on"],"default":"on"}})");
    CHECK(oa::dialect::declared_reasoning()(on) == std::optional<bool>{true});

    // 默认 : "off" 仍然意味着 SUPPORTED
    // ——它只需要问。梯子默认是一个单独的观察,故意不折叠到
    // yes/no.
    const auto off = json::parse(
        R"({"reasoning":{"allowed_options":["off","on"],"default":"off"}})");
    CHECK(oa::dialect::declared_reasoning()(off) == std::optional<bool>{true});
    CHECK(oa::dialect::declared_reasoning_default()(off)
              == std::optional<std::string>{"off"});
}

TEST_CASE("Observed: LiteLLM declares reasoning as supports_reasoning") {
    // LiteLLM 命名该能力 `supports_reasoning` -
    // 看见了 ProviderSpecificModelInfo 输入 litellm/types/utils.py
    // 表面的它,并把它打开 两者 形状客户端可以到达:行上平坦,并嵌入到下面
    // `model_info` ,这就是代理配置 `model_info:` 块通过到/
    // v1/models 和/ 备忘 model/info.
    //
    // 该 BUG 此 PINS : 本表知道 `reasoning` (伊斯兰教,
    // LM 工作室),没有别的,所以 LiteLLM 正确设置的用户
    // `supports_reasoning: true` 一点都没想过,
    // 一边指着 agentty 以相同方式提交 llama.cpp DIRECTLY
    // 工作——因为 llama.cpp 通过不同的门(/道具)检测到。
    // chat_template_caps )这个配置是正确的;读者从未听说过这个词。
    CHECK(oa::dialect::declared_reasoning()(
              json::parse(R"({"id":"gpt-oss","supports_reasoning":true})"))
          == std::optional<bool>{true});
    CHECK(oa::dialect::declared_reasoning()(json::parse(
              R"({"model_name":"gpt-oss",
                  "model_info":{"supports_reasoning":true,
                                "max_input_tokens":131072}})"))
          == std::optional<bool>{true});

    // 两种极性,两种形状:一种是说假的代理,
    // 已经做出了真实的宣告,不能读作缺席.
    CHECK(oa::dialect::declared_reasoning()(
              json::parse(R"({"supports_reasoning":false})"))
          == std::optional<bool>{false});
    CHECK(oa::dialect::declared_reasoning()(
              json::parse(R"({"model_info":{"supports_reasoning":false}})"))
          == std::optional<bool>{false});

    // A LiteLLM 带窗口但能力键未实现的行
    // ABSENT ——窗口不是推理的证据,也不管用什么方法.
    CHECK(!oa::dialect::declared_reasoning()(json::parse(
              R"({"model_info":{"max_input_tokens":131072}})")));
}

// ── 该 COMPLETENESS GATE  ⁇ ────────────────────────────────────────────────
//
// 上面的一切都是个 EXAMPLE 测试:每个服务器一个病例 有人击中。
// 迷雾,那么 LM 演播室,然后 llama.cpp ,
// 则 LiteLLM ——四个修补,四个错误报告,每个都在一个
// 用户注意到他们的推理模型没有思考后添加. 实例测试在下面的表格
// 保持不完整时永远通过,这样历史就是同样的错失,发生四次.
//
// 隔壁的窗户梯子有一个门 正好与这个相对 (该:
// 所有声明上下文窗口的网关方言都能恢复正确值。
// 输入 gateway_context_window_test.cpp 理性从来没有一个,
// 这就是结构原因 LiteLLM's `supports_reasoning` 就
// 能坐两把钥匙离一个 `max_input_tokens` 我们在同一行中正确读了一下,
// 在一个我们已经做的探测器上, 并且仍然隐形。
//
// 这是那扇门 合同: 一个 ROW PER SERVING
// STACK ,使用堆叠实际释放的字段。添加网关意味着将
// 拼写添加到 declared_reasoning() 和
// 这里有排队,没有 回退 ,可以猜到拼写,所以本列表中缺少的
// 堆栈是一个堆栈,其推理模型被文件名分类.
//
// 被描述为表而不是松散的CHECKs,所以一个缺口读作缺的
// LINE——一个评论者可以实际注意到的东西.
TEST_CASE("dialect: every gateway that declares reasoning is recovered") {
    struct Row {
        const char* stack;   // 服务堆栈, 名称
        const char* json;    // 其形状会发出
        bool        reasons; // 观察我们所要观察的
    };

    static const Row rows[] = {
        // Mistral /v1/models——一个平坦的bul,
        // 这个读取器的形状最初是用来写作的.
        {"mistral", R"({"id":"magistral-small","reasoning":true})", true},

        // LM 工作室/ 办公室 api/v1/models - 一个 OBJECT。
        // 存在是声明;不能以任何理由省略密钥的模型。 is_boolean()
        // -only 读者完全跳过(Cf8854ba中固定)。
        {"lm studio",
         R"({"id":"qwen3-8b",
             "reasoning":{"allowed_options":["off","on"],"default":"on"}})",
         true},

        // LiteLLM 平地在排队。
        {"litellm (flat)", R"({"id":"gpt-oss","supports_reasoning":true})", true},

        // LiteLLM ,巢在下面, model_info ——什么代理配置的" ⁇ ".
        // `model_info:` 块通过到/ v1/models 和/ 备忘 model/info.
        // 这是用户正确设置的形状, 而agentty则
        // 完全没有思维。
        {"litellm (model_info)",
         R"({"model_name":"gpt-oss",
             "model_info":{"supports_reasoning":true,
                           "max_input_tokens":131072}})",
         true},

        // 每种拼写上都存在两极分法:明显的假就是真实的宣告(第1段).
        // "this model cannot reason" ) ,
        // 必须被观察到为虚假,而不是被吞噬为缺席。如果丢弃这些,
        // 就会让一个镜头返回常数。 `true` 整个桌子都通过。
        {"mistral (false)", R"({"id":"x","reasoning":false})", false},
        {"litellm (flat false)", R"({"id":"x","supports_reasoning":false})", false},
        {"litellm (nested false)",
         R"({"model_info":{"supports_reasoning":false}})", false},
    };

    for (const auto& r : rows) {
        const auto got = oa::dialect::declared_reasoning()(json::parse(r.json));
        // std::string 而不是生的锥形的 doctest
        // 数据流 赤色的字符指针 ADDRESS ,所以这个消息存在的
        // 唯一一句话是(堆叠的)以0x5648的形式出来. .
        INFO("serving stack: " << std::string{r.stack});
        REQUIRE(got.has_value());          // 此处沉默 = 按文件名分类
        CHECK(*got == r.reasons);
    }
}

TEST_CASE("dialect: a row that declares only a WINDOW says nothing about reasoning") {
    // 隐藏了 秘密的混乱 LiteLLM 虫子的时间,只要它。
    //
    // 我们仔细阅读了这些行——为上下文窗口。 `model_info` 块载
    // `max_input_tokens` 是个井 -understood 说得没错
    // -tested 形状,所以行 LOOKED 已经处理过了,就在一个轴上
    //
    // 两种独立的事实都存在于一行。 了解窗口绝不可读取为任何
    // 方向的推理证据: 不是真的( 宣布限制的网关不说思考) ,
    // 而不是虚假( 将覆盖 ID) -inference
    // 摇摆和插针a DeepSeek-R1 蒸馏为无
    // -reasoning 以至已宣布其法定期限。
    for (const char* window_only : {
             R"({"id":"x","max_input_tokens":131072})",
             R"({"id":"x","model_info":{"max_input_tokens":131072}})",
             R"({"id":"x","max_model_len":262144})",
             R"({"id":"x","n_ctx":32768,"n_ctx_train":131072})",
             R"({"id":"x","context_length":200000})",
         }) {
        INFO("window-only row: " << std::string{window_only});
        CHECK(!oa::dialect::declared_reasoning()(json::parse(window_only)));
    }
}

TEST_CASE("Observed: no reasoning key is ABSENT, not false") {
    // 区别 目录 取决于。 "Absent" 让小径空空
    // 所以id推断 仍然有发言权; "false" 以
    // 服务器从未做过的声称来覆盖它。
    CHECK(!oa::dialect::declared_reasoning()(json::parse(R"({"vision":true})")));
    CHECK(!oa::dialect::declared_reasoning()(json::parse(R"({})")));
}

TEST_CASE("key<bool> reads a JSON boolean, not an integer") {
    // 退转 : std::is_integral_v<bool> 谓 TRUE ,所以,
    // 我说 `key<bool>` 掉进一个完整的手臂 并测试 is_number_integer
    // ()——对真实的,是虚假的. JSON 嘘,每个 `"reasoning": true`
    // 读为 ABSENT ,这正是这个表格存在的 防止的失败,
    // 它会打破米斯特拉尔同时修复 LM 工作室 工作室
    CHECK(oa::key<bool>("x")(json::parse(R"({"x":true})")) == std::optional<bool>{true});
    CHECK(oa::key<bool>("x")(json::parse(R"({"x":false})")) == std::optional<bool>{false});
    // 严格 : 0/1 财务报告和已审计财务报表 "true" 已经
    // 不 布尔。 服务器表示布尔发送布尔; 其它任何东西都是不同的
    // 字段形状, 并且应该掉到链条中的下一个镜头中, 而不是被胁迫。
    CHECK(!oa::key<bool>("x")(json::parse(R"({"x":1})")));
    CHECK(!oa::key<bool>("x")(json::parse(R"({"x":"true"})")));
}

TEST_CASE("Observed: LM Studio declares tool training") {
    // 相同的能力对象。 名称来自它声称的: 该模型是 TRAINED 用于
    // 工具的 – 强于 "the server accepts a tools array" ,
    // 哪个 llama.cpp 任何模型都会做, 并且让失败在
    // 一代人的时间。
    const auto caps = json::parse(R"({"trained_for_tool_use":true,"vision":false})");
    CHECK(oa::dialect::declared_tool_use()(caps) == std::optional<bool>{true});
    CHECK(!oa::dialect::declared_tool_use()(json::parse(R"({"vision":true})")));
}

// ═══ llama.cpp: chat_template_caps ═════════════════════════════════════
//
// llama.cpp's /v1/models 行载 NO 能力物体,
// 所以上面的两只镜头 永远不要为它开火 每个模型都掉到一个的 id推论
// GGUF 路径。/props确实带了一条,这是现有的最佳证据:服务器
// RUNS 模型自己的jinja模板与探测器输入相对应,并显示输出,
// 以查看其反应( common/jinja/caps.cpp, caps_get).
//
// 键式拼写由 caps::to_map ()在该文件中。
// 如果 上游 重命名一个这些固定装置 总是大声的失败 而不是
// 这个功能悄悄地变暗

TEST_CASE("dialect: llama.cpp declares reasoning-effort support in /props") {
    // 缩写真实/道具形状。窗口字段位于盖子旁边,
    // 用不同的镜头阅读,两者都来自一个请求。
    const auto props = json::parse(R"({
        "model_path": "/models/Qwen3-30B-A3B-Q4_K_M.gguf",
        "default_generation_settings": {"n_ctx": 65536},
        "chat_template_caps": {
            "supports_tools": true,
            "supports_tool_calls": true,
            "supports_reasoning_effort": true,
            "supports_preserve_reasoning": false
        }
    })");
    CHECK(oa::dialect::template_reasoning_effort()(props)
              == std::optional<bool>{true});
    CHECK(oa::dialect::template_tool_calls()(props) == std::optional<bool>{true});
}

TEST_CASE("dialect: a template that ignores reasoning_effort says so") {
    // 这件案子很重要 llama.cpp ACCEPTS reasoning_effort
    // 任何请求上, 并将其转发到模板中, 作为 kwarg;
    // 不读取 kwarg 的模板会默默地将其丢弃。
    // 所以 "server took the field" 并不是什么证据,
    // 没有这个声明 agentty 将会提供一个努力梯子 连接到无。
    const auto props = json::parse(
        R"({"chat_template_caps":{"supports_reasoning_effort":false}})");
    CHECK(oa::dialect::template_reasoning_effort()(props)
              == std::optional<bool>{false});
}

TEST_CASE("dialect: an older llama.cpp build is ABSENT, not false") {
    // chat_template_caps比llama.cpp的服务器更新,
    // 所以没有它,一个建筑必须离开目录 rung 空,
    // 让id 推断仍然有发言权。在这里记录`false`将会
    // 积极禁用一个从未说过任何话的服务器上的推理。
    const auto old = json::parse(
        R"({"default_generation_settings":{"n_ctx":4096},"total_slots":1})");
    CHECK(!oa::dialect::template_reasoning_effort()(old));
    CHECK(!oa::dialect::template_tool_calls()(old));

    // 现但漏了一把钥匙:同样的答案。
    const auto partial = json::parse(
        R"({"chat_template_caps":{"supports_tools":true}})");
    CHECK(!oa::dialect::template_reasoning_effort()(partial));
}

TEST_CASE("dialect: tool-call support reads supports_tool_calls, not supports_tools") {
    // 一个模板可以 渲染 工具阵列,
    // 但仍然无法 渲染 助理的电话 tool_calls
    // 回到历史,开始转弯 两个 因为它把一切
    // 称为“失败”, 而不是完全没有工具。
    const auto props = json::parse(R"({"chat_template_caps":{
        "supports_tools": true, "supports_tool_calls": false}})");
    CHECK(oa::dialect::template_tool_calls()(props) == std::optional<bool>{false});
}
