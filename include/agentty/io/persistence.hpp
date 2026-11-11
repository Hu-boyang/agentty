#pragma once
// 用于存储域的文件系统适配器。  存在于 `io/` 中，
// 因为它与文件系统通信；它所满足的概念存在于
// `agentty/store/store.hpp`中。  作为免费函数和
// `FsStore` 薄包装器公开，因此测试可以放入替代方案，
// 而无需触及应用程序的其余部分。

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "agentty/domain/conversation.hpp"
#include "agentty/domain/thread_delta.hpp"
#include "agentty/store/store.hpp"
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::persistence {

// 锁订单(agentty/util/lock_levels.hpp)。
// 采用 锁 的函数表示哪个作为功能参数：
//
//   below_file_lane进入共享-file通道（线程索引、
//                     设置文件）：调用它不持有排名在
//                     persistence.file_lane或之上的任何内容，
//   低于<leaf>仅占用其他线程上的叶（
//   异步写入器的状态）解锁块（刷新加入写入器）
//
// 一个调用从一个锁里面排名太高，不会有编译。

// ── 带类型的 反序列化 错误 ─────────────────────────────────────────
// JSON↔值-type边界返回`expected<T, DeserializeError>`，
// 而不是默默默认missing/invalid字段。
// 目录遍历加载器使用它来记录+跳过坏文件，而不是默默地
// 删除它们。
enum class DeserializeErrorKind : std::uint8_t {
    JsonParse,         // 正文 无效 JSON
    MissingField,      // 缺少必填字段
    InvalidValue,      // 字段存在但类型错误/超出范围
    InvalidVariantTag, // 未知鉴别器(e.g.status_tag，角色)
    Io,                // 文件-open/读取失败
};

struct DeserializeError {
    DeserializeErrorKind kind = DeserializeErrorKind::JsonParse;
    std::string field;        // 到违规场地的虚线路径，如果 N/A 则为 ""
    std::string detail;
    [[nodiscard]] std::string render() const;
};

[[nodiscard]] std::filesystem::path data_dir();
[[nodiscard]] std::filesystem::path threads_dir();

// Write `t`的CLEAN、human/LLM-readable转录本
// （角色+文本，工具调用折叠成one-line标记——没有on
// -disk <id>.JSON的原始JSON噪音）。
// 返回写入的绝对路径，失败时返回空。由 fork 使用：新的 线
// 程 开始 空（零上下文），并且模型根据需要读取 此 文件以获取较
// 早的上下文，而不是继承整个转录本。
[[nodiscard]] std::filesystem::path
write_thread_transcript_md(Io, const Thread& t);

// Directory-walking 加载程序：返回每个 线程，我们可以 反序列化，
// **仅元数据**（id、标题、created_at、updated_at —
// `messages` 为空）。 线程 选择面板只需要元数据；完整消息 正文 是 已加载，
// 在选择时通过 `load_thread_file` 延迟。这使得启动 RAM
// 与 线程 计数成比例，而不是与成绩单 字节 成比例 — -disk 历史
// 记录上的 649-thread / 376 MB 之前已加载 ~1.2 GB。
//
// 失败的文件（错误的 JSON、缺少必填字段）被记录到 stderr
// 并被跳过 — per-file 失败类型是 `DeserializeError`，
// 在内部保存；想要严格语义的调用者可以使用下面的
// `load_thread_file`。
[[nodiscard]] std::vector<Thread> load_all_threads(Io, const lock_levels::below_file_lane&);

// 填写线程 已保存存在之前的选择面板摘要（预览、
// 消息计数），并将其存储在索引中，以便一次完成。仅返回 线程、
// 头文件。每个 线程（完整的 加载）速度很慢，
// 因此调用者在列表列出后，小批量地从 UI 线程 运行它。
[[nodiscard]] std::vector<Thread> backfill_picker_summaries(Io, const lock_levels::below_file_lane&,
                                                            const std::vector<ThreadId>& ids);

// 严格按照-file装载机。返回 带类型的 错误，以便调用者可以对特定类型做出反应（e.g。
// 表面 MissingField 为 "schema upgrade needed"，JsonParse 为
// "corrupt file, restore from backup"）。（文件损坏，应从备份恢复。）
[[nodiscard]] std::expected<Thread, DeserializeError>
load_thread_file(Io, const std::filesystem::path& p);

// 对整个线程中的保存进行排队。写入者仍然只重写磁盘上
// 更改的内容，但调用者需要为复制每条消息付费。
void save_thread(Io, const maya::below<maya::leaf>&, const Thread& t);
// 纯净。 指纹 `t` 的每个持久消息，与 `prev`（为此
// 线程 描述的最后一个 保存 的 FPS）进行比较，
// 并仅复制从第一个差异开始的消息。空的 `prev` 会发送所有内容。
[[nodiscard]] ThreadDelta describe_save(const Thread& t, const std::vector<std::uint64_t>& prev);
// 纯净。 `t` 的每个字段（除了其消息）以及 选择面板 摘要。
[[nodiscard]] Thread thread_header(const Thread& t);
// 对所描述的保存进行排队。当增量没有基础时返回 false（没有
// 排队）：此 线程 的较早 保存 失败或从未见过。然后调用者发送
// 整个 线程（describe_save 和空的 `prev`）。
// from==0的增量是整个线程并且总是排队。
[[nodiscard]] bool save_thread_delta(Io, const maya::below<maya::leaf>&, ThreadDelta d);
// 阻塞直到每个排队的后台保存都已命中磁盘。最后 状态更新函数 步骤
// 后关闭时的 调用（退出 处理函数 发出最终的 save_thread），
// 因此我们在退出时不会丢失最 -recent 记录。它加入了 writer，
// 因此需要 `unlocked`。
void flush_pending_saves(Io, const maya::unlocked&);
void delete_thread(Io, const lock_levels::below_thread_log&, const ThreadId& id);
// 重新命名 已保存 线程。与 save_thread 在同一个
// 写入器上排队，因此它会在已经运行的 线程 的任何写入之后着陆，
// 并且仍在排队的 保存 会获取新标题，而不是将旧标题写回其上。
// updated_at被保留，因此线程在选择面板中不移动。 `title` 按给
// 定方式存储：首先使用 clean_title() 清理它。
// `renamed_at`（unix 秒）与其一起存储；后来的 保存 带有较旧的邮票，
// 保留了这个标题而不是它自己的标题。 `name_is_auto` 标记已清除的重命名：
// 邮票仍然赢得过期副本，但线程可能是auto-titled。
void rename_thread(Io, const maya::below<maya::leaf>&, const ThreadId& id, std::string title,
                   std::int64_t renamed_at, bool name_is_auto = false);

// 线程保留。删除过去 `keep_days` 天内没有活动（文件 mtime）
// 的 线程，跳过 `keep`（打开的）。 keep_days <= 0 不
// 执行任何操作。它们的 blob 留给 blob gc。返回它删除的内容（或者
// 将使用 dry_run）。
struct ExpireStats { std::size_t threads = 0; std::uintmax_t bytes = 0; };
ExpireStats expire_threads(Io, const lock_levels::below_thread_log&, int keep_days,
                           const ThreadId& keep, bool dry_run = false);

// $AGENTTY_THREADS_KEEP_DAYS，或当未设置或无效时为0（保留所有内容）。
[[nodiscard]] int thread_keep_days(Io);

// per-message 指纹增量保存路径用于查找改变的内容。
// 暴露 仅，因此 保存 工作台可以与 保存 的其余部分
// 分开定价；应用程序中没有任何内容应该 调用。
[[nodiscard]] std::uint64_t debug_message_fingerprint(const Message& m);
// 测试挂钩：使编写者忘记其对`id`的了解，就像失败的保存所做的那样。
// 应用程序中没有任何内容应该 调用。
void debug_forget_saved(const maya::below<maya::leaf>&, const ThreadId& id);
// 测试钩子：设置本进程认为其最后已加载或写入的文档（保存
// -time合并基线）。让一个测试进程运行两个 agentty 实例，
// 每个实例都有自己的启动 快照。应用程序中没有任何内容应该
// 调用。
void debug_set_settings_baseline(std::string_view json_text);

[[nodiscard]] store::Settings load_settings(Io);
// Write settings.json，然后告诉设置缓存 (on_settings_written)，
// 以便它丢弃其副本。 缓存 的反应耗尽了它的队列——等待——所以这需要
// `unlocked`。
void save_settings(Io, const maya::unlocked&, const store::Settings& s);
// 同样的写入WITHOUT告诉缓存:对于缓存本身来说，
// 自己的写入一定不能使其无效。仅采用设置通道。
void save_settings_quiet(Io, const lock_levels::below_file_lane&, const store::Settings& s);

// 在每次将 DIRECTLY 写入磁盘的 save_settings()之后调用。
//
// TUI通过假定它是唯一写入器的写入-behind 缓存
// (runtime/app/settings_cache.hpp)来读取设置。
// 它不是：凭据帮助程序（credentials::add_key，
// vault::key_clear，account_switch，--model/
// --provider CLI路径）调用 save_settings直接在这里，
// 因为它们也在不存在Deps的路径上运行。持有此类写入之前的副本的 缓存 会将其发布回
// 下一个 保存 并默默地撤消它。
//
// 因此，缓存在此注册其自身并丢弃其副本。这存在于 I/O 层，
// 而不是在每个写入器中，因为写入器跨越 provider/、
// io/ 和 main() — 否则新的写入器将必须 KNOW
// 才能失效，这正是一种被遗忘的规则。观察者收到作者的
// `unlocked`：它可能会等待（耗尽），并且类型如此表示。
void on_settings_written(std::function<void(const maya::unlocked&)> observer);

[[nodiscard]] ThreadId new_id();
// 64个随机位作为16个十六进制数字；支持 new_id() 和 new_message_id()。
[[nodiscard]] std::string random_hex_id();
[[nodiscard]] std::string title_from_first_message(std::string_view text);

// 最长的标题，在字节中：title_from_first_message
// 产生什么以及重命名是什么，因此每个选择面板行具有相同的预算。
inline constexpr std::size_t kTitleMaxBytes = 60;

// 带类型的或粘贴的标题可以安全地存储和绘制在一行上：换行
// 符和制表符变成空格，其他控件字节被删除（标题到达终端，
// 因此没有转义序列），无效的UTF-8被修复，
// 结果在代码-point边界上被剪切到`max_bytes`并
// 修剪掉两端的空白。可能会返回 ""。
[[nodiscard]] std::string clean_title(std::string_view text,
                                      std::size_t max_bytes = kTitleMaxBytes);

// 选择面板预览：用户带类型的的第一条消息以与标题相同的方式
// 创建了一条安全线。
inline constexpr std::size_t kPreviewMaxBytes = 200;

// 从 `messages` 开始填充 out.preview 和 out.message_count。
// 对于空列表不执行任何操作，因此 头文件-only 重新加载无法将其清空。
void summarise_for_picker(const std::vector<Message>& messages, Thread& out);
// 同样，读取t.messages。
void refresh_picker_summary(Thread& t);

// `content`到`target`(persistence::write_atomic和Durability::Durable)
// 的原子+持久写入：唯一的临时、fsync、重命名。读者可以看到旧文件或新文件。
// 如果出现任何 I/O 故障，则返回 False（临时值被删除）。
bool write_json_atomic(Io, const std::filesystem::path& target,
                       const std::string& content);

// ── 人数 -message 代码c ────────────────────────────────────────────────
//
// "a Message on disk"的单一定义。暴露是因为
// 线程 LOG (io/thread_log.hpp) 每行存储其中一个，
// 并且必须向整个 -document 编写器生成 字节
// -identical 输出 - 两个编解码器会发生漂移，
// 并且这种漂移只会以不常写入的格式以无声的破坏历史的形式出现。
//
// 双方都有值得不重复的真实行为：作者擦除到有效的UTF
// -8(任意工具输出可以保存nlohmann拒绝转储
// 的Latin-1或Shift-JIS 字节)并将
// 大载荷提升到blob存储中；读者接受每一个历史形状，
// 包括在 blob 存在之前编写的内联 载荷。
[[nodiscard]] nlohmann::json message_to_json(const Message& m);
[[nodiscard]] std::expected<Message, DeserializeError>
message_from_json(const nlohmann::json& j);

// 同样，对于线程携带EXCEPT其消息的所有内容：标题、
// 分叉出处、RAG覆盖、时间戳、压缩记录。
//
// 分裂是因为两半在磁盘上有不同的生命周期。
// 消息是append-only；元数据是可变并被整体重写。
// 正是出于这个原因，线程日志将它们存储在单独的文件中，
// 并且重用它们，因此仍然存在每个字段的on
// -disk形式的ONE定义。
//
// `thread_meta_to_json`从不发出
// "messages"密钥； `thread_meta_from_json` 会
// 忽略存在的一个，因此它也可以指向遗留的整个 -thread 文档以仅读取 头文件。
[[nodiscard]] nlohmann::json thread_meta_to_json(const Thread& t);
[[nodiscard]] std::expected<Thread, DeserializeError>
thread_meta_from_json(const nlohmann::json& j);

// 丢弃其边界位于`t.messages`末尾之后
// 的压缩记录。 `thread_meta_from_json`
// 本身无法做到这一点 - 在日志格式中，元数据在任何
// 消息之前被读取 - 因此调用者在附加记录后应用它。
// 过期 记录仅在中断的 保存 中保留下来，
// 否则会使 传输 总结不再存在的前缀。
void clamp_compactions(Thread& t);

// 加载通过id 1 线程，优选LOG格式并回退到传统的whole
// -document `<id>.json`。
//
// 这是接口边界，其中新格式变为加载-bearing。
// 两种格式都会被永久读取：用户的历史记录早于日志，
// 并且在 线程 成为下一个 已保存 之前不会重写任何内容。
[[nodiscard]] std::optional<Thread> load_thread_by_id(Io, const ThreadId& id);

// 保留子代理运行以供编辑器打开(threads/subagents/<id>.JSON)。
// 不在线程列表中； blob GC 读取它们。
bool save_subagent_thread(Io, const Thread& t);
[[nodiscard]] std::optional<Thread> load_subagent_thread(Io, const ThreadId& id);

} // namespace agentty::persistence

namespace agentty::io {

// 文件系统-backed存储满足agentty::store::Store。
// 仅通过app::deps(I/O)到达，因此它持有主机给它的I/O。
//
// 锁顺序：store::Store概念在其签名中没有能力，因为存储是EFFECT
// 接口边界——它仅被主机和init到达的副作用主机(app::Host)
// 和deps()调用，两者都在步骤之间的循环线程上，其中没有任何内容。
// 这与 I/O 做出的承诺 ("I am an effect entry point") 相同；
// 这里的每个 调用 都将其重新-states 作为功能根，
// 一个地方，进行审查（lint：io_roots + cap_roots
// 列出此文件）。
class FsStore {
public:
    explicit FsStore(Io io) : io_(io) {}
    [[nodiscard]] std::vector<Thread> load_threads() {
        return maya::thread_root(
            [&](const maya::unlocked& c) { return persistence::load_all_threads(io_, c); });
    }
    [[nodiscard]] std::optional<Thread> load_thread(const ThreadId& id) {
        return persistence::load_thread_by_id(io_, id);
    }
    [[nodiscard]] std::vector<Thread> backfill_summaries(const std::vector<ThreadId>& ids) {
        return maya::thread_root([&](const maya::unlocked& c) {
            return persistence::backfill_picker_summaries(io_, c, ids);
        });
    }
    void save_thread(const Thread& t) {
        maya::thread_root([&](const maya::unlocked& c) { persistence::save_thread(io_, c, t); });
    }
    void delete_thread(const ThreadId& id) {
        maya::thread_root([&](const maya::unlocked& c) { persistence::delete_thread(io_, c, id); });
    }
    [[nodiscard]] store::Settings load_settings()          { return persistence::load_settings(io_); }
    void save_settings(const store::Settings& s) {
        maya::thread_root([&](const maya::unlocked& c) { persistence::save_settings(io_, c, s); });
    }
    // 设置缓存自己的磁盘写入：不通知缓存。
    void save_settings_quiet(const maya::unlocked& c, const store::Settings& s) {
        persistence::save_settings_quiet(io_, c, s);
    }
    [[nodiscard]] ThreadId new_id()              { return persistence::new_id(); }
    [[nodiscard]] std::string title_from(std::string_view text) {
        return persistence::title_from_first_message(text);
    }

private:
    Io io_;
};

static_assert(store::Store<FsStore>);

} // namespace agentty::io
