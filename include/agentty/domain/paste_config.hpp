#pragma once
// agentty::paste_cfg-Ctrl+V和`agentty paste`如何读取剪贴板。
//
// 读数本身是richpaste (third_party/richpaste)。
// agentty 拥有选择权：粘贴可能采用哪些路线、粘贴可能有多大、粘贴的文件会变成什么。
// 它们是设置行（粘贴。*），因此它们位于 settings.json 中，
// 显示在“设置”→“粘贴”中，采用 AGENTTY_PASTE_* 覆盖并与
// `agentty config get|set` 一起使用。
//
// 纯粹：没有IO，没有全局变量。 io/clipboard.cpp 将一个 Config 变成 richpaste 的
// rp::Config。

#include <cstdint>
#include <string>

namespace agentty::paste_cfg {

// 接受的最大焊膏，在 MB。 Anthropic 拍摄最多 5 个 MB 的图像，
// 但复制的文件可能更大，并且是 已保存，不会发送。
inline constexpr int kMaxMbMin     = 1;
inline constexpr int kMaxMbMax     = 1024;
inline constexpr int kMaxMbDefault = 32;

// 最多这么大(KB)的粘贴文本文件作为其文本进入；较大的是 已保存，
// 它们的路径是 带类型的。
inline constexpr int kInlineKbMin     = 0;
inline constexpr int kInlineKbMax     = 4096;
inline constexpr int kInlineKbDefault = 256;

// paste.terminal:agentty如何向终端请求剪贴板。
inline constexpr std::string_view kTerminalModes = "auto|osc5522|osc52|off";

struct Config {
    // 通过ssh，通过转发代理（笔记本电脑上的`agentty paste setup`）
    // 读取笔记本电脑的剪贴板。
    bool forwards = true;

    // 读取本机自身的剪贴板(Wayland、X11、macOS、Windows)。
    bool display = true;

    // 通过ssh，回退到SERVER自己的剪贴板。默认关闭：
    // 这不是您复制到的剪贴板。
    bool server_display = false;

    // 当没有其他答案时如何询问终端(OSC 5522/OSC 52)。
    // "auto"询问终端支持什么； "osc5522" / "osc52" 强制一
    // for terminals that misreport; "off" never asks.
    std::string terminal = "auto";

    // 将粘贴文本中的`richpaste:1:`行转回到图像或文件中。
    bool decode_text = true;

    // 接受的最大焊膏（MB)。
    int max_mb = kMaxMbDefault;

    // 复制到该大小（KB)的文本文件被粘贴为其文本。 0 = 始终为 保存。
    int inline_kb = kInlineKbDefault;

    // 其中粘贴的文件是已保存。空 = <state dir>/粘贴（$AGENTTY_HOME
    // /paste/pasted 或 ~/.agentty/paste/pasted）。
    std::string save_dir;

    // Ask仅此代理套接字（一个中继，一个测试）。空=找到它们。
    std::string socket;

    bool operator==(const Config&) const = default;
};

} // namespace agentty::paste_cfg
