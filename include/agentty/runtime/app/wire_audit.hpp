#pragma once
// wire_audit.hpp — 在我们发送该请求之前，该请求的 WRONG 是什么。
//
// ════════════════════════════════════════════════════════════════════════
// 原因
// ════════════════════════════════════════════════════════════════════════
//
// 每个提供商都用 400 和为人类编写的句子来拒绝格式错误的请求。
// “'messages[7].tool_calls[0].function.name' 无效：
// 空字符串" is one of the better ones; most are worse, and several vendors just say "
// invalid_request_error”。
//
// 因此，故障模式总是相同的：用户看到回合死亡，
// 日志有 400 和 正文，并且没有人能说 对应的 消息
// 格式错误或它是如何得到的——因为那时 载荷 是一个 90
// KB JSON blob，并且缺陷是其中的一个空字符串。
//
// GitHub的运行时不这样做。其故障遥测带有
// `ModelCallFailureRequestFingerprint` —
// 七个字段描述失败请求的 SHAPE：
//
//     messageCount toolResultMessageCount
//     namelessToolCallCount imagePartCount
//     imagePartsMissingMediaType
//     lastMessageRole badRequestKind
//
// 以及预检计数器`request_nameless_tool_call_count`，
// 其测量BEFORE请求发出。他们知道请求格式错误，
// 但他们仍然可以命名缺陷，并且他们知道这种情况在他们
// 的车队中发生的频率。
//
// ════════════════════════════════════════════════════════════════════════
// 该 SHAPE OF 此 API
// ════════════════════════════════════════════════════════════════════════
//
// 布尔值是没有用的。 "Is this request valid" 没有给出任何
// 你可以采取行动的答案——你仍然必须去寻找缺陷。所以 `audit_wire()`
// 返回一个值，NAMES 每个缺陷和 WHERE 是：
//
//     for (const auto& d:audit_wire(msgs).
//         defects)log("{} at message {}"，描述(d.kind)，d.message_index)；
//
// `Defect::Kind`是闭合的枚举，而不是字符串，
// 出于同样的原因ErrorClass是：呼叫者可以详尽地打开
// 它并且编译器检查开关。新的缺陷类型在每个对其进行分类的站
// 点上都会变成 编译 错误，这正是您想要的 - 另一种选择是
// 无人处理的字符串。
//
// ════════════════════════════════════════════════════════════════════════
// 内容 此 IS 不
// ════════════════════════════════════════════════════════════════════════
//
// 不是拒绝发送的验证者。提供商对于什么是合法的存在分歧，
// 他们会改变，并且拒绝服务器本应接受的请求的
// 客户端比发送服务器拒绝的请求的客户端更糟糕。
// 这里的每个缺陷都是STRONG SUSPICION，被记录并计数，
// 并且请求仍然发出。
//
// 也不是维修合格证。修复属于缺陷产生的地方，
// 并在上下文中正确修复它。这只是命名事物。

#include <cstddef>
#include <string_view>
#include <vector>

#include "agentty/domain/conversation.hpp"

namespace agentty::app::cmd {

// 一条消息有一个问题。
struct Defect {
    enum class Kind {
        // 名称为空的工具调用。每种方言中 400 的最常见原因是
        // 服务器无法路由它，并且它返回的错误命名了索引，而不是原因。
        // 这是按名称计算的 GitHub (
        // `request_nameless_tool_call_count`)。
        NamelessToolCall,

        // 工具RESULT，其ID与记录中的调用不匹配。
        // Anthropic彻底拒绝该请求； OpenAI
        // -compatible 网关有所不同。通常，一个迹象是
        // 调用 在 -stream 中期被丢弃，而其结果却幸存
        // 下来——这正是 接口边界 属性所要防止的损坏，从另一端观察到。
        OrphanToolResult,

        // 在完成的转弯中没有匹配结果的工具CALL。下一个请求将被 Anthropic
        // ("tool_use ids must have corresponding tool_result blocks") 拒绝。
        // 良性的 mid-turn，这就是为什么仅
        // 对不是最后的消息进行 标志 审核的原因。
        UnansweredToolCall,

        // 没有媒体类型的图像部分。几个网关400就这而不是嗅探。
        // GitHub 将其跟踪为其自己的 指纹 字段 (
        // `imagePartsMissingMediaType`)，
        // 这就是我们如何知道它经常发生的重要性。
        ImageMissingMediaType,

        // 完全不携带任何内容的消息——没有文本、没有工具调用、
        // 没有图像。 Anthropic 拒绝空内容块；
        // 其他人则默默地放弃转弯，这更糟糕，因为模型随后
        // 回答了一个它看不到的问题。
        EmptyMessage,
    };

    Kind        kind;
    std::size_t message_index;   // 索引到WIRE消息，而不是线程
    // 当缺陷有一个时，所涉及的工具调用 ID或名称。
    // 否则为空。故意复制：审核比它们描述的 载荷
    // 更长久（它们在请求消失后进入日志行）。
    std::string detail;
};

// 缺陷类型的稳定的、可查询的名称。
//
// 将 string_view 返回到静态存储中——这些是日志标记，而不是散文，
// 因此它们在各个版本中保持为 machine-greppable。
[[nodiscard]] constexpr std::string_view describe(Defect::Kind k) noexcept {
    switch (k) {
        case Defect::Kind::NamelessToolCall:      return "nameless_tool_call";
        case Defect::Kind::OrphanToolResult:      return "orphan_tool_result";
        case Defect::Kind::UnansweredToolCall:    return "unanswered_tool_call";
        case Defect::Kind::ImageMissingMediaType: return "image_missing_media_type";
        case Defect::Kind::EmptyMessage:          return "empty_message";
    }
    return "unknown";
}

// 请求的SHAPE，以及其中的所有可疑内容。
//
// 计数反映了GitHub的指纹，因为他们选择它们的原因是正确的：当您看不到正文时，
// 它们是区分一个400与另一个400的数字。
// 带有 `messages=48 tool_results=23 last_role=assistant`
// 的请求与 `messages=2 tool_results=0 last_role=user`
// 的请求是不同的情况，并且这种区别
// 存在于日志行中，无需任何成本。
struct WireAudit {
    std::size_t message_count       = 0;
    std::size_t tool_result_count   = 0;
    std::size_t tool_call_count     = 0;
    std::size_t image_part_count    = 0;
    // 最后消息的作用。以助理回合结束的请求在某些方言中是合法的，
    // 而在其他方言中以 400 结尾的请求是合法的，
    // 并且首先要检查回合何时立即终止。
    Role        last_role           = Role::User;

    std::vector<Defect> defects;

    [[nodiscard]] bool clean() const noexcept { return defects.empty(); }

    // 一种缺陷有多少个。每个 -kind 的计数使
    // 比率可见：一个无名的 调用 是一个错误报告，
    // 四十个是产生它们的系统性故障。
    [[nodiscard]] std::size_t count(Defect::Kind k) const noexcept {
        std::size_t n = 0;
        for (const auto& d : defects) if (d.kind == k) ++n;
        return n;
    }
};

// 审核传输 载荷。纯粹：没有日志记录，没有突变，没有I/O.
//
// 采用 传输 消息（-compaction 替换后），
// 因为这是提供者将实际看到的——审核原始
// 记录将报告未发送的消息中的缺陷。
[[nodiscard]] WireAudit audit_wire(const std::vector<Message>& wire);

}  // namespace agentty::app::cmd
