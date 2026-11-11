// agentty::provider::chatgpt —共享 Responses-API 编解码器的
// ChatGPT/Codex **主机**。
//
// 方言机制（对话→输入[]、工具[]、SSE状态机）曾经居住在这里；
// 一旦我们测量到 GitHub 副驾驶说的是完全
// 相同的 传输，它就会被提取到 src/provider/responses/codec.cpp。
// 剩下的正是 TRUE OF CHATGPT 和
// 无内容 ELSE：
//
//   • 端点 chatgpt.com/backend-api/codex/responses
//   • ChatGPT OAuth 信用 (auto-refreshing) + chatgpt-account-id
//   ·从 线程 ID 派生的 提示词-cache 会话 ID
//   • `store:false` + 包括[reasoning.encrypted_content]
//   · Codex-flavoured HTTP 错误散文 ("run `agentty login`…")
//   • 此帐户的实时 /models 目录
//
// 其他一切都是共享的。添加响应后端是一个站点行，
// 类似于此文件底部的行，而不是编解码器的第二个副本。
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/responses/responses.hpp"

#include <maya/runtime.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/chatgpt/oauth.hpp"
#include "agentty/io/http.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::provider::chatgpt {
namespace {
using json = nlohmann::json;

// UTF-8 擦洗 — 粘贴的斑点/工具输出可以携带无效的内容UTF-8那个nlohmann::
// dump() 会抛出。用 U+ 替换格式错误的字节FFFD.
// （本地副本：编解码器为其构建的 正文 保留自己的副本；
// 该副本仅由下面的错误格式化程序使用。）
std::string scrub_utf8(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    const auto* p = reinterpret_cast<const unsigned char*>(in.data());
    const auto* end = p + in.size();
    while (p < end) {
        unsigned char c = *p;
        int extra = 0;
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++p; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else { out.append("\xEF\xBF\xBD"); ++p; continue; }
        if (p + extra >= end) { out.append("\xEF\xBF\xBD"); ++p; continue; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k)
            if ((p[k] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out.append("\xEF\xBF\xBD"); ++p; continue; }
        out.append(reinterpret_cast<const char*>(p), extra + 1);
        p += extra + 1;
    }
    return out;
}

std::string format_http_error(int status, std::string_view body) {
    const std::string generic = "Codex backend returned HTTP " + std::to_string(status);
    std::string message = generic;

    auto decode = [&](const json& j) -> bool {
        const json* detail = &j;
        std::string tag;
        if (j.is_object() && j.contains("error")) detail = &j["error"];
        if (detail->is_string()) {
            message = detail->get<std::string>();
            return !message.empty();
        }
        if (!detail->is_object()) return false;
        tag = detail->value("type", detail->value("code", std::string{}));
        std::string text = detail->value("message", std::string{});
        if (text.empty() && detail->contains("detail") && (*detail)["detail"].is_string())
            text = (*detail)["detail"].get<std::string>();
        if (text.empty() && detail != &j && j.contains("message") && j["message"].is_string())
            text = j["message"].get<std::string>();
        if (text.empty()) return false;
        message = tag.empty() ? std::move(text) : tag + ": " + text;
        return true;
    };

    bool decoded = false;
    try { decoded = decode(json::parse(body)); }
    catch (const std::exception& e) {
        util::dbglog("chatgpt.http_error.decode", e.what());
    } catch (...) {}

    // 一些边缘响应即使出现HTTP错误也保留SSE帧。
    // 解码其 `data: {...}` 载荷，而不是显示原始 帧。
    if (!decoded) {
        std::size_t pos = 0;
        while (pos < body.size()) {
            const auto nl = body.find('\n', pos);
            std::string_view line = body.substr(pos, nl == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : nl - pos);
            const auto end = nl;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.remove_suffix(1);
            constexpr std::string_view kData = "data:";
            if (line.size() > kData.size() && line.substr(0, kData.size()) == kData) {
                std::string_view payload = line.substr(kData.size());
                while (!payload.empty() && payload.front() == ' ')
                    payload.remove_prefix(1);
                try {
                    if (decode(json::parse(payload))) { decoded = true; break; }
                } catch (...) {}
            }
            if (end == std::string_view::npos) break;
            pos = end + 1;
        }
    }

    if (!decoded && !body.empty()) {
        std::string raw = scrub_utf8(body.substr(0, 1024));
        while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n' || raw.back() == ' '))
            raw.pop_back();
        if (!raw.empty()) message = generic + ": " + raw;
    }
    if (status == 401)
        message += " — session expired; run `agentty login` and sign in to ChatGPT again";
    return message;
}

std::string new_uuid_v4() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t a = rng(), b = rng();
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;   // 版本4
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;   // 变体 1
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<unsigned>(a >> 32),
        static_cast<unsigned>((a >> 16) & 0xFFFF),
        static_cast<unsigned>(a & 0xFFFF),
        static_cast<unsigned>(b >> 48),
        static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
    return std::string{buf};
}

// 响应后端的稳定的 per-conversation 会话 ID。
//
// Codex后端密钥提示词在该头文件上缓存，因此携带SAME id的
// SAME对话的连续轮次命中了缓存的前缀（系统指令+工具模式定义
// +先前的轮次），而每轮新的id都会强制执行完整的re-encode
// ——这正是随机uuid-per-request所做的，默默地击败了
// 缓存每次主持 ChatGPT 回合。 codex-rs 为整个会话
// 保留一个 id；我们通过从调用者的稳定 session_key（线
// 程 id）派生确定性 v4 形 uuid 来执行相同的操作。
// 当没有提供 session_key 时（没有持久的对话身份），
// 我们会回退到一个随机 ID——正确性第一，只有当我们能够稳定时才进行缓存。
std::string session_id_for(std::string_view session_key) {
    if (session_key.empty()) return new_uuid_v4();
    // FNV-1a在密钥→128位上，然后格式化为v4
    // uuid以便后端接受它。确定性：相同的对话→每次都相同
    // 的id→缓存连续性。
    auto fnv = [](std::string_view s, std::uint64_t seed) {
        std::uint64_t h = seed;
        for (unsigned char c : s) { h ^= c; h *= 0x00000100000001B3ULL; }
        return h;
    };
    std::uint64_t a = fnv(session_key, 0xcbf29ce484222325ULL);
    std::uint64_t b = fnv(session_key, 0x84222325cbf29ce4ULL);
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;   // 版本4
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;   // 变体 1
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<unsigned>(a >> 32),
        static_cast<unsigned>((a >> 16) & 0xFFFF),
        static_cast<unsigned>(a & 0xFFFF),
        static_cast<unsigned>(b >> 48),
        static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
    return std::string{buf};
}

// ── 电话 ChatGPT 站点 ──────────────────────────────────────────────────────

std::expected<responses::Target, std::string>
chatgpt_authorize(const maya::unlocked& cap, provider::Request& req) {
    auto creds = codex_fresh_credentials(cap);   // auto-refreshes 如果 过期
    if (!creds || creds->access_token.empty())
        return std::unexpected(
            std::string{"not signed in to ChatGPT \xe2\x80\x94 run `agentty login` and "
                        "choose ChatGPT, or use --provider with an API key"});
    // 刷新永久失败（过期/重用/撤销）：发送死令牌只会
    // 以令人困惑的 401 的形式返回。
    if (const auto why = refresh_failure_message(codex_last_refresh_failure());
        !why.empty() && creds->expired())
        return std::unexpected(std::string{"401 "} + std::string{why});

    responses::Target t;
    t.host = "chatgpt.com";
    t.port = 443;
    t.path = "/backend-api/codex/responses";
    // codex-rs以三种方式发送会话id:`session_id`(传统)、
    // `session-id`+`thread-id`和`x-client-request-id`。
    // 后端按键路由和提示词-cache粘性在它们上，所以发送相同的设置。
    const std::string sid = session_id_for(req.session_key);
    t.headers = {
        {"authorization",     "Bearer " + creds->access_token},
        {"content-type",      "application/json"},
        {"accept",            "text/event-stream"},
        {"openai-beta",       "responses=experimental"},
        {"originator",        OAuthConfig::originator},
        {"session_id",        sid},
        {"session-id",        sid},
        {"thread-id",         sid},
        {"x-client-request-id", sid},
        {"user-agent",        std::string("codex_cli_rs/") + OAuthConfig::codex_client_version},
    };
    if (!creds->account_id.empty())
        t.headers.push_back({"chatgpt-account-id", creds->account_id});
    // 当调用者未固定后端时托管的Codex后端的默认段。
    t.model = (req.model.empty() || req.model == "chatgpt-default")
                  ? "gpt-5-codex" : req.model;
    return t;
}

void chatgpt_decorate_body(json& body, const provider::Request& req) {
    // Codex后端运行无状态：我们每轮重放整个对话，
    // 因此服务器-side不应该存储任何内容。
    body["store"] = false;
    // 相同的稳定密钥代码x-rs从会话中导出，
    // 因此一个对话轮流击中相同的提示词缓存。
    if (!req.session_key.empty()) body["prompt_cache_key"] = session_id_for(req.session_key);
    // Ask后端返回每个推理项的encrypted_content这样
    // 我们就可以在下一轮的 input[] 中重播它。下商店：假这是
    // ONLY携带链条的方式-of-thought跨工具轮次 -
    // 没有它推理模型重新-derives每轮都从头开始制定计划（更糟糕的答案，
    // 浪费代币）。镜子法典-rs.
    body["include"] = json::array({"reasoning.encrypted_content"});
}

// 看起来有效的令牌上的 401：立即刷新并重新发送一次。
std::expected<responses::Target, std::string>
chatgpt_reauthorize(const maya::unlocked& cap, provider::Request& req,
                    const responses::Target& rejected) {
    std::string old_token;
    for (const auto& [k, v] : rejected.headers)
        if (k == "authorization" && v.starts_with("Bearer ")) old_token = v.substr(7);
    const auto out = codex_force_refresh(cap, old_token);
    if (const auto why = refresh_failure_message(out.failure); !why.empty())
        return std::unexpected(std::string{"401 "} + std::string{why});
    if (!out.creds || out.creds->access_token == old_token)
        return std::unexpected(std::string{
            "401 ChatGPT rejected the sign-in \xe2\x80\x94 run `agentty login` and sign in again"});
    return chatgpt_authorize(cap, req);
}

const responses::Site kChatGptSite{
    .id                 = "chatgpt",
    .authorize          = &chatgpt_authorize,
    .decorate_body      = &chatgpt_decorate_body,
    .explain_http_error = &format_http_error,
    .reauthorize        = &chatgpt_reauthorize,
};

} // 命名空间结束。

bool responses_available() {
    // 每个呈现的帧由提供者-picker VIEW调用一次。当 选择面板 打开时，
    // 幼稚路径（读取 codex_credentials.json → 解封 →
    // JSON 解析）会花费磁盘 + 每个 帧 的 AES 工作量。 缓存 文件的
    // 布尔值（mtime、大小）：统计数据约为 1μs，并且在 sign-in、
    // sign-out 和 cross-process 刷新时正确无效。
    namespace fs = std::filesystem;
    struct Cache {
        bool           ok = false;
        std::int64_t   mtime = 0;   // file_time_type 刻度：可发送键
        std::uintmax_t size = static_cast<std::uintmax_t>(-1);
    };
    static maya::snapshot<Cache> cache;
    std::error_code ec;
    const auto p = codex_credentials_path();
    const std::int64_t mtime = fs::last_write_time(p, ec).time_since_epoch().count();
    const auto size  = ec ? 0 : fs::file_size(p, ec);
    if (ec) {   // missing/unreadable→已注销；记得便宜
        cache.store(Cache{});
        return false;
    }
    if (auto hit = cache.load(); hit->mtime == mtime && hit->size == size)
        return hit->ok;
    // 修改：解封+解析，然后发布。
    auto c = load_codex_credentials();
    const bool ok = c && !c->access_token.empty();
    cache.store(Cache{ok, mtime, size});
    return ok;
}

// ── 实况模式目录 ────────────────────────────────────────────────────
std::vector<CatalogModel> fetch_models(Io io, const maya::unlocked& cap) {
    auto creds = codex_fresh_credentials(cap);
    if (!creds || creds->access_token.empty()) return {};

    http::Request hr;
    hr.method = http::HttpMethod::Get;
    hr.host   = "chatgpt.com";
    hr.port   = 443;
    hr.path   = std::string("/backend-api/codex/models?client_version=")
              + OAuthConfig::codex_client_version;
    hr.headers = {
        {"authorization", "Bearer " + creds->access_token},
        {"accept",        "application/json"},
        {"openai-beta",   "responses=experimental"},
        {"originator",    OAuthConfig::originator},
        {"user-agent",    std::string("codex_cli_rs/") + OAuthConfig::codex_client_version},
    };
    if (!creds->account_id.empty())
        hr.headers.push_back({"chatgpt-account-id", creds->account_id});

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(8'000);
    tos.total   = std::chrono::milliseconds(15'000);
    // 目录是一个小的JSON列表（〜一些KB)。严格限制，以便行为不当的代理/重放循环无法在例行
    // 选择面板 探测上将 数据流 进入 OOM（与 Anthropic 的
    // list_models 奇偶校验）。
    hr.max_body_bytes = 1ull * 1024 * 1024;

    auto result = http::default_client(io).send(hr, tos);
    if (!result || result->status < 200 || result->status >= 300) return {};

    std::vector<CatalogModel> out;
    try {
        auto j = nlohmann::json::parse(result->body);
        const auto& arr = j.contains("models") ? j["models"] : j;
        if (!arr.is_array()) return {};
        for (const auto& m : arr) {
            // 目录携带选择面板不得显示的内部模型(e.g.
            // `codex-auto-review`，可见性＝"hide")。镜像代码x-rs：
            // 仅帐户打算选择的表面模型。
            // 任何未明确列为可见的内容都会被删除。
            const std::string vis = m.value("visibility", std::string{"list"});
            if (vis == "hide" || vis == "hidden") continue;
            CatalogModel cm;
            cm.slug = m.value("slug", m.value("id", std::string{}));
            if (cm.slug.empty()) continue;
            cm.display_name   = m.value("display_name", cm.slug);
            cm.context_window = m.value("context_window", 272000);
            cm.is_default     = m.value("is_default", false);
            out.push_back(std::move(cm));
        }
    } catch (...) {
        return {};
    }
    return out;
}

provider::StreamResult stream_responses(const maya::unlocked& cap,
                                        provider::Request req,
                                        provider::EventSink sink) {
    return responses::stream(cap, kChatGptSite, std::move(req), std::move(sink));
}

// ── 测试接口边界 ────────────────────────────────────────────────────────────
// 这些保持其历史ChatGPT-flavoured行为：build_body_for_test
// 返回正文 AS SENT(中性编解码器正文+该主机的装饰)，
// 因此关于store/include的现有期望仍然成立。
nlohmann::json build_body_for_test(const provider::Request& req) {
    provider::Request r = req;
    // 建立 FOR 这个网站，就像 stream() 所做的那样。
    // 接口边界 声称返回发送时的 正文，因此它必须传递相同的站
    // 点 ID — 否则加密的 -reasoning 重播将在此处关闭，
    // 而不会在其他地方关闭，并且测试将断言 正文 不会
    // 产生任何请求。
    json body = responses::build_body(r, kChatGptSite.id);
    if (r.model.empty() || r.model == "chatgpt-default")
        body["model"] = "gpt-5-codex";
    chatgpt_decorate_body(body, r);
    return body;
}

std::string session_id_for_test(std::string_view session_key) {
    return session_id_for(session_key);
}

std::string format_http_error_for_test(int status, std::string_view body) {
    return format_http_error(status, body);
}

std::vector<Msg> parse_sse_for_test(const std::vector<std::string>& sse_data_lines,
                                    bool show_reasoning) {
    return responses::parse_sse_for_test(sse_data_lines, show_reasoning);
}

} // namespace agentty::provider::chatgpt
