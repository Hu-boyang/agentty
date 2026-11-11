// thread_migration_probe - 运行 真实 保存
// 路径过 a 真实 线程 并证明移民是安全的 之前相信它的历史。
//
// thread_migration_test用合成信息覆盖逻辑.
// 这把代码运行在 A 上 线程 磁盘关闭——
// 1200 工具 调用 10张图片,无论那天制成什么
// 编码 检查实际重要的财产:
//
//   遗产 <id>.JSON 被删除 仅 日志后, 每一个消息、
//   每个工具输出和每个图像都会读回 字节 完好无损
//
//   ./build/agentty_standalone_tests thread_migration_probe <thread.json>
//
// 电话 线程 被复制到测试中 沙箱 首先,所以用户的真实历史
// 从未被触及。无参数 = 没有 -op 过.

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

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("thread_migration_probe: no thread given, nothing to do.\n"
                    "usage: %s <path-to-thread.json>\n", argv[0]);
        return 0;
    }

    const fs::path src = argv[1];
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        std::printf("FAIL: %s is not a file\n", src.string().c_str());
        return 1;
    }

    // 进入控制层 沙箱 - 永远不要在真实的历史上运作。
    const ThreadId id{src.stem().string()};
    const auto legacy = persistence::threads_dir() / (id.value + ".json");
    fs::copy_file(src, legacy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::printf("FAIL: could not stage: %s\n", ec.message().c_str());
        return 1;
    }
    const auto legacy_size = fs::file_size(legacy, ec);

    // 电话 线程 透过传统读者,
    auto before = persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
    if (!before) { std::printf("FAIL: could not load the staged thread\n"); return 1; }

    std::size_t tools = 0, images = 0;
    std::size_t tool_bytes = 0, image_bytes = 0;
    for (const auto& m : before->messages) {
        tools  += m.tool_calls.size();
        images += m.images.size();
        for (const auto& tc : m.tool_calls) tool_bytes  += tc.output().size();
        for (const auto& im : m.images)     image_bytes += im.bytes().size();
    }
    std::printf("staged %s: %.1f MB, %zu messages, %zu tool calls "
                "(%.1f MB output), %zu images (%.1f MB)\n",
                id.value.c_str(), legacy_size / 1e6, before->messages.size(),
                tools, tool_bytes / 1e6, images, image_bytes / 1e6);

    // 该 MIGRATION : 真实 保存 径,正转弯会运行它。
    const auto t0 = std::chrono::steady_clock::now();
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), *before);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    const auto save_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();

    const auto log_file = persistence::threads_dir() / (id.value + ".jsonl");
    check(fs::exists(log_file), "the save must write a log");
    check(!fs::exists(legacy),  "and retire the legacy document");
    if (fs::exists(log_file)) {
        const auto sz = fs::file_size(log_file, ec)
                      + fs::file_size(persistence::threads_dir()
                                      / (id.value + ".ofs"), ec);
        std::printf("migrated in %lld ms: %.1f MB -> %.1f MB\n",
                    static_cast<long long>(save_ms),
                    legacy_size / 1e6, sz / 1e6);
    }

    // 万物必须生存 字节 费用 字节.
    auto after = persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
    if (!after) { std::printf("FAIL: migrated thread does not load\n"); return 1; }

    check(after->id.value    == before->id.value,    "id survives");
    check(after->title       == before->title,       "title survives");
    check(after->forked_from == before->forked_from, "fork provenance survives");
    check(after->compactions.size() == before->compactions.size(),
          "compaction records survive");
    check(after->messages.size() == before->messages.size(),
          "every message survives");

    if (after->messages.size() == before->messages.size()) {
        std::size_t bad_text = 0, bad_tool = 0, bad_image = 0;
        for (std::size_t i = 0; i < before->messages.size(); ++i) {
            const auto& a = before->messages[i];
            const auto& b = after->messages[i];
            if (a.text != b.text || a.id.value != b.id.value
                || a.role != b.role || a.thinking != b.thinking) ++bad_text;
            if (a.tool_calls.size() != b.tool_calls.size()) { ++bad_tool; continue; }
            for (std::size_t k = 0; k < a.tool_calls.size(); ++k)
                if (a.tool_calls[k].output() != b.tool_calls[k].output()) ++bad_tool;
            if (a.images.size() != b.images.size()) { ++bad_image; continue; }
            for (std::size_t k = 0; k < a.images.size(); ++k)
                if (a.images[k].bytes() != b.images[k].bytes()) ++bad_image;
        }
        check(bad_text  == 0, "all message text/ids/roles identical");
        check(bad_tool  == 0, "all tool outputs identical");
        check(bad_image == 0, "all image bytes identical");
        if (bad_text || bad_tool || bad_image)
            std::printf("  (%zu text, %zu tool, %zu image mismatches)\n",
                        bad_text, bad_tool, bad_image);
    }

    // 电话 选择面板 一定还是找到它, 确切的一次, 与它的标题。
    const auto all = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    std::size_t seen = 0;
    for (const auto& t : all) if (t.id.value == id.value) ++seen;
    check(seen == 1, "listed exactly once after migrating");

    // 一会儿 保存 (i.e . 下一转)必须是不 -op 而不是腐败
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), *after);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    auto again = persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
    check(again.has_value() && again->messages.size() == before->messages.size(),
          "a second save leaves the thread intact");

    if (failures == 0)
        std::printf("PASS: migrated with nothing lost\n");
    return failures == 0 ? 0 : 1;
}
