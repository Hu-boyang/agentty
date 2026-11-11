#pragma once
// tool_calls.hpp-WHO是流式工具-call块所属的。
//
// ════════════════════════════════════════════════════════════════════════
// 该 PROBLEM
// ════════════════════════════════════════════════════════════════════════
//
// 流式传输聊天协议将一个工具调用分布在许多块上，
// 并且多个调用可以同时运行。将一个块归因于 调用 就是
// 整个工作，而聊天完成为您提供了两个不可靠的句柄：
//
//   ·`id`-在延续中可能不存在，可以重述为""，
//               并且可以跨平行调用重复。
//   ·`index` —可能完全不存在，并且对于新的调用来说可能是REUSED
//               一旦该索引处的前一个完成。
//
// 两者都不是单独的身份。每一个将其视为身份的安全带都发出了相同的错误：openai
// -python #3377（在到达订单上键入）、opik #8360、strands
// #3950、crewai、azure-ai、theia、llm-gateway
// #136、zed #42584。我们的也这样做了——index-only 键控合并
// 了两个重复使用索引 0 的并行 调用，从而破坏了两组参数。
//
// ════════════════════════════════════════════════════════════════════════
// 该 RULE
// ════════════════════════════════════════════════════════════════════════
//
//   身份是(id，索引)JOINTLY。使用该块携带的任何一个，
//   当它不携带任何一个时，使用 FAIL 而不是猜测。
//
// 失败的是加载-bearing部分。错误的猜测会将一个
// 调用 的 字节 附加到另一个，并生成 JSON 和
// 解析 — 因此损坏是无声的并落在工具调用中。
// 显式失败是一回合中可见的错误。第二个绝对更好，传输 没有什么
// 能让你避免选择。
//
// 这是 Zed 的 ToolCallAccumulator (
// crates/language_model_core/src/chat_completion.rs)
// 中推理的直接移植，这是我发现的唯一明确地陈述 joint-identity
// 规则而不是一次发现一个错误报告的实现。
//
// ════════════════════════════════════════════════════════════════════════
// 原因 IT LIVES 此处
// ════════════════════════════════════════════════════════════════════════
//
// 两个解码器（openai/transport.cpp、
// responses/codec.cpp)各自增长了自己的内联归因逻辑，
// 内部函数也进行 JSON 行走和 事件 发射。这就是两者的漂移方式：其中
// 一个的修复对另一个来说是不可见的，并且如果没有完整的 SSE 固定装置，
// 则两者都无法进行测试。这里它是一个没有 I/O 的值类型，因此错误报告
// 中的一个案例是四行测试。

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentty::provider::wire {

// 一个块告诉我们它属于哪个调用。两半都是可选的，
// 因为两者在真实的传输上都会丢失。
struct ChunkIdentity {
    std::string_view          id;      // "" = 不存在或重述为空
    std::optional<std::size_t> index;  // nullopt=字段不存在
};

// 为什么归因失败。一个值，而不是布尔值，因此调用者可以向用户说出一些真实的内容，
// 而不是 "tool call failed"。
enum class AttributionError : std::uint8_t {
    // 该块既不携带id也不携带索引，并且不止一个调用在飞行中，
    // 因此没有证据表明它延伸到哪一个。
    Ambiguous,
};

// 一个调用的累积状态。解码器拥有字节；
// 这就产生了他们属于哪个调用的问题。
struct Call {
    std::string                id;
    std::string                name;
    std::string                args;
    std::optional<std::size_t> index;
    // 一旦解码器已经公布了这个调用 下游就设置，
    // 因此re-attributed块不会re-announce它。
    bool                       started = false;
    bool                       ended   = false;
};

// 归属一个块的结果：它属于哪一个调用，
// 以及该调用是NEW(调用者必须宣布它)还是一个已
// 存在的被继续。
struct Attribution {
    std::size_t call;        // 索引进入calls()
    bool        is_new;      // 为此块创建了调用
    // 调用这个chunk DISPLACED，如果服务器重用了不同id的索引。
    // 调用者必须关闭它 - 未关闭的 调用 留下没有 tool_result
    // 的 tool_use 并挂起回合。
    std::optional<std::size_t> displaced;
};

// 将流传输的块归因于调用。没有 I/O，没有 JSON，
// 没有 事件：将块中有关身份的内容交给它，它会告诉你那是哪个 调用。
class ToolCallTracker {
public:
    // 本回合每出现 调用，ITERATION 仅。
    //
    // 故意不是向量：容器分发back()、front()、操作符[]和begin()，
    // 并且其中每一个都是"pick a call without proving which"的拼写。
    // 这就是整个 头文件 存在的目的是为了防止这个错误，
    // 而扫描仪只能捕获它已经知道的拼写 - `*s.begin()`
    // 被捕获，而 `calls().back()`
    // 则不会。
    //
    // 仅具有 begin/end 的范围支持一种合法使用（在
    // 回合结束时关闭每个打开的 调用，其中获取其中的 全部
    // 不是一种选择）并且不支持任何其他用途。该错误变得不可写而
    // 不仅仅是可检测到的。
    // 范围是ITERABLE而不是INDEXABLE。
    //
    // 实现这一点需要两次尝试，两次尝试都值得记录，
    // 因为两者看起来都有效：
    //   ·连续迭代器上的std::ranges::subrange保持
    //     random-access，所以operator[]仍然是编译。
    //   ·views::transform保留源类别，因此它也保留。
    // views::filter不能是随机-access（找到
    // 第N个匹配需要步行），因此类别降级为双向——已验证：
    // random_access_range是假的。
    // 谓词始终为真，因此迭代顺序和内容不变。
    //
    // 内容 此 执行 和 不执行 BUY。 `t.all()[i]` 不再是 编译，
    // 因此位置 -pick 拼写消失了。 `t.all().back()`
    // 执行 仍然是 编译 — back() 在双向范围内是合法的，
    // 并且没有视图类别会在保持可迭代的同时删除它。该残留物被
    // attribution_discipline_test 覆盖，它会按名称扫描它。
    //
    // 因此，保证是分层的，而且说实话：类型删除它可以删除的拼写，
    // 扫描仪捕获它不能删除的拼写，
    // 并且文档（docs/TOOL_CALL_ATTRIBUTION.md）
    // 规定了双方都看不到的情况的规则。仅仅声明类型就使错误变得不可写，
    // 就像将硬币翻转命名为 `latest_tool_item`
    // 一样的过度自信。
    [[nodiscard]] auto all() const noexcept {
        return calls_ | std::views::filter([](const Call&) { return true; });
    }
    [[nodiscard]] auto all() noexcept {
        return calls_ | std::views::filter([](const Call&) { return true; });
    }
    [[nodiscard]] std::size_t size()  const noexcept { return calls_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return calls_.empty(); }

    // 调用是属性名称。获取返回的句柄 attribute()，
    // 因此在没有首先证明它是哪一个的情况下，没有拼写选择 调用
    // — `at(0)`、`back()`、
    // "the most recent" 都是不可写的，因为索引类型仅来自属性。
    [[nodiscard]] Call& at(const Attribution& who) { return calls_[who.call]; }
    [[nodiscard]] Call& displaced(const Attribution& who) {
        return calls_[*who.displaced];
    }

    // 该块属于哪个调用？
    //
    // 这四种情况，按证据支持的顺序排列：
    //
    //   (id,index) 都存在——最强的句柄。我们知道的 id
    //                会继续 调用，即使它的索引发生了移动；我们不知道的
    //                id 会启动一个新的 调用、DISPLACING，
    //                无论该索引包含什么（重用情况）。
    //   (id, -) 单独的 id — 按 id 匹配，否则为新的 调用。
    //                这是 MiniMax 形状：通过 id 进行身份识别，没有可用的索引。
    //   (-，索引)单独索引——按索引匹配，否则是新的调用。
    //                普通的延续块。
    //   (-，-)无证据。仅当恰好有一个 调用 正在飞行时才合法；
    //                否则是模棱两可的。
    [[nodiscard]] std::expected<Attribution, AttributionError>
    attribute(const ChunkIdentity& who) {
        const bool has_id = !who.id.empty();

        if (has_id) {
            // 已知的id:这个调用继续，无论它的索引现在在哪里。
            if (const auto found = find_by_id(who.id)) {
                if (who.index) bind_index(*who.index, *found);
                return Attribution{*found, /*is_new=*/false, std::nullopt};
            }
            // 未知的ID。如果其他某个 调用 持有此索引，则服务器将
            // REUSED 作为新 调用 的索引 - 旧索引已结束。
            std::optional<std::size_t> displaced;
            if (who.index) {
                if (const auto at = find_by_index(*who.index);
                    at && !calls_[*at].ended)
                    displaced = at;
            }
            const auto fresh = open(std::string{who.id}, who.index);
            return Attribution{fresh, /*is_new=*/true, displaced};
        }

        if (who.index) {
            if (const auto found = find_by_index(*who.index))
                return Attribution{*found, /*is_new=*/false, std::nullopt};
            const auto fresh = open(std::string{}, who.index);
            return Attribution{fresh, /*is_new=*/true, std::nullopt};
        }

        // 根本没有身份。
        if (calls_.empty())
            return Attribution{open(std::string{}, std::nullopt), true, std::nullopt};
        if (calls_.size() == 1)
            return Attribution{0, /*is_new=*/false, std::nullopt};
        return std::unexpected(AttributionError::Ambiguous);
    }

    void reset() noexcept {
        calls_.clear();
        by_index_.clear();
    }

private:
    [[nodiscard]] std::optional<std::size_t> find_by_id(std::string_view id) const {
        for (std::size_t i = 0; i < calls_.size(); ++i)
            if (!calls_[i].id.empty() && calls_[i].id == id) return i;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> find_by_index(std::size_t index) const {
        for (const auto& [idx, call] : by_index_)
            if (idx == index) return call;
        return std::nullopt;
    }

    void bind_index(std::size_t index, std::size_t call) {
        calls_[call].index = index;
        for (auto& [idx, c] : by_index_)
            if (idx == index) { c = call; return; }
        by_index_.emplace_back(index, call);
    }

    std::size_t open(std::string id, std::optional<std::size_t> index) {
        calls_.push_back(Call{std::move(id), {}, {}, index, false, false});
        const auto at = calls_.size() - 1;
        if (index) bind_index(*index, at);
        return at;
    }

    std::vector<Call> calls_;
    // Small-N关联列表而不是映射：一轮有少量并行的调用，
    // 并且超过2-4个条目的线性扫描击败散列。
    std::vector<std::pair<std::size_t, std::size_t>> by_index_;
};

}  // namespace agentty::provider::wire
