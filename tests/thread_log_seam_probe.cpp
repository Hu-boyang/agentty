// thread_log_seam_probe - 做吗? STORE 更喜欢木头吗?
//
// thread_log_test 证明日志圆 -trips 证明 接口边界 : 即:
// persistence::load_thread_by_id 返回日志,
// 当一个日志存在时,返回遗留文档,如果它不存在,返回日志 相同 线程 不管是哪种方式。
//
// 与真人对战 线程 ,在一个孤立的 AGENTTY_HOME:
//
//   AGENTTY_HOME=/tmp/x ./build/agentty_standalone_tests \
//       thread_log_seam_probe <id> 国 务 院
//
// 它可以转换遗产 <id>.JSON 输入日志格式,
// 通过存储读取 接口边界 ,并按字段比较,
// 包括实际图像 字节 ,则一个破损的blob 引用会隐藏。
// -op 过关了,所以测试没有代价。

#include <agentty/domain/conversation.hpp>
#include "agtest_cap.hpp"
#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

long long ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t).count();
}

bool same_thread(const Thread& a, const Thread& b, std::string& why) {
    auto fail = [&](const char* f) { why = f; return false; };
    if (a.id.value     != b.id.value)     return fail("id");
    if (a.title        != b.title)        return fail("title");
    if (a.forked_from  != b.forked_from)  return fail("forked_from");
    if (a.messages.size() != b.messages.size()) return fail("message count");
    if (a.compactions.size() != b.compactions.size())
        return fail("compaction count");
    for (std::size_t i = 0; i < a.messages.size(); ++i) {
        const auto& x = a.messages[i];
        const auto& y = b.messages[i];
        if (x.id.value != y.id.value) return fail("message id");
        if (x.role     != y.role)     return fail("message role");
        if (x.text     != y.text)     return fail("message text");
        if (x.thinking != y.thinking) return fail("message thinking");
        if (x.tool_calls.size() != y.tool_calls.size())
            return fail("tool_calls size");
        for (std::size_t k = 0; k < x.tool_calls.size(); ++k)
            if (x.tool_calls[k].output() != y.tool_calls[k].output())
                return fail("tool output");
        if (x.images.size() != y.images.size()) return fail("images size");
        for (std::size_t k = 0; k < x.images.size(); ++k)
            if (x.images[k].bytes() != y.images[k].bytes())
                return fail("image bytes");
    }
    return true;
}

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("thread_log_seam_probe: no thread given, nothing to do.\n"
                    "usage: %s <path-to-thread.json>\n", argv[0]);
        return 0;
    }

    // 独立的绳索点 AGENTTY_HOME 刚开始的
    // 沙箱 测试永远不能触及真实的历史,这是正确的,
    // 意味着探测器必须 COPY 编号 线程 被
    // 送进那个 沙箱 而不是在原地读取,下面的一切就
    // 全部在 沙箱 ,包括结尾处的删除。
    const fs::path src = argv[1];
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        std::printf("FAIL: %s is not a file\n", src.string().c_str());
        return 1;
    }
    const ThreadId id{src.stem().string()};
    const auto dst = persistence::threads_dir() / (id.value + ".json");
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::printf("FAIL: could not stage %s into the sandbox: %s\n",
                    src.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::printf("staged %s (%.1f MB) into %s\n", id.value.c_str(),
                static_cast<double>(fs::file_size(dst, ec)) / 1e6,
                persistence::threads_dir().string().c_str());

    // 1. 读取 接口边界 之前 任何日志都存在——必须走遗留的道路。
    auto t0 = std::chrono::steady_clock::now();
    auto legacy = persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
    const auto legacy_ms = ms_since(t0);
    if (!legacy) {
        std::printf("FAIL: could not load %s at all\n", id.value.c_str());
        return 1;
    }
    std::printf("legacy load: %lld ms, %zu messages\n",
                legacy_ms, legacy->messages.size());

    const auto log_path = persistence::threads_dir() / (id.value + ".jsonl");
    check(!fs::exists(log_path), "no log should exist yet");

    // 2. 转换为日志格式。
    {
        auto log = ThreadLog::open(id);
        if (!log) { std::printf("FAIL: could not open log\n"); return 1; }
        check(log->store_thread(*legacy), "store_thread must succeed");
    }
    check(fs::exists(log_path), "the log must now exist");

    // 3. 通过《公约》阅读 接口边界 AGAIN ——现在必须走日志路径,必须
    //    生成相同的 线程。这是一次检查中的全部承诺。
    t0 = std::chrono::steady_clock::now();
    auto viaLog = persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
    const auto log_ms = ms_since(t0);
    if (!viaLog) { std::printf("FAIL: log load returned nothing\n"); return 1; }
    std::printf("log load:    %lld ms, %zu messages\n",
                log_ms, viaLog->messages.size());

    std::string why;
    check(same_thread(*legacy, *viaLog, why),
          why.empty() ? "threads differ" : why.c_str());

    // 4. 现 告 选择面板 步行必须显示准确的行 线程 ,没有
    //    2(legacy + log),而非0.
    const auto all = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    std::size_t seen = 0;
    for (const auto& t : all) if (t.id.value == id.value) ++seen;
    check(seen == 1, "the thread list must show the thread exactly once");
    if (seen != 1)
        std::printf("  (saw it %zu times among %zu threads)\n", seen, all.size());

    // 5. 删除必须携带记录。
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), id);
    check(!fs::exists(log_path), "delete_thread must remove the log");
    check(!persistence::load_thread_by_id(::agentty::IoAccess::grant(), id).has_value(),
          "a deleted thread must not load");

    if (failures == 0) {
        std::printf("PASS: the store prefers the log, and returns the same "
                    "thread either way\n");
        if (legacy_ms > 0)
            std::printf("      (%lld ms -> %lld ms)\n", legacy_ms, log_ms);
    }
    return failures == 0 ? 0 : 1;
}
