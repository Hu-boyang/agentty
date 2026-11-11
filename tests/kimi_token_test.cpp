// kimi_token_test 纯基米码 OAuth 解析 / 过期逻辑( 无网络)。
//
// 锁 符号 -response 解析器 和摇摆 -safe 重新决定,
// 加载-bearing 轻轻地 -to-get-subtly-wrong Kimi 集成
// 部分(错误过期数学 ) 中 -turn 401s; 缺少 refresh_token
// Kimi 的设备 -flow access_token 是那个 API 直接持
// 卡人(没有代理交换),通过 refresh_token 授权.

#include <string>

#include "agtest.hpp"

#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/provider/kimi/provider.hpp"

using namespace agentty;
using namespace agentty::provider::kimi;

TEST_CASE("kimi token response parse + expiry") {
    constexpr std::int64_t kNow = 1'000'000'000'000;   // 固定的"now"(毫秒)

    // ── 完整的符号捆绑 解析 ; 过期 = 现在 + ( expires_in - 60s) ───────
    {
        auto t = parse_token_response(
            R"({"access_token":"at-123","refresh_token":"rt-456",
                "token_type":"Bearer","scope":"api","expires_in":3600})", kNow);
        CHECK(t.has_value(), "valid response parses");
        CHECK(t->access_token == "at-123", "access_token extracted");
        CHECK(t->refresh_token == "rt-456", "refresh_token extracted");
        CHECK(t->token_type == "Bearer", "token_type extracted");
        CHECK(t->scope == "api", "scope extracted");
        // expires_in(3600)_现在+(3600-60)*1000(swew less).
        CHECK(t->expires_at_ms == kNow + (3600 - 60) * 1000,
              "expiry = now + (expires_in - slack) — refresh before real expiry");
    }

    // 缺少 expires_in 时，将 expiry 保留为 0，表示未知并按不过期处理。
    {
        auto t = parse_token_response(
            R"({"access_token":"x","refresh_token":"y"})", kNow);
        CHECK(t && t->expires_at_ms == 0, "no expires_in → 0 (unknown)");
        CHECK(!t->expired(999999), "unknown expiry (0) is never treated as expired");
    }

    // ── 畸形/ 空 / 无名 / 永远不丢 ──────────────────────────
    CHECK(!parse_token_response("not json", kNow), "garbage → nullopt");
    CHECK(!parse_token_response("{}", kNow), "no access_token field → nullopt");
    CHECK(!parse_token_response(R"({"access_token":""})", kNow),
          "empty access_token → nullopt");

    // ── expired() 正在摇摆 -aware (用真正的墙钟) ──────────────────
    {
        KimiToken past;
        past.access_token = "x";
        past.expires_at_ms = KimiToken::now_ms() - 1000;
        CHECK(past.expired(0), "a past expiry is expired");

        KimiToken future;
        future.access_token = "x";
        future.expires_at_ms = KimiToken::now_ms() + 30 * 60 * 1000;   // 30分钟后
        CHECK(!future.expired(0), "a 30-min-out token is not expired");

        KimiToken soon;
        soon.access_token = "x";
        soon.expires_at_ms = KimiToken::now_ms() + 20 * 1000;   // 20点了
        CHECK(soon.expired(30 * 1000),
              "a token inside the skew buffer is treated as stale (refresh early)");
        CHECK(!soon.expired(0), "…but not stale without the buffer");

        KimiToken never;
        never.access_token = "x";
        never.expires_at_ms = 0;   // 0 = unknown
        CHECK(!never.expired(999999), "unknown expiry (0) is never treated as expired");
    }

    // ── valid() 门在非 -empty 访问符 ───────────────────────────
    {
        KimiToken empty;
        CHECK(!empty.valid(), "empty token is not valid");
        KimiToken ok;
        ok.access_token = "x";
        CHECK(ok.valid(), "non-empty access_token is valid");
    }
}

TEST_CASE("kimi device headers on the API endpoint") {
    // Kimi推论的终点必须带有相同的 X -Msh- * 设备 -identity
    // 头文件 欧奥特流发出——官员 kimi_code_cli 把它们附在聊天+
    // 模型请求中,没有它们,则将/模型 目录 空空回来,一个 SSOT 建构器
    // ( E) device_headers )供养二者.
    auto hdrs = device_headers();
    CHECK(!hdrs.empty());
    auto has = [&](const char* n) {
        for (const auto& h : hdrs) if (h.first == n) return !h.second.empty();
        return false;
    };
    CHECK(has("x-msh-platform"));
    CHECK(has("x-msh-device-id"));
    CHECK(has("x-msh-version"));
    for (const auto& h : hdrs)
        if (h.first == "x-msh-platform") CHECK(h.second == "kimi_code_cli");

    // 端点构建器将它们相接,这样共享的openai 传输层就按
    // 每一次请求发送它们.
    auto ep = KimiProvider::make_endpoint();
    CHECK(ep.host == "api.kimi.com");
    CHECK(ep.path == "/coding/v1/chat/completions");
    CHECK(!ep.extra_headers.empty());
    bool ep_has_platform = false;
    for (const auto& h : ep.extra_headers)
        if (h.first == "x-msh-platform") ep_has_platform = true;
    CHECK(ep_has_platform);
}
