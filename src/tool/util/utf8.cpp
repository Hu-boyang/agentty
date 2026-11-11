#include "agentty/tool/util/utf8.hpp"

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace agentty::tools::util {

bool is_valid_utf8(std::string_view s) noexcept {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { ++i; continue; }
        int extra; unsigned char mask; uint32_t min_cp;
        if      ((c & 0xE0) == 0xC0) { extra = 1; mask = 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; mask = 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; mask = 0x07; min_cp = 0x10000; }
        else return false;
        if (i + (size_t)extra >= s.size()) return false;
        uint32_t cp = c & mask;
        for (int k = 1; k <= extra; ++k) {
            unsigned char d = (unsigned char)s[i + (size_t)k];
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += (size_t)extra + 1;
    }
    return true;
}

std::string sanitize_utf8(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    auto repl = [&]{ out.append("\xEF\xBF\xBD"); };
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) { out.push_back((char)c); ++i; continue; }
        int extra; unsigned char mask; uint32_t min_cp;
        if      ((c & 0xE0) == 0xC0) { extra = 1; mask = 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; mask = 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; mask = 0x07; min_cp = 0x10000; }
        else { repl(); ++i; continue; }
        if (i + (size_t)extra >= in.size()) { repl(); ++i; continue; }
        uint32_t cp = c & mask;
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char d = (unsigned char)in[i + (size_t)k];
            if ((d & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok || cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            repl(); ++i; continue;
        }
        out.append(in.data() + i, (size_t)(extra + 1));
        i += (size_t)extra + 1;
    }
    return out;
}

#ifdef _WIN32
// 转码自 a Windows 密码页( Q) OEM 控制台输出 / CP1252
// / 等)改为 UTF -8号从宽处 -char 键。 返回空的任意 API 失败,
// 所以呼叫者可以返回原始 字节 + 擦拭路径。
static std::string windows_cp_to_utf8(std::string_view s, UINT cp) {
    if (s.empty()) return {};
    int wlen = ::MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w((size_t)wlen, L'\0');
    ::MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), wlen);
    int u8 = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8 <= 0) return {};
    std::string out((size_t)u8, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, out.data(), u8, nullptr, nullptr);
    return out;
}
#endif

std::size_t safe_utf8_cut(std::string_view s, std::size_t max_bytes) noexcept {
    if (s.size() <= max_bytes) return s.size();
    // 我们要最大的 `cut <= max_bytes` 以 s[0,
    // 切]结尾 UTF -8码 -point 边界。这意味着 s[切
    // (第一个被排除) 字节 )不得 继续 字节 如果是这样,
    // 它的序列的领先者会活在[0,切],我们就会把它分开.
    //
    // 开始于 max_bytes 继续走回去,而我们是续作 字节 (0x80.0xBF
    // ——上位数 10xxxx×). 以有效方式 UTF -8,最多3个后退(4
    // 个) -byte 序列最宽。循环后, s[cut] 要么已经过去 -the-end ,
    // 线索 字节 ,或 ,或 ASCII - 所有安全切点
    std::size_t cut = max_bytes;
    while (cut > 0 && cut < s.size() && ((unsigned char)s[cut] & 0xC0) == 0x80)
        --cut;
    return cut;
}

std::string to_valid_utf8(std::string s) {
    if (is_valid_utf8(s)) return s;
#ifdef _WIN32
    if (UINT cp = ::GetConsoleOutputCP(); cp != 0 && cp != CP_UTF8) {
        auto converted = windows_cp_to_utf8(s, cp);
        if (!converted.empty() && is_valid_utf8(converted)) return converted;
    }
    {
        auto converted = windows_cp_to_utf8(s, CP_ACP);
        if (!converted.empty() && is_valid_utf8(converted)) return converted;
    }
#endif
    return sanitize_utf8(s);
}

std::string strip_terminal_controls(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    // 字节 减 减: `out` 中标 当前 (未终止) 线路开始。
    // \r 倒转到它;\ n 将它通过刚刚写的新线。
    std::size_t line_start = 0;

    for (std::size_t i = 0; i < in.size(); ) {
        const unsigned char b = static_cast<unsigned char>(in[i]);

        if (b == 0x1b) {                               // ESC——分类
            if (i + 1 >= in.size()) break;             // 端端拖动 ESC: 滴
            const unsigned char next = static_cast<unsigned char>(in[i + 1]);
            if (next == '[') {                         // CSI... 最终 0x40-0x7E
                std::size_t j = i + 2;
                bool complete = false;
                while (j < in.size()) {
                    const unsigned char c = static_cast<unsigned char>(in[j++]);
                    if (c >= 0x40 && c <= 0x7e) { complete = true; break; }
                }
                if (!complete) break;                  // 切换中间 -CSI : 丢弃 末尾
                i = j;
                continue;
            }
            if (next == ']' || next == 'P' || next == '^'
                || next == '_' || next == 'X') {       // OSC/DCS/PM/APC/SOS
                std::size_t j = i + 2;
                bool complete = false;
                // 扫描后就把垃圾都给盖上 缓冲区 满 二进制程序
                // 无法使一个未终止 OSC 吞下兆字节。
                const std::size_t cap = std::min(in.size(), j + 8192);
                while (j < cap) {
                    const unsigned char c = static_cast<unsigned char>(in[j]);
                    if (c == 0x07) { ++j; complete = true; break; }
                    if (c == 0x1b && j + 1 < in.size() && in[j + 1] == '\\') {
                        j += 2; complete = true; break;
                    }
                    ++j;
                }
                if (!complete && j >= in.size()) break; // 切换中间 -string : 丢弃 末尾
                i = j;                                  // 完成,或盖盖击打
                continue;
            }
            i += 2;                                    // 双 -byte ESC 双对
            continue;
        }

        if (b == '\r') {                               // CR:覆盖语义
            if (i + 1 < in.size() && in[i + 1] == '\n') {
                out.push_back('\n');                   // \r\n → \n
                line_start = out.size();
                i += 2;
                continue;
            }
            if (i + 1 >= in.size()) { ++i; continue; } // 尾端\r: no-op
            out.resize(line_start);                    // 进程-bar 倒转
            ++i;
            continue;
        }

        if (b == '\n') {
            out.push_back('\n');
            line_start = out.size();
            ++i;
            continue;
        }

        if (b == '\b') {                               // BS: 擦除前置代码点
            std::size_t p = out.size();
            while (p > line_start
                   && (static_cast<unsigned char>(out[p - 1]) & 0xC0) == 0x80)
                --p;                                   // 跳过续作
            if (p > line_start) --p;                   // /ASCII 字节 / 领导者
            out.resize(p);
            ++i;
            continue;
        }

        if (b < 0x20 && b != '\t') { ++i; continue; }  // 其它 C0: 下降
        if (b == 0x7f) { ++i; continue; }              // DEL: drop

        out.push_back(in[i]);
        ++i;
    }
    return out;
}

} // namespace agentty::tools::util
