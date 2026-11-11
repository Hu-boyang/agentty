#pragma once

// ── Provider 数据流 内涵:共享 "how a streamed turn ends" 层 ────
//
// 一旦其 SSE/NDJSON 正文 被馈送到 HTTP 客户端，每个 流式传输 传输层 (Anthropic、
// OpenAI-compat、Ollama、ChatGPT/Codex)就面临
// 相同 两个问题：
//
//   1.发出EXACTLY 一个 终端 事件。一回合以单曲结束
//      StreamFinished(成功)或StreamError(失败)-从不为零，
//      从不为二。 正文 本身通常会触发它（`response.completed`
//      / `message_stop` / `[DONE]` 帧），因此 post
//      -loop 代码不得 发出第二个。忘记这个守卫正是 ChatGPT 路径所存在的
//      错误：它在 `response.completed` 之后中止读取，
//      HTTP 层将故意中止报告为 "cancelled"，而 post-loop
//      将其转发为虚假的 StreamError — 因此，干净的转向显示
//      "cancelled"。
//
//   2、统一解释循环出口。当 正文 完成 不 时，
//      数据流循环返回的原因具有固定优先级：真实用户取消，
//      然后HTTP错误状态，然后传输层错误，否则干净套接字
//      关闭（以最后-seen停止原因结束）。
//
// 历史上每个传输层都打开-coded，并且它们漂移（ChatGPT
// 路径出现#1错误）。这个 头文件 是唯一的事实来源，
// 因此所有提供商的行为都是相同的，而新的 传输层 则免费提供正确的信息。
//
// 它故意很小并且头文件-only:对于公共词汇表(EventSink、StopReason、
// StreamFinished/StreamError、CancelToken)的两个自由函数。
// 传输层-specific "flush before finishing"
// 工作（抢救泄漏的工具调用，关闭打开的工具块）被作为回调注入，
// 因此共享规则成立，而共享代码不需要知道每个传输层的内部结构。

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "agentty/io/http.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/runtime/msg.hpp"

namespace agentty::provider {

// 发射单个 终端 事件 一圈，恰好一次。
//
//   终止in/out锁存器。如果已经为真，则这是一个 no-op（正文 - 或
//                 更早的尾声 调用 - 已经完成了回合）。在发出时设置为 true，
//                 这样后面的 调用 就不能加倍 -finish。
//   沉传输层的EventSink。
//   停止报告SUCCESS的停止原因（错误时被忽略）。
//   错误std::nullopt→成功（StreamFinished{停止}）；否则 StreamError
//                 {*err, retry_after}。
//   retry_after服务器重试-After提示进行错误(429/529)。
//   before_finish 传输层钩子在成功路径上运行ONLY，
//                 就在StreamFinished之前——
//                 刷新保持的文本/关闭打开的工具块/抢救泄漏的工具调用的位置。出错时不运行。
//
// 这是每个传输层的旧`emit_terminal` lambda的共享正文。
inline void finish_turn_once(
        bool& terminated,
        const EventSink& sink,
        StopReason stop,
        std::optional<std::string> err = std::nullopt,
        std::optional<std::chrono::seconds> retry_after = std::nullopt,
        const std::function<void()>& before_finish = {},
        int http_status = 0,
        bool non_replayable = false) {
    if (terminated) return;
    terminated = true;
    if (err) {
        StreamError e{std::move(*err), retry_after};
        e.http_status = http_status;
        e.non_replayable = non_replayable;
        sink(std::move(e));
    } else {
        if (before_finish) before_finish();
        sink(StreamFinished{stop});
    }
}

// 数据流 循环如何结束（按优先顺序）。 `classify_stream_end`
// 返回正好 1； 传输层 打开它以发出正确的 终端 事件（或者对于
// AlreadyTerminated 则什么也不发出）。
enum class StreamEnd {
    // SSE/NDJSON 正文已经发射了终端 事件 (并且设置了`terminated`)。
    // 发出更多 无内容 — 这是必须获胜的情况，因此在干净完成后有意读
    // 取 -abort 绝不会被误认为是 cancel/error.
    AlreadyTerminated,
    // 设置呼叫者的取消令牌：真实用户Esc。发出 StreamError
    // {"cancelled"}。
    UserCancelled,
    // 响应状态> = 400。传输层从缓冲的错误
    // 正文构建错误消息。
    HttpError,
    // HTTP客户端返回错误(connection/TLS/reset)并且它不是用户取消。
    // 传输层 渲染 result.error()。
    TransportError,
    // 2xx 数据流干净地关闭，没有显式的终端 帧（代理截止）。
    // 完成最后一个-seen 停止原因。
    CleanClose,
};

// ── StreamResult : 流转的结束, 作为 值 ────────────────────
//
// 转弯的终端结果过去只能作为副作用观察到——
// StreamFinished/StreamError Msg被推入接收器，
// 呼叫者必须拦截并重新-decode以了解“这个转弯是否错误？是已取消吗？
// 重试 提示是什么？”。 StreamResult 使该结果成为第一个
// -class 返回值：`stream()`（和 finish_stream）
// 将其交回，发出的 终端 Msg 是其中的 派生，调用者读取一个字段而不是嗅探
// 事件。
//
// (与acp_backend.hpp中的provider::TurnResult不同，
// ACP后端抽象的per-ROUND结果带有更丰富的TurnError。
// 这是in-process SSE/NDJSON尾声的结果，
// 以StreamEnd +原始HTTP状态为关键——两者描述相邻层并且故意不是
// 同一类型。)
//
// `end`是分类优先获胜者（参见classify_stream_end)。
// 下面的派生访问器 归并 和 StreamEnd + 载荷 转化为调用者
// 实际询问的两个问题。
struct StreamResult {
    StreamEnd                             end = StreamEnd::CleanClose;
    StopReason                            stop = StopReason::EndTurn;
    std::optional<std::string>            error;        // 当且仅当转弯失败时设置
    std::optional<std::chrono::seconds>   retry_after;  // 429/529 上的服务器提示
    int                                   http_status = 0;
    bool                                  non_replayable = false;

    // 回合是否成功结束（完全关闭，或者 正文 已经以我们尊重的
    // 终端 结束）？ 取消 / HTTP / 传输层 错误为假。
    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
    // 这是用户-initiated 取消（相对于真正的故障）吗？
    [[nodiscard]] bool cancelled() const noexcept {
        return end == StreamEnd::UserCancelled;
    }
    // 正文是否已经发射了自己的终端，所以尾声什么也没发出？ （在 post
    // -loop 之前干净的早期-abort / `[DONE]`。）
    [[nodiscard]] bool already_terminated() const noexcept {
        return end == StreamEnd::AlreadyTerminated;
    }

    // 对于少数 pre-flight bail-outs，
    // 传输层 在 数据流 循环甚至开始之前就处理自身（未验证/请求-build
    // 失败）——它发出自己的 StreamError 并返回它，
    // 因此该值仍然反映现实。
    static StreamResult failed(std::string msg) {
        StreamResult r;
        r.end   = StreamEnd::TransportError;
        r.error = std::move(msg);
        return r;
    }
};

// 对循环出口进行分类。 `result_ok` 是 `bool(result)`，
// 代表 传输层 的 std::expected 返回； `http_status` 是
// 观察到的响应状态（如果 头文件 从未到达，则为 0 — 被视为 not-an-HTTP-error，
// 因此 传输层 错误或 取消 获胜）。每个提供商的优先级都是固定且相同的。
[[nodiscard]] inline StreamEnd classify_stream_end(
        bool terminated,
        bool result_ok,
        int http_status,
        const http::CancelTokenPtr& cancel) noexcept {
    if (terminated)                             return StreamEnd::AlreadyTerminated;
    if (cancel && cancel->is_cancelled())       return StreamEnd::UserCancelled;
    if (http_status >= 400)                     return StreamEnd::HttpError;
    if (!result_ok)                             return StreamEnd::TransportError;
    return StreamEnd::CleanClose;
}

// 传输层的post-loop结束回合所需的所有东西都集中在一个包中。
// 所有挂钩都是可选的；合理的默认值使常见情况保持简洁。每一个事实都被陈述了
// 一次：`terminated` 是对 传输层 自己的锁存器的引用（不是传递两次
// 的副本），而 `sink` 是 传输层 的 EventSink —
// 因此 finish_stream 正是采用这一参数，并且在 调用 站点上
// 没有任何重复。
struct StreamOutcome {
    bool&              terminated;    // 传输层的终端锁存器(in/out)
    const EventSink&   sink;          // 终端 Msg被发射的地方
    bool               result_ok;     // http 数据流 调用 的 bool(结果)
    int                http_status;   // 观察到的状态（0=头文件从未到来）
    bool               non_replayable = false; // 接受POST;从不 auto-retry
    http::CancelTokenPtr cancel;      // 呼叫者的取消令牌
    StopReason         stop = StopReason::EndTurn;   // 停于CleanClose

    // 根据缓冲的错误正文构建HTTP > = 400状态的user
    // -facing错误消息。当 4xx/5xx 可能时需要。
    std::function<std::string()>                 http_error_message;
    // 可选的重试-After提示附加到HTTP错误(429/529)。
    std::optional<std::chrono::seconds>          retry_after;
    // 渲染 传输层-level错误(connection/TLS/reset)。必需的。
    std::function<std::string()>                 transport_error_message;
    // Success-only钩子：刷新保持的文本/关闭工具块/抢救截断的工具调用，
    // 就在StreamFinished之前。运行时间为 CleanClose。
    std::function<void()>                        before_finish;
    // 全部-paths 钩子，在每次非 AlreadyTerminated
    // 退出时在 终端 事件 之前运行一次（成功 和 错误）。无论转弯是否成功，
    // 都必须进行清理的地方——e.g。 Anthropic 在这里合成了
    // StreamToolUseEnd，因此 状态更新函数 的抢救路径在部分工具
    // JSON 上运行，即使 对端 在 -tool-use 中期死亡。
    std::function<void()>                        on_any_end;
    // 方言总是发送终端 事件 (Anthropic message_stop、
    // 响应response.completed、聊天[DONE])。
    // 当它这样做并且连接在没有连接的情况下干净地关闭时，
    // 回复在传输过程中被切断：报告这一点，因此重试而不是
    // 在中途默默结束。对于普通结束语是正常的方言，关闭。
    bool                                         expects_terminal_event = false;
};

// WHOLE后-loop为流式传输 传输层，统一。对循环出口进行分类，然后准确地发出
// 一个 终端 事件 和正确的 message/precedence — 对于每个提供商
// 来说都是相同的。将每个 传输层 的定制 `emit_terminal`
// lambda 加手写判断，例如 if (!result) 和 if (!is_success)。
// 梯子替换为单个 调用，因此新的 传输层 免费正确结束转弯：
//
//   return finish_stream({
//       .terminate = ctx.terminated, .sink = ctx.sink,
//       .result_ok = bool(结果), .http_status = http_status,
//       .取消 = 取消, .stop = ctx.stop, .
//       http_error_message = [&]{ return build_http_error(); },
//       .transport_error_message = [&]{ 返回 result.error().render(); }, .
//       before_finish = [&]{ flush_and_salvage(ctx); },
//   });
//
// `o.terminated`是传输层自己的锁存器（参考），因此已经完成的部分正文受到尊重，
// 并且稍后的调用不能加倍-finish。在 UserCancelled 我们发出
// StreamError{"cancelled"}；在 AlreadyTerminated 我们什么
// 也没发出。
//
// 返回描述结果的StreamResult。发出的 终端 Msg
// 源自 相同 分类，因此返回值和接收器 事件 永远不会不一致 - 想要
// 结果的调用者读取该值，对消息做出反应的调用者仍然看到
// StreamFinished / StreamError。
inline StreamResult finish_stream(StreamOutcome o) {
    bool& terminated = o.terminated;
    const EventSink& sink = o.sink;
    const StreamEnd end = classify_stream_end(o.terminated, o.result_ok,
                                              o.http_status, o.cancel);
    StreamResult tr;
    tr.end         = end;
    tr.stop        = o.stop;
    tr.http_status = o.http_status;
    tr.retry_after = o.retry_after;
    tr.non_replayable = o.non_replayable;

    // EVERY提供者的回合在此结束，因此一行记录了每一回
    // 合如何结束——对于所有四种方言，而不触及单个传输层。
    //
    // 上方 AlreadyTerminated早期-return:
    // 正常的流式传输回合通过看到其终端 SSE 事件而结束，
    // 因此它采取该分支。日志记录仅记录失败，而健康的转变则不可见——这
    // 正是支持 线程 开头的 "did my turn even run?"
    // 问题。
    //
    // 失败的转弯是警告，因此即使文件注销，
    // 它也会降落在飞行记录器中并因此降落在故障转储中。
    AGT_LOGL(Wire, end == StreamEnd::CleanClose
                       || end == StreamEnd::AlreadyTerminated
                   ? ::agentty::logx::Level::Debug
                   : ::agentty::logx::Level::Warn,
            "stream.end", "end={} http={} stop={} replayable={}",
            // NAMES，而不是 枚举 索引：用户粘贴到错误报告中的日志必须在
            // 源码树 打开的情况下可读。 `end=3` 对任何人来说都毫无意义；
            // `end=transport_error` 就是答案。
            end == StreamEnd::AlreadyTerminated ? "already_terminated"
          : end == StreamEnd::UserCancelled     ? "cancelled"
          : end == StreamEnd::HttpError         ? "http_error"
          : end == StreamEnd::TransportError    ? "transport_error"
                                                : "clean_close",
            o.http_status,
            o.stop == StopReason::EndTurn      ? "end_turn"
          : o.stop == StopReason::ToolUse      ? "tool_use"
          : o.stop == StopReason::MaxTokens    ? "max_tokens"
          : o.stop == StopReason::StopSequence ? "stop_sequence"
                                               : "unspecified",
            !o.non_replayable);

    if (end == StreamEnd::AlreadyTerminated) return tr;
    // 在终端 事件之前进行所有-paths清理(e.g.
    // 关闭打开的工具块)，无论成功还是错误。只运行一次。
    if (o.on_any_end) o.on_any_end();
    switch (end) {
        case StreamEnd::AlreadyTerminated:
            return tr;
        case StreamEnd::UserCancelled:
            tr.error = std::string{"cancelled"};
            finish_turn_once(terminated, sink, o.stop, tr.error);
            return tr;
        case StreamEnd::HttpError:
            tr.error = o.http_error_message
                           ? o.http_error_message()
                           : std::string{"HTTP "} + std::to_string(o.http_status);
            // USER在日志中逐字看到的消息——因此支持
            // 线程可以从真实的提供者文本而不是它的
            // 屏幕截图开始。
            AGT_LOG(Wire, Warn, "stream.http_error", "status={} msg={}",
                    o.http_status, *tr.error);
            // 在Msg上标记精确状态，以便重试 状态更新函数可以通过带类型的
            // provider::classify(HttpError)路径进行分类。
            finish_turn_once(terminated, sink, o.stop, tr.error, o.retry_after,
                             {}, o.http_status);
            return tr;
        case StreamEnd::TransportError:
            tr.error = o.transport_error_message
                           ? o.transport_error_message()
                           : std::string{"transport error"};
            // USER在日志中逐字看到的消息——因此支持
            // 线程可以从真实的提供者文本而不是它的
            // 屏幕截图开始。
            AGT_LOG(Wire, Warn, "stream.transport_error", "status={} msg={}",
                    o.http_status, *tr.error);
            finish_turn_once(terminated, sink, o.stop, tr.error,
                             std::nullopt, {}, 0, o.non_replayable);
            return tr;
        case StreamEnd::CleanClose:
            if (o.expects_terminal_event) {
                tr.error = std::string{"the reply was cut off before it finished "
                                       "(connection closed early)"};
                AGT_LOG(Wire, Warn, "stream.cut_off", "status={}", o.http_status);
                finish_turn_once(terminated, sink, o.stop, tr.error,
                                 std::nullopt, {}, 0, o.non_replayable);
                return tr;
            }
            finish_turn_once(terminated, sink, o.stop, std::nullopt,
                             std::nullopt, o.before_finish);
            return tr;
    }
    return tr;
}

// ── 共享 重试-After 头文件 解析器 ────────────────────────────────────────
//
// 每个 传输层 的 on_headers 回调 用于处理 -roll 相同 循环，
// 以从响应 头文件 中提取服务器 退避 提示 — 扫描
// `retry-after` (case-insensitive)、解析 整秒，
// 拒绝任何不是干净正整数的内容 (HTTP-date 形式是故意的NOT 很荣幸；
// 我们宁愿回到自己的日程，也不愿错过 -parse 的约会）。副本发生了漂移：
// Ollama 传输层 默默地完全忽略了它并忽略了服务器 退避。
// 这是唯一的事实来源——每个 传输层 调用 都来自 on_headers，
// 并将结果提供给 finish_stream 的 `retry_after`。
[[nodiscard]] inline std::optional<std::chrono::seconds>
parse_retry_after(const http::Headers& headers) noexcept {
    auto eq_ci = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
            if (x != y) return false;
        }
        return true;
    };
    for (const auto& h : headers) {
        if (!eq_ci(h.name, "retry-after")) continue;
        try {
            std::size_t consumed = 0;
            const unsigned long v = std::stoul(h.value, &consumed);
            if (consumed == h.value.size() && v > 0)
                return std::chrono::seconds(v);
        } catch (...) {
            // Non-integer (或 HTTP-date) — 保留未设置；
            // 运行时 回到其自己的 退避 时间表。
        }
        return std::nullopt;   // 匹配头文件;不要进一步扫描
    }
    return std::nullopt;
}

// `s`开始处的Go-style持续时间（"6043s"、"43.39s"、
// "1h40m43.390047655s"、"500ms")，向上舍入到整秒。如果 `s` 不以 1 开头，
// 则为 nullopt。
[[nodiscard]] inline std::optional<std::chrono::seconds>
parse_go_duration(std::string_view s) noexcept {
    double total = 0;
    bool any = false;
    std::size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        double whole = 0, frac = 0, scale = 1;
        bool dot = false, digits = false;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (c >= '0' && c <= '9') {
                digits = true;
                if (dot) { scale /= 10; frac += (c - '0') * scale; }
                else     whole = whole * 10 + (c - '0');
            } else if (c == '.' && !dot) {
                dot = true;
            } else {
                break;
            }
        }
        if (!digits) return std::nullopt;
        const double n = whole + frac;
        const std::string_view rest = s.substr(i);
        if (rest.starts_with("ms"))      { total += n / 1000;    i += 2; }
        else if (rest.starts_with("h")) { total += n * 3600;    i += 1; }
        else if (rest.starts_with("m")) { total += n * 60;      i += 1; }
        else if (rest.starts_with("s")) { total += n;           i += 1; }
        else return std::nullopt;
        any = true;
    }
    if (!any || total <= 0) return std::nullopt;
    const auto whole = static_cast<long long>(total);
    return std::chrono::seconds(whole + (total > static_cast<double>(whole) ? 1 : 0));
}

// 服务器在 BODY 中时是 "wait this long"，而不是 重试-After 头文件。
// Google 的 Gemini API 就是这样做的：它的 429 带有
// google.rpc.RetryInfo 详细信息 (`"retryDelay": "6043s"`) 并
// 在消息中重复它 ("Please retry in 1h40m43.39s.")，根本没有 头文件。
// 如果没有这个，每日配额看起来就像是突发限制，
// 并且在失败并显示第一个响应已经有的消息之前重试了几分钟。
[[nodiscard]] inline std::optional<std::chrono::seconds>
parse_retry_after_body(std::string_view body) noexcept {
    if (auto k = body.find("\"retryDelay\""); k != std::string_view::npos) {
        auto q = body.find('"', body.find(':', k) + 1);
        if (q != std::string_view::npos)
            if (auto d = parse_go_duration(body.substr(q + 1))) return d;
    }
    for (std::string_view key : {"retry in ", "Retry in "}) {
        if (auto k = body.find(key); k != std::string_view::npos)
            if (auto d = parse_go_duration(body.substr(k + key.size()))) return d;
    }
    return std::nullopt;
}

} // namespace agentty::provider
