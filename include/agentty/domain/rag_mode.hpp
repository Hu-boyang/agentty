#pragma once
// domain/rag_mode.hpp-主动-retrieval模式，作为词汇。
//
// 该 枚举 曾经位于 store/store.hpp 中，
// 位于保存它的设置结构旁边。这在存储层中放入了一段 DOMAIN 词汇，
// 只有当 线程 需要命名自己的 per-thread 覆盖时，
// 它才变得可见：store/store.hpp 包括
// domain/conversation.hpp，因此该字段在没有循环
// 的情况下无法命名自己的类型。解决方法是使用第二个相同的 枚举——这种完全
// 相同的重复会让同一想法的两个拼写变得不同。
//
// 模式是关于对话的事实，而不是关于文件格式的事实，因此它属于这里。
// store:: 保留一个别名，因此 `store::RagMode`
// 在已经写入的地方仍然是 编译。

#include <cstdint>
#include <string_view>

namespace agentty {

enum class RagMode : std::uint8_t {
    On = 0,        // 每次都主动进行 pre-turn 检索
    FirstTurnOnly, // 仅在线程的第一回合主动检索
    Off,           // 无主动注入（search_docs/search_code仍然有效）
};

[[nodiscard]] constexpr std::string_view to_string(RagMode m) noexcept {
    switch (m) {
        case RagMode::On:            return "On";
        case RagMode::FirstTurnOnly: return "First turn only";
        case RagMode::Off:           return "Off";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view describe(RagMode m) noexcept {
    switch (m) {
        case RagMode::On:
            return "retrieve context before every turn";
        case RagMode::FirstTurnOnly:
            return "retrieve once, at the start of a thread";
        case RagMode::Off:
            return "no automatic retrieval";
    }
    return "";
}

} // namespace agentty
