#pragma once
#include <cstdint>

namespace agentty {

// 模型的推理如何切换OFF。对于默认情况下思考的模型（Qwen3、
// DeepSeek V3.1+、GLM 4.5+、gpt-5.1+）来说，
// 忽略工作量字段并不是关闭的：它在默认级别进行思考。所以必须发送off，
// 具体如何发送取决于型号。
enum class ReasoningOff : std::uint8_t {
    Unknown = 0,   // 无声明
    Omit,          // 不是推理模型，或者仅在被问时给出理由
    EffortNone,    // reasoning_effort:"none"(gpt-5.1+、Mistral、许多主机)
    Toggle,        // on/off交换机(聊天模板enable_thinking等)
    Impossible,    // 总是原因(gpt-oss、o-series、R1):最低层是楼层
};

} // namespace agentty
