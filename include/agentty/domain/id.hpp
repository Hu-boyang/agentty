#pragma once
// agentty::Id<Tag>-string-shaped ID的zero-overhead强新类型。
//
// 鲁斯特法则-of-thumb：永远不要通过原始`String`跨越一个
// API当调用者可能将其与另一个混淆时的边界`String`。
// 这里同样的规则——aThreadId和一个ToolCallId都是"some hex"
// 在运行时，但编译器将它们分开。

#include <compare>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace agentty {

template <typename Tag>
struct Id {
    std::string value;

    Id() = default;
    explicit Id(std::string s) noexcept : value(std::move(s)) {}

    [[nodiscard]] bool        empty() const noexcept { return value.empty(); }
    [[nodiscard]] const char* c_str() const noexcept { return value.c_str(); }

    bool operator==(const Id&) const = default;
    auto operator<=>(const Id&) const = default;

    [[nodiscard]] bool operator==(std::string_view sv) const noexcept { return value == sv; }

    friend void to_json(nlohmann::json& j, const Id& id) { j = id.value; }
    friend void from_json(const nlohmann::json& j, Id& id) { j.get_to(id.value); }
};

struct ThreadIdTag     {};
struct ToolCallIdTag   {};
struct ModelIdTag      {};
struct CheckpointIdTag {};
struct ToolNameTag     {};
struct MessageIdTag    {};

using ThreadId     = Id<ThreadIdTag>;
using ToolCallId   = Id<ToolCallIdTag>;
using ModelId      = Id<ModelIdTag>;
using CheckpointId = Id<CheckpointIdTag>;
using ToolName     = Id<ToolNameTag>;
// Per-message稳定身份。在消息构造时生成并持久保存到磁盘，
// 因此 缓存 键（以及任何未来的 per-message 索引）
// 在重新加载时保持有效。让 view-side 渲染 缓存
// 通过内容标识而不是位置进行键控 - 压缩/删除/重新排序消息不会像
// （thread_id、msg_idx）键控那样与
// 过期 缓存 条目发生冲突。
using MessageId    = Id<MessageIdTag>;

// 生成新的MessageId。 Hex-encoded 64-bit 随机
// — 足够宽，-process 内发生碰撞的可能性很小。
// 在 src/io/persistence.cpp 中与匹配的 ThreadId 生成器一起定义。
[[nodiscard]] MessageId new_message_id();

} // namespace agentty
