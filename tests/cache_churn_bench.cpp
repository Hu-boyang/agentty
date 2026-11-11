// cache_churn_bench — 每条新请求的前
// 缀有多少无效?
//
// 重放一个 真实 已保存 线程 一次一个请求( 每个前缀以工具的助手转折结束)
// 调用 = 一人代理, = 4人, =1人, 或1人,=1人 -loop ),
// 建立精确的 Anthropic `messages` 正文 并比较连续 正文.
// 电话 提示词缓存 只能重复使用 字节 至第一个差数,所以无效=len
//   (prev) - common_prefix (prev,
// 下)是前缀 API 内容 -bills 以 cache_creation
// 改为 cache_read。理想是~0,每一步(纯附件)。
// 在差异开始时,用信息索引报告全部步骤和最差步骤。
//   agentty_standalone_tests cache_churn_bench <thread.jsonl>  ⁇

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "agentty/io/thread_log.hpp"
#include "agentty/provider/anthropic/transport.hpp"

using namespace agentty;

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: cache_churn_bench <thread.jsonl>\n"); return 2; }
    auto log = ThreadLog::open_path(argv[1]);
    if (!log) { std::printf("could not open %s\n", argv[1]); return 1; }
    const Thread full = log->load_thread();

    std::string prev;
    std::size_t total_prev = 0, total_lost = 0, steps = 0, broke = 0;
    struct Worst { std::size_t lost; std::size_t at_msg; std::string ctx; };
    std::vector<Worst> worst;
    for (std::size_t n = 2; n <= full.messages.size(); ++n) {
        const auto& last = full.messages[n - 1];
        if (last.role != Role::Assistant || last.tool_calls.empty()) continue;
        Thread t = full;
        t.messages.resize(n);
        std::string body = provider::anthropic::messages_json_string(t);
        // 脱 cache_control 标记: 它们 EXPECTED
        // 以移动每个转弯,而不是缓存内容的一部分。
        for (std::size_t p; (p = body.find(",\"cache_control\":{")) != std::string::npos;) {
            auto e = body.find('}', p);
            body.erase(p, e - p + 1);
        }
        if (!prev.empty()) {
            const auto c = static_cast<std::size_t>(
                std::mismatch(prev.begin(), prev.end(), body.begin(), body.end()).first
                - prev.begin());
            const std::size_t lost = prev.size() - c;
            ++steps; total_prev += prev.size(); total_lost += lost;
            if (lost > 64) {
                ++broke;
                // 分歧是哪条传输信息?
                std::size_t msg = 0, depth = 0;
                bool in_str = false, esc = false;
                for (std::size_t i = 0; i < c; ++i) {
                    char ch = prev[i];
                    if (in_str) { if (esc) esc = false; else if (ch == '\\') esc = true; else if (ch == '"') in_str = false; continue; }
                    if (ch == '"') in_str = true;
                    else if (ch == '{' || ch == '[') { if (++depth == 2 && ch == '{') ++msg; }
                    else if (ch == '}' || ch == ']') --depth;
                }
                worst.push_back({lost, msg, prev.substr(c > 60 ? c - 60 : 0, 140)});
            }
        }
        prev = std::move(body);
    }
    std::sort(worst.begin(), worst.end(), [](auto& a, auto& b) { return a.lost > b.lost; });
    std::printf("thread %s: %zu messages, %zu agent-loop requests\n",
                argv[1], full.messages.size(), steps + 1);
    std::printf("steps that rewrote history: %zu of %zu\n", broke, steps);
    std::printf("prefix invalidated: %.1f KB of %.1f KB replayed (%.2f%%)\n",
                total_lost / 1024.0, total_prev / 1024.0,
                total_prev ? 100.0 * total_lost / total_prev : 0.0);
    for (std::size_t i = 0; i < std::min<std::size_t>(worst.size(), 8); ++i) {
        std::string ctx = worst[i].ctx;
        for (auto& ch : ctx) if (ch == '\n') ch = ' ';
        std::printf("  lost %7zu B at wire msg #%-4zu  ...%s...\n",
                    worst[i].lost, worst[i].at_msg, ctx.c_str());
    }
    return 0;
}
