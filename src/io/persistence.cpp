#include "agentty/io/persistence.hpp"
#include "agentty/util/callback.hpp"
#include "agentty/io/blob_store.hpp"
#include "agentty/io/thread_log.hpp"
#include "agentty/runtime/settings_registry.hpp"

#include "agentty/util/logx.hpp"
#include "agentty/util/background.hpp"   // util::WorkerGroup
#include "agentty/util/lock_levels.hpp"
#include "agentty/util/teardown.hpp"

#include "agentty/util/sendable.hpp"   // maya::guarded 加 JSON，可主动选择启用。
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

#ifdef _WIN32
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "agentty/tool/util/utf8.hpp"
#include "agentty/auth/keys.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/base64.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

namespace agentty::persistence {

namespace fs = std::filesystem;
using json = nlohmann::json;

// 原子+持久写入：写入<target>.tmp、fsync、重命名。
// 崩溃或 ctrl-C mid-write 会使以前的版本保持完整 -
// 加载程序永远不会看到其 `catch (...)` 会默默删除的截断文件。
// 二进制程序模式避免CRLF转换，因此on-disk 字节匹配转储(2)。
// 公开（在persistence.hpp中声明），因此其他JSON sidecar（
// ACP会话索引等）共享相同的crash-safety保证。
// 持久化自己的内部结构（异步写入器、
// 设置锁)仅在下面的I/O-taking入口点之一下运行，因此它们在这里
// 重新-mint，而不是通过每个帮助器将其线程化。
static Io internal_io() { return IoAccess::grant(); }

bool write_json_atomic(Io io, const fs::path& target, const std::string& content) {
    return write_atomic(io, target, content, {.durability = Durability::Durable});
}

// ── Cross-process 独家 锁 设置文件 ──────────────────
//
// write_json_atomic给予CRASH安全性（tmp->fsync->
// 重命名），这是与CONCURRENCY安全性不同的属性，并且两者被混为一谈。
// 原子性意味着没有读者会看到破损的文件；它没有提到 LOST UPDATES。
// save_settings 序列化调用者的整个 in-memory
// 记录并替换文档，因此打开两个 agentty 实例：
//
//   A开始（主题=Harper）B开始（主题=Harper）
//   B：选择Sumi Phosphor，Enter->文件说Sumi
//   Phosphor A：更改ANY不相关行->A写入其STALE
//   记录->主题再次是Harper，并且没有失败
//
// 两次写入都是完整、有效且持久的。其中一人仅获得
// 第二名并赢得了整个比赛。在运行五个实例的情况下观察：
// 主题不断恢复，并且看起来不确定，因为获胜者是最后
// 一个进程 已保存。
//
// 互斥量无法解决这个问题——作者是单独的PROCESSES。 锁 必须存在于文件系统中，
// 并且该原语是 jaal (maya::platform::native_file_lock)，
// 而不是这里的第三手 -rolled 副本。
// persistence::with_shared_file 将其与 process
// -wide 通道配对，因为两个 竞态 不同，并且每个 锁 仅关闭其中一个：
// 参见include/agentty/io/shared_file.hpp.
//
// 注意此替换的更正：这里声称选择F_SETLKW的部分原因是
// 它在进程内共享，因此以后即使增加线程写入方，也会串行执行。
// 那是倒退。 Per-process 意味着一个进程的两个
// THREADS 都获取并且都不等待 - jaal 的一致性套件
// 将其固定为检查 13 - 因此 互斥量 与文件 锁 不是冗余的，
// 它是修复的另一半。
//
// 锁位于SIDECAR (settings.json.锁)上而不是
// settings.json本身上，因为原子写入重命名目标上的新
// inode：旧inode上保存的锁将保护不再存在的文件。
//
// 建议，并且故意最好-effort：如果不能采取锁，
// 我们记录并继续进行明文写入。丢失更新固然不好，
// 但因为 锁 文件不可用而拒绝 保存 用户设置则更糟糕。
persistence::Lane& settings_mu() {
    static persistence::Lane m;
    return m;
}

fs::path data_dir();
// 文件的一个名称，因此锁 sidecar和写入永远不会漂移
// 到不同的路径上。
fs::path settings_path() { return data_dir() / "settings.json"; }

fs::path data_dir() {
    // 单个 per-user 根（~/.agentty 或 $AGENTTY_HOME)
    // — 请参见 util/user_root.hpp 的布局以及它在 ~/.config.
    // 下的 不 的原因，user_root() 创建它 0700 并运行 one-time
    // Legacy-config 迁移。
    fs::path p = util::user_root();
    std::error_code ec;
    // 表面出现持久化-storage故障一次。
    // 默默吞下意味着threads/settings/memory写入变成了零反馈的no-ops
    // （读-only $HOME，全盘，EACCES）。
    // 对 stderr 的一个警告就足够了——它在 maya 占据屏幕之前打印，
    // 并且静态防护防止它在每个 保存 上发送垃圾邮件。
    if (p.empty() || !fs::is_directory(p, ec)) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true, std::memory_order_relaxed)) {
            std::fprintf(stderr,
                "agentty: warning: cannot create data dir '%s' (%s) — "
                "threads and settings will not persist this session\n",
                p.string().c_str(), ec.message().c_str());
        }
    }
    return p;
}

fs::path threads_dir() {
    // 通过 user_threads_dir() 而不是
    // `data_dir() / "threads"`，
    // 因此 $AGENTTY_THREADS_DIR 覆盖也适用于此。
    // 对话历史记录是 agentty 写入的第二个 -largest 内容，
    // 如果它继续加入叶子本身，它就会默默地保留在配置磁盘上 - 这正是 one-root
    // 头文件 警告的漂移。
    return util::user_threads_dir();
}

// ---- 图像 blob 存储------------------------------------------------
//
// MOVED至io/blob_store.{hpp，cpp}。
// 存储内容为 -agnostic（字节 输入，名称输出），线程 日志需要它，
// 而无需拖入整个文件。下面的 调用 站点直接使用 blobs::put /
// blobs::get — 没有转发包装器，因此每个操作只有一个名称。
//
// 散列和on-disk布局不变，因此在移动之前写入的每个
// 斑点仍然解析。旧的 线程 继续工作：加载程序仍然接受
// 内联 "data" 字段，因此不需要迁移任何内容，
// 降级只会丢失重复数据删除，而不是图像。

// 解析惰性 ImageContent的字节。在启动时安装到域类型中（
// 请参阅下面的初始化程序），因此 conversation.hpp（
// 纯数据 头文件）不需要了解 blob 目录或 base64。
//
// 镜像提前加载加载器用于内联执行的操作，包括其故障模式：
// 丢失的斑点或损坏的base64产生空的字节，并且每个传输
// 路径已经跳过空的-byte图像。
static std::string resolve_image_source(const ImageContent::Source& s) {
    if (!s.blob.empty()) return blobs::get(s.blob);
    if (!s.b64.empty())  return util::base64_decode(s.b64);
    return {};
}

// 在 main() 之前运行，因此 任意 入口点（TUI、无头运行、
// ACP 服务器、单元测试、加载 和 线程）可以工作 惰性 映像，
// 而不必记住 init 调用。
const bool g_image_resolver_installed = [] {
    ImageContent::set_resolver(&resolve_image_source);
    return true;
}();

static std::string role_to_string(Role r);

namespace {

// 成绩单预算。转录本是其父代的 on-disk 内存 的
// fork：模型 READS 按需（分页/可 grepable），
// 因此即使父代 线程 巨大（1M-token 代理运行），
// 它也必须保持为有界的、有用的工件。两个独立的上限：
//
//   kMaxMsgTextBytes — 一个巨大的 pasted/emitted `text` 块不能
//     主宰；它的头部被剪短+末尾，带有"… N bytes elided …"标记。
//   kMaxTranscriptBytes——总输出上限。当整个线程
//     不适合，我们保留 MOST RECENT 圈（叉子最有
//     可能需要的圈）并丢弃最旧的，记下我们省略了多少个。
//
// 限制OUTPUT也限制了峰值内存：in-RAM字符串至多为〜
// kMaxTranscriptBytes，因此在失控的线程的分叉上不存在无限制的
// ostringstream。 Tool OUTPUT 从未被写入（仅名称 +
// 120-char arg 提示），这已经剥离了长 线程 中最重的 字节。
constexpr std::size_t kMaxMsgTextBytes     = 16 * 1024;    // 16KB/消息
constexpr std::size_t kMaxTranscriptBytes  = 512 * 1024;   // 512 KB 总计

// 将文本块剪辑到字节预算，保留头部和末尾（末端携带最多
// 信号——问题的提出+其结论）并标记间隙。 UTF-8
// -safe：在代码点边界上进行切割，因此 .
// md 永远不会包含截断的多字节序列。
std::string clip_text(const std::string& s, std::size_t budget) {
    if (s.size() <= budget) return s;
    const std::size_t head = budget * 3 / 4;       // 75%头，25%末尾
    const std::size_t tail = budget - head;
    const std::size_t hcut = tools::util::safe_utf8_cut(s, head);
    // 末尾开始：从末尾退后`tail` 字节，然后前进到下一个代码点边界，
    // 因此我们从不开始中间-sequence。
    std::size_t tstart = s.size() > tail ? s.size() - tail : 0;
    while (tstart < s.size() && (static_cast<unsigned char>(s[tstart]) & 0xC0) == 0x80)
        ++tstart;
    const std::size_t elided = tstart > hcut ? tstart - hcut : 0;
    std::string out;
    out.reserve(hcut + (s.size() - tstart) + 48);
    out.append(s, 0, hcut);
    out.append("\n… [").append(std::to_string(elided)).append(" bytes elided] …\n");
    out.append(s, tstart, std::string::npos);
    return out;
}

// 渲染 ONE消息到其转录块（头文件+剪切的文本+折叠的工具行）。
// 没有内容的合成 view-only 卡（
// smart_routing）将被完全跳过 - 它们将是空洞的噪音。
// 对于不贡献任何内容的消息，返回一个空字符串。
std::string render_message_md(const Message& m) {
    if (m.smart_routing) return {};   // zero-content路由遥测
    std::string chunk = "## ";
    chunk += role_to_string(m.role);
    chunk += '\n';
    bool any = false;
    if (!m.text.empty()) {
        chunk += clip_text(tools::util::to_valid_utf8(m.text), kMaxMsgTextBytes);
        chunk += '\n';
        any = true;
    }
    for (const auto& tc : m.tool_calls) {
        chunk += "› tool(";
        chunk += tc.name.value;
        chunk += ')';
        if (!tc.args.is_null()) {
            std::string a = tc.args.dump();
            if (a.size() > 120) { a.resize(tools::util::safe_utf8_cut(a, 120)); a += "…"; }
            chunk += ' ';
            chunk += a;
        }
        chunk += '\n';
        any = true;
    }
    if (!any) return {};   // e.g。一个空的助手占位符
    chunk += '\n';
    return chunk;
}

} // 命名空间结束。

fs::path write_thread_transcript_md(Io io, const Thread& t) {
    // 干净，BOUNDED记录："## user"/"## assistant"
    // 头文件+(剪切的)文本，工具调用折叠成单个`› tool(name)`行。
    // 没有<id>.JSON噪音。体积小且可 grepable，因此叉子就可以便宜地 `read`；
    // rency-biased + size-capped 因此，即使是巨大的父
    // 线程 也会产生有用的工件，而不是 multi-MB 文件。
    //
    // Two-pass用于新近度偏差：渲染最新→最旧，
    // 累积直到达到总预算，然后如果我们丢弃最旧的回合，
    // 则用省略标记发出保留的最旧→最新（自然阅读顺序）切片。
    std::vector<std::string> kept;   // 构建时最新的-first
    std::size_t used = 0;
    std::size_t kept_count = 0;
    bool truncated = false;
    for (auto it = t.messages.rbegin(); it != t.messages.rend(); ++it) {
        std::string chunk = render_message_md(*it);
        if (chunk.empty()) continue;
        if (used + chunk.size() > kMaxTranscriptBytes && !kept.empty()) {
            // 预算达到了，我们至少已经有了最新的转折——停止。
            // (`!kept.empty()`警卫保证我们ALWAYS
            // 保留最新的有内容的消息，即使它本身就超出了预算；
            // 它自己的文本已经被剪辑到kMaxMsgTextBytes。)
            truncated = true;
            break;
        }
        used += chunk.size();
        ++kept_count;
        kept.push_back(std::move(chunk));
    }

    std::string md;
    md.reserve(used + 256);
    md += "# Transcript: ";
    md += (t.title.empty() ? t.id.value : t.title);
    md += '\n';
    md += "# (";
    md += std::to_string(t.messages.size());
    md += " messages total";
    if (truncated) {
        md += "; showing the ";
        md += std::to_string(kept_count);
        md += " most recent — read the parent thread for older turns";
    }
    md += "; read/grep as needed)\n\n";
    if (truncated) {
        md += "_[… older turns elided to keep this transcript bounded; the ";
        md += "newest ";
        md += std::to_string(kept_count);
        md += " messages follow …]_\n\n";
    }
    // 发出最旧→最新(最新-first `kept`的逆向)。
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) md += *it;

    // Write位于线程文件旁边，具有稳定的、可发现的名称。
    const fs::path out = threads_dir() / (t.id.value + ".transcript.md");
    if (!write_json_atomic(io, out, md)) return {};
    return out;
}

static std::string role_to_string(Role r) {
    switch (r) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
    }
    return "user";
}
static Role role_from_string(const std::string& s) {
    if (s == "assistant") return Role::Assistant;
    if (s == "system")    return Role::System;
    return Role::User;
}

json message_to_json(const Message& m) {
    // 皮带-and-suspenders UTF-8磨砂。 Tool 输出和自由
    // 格式文本可以包含来自 nlohmann::json::dump() 拒绝序列化的
    // 任意文件（Latin-1 .htm、Shift-JIS 日志）的原始
    // 字节 — 它会抛出 type_error.316，而我们过去使用的是
    // terminate()。边界处擦洗得如此糟糕，字节永远无法到达dump()。
    // 已经清除 上游 的工具在此仅支付验证成本。
    json j;
    // Round-trip per-message 稳定 ID 等
    // -disk→in-memory 重新加载跨会话保留 缓存 密钥。
    // 在 加载 上缺失时生成新的（请参阅 parse_message），
    // 因此较旧的 线程 可以透明升级 — 无需迁移步骤。
    j["id"] = m.id.value;
    j["role"] = role_to_string(m.role);
    j["text"] = tools::util::to_valid_utf8(m.text);
    j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        m.timestamp.time_since_epoch()).count();
    json tcs = json::array();
    for (const auto& tc : m.tool_calls) {
        json t;
        t["id"] = tc.id;
        t["name"] = tc.name;
        t["args"] = tc.args;
        if (tc.extra_content.is_object() && !tc.extra_content.empty())
            t["extra_content"] = tc.extra_content;
        // Tool OUTPUT是长线程中最大的单个事物——这里一个真实的线
        // 程中跨越5.5k 调用的12 MB，相对于实际对话文本的0.7 MB。
        // 内联，每个 字节 都是每个 加载 上的 re-parsed
        // （使用 JSON 字符串-escape 处理）。
        //
        // 大输出转而进入 blob 存储。截断会更快，
        // 但会破坏用户的历史记录；参考文献保留了 字节 的逐字记录，
        // 同时将它们从 解析 中取出。小输出保持内联
        // - 低于阈值，单独的文件比 保存 花费更多（
        // 一个索引节点、一次打开、一次读取）。
        constexpr std::size_t kOutputBlobMin = 8u * 1024u;
        auto out = tools::util::to_valid_utf8(tc.output()); // 空，除非 终端
        if (out.size() >= kOutputBlobMin) {
            if (auto name = blobs::put(out); !name.empty())
                t["output_blob"] = std::move(name);
            else
                t["output"] = std::move(out);
        } else {
            t["output"] = std::move(out);
        }
        t["status"] = std::string{tc.status_name()};
        // EXECUTION窗口以毫秒为单位持续存在，
        // 而不是time_point：steady_clock偏移量在重新
        // 启动时毫无意义，因此序列化点将归并 "time since boot"
        // 到每个重新加载的线程的统计数据中（2h20m类谎言）。仅限 终端；
        // 归并 已经拒绝 pending/running 跨度。
        // exec_window_at() 读取 executing_since
        // 无需 卡 -birth 回退 — 从未调度的工具必须 序列化
        // NO 窗口，因为重新加载时 回退 (started_at)
        // 甚至无法重建，否则 反序列化 会用假纪元填充它。
        if (tc.is_terminal()) {
            const auto began = tc.exec_window_at();
            const auto done  = tc.finished_at();
            if (began && done.time_since_epoch().count() != 0
                && done > *began)
                t["exec_ms"] = std::chrono::duration_cast<
                    std::chrono::milliseconds>(done - *began).count();
        }
        tcs.push_back(std::move(t));
    }
    j["tool_calls"] = std::move(tcs);
    // 用户消息上的图像附件。 BYTES 存在于 blob 存储 (
    // threads/blobs/<hash>) 中，并且该消息保留引用，
    // 因此 1 MB 屏幕截图在此花费约 40 字节，而不是 base64 的约
    // 1.3 MB，每个未来的 线程 加载 都必须 re-read 和 re-decode。
    // 如果 blob 写入失败，则退回到内联 — 缓慢的 线程
    // 会击败丢失的图像。
    if (!m.images.empty()) {
        json imgs = json::array();
        for (const auto& img : m.images) {
            json e;
            e["media_type"] = img.media_type;
            // 我们已加载但从未具体化的图像是REFERENCE的re
            // -persisted：没有blob读取，没有解码，
            // 没有re-encode。这就是在 线程 第一次写回（每次）时
            // 惰性 加载 不会变成 提前加载 保存 的原因。
            if (!img.materialised()) {
                const auto& s = img.source();
                if (!s.blob.empty()) {
                    e["blob"] = s.blob;
                    imgs.push_back(std::move(e));
                    continue;
                }
                if (!s.b64.empty()) {
                    // 旧版内联base64（在blob存储之前写入）。
                    // 迁移它ONCE：解码、存储为blob，并且从现在开始，
                    // 这个线程携带64-char引用而不是兆字节的base64。
                    // 如果没有这个，文件永远不会缩小，并且每个未来
                    // 的加载都会重新-tokenizes整个载荷。
                    //
                    // 如果 blob 写入失败，则回退到不受影响地写入
                    // base64 — 缓慢的 线程 会击败丢失的图像。
                    if (auto name = blobs::put(util::base64_decode(s.b64));
                        !name.empty())
                        e["blob"] = std::move(name);
                    else
                        e["data"] = s.b64;
                    imgs.push_back(std::move(e));
                }
                continue;
            }
            if (img.bytes().empty()) continue;
            if (auto name = blobs::put(img.bytes()); !name.empty())
                e["blob"] = std::move(name);
            else
                e["data"] = util::base64_encode(img.bytes());
            imgs.push_back(std::move(e));
        }
        j["images"] = std::move(imgs);
    }
    if (m.checkpoint_id) j["checkpoint_id"] = *m.checkpoint_id;
    // 保留 per-message 错误，以便重新打开 线程 显示哪个
    // 回合死亡以及原因。 UTF-8 的擦洗原因与 `text` 相同。
    if (m.error) j["error"] = tools::util::to_valid_utf8(*m.error);
    if (m.is_compact_summary) j["is_compact_summary"] = true;
    // Proactive-retrieval标记+门控它的置信度。坚持这样，
    // 重新加载的 线程 仍然是 渲染 安静的 "Retrieved context"
    // 卡（及其源列表 + 置信栏），而不是显示原始 <retrieved-context> 块，
    // 就好像用户拥有 带类型的 一样。
    if (m.proactive) {
        j["proactive_context"] = true;
        if (m.proactive->confidence)
            j["proactive_confidence"] = *m.proactive->confidence;
    }
    // 叉出处卡。坚持下来，所以重新加载的叉子仍然是 渲染
    // "\u2443 Forked" 事件 卡，并且模型仍然看到成绩单指针（这是一条
    // 真正的 传输 用户消息，所以它必须像一个 round-trip 一样）。
    if (m.fork_note) {
        j["fork_note"] = true;
        if (!m.fork_transcript.empty())
            j["fork_transcript"] = tools::util::to_valid_utf8(m.fork_transcript);
    }
    // 回合出处：ACTUALLY在本回合中服务的模型（智能
    // 模式将其从选择面板选择中路由出去）及其所扮演的角色。
    // 仅在设置时写入，因此non-Smart-Mode 线程增益没有字节。
    if (!m.served_model.empty())
        j["served_model"] = m.served_model.value;
    if (m.served_role)
        j["served_role"] = std::string{smart::role_wire_name(*m.served_role)};
    // Per-turn遥测。一个紧凑的物体，仅在测量时 -
    // 用户转动和前 -telemetry 辅助转动增益没有 字节，
    // 并且缺席仍然可以与重新加载时测量的零区分开。
    //
    // 由于同样的原因，Zero-valued成员被单独省略：
    // 大多数回合没有重试也没有缓存，
    // 因此写入每个字段的成本将超过长线程上的数字的价值。
    if (m.telemetry) {
        const auto& t = *m.telemetry;
        json tj;
        if (t.ttft_ms)             tj["ttft_ms"]   = t.ttft_ms;
        if (t.stream_ms)           tj["stream_ms"] = t.stream_ms;
        if (t.input_tokens)        tj["in"]        = t.input_tokens;
        if (t.output_tokens)       tj["out"]       = t.output_tokens;
        if (t.reasoning_tokens)    tj["reasoning"] = t.reasoning_tokens;
        if (t.cache_read)          tj["cache_r"]   = t.cache_read;
        if (t.cache_creation)      tj["cache_w"]   = t.cache_creation;
        if (t.transient_retries)   tj["retries"]   = t.transient_retries;
        if (t.mid_stream_failures) tj["mid_fail"]  = t.mid_stream_failures;
        if (t.no_progress_failures) tj["stall"]    = t.no_progress_failures;
        if (t.wire_bytes)          tj["bytes"]     = t.wire_bytes;
        // all-zero遥测仍然是MEASUREMENT
        // （运行且无需任何可记录成本的回合），因此即使每个
        // 成员都被省略，密钥也会被写入。扔掉它会在下一次
        // 重新装填时悄悄地将测量的转弯变成未测量的转弯。
        j["telemetry"] = std::move(tj);
    }
    // Adaptive-thinking块(助手在努力设置下转动)。
    // 坚持下来，以便重新加载的线程可以在跟随-up回合中重播它——
    // Anthropic 400秒，其思维块被丢弃的tool_use回合。
    // 思考 载荷 是长 线程 中的第三个批量项（一个真正的 32
    // MB 线程 中的 4.2 MB 块 + 2.6 MB 签名）。
    // 签名是不透明的 base64 斑点——每个签名有数百个 字节，
    // 从未显示，仅重播给提供商。与工具输出的处理方式相同：字
    // 节 逐字转到 blob 存储，并且消息保留引用，
    // 因此它们会留下 per-switch 解析 而不会丢失。
    constexpr std::size_t kTextBlobMin = 8u * 1024u;
    auto put_or_inline = [&](json& obj, const char* key, std::string text) {
        if (text.size() >= kTextBlobMin) {
            if (auto name = blobs::put(text); !name.empty()) {
                obj[std::string{key} + "_blob"] = std::move(name);
                return;
            }
        }
        obj[key] = std::move(text);
    };

    if (!m.thinking.empty())
        put_or_inline(j, "thinking", tools::util::to_valid_utf8(m.thinking));
    if (!m.thinking_signature.empty())
        put_or_inline(j, "thinking_signature", m.thinking_signature);
    // 已结算的"· 3.2s" 头文件仪表的推理持续时间(ms)。
    if (m.reasoning_ms > 0)
        j["reasoning_ms"] = m.reasoning_ms;
    // Per-block(文本，签名)对——当交错思维
    // 产生多个签名块时的权威重放源。上面的旧版对保留了
    // 较旧的-binary 兼容性。
    if (!m.thinking_blocks.empty()) {
        json blocks = json::array();
        for (const auto& tb : m.thinking_blocks) {
            json b;
            put_or_inline(b, "text", tools::util::to_valid_utf8(tb.text));
            put_or_inline(b, "signature", tb.signature);
            if (!tb.redacted_data.empty()) b["redacted_data"] = tb.redacted_data;
            blocks.push_back(std::move(b));
        }
        j["thinking_blocks"] = std::move(blocks);
    }
    // 旧版可见-reasoning 回退（直接填充
    // reasoning_summary、e.g、外部ACP后端的路径）。
    // 如果没有这个，推理块就会从重新加载的 线程 中消失。
    if (!m.reasoning_summary.empty())
        j["reasoning_summary"] = tools::util::to_valid_utf8(m.reasoning_summary);
    // Codex/Responses 加密推理 blob。坚持下来，以便重新加载的
    // 线程 仍然可以在工具轮次中重播链 -of-thought。不透明 base64
    // -ish 密文 (ASCII)，因此不需要 UTF-8 擦除。
    if (!m.reasoning_encrypted.empty())
        j["reasoning_encrypted"] = m.reasoning_encrypted;
    // 铸造这些斑点的站点。坚持WITH：明天
    // 重新加载的线程仍然必须知道哪个后端可以解
    // 密其密文，否则重启后的第一回合会盲目重放。
    if (!m.reasoning_site.empty())
        j["reasoning_site"] = m.reasoning_site;
    if (!m.reasoning_wire_field.empty())
        j["reasoning_wire_field"] = m.reasoning_wire_field;
    // 非-image附件(粘贴/FileRef/符号)。坚持下来，
    // 以便重新加载的 线程 可以重建其 传输 载荷 — 用户的 `text`
    // 带有 标签 占位符，并且模型仅在 `attachment::expand(...)`
    // 在请求 -build 时间将 正文 拼接回来后才能看到
    // 真实内容。 正文 字节 是 base64-encoded，
    // 因为粘贴的文本可以包含任何内容（NUL、单独代理、
    // 控制 字节，否则 UTF-8 擦除会损坏）。
    if (!m.attachments.empty()) {
        json atts = json::array();
        for (const auto& a : m.attachments) {
            json e;
            switch (a.kind) {
                case Attachment::Kind::Paste:   e["kind"] = "paste";   break;
                case Attachment::Kind::FileRef: e["kind"] = "fileref"; break;
                case Attachment::Kind::Symbol:  e["kind"] = "symbol";  break;
                case Attachment::Kind::Image:   e["kind"] = "image";   break;
                case Attachment::Kind::Output:  e["kind"] = "output";  break;
            }
            // BODY是载荷，对于输出附件，它可以是2MB构建日志——在
            // 真实商店中测量，26个输出附件持有base64的4.2MB，
            // 每个加载上的re-decoded，到渲染一个仅读取`name`
            // 的one-line 标签和 `byte_count`。
            // 图像通过将 字节 移动到 blob 存储来解决这个问题；
            // 所有其他类型在这里都得到相同的处理，
            // 因此 "any file type" 花费 线程 文件 ~40 字节，
            // 无论 载荷 大小如何。
            //
            // 与工具输出相同的阈值和相同的 回退：在其之下，
            // 单独的文件花费比它 保存 更高的成本，
            // 并且失败的 blob 写入回退到内联而不是丢失 正文。
            constexpr std::size_t kAttachmentBlobMin = 8u * 1024u;
            // 附件已加载但从未实现是re-persisted BY
            // REFERENCE——没有blob读取、没有解码、
            // 没有re-encode。与图像相同的规则：每次 线
            // 程 第一次写回时，惰性 加载 不得变成 提前加载
            // 保存。
            if (!a.body.materialised() && !a.body.source().blob.empty()) {
                e["body_blob"] = a.body.source().blob;
            } else if (!a.body.materialised() && !a.body.source().b64.empty()) {
                // 传统内联base64：迁移一次，
                // 因此线程文件此后永远停止携带载荷。
                const std::string raw = a.body.bytes();
                if (raw.size() >= kAttachmentBlobMin) {
                    if (auto name = blobs::put(raw); !name.empty())
                        e["body_blob"] = std::move(name);
                    else
                        e["body"] = a.body.source().b64;
                } else {
                    e["body"] = a.body.source().b64;
                }
            } else if (a.body.bytes().size() >= kAttachmentBlobMin) {
                if (auto name = blobs::put(a.body.bytes()); !name.empty())
                    e["body_blob"] = std::move(name);
                else
                    e["body"] = util::base64_encode(a.body.bytes());
            } else {
                e["body"] = util::base64_encode(a.body.bytes());
            }
            if (!a.path.empty())       e["path"]        = a.path;
            if (!a.media_type.empty()) e["media_type"] = a.media_type;
            if (!a.name.empty())       e["name"]        = a.name;
            if (a.line_number > 0)     e["line_number"] = a.line_number;
            e["line_count"] = a.line_count;
            e["byte_count"] = a.byte_count;
            atts.push_back(std::move(e));
        }
        j["attachments"] = std::move(atts);
    }
    return j;
}

// ── 带类型的 解雷剂 ──────────────────────────────────────────────────
// "what does a valid Thread JSON look like."必
// 填字段的一个真实来源因`MissingField`而失败；错误的-type字段失败并显示
// `InvalidValue`；无法识别的鉴别器会失败，并显示 `InvalidVariantTag`。
// 可选字段静默地回退到默认值（时间戳、错误字符串）——
// 这些字段是可恢复的；错过他们不应该杀死整个线程。

std::string DeserializeError::render() const {
    static constexpr std::string_view kind_str[] = {
        "json_parse", "missing_field", "invalid_value",
        "invalid_variant_tag", "io",
    };
    // 将表固定到枚举：添加没有匹配行的DeserializeErrorKind
    // 臂是COMPILE错误，而不是静默out-of-bounds读取。
    // `Io` 是最后一个臂，因此其基础值 + 1 是臂数。
    static_assert(std::size(kind_str)
                      == std::to_underlying(DeserializeErrorKind::Io) + 1u,
                  "kind_str is out of sync with DeserializeErrorKind — "
                  "add the missing row");
    std::string out = "[";
    out += kind_str[std::to_underlying(kind)];
    out += "] ";
    if (!field.empty()) { out += field; out += ": "; }
    out += detail;
    return out;
}

static std::expected<ToolUse::Status, DeserializeError>
parse_tool_status(std::string_view status_tag, std::string&& output,
                  std::uint64_t exec_ms) {
    // 重建变体。持久化的 线程 只会进入 终端 状态（
    // -flight 工具永远不会序列化），因此中间状态重置
    // 为 no-arg-time-stamp 默认值。
    // exec_ms是状态标签旁边的EXECUTION窗口(分派→终端)已保存；
    // 将其重新水合为 SYNTHETIC 时钟窗口 — 基数 = 纪元
    // +1 毫秒，完成 = 基数 + exec_ms — 因此统计
    // 信息 归并 会持续测量工具，而不是重新启动。 Zero/absent
    // = 旧的 线程 文件：将窗口留空，归并 的守卫会跳过样本
    // 而不是计算假跨度。
    if (status_tag == "done" || status_tag == "failed"
        || status_tag == "error") {
        if (exec_ms == 0) {
            if (status_tag == "done")
                return ToolUse::Status{ToolUse::Done{{}, {}, std::move(output)}};
            return ToolUse::Status{ToolUse::Failed{{}, {}, std::move(output)}};
        }
        const auto base = std::chrono::steady_clock::time_point{
            std::chrono::milliseconds{1}};   // non-zero:归并的守卫
                                             // 读取 0 为 "no window"
        const auto finish = base + std::chrono::milliseconds{
            static_cast<std::int64_t>(exec_ms)};   // diff == exec_ms 完全一致
        if (status_tag == "done") {
            ToolUse::Done d{base, finish, std::move(output)};
            d.executing_since = base;
            return ToolUse::Status{std::move(d)};
        }
        ToolUse::Failed fp{base, finish, std::move(output)};
        fp.executing_since = base;
        return ToolUse::Status{std::move(fp)};
    }
    if (status_tag == "rejected") return ToolUse::Status{ToolUse::Rejected{{}}};
    // 持久的线程 SHOULD仅携带终端工具状态，但会话杀死mid
    // -tool(崩溃、SIGKILL、
    // 断电)在磁盘上留下pending/running/approved工具。
    // 这样的工具永远不会完成，也永远不会——强制其进入 终端 失败状态，
    // 因此恢复运行时为 freezable/renderable（
    // run_is_freezable 拒绝任何非 -terminal 工具，
    // 否则会从重新水合的转录本中删除整个跟踪运行）。
    if (status_tag == "running" || status_tag == "approved"
        || status_tag == "pending") {
        std::string note = output.empty() ? "interrupted" : std::move(output);
        if (exec_ms) {
            // 防御性：序列化器只写exec_ms在下面
            // is_terminal()，
            // 所以非-terminal标签永远不应该带有一个——但如果未来的作家这样做了，
            // 请尊重它，而不是默默地报告运行是瞬时的。
            const auto base = std::chrono::steady_clock::time_point{
                std::chrono::milliseconds{1}};
            auto st = ToolUse::Failed{base,
                base + std::chrono::milliseconds{
                    static_cast<std::int64_t>(exec_ms)},
                std::move(note)};
            st.executing_since = base;
            return ToolUse::Status{std::move(st)};
        }
        return ToolUse::Status{ToolUse::Failed{{}, {}, std::move(note)}};
    }
    return std::unexpected(DeserializeError{
        DeserializeErrorKind::InvalidVariantTag, "tool_calls[*].status",
        std::string{"unknown status tag: "} + std::string{status_tag}});
}

std::expected<Message, DeserializeError> message_from_json(const json& j) {
    if (!j.is_object())
        return std::unexpected(DeserializeError{
            DeserializeErrorKind::InvalidValue, "messages[*]",
            "expected object"});
    Message m;
    // `id`于2026-05添加；较旧的 线程 文件不包含它，
    // 因此默认的 -constructed `m.id` （来自
    // new_message_id() 的 -fresh）代表它们。
    // 新的写入将保留这个新的 ID，因此 保存-after-load 完成了迁移。
    if (auto it = j.find("id"); it != j.end() && it->is_string()
        && !it->get<std::string>().empty())
        m.id = MessageId{it->get<std::string>()};
    m.role = role_from_string(j.value("role", "user"));
    m.text = j.value("text", "");
    // 转动出处（model/role实际为其提供服务）。
    // 在该字段存在之前写入的 线程 中缺失 - 视图回落到实时选择，
    // 无论如何，这就是那些轮流用于 渲染 的内容。
    m.served_model = ModelId{j.value("served_model", "")};
    // 无法识别的角色读取为 "no role"，
    // 而不是失败 加载：由知道该角色仍不必打开的
    // 构建编写的 线程，只是没有重音标签。
    m.served_role  = smart::role_from_wire_name(j.value("served_role", ""));
    // Per-turn遥测。不存在用户转动以及该字
    // 段存在之前写入的任何内容 - 这与测量的零
    // 保持可区分，因此统计信息 归并 可以从其分母中
    // 排除未测量的转动，而不是将假零平均到其中。
    if (auto it = j.find("telemetry"); it != j.end() && it->is_object()) {
        const auto& tj = *it;
        Message::Telemetry t;
        auto u32 = [&](const char* k) {
            return static_cast<std::uint32_t>(
                tj.value(k, static_cast<std::uint64_t>(0)));
        };
        auto u16 = [&](const char* k) {
            return static_cast<std::uint16_t>(
                tj.value(k, static_cast<std::uint64_t>(0)));
        };
        t.ttft_ms             = u32("ttft_ms");
        t.stream_ms           = u32("stream_ms");
        t.input_tokens        = u32("in");
        t.output_tokens       = u32("out");
        t.reasoning_tokens    = u32("reasoning");
        t.cache_read          = u32("cache_r");
        t.cache_creation      = u32("cache_w");
        t.transient_retries   = u16("retries");
        t.mid_stream_failures = u16("mid_fail");
        t.no_progress_failures = u16("stall");
        t.wire_bytes          = u32("bytes");
        m.telemetry = t;
    }
    // Blob引用(当前)或内联(旧的线程/回退)。
    auto text_or_blob = [](const json& obj, const char* key) -> std::string {
        if (auto it = obj.find(std::string{key} + "_blob");
            it != obj.end() && it->is_string())
            return blobs::get(it->get<std::string>());
        return obj.value(key, "");
    };
    m.thinking = text_or_blob(j, "thinking");
    m.thinking_signature = text_or_blob(j, "thinking_signature");
    m.reasoning_ms = j.value("reasoning_ms", static_cast<std::int64_t>(0));
    if (auto it = j.find("thinking_blocks"); it != j.end() && it->is_array())
        for (const auto& tb : *it)
            if (tb.is_object())
                m.thinking_blocks.push_back(Message::ThinkingBlock{
                    text_or_blob(tb, "text"), text_or_blob(tb, "signature"),
                    tb.value("redacted_data", "")});
    m.reasoning_summary = j.value("reasoning_summary", "");
    m.reasoning_encrypted = j.value("reasoning_encrypted", "");
    // 在标签存在之前写入的线程上不存在。
    // 空意味着 "we don't know which site minted this"，
    // 而 build_input 在没有匹配的情况下不会重放任何内容 - 因此旧
    // 的 线程 会悄悄地失去推理连续性，而不是在我们无法放置的斑点上冒 400 的风险。
    m.reasoning_site = j.value("reasoning_site", "");
    m.reasoning_wire_field = j.value("reasoning_wire_field", "");
    if (auto it = j.find("error"); it != j.end() && it->is_string()
        && !it->get<std::string>().empty())
        m.error = it->get<std::string>();
    if (j.contains("timestamp")) {
        const auto& ts = j["timestamp"];
        if (!ts.is_number_integer())
            return std::unexpected(DeserializeError{
                DeserializeErrorKind::InvalidValue, "messages[*].timestamp",
                "expected integer seconds-since-epoch"});
        m.timestamp = std::chrono::system_clock::time_point{
            std::chrono::seconds{ts.get<long long>()}};
    }
    if (j.contains("tool_calls")) {
        const auto& arr = j["tool_calls"];
        if (!arr.is_array())
            return std::unexpected(DeserializeError{
                DeserializeErrorKind::InvalidValue, "messages[*].tool_calls",
                "expected array"});
        for (const auto& t : arr) {
            ToolUse tc;
            tc.id = ToolCallId{t.value("id", "")};
            tc.name = ToolName{t.value("name", "")};
            tc.args = t.value("args", json::object());
            if (auto ec = t.find("extra_content"); ec != t.end() && ec->is_object())
                tc.extra_content = *ec;
            // 旧的持久化线程存储状态为int 枚举；
            // 新的使用 ToolUse::status_name() 返回的字符串标记。
            // 接受两者，以便现有的 on-disk 线程 继续加载。
            std::string status_tag = "pending";
            // Blob引用(当前)或内联(旧的线程/回退)。
            std::string output = t.value("output", "");
            if (auto ob = t.value("output_blob", std::string{}); !ob.empty())
                output = blobs::get(ob);
            if (auto it = t.find("status"); it != t.end()) {
                if (it->is_string()) {
                    status_tag = it->get<std::string>();
                } else if (it->is_number()) {
                    static constexpr std::string_view legacy[] = {
                        "pending","approved","running","done","failed","rejected"};
                    int idx = it->get<int>();
                    status_tag = idx >= 0 && idx < static_cast<int>(std::size(legacy))
                        ? std::string{legacy[idx]} : std::string{"pending"};
                }
            }
            auto status = parse_tool_status(status_tag, std::move(output),
                                            t.value("exec_ms",
                                                    static_cast<std::uint64_t>(0)));
            if (!status) return std::unexpected(std::move(status).error());
            tc.status = std::move(*status);
            m.tool_calls.push_back(std::move(tc));
        }
    }
    if (j.contains("checkpoint_id")) {
        const auto& cp = j["checkpoint_id"];
        if (!cp.is_string())
            return std::unexpected(DeserializeError{
                DeserializeErrorKind::InvalidValue, "messages[*].checkpoint_id",
                "expected string"});
        m.checkpoint_id = CheckpointId{cp.get<std::string>()};
    }
    if (j.contains("is_compact_summary")) {
        const auto& v = j["is_compact_summary"];
        if (v.is_boolean()) m.is_compact_summary = v.get<bool>();
    }
    // on-disk形状是嵌套的（仅在主动消息内置信），
    // 现在in-memory形状与其匹配。将它们作为两个独立的密钥读取，
    // 用于允许没有 `proactive_context` 的杂散
    // `proactive_confidence` 对普通消息产生信任；
    // 这种分组使得这种情况变得不可代表性，而不仅仅是不可能。
    if (auto it = j.find("proactive_context");
        it != j.end() && it->is_boolean() && it->get<bool>()) {
        Message::ProactiveContext pc;
        if (auto ct = j.find("proactive_confidence");
            ct != j.end() && ct->is_number())
            pc.confidence = ct->get<double>();
        m.proactive = std::move(pc);
    }
    if (auto it = j.find("fork_note");
        it != j.end() && it->is_boolean())
        m.fork_note = it->get<bool>();
    if (auto it = j.find("fork_transcript");
        it != j.end() && it->is_string())
        m.fork_transcript = it->get<std::string>();
    if (j.contains("images")) {
        const auto& arr = j["images"];
        if (!arr.is_array())
            return std::unexpected(DeserializeError{
                DeserializeErrorKind::InvalidValue, "messages[*].images",
                "expected array"});
        for (const auto& e : arr) {
            if (!e.is_object()) continue;
            ImageContent img;
            img.media_type = e.value("media_type", "image/png");
            auto data_b64 = e.value("data", std::string{});
            // LAZY：记录HOW得到字节，不获取。
            // 解码这里的每个图像是图像-heavy 线程开关的大部分，
            // 并产生视图永远不会读取的字节（它只需要尺寸）；
            // 如果消息是 re-sent，则 传输 按需加载 他们。
            ImageContent::Source src;
            src.blob = e.value("blob", std::string{});
            if (src.blob.empty()) src.b64 = data_b64;
            // 既没有斑点也没有 base64 的条目不携带任何内容——与旧的
            // "decoded to empty" 情况相同，所以丢弃它。
            if (src.empty()) continue;
            img = ImageContent::lazy(std::move(img.media_type), std::move(src));
            m.images.push_back(std::move(img));
        }
    }
    if (j.contains("attachments")) {
        const auto& arr = j["attachments"];
        if (!arr.is_array())
            return std::unexpected(DeserializeError{
                DeserializeErrorKind::InvalidValue, "messages[*].attachments",
                "expected array"});
        for (const auto& e : arr) {
            if (!e.is_object()) continue;
            Attachment a;
            auto kind = e.value("kind", std::string{"paste"});
            if      (kind == "paste")   a.kind = Attachment::Kind::Paste;
            else if (kind == "fileref") a.kind = Attachment::Kind::FileRef;
            else if (kind == "symbol")  a.kind = Attachment::Kind::Symbol;
            else if (kind == "image")   a.kind = Attachment::Kind::Image;
            else if (kind == "output")  a.kind = Attachment::Kind::Output;
            else                        a.kind = Attachment::Kind::Paste;
            // Blob引用(当前)或内联base64(线程在
            // 附件使用Blob存储之前写入)。
            // 两者都会被永久读取——旧的 线程 不需要迁移才能打开。
            //
            // LAZY，出于同样的原因，图像是:标签、渲染器绘制读取`name`、
            // `path`、`byte_count`和`line_count`
            // 以及NEVER、正文(参见attachment::chip_label)。
            // 字节 的唯一使用者是请求 -build 时的
            // attachment::expand()，因此 2 MB 构建日志不需要
            // 任何成本，直到它所属的回合实际上是 re-sent。
            if (auto ref = e.value("body_blob", std::string{}); !ref.empty())
                a.body = LazyBytes::from_blob(std::move(ref));
            else
                a.body = LazyBytes::from_base64(e.value("body", std::string{}));
            a.path        = e.value("path", std::string{});
            a.media_type  = e.value("media_type", std::string{});
            a.name        = e.value("name", std::string{});
            a.line_number = e.value("line_number", 0);
            a.line_count  = e.value("line_count", std::size_t{0});
            a.byte_count  = e.value("byte_count", std::size_t{0});
            m.attachments.push_back(std::move(a));
        }
    }
    return m;
}

// 仅填充id/title/timestamps；将消息留空。
// 由支持 线程 选择面板 的目录 -walking 元数据 加载 使用。
static std::expected<Thread, DeserializeError>
parse_thread_meta_only(const json& j) {
    if (!j.is_object())
        return std::unexpected(DeserializeError{
            DeserializeErrorKind::InvalidValue, "", "expected top-level object"});
    Thread t;
    auto id_str = j.value("id", "");
    if (id_str.empty())
        return std::unexpected(DeserializeError{
            DeserializeErrorKind::MissingField, "id",
            "thread JSON has no `id` field"});
    t.id = ThreadId{std::move(id_str)};
    t.title = j.value("title", "");
    t.forked_from = j.value("forked_from", "");
    if (const auto it = j.find("imported"); it != j.end() && it->is_object()) {
        Thread::Import im;
        auto str = [&](const char* k) {
            auto f = it->find(k);
            return f != it->end() && f->is_string() ? f->get<std::string>() : std::string{};
        };
        im.source     = str("source");
        im.session_id = str("session_id");
        im.path       = str("path");
        if (auto f = it->find("size"); f != it->end() && f->is_number_unsigned())
            im.size = f->get<std::uint64_t>();
        if (auto f = it->find("mtime"); f != it->end() && f->is_number_integer())
            im.mtime = f->get<std::int64_t>();
        if (auto f = it->find("messages"); f != it->end() && f->is_number_unsigned())
            im.messages = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(f->get<std::uint64_t>(), UINT32_MAX));
        if (!im.source.empty()) t.imported = std::move(im);
    }
    // On-disk形式不变：键不存在=继承，否则RagMode的整数。
    // 枚举 之外的任何内容都被视为 "inherit"，
    // 而不是盲目地进行转换 - 损坏的或未来的值不得成为一种模式。
    if (const auto it = j.find("rag_mode_override");
        it != j.end() && it->is_number_integer()) {
        const int v = it->get<int>();
        if (v >= static_cast<int>(store::RagMode::On)
            && v <= static_cast<int>(store::RagMode::Off))
            t.rag_mode_override = static_cast<store::RagMode>(v);
    }
    if (j.contains("created_at"))
        t.created_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{j["created_at"].get<long long>()}};
    if (j.contains("updated_at"))
        t.updated_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{j["updated_at"].get<long long>()}};
    if (const auto it = j.find("renamed_at"); it != j.end() && it->is_number_integer())
        t.renamed_at = it->get<std::int64_t>();
    t.auto_titled = j.value("auto_titled", false);
    t.auto_title_ok = j.value("auto_title_ok", false);
    t.name_is_auto = j.value("name_is_auto", false);
    // Type-checked:一手-edited或未来值不得抛出加载。
    if (const auto it = j.find("workspace"); it != j.end() && it->is_string())
        t.workspace = it->get<std::string>();
    if (const auto it = j.find("preview"); it != j.end() && it->is_string())
        t.preview = it->get<std::string>();
    if (const auto it = j.find("message_count"); it != j.end() && it->is_number_unsigned())
        t.message_count = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(it->get<std::uint64_t>(), UINT32_MAX));
    return t;
}

std::expected<Thread, DeserializeError> thread_meta_from_json(const json& j) {
    auto meta = parse_thread_meta_only(j);
    if (!meta) return meta;
    Thread t = std::move(*meta);
    // 压实：可选的。在该功能存在之前的 线程 上以及尚未压缩的 线
    // 程 上缺失 - 两者与 -disk 上无法区分，
    // 并且都正确地默认为空向量。 Per-field 容差是有意为
    // 之：跳过格式错误的记录（e.g。负 `up_to_index`），
    // 而不是使整个 加载 失败，因为 传输-substitution
    // 帮助程序已经验证了 `up_to_index <= messages.size()`，
    // 并在没有验证时优雅地回退到 "no compaction"
    // — 最坏的情况是用户手动 re-compacts，
    // 而不是丢失整个 线程。
    //
    // NOTE:这里的记录是NOT范围-checked，
    // 因为这个函数故意不知道线程有多少条消息——那是调用者的事，
    // 而在日志格式中，元数据是在单个消息之前读取的。
    // 一旦消息计数已知，下面的 `clamp_compactions`
    // 就会应用界限。
    if (j.contains("compactions") && j["compactions"].is_array()) {
        for (const auto& cj : j["compactions"]) {
            if (!cj.is_object()) continue;
            Thread::CompactionRecord rec;
            if (cj.contains("up_to_index") && cj["up_to_index"].is_number_integer()) {
                auto v = cj["up_to_index"].get<long long>();
                if (v < 0) continue;
                rec.up_to_index = static_cast<std::size_t>(v);
            } else {
                continue;
            }
            if (cj.contains("summary") && cj["summary"].is_string()) {
                rec.summary = cj["summary"].get<std::string>();
            }
            if (cj.contains("created_at") && cj["created_at"].is_number_integer()) {
                rec.created_at = std::chrono::system_clock::time_point{
                    std::chrono::seconds{cj["created_at"].get<int64_t>()}};
            }
            // 测量存在之前写入的记录中不存在。
            // 将它们保留为 0 会使 reclaimed() 返回 nullopt
            // — "unknown"，这是事实 — 而不是捏造的零。
            if (cj.contains("tokens_before") && cj["tokens_before"].is_number_integer())
                rec.tokens_before = cj["tokens_before"].get<int>();
            if (cj.contains("tokens_after") && cj["tokens_after"].is_number_integer())
                rec.tokens_after = cj["tokens_after"].get<int>();
            t.compactions.push_back(std::move(rec));
        }
    }
    return t;
}

// 删除其边界不适合实际已加载的转录本的压缩记录——通常
// 仅在保存中断mid-compaction之后。
// 一旦知道消息计数就应用，因此 Whole-document
// 和日志格式都获得相同的保证。
void clamp_compactions(Thread& t) {
    std::erase_if(t.compactions, [&](const Thread::CompactionRecord& r) {
        return r.up_to_index > t.messages.size();
    });
}

static std::expected<Thread, DeserializeError> parse_thread(const json& j) {
    auto meta = thread_meta_from_json(j);
    if (!meta) return meta;
    Thread t = std::move(*meta);
    for (const auto& mj : j.value("messages", json::array())) {
        auto msg = message_from_json(mj);
        if (!msg) return std::unexpected(std::move(msg).error());
        t.messages.push_back(std::move(*msg));
    }
    clamp_compactions(t);
    return t;
}

// SAX 处理函数从线程 JSON文件中拉出四个top-level元数据字段，
// 而没有按需加载消息数组。目录遍历在启动时对每个文件运行一次；
// 649 个文件位于 ~580 KB，每个先前的 源码树-build
// 路径在中间 json::value_t 分配 的数百个 MB 处达到
// 峰值*并且*让转换后的 Thread::messages 永远存在。
// SAX 为我们提供了 O(file_bytes) 解析 成本，
// 每个文件的实时状态为 O(1)：元数据字段加上深度跟踪需要几十个 字节。
//
// 注意，on-disk键顺序是按字母顺序的（json::dump(2)对键进行排序），
// 因此布局是`created_at, id, messages, title, updated_at`-i.e。
// `title`和`updated_at`到达消息阵列
// “之后”。因此，当数组关闭时，跳过状态必须干净地展开，
// 否则这两个字段将默默丢失。
struct ThreadMetaSax {
    Thread out;
    std::string last_key;
    int depth = 0;        // JSON内当前嵌套深度
    int skip_target = -1; // -1==不跳过；否则当深度 <= skip_target 时恢复
    bool got_id = false;

    bool skipping() const noexcept { return skip_target >= 0; }

    bool key(std::string& v) {
        last_key = std::move(v);
        return true;
    }
    bool string(std::string& v) {
        if (!skipping() && depth == 1) {
            if (last_key == "id") {
                if (v.empty()) return false; // 硬失败 — 调用者将其视为 解析 错误
                out.id = ThreadId{std::move(v)};
                got_id = true;
            } else if (last_key == "title") {
                out.title = std::move(v);
            } else if (last_key == "workspace") {
                out.workspace = std::move(v);
            } else if (last_key == "preview") {
                out.preview = std::move(v);
            }
        }
        return true;
    }
    bool number_integer(std::int64_t v)            { return num(static_cast<long long>(v)); }
    bool number_unsigned(std::uint64_t v)          { return num(static_cast<long long>(v)); }
    bool number_float(double, const std::string&)  { return true; }
    bool num(long long v) {
        if (!skipping() && depth == 1) {
            if (last_key == "created_at")
                out.created_at = std::chrono::system_clock::time_point{
                    std::chrono::seconds{v}};
            else if (last_key == "updated_at")
                out.updated_at = std::chrono::system_clock::time_point{
                    std::chrono::seconds{v}};
            else if (last_key == "renamed_at")
                out.renamed_at = v;
            else if (last_key == "message_count" && v > 0)
                out.message_count = static_cast<std::uint32_t>(
                    std::min<long long>(v, UINT32_MAX));
        }
        return true;
    }
    bool boolean(bool)             { return true; }
    bool null()                    { return true; }
    bool start_object(std::size_t) { return enter_container(); }
    bool end_object()              { return leave_container(); }
    bool start_array(std::size_t)  { return enter_container(); }
    bool end_array()               { return leave_container(); }
    bool binary(json::binary_t&)   { return true; }
    bool parse_error(std::size_t, const std::string&,
                     const json::exception&) { return false; }

    bool enter_container() {
        // 如果我们位于 top-level 对象（深度==1）
        // 并且最新的键是 "messages"，则启动跳过：
        // 当深度回落到 1 时我们将继续。++深度必须到达 之后
        // 此检查，以便消息数组内的嵌套容器不会重新-arm。
        if (!skipping() && depth == 1 && last_key == "messages")
            skip_target = 1;
        ++depth;
        return true;
    }
    bool leave_container() {
        --depth;
        if (skipping() && depth == skip_target)
            skip_target = -1;
        return true;
    }
};

[[nodiscard]] static std::expected<Thread, DeserializeError>
load_thread_meta_file(const fs::path& p) {
    std::ifstream ifs(p);
    if (!ifs) return std::unexpected(DeserializeError{
        DeserializeErrorKind::Io, "", "open failed: " + p.string()});
    ThreadMetaSax sax;
    bool ok = json::sax_parse(ifs, &sax);
    if (!ok || !sax.got_id) {
        return std::unexpected(DeserializeError{
            DeserializeErrorKind::JsonParse, "",
            "metadata sax parse failed: " + p.string()});
    }
    return std::move(sax.out);
}

std::expected<Thread, DeserializeError>
load_thread_file(Io, const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return std::unexpected(DeserializeError{
        DeserializeErrorKind::Io, "", "open failed: " + p.string()});
    // 将整个文件吸成一个字符串，然后从连续的 内存 中取出 解析。
    // nlohmann 的 istream_iterator 路径通过 数据流 的
    // locale/sentry 机器一次读取 DOM 一个字符 - 在
    // multi-MB 转录本上明显慢一些（以这种方式解析 12 MB 线程 约
    // 110 毫秒，阻塞了 线程-switch 上的 状态更新函数）。单个大小的读
    // 取 + json::parse 超过连续的 缓冲区 大约将其减半。
    std::string buf;
    {
        std::error_code ec;
        auto sz = fs::file_size(p, ec);
        if (!ec && sz > 0) {
            buf.resize(static_cast<std::size_t>(sz));
            ifs.read(buf.data(), static_cast<std::streamsize>(sz));
            // 短读取（文件在统计和读取之间收缩）使 末尾 未初始化—
            // —修剪到我们实际得到的，所以我们永远不会交出
            // json::parse 过期 字节。
            buf.resize(static_cast<std::size_t>(ifs.gcount()));
        } else {
            // size_t 未知（pipe/procfs/stat 错误）-回
            // 退到 流式传输 吸食。
            buf.assign(std::istreambuf_iterator<char>(ifs),
                       std::istreambuf_iterator<char>());
        }
    }
    json j;
    try { j = json::parse(buf); }
    catch (const std::exception& e) {
        return std::unexpected(DeserializeError{
            DeserializeErrorKind::JsonParse, "",
            std::string{"json parse failed: "} + e.what()});
    }
    return parse_thread(j);
}

// ── 线程 元数据索引旁车 ────────────────────────────────────
//
// 选择面板仅需要id+标题+created_at+updated_at，
// 但是这些键存在于每个线程文件中的multi-MB `compactions`/
// `messages`数组中的AFTER中，因此元数据-only SAX仍然在每个文件的每个字
// 节的数据流上行走以到达它们——约1秒247-thread / 281 MB 历史记录。
//
// `index.json` 缓存即元数据在一个小文件中：id的映射
// →{title，created_at，updated_at，mtime}。
// 在 加载 上，我们读取索引并信任其记录的 mtime 仍然与 线程
// 文件的 on-disk mtime 匹配的条目；只有自写入索引以来新的
// 或更改的文件才能获取完整的 per-file SAX 解析 （并刷新索引）。
// Delete index.json 和所有 self-heals
// 通过冷路径。这会将热启动变成一次小读取 + N stat()。
namespace {

fs::path thread_index_path() { return threads_dir() / "index.json"; }

// index.json是从该过程的两个THREADS (经由reindex_thread的AsyncWriter
// 工作线程，以及经由delete_thread/load_all_threads的UI/reducer
// 线程)以及从每个其他RUNNING AGENTTY读取-modify-written。
// 两者都是相同的丢失更新：每个人都读取相同的地图，
// 每个人都写自己的过期副本，并且最后登陆的人会丢弃其他人的
// 条目。
//
// 内容 该 ACTUALLY COSTS，测量而不是假设：这个
// 文件是CACHE，不是记录。 load_all_threads
// () 枚举 线程 目录，并且仅信任其记录的 mtime
// 仍与文件的匹配的索引条目；丢失的条目是 缓存 缺失，
// 因此 线程 是 re-parsed，索引是 self-heals。
// 因此，损坏是较慢的 加载 和 选择面板 中短暂的 过期
// 标题 - 不 和消失的 线程。该修复仍然值得拥有（它很便宜，
// 并且该评论用来断言的保证实际上并不有效），
// 但它是一个性能和新鲜度修复，并且声称否则会让下一个读者寻找不在这里
// 的 data-loss 错误。
//
// 单独的互斥量仅关闭in-process的一半——它对于其他进程是不可见的，
// 并且两个agentty窗口是使用该程序的正常方式。一个文件锁单独
// 关闭只跨-process一半：POSIX记录了锁属于进程，
// 所以我们自己的两个线程都走过了它（jaal的file_lock一致性
// 检查13）。 with_shared_file 两者皆可；
// 请参阅 include/agentty/io/shared_file.hpp
// 了解为什么两者都不够。争用为零：这些是启动+ per-save。
persistence::Lane& thread_index_mu() {
    static persistence::Lane m;
    return m;
}

// 临界区为index.json。每对 read_thread_index_locked /
// write_thread_index_locked 都在其中的 一个 内运行：
// 正文(常量持有<file_lane>&，持有，参数...)。
// 正文只接触文件，因此忽略该功能；它就在那里，所以未来需要排名
// 锁 的 正文 得到一个，检查。
template <class F, class... Args>
auto with_thread_index(const lock_levels::below_file_lane& cap, F body, Args... args) {
    return persistence::with_shared_file(cap, thread_index_mu(), thread_index_path(),
                                          body, std::move(args)...);
}

// 碰撞此操作会丢弃每个用户的索引，并在下次启动时重新
// -parses他们的整个历史记录（旧线程的约1GB超过一分钟）。
// 新字段作为可选键进入：没有它们的条目只会读为空。
constexpr int kThreadIndexVersion = 4;

struct IndexEntry {
    std::string title;
    long long   created_at = 0;   // unix秒
    long long   updated_at = 0;   // unix秒
    long long   renamed_at = 0;   // unix 秒，0 = 从未重命名
    bool        auto_titled  = false;   // 名字来源于型号
    bool        name_is_auto = false;   // 清除重命名
    std::string workspace;
    std::string preview;
    std::uint32_t message_count = 0;
    std::string import_source;   // 外来工具密钥，""为本机线程
    std::string import_session;
    std::string import_path;
    long long   import_size  = 0;
    long long   import_mtime = 0;
    long long   import_messages = 0;
    long long   mtime      = 0;   // 文件 last_write_time，自时钟纪元以来的 ns
    long long   size       = -1;  // 文件大小为字节； -1 == 未知（pre-v2）
};

void set_import(IndexEntry& ie, const Thread& t) {
    if (!t.imported) return;
    ie.import_source  = t.imported->source;
    ie.import_session = t.imported->session_id;
    ie.import_path    = t.imported->path;
    ie.import_size    = static_cast<long long>(t.imported->size);
    ie.import_mtime   = t.imported->mtime;
    ie.import_messages = t.imported->messages;
}

// 文件的最后-write时间，自UNIX纪元以来的SECONDS。
//
// 这里有两个独立的问题，并且修复一个而不修复另一个
// 仍然会使 缓存 死掉：
//
//   1.NANOSECONDSOVERFLOWED。 fs::file_time_type 的纪元是
//      实现-defined，并且在libstdc++上，
//      它在1970年之前就已经足够远了，以至于当前-day时间戳转换为纳秒会包装int64并输出负值。
//      因此秒。
//
//   2. 该 EPOCH IS 仍然 不 1970. 直接计算秒数
//      time_since_epoch()不会溢出，但它是从file_clock
//      使用的任何纪元开始测量的——这里大约是-4.65e9，i.e。 1822.
//      这是一个稳定的数字，因此 缓存 可能是偶然工作的；
//      但它在其他工具读取的文件中毫无意义，并且如果标准 库
//      发生变化，它就会发生变化。
//
// clock_cast是可移植转换，并且是C++20添加它的目的。
// 回退是针对在没有强制转换的情况下运送file_clock的一个
// libstdc++版本的：file_clock和system_clock
// 共享一个纪元，因此差异为零并且减法是精确的而不是猜测。
//
// 0仍然意味着"unknown"，load_all_threads将其视为缓存未命中。
// 纳秒的分辨率损失并不重要，因为检查与文件
// 大小配对——参见下面的同伴。
long long file_mtime_secs(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
#else
    const auto sys = std::chrono::system_clock::now() +
                     (t - fs::file_time_type::clock::now());
#endif
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

// 与 file_mtime_secs 一起进行新鲜度检查。单独的 mtime
// 是不够的：mtime 粒度是 filesystem-dependent
// (FAT 2 s, HFS+ 1 s)，因此在同一滴答内重写的 线程 与其
// 记录的时间戳比较相等，并且 缓存 永远为 旧 title/updated_at
// 服务 - 没有其他东西会使它失效。将 mtime 与 size 配对可以
// 捕获更改文件长度的每个相同的 -tick 编辑，对于刚刚增长消息
// 的 JSON 转录本来说，这基本上是所有这些编辑。
// 这反映了 fs_helpers.cpp 已用于 StaleVerdict
// 检查的 mtime+size 规则。出错时返回 -1，因此它不能为实际大小指定别名。
long long file_size_bytes(const fs::path& p) {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    if (ec) return -1;
    return static_cast<long long>(s);
}

// 读取原始索引。 PRECONDITION：在with_thread_index
// ()内部调用。读取和匹配的写入必须位于 一个 关键部分内，
// 否则上述丢失的-update 竞态 会重新打开。
std::unordered_map<std::string, IndexEntry> read_thread_index_locked() {
    std::unordered_map<std::string, IndexEntry> out;
    std::ifstream ifs(thread_index_path());
    if (!ifs) return out;
    try {
        json j; ifs >> j;
        if (!j.is_object()) return out;
        // 两个 mtime bug、两个版本冲突以及在其中一个下写入
        // 的索引是无用的：
        //
        //   自 file_clock 纪元以来的 v2 纳秒 — 溢出
        //       int64 并换行为负数，因此没有条目匹配。
        //   v3 秒，但仍然是从 FILE_CLOCK 的纪元（libstdc
        //       ++ 上的 ~1822）开始，而不是 Unix 纪元。
        //       Self-consistent，所以缓存起作用了；但这个数字对于读取该
        //       文件的其他任何东西来说没有任何意义，并且如果标准 库 移动，它就会移动。
        //   自 Unix 纪元以来 v4 秒，通过 clock_cast。
        //
        // 读取 过期 索引无论如何都会花费一次完整的重新解析，
        // 因此旧的索引会被整个删除，并且此运行会正确地重建它。
        if (j.value("version", 0) < kThreadIndexVersion) return out;
        auto& threads = j.contains("threads") ? j["threads"] : j;
        if (!threads.is_object()) return out;
        for (auto& [id, e] : threads.items()) {
            if (!e.is_object()) continue;
            IndexEntry ie;
            ie.title      = e.value("title", std::string{});
            ie.created_at = e.value("created_at", 0LL);
            ie.updated_at = e.value("updated_at", 0LL);
            ie.renamed_at = e.value("renamed_at", 0LL);
            ie.auto_titled  = e.value("auto_titled", false);
            ie.name_is_auto = e.value("name_is_auto", false);
            ie.workspace  = e.value("workspace", std::string{});
            ie.preview    = e.value("preview", std::string{});
            ie.message_count = static_cast<std::uint32_t>(
                std::clamp<long long>(e.value("message_count", 0LL), 0, UINT32_MAX));
            if (auto im = e.find("imported"); im != e.end() && im->is_object()) {
                ie.import_source  = im->value("source", std::string{});
                ie.import_session = im->value("session_id", std::string{});
                ie.import_path    = im->value("path", std::string{});
                ie.import_size    = im->value("size", 0LL);
                ie.import_mtime   = im->value("mtime", 0LL);
                ie.import_messages = im->value("messages", 0LL);
            }
            ie.mtime      = e.value("mtime", 0LL);
            // v1索引中不存在——-1表示"unknown"，
            // 新鲜度检查将其视为未命中，因此该条目获得re
            // -parsed一次并升级为具有一定大小的v2条目。
            ie.size       = e.value("size", -1LL);
            out.emplace(id, std::move(ie));
        }
    } catch (const std::exception&) {
        // 索引损坏——视为空；冷路重建它。
        out.clear();
    }
    return out;
}

// 原始索引写入。 PRECONDITION：在with_thread_index()内部调用。
void write_thread_index_locked(const std::unordered_map<std::string, IndexEntry>& idx) {
    json threads = json::object();
    for (const auto& [id, e] : idx) {
        json ej;
        ej["title"]      = e.title;
        ej["created_at"] = e.created_at;
        ej["updated_at"] = e.updated_at;
        if (e.renamed_at > 0) ej["renamed_at"] = e.renamed_at;
        if (e.auto_titled)  ej["auto_titled"]  = true;
        if (e.name_is_auto) ej["name_is_auto"] = true;
        if (!e.workspace.empty()) ej["workspace"] = e.workspace;
        if (!e.preview.empty())   ej["preview"]   = e.preview;
        if (e.message_count > 0)  ej["message_count"] = e.message_count;
        if (!e.import_source.empty())
            ej["imported"] = {{"source", e.import_source}, {"session_id", e.import_session},
                              {"path", e.import_path}, {"size", e.import_size},
                              {"mtime", e.import_mtime}, {"messages", e.import_messages}};
        ej["mtime"]      = e.mtime;
        ej["size"]       = e.size;
        threads[id] = std::move(ej);
    }
    json j;
    j["version"] = kThreadIndexVersion;
    j["threads"] = std::move(threads);
    try { (void)write_json_atomic(internal_io(), thread_index_path(), j.dump()); }
    catch (const std::exception&) { /* 最佳-effort 缓存 */ }
}

Thread thread_from_index(const std::string& id, const IndexEntry& e) {
    Thread t;
    t.id    = ThreadId{id};
    t.title = e.title;
    t.created_at = std::chrono::system_clock::time_point{
        std::chrono::seconds{e.created_at}};
    t.updated_at = std::chrono::system_clock::time_point{
        std::chrono::seconds{e.updated_at}};
    t.renamed_at = e.renamed_at;
    t.auto_titled  = e.auto_titled;
    t.name_is_auto = e.name_is_auto;
    t.workspace     = e.workspace;
    t.preview       = e.preview;
    t.message_count = e.message_count;
    if (!e.import_source.empty())
        t.imported = Thread::Import{e.import_source, e.import_session, e.import_path,
                                    static_cast<std::uint64_t>(std::max(0LL, e.import_size)),
                                    e.import_mtime,
                                    static_cast<std::uint32_t>(std::clamp<long long>(
                                        e.import_messages, 0, UINT32_MAX))};
    return t;
}

// 在其文件被（重新）写入之后刷新单个线程的索引条目，在-disk
// mtime上标记NEW，以便下一次启动采用该线程的快速路径
// 而不是重新-parsing它。 Best-effort：索引更新
// 失败仅意味着下次启动时缓慢 解析，然后是 self-heals。
void reindex_thread(const lock_levels::below_file_lane& cap, const Thread& t) {
    // 统计该 线程 实际存在于哪个文件。迁移的 线程 没有 <id>.JSON，
    // 因此无条件地声明将 缓存 mtime=0/size=0 — 并且
    // load_all_threads 将 mt==0 视为 "cache miss"，
    // 使得每个启动 re-parse 每个迁移的 线程 的元数据。
    // .meta.json sidecar 是日志 线程 的目录行走统计信息，
    // 因此它也必须是我们在这里记录的内容。
    const auto meta_file   = threads_dir() / (t.id.value + ".meta.json");
    const auto legacy_file = threads_dir() / (t.id.value + ".json");
    std::error_code ec;
    const auto file = fs::exists(meta_file, ec) ? meta_file : legacy_file;
    // 统计OUTSIDE和锁，然后读取其中的-modify-write，
    // 因此整个更新相对于delete_thread()是原子的。
    const long long mt = file_mtime_secs(file);
    const long long sz = file_size_bytes(file);
    IndexEntry ie;
    ie.title      = t.title;
    ie.created_at = std::chrono::duration_cast<std::chrono::seconds>(
                        t.created_at.time_since_epoch()).count();
    ie.updated_at = std::chrono::duration_cast<std::chrono::seconds>(
                        t.updated_at.time_since_epoch()).count();
    ie.renamed_at = t.renamed_at;
    ie.auto_titled  = t.auto_titled;
    ie.name_is_auto = t.name_is_auto;
    ie.workspace     = t.workspace;
    ie.preview       = t.preview;
    ie.message_count = t.message_count;
    set_import(ie, t);
    ie.mtime      = mt;
    ie.size       = sz;
    with_thread_index(cap, [](const maya::held<lock_levels::file_lane>&, persistence::Held,
                              std::string id, IndexEntry e) {
        auto idx = read_thread_index_locked();
        idx[id] = std::move(e);
        write_thread_index_locked(idx);
    }, t.id.value, std::move(ie));
}

} // 命名空间结束。

std::vector<Thread> backfill_picker_summaries(Io io, const lock_levels::below_file_lane& cap,
                                              const std::vector<ThreadId>& ids) {
    std::vector<Thread> out;
    for (const auto& id : ids) {
        auto full = load_thread_by_id(io, id);
        if (!full) continue;
        refresh_picker_summary(*full);
        // 空的线程保持为0；没有什么可显示的，对 重试 来说也没什么。
        full->messages.clear();
        full->messages.shrink_to_fit();
        reindex_thread(cap, *full);
        out.push_back(std::move(*full));
    }
    return out;
}

std::vector<Thread> load_all_threads(Io, const lock_levels::below_file_lane& cap) {
    // Metadata-only目录遍历，index-accelerated
    // (参见上面的索引边车注释)。逐字使用其 mtime 仍与文件的
    // on-disk mtime 匹配的缓存条目 — 无 open/parse.
    // 只有新的或更改的文件才能获取完整的 SAX 元 解析，
    // 并且如果有任何更改，索引将被重写，以便下次启动再次温暖。
    std::vector<Thread> out;
    std::error_code ec;
    if (!fs::exists(threads_dir(), ec)) return out;

    // 在整个过程中保持：顶部的读取和底部的刷新是一次读取
    // -modify-write，并且中间的保存着陆
    // ——来自该进程OR的另一个实例——不得使其条目被
    // 我们的`fresh` 快照破坏。
    out = with_thread_index(cap, [](const maya::held<lock_levels::file_lane>&, persistence::Held) {
    std::vector<Thread> out;
    std::error_code ec;
    auto index = read_thread_index_locked();
    std::unordered_map<std::string, IndexEntry> fresh;
    fresh.reserve(index.size() + 8);
    bool index_dirty = false;

    for (const auto& e : fs::directory_iterator(threads_dir(), ec)) {
        // 完全跳过子目录。 `blobs/`（
        // 图像/工具-output/思考载荷存储）位于此处，虽然下面的.
        // JSON过滤器今天碰巧排除了它，但这是偶然的——未来名为
        // `foo.json`的sidecar目录将被解析为线程。
        if (e.is_directory(ec)) continue;
        if (e.path().extension() != ".json") continue;
        // acp_sessions.json是ACP服务器的sidecar会话索引(
        // sessionId→{cwd，title，updatedAt})，而不是线程文件；
        // 而index.json就是我们自己的缓存。 线程 都不是——两者都跳过。
        const auto fname = e.path().filename();
        if (fname == "acp_sessions.json" || fname == "index.json") continue;

        // log-format 线程是<id>.jsonl+<id>.ofs+<id>.meta.json。
        // 元数据 sidecar 正是本次遍历所需要的 — 它准确地保存了
        // 选择面板 显示的标题和时间戳，而且它很小 — 因此在这里通过其 .
        // JSON 扩展名拾取它。 "abc.meta.json"
        // 上的 `stem()` 产生 "abc.meta"，
        // 因此后缀被剥离以恢复 线程 id；否则，每个迁移的 线程
        // 都会以虚假的 "<id>.meta" 名称出现在 选择面板 中。
        std::string id = e.path().stem().string();
        static constexpr std::string_view kMetaSuffix = ".meta";
        const bool is_meta = id.size() > kMetaSuffix.size()
                          && id.ends_with(kMetaSuffix);
        if (is_meta) id.resize(id.size() - kMetaSuffix.size());

        // 两种形式可以同时存在，mid-migration：
        // 写入日志并留下遗留文档，直到确认其可读为止。
        // 当两者都存在时，传统的一个被跳过，
        // 因此选择面板显示加载器将返回的相同的线程——
        // 并且一次对话永远不会产生两行。
        if (!is_meta && fs::exists(threads_dir() / (id + ".meta.json"), ec))
            continue;

        const long long   mt = file_mtime_secs(e.path());
        const long long   sz = file_size_bytes(e.path());

        // 快速路径：其mtime AND大小仍然匹配的索引条目——
        // 没有open/parse.大小==-1（v1条目，
        // 或失败的统计）永远不会匹配实际大小，因此这些条目会失败并被升级。
        if (auto it = index.find(id);
            it != index.end() && it->second.mtime == mt && mt != 0
            && it->second.size == sz && sz >= 0) {
            out.push_back(thread_from_index(id, it->second));
            fresh.emplace(id, it->second);
            continue;
        }

        // 慢速路径：新的或更改的文件-SAX-parse元数据+重新索引。
        auto loaded = load_thread_meta_file(e.path());
        if (loaded) {
            IndexEntry ie;
            ie.title      = loaded->title;
            ie.created_at = std::chrono::duration_cast<std::chrono::seconds>(
                                loaded->created_at.time_since_epoch()).count();
            ie.updated_at = std::chrono::duration_cast<std::chrono::seconds>(
                                loaded->updated_at.time_since_epoch()).count();
            ie.renamed_at = loaded->renamed_at;
            ie.auto_titled  = loaded->auto_titled;
            ie.name_is_auto = loaded->name_is_auto;
            ie.workspace     = loaded->workspace;
            ie.preview       = loaded->preview;
            ie.message_count = loaded->message_count;
            set_import(ie, *loaded);
            ie.mtime      = mt;
            ie.size       = sz;
            fresh.emplace(id, std::move(ie));
            index_dirty = true;
            out.push_back(std::move(*loaded));
        } else {
            // 记录并跳过——损坏的或模式定义-incompatible文件不会终止目录遍历的其余部分。
            // 任何观看 stderr 的人都可以看到 带
            // 类型的 类型；想要严格的 加载 的编程调用者
            // 可以直接使用 load_thread_file。
            std::fprintf(stderr,
                "agentty: skipping %s — %s\n",
                e.path().string().c_str(),
                loaded.error().render().c_str());
        }
    }

    // 修剪文件消失(删除线程)的条目并保留
    // 刷新的索引以用于下一次热启动。
    if (fresh.size() != index.size()) index_dirty = true;
    if (index_dirty) write_thread_index_locked(fresh);
    return out;
    });

    std::sort(out.begin(), out.end(), [](const Thread& a, const Thread& b){
        return a.updated_at > b.updated_at;
    });
    return out;
}

// 同步工作线程 —构建JSON、fsync、atomic-rename。
// 公共 `save_thread` 入口点排队到后台编写器上，
// 而不是在调用者的 线程 上运行；序列化 200-message 转录本
// 可能需要 50-200 毫秒，并且每次阻塞 状态更新函数 都会冻结
// UI -finalize。仅由下面的工作线程调用； 工作线程 每个 id
// 最多保留一个 待处理 线程（较新的 保存 获胜），因此两个 Finalize
// -back-to-back 调用 最多执行一次磁盘写入操作。
json thread_meta_to_json(const Thread& t) {
    json j;
    j["id"] = t.id;
    j["title"] = t.title;
    if (!t.forked_from.empty()) j["forked_from"] = t.forked_from;
    if (t.imported)
        j["imported"] = {{"source", t.imported->source},
                         {"session_id", t.imported->session_id},
                         {"path", t.imported->path},
                         {"size", t.imported->size},
                         {"mtime", t.imported->mtime},
                         {"messages", t.imported->messages}};
    if (t.rag_mode_override)
        j["rag_mode_override"] = static_cast<int>(*t.rag_mode_override);
    j["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        t.created_at.time_since_epoch()).count();
    j["updated_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        t.updated_at.time_since_epoch()).count();
    if (t.renamed_at > 0) j["renamed_at"] = t.renamed_at;
    if (t.auto_titled) j["auto_titled"] = true;
    if (t.auto_title_ok) j["auto_title_ok"] = true;
    if (t.name_is_auto) j["name_is_auto"] = true;
    if (!t.workspace.empty()) j["workspace"] = t.workspace;
    if (!t.preview.empty())   j["preview"]   = t.preview;
    if (t.message_count > 0)  j["message_count"] = t.message_count;
    // 传输-only压缩记录。坚持这些可以让重新加载的
    // 线程 继续发送它在关闭之前发送的 相同 传输 载荷
    // — 如果用户在第 40 回合压缩然后关闭应用程序，
    // 则重新加载后的下一个请求仍会总结 [0, 40) 前缀，
    // 而不是重新发送所有 40 个原始回合并破坏上下文。空
    // for threads that have never been compacted (the common case);
    // 旧的 on-disk 线程 早于该字段，
    // 并且阅读器将其默认为空，因此升级是透明的。
    if (!t.compactions.empty()) {
        json comps = json::array();
        for (const auto& c : t.compactions) {
            json cj;
            cj["up_to_index"] = c.up_to_index;
            cj["summary"]     = tools::util::to_valid_utf8(c.summary);
            cj["created_at"]  = std::chrono::duration_cast<std::chrono::seconds>(
                                    c.created_at.time_since_epoch()).count();
            // 仅当测量时。将 0/0 写入未测量的记录将使
            // "we didn't record this" 与
            // "this compaction reclaimed nothing" 无法区分
            // ——而那些人需要相反的反应。
            if (c.tokens_before > 0 && c.tokens_after > 0) {
                cj["tokens_before"] = c.tokens_before;
                cj["tokens_after"]  = c.tokens_after;
            }
            comps.push_back(std::move(cj));
        }
        j["compactions"] = std::move(comps);
    }
    return j;
}

// 将 线程 序列化为传统 整个-DOCUMENT 形式：元
// 数据加上内联 "messages" 数组。为尚未移至日志的
// 线程 保留，并在日志写入失败时保留为 回退。
static bool save_thread_legacy(const Thread& t) {
    json j = thread_meta_to_json(t);
    json msgs = json::array();
    for (const auto& m : t.messages) {
        // 智能模式路由卡是view-only遥测（无传输内容）
        // ——从不保留它们，就像它们从未发送到模型一样。
        if (m.smart_routing) continue;
        msgs.push_back(message_to_json(m));
    }
    j["messages"] = std::move(msgs);
    // dump()在非-UTF-8 字节上抛出type_error.316。
    // 在 message_to_json 中进行擦洗应该已经捕获了所有内容，
    // 但是将异常吞下，作为皮带 -and-suspenders 防范未来的回归 - 默默
    // 地 -skipped 保存 击败了进程 -terminating 未捕获的抛出。
    try {
        return write_json_atomic(internal_io(), threads_dir() / (t.id.value + ".json"),
                                 j.dump(2));
    } catch (const nlohmann::json::exception& e) {
        AGT_LOG(Persist, Error, "thread.save", "result=json_error id={} err={}",
                t.id.value, e.what());
        return false;
    }
}

// 保存负责的消息——i.e。减去 view-only 智能
// 模式路由卡，这些路由卡永远不会保留，也不会发送。
static std::vector<Message> persistable(const std::vector<Message>& in) {
    std::vector<Message> out;
    out.reserve(in.size());
    for (const auto& m : in)
        if (!m.smart_routing) out.push_back(m);
    return out;
}

// 刚刚写入的内容是否按照我们想要写入的内容读回？
//
// 这是让保存 DELETE成为遗留文件的门。
// 保存 每回合都会运行，一旦 .JSON 消失，
// 编写器中默默地丢弃或损坏消息的错误将无法恢复 - 因此日志是从
// 磁盘重新 -read 并在删除任何内容之前进行比较。
//
// 故意比较用户会注意到丢失的字段，而不是 字节 差异：
// re-serialising 不保证稳定（blob
// 升级可能会使 载荷 在进入过程中脱线），并且 字节
// 比较将在非数据丢失的差异上失败。
// 这里的裸布尔在实践中是一个死胡同：保存 路径无缘无故
// 地在真正的 线程 上记录了 "log verification FAILED"，
// 然后突发地自行愈合。这是诊断可能具有的最糟糕的情况
// - 它命名了一种症状，并保留了告诉编写者错误（数据丢失，
// 必须保留遗留文件）所需的所有事实，
// 除了良性的 竞态（线程 在我们验证它时增长）。
// 因此，比较报告 WHERE 存在分歧，在 传输 审计
// 使用的相同词汇中：类型、索引和两个值。
struct LogMismatch {
    enum class Kind {
        None,
        Count,       // 不同数量的消息
        Id,
        Role,
        Text,
        Thinking,
        TollCallCount,
        ToolOutput,
        ImageCount,
    };
    Kind        kind  = Kind::None;
    std::size_t index = 0;   // 消息索引，或者Count的消息计数
    std::string want;        // 我们想写什么
    std::string got;         // 回读什么

    [[nodiscard]] bool ok() const noexcept { return kind == Kind::None; }

    [[nodiscard]] std::string_view kind_name() const noexcept {
        switch (kind) {
            case Kind::None:          return "none";
            case Kind::Count:         return "message_count";
            case Kind::Id:            return "id";
            case Kind::Role:          return "role";
            case Kind::Text:          return "text";
            case Kind::Thinking:      return "thinking";
            case Kind::TollCallCount: return "tool_call_count";
            case Kind::ToolOutput:    return "tool_output";
            case Kind::ImageCount:    return "image_count";
        }
        return "unknown";
    }
};

// 长字段进入日志； 200 KB 工具输出不得。
static std::string brief(std::string_view s) {
    constexpr std::size_t kMax = 80;
    if (s.size() <= kMax) return std::string{s};
    return std::string{s.substr(0, kMax)} + "…(" + std::to_string(s.size()) + "B)";
}

// on-disk格式承诺为一个字段返回什么。
//
// 两个变换是INTENTIONAL并且不能被读取为数据丢失：
//
//  1.UTF-8擦洗。 message_to_json 遍历每个字符串
//     to_valid_utf8，因此内存中的单独代理或
//     截断序列返回修复。将原始 内存 与文件 标志 进行比较，
//     当编写者执行其工作时，该文件已损坏。
//
//  2.处于-flight刀具状态。 保存 在每个回合都会开火，包括当
//     工具仍在运行，并且读者故意将`pending`/`running`
//     调用强制为失败{"interrupted"}——在
//     -tool中死亡的进程不得以永远等待的调用重新加载。因此，根据设计，
//     output() 是磁盘上 内存 和 "interrupted" 中的 ""。
//
// 这是在现场 线程 中看到的 "log verification FAILED"
// 爆发的实际原因：它们带着飞行中的工具在转弯处着陆，
// 并在完成后愈合。由于格式保证的差异，
// 大门拒绝废弃旧文档。
static bool round_trips(std::string_view want, std::string_view got) {
    return got == want || got == tools::util::to_valid_utf8(std::string{want});
}

// 该调用是否在快照被拍摄之前完成？只有 终端
// 调用 有输出，该文件预计会逐字重现。
static bool settled(const ToolUse& t) noexcept {
    return std::holds_alternative<ToolUse::Done>(t.status)
           || std::holds_alternative<ToolUse::Failed>(t.status)
           || std::holds_alternative<ToolUse::Rejected>(t.status);
}

// 将读回的内容（`got`)与要写入的内容（`want`)
// 进行比较，两者都从线程的消息`base`开始。
// 由完整的 保存（基数 0，整个日志）和 末尾 保存（仅新行）共享。
static LogMismatch log_diff_range(const std::vector<Message>& got,
                                  const std::vector<Message>& want,
                                  std::size_t base) {
    using K = LogMismatch::Kind;
    if (got.size() != want.size())
        return {K::Count, base + want.size(), std::to_string(base + want.size()),
                std::to_string(base + got.size())};
    for (std::size_t j = 0; j < got.size(); ++j) {
        const std::size_t i = base + j;
        const auto& a = want[j];
        const auto& b = got[j];
        if (a.id.value != b.id.value)
            return {K::Id, i, a.id.value, b.id.value};
        if (a.role != b.role)
            return {K::Role, i, role_to_string(a.role), role_to_string(b.role)};
        if (!round_trips(a.text, b.text))
            return {K::Text, i, brief(a.text), brief(b.text)};
        if (!round_trips(a.thinking, b.thinking))
            return {K::Thinking, i, brief(a.thinking), brief(b.thinking)};
        if (a.tool_calls.size() != b.tool_calls.size())
            return {K::TollCallCount, i, std::to_string(a.tool_calls.size()),
                    std::to_string(b.tool_calls.size())};
        for (std::size_t k = 0; k < a.tool_calls.size(); ++k) {
            // 允许未解决的调用作为读者的中断
            // 形式返回。检查它会导致瞬变。
            if (!settled(a.tool_calls[k])) continue;
            if (!round_trips(a.tool_calls[k].output(), b.tool_calls[k].output()))
                return {K::ToolOutput, i, brief(a.tool_calls[k].output()),
                        brief(b.tool_calls[k].output())};
        }
        if (a.images.size() != b.images.size())
            return {K::ImageCount, i, std::to_string(a.images.size()),
                    std::to_string(b.images.size())};
    }
    return {};
}

static LogMismatch log_diff(const ThreadLog& log, const std::vector<Message>& want) {
    return log_diff_range(log.all(), want, 0);
}

// 如果 线程 现在以 LOG 格式位于磁盘上并且该日志已被验证，
// 则返回 true。增量写入者仅在此处为 true 后才信任其
// per-thread 簿记； false 表示下一个 保存 必须已满。
static bool save_thread_sync(const lock_levels::below_file_lane& cap, const Thread& t) {
    if (t.id.empty() || t.messages.empty()) return false;

    // ── Write 日志,然后退出遗留文件 ──────
    //
    // 线程一次移动一个，因为它们被使用：没有迁移通行证，
    // 也没有标志日，所以历史的597个MB永远不会立即飞行。
    // 用户再也不会打开的 线程 会永远保留，并且仍然是 加
    // 载 — 阅读器永久保留两条路径（约 105 行，
    // 冻结），对于无法丢失历史记录来说，这是一个微不足道的
    // 代价。
    //
    // 顺序：写入该对→从磁盘上读取BACK→比较→然后才删除.
    // JSON。任何步骤中的任何失败都会使遗留文件
    // 准确地留在原来的位置，因此最坏的情况是本轮未
    // 迁移的 线程，而不是损坏的 线程。
    const auto legacy_path = threads_dir() / (t.id.value + ".json");
    std::error_code ec;

    // open_for_write:写入和检查都不需要元
    // 数据sidecar，其保存整个压缩历史。
    auto log = ThreadLog::open_for_write(t.id);
    if (log && log->store_thread(t)) {
        // Re-open从头开始，因此验证读取FILES，
        // 而不是刚刚写入它们的in-memory状态。
        auto check = ThreadLog::open_for_write(t.id);
        const LogMismatch diff =
            check ? log_diff(*check, persistable(t.messages))
                  : LogMismatch{LogMismatch::Kind::Count, 0, "log", "unopenable"};
        if (diff.ok()) {
            if (fs::exists(legacy_path, ec)) {
                fs::remove(legacy_path, ec);
                AGT_LOG(Persist, Info, "thread.save",
                        "migrated id={} messages={} (legacy document retired)",
                        t.id.value, t.messages.size());
            } else {
                AGT_LOG(Persist, Debug, "thread.save",
                        "result=ok format=log id={} messages={}",
                        t.id.value, t.messages.size());
            }
            // 头文件写入set_meta，其可能携带比t更新的重命名。
            Thread written = log->meta();
            reindex_thread(cap, written.id.empty() ? t : written);
            return true;
        }
        // 已写入，但未正确读回。保留遗留文件（写在下面）
        // 并大声说出来——这是日志编写器的一个错误，
        // 用户的历史记录是绝对不能为此付出代价的
        // 东西。
        AGT_LOG(Persist, Error, "thread.save",
                "log verification FAILED id={} messages={} kind={} index={} "
                "want={} got={} — keeping the legacy document",
                t.id.value, t.messages.size(), diff.kind_name(), diff.index,
                diff.want, diff.got);
    } else if (!log) {
        AGT_LOG(Persist, Warn, "thread.save",
                "could not open a log for id={} — falling back to legacy",
                t.id.value);
    } else {
        AGT_LOG(Persist, Warn, "thread.save",
                "log write failed for id={} — falling back to legacy",
                t.id.value);
    }

    // 回退:历史全部-document写入。
    const bool ok = save_thread_legacy(t);
    // 失败的保存丢失了用户的对话，屏幕上没有任何内容
    // 可以说明这一点——最糟糕的一种无声失败，
    // 并且之前是不可见的，因为结果被丢弃。
    if (!ok)
        AGT_LOG(Persist, Error, "thread.save",
                "result=write_failed id={} messages={}",
                t.id.value, t.messages.size());
    else
        AGT_LOG(Persist, Debug, "thread.save",
                "result=ok format=legacy id={} messages={}",
                t.id.value, t.messages.size());
    // 将元数据索引与我们刚刚编写的文件一起保留在锁-step中，
    // 以便下次启动的快速路径拾取它。
    reindex_thread(cap, t);
    return false;
}

// ── 递增的保存 ────────────────────────────────────────────────
//
// 保存 在每个模型回合结束时触发，它用于写入 整个 线程:
// re-encode 每条消息，fsync 完整日志，
// 然后 解析 将其全部返回（从磁盘读取每个 blob）以进行验证。
// 在 30 MB 线程 上，每轮需要数十 MB 工作，
// 因此长代理循环运行的时间越长，速度就越慢。几乎不需要这些：开放回合
// 之前的历史记录在两轮之间不会改变。
//
// 因此，写入者根据 线程 保留最后放入日志中的每条消息
// 的廉价 指纹。 保存 找到 指纹 发生变化的第一
// 条消息（通常是开放转弯或刚刚稳定的工具），
// 在那里切割日志（O(1)，偏移量已知）并从该点开始追加。
// 仅读回并验证附加的行。
//
// 任何重写历史的事情（压缩、分叉、编辑、倒带、
// 视觉错误时图像清晰）都会改变早期的指纹或计数，
// 因此它回落到与之前相同的完整写入。在此过程中以及
// 发生任何故障后，还会为 线程 的第一个 保存 获取
// 完整路径（尚不知道这些文件）。只有在经过验证的日志写入后，
// 簿记才可信，因此最坏的情况是一个额外的完整 保存，
// 而不是错误的文件。

// 指纹的一切message_to_json仍然存在。散列内容（不仅仅是大小），
// 因此相同长度的 in-place 编辑仍然显示，但不触及任何斑点，
// 不执行 I/O 并且不分配任何内容。
//
// 这在REDUCER上运行在每个保存上的线程的每条消息上，
// 因此其吞吐量是保存成本的下限。单个累加器使每个 8
// -byte 步骤等待前一个乘法（串行依赖链，~5 GB/s）；
// 下面的批量循环在每次迭代中运行 FOUR 独立通道
// 超过 32 字节，CPU 可以重叠，并且 归并 最后将
// 它们组合在一起（测量到约 22 GB/s，4.1x）。
// 不是加密的：它只需要注意到消息改变了，并且没有任何
// 对抗性的东西向它提供。
struct Fnv {
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    // 散装路径的独立通道，由finish()折叠成`h`。
    std::uint64_t l1 = 0xBF58476D1CE4E5B9ull;
    std::uint64_t l2 = 0x94D049BB133111EBull;
    std::uint64_t l3 = 0x2545F4914F6CDD1Dull;
    static constexpr std::uint64_t kMul = 0xFF51AFD7ED558CCDull;
    void word(std::uint64_t w) noexcept {
        h ^= w;
        h *= kMul;
        h ^= h >> 32;
    }
    void bytes(const void* p, std::size_t n) noexcept {
        const auto* c = static_cast<const unsigned char*>(p);
        std::size_t i = 0;
        // 四通道，每次迭代 32 字节。每个通道的乘法仅取决
        // 于其自身先前的值，因此它们是管道式的。
        for (; i + 32 <= n; i += 32) {
            std::uint64_t w0, w1, w2, w3;
            std::memcpy(&w0, c + i,      8);
            std::memcpy(&w1, c + i +  8, 8);
            std::memcpy(&w2, c + i + 16, 8);
            std::memcpy(&w3, c + i + 24, 8);
            h  = (h  ^ w0) * kMul;
            l1 = (l1 ^ w1) * kMul;
            l2 = (l2 ^ w2) * kMul;
            l3 = (l3 ^ w3) * kMul;
        }
        for (; i + 8 <= n; i += 8) {
            std::uint64_t w;
            std::memcpy(&w, c + i, 8);
            word(w);
        }
        std::uint64_t last = 0;
        if (i < n) std::memcpy(&last, c + i, n - i);
        word(last ^ (static_cast<std::uint64_t>(n - i) << 56));
    }
    void str(std::string_view s) noexcept { word(s.size()); bytes(s.data(), s.size()); }
    template <class N> void num(N v) noexcept {
        std::uint64_t w = 0;
        std::memcpy(&w, &v, sizeof v < 8 ? sizeof v : 8);
        word(w);
    }
    // 折叠车道。每个泳道都必须达到结果，
    // 否则只有其中一个泳道的内容不同就会指纹相同。
    [[nodiscard]] std::uint64_t finish() const noexcept {
        std::uint64_t x = h ^ (l1 + 0x9E3779B97F4A7C15ull)
                            ^ (l2 << 1) ^ (l3 >> 1);
        x ^= x >> 33; x *= kMul;
        x ^= x >> 29;
        return x;
    }
};

// A 惰性 载荷 (图像，附件正文)。来自磁盘的 载荷 由其源（
// blob 名称/旧版 base64）进行标识，并通过相同的引用
// 进行持久化，因此对源进行哈希处理是准确的，并且不会将 加载 与
// 字节 进行比较。故意忽略 materialised()：视图
// 可以随时解析另一个 线程 上的图像，并且不能使旧消息看起来已被编辑。
// 只有原始的 载荷（新鲜的糊状物）才会散列它们的 字节。
template <class Payload>
static void fp_payload(Fnv& f, const Payload& p) {
    const auto& src = p.source();
    if (!src.blob.empty() || !src.b64.empty()) {
        f.num(1); f.str(src.blob); f.str(src.b64);
    } else {
        f.num(2); f.str(p.bytes());
    }
}

static std::uint64_t message_fingerprint(const Message& m) {
    Fnv f;
    f.str(m.id.value);
    f.num(static_cast<int>(m.role));
    f.str(m.text);
    f.num(m.timestamp.time_since_epoch().count());
    f.str(m.served_model.value);
    f.num(m.served_role.has_value());
    if (m.served_role) f.num(static_cast<int>(*m.served_role));
    f.str(m.thinking);
    f.str(m.thinking_signature);
    f.num(m.reasoning_ms);
    f.str(m.reasoning_encrypted);
    f.str(m.reasoning_site);
    f.str(m.reasoning_wire_field);
    f.str(m.reasoning_summary);
    f.num(m.thinking_blocks.size());
    for (const auto& b : m.thinking_blocks) {
        f.str(b.text); f.str(b.signature); f.str(b.redacted_data);
    }
    f.num(m.tool_calls.size());
    for (const auto& tc : m.tool_calls) {
        f.str(tc.id.value);
        f.str(tc.name.value);
        f.str(tc.args_dump());
        if (!tc.extra_content.is_null()) f.str(tc.extra_content.dump());
        f.str(tc.status_name());
        f.str(tc.output());
        f.num(tc.done_images().size());
        for (const auto& img : tc.done_images()) {
            f.str(img.media_type);
            fp_payload(f, img);
        }
    }
    f.num(m.images.size());
    for (const auto& img : m.images) {
        f.str(img.media_type);
        fp_payload(f, img);
    }
    f.num(m.attachments.size());
    for (const auto& a : m.attachments) {
        f.num(static_cast<int>(a.kind));
        fp_payload(f, a.body);
        f.str(a.path); f.str(a.media_type); f.str(a.name);
        f.num(a.line_number); f.num(a.line_count); f.num(a.byte_count);
    }
    f.num(m.checkpoint_id.has_value());
    if (m.checkpoint_id) f.str(m.checkpoint_id->value);
    f.num(m.error.has_value());
    if (m.error) f.str(*m.error);
    f.num(m.is_compact_summary);
    f.num(m.proactive.has_value());
    if (m.proactive) {
        f.num(m.proactive->confidence.has_value());
        if (m.proactive->confidence) f.num(*m.proactive->confidence);
    }
    f.num(m.telemetry.has_value());
    if (m.telemetry) {
        const auto& t = *m.telemetry;
        f.num(t.ttft_ms); f.num(t.stream_ms); f.num(t.input_tokens);
        f.num(t.output_tokens); f.num(t.reasoning_tokens); f.num(t.cache_read);
        f.num(t.cache_creation); f.num(t.transient_retries);
        f.num(t.mid_stream_failures); f.num(t.no_progress_failures);
        f.num(t.wire_bytes);
    }
    f.num(m.fork_note);
    f.str(m.fork_transcript);
    return f.finish();
}

// 作者所知道的是在日志中一个线程：每个持久化消息
// 一个指纹，按照日志顺序。仅在验证写入后设置。
struct LogState {
    std::vector<std::uint64_t> fps;
};

// 排队的保存。 `from` 是第一个可能与日志不同的
// 索引（在可持久消息中）； `tail` 就是这些消息。
// `full` (from==0)重写从`meta`+`tail`开始的整个日志。
// `retitle`标记重命名：仅设置meta.id和meta.title，
// write_job重写存储的标题，仅此而已。
struct SaveJob {
    Thread                     meta;         // 仅 头文件，消息为空
    std::size_t                from = 0;
    std::vector<Message>       tail;
    std::vector<std::uint64_t> fps;          // ALL消息的指纹
    bool                       full = false;
    bool                       retitle = false;
};

// 在`from`处切割日志后编码+附加[from，end)，
// 然后仅验证新行。任何失败都返回 false；
// 然后调用者执行完整的 保存，这也会修复留下的任何内容。
static bool save_tail_sync(const SaveJob& job, std::size_t total) {
    auto log = ThreadLog::open_for_write(job.meta.id);
    if (!log || !log->exists() || log->size() < job.from) return false;
    if (!log->truncate_to(job.from)) return false;
    for (const auto& m : job.tail)
        if (!log->append(m)) return false;
    if (!log->set_meta(job.meta)) return false;
    if (log->size() != total) return false;

    // 与完整路径相同的门，范围为该保存所写的内容。
    // 通过新打开读回 FILES 中的附加行。
    auto check = ThreadLog::open_for_write(job.meta.id);
    if (!check) return false;
    const auto got = check->range(job.from, check->size());
    const LogMismatch diff = log_diff_range(got, job.tail, job.from);
    if (!diff.ok()) {
        AGT_LOG(Persist, Error, "thread.save",
                "tail verification FAILED id={} from={} kind={} index={} "
                "want={} got={} — falling back to a full save",
                job.meta.id.value, job.from, diff.kind_name(), diff.index,
                diff.want, diff.got);
        return false;
    }
    AGT_LOG(Persist, Debug, "thread.save",
            "result=ok format=log mode=tail id={} from={} wrote={} messages={}",
            job.meta.id.value, job.from, job.tail.size(), total);
    return true;
}

// Store是线程的新标题，单独保留其消息和时间戳。
// 日志 线程 仅重写其小 .meta.json。
// 遗留文档无处可写，因此它是 已保存 整体，这也会将其移动到日志中。
// 在 线程 的部分中运行，就像它的所有其他写入一样。
static bool retitle_sync(const lock_levels::below_file_lane& cap,
                         const ThreadId& id, const std::string& title,
                         std::int64_t renamed_at, bool name_is_auto) {
    std::error_code ec;
    if (fs::exists(threads_dir() / (id.value + ".meta.json"), ec)) {
        auto log = ThreadLog::open(id);
        if (!log) {
            AGT_LOG(Persist, Error, "thread.rename", "result=unopenable id={} err={}",
                    id.value, log.error().render());
            return false;
        }
        Thread meta = log->meta();
        if (meta.id.value.empty()) meta.id = id;
        meta.title = title;
        meta.renamed_at = renamed_at;
        meta.name_is_auto = name_is_auto;
        if (!log->set_meta(meta)) {
            AGT_LOG(Persist, Error, "thread.rename", "result=write_failed id={}", id.value);
            return false;
        }
        reindex_thread(cap, meta);
        return true;
    }
    const auto legacy = threads_dir() / (id.value + ".json");
    if (fs::exists(legacy, ec)) {
        auto loaded = load_thread_file(internal_io(), legacy);
        if (!loaded) {
            AGT_LOG(Persist, Error, "thread.rename", "result=unreadable id={} err={}",
                    id.value, loaded.error().render());
            return false;
        }
        loaded->title = title;
        loaded->renamed_at = renamed_at;
        loaded->name_is_auto = name_is_auto;
        // 记录其自身的故障，并刷新每个路径上的索引。
        return save_thread_sync(cap, *loaded);
    }
    AGT_LOG(Persist, Warn, "thread.rename", "result=missing id={}", id.value);
    return false;
}

// 每个线程的日志一条通道；文件 锁（在该部分内）是每个 线程，
// 因此保存 不同 线程 的两个 agentty 永远不会互相等待。
persistence::LaneOf<lock_levels::thread_log>& thread_log_lane() {
    static persistence::LaneOf<lock_levels::thread_log> l;
    return l;
}

// 一项排空作业，在其 线程 部分内：末尾 路径，并且在失败时
// 从已验证的头进行完全重写；或完整的 保存。
static bool write_job(const maya::held<lock_levels::thread_log>& cap, SaveJob& next,
                      std::size_t total) {
    if (next.retitle) return retitle_sync(cap, next.meta.id, next.meta.title, next.meta.renamed_at,
                                          next.meta.name_is_auto);
    if (next.full) {
        Thread t = next.meta;
        t.messages = std::move(next.tail);
        return save_thread_sync(cap, t);
    }
    if (save_tail_sync(next, total)) return true;
    // 末尾写入中途失败。 `from` 之前的头是已确认、
    // 已验证的状态（该部分将所有其他写入器排除在外），
    // 因此从它重建整个 线程 并将其全部写入，这也会修复 末尾 留下
    // 的任何内容。
    if (auto log = ThreadLog::open_for_write(next.meta.id); log && log->size() >= next.from) {
        Thread t = next.meta;
        t.messages = log->range(0, next.from);
        if (t.messages.size() == next.from) {
            for (const auto& m : next.tail) t.messages.push_back(m);
            if (save_thread_sync(cap, t)) return true;
        }
    }
    // 头也不见了（原木被清空或在我们背后更换）。
    // 该作业只携带末尾，所以不能写全；呼叫者掉落 `known`，
    // 这使得 下一个 保存 充满并治愈它。
    AGT_LOG(Persist, Error, "thread.save",
            "tail save failed and log head unreadable id={} from={} — next save will be full",
            next.meta.id.value, next.from);
    return false;
}

// ── 自动同步写入器 ─────────────────────────────────────────────────────
//
// 单一背景线程+由ThreadId键控的合并待处理-map。
// `save_thread(t)`将t插入到地图中并向工作线程
// 发出信号； 工作线程 通过一次重复提取一个条目并在其上运行
// `save_thread_sync` 来耗尽映射。
// 当 工作线程 正忙时，同一 线程 的两个 保存 到达，
// 崩溃为最新 快照 的一个磁盘写入 — 映射已按键进行重复数据删除。
// 不同 线程 的两个 保存 各写入一次。
//
// 生命周期：工作线程在第一个保存上延迟启动并在进程的生命周期内运行。
// 关闭时没有 `flush + join`，因为 状态更新函数 的
// Quit 处理函数 在 maya 返回之前发出了最终的 save_thread()；
// 我们等待队列在 flush_and_stop() 内耗尽，
// 即 maya::run 返回后的 `main` 调用。
// 状态更新函数和排水管工作线程共享一切。普通数据，因此它存在于受保护的环境中；
// 工作线程 与 wait_with 一起睡在上面。
//
// 锁命令：状态为LEAF。下面的每个 正文 都是这些映射上的几行，
// 调用 没有 锁（缓慢的工作——编码、fsync、重新索引——在
// 正文 之间运行），因此 enqueue() 可以从任何 锁-free
// 调用者调用，并且排水 工作线程 永远不会在 IO 上保留它。
// 运行排水管的工作线程组是锁中的NOT：它在破坏时加入，
// 因此jaal在守卫内拒绝它。它是 writer 的普通成员，
// 启动一次并在退出时停止一次。
struct WriterState {
    std::unordered_map<std::string, SaveJob> pending;
    // Per-thread知识日志。 状态更新函数 读取它来构
    // 建 末尾-only 作业； 工作线程 在验证 保存 之后写入。
    // 在删除和任何失败时丢弃。
    std::unordered_map<std::string, LogState> known;
    // 每个 线程 的最新 保存 ACCEPTED 的指纹（排队或完成）。
    // 增量必须扩展它。与 `known` 一起掉落，所以下一个已满。
    std::unordered_map<std::string, std::vector<std::uint64_t>> latest;
    // 最后(标题，updated_at)按照线程写到index.json。
    std::unordered_map<std::string, std::string> indexed;
    // 线程在其保存为mid-write时被删除。
    // delete_thread() 直接删除排队的 保存，
    // 但已经写入的文件会在删除后登陆其文件并将 线程 带回来； 工作线程
    // 完成后会将其撤消。 保存 排队 之后 删除是一个新的 保存 并登陆。
    std::string undo;
    std::string in_flight;
    bool        stopping = false;
    bool        started  = false;   // 排水作业已发布
};

using WriterStateLock = maya::guarded<WriterState>;
WriterStateLock& async_writer_state() {
    static WriterStateLock s;
    return s;
}

// 在编写器之后定义：delete_thread()和编写器对
// 与删除竞争的保存的撤消都删除了线程的文件和通过它的索引行。
void remove_thread_files(const lock_levels::below_thread_log& cap, const ThreadId& id);

// 头文件和保存作业携带：除消息之外的每个字段，加上从中得出的选择面板摘要。
// 逐个字段，因为 `meta = t; meta.messages.clear()`
// 会首先深入 -copy 成绩单。
Thread thread_header(const Thread& t) {
    Thread h;
    h.id                = t.id;
    h.title             = t.title;
    h.forked_from       = t.forked_from;
    h.imported          = t.imported;
    h.rag_mode_override = t.rag_mode_override;
    h.created_at        = t.created_at;
    h.updated_at        = t.updated_at;
    h.renamed_at        = t.renamed_at;
    h.auto_titled       = t.auto_titled;
    h.auto_title_ok     = t.auto_title_ok;
    h.name_is_auto      = t.name_is_auto;
    h.workspace         = t.workspace;
    h.compactions       = t.compactions;
    h.preview           = t.preview;
    h.message_count     = t.message_count;
    summarise_for_picker(t.messages, h);
    return h;
}

ThreadDelta describe_save(const Thread& t, const std::vector<std::uint64_t>& prev) {
    ThreadDelta d;
    d.header = thread_header(t);
    d.fps.reserve(t.messages.size());
    for (const auto& m : t.messages)
        if (!m.smart_routing) d.fps.push_back(message_fingerprint(m));
    const std::size_t n = std::min(prev.size(), d.fps.size());
    while (d.from < n && prev[d.from] == d.fps[d.from]) ++d.from;
    std::size_t i = 0;
    for (const auto& m : t.messages) {
        if (m.smart_routing) continue;
        if (i++ >= d.from) d.tail.push_back(m);
    }
    return d;
}

struct AsyncWriter {
    // 在构造函数中到达，因此状态静态首先被构建并且
    // 在这之后被销毁。
    WriterStateLock& st = async_writer_state();

    // 排水工作线程:加入且没有停止截止时间，因此每个排队的保存都着陆。
    // 已发布的插槽，而不是 锁：无论谁翻转 `started`（在
    // `st` 下），都将创建并发布它； flush_and_stop
    // 把它拿出来加入其中，后来的 保存 又开始了一个新的。 flush()
    // 仅等待队列为空，因此正常刷新不会重新启动 线程。
    maya::published<util::WorkerGroup> worker;

    // 对所描述的保存进行排队。增量仅在该 线程 (`latest`)
    // 接受的最新 保存 之上有效：其第一个 `from`
    // FPS 必须匹配。排队的作业会与其合并而不是被替换，
    // 因为增量本身并不携带排队作业更改的内容。当增量没有基础
    // 可继续时，返回 false（没有排队）。
    [[nodiscard]] bool enqueue(const maya::below<maya::leaf>& cap, ThreadDelta d, bool full) {
        full = full || d.from == 0;
        const std::string key = d.header.id.value;
        const auto r = st.with(cap, [](WriterState& s, std::string k, ThreadDelta d, bool full)
                                        -> std::optional<bool> {
            if (!full) {
                const auto it = s.latest.find(k);
                if (it == s.latest.end() || it->second.size() < d.from
                    || !std::equal(d.fps.begin(), d.fps.begin() + static_cast<std::ptrdiff_t>(d.from),
                                   it->second.begin()))
                    return std::nullopt;
            }
            s.latest[k] = d.fps;
            SaveJob j;
            j.meta = std::move(d.header);
            j.fps  = std::move(d.fps);
            j.full = full;
            j.from = full ? 0 : d.from;
            j.tail = std::move(d.tail);
            if (auto p = s.pending.find(k); p != s.pending.end() && !p->second.retitle) {
                // 将排队作业更改的内容保留在我们的 `from` 下面。
                SaveJob& q = p->second;
                if (!full && q.from < j.from) {
                    q.tail.resize(j.from - q.from);
                    for (auto& m : j.tail) q.tail.push_back(std::move(m));
                    j.tail = std::move(q.tail);
                    j.from = q.from;
                    j.full = q.full;
                } else if (!full) {
                    j.full = q.full;   // q.full表示q.from==0==j.from
                }
                q = std::move(j);
            } else {
                // 待处理重标题被折叠：头文件带有状态更新函数
                // 现在所具有的标题。
                s.pending.insert_or_assign(k, std::move(j));
            }
            if (s.started) return false;
            s.started = true;
            return true;
        }, key, std::move(d), full);
        if (!r) return false;
        if (*r) start_worker();
        return true;
    }

    // 线程-at-once 保存(测试、ACP、子代理)：最后
    // 接受的任何内容的增量，或者当没有基础时整个线程。
    void enqueue(const maya::below<maya::leaf>& cap, const Thread& t) {
        auto prev = st.read(cap, [](const WriterState& s, std::string k) {
            const auto it = s.latest.find(k);
            return it == s.latest.end() ? std::vector<std::uint64_t>{} : it->second;
        }, t.id.value);
        const bool full = prev.empty();
        if (enqueue(cap, describe_save(t, prev), full)) return;
        (void)enqueue(cap, describe_save(t, {}), true);
    }

    void forget(const maya::below<maya::leaf>& cap, const std::string& key) {
        st.with(cap, [](WriterState& s, std::string k) { s.known.erase(k); s.latest.erase(k); }, key);
    }

    // 线程 正在被删除：删除其排队的 保存，并让 工作线程
    // 撤消它现在正在写入的内容（如果有）。
    void cancel_saves(const maya::below<maya::leaf>& cap, const std::string& key) {
        st.with(cap, [](WriterState& s, std::string k) {
            s.known.erase(k);
            s.latest.erase(k);
            s.indexed.erase(k);
            s.pending.erase(k);
            if (s.in_flight == k) s.undo = std::move(k);
        }, key);
    }

    // 仍然排队的该线程的保存是用旧标题构建的并且将其
    // 写回重命名，因此它采用新标题而不是其他任何东西排队。
    // 保存 已经在 -write 中，
    // 没有 待处理 条目：重标题在其后面排队并排在第二位。
    void retitle(const maya::below<maya::leaf>& cap, const ThreadId& id, std::string title,
                 std::int64_t renamed_at, bool name_is_auto) {
        const bool start = st.with(cap, [](WriterState& s, std::string k, std::string t,
                                           std::int64_t at, bool na) {
            if (auto it = s.pending.find(k); it != s.pending.end()) {
                it->second.meta.title = t;
                it->second.meta.renamed_at = at;
                it->second.meta.name_is_auto = na;
                return false;
            }
            SaveJob j;
            j.meta.id    = ThreadId{k};
            j.meta.title = std::move(t);
            j.meta.renamed_at = at;
            j.meta.name_is_auto = na;
            j.retitle    = true;
            s.pending.emplace(std::move(k), std::move(j));
            if (s.started) return false;
            s.started = true;
            return true;
        }, id.value, std::move(title), renamed_at, name_is_auto);
        if (start) start_worker();
    }

    // 已保存或按此进程排队：线程某人已打开。
    [[nodiscard]] bool live(const maya::below<maya::leaf>& cap, const std::string& key) {
        return st.read(cap, [](const WriterState& s, std::string k) {
            return s.known.contains(k) || s.pending.contains(k);
        }, key);
    }

    // 等待直到该调用之前排队的所有内容都在磁盘上。
    // 工作线程继续运行；返回后排队的 保存 是调用者的。
    void flush(const maya::unlocked& cap) {
        st.wait_with(cap,
            [](const WriterState& s) {
                return (s.pending.empty() && s.in_flight.empty()) || !s.started;
            },
            [](WriterState&) {});
    }

    // 进程退出（或测试的干净状态）：排出、加入工作线程，
    // 并让写入器为稍后的保存做好准备。 `started` 保持
    // true 直到连接完成，因此 保存 会排队到耗尽的 工作线程 上；
    // run() 退出后滑入的 保存 会得到一个新的。
    void flush_and_stop(const maya::unlocked& cap) {
        st.with(cap, [](WriterState& s) { s.stopping = true; });
        if (auto g = worker.take()) g->stop(cap);
        const bool leftover = st.with(cap, [](WriterState& s) {
            s.stopping = false;
            s.started  = !s.pending.empty();
            return s.started;
        });
        if (leftover) start_worker();
    }

    // 静态销毁，在main之后。 run() 等待 "pending or stopping"，
    // ~WorkerGroup 会永远加入它，所以先设置停止。
    // 不能将 `unlocked` 交给析构函数；它需要根：这个文件调用
    // exit()中没有来自锁 正文（列入允许清单：cap_mint）的内容。
    ~AsyncWriter() {
        if (!worker.current()) return;
        maya::thread_root([this](const maya::unlocked& cap) {
            st.with(cap, [](WriterState& s) { s.stopping = true; });
            if (auto g = worker.take()) g->stop(cap);
        });
    }

private:
    void start_worker() {
        // 只有翻转`started`的呼叫者才能到达这里，
        // 因此一次最多有一个工作线程在线。
        auto g = std::make_shared<util::WorkerGroup>("persistence.async_writer");
        g->post([](const maya::unlocked& job_cap, std::stop_token) { run(job_cap); });
        worker.publish(std::move(g));
    }

    struct Next { std::string key; SaveJob job; bool dropped = false; };

    static void run(const maya::unlocked& cap);

    // 选择面板索引保存的字段：标题、标题标志和updated_at。
    // updated_at 是磁盘上的第二个 -resolution，
    // 因此按该粒度进行比较。
    static std::string index_key(const Thread& t) {
        return t.title + '\x1f' + (t.auto_titled ? 'a' : '-') + (t.name_is_auto ? 'n' : '-')
             + std::to_string(t.renamed_at) + '\x1f'
             + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                   t.updated_at.time_since_epoch()).count());
    }

    static void maybe_reindex(const maya::unlocked& cap, const Thread& meta);
};

static AsyncWriter& async_writer() {
    static AsyncWriter w;
    // 像所有其他线程子系统一样，在静态析构函数之前加入。
    static const auto registered = [] {
        return util::teardown::on_shutdown("persistence.async_writer",
            [](const maya::unlocked& cap) { async_writer().flush_and_stop(cap); });
    }();
    (void)registered;
    return w;
}

void AsyncWriter::maybe_reindex(const maya::unlocked& cap, const Thread& meta) {
    const bool changed = async_writer().st.with(cap,
        [](WriterState& s, std::string id, std::string k) {
            auto it = s.indexed.find(id);
            if (it != s.indexed.end() && it->second == k) return false;
            s.indexed[id] = k;
            return true;
        }, meta.id.value, index_key(meta));
    if (changed) reindex_thread(cap, meta);
}

void AsyncWriter::run(const maya::unlocked& cap) {
    auto& st = async_writer().st;
    for (;;) {
        // Drain-then-stop:即使设置了`stopping`，
        // 我们也会继续弹出直到`pending`为空，因此排队的退出
        // 状态更新函数的最终快照总是落地。在同一步骤中标记飞行中的关键
        // 可以防止 enqueue() 在它还看不到的结果上构建 末尾。
        auto taken = st.wait_with(cap,
            [](const WriterState& s) { return !s.pending.empty() || s.stopping; },
            [](WriterState& s) -> std::optional<Next> {
                if (s.pending.empty()) return std::nullopt;
                auto it = s.pending.begin();
                Next n{it->first, std::move(it->second)};
                s.pending.erase(it);
                // 末尾被切割在磁盘上最后一个保存之上。
                // 如果保存失败，则头部不是末尾所假设的：
                // 丢弃它，并且下一个保存被拒绝并且整个发送。
                if (!n.job.full && !n.job.retitle) {
                    const auto k = s.known.find(n.key);
                    const auto from = static_cast<std::ptrdiff_t>(n.job.from);
                    if (k == s.known.end() || k->second.fps.size() < n.job.from
                        || !std::equal(n.job.fps.begin(), n.job.fps.begin() + from,
                                       k->second.fps.begin())) {
                        s.latest.erase(n.key);
                        s.known.erase(n.key);
                        n.dropped = true;
                        return n;
                    }
                }
                s.in_flight = n.key;
                return n;
            });
        if (!taken) return;
        if (taken->dropped) {
            AGT_LOG(Persist, Warn, "thread.save",
                    "id={} tail dropped: its base never reached disk; next save will be full",
                    taken->key);
            continue;
        }
        auto& key  = taken->key;
        auto& next = taken->job;

        // 在写入器状态之外运行，因此 enqueue() 从不等待 fsync，
        // 并且在 线程 自己的部分内运行，因此保存相同 线程
        // 的第二个 agentty 等待轮到它而不是交错附加。
        bool ok = false;
        // 下面的记账需要什么，在工作搬入之前进行。
        const bool        was_full = next.full;
        const bool        retitle  = next.retitle;
        const Thread      meta     = next.meta;
        const std::optional<std::string> full_idx =
            was_full ? std::optional<std::string>{index_key(next.meta)}
                     : std::nullopt;
        std::vector<std::uint64_t> fps = std::move(next.fps);
        try {
            ok = persistence::with_shared_file(cap, thread_log_lane(),
                threads_dir() / (meta.id.value + ".jsonl"),
                [](const maya::held<lock_levels::thread_log>& h, persistence::Held,
                   SaveJob job, std::size_t total) { return write_job(h, job, total); },
                std::move(next), fps.size());
            // 选择面板索引仅在其显示的内容发生变化(标题、
            // 时间戳)时或者在第一个保存时才需要刷新。
            // 它是一个 JSON 文件的 read-modify-write
            // 覆盖每个 线程，所以每一轮都这样做纯粹是浪费。
            // 重命名已经刷新了它，从真正的头文件；它的 `meta`
            // 是一个没有时间戳的存根，并且不能到达索引。
            // 头文件 被读回，因为磁盘上较新的重命名可能保留了其标题。
            if (ok && !was_full && !retitle) {
                Thread shown = meta;
                if (auto on_disk = load_thread_meta_file(threads_dir() / (meta.id.value + ".meta.json"));
                    on_disk && on_disk->renamed_at > shown.renamed_at) {
                    shown.title        = on_disk->title;
                    shown.renamed_at   = on_disk->renamed_at;
                    shown.name_is_auto = on_disk->name_is_auto;
                }
                maybe_reindex(cap, shown);
            }
        }
        catch (const std::exception& e) {
            // best-effort，与同步路径相同的策略
            util::dbglog("persistence.async_save", e.what());
        }
        catch (...) { util::dbglog("persistence.async_save", "non-std exception"); }

        std::optional<std::string> idx;
        if (ok && full_idx) idx = full_idx;
        const bool undo = st.with(cap, [](WriterState& s, std::string k, bool good, bool rt,
                   std::vector<std::uint64_t> fps, std::optional<std::string> ik) {
            // 在该 保存 写入时删除：它刚刚写入的文件不得在删除后存活。
            // 保持飞行直到它们消失，否则 flush()
            // 可能会带着它们仍然在磁盘上返回。
            if (!s.undo.empty() && s.undo == k) { s.undo.clear(); return true; }
            s.in_flight.clear();
            if (rt) {
                // 重命名不会影响消息，因此作者对日志的
                // 了解仍然有效；它没有携带 `fps`
                // 来替换它。索引行在缓存下发生了变化。
                s.indexed.erase(k);
                return false;
            }
            if (!good) { s.known.erase(k); s.latest.erase(k); }
            else       s.known[k].fps = std::move(fps);
            if (ik) s.indexed[k] = std::move(*ik);
            return false;
        }, key, ok, retitle, std::move(fps), std::move(idx));
        if (undo) {
            AGT_LOG(Persist, Info, "thread.save",
                    "id={} deleted mid-save; removing what the save wrote", key);
            remove_thread_files(cap, ThreadId{key});
            st.with(cap, [](WriterState& s) { s.in_flight.clear(); });
        }
    }
}

void save_thread(Io, const maya::below<maya::leaf>& cap, const Thread& t) {
    if (t.id.empty() || t.messages.empty()) return;
    async_writer().enqueue(cap, t);
}

bool save_thread_delta(Io, const maya::below<maya::leaf>& cap, ThreadDelta d) {
    if (d.header.id.empty() || d.fps.empty()) return true;
    return async_writer().enqueue(cap, std::move(d), false);
}

void rename_thread(Io, const maya::below<maya::leaf>& cap, const ThreadId& id, std::string title,
                   std::int64_t renamed_at, bool name_is_auto) {
    if (id.empty()) return;
    async_writer().retitle(cap, id, std::move(title), renamed_at, name_is_auto);
}

// 线程在磁盘上拥有的文件及其索引行。
// 由 delete_thread() 和异步写入器对 保存 进行的竞态删除共享。
void remove_thread_files(const lock_levels::below_thread_log& cap, const ThreadId& id) {
    // 在 线程 的部分中，因此另一个 agentty 中的 保存 不能在
    // 我们要删除的文件的 re-creating 过程中进行。
    persistence::with_shared_file(cap, thread_log_lane(), threads_dir() / (id.value + ".jsonl"),
        [](const maya::held<lock_levels::thread_log>&, persistence::Held, std::string v) {
            std::error_code ec;
            fs::remove(threads_dir() / (v + ".json"), ec);
            fs::remove(threads_dir() / (v + ".transcript.md"), ec);
            // 日志格式：.jsonl、.ofs、.meta.json。
            if (auto log = ThreadLog::open(ThreadId{v})) log->remove();
        }, id.value);
    // Blob在线程 (content-addressed)之间共享，
    // 因此它们被留给blob gc的mark-and-sweep。索引条目现在
    // 位于其 锁 下，因此 选择面板 不显示任何幽灵行。
    with_thread_index(cap, [](const maya::held<lock_levels::file_lane>&, persistence::Held,
                              std::string key) {
        auto idx = read_thread_index_locked();
        if (idx.erase(key) > 0) write_thread_index_locked(idx);
    }, id.value);
}

void flush_pending_saves(Io, const maya::unlocked& cap) {
    async_writer().flush(cap);
}

namespace {
fs::path subagent_path(const ThreadId& id) {
    return threads_dir() / "subagents" / (id.value + ".json");
}
}  // 命名空间结束。

bool save_subagent_thread(Io io, const Thread& t) {
    if (t.id.value.empty()) return false;
    std::error_code ec;
    fs::create_directories(threads_dir() / "subagents", ec);
    json j = thread_meta_to_json(t);
    json msgs = json::array();
    for (const auto& m : t.messages)
        if (!m.smart_routing) msgs.push_back(message_to_json(m));
    j["messages"] = std::move(msgs);
    try {
        return write_json_atomic(io, subagent_path(t.id), j.dump(2));
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::optional<Thread> load_subagent_thread(Io io, const ThreadId& id) {
    // Id是16个十六进制数字；其他任何东西都不会在这里命名文件。
    if (id.value.empty() || id.value.find_first_not_of("0123456789abcdef") != std::string::npos)
        return std::nullopt;
    std::error_code ec;
    if (!fs::exists(subagent_path(id), ec)) return std::nullopt;
    auto t = load_thread_file(io, subagent_path(id));
    if (!t) return std::nullopt;
    return std::move(*t);
}

std::uint64_t debug_message_fingerprint(const Message& m) {
    return message_fingerprint(m);
}

void debug_forget_saved(const maya::below<maya::leaf>& cap, const ThreadId& id) {
    async_writer().forget(cap, id.value);
}

std::optional<Thread> load_thread_by_id(Io io, const ThreadId& id) {
    if (id.value.empty()) return std::nullopt;

    // 优选日志。自格式登陆以来，线程 仅在 已保存 后才有一个，
    // 因此这是每 -thread 迁移前沿，
    // 而不是 标志 天 - 两个读者都无限期地存活。
    if (auto log = ThreadLog::open(id); log && log->exists()) {
        // ...除非日志是EMPTY并且遗留文档
        // 仍然有内容。这种组合意味着出现了问题
        // （日志被截断、迁移中断、杂散文件），
        // 并且默默地返回空的 线程 看起来就像用户的
        // 历史记录被删除一样。后退花费 解析 并且不会
        // 损失任何东西。
        std::error_code ec;
        const auto legacy = threads_dir() / (id.value + ".json");
        if (log->empty() && fs::exists(legacy, ec)) {
            AGT_LOG(Persist, Warn, "thread.load",
                    "empty log for id={} but a legacy document exists — "
                    "using the legacy document", id.value);
        } else {
            Thread t = log->load_thread();
            // 其元数据sidecar丢失的日志仍然知道其自己的id。
            if (t.id.value.empty()) t.id = id;
            AGT_LOG(Persist, Debug, "thread.load",
                    "format=log id={} messages={}", id.value, t.messages.size());
            return t;
        }
    }

    auto p = threads_dir() / (id.value + ".json");
    auto loaded = load_thread_file(io, p);
    if (!loaded) return std::nullopt;
    AGT_LOG(Persist, Debug, "thread.load", "format=legacy id={} messages={}",
            id.value, loaded->messages.size());
    return std::move(*loaded);
}

void delete_thread(Io, const lock_levels::below_thread_log& cap, const ThreadId& id) {
    // 取消 首次：删除此删除之前排队的保存，因此它无法覆盖下面的删除。
    // 一个已经 mid-write 的任务在完成时
    // 被工作人员撤消。
    async_writer().cancel_saves(cap, id.value);
    remove_thread_files(cap, id);
}

ExpireStats expire_threads(Io io, const lock_levels::below_thread_log& cap, int keep_days,
                           const ThreadId& keep, bool dry_run) {
    ExpireStats st;
    if (keep_days <= 0) return st;
    const fs::path dir = threads_dir();
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::days{keep_days};

    // 线程的文件共享其id作为词干；其中最新的是它的最后一个活动。
    // ID 是十六进制，因此会跳过其他任何内容（index.json、blobs/）。
    // file_time_type{} 是时钟的纪元（libstdc++ 上为 2174 年），而不是 "oldest"。
    struct Seen { fs::file_time_type newest = fs::file_time_type::min(); std::uintmax_t bytes = 0; };
    std::unordered_map<std::string, Seen> by_id;
    std::error_code ec;
    for (fs::directory_iterator it{dir, ec}, end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const std::string name = it->path().filename().string();
        const auto dot = name.find('.');
        if (dot == 0 || dot == std::string::npos) continue;
        const std::string id = name.substr(0, dot);
        if (!std::ranges::all_of(id, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            continue;
        auto& s = by_id[id];
        const auto mt = it->last_write_time(fec);
        if (!fec && mt > s.newest) s.newest = mt;
        const auto sz = it->file_size(fec);
        if (!fec) s.bytes += sz;
    }
    for (const auto& [id, s] : by_id) {
        if (id == keep.value || s.newest >= cutoff || async_writer().live(cap, id)) continue;
        ++st.threads;
        st.bytes += s.bytes;
        if (!dry_run) delete_thread(io, cap, ThreadId{id});
    }
    // 为编辑保留的子代理运行以同样的方式老化。
    std::error_code sec;
    for (fs::directory_iterator it{dir / "subagents", sec}, end; !sec && it != end; it.increment(sec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const auto mt = it->last_write_time(fec);
        if (fec || mt >= cutoff) continue;
        st.bytes += it->file_size(fec);
        if (!dry_run) fs::remove(it->path(), fec);
    }
    if (st.threads > 0)
        AGT_LOG(Persist, Info, "threads.expire", "{} {} thread(s) older than {}d, {} KB",
                dry_run ? "would delete" : "deleted", st.threads, keep_days, st.bytes / 1024);
    return st;
}

int thread_keep_days(Io) {
    const char* v = std::getenv("AGENTTY_THREADS_KEEP_DAYS");
    if (!v || !*v) return 0;
    char* end = nullptr;
    const long n = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || n <= 0) return 0;
    return static_cast<int>(std::min<long>(n, 365L * 100));
}

// 本过程最后的LOADED来自磁盘的原始JSON。
//
// save_settings中合并的基线：我们没有触及其值仍然
// 等于该值的键，因此它的并发编写器版本必须获胜，而不是被我们在
// 启动时读取的内容覆盖。如果没有基线，唯一的选项是
// "clobber everything"（错误）或
// "merge nothing"，并且都不保留第二个实例的编辑。
//
// 快照:load_settings在启动时在引擎线程上运行
// 并且save_settings可以从工作线程运行，因此两者可以重叠。
// 每次使用都会读取或替换 整个 文档，该文档完全是 快照 的形状
// - 没有 锁，因此设置通道的 正文 可以读取并替换它，
// 而无需在通道之上有任何级别。
//
// 持有SERIALISED:快照的值必须是可证明的不可变(可共享的)，
// 而nlohmann::json则不能被证明。一根绳子就可以。
// 每个设置 保存 都会读取基线两次，因此 解析 是文件 IO 旁边的噪声。
struct Baseline {
    maya::snapshot<std::string> doc{std::string{"{}"}};
    void store(const json& j) { doc.store(j.dump()); }
    [[nodiscard]] json load() const {
        try { return json::parse(*doc.load()); } catch (...) { return json::object(); }
    }
};
Baseline& loaded_baseline() {
    static Baseline b;
    return b;
}

void debug_set_settings_baseline(std::string_view json_text) {
    try {
        loaded_baseline().store(json::parse(json_text));
    } catch (...) {
        loaded_baseline().store(json::object());
    }
}

namespace {
store::Settings load_settings_from_disk();
} // 命名空间结束。

store::Settings load_settings(Io io) {
    store::Settings s = load_settings_from_disk();
    // 搜索策略的环境覆盖必须在每个路径上保持（没有
    // settings.json，格式错误），而不仅仅是在干净的解析之后，
    // 或者当工具在默认情况下运行时面板将显示锁定的行。
    // 最后应用，因此导出优于存储值。
    settings::registry::apply_env(io, s.web_search);
    settings::registry::apply_env(io, s.paste);
    return s;
}

namespace {
store::Settings load_settings_from_disk() {
    store::Settings s;
    std::ifstream ifs(settings_path());
    if (!ifs) return s;
    try {
        json j; ifs >> j;
        loaded_baseline().store(j);
        s.model_id = ModelId{j.value("model_id", "")};
        s.profile = static_cast<Profile>(j.value("profile", 0));
        // 外观。使用结构自身的默认值作为 回退 逐字段读取，
        // 因此由较旧的版本编写的配置（或者用户手写的
        // -edited 严重）会降级为默认外观，
        // 而不是拒绝 加载。
        if (j.contains("ui") && j["ui"].is_object()) {
            const auto& u = j["ui"];
            // 步行，未拼写。 ABSENT 键保留 Prefs
            // 已经保存的值（它自己的默认值），因此由较旧的版本编写的配置，
            // 或者糟糕的 hand-edited，会降级为默认外观，
            // 而不是拒绝 加载。
            ui_prefs::for_each_field([&](auto member, std::string_view key) {
                const std::string k{key};
                if (!u.contains(k)) return;
                auto& dst = s.ui.*member;
                using V = std::remove_cvref_t<decltype(dst)>;
                try {
                    if constexpr (std::is_enum_v<V>) {
                        if (u[k].is_number_integer())
                            dst = static_cast<V>(u[k].get<int>());
                    } else {
                        dst = u[k].get<V>();
                    }
                } catch (...) {
                    // 该字段的JSON类型错误：保留默认值。
                    // 一个错误的密钥不得让用户花费所有其他设置。
                }
            });
            // 文件声称的 Range-check。
            // out-of-range 枚举 是手-edit 或降级，
            // 直接将其放入将索引超出标签表的末尾。
            if (static_cast<int>(s.ui.scrollback) > static_cast<int>(ui_prefs::Scrollback::Auto))
                s.ui.scrollback = ui_prefs::Scrollback::Auto;
        }
        auto favs = j.value("favorite_models", std::vector<std::string>{});
        for (auto& f : favs) s.favorite_models.push_back(ModelId{std::move(f)});
        s.provider = j.value("provider", "");
        // 提供方密钥不再存放于此。
        // 此处-它们被密封在auth::keys保险库中(
        // provider-keys.json，密钥库-mirrored)。
        // 金库的存在是NOT内容的信号，因此地图无条件是已加载。
        // 下面的对象是 LEGACY-ONLY 导入源：由
        // pre-vault 构建的键 已保存。它被优先考虑（旧
        // 文件不得破坏已由以后运行重新密封的保管库），然后在下一个
        // 保存 删除，因此升级安装的密钥以加密形式存在 仅。
        //
        // PERSIST现在是金库，在第一次看到明文的加载上：
        // 设置 缓存 和 状态更新函数 可能会在任何以后的 保存
        // 运行之前将该映射保存很多分钟（或者进程可能会退出），
        // 因此在这里密封（而不是在 save_settings 中）
        // 会关闭窗口，在该窗口中，导入密钥的唯一副本将从
        // settings.json 中剥离，而仍然没有密封。
        s.provider_keys = auth::keys::load();
        if (j.contains("provider_keys") && j["provider_keys"].is_object()) {
            bool migrated = false;
            for (auto& [k, v] : j["provider_keys"].items())
                if (v.is_string()) {
                    s.provider_keys[k] = v.get<std::string>();
                    migrated = true;
                }
            if (migrated) auth::keys::save(s.provider_keys);
        }
        if (j.contains("provider_models") && j["provider_models"].is_object()) {
            for (auto& [k, v] : j["provider_models"].items())
                if (v.is_string()) s.provider_models[k] = v.get<std::string>();
        }
        // Per-model context-window 覆盖。 Non-positive
        // 值在读取时会被丢弃而不是被限制：0 表示 "no override"，
        // 因此存储 1 将是表达 "override with nothing" 的一种方式。
        if (j.contains("context_overrides") && j["context_overrides"].is_object()) {
            for (auto& [k, v] : j["context_overrides"].items())
                if (v.is_number_integer() && v.get<int>() > 0)
                    s.context_overrides[k] = v.get<int>();
        }
        if (j.contains("probe_hosts") && j["probe_hosts"].is_array()) {
            for (auto& v : j["probe_hosts"])
                if (v.is_string() && !v.get<std::string>().empty())
                    s.probe_hosts.insert(v.get<std::string>());
        }
        if (j.contains("recent_models") && j["recent_models"].is_array()) {
            for (auto& v : j["recent_models"])
                if (v.is_string()) s.recent_models.push_back(v.get<std::string>());
        }
        s.effort = j.value("effort", "");
        if (j.contains("reasoning_effort_overrides")
            && j["reasoning_effort_overrides"].is_object()) {
            for (auto& [k, v] : j["reasoning_effort_overrides"].items())
                if (v.is_boolean()) s.reasoning_effort_overrides[k] = v.get<bool>();
        }
        if (j.contains("learned_effort_sets")
            && j["learned_effort_sets"].is_object()) {
            for (auto& [k, v] : j["learned_effort_sets"].items())
                if (v.is_number_unsigned())
                    s.learned_effort_sets[k] =
                        static_cast<std::uint8_t>(v.get<unsigned>() & 0xFFu);
        }
        if (j.contains("rejected_fields") && j["rejected_fields"].is_object()) {
            for (auto& [k, v] : j["rejected_fields"].items())
                if (v.is_number_unsigned())
                    s.rejected_fields[k] =
                        static_cast<std::uint16_t>(v.get<unsigned>() & 0xFFFFu);
        }
        if (j.contains("reasoning_echo") && j["reasoning_echo"].is_object()) {
            // 过去所有(2)的范围不是我们的：手工编辑或较新的构建。
            for (auto& [k, v] : j["reasoning_echo"].items())
                if (v.is_number_unsigned() && v.get<unsigned>() <= 2u)
                    s.reasoning_echo[k] = static_cast<std::uint8_t>(v.get<unsigned>());
        }
        auto grants = j.value("always_allow_tools", std::vector<std::string>{});
        s.always_allow_tools = std::move(grants);
        s.context_1m_blocked = j.value("context_1m_blocked", false);
        // ACCOUNT-SCOPED权利(参见domain/entitlement.hpp)。
        // 键是由 entitlement::key_for 构建的不透明字符串；存储为对象，
        // 以便 hand-edited settings.json 保持可读。
        if (j.contains("entitlements") && j["entitlements"].is_object()) {
            for (auto& [k, v] : j["entitlements"].items())
                if (v.is_boolean() && v.get<bool>())
                    s.entitlements[k] = true;
        }
        s.show_changes_strip = j.value("show_changes_strip", false);
        s.show_reasoning     = j.value("show_reasoning", false);
        s.auto_title         = j.value("auto_title", true);
        s.auto_compact       = j.value("auto_compact", true);
        if (j.contains("tool_loading") && j["tool_loading"].is_string())
            s.tool_loading = j["tool_loading"].get<std::string>();

        // 沙箱政策。每个字段都会回退到 当前 值而不是文字，
        // 因此默认值位于一个位置（结构体），并且稍后添加的键在较旧的
        // settings.json 上读取为 "keep whatever the struct says"，
        // 而不是默默地变为零。
        if (j.contains("sandbox") && j["sandbox"].is_object()) {
            const auto& b = j["sandbox"];
            auto& c = s.sandbox;
            c.configured   = b.value("configured", true);  // 现在 ⇒ 用户-set
            // 发动机。夹紧而不是盲目：hand-edited
            // settings.json是受支持的输入，并且此处的out
            // -of-range值将选择不存在的后端。
            {
                // 现在有一个后端。 bwrap 存在时写入的文件
                // 带有其旧的 序号；将其读取为claybin而不是
                // 拒绝加载，并且下一个保存写入当前值。读取-old，
                // 写入-new，从不写入-old——与上面
                // 的权利迁移的形状相同。
                (void)b.value("backend", 0);
                c.backend = sandbox_cfg::LinuxBackend::Claybin;
            }
            // 错误的-typed值保留默认值而不是抛出：
            // 这里的抛出结束整个加载并丢弃后面的每个部分。
            auto get = [&b](const char* k, auto def) {
                using T = decltype(def);
                const auto it = b.find(k);
                if (it == b.end()) return def;
                if constexpr (std::is_same_v<T, bool>) {
                    return it->is_boolean() ? it->template get<bool>() : def;
                } else {
                    if (!it->is_number_integer()) return def;
                    // 比较字段的OWN域。旧的检查将 numeric_limits
                    // <T>::max() 转换为 long long，
                    // 对于 64-bit 无符号字段（即 -1）：每个正 memory_mb
                    // 和 tmp_mb 读取为 "out of range" 并重置为默认值。
                    // 这是 "I chose Airgapped and restarted and it says Custom"
                    // 错误——硬化和气隙设置 memory_mb，
                    // 它返回 0，因此两个预设都无法在重新启动后幸存。
                    if (it->is_number_unsigned()) {
                        const auto u = it->template get<std::uint64_t>();
                        if (u > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                            return def;
                        return static_cast<T>(u);
                    }
                    const auto v = it->template get<long long>();
                    if (v < 0) return def;
                    if (static_cast<unsigned long long>(v) >
                        static_cast<unsigned long long>(std::numeric_limits<T>::max()))
                        return def;
                    return static_cast<T>(v);
                }
            };
            // out-of-range 枚举保持默认。
            auto en = [&get](const char* k, auto def, int count) {
                using E = decltype(def);
                const int v = get(k, static_cast<int>(def));
                return v < count ? static_cast<E>(v) : def;
            };
            c.fs_scope     = en("fs_scope", c.fs_scope, 3);
            c.net_mode     = en("net_mode", c.net_mode, 3);
            c.syscall_mode = en("syscall_mode", c.syscall_mode, 3);
            c.handoff      = en("handoff", c.handoff, 3);
            c.wx_protect   = get("wx_protect", c.wx_protect);
            c.max_open_files  = get("max_open_files", c.max_open_files);
            c.wall_clock_secs = get("wall_clock_secs", c.wall_clock_secs);
            c.cpu_secs        = get("cpu_secs", c.cpu_secs);
            c.fake_hostname   = get("fake_hostname", c.fake_hostname);
            // 受限制，不可信：这限制了在每次生成时运行的目录遍历，
            // 因此 hand-edited 10000 将在每个 shell
            // 命令的延迟路径中放置完整的-tree 扫描。
            c.mask_scan_depth = std::min<std::uint32_t>(
                get("mask_scan_depth", c.mask_scan_depth), 8u);
            c.scope_ipc    = get("scope_ipc", c.scope_ipc);
            c.close_inherited_fds = get("close_fds", c.close_inherited_fds);
            c.memory_mb    = get("memory_mb", c.memory_mb);
            c.max_procs    = get("max_procs", c.max_procs);
            c.cpu_percent  = get("cpu_percent", c.cpu_percent);
            c.tmp_mb       = get("tmp_mb", c.tmp_mb);
            // 列表在为空时不存在，因此仅在存在时读取它们——并且键入
            // -check，因为hand-edited
            // settings.json是受支持的输入并且数组所属的字符串不应该抛出。
            //
            auto strs = [&](const char* k, std::vector<std::string>& out) {
                if (b.contains(k) && b[k].is_array())
                    for (const auto& v : b[k])
                        if (v.is_string()) out.push_back(v.get<std::string>());
            };
            strs("read_paths", c.read_paths);
            strs("write_paths", c.write_paths);
            strs("deny_paths", c.deny_paths);
            strs("allow_hosts", c.allow_hosts);
            if (b.contains("allow_ports") && b["allow_ports"].is_array()) {
                c.allow_ports.clear();
                for (const auto& v : b["allow_ports"])
                    if (v.is_number_unsigned()) {
                        const auto p = v.get<std::uint64_t>();
                        if (p > 0 && p <= 65535)
                            c.allow_ports.push_back(static_cast<std::uint16_t>(p));
                    }
            }
        }
        if (j.contains("rag") && j["rag"].is_object()) {
            const auto& r = j["rag"];
            auto& c = s.rag;
            c.configured        = r.value("configured", true); // 现在 ⇒ 用户-set
            c.mode              = static_cast<store::RagMode>(
                                      r.value("mode", static_cast<int>(c.mode)));
            c.skills            = r.value("skills", c.skills);
            c.memory            = r.value("memory", c.memory);
            c.mcp_resources     = r.value("mcp_resources", c.mcp_resources);
            c.contextual        = r.value("contextual", c.contextual);
            c.dedup             = r.value("dedup", c.dedup);
            c.mmr               = r.value("mmr", c.mmr);
            c.stitch            = r.value("stitch", c.stitch);
            c.autocut           = r.value("autocut", c.autocut);
            c.prf               = r.value("prf", c.prf);
            c.corrective        = r.value("corrective", c.corrective);
            c.graph             = r.value("graph", c.graph);
            c.expand            = r.value("expand", c.expand);
            c.hyde              = r.value("hyde", c.hyde);
            c.fusion            = r.value("fusion", c.fusion);
            c.adaptive_fusion   = r.value("adaptive_fusion", c.adaptive_fusion);
            c.proactive         = r.value("proactive", c.proactive);
            c.proactive_min_conf= r.value("proactive_min_conf", c.proactive_min_conf);
            c.proactive_bytes   = r.value("proactive_bytes", c.proactive_bytes);
            c.persist           = r.value("persist", c.persist);
            c.learn             = r.value("learn", c.learn);
            c.trace             = r.value("trace", c.trace);
            // 注册表-owned行，通过走动表来读取——添加了旋钮，
            // 这里有已加载，没有编辑。键是行 ID (
            // "rag.mmr_lambda")，平面存储，
            // 因此重命名在文件中可见，而不是默默地重置为默认值。
            for (const auto& d : settings::registry::kSettings) {
                const std::string key{d.id};
                if (!r.contains(key)) continue;
                const auto& v = r.at(key);
                std::string as_text;
                if      (v.is_boolean())        as_text = v.get<bool>() ? "true" : "false";
                else if (v.is_number_integer()) as_text = std::to_string(v.get<long long>());
                else if (v.is_number())         as_text = std::to_string(v.get<double>());
                else if (v.is_string())         as_text = v.get<std::string>();
                else continue;
                (void)settings::registry::set(c, d, as_text);
            }
            // 嵌入。缺少密钥会使 env-derived 默认值保持不变。
            c.embed_backend        = r.value("embed_backend", c.embed_backend);
            c.embed_model          = r.value("embed_model", c.embed_model);
            c.embed_host           = r.value("embed_host", c.embed_host);
            c.embed_port           = static_cast<std::uint16_t>(
                                        r.value("embed_port", static_cast<int>(c.embed_port)));
            c.embed_tls            = r.value("embed_tls", c.embed_tls);
            c.embed_path           = r.value("embed_path", c.embed_path);
            c.embed_model_path     = r.value("embed_model_path", c.embed_model_path);
            c.embed_tokenizer_path = r.value("embed_tokenizer_path", c.embed_tokenizer_path);
            c.embed_dim            = static_cast<std::uint32_t>(
                                        r.value("embed_dim", static_cast<int>(c.embed_dim)));
        }
        if (j.contains("smart") && j["smart"].is_object()) {
            const auto& sm = j["smart"];
            s.smart.enabled = sm.value("enabled", false);
            // 七个子-layer 标志 (route_internal、
            // 编排、route_subagents、learn_routing、
            // outcome_feedback、推测、recall_plans)
            // 不再被读取。现有 settings.json 中留下的按键将被忽略
            // - 三个折叠到主开关中，四个随 self-supervised
            // 层一起删除。无需迁移：始终允许未知密钥。

            // 槽直接读入域类型。 FLAT 传输 键未更改，
            // 因此 settings.json 由旧版本 加载 写入，
            // 其引脚完好无损 - 形状在 内存 中发生变化，而不是在磁盘上发生变化。
            //
            // 管脚在型号命名时为`set`，
            // 与旧的3-field映射应用的规则相同；将其保留在这里意味
            // 着有一个地方决定固定插槽 IS 是什么。
            const auto slot = [&](const char* model_key, const char* eff_key,
                                  const char* prov_key) {
                smart::SlotOverride o;
                o.model = sm.value(model_key, "");
                if (!o.model.empty()) {
                    o.effort = effort_from_wire(sm.value(eff_key, ""));
                    o.set    = true;
                    // 缺失（在提供者
                    // -scoped的引脚之前写入的设置）⇒"" =来源未知，
                    // 在每个提供者下均受到尊重。与该字段存在之前
                    // 的行为相同。
                    o.provider = sm.value(prov_key, "");
                }
                return o;
            };
            s.smart.strategic =
                slot("strategic_model", "strategic_effort", "strategic_provider");
            s.smart.implementation =
                slot("impl_model", "impl_effort", "impl_provider");
            s.smart.utility =
                slot("utility_model", "utility_effort", "utility_provider");

            // 数字路由策略，由 WALKING 和 注册表 读取—
            // —这就是将每个值限制在其进入的行范围内的策略，
            // 因此手 -edited settings.json 无法将
            // 配置置于 UI 永远无法产生的状态。缺少的键保留默认值。
            for (const auto& d : settings::registry::kSettings) {
                if (d.owner() != settings::registry::Owner::Smart) continue;
                const auto dot = d.id.find('.');
                const std::string key{d.id.substr(dot + 1)};
                if (!sm.contains(key)) continue;
                const auto& v = sm[key];
                std::string as_text;
                if      (v.is_boolean())        as_text = v.get<bool>() ? "true" : "false";
                else if (v.is_number_integer()) as_text = std::to_string(v.get<long long>());
                else if (v.is_number())         as_text = std::to_string(v.get<double>());
                else if (v.is_string())         as_text = v.get<std::string>();
                else continue;
                (void)settings::registry::set(s.smart, d, as_text);
            }
        }
        // web_search 策略（设置 → 网页搜索）。
        // 由 WALKING 读取 注册表，因此每个值在传入时都被限制在其行的范围内，
        // 并且手 -edited settings.json 无法容纳 面板
        // 无法产生的内容。键是不带 "web_search." 前缀的行 ID，
        // 与 `smart` 块使用的形状相同。缺少的键保留默认值。
        if (j.contains("web_search") && j["web_search"].is_object()) {
            const auto& se = j["web_search"];
            for (const auto& d : settings::registry::kSettings) {
                if (d.owner() != settings::registry::Owner::WebSearch) continue;
                const auto dot = d.id.find('.');
                const std::string key{d.id.substr(dot + 1)};
                if (!se.contains(key)) continue;
                const auto& v = se[key];
                std::string as_text;
                if      (v.is_boolean())        as_text = v.get<bool>() ? "true" : "false";
                else if (v.is_number_integer()) as_text = std::to_string(v.get<long long>());
                else if (v.is_number())         as_text = std::to_string(v.get<double>());
                else if (v.is_string())         as_text = v.get<std::string>();
                else continue;
                (void)settings::registry::set(s.web_search, d, as_text);
            }
            // 每-service拨打：{"<service>.<dial>":"<value>"}。
            // 不是 注册表 行（该集取决于存在哪些服务），因此请阅读此处。
            // 仅Shape-checked：服务列出的值是否为mcp
            // -cpp的调用，在每次请求时再次进行，
            // 因此过期或hand-edited条目不能在传输上放置任意文本。
            if (se.contains("dials") && se["dials"].is_object()) {
                for (const auto& [k, v] : se["dials"].items()) {
                    if (!web_search_cfg::dial_entry_ok(k, v.is_string() ? v.get<std::string>()
                                                                         : std::string{}))
                        continue;
                    if (s.web_search.dials.size() >= web_search_cfg::kMaxDials) break;
                    s.web_search.dials[k] = v.get<std::string>();
                }
            }
        }
        // （web_search 的环境覆盖：由 load_settings() 在每个路径上应用。）

        // 粘贴（设置→粘贴）：与web_search相同的形状和夹紧。
        if (j.contains("paste") && j["paste"].is_object()) {
            const auto& pj = j["paste"];
            for (const auto& d : settings::registry::kSettings) {
                if (d.owner() != settings::registry::Owner::Paste) continue;
                const std::string key{d.id.substr(d.id.find('.') + 1)};
                if (!pj.contains(key)) continue;
                const auto& v = pj[key];
                std::string as_text;
                if      (v.is_boolean())        as_text = v.get<bool>() ? "true" : "false";
                else if (v.is_number_integer()) as_text = std::to_string(v.get<long long>());
                else if (v.is_number())         as_text = std::to_string(v.get<double>());
                else if (v.is_string())         as_text = v.get<std::string>();
                else continue;
                (void)settings::registry::set(s.paste, d, as_text);
            }
        }

        // 环境覆盖，由 WALKING 和 注册表 应用。这就是为什么
        // 行的环境别名对于每个 Settings-owned 旋钮同时
        // 都是真实的，并且它在进入时固定到每行的范围。
        //
        // OUTSIDE 故意阻止 `smart`：导出必须
        // 在从未编写过该部分的配置上采用 副作用，
        // 这是用户更改某些内容之前的每个配置。在 JSON 之后应用，
        // 因此导出优于存储值 - 在 UI 广告的设置中对
        // 锁定行进行分层。
        settings::registry::apply_env(internal_io(), s.smart);
    } catch (const std::exception& e) {
        util::dbglog("persistence.load_settings", e.what());
    } catch (...) {
        util::dbglog("persistence.load_settings", "non-std exception");
    }
    return s;
}
} // 命名空间结束。

namespace {
void save_settings_locked(persistence::Held held, const store::Settings& s);
// 观察者等待（缓存耗尽其上的队列），因此它被交给调用者的
// `unlocked`。已发布，未锁定：由 缓存 设置一次。
using SettingsObserver = util::Callback<void(const maya::unlocked&)>;
maya::published<const SettingsObserver>& settings_observer() {
    static maya::published<const SettingsObserver> o;
    return o;
}
void notify_written(const maya::unlocked& cap);
}

void save_settings(Io, const maya::unlocked& cap, const store::Settings& s) {
    // 对WHOLE的read-modify-write进行持有，而不仅仅是写入。
    // 两个实例各自序列化自己的完整 in-memory 记录，因此如果没有此记录，
    // 第二个编写器将默默地恢复第一个更改的每个字段 - 请参阅 settings_mu
    // () 以了解此修复的 Five-instance 主题-revert。
    //
    // 采取BEFORE，构建JSON，因此我们发出的
    // 文档不能由在我们工作时另一个进程无效的状态组成。
    persistence::with_shared_file(cap, settings_mu(), settings_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held h, store::Settings v) {
            save_settings_locked(h, v);
        }, s);
    notify_written(cap);
}

void save_settings_quiet(Io, const lock_levels::below_file_lane& cap,
                         const store::Settings& s) {
    persistence::with_shared_file(cap, settings_mu(), settings_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held h, store::Settings v) {
            save_settings_locked(h, v);
        }, s);
}

namespace {
void notify_written(const maya::unlocked& cap) {
    // 告诉任何缓存设置，在通道被释放之后，文件移动到它们下面。
    // 请参阅 persistence.hpp 中的 on_settings_written()。
    if (auto obs = settings_observer().current(); obs && *obs) (*obs)(cap);
}
}  // 命名空间结束。

namespace {
void save_settings_locked(persistence::Held held, const store::Settings& s) {
    if (!held.cross_process)
        AGT_LOG(Persist, Warn, "settings.save",
                "result=unlocked reason=lock_unavailable (concurrent writes may be lost)");

    json j;
    j["model_id"] = s.model_id;
    j["profile"] = static_cast<int>(s.profile);
    // 仅当它与默认外观不同时，设置文件才保留用户
    // CHANGED的记录，而不是每个默认外观的转储。
    if (s.ui != ui_prefs::Prefs{}) {
        // 步行，未拼写。每个字段都会遍历 ui_prefs.hpp
        // 中的一个表，因此会在不触及该表的情况下写入新设置。
        //
        // 当 空 而不是默认时，主题和语言被省略——同样的事情，
        // 因为 "" 是它们的默认值，而缺少正是
        // `auto` 工作的原因（写入 "en" 会在用户移动
        // 到德国机器时将其冻结为英语）。
        const ui_prefs::Prefs def{};
        json u;
        ui_prefs::for_each_field([&](auto member, std::string_view key) {
            const auto& v = s.ui.*member;
            if (v == def.*member) return;          // 默认：不写
            using V = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_enum_v<V>) u[std::string{key}] = static_cast<int>(v);
            else                             u[std::string{key}] = v;
        });
        if (!u.empty()) j["ui"] = std::move(u);
    }
    json favs = json::array();
    for (const auto& mid : s.favorite_models) favs.push_back(mid);
    j["favorite_models"] = std::move(favs);
    if (!s.provider.empty()) j["provider"] = s.provider;
    // Provider密钥静态密封在auth::keys保险库中，
    // NEVER密封在明文设置正文中——它们甚至没有被写入为空对象，
    // 因此settings.json根本不携带凭据-shaped字段。
    // 带有空地图的save()会擦除保险库，因此清空地图的符号
    // -out会清除静态的密钥——
    // load-sealed/imported保险库永远不会被空的-write复活。
    auth::keys::save(s.provider_keys);
    if (!s.provider_models.empty()) {
        json pm = json::object();
        for (const auto& [k, v] : s.provider_models) pm[k] = v;
        j["provider_models"] = std::move(pm);
    }
    if (!s.context_overrides.empty()) {
        json co = json::object();
        for (const auto& [k, v] : s.context_overrides)
            if (v > 0) co[k] = v;
        if (!co.empty()) j["context_overrides"] = std::move(co);
    }
    if (!s.probe_hosts.empty()) {
        json ph = json::array();
        for (const auto& h : s.probe_hosts) ph.push_back(h);
        j["probe_hosts"] = std::move(ph);
    }
    if (!s.recent_models.empty()) {
        json rm = json::array();
        for (const auto& e : s.recent_models) rm.push_back(e);
        j["recent_models"] = std::move(rm);
    }
    if (!s.effort.empty()) j["effort"] = s.effort;
    if (!s.reasoning_effort_overrides.empty()) {
        json ro = json::object();
        for (const auto& [k, v] : s.reasoning_effort_overrides) ro[k] = v;
        j["reasoning_effort_overrides"] = std::move(ro);
    }
    if (!s.learned_effort_sets.empty()) {
        json le = json::object();
        for (const auto& [k, v] : s.learned_effort_sets)
            le[k] = static_cast<unsigned>(v);
        j["learned_effort_sets"] = std::move(le);
    }
    if (!s.rejected_fields.empty()) {
        json rf = json::object();
        for (const auto& [k, v] : s.rejected_fields)
            rf[k] = static_cast<unsigned>(v);
        j["rejected_fields"] = std::move(rf);
    }
    if (!s.reasoning_echo.empty()) {
        json re = json::object();
        for (const auto& [k, v] : s.reasoning_echo)
            re[k] = static_cast<unsigned>(v);
        j["reasoning_echo"] = std::move(re);
    }
    if (!s.always_allow_tools.empty())
        j["always_allow_tools"] = s.always_allow_tools;
    // 旧帐户-blind 标志是READ（用于迁移，上面），
    // 但从未写回——带密钥的`entitlements`存储取代了它。
    // 将其放在下一个 保存 上是迁移的最后一步：读取此文件的旧
    // agentty 只是在其第一个 400 上 re-learns 事实，
    // 这正是它之前的行为。
    if (!s.entitlements.empty()) {
        json ent = json::object();
        for (const auto& [k, v] : s.entitlements)
            if (v) ent[k] = true;
        if (!ent.empty()) j["entitlements"] = std::move(ent);
    }
    // 仅在打开 ON 时持续（默认关闭），保持新鲜配置干净。
    if (s.show_changes_strip) j["show_changes_strip"] = true;
    if (s.show_reasoning)     j["show_reasoning"] = true;
    // 仅在打开OFF时持续（默认为打开）。
    if (!s.auto_title)        j["auto_title"] = false;
    if (!s.auto_compact)      j["auto_compact"] = false;
    // MCP刀具加载：不存在表示自动。
    if (!s.tool_loading.empty() && s.tool_loading != "auto")
        j["tool_loading"] = s.tool_loading;

    // 沙箱策略，仅在用户对其进行编辑后写入。在此之前，
    // 运行时 保持其出厂状态，因此升级永远不会默默地改变
    // 任何人的边界 - 与 rag.configured
    // 具有相同的合同，以及安全默认设置可以严格而不会破坏其
    // 所在版本的人们的原因。
    if (s.sandbox.configured) {
        const auto& c = s.sandbox;
        json sb = {
            {"configured",   true},
            {"backend",      static_cast<int>(c.backend)},
            {"fs_scope",     static_cast<int>(c.fs_scope)},
            {"net_mode",     static_cast<int>(c.net_mode)},
            {"syscall_mode", static_cast<int>(c.syscall_mode)},
            {"handoff",      static_cast<int>(c.handoff)},
            {"wx_protect",   c.wx_protect},
            {"max_open_files",  c.max_open_files},
            {"wall_clock_secs", c.wall_clock_secs},
            {"cpu_secs",        c.cpu_secs},
            {"fake_hostname",   c.fake_hostname},
            {"mask_scan_depth", c.mask_scan_depth},
            {"scope_ipc",    c.scope_ipc},
            {"close_fds",    c.close_inherited_fds},
            {"memory_mb",    c.memory_mb},
            {"max_procs",    c.max_procs},
            {"cpu_percent",  c.cpu_percent},
            {"tmp_mb",       c.tmp_mb},
        };
        // 仅当非-empty时列出：新的配置不应携带四个空数组，
        // 并且不存在的键在加载上读取为"nothing added"，
        // 而不需要特殊情况。
        if (!c.read_paths.empty())  sb["read_paths"]  = c.read_paths;
        if (!c.write_paths.empty()) sb["write_paths"] = c.write_paths;
        if (!c.deny_paths.empty())  sb["deny_paths"]  = c.deny_paths;
        if (!c.allow_hosts.empty()) sb["allow_hosts"] = c.allow_hosts;
        // 始终写入：默认端口缺少键 加载，因此用户选择的空
        // 列表将返回为 443/80/22/53.
        sb["allow_ports"] = c.allow_ports;
        j["sandbox"] = std::move(sb);
    }
    if (s.rag.configured) {
        const auto& c = s.rag;
        // 选择面板仅设置`mode`；其余的是内部默认值，仍然是 round-tripped，
        // 因此 env/power-user 覆盖在 保存 后仍然存在。
        j["rag"] = {
            {"configured",         true},
            {"mode",               static_cast<int>(c.mode)},
            {"skills",             c.skills},
            {"memory",             c.memory},
            {"mcp_resources",      c.mcp_resources},
            {"contextual",         c.contextual},
            {"dedup",              c.dedup},
            {"mmr",                c.mmr},
            {"stitch",             c.stitch},
            {"autocut",            c.autocut},
            {"prf",                c.prf},
            {"corrective",         c.corrective},
            {"graph",              c.graph},
            {"expand",             c.expand},
            {"hyde",               c.hyde},
            {"fusion",             c.fusion},
            {"adaptive_fusion",    c.adaptive_fusion},
            {"proactive",          c.proactive},
            {"proactive_min_conf", c.proactive_min_conf},
            {"proactive_bytes",    c.proactive_bytes},
            {"persist",            c.persist},
            {"learn",              c.learn},
            {"trace",              c.trace},
        };
        // 注册表-owned行：通过遍历表写入，
        // 并且当它们与出厂默认值不同时写入ONLY。从未接触过旋
        // 钮的配置会保持干净，并且将来对默认值的更改会影响到
        // 从未覆盖它的用户。
        {
            auto& r = j["rag"];
            for (const auto& d : settings::registry::kSettings) {
                if (settings::registry::is_default(c, d)) continue;
                const std::string key{d.id};
                const std::string val = settings::registry::get(c, d);
                switch (d.type) {
                    case settings::registry::Type::Bool: r[key] = (val == "true"); break;
                    case settings::registry::Type::Int:
                        try { r[key] = std::stoll(val); } catch (...) {}
                        break;
                    case settings::registry::Type::Real:
                        try { r[key] = std::stod(val); } catch (...) {}
                        break;
                    case settings::registry::Type::Enum:
                    case settings::registry::Type::Text: r[key] = val; break;
                }
            }
        }
        // 嵌入：仅在用户实际选择后端时写入，
        // 因此从未触及面板的配置保持干净并保持遵循env/default路径。
        // API 密钥是此处写入的 绝不 — 它
        // 位于 OS 密钥库中。
        if (!c.embed_backend.empty()) {
            auto& r = j["rag"];
            r["embed_backend"] = c.embed_backend;
            if (!c.embed_model.empty())          r["embed_model"]          = c.embed_model;
            if (!c.embed_host.empty())           r["embed_host"]           = c.embed_host;
            if (c.embed_port != 0)               r["embed_port"]           = c.embed_port;
            if (c.embed_tls)                     r["embed_tls"]            = true;
            if (!c.embed_path.empty())           r["embed_path"]           = c.embed_path;
            if (!c.embed_model_path.empty())     r["embed_model_path"]     = c.embed_model_path;
            if (!c.embed_tokenizer_path.empty()) r["embed_tokenizer_path"] = c.embed_tokenizer_path;
            if (c.embed_dim != 0)                r["embed_dim"]            = c.embed_dim;
        }
    }
    // 智能模式：仅当有意义地配置时（启用、
    // 固定任何槽或调整行移出其默认值）才持续，因此新的配置
    // 保持干净。
    const bool smart_tuned = [&] {
        for (const auto& d : settings::registry::kSettings)
            if (d.owner() == settings::registry::Owner::Smart
                && !settings::registry::is_default(s.smart, d)) return true;
        return false;
    }();
    if (s.smart.enabled || s.smart.strategic.set || s.smart.implementation.set
        || s.smart.utility.set || smart_tuned) {
        nlohmann::json sm;
        sm["enabled"] = s.smart.enabled;

        // 写入FLAT的槽，传输格式的旧版本已经读取——in
        // -memory形状改变了，文件没有。 Provider 出
        // 处与每个引脚相关：模型 ID 仅对为其提供服务的端点有意义，
        // 因此 resolve_role 在其所在的提供商下重放
        // 引脚 仅。缺席（旧设置）读回为 "" = 到处都
        // 是荣誉。
        const auto put_slot = [&](const smart::SlotOverride& o,
                                  const char* model_key, const char* eff_key,
                                  const char* prov_key) {
            if (o.model.empty()) return;
            sm[model_key] = o.model;
            if (const auto e = effort_wire(o.effort); !e.empty())
                sm[eff_key] = std::string{e};
            if (!o.provider.empty()) sm[prov_key] = o.provider;
        };
        put_slot(s.smart.strategic, "strategic_model", "strategic_effort",
                 "strategic_provider");
        put_slot(s.smart.implementation, "impl_model", "impl_effort",
                 "impl_provider");
        put_slot(s.smart.utility, "utility_model", "utility_effort",
                 "utility_provider");

        // 数字路由策略，由 WALKING 和 注册表 编写 — 与碎布
        // 块使用的规则相同。仅发出非 -default 行，
        // 因此 settings.json 保持可读，并且发货的 -default
        // 更改仍然到达从未接触过旋钮的用户。
        for (const auto& d : settings::registry::kSettings) {
            if (d.owner() != settings::registry::Owner::Smart) continue;
            if (settings::registry::is_default(s.smart, d)) continue;
            const auto dot = d.id.find('.');
            const std::string key{d.id.substr(dot + 1)};
            const std::string val = settings::registry::get(s.smart, d);
            switch (d.type) {
                case settings::registry::Type::Bool: sm[key] = (val == "true"); break;
                case settings::registry::Type::Int:
                    sm[key] = std::atoi(val.c_str()); break;
                case settings::registry::Type::Real:
                    sm[key] = std::atof(val.c_str()); break;
                case settings::registry::Type::Enum: sm[key] = val; break;
                case settings::registry::Type::Text: sm[key] = val; break;
            }
        }
        j["smart"] = std::move(sm);
    }

    // web_search：仅当一行移出其出厂默认值时才写入，
    // 因此从未打开 面板 的配置根本不携带 `web_search`
    // 密钥 — 并且未来对默认值的更改会影响到所有从未覆盖它的人。
    //
    // 环境变量设置为从 `s.web_search` 写入的 不 的行：该
    // 值是由 load_settings() 应用的导出，
    // 并且保存它会将 one-session 覆盖变成用户从未选择的永久设置。
    // 这样的行保留为其保留的任何settings.json（基线），
    // 或者保持不存在。 面板 锁 这些行，所以没有其他东西可以移动它们。
    {
        json base_web_search = [](const json& b) {
            return b.contains("web_search") && b["web_search"].is_object()
                       ? b["web_search"] : json{};
        }(loaded_baseline().load());
        const auto env = settings::registry::read_env(internal_io());
        nlohmann::json se = nlohmann::json::object();
        for (const auto& d : settings::registry::kSettings) {
            if (d.owner() != settings::registry::Owner::WebSearch) continue;
            const auto dot = d.id.find('.');
            const std::string key{d.id.substr(dot + 1)};
            if (!settings::registry::env_override(d, env).empty()) {
                if (base_web_search.is_object() && base_web_search.contains(key))
                    se[key] = base_web_search[key];
                continue;
            }
            if (settings::registry::is_default(s.web_search, d)) continue;
            const std::string val = settings::registry::get(s.web_search, d);
            switch (d.type) {
                case settings::registry::Type::Bool: se[key] = (val == "true"); break;
                case settings::registry::Type::Int:
                    try { se[key] = std::stoll(val); } catch (...) {}
                    break;
                case settings::registry::Type::Real:
                    try { se[key] = std::stod(val); } catch (...) {}
                    break;
                case settings::registry::Type::Enum:
                case settings::registry::Type::Text: se[key] = val; break;
            }
        }
        // 刻度盘：仅那些移出"the service's own default"
        // 的刻度盘。根据相同的规则和加载程序应用的上限，保留未知服务
        // 的密钥（较新的版本可能知道它）。
        if (!s.web_search.dials.empty()) {
            nlohmann::json dj = nlohmann::json::object();
            for (const auto& [k, v] : s.web_search.dials) {
                if (!web_search_cfg::dial_entry_ok(k, v)) continue;
                if (dj.size() >= web_search_cfg::kMaxDials) break;
                dj[k] = v;
            }
            if (!dj.empty()) se["dials"] = std::move(dj);
        }
        if (!se.empty()) {
            j["web_search"] = std::move(se);
        } else if (base_web_search.is_object()) {
            // 我们已加载一个`web_search`块并且现在保持所有默认值：用户重置它。
            // Write 是空对象，因此合并会看到 OUR 更改，
            // 并且不会从磁盘重新修改 -adopt 旧块。
            j["web_search"] = json::object();
        }
        // 否则：从来没有过，仍然都是默认的。不存在 — 并且合并采用
        // 磁盘中同级实例的较新 `web_search` 块，而不是删除它（请
        // 参见下面的 adopt_disk_new）。
    }

    // 粘贴：与 web_search 相同的规则（仅行偏离其默认值；
    // env-overridden 行保留其基线值，从不导出）。
    {
        json base_paste = [](const json& b) {
            return b.contains("paste") && b["paste"].is_object() ? b["paste"] : json{};
        }(loaded_baseline().load());
        const auto env = settings::registry::read_env(internal_io());
        nlohmann::json pe = nlohmann::json::object();
        for (const auto& d : settings::registry::kSettings) {
            if (d.owner() != settings::registry::Owner::Paste) continue;
            const std::string key{d.id.substr(d.id.find('.') + 1)};
            if (!settings::registry::env_override(d, env).empty()) {
                if (base_paste.is_object() && base_paste.contains(key)) pe[key] = base_paste[key];
                continue;
            }
            if (settings::registry::is_default(s.paste, d)) continue;
            const std::string val = settings::registry::get(s.paste, d);
            switch (d.type) {
                case settings::registry::Type::Bool: pe[key] = (val == "true"); break;
                case settings::registry::Type::Int:
                    try { pe[key] = std::stoll(val); } catch (...) {}
                    break;
                case settings::registry::Type::Real:
                    try { pe[key] = std::stod(val); } catch (...) {}
                    break;
                case settings::registry::Type::Enum:
                case settings::registry::Type::Text: pe[key] = val; break;
            }
        }
        if (!pe.empty())                     j["paste"] = std::move(pe);
        else if (base_paste.is_object())     j["paste"] = json::object();   // 重置为默认值
    }

    // ── 合并,而不是替换 ──────────────────────────────────
    //
    // 上述锁使得写入SEQUENTIAL；它不能使该进程的记录为
    // FRESH。我们的 in-memory 副本来自启动，
    // 因此我们没有更改的每个键仍然保留着startup-era值——
    // 而写回这些值正是丢失的更新（主题恢复）。
    //
    // 因此，将每个top-level键与我们已加载的基线进行比较。
    // 如果我们的值与 WE 读取的值没有变化，我们对此没有意见：
    // 采用现在磁盘上的任何内容，这可能是另一个实例的较新编辑。
    // 如果不同，则该过程改变它并且我们的价值获胜。
    //
    // Top-level粒度是有意的。深度 per-leaf 合并需要
    // 知道哪些缺席是有意义的，缺席 IS 在这里有意义（save_settings
    // 故意省略默认值，加载 将 `effort: ""` 读取为自动）。
    // Key-level 很粗糙，但没有这样的故障模式：`ui`
    // 作为一个单元移动，这就是用户编辑它的方式。
    {
        std::error_code ec;
        const fs::path path = data_dir() / "settings.json";
        json disk = json::object();
        if (fs::exists(path, ec)) {
            std::ifstream ifs(path);
            if (ifs) { try { ifs >> disk; } catch (...) { disk = json::object(); } }
        }
        if (disk.is_object()) {
            const json base = loaded_baseline().load();

            // `ui`合并PER FIELD，不是作为一个块。
            //
            // 这是用户在多个正在运行的会话中一次编辑字段的一个块，
            // 并且块-level合并丢失了这些编辑：实例A设置回滚缓冲区，
            // 实例B（持有startup-era ui）然后改变其主题，
            // B的整个块与B的基线不同，因此B的过期 回滚缓冲区
            // 被写回A的上面。 "Settings don't persist"
            // ——他们做到了，直到兄弟姐妹已保存。
            //
            // 下面的注释解释了为什么top-level是默认值：
            // ABSENT密钥不能与默认值区分开。这在 `ui` 中
            // 并不成立：每个字段都有固定的默认值，因此缺失的字段
            // 意味着该默认值，而 "did we change it"
            // 是与我们的基线值的简单比较。
            //
            // 作者删除了默认字段（当所有字段都是默认字段时，还删除了整个块），
            // 因此缺席不是 "no opinion"——它是默认字段。
            // 比较 FILLED 值使这两个值相同；
            // 比较原始存在读取一个会话，该会话的 ui 已返回到 all
            // -defaults，因为每个字段都有 DROPPED，
            // 因此其空块获胜并擦除了同级的 回滚缓冲区。
            //
            // 通过在默认构造的首选项上遍历
            // ui_prefs::for_each_field 来构建，因此不存在从
            // 结构中漂移的默认字段 OR 的第二列表。
            const auto ui_defaults = [] {
                json d = json::object();
                const ui_prefs::Prefs p{};
                ui_prefs::for_each_field([&](auto member, std::string_view key) {
                    const auto& v = p.*member;
                    using V = std::remove_cvref_t<decltype(v)>;
                    if constexpr (std::is_enum_v<V>) d[std::string{key}] = static_cast<int>(v);
                    else                             d[std::string{key}] = v;
                });
                return d;
            }();
            const auto ui_filled = [&](const json& doc) {
                json out = ui_defaults;
                if (doc.contains("ui") && doc["ui"].is_object())
                    for (auto it = doc["ui"].begin(); it != doc["ui"].end(); ++it)
                        out[it.key()] = it.value();
                return out;
            };
            const json ui_base = ui_filled(base);
            const json ui_disk = ui_filled(disk);
            const json ui_ours = ui_filled(j);
            json       ui_out  = ui_ours;
            bool ui_merged = false;
            for (auto it = ui_disk.begin(); it != ui_disk.end(); ++it) {
                const auto& k = it.key();
                // 我们的胜利仅在我们自加载以来改变它的地方。
                if (ui_ours.value(k, json{}) != ui_base.value(k, json{})) continue;
                if (ui_ours.value(k, json{}) == it.value()) continue;   // 同意
                AGT_LOG(Persist, Info, "settings.save",
                        "merge=adopt_disk key=ui.{}", k);
                ui_out[k] = it.value();
                ui_merged = true;
            }
            if (ui_merged) {
                // Write它回到作者自己的形状：只有非-default字段，
                // 并且如果没有留下任何内容则根本没有块。
                // 写入填充的地图会将每个默认值冻结到文件中，
                // 并阻止已发货的 -default 更改到达此用户。
                json compact = json::object();
                for (auto it = ui_out.begin(); it != ui_out.end(); ++it)
                    if (!ui_defaults.contains(it.key())
                        || ui_defaults[it.key()] != it.value())
                        compact[it.key()] = it.value();
                if (compact.empty()) j.erase("ui");
                else                 j["ui"] = std::move(compact);
            }

            for (auto it = disk.begin(); it != disk.end(); ++it) {
                const auto& key = it.key();
                // 上面处理了字段-by-field。
                if (key == "ui") continue;
                // 不存在我们LOADED和我们WRITING：
                // 我们从未对此键有意见，因此兄弟实例将其添加到我们的 加载 之后。
                // 保留他们的。 （以前，
                // 这落入 "appeared/vanished for us"
                // 并且密钥被删除 - 诸如 `search` 之类的块，
                // 在更改之前设计上不存在，被任何其他实例的下一个 保存 删除。）
                if (!base.contains(key) && !j.contains(key)) {
                    AGT_LOG(Persist, Info, "settings.save",
                            "merge=adopt_disk_new key={}", key);
                    j[key] = it.value();
                    continue;
                }
                const bool we_changed =
                    !base.contains(key) || !j.contains(key)
                        ? true                       // appeared/vanished 为我们
                        : j[key] != base[key];
                if (we_changed) continue;            // 我们的编辑获胜
                if (j.contains(key) && j[key] == it.value()) continue;  // 同意
                // 未被我们触及且磁盘上较新的：拿走他们的。
                AGT_LOG(Persist, Info, "settings.save",
                        "merge=adopt_disk key={}", key);
                j[key] = it.value();
            }
        }
    }

    // 失败的设置写入会默默地丢弃用户的提供者密钥、
    // 模型选择和偏好——它们只是在重新启动时"don't stick"，
    // 没有任何解释原因。结果在这里被丢弃了。
    if (!write_json_atomic(internal_io(), data_dir() / "settings.json", j.dump(2)))
        AGT_LOG(Persist, Error, "settings.save", "result=write_failed path={}",
                (data_dir() / "settings.json").string());
    else {
        // 我们刚刚在文档中写入了IS新基线。如果没有这个，
        // 同一会话中的第二个 保存 将与启动 快照 不同，
        // 将其自己的早期编辑视为 "changed"，
        // 并将 re-asserting 保留在其他实例的较新值之上。
        loaded_baseline().store(j);
    }
}
} // 命名空间结束。

void on_settings_written(std::function<void(const maya::unlocked&)> observer) {
    settings_observer().publish(
        std::make_shared<const SettingsObserver>(SettingsObserver{std::move(observer)}));
}

// 64个随机位作为16个十六进制数字。 Zero-padded 所以每个 id 都是固定宽度的：
// 考虑到 缓存 键中的 ":" 分隔符，可变宽度是明确的，
// 但很脆弱（0x1 卷产生 "1"，大多数其他 ID 的子串）。
// 由 线程 和消息 ID 共享。
//
// 锁-free:从状态更新函数、工具正文和锁 正文类似地调用
// new_id()，因此这里的锁将赋予一半程序能力。
// 一个原子计数器从 OS 播种，通过 splitmix64 混合（
// 一种双射，因此不同的计数器值给出不同的 id，并且输出通过
// BigCrush）。 ID 需要在启动过程中具有唯一性和不可预测性，
// 而不是加密强度。
std::string random_hex_id() {
    static std::atomic<std::uint64_t> counter{[] {
        std::random_device rd;
        return (std::uint64_t{rd()} << 32) ^ rd();
    }()};
    std::uint64_t z = counter.fetch_add(0x9E3779B97F4A7C15ull, std::memory_order_relaxed);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    const std::uint64_t v = z ^ (z >> 31);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
}

ThreadId new_id() {
    return ThreadId{random_hex_id()};
}

std::string title_from_first_message(std::string_view text) {
    std::string t{text};
    for (auto& c : t) if (c == '\n' || c == '\r') c = ' ';
    if (t.size() > kTitleMaxBytes) {
        // UTF-8-safe剪切：简单的调整大小(57)可以分割multi-byte序列，
        // 并且部分字节传播到json::dump()中，其在任何无效的
        // UTF-8上抛出type_error.316。然后擦洗皮带
        // -and- 吊带，以防 `text` 本身到达时变形。
        t.resize(tools::util::safe_utf8_cut(t, kTitleMaxBytes - 3));
        t = tools::util::to_valid_utf8(std::move(t));
        t += "...";
    }
    if (t.empty()) t = "New thread";
    return t;
}

std::string clean_title(std::string_view text, std::size_t max_bytes) {
    // 首先修复，以便下面的过滤器遍历整个代码点。
    const std::string valid = tools::util::to_valid_utf8(std::string{text});
    std::string t;
    t.reserve(valid.size());
    for (std::size_t i = 0; i < valid.size(); ++i) {
        const auto u = static_cast<unsigned char>(valid[i]);
        if (u == '\n' || u == '\r' || u == '\t') { t.push_back(' '); continue; }
        if (u < 0x20 || u == 0x7f) continue;
        // C1控制(U+0080..U+009F，编码C2 80..
        // C2 9F)仍然可以在某些终端上启动转义序列。
        if (u == 0xC2 && i + 1 < valid.size()) {
            const auto n = static_cast<unsigned char>(valid[i + 1]);
            if (n >= 0x80 && n <= 0x9F) { ++i; continue; }
        }
        t.push_back(valid[i]);
    }
    t.resize(tools::util::safe_utf8_cut(t, max_bytes));
    const auto first = t.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    t.erase(t.find_last_not_of(' ') + 1);
    t.erase(0, first);
    return t;
}

void summarise_for_picker(const std::vector<Message>& messages, Thread& out) {
    if (messages.empty()) return;
    std::uint32_t n = 0;
    const Message* first = nullptr;
    for (const auto& msg : messages) {
        if (msg.typed_by_user()) {
            ++n;
            if (!first && !msg.text.empty()) first = &msg;
        } else if (msg.role == Role::Assistant && !msg.smart_routing) {
            ++n;
        }
    }
    out.message_count = n;
    // 来自粘贴块的一系列空格读作一行上的一个间隙。
    if (first) {
        std::string p = clean_title(first->text, kPreviewMaxBytes);
        p.erase(std::unique(p.begin(), p.end(),
                            [](char a, char b) { return a == ' ' && b == ' '; }),
                p.end());
        out.preview = std::move(p);
    }
}

void refresh_picker_summary(Thread& t) { summarise_for_picker(t.messages, t); }

} // namespace agentty::persistence

namespace agentty {

// Per-Message稳定身份。在消息 default-construction
// 处生成（请参阅 conversation.hpp 中的 Message::id）。
// 缓存 键（thread_id、message_id）在向量索引移位（压缩、
// 删除、重新排序）中保持稳定，因此 渲染-cache 查找永远不会
// 为同一位置的 now-different 消息返回 过期 元素。
//
// 实现镜像 persistence::new_id() — 64-bit 随机
// 十六进制，thread-safe 通过静态 mt19937 +
// std::random_device。 16 个十六进制数字对于 inside
// -process 的唯一性来说绰绰有余；即使有一百万条消息，两个 ID 在会话中发生冲突的
// 几率也约为 2⁻³²，远低于任何实际负载。
MessageId new_message_id() {
    return MessageId{persistence::random_hex_id()};
}

} // namespace agentty
