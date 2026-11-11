// 磁盘作为mcp-cpp的FileSystem,加上read/write的帮助者agentty自己的代码使用.
// agentty的工具层触摸文件字节的其中一个地方.
#include "agentty/tool/util/disk_files.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // read_file / write_file 数据

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  include <share.h>
#  include <process.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#endif

namespace agentty::tools::util {

using ::mcp::tools::DirEntry;
using ::mcp::tools::FileStat;
using ::mcp::tools::WalkStep;

namespace {
// 将最常见的文件系统错误值转换为模型可以操作的句子。 Raw `strerror`
// 读作 "Permission denied" /
// "No such file or directory" - 人类可以,
// 但是... LLM 更好地适应较长的表格(
// "you don't have write permission to X" 当它决定是否 重
// 试 以不同的路径或表面显示人类的询问。 path/operation 上下文。
std::string explain_errno(int e) {
    switch (e) {
        case EACCES:        return "permission denied";
        case EPERM:         return "operation not permitted (privileged op)";
        case ENOENT:        return "path not found";
        case ENOTDIR:       return "expected a directory but found a file";
        case EISDIR:        return "expected a file but found a directory";
        case ENOSPC:        return "out of disk space";
        case EROFS:         return "filesystem is read-only";
        case EMFILE:
        case ENFILE:        return "too many open files (process FD limit hit)";
        case ELOOP:         return "symlink loop";
        case ENAMETOOLONG:  return "path is too long";
        case EBUSY:         return "file is busy / locked by another process";
#ifdef EDQUOT
        case EDQUOT:        return "disk quota exceeded";
#endif
        default:            return std::strerror(e);
    }
}

}  // 命名空间结束。

std::string read_file(const fs::path& p) {
    // 大小 -then-read : 避免出现“流”。 str() 链条,
    // 双倍 -allocates 一份统计,一份统计,
    // 一本读数 回退 以排水为主,如果体积不明( e.g ./proc,
    // 管道)——这些产品 file_size ⁇ 或掷.
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
#ifdef _WIN32
    // 原生 Win32 路径 : CreateFileW 与
    // FILE_FLAG_SEQUENTIAL_SCAN 告诉 缓存 管理器将积极读取(
    // 使用后, 页面被丢弃)。 将“ / grep / 编辑” 改为“ / 添加更多
    // 预选窗口 ” 高频路径 ——一条线性通道,没有求求——这在物质上比"求"要快.
    // CRT ReadFile 说 std::ifstream 低调到,
    // 还避免了 CRT 单位 -char 缓冲区 翻译大文件的间接费用。
    if (!ec && sz > 0) {
        HANDLE h = ::CreateFileW(p.wstring().c_str(),
                                 GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                 nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            std::string out;
            out.resize(sz);
            char*  buf   = out.data();
            size_t total = 0;
            const size_t want = static_cast<size_t>(sz);
            while (total < want) {
                DWORD chunk = static_cast<DWORD>(
                    std::min<size_t>(want - total, 1u << 20));   // 1 MiB 读作
                DWORD got = 0;
                if (!::ReadFile(h, buf + total, chunk, &got, nullptr) || got == 0)
                    break;
                total += got;
            }
            ::CloseHandle(h);
            if (total != want) out.resize(total);
            return out;
        }
        // 掉进到下游 CreateFileW 失败( 径:
        // 锁定 /) 权限-denied 文档中 CRT
        // 可能还 重试 更能优雅地贬低别人,而不是拒绝别人。
    }
#endif
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    if (!ec && sz > 0) {
        std::string out;
        out.resize(sz);
        ifs.read(out.data(), static_cast<std::streamsize>(sz));
        if (auto got = ifs.gcount(); static_cast<uintmax_t>(got) != sz)
            out.resize(static_cast<size_t>(got));
        return out;
    }
    std::ostringstream oss; oss << ifs.rdbuf();
    return oss.str();
}

std::string write_file(const fs::path& p, std::string_view content) {
    // 跟着一个符号链接到它指的 AT 在写东西之前
    //
    // 下面的出版步骤是 rename() ,它取代它给定的名称 --
    // -- 对于一个连锁关系,它意味着替换 LINK。没有这个,
    // 通过一个连锁编辑一次就默默地做了三个错误的事情: 链接
    // 成为常规文件, 真正的目标保留了旧内容, 工具报告成功 diff 变化,
    // 进入了全新的 inode 没有人在看。 Dotfiles
    // 仓库 生成 -file 符号链接( Q)
    // compile_commands.json 立即打这个。
    //
    // 封存不受影响 : is_within_workspace
    // 之前的卡尼卡利克斯 我们曾经被叫,所以 链接 指向外边
    // 工作区 根已经被拒绝。 溶解在这里只能降落在它内部。
    fs::path real = p;
    {
        std::error_code ec;
        if (fs::is_symlink(real, ec) && !ec) {
            auto resolved = fs::weakly_canonical(real, ec);
            if (!ec && !resolved.empty()) real = std::move(resolved);
        }
    }

    auto parent = real.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) return "failed to create directory '" + parent.string() + "': " + ec.message();
    }

    // 原子写法:写到兄弟临时文件,fsync,重命名于目标.
    // 崩溃或电源 -loss 中点 -write 保留原始文件完整;
    // 仅完整 -written , fsync'd copy 永远
    // 可见目标路径。临时值必须在同一目录中存在,所以重命名是一个单一
    // 的文件系统操作——交叉 -device 重命名归为非
    // -atomic ⁇ .
    //
    // 临时名称之所以独特,是因为创建它时只(O_EXCL)和重试,
    // 如果存在,则使用新的后缀,所以没有对应
    // 的写手共享。后缀混合了pid,
    // 一个堆栈地址和尝试,这足以使碰撞变得罕见.
#ifdef _WIN32
    const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    const auto salt = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&pid));
    fs::path tmp;
    auto tmp_name = [&](int attempt) {
        fs::path t = real;
        t += fs::path(".agentty-tmp-" + std::to_string(pid) + "-"
                      + std::to_string((salt >> 4) ^ (static_cast<std::uint64_t>(attempt) * 0x9e3779b97f4a7c15ULL)));
        return t;
    };

    // 保留POSIX上的现有模式,这样重命名不会退缩转式.
#ifndef _WIN32
    mode_t target_mode = 0644;
    bool   had_mode    = false;
    {
        struct stat st{};
        if (::stat(real.c_str(), &st) == 0) {
            target_mode = st.st_mode & 07777;
            had_mode    = true;
        }
    }
#endif

    // 降入POSIX/Win32 fd,这样我们就可以在重命名前进行fsync.
    // ofstream::flush 仅空出 libstdc++ 流泡
    // 进入 OS ——权力 -loss 坠机仍然会失去 字节 ,
    // 并在某些 FUSE 和网络文件系统,数据在fsync完成之前,
    // 在下一个打开时是无法读取的.
#ifdef _WIN32
    // 宽度 -char 变体 因此 Unicode 路径( 代码路径) ( e.g。
    // 在非 -ASCII %USERPROFILE 百分比)圆 -trip 正确,我记得 `_sopen_s`
    // 需要一个 ANSI/MBCS 静静地破坏多面性的道路 -byte 某些区域上的序列
    // MinGW ucrt配置图.
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        tmp = tmp_name(attempt);
        auto ws = tmp.wstring();
        if (::_wsopen_s(&fd, ws.c_str(),
                        _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                        _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) {
            fd = -1;
            if (errno != EEXIST) break;
        }
    }
    if (fd < 0)
        return "cannot open '" + p.string() + "' for writing";
#else
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        tmp = tmp_name(attempt);
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0)
        return std::string("cannot open '") + p.string() + "' for writing: "
             + explain_errno(errno);
#endif

    auto cleanup_tmp = [&] {
        std::error_code ec;
        fs::remove(tmp, ec);
    };

    const char* data = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
#ifdef _WIN32
        int n2 = _write(fd, data, static_cast<unsigned>(
            remaining > 0x7fffffff ? 0x7fffffff : remaining));
#else
        ssize_t n2 = ::write(fd, data, remaining);
        if (n2 < 0 && errno == EINTR) continue;
#endif
        if (n2 <= 0) {
            std::string err = std::string("write to '") + p.string()
                + "' failed: " + explain_errno(errno);
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
            cleanup_tmp();
            return err;
        }
        data += n2;
        remaining -= static_cast<size_t>(n2);
    }

    // fsync time:数据在重命名发布之前是耐用性的.
#ifdef _WIN32
    (void)_commit(fd);
    _close(fd);
#else
    if (had_mode) (void)::fchmod(fd, target_mode);
    // Linux/FreeBSD 拥有 fdatasync(略快的斯基
    // 普斯元数据冲洗); macOS 只有 fsync。 我们需要的
    // 耐久性保证是相同的。 —— 文件内容在重命名发布前承诺在磁盘上
    // —— 因此, 苹果上返回 fsyncm 最多花费一个元数据块。
#if defined(__APPLE__)
    (void)::fsync(fd);
#else
    (void)::fdatasync(fd);
#endif
    ::close(fd);
#endif

    // 原子出版. Windows MoveFileExW 与 REPLACE_EXISTING 在一起,
    // 是 NTFS上的原子(单 MFT更新); WRITE_THROUGH 冲洗目录条目.
    // POSIX rename() 原子值,按光谱计,当 src/dst 都一样 FS.
#ifdef _WIN32
    auto tmp_w = tmp.wstring();
    auto dst_w = real.wstring();
    if (!::MoveFileExW(tmp_w.c_str(), dst_w.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD e = ::GetLastError();
        cleanup_tmp();
        return "atomic rename to '" + p.string() + "' failed (GLE=" + std::to_string(e) + ")";
    }
#else
    if (::rename(tmp.c_str(), real.c_str()) != 0) {
        std::string err = std::string("atomic rename to '") + p.string()
            + "' failed: " + explain_errno(errno);
        cleanup_tmp();
        return err;
    }
    // fsync 父目录,所以重命名本身能幸存断电.
    if (!parent.empty()) {
        int dfd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
    }
#endif
    return {};
}

namespace {

FileStat stat_of(const fs::directory_entry& e, int depth, DirEntry* out) {
    FileStat s;
    std::error_code ec;
    s.symlink   = e.is_symlink(ec);
    s.directory = e.is_directory(ec);
    s.regular   = e.is_regular_file(ec);
    s.exists    = true;
    if (s.regular) { auto z = e.file_size(ec); if (!ec) s.size = z; }
    auto t = e.last_write_time(ec);
    if (!ec) s.mtime = t;
    if (out) {
        out->path = e.path(); out->regular = s.regular; out->directory = s.directory;
        out->symlink = s.symlink; out->size = s.size; out->mtime = s.mtime; out->depth = depth;
    }
    return s;
}

}  // 命名空间结束。

FileStat DiskFiles::stat(const fs::path& p) const {
    FileStat s;
    std::error_code ec;
    const auto st = fs::status(p, ec);
    if (ec || !fs::exists(st)) {
        // 连锁锁仍然在摇摆 "is" 某件能列出它的工具。
        const auto lst = fs::symlink_status(p, ec);
        if (!ec && fs::is_symlink(lst)) { s.exists = true; s.symlink = true; }
        return s;
    }
    s.exists    = true;
    s.regular   = fs::is_regular_file(st);
    s.directory = fs::is_directory(st);
    s.symlink   = fs::is_symlink(fs::symlink_status(p, ec));
    if (s.regular) { auto z = fs::file_size(p, ec); if (!ec) s.size = z; }
    auto t = fs::last_write_time(p, ec);
    if (!ec) s.mtime = t;
    return s;
}

fs::path DiskFiles::canonical(const fs::path& p, std::error_code& ec) const {
    return fs::weakly_canonical(p, ec);
}

fs::path DiskFiles::read_symlink(const fs::path& p, std::error_code& ec) const {
    return fs::read_symlink(p, ec);
}

fs::path DiskFiles::cwd() const {
    std::error_code ec;
    auto c = fs::current_path(ec);
    return ec ? fs::path{} : c;
}

fs::path DiskFiles::scratch_dir() const {
    std::error_code ec;
    auto t = fs::temp_directory_path(ec);
    return ec ? fs::path{} : t;
}

std::optional<std::string> DiskFiles::read(const fs::path& p) const {
    std::error_code ec;
    if (!fs::exists(p, ec) || ec) return std::nullopt;
    {
        std::ifstream probe(p, std::ios::binary);
        if (!probe) return std::nullopt;
    }
    return read_file(p);
}

std::optional<std::string> DiskFiles::read_prefix(const fs::path& p, std::size_t n) const {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return std::nullopt;
    std::string out(n, '\0');
    ifs.read(out.data(), static_cast<std::streamsize>(n));
    out.resize(static_cast<std::size_t>(ifs.gcount()));
    return out;
}

std::string DiskFiles::write_atomic(const fs::path& p, std::string_view content) {
    return write_file(p, content);
}

std::vector<DirEntry> DiskFiles::list(const fs::path& dir, std::error_code& ec) const {
    std::vector<DirEntry> out;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        DirEntry d;
        stat_of(*it, 0, &d);
        out.push_back(std::move(d));
    }
    return out;
}

bool DiskFiles::walk(const fs::path& root,
                            const std::function<WalkStep(const DirEntry&)>& visit) const {
    std::error_code ec;
    // 文件根是普通文件中的1的行. recursive_directory_iterator
    // 在ENOTDIR上失败,它默默地使内建的grep(唯一一个在没有
    // rigrep的主机上——一个股票Mac)报告"No matches"为
    // `grep path=src/foo.cpp`.
    if (fs::directory_entry self(root, ec); !ec && !self.is_directory(ec)) {
        if (ec || !self.exists(ec)) return false;
        DirEntry d;
        stat_of(self, 0, &d);
        (void)visit(d);
        return true;
    }
    ec.clear();
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return false;
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        DirEntry d;
        stat_of(*it, it.depth(), &d);
        // 永不通过一个连锁目录:一个循环的链接永不结束.
        if (d.directory && d.symlink) it.disable_recursion_pending();
        switch (visit(d)) {
            case WalkStep::next:     break;
            case WalkStep::skip_dir: if (d.directory) it.disable_recursion_pending(); break;
            case WalkStep::stop:     return true;
        }
    }
    return true;
}

bool DiskFiles::create_directories(const fs::path& p, std::error_code& ec) {
    return fs::create_directories(p, ec);
}

bool DiskFiles::remove(const fs::path& p, std::error_code& ec) {
    return fs::remove(p, ec);
}

std::uintmax_t DiskFiles::remove_all(const fs::path& p, std::error_code& ec) {
    return fs::remove_all(p, ec);
}

void DiskFiles::rename(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::rename(from, to, ec);
}

void DiskFiles::copy(const fs::path& from, const fs::path& to, std::error_code& ec) {
    // 链接 复制为 链接 永远不要跟随他们指向什么
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
}

}  // namespace agentty::tools::util
