// save_bench <thread-id> [圆 ——按 -round 保存 实际成本 线程.
//
// 加载 a 线程 从用户的商店到刮伤 AGENTTY_HOME ,
// 然后模拟代理环:每个圆形附件一个消息和 调用
// save_thread + 冲。 第1轮是全写( 第1轮)。
// 保存 ) ; 以后各轮采用递增的 末尾 路径。打印两个都这样
// 返回到整个 -thread 保存 很明显
//
// 构建: 部分 agentty_standalone_tests (LABELS 报告(第1段)
// 运行时间: agentty_standalone_tests save_bench 1c6fff4258e07787 10

#include <agentty/io/persistence.hpp>
#include "agtest_cap.hpp"
#include <agentty/io/thread_log.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using namespace agentty;
using clk = std::chrono::steady_clock;

static double ms(clk::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: save_bench <thread-file.json|.jsonl> [rounds]\n");
        return 2;
    }
    const fs::path src{argv[1]};
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 8;

    // 测试主沙盒 AGENTTY_HOME ,因此按路径选择
    // 源(读) -only 让每个人写土地 沙箱.
    std::optional<Thread> loaded;
    if (src.extension() == ".jsonl") {
        if (auto log = ThreadLog::open_path(src)) loaded = log->load_thread();
    } else if (auto t = persistence::load_thread_file(::agentty::IoAccess::grant(), src)) {
        loaded = std::move(*t);
    }
    if (!loaded || loaded->messages.empty()) {
        std::fprintf(stderr, "save_bench: can't load %s\n", src.string().c_str());
        return 1;
    }
    Thread t = std::move(*loaded);
    t.id = ThreadId{"save-bench-" + src.stem().string()};

    std::printf("%s: %zu messages\n", src.filename().string().c_str(), t.messages.size());

    // 哪里有这个? 状态更新函数-side 花掉一半的时间
    // enqueue() 单独: 将每封信件都散开查找更改,
    // 相对于复制更改 末尾 圆桌会议前报告,因此,
    // 其中一半的倒退可以归咎于圆桌会议。
    {
        std::size_t bytes = 0;
        for (const auto& m : t.messages) {
            bytes += m.text.size();
            for (const auto& tc : m.tool_calls)
                bytes += tc.output().size() + tc.args_dump().size();
        }
        const auto f0 = clk::now();
        volatile std::uint64_t sink = 0;
        for (int i = 0; i < 5; ++i)
            for (const auto& m : t.messages)
                sink ^= persistence::debug_message_fingerprint(m);
        const auto f1 = clk::now();
        (void)sink;
        std::printf("  fingerprint pass  %7.2f ms   (%zu KB of content)\n",
                    ms(f1 - f0) / 5.0, bytes / 1024);
    }
    for (int r = 0; r < rounds; ++r) {
        Message m;
        m.role = (r % 2 == 0) ? Role::User : Role::Assistant;
        m.text = "bench round " + std::to_string(r);
        t.messages.push_back(std::move(m));

        const auto a = clk::now();
        persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);            // 状态更新函数-side 费用
        const auto b = clk::now();
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());     // + 写作 -side 费用
        const auto c = clk::now();
        auto ms_local = [](auto d) {
            return std::chrono::duration<double, std::milli>(d).count();
        };
        std::printf("round %2d  %-4s  enqueue %8.2f ms   total %9.2f ms\n",
                    r, r == 0 ? "full" : "tail", ms_local(b - a), ms_local(c - a));
    }
    // 何等为回转 线程 工资/月 保存 现在:描述三角洲对上一个
    // 保存 (状态更新函数 ,排队(主机),旁边是整个 -Thread
    // 复制一个 保存 之前就已经花费了多少钱
    {
        std::vector<std::uint64_t> saved;
        auto first = persistence::describe_save(t, saved);
        saved = first.fps;
        (void)persistence::save_thread_delta(::agentty::IoAccess::grant(), agtest::cap(), std::move(first));
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
        for (int r = 0; r < rounds; ++r) {
            Message m;
            m.role = Role::Assistant;
            m.text = "delta round " + std::to_string(r);
            t.messages.push_back(std::move(m));
            const auto a = clk::now();
            auto d = persistence::describe_save(t, saved);
            saved = d.fps;
            const bool ok = persistence::save_thread_delta(::agentty::IoAccess::grant(), agtest::cap(), std::move(d));
            const auto b = clk::now();
            Thread copy = t;   // 什麽 SaveThread{m.d.current 曾经做
            const auto c = clk::now();
            persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
            std::printf("delta %2d  loop-thread %6.2f ms (queued=%d)   old whole copy %7.2f ms\n",
                        r, ms(b - a), int(ok), ms(c - b));
            (void)copy;
        }
    }
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    return 0;
}
