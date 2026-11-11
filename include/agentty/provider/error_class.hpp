#pragma once
// agentty::provider::error_class-将数据流-level故障分类为{瞬态、
// RateLimit、Auth、已取消、终端}之一。 状态更新函数 读取结果以在
// auto-retry、re-auth 和 surface-to-the-user
// 之间做出决定。
//
// 两个入口点，按来源：
//
//   在 `kind`/`http_status` 上分类（HttpError)-带类型的 分派。
//                                当源是 HTTP-layer 故障（无
//                                SSE 事件）时使用此。零字符串检查，
//                                对 枚举 进行详尽的检查。
//
//   classify(std::string_view)-子串嗅探。用于 SSE
//                                `event: error` 载荷，其中 传输 仅提供
//                                Anthropic 的 `message` 文本（e.g、
//                                "Overloaded"、"rate_limit_error"）。
//                                HTTP路径为带类型的路径；
//                                传输-only 错误形状
//                                存在此字符串路径。
//
// 添加新的故障种类是分类中的一个新的枚举条目加上一个开关臂(HttpError)；
// 仅当 Anthropic 发送新的 error_type 时，
// 字符串 -sniff 列表才会增长。

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "agentty/io/http.hpp"

namespace agentty::provider {

enum class ErrorClass {
    // 瞬态 — 可使用 退避 重试。服务器已启动，
    // 但暂时不高兴（加载 棚，队列已满，5xx）。
    // 同样的请求可能会在几秒钟内成功。
    Transient,
    // Rate-limited —可用更长的退避重试。
    // 常带有重试-After提示上游；我们在这里使用统一的时间表。
    RateLimit,
    // 身份验证-OAuth令牌在-session年中过期或被撤销。
    // 调用者应刷新并重试一次；如果刷新失败，则显示为 终端，
    // 以便用户可以查看 `agentty login`。
    Auth,
    // 已取消——用户按下Esc；从来没有重试。最终的。
    Cancelled,
    // 终端 — 无效请求、未找到型号、计费等。
    // Re-sending将以同样的方式失败。浮出水面并停止。
    Terminal,
};

// 带类型的 分派 —首选路径。直接读取HttpError::kind，
// 不进行字符串检查。每当故障源自 HTTP 层（Client::send /
// Client::stream 返回 `unexpected`）时，请使用此选项。
[[nodiscard]] constexpr ErrorClass classify(const agentty::http::HttpError& e) noexcept {
    using K = agentty::http::HttpErrorKind;
    switch (e.kind) {
        case K::Cancelled:    return ErrorClass::Cancelled;
        case K::Resolve:
        case K::Connect:
        case K::Tls:
        case K::Protocol:
        case K::SocketHangup:
        case K::Timeout:
        case K::PeerClosed:
            return ErrorClass::Transient;
        case K::Status:
            // 将HTTP-status语义映射到ErrorClass。 401/403 是身份验证；
            // 429为速率限制； 408/5xx (502/503/504/529) 是暂时的；
            // 其他所有内容 (4xx) 都是 终端。
            if (e.http_status == 401 || e.http_status == 403)
                return ErrorClass::Auth;
            if (e.http_status == 429) return ErrorClass::RateLimit;
            // 408和每5xx(包括500)是服务器的问题，
            // 而不是请求的问题：相同的请求可以在重试上成功。
            // 501（未实现）是永远不会实现的 5xx。
            if (e.http_status == 408
             || (e.http_status >= 500 && e.http_status <= 599 && e.http_status != 501))
                return ErrorClass::Transient;
            return ErrorClass::Terminal;
        case K::Body:
        case K::Unknown:
            return ErrorClass::Terminal;
    }
    return ErrorClass::Terminal;
}

// 字符串-sniff 回退。用于 SSE `event: error` 载荷，
// 其中 传输 只为我们提供 Anthropic 的 `error.message`
// 文本 - 在该边界上没有 HttpError 到 分派。
// 还涵盖了由于历史原因 pre-typed 错误流经
// `StreamError{string}` 的遗留路径。使该列表与
// Anthropic 的错误分类保持一致；我们想要专门显示的每个形状一个条目。
[[nodiscard]] inline ErrorClass classify(std::string_view msg) noexcept {
    auto contains = [&](std::string_view needle) noexcept -> bool {
        if (needle.size() > msg.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= msg.size(); ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = msg[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    };

    // 取消作为来自工作线程的专用字符串出现。
    if (contains("cancel")) return ErrorClass::Cancelled;

    // Auth表面为来自传输层的HTTP 401/403或来自Anthropic的正文的显式
    // "authentication_error"类型。
    if (contains("401")
     || contains("403")
     || contains("authentication_error")
     || contains("invalid api key")
     || contains("not authenticated"))
        return ErrorClass::Auth;

    // 信用不足。在速率限制之前检查：OpenAI 将
    // insufficient_quota 作为 429 发送，而 重试 计费墙
    // 只会延迟通知用户。
    if (contains("insufficient_quota")
     || contains("billing_error")
     || contains("credit balance")
     || contains("payment_required")
     || contains("credit_balance_exhausted"))
        return ErrorClass::Terminal;

    // 速率限制 — Anthropic 的 "rate_limit_error" 或 HTTP 429。
    if (contains("rate_limit") || contains("429") || contains("slow_down"))
        return ErrorClass::RateLimit;

    // 过载/服务器/网络——瞬态。
    if (contains("overloaded")
     || contains("overload_error")
     || contains("server_is_overloaded")
     || contains("api_error")             // Anthropic的内部500
     || contains("server_error")          // OpenAI的内部500
     || contains("internal_server_error")
     || contains("timeout_error")
     || contains("HTTP 500")
     || contains("502")
     || contains("503")
     || contains("504")
     || contains("529")              // Anthropic的"overloaded" HTTP代码
     || contains("connection")       // "connection refused/reset"
     || contains("timeout")
     || contains("eof")
     || contains("broken pipe")
     || contains("network")
     || contains("stall"))           // 从运行时档位看门狗合成
        return ErrorClass::Transient;

    return ErrorClass::Terminal;
}

// 分派器、重试、状态更新函数、调用在StreamError、Msg上。当 传输层 在
// Msg (StreamError::http_status != 0) 上标记真实
// 的 HTTP 状态时，首选 TYPED、编译-time-proven 分类 (
// HttpError) 路径，并且仅回退到真正没有状态的 传输 形状的子字符串嗅探：SSE
// `event: error` 正文， transport/socket 失败、
// 用户取消和合成 Stall-watchdog 错误（全部 http_status == 0）。
//
// 这就是 传输层 的 KNOWN 状态在没有有损 int→English→substring
// round-trip 的情况下如何达到分类的方式：传输层 已经具有确切的
// 429/401/503，因此将 "429" 表述为 "Too Many Requests"
// (正文 中没有数字)的代理仍然分类为 RateLimit，以及 终端消息中恰好
// 包含 "connection" 一词的 400 不再将 -retried
// 误认为是瞬态。当状态 == 0 时，旧分类（消息）调用 的行为为 字
// 节-identical。
// 该错误是否表明帐户信用不足？与提供商的稳定错误代码相匹配，
// 因此无论状态如何，重试 都无法修复它。
[[nodiscard]] inline bool is_billing_error(std::string_view msg) noexcept {
    for (std::string_view k : {"insufficient_quota", "billing_error",
                               "payment_required", "credit_balance_exhausted",
                               "credit balance is too low"})
        if (msg.find(k) != std::string_view::npos) return true;
    return false;
}

[[nodiscard]] inline ErrorClass classify_stream_error(
        std::string_view message, int http_status) noexcept {
    // 信用不足永远不值得 重试，无论其状态如何：
    // OpenAI将insufficient_quota作为429发送，
    // 仅该状态就会在告诉用户之前将重试发送六次。
    if (is_billing_error(message)) return ErrorClass::Terminal;
    if (http_status != 0) {
        return classify(agentty::http::HttpError{
            agentty::http::HttpErrorKind::Status, http_status, std::string{}});
    }
    return classify(message);
}

// 这个错误是否意味着
// "this account's subscription is not entitled to the context-1m long-context beta"？（当前账号订阅无权使用 context-1m 长上下文测试功能。）
// 当 `context-1m-2025-08-07` beta 头文件 运行在无权帐户上时，
// Anthropic 400 会通过一条独特的消息来处理整个请求。这是 MODEL
// -CAPABILITY 发现，而不是真正的失败：没有 beta 的相同请求（i.e。
// 在普通的 200K 窗口上）成功。 状态更新函数 使用此功能立即剥离 `[1m]` 选择
// 面板 标记和 重试，而不是在每回合都显示 dead-end 终端 错误。在消息的稳定
// 片段 ("long context beta is not") 上匹配 case
// -insensitively 加上状态，因此次要 上游 改写 (
// "...for this subscription" → "...for this organization")（订阅级错误文字变化为组织级错误文字。）
// 保持匹配。
[[nodiscard]] inline bool is_long_context_rejection(
        std::string_view message, int http_status) noexcept {
    if (http_status != 0 && http_status != 400) return false;
    auto lower_contains = [&](std::string_view needle) noexcept -> bool {
        if (needle.size() > message.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= message.size(); ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = message[i + j], b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    };
    return lower_contains("long context beta is not")
        || lower_contains("context-1m");   // 未来错误-shape漂移
}

// 该请求是否不适合模型的上下文窗口？ Anthropic 表示 "prompt is too long"、
// OpenAI "context_length_exceeded" / "maximum context length"、
// llama.cpp 报告超出可用上下文大小。
[[nodiscard]] inline bool is_context_overflow(std::string_view message) noexcept {
    auto lower_contains = [&](std::string_view needle) noexcept -> bool {
        if (needle.size() > message.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= message.size(); ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = message[i + j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (a != needle[j]) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    };
    return lower_contains("prompt is too long")
        || lower_contains("context_length_exceeded")
        || lower_contains("maximum context length")
        || lower_contains("exceeds the context window")
        || lower_contains("exceeds the available context size")
        || lower_contains("exceed_context_size")
        || lower_contains("request_too_large");
}

// 当溢出消息指出时，模型的真实上下文限制。
// Anthropic 报告提示过长：210000 token 超过 200000 上限。
// OpenAI 报告最大上下文长度为 128000 token。
// llama.cpp 报告超出可用的 8192 token 上下文。
// 当没有号码可信任时为0。
[[nodiscard]] inline long context_limit_from_overflow(std::string_view m) noexcept {
    auto number_before = [&](std::size_t end) -> long {
        std::size_t b = end;
        while (b > 0 && m[b - 1] >= '0' && m[b - 1] <= '9') --b;
        long v = 0;
        for (std::size_t i = b; i < end && i - b < 10; ++i) v = v * 10 + (m[i] - '0');
        return v;
    };
    auto number_after = [&](std::size_t at) -> long {
        while (at < m.size() && m[at] == ' ') ++at;
        long v = 0;
        for (std::size_t n = 0; at < m.size() && m[at] >= '0' && m[at] <= '9' && n < 10; ++at, ++n)
            v = v * 10 + (m[at] - '0');
        return v;
    };
    if (auto p = m.find(" maximum"); p != std::string_view::npos)
        if (long v = number_before(p > 7 && m.substr(p - 7, 7) == " tokens" ? p - 7 : p); v > 0)
            return v;
    if (auto p = m.find("maximum context length is"); p != std::string_view::npos)
        if (long v = number_after(p + 25); v > 0) return v;
    if (auto p = m.find("context size ("); p != std::string_view::npos)
        if (long v = number_after(p + 14); v > 0) return v;
    return 0;
}

// ── 视障排斥 —— 学习能力/应享权利 ────────────────
//
// 携带图像的请求可能因FOUR不同的原因而被拒绝，并且他们想要三种不同的反应。
// Copilot 的 API 将每个字符串返回为自己的句子 -
// 这些字符串是从 GitHub CLI 自己的 二进制程序 (
// `runtime.node`) 中提取的，因此它们是服务器实际所说的内容，
// 而不是我们想象的内容：
//
//   "not supported for vision"型号看不到
//   "image media type not supported"，这个FORMAT已经出来了，
//   "exceeded maximum number of images"太MANY，
//   不是太大"vision is not enabled for this organization"政策，不是能力
//
// 最后一个是有趣的，这就是为什么它返回 KIND 而不是 bool 的原因。
// "The model cannot see" 和 "your org forbids it"
// 都是 400 提到的愿景，但是：
//
//   ·模型能力是MODEL的属性——切换账户
//     没有任何改变，解决方法是选择不同的模型
//   • 组织政策是 ACCOUNT 的财产 — 相同的模型有效
//     在另一个登录上很好，并将其记录为模型功能会毒害每个帐户的模型（
//     正是为了防止错误domain::entitlement
//     的密钥存储而构建的，请参阅帐户-blind
//     `context_1m_blocked` bool上的头文件注释）
//
// 折叠它们失去了决定告诉用户什么和记住
// 什么的区别。
enum class VisionRejection : std::uint8_t {
    None,           // 根本不是视力排斥
    ModelCapability,// 此型号不能接受图片（任何帐户）
    MediaType,      // 模型看到图像，只是不是THIS格式
    TooManyImages,  // 超过每-request图像计数限制
    OrgPolicy,      // 该账户的组织残障视力
};

[[nodiscard]] constexpr std::string_view tag(VisionRejection v) noexcept {
    switch (v) {
        case VisionRejection::None:            return "none";
        case VisionRejection::ModelCapability: return "model_capability";
        case VisionRejection::MediaType:       return "media_type";
        case VisionRejection::TooManyImages:   return "too_many_images";
        case VisionRejection::OrgPolicy:       return "org_policy";
    }
    return "?";
}

[[nodiscard]] inline VisionRejection classify_vision_rejection(
        std::string_view message, int http_status) noexcept {
    // 与long-context检测器相同的状态门：
    // 0表示状态不可用(200内的SSE错误正文)，
    // 这正是消息嗅探是唯一证据的时候。
    if (http_status != 0 && http_status != 400 && http_status != 422)
        return VisionRejection::None;

    auto lower_contains = [&](std::string_view needle) noexcept -> bool {
        if (needle.size() > message.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= message.size(); ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = message[i + j], b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    };

    // ORDER MATTERS。
    // "vision is not enabled for this organization"（当前组织未启用图像理解。）
    // 还包含 "vision"，因此必须首先运行最窄的、most-specific 测试，
    // 否则 org-policy 块将被记录为永久模型功能 - 并且该模型将在每个帐户（包括
    // 有权的帐户）上保留 image-less。
    if (lower_contains("for this organization")
     || lower_contains("for this organisation")   // en-GB漂移
     || lower_contains("organization policy"))
        return VisionRejection::OrgPolicy;

    if (lower_contains("maximum number of images")
     || lower_contains("too many images"))
        return VisionRejection::TooManyImages;

    if (lower_contains("media type not supported")
     || lower_contains("could not process image")
     || lower_contains("unsupported image"))
        return VisionRejection::MediaType;

    if (lower_contains("not supported for vision")
     || lower_contains("does not support image")
     || lower_contains("image input is not supported")
     || lower_contains("vision is not supported"))
        return VisionRejection::ModelCapability;

    return VisionRejection::None;
}

// ── 可选的-field 拒绝 − 学习能力 ────────────────────────
// OpenAI-compat家族不是一个API：每个主机根据其OWN
// 模式定义来验证正文，并且对它不知道的任何字段进行严格的400秒——
// 杀死一个OPTIMISATION（#93）：
//   Groq:"property 'prompt_cache_key' is unsupported"
//   Gemini 报告 JSON 载荷无效，不认识 prompt_cache_key 字段。
//            找不到字段。
//   提供了" others: "无法识别的请求参数：
//           stream_options", "不允许额外输入”（
// pydantic：loc对其进行命名），…注册表选择主机进入它知道它们采用的字段，
// 因此built-in行永远不会遇到此问题。它是其他所有内容的安全网：
// 定制主机、严格后端前面的网关、下个月收紧其模式定义的提供商。
// 返回服务器命名的 OPTIONAL 字段 — 只有 agentty
// 字段可以丢弃并仍然发送正确的请求；拒绝 `messages`/
// `model`/`tools` 是一个真正的错误，永远不会消失
// "learned"。 状态更新函数 记录了该主机的记录，
// 重试 记录了 相同 的记录，没有字段。
enum class OptionalField : std::uint16_t {
    None = 0,
    PromptCacheKey   = 1u << 0,
    StreamOptions    = 1u << 1,
    // 重播推理/签名和缓存提示。每一种都会使模型变得更好
    // 或更便宜；没有一个值得对拒绝它的主机进行彻底的攻击。
    ReasoningDetails = 1u << 2,
    ExtraContent     = 1u << 3,
    CacheControl     = 1u << 4,
    Reasoning        = 1u << 5,   // OpenRouter的{推理：{努力}}
    SessionId        = 1u << 6,
    // "reasoning off" 如何发送到默认认为的主机。
    // 仅在用户摘取时发送；拒绝它的主机只能学习一次。
    ChatTemplateKwargs = 1u << 7,
    // reasoning_effort:"none" 已将 "off" 发送至自定义主机。
    // 大多数服务器都接受它（vLLM，当前llama.cpp）；旧的价值 400 多美元。
    EffortNone       = 1u << 8,
    // 不是删除而是重命名：OpenAI在聊天400 `max_tokens`上的
    // 推理模型并且想要`max_completion_tokens`。设置=使用该名称。
    MaxTokens        = 1u << 9,
};
[[nodiscard]] constexpr std::uint16_t bit(OptionalField f) noexcept {
    return static_cast<std::uint16_t>(f);
}
[[nodiscard]] constexpr std::string_view wire_name(OptionalField f) noexcept {
    switch (f) {
        case OptionalField::PromptCacheKey:   return "prompt_cache_key";
        case OptionalField::StreamOptions:    return "stream_options";
        case OptionalField::ReasoningDetails: return "reasoning_details";
        case OptionalField::ExtraContent:     return "extra_content";
        case OptionalField::CacheControl:     return "cache_control";
        case OptionalField::Reasoning:        return "reasoning";
        case OptionalField::SessionId:        return "session_id";
        case OptionalField::ChatTemplateKwargs: return "chat_template_kwargs";
        case OptionalField::EffortNone:       return "reasoning_effort";
        case OptionalField::MaxTokens:        return "max_tokens";
        case OptionalField::None:             break;
    }
    return {};
}
[[nodiscard]] inline OptionalField parse_rejected_field(
        std::string_view message, int http_status) noexcept {
    if (http_status != 0 && http_status != 400 && http_status != 422)
        return OptionalField::None;
    auto lower = [](char c) noexcept {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    auto icontains = [&](std::string_view needle) noexcept {
        if (needle.size() > message.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= message.size(); ++i) {
            std::size_t k = 0;
            while (k < needle.size() && lower(message[i + k]) == needle[k]) ++k;
            if (k == needle.size()) return true;
        }
        return false;
    };
    // 消息必须SAY，字段是unknown/unsupported——在一些不
    // 相关的错误（"prompt_cache_key must be a string")
    // 中命名字段是一个值错误，我们不能通过删除它来掩盖它。
    constexpr std::string_view kRejectionWords[] = {
        "unsupported", "not supported", "unknown name", "unknown field",
        "unknown parameter", "unrecognized", "unrecognised",
        "cannot find field", "not permitted", "not allowed",
        "extra inputs", "additional properties", "invalid field",
    };
    bool says_unknown = false;
    for (auto w : kRejectionWords) if (icontains(w)) { says_unknown = true; break; }
    if (!says_unknown) return OptionalField::None;
    // 最长的名称在前："reasoning"是"reasoning_details"的前缀。
    // reasoning_effort拥有自己的手臂（努力-set学习，
    // 以及定制-host "none"手臂）；命名它的消息与 `reasoning` 无关。
    if (icontains("reasoning_effort") || icontains("reasoning.effort"))
        return OptionalField::None;
    // "'max_tokens' is not supported with this model. Use 'max_completion_tokens' instead."（此模型不支持 max_tokens，应改用 max_completion_tokens。）
    // 需要提示：大量的400将max_tokens命名为
    // too-large 值，并且这些名字一定不能翻转。
    if (icontains("max_tokens") && icontains("max_completion_tokens"))
        return OptionalField::MaxTokens;
    if (icontains("max_tokens")) return OptionalField::None;
    for (auto f : {OptionalField::PromptCacheKey, OptionalField::StreamOptions,
                   OptionalField::ReasoningDetails, OptionalField::ExtraContent,
                   OptionalField::CacheControl, OptionalField::SessionId,
                   OptionalField::ChatTemplateKwargs,
                   OptionalField::Reasoning})
        if (icontains(wire_name(f))) return f;
    return OptionalField::None;
}

// ── 必要推理回响 + 学习型合同(# 117) ────────────────────
// OptionalField的镜像：服务器想要ADDED的字段，
// 不丢弃。 Thinking-mode 模型希望发回每个
// 早期助理回合的推理，并且他们 400 缺乏它的历史：
//   光束（反射；Vercel / Pollinations / OpenCode Zen 网关）：
//     "Missing required parameter: 'messages[3].reasoning_content'. This model needs the reasoning of every earlier assistant message: …"（缺少必需参数 messages[3].reasoning_content；模型要求回传此前所有助手消息的推理内容。）
//      基米/登月：
//
//     "thinking is enabled but reasoning_content is missing in assistant tool call message at index 2"（已启用思考，但索引 2 的助手工具调用消息缺少 reasoning_content。）
//      DeepSeek:
//
//     "The `reasoning_content` in the thinking mode must be passed back to the API."（思考模式下必须向 API 回传 reasoning_content。）
//      将此
// 模型流式传输已经发送回其推理（消息::
// reasoning_wire_field)。没有 echo 可以修复的是没有任何可发送的转弯：
// 它来自另一个模型，或者这个模型没有推理那个转弯。
// 合同规定哪些回合必须携带场地，传输层 填充它们（
// openai::build_messages）。
//
// 范围是 正文 不同意的 一个 事物，并且是 加载-bearing：
// DeepSeek和Kimi仅检查工具-call轮次，Beam每轮检查一次。
enum class ReasoningEcho : std::uint8_t {
    None      = 0,   // 没有已知的要求
    ToolCalls = 1,   // 助手轮流制作工具调用
    All       = 2,   // 每位助理轮流
};
[[nodiscard]] inline ReasoningEcho parse_reasoning_echo_required(
        std::string_view message, int http_status) noexcept {
    if (http_status != 0 && http_status != 400 && http_status != 422)
        return ReasoningEcho::None;
    auto lower = [](char c) noexcept {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    auto icontains = [&](std::string_view needle) noexcept {
        if (needle.size() > message.size()) return false;
        for (std::size_t i = 0; i + needle.size() <= message.size(); ++i) {
            std::size_t k = 0;
            while (k < needle.size() && lower(message[i + k]) == needle[k]) ++k;
            if (k == needle.size()) return true;
        }
        return false;
    };
    // 必须将字段命名为AND表示它不存在。
    // "reasoning_content must be a string"是数值错误，不是漏转；
    // 填充轮流无法修复该问题，因此不得尝试。
    if (!icontains("reasoning_content")) return ReasoningEcho::None;
    const bool missing = icontains("missing") || icontains("is required")
                      || icontains("must be passed back")
                      || icontains("needs the reasoning");
    if (!missing) return ReasoningEcho::None;
    // Beam 直言不讳地说，这是每个回合。 Kimi命名一个工具调用消息；
    // DeepSeek 的句子没有范围，其文档说 tool-call 转动。
    // 无范围：开始范围狭窄；接下来的 400（如果有）将其加宽。
    if (icontains("every earlier assistant") || icontains("all assistant")
        || icontains("every assistant"))
        return ReasoningEcho::All;
    return ReasoningEcho::ToolCalls;
}

// ── reasoning_effort 拒绝 + 学习能力 ──────────────────────
// 解析 4xx 正文 REJECTS reasoning_effort
// 参数并提取提供者所说的它接受的内容——这是能力DISCOVERY，
// 而不是真正的失败：具有受支持的（或没有）努力值的相同请求成功。
// 观察到的形状：
//   Mistral (400):
//                    "reasoning_effort low is not supported for this model, supported values: [<ReasoningEffort.high: 'high'>, <ReasoningEffort.none: 'none'>]"（此模型不支持 low 推理力度，只允许 high 或 none。）
//                    Mistral (400):
//   "reasoning_effort is not enabled for this model"（此模型未启用 reasoning_effort。）
//   通用兼容性："Unrecognized request argument: reasoning_effort"
//                   /"unknown field: reasoning_effort"
// /…当错误是NOT努力拒绝时返回nullopt；否则接受 ON 级别
// 的位掩码（0 = 模型根本不需要任何努力参数）。 状态更新函数 通过
// set_learned_effort_set 和 重试 使用钳位值
// 记录它 — 拒绝 → 学习 → 钳位 → 重试 → 记住。
[[nodiscard]] inline std::optional<std::uint8_t> parse_effort_rejection(
        std::string_view message, int http_status) noexcept {
    if (http_status != 0 && http_status != 400 && http_status != 422)
        return std::nullopt;
    // Case-insensitive子字符串搜索，返回位置(npos
    // -like不存在时为-1)，以便我们可以在匹配后对末尾进行切片。
    auto ifind = [&](std::string_view hay, std::string_view needle,
                     std::size_t from) noexcept -> std::size_t {
        if (needle.empty() || needle.size() > hay.size())
            return std::string_view::npos;
        for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
            bool ok = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = hay[i + j], b = needle[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) { ok = false; break; }
            }
            if (ok) return i;
        }
        return std::string_view::npos;
    };
    // 拒绝下的参数。同一旋钮的三个 传输 拼写：
    //   · reasoning_effort —OpenAI-compat 聊天（米斯特拉尔、DeepSeek，……）
    //   ·reasoning.effort/"reasoning" —响应-style 正文
    //   •认为-Ollama本机("invalid think value: \"minimal\"(必须
    //     为\"high\", \"medium\", \"low\", true, or false)")
    // 其中任意一个锚定解析；下面的值-harvest是共享的。
    std::size_t at = ifind(message, "reasoning_effort", 0);
    if (at == std::string_view::npos)
        at = ifind(message, "think value", 0);
    if (at == std::string_view::npos)
        at = ifind(message, "reasoning.effort", 0);
    if (at == std::string_view::npos) return std::nullopt;
    // 必须看起来像是对参数的拒绝，而不是偶然提及。
    const bool rejected =
        ifind(message, "not supported", 0)   != std::string_view::npos
     || ifind(message, "not enabled", 0)     != std::string_view::npos
     || ifind(message, "unrecognized", 0)    != std::string_view::npos
     || ifind(message, "unknown field", 0)   != std::string_view::npos
     || ifind(message, "unexpected", 0)      != std::string_view::npos
     || ifind(message, "invalid", 0)         != std::string_view::npos
     || ifind(message, "unsupported", 0)     != std::string_view::npos
     || ifind(message, "should be", 0)       != std::string_view::npos
     || ifind(message, "must be one of", 0)  != std::string_view::npos;
    if (!rejected) return std::nullopt;

    // 当存在时，从 "supported values"/"must be one of"
    // /"should be" 之后的 末尾 收获接受的值，否则扫描整个 正文。
    std::size_t tail = 0;
    for (std::string_view marker : {"supported values", "must be one of",
                                    "should be", "expected"}) {
        if (auto p = ifind(message, marker, 0); p != std::string_view::npos) {
            tail = p; break;
        }
    }
    std::uint8_t set = 0;
    bool any_named = false;
    // 位值MUST镜像catalog.hpp的effort_bit(
    // error_class.hpp不能包括catalog.hpp-分层)：低=1<<0中=1<<1高=
    // 1<<2X高=1<<3最大=1<<4Minimal=1<<5。引用形式涵盖了观察到的错误语法：
    // 'single'（Mistral 枚举）、"double"（Ollama 认为）、.dotted（
    // Mistral ReasoningEffort.high）、[括号内/空格-led 列表。
    struct { std::string_view name; std::uint8_t bit; } levels[] = {
        {"'minimal'", 1u << 5}, {"\"minimal\"", 1u << 5}, {".minimal", 1u << 5},
        {"'low'", 1u << 0},   {"\"low\"", 1u << 0},   {".low", 1u << 0},   {"[low", 1u << 0},   {" low", 1u << 0},
        {"'medium'", 1u << 1},{"\"medium\"", 1u << 1},{".medium", 1u << 1},{"[medium", 1u << 1},{" medium", 1u << 1},
        {"'high'", 1u << 2},  {"\"high\"", 1u << 2},  {".high", 1u << 2},  {"[high", 1u << 2},  {" high", 1u << 2},
        {"'xhigh'", 1u << 3}, {"\"xhigh\"", 1u << 3}, {".xhigh", 1u << 3},
        {"'max'", 1u << 4},   {"\"max\"", 1u << 4},   {".max", 1u << 4},
    };
    // "xhigh"包含"high"：
    // 扫描xhigh拼写FIRST并通过要求存在歧义的短名称的精确quoted/dotted形式来在概念上将其空白。
    // bracketed/space表格仅适用于low/ medium/high，
    // 一旦检查了xhigh的引用表格，它们就不会冲突。
    for (const auto& lv : levels) {
        if (lv.bit == 0) continue;
        if (ifind(message, lv.name, tail) != std::string_view::npos) {
            set |= lv.bit;
            any_named = true;
        }
    }
    // "xhigh"还匹配" high"/".high"/"[high"扫描
    // ——这很好（在其接受列表中命名xhigh的提供者接受high-ish层；
    // nearest_effort钳位以任何方式将请求保留在集合内）。
    if (!any_named) return std::uint8_t{0};   // 参数被彻底拒绝
    return set;
}

// ── 传输 错误 TYPE – HTTP 状态 ────────────────────────────────────────
//
// mid-stream `event: error` 正文携带机器
// -readable `error.type`并且没有HTTP状态：当
// SSE 正文流动时，状态线已经过去很久了(是200)。因此分类器又
// 转而嗅探人类 `message`，这是 头文件 存在要避免的有损路径。
//
// 回退只是大部分正确，并且失败是不对称的：
//
//   ·提供者将`overloaded_error`表述为“该模型当前是
//     “经历高需求”没有嗅探器知道的子串，
//     因此可重试的过载将 终端 分类并且回合结束
//   ·一个真正的`invalid_request_error`，其消息恰好包含
//     单词"connection"将瞬态分类并且
//     针对永远不会成功的请求重试六次
//
// 如果我们使用服务器已经为我们带类型的的字段，则两者都会消失。
// 这会将错误分类映射到等效的 HTTP 状态，因此 SSE 路径到达
// 相同 编译-time-proven 分类 (HttpError)
// 表作为 头文件 路径，而不是并行的漂移子字符串列表。
//
// 对于无法识别的类型返回 0，这意味着 "no opinion"——调
// 用者保持其现有的 string-sniff 行为而不是猜测 终端。
//
// 涵盖 Anthropic 的已发布分类法以及含义相同的
// OpenAI/Responses 拼写；两种方言都以这种方式出现错误，
// 并且共享表是一个更少的漂移地方。
[[nodiscard]] inline int wire_error_type_status(std::string_view type) noexcept {
    // 速率限制——服务器故意丢弃加载。
    if (type == "rate_limit_error" || type == "rate_limit_exceeded"
     || type == "slow_down")
        return 429;
    // 信用不足。从未重试：每次尝试都会得到相同的答案。
    // (insufficient_quota曾经与上面的429坐在一起，
    // 所以在用户看到之前，计费墙被重试了六次。)
    if (type == "insufficient_quota" || type == "billing_error"
     || type == "payment_required_error" || type == "credit_balance_exhausted"
     || type == "payment_required")
        return 402;
    // 过载/瞬态服务器-side —相同的请求可能会起作用。
    if (type == "overloaded_error" || type == "server_is_overloaded"
     || type == "service_unavailable")
        return 503;
    if (type == "api_error" || type == "server_error"
     || type == "internal_server_error")
        return 500;
    if (type == "timeout_error" || type == "request_timed_out")
        return 504;
    // Auth — 凭据，不是请求。
    if (type == "authentication_error" || type == "invalid_api_key")
        return 401;
    if (type == "permission_error" || type == "permission_denied")
        return 403;
    // 终端请求问题——re-sending没有任何改变。
    if (type == "invalid_request_error" || type == "invalid_request")
        return 400;
    if (type == "not_found_error" || type == "model_not_found")
        return 404;
    if (type == "request_too_large" || type == "context_length_exceeded")
        return 413;
    return 0;   // 未知——无意见，让来电者嗅探
}

// 第 N 次 重试 尝试的 退避 持续时间（0-indexed)。尝试次数上限
// 为 6 次；由于 Anthropic 的 per-minute 窗口不会按需重置，
// 因此 RateLimit 的时间表更长。返回 `std::chrono::milliseconds`，
// 因此该单位属于以下类型 — 使用 `Cmd::after(d)`
// 进行调度的调用者不会意外地在预期的毫秒数处输入秒数。
//
// 瞬态调度开始激进(500ms):
// 绝大多数stalls/brown-outs是Anthropic边缘上的单个楔形h2数据流，
// 并且新连接几乎总是成功。我们只会在稍后
// 的尝试中徘徊，此时中断要么是真实的，
// 要么我们即将将其显示为 终端。
[[nodiscard]] constexpr std::chrono::milliseconds
backoff(ErrorClass kind, int attempt) noexcept {
    using std::chrono::milliseconds;
    if (attempt < 0) attempt = 0;
    if (attempt > 5) attempt = 5;
    if (kind == ErrorClass::RateLimit) {
        constexpr milliseconds table[6] = {
            milliseconds{3000},  milliseconds{8000},  milliseconds{20000},
            milliseconds{40000}, milliseconds{60000}, milliseconds{90000},
        };
        return table[attempt];
    }
    // 瞬态/认证重试-首先快速从单个数据流楔子恢复，
    // 然后逐渐后退。
    constexpr milliseconds table[6] = {
        milliseconds{500},   milliseconds{2000},  milliseconds{5000},
        milliseconds{12000}, milliseconds{25000}, milliseconds{45000},
    };
    return table[attempt];
}

// 退避，其中±20%抖动应用于基本调度。当服务器没有向我们提供
// 重试-After 提示并且我们退回到我们自己的梯子时使用。
// 在区域 Brown-outs 期间，抖动很重要：数百个客户
// 端在 1 秒窗口内命中相同的 503，如果没有抖动，
// 每个客户端都会在相同的 500 ms / 2 s / 5 s
// 滴答内执行 重试 — 重新超载刚刚摆脱它们的边缘。
// ±20% 将群体分布在 0.8x–1.2x 窗口内，
// 而不会让用户感觉延迟不可预测。
//
// 不是constexpr，因为thread_local mt19937需要运行时 init。
// 呼叫者有责任在同一回合的 重试 上一致地传递 `attempt` (源
// ctx 的 `transient_retries` 计数器)。
[[nodiscard]] inline std::chrono::milliseconds
backoff_with_jitter(ErrorClass kind, int attempt) noexcept {
    auto base = backoff(kind, attempt);
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.80, 1.20);
    auto scaled = static_cast<std::int64_t>(
        static_cast<double>(base.count()) * dist(rng));
    if (scaled < 100) scaled = 100;  // 地板-从不忙-loop在一个小底座上
    return std::chrono::milliseconds{scaled};
}

// 自动 重试 上的硬上限。在此之后，表面为 终端。
// 从4提高到6：数据流可以在区域Anthropic事件期间
// 楔入几分钟，并且指数末尾（25秒+45秒）意味着尝试5+
// 6花费很少的延迟，但将user-visible
// "error: stream stalled"转换为长时间
// 中断时的透明恢复。 Esc 仍然会随时中断循环。
inline constexpr int kMaxRetries = 6;

// Per-error-class 重试 帽。单个全局 kMaxRetries
// 对每个瞬态都进行相同的处理，这就是为什么不断切断 mid-stream 的
// 片状 传输 会向 重试 横幅发送高达 6 倍的垃圾邮件。 Zed 的
// 代理循环 (crates/agent/src/thread.rs::
// retry_strategy_for) 相反会调整每个错误形状的上限：rate
// -limit / 过载获得完整预算（服务器将恢复并通过 重试-After 告诉我们），
// 但是在 -body 中意外结束的 数据流 几乎没有尝试，
// 因为 re-hammering 和在 -stream 中下降的 传输 很少有帮助，
// 而且看起来很糟糕用户。我们镜像那个形状。
//
//   RateLimit (429 / 过载 / 529) ....... kMaxRetries
//       (6) — 服务器正在丢弃 加载，通常会传递 重试-After；渡过难关。
//   瞬时 connect/dial 光点（无内容）。 kMaxRetries
//       (6) — 重新连接成本低廉，几乎总是在新连接上恢复。
//   Mid-stream 失速/意外的 EOF ......
//       4 — 传输 到达我们然后死亡。一个长期超载的提供商（
//       寓言/代理边缘脱落 加载）几乎每次都会掉到 -body 中间，
//       而 2 的上限几乎立即出现 终端 错误 - 横幅感觉永久卡住了。
//       4 更安静地度过了一个棕色-out（500ms→2s→
//       5s→12s 瞬态阶梯意味着尝试 3-4 增加了约 7
//       -17 秒的耐心，而不是几分钟），同时仍然收敛得足够快，
//       以至于真正死亡的 传输 不会结结巴巴地通过 6。
//
// 当故障发生AFTER时，`mid_stream`由调用者设置，
// 数据流在本轮已证明自己还活着（失速-watchdog火灾，
// 或在第一个增量之后重置）。分类器无法仅从消息中看到这一点，
// 因此将其传入。
[[nodiscard]] inline int max_retries_for(ErrorClass k, bool mid_stream) noexcept {
    switch (k) {
        case ErrorClass::RateLimit: return kMaxRetries;
        case ErrorClass::Transient: return mid_stream ? 4 : kMaxRetries;
        case ErrorClass::Auth:      return kMaxRetries;  // 一次刷新+松弛
        case ErrorClass::Cancelled:
        case ErrorClass::Terminal:  return 0;
    }
    return kMaxRetries;
}

// 预算-decay窗口。 transient_retries 是
// 一个 per-turn 计数器，但长时间的会话会导致不相关的
// Brown-outs 累积到其中。如果先前的故障发生的时间比这更长，
// 则连接在此期间一直正常，因此 重试 决策会将计数器重置为 0，
// 然后再对新的故障进行计数。与 kMaxRetries 解耦，
// 因此缓慢的区域事件（在窗口内每 20-45 秒失败一次）
// 仍然会收敛到 终端，而相隔几分钟的瞬态信号永远不会收敛。
inline constexpr std::chrono::seconds kRetryDecayWindow{90};

// 对具有 ZERO 模型进度的连续预定 重试 的硬上限（无内容增量，
// 无结构化工具 调用 — 不 计数为心跳）。上面的衰减窗口在
// "healthy wire" 的 90 秒后重置
// transient_retries，但 退避 等待本身会勾选
// 该窗口 - 因此接受连接并失败每个请求（本地服务器、
// 错误路径或模型）的端点可能会循环失败→退避→重置→永远失败。
// 经过多次 no-progress 尝试后，无论类别如何，失败都会锁存 终端。
inline constexpr int kMaxNoProgressFailures = 6;

// 比这个长的`Retry-After`是QUOTA，而不是突发限制
// ——将其视为终端而不是等待。
//
// 429涵盖两种不相关的情况，并且只有其中一种值重试。
// burst/concurrency限制在几秒内清除。计划配额在重置之前不会清除：
// Yolo-Auto 的免费套餐以 type=insufficient_quota
// 回答 `retry-after: 29410`（8 小时，至 00:00 UTC），
// 托管提供商在计费上限上回答类似。
//
// 重试第二种总是错误的——每次尝试都保证失败，
// 并且FIRST响应已经携带消息，因此用户等待
// 退避什么也学不到。 15 分钟是界限：
// 超过这个界限，就没有人坐在 终端 等待窗户打开，
// 而且立即说出来比稍后说更有用。
inline constexpr long kRetryAfterTerminalSeconds = 15 * 60;

[[nodiscard]] constexpr std::string_view to_string(ErrorClass k) noexcept {
    switch (k) {
        case ErrorClass::Transient: return "transient";
        case ErrorClass::RateLimit: return "rate_limit";
        case ErrorClass::Auth:      return "auth";
        case ErrorClass::Cancelled: return "cancelled";
        case ErrorClass::Terminal:  return "terminal";
    }
    return "unknown";
}

// ── 编译-time 证明文件 HTTP→ErrorClass 绘图 ──────────────────
// 此处针对分类器检查HTTP层可以返回的每个kind/status。
// 如果表发生漂移（有人对开关重新排序、
// 添加新的 HttpErrorKind 而不更新此重载，
// 或者更改 auth/rate-limit 状态映射），则在任何 运行
// 时 路径采用错误分支之前构建就会中断。
namespace proofs {
using agentty::http::HttpError;
using agentty::http::HttpErrorKind;

// 已取消始终是已取消——从不重新-issued，从不重新分类。
static_assert(classify(HttpError{HttpErrorKind::Cancelled, 0, ""}) == ErrorClass::Cancelled);

// 所有 传输层-layer 类型映射到瞬态。
static_assert(classify(HttpError{HttpErrorKind::Resolve,      0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Connect,      0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Tls,          0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Protocol,     0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::SocketHangup, 0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Timeout,      0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::PeerClosed,   0, ""}) == ErrorClass::Transient);

// 正文/未知是终端——re
// -issuing不会改变格式错误的响应或程序员错误。
static_assert(classify(HttpError{HttpErrorKind::Body,    0, ""}) == ErrorClass::Terminal);
static_assert(classify(HttpError{HttpErrorKind::Unknown, 0, ""}) == ErrorClass::Terminal);

// 状态映射-HTTP-status子-table。
static_assert(classify(HttpError{HttpErrorKind::Status, 401, ""}) == ErrorClass::Auth);
static_assert(classify(HttpError{HttpErrorKind::Status, 403, ""}) == ErrorClass::Auth);
static_assert(classify(HttpError{HttpErrorKind::Status, 429, ""}) == ErrorClass::RateLimit);
static_assert(classify(HttpError{HttpErrorKind::Status, 408, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Status, 502, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Status, 503, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Status, 504, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Status, 529, ""}) == ErrorClass::Transient);
// 任何其他 4xx 都是 终端 — 模型说不，
// 重试 不会改变主意。
static_assert(classify(HttpError{HttpErrorKind::Status, 400, ""}) == ErrorClass::Terminal);
static_assert(classify(HttpError{HttpErrorKind::Status, 404, ""}) == ErrorClass::Terminal);
static_assert(classify(HttpError{HttpErrorKind::Status, 422, ""}) == ErrorClass::Terminal);
// 200 OK到达分类器意味着应用程序-level不一致（
// 调用者将其显示为带有200的状态——在实践中仅通过正文
// 类型发生；为了完整性而保留）。
static_assert(classify(HttpError{HttpErrorKind::Status, 200, ""}) == ErrorClass::Terminal);

} // namespace proofs

} // namespace agentty::provider
