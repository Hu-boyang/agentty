#pragma once
// agentty::foreign::Scan — 选择面板 对其他代理会话的视图。
// 明文数据，Model可以容纳；阅读内容位于 sessions.hpp。

#include "agentty/domain/conversation.hpp"

#include <vector>

namespace agentty::foreign {

// 找到的每个用户会话，如选择面板 头文件（无消息，foreign_only设置），
// 最新的在前。 `skipped` 会记住不是用户会话的文件（子
// 代理或 SDK 运行，空文件），因此重新扫描不会再次读取它们。
struct Scan {
    std::vector<Thread>         sessions;
    std::vector<Thread::Import> skipped;
};

}  // namespace agentty::foreign
