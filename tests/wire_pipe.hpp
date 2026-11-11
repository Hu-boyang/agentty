// tests/wire_pipe.hpp - 一个真实的 maya::Writer 过
// 一个真实 OS 烟斗,用于 传输 测试 渲染 帧 检查准确 字节 撞到的车 "tty".
//
//     auto [writer, rfd] = wire_pipe::make_writer();
//     ... 渲染 a 帧 透过作家...
//     std::string 字节 = wire_pipe::drain (rfd); // 永远不设块
//     ::close(rfd);
//
// POSIX : 管道(2),两端 O_NONBLOCK ; 排水量读取到 EAGAIN.
// Windows : 匿名者 CRT 带有大管子 缓冲区 (
// WriteFile 在一个匿名的管道块 当满,所以 缓冲区 一定有
// 整个 帧 ——每一次试验后排水量 帧 ); 排水层问
// PeekNamedPipe 排队和读得准确,所以它从来不会在空管上阻塞。
//
// 写端在两个平台上对生命周期进程保持开放(
// Writer不拥有它的手柄),匹配这些测试的行为.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <maya/platform/io.hpp>
#include <maya/terminal/writer.hpp>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace wire_pipe {

#if defined(_WIN32)
// 比任何单人都大 帧 编号 传输 测试( 完全) -screen
// 宽毛线重画 终端 几百块 KB).
inline constexpr unsigned kPipeBytes = 16u << 20;
#endif

[[nodiscard]] inline std::pair<maya::Writer, int> make_writer() {
    int fds[2];
#if defined(_WIN32)
    if (::_pipe(fds, kPipeBytes, _O_BINARY | _O_NOINHERIT) != 0) { std::perror("_pipe"); std::abort(); }
    const auto h = reinterpret_cast<maya::platform::NativeHandle>(::_get_osfhandle(fds[1]));
    return {maya::Writer{h}, fds[0]};
#else
    if (::pipe(fds) != 0) { std::perror("pipe"); std::abort(); }
    ::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    return {maya::Writer{static_cast<maya::platform::NativeHandle>(fds[1])}, fds[0]};
#endif
}

// 当前在读端上队列的所有项目; 当空时返回 ""。
[[nodiscard]] inline std::string drain(int rfd) {
    std::string out;
    char buf[8192];
#if defined(_WIN32)
    const auto h = reinterpret_cast<HANDLE>(::_get_osfhandle(rfd));
    for (;;) {
        DWORD avail = 0;
        if (!::PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
            break;
        const unsigned want = avail < sizeof(buf) ? static_cast<unsigned>(avail)
                                                  : static_cast<unsigned>(sizeof(buf));
        const int n = ::_read(rfd, buf, want);
        if (n <= 0) break;
        out.append(buf, static_cast<std::size_t>(n));
    }
#else
    ssize_t n;
    while ((n = ::read(rfd, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<std::size_t>(n));
#endif
    return out;
}

}  // namespace wire_pipe
