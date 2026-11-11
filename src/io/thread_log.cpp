// agentty::ThreadLog-附加-only消息日志+字节-offset索引。
// 参见thread_log.hpp了解其背后的设计和测量。

#include "agentty/io/thread_log.hpp"

#include "agentty/io/shared_file.hpp"
#include "agentty/util/logx.hpp"

#include <nlohmann/json.hpp>
#include <simdjson.h>

#include <cstdio>
#include <fstream>
#include <system_error>

namespace agentty {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::size_t kOffsetWidth = 8;   // uint64little-endian

// Little-endian通过法令，
// 因此在机器之间复制的threads/目录保持工作。字节序是一种
// 现在可以免费获得但以后发现的代价昂贵的东西。
void put_u64_le(char* dst, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) dst[i] = static_cast<char>((v >> (i * 8)) & 0xFF);
}

[[nodiscard]] std::uint64_t get_u64_le(const char* src) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(src[i])) << (i * 8);
    return v;
}

[[nodiscard]] std::uintmax_t file_size_or_zero(const fs::path& p) noexcept {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return ec ? 0u : n;
}

// 一条消息出现在一行上：共享编解码器，转储紧凑。
//
// dump()仍然可以抛在输入UTF-8擦洗里面的
// message_to_json没有达到的；单个不可序列
// 化消息不得取消整个 线程 的 保存 路径，
// 因此故障包含在此处并报告为调用者跳过的空行。
[[nodiscard]] std::string encode_line(const Message& m) {
    try {
        return persistence::message_to_json(m).dump();
    } catch (const std::exception& e) {
        AGT_LOG(General, Error, "thread_log.encode",
                "message {} failed to serialise: {}", m.id.value, e.what());
        return {};
    }
}

// 智能模式路由卡是view-only遥测：它们不携带
// 传输内容，从不发送到模型，并且从不保留。
// Whole-document 作者总是跳过它们，
// 因此日志也必须跳过它们 - 否则迁移的 线程 在每次
// 重新加载时都会增加一行，并且 保存-time 验证（与
// 可持久集进行比较）失败并正确拒绝废弃旧文件。
[[nodiscard]] bool persistable(const Message& m) noexcept {
    return !m.smart_routing;
}

// ── 解析号线 ─────────────────────────────────────────────────
//
// 解析支配线程 加载：打开2519-message日志所花费的〜37毫秒
// 中的28毫秒，并且传输在每次转动之前再次支付相同的成本。 simdjson
// 解析 与 字节 相同，比 nlohmann 快约 7.6 倍。
//
// 但是 message_from_json 是跨 28 个
// 访问站点的消息在磁盘上曾经具有的每种形状的 170 行累积容差。
// 将其移植到第二个 JSON API 将是整个项目中为了
// 纯粹的速度胜利而进行的最危险的改变 - 正是这种重写会丢失
// 一个没人注意到的字段，直到他们的历史已经是 已保存 没有它。
//
// 因此：解析与simdjson、CONVERT给nlohmann，
// 并且让读者保持不变。这种转换花费了一些回报——3.9倍而不是
// 7.6倍，在真实的7.5 MB日志上测量——并且购买了读者，
// 以及它处理的每个历史形状，是字节-for-byte，
// 一直在阅读这些线程的代码。
[[nodiscard]] json to_nlohmann(simdjson::dom::element e) {
    using namespace simdjson;
    switch (e.type()) {
        case dom::element_type::OBJECT: {
            json o = json::object();
            for (auto [k, v] : dom::object(e))
                o[std::string{k}] = to_nlohmann(v);
            return o;
        }
        case dom::element_type::ARRAY: {
            json a = json::array();
            for (auto v : dom::array(e)) a.push_back(to_nlohmann(v));
            return a;
        }
        case dom::element_type::STRING:
            return std::string{std::string_view(e)};
        case dom::element_type::INT64:   return std::int64_t(e);
        case dom::element_type::UINT64:  return std::uint64_t(e);
        case dom::element_type::DOUBLE:  return double(e);
        case dom::element_type::BOOL:    return bool(e);
        case dom::element_type::NULL_VALUE:
        default:                         return nullptr;
    }
}

// 一个日志行-> JSON文档，或者`discarded`（如果它不是有效的JSON）。
//
// simdjson需要SIMDJSON_PADDING可读字节过去的结尾，
// 其中一个string_view写入读取缓冲区无法保证，所以该行被复制
// 成一个padded_string。该副本的定价已达到上述 3.9 倍。
//
// nlohmann 被保留为 回退 而不是被删除：如果 simdjson
// 曾拒绝 nlohmann 接受的某些内容，则用户的历史
// 不得因 解析器 分歧而变得不可读。
[[nodiscard]] json parse_line(simdjson::dom::parser& parser,
                              std::string_view line) {
    simdjson::padded_string padded{line};
    simdjson::dom::element el;
    if (!parser.parse(padded).get(el)) return to_nlohmann(el);
    return json::parse(line, nullptr, /*allow_exceptions=*/false);
}

} // 命名空间结束。

// ── 打开 ─────────────────────────────────────────────────────────────

std::expected<ThreadLog, persistence::DeserializeError>
ThreadLog::open_path(fs::path log_path) { return open_impl(std::move(log_path), true); }

std::expected<ThreadLog, persistence::DeserializeError>
ThreadLog::open_impl(fs::path log_path, bool read_meta) {
    ThreadLog t;
    t.log_ = std::move(log_path);
    t.idx_ = t.log_;
    t.idx_.replace_extension(".ofs");
    t.meta_ = t.log_;
    t.meta_.replace_extension(".meta.json");

    std::error_code ec;
    fs::create_directories(t.log_.parent_path(), ec);

    // 元数据优先：它很小，只读取一次，并且没有消息但仍然
    // 具有标题和时间戳的线程。丢失或不可读的 sidecar
    // 会留下默认的 -constructed 线程，
    // 而不是打开失败 - 消息是不可替代的部分，
    // 并且返回无标题的 线程 会击败无法打开的消息。
    if (read_meta) {
        if (auto buf = persistence::read_file(::agentty::IoAccess::grant(), t.meta_)) {
            json mj = json::parse(*buf, nullptr, /*allow_exceptions=*/false);
            if (!mj.is_discarded()) {
                if (auto m = persistence::thread_meta_from_json(mj)) {
                    t.meta_thread_ = std::move(*m);
                } else {
                    AGT_LOG(General, Warn, "thread_log.meta",
                            "unreadable metadata for {}: {}",
                            t.log_.string(), m.error().render());
                }
            }
        }
    }

    const auto log_bytes = file_size_or_zero(t.log_);
    if (log_bytes == 0) {
        // 日志不存在或为空是合法状态（品牌-new 线程），
        // 而不是错误。根据定义，其旁边的任何索引都是 过期。
        t.offsets_.clear();
        return t;
    }

    // 阅读该索引，然后决定是否相信它。
    const auto idx_bytes = file_size_or_zero(t.idx_);
    bool usable = idx_bytes > 0 && (idx_bytes % kOffsetWidth) == 0;
    if (usable) {
        std::ifstream in(t.idx_, std::ios::binary);
        std::string buf(static_cast<std::size_t>(idx_bytes), '\0');
        in.read(buf.data(), static_cast<std::streamsize>(idx_bytes));
        usable = in && static_cast<std::uintmax_t>(in.gcount()) == idx_bytes;
        if (usable) {
            const std::size_t n = buf.size() / kOffsetWidth;
            t.offsets_.resize(n);
            for (std::size_t i = 0; i < n; ++i)
                t.offsets_[i] = get_u64_le(buf.data() + i * kOffsetWidth);

            // 对照日志进行验证。这些是廉价的检查，
            // 可以捕获该对可能失步的所有方式：两个
            // 追加之间的崩溃、截断的索引、
            // 来自不同（或重写）日志的索引、不指向文件的偏移量。
            //
            // 特意NOT验证：每个偏移落在真实的线边界上。
            // 这将需要进行全面扫描，而索引的存在正是为了避免
            // 这种情况——并且错误的 -but-in-range
            // 偏移量会被未能到达 解析 的行捕获。
            if (t.offsets_.empty() || t.offsets_.front() != 0) usable = false;
            for (std::size_t i = 1; usable && i < n; ++i)
                if (t.offsets_[i] <= t.offsets_[i - 1]) usable = false;
            if (usable && t.offsets_.back() >= log_bytes) usable = false;
        }
    }

    if (!usable) {
        // 索引是缓存：默默地重建而不是为用户不打算考虑的
        // 文件显示错误。花费一次扫描（29 MB 线程
        // 需要 12 毫秒）和永久 self-heals。
        t.rebuild_index_();
        if (!t.write_index_())
            AGT_LOG(General, Warn, "thread_log.index",
                    "could not persist rebuilt index for {}", t.log_.string());
    } else {
        // 索引是可信的，因此完整扫描从未运行——但
        // 最后一行仍然有可能被破坏（崩溃会按顺序写
        // 入 字节、'\n' 和索引，因此索引可以
        // 合法地描述最后一行不完整的日志）。
        //
        // 检测它花费ONE 字节：如果日志不以换行符结束，
        // 则最后记录的偏移量开始未终止的行。
        // 跳过该检查将使下一个附加将其自身粘合到
        // 部分行上并丢失两条消息。
        std::ifstream in(t.log_, std::ios::binary);
        if (in) {
            in.seekg(-1, std::ios::end);
            char last = '\n';
            in.read(&last, 1);
            if (in && last != '\n' && !t.offsets_.empty())
                t.torn_tail_at_ = t.offsets_.back();
        }
    }
    return t;
}

std::expected<ThreadLog, persistence::DeserializeError>
ThreadLog::open(const ThreadId& id) {
    if (id.value.empty())
        return std::unexpected(persistence::DeserializeError{
            persistence::DeserializeErrorKind::InvalidValue, "id", "empty thread id"});
    return open_path(persistence::threads_dir() / (id.value + ".jsonl"));
}

std::expected<ThreadLog, persistence::DeserializeError>
ThreadLog::open_for_write(const ThreadId& id) {
    if (id.value.empty())
        return std::unexpected(persistence::DeserializeError{
            persistence::DeserializeErrorKind::InvalidValue, "id", "empty thread id"});
    return open_impl(persistence::threads_dir() / (id.value + ".jsonl"), false);
}

// ── 指数维护 ────────────────────────────────────────────────

void ThreadLog::rebuild_index_() {
    offsets_.clear();
    torn_tail_at_ = kNoTear;
    std::ifstream in(log_, std::ios::binary);
    if (!in) return;

    // 数据流以块的形式：29MB日志不得仅仅为了找到其行
    // 开始而变成29MB分配。
    constexpr std::size_t kChunk = 1u << 16;
    std::string buf(kChunk, '\0');
    std::uint64_t pos = 0;
    bool at_line_start = true;
    std::uint64_t last_start = 0;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(kChunk));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            if (at_line_start) {
                last_start = pos + static_cast<std::uint64_t>(i);
                offsets_.push_back(last_start);
                at_line_start = false;
            }
            if (buf[static_cast<std::size_t>(i)] == '\n') at_line_start = true;
        }
        pos += static_cast<std::uint64_t>(got);
    }
    // 没有终止换行符的最后一行是TORN APPEND：
    // 进程在写入字节和写入'\n'之间终止。
    // 它被记录为偏移量（因此 range() 看到它，
    // 无法 解析 并删除它），但也在这里被记住，
    // 因为在它后面附加必须将 不 连接到它上面 - 这会将旧
    // 消息的残骸与新消息合并并丢失 两者。
    if (!at_line_start && !offsets_.empty()) torn_tail_at_ = last_start;
}

bool ThreadLog::write_index_() const {
    std::string buf(offsets_.size() * kOffsetWidth, '\0');
    for (std::size_t i = 0; i < offsets_.size(); ++i)
        put_u64_le(buf.data() + i * kOffsetWidth, offsets_[i]);
    return persistence::write_json_atomic(::agentty::IoAccess::grant(), idx_, buf);
}

bool ThreadLog::append_offset_(std::uint64_t at) const {
    // 8个字节，1个O_APPEND写入。崩溃无法撕裂它（短索引是
    // 8 的非 -multiple，open() 通过重新扫描修复）；
    // 索引是缓存，允许有错误，但从来不相信它是错误的。
    char b[kOffsetWidth];
    put_u64_le(b, at);
    return persistence::append_atomic(::agentty::IoAccess::grant(), idx_,
                                      std::string_view{b, sizeof b});
}

bool ThreadLog::set_meta(const Thread& t) {
    // 仅 Store 和 头文件 — 消息存在于日志中，
    // 并且在此处保留第二个副本是两者的漂移方式。
    meta_thread_ = t;
    meta_thread_.messages.clear();
    try {
        const auto cur = persistence::peek_file(meta_);
        // 磁盘上较新的重命名（另一个agentty，
        // 或在获取此副本之后登陆的重命名）赢得了该保存所携带的标题。
        if (cur) {
            const json on_disk = json::parse(*cur, nullptr, /*allow_exceptions=*/false);
            const auto it = on_disk.is_object() ? on_disk.find("renamed_at") : on_disk.end();
            if (it != on_disk.end() && it->is_number_integer()
                && it->get<std::int64_t>() > meta_thread_.renamed_at) {
                meta_thread_.renamed_at   = it->get<std::int64_t>();
                meta_thread_.title        = on_disk.value("title", meta_thread_.title);
                meta_thread_.name_is_auto = on_disk.value("name_is_auto", false);
            }
        }
        std::string bytes = persistence::thread_meta_to_json(meta_thread_).dump(2);
        // 大多数 保存 在这里没有任何改变（它保存标题、
        // 时间戳和压缩历史）。每回合重写和同步〜45 KB是末尾
        // 保存的IO的大部分，因此不会写入相同的-bytes 头文件。
        if (cur && *cur == bytes) return true;
        return persistence::write_json_atomic(::agentty::IoAccess::grant(), meta_, bytes);
    } catch (const std::exception& e) {
        AGT_LOG(General, Error, "thread_log.meta",
                "could not serialise metadata for {}: {}",
                log_.string(), e.what());
        return false;
    }
}

// ── 读 ─────────────────────────────────────────────────────────────

std::vector<Message> ThreadLog::range(std::size_t from, std::size_t to) const {
    std::vector<Message> out;
    const std::size_t n = offsets_.size();
    if (from >= n || from >= to) return out;
    to = std::min(to, n);

    std::ifstream in(log_, std::ios::binary);
    if (!in) return out;

    // 一次查找，然后一次连续读取至窗口的末尾。
    // 通过数据流读取line-by-line将针对每条消息发出系统调用；
    // 窗口受调用者限制，因此读取它既
    // 简单又快捷。
    const std::uint64_t begin = offsets_[from];
    const std::uint64_t end   = (to < n) ? offsets_[to]
                                         : static_cast<std::uint64_t>(
                                               file_size_or_zero(log_));
    if (end <= begin) return out;

    std::string buf(static_cast<std::size_t>(end - begin), '\0');
    in.seekg(static_cast<std::streamoff>(begin));
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));

    out.reserve(to - from);
    // 一个解析器用于整个窗口：simdjson在
    // 解析上重复使用其内部缓冲区，因此每行构建它会失去
    // 大部分胜利。
    simdjson::dom::parser parser;
    std::size_t pos = 0;
    while (pos < buf.size()) {
        std::size_t nl = buf.find('\n', pos);
        const bool last = (nl == std::string::npos);
        const std::string_view line{buf.data() + pos,
                                    (last ? buf.size() : nl) - pos};
        pos = last ? buf.size() : nl + 1;
        if (line.empty()) continue;

        // 坏线消耗该消息，而不是 线程。这是一个被撕裂
        // 的最终附加的地方：它 解析 作为垃圾并被丢弃，
        // 使每条完整的消息完好无损。
        json j = parse_line(parser, line);
        if (j.is_discarded()) {
            AGT_LOG(General, Warn, "thread_log.read",
                    "skipping unparseable line in {}", log_.string());
            continue;
        }
        auto m = persistence::message_from_json(j);
        if (!m) {
            AGT_LOG(General, Warn, "thread_log.read",
                    "skipping malformed message in {}: {}",
                    log_.string(), m.error().render());
            continue;
        }
        out.push_back(std::move(*m));
    }
    return out;
}

// ── 写入 ────────────────────────────────────────────────────────────

bool ThreadLog::append(const Message& m) {
    if (!persistable(m)) return true;   // 默默地不是-persisted，正如设计的那样
    const std::string line = encode_line(m);
    if (line.empty()) return false;

    // HEAL A TORN TAIL 首次。
    //
    // 如果上一次运行在 -append 中期终止，
    // 则日志以部分行结束且没有换行符。将 CONCATENATE
    // 附加到旧消息的尸体上，将新消息附加到旧消息的尸体上，
    // 产生一行不可解析的行 - 丢失新消息以及撕裂的消息，
    // 默默地，永远。 字节已经无法恢复；
    // 绝对不能发生的事情就是把好消息带给他们。
    //
    // 截断到最后一个完整行是诚实的修复：这正是
    // range() 已经报告的内容，因此该文件现在与每个
    // 读者所看到的内容相匹配。
    if (torn_tail_at_ != kNoTear) {
        std::error_code ec;
        fs::resize_file(log_, static_cast<std::uintmax_t>(torn_tail_at_), ec);
        if (ec) {
            AGT_LOG(General, Error, "thread_log.append",
                    "cannot truncate torn tail of {}: {}",
                    log_.string(), ec.message());
            return false;
        }
        AGT_LOG(General, Warn, "thread_log.append",
                "dropped a torn final line from {} before appending",
                log_.string());
        if (!offsets_.empty() && offsets_.back() == torn_tail_at_)
            offsets_.pop_back();
        torn_tail_at_ = kNoTear;
    }

    // 该行及其 '\n' 在 一个 写入中消失，
    // 因此崩溃或其他写入者无法分割消息。偏移量是从同一描述符读回的，
    // 因此它是 字节 实际着陆的位置。索引之前的日志：
    // 超过日志末尾的偏移量是一个不一致open()不能自身
    // -correct。
    std::string rec;
    rec.reserve(line.size() + 1);
    rec += line;
    rec += '\n';
    const auto landed = persistence::append_record(::agentty::IoAccess::grant(), log_, rec);
    if (!landed) return false;
    const std::uint64_t at = *landed;

    offsets_.push_back(at);

    // APPEND 是一个新的偏移量——8 字节——而不是重写整个索引。
    // 索引是append-only，原因与日志完全相同，因此两个文件
    // 的一个回合成本为O(1) 字节，并且在任何线程长度下都保持这种状态。
    // 每回合重写它的时间为 O（消息）：2519-message
    // 线程（不可测量）上有 20 KB，但在 50k 时有
    // 400 KB，无限制增长——整个设计要避免的形状。
    //
    // 如果追加失败，则 in-memory 偏移对于该会话仍然是正确的，
    // 并且 open() 下次修复文件：它根据日志验证
    // 索引并在任何不匹配时重建索引。这与两次写入之间的崩溃所
    // 采用的 self-healing 路径相同。
    if (!append_offset_(at)) {
        AGT_LOG(General, Warn, "thread_log.append",
                "index append failed for {} (will rebuild on next open)",
                log_.string());
    }
    return true;
}

bool ThreadLog::truncate_to(std::size_t n) {
    if (n >= offsets_.size()) return true;
    const std::uint64_t cut = offsets_[n];

    // 日志FIRST，索引第二。两者之间的崩溃会留下一个偏移量
    // n 等于新日志大小的索引，该索引 open() 已经拒绝
    // (offsets_.back() >= log_bytes)
    // 并通过扫描重建。另一个命令会在长日志上留下一个短索引，
    // 最后一个索引消息将默默地吸收其后面的行。
    std::error_code ec;
    fs::resize_file(log_, static_cast<std::uintmax_t>(cut), ec);
    if (ec) {
        AGT_LOG(General, Error, "thread_log.truncate",
                "cannot cut {} at {}: {}", log_.string(), cut, ec.message());
        return false;
    }
    offsets_.resize(n);
    // `cut`之后的所有内容都消失了，包括任何撕裂的最后一行。
    torn_tail_at_ = kNoTear;

    // 如果较早的append_offset_失败，则索引可以比offsets_短；
    // 调整大小会用零填充。在这种情况下，将其全部重写（罕见，
    // O(n) 的 8-byte 条目），否则将其剪切到位。
    const auto want = static_cast<std::uintmax_t>(n) * kOffsetWidth;
    if (file_size_or_zero(idx_) >= want) {
        fs::resize_file(idx_, want, ec);
        if (!ec) return true;
    }
    if (!write_index_()) {
        AGT_LOG(General, Warn, "thread_log.truncate",
                "index cut failed for {} (will rebuild on next open)",
                log_.string());
    }
    return true;
}

bool ThreadLog::rewrite(const std::vector<Message>& msgs) {
    std::string body;
    std::vector<std::uint64_t> offs;
    offs.reserve(msgs.size());
    for (const auto& m : msgs) {
        if (!persistable(m)) continue;
        const std::string line = encode_line(m);
        if (line.empty()) continue;      // 参见encode_line：已记录，已跳过
        offs.push_back(static_cast<std::uint64_t>(body.size()));
        body += line;
        body += '\n';
    }

    // 两个文件都原子地着陆。 write_json_atomic 是
    // temp+fsync+rename，因此中断的重写会将前一个日志
    // 和 保留在适当的位置，而不会是带有旧索引的新日志。
    if (!persistence::write_json_atomic(::agentty::IoAccess::grant(), log_, body)) return false;
    offsets_ = std::move(offs);
    torn_tail_at_ = kNoTear;   // 完全重写总是干净利落地结束
    if (!write_index_()) {
        AGT_LOG(General, Warn, "thread_log.rewrite",
                "index write failed for {} (will rebuild on next open)",
                log_.string());
    }
    return true;
}

// ── 整个-thread便利 ────────────────────────────────────

bool ThreadLog::exists() const noexcept {
    // is_regular_file，不存在：位于日志路径
    // 的目录（或套接字，或其他任何东西）是NOT日志。
    // 将其视为一个，使得加载程序获取日志分支，找不到任何消息，
    // 并交回一个空的 线程，而旁边有一份完美的遗留文档。
    std::error_code ec;
    return fs::is_regular_file(log_, ec);
}

Thread ThreadLog::load_thread() const {
    Thread t = meta_thread_;
    t.messages = all();
    // 压缩记录在消息计数已知之前被读取（元数据
    // 是单独的文件），因此应用整个
    // -document加载器所应用的相同的界限：指向
    // 超过记录末尾的记录来自中断的保存并且不能存活。
    persistence::clamp_compactions(t);
    return t;
}

bool ThreadLog::store_thread(const Thread& t) {
    // 首先消息。如果日志写入失败，元数据将被保留，
    // 因此磁盘上的元数据对将保留上次完整写入的元数据，
    // 而不是描述旧历史记录的新元数据。
    if (!rewrite(t.messages)) return false;
    return set_meta(t);
}

void ThreadLog::remove() {
    std::error_code ec;
    fs::remove(log_, ec);
    fs::remove(idx_, ec);
    fs::remove(meta_, ec);
    offsets_.clear();
    torn_tail_at_ = kNoTear;
    meta_thread_ = Thread{};
}

} // namespace agentty
