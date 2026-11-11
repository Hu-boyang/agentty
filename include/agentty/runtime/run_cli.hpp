#pragma once
// agentty运行：main()解析的选项，以及入口点。

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <maya/runtime.hpp>

namespace agentty::run_cli {

struct Options {
    std::string prompt;
    std::string agent;                  // empty: general
    std::string output;                 // -o FILE;空：标准输出
    int  max_turns = 0;                 // 0：角色的默认值
    std::chrono::seconds timeout{0};    // 0:默认挂钟
    bool json = false;
    bool verbose = false;               // stderr 上的一个统计行
    bool read_only = false;             // --read-only
    bool no_network = false;            // --no-network
    std::string effort;                 // --effort
    std::string resume;                 // --resume ID|最后
    bool save = false;                  // --save
    bool events = false;                // --events jsonl (reasoning/text 也是)
};

// 运行提示词并返回进程退出代码：
//   0 正常，1 任务失败或不按顺序，3 provider/auth 问题，
//   4 --resume 线程 未找到，5 上下文窗口已满，124 超时，
//   130/143/129 被 SIGINT/SIGTERM/SIGHUP. 停止
// 运行代理并等待它（及其信号观察器）：`unlocked`。
[[nodiscard]] int run(const maya::unlocked&, const Options& o);

// "25"->25 (1..500)。当它不是一个时，nullopt。
[[nodiscard]] std::optional<int> parse_count(std::string_view s);
// "90"、"90s"、"5m"、"1h"->秒(>0)。当它不是一个时，nullopt。
[[nodiscard]] std::optional<std::chrono::seconds> parse_duration(std::string_view s);
// 线程 ID (16个十六进制数字)或"last"。
[[nodiscard]] bool valid_resume(std::string_view s);
// 低|中|高|x高|最大|自动。
[[nodiscard]] bool valid_effort(std::string_view s);

}  // namespace agentty::run_cli
