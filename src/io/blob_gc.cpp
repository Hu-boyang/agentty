// agentty::blobs::gc-标记和清除。设计见 blob_gc.hpp。

#include "agentty/io/blob_gc.hpp"

#include "agentty/io/blob_store.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/util/teardown.hpp"

#include <nlohmann/json.hpp>

#include <maya/runtime.hpp>

#include <chrono>
#include <fstream>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_set>

namespace agentty::blobs {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::string_view kStampName = ".gc-stamp";

persistence::Lane& gc_claim_lane() {
    static persistence::Lane l;
    return l;
}

// 其下可出现斑点名称的每个键。
//
// 故意将 不 硬编码为列表。作者动态生成其中一些 -
// put_or_inline() 为给定的任何字段写入 `<field>_blob`，
// 因此 `thinking_blob`、`signature_blob`
// 和 `text_blob` 都存在，并且当有人使用新字段
// 调用 时，就会出现一个新字段。固定列表会默默地停止保护
// 这些内容，症状将是删除 IS 引用的 载荷。
//
// 所以规则是结构性的：任何KEY为"blob"或以"_blob"
// 结尾的字符串值都是引用。这与作者的创作方式相符，
// 因此两者不会发生偏差。
[[nodiscard]] bool is_blob_key(std::string_view key) noexcept {
    return key == "blob" || key.ends_with("_blob");
}

// 遍历已解析的文档，收集每个斑点引用。递归，因为引用存在于多个深度：message.images[].
// blob、message.tool_calls[].output_blob、
// message.thinking_blocks[].text_blob。
void collect_refs(const json& j, std::unordered_set<std::string>& out) {
    if (j.is_object()) {
        for (const auto& [k, v] : j.items()) {
            if (is_blob_key(k) && v.is_string()) {
                auto name = v.get<std::string>();
                if (!name.empty()) out.insert(std::move(name));
            } else {
                collect_refs(v, out);
            }
        }
    } else if (j.is_array()) {
        for (const auto& v : j) collect_refs(v, out);
    }
}

// 读取一个线程文件的参考。如果文件存在但无法理解，
// 则返回 false — 调用者将其视为 "abort the sweep"，
// 因为引用未知的 线程 不得删除其
// 载荷。
[[nodiscard]] bool refs_from_file(const fs::path& p,
                                  std::unordered_set<std::string>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;

    if (p.extension() == ".jsonl") {
        // 日志：每行一个文档。这里容忍单个不可解析的行，
        // 其原因与 range() 容忍它的原因相同 -
        // 一个撕裂的最终附加 - 但这意味着该文件的引用不完整，
        // 因此它仍然无法完成整个扫描。
        std::string line;
        bool ok = true;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
            if (j.is_discarded()) { ok = false; continue; }
            collect_refs(j, out);
        }
        return ok;
    }

    std::string buf{std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()};
    json j = json::parse(buf, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return false;
    collect_refs(j, out);
    return true;
}

} // 命名空间结束。

GcStats collect_in(const fs::path& threads_dir, bool dry_run,
                   std::chrono::seconds min_age, std::stop_token stop) {
    GcStats st;
    std::error_code ec;
    if (!fs::is_directory(threads_dir, ec)) return st;

    // ── MARK ──────────────────────────────────────────────────────────
    std::unordered_set<std::string> referenced;
    {
        // 开走EXPLICITLY如此失败是事实，而不是缺席。
        //
        // 当无法打开目录时，`directory_iterator(dir, ec)`
        // 不会产生任何结果——这与"no threads"到范围-for是无法区分的。
        // 这就是 "nothing references these blobs"
        // 和 "I could not find out" 之间的区别，
        // 这里第二个默默地变成了第一个：`referenced` 保持为空，
        // `unreadable` 保持 0，然后 SWEEP 将 每个 blob 删除为未引用。
        //
        // 改为将其视为不可读。下面的安全规则已经
        // 说明了如何处理——报告和删除任何内容——只是
        // 从未被告知。
        std::error_code oec;
        fs::directory_iterator walk{threads_dir, oec};
        if (oec) {
            ++st.unreadable;
            AGT_LOG(Persist, Error, "blob_gc",
                    "cannot list {} ({}) — deleted nothing",
                    threads_dir.string(), oec.message());
            return st;   // 跑==假
        }
        for (const auto& e : walk) {
            if (stop.stop_requested()) return st;   // ran == false：没有删除任何内容
            if (!e.is_regular_file(ec)) continue;
            const auto p   = e.path();
            const auto ext = p.extension();
            if (ext != ".json" && ext != ".jsonl") continue;

            // index.json是我们自己的选择面板，
            // 缓存和acp_sessions.json属于ACP服务器；
            // 两者都不是 线程，也不包含 blob 引用。元数据 sidecar
            // (<id>.meta.json) DO 被扫描：它们携带压缩摘要，
            // 经过 put_or_inline，因此可以保存 blob 引用。
            const auto name = p.filename().string();
            if (name == "index.json" || name.starts_with("acp_sessions"))
                continue;

            ++st.scanned_threads;
            if (!refs_from_file(p, referenced)) {
                ++st.unreadable;
                AGT_LOG(Persist, Warn, "blob_gc",
                        "cannot read {} — aborting sweep", p.string());
            }
        }
        // 为编辑保留的子代理记录（threads/subagents/)也
        // 包含参考文献。
        if (fs::is_directory(threads_dir / "subagents", ec)) {
            std::error_code sec;
            fs::directory_iterator sub{threads_dir / "subagents", sec};
            if (sec) { ++st.unreadable; return st; }
            for (const auto& e : sub) {
                if (stop.stop_requested()) return st;
                if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
                ++st.scanned_threads;
                if (!refs_from_file(e.path(), referenced)) ++st.unreadable;
            }
        }
    }

    // ── 安全规则 ───────────────────────────────────────────────
    // 一个不可读的线程表示一组未知的参考。
    // 此时删除任何内容都有可能破坏正在使用的 IS 的 载荷，
    // 而无法读取的 线程 文件正是用户最需要完整保存
    // 载荷 的时候。报告并停止。
    const auto blob_dir = threads_dir / "blobs";
    if (!fs::is_directory(blob_dir, ec)) { st.ran = st.unreadable == 0; return st; }

    for (const auto& e : fs::directory_iterator(blob_dir, ec))
        if (e.is_regular_file(ec) && e.path().filename() != kStampName)
            ++st.total_blobs;
    for (const auto& r : referenced)
        if (fs::exists(blob_dir / r, ec)) ++st.referenced;
    st.dangling = referenced.size() - st.referenced;

    if (st.unreadable > 0) {
        AGT_LOG(Persist, Error, "blob_gc",
                "{} unreadable thread file(s) — deleted nothing", st.unreadable);
        return st;   // 跑==假
    }
    st.ran = true;

    // ── SWEEP ─────────────────────────────────────────────────────────────────────
    const auto cutoff = fs::file_time_type::clock::now() - min_age;
    for (const auto& e : fs::directory_iterator(blob_dir, ec)) {
        if (stop.stop_requested()) break;   // 部分扫描是安全的：每个删除都是独立的
        if (!e.is_regular_file(ec)) continue;
        const auto name = e.path().filename().string();
        if (referenced.count(name)) continue;
        if (name == kStampName) continue;
        if (min_age.count() > 0) {
            std::error_code tec;
            const auto mt = e.last_write_time(tec);
            // 年龄未知或太年轻：保存可能即将引用它（或者它是作者的
            // in-progress临时值）。留下它。
            if (tec || mt > cutoff) continue;
        }

        const auto sz = e.file_size(ec);
        if (dry_run) {
            st.bytes_freed += ec ? 0u : sz;
            ++st.deleted;
            continue;
        }
        std::error_code rm;
        if (fs::remove(e.path(), rm)) {
            st.bytes_freed += ec ? 0u : sz;
            ++st.deleted;
        }
    }

    AGT_LOG(Persist, Info, "blob_gc",
            "{} {} orphan blob(s), {} KB, across {} threads",
            dry_run ? "would reclaim" : "reclaimed",
            st.deleted, st.bytes_freed / 1024, st.scanned_threads);
    return st;
}

GcStats collect(bool dry_run) {
    return collect_in(persistence::threads_dir(), dry_run);
}

std::optional<GcStats> collect_if_due(const maya::unlocked& cap, std::stop_token stop) {
    using namespace std::chrono;
    const auto blob_dir = persistence::threads_dir() / "blobs";
    std::error_code ec;
    if (!fs::is_directory(blob_dir, ec)) return std::nullopt;
    const auto stamp = blob_dir / kStampName;
    // 索取今天的扫掠：检查印章并将其撞在一个部分中，
    // 因此两个一起出发的agentty不能同时看到它过期并且都扫掠。
    const bool claimed = persistence::with_shared_file(cap, gc_claim_lane(), stamp,
        [](const maya::held<lock_levels::file_lane>&, persistence::Held, std::string s) {
            const auto now = fs::file_time_type::clock::now();
            std::error_code e;
            if (auto t = fs::last_write_time(s, e); !e && now - t < hours{24}) return false;
            (void)persistence::detail::write_in_section(s, {},
                {.durability = persistence::Durability::Atomic});
            return true;
        }, stamp.string());
    if (!claimed) return std::nullopt;
    // 线程首先保留，因此过期的线程的斑点进入本次扫描。
    const Io io = IoAccess::grant();
    if (const int days = persistence::thread_keep_days(io); days > 0 && !stop.stop_requested())
        (void)persistence::expire_threads(io, cap, days, ThreadId{});
    return collect_in(persistence::threads_dir(), /*dry_run=*/false, hours{24}, stop);
}

namespace {
// 该子系统的 自身 池，因为 join_background_gc() 是
// ITS 扫描完成的公开承诺 — 并且您不能从共享的 池 中加入一项作业。
// 即策略行：无连接的 fire-and-forget 前往
// util::run_background/run_isolated_detached；
// 与拥有 池 的命名联接一起工作。没有人发布的 池 永远不会生成 线程，
// 因此在安排扫描之前这不会产生任何费用。
//
// 这取代了：互斥量+条件变量+原始工作线程线程+停止标志，
// 如果可以的话则加入其关闭，如果行走仍在运行则加入DETACHED。
// 分离器是最重要的部分——它让扫掠继续行走，而 CRT
// 则破坏了其下方的静力学。 pool::shutdown
// 要求作业停止，在有限的宽限内等待，然后才放弃；请参阅其评论，
// 该评论记录了 agentty 自己的 符号 扫描的具体失败
// 情况。
maya::pool& gc_pool() {
    // 一次工作线程：仅存在一次扫描。
    static maya::pool p{/*max_workers=*/1};
    return p;
}
constexpr auto kStartDelay = std::chrono::seconds(20);
}  // 命名空间结束。

void start_background_gc() {
    // 注册创建工作的连接，因此main()永远不必知道该子系统是线程化的。
    // gc_pool() 是一个 process-lifetime
    // function-local static，因此 回调 不能比其目标寿命更长，
    // 并且不需要 cancel()。
    // 每个进程一次。
    static std::once_flag once;
    maya::once(once, [] {
        util::teardown::on_shutdown("blobs.gc",
                                    [](const maya::unlocked& cap) { join_background_gc(cap); });

    // 隔离的、不排队的：原则上行走是无界的（慢速磁盘上的大
    // blob存储），并且它不能占用共享的工作线程。
    gc_pool().post_isolated([](const maya::unlocked& cap, std::stop_token st) {
        // 延迟意味着立即退出的进程根本不会开始行走。
        // 如果我们被要求首先停止一个 取消 通道、令牌，
        // 那么 delay_for 返回 true，而不是有人必须
        // 记住通知的第二个停止 标志。
        if (maya::delay_for(cap, st, kStartDelay)) return;
        try {
            (void)collect_if_due(cap, st);   // 在下一次关闭检查时停止
        } catch (const std::exception& e) {
            AGT_LOG(Persist, Warn, "blob_gc", "{}", e.what());
        } catch (...) {}
    });
    });
}

void join_background_gc(const maya::unlocked& cap) noexcept {
    (void)gc_pool().shutdown(cap);   // 请求停止，在宽限内等待
}

} // namespace agentty::blobs
