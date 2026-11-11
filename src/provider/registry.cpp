// agentty::provider — 注册表 helpers that need a .cpp.
//
// wire_streams_reasoning_text()是重要
// 的：它决定UI是否可以为（提供商、模型）对提供^R思维可供性。
//
// 此前它会自行根据当前条目的
// wire 字段和各提供方特例回答该问题。
// for Copilot.这是对 第二 的猜测，当 传输层 选择 URL 时，它也会回答这个问题，
// 并且两者发生了漂移 - `openai` 行在拨打 /v1/chat/completions 时
// 声明了 Wire::OpenAIResponses，因此方言 -only 谓词对从不发送任何内容的
// 传输 表示 "yes, reasoning"。
//
// 该问题现在恰好具有一个权威：dialect.cpp中的provider::
// dialect_for()，该谓词和传输层都参考该权威。
// 此函数保留为公共拼写（调用者和测试使用此名称），
// 但现在是一个精简的前向，因此 UI 和 传输 不能不一致。

#include "agentty/provider/registry.hpp"

#include "agentty/provider/dialect.hpp"

namespace agentty::provider {

bool wire_streams_reasoning_text(std::string_view provider_id,
                                 std::string_view model) noexcept {
    return streams_reasoning_text(provider_id, model);
}

} // namespace agentty::provider
