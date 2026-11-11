// src/io/shared_file.cpp — 参见 include/agentty/io/shared_file.hpp

#include "agentty/io/shared_file.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>

#include "agentty/util/logx.hpp"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#  include <share.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace agentty::persistence {

namespace fs = std::filesystem;

namespace {

// 每个文件记录降级ONCE：现在不能获取的锁也不能在下
// 一个保存上获取，并且每个保存一行将埋葬日志。
// 快照，而不是锁：它在通道的正文内运行。
bool first_failure_for(const std::string& path) {
    static maya::snapshot<std::set<std::string>> seen;
    if (seen.load()->count(path)) return false;
    const auto after = seen.update([](const std::set<std::string>& s, const std::string& p) {
        auto out = s;
        out.insert(p);
        return out;
    }, path);
    return after->count(path) == 1;
}

// 其他编写者无法选择的临时名称：pid + process-wide
// 计数器。与目标目录相同，因此重命名保留在一个文件系统上。
fs::path temp_for(const fs::path& target) {
    static maya::snapshot<unsigned long long> seq{0ull};
    const auto n = *seq.update([](const unsigned long long& v) { return v + 1; });
#ifdef _WIN32
    const unsigned long pid = ::GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    fs::path t = target;
    t += ".tmp." + std::to_string(pid) + "." + std::to_string(n);
    return t;
}

bool write_all(int fd, const char* p, std::size_t n) {
    while (n > 0) {
#ifdef _WIN32
        const int w = ::_write(fd, p, static_cast<unsigned>(n > 0x40000000u ? 0x40000000u : n));
#else
        const ssize_t w = ::write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
#endif
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool sync_fd(int fd) {
#ifdef _WIN32
    return ::_commit(fd) == 0;
#elif defined(__APPLE__)
    // macOS上的fsync仅到达驱动器缓存； F_FULLFSYNC 到达媒体。
    return ::fcntl(fd, F_FULLFSYNC) == 0 || ::fsync(fd) == 0;
#else
    return ::fdatasync(fd) == 0;
#endif
}

void sync_dir(const fs::path& dir) {
#ifndef _WIN32
    if (dir.empty()) return;
    const int d = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d >= 0) { (void)::fsync(d); ::close(d); }
#else
    (void)dir;   // NTFS日志自行更名
#endif
}

int open_new(const fs::path& p, unsigned mode, bool append) {
#ifdef _WIN32
    (void)mode;
    int fd = -1;
    const int flags = _O_WRONLY | _O_CREAT | _O_BINARY | _O_NOINHERIT
                    | (append ? _O_APPEND : _O_EXCL);
    (void)::_wsopen_s(&fd, p.wstring().c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return fd;
#else
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_EXCL);
    int fd;
    do { fd = ::open(p.c_str(), flags, static_cast<mode_t>(mode)); }
    while (fd < 0 && errno == EINTR);
    return fd;
#endif
}

void close_fd(int fd) {
#ifdef _WIN32
    ::_close(fd);
#else
    ::close(fd);
#endif
}

bool publish(const fs::path& tmp, const fs::path& target) {
#ifdef _WIN32
    // Windows不会替换另一个进程在没有FILE_SHARE_DELETE
    // 的情况下打开的文件，并且普通阅读器(ifstream)在没有它的情况下打开。
    // 他们将其保留微秒，因此 重试 会短暂地写入，而不是丢弃写入（测量：
    // 24 个并发 mcp.json 编辑，2 个在没有此操作的情况下丢失）。
    for (int attempt = 0;; ++attempt) {
        if (::MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        const DWORD e = ::GetLastError();
        if ((e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED) || attempt >= 50)
            return false;
        ::Sleep(static_cast<DWORD>(1 + attempt));
    }
#else
    return ::rename(tmp.c_str(), target.c_str()) == 0;
#endif
}

}  // 命名空间结束。

namespace detail {
std::optional<maya::platform::native_file_lock> acquire_cross(const std::string& t) {
    auto got = maya::platform::acquire_within<maya::platform::native_file_lock>(t, kSharedFileWait);
    if (got && *got) return std::move(**got);
    if (first_failure_for(t)) {
        if (got)
            AGT_LOG(Persist, Warn, "shared_file.busy",
                    "file={} another agentty held its lock past {} ms; going on "
                    "with the in-process guarantee only",
                    t, kSharedFileWait.count());
        else
            AGT_LOG(Persist, Warn, "shared_file.degraded",
                    "file={} err={} — in-process guarantee only; a second "
                    "agentty instance can interleave on this file",
                    t, got.error().what);
    }
    return std::nullopt;
}
}  // namespace detail

FileLocks::FileLocks(std::vector<fs::path> files) {
    std::vector<std::string> keys;
    for (auto& f : files)
        if (!f.empty()) keys.push_back(f.lexically_normal().string());
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (const auto& k : keys) {
        if (auto l = detail::acquire_cross(k)) held_.push_back(std::move(*l));
        else all_ = false;
    }
}

bool write_atomic(Io, const fs::path& target, std::string_view bytes, WriteOptions o) {
    std::error_code ec;
    if (const auto dir = target.parent_path(); !dir.empty()) fs::create_directories(dir, ec);

    unsigned mode = o.mode;
#ifndef _WIN32
    if (o.keep_mode) {
        struct stat st{};
        if (::stat(target.c_str(), &st) == 0) mode = st.st_mode & 07777;
    }
#endif

    const fs::path tmp = temp_for(target);
    const int fd = open_new(tmp, mode, /*附加=*/false);
    if (fd < 0) return false;
#ifndef _WIN32
    // O_CREAT的模式被umask屏蔽；保留的模式必须准确。
    if (o.keep_mode) (void)::fchmod(fd, static_cast<mode_t>(mode));
#endif

    bool ok = write_all(fd, bytes.data(), bytes.size());
    if (ok && o.durability == Durability::Durable) ok = sync_fd(fd);
    close_fd(fd);
    if (ok) ok = publish(tmp, target);
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    if (o.durability == Durability::Durable) sync_dir(target.parent_path());
    return true;
}

bool append_atomic(Io, const fs::path& target, std::string_view bytes,
                   Durability d, unsigned mode) {
    std::error_code ec;
    if (const auto dir = target.parent_path(); !dir.empty()) fs::create_directories(dir, ec);
    const int fd = open_new(target, mode, /*附加=*/true);
    if (fd < 0) return false;
    bool ok = write_all(fd, bytes.data(), bytes.size());
    if (ok && d == Durability::Durable) ok = sync_fd(fd);
    close_fd(fd);
    return ok;
}

Lane& small_files_lane() {
    static Lane l;
    return l;
}

LaneOf<lock_levels::cred_file>& cred_lane(const fs::path& file) {
    // 固定槽，通过文件名选择：没有分配，没有锁来找到一个
    // （这在其他锁 正文内部运行）。相同的文件总是映射到相同的槽，
    // 这都是正确性的需要；不同的文件几乎总是分开的，
    // 因此一个提供商的刷新很少会等待另一个提供商的刷新。
    static std::array<LaneOf<lock_levels::cred_file>, 16> lanes;
    return lanes[std::hash<std::string>{}(file.filename().string()) % lanes.size()];
}

std::optional<std::string> peek_file(const fs::path& p) { return read_file(IoAccess::grant(), p); }

namespace detail {
bool write_in_section(const std::string& target, const std::string& bytes, WriteOptions o) {
    return write_atomic(IoAccess::grant(), target, bytes, o);
}
bool append_in_section(const std::string& target, const std::string& bytes, Durability d,
                       unsigned mode) {
    return append_atomic(IoAccess::grant(), target, bytes, d, mode);
}
bool create_in_section(const std::string& target, const std::string& bytes, unsigned mode) {
    return create_exclusive(IoAccess::grant(), target, bytes, mode);
}
}  // namespace detail

bool create_exclusive(Io io, const fs::path& target, std::string_view bytes, unsigned mode) {
    std::error_code ec;
    if (const auto dir = target.parent_path(); !dir.empty()) fs::create_directories(dir, ec);
    const fs::path tmp = temp_for(target);
    const int fd = open_new(tmp, mode, /*附加=*/false);
    if (fd < 0) return false;
    bool ok = write_all(fd, bytes.data(), bytes.size()) && sync_fd(fd);
    close_fd(fd);
    if (ok) {
#ifdef _WIN32
        // 如果名称被占用，则没有 REPLACE_EXISTING 的 MoveFileEx 会失败。
        ok = ::MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(),
                           MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = ::link(tmp.c_str(), target.c_str()) == 0;
        // FAT、一些FUSE和SMB安装座没有硬链接。
        // 回退到目标本身的独占创建：仍然是第一个-wins，并且只有中间
        // -write（一个撕裂的文件）崩溃是可能的，而不是两个获胜者。
        if (!ok && (errno == EPERM || errno == ENOTSUP || errno == EOPNOTSUPP
                    || errno == ENOSYS || errno == EMLINK)) {
            const int t = open_new(target, mode, /*附加=*/false);
            if (t >= 0) {
                ok = write_all(t, bytes.data(), bytes.size()) && sync_fd(t);
                close_fd(t);
                if (!ok) fs::remove(target, ec);
            }
        }
#endif
    }
    fs::remove(tmp, ec);
    if (ok) sync_dir(target.parent_path());
    (void)io;
    return ok;
}

std::optional<std::uint64_t> append_record(Io, const fs::path& target, std::string_view bytes,
                                           Durability d) {
    std::error_code ec;
    if (const auto dir = target.parent_path(); !dir.empty()) fs::create_directories(dir, ec);
    const int fd = open_new(target, 0644, /*附加=*/true);
    if (fd < 0) return std::nullopt;
    bool ok = write_all(fd, bytes.data(), bytes.size());
    if (ok && d == Durability::Durable) ok = sync_fd(fd);
#ifdef _WIN32
    const long long end = ok ? ::_lseeki64(fd, 0, SEEK_CUR) : -1;
#else
    const off_t end = ok ? ::lseek(fd, 0, SEEK_CUR) : -1;
#endif
    close_fd(fd);
    if (!ok || end < static_cast<long long>(bytes.size())) return std::nullopt;
    return static_cast<std::uint64_t>(end) - bytes.size();
}

std::optional<std::string> read_file(Io, const fs::path& p) {
#ifdef _WIN32
    FILE* f = ::_wfopen(p.wstring().c_str(), L"rb");
#else
    FILE* f = std::fopen(p.c_str(), "rb");
#endif
    if (!f) {
        if (errno != ENOENT)
            AGT_LOG(Persist, Debug, "shared_file.read", "file={} err={}",
                    p.string(), std::strerror(errno));
        return std::nullopt;
    }
    std::string out;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) return std::nullopt;
    return out;
}

}  // namespace agentty::persistence
