#pragma once
// agentty::tool —Tool概念+DynamicDispatch适配器。
//
// Tool概念描述了每个工具模块公开的静态接口：带类型的(参数、
// 结果、副作用)捆绑加上标识(名称、描述、模式定义)。
// 运行时 分派器使用无类型的 注册表边（ToolDef）查找+调用；
// 单独的工具位于 `util::adapt<Args>(parse, run)` 后面，
// 可将 带类型的 对提升到 JSON
// -typed 签名 ToolDef 中。

#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "agentty/runtime/model.hpp"
#include "agentty/tool/effects.hpp"
#include "agentty/tool/policy.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/spec.hpp"
#include "agentty/tool/write_lanes.hpp"

namespace agentty::tool {

using tools::ToolOutput;
using tools::ToolError;
using tools::ExecResult;
using tools::EffectSet;

namespace detail {

// UTF-8-safe右-trim:从不在上限边界处分割multi
// -byte序列。 UTF-8前导字节的最高位为0xxxxxxx（
// ASCII）或11xxxxxx；继续 字节 是 10xxxxxx。
// 从 `n` 往回走，直到 `n-1` 处的 字节 是前导 字节，
// 因此切片以完整的代码点结束。限制为 4-byte 倒带，因为没有 UTF
// -8 字符占用超过 4 字节。
[[nodiscard]] inline std::size_t utf8_safe_floor(const std::string& s,
                                                 std::size_t n) noexcept {
    if (n >= s.size()) return s.size();
    for (std::size_t back = 0; back < 4 && n > 0; ++back, --n) {
        unsigned char c = static_cast<unsigned char>(s[n]);
        if ((c & 0xC0) != 0x80) return n;   // 不是延续 字节 → 安全
    }
    return n;
}

// 相同的想法，但上限——首先在 `n` 处或之后领先 字节。
// 由 末尾 策略使用，因此将切片 STARTS 保留在完整的
// 代码点上，而不是延续 字节。
[[nodiscard]] inline std::size_t utf8_safe_ceil(const std::string& s,
                                                std::size_t n) noexcept {
    if (n >= s.size()) return s.size();
    for (std::size_t fwd = 0; fwd < 4 && n < s.size(); ++fwd, ++n) {
        unsigned char c = static_cast<unsigned char>(s[n]);
        if ((c & 0xC0) != 0x80) return n;
    }
    return n;
}

// 应用每-tool输出预算。预算== 0 表示"no cap"。
// 如果文本已经适合，则返回不受影响的文本；否则按 `strategy`
// 截断并附加一个 -line 标记，模型可以读取该标记以了解
// 某些输出已被省略。该标记使用 ASCII，因此它可以在
// 任何 下游 编码过程中幸存下来，而无需进一步转义。策略：
//
//   头部——保留前`budget`个字符，附加"[... N chars elided ...]"。
//              适合答案为 "the requested chunk, in order"（读取、
//              编辑、Write）的工具。
//   末尾 — 前置 "[... N chars elided ...]\n"，
//              保留最后 `budget` 字符。适用于 log-stream 工具（Bash、诊断）。
//   HeadTail —保留预算的前 70%，标记，保留预算的后 30%。
//              适合两端都带有信号的工具（Grep、Web*、
//              Git diff/log/status）。
[[nodiscard]] inline std::string
apply_output_budget(std::string text, int budget,
                    tools::OutputTruncation strategy) {
    if (budget <= 0) return text;
    auto bsz = static_cast<std::size_t>(budget);
    if (text.size() <= bsz) return text;

    using Strat = tools::OutputTruncation;
    switch (strategy) {
        case Strat::Head: {
            auto cut = utf8_safe_floor(text, bsz);
            std::string out = text.substr(0, cut);
            out += std::format("\n\n[... {} chars elided — output exceeded "
                               "tool's budget; refine your request to see "
                               "more ...]",
                               text.size() - cut);
            return out;
        }
        case Strat::Tail: {
            auto cut = utf8_safe_ceil(text, text.size() - bsz);
            std::string out = std::format("[... {} chars elided from start — "
                                          "showing tail of output ...]\n\n",
                                          cut);
            out.append(text, cut, std::string::npos);
            return out;
        }
        case Strat::HeadTail: {
            // 70 / 30 拆分，均为 UTF-8-safe。
            auto head_n = (bsz * 7) / 10;
            auto tail_n = bsz - head_n;
            auto head_cut = utf8_safe_floor(text, head_n);
            auto tail_start = utf8_safe_ceil(text, text.size() - tail_n);
            // 如果头部和 末尾 重叠（给定 目录 值，
            // 大文本上的小预算不应该发生，但要防御），
            // 则折叠为普通头部。
            if (tail_start <= head_cut) {
                std::string out = text.substr(0, head_cut);
                out += std::format("\n\n[... {} chars elided ...]",
                                   text.size() - head_cut);
                return out;
            }
            std::string out = text.substr(0, head_cut);
            out += std::format("\n\n[... {} chars elided from middle ...]\n\n",
                               tail_start - head_cut);
            out.append(text, tail_start, std::string::npos);
            return out;
        }
    }
    return text;   // 无法接通
}

} // namespace detail

// Tool是身份+模式定义+副作用+行为的静态-type捆绑。
// `Args` 和 `Result` 类型作为嵌套
// typedef 公开，因此表面在 编译 时间完全是 带类型的；
// 只有 分派器 边界讲 JSON。
template <class T>
concept Tool = requires {
    typename T::Args;
    typename T::Result;
    { T::name() }         -> std::convertible_to<std::string_view>;
    { T::description() }  -> std::convertible_to<std::string_view>;
    { T::input_schema() } -> std::convertible_to<nlohmann::json>;
    { T::effects() }      -> std::convertible_to<EffectSet>;
} && requires(const nlohmann::json& args) {
    { T::execute(args) }  -> std::convertible_to<ExecResult>;
};

struct DynamicDispatch {
    [[nodiscard]] static tools::Found find(std::string_view name) noexcept {
        return tools::find(name);
    }

    // 解析工具ONCE，因此选通然后执行的调用者都针对SAME定义进行
    // 操作：两者之间的`mcp/tools/list_changed`
    // 否则会将名称解析为具有不同副作用集的两个ToolDefs。
    // The Found 保持其 目录 处于活动状态，因此在运行几分钟
    // 的 调用 下无法释放该定义。 gateway-then-execute
    // 的调用者解析一次并将句柄传递给下面的 *_with 重载。
    [[nodiscard]] static tools::Found resolve(std::string_view name) noexcept {
        return tools::find(name);
    }

    // 针对 pre-resolved ToolDef 执行（参见 resolve())。
    // 与execute(name, args) 相同的正文，但跳过第二个名称查找。工具在不包含
    // 锁 的 工作线程 上运行，并且可能会阻止：`unlocked`。
    [[nodiscard]] static ExecResult execute_with(const maya::unlocked& cap,
                                                 const tools::Found& td,
                                                 std::string_view name,
                                                 const nlohmann::json& args,
                                                 const tools::CallContext& ctx) noexcept {
        if (!td) return std::unexpected(ToolError::not_found(tools::unknown_tool_error(name)));
        static const nlohmann::json kEmpty = nlohmann::json::object();
        const nlohmann::json& safe_args = args.is_object() ? args : kEmpty;
        ExecResult result;
        try {
            // 对于正是这个调用，该能力跨越mcp-cpp的回调的上下文：
            // 每个-call副本保存它，并且当调用返回(
            // CallContext::cap)时该副本消亡。
            tools::CallContext call_ctx = ctx;
            call_ctx.cap.set(std::addressof(cap));
            // 写入器将其写入的文件保存为调用的长度，因此一个文件的两
            // 个写入器（来自任何调用者：父批次、子代理、ACP、
            // mcp-serve）相继运行而不是丢失更新。应用 此处，
            // 因为这是工具 正文 运行的唯一位置 (tools::ToolBody)；
            // 参见 write_lanes.hpp。
            //
            // 不适用于分派更多工具（任务）的工具：它的 权限 副作用
            // 说 Exec，而不是 WriteFs，并且它使 调用
            // 占用自己的通道，因此它从不等待它已经占用的通道。
            std::vector<std::string> lanes;
            if (td->effects.has(tools::Effect::WriteFs))
                lanes = tools::write_lanes::lanes_for(name, safe_args, call_ctx);
            const tools::write_lanes::Hold lane{cap, std::move(lanes), call_ctx};
            if (!lane.held())
                return std::unexpected(ToolError::io(
                    "cancelled while waiting for another write to the same file"));
            result = td->execute(tools::DispatchKey{}, cap, safe_args, call_ctx);
        } catch (const std::exception& e) {
            return std::unexpected(ToolError::unknown(std::string{"tool crashed: "} + e.what()));
        } catch (...) {
            return std::unexpected(ToolError::unknown("tool crashed with a non-standard exception"));
        }
        // Per-tool输出预算：其自身限制在病态输入上失败
        // 的工具的分派器-level安全网。 0 绕过它。
        if (result && td->max_output_chars > 0) {
            result->text = detail::apply_output_budget(
                std::move(result->text), td->max_output_chars,
                td->output_truncation);
        } else if (!result && td->max_output_chars > 0) {
            // 也有错误：MCP 服务器的错误 载荷 和子进程的
            // stderr 逐字跨越此边界。慷慨且 HeadTail，
            // 因此截断的错误仍可诊断。
            constexpr int kMinErrorBudget = 4'000;
            const int budget = std::max(td->max_output_chars, kMinErrorBudget);
            result.error().detail = detail::apply_output_budget(
                std::move(result.error().detail), budget,
                tools::OutputTruncation::HeadTail);
        }
        return result;
    }

    // 权限针对前-resolved ToolDef的决定（参见resolve())。
    [[nodiscard]] static bool needs_permission_with(const tools::Found& td,
                                                    Profile profile) noexcept {
        if (!td) return true;   // 未知工具关闭失败
        return tools::policy::permission(td->effects, profile)
               == tools::policy::Decision::Prompt;
    }

    // 执行已同意的调用，验证该工具仍然是已
    // 同意的工具。
    //
    // 同意记录针对NAME，并且对于MCP工具，
    // 服务器拥有该名称。在决策和 调用 之间，
    // 可以重建 目录——tools/list_changed 通知、
    // mcp 重新加载、切换——并且名称可以返回指向不同的工具。
    // 门和执行是同一字符串的两次独立查找，因此没有注意到。
    // Re-checking 和 指纹 此处使批准
    // 引用定义而不是字符串。
    //
    // `approved_hash == 0`意味着没有记录
    // 任何内容（没有门的路径，或者之前存在的线程），
    // 并且检查被跳过而不是失败关闭——拒绝这些将破坏恢复的线程，
    // 因为仅存在于实际给出授权的情况下的风险。
    [[nodiscard]] static ExecResult execute_approved(
        const maya::unlocked& cap, std::string_view name, const nlohmann::json& args,
        std::uint64_t approved_hash, const tools::CallContext& ctx) noexcept {
        const auto td = tools::find(name);
        if (!td) return std::unexpected(ToolError::not_found(tools::unknown_tool_error(name)));
        if (approved_hash != 0 && td->definition_hash() != approved_hash) {
            return std::unexpected(ToolError::denied(
                "tool '" + std::string{name} + "' was redefined after it was "
                "approved, so the approval no longer applies. This is what a "
                "compromised or updated MCP server looks like. Re-request the "
                "call to be shown the new definition."));
        }
        return execute_with(cap, td, name, args, ctx);
    }

    [[nodiscard]] static ExecResult execute(const maya::unlocked& cap,
                                            std::string_view name,
                                            const nlohmann::json& args,
                                            const tools::CallContext& ctx = {}) noexcept {
        return execute_with(cap, tools::find(name), name, args, ctx);
    }

    // 关于工具是否控制用户的单一事实来源。
    // 读取工具声明的副作用，询问策略。未知工具默认为
    // "needs permission"（失败关闭）。
    [[nodiscard]] static bool needs_permission(std::string_view name,
                                               Profile profile) noexcept {
        const auto td = tools::find(name);
        if (!td) return true;
        return tools::policy::permission(td->effects, profile)
               == tools::policy::Decision::Prompt;
    }

    // 权限卡的友好原因文本。读取工具的 副作用
    // + 活动配置文件以解释为什么需要 权限。
    [[nodiscard]] static std::string_view permission_reason(
        std::string_view name, Profile profile) noexcept {
        const auto td = tools::find(name);
        if (!td) return "unknown tool";
        return tools::policy::reason(td->effects, profile);
    }
};

} // namespace agentty::tool
