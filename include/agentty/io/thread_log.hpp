#pragma once
// agentty::ThreadLog-线程作为消息的append-only日志。
//
// 该 PROBLEM
//
// 线程今天被存储为ONE JSON文档。每隔 字节 打开它 解析：
// 在真正的 29 MB / 2519-message 线程 上，
// 即可以绘制一行之前 226 毫秒，并且保存一个新回合会重写所有
// 29 MB。随着对话的继续，这两种成本都会无限制地增长，
// 而且都与 线程 实际显示在屏幕上的数量（约 60 条消息）无关。
//
// 该 SHAPE
//
//   <id>.jsonl 每行一条消息，附加，从未重写 <id>.
//   ofs 每条消息一个 8-byte 偏移 — 20 KB 对于 2519 条消息
//
// 在同一个 线程 上测量，今天为 226 毫秒：
//
//   读取整个线程 49毫秒（2519个小解析击败一大条）读取最后
//   60条消息1.4毫秒（寻道ofs[n-60]，
//   解析到EOF）附加一圈0.01毫秒（一行+8个字节，均fsynced）
//
// 完整读取时的 4x 不是我们设计的优化——它超出了
// -delimiting 的范围。 解析器 传递了 2519 个小文档，
// 每个文档都以小短 -lived 分配 开头，
// 而不是在 29 个 MB 中携带一个深层嵌套上下文。
//
// 原因 NO DATABASE
//
// 对于 250 KB C 依赖性以及您自己的历史记录中 `grep`/`jq` 的丢失，
// SQLite 测量了 1.3 ms，而该设计的 1.4 ms 在噪声范围内。
// 该子系统故意添加NO第三-party依赖项：它是<fstream>、
// <filesystem>和persistence.cpp中已经存在的per
// -message编解码器。有关完整比较，
// 请参阅 docs/THREAD_STORE.md §3.1（压缩也丢失了：一半的磁盘
// 空间是 加载 的三倍，并且它完全取消了查找）。
//
// CRASH SAFETY，FOR FREE
//
// Append-only加上line-delimited就是为什么这里没有WAL和CRC的原因：
//
//   * 已经写入的内容不会被再次写入，因此撕裂的追加可能会
//     仅损坏LAST线。读取器删除尾部不可解析的行，
//     这正是正确的恢复 - 通过截断真实日志 mid-line
//     进行验证：读回 2538 条消息，删除部分消息。
//   * `.ofs`是CACHE，不是事实。它可以被删除、截断或
//     充满垃圾； open() 检测到不匹配并通过扫描换行
//     符重建它（29 MB 线程 需要 12 毫秒）。
//     正确性与否无关——只有速度。
//   * Write顺序是log→fsync→offset。他们之间的一场碰撞离开了
//     偏移文件短，即open()的情况已经修复。
//
// 磁盘消息格式的唯一权威定义。
//
// 行由persistence::
// message_to_json产生并由persistence::message_from_json读回
// ——与整个-document格式使用相同的功能。这是故意的：两个编解码
// 器会发生漂移，并且这种漂移会以悄无声息的破坏历史的形式浮出水面，
// 无论哪种格式碰巧写入频率较低。

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "agentty/domain/conversation.hpp"
#include "agentty/io/persistence.hpp"   // DeserializeError

namespace agentty {

class ThreadLog {
public:
    // 打开 `id` 的日志，如果不存在则创建它。
    //
    // 仅读取并验证`.ofs`——消息正文被读取了NOT，
    // 因此这是O(消息计数)，而不是O(字节)。
    // 丢失、短或损坏的索引会在此处重建而不是报告：
    // 索引是 缓存，用户永远不应该看到他们不应该知道
    // 存在的文件的错误。
    [[nodiscard]] static std::expected<ThreadLog, persistence::DeserializeError>
    open(const ThreadId& id);

    // 同样，对于显式路径（测试、迁移、导出）。 `log_path` 是 .
    // jsonl；索引是`log_path`，扩展名被替换。
    [[nodiscard]] static std::expected<ThreadLog, persistence::DeserializeError>
    open_path(std::filesystem::path log_path);

    // 仅日志及其索引，而不读取元数据边车：保存需要什么（
    // 它仅附加、截断和写入元数据，从不读取它）。
    // 结果上的 meta() 为空。 Sidecar 保存了
    // 整个压缩历史，因此跳过它是 末尾 保存 的大部分成本。
    [[nodiscard]] static std::expected<ThreadLog, persistence::DeserializeError>
    open_for_write(const ThreadId& id);

    [[nodiscard]] std::size_t size() const noexcept { return offsets_.size(); }
    [[nodiscard]] bool empty()      const noexcept { return offsets_.empty(); }

    [[nodiscard]] const std::filesystem::path& path()       const noexcept { return log_; }
    [[nodiscard]] const std::filesystem::path& index_path() const noexcept { return idx_; }
    [[nodiscard]] const std::filesystem::path& meta_path()  const noexcept { return meta_; }

    // ── 线程 元数据 ─────────────────────────────────────────────
    //
    // 线程不仅仅是它的消息：它还携带标题、分叉出处、
    // per-thread RAG覆盖、时间戳和传输-only压缩记录。
    // 这些都不属于 IN 消息日志——它是 可变（标题被重命名；
    // `updated_at` 每回合都会移动），日志是append
    // -only——所以它存在于一个完整写入的小 sidecar 中。
    //
    // 它很小（几百个字节，加上压缩摘要）并且在保存上重写，
    // 这对于作为一个单元改变并且每次打开仅
    // 读取一次的东西来说是正确的交易。
    //
    // `meta()`返回线程和`messages` 空—
    // —调用者决定附加多少历史记录，这是日志的全部要点。
    [[nodiscard]] Thread meta() const { return meta_thread_; }
    bool set_meta(const Thread& t);

    // 解析消息[来自，至)。夹在原木上；从 >= 到产生 {}。
    //
    // 这是ONLY读取路径。整个-thread 调用为范围(0,
    // size())，加窗后的调用为范围(size()-60,
    // size())；两者都是相同的代码，因为区别仅在于查找的位置。
    //
    // 解析失败的行是SKIPPED而不是读取失败。
    // 2519-message历史记录中的单个损坏行不得使用户花费其他2518
    // ——对于许多文件中的损坏的线程文件已经做出相同的判断
    // load_all_threads。
    [[nodiscard]] std::vector<Message> range(std::size_t from,
                                             std::size_t to) const;

    [[nodiscard]] std::vector<Message> all() const { return range(0, size()); }

    // 追加一条消息：向日志写入一行，向索引写入一个偏移量。
    // 线程大小的O(1)——这取代了每次都重写整个文件。
    // 如果日志写入失败，则返回 false（在这种情况下，
    // 索引中也不会附加任何内容，因此两者保持
    // 一致）。
    bool append(const Message& m);

    // 删除索引 `n` 上的每条消息。 O(1)：偏移量准确地说明了第
    // n 行开始的位置，因此两个文件都被剪切，每个文件都调整了一次大小，
    // 并且没有任何内容是 re-encoded。这就是让 保存 仅
    // 替换 线程 发生变化的部分（开放回合，刚刚解决的工具），
    // 然后再次 append()，而不是 rewrite()-ing 每轮
    // 的整个历史。 n >= size() 是 no-op。
    bool truncate_to(std::size_t n);

    // 替换整个日志。真正重写历史的路径（压缩、消息编辑、
    // 分叉）以及迁移都需要它。 O(n)，但很少见，
    // 用户-initiated。
    //
    // 原子：在旧的日志和索引旁边构建新的日志和索引，
    // 并将两者重命名到位，因此中断的重写使先前的历史
    // 保持完整而不是半-written的历史。
    bool rewrite(const std::vector<Message>& msgs);

    // ── 整个-thread便利 ─────────────────────────────────────
    //
    // 元数据+每条消息，在一个调用中。这就是商店 接口边界 今天想要的，
    // 因为应用程序仍然期望完全驻留 线程（请参阅
    // docs/THREAD_STORE.md §9.0）。它不是快速路径 -
    // 即 meta() 加上窗口 range() - 但它已经比 解析 整个
    // -document JSON 快约 4 倍，所以值得首先拥有。
    [[nodiscard]] Thread load_thread() const;

    // 元数据+完整历史记录，一起写入。
    // 由 保存 以及从旧格式的迁移使用。
    bool store_thread(const Thread& t);

    // 当该 线程 在磁盘上有日志时为真。
    // 让调用者更喜欢日志并回退到旧文档而无需猜测。
    [[nodiscard]] bool exists() const noexcept;

    // Delete日志、其索引及其元数据。斑点被 不 触及：
    // 它们通过内容哈希在 线程 上共享，因此回收它们
    // 是单独的扫描，而不是该函数的业务。
    void remove();

private:
    ThreadLog() = default;

    // 扫描日志计数行开始。丢失或不一致索引的恢复路径，
    // 以及旧版 线程 如何获取第一个索引。
    void rebuild_index_();
    // Write全索引。 O(消息) — 仅用于 rewrite() 和修复。
    [[nodiscard]] bool write_index_() const;
    // 附加一个偏移量。 O(1)，并且是一轮执行的唯一索引写入。
    [[nodiscard]] bool append_offset_(std::uint64_t at) const;

    // 没有终止换行符的最后一行的偏移量——i.e。
    // 写入消息和写入 '\n' 之间发生崩溃。 kNoTear 当
    // 日志干净地结束时。
    //
    // 读者已经应付了(部分行未能到达解析并被丢弃)。
    // WRITER 存在这种情况：附加到未终止的行会将新
    // 消息粘合到损坏的行上并丢失两者，因此 append()
    // 首先截断撕裂。检测起来很便宜——它是一个位于
    // 已知位置的 字节。
    static constexpr std::uint64_t kNoTear = ~0ull;

    std::filesystem::path      log_;
    std::filesystem::path      idx_;
    std::filesystem::path      meta_;
    std::vector<std::uint64_t> offsets_;
    std::uint64_t              torn_tail_at_ = kNoTear;
    Thread                     meta_thread_;   // 消息始终为空

    [[nodiscard]] static std::expected<ThreadLog, persistence::DeserializeError>
    open_impl(std::filesystem::path log_path, bool read_meta);
};

} // namespace agentty
