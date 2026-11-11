#pragma once
// 系统剪贴板，通过richpaste(third_party/richpaste)。
//
// 读取:本地显示(Wayland data-control、X11、macOS粘贴板、
// Win32)，否则用户笔记本电脑上的richpaste代理通过ssh转发(
// `agentty paste setup`一旦在那里)，否则终端本身(OSC
// 5522/OSC 52，作曲家通过其驱动) maya）。作为文本复制的
// `richpaste:1:` 行在每条路线上都会解码回图像或文件。
//
// 写入：本机工具(pbcopy/wl-copy/xclip/clip.exe)，
// 由于OSC 52写入在Terminal.app和iTerm2中默认关闭。

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/paste_config.hpp"

namespace agentty {

struct ClipboardItem {
    enum class Kind : std::uint8_t { Text, Image, File };
    Kind        kind = Kind::Text;
    std::string mime;   // "image/png"，"application/pdf"，……
    std::string name;   // File 的文件名，否则为空
    std::string bytes;  // UTF-8 表示文本，否则为原始 字节
    bool        saved_path = false;  // 已保存文件的路径文本
};

// 当剪贴板读取没有返回任何内容时，作曲家会做什么。
enum class ClipboardMiss : std::uint8_t {
    None,          // 没有错过
    AskTerminal,   // 询问终端本身，通过我们自己输入，然后恢复
    Empty,         // 剪贴板为空；决赛
    TooBig,        // 经过设置→粘贴→最大尺寸；决赛
    Refused,       // 终端不会让我们读取它的剪贴板；最终，`error` 表示修复
    NoRoute,       // 每条路线出现空-handed;最后，`error` 说要做什么
};

// 其中终端读取发送给AskTerminal。
enum class TerminalAsk : std::uint8_t { Images, Text };

// 正在进行粘贴（richpaste的路线行走），作为普通数据，因此它可以在
// 状态更新函数和工作线程之间乘坐Msg。
struct ClipboardWalk {
    bool ssh = false, tmux = false;
    std::uint8_t images = 1;              // rp::Images
    std::vector<std::uint8_t> routes;     // rp::Route，依次
    std::size_t next = 0;
    struct Cause {
        std::uint8_t route = 0;           // rp::Route
        std::uint8_t code = 0;            // rp::Code
        std::string what;
    };
    std::vector<Cause> causes;
    [[nodiscard]] bool active() const noexcept { return next < routes.size(); }
};

struct ClipboardContents {
    std::vector<ClipboardItem> items;  // 复制文件，否则图像，否则文本
    std::string source;                // "wayland wayland-1"，"agent via ssh"，……
    ClipboardMiss miss = ClipboardMiss::None;
    std::string error;                 // 当项目为空时设置
    TerminalAsk ask = TerminalAsk::Text;   // AskTerminal:读取发送的内容
    ClipboardWalk walk;                    // AskTerminal:手回恢复
};

// 安装粘贴设置（设置→粘贴/settings.json/AGENTTY_PASTE_*）。
// 调用 在启动时以及每当它们发生变化时；
// 已经运行的粘贴会保留它所读取的内容。
void clipboard_configure(const paste_cfg::Config& c);
[[nodiscard]] paste_cfg::Config clipboard_config();
// 现在粘贴的图像和文件是已保存。
[[nodiscard]] std::filesystem::path clipboard_save_dir();

// 读取剪贴板，尝试从最便宜和最忠实的路由开始，除了 ssh
// 代理之外不需要设置。阻塞（套接字往返，可能通过 ssh，
// 或 tmux 询问 终端），因此 调用 来自 工作线程。
// 当下一条路线询问 终端 时，它会以 AskTerminal
// 停止：应用程序通过自己的输入进行询问，如果步行时没有带来
// 调用 resume_clipboard。
[[nodiscard]] ClipboardContents read_clipboard();
// 终端 没有回答（或没有回答）：记下原因并继续。
// `refused`：它回答否（小猫的EPERM），因此再次询问它的路线被跳过。
[[nodiscard]] ClipboardContents resume_clipboard(ClipboardWalk walk, std::string why, bool refused);
// 只有本机自带的剪贴板（没有ssh，没有终端）。
[[nodiscard]] ClipboardContents read_local_clipboard();

// 从粘贴的文本中拉出 `richpaste:1:` 装甲块。
// 按顺序返回项目，块之间的纯文本作为文本项目。
// 当文本没有装甲块时为空。
[[nodiscard]] std::vector<ClipboardItem> decode_armored(std::string_view text,
                                                        std::string* error = nullptr);
[[nodiscard]] bool has_armored(std::string_view text) noexcept;
// 确实，当文本的装甲块仍在到达时（碎片粘贴）。
[[nodiscard]] bool armored_partial(std::string_view text) noexcept;
// 迄今为止接收到的到达块的字节，及其完整大小（0，
// 而其头文件仍在途中）。当没有任何东西到达时，两者都为 0。
struct ArmoredProgress { std::uint64_t have = 0, need = 0; bool arriving = false; };
[[nodiscard]] ArmoredProgress armored_progress(std::string_view text) noexcept;

// 终端剪贴板回复（OSC 5522)以纯文本形式到达输入，
// 因为某些东西剥离了其ESC 字节。切勿将其粘贴为文本。
enum class LeakedReply : std::uint8_t { None, Partial, Complete };
[[nodiscard]] LeakedReply leaked_reply(std::string_view text) noexcept;
// 完整泄露回复携带的项目(无法读取则为空)。
[[nodiscard]] std::vector<ClipboardItem> decode_leaked_reply(std::string_view text,
                                                             std::string* error = nullptr);

// 将剪贴板项目变成作曲家可以容纳的内容：图像和文本通过，
// 文本文件变成文本，
// 任何其他文件是~/.agentty/paste/pasted下的已保存并且变成具有其路径的文本项目。
// 确实磁盘IO，所以调用它来自工作线程。
void settle_clipboard_items(std::vector<ClipboardItem>& items);

/// Write用平台工具将纯文本UTF-8文本复制到系统剪贴板。
/// 呼叫者还发送OSC 52，其仍然携带跨越ssh/tmux.的副本
/// 失败时，将 one-line 原因写入 `*error_out`。
bool write_clipboard_text(std::string_view text, std::string* error_out = nullptr);

// `agentty paste …`:richpaste的命令行。它的默认值是 agentty（re-runs
// `<this exe> paste agent|ssh-hook`，~/.agentty/paste 中的状态）。
int  clipboard_cli(std::span<const std::string> args);

// 用于ssh会话的`RemoteForward`对（远程套接字、本地端点），
// 如果需要则启动本地代理。失败时为空对，原因为 `*error`。
// 由 `agentty airgap --clipboard-relay` 使用。
struct ClipboardForward { std::string remote, local; };
[[nodiscard]] ClipboardForward clipboard_ssh_forward(std::string* error = nullptr);

} // namespace agentty
