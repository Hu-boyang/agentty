// turn_prep_bench — UI-thread 启动下一个模型请求的成本。
//
// 加载 a 真实 已保存 线程 和同步工作的时间 launch_stream
// 显示在 UI 线程 在请求去之前 工作线程 : 复制 线程 快照 时间
// Anthropic 传输 正文 建( 建( O) 工作线程 双倍的
// 工具完成到字节发往传输链路之间的延迟。
//   agentty_standalone_tests turn_prep_bench <thread-id> [跑

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "agentty/io/persistence.hpp"
#include "agentty/io/thread_log.hpp"
#include <optional>
#include "agentty/domain/conversation.hpp"

using namespace agentty;

namespace {
long pct(std::vector<long> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(p * v.size()))];
}
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: turn_prep_bench <thread-id> [runs]\n"); return 2; }
    const int runs = argc > 2 ? std::atoi(argv[2]) : 50;
    std::filesystem::path src{argv[1]};
    if (!std::filesystem::exists(src))
        src = persistence::threads_dir() / (std::string{argv[1]} + ".jsonl");
    std::optional<Thread> loaded;
    if (src.extension() == ".jsonl") {
        if (auto log = ThreadLog::open_path(src)) loaded = log->load_thread();
    } else if (auto t = persistence::load_thread_file(::agentty::IoAccess::grant(), src)) {
        loaded = std::move(*t);
    }
    if (!loaded) {
        std::printf("could not load %s\n", src.c_str());
        return 1;
    }
    const Thread& t = *loaded;
    std::size_t bytes = 0;
    for (const auto& m : t.messages) {
        bytes += m.text.size();
        for (const auto& tc : m.tool_calls) bytes += tc.output().size() + tc.args_dump().size();
    }
    std::printf("thread %s: %zu messages, ~%zu KB of content\n",
                argv[1], t.messages.size(), bytes / 1024);

    std::vector<long> copy_us;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        Thread snap = t;
        const auto t1 = std::chrono::steady_clock::now();
        copy_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        if (snap.messages.size() != t.messages.size()) std::abort();
    }
    std::printf("Thread copy (UI thread):  p50=%ld us  p90=%ld us\n",
                pct(copy_us, .5), pct(copy_us, .9));
    return 0;
}
