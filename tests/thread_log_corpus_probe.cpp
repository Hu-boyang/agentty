// thread_log_corpus_probe - 圆形 -trip 每隔一段时间 真实 线程 穿过木头。
//
// thread_log_test.cpp中的单元测试使用合成信息,
// 这意味着它们只行使我想象的形状。这个探测器运行主属性—
// —写到一个日志,读回,得到同样的线程——
// 比~/.agentty/threads中实际的线程更远,
// 它包含着没有人会想发明的形状:工具调用失败了-stream,
// 来自三个不同提供者的图像,思维块,压缩标记,
// 半-migrated blob引用,以及每个贝壳的编码可以产生
// 的文本.
//
//   ./build/agentty_standalone_tests thread_log_corpus_probe [dir] [--verbose]
//
// 退出 0 = 每条 线程 圆形 -tripped。无 -zero =没有的伯爵,
// 最早的几个名字.
//
// 而不是一个有真实数据的测试案例:它读取了用户的家目录,
// 所以当程序不存在时它不会(和通过)-ops。
// 在相信迁移之前先用手进行。

#include <agentty/domain/conversation.hpp>
#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

struct Mismatch {
    std::string thread;
    std::string why;
};

// 对比日志回合前后的信件 -trip . 故意领域
// -by-field 而不是连载 -and-diff :
// 当此失败时, 更改的字段名称是整个调试故事。
bool compare(const Message& a, const Message& b, std::string& why) {
    auto fail = [&](const char* f) { why = f; return false; };
    if (a.id.value  != b.id.value)  return fail("id");
    if (a.role      != b.role)      return fail("role");
    if (a.text      != b.text)      return fail("text");
    if (a.thinking  != b.thinking)  return fail("thinking");
    if (a.thinking_signature != b.thinking_signature)
        return fail("thinking_signature");
    if (a.reasoning_summary  != b.reasoning_summary)
        return fail("reasoning_summary");
    if (a.images.size()     != b.images.size())     return fail("images.size");
    if (a.tool_calls.size() != b.tool_calls.size()) return fail("tool_calls.size");
    if (a.attachments.size() != b.attachments.size())
        return fail("attachments.size");

    for (std::size_t i = 0; i < a.images.size(); ++i) {
        if (a.images[i].media_type != b.images[i].media_type)
            return fail("image.media_type");
        // 按需加载 两者:这里是断裂的blob参考物或
        // 丢失的 惰性 源会出现,而失败是否则在用户
        // 重新使用之前是隐形的 -sent 轮到你了
        if (a.images[i].bytes() != b.images[i].bytes())
            return fail("image.bytes");
    }
    for (std::size_t i = 0; i < a.tool_calls.size(); ++i) {
        const auto& x = a.tool_calls[i];
        const auto& y = b.tool_calls[i];
        if (x.id.value   != y.id.value)   return fail("tool.id");
        if (x.name.value != y.name.value) return fail("tool.name");
        if (x.output()   != y.output())   return fail("tool.output");
        if (x.status_name() != y.status_name()) return fail("tool.status");
    }
    for (std::size_t i = 0; i < a.attachments.size(); ++i) {
        if (a.attachments[i].kind != b.attachments[i].kind)
            return fail("attachment.kind");
        // 按需加载 两者:这里是丢失的blob
        // 参考或断裂之处 惰性 源会出现, 正是失败
        // 保持隐形,直到转弯 -sent.
        if (a.attachments[i].body.bytes() != b.attachments[i].body.bytes())
            return fail("attachment.body");
    }
    return true;
}

} // 命名空间结束。

int main(int argc, char** argv) {
    fs::path dir;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose") verbose = true;
        else                  dir = a;
    }
    if (dir.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::printf("thread_log_corpus_probe: no HOME, nothing to do.\n");
            return 0;
        }
        dir = fs::path{home} / ".agentty" / "threads";
    }

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::printf("thread_log_corpus_probe: %s not present, nothing to do.\n",
                    dir.string().c_str());
        return 0;
    }

    // 写日志的地方 不 用户的 线程 目录。
    const fs::path work = fs::temp_directory_path() / "agentty_log_corpus";
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    std::size_t checked = 0, skipped = 0, msgs = 0;
    std::uintmax_t bytes = 0;
    std::vector<Mismatch> bad;
    const auto t0 = std::chrono::steady_clock::now();

    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const auto p = e.path();
        if (p.extension() != ".json") continue;
        if (p.filename() == "index.json") continue;
        if (p.filename().string().find("acp_sessions") != std::string::npos) continue;

        auto loaded = persistence::load_thread_file(::agentty::IoAccess::grant(), p);
        if (!loaded) { ++skipped; continue; }

        const std::string id = p.stem().string();
        auto log = ThreadLog::open_path(work / (id + ".jsonl"));
        if (!log) {
            bad.push_back({id, "could not open log"});
            continue;
        }
        if (!log->rewrite(loaded->messages)) {
            bad.push_back({id, "rewrite failed"});
            continue;
        }

        // 从头重新打开,这样索引会被从磁盘上读回,
        // 而不是从内存重新使用——一个真正的切换器的读取路径.
        auto reopened = ThreadLog::open_path(work / (id + ".jsonl"));
        if (!reopened) { bad.push_back({id, "could not reopen"}); continue; }

        const auto back = reopened->all();
        if (back.size() != loaded->messages.size()) {
            bad.push_back({id, "message count " + std::to_string(back.size())
                               + " != " + std::to_string(loaded->messages.size())});
            continue;
        }

        std::string why;
        bool ok = true;
        for (std::size_t i = 0; i < back.size() && ok; ++i) {
            if (!compare(loaded->messages[i], back[i], why)) {
                bad.push_back({id, "messages[" + std::to_string(i) + "]." + why});
                ok = false;
            }
        }
        if (!ok) continue;

        // 窗口读数必须与全读数一致,
        // 因为这是抵消指数的全部点。
        if (back.size() > 60) {
            const auto tail = reopened->range(back.size() - 60, back.size());
            if (tail.size() != 60) {
                bad.push_back({id, "tail window size"});
                continue;
            }
            for (std::size_t i = 0; i < tail.size(); ++i) {
                if (!compare(back[back.size() - 60 + i], tail[i], why)) {
                    bad.push_back({id, "tail window differs: " + why});
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
        }

        ++checked;
        msgs  += back.size();
        bytes += fs::file_size(p, ec);
        if (verbose)
            std::printf("  ok %-20s %5zu msgs\n", id.c_str(), back.size());
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();

    std::printf("thread_log_corpus_probe: %zu threads, %zu messages, "
                "%.0f MB, %lld ms\n",
                checked, msgs, static_cast<double>(bytes) / 1e6,
                static_cast<long long>(ms));
    if (skipped) std::printf("  (%zu unreadable files skipped)\n", skipped);

    if (!bad.empty()) {
        std::printf("FAILED: %zu thread(s) did not round-trip\n", bad.size());
        for (std::size_t i = 0; i < bad.size() && i < 10; ++i)
            std::printf("  %-20s %s\n", bad[i].thread.c_str(),
                        bad[i].why.c_str());
        return 1;
    }
    std::printf("PASS: every thread round-tripped verbatim\n");
    fs::remove_all(work, ec);
    return 0;
}
