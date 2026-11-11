#include "agentty/runtime/composer_attachment.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "agentty/io/clipboard.hpp"   // armored_progress
#include "agentty/tool/util/fs_helpers.hpp"

namespace agentty::attachment {

std::string make_placeholder(std::size_t idx) {
    // \x01ATT:N\x01 — 排版原理见头文件.
    std::string out;
    out.reserve(8);
    out.push_back(kSentinel);
    out.append("ATT:");
    out.append(std::to_string(idx));
    out.push_back(kSentinel);
    return out;
}

std::size_t placeholder_len_at(std::string_view text, std::size_t pos) noexcept {
    // 形状:\x01 A T T :
    // <digits>\x01 最小长度7(一个数字). bail早期在廉价错配上.
    if (pos >= text.size() || text[pos] != kSentinel) return 0;
    if (pos + 7 > text.size()) return 0;
    if (text[pos + 1] != 'A' || text[pos + 2] != 'T'
        || text[pos + 3] != 'T' || text[pos + 4] != ':') return 0;
    // 步行数字。
    std::size_t p = pos + 5;
    if (p >= text.size() || text[p] < '0' || text[p] > '9') return 0;
    while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
    if (p >= text.size() || text[p] != kSentinel) return 0;
    return (p + 1) - pos;
}

std::size_t placeholder_len_ending_at(std::string_view text, std::size_t pos) noexcept {
    // 公子是,我们是 字节 之后 关闭的哨; 占位符占用 [sart,
    // pos]。 向后走以找到打开的\ x01。
    if (pos == 0) return 0;
    if (text[pos - 1] != kSentinel) return 0;
    // 标志结尾为: <digits> \x01 退后, 过数字。
    if (pos < 2) return 0;
    std::size_t p = pos - 2;
    while (p > 0 && text[p] >= '0' && text[p] <= '9') --p;
    // 现在p应指第"ATT:"的':'.
    if (text[p] != ':') return 0;
    // 需要完全的前缀\x01ATT。
    if (p < 4) return 0;
    if (text[p - 1] != 'T' || text[p - 2] != 'T' || text[p - 3] != 'A'
        || text[p - 4] != kSentinel) return 0;
    std::size_t start = p - 4;
    // 确认前方形状(数字+关闭的哨兵),
    // 因为上面的后方行走已经建立了后缀.
    auto fwd = placeholder_len_at(text, start);
    if (fwd != pos - start) return 0;
    return fwd;
}

std::size_t placeholder_index(std::string_view text, std::size_t pos) noexcept {
    auto len = placeholder_len_at(text, pos);
    if (len == 0) return static_cast<std::size_t>(-1);
    // 数字活在[pos+5,pos+len-1].
    std::size_t value = 0;
    for (std::size_t p = pos + 5; p + 1 < pos + len; ++p) {
        value = value * 10 + static_cast<std::size_t>(text[p] - '0');
    }
    return value;
}

namespace {

// 单曲上的截断盖 FileRef 256 扩大
// KiB 基本上覆盖每个真实源文件,
// 同时防止一个不小心的@在建筑动画/日志/资产上淹死 提示词.
inline constexpr std::size_t kFileRefMaxBytes = 256 * 1024;

std::string render_attachment_body(const Attachment& a) {
    if (a.kind == Attachment::Kind::FileRef) {
        // 惰性 正文 加载。来电者可能有预 -filled
        // `body` (试带,贴贴) -of-a-file 以其他
        // 方式解决与“未来道路”相对应的路径。 工作区 根并捣烂.
        std::string body = a.body.bytes();
        if (body.empty() && !a.path.empty()) {
            namespace fs = std::filesystem;
            fs::path p{a.path};
            if (p.is_relative()) p = tools::util::workspace_root() / p;
            // 门通 promote_to_workspace_path
            // : 绝对路径逃脱 工作区 (模型喂我们) `/etc/passwd`
            // 作为 @ file) 必须 拒绝,而不是默默地贬低。
            // -WorkspacePath ,
            // 相对 -vs-absolute 上面的树枝让绝对的逃生通过。
            tools::util::NormalizedPath np{p.string()};
            auto wp = tools::util::promote_to_workspace_path(
                std::move(np), "@file attachment");
            if (wp) body = tools::util::read_file(*wp);
        }
        std::string out;
        out.reserve(body.size() + a.path.size() + 32);
        // 路径 头文件 保持锁定以下块来自的文件的模型
        // – 相同的图案 Cursor / Claude
        // 内置文件引用的代码使用。
        out.append("// path: ");
        out.append(a.path);
        out.push_back('\n');
        if (body.size() > kFileRefMaxBytes) {
            out.append(body.data(), kFileRefMaxBytes);
            out.append("\n[… file truncated, ");
            out.append(std::to_string(body.size() - kFileRefMaxBytes));
            out.append(" bytes elided]");
        } else {
            out.append(body);
        }
        return out;
    }
    if (a.kind == Attachment::Kind::Symbol) {
        // 摘录以 符号 声明行,使模型立即获得签名上下文,
        // 加上几行 正文 调用者( 包含全部文件 )
        // 传输层 / 提交 -time 传输-up ) 将它
        // 视为一个代码 -fenced 节录,
        // 带 `// symbol:` 头文件 很明显是锚定的
        constexpr int kSymbolWindow = 20;
        std::string body = a.body.bytes();
        if (body.empty() && !a.path.empty()) {
            namespace fs = std::filesystem;
            fs::path p{a.path};
            if (p.is_relative()) p = tools::util::workspace_root() / p;
            // 和上面的FileRef分支一样的封存门.
            tools::util::NormalizedPath np{p.string()};
            auto wp = tools::util::promote_to_workspace_path(
                std::move(np), "@symbol attachment");
            if (wp) body = tools::util::read_file(*wp);
        }
        std::string out;
        out.reserve(a.name.size() + a.path.size() + 64);
        out.append("// symbol: ");
        out.append(a.name);
        out.append(" (");
        out.append(a.path);
        out.push_back(':');
        out.append(std::to_string(a.line_number));
        out.append(")\n");
        // 继续走 正文 数线, 切开窗户。
        int target = a.line_number > 0 ? a.line_number : 1;
        int start = std::max(1, target - kSymbolWindow / 4);
        int end   = target + (kSymbolWindow * 3 / 4);
        int line  = 1;
        std::size_t i = 0;
        while (i < body.size() && line < start) {
            if (body[i] == '\n') ++line;
            ++i;
        }
        while (i < body.size() && line <= end) {
            out.push_back(body[i]);
            if (body[i] == '\n') ++line;
            ++i;
        }
        if (out.empty() || out.back() != '\n') out.push_back('\n');
        return out;
    }
    if (a.kind == Attachment::Kind::Image) {
        // 传输层-level 线条( 4) Anthropic
        // 图像内容块)在此文本之外处理 -flatten
        // 路径——消息 -API 构建者在流言语被串行之前
        // 直接将图像附件拉下作曲家。我们仍然发出文字标记,
        // 这样,流言语就绕着流言语绕了。 标签 继续锁定到
        // 用户丢弃图像的地方, 从而调试日志( 打印平面的
        // 日志) user.text ) 命名文件。
        std::string out;
        out.reserve(a.path.size() + 24);
        out.append("[image: ");
        out.append(a.path.empty() ? "<inline>" : a.path);
        out.push_back(']');
        return out;
    }
    if (a.kind == Attachment::Kind::Output) {
        // 捕获本地运行。 把它提交到模型中,
        // 完全按照用户在手动运行后粘贴的方式: 命令行,
        // 然后是( 可能很大) 输出的栅栏块。
        // 这里没有切换, 整个日志是用户选择附加的;
        // 作曲家已经显示为 标签 的紧凑,
        // 所以大小成本是故意的, 可见的动作。
        std::string out;
        out.reserve(a.body.bytes().size() + a.name.size() + 32);
        out.append("I ran:\n```sh\n");
        out.append(a.name);
        out.append("\n```\noutput:\n```\n");
        out.append(a.body.bytes());
        if (!a.body.bytes().empty() && a.body.bytes().back() != '\n')
            out.push_back('\n');
        out.append("```");
        return out;
    }
    return a.body.bytes();
}

} // 命名空间结束。

std::string expand(std::string_view text,
                   const std::vector<Attachment>& attachments) {
    std::string out;
    out.reserve(text.size() + attachments.size() * 64);
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == kSentinel) {
            auto len = placeholder_len_at(text, i);
            if (len > 0) {
                auto idx = placeholder_index(text, i);
                if (idx < attachments.size()) {
                    // 包扎图案 正文 段落中断断,
                    // 所以周围的传言不会碰到它。 标签 崩溃到
                    // 单个\ nn\ n 通过帖子
                    // -pass 下图。
                    if (!out.empty() && out.back() != '\n') out.push_back('\n');
                    if (!out.empty() && (out.size() < 2 || out[out.size() - 2] != '\n'))
                        out.push_back('\n');
                    out.append(render_attachment_body(attachments[idx]));
                    // 确保正文在自己的线路上结束,
                    // 这样跟随内容不会碰到它.
                    if (out.empty() || out.back() != '\n') out.push_back('\n');
                }
                i += len;
                // 添加空白 -line 分隔符 仅 当内容更多
                // (文本或其它) 标签 ) 跟着这个占位符。
                // 没有这名警卫,就贴上贴纸 -as-only-message
                // 结尾为尾随 \ nn 渲染 以幽灵的空白行排列
                // 正文 ——可见于 `┃` / `┃` 用户底部的行
                // -turn 框中选择一个选项。
                if (i < text.size()) out.push_back('\n');
                continue;
            }
            // 草条哨兵 字节 (被腐蚀或用户) -typed 控制字符。
            // 静默地放下。 — 消息 正文 不应该携带 C0 控制
            // 字节 反正我还是会去的
            ++i;
            continue;
        }
        out.push_back(text[i++]);
    }
    // 3+ 的折叠运行 '\n' 回到两个这么深的地方 -to-back
    // 标签 不要产生明显的空白 -paragraph 堆叠。
    std::string collapsed;
    collapsed.reserve(out.size());
    int run = 0;
    for (char c : out) {
        if (c == '\n') {
            if (++run <= 2) collapsed.push_back('\n');
        } else {
            run = 0;
            collapsed.push_back(c);
        }
    }
    return collapsed;
}

namespace {

// "src/runtime/foo.cpp" → "foo.cpp"。
// 该词 标签 内线作曲中的标题是空格 -constrained
// – 完整路径需要30- 70 col 并主导可见行。
// 用户刚刚从 @ -picker 故你应当只以文件写着,
// 以便他记起他所附着的。 -time 扩展
// 仍然使用全路径.
std::string filename_only(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

} // 命名空间结束。

std::string chip_label(const Attachment& a) {
    char buf[256];
    if (a.kind == Attachment::Kind::FileRef) {
        std::snprintf(buf, sizeof(buf), "@%s", filename_only(a.path).c_str());
        return buf;
    }
    if (a.kind == Attachment::Kind::Symbol) {
        std::snprintf(buf, sizeof(buf), "#%s \xc2\xb7 %s:%d",
                      a.name.c_str(),
                      filename_only(a.path).c_str(), a.line_number);
        return buf;
    }
    if (a.kind == Attachment::Kind::Output) {
        // ⌘ <command> · 线条: K KB 紧凑的药丸,
        // 即使捕获的原木巨大。命令被剪断,
        // 所以长长的管道不会吹断标题宽度。
        std::string cmd = a.name;
        constexpr std::size_t kCmdMax = 32;
        bool clip = cmd.size() > kCmdMax;
        if (clip) cmd.resize(kCmdMax);
        for (char& c : cmd) if (c == '\n' || c == '\t' || c == '\r') c = ' ';
        const char* ell = clip ? "\xe2\x80\xa6" : "";
        if (a.byte_count >= 1024) {
            std::snprintf(buf, sizeof(buf),
                          "Output: %s%s \xc2\xb7 %zu lines \xc2\xb7 %zu KB",
                          cmd.c_str(), ell, a.line_count, a.byte_count / 1024);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "Output: %s%s \xc2\xb7 %zu lines \xc2\xb7 %zu B",
                          cmd.c_str(), ell, a.line_count, a.byte_count);
        }
        return buf;
    }
    if (a.kind == Attachment::Kind::Image) {
        const char* type = a.media_type.empty() ? "image" : a.media_type.c_str();
        // <clipboard> / <paste> 让他们的假象
        // -name 通; 一个真实路径只获得文件名, 原因与 QX
        // 相同 FileRef.
        bool sentinel = !a.path.empty() && a.path.front() == '<';
        std::string where = sentinel ? a.path : filename_only(a.path);
        if (a.byte_count >= 1024) {
            std::snprintf(buf, sizeof(buf),
                          "Image \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 %zu KB",
                          where.c_str(), type, a.byte_count / 1024);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "Image \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 %zu B",
                          where.c_str(), type, a.byte_count);
        }
        return buf;
    }
    // 粘贴 :
    //   — 图像或文件作为文本发送: 当它仍然到达时, 请显示它沿途有多远。
    //     如果它停止了短, 请说, 而不是 base64 的墙。
    if (a.byte_count >= 12 && a.body.bytes().starts_with("richpaste:1:")) {
        const auto p = armored_progress(a.body.bytes());
        if (p.arriving && !a.stalled && p.need > 0) {
            std::snprintf(buf, sizeof(buf), "Receiving image \xc2\xb7 %llu of %llu KB",
                          static_cast<unsigned long long>(p.have / 1024),
                          static_cast<unsigned long long>((p.need + 1023) / 1024));
            return buf;
        }
        if (p.arriving && !a.stalled) return "Receiving image\xe2\x80\xa6";
        if (p.need > 0)
            std::snprintf(buf, sizeof(buf), "Incomplete image \xc2\xb7 %llu of %llu KB arrived \xc2\xb7 copy it again",
                          static_cast<unsigned long long>(p.have / 1024),
                          static_cast<unsigned long long>((p.need + 1023) / 1024));
        else
            std::snprintf(buf, sizeof(buf), "Incomplete pasted image \xc2\xb7 %zu KB \xc2\xb7 copy it again",
                          a.byte_count / 1024);
        return buf;
    }
    //   – 短单行 ~ 标题显示实际文本( 最多~ 50 cols) ,
    //     这样用户就可以看到它们粘贴的, 而无需撤销和重排。
    //     Newlines / tabs 崩溃到
    //     空格中以备读取。
    //   - 一条长的单线( base64 , 简化代码, 符号堆) 或多
    //     -line 内容 __ "lines · bytes" 摘要;
    //     预览是噪音。
    if (a.line_count <= 1 && a.byte_count <= 200) {
        std::string preview;
        preview.reserve(60);
        constexpr std::size_t kPreviewMax = 50;
        bool truncated = false;
        for (char c : a.body.bytes()) {
            if (preview.size() >= kPreviewMax) { truncated = true; break; }
            if (c == '\n' || c == '\t' || c == '\r') {
                if (preview.empty() || preview.back() != ' ') preview.push_back(' ');
            } else if (static_cast<unsigned char>(c) >= 0x20) {
                preview.push_back(c);
            }
        }
        while (!preview.empty() && preview.back() == ' ') preview.pop_back();
        if (truncated) preview.append("\xe2\x80\xa6");  // …
        if (preview.empty()) {
            std::snprintf(buf, sizeof(buf),
                          "Pasted text \xc2\xb7 %zu B", a.byte_count);
        } else {
            std::snprintf(buf, sizeof(buf), "Pasted: %s", preview.c_str());
        }
        return buf;
    }
    const char* lines_word = a.line_count == 1 ? "line" : "lines";
    if (a.byte_count >= 1024) {
        std::snprintf(buf, sizeof(buf),
                      "Pasted text \xc2\xb7 %zu %s \xc2\xb7 %zu KB",
                      a.line_count, lines_word, a.byte_count / 1024);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "Pasted text \xc2\xb7 %zu %s \xc2\xb7 %zu B",
                      a.line_count, lines_word, a.byte_count);
    }
    return buf;
}

} // namespace agentty::attachment
