// cmd::sniff_image_path — 此粘贴路径是图像文件吗？
//
// 放置的文件作为其在括号内的粘贴中的路径到达。决定是否命名图像
// 意味着 stat-ing 并读取文件，因此它在 工作线程
// 上运行并回复 ImagePathSniffed。
// 状态更新函数 会附加图像，或者当路径不是一个时将文本粘贴为文本。

#include "agentty/runtime/app/cmd_factory.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "agentty/util/home_dir.hpp"

namespace agentty::app::detail {
const char* detect_image_media_type(std::string_view bytes) noexcept;
}

namespace agentty::app::cmd {

namespace {

namespace fs = std::filesystem;
using detail::detect_image_media_type;

// 如果粘贴看起来像识别的图像文件的单个-line路径，则返回(路径，
// media_type)。没有匹配项时路径为空。
struct ImagePasteResult {
    std::string  path;
    const char*  media_type = nullptr;
    std::string  body;       // 原始图像字节
};

// 规范化粘贴的候选路径：
//   – 将前导 `~/` 扩展为 $HOME（文件管理器/shell 会发出此信息） –
//   unescape `\ ` / `\(` / `\)` / `\'` 等（在
//     macOS 上拖动 -drop 和几个 Linux 文件管理器反斜杠-escape
//     每个 shell-special 字符，以便路径可以重新-pasted逐字进入外壳）
//   – 如果 CRLF 终端 滑过，则掉落 CR
std::string normalize_path_candidate(std::string_view in) {
    // 修剪尾随 CR。
    while (!in.empty() && (in.back() == '\r' || in.back() == ' ')) in.remove_suffix(1);
    std::string s;
    s.reserve(in.size());
    if (in.size() >= 2 && in[0] == '~' && in[1] == '/') {
        // 统一主根（$HOME在POSIX/MSYS2上，
        // $USERPROFILE在本机Windows上），因此删除的`~/…`路径在每个平台上扩展。
        const fs::path home = agentty::util::home_dir();
        if (!home.empty()) {
            s.append(home.string());
            in.remove_prefix(1);  // 保留'/'
        }
    }
#ifdef _WIN32
    // 在 Windows 上，反斜杠是 PATH SEPARATOR，而不是 shell 转义：
    // 被删除的本机路径`C:\Users\foo\bar`必须逐字保留。
    // 此处剥离`\`会将其损坏为`C:Usersfoobar`。将剩余部分全部
    // 视为 -is（Windows 上的 drag/drop 源代码不加壳
    // -escape）。
    s.append(in);
#else
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            // Shell-style转义(
            // macOS/Linux文件管理器反斜杠每个shell-special字符)。删除反斜杠，保留字符。
            s.push_back(in[i + 1]);
            ++i;
            continue;
        }
        s.push_back(in[i]);
    }
#endif
    return s;
}

ImagePasteResult sniff_image_paste(std::string_view text) {
    ImagePasteResult r;
    // 修剪前导/尾随空白(包括CR)。
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    auto trimmed = text.substr(a, b - a);
    if (trimmed.empty()) return r;
    // 必须是单行——嵌入的换行符暗示粘贴的散文
    // 而不是路径。
    if (trimmed.find('\n') != std::string_view::npos) return r;
    if (trimmed.size() > 4096) return r;  // 理智上限

    // 去除周围的引号（
    // 自动将-and-drop拖到macOS/GNOME引号路径上）。
    if (trimmed.size() >= 2
        && (trimmed.front() == '\'' || trimmed.front() == '"')
        && trimmed.back() == trimmed.front()) {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    // file:// URI 在某些桌面上位于此处。接受两个或三个前导
    // 斜杠（file:// 与 file:///）。
    constexpr std::string_view kFileUri = "file://";
    if (trimmed.size() > kFileUri.size()
        && trimmed.substr(0, kFileUri.size()) == kFileUri) {
        trimmed.remove_prefix(kFileUri.size());
        // 一些发射器使用 file:/// 作为绝对路径。
        // 折叠多余的斜杠，以便结果恰好以 1 '/' 开头。
        if (!trimmed.empty() && trimmed.front() == '/') {
            // 已经绝对好了
        }
    }

    auto candidate = normalize_path_candidate(trimmed);
    if (candidate.empty()) return r;

    fs::path p{candidate};
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) return r;
    // 嗅探第一个 16 字节 的魔法前缀。
    std::ifstream in(p, std::ios::binary);
    if (!in) return r;
    char buf[16]{};
    in.read(buf, sizeof(buf));
    auto got = static_cast<std::size_t>(in.gcount());
    auto* mt = detect_image_media_type(std::string_view{buf, got});
    if (!mt) return r;
    // 吸满字节。 8 MiB 上限 — Anthropic 的 per-image
    // 限制为 5 MB，base64 扩展增加了约 33%，因此磁盘上任何大于约
    // 6 MB 的内容都会导致 server-side 失败。
    auto sz = fs::file_size(p, ec);
    if (ec || sz > 8 * 1024 * 1024) return r;
    in.clear();
    in.seekg(0);
    std::string body(static_cast<std::size_t>(sz), '\0');
    in.read(body.data(), static_cast<std::streamsize>(sz));
    if (in.gcount() != static_cast<std::streamsize>(sz)) return r;

    r.path       = p.string();
    r.media_type = mt;
    r.body       = std::move(body);
    return r;
}

}  // 命名空间结束。

Cmd sniff_image_path(std::string text) {
    return Cmd::task(
        [](maya::Sink<Msg> out, std::stop_token, std::string text) {
            ImagePathSniffed r;
            try {
                auto img = sniff_image_paste(text);
                if (!img.path.empty() && img.media_type) {
                    r.path       = std::move(img.path);
                    r.media_type = img.media_type;
                    r.body       = std::move(img.body);
                }
            } catch (...) {
                r = ImagePathSniffed{};
            }
            r.text = std::move(text);
            out.send(Msg{msg::ComposerMsg{std::move(r)}});
        },
        std::move(text));
}

}  // namespace agentty::app::cmd
