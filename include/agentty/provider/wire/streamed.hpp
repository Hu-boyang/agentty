#pragma once
// unseen() —协调作为片段、快照或两者的任意组合
// 传送的流式传输值。
//
// 每个流式传输聊天协议以两种方式携带工具调用的参数：
// 增量片段("the next 12 bytes")和权威总数(
// "the arguments, complete")。两者都是spec-legal，
// 您得到的是SERVER：Codex 数据流片段的属性，GitHub Copilot
// 的代理合并并仅发送最终的快照。
//
// 我们的响应编解码器解码了片段并忽略了快照，因此针对Copilot，与`{}`一起
// 调度的每个工具调用-`grep`失败了"pattern required"，
// `read`失败了"path required"，并且zero
// -argument工具（repo_map，列表）仍然有效，这使得它看起来像一个片状模型
// 而不是一个丢弃的模型事件。
//
// 修复方法是停止选择：附加您所给予的内容，保留您拥有的内容，
// 并且仅发出新已知的内容。然后将 每个 载波路由到其中
// 是安全的，因为冗余载波没有任何贡献。

#include <string>
#include <string_view>

namespace agentty::provider::wire {

// 将`chunk`合并到`have`中，并返回刚刚已知的字节。
//
// `total`表示`chunk`是服务器的完整视图(
// 快照)还是下一块(片段)。 快照 是 幂等 — 重播一个，
// 或者在已经拼出的片段之后接收一个，会产生 ""。
//
// 比我们所持有的短的快照被忽略：已经发出的字节不能是
// un-emitted，因此保留它们是唯一一致的选择。
[[nodiscard]] inline std::string_view unseen(std::string& have,
                                             std::string_view chunk,
                                             bool total) {
    const std::size_t at = have.size();
    if (!total) { have += chunk; }
    else if (chunk.size() > at) { have.assign(chunk); }
    else { return {}; }
    return std::string_view{have}.substr(at);
}

} // namespace agentty::provider::wire
