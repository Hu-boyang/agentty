#pragma once
// agentty运行：有一个提示词通过子代理循环，没有UI。
// 不含mcp 头文件，因此main.cpp可以包括它。

#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>

#include <maya/runtime.hpp>

namespace agentty::tools {

struct OneShotOptions {
    std::string prompt;
    std::string agent_type;              // empty: general
    int max_turns = 0;                   // 0：角色的默认值
    std::chrono::seconds timeout{0};     // 0:默认挂钟
    std::stop_token cancel;              // 停止运行(Ctrl-C，SIGTERM)
    bool read_only = false;              // 无write/exec/net工具
    bool no_network = false;             // 没有net工具（shell net是沙箱的工作）
    std::string effort;                  // "":关闭；其他低|中|高|x高|最大|自动
    bool events = false;                 // stderr 上的 reasoning/text/turn 行
    std::string resume;                  // 线程 id继续； "" 为新的 线程
    bool save = false;                   // 保存作为线程（总是在恢复时）
};

struct OneShotResult {
    enum class Status { Ok, Failed, TurnLimit, Timeout, ProviderError, Cancelled, Unavailable,
                        NotFound, ContextFull };
    Status      status = Status::Ok;
    std::string text;       // 模型的最后答案；可能会部分失败
    std::string error;      // 为什么没有成功；确定时为空
    std::string agent;
    std::string model;
    int         turns = 0;
    int         tool_calls = 0;
    int         tool_errors = 0;
    long long   input_tokens = 0;    // 请求汇总
    long long   output_tokens = 0;
    long long   last_input_tokens = 0;  // 提示词上次请求的大小
    long long   context_window = 0;     // 模型的窗口，如果未知则为0
    long long   duration_ms = 0;
    std::string thread_id;   // 其中对话为已保存; "" 如果没有

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
};

[[nodiscard]] OneShotResult run_one_shot(const maya::unlocked&, const OneShotOptions& opts);

// "ok"、"failed"、"turn_limit"、...用于 --json 和消息。
[[nodiscard]] std::string_view status_name(OneShotResult::Status s) noexcept;

}  // namespace agentty::tools
