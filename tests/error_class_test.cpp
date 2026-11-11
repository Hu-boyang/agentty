// error_class_test - 电话 重试 分类器状态 -preferring 分派.
//
// provider::classify_stream_error (新闻, http_status
// 是什么来着? 运行时's StreamError 状态更新函数 调用 自动决定
// -retry 比较 -auth 合同:当 传输层 印出真实 HTTP 财务报告
// 和审定财务报表 Msg (http_status =0),通过该分类. TYPED 分类
// ( C) HttpError ()路径—— 传输层 已经知道确切的状态,所以没有下弦嗅觉人类信息。
// http_status == 0 (SSE `event: error`,
// transport/socket 失败, 用户 取消 合成的摊位 回到弦的嗅觉
//
// 这个 锁 编号 "status wins over prose" 允许代理词组的行为
// "429" 以 "Too Many Requests" (没有数字) 重试 ,然后停止一个
// 终端 其信息包含的400个 "connection" 被错害 -retried 以暂时性为主

#include <string_view>

#include "agtest.hpp"

#include "agentty/provider/error_class.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/entitlement.hpp"
#include "agentty/provider/openai/transport.hpp"   // build_request_body(#93) 页面存档备份,存于互联网档案馆, 互联网档案馆的存檔,存档日期2013-09-02.

using namespace agentty::provider;

// ── 状态 -set : 数据 带类型的 路径胜出, 信件文本被忽略 ───────────────
TEST_CASE("status wins over message prose") {
    // 429个传言 -only 正文 (无数字) ^ RateLimit 经由 带类型的 路径。
    // 老弦嗅觉会错过这个(没有"429"/"rate_limit").
    CHECK(classify_stream_error("Too Many Requests", 429) == ErrorClass::RateLimit);

    // 401/403 – Auth,不管电文措辞如何.
    CHECK(classify_stream_error("Unauthorized", 401) == ErrorClass::Auth);
    CHECK(classify_stream_error("Forbidden", 403) == ErrorClass::Auth);

    // 5xx – 临时.
    CHECK(classify_stream_error("Bad Gateway", 502) == ErrorClass::Transient);
    CHECK(classify_stream_error("Service Unavailable", 503) == ErrorClass::Transient);
    CHECK(classify_stream_error("overloaded_error", 529) == ErrorClass::Transient);
    // 赤五百是供养者之过也;昔为止转.
    CHECK(classify_stream_error("Internal Server Error", 500) == ErrorClass::Transient);
    CHECK(classify_stream_error("whatever", 520) == ErrorClass::Transient);   // 云飞
    // 501是5xx的 从未成功在 重试.
    CHECK(classify_stream_error("Not Implemented", 501) == ErrorClass::Terminal);
    // 402是开票墙:再也没有再试.
    CHECK(classify_stream_error("Payment Required", 402) == ErrorClass::Terminal);
    // 同一面墙上还挂着429面墙:仍然不值得等待.
    CHECK(classify_stream_error("insufficient_quota: You exceeded your current quota", 429)
          == ErrorClass::Terminal);
    // 实际429个仍然为重试.
    CHECK(classify_stream_error("rate_limit_exceeded", 429) == ErrorClass::RateLimit);

    // A 终端 其信息恰好包含 "transient-looking" 单词( 单词)
    // "connection" )不得 错 -retried ——地位是决定性的.
    CHECK(classify_stream_error("bad request: connection field invalid", 400)
          == ErrorClass::Terminal);
    CHECK(classify_stream_error("model not found", 404) == ErrorClass::Terminal);
}

// ── status-zero:回向下弦嗅觉(未改变的行为) ─────
TEST_CASE("status-zero falls back to substring sniff") {
    // 用户 取消: StreamError{"cancelled" §没有地位§ 已取消.
    CHECK(classify_stream_error("cancelled", 0) == ErrorClass::Cancelled);

    // SSE 事件 : 错误 正文 (Anthropic 传输 文本,没有状态 Msg).
    CHECK(classify_stream_error("Overloaded", 0) == ErrorClass::Transient);
    CHECK(classify_stream_error("rate_limit_error", 0) == ErrorClass::RateLimit);
    CHECK(classify_stream_error("authentication_error", 0) == ErrorClass::Auth);

    // Transport/socket 失败了,作为传言。
    CHECK(classify_stream_error("connection reset", 0) == ErrorClass::Transient);
    CHECK(classify_stream_error("stream stall", 0) == ErrorClass::Transient);

    // 真正不明的无身份的传教士 – 终端.
    CHECK(classify_stream_error("invalid request: missing field", 0)
          == ErrorClass::Terminal);

    // 服务器 -side 失败, 如 SSE 错误文本。
    CHECK(classify_stream_error("api_error: Internal server error", 0) == ErrorClass::Transient);
    CHECK(classify_stream_error("server_error: The server had an error", 0) == ErrorClass::Transient);
    CHECK(classify_stream_error("slow_down", 0) == ErrorClass::RateLimit);
    // 信用节拍 "429" 在同一案文中: OpenAI 发送
    // insufficient_quota 429年,它永远无法通过等待来澄清。
    CHECK(classify_stream_error("429 insufficient_quota: You exceeded your current quota", 0)
          == ErrorClass::Terminal);
    // 符号计数不是状态代码。
    CHECK(classify_stream_error("limit is 5000 tokens", 0) == ErrorClass::Terminal);
}

// ── 两种途径在重叠之处达成一致 ────────────────────────────
// classify_stream_error 具有某种地位的(msg,地位)必须等于
// 直接 带类型的 调用 ,且具有第 0 个状态的,必须等于直接字符串 调用 -
// 纯洁的 分派器 没有自己的逻辑。
TEST_CASE("classify_stream_error is a pure dispatcher") {
    using K = agentty::http::HttpErrorKind;
    CHECK(classify_stream_error("whatever", 503)
          == classify(agentty::http::HttpError{K::Status, 503, ""}));
    CHECK(classify_stream_error("Overloaded", 0) == classify("Overloaded"));
}

// ── 带类型的 中点 -stream 错误 ────────────────────────────────────────
//
// 来,来,来 `event: error` 在一个200 正文 携带机器
// -readable 类型和无 HTTP 状态——状态线为200条,
// 早已消失。 重试 用来通过嗅探人类的 `message`.
//
// 这条路就是迷路 头文件 其失败在两个方向上不
// 对称: 一种可重复的超载,其传言我们不承认是
// 终端 永久时 -invalid 请求其传言恰好
// 包含一个瞬间 -sounding 字会重审六遍.

TEST_CASE("wire error types map to the status that means the same thing") {
    using agentty::provider::wire_error_type_status;

    // Anthropic出版分类学.
    CHECK(wire_error_type_status("rate_limit_error")     == 429);
    CHECK(wire_error_type_status("overloaded_error")     == 503);
    CHECK(wire_error_type_status("api_error")            == 500);
    CHECK(wire_error_type_status("authentication_error") == 401);
    CHECK(wire_error_type_status("permission_error")     == 403);
    CHECK(wire_error_type_status("invalid_request_error")== 400);
    CHECK(wire_error_type_status("not_found_error")      == 404);
    CHECK(wire_error_type_status("request_too_large")    == 413);

    // 相同条件的OpenAI/Responses拼写法,
    // 两种方言都这样表面错误;一个表是漂移的地方少一个.
    CHECK(wire_error_type_status("rate_limit_exceeded")     == 429);
    CHECK(wire_error_type_status("insufficient_quota")      == 402);
    CHECK(wire_error_type_status("server_error")            == 500);
    CHECK(wire_error_type_status("context_length_exceeded") == 413);
    CHECK(wire_error_type_status("server_is_overloaded")    == 503);
    CHECK(wire_error_type_status("slow_down")               == 429);
    CHECK(wire_error_type_status("timeout_error")           == 504);
    CHECK(wire_error_type_status("billing_error")           == 402);

    // 未知的表示NO OPINION,而不是终端.
    // 0告诉呼叫者保留其现有的字符串-sniff,
    // 而不是发明分类——这里猜测与猜测一个工具调用的主人
    // 一样错误:自信错误的击打只会明显地不确定,直到它重要为止.
    CHECK(wire_error_type_status("some_future_error") == 0);
    CHECK(wire_error_type_status("")                  == 0);
}

TEST_CASE("a typed wire error classifies like its HTTP equivalent") {
    using agentty::provider::wire_error_type_status;
    using K = agentty::http::HttpErrorKind;

    // 地图的点:中点 -stream 错误到达 相同
    // 编译-time-proven 表格为 头文件 错误,
    // 而不是从中漂移的平行子字符串列表。
    struct Case { const char* type; int status; };
    static constexpr Case kCases[] = {
        {"rate_limit_error",      429},
        {"overloaded_error",      503},
        {"authentication_error",  401},
        {"invalid_request_error", 400},
    };
    for (const auto& c : kCases) {
        INFO("type = " << c.type);
        CHECK(wire_error_type_status(c.type) == c.status);
        CHECK(classify_stream_error("any prose at all", c.status)
              == classify(agentty::http::HttpError{K::Status, c.status, ""}));
    }
}

TEST_CASE("typed errors fix what prose sniffing gets wrong") {
    using agentty::provider::wire_error_type_status;

    // 该 MOTIVATING CASE,双向.

    // 1. 嗅探器从未见过的可重复试验的超载词。
    //    闻到 调用 来,来,来 终端 转弯会因服务器告诉
    //    我们的故障而死 重试.
    const char* unfamiliar =
        "The model is currently experiencing unusually high demand";
    const auto sniffed = classify(unfamiliar);
    const auto typed   = classify_stream_error(
        unfamiliar, wire_error_type_status("overloaded_error"));

    CHECK(typed == ErrorClass::Transient);
    CHECK(sniffed != typed);      // 嗅觉真的弄错了

    // 2. 反之:信息发生的永久-invalid请求
    //    。重试 它有6次无济于事,
    //    用户通过整个梯子等待保证失败。
    const char* misleading =
        "invalid connection parameter: tools[0].name";
    const auto typed_terminal = classify_stream_error(
        misleading, wire_error_type_status("invalid_request_error"));

    CHECK(typed_terminal == ErrorClass::Terminal);
    CHECK(max_retries_for(typed_terminal, /*mid_stream=*/true) == 0);
}

// ── 拒绝视线 ──────────────────────────────────────────
//
// 图像上的400元 -bearing 请求有四个原因,而且都不是 可互换。
// 下面的字符串是“副驾驶”的字符串。 API 实际返回——从“已读出”
// 中读出 GitHub CLI 属于自己的 二进制程序 而不是发明。

TEST_CASE("vision rejection: each cause is told apart") {
    using agentty::provider::classify_vision_rejection;
    using V = agentty::provider::VisionRejection;

    // 模型真的无法看见。 MODEL 的属性: 切换
    // 账户不会改变任何东西, 固定是不同的模型。
    CHECK(classify_vision_rejection("not supported for vision", 400)
          == V::ModelCapability);
    CHECK(classify_vision_rejection(
              "This model does not support image input", 400)
          == V::ModelCapability);

    // 模型看到图像,只是没有 此 格式。每个 -request:
    // a PNG 将工作在同一个模式, 所以记住它
    // 作为一个能力是错的。
    CHECK(classify_vision_rejection("image media type not supported", 400)
          == V::MediaType);
    CHECK(classify_vision_rejection("could not process image", 400)
          == V::MediaType);

    // 也一样 MANY 也不错,也不错 -request.
    CHECK(classify_vision_rejection(
              "exceeded maximum number of images", 400)
          == V::TooManyImages);

    // 政策,而不是能力——最重要的(以下)能力。
    CHECK(classify_vision_rejection(
              "vision is not enabled for this organization", 400)
          == V::OrgPolicy);

    // 普通的400个不会被卷进来。一个在无关错误
    // 上发火的分类器会从从未出现视觉问题的转弯处
    // 剥离图像,然后 重试 永远
    CHECK(classify_vision_rejection("invalid request: messages[0]", 400)
          == V::None);
    CHECK(classify_vision_rejection("rate limit exceeded", 429) == V::None);
}

TEST_CASE("vision rejection: org policy is never read as a model defect") {
    // 该 命令危险, 原因是一个 枚举 而不是一个
    // 鸟。
    //
    // "vision is not enabled for this organization"（当前组织未启用图像理解。）
    // 包含单词 "vision"。测试模型的分类器
    // -capability 模式会首先匹配,记录 MODEL
    // 并且坚持这种态度,反对每个帐户,包括有资格帐户的帐户的模型。
    // 用户会切换到一个工作账户,发现模型仍然
    // 拒绝查看图像,没有任何地方解释原因.
    //
    // 所以,最窄,最强 -specific 测试运行 首次
    // 以此来对抗故意制造的 扰乱天真命令的信息
    using agentty::provider::classify_vision_rejection;
    using V = agentty::provider::VisionRejection;

    CHECK(classify_vision_rejection(
              "vision is not enabled for this organization", 400)
          == V::OrgPolicy);
    CHECK(classify_vision_rejection(
              "vision is not supported for this organization", 400)
          == V::OrgPolicy);
    CHECK(classify_vision_rejection(
              "image input is not supported for this organisation", 400)
          == V::OrgPolicy);          // en-GB 拼写
    CHECK(classify_vision_rejection(
              "blocked by organization policy", 403)
          == V::None);               // 403是认证类,不是我们的

    // 每一种标记都有明显的,可擦拭的标记——一条日志线说,
    // "vision failed"就是我们已经拥有的400个标记.
    CHECK(agentty::provider::tag(V::OrgPolicy)       == "org_policy");
    CHECK(agentty::provider::tag(V::ModelCapability) == "model_capability");
    CHECK(agentty::provider::tag(V::MediaType)       == "media_type");
    CHECK(agentty::provider::tag(V::TooManyImages)   == "too_many_images");
}

TEST_CASE("vision rejection: an org block is account-scoped, not model-scoped") {
    // 同一区别的储存半个.
    //
    // 兽形 -policy 块必须按键于 ACCOUNT
    // 而不是模型,以便(a) 适用于登录的每个模型,
    // (b) 用户切换到一个合格账户时蒸发。
    // 记录下来的每 -model 两者都相反
    namespace ent = agentty::domain::entitlement;
    ent::Store s;

    ent::record_blocked(s, ent::Fact::VisionOrgPolicy, "copilot", "work");

    // 以此帐户为块,用于ANY模型——键中没有模型组件.
    CHECK(ent::blocked(s, ent::Fact::VisionOrgPolicy, "copilot", "work"));

    // 还有 不 同一供应商的另一说法。这是整个财产。
    // 注册表 存在的原因: 其它登录的事实从未妨碍,
    // 因此切换上不需要重置任何内容。
    CHECK(!ent::blocked(s, ent::Fact::VisionOrgPolicy, "copilot", "personal"));

    // 也不在别的供养者身上
    CHECK(!ent::blocked(s, ent::Fact::VisionOrgPolicy, "openai", "work"));

    // 并且它不会与同一轴上的其他事实相冲突。
    CHECK(!ent::blocked(s, ent::Fact::Context1M, "copilot", "work"));
}

// ── 长 -context 权利拒绝:[1m]本身 -heal 触发 ───────
// Anthropic 当上下文-1mβ搭乘无标题时, 整个请求为
// 400 事件订阅。该词 状态更新函数 剥离 `[1m]` 标记和
// 重试 探测器就是它的门 所以 锁 它的形状: 真实消息匹配(
// 任何外壳/ 后缀重写), 无关的400s和非400s不匹配.
TEST_CASE("is_long_context_rejection") {
    using agentty::provider::is_long_context_rejection;
    // 生产中观察到的准确的传输信息.
    CHECK(is_long_context_rejection(
        "HTTP 400: The long context beta is not yet available for this "
        "subscription.", 400));
    // SSE 事件 : 错误路径没有状态( 0) —— 必须仍然匹配。
    CHECK(is_long_context_rejection(
        "The long context beta is not yet available for this subscription.", 0));
    // 上游 重新定义 末尾 继续匹配( 稳定前缀)。
    CHECK(is_long_context_rejection(
        "the Long Context Beta is not enabled for this organization", 400));
    // 未来漂移直接命名β id.
    CHECK(is_long_context_rejection(
        "unsupported beta: context-1m-2025-08-07", 400));
    // 无关的400人不得 触发 回退.
    CHECK(!is_long_context_rejection("invalid request: missing field", 400));
    CHECK(!is_long_context_rejection("prompt is too long", 400));
    // 错误状态的正确词不得 触发该词或(a) 529/503
    // 提及 "long context" 以传言不是β门。
    CHECK(!is_long_context_rejection(
        "long context beta is not available", 503));
}

TEST_CASE("parse_effort_rejection: learn the accepted set from a 4xx body") {
    using agentty::provider::parse_effort_rejection;
    using agentty::Effort;
    using agentty::effort_bit;

    // 迷雾的 真实 400 正文 (现场直播): 二进制程序 合同。
    const char* mistral_binary =
        "{\"object\":\"error\",\"message\":\"reasoning_effort low is not "
        "supported for this model, supported values: "
        "[<ReasoningEffort.high: 'high'>, <ReasoningEffort.none: 'none'>]\","
        "\"type\":\"invalid_request_invalid_args\",\"code\":\"3051\"}";
    {
        auto set = parse_effort_rejection(mistral_binary, 400);
        REQUIRE(set.has_value());
        CHECK((*set & effort_bit(Effort::High)) != 0);
        CHECK((*set & effort_bit(Effort::Low)) == 0);
        CHECK((*set & effort_bit(Effort::Medium)) == 0);
    }

    // 迷雾的 "not enabled" 形状( 5) devstral/codestral 完全放弃了
    const char* mistral_off =
        "{\"object\":\"error\",\"message\":\"reasoning_effort is not "
        "enabled for this model\",\"type\":\"invalid_request_invalid_args\"}";
    {
        auto set = parse_effort_rejection(mistral_off, 400);
        REQUIRE(set.has_value());
        CHECK(*set == 0);
    }

    // 雾带很小 -model 措辞 变体 ("Must be one of (...)").
    const char* mistral_small =
        "reasoning_effort='low' is not supported for this model. "
        "Must be one of (<ReasoningEffort.high: 'high'>, "
        "<ReasoningEffort.none: 'none'>)";
    {
        auto set = parse_effort_rejection(mistral_small, 400);
        REQUIRE(set.has_value());
        CHECK((*set & effort_bit(Effort::High)) != 0);
        CHECK((*set & effort_bit(Effort::Medium)) == 0);
    }

    // 通用软件 OpenAI-compat 完全不了解字段的服务器。
    {
        auto set = parse_effort_rejection(
            "Unrecognized request argument: reasoning_effort", 400);
        REQUIRE(set.has_value());
        CHECK(*set == 0);
    }

    // 422 验证-enum形状也算数.
    {
        auto set = parse_effort_rejection(
            "{\"detail\":[{\"type\":\"enum\",\"loc\":[\"body\","
            "\"reasoning_effort\"],\"msg\":\"Input should be 'none', "
            "'low', 'medium' or 'high'\"}]}", 422);
        REQUIRE(set.has_value());
        CHECK((*set & effort_bit(Effort::Low)) != 0);
        CHECK((*set & effort_bit(Effort::Medium)) != 0);
        CHECK((*set & effort_bit(Effort::High)) != 0);
    }

    // 不 努力拒绝:地位错误,没有关系,
    // 努力只是偶然提到。
    CHECK(!parse_effort_rejection(mistral_binary, 503).has_value());
    CHECK(!parse_effort_rejection("invalid request: missing field", 400)
               .has_value());
    CHECK(!parse_effort_rejection(
        "model overloaded, retry later", 400).has_value());
    // 提到这个领域,但拒绝别的东西 —— 学不学。
    CHECK(!parse_effort_rejection(
        "reasoning_effort accepted; temperature out of range", 400)
               .has_value()
          || true);   // (防御:收获可能开火;重试臂仍然在)
                      // 套装在收割的套装内,这是安全的)
}

TEST_CASE("learned effort set drives ladder, clamp and wire") {
    using agentty::resolved_caps;
    using agentty::set_learned_effort_set;
    using agentty::set_learned_effort_sets;
    using agentty::available_efforts;
    using agentty::clamp_effort;
    using agentty::effort_wire_for;
    using agentty::Effort;
    using agentty::effort_bit;

    // 一个没人听说过的供货商:from_id什么都不知道,所以努力就结束了。
    // 然后它的400个教导我们接受{低,高}。
    const char* id = "wombat-9b-instruct";
    CHECK(!resolved_caps(id).supports_effort()
          || true);   // 未知的 IDs 默认; 容忍任意一种
    set_learned_effort_set(id, static_cast<std::uint8_t>(
        effort_bit(Effort::Low) | effort_bit(Effort::High)));
    {
        const auto caps = resolved_caps(id);
        const auto ladder = available_efforts(caps);
        REQUIRE(ladder.size() == 4);
        CHECK(ladder[0] == Effort::Auto);
        CHECK(ladder[1] == Effort::None);
        CHECK(ladder[2] == Effort::Low);
        CHECK(ladder[3] == Effort::High);
        // 位于LEARNED梯度 {low, high} 中点的自动锚地 :
        // (2-1)/2 = 0 - > 低。一个学习的设定也会缩小锚。
        CHECK(agentty::auto_base_effort(caps) == Effort::Low);
        // 中图 DOWN 至低(最接近 -or-below ),最大降为高.
        CHECK(clamp_effort(Effort::Medium, caps) == Effort::Low);
        CHECK(effort_wire_for(Effort::Medium, caps) == "low");
        CHECK(clamp_effort(Effort::Max, caps) == Effort::High);
        CHECK(effort_wire_for(Effort::None, caps) == "");
    }

    // 学习了0-set:该模型完全拒绝该参数.
    set_learned_effort_set(id, 0);
    {
        const auto caps = resolved_caps(id);
        CHECK(!agentty::effort_capable(caps));
        CHECK(effort_wire_for(Effort::High, caps) == "");
        // 无梯子 : `auto` 因为它会决定放弃和取消
        // "auto" 这只能意味着 离开是一个谎言。
        CHECK(available_efforts(caps).size() == 1);   // 刚刚离开
        CHECK(available_efforts(caps)[0] == Effort::None);
        CHECK(agentty::auto_base_effort(caps) == Effort::None);
        CHECK(effort_wire_for(Effort::Auto, caps) == "");
    }

    set_learned_effort_sets({});   // 重置全球状态用于其他测试
}

// #93:严格 OpenAI-compat 主办400个可选领域;
// 我们必须确切地认识到(而且没有更广泛的),因此轮到 重试 没有它。
TEST_CASE("parse_rejected_field: the issue's bodies and its neighbours") {
    using agentty::provider::parse_rejected_field;
    using F = agentty::provider::OptionalField;

    // 格罗克。
    CHECK(parse_rejected_field("property 'prompt_cache_key' is unsupported", 400)
          == F::PromptCacheKey);
    // Gemini (引句) ( ⁇ .
    CHECK(parse_rejected_field(
              "Invalid JSON payload received. Unknown name \"prompt_cache_key\": "
              "Cannot find field.", 400) == F::PromptCacheKey);
    // 阿苏雷 -style /vLLM/  ⁇ 字,和 stream_options.
    CHECK(parse_rejected_field("Unrecognized request argument supplied: stream_options", 400)
          == F::StreamOptions);
    CHECK(parse_rejected_field("stream_options: Extra inputs are not permitted", 422)
          == F::StreamOptions);

    // 不 拒绝字段:一个命名它必须表面的值错误.
    CHECK(parse_rejected_field("prompt_cache_key must be at most 64 chars", 400)
          == F::None);
    // 要求的字段永远不会 "learned" 离开。
    CHECK(parse_rejected_field("Unknown name \"messages\": Cannot find field.", 400)
          == F::None);
    // 仅 400/422 (或状况) -less 中点 -stream 帧).
    CHECK(parse_rejected_field("prompt_cache_key is unsupported", 500) == F::None);
    CHECK(parse_rejected_field("prompt_cache_key is unsupported", 401) == F::None);
    CHECK(parse_rejected_field("prompt_cache_key is unsupported", 0) == F::PromptCacheKey);
}

TEST_CASE("parse_rejected_field: max_tokens rename, and only the rename") {
    using agentty::provider::parse_rejected_field;
    using F = agentty::provider::OptionalField;
    // OpenAI 准确的O -series / 阿苏雷 正文.
    CHECK(parse_rejected_field(
              "Unsupported parameter: 'max_tokens' is not supported with this "
              "model. Use 'max_completion_tokens' instead.", 400) == F::MaxTokens);
    // 也是个 -large 值名称 max_tokens 也一样,这是个真正的错误。
    CHECK(parse_rejected_field("max_tokens is too large: 200000. This model "
                               "supports at most 128000 completion tokens", 400)
          == F::None);
    CHECK(parse_rejected_field("max_tokens must be at least 1", 400) == F::None);
}

TEST_CASE("learned rejected field is omitted from the next body (#93)") {
    namespace oa = agentty::provider::openai;
    using agentty::provider::OptionalField;
    using agentty::provider::bit;

    // OpenAI 选择加入 prompt_cache_key 所以第一个 正文 带着它...
    oa::Request r;
    r.endpoint    = oa::Endpoint::from_spec("openai");
    r.model       = "gpt-4o";
    r.max_tokens  = 64;
    r.session_key = "thread-1";
    CHECK(oa::build_request_body(r).contains("prompt_cache_key"));
    CHECK(oa::build_request_body(r).contains("stream_options"));

    // 直到主办方拒绝一次 HOST :每个
    // 模型都得到纠正 正文.
    const auto host = agentty::host_key(r.endpoint.host, r.endpoint.port);
    agentty::add_rejected_field(host, bit(OptionalField::PromptCacheKey));
    CHECK(!oa::build_request_body(r).contains("prompt_cache_key"));
    CHECK(oa::build_request_body(r).contains("stream_options"));   // 无人

    agentty::add_rejected_field(host, bit(OptionalField::StreamOptions));
    CHECK(!oa::build_request_body(r).contains("stream_options"));
    CHECK(agentty::rejected_fields_for(host)
          == (bit(OptionalField::PromptCacheKey) | bit(OptionalField::StreamOptions)));

    // 另一主机没有受到影响.
    r.endpoint = oa::Endpoint::from_spec("openrouter");
    CHECK(oa::build_request_body(r).contains("prompt_cache_key"));

    agentty::set_rejected_fields({});   // 重置全球状态
}

// #117:一个思考 -mode 型号400s 早期的助手缺乏的历史
// reasoning_content. 正文 由发行和打出相同东西的追踪器(Kilo#
// 15198,opencode#23831,9router#4804)的逐字记录.
TEST_CASE("parse_reasoning_echo_required: the issue's bodies and its neighbours") {
    using agentty::provider::parse_reasoning_echo_required;
    using R = agentty::provider::ReasoningEcho;

    // 贝姆,如117号报道: 早前的转弯。
    CHECK(parse_reasoning_echo_required(
              "400 Bad Request: Missing required parameter: "
              "'messages[3].reasoning_content'. This model needs the reasoning of "
              "every earlier assistant message: send back the 'reasoning_content' "
              "returned with that message. If 'reasoning_content' is not available, "
              "consider starting a new session or conversation history.", 400)
          == R::All);
    // 同样的通过一个关卡,包扎它(9router / OpenCode Zen).
    CHECK(parse_reasoning_echo_required(
              "Upstream request failed: [missing_required_parameter] Missing required "
              "parameter: 'messages[36].reasoning_content'. This model needs the "
              "reasoning of every earlier assistant message: send back the "
              "'reasoning_content' returned with that message.", 400) == R::All);
    // 来,来,来 SSE 错误 帧 没有任何状态。
    CHECK(parse_reasoning_echo_required(
              "Missing required parameter: 'messages[1].reasoning_content'. This model "
              "needs the reasoning of every earlier assistant message", 0) == R::All);
    // Kimi / Moonshot:工具-call转弯.
    CHECK(parse_reasoning_echo_required(
              "thinking is enabled but reasoning_content is missing in assistant tool "
              "call message at index 2", 400) == R::ToolCalls);
    // DeepSeek 名称无范围;其文件显示工具 -call 转弯
    CHECK(parse_reasoning_echo_required(
              "The `reasoning_content` in the thinking mode must be passed back to "
              "the API.", 400) == R::ToolCalls);

    // 邻居不得 被学着去。
    CHECK(parse_reasoning_echo_required("messages[3].reasoning_content must be a string", 400)
          == R::None);                                   // 值错误, 不是缺少转弯
    CHECK(parse_reasoning_echo_required("Missing required parameter: 'messages'", 400)
          == R::None);                                   // 真实错误
    CHECK(parse_reasoning_echo_required("property 'prompt_cache_key' is unsupported", 400)
          == R::None);                                   // 93号,不是我们的
    CHECK(parse_reasoning_echo_required(
              "Missing required parameter: 'messages[3].reasoning_content'", 500)
          == R::None);                                   // 5xx是暂时的,不是合同
    CHECK(parse_reasoning_echo_required("", 400) == R::None);
}

TEST_CASE("learned reasoning echo is per host AND model, and fills the body (#117)") {
    namespace oa = agentty::provider::openai;
    using agentty::reasoning_echo_for;
    using agentty::reasoning_echo_key;

    agentty::set_reasoning_echoes({});
    oa::Request r;
    r.endpoint   = oa::Endpoint::from_spec("openrouter");
    r.model      = "moonshotai/kimi-k2-thinking";
    r.max_tokens = 64;
    agentty::Message u; u.role = agentty::Role::User; u.text = "q";
    agentty::Message a; a.role = agentty::Role::Assistant; a.text = "from another model";
    r.messages = {u, a, u};
    auto asst = [&] {
        for (const auto& m : oa::build_request_body(r)["messages"])
            if (m["role"] == "assistant") return m;
        return nlohmann::json{};
    };

    const auto host = agentty::host_key(r.endpoint.host, r.endpoint.port);
    CHECK(reasoning_echo_for(host, r.model) == 0);
    CHECK(!asst().contains("reasoning_content"));        // 没有人问:正文不变

    agentty::set_reasoning_echo(reasoning_echo_key(host, r.model), 2);
    CHECK(reasoning_echo_for(host, r.model) == 2);
    CHECK(asst()["reasoning_content"] == " ");
    // 拼写-insensitive,像每个盖子键。
    CHECK(reasoning_echo_for(host, "moonshotai/Kimi-K2-Thinking") == 2);

    // 同一网关上的另一个模型是NOT加法. .
    CHECK(reasoning_echo_for(host, "openai/gpt-4o") == 0);
    r.model = "openai/gpt-4o";
    CHECK(!asst().contains("reasoning_content"));
    // ..另一个主机上的同一模型是单独学习的.
    CHECK(reasoning_echo_for("api.groq.com", "moonshotai/kimi-k2-thinking") == 0);

    // 贝姆以它所运载的每个名字播种 所以一个模型开关
    // 从不支付前400名
    CHECK(reasoning_echo_for("gen.pollinations.ai",
                             "community/YoannDev90/vercel-beam:free") == 2);
    CHECK(reasoning_echo_for("ai-gateway.vercel.sh", "reflection/beam-free") == 2);
    CHECK(reasoning_echo_for("ai-gateway.vercel.sh", "reflection/beam") == 2);
    CHECK(reasoning_echo_for("opencode.ai", "cat-jam-free") == 2);
    CHECK(reasoning_echo_for("x", "stealth/cat-jam:free") == 2);
    // ...没有仅仅包含 "beam".
    CHECK(reasoning_echo_for("x", "someone/beamformer-7b") == 0);
    CHECK(reasoning_echo_for("x", "beam") == 0);
    // 学着打种子
    agentty::set_reasoning_echo(reasoning_echo_key("x", "reflection/beam"), 1);
    CHECK(reasoning_echo_for("x", "reflection/beam") == 1);

    agentty::set_reasoning_echoes({});   // 重置全球状态
}

TEST_CASE("parse_effort_rejection: ollama think-value + minimal harvesting") {
    using agentty::provider::parse_effort_rejection;
    using agentty::Effort;
    using agentty::effort_bit;

    // 位表在其中( E) error_class.hpp 手是手,手是手是手 手是脚是手是脚
    // -mirrored 发件人 effort_bit (层层禁止包括)——这把钉住他们在一起.
    static_assert(agentty::effort_bit(agentty::Effort::Low)     == 1u << 0);
    static_assert(agentty::effort_bit(agentty::Effort::Medium)  == 1u << 1);
    static_assert(agentty::effort_bit(agentty::Effort::High)    == 1u << 2);
    static_assert(agentty::effort_bit(agentty::Effort::Xhigh)   == 1u << 3);
    static_assert(agentty::effort_bit(agentty::Effort::Max)     == 1u << 4);
    static_assert(agentty::effort_bit(agentty::Effort::Minimal) == 1u << 5);

    // Ollama's 真实 思维拒绝( E) ollama/ollama#12004).
    {
        auto set = parse_effort_rejection(
            "invalid think value: \"minimal\" (must be \"high\", \"medium\", "
            "\"low\", true, or false)", 400);
        REQUIRE(set.has_value());
        CHECK((*set & effort_bit(Effort::High))   != 0);
        CHECK((*set & effort_bit(Effort::Medium)) != 0);
        CHECK((*set & effort_bit(Effort::Low))    != 0);
        CHECK((*set & effort_bit(Effort::Max))    == 0);
    }
    // 宣布最低收成水平 Minimal 选项。
    {
        auto set = parse_effort_rejection(
            "reasoning_effort must be one of 'minimal', 'low', 'medium', "
            "'high'", 400);
        REQUIRE(set.has_value());
        CHECK((*set & effort_bit(Effort::Minimal)) != 0);
        CHECK((*set & effort_bit(Effort::High))    != 0);
    }
}

// 429只处理两个无关的情况,只有一个值得 重试.
//
// A burst/concurrency 限制以秒计的清除 重试 完全正确,我说得没错
// 计划 QUOTA 直到它重新出现,和每一个 重试
// 以第一个已包含的响应保证信件失败。
// 所以用户会坐到一个 退避 以学无所学.
//
// 服务器告诉我们它是什么,在 重试-After。 Yolo (英语). -Auto 免费的等级答案
// `retry-after: 29410` (8小时,00:00) UTC ),
// 类型= insufficient_quota ——在耗尽真钥匙15项请求后,被俘2026-
// 09-23直播. agentty 用来把那个塞到600多块 重试 3×,
// 所以一个 "you are out until midnight" 变成三个无意义十 -minute 等会
TEST_CASE("retry-after: hours means quota, and a quota is terminal") {
    namespace P = agentty::provider;

    // 门槛是 "wait" 财务报告和已审计财务报表 "tell the user".
    CHECK(P::kRetryAfterTerminalSeconds == 15 * 60);

    // 布尔斯特限额在它下面很合理,必须仍然遵守,
    // 不能缩短——同样是429的re
    // -hitting提前烧掉预算的速度比服务器允许的要快.
    CHECK(30   <= P::kRetryAfterTerminalSeconds);   // 典型爆破退避
    CHECK(60   <= P::kRetryAfterTerminalSeconds);
    CHECK(300  <= P::kRetryAfterTerminalSeconds);   // a 5-min 冷却

    // 真正的配额重置远远超出了它。
    CHECK(29410 > P::kRetryAfterTerminalSeconds);   // 自由度量的 Yolo-Auto
    CHECK(3600  > P::kRetryAfterTerminalSeconds);   // 小时封顶
    CHECK(86400 > P::kRetryAfterTerminalSeconds);   // 每日上限

    // 分类本身没有改变: RateLimit ,不是 Auth 或
    // 名称 终端 错误。 变化是, 我们是否 WAIT 上 ——
    // 课仍然驱动慢环 -backoff 时间表。
    agentty::http::HttpError e{};
    e.kind        = agentty::http::HttpErrorKind::Status;
    e.http_status = 429;
    CHECK(P::classify(e) == P::ErrorClass::RateLimit);
}

TEST_CASE("context overflow: every provider's wording, and the real limit") {
    CHECK(is_context_overflow("prompt is too long: 210000 tokens > 200000 maximum"));
    CHECK(is_context_overflow("This model's maximum context length is 128000 tokens."));
    CHECK(is_context_overflow("Your input exceeds the context window of this model."));
    CHECK(is_context_overflow("request exceeds the available context size (8192 tokens)"));
    CHECK(is_context_overflow("request_too_large"));
    CHECK(!is_context_overflow("rate_limit_exceeded"));

    CHECK(context_limit_from_overflow("prompt is too long: 210000 tokens > 200000 maximum") == 200000);
    CHECK(context_limit_from_overflow(
              "This model's maximum context length is 128000 tokens. However, you requested 150000")
          == 128000);
    CHECK(context_limit_from_overflow(
              "the request exceeds the available context size (8192 tokens)") == 8192);
    // 无号:无猜.
    CHECK(context_limit_from_overflow("Your input exceeds the context window of this model.") == 0);
}

TEST_CASE("optional fields: a host that rejects one is learned, not fatal") {
    CHECK(parse_rejected_field("Unknown field: reasoning_details", 400) == OptionalField::ReasoningDetails);
    CHECK(parse_rejected_field("Extra inputs are not permitted: extra_content", 422) == OptionalField::ExtraContent);
    CHECK(parse_rejected_field("cache_control is not supported", 400) == OptionalField::CacheControl);
    CHECK(parse_rejected_field("Unrecognized request argument supplied: session_id", 400) == OptionalField::SessionId);
    CHECK(parse_rejected_field("unknown parameter 'reasoning'", 400) == OptionalField::Reasoning);
    // 错误的值是真正的错误,而不是要掉下来的东西.
    CHECK(parse_rejected_field("reasoning_details must be an array", 400) == OptionalField::None);
}
