#pragma once
// agentty::Msg-运行时可以处理每个事件，作为封闭分组的变体。
//
// 历史：
//   v1:一个std::variant中的79个叶子替代品。 sizeof(Msg)
//       = 79 个叶子的最大值；一个std::visit 调用实例化了一个79
//       -arm 分派表；一个 update.cpp TU 在一次重载中编译了所有
//       79 个分支{}。一开始还可以忍受；开始随着规模摇摆 - update.cpp
//       的 编译 时间攀升超过 15 秒，无论哪条路径处于活动状态，
//       sizeof(Msg) 都被最重的替代方案固定，并且任何叶更改都会强制完全
//       重新编译 分派 站点。
//
//   v2(本文件)：叶分组为10个域sub-variants。 top-level
//       Msg 是这些域的​​ `std::variant`。
//       平面构造语法仍然有效：
//
//           Msg m1=ComposerEnter{}；       // -> Msg{ComposerMsg{ComposerEnter{}}}
//           dispatch(StreamTextDelta{"x"}); // -> dispatch(Msg{StreamMsg{...}})
//
//       std::variant的转换构造函数遍历每个备选方案；
//       只有匹配的域接受给定的叶子，因此换行是明确且隐式的。
//       调用 站点不变。
//
//       Per-domain 状态更新函数 存在于 update/<domain>.cpp 中；
//       update.cpp 的 top-level std::visit 现在是转发到
//       其中的 10-arm 分派器。每个域的 TU 编译 都是独立的 - 触摸作曲家叶子
//       不再强制 stream/login/diff 重新编译。

#include <chrono>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "agentty/auth/auth.hpp"
#include "agentty/io/clipboard.hpp"   // ClipboardItem
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/runtime/panel/fork.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/runtime/panel/settings/categories.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/tool/registry.hpp"

namespace agentty {

// ============================================================================
// 叶类型——实际的事件。按源顺序按域分组；
// 运行时 分类由进一步向下的域 变体 强制执行。
// ============================================================================

// ── 编曲 ─────────────────────────────────────────────────────────────
struct ComposerCharInput { char32_t ch; };
struct ComposerBackspace {};
struct ComposerEnter {};
struct ComposerNewline {};
struct ComposerSubmit {};
struct ComposerToggleExpand {};
// ^B-arm/disarm LOOP模式：在每次完成转动后自动重新-send布防消息，
// 直到切换关闭。将作曲家当前的文本+附件（参见ComposerState::
// loop_text）武装到快照并立即提交，因此^B读作
// 发送此消息，并持续重复发送。
struct ComposerToggleLoop {};
struct ComposerCursorLeft {};
struct ComposerCursorRight {};
struct ComposerCursorHome {};
struct ComposerCursorEnd {};
// 编辑器上方的 `/` 菜单。 “移动”会移动选择，
// “完成”会将所选名称写入文本，“关闭”会将其隐藏为标记。
struct ComposerSlashMove { int delta; };
struct ComposerSlashComplete {};
struct ComposerSlashDismiss {};
// MCP `/prompt`由UI 线程渲染；作为消息提交。
// `usage` 标记了关于缺少必需参数的注释：显示为祝酒词并提交了 不，
// 因此打字错误不会造成任何损失。
struct SlashPromptRendered { std::string typed; std::string text; bool usage = false; };
struct ComposerPaste {
    std::string text;
    // 当该粘贴已通过 image-path 检查（参见
    // ImagePathSniffed）时设置，因此不会再次发送。
    bool path_checked = false;
    // 带类型的按原样进入文本，绝不是标签（已保存粘贴文件的路径）。
    bool literal = false;
    // 分批交付的终端的下一块粘贴：在光标
    // 之前生长标签而不是添加新的。
    bool continues_paste = false;
};
// 工作线程查看了粘贴的路径。当`path`不是可读图像时为空；
// 然后 状态更新函数 将 `text` 粘贴为普通文本。
struct ImagePathSniffed {
    std::string text;
    std::string path;
    std::string media_type;
    std::string body;
};
// 在发出 escape-based 剪贴板读取（OSC 5522/OSC 52）
// 后约 1.2 秒触发。如果那时没有收到粘贴回复，状态更新函数 会显示
// ACTIONABLE 诊断（其中 终端、tmux、mosh、确切的 env
// -var 修复），而不是旧的行为 — "reading clipboard…"
// toast 默默地失效，并且不会向用户传授任何内容。 `seq` 与武装
// 它的查询相匹配，因此 过期 超时 在成功粘贴后永远不会触发。
struct ClipboardQueryTimeout { std::uint64_t seq = 0; };
// 粘贴的图像-as-text 标签停止生长。武装时，`generation`
// 与 UI::armored_paste_gen 匹配；后来的一块会撞到它，
// 所以只有最后一块的最后一次检查才算数。
struct ArmoredPasteStall { std::uint64_t generation = 0; };
// 工作线程对"why did that clipboard read go unanswered"的回答。
struct ClipboardDiagnosed { std::string message; };
// Ctrl+V剪贴板读取的一步(richpaste的路线行走：显示器、
// ssh代理、终端、tmux)。空项目：`miss` 说明原因以及下一步；
// AskTerminal 进行步行，如果 终端 保持安静则恢复。
struct ClipboardRead {
    std::vector<ClipboardItem> items;
    ClipboardMiss              miss = ClipboardMiss::None;
    std::string                error;   // 为什么物品是空的，对于人来说
    TerminalAsk                ask = TerminalAsk::Text;
    ClipboardWalk              walk;
};
// 将排队的消息调用回编辑器以进行编辑。当组合器为空
// 并且存在排队消息时绑定到 Up-arrow。
// 镜像Claude代码的`Lc_` (二进制程序偏移
// 76303220):将每个可编辑的排队项目按照由"\n"
// 加入的队列顺序排出到编辑器中，其中光标落在调用的文本和任何前
// -existing编辑器文本之间的边界处。对队列具有破坏性
// - 这些项目随后仅存在于作曲家 缓冲区 中，因此用户必须
// 重新提交到 re-queue。如果用户在调用后清除了编辑器，
// 则这些项目就会消失（与 Claude 代码的行为相同）。
struct ComposerRecallQueued {};

// Per-item队列编辑器。 ComposerRecallQueued 的较大兄弟：
// 而不是排出-all，这些让用户循环THROUGH排队的项目，
// 加载一次一个进入编辑器，编辑它，然后提交（其在末尾处重新-queues
// ——现有流程）。绑定到 Alt+↑ / Alt+↓ / Alt+
// Backspace。
//
//   Alt+↑ ComposerQueuePeekPrev — 加载
//                                    将前一个排队项目放入编辑器中进行编辑。
//                                    在第一次按下时，实时草稿被快照
//                                    到 composer.draft_save 中，
//                                    因此圆形 -trip 背面
//                                    是非破坏性的（与历史行走相同
//                                    的想法）。该项目保留在队列中
//                                    while being peeked; submitting
//                                    将其从队列中移除并将编辑
//                                    后的版本重新排队到末尾
//                                    处。
//   Alt+↓ ComposerQueuePeekNext — 加载
//                                    下一个排队项目；在队列末尾，
//                                    恢复实时草稿并退出查看模式。
//   Alt+Backspace（在没有活动的 peek
//          的空作曲器上）ComposerQueuePopLast-
//                                    将最近排队的项目完全从 末尾 中弹出。
//                                    撤消意外队列提交。
struct ComposerQueuePeekPrev {};
struct ComposerQueuePeekNext {};
struct ComposerQueuePopLast {};

// Word-wise光标跳转(Ctrl+左/Ctrl+右)。
// 单词边界是空格； 标签 占位符算作单个单词。
struct ComposerCursorWordLeft {};
struct ComposerCursorWordRight {};
// Kill-line家族(Ctrl+K/Ctrl+U)。 Kill-to-end 从光
// 标处删除到下一个'\n'（或end-of-buffer）；
// kill-to-beginning 删除从前一个'\n'（或start-of-buffer）到光标的内容。
struct ComposerKillToEndOfLine {};
struct ComposerKillToBeginningOfLine {};
// Word-wise删除(Ctrl+W/Alt+D)。 Back 删除
// 从上一个字边界到光标处（readline `unix-word-rubout`）；
// 前进从光标删除到下一个字边界（`kill-word`）。
// 两者都重用光标跳转使用的相同标签-aware
// word_left/word_right边界，因此在标签上的delete
// -word会一次性删除整个附件标记。
struct ComposerDeleteWordBack {};
struct ComposerDeleteWordForward {};
// 撤消/重做(Ctrl+Z/Ctrl+Y)。每个变异 Composer 将
// 快照 的先前状态放入 per-composer 堆栈中；新编辑清除重做。
struct ComposerUndo {};
struct ComposerRedo {};
// 历史行走 — ↑/↓ 超过活动 线程 中的先前用户消息。
// Prev 进一步回顾过去；接下来回到现场
// 草稿（这是在第一个上一页上拍摄的快照）。
struct ComposerHistoryPrev {};
struct ComposerHistoryNext {};
// 显式"paste image from system clipboard" (Ctrl+V)。
// 带括号的粘贴 (Ctrl+Shift+V) 仅包含 UTF-8 文本 — 对于图像-on-
// 剪贴板路径，状态更新函数 会转至 wl-paste / xclip / pngpaste
// / PowerShell 以捕获原始 PNG 字节。参见 io/clipboard.
// {hpp,cpp}。
struct ComposerImagePasteFromClipboard {};

// ── 供应商提供的流式传输 ──────────────────────────────────────────────
struct StreamStarted {};
struct StreamTextDelta { std::string text; };
// 当 传输 关闭 TEXT 内容块（对于非 -tool 块来说是
// Anthropic content_block_stop）时发出。
// 始终在 tool_use content_block_start 之前，
// 因此它是最早的权威 "model finished typing prose"
// 信号 - 状态更新函数 翻转 in-flight 消息的 text_block_closed，
// 因此视图在推送任何工具卡之前将 逐步显示 光标排到边缘（杀死文本→工具
// 接口边界 处的 逐步显示 突发）。如果没有打开文本块，则为 No-op。
struct StreamTextBlockClosed {};
struct StreamToolUseStart { ToolCallId id; ToolName name; };
// 附加-only 传输片段，始终由稳定工具-call id寻址。
// Anthropic、Codex响应和OpenAI聊天在调用打开时都公开ID；
// 每次更新时都携带它可以使 overlapping/parallel 数据流 安全，
// 并删除 状态更新函数 的提供程序-ordering 回退。
struct StreamToolUseDelta { ToolCallId id; std::string partial_json; };
// 完整的当前参数快照。 ACP tool_call_update.rawInput
// 具有替换语义（它不是文本增量），因此保持这种不同可以
// 防止重复的 快照 产生串联的无效 JSON。
struct StreamToolUseSnapshot { ToolCallId id; std::string json; };
// 还明确地解决了完成。 empty/default id 不是有效的
// 传输 事件：适配器必须保留在 调用 启动时收到的 ID。
struct StreamToolUseEnd {
    ToolCallId id;
    // OpenAI-compatible `extra_content` 看到了这个 调用
    // (Gemini 的思想签名存在于此)。存储在 ToolUse 上并在下一回合发回。
    nlohmann::json extra_content = nullptr;
};
// 由委托代理执行的工具的终端结果（外部ACP)。
// 它遵循StreamToolUseEnd，因此共享参数汇编
// 器仍然将解析作为最终输入，然后将卡标记为终端，
// 而不第二次调度主机的工具执行器。
struct StreamObservedToolResult {
    ToolCallId id;
    bool failed = false;
    std::string output;
};
// 助理思维块的一大块（适应性思维）。 `text` 为可见
// 推理增量（显示下为空：省略）； `signature` 是
// 不透明的 per-block 签名，在块末尾附近到达一次。
// 状态更新函数将两者累积到in-flight辅助消息上，因此可以在下
// 一回合中逐字重放该块（当该回合还携带tool_use时，
// Anthropic需要它）。兼作活跃心跳 - 处理函数 像
// StreamHeartbeat 一样碰撞 last_event_at。
//
// `block_boundary`标记NEW推理block/paragraph的START:
// Anthropic在每个思维content_block_start
// 上发出它（交错思维为每个响应产生几个签名块——合并
// 它们会破坏签名重放），
// 并且Codex/Responses在每个新的推理摘要部分/推理项（
// 否则将连接成run-together散文的段落边界）上发出它。
// 状态更新函数 密封了前一个块并插入了显示分隔符。
struct StreamThinkingDelta {
    std::string text;
    std::string signature;
    bool block_boundary = false;
    // Non-empty=完整的REDACTED思维块(Anthropic安全加密；不透明)。
    // 状态更新函数 将其存储为自己的 ThinkingBlock，
    // 因此 传输 会逐字重播它 - 在 tool_use 之前需要。未渲​​染。
    std::string redacted_data;
    // 仅聊天传输：文本到达的字段("reasoning_content"
    // 或"reasoning")。记录在消息中，以便下一回合以相同
    // 的名称发送回推理，就像 GitHub 的客户端所做的那样。
    std::string wire_field;
};
// Codex/Responses推理-item捕获。
// 与 StreamThinkingDelta（携带思维块的可见摘要文本）不同，
// 它携带来自已完成推理输出项的 OPAQUE `encrypted_content` blob。
// 状态更新函数将其隐藏在in-flight助手消息上，以便follow-up回合可以在
// `input[]`中重播它，并在store:false下跨工具轮次保存chain
// -of-thought。从未渲染过； 传输-only。
// 后端有一个不透明的 ACCOUNT-SCOPED 推理块要求我们在下一回合重播。
// `site` 记录了哪个主机铸造了它（responses::Site::id）。
//
// 配对是重点：从一个后端发送到另一个后端的密文是整个
// 请求失败的 400，而不是服务器忽略的字段（
// openclaw#72602）。带着 载荷 的起源是
// 决定重播而不是假设的原因——两者一起旅行，
// 或者 不变量 只是一个评论。
struct StreamReasoning { std::string encrypted; std::string site; };
// 镜子Anthropic的message.usage形状。仅当请求命中
// cache_control 断点时，cache_* 字段才为非-zero。字段默认为 0，
// 因此只关心 input/output 的调用者可以继续工作。
struct StreamUsage {
    int input_tokens               = 0;
    int output_tokens              = 0;
    int cache_creation_input_tokens = 0;
    int cache_read_input_tokens    = 0;
    // Reasoning/thinking代币计费为WITHIN output_tokens（
    // OpenAI将它们计入completion_tokens_details.reasoning_tokens/output_tokens_details.reasoning_tokens；
    // Anthropic不将它们分开）。纯粹提供信息 - 是否将 不
    // 添加到 output_tokens 以进行 cost/ 上下文数学，
    // 它们已经包含在那里。 0 当 传输 省略详细信息时（非
    // -reasoning 型号，或未报告该详细信息的提供商）。
    int reasoning_output_tokens    = 0;
};
// 数据流为何结束。将 Anthropic 的 传输 字符串（
// `message_delta.delta.stop_reason`："end_turn"
// | "tool_use" | "max_tokens" | "stop_sequence" |
// 不存在）映射到封闭的 枚举 中，以便 状态更新函数 可以在其上进行 `switch` 而无需进行
// 字符串比较。 传输 无法识别的任何内容（forward-compat：未来 Anthropic
// 停止原因）都会变成 `Unspecified` — 与缺失字段的处理方式相同，
// 这意味着 "treat as a clean stream end."
enum class StopReason : std::uint8_t {
    EndTurn,        // 模型自然完成
    ToolUse,        // 模型在继续之前需要工具结果
    MaxTokens,      // 达到产出代币上限 mid-stream
    StopSequence,   // 匹配配置的stop_sequence
    // 安全分类器拒绝了该请求。到达时为 NORMAL HTTP 200，
    // 带有井 -formed 数据流——一个思考块，一些输出令牌，
    // 并且根本没有内容块。 传输层 看起来没有任何问题，
    // 这正是为什么它需要自己的价值：折叠到 Unspecified 中，
    // 它是 "treat as a clean stream end"，
    // 并且一个干净的结尾，没有内容 渲染 作为一个空气泡。
    // 现场观察：out=4 的转弯，没有文字，没有工具 调用，也没有可见的解释。
    Refusal,
    // 响应填充了模型的上下文窗口（与请求自己的
    // max_tokens 上限相对，即 MaxTokens）。
    // 相同的截断语义，不同的原因，用户需要的修复也不同：
    // 提高max_tokens对此无济于事。
    ContextExceeded,
    Unspecified,    // 传输 不存在、为空或未知
};

[[nodiscard]] constexpr std::string_view to_string(StopReason r) noexcept {
    switch (r) {
        case StopReason::EndTurn:      return "end_turn";
        case StopReason::ToolUse:      return "tool_use";
        case StopReason::MaxTokens:    return "max_tokens";
        case StopReason::StopSequence: return "stop_sequence";
        case StopReason::Refusal:      return "refusal";
        case StopReason::ContextExceeded:
            return "model_context_window_exceeded";
        case StopReason::Unspecified:  return "";
    }
    return "";
}

// 逆：将解析一个传输字符串转换成带类型的 枚举。
// 用于`provider/anthropic/transport.cpp`中的动态边界。
// 无法识别的值会变成 `Unspecified`，因此将来添加 Anthropic 不会
// 导致 状态更新函数 崩溃。
[[nodiscard]] constexpr StopReason parse_stop_reason(std::string_view s) noexcept {
    if (s == "end_turn")      return StopReason::EndTurn;
    if (s == "tool_use")      return StopReason::ToolUse;
    if (s == "max_tokens")    return StopReason::MaxTokens;
    if (s == "stop_sequence") return StopReason::StopSequence;
    if (s == "refusal")       return StopReason::Refusal;
    if (s == "model_context_window_exceeded")
        return StopReason::ContextExceeded;
    // pause_turn:服务器-tool采样循环达到其迭代限制。
    // 我们没有发送服务器工具，所以今天无法到达；当可以时，
    // 它需要 RESUME 语义（将助手内容发回），
    // 而不是停止。故意未映射而不是赋予任何未实现的值。
    return StopReason::Unspecified;
}

struct StreamFinished { StopReason stop_reason = StopReason::Unspecified; };

// 数据流-level失败。 `message` 是 human-readable（用于
// 状态横幅和 `provider::classify(string)` 回退）。
// `retry_after` 是服务器的 重试-After 提示（如果存在）—
// Anthropic 将其设置为 429 (rate_limit_error) 和 529
// (overloaded_error)；与硬编码的 退避 时间表相比，运行时 更喜欢这个，
// 因为服务器比我们更清楚 Brown-out 会持续多久（参见 Zed 的
// `parse_retry_after`、anthropic.rs:574-580）。
// std::chrono::seconds 因为 Anthropic 总是发出整秒；
// 固定在使用地点，因此有缺陷的代理在一个小时内无法锁定我们。
struct StreamError {
    std::string message;
    std::optional<std::chrono::seconds> retry_after;
    // 当故障是 HTTP-status 错误时观察到的 HTTP 响应状态（当
    // 不是 1 时为 0 — transport/socket 故障、
    // 综合停顿或没有可用状态的 SSE `event: error` 正文）。
    // 传输层 已经是 KNOWS 了；携带它可以让 重试 状态更新函数 通过 带类型的、
    // 编译-time-proven provider::classify
    // (HttpError) 路径而不是子字符串 -sniffing 对人类
    // `message` 进行分类。 Non-zero ⇒ 按状态分类；
    // 零⇒回落到字符串嗅探。这就是在不改变Msg-driven 运行时 接口边界的情况下，
    // StreamResult的精度达到状态更新函数的方法。
    int http_status = 0;
    // 服务器在传输层失败之前接受了非-idempotent请求；
    // 重试 可以复制型号 work/cost.
    bool non_replayable = false;
    // 仅对于 SYNTHETIC StreamError 为真，
    // 停顿 看门狗 在触发 取消 令牌后调度（请参阅 meta.cpp 勾选）。
    // 处理函数 必须将 `from_stall` 错误视为可恢复的 上游
    // 停顿（将产生的 "cancelled" 工作线程 展开重新分类为瞬
    // 态） - 但它不得 从易失性 `in_stall_fired()`
    // 阶段状态推断出该意图，因为稍后的转向可以重新 -enter
    // StallFired，并且在该窗口中重合的真正 user
    // -cancelled 错误将是错误的重新分类。携带意图 ON 的
    // 消息将其与产生它的特定停顿联系起来，不受其间运行的其他
    // turns/retries 的相位搅动的影响。
    bool from_stall = false;
};
// 传输-alive 心跳。 Model/SSE 心跳重置瞬态 重试 历史记录；
// 传输层-only 活动（例如 HTTP/2 PING ACK 字节）
// 仅防止 120-second UI 看门狗 将缓冲公司网关错误
// 分类为已失效。
struct StreamHeartbeat {
    bool transport_only = false;
};
// 现场中介正在保留响应DATA。与通用心跳不同的是，
// 这是 user-visible：状态更新函数 使有界请求保持
// 活动状态，并解释了为什么输出可能会一次性到达而不是增量到达。
struct StreamBufferedWait {};
// 用户-driven 取消的in-flight 数据流 (Esc同时流式传输)。
// 状态更新函数 使 StreamState 取消 令牌跳闸； http 层在大约 200
// 毫秒内发出通知，工作线程 线程 最终发出 StreamError("cancelled")。
struct CancelStream {};
// 在瞬态-error 退避（过载/429/5xx/网络信号）之后安排in-flight
// 数据流的re-launch。 状态更新函数 从 StreamError 处理函数
// 发出 `Cmd::after(delay, RetryStream{})`；
// 当这个 Msg 触发时，数据流 在相同的上下文中是 re-launched。
// 用户在等待期间可以通过Esc→CancelStream进行拦截。
struct RetryStream {};

// ── Tool 执行(当地) ───────────────────────────────────────────────
// Tool执行完毕。 `result` 是 `expected<output_text, ToolError>`
// — success/failure 的区别是类型，而不是并行的 `bool error` 标志。
// 状态更新函数 通过 `std::visit` 发送（或常见情况下的
// `if (e.result)` 缩写形式）； 带类型的 `ToolError::kind`
// 一直流向视图，在那里它可以驱动每个类别不同的​​ 渲染。
struct ToolExecOutput {
    ToolCallId id;
    std::expected<std::string, tools::ToolError> result;
    // 结构化文件系统突变文件-touching工具（编辑/写入/apply_patch/替换）
    // 与其文本一起返回； std::nullopt 适用于所有其他工具。
    // 状态更新函数 将其附加到 m.d.pending_changes，
    // 以便 diff-review 面板 (Ctrl+R) 可以在保留 差异块
    // 之前行走/接受/拒绝它们。从 mcp 桥 (decode_result) 中工具的
    // FileChange 元解码，通过 diff::compute 重建结构化 差异块。
    std::optional<FileChange> change;
    // Multi-file 编辑（替换）——每个写入文件一个条目。当非-empty时，
    // 它取代`change`进行排队； `change` 仍然是 single
    // -file 兼容性的第一个元素。
    std::vector<FileChange> changes;
    // 对视觉模型出现的工具进行成像（在图像文件上读取）。 状态更新函数 将它们存储在
    // ToolUse::Done 上，因此 传输 渲染 将它们作为图像块存储在 调
    // 用 的 tool_result 中。 text-only 工具为空。
    std::vector<ImageContent> images;
    // 哪个执行产生了这个结果(ToolUse::Running::exec_seq)。
    // Tool id在轮次中不是唯一的（网关和我们自己的合成id在
    // call_0处重新启动），因此比取消寿命更长的工作线程不得
    // 将其输出放置在重用该id的新调用上。 0 = 未标记。
    std::uint64_t exec_seq = 0;
};
// 包含FULL累积输出，而不是增量——更新处理函数可以无
// 条件分配而不维持附加状态。在子进程边界（约 100 毫秒）合并，
// 因此繁琐的命令不会用 micro-updates
// 淹没 事件 队列。
struct ToolExecProgress {
    ToolCallId id;
    std::string snapshot;
    std::uint64_t exec_seq = 0;   // 参见ToolExecOutput::exec_seq
};
// Wall-clock 看门狗用于工具执行。当非 -subprocess
// 工具转换为正在运行时，由 kick_pending_tools 通过
// Cmd::after 进行调度。如果在检查触发时工具已达到 终端 状态，
// 则这是 no-op；否则该工具是force-failed，
// 因此UI不会永远位于挂起的文件系统调用/阻塞的系统调用上。拥有该工具的 工作线程
// 线程 可能会继续运行 - 它最终的 ToolExecOutput 会被
// apply_tool_output 的 幂等 守卫默默丢弃。
struct ToolTimeoutCheck { ToolCallId id; };

// 来自用户的权限-prompt分辨率。与 ToolMsg 相关，
// 因为 权限 提示词 始终与特定的 待处理 工具 调
// 用 有关，并且分辨率直接反馈回工具状态机。
struct PermissionApprove {};
struct PermissionReject {};
struct PermissionApproveAlways {};

// ── Model 目录 国 务 院 ────────────────────────────────────────────────────────
// 后台模型-catalog获取的结果。 `provider_id`
// 是获取的提供者的规范 ID 为 FOR（在获取启动时捕获）：
// 如果活动提供者发生了变化，状态更新函数就会删除载荷——如果没有它，
// 在切换到B之后，提供者A登陆的in-flight获取会在B下安装A的目录（
// 从中选择然后带有A-model id的数据流 B：
// "model changed, provider didn't"错误）。
struct ModelsLoaded {
    std::vector<ModelInfo> models;
    std::string            provider_id;
    // Non-empty⇒获取FAILED，这是人类-readable的原因。
    // 此处携带（不是作为 StreamError)，
    // 因为 StreamError 提供 LIVE TURN 的 重
    // 试 状态机：当 数据流 处于活动状态时，作为 StreamError
    // 分派的 目录 故障将弹出接收增量的辅助消息、调度 RetryStream
    // (将第二个 工作线程 竞争到会话中）或锁定健康状态机转向
    // 终端 — 所有这些都是与 数据流 无关的错误。 状态更新函数 将
    // 其显示为短暂状态 toast。
    std::string            error;
};

// ── 折叠的十字架 -provider 模型 选择面板 ────────────────────────────────────
// 跨越EVERY认证提供商（docs/design/
// unified-model-picker.md)的统一列表。每行都是用户自动
// 切换到的具体（提供者、模型），或者路由到登录的 "sign in to X" 提供。
// 使用 `^/` 和 `/model` 斜杠命令打开。
//
// 这是ONE模型表面。它还提供智能模式角色→模型分配：
// 当打开的模型面板带有 assign_slot 时，
// 列表的
// 范围限定为活动提供者，并且选择写入插槽而不是切换。
struct OpenModels {};
// 延迟的 惰性 刷新：在打开时发出 活动 提供者的快速重新获取之后，
// 这会在每个 OTHER 授权的提供者的实时 目录 上触发（通过
// 短暂的延迟后）到背景 -refresh。将它们排除在初始批次
// 之外意味着活动提供程序的结果不会在有限的 工作线程 池 上排在较
// 慢的提供程序后面 - 所选模型首先刷新，其余模型慢慢进入。
struct FusedRefreshOthers {};
// re-probing ONE模型在本地端点上的实时窗口的结果（参见
// cmd::probe_model_window)。一次驻留一个型号的
// llama.cpp 路由器仅报告 已加载 型号的大小，因此在切换后和每次转动后，
// 窗口都会重新-checked。当服务器无法说出时，`window` 为
// 0（型号还不是 已加载）； 状态更新函数 然后保留它所拥有的。
struct ModelWindowProbed {
    std::string provider_id;
    std::string model_id;
    int         window = 0;
};
// ^L-强制完全实时刷新：标记EVERY授权提供者的目录
// 过期并重新获取（立即有效，其他推迟）。当 目录 为
// stale/failed 并且用户不想等待 TTL 或重新打开时，
// 手动逃生舱口。在获取过程中显示加载提示。
struct ModelsRefresh {};
struct CloseModels {};
struct ModelsMove { int delta; };
struct ModelsJump { enum class Where { Home, End, PageUp, PageDown }; Where where; };
struct ModelsSelect {};        // 原子切换到突出显示的行
struct ModelsToggleFavorite {};
// ←/→循环高亮模型的推理-effort层； ^E 切换 思考 on/off.
// 融合的 选择面板 是 COMPLETE "pick + tune your model"
// 表面 — 没有第二个 选择面板。
struct ModelsCycleEffort { int delta; };
// 将突出显示的模型的CONTEXT WINDOW超越通过常见
// 尺寸的梯子，回绕到"auto"。
//
// 故意循环而不是文本提示词：选择面板是具有实时过滤器的列表，
// 因此数字输入模式必须从过滤器中窃取键盘，然后将其交还。
// 梯子涵盖了网关实际服务的内容，"auto"（无
// 覆盖）是梯子上的一个梯级 - 因此控制是可逆的，
// 无需单独的明确操作。
struct ModelsCycleContext { int delta = +1; };
struct ModelsToggleReasoning {};
// ^/ — 将 browse/filter 列表的范围限定为 仅，当前突出显示的行的提供
// 者（以便您可以深入到 "just this provider's models"）。
// 当已经确定范围时再次按下它会清除范围（返回到所有提供者）。
// 当突出显示的行没有提供商（报价）时，为 no-op。
struct ModelsScopeProvider {};
struct ModelsFilterInput { char32_t ch; };
struct ModelsFilterBackspace {};
// 一个已授权提供商的目录已解析（异步，打开时每个提供商一个）。
// `provider_id`防范供应商在-fetch中期签出； `ok=false`
// 标记该组失败。就地合并到 Model::d.provider_catalogs。
struct FusedCatalogLoaded {
    std::string            provider_id;
    std::vector<ModelInfo> models;
    bool                   ok = true;
    std::string            error;   // 如果实时获取失败的话，为什么会失败
};
// ^Tab MRU循环：从最近的(提供商，模型)环走到逐渐旧的
// 模型(A→B→C→D→A)，没有覆盖面板。重用atomic
// -switch分辨率，但NOT会对MRU重新排序，
// 因此重复按下会循环整个环，而不是切换最后两个环。
struct SwitchToPreviousModel {};
// ── Provider 选择面板 国 务 院 ──────────────────────────────────────────────────────
// 镜像型号 选择面板。选择活动后端的供应商 live-switches
// （provider::select + deps() 接口边界 交换），
// 保留该选择，并启动新的模型获取，以便模型列表反映新的后端。
struct OpenProviders {};
struct CloseProviders {};
struct ProvidersMove { int delta; };
struct ProvidersJump  { enum class Where { Home, End, PageUp, PageDown }; Where where; };
struct ProvidersSelect {};
// 提供商列表上的实时搜索-filter（镜像模型选择面板）：带类型的
// 字符附加到查询，Backspace修剪它。行列表缩小到 id +
// 标签 + 简介上的 fuzzy/substring 匹配项。
struct ProvidersFilterInput { char32_t codepoint; };
struct ProvidersFilterBackspace {};
// ^D 连续 — 两个 -press 删除（confirm_remove)：从设置中
// 完全删除 SAVED CUSTOM HOST，OR 退出具有 已保存 密钥的 PRESET
// （清除密钥；内置 -in 预设保留）。镜子 ThreadListDelete /
// AccountRemove。 No-op 在没有 已保存 键、ACP 和哨兵的预设上。
struct ProvidersDelete {};
// "N more providers" 路标上的 ^A
// 或 Enter — 显示您配置了 不 的提供商，
// 浏览视图会折叠。展开将光标停放在第一个显示的行上：您要求查看它们，
// 因此回报应该位于光标下方，而不是您碰巧所在的位置。
struct ProvidersToggleAll {};

// ── 线程 列表 ──────────────────────────────────────────────────────────
struct OpenThreadList {};
struct CloseThreadList {};
struct ThreadListMove { int delta; };
// 长线程历史的Absolute-jump导航。参见 ModelsJump。
struct ThreadListJump  { enum class Where { Home, End, PageUp, PageDown }; Where where; };
struct ThreadListSelect {};
// Tab/Shift+Tab：步进范围全部→该文件夹→Codex&Claude→全部。
struct ThreadListScopeToggle { int delta = +1; };
// ← → 滚动浏览一圈； Shift 跳转到第一个/最新的。
struct ThreadPeekScroll { int delta; bool to_end = false; };
// 如果光标仍然存在，则针对 `id`:加载 触发结算定时器。
struct ThreadPeekSettled { ThreadId id; };
// 被偷看的线程完成对状态更新函数的加载，已经切入卡的回合。
// 当 加载 失败时，`turns` 为空。
struct ThreadPeekLoaded {
    ThreadId id;
    std::optional<std::vector<ui::PeekTurn>> turns;
};
// 键入按标题过滤 线程 列表。
struct ThreadListFilterInput     { char32_t ch; };
struct ThreadListFilterBackspace {};
// ^D中的线程 选择面板：将两个-press与confirm_remove删除。
// 镜像所建立的SettingsListRemove/AccountRemove模式：
// 第一次按下将聚焦的线程标记为待处理-delete (视图中的警告标记），
// 第二次按下SAME行返回DeleteThread 副作用。
// 任何move/select/new/close解除待处理状态。
struct ThreadListDelete {};
// ^R中的线程 选择面板：就地编辑焦点行的标题。 Start 打开
// 编辑器 pre-filled，当前标题为 光标；
// Char/Backspace/Delete/Paste 在 光标 处编辑，
// 光标移动它；提交存储它（空或不变= 取消）； 取消 (Esc)
// 保持原样。
struct ThreadListRenameStart     {};
// `/rename <title>`:将打开的线程重命名为不带选择面板。
struct RenameCurrentThread       { std::string title; };
struct ThreadListRenameChar      { char32_t ch; };
struct ThreadListRenameBackspace {};
struct ThreadListRenameDelete    {};
struct ThreadListRenameCursor    { enum class To { Left, Right, Home, End }; To to; };
struct ThreadListRenamePaste     { std::string text; };
struct ThreadListRenameSubmit    {};
struct ThreadListRenameCancel    {};
// Quick-cycle:切换到相邻的线程 (按新近顺序，
// 与^J 选择面板显示的顺序相同)WITHOUT打开选择面板。
// 增量应用于 线程-list 索引：+1 = 较旧，-1 = 较新；
// 两端缠绕。全局绑定到 Alt+←/→，当作曲家为空并且没有激活代理回合时，
// 绑定到 Ctrl+←/→（否则 Ctrl+箭头保持跳跃 -by-word）。
// 状态更新函数 会显示 "thread k/N · title" 吐司，
// 因此用户始终知道他们降落在哪里。
struct ThreadCycle { int delta; };
struct NewThread {};
// 后台线程的结果-history负载开始于
// `AgenttyApp::init()`。
// 上-disk线JSONwalk 用于在启动时同步运行；与数百个多-MB文件
// （真实的-world用法）它是主要的启动成本（376 线程下 643
// 个线程约为 1.7 秒）MB全部的）。现在`init()`立即
// 返回一个空值`m.d.threads`和一个`Cmd::task`
// 这就是目录行走+JSON解析掉UI线;这Msg完成后着陆。
struct ThreadsLoaded    { std::vector<Thread> threads; };
// Codex/Claude 在此机器上找到代码会话。
struct ForeignSessionsLoaded { foreign::Scan scan; };
// 一批老线程在后台填写了他们的选择面板摘要（预览、
// 消息计数）。仅限 头文件。
struct ThreadSummariesFilled { std::vector<Thread> threads; };
// 模型在第一次回复后为线程写的简短标题。
// 为空`title`表示失败； 线程 保留其原有标题。
struct ThreadTitleGenerated { ThreadId id; std::string title; };
// 作者拒绝了线程 保存：它是从未到达磁盘的基础上的增量。
// 状态更新函数 忘记了底座，所以下一个 保存 是完整的。
struct SaveRefused { ThreadId id; };
// 当用户在线程 选择面板中的一行上点击Enter时，
// 后台单-thread 加载的结果开始。同步 加载 是 JSON
// 解析 的约 30 毫秒 + 实际 线程 上约 3 毫秒的再水合（
// 786+ 消息）——单独很小，但直接应用于按键和下一个油漆之间，
// 它显示为可见的 "click and wait"。 选择面板
// 现在立即关闭，旧的 线程 保留在屏幕上，当新的 线程 的 字节
// 被解析时，这个 Msg 落地；然后 状态更新函数 交换
// `m.d.current`，重新水化冻结的前缀，并一次性提交
// 回滚缓冲区-overflow 接口边界。
struct ThreadLoaded     { Thread thread; };

// ── 命令调色板 ──────────────────────────────────────────────────────
struct OpenPalette {};
struct ClosePalette {};
struct PaletteInput { char32_t ch; };
// 当 FILTER 面板拥有键盘（调色板、型号、提供商、提及、
// 符号）时进行括号粘贴：文本位于该面板的查询中，
// 而不是其背后的作曲家。一条消息；无论哪个面板打开，
// 状态更新函数 都会处理它（路由器以开放类型为目标，状态更新函数
// re-verify — 与每个输入路径相同的过时规则）。
struct PanelFilterPaste { std::string text; };
struct PaletteBackspace {};
struct PaletteMove { int delta; };
struct PaletteSelect {};

// ── @ file 提及 选择面板 ────────────────────────────────────────────────
struct CloseMention {};
struct MentionInput { char32_t ch; };
struct MentionBackspace {};
struct MentionMove { int delta; };
struct MentionSelect {};

// ── #符号 选择面板 (与 @ file 并行) ────────────────────────────────────
struct CloseSymbol {};
struct SymbolInput { char32_t ch; };
struct SymbolBackspace {};
struct SymbolMove { int delta; };
struct SymbolSelect {};

// ── 代码 -block 选择面板 (Ctrl +G —— 运行 AI-suggested 命令) ──────────
// Open scans the newest assistant reply for fenced ``` blocks; the modal
// 列出它们并且用户选择每个块的操作：
//   选择（Enter /数字）→通过/bin/sh运行块，
//     异步——命令+输出落在线程中，以便模型在下一轮看到结果。
//     `index` ≥ 0 针对特定行（数字快捷方式）； -1 表示
//     "the cursor row"。
//   编辑 → 将清理后的 正文 暂存到编辑器中（调整，
//     然后 Enter 作为普通消息提交 — 或者用户删除它，无论如何）。
//   复制→清理后的正文到系统剪贴板。
// RunFinished是携带捕获输出的运行完成；
// 它打开 RESULT 卡（退出代码 + 末尾 预览），
// 用户在其中决定：a = 作为输出 标签 附加到作曲家，y = 复制干净，
// Esc = 丢弃（实时转录已位于 回滚缓冲区 中）。
struct OpenCodeBlocks {};
struct CloseCodeBlocks {};
struct CodeBlocksMove { int delta; };
// 选择代码块。 `index` 缺失 = 作用于光标下的行 (Enter)；
// Engage = 直接用 1-9 数字键命名一行。
// 它是`int = -1`，所以"no override"和"row -1"
// 是相同的值，每个读者都必须记住它的含义。
struct CodeBlocksSelect { std::optional<int> index; };
struct CodeBlocksEdit {};
struct CodeBlocksCopy {};
struct CodeBlockRunFinished {
    std::string command;
    std::string output;
    int         exit_code = 0;
    bool        timed_out = false;
};
struct CodeBlockResultAttach {};
struct CodeBlockResultCopy {};
struct CodeBlockResultDiscard {};

// ── Tool-output 查看器 ───────────────────────────────────────────────────────
// Ctrl+O可滚动覆盖面板，用于读取任何固定工具调用
// 的FULL存储的输出——时间线卡省略长正文 (
// head/tail窗口)并且将它们扩展到位将重写提交的
// 回滚缓冲区行，因此检查发生在严格绘制在实时可视区域上的覆盖面板中。
// 打开快照的条目；移动驱动列表光标（或查看阶段的正文
// 滚动条）； Select进入正文阶段；
// 关闭=Esc（正文→返回列表，列表→关闭）；复制 = y.
struct OpenToolOutput {};
struct CloseToolOutput {};
struct ToolOutputMove { int delta; int pages = 0; };   // pages: PgUp/PgDn
struct ToolOutputSelect {};
struct ToolOutputCopy {};
// ←/→：在正文阶段，直接跳到previous/next条目的输出，
// 不跳列表；在列表阶段 ←/→ 未绑定（查看器
// 在打开时吞下所有键，因此它们只是 no-op）。
struct ToolOutputStep { int delta; };
// 在正文中查找：/打开提示词，边走边输入搜索，
// Enter保留查询，Esc删除它，n/N转到下一个/上一个匹配。
struct ToolOutputFind {
    enum class Op : std::uint8_t { Open, Type, Erase, Commit, Cancel, Next, Prev };
    Op       op;
    char32_t ch = 0;   // 对于类型
};

// ── 待办 弹出面板 ───────────────────────────────────────────────────────────
struct OpenTodoModal {};
struct CloseTodoModal {};

// ── 智能模式覆盖面板 ───────────────────────────────────────────────────
// 用于角色-based路由（docs/design/smart-mode.md)的专用配置覆盖面板。
// A 选择面板-style 弹出面板:第0行是主使能切换，第1-3行是战略/实施/实用槽。
// 第 0 行上的 Enter 翻转已启用；插槽行上的 Enter 在
// slot-assign 模式下打开模型 选择面板。所有更改均保留至
// settings.json。
//
// ONE关键信息，如检索面板：导航，主控切换和选择
// 面板手-off都是由共享表单层解析的，
// 所以这个面板不能偏离那个。
// `focus`:将光标放在该行id上打开(设置搜索)。
// 空=通常的第一行。
struct OpenSmartMode { std::string focus; };
struct CloseSmartMode {};
struct SmartModeKey { form::keys::Action action; };
// 在编辑智能模式字段时加括号的粘贴——落在该字段的
// 缓冲区中，而不是面板后面的作曲家。
struct SmartModePaste { std::string text; };
// Reveal/hide高级路由-policy行(^A)。
// 查看覆盖面板上的状态； 面板 重建。没有坚持。
struct SmartModeAdvanced {};
struct SmartModeClearSlot {};     // 插槽行上的'x'：将其重置为自动

// ── 网络搜索( 设置 ) – Web搜索 ) ─────────────────────────
// web_search工具的策略：on/off、结果计数、上限、
// 排除站点。一种形式 面板，由设置 注册表 的 web_search.
// * 行构建；每个提交的编辑都会保留并作为 副作用 发布到
// 工具层。
//
// ONE关键信息如智能模式和检索：导航、
// 切换和文本编辑均来自共享表单层。
struct OpenWebSearch { std::string focus; };   // 焦点：行id，为OpenSmartMode
struct CloseWebSearch {};
struct WebSearchKey { form::keys::Action action; };
// Web 搜索 面板 打开时用括号括起来的粘贴。 状态更新函数
// 将其放置在聚焦的文本行上（开始编辑，就像打字一样）并将
// 其放在其他任何地方 - API 键是粘贴的字段。
struct WebSearchPaste { std::string text; };
// API-key存储是IO(密封文件，当OS密钥存储打开
// 时是`secret-tool`)，因此面板从不从状态更新函数
// 触及它：它在工作线程上读取和写入，并且这些将结果带回。
// 他们都没有带钥匙。 `gen`是开始读取时面板的keys_gen；
// 对较旧读取的回复（已被存储或删除取代的读取）将被
// 丢弃。
struct WebSearchKeysRead {
    std::uint64_t gen = 0;
    // 服务 ID -> "stored" | "env: AGENTTY_WEB_SEARCH_KEY_..." | ""（无）
    std::vector<std::pair<std::string, std::string>> origins;
};
struct WebSearchKeySaved   { std::string service; bool ok = true; };
struct WebSearchKeyRemoved { std::string service; bool ok = true; };

// ── 粘贴( 安排 ) – 粘贴) ────────────────────────────────────────
// Ctrl+V如何读取剪贴板。在粘贴上形成 面板。* 注册表 行，
// 如 Web 搜索：提交的编辑 保存 并重新安装配置。
struct OpenPasteSettings { std::string focus; };
struct ClosePasteSettings {};
struct PasteSettingsKey { form::keys::Action action; };
struct PasteSettingsPaste { std::string text; };   // 进入焦点文本行

// ── 插件编辑器( 详细 / 添加窗体) ──────────────────────────
// 从插件设置列表中打开：服务器行上的 `e` → 详细信息表单
// for THAT server; `a` → add form (kind-first: the kind choice rebuilds the
// 字段设置为用户选择stdio/http/sse/passthrough)。
// 与 SmartMode/Rag 相同的共享表单层 — 一条关键消息，
// 粘贴路由到焦点字段，通过导航关闭，以便 Esc 恢复设置列表。
struct OpenPluginEdit {
    std::string server;   // ""=添加模式
    bool        project = false;
};
struct ClosePluginEdit {};
struct PluginEditKey { form::keys::Action action; };
struct PluginEditPaste { std::string text; };

// ── 外观面板 ──────────────────────────────────────────────────────
// 事物的外观，作为一种形式：主题、颜色层、密度、运动、
// 镶边。每行都是 LIVE — 状态更新函数 会改变首选项，
// 将其保存到用户存储中，然后用它绘制下一个 帧。
// 外观设置屏幕不能有应用步骤：主题由 LOOKING 判断，
// 并且在选择和查看之间不得重新启动。
struct OpenAppearance { std::string focus; };  // 焦点：行id，为OpenSmartMode
struct CloseAppearance {};
struct AppearanceKey { form::keys::Action action; };
struct AppearancePaste { std::string text; };
// 主题浏览器，悬浮在面板上。它自己的消息，因为它是一个真正的 选择面板（57 个方案，
// 并且不断增长，模糊-searched）而不是一个下拉菜单 - 请参阅
// form.hpp 的 Pick-vs-Choice 规则。
struct AppearancePickTheme {};        // 打开浏览器上的Theme行
struct AppearanceThemeMove { int delta = 0; };
struct AppearanceThemeQuery { std::string text; };   // ""=退格一
struct AppearanceThemeCommit {};      // Enter:保留突出显示的方案
struct AppearanceThemeCancel {};      // Esc:恢复我们进来的主题

// ── 沙箱 面板 国 务 院 ─────────────────────────────────────────
// 命令可能涉及什么：文件系统范围、网络、系统调用配置文件、
// 资源上限和trust-handoff策略。
//
// 与外观不同的是，这个面板是NOT活体。在已经运行的命令下，
// 边界不得更改，而 half-applied 策略是无人能
// 推理的策略——因此编辑会在表单中累积，面板 显示它们
// WOULD 生成的墙（编译，而不是生成），
// SandboxSave 立即提交整个配置。这也是为什么这里有
// 保存 消息，而外观中却没有：主题是通过外观来判断的，
// 沙箱 是通过在信任它之前阅读它的承诺来判断的。
struct OpenSandbox { std::string focus; };     // 焦点：行id，为OpenSmartMode
struct CloseSandbox {};
struct SandboxKey { form::keys::Action action; };
struct SandboxPaste { std::string text; };
// 从当前表单重新编译预览。 状态更新函数 在任何编辑后
// 都会引发此问题，因此墙上报告永远无法描述行不再说明
// 的配置。
struct SandboxRefreshPreview {};
// 保留表单的配置并在会话的其余部分安装它。
struct SandboxSave {};

// ── 路径列表编辑器 ──────────────────────────────────────────
//
// 路径行("Also readable"、"Masked"、
// 允许的端口)是保存逗号-separated字符串的ONE文本字段。
// 这是一种序列化格式，而不是界面：您看不到一个条目的结束位置，
// 一个逗号会默默地更改策略，编辑四个路径中的第三个意味着
// 将光标移过其他三个路径。对于整个工作就是清晰易读的安全
// 控制来说，这是错误的形状。
//
// 因此，该行成为打开真实列表的选择：每行一个条目，
// 添加和删除作为离散行为。在编辑器提交之前，父表单保持不变，
// 因此 Esc 是真正的 取消。
struct SandboxEditList { std::string row_id; };   // 打开编辑器进行一行
struct SandboxListKey  { form::keys::Action action; };
struct SandboxListPaste { std::string text; };
struct SandboxListClose {};                       // 提交回到行中

// ── 内 -app 登录 弹出面板 ───────────────────────────────────────────────────
// 当用户启动 agentty 而没有有效的 凭据、
// OR 时在 -app 中显式触发以登录或添加账户时示出。
// 与其他模式相同的状态-machine风味：关闭→采摘→{​​oauth_code
// | api_key_input}→完成。异步 OAuth 交换在 工作线程 线
// 程 (Cmd::task) 上运行，并通过 LoginExchanged 报告。
struct OpenLogin {};
struct CloseLogin {};
// 登录子-modal中的Esc：弹出ONE级别的流程（键提示
// 词→主机输入→提供商选择面板→关闭）而不是折叠所有内容。
// 状态更新函数读取子-state的`back`来源字段。
struct LoginBack {};
// 异步自定义-host探测结果(工作线程→状态更新函数)。 `ok` 表示某处已
// 回答的型号列表； `models_path`/`native_api` 带有
// DETECTED 方言，因此提交采用服务器实际所说的内容。失败时，`error` 是人为
// 原因（"nothing listening"、"HTTP 401"，...）。
// attempt_id匹配HostProbing，因此过期结果被丢弃。
struct HostProbed {
    std::uint64_t attempt_id = 0;
    std::string   spec;
    bool          ok = false;
    std::string   models_path;   // 回答的路径
    bool          native_api = false;
    int           model_count = 0;
    long          latency_ms = 0;
    std::string   error;
    // 端点回答401/403：ADDRESS IS CORRECT，
    // 它只想要凭据。与其他故障不同，因为修复是 "paste a key"，
    // 而不是 "fix the URL" — 因此 状态更新函数
    // 将用户发送到密钥 提示词，而不是返回到已经正确的地址字段。
    // 如果没有这个，最常见的paid-provider入职（输入主机，获取
    // 401) 让你重新编辑 URL，你已经正确地得到了 带类型的。
    bool          needs_key = false;
    // 这位主持人是否告诉我们真实的上下文窗口，如果没有，
    // 是否是因为我们拒绝询问？
    //
    // 本地服务器是唯一知道其自己的RUNTIME窗口的东西（
    // llama.cpp的meta.n_ctx、LM Studio的
    // 已加载实例、Ollama的/api/ps)——没有目录可以。
    // 当我们得到一个数字时，在添加时间说出来是信任这个数字和怀疑它之间的区别。
    //
    // 当我们DON'T时，原因很重要。 `probe_skipped`
    // 意味着主机没有看到 self-hosted，因此额外的 round
    // -trips 被故意保留 - 这对于 api.openai.com
    // 是正确的，对于通过公开名称访问某人的 LAN 上的 GPU 框
    // 是错误的。这是唯一值得在用户正在寻找的地方提供修复的情况。
    int  window_tokens  = 0;      // 0=没有学到
    bool probe_skipped  = false;  // true=主机未分类self-hosted
    // 当位于 PUBLIC-looking 地址的主机无论如何应答 运行时 路由时
    // 设置 — i.e。尽管没有看，但它是 self-hosted。
    //
    // /props、/api/ps、
    // loaded_instances和meta.n_ctx不是托管API服务的东西，
    // 因此回答一个就是证明。记住这里的主机可以让以后的每个
    // 模型刷新也探测它，这就是否则需要设置 切换 的
    // 全部原因：服务器已经告诉我们它是什么，所以要求用户
    // 确认它会要求他们重复我们的答案。
    std::string learned_self_hosted;   // "" = 没有学到任何东西
};
// 退出ACTIVE提供者：清除其on-disk 凭据（Anthropic
// credentials.json或Codex/ChatGPT令牌存储），
// 通过update_auth将实时身份验证头文件清零，并重新-open标志-in
// 弹出面板，以便用户准确地到达他们开始新登录的位置。从命令面板 ("Sign out") 触发。
// 允许用户切换 OAuth ↔ API 密钥/帐户，而无需切换到 CLI
// `agentty logout`。
struct SignOut {};
// 打开ACTIVE提供商的in-app账户切换器。在提供商 选择面板
// 中输入活动的 Anthropic 或 ChatGPT 行会打开此列表，
// 因此提供商和账户选择保留在一个提供商-centric 流中。
// 它列出了每个 已保存 帐户，以便用户可以在不离开 agentty 的情况
// 下切换登录者。首次仅使用旧版单一登录名打开时，该登录名是 auto
// -registered 为 "default"，因此它显示为可切换行。
// 打开账户管理器。空`provider` = ACTIVE提供者（经典​​
// 的drill-down）；非 -empty id 会为 该 提供商打开帐户，
// 而无需先切换到该提供商 - 因此任何提供商行上的 Enter 都会显示其帐户，
// 而不是型号 选择面板。
struct OpenAccounts { std::string provider; };
// 移动帐户切换器中的突出显示(在两端环绕；最后一行始终是
// "+ Add another account…")。
struct AccountMove { int delta; };
// 激活突出显示的帐户切换器行。在 已保存 帐户上：将其存储的 凭据
// 交换到实时存储中 + re-install 身份验证 头文件。
// 在尾部的 add-new 行上：放入正常的拣货流程中，
// 并进行标记，以便以新名称捕获生成的登录信息。
struct AccountSelect {};
// 删除突出显示的已保存帐户（其存储的凭据 快照）。
// 如果它是活动帐户，则提供商的最新剩余帐户将变为活动帐户；
// 如果是最后一个，则切换器会退回到 Picking。
struct AccountRemove {};
struct LoginPickMethod  { char32_t key; };          // '1'=ApiKey，'2'=OAuth，'3'=ChatGPT
struct LoginCharInput   { char32_t ch; };
struct LoginBackspace   {};
struct LoginPaste       { std::string text; };
struct LoginCursorLeft  {};
struct LoginCursorRight {};
struct LoginSubmit      {};
// 当 OAuthCode 弹出面板 出现时，用户按下了
// "copy URL to clipboard" 键。 状态更新函数
// 发出带有主动授权 URL 的 cmd::write_clipboard，
// 然后显示一个简短的状态 toast，以便用户可以直观地确认已注册的击键。
struct LoginCopyAuthUrl {};
// 用户在设备-flow 弹出面板中按下copy-CODE键：复制one-time
// user_code（将带类型的的东西复制到浏览器中）而不是URL。
// 与 LoginCopyAuthUrl 不同，因此设备模式可以提供 两者 快捷方式。
struct LoginCopyCode {};
// 用户按下了"open browser again"键。
// Re-issues 与首次选择 OAuth 时触发的 xdg
// -open / `open` 调用相同，以防错过原始启动（alt
// -tabbed 在浏览器出现之前消失，或者 OS 默默地吞噬了第一个打开）。
// 幂等 — 重用已处于 OAuthCode 状态的 URL。
struct LoginOpenBrowserAgain {};
// 异步OAuth代码-exchange的结果。携带 带类型的
// `auth::TokenResult`，因此 状态更新函数 可以区分 ApiError
// / 网络 / MissingToken 而无需 解析 字符串。
struct LoginExchanged   { agentty::auth::TokenResult result; };
// 用于 Anthropic OAuth 登录的 PKCE 对，由 Cmd 创建（
// 随机 字节 不是可以更新的东西）。当 CSPRNG 失败时设置 `error`。
struct LoginOAuthMinted {
    agentty::auth::PkceVerifier verifier;
    agentty::auth::OAuthState   state;
    std::string                 authorize_url;
    std::string                 error;
};
// 一旦 OpenAI 分配了 one-time 代码，
// SSH/device 流程就调度该代码，同时同一 工作线程 继续轮询以获得批准。
struct CodexDeviceCodeReady {
    std::uint64_t attempt_id = 0;
    std::string verification_url;
    std::string user_code;
};

// 本机ChatGPT OAuth登录的最终结果。
// 该流程为本地环回回调或SSH-friendly设备授权；
// 两者都返回相同的 凭据 形状。 状态更新函数 仅在匹配活动尝试 ID
// 后才将其保留。
struct CodexLoginDone {
    std::uint64_t attempt_id = 0;
    std::expected<agentty::provider::chatgpt::CodexCredentials,
                  agentty::auth::OAuthError> result;
};

// 本机OAuth **device-flow**登录（GitHub Copilot、
// Kimi或任何未来的device-flow提供者）-Codex对的同级，但是PROVIDER-GENERIC。
// 一旦提供者发出one-time代码(因此弹出面板可以显示它)，工作线程就调度
// DeviceCodeReady，并且当用户批准(或者流fails/times出去)时，
// DeviceLoginDone就调度。 `provider` 是规范的 注册表 ID（"copilot"、
// "kimi"）。 工作线程 在分派 DeviceLoginDone 之前保留令牌本身，
// 因此 状态更新函数 只需要 success-or-error — 因此需要 `error`
// （nullopt = success）而不是提供程序-specific 令牌类型。
struct DeviceCodeReady {
    std::string   provider;
    std::uint64_t attempt_id = 0;
    std::string   verification_url;   // BARE url（显示在面板中，有一个代码字段）
    std::string   browser_url;         // pre-filled url（auto-opened；由`u`复制）
    std::string   user_code;
};
struct DeviceLoginDone {
    std::string                 provider;
    std::string                 provider_label;   // "GitHub Copilot" | "Kimi"
    std::uint64_t               attempt_id = 0;
    std::optional<std::string>  error;             // nullopt = 签署于 OK
};
// 当 `auth::resolve()` 返回与刷新令牌配对的过期令牌时，
// 后台 OAuth 刷新的结果从 init() 开始。与 LoginExchanged
// 相同的 TokenResult 形状，在 update/login.cpp::
// token_refreshed 中处理：成功通过 `update_auth`
// + 保存 将新信用安装到磁盘 + 排出任何排队的作曲家文本；失败会显示
// `error: token refresh failed: ...`
// toast 并保持队列不变，因此用户可以通过 in-app 登录 弹出面板
// 进行 重试。
struct TokenRefreshed   { agentty::auth::TokenResult result; };
// cmd::refresh_oauth_if_due看起来并且令牌未接近到期（或不可刷新）。
// 当询问时清除 in-flight 标志 和 状态更新函数 设置。
struct OAuthRefreshNotDue {};
// 主机re-read 凭据状态（参见LoadAuthView)。
// `provider_keys` 是现在的密钥保管库：帐户操作将其写入主机端，
// 因此回复将其带回 m.d.persisted。
struct AuthViewLoaded {
    agentty::auth::AuthView view;
    std::map<std::string, std::string> provider_keys;
};

// 主动pre-turn检索在-thread着陆。主动 RAG 在小墙
// -clock 树篱下的提交路径上运行，因此 Enter 永远不会冻结（
// 请参阅 proactive_retrieve）。当漏斗超出
// large/slow 语料库上的树篱时，工作线程 保持分离运行，
// 并在接地准备就绪时调度 此。 `block` 是被隔离的 <retrieved-context> 文本；
// 空意味着后期检索清除了无置信栏（没有可注入的内容）。
// 状态更新函数 STAGES (m.d.staged_proactive_context)
// 因此下一个用户提交将其刷新到安全边界处的记录中——
// 缓慢的第一回合的接地被推迟一个回合而不是被丢弃。
struct ProactiveContextReady { std::string block; double confidence = -1.0; std::uint64_t id = 0; };

// ── Stats 查看器 ───────────────────────────────────────
// 会话实际执行的内容的 read-only、选项卡式投影。
// 三个消息代表整个表面：它打开，它在选项卡之间移动，
// 它关闭。没有什么可以选择，也没有什么可以提交——这是查看器的重点，
// 也是为什么它不需要 per-row 消息。
struct OpenStats {};
struct CloseStats {};
// 阅读-only技能查看器。故意没有 ApproveSkill
// 消息：从​​您已经滚动的列表中按一下键即可批准，
// 这是安装流程需要避免的习惯（93％的 Claude 代码
// 权限 提示词 获得批准）。该决定位于
// `agentty skill approve`，其中同意屏幕可以显示 正文。
struct OpenSkills {};
struct CloseSkills {};
struct SkillsMove { int delta = 0; };
// 步骤选项卡选择、换行。签名后，一条消息服务于 Tab 和 Shift-Tab
// （以及 ←/→），而不是两条近 -identical 的消息。
struct StatsTab { int delta = +1; };
// 滚动统计数据正文。面板是 READ-ONLY，因此没有光标可移动，
// 并且 ↑/↓ 直接作用于 可视区域 — 这也是增量位于行而不是
// 项目中的原因：正文 是渲染的工作表，而不是列表，
// 而 "the next row" 是唯一表示其上任何内容的单位。
struct StatsScroll { int delta = +1; };

// ── RAG模式 选择面板 ────────────────────────────────────────────────
// 一个决定：主动（pre-turn)检索如何表现——开/仅第一次打开/关。
// 选择提交模式（persist + live-apply）并关闭。
struct OpenRag { std::string focus; };         // 焦点：行id，为OpenSmartMode
struct CloseRag {};
// Reveal/hide Tier::Advanced行(^A)。 面板 重塑其外形；
// 标志 是视图状态，而不是配置，因此它不会被持久化。
struct RagAdvanced {};

// ── 嵌入子 -form (RAG 选择面板 ——嵌入式) ──────────────────
// 检索使用哪个嵌入器，完全在TUI中配置。
//
// 有ONE关键消息，不是十五。导航、编辑和下拉列表属于共享表单层 (
// runtime/panel/form_keys.hpp)，它将 KeyEvent
// 转换为 面板-agnostic 意图； 状态更新函数 应用它并仅处理真正
// 嵌入的 -specific。一个 面板 拼出
// Char/Backspace/CursorLeft/… 消息将是 re-deriving
// 该键映射，而两个 面板 这样做就是它们如何分开的。
struct RagEmbedClose {};                   // 留一级（menu/field/pane）
struct RagEmbedKey   { form::keys::Action action; };
// 在编辑检索字段时用括号括起来的粘贴（API键、主机、
// 路径——粘贴字段就是为此而生的）。
struct RagEmbedPaste { std::string text; };
struct RagEmbedTest  {};                   // 运行探针（工作线程 线程)
// 探测结果。携带 MEASURED 维度 - 这是唯一值得信赖的来源，
// 因为错误的维度会导致 rag-cpp 的 HNSW 默默地丢弃每个向量。
struct RagEmbedTestDone {
    bool          ok = false;
    std::uint32_t dim = 0;
    int           latency_ms = 0;
    std::string   error;
    // 启动时EmbedForm::probe_gen的回声——状态更新函数
    // 从被取代的启动中删除答案（同时编辑配置/re-tested）。
    std::uint64_t gen = 0;
};
struct RagEmbedSave  {};                    // 坚持+live-apply
// 存储的面板端点的API密钥，打开后在工作线程上读取。
struct RagEmbedKeyLoaded { std::string endpoint; std::string key; };
// 当面板打开时活体检索器报告的内容，在工作线程上阅读。
// 那么`gen`就是表格的probe_gen；稍后的编辑或测试将取代它。
struct RagEmbedStatusRead {
    std::uint64_t gen = 0;
    bool          ready = false;
    bool          failed = false;
    std::uint32_t dim = 0;
    int           latency_ms = 0;
    std::string   reason;
};
// 键写要求的保存；当没有安全存储时为 false。
struct RagEmbedKeySaved  { bool ok = true; };

// ── 设置 选择面板 (Ctrl+K → Plugins/Commands/Agents/Hooks) ──────
// 一个共享列表弹出面板，由配置关注点参数化。
// 开口承载着哪些关怀；移动卷轴；激活对焦点行进行操作。
struct OpenSettingsList  { settings::Category concern; };
struct CloseSettingsList {};
struct SettingsListMove  { int delta; };
struct SettingsListActivate {};
// 内联add-mode。
struct SettingsListAddStart   {};              // `A`——输入-line添加提示词
struct SettingsListRemove     {};              // `d`——删除突出显示的插件（故意的）
// `e`/`a`-打开插件编辑器表单（突出显示的服务器上的详细信息，或添加模式）。
// 对于非 -Plugins 问题，`add=true` 回退到 one
// -line 启动器 提示词，而 `add=false` 是 no-op —
// 在已知问题的 状态更新函数 中解决。
struct SettingsListEditOpen   { bool add = false; };
struct SettingsListChar       { char32_t ch; };// 带类型的码点
struct SettingsListPaste      { std::string text; }; // 将括号内的内容粘贴到add-mode输入中
// MCP连接快照插件面板渲染。当 connect/reload 在 工作线程 上完成时，
// 由 load_plugins_async() Cmd 调度。 状态更新函数 将
// 其存储在 m.ui.plugins — 视图读取的 唯一 源中，因此该面板是
// Model 的纯函数（在 渲染 时间没有 plugin_model()）。
// 镜像 ThreadsLoaded{vec} 模式。
struct PluginsUpdated         { mcp::PluginModel model; };

// 对mcp.json的写入结果被报告回请求它的
// 状态更新函数。
//
// 状态更新函数 直接用于 调用 tools::plugin::*()
// 并在返回值上分支 — 文件读取、JSON 重写和原子重命名，
// 所有这些都在更新内。现在 状态更新函数 返回 cmd::edit_plugin
// (op)，写入发生在 工作线程 上，而 Msg 将结果带回。
// 成功写入后 状态更新函数 执行的所有操作（重新加载插件模型、吐司、
// 关闭 面板）都会在到达时发生。
//
// `op` 是回显的，而不是重建的，因此回复准确地携带了所
// 询问的内容——服务器名称、工具、切换 的方向——即使
// 面板 此后已经移动。
struct PluginEdited {
    enum class Kind : std::uint8_t {
        SetServerDisabled, SetToolEnabled, Approve, Remove, Add, Update,
        SetToolPinned,   // tools.pin：总是已加载，从不按需（#98）
    };
    enum class From : std::uint8_t { SettingsList, EditPane };
    Kind        kind{};
    From        from{};
    std::string server;
    std::string tool;          // SetToolEnabled/SetToolPinned
    bool        flag = false;  // SetServerDisabled:禁用； SetToolEnabled：启用； SetToolPinned：已固定
    bool        exit_commit = false;  // EditPane 保存:字段-exit、面板保持开放
    // 编辑 面板 正在编辑哪个配置（项目 mcp.json
    // 与用户一）。回显，以便回复可以在 相同 范围上
    // 重新打开 面板，而不是在 面板 落地时读取它。
    bool        project = false;
    // 结果。 `ok` 归并 approve_server 的 bool 和 EditResult::Ok
    // 合为一个答案； `already_exists` 和 `parse_error` 被保留，
    // 因为编辑 面板 显示它们内联在特定行上。
    bool        ok = false;
    bool        already_exists = false;
    bool        parse_error    = false;
};
struct SettingsListBackspace  {};
// 插件工具行上的 `p`：固定它（始终 已加载）或取消固定它（#98）。
struct SettingsListPin        {};
// →/← 关于插件：显示或隐藏服务器的工具。
struct SettingsListExpand     { bool open; };
// 设置中心搜索：键入以搜索每个设置，Backspace进行修剪。
struct SettingsSearchInput     { char32_t ch; };
struct SettingsSearchBackspace {};
struct SettingsSearchClear     {};   // Esc带有查询：清除它，保持打开状态
struct SettingsListSubmitInput{};              // Enter在add-mode→创建
struct SettingsListCancelInput{};              // Esc 在 add-mode → 返回列表
// add-mode创建(mcp.json的插件，或起始command/agent文件)
// 作为副作用运行；这就是它的答案。
struct SettingsAddDone {
    settings::Category concern{};
    bool               ok = false;
    std::string        message;
};

// ── 叉子 选择面板 ──────────────────────────────────────────────
// 将当前线程分支为FRESH分支（靠近-zero上下文）并选择RAG在
// 分叉中的主动行为。父级的记录被写入磁盘，并且分叉带有指向它的 read
// -on-demand 指针 - 没有副本，没有摘要。
// ForkThread 做分支。参见docs/FORK.md.
struct OpenFork  {};
struct CloseFork {};
struct ForkMove  { int delta; };
// 使用CURRENTLY-highlighted选项进行分叉(状态更新函数读取
// 选择面板的光标)。没有 载荷 的叶子使密钥 处理函数 不再需要 Model。
struct ForkThread      {};
// 工作线程写下了父母的成绩单（并让读取工具看到它）。
// 写入失败时，`path`为空。该分叉是建立在这个回复的基础上的。
struct ForkTranscriptWritten {
    std::string        parent_id;
    fork_panel::Choice choice = fork_panel::Choice::RagOff;
    std::string        path;
};

// ── diff审查 ──────────────────────────────────────────────────────────
struct OpenDiffReview {};
struct CloseDiffReview {};
struct DiffReviewMove { int delta; };
// 通过 `delta` 差异线（^D/^U、PgDn/PgUp)对 FOCUSED
// 差异块 的 正文 窗口进行分页。根据 差异块 的实际行数夹在 状态更新函数 中。
struct DiffReviewScroll { int delta; };
struct DiffReviewNextFile {};
struct DiffReviewPrevFile {};
struct AcceptHunk {};
struct RejectHunk {};
struct AcceptAllChanges {};
// 恢复磁盘上的每个 待处理 更改。 `confirmed` 携带 INTENT 穿过
// 状态更新函数 的 Two-press 防护：一个关键和弦将其发送为 false
// （第一次按下手臂，第二次执行），而明确选择的调色板行 - 已经打开了一个 multi
// -step → 选择 → Enter - 将其发送为 true 并立即执行。
// 将其保留在消息上意味着呼叫者不必在Model中预先-stage保护状态，
// 然后重新-enter update()。
struct RejectAllChanges { bool confirmed = false; };

// ── Meta/ 会议-level ─────────────────────────────────────────────────
// CompactContext、勾选、退出、NoOp、ClearStatus、
// CycleProfile、
// RestoreCheckpoint —所有 事件 在概念上是 "above" 任何单
// 个域（会话本身、滴答时钟、简档模式等）。

// 自动/手动会话压实。镜像 Claude 代码 2.1.119
// 的 `BetaToolRunner.compactionControl`
// （二进制程序 靠近偏移 134600）。当运行的输入
// -token总计接近模型的上下文窗口，或者用户从调色板调用
// "Compact context"时，运行时会附加一条合成用户消息，
// 要求模型根据结构化的模式定义总结对话；然后，生成的助理文本将
// 提升为单个用户消息，即 REPLACES 整个对话历史记录。
// 下一轮继续针对压缩的前缀，就好像摘要是唯一的先前上下文一样。
// m.s.compacting 是 in-flight
// 标志 — 在 分派 上设置，在登陆摘要的 StreamFinished
// 上清除。
struct CompactContext {};

struct CycleProfile {};
// 显示/隐藏持久化 "N changes"横幅。显示 PREFERENCE，
// 因此它有自己的消息，而不是内嵌在调色板 状态更新函数 中：
// 设置现在拥有它，并且仅从一个位置可到达的设置是
// 当该位置移动时移动的设置。
struct ToggleChangesStrip {};
// 在第一次回复后命名新的聊天记录。
struct ToggleAutoTitle {};
// 当上下文靠近窗口时，上下文自行压缩。
struct ToggleAutoCompact {};
// MCP刀具装载：自动→加载全部→加载按需（#98）。
struct CycleToolLoading {};
struct RestoreCheckpoint { CheckpointId id; };
// 由 RestoreCheckpoint 启动的异步 git-restore 的完成。
// `ok=false`在`error`中携带人类-readable原因。
// 转录截断+作曲家重新填充发生在 此处（不是在 分派），
// 因此当用户看到旧的 提示词 重新出现在作曲家中时，工作树已经是
// 字节-identical 到 快照。
struct CheckpointRestored { CheckpointId id; bool ok = false; std::string error; };

// ── 检查站 选择面板 ────────────────────────────────────────────────────
// 在 线程 中的 全部 检查点回合上倒回 选择面板（不仅仅是最新的回合）。
// Open 从携带 checkpoint_id 的消息构建条目列表，
// 并为每个条目踢出一个异步 diff-summary；选择将突出显示的
// 条目映射到现有 RestoreCheckpoint 流。
struct OpenCheckpoints {};
struct CloseCheckpoints {};
struct CheckpointsMove { int delta; };
struct CheckpointsSelect {};
// 一项的异步"what changed since here"摘要已登陆。
// `index` 是条目在 分派 时间在 Open::entries 中的位置；
// 将 状态更新函数 re-validates 与当前列表进行比较（open/close
// 竞态 不能损坏 过期 索引）。
struct CheckpointDiffLoaded {
    int  index          = 0;
    bool ok             = false;
    int  files_changed  = 0;
    int  insertions     = 0;
    int  deletions      = 0;
};
// 启动探针回答：工作区是git 仓库吗？检查点从 Model 读
// 取此信息，而不是在 状态更新函数 线程 上询问 git。
struct RepoProbed { bool in_repo = false; };
// 终端的回滚缓冲区长度，在启动时读取UI 线程(其配置文件，
// gsettings，……)。 回滚缓冲区=自动重绘这么多行。
// 第 0 行 + !unlimited = 未知。
struct TerminalScrollbackProbed {
    int         rows      = 0;
    bool        unlimited = false;
    bool        exact     = false;
    std::string source;
};
// on-disk 库(技能、批准、命令、挂钩)，读取工作线程。
struct LibraryLoaded { Library library; };
// 终端窗口焦点改变(？1004 CSI I/O，AgenttyApp::on(FocusEvent))。
// 门控硬件光标：未聚焦的agentty停放+隐藏真实光标，
// 而不是在不活动的面板中留下闪烁条。
struct TerminalFocus { bool focused = true; };
// 终端的大小，来自主机：一次在连接时，然后在每个SIGWINCH上。
// 状态更新函数'行数学从Model读取它而不是询问tty。
struct TerminalResized { int cols = 0; int rows = 0; };
// 抄本上的
// Ctrl+U-在其紧凑的one-line-per-source形式和完整的-passage-text扩展之间翻转最新检索到的-context卡。
// 携带目标消息id，因此状态更新函数精确地突变该卡（id
// -addressed突变+渲染-key碰撞）。
struct ToggleRetrievedExpanded { MessageId id; };
struct Tick {};
struct Quit {};
struct NoOp {};
// 用户-triggered "drop the renderer's cell cache and repaint from scratch"。
// 绑定到 Ctrl-L（通用 终端 重绘 约定）。
// 调度 `cmd::force_redraw()`，其镜像 SIGWINCH
// coherence-collapse — 没有 回滚缓冲区 擦除，
// 只是从当前画布对 `prev_cells` 进行 in-place 重建。
// 当某些东西明显不同步时，可兼作调试舱口。
struct RedrawScreen {};
// 清除 `m.s.status` 的延迟哨兵，当且仅当自安排
// toast 以来它尚未被覆盖。 `stamp` 是
// `m.s.status_until` 在计划时间的值；如果 状态更新函数
// 此后写入了更新的状态，则标记将不匹配，并且此 Msg 是 no-op。
struct ClearStatus { std::chrono::steady_clock::time_point stamp; };

// ── 自行-update 数据 ───────────────────────────────────────────
// 后台释放检查完成(工作线程 线程→UI)。空 `latest` 或
// update_available=false = 没有什么可宣布的；
// 状态更新函数 存储状态，因此会出现 status-bar 标签 + 调色板条目。
// 检查错误会被悄悄吞掉——更新 NOTICE 绝不能作为错误出现。
struct UpdateCheckDone {
    bool        update_available = false;
    std::string latest;   // "0.3.1"
    std::string url;      // 发布页面（用于吐司）
};
// in-TUI更新期间下载进度(工作线程→UI)。
//
// perform_update()总是从外壳取得进度回调并且
// `agentty update`总是使用它——它打印实时百分比。
// TUI 根本没有通过 回调，因此在整个传输过程中，大约 15 个
// MB 下载位于一条冻结的 "⬆ downloading…" 线路后面。
// 在与挂起无法区分的缓慢 链接 上，用户不能想到 self
// -update 的一件事是它已挂起。
//
// 当服务器没有发送Content-Length时，`total`为0；在这种情况下，
// 状态更新函数 显示 字节-so-far 而不是百分比。
struct UpdateProgress {
    std::size_t got   = 0;
    std::size_t total = 0;   // 0 = unknown
};
// 在-TUI更新完成(工作线程→UI)。成功显示重启toast；
// 失败显示错误+从 shell 运行 `agentty update` 的提示。
struct UpdateApplied {
    bool        ok = false;
    std::string detail;   // 成功时为版本，失败时为错误文本
};

// `@` 选择面板的工作-set排名后面的后台`git status`刷新已
// 完成(工作线程→UI)。不携带载荷：git映射是一个进程-wide 缓存
// 选择面板直接读取。它的ONLY的工作是清除Model::
// UI::git_refresh_inflight，这就是使刷新变为single
// -flight的原因。
//
// 以这种方式合并——Model上的标志，由Msg清除——
// 而不是在工作区层内使用静态原子，这使得它保持Elm：in
// -flight状态是测试可以构造和断言的模型的一部分，
// 而不是状态更新函数看不到的隐藏可变状态。
struct GitSignalsRefreshed {};

// ============================================================================
// 域变体——运行时的每个正交切片一个。每个叶子上都有一个 `std::variant`；
// per-domain 状态更新函数 访问这些。
// ============================================================================
namespace msg {

using ComposerMsg = std::variant<
    ComposerCharInput, ComposerBackspace, ComposerEnter, ComposerNewline,
    ComposerSubmit, ComposerToggleExpand, ComposerToggleLoop,
    ComposerCursorLeft, ComposerCursorRight, ComposerCursorHome, ComposerCursorEnd,
    ComposerSlashMove, ComposerSlashComplete, ComposerSlashDismiss, SlashPromptRendered,
    ComposerCursorWordLeft, ComposerCursorWordRight,
    ComposerKillToEndOfLine, ComposerKillToBeginningOfLine,
    ComposerDeleteWordBack, ComposerDeleteWordForward,
    ComposerUndo, ComposerRedo,
    ComposerHistoryPrev, ComposerHistoryNext,
    ComposerImagePasteFromClipboard,
    ComposerPaste, ImagePathSniffed, ClipboardQueryTimeout, ArmoredPasteStall, ClipboardDiagnosed,
    ClipboardRead,
    ComposerRecallQueued,
    ComposerQueuePeekPrev, ComposerQueuePeekNext, ComposerQueuePopLast>;

using StreamMsg = std::variant<
    StreamStarted, StreamTextDelta, StreamTextBlockClosed,
    StreamToolUseStart, StreamToolUseDelta, StreamToolUseSnapshot,
    StreamToolUseEnd, StreamObservedToolResult,
    StreamThinkingDelta,
    StreamReasoning,
    StreamUsage, StreamFinished, StreamError, StreamHeartbeat,
    StreamBufferedWait, CancelStream, RetryStream, ProactiveContextReady>;

using ToolMsg = std::variant<
    ToolExecOutput, ToolExecProgress, ToolTimeoutCheck,
    PermissionApprove, PermissionReject, PermissionApproveAlways>;

// 工具-output查看器PANEL的消息——从ToolMsg中分离，
// 因此面板的状态更新函数存在于其自己的文件（update/tool_output.cpp)中，
// 将tool.cpp留给执行管道名称承诺。
using ToolOutputMsg = std::variant<
    OpenToolOutput, CloseToolOutput,
    ToolOutputMove, ToolOutputSelect, ToolOutputCopy, ToolOutputStep,
    ToolOutputFind>;

using ProvidersMsg = std::variant<
    OpenProviders, CloseProviders, ProvidersMove,
    ProvidersJump, ProvidersSelect,
    ProvidersFilterInput, ProvidersFilterBackspace,
    ProvidersDelete, ProvidersToggleAll>;

// ONE模型表面：browse/filter每个提供商的模型，调整工作量，并且（在智能
// 模式槽-assign模式下）固定角色→模型。 `ModelsLoaded` 是活动
// 提供者的 目录 获取； `FusedCatalogLoaded` 是 -provider
// 的一个背景。
using ModelsMsg = std::variant<
    OpenModels, CloseModels, ModelsMove, ModelsJump,
    ModelsSelect, ModelsToggleFavorite,
    ModelsCycleEffort, ModelsCycleContext, ModelsToggleReasoning,
    ModelsScopeProvider,
    ModelsFilterInput, ModelsFilterBackspace,
    ModelsLoaded, FusedCatalogLoaded, SwitchToPreviousModel, FusedRefreshOthers,
    ModelsRefresh, ModelWindowProbed>;

using ThreadListMsg = std::variant<
    OpenThreadList, CloseThreadList, ThreadListMove, ThreadListJump,
    ThreadListFilterInput, ThreadListFilterBackspace,
    ThreadListSelect, ThreadListScopeToggle, ThreadPeekScroll, ThreadPeekSettled,
    ThreadPeekLoaded,
    ThreadListDelete,
    ThreadListRenameStart, ThreadListRenameChar, ThreadListRenameBackspace,
    ThreadListRenameDelete, ThreadListRenameCursor,
    ThreadListRenamePaste, ThreadListRenameSubmit, ThreadListRenameCancel,
    RenameCurrentThread,
    ThreadCycle, NewThread, ThreadsLoaded, ForeignSessionsLoaded, ThreadSummariesFilled, ThreadLoaded, ThreadTitleGenerated,
    SaveRefused>;

using PaletteMsg = std::variant<
    OpenPalette, ClosePalette, PaletteInput, PanelFilterPaste,
    PaletteBackspace, PaletteMove, PaletteSelect>;

using MentionMsg = std::variant<
    CloseMention, MentionInput,
    MentionBackspace, MentionMove, MentionSelect>;

using SymbolMsg = std::variant<
    CloseSymbol, SymbolInput,
    SymbolBackspace, SymbolMove, SymbolSelect>;

using CodeBlockMsg = std::variant<
    OpenCodeBlocks, CloseCodeBlocks, CodeBlocksMove,
    CodeBlocksSelect, CodeBlocksEdit, CodeBlocksCopy,
    CodeBlockRunFinished,
    CodeBlockResultAttach, CodeBlockResultCopy, CodeBlockResultDiscard>;

using CheckpointMsg = std::variant<
    OpenCheckpoints, CloseCheckpoints, CheckpointsMove,
    CheckpointsSelect, CheckpointDiffLoaded, RepoProbed, TerminalScrollbackProbed, LibraryLoaded>;

using RagMsg = std::variant<
    OpenRag, CloseRag, RagAdvanced,
    RagEmbedClose, RagEmbedKey, RagEmbedPaste,
    RagEmbedTest, RagEmbedTestDone, RagEmbedSave,
    RagEmbedKeyLoaded, RagEmbedKeySaved, RagEmbedStatusRead>;

// 它自己的域，而不是 RagMsg 的一角：统计查看器不与检索共享任何状态，
// 也不共享 状态更新函数，并且将不相关的面板折叠到一个 变体
// 中是 状态更新函数 增长它没有业务拥有的武器的方式。
using StatsMsg = std::variant<
    OpenStats, CloseStats, StatsTab, StatsScroll,
    OpenSkills, CloseSkills, SkillsMove>;

using SettingsListMsg = std::variant<
    OpenSettingsList, CloseSettingsList, SettingsListMove,
    SettingsSearchInput, SettingsSearchBackspace, SettingsSearchClear,
    SettingsListActivate, SettingsListAddStart, SettingsListRemove, SettingsListPin,
    SettingsListExpand,
    SettingsListEditOpen,
    SettingsListChar, SettingsListPaste, PluginsUpdated, PluginEdited,
    SettingsListBackspace, SettingsListSubmitInput, SettingsListCancelInput,
    SettingsAddDone>;

using ForkMsg = std::variant<
    OpenFork, CloseFork, ForkMove, ForkThread, ForkTranscriptWritten>;

using TodoMsg = std::variant<
    OpenTodoModal, CloseTodoModal>;

using LoginMsg = std::variant<
    OpenLogin, CloseLogin, LoginBack, SignOut,
    OpenAccounts, AccountMove, AccountSelect, AccountRemove,
    LoginPickMethod, LoginCharInput, LoginBackspace,
    LoginPaste, LoginCursorLeft, LoginCursorRight, LoginSubmit,
    LoginCopyAuthUrl, LoginCopyCode, LoginOpenBrowserAgain,
    LoginExchanged, LoginOAuthMinted, CodexDeviceCodeReady, CodexLoginDone,
    DeviceCodeReady, DeviceLoginDone, TokenRefreshed, OAuthRefreshNotDue,
    AuthViewLoaded, HostProbed>;

using DiffReviewMsg = std::variant<
    OpenDiffReview, CloseDiffReview, DiffReviewMove, DiffReviewScroll,
    DiffReviewNextFile, DiffReviewPrevFile,
    AcceptHunk, RejectHunk, AcceptAllChanges, RejectAllChanges>;

using SmartModeMsg = std::variant<
    OpenSmartMode, CloseSmartMode, SmartModeKey, SmartModePaste,
    SmartModeAdvanced, SmartModeClearSlot>;

// ── 网络搜索(web_search政策和服务) ─────────────
using WebSearchMsg = std::variant<
    OpenWebSearch, CloseWebSearch, WebSearchKey, WebSearchPaste,
    WebSearchKeysRead, WebSearchKeySaved, WebSearchKeyRemoved>;

// ── 粘贴设置( 如何粘贴) Ctrl +V 读取剪贴板) ─────────────
using PasteSettingsMsg = std::variant<
    OpenPasteSettings, ClosePasteSettings, PasteSettingsKey, PasteSettingsPaste>;

// ── 插件编辑器( 详细 / 添加窗体) ──────────────────────────
using PluginEditMsg = std::variant<
    OpenPluginEdit, ClosePluginEdit, PluginEditKey, PluginEditPaste>;

// ── 外观(主题/密度/运动) ──────────────────────
// ── 外观(主题/密度/运动) ─────────────
using AppearanceMsg = std::variant<
    OpenAppearance, CloseAppearance, AppearanceKey, AppearancePaste, AppearancePickTheme,
    AppearanceThemeMove, AppearanceThemeQuery, AppearanceThemeCommit,
    AppearanceThemeCancel>;

// ── 沙箱(文件系统/网络/音节/限制) ─────
using SandboxMsg = std::variant<
    OpenSandbox, CloseSandbox, SandboxKey, SandboxPaste, SandboxRefreshPreview,
    SandboxSave,
    SandboxEditList, SandboxListKey, SandboxListPaste, SandboxListClose>;

using MetaMsg = std::variant<
    CompactContext, CycleProfile, ToggleChangesStrip, ToggleAutoTitle, ToggleAutoCompact, CycleToolLoading,
    RestoreCheckpoint, CheckpointRestored,
    ToggleRetrievedExpanded,
    TerminalFocus,
    TerminalResized,
    Tick, Quit, NoOp, ClearStatus, RedrawScreen,
    UpdateCheckDone, UpdateProgress, UpdateApplied, GitSignalsRefreshed>;

} // namespace msg

// ============================================================================
// Msg —顶部-level分组为变体。从任何叶子进行构造都有效，
// 因为 std::variant 的转换构造函数会遍历每个替代项；
// 只有匹配的域接受给定的叶子，因此换行是明确且隐
// 式的：
//
//   Msg米=ComposerEnter{}；       // -> Msg{ComposerMsg{ComposerEnter{}}}
//   Cmd::after(d, RetryStream{}); // same path
//
// Msg上的std::visit在域而不是叶上分派——参见update.cpp。
// ============================================================================
// Msg 是一个结构体，而不是它的别名：提到别名的每种类型都将所有 26 个域
// 拼写为其 RTTI 和 符号 名称（每个约 10 KB，发行版 二进制程序
// 的约 730 KB）。 std::visit、get_if 和
// holds_alternative 都适用于派生类型。
using MsgBase = std::variant<
    msg::ComposerMsg,
    msg::StreamMsg,
    msg::ToolMsg,
    msg::ToolOutputMsg,
    msg::ProvidersMsg,
    msg::ModelsMsg,
    msg::ThreadListMsg,
    msg::PaletteMsg,
    msg::MentionMsg,
    msg::SymbolMsg,
    msg::CodeBlockMsg,
    msg::CheckpointMsg,
    msg::RagMsg,
    msg::StatsMsg,
    msg::SettingsListMsg,
    msg::ForkMsg,
    msg::TodoMsg,
    msg::LoginMsg,
    msg::DiffReviewMsg,
    msg::SmartModeMsg,
    msg::WebSearchMsg,
    msg::PasteSettingsMsg,
    msg::PluginEditMsg,
    msg::AppearanceMsg,
    msg::SandboxMsg,
    msg::MetaMsg
>;
struct Msg : MsgBase {
    using MsgBase::MsgBase;
    using MsgBase::operator=;
    using base = MsgBase;
};

// ── Msg-domain 证明 ─────────────────────────────────────────
// Msg 变体依赖于`std::variant`的转换构造
// 函数通过找到变体接受的UNIQUE域来将叶(e.g.
// `ComposerEnter{}`)路由到右域臂中。
// "Unique" 是 加载-bearing 词：如果
// 叶子出现在两个域 变体 中，则转换构造函数不明确并且构建
// 失败 - 但仅限于尝试构建 Msg 的 调用 站点。
// 下面的证据表明，属性在一处，因此故障发生在 此 行，
// 而不是在 代码库 中的每个 分派 处。
namespace msg_proofs {

// 如果叶类型L是变体 V的替代方案之一，则为真。
template <class L, class V>
struct in_variant : std::false_type {};
template <class L, class... Ts>
struct in_variant<L, std::variant<Ts...>>
    : std::bool_constant<(std::is_same_v<L, Ts> || ...)> {};
template <class L, class V>
inline constexpr bool in_variant_v = in_variant<L, V>::value;

// 计算有多少个域变体包含叶L。对于运行时实际
// 使用的每个叶来说应该正好是1。
template <class L>
consteval int leaf_domain_count() {
    return int{in_variant_v<L, msg::ComposerMsg>}
         + int{in_variant_v<L, msg::StreamMsg>}
         + int{in_variant_v<L, msg::ToolMsg>}
         + int{in_variant_v<L, msg::ProvidersMsg>}
         + int{in_variant_v<L, msg::ModelsMsg>}
         + int{in_variant_v<L, msg::ThreadListMsg>}
         + int{in_variant_v<L, msg::PaletteMsg>}
         + int{in_variant_v<L, msg::MentionMsg>}
         + int{in_variant_v<L, msg::SymbolMsg>}
         + int{in_variant_v<L, msg::CodeBlockMsg>}
         + int{in_variant_v<L, msg::CheckpointMsg>}
         + int{in_variant_v<L, msg::RagMsg>}
         + int{in_variant_v<L, msg::StatsMsg>}
         + int{in_variant_v<L, msg::ForkMsg>}
         + int{in_variant_v<L, msg::TodoMsg>}
         + int{in_variant_v<L, msg::LoginMsg>}
         + int{in_variant_v<L, msg::DiffReviewMsg>}
         + int{in_variant_v<L, msg::SmartModeMsg>}
         + int{in_variant_v<L, msg::WebSearchMsg>}
         + int{in_variant_v<L, msg::PasteSettingsMsg>}
         + int{in_variant_v<L, msg::PluginEditMsg>}
         + int{in_variant_v<L, msg::AppearanceMsg>}
         + int{in_variant_v<L, msg::SandboxMsg>}
         + int{in_variant_v<L, msg::ToolOutputMsg>}
         + int{in_variant_v<L, msg::MetaMsg>};
}

// 跨每个域的代表性叶子的样本。如果其中任何一个落在 0 域中，
// 则 变体 成员需要一只手臂；如果有任何落在 2+ 中，
// 则 Msg 的转换构造函数将变得不明确，
// 并且 调用 站点将停止编译。我们为每个域分配 -pick
// 一个叶子，而不是尝试枚举所有 79 个以上的叶子 -
// 证明只需要捕获 SOMEONE 意外在两个域中添加叶子的情况；
// 每个域有一个见证人就足以让纪律在这里可见。
static_assert(leaf_domain_count<ComposerCharInput>()         == 1,
              "ComposerCharInput must belong to exactly one Msg domain");
static_assert(leaf_domain_count<StreamTextDelta>()           == 1,
              "StreamTextDelta must belong to exactly one Msg domain");
static_assert(leaf_domain_count<ToolExecOutput>()            == 1,
              "ToolExecOutput must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenModels>()            == 1,
              "OpenModels must belong to exactly one Msg domain");
static_assert(leaf_domain_count<ModelsLoaded>()               == 1,
              "ModelsLoaded must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenProviders>()        == 1,
              "OpenProviders must belong to exactly one Msg domain");
static_assert(leaf_domain_count<NewThread>()                 == 1,
              "NewThread must belong to exactly one Msg domain");
static_assert(leaf_domain_count<PaletteSelect>()      == 1,
              "PaletteSelect must belong to exactly one Msg domain");
static_assert(leaf_domain_count<MentionSelect>()      == 1,
              "MentionSelect must belong to exactly one Msg domain");
static_assert(leaf_domain_count<SymbolSelect>()       == 1,
              "SymbolSelect must belong to exactly one Msg domain");
static_assert(leaf_domain_count<CodeBlocksSelect>()     == 1,
              "CodeBlocksSelect must belong to exactly one Msg domain");
static_assert(leaf_domain_count<CheckpointsSelect>()    == 1,
              "CheckpointsSelect must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenRag>()           == 1,
              "OpenRag must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenStats>()         == 1,
              "OpenStats must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenSkills>()        == 1,
              "OpenSkills must belong to exactly one Msg domain");
static_assert(leaf_domain_count<StatsTab>()          == 1,
              "StatsTab must belong to exactly one Msg domain");
static_assert(leaf_domain_count<StatsScroll>()       == 1,
              "StatsScroll must belong to exactly one Msg domain");
static_assert(leaf_domain_count<ForkThread>()                == 1,
              "ForkThread must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenTodoModal>()             == 1,
              "OpenTodoModal must belong to exactly one Msg domain");
static_assert(leaf_domain_count<LoginSubmit>()               == 1,
              "LoginSubmit must belong to exactly one Msg domain");
static_assert(leaf_domain_count<AcceptAllChanges>()          == 1,
              "AcceptAllChanges must belong to exactly one Msg domain");
static_assert(leaf_domain_count<Tick>()                      == 1,
              "Tick must belong to exactly one Msg domain");
static_assert(leaf_domain_count<OpenWebSearch>()             == 1,
              "OpenWebSearch must belong to exactly one Msg domain");

// 也固定 top-level Msg 域计数——如果有人添加新域，
// 他们还必须更新 update.cpp 中 分派器 使用的
// kDomains 数组，该数组目前在 12 个臂上耗尽。
// 不匹配 → 分派 交换机默默地丢失域。
static_assert(std::variant_size_v<MsgBase> == 26,
              "Msg domain count changed — update the dispatcher in "
              "src/runtime/app/update.cpp and this proof to match");

// Spot-check对于一些代表性叶子来说转换-constructor是明确的。
// 如果叶子出现在两个域 变体 中，
// 则 `Msg{X{}}` 行将无法在此处 编译 — 在
// 证明站点而不是每个 分派 调用 站点上出现问题。
static_assert([] {
    Msg m1 = ComposerEnter{};       (void)m1;
    Msg m2 = StreamFinished{};      (void)m2;
    Msg m3 = Tick{};                (void)m3;
    Msg m4 = NewThread{};           (void)m4;
    Msg m5 = OpenLogin{};           (void)m5;
    return true;
}(), "Msg leaf construction must be unambiguous — if this fires, some "
     "leaf appears in two domain variants");

} // namespace msg_proofs

} // namespace agentty
