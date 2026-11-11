// copilot_token_test — 纯副驾驶认证逻辑( 无网络).
//
// 锁 符号 -envelope 解析器 和摇摆 -safe 更新决定,
// 即 加载-bearing 轻轻地 -to-get-subtly-wrong 副驾驶一体化
// (每3次) -party 工具在此损坏: 错误的过期数学 ^ 中 -turn 401s,
// 或忽略 endpoints.api ——404开 Business/Enterprise
// 也给副驾驶针头 -fronted 模型标识决定了理智的能力。

#include <string>

#include "agtest.hpp"

#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/copilot/provider.hpp"
#include "agentty/domain/catalog.hpp"

using namespace agentty;
using namespace agentty::provider::copilot;

TEST_CASE("copilot token exchange + expiry") {
    constexpr std::int64_t kNow = 1'000'000'000'000;   // 固定的"now"(毫秒)

    // ── refresh_in 驱动一个 skew -safe LOCAL 过期( 现 +) refresh_in + 60s) ──
    {
        auto t = parse_token_envelope(
            R"({"token":"tid=abc:xyz","refresh_in":1500,"expires_at":9999999999,
                "endpoints":{"api":"https://api.githubcopilot.com"},"sku":"copilot_pro",
                "chat_enabled":true})", kNow);
        CHECK(t.has_value(), "valid envelope parses");
        CHECK(t->token == "tid=abc:xyz", "token extracted");
        // refresh_in 1500首选胜于 expires_at + 现在+ 1500+60* 1000.
        CHECK(t->expires_at_ms == kNow + (1500 + 60) * 1000,
              "expiry = now + (refresh_in + slack) — skew-safe, not absolute");
        CHECK(t->endpoint_api == "https://api.githubcopilot.com", "endpoints.api used");
        CHECK(t->sku == "copilot_pro", "sku carried");
        CHECK(t->chat_enabled, "chat_enabled carried");
        CHECK(!t->quota_exhausted, "no quota block → not exhausted");
    }

    // ── endpoints.api 谓 AUTHORITATIVE (Business/Enterprise 路由此处) ──────
    {
        auto t = parse_token_envelope(
            R"({"token":"x","refresh_in":1000,
                "endpoints":{"api":"https://api.business.githubcopilot.com"}})", kNow);
        CHECK(t && t->endpoint_api == "https://api.business.githubcopilot.com",
              "business endpoint routed from endpoints.api, not hardcoded");
    }

    // ── 缺少 endpoints.api 返回默认主机 ────────────────
    {
        auto t = parse_token_envelope(R"({"token":"x","refresh_in":1000})", kNow);
        CHECK(t && t->endpoint_api == "https://api.githubcopilot.com",
              "missing endpoints.api → default host");
    }

    // ── 无 refresh_in ~ 倒向绝对值 ~ expires_at ────────────────────
    {
        auto t = parse_token_envelope(
            R"({"token":"x","expires_at":2000000000})", kNow);
        CHECK(t && t->expires_at_ms == 2000000000LL * 1000,
              "no refresh_in → expires_at (seconds→ms)");
    }

    // ── 既无字段 → 保守的 25-min 默认值 ─────────────────────────
    {
        auto t = parse_token_envelope(R"({"token":"x"})", kNow);
        CHECK(t && t->expires_at_ms == kNow + 25 * 60 * 1000,
              "no expiry fields → conservative 25-min default");
    }

    // ── 免费 -tier 配额+聊天权限 标志 ────────────────────────────
    {
        auto t = parse_token_envelope(
            R"({"token":"x","refresh_in":1000,"sku":"free_limited_copilot",
                "chat_enabled":true,"limited_user_quotas":{"chat":0}})", kNow);
        CHECK(t && t->quota_exhausted, "chat quota 0 → exhausted");
        auto t2 = parse_token_envelope(
            R"({"token":"x","refresh_in":1000,"chat_enabled":false})", kNow);
        CHECK(t2 && !t2->chat_enabled, "chat_enabled:false surfaced");
    }

    // ── 畸形/ 空 / 无名 / 永远不丢 ──────────────────────────
    CHECK(!parse_token_envelope("not json", kNow), "garbage → nullopt");
    CHECK(!parse_token_envelope("{}", kNow), "no token field → nullopt");
    CHECK(!parse_token_envelope(R"({"token":""})", kNow), "empty token → nullopt");

    // ── expired() 正在摇摆 -aware (用真正的墙钟) ──────────────────
    {
        CopilotToken past;
        past.token = "x";
        past.expires_at_ms = CopilotToken::now_ms() - 1000;
        CHECK(past.expired(0), "a past expiry is expired");

        CopilotToken future;
        future.token = "x";
        future.expires_at_ms = CopilotToken::now_ms() + 30 * 60 * 1000;   // 30分钟后
        CHECK(!future.expired(0), "a 30-min-out token is not expired");

        CopilotToken soon;
        soon.token = "x";
        soon.expires_at_ms = CopilotToken::now_ms() + 60 * 1000;   // 1分钟后
        CHECK(soon.expired(5 * 60 * 1000),
              "a token inside the skew buffer is treated as stale (refresh early)");
        CHECK(!soon.expired(0), "…but not stale without the buffer");

        CopilotToken never;
        never.token = "x";
        never.expires_at_ms = 0;   // 0 = unknown
        CHECK(!never.expired(999999), "unknown expiry (0) is never treated as expired");
    }

    // ── 副驾驶-fronted 模型 ids 决心实现正常能力 ──────────────
    // 副驾驶返回标准三 -party 身份证; from_id 认得他们。
    {
        auto gpt = ModelCapabilities::from_id("gpt-4o");
        auto claude = ModelCapabilities::from_id("claude-sonnet-4");
        (void)gpt; (void)claude;   // from_id 绝不能在这些标识上崩溃
        CHECK(true, "from_id handles Copilot model ids without crashing");
    }
}

// ── 混合-dialect 路线 ────────────────────────────────────────────────
//
// 副驾驶是 agentty '是提供商的唯一地点 '' 相同 账户为一些模式
// 服务 / chat/completions 答复的其他方面的答复 API。
// 这些期望是: MEASURED 而不是假设——对
// api.individual.githubcopilot.com 返回自动会话 :
//
//   长5 -mini /答复 200+真实推理摘要文本( SSE response.reasoning_summary_text.delta);
//                       /chat/completions 200块 NO 推理文本
//                       mai -code-1.1-flash /答复200;
//   / chat/completions 400 unsupported_api_for_model
//                       克劳德 -haiku-4.5
//   /chat/completions 200;/答复400 gpt-
//   4.1 / 请求 chat/completions 200;/答复400
//
// 搞错了是沉默的:转弯仍然"works",
// 用户只是从来没有看到他们承诺过的想法(或者整个模型
// 家族保持隐形),这正是这个路由所建的bug用来修复的.
TEST_CASE("copilot dialect routing follows the measured wire behaviour") {
    // gpt-5* – 答复。 这是 Copilot 返回推理文本的
    // 仅 方言, 所以更喜欢的是它使 ^R 对 GPT-5 诚实。
    CHECK(prefers_responses_dialect("gpt-5-mini"),
          "gpt-5-mini prefers Responses (only dialect carrying reasoning text)");
    CHECK(prefers_responses_dialect("gpt-5"),
          "the whole gpt-5 family prefers Responses");
    // ...但是Gpt-5是,我们... 不 聊天 -incapable :
    // 聊天是有效的,只是不能显示思维。区别很重要——聊天 回退 必须要保持合法。
    CHECK(!chat_dialect_unsupported("gpt-5-mini"),
          "gpt-5-mini still speaks chat (fallback must remain available)");

    // 玛伊 -code- * 是回应 -ONLY : 400s
    // 在聊天路径上。 在回复之前 传输层 已过滤出 选择
    // 面板 完全可以,现在可以到达。
    CHECK(chat_dialect_unsupported("mai-code-1.1-flash"),
          "mai-code-* rejects chat/completions");
    CHECK(prefers_responses_dialect("mai-code-1.1-flash"),
          "a chat-incapable model must route to Responses, not be skipped");

    // 聊天 -only 家庭不得 被拖到回复上,他们有400个。
    // 这是一种回归,它会打破每一个 Claude 打开副驾驶机
    CHECK(!prefers_responses_dialect("claude-haiku-4.5"),
          "claude stays on chat/completions (400s on /responses)");
    CHECK(!prefers_responses_dialect("claude-sonnet-4.6"),
          "no claude model is routed to Responses");
    CHECK(!prefers_responses_dialect("gpt-4.1"),
          "gpt-4.x stays on chat/completions");
    CHECK(!prefers_responses_dialect("gpt-4o"),
          "gpt-4o stays on chat/completions");
    CHECK(!chat_dialect_unsupported("claude-haiku-4.5"),
          "claude is chat-capable");
}
