#pragma once
// agentty::persistence——agentty写入文件的一种方式，
// 另一个agentty也可能正在写入。
//
// jaal拥有原语：用于线程的maya::
// guarded和用于进程的maya::platform::native_file_lock。
// 共享哪些文件、无法获取 锁 时该怎么办以及 字节 如何到达磁盘，这些都是 agentty
// 的策略，它们就存在于此。 agentty 调用 文件 锁 中没有其他内容，
// 也没有重命名文件 (tests/lint/disk_writes.py) 上的临时文件。
//
// 两者 LOCKS，始终
//
// 受保护的进程对于另一个进程不可见，并且POSIX记录锁
// 对于同一进程的另一个线程不可见(jaal一致性检查13)。
// 所以一个节取文件的in-process里，然后是文件锁。
//
// LOCK ORDER IS TYPE
//
// 每个泳道都有一个等级(lock_levels)。
// 输入的节具有证明 线程 仅具有较低等级的能力，因此两个节只能
// 按等级顺序嵌套，并且每个进程都以相同的顺序获取其文件 锁。
// cross-process订单是in-process订单，
// 编译器检查它。
//
// BOUNDED、和 绝不 FATAL
//
// 文件锁最多等待kSharedFileWait(jaal acquire_within，
// 锁 正文内合法的内核等待)。除此之外，或者在无法 锁 的文件系统上，
// 正文 单独保持通道运行，而 Held::cross_process 为
// false，每个文件记录一次。丢失更新是糟糕的一天； agentty
// 冻结在 对端 后面，并以 ^Z 停止，情况更糟。
//
// FAST BY DEFAULT
//
// 无竞争成本是边车上的1个open+1个fcntl，并且车道是互斥量。
// write_atomic 仅 fsyncs 必须在断电后存活的
// 内容（持久性），并使用最终模式创建其温度，因此没有 chmod 窗口，
// 也没有额外的系统调用。

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::persistence {

/// 一个部分在没有另一个进程的情况下继续进行之前等待另一个进程多长时间。
/// 这些文件之一的读取-modify-write花费的时间远低于一毫秒，
/// 因此超过此时间的持有者被停止，而不是忙碌。
inline constexpr std::chrono::milliseconds kSharedFileWait{5000};

/// 一个共享文件的 in-process 一半：所有者在文件路径
/// 旁边保留的静态。等级R将其置于lock_levels；
/// 其节嵌套的文件需要不同的等级，外部优先。
struct FileLane {};
template <class R = lock_levels::file_lane>
using LaneOf = maya::guarded<FileLane, R>;
using Lane   = LaneOf<lock_levels::file_lane>;

/// 区段的正文所看到的。
struct Held {
    /// 我们得到了cross-process保证，还是只有in-process保证？
    /// False意味着另一个实例仍然可以与我们交织。
    bool cross_process = false;
};

namespace detail {
/// 文件锁为`target`，最多等待kSharedFileWait。
/// 当无法拥有它时，nullopt（每个文件记录一次）。
std::optional<maya::platform::native_file_lock> acquire_cross(const std::string& target);
}  // namespace detail

/// Enter为`target`的部分：其通道，然后其文件锁。
/// 运行 `body(const held<R>&, Held, args...)`
/// 并离开。与 Guarded<T>::with 相同的规则：正文 是无捕获的，
/// 参数被移入，并且结果必须是可发送的。持有的<R>让正文处于R以上的状态，
/// 包括更高的-ranked部分。
template <class R, class F, class... Args>
auto with_shared_file(const std::type_identity_t<maya::below<R>>& cap, LaneOf<R>& lane,
                      const std::filesystem::path& target, F body, Args... args) {
    (void)body;
    return lane.with(cap, [](const maya::held<R>& h, FileLane&, std::string t, Args... a) {
        const auto cross = detail::acquire_cross(t);
        return F{}(h, Held{cross.has_value()}, std::move(a)...);
    }, target.string(), std::move(args)...);
}

/// 文件锁为多个文件，对于重写多个文件的通道正文(内存的用户+项目范围)。
/// 按照排序的路径顺序，每个进程中的顺序相同，因此两个进程不能
/// 各自持有一个进程并等待另一个进程。 Bounded 和
/// best-effort 就像一个部分：cross_process
/// () 表示是否所有这些都已拥有。仅在所属车道的 正文 内。
class FileLocks {
public:
    explicit FileLocks(std::vector<std::filesystem::path> files);
    [[nodiscard]] bool cross_process() const noexcept { return all_; }
    FileLocks(const FileLocks&)            = delete;
    FileLocks& operator=(const FileLocks&) = delete;
private:
    std::vector<maya::platform::native_file_lock> held_;
    bool all_ = true;
};

// ── 写入 字节 ────────────────────────────────────────────────────

/// 什么写必须生存。
enum class Durability {
    /// 断电可能会丢失写入但不会撕毁文件：临时、重命名。
    /// 对于 缓存 以及从其他地方重建的任何东西。
    Atomic,
    /// 断电后仍能幸存：fsync重命名之前的数据和之后的目录。
    /// 对于用户可能会失去的任何东西。
    Durable,
};

struct WriteOptions {
    Durability durability = Durability::Durable;
    /// 创建文件时使用的 POSIX 模式（umask 仍适用于默认值）。
    /// 0600 表示秘密：临时变量天生就是私有的，因此没有可读的窗口。
    /// 在 Windows 上被忽略。
    unsigned mode = 0644;
    /// 如果目标有模式（用户文件），则保留目标的现有模式。
    bool keep_mode = false;
};

/// 将 `target` 原子地替换为 `bytes`：同一
/// 目录中具有该进程唯一的名称的临时文件和 调用，然后重命名。
/// 读者会看到旧文件或新文件，而不是混合的。创建父目录。
/// 任何失败时返回假（临时被移除）。
[[nodiscard]] bool write_atomic(Io, const std::filesystem::path& target,
                                std::string_view bytes, WriteOptions = {});

/// 在 一个 write(2) 中将 `bytes` 附加到 O_APPEND，
/// 因此并发附加器（线程 或进程）永远不会在记录内交错。 `bytes`
/// 应该是以 '\n' 结尾的整个记录​​。返回之前的持久 fsync。
[[nodiscard]] bool append_atomic(Io, const std::filesystem::path& target,
                                 std::string_view bytes,
                                 Durability = Durability::Atomic,
                                 unsigned mode = 0644);

/// append_atomic，加上记录着陆处的字节偏移量，
/// 在写入后从同一描述符读取（O_APPEND将其放在内核看到的末尾，
/// 而不是该进程最后看到的末尾）。失败时无效选择。
/// 仅当调用者是文件的唯一写入者时才作为偏移量有效，
/// 这是节所保证的。
[[nodiscard]] std::optional<std::uint64_t> append_record(Io, const std::filesystem::path& target,
                                                         std::string_view bytes,
                                                         Durability = Durability::Atomic);

/// 创造`target`保持`bytes`仅当它尚不存在时。
/// 读者永远不会看到它的部分写入：它是写给临时的，然后是硬的-linked到位，
/// 如果名称被占用，则失败。如果我们创造了它，那就对了。
[[nodiscard]] bool create_exclusive(Io, const std::filesystem::path& target,
                                    std::string_view bytes, unsigned mode = 0644);

/// 一个 凭据 文件的通道，由写入该文件的所有内容共享（其
/// 提供者的刷新、帐户切换）。排名 cred_file：从
/// 提供商的刷新通道内进入。通道由文件名（一个小型​​固定
/// 表）作为键控，因此两个提供者的刷新很少共享一个。
LaneOf<lock_levels::cred_file>& cred_lane(const std::filesystem::path& file);

/// 整个文件，或者当无法打开时为 nullopt。记录除 "missing"
/// 之外的错误。
[[nodiscard]] std::optional<std::string> read_file(Io, const std::filesystem::path& p);

/// 一个小共享文件的整个 read-modify-write，
/// 对于没有人与另一个嵌套的文件：读取该部分内的 字节，将它们交给
/// `edit` (当不存在时为 nullopt)，并原子地写回它返回的内容。
/// `edit` 返回 nullopt 以保留该文件。一个进程-wide通道（
/// 排名file_lane）为每个这样的文件提供服务；文件 锁 是每个路径的。
///
///     update_shared(I/O，上限，路径，选项，[](std::optional<std::string> old, Args...) ->
///                                            std::optional<std::string> {...}，args...);
enum class Update { Unchanged, Written, Failed };

Lane& small_files_lane();

namespace detail {
// IO来自正文段内：受保护不能携带呼叫者的I/O进来。
bool write_in_section(const std::string& target, const std::string& bytes, WriteOptions o);
bool append_in_section(const std::string& target, const std::string& bytes, Durability d,
                       unsigned mode);
bool create_in_section(const std::string& target, const std::string& bytes, unsigned mode);
}  // namespace detail

/// 不需要I/O的小agentty-owned文件（配置、
/// 信任存储）的普通读取：它不改变任何内容，并且其调用者包括锁 正文和纯检查。
/// 与 read_file 相同的结果。
[[nodiscard]] std::optional<std::string> peek_file(const std::filesystem::path& p);

template <class F, class... Args>
Update update_shared(Io, const lock_levels::below_file_lane& cap,
                     const std::filesystem::path& target, WriteOptions o, F edit, Args... args) {
    (void)edit;
    return with_shared_file(cap, small_files_lane(), target,
        [](const maya::held<lock_levels::file_lane>&, Held, std::string t, WriteOptions w,
           Args... a) {
            std::optional<std::string> next = F{}(peek_file(t), std::move(a)...);
            if (!next) return Update::Unchanged;
            return detail::write_in_section(t, *next, w) ? Update::Written : Update::Failed;
        }, target.string(), o, std::move(args)...);
}

}  // namespace agentty::persistence
