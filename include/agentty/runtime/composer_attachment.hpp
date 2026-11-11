#pragma once
// 作曲家附件——DON'T内联存在于ComposerState::text中的字节。
// 长粘贴（或 @file pick）成为作曲家
// `attachments` 向量中的附件，
// 并且在 `text` 中的光标处插入单个占位符标记
// `\x01ATT:N\x01`。视图渲染占位符为one
// -line 标签；提交时，占位符将替换为附件的 正文，
// 因此模型会看到文字 字节。
// 想法＆UX直接来自光标/Claude代码：整洁的作曲家，完整的载荷到传输。
//
// 为什么使用神奇的占位符而不是单独的 "rich text" 模型：
//   -光标/选择数学保持简单-string。 utf8_prev / utf8_next
//     只需要将占位符识别为单个导航
//     单元。
//   -Multi-attachment 排序是它们出现的任何顺序
//     `text`;没有并行索引来保持同步。
//   -提交-time替换是单个字符串替换传递；不
//     特殊-case为
//     此附件相对于周围正文应放在什么位置？
//
// 占位符字节使用\x01 (Start-Of-Heading，
// SOH)作为外部分隔符——实际上从未出现在user
// -typed文本或粘贴中的C0控制字符（StreamSink将
// 它们擦洗到传输上的U+FFFD）。中间是`ATT:` +十进制索引，
// 因此整个令牌最小是`\x01ATT:0\x01`（8 字节），
// 其后长度可变。

#include "agentty/domain/lazy_bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agentty {

struct Attachment {
    enum class Kind : std::uint8_t {
        Paste,      // 正文是逐字粘贴的文本。
        FileRef,    // 正文是提交时间（或标签-build)时的`path`的已加载。
        Image,      // 正文为原始图像字节； `media_type` 已设置。
        Symbol,     // 正文在提交时填写，路径：行跨度；携带`name`。
        Output,     // 捕获本地-run代码块的输出。 正文 是
                    // (可能巨大的）stdout+stderr；
                    // `name` 保存 标签 标题的命令行。
                    // 与 Paste 相同的折叠/展开-on-submit合同
                    // - 整个日志到达传输上的模型，但作曲家显示了一个one
                    // -line药丸。
    };

    Kind kind = Kind::Paste;
    /// 在提交时拼接到消息中的字节。对于粘贴，
    /// 此值在粘贴时填充；对于 FileRef，
    /// 它在提交时被延迟填充，因此最新的磁盘内容是
    /// 到达模型的内容（但上限为几百个 KiB）。
    /// 对于图像，它是原始图像字节 (不 base64 —编码是
    /// 传输层的工作，因此in-memory 标签保持紧凑)。
    ///
    /// LazyBytes，而不是 std::string：持久化的 正文 可以是 2
    /// MB 构建日志（输出附件），渲染 路径上的 无内容 读取它 —
    /// attachment::chip_label 从 `name`、`path`、
    /// `byte_count` 和 `line_count` 中提取。 字节 需要一次，
    /// 由 attachment::expand() 在请求 -build 时使用。
    /// 在每个 线程 加载 上热切地阅读它们是在真实商店中解码的 base64 的 4.2 MB，
    /// 但没有产生任何人看到的结果。 调用 `.bytes()` 至 按需加载；
    /// 请参阅 domain/conversation.hpp. 中的 LazyBytes
    LazyBytes body;
    /// 工作区-relative FileRef/图像/符号的路径。为空用于粘贴。
    std::string path;
    /// 图像附件的MIME类型(e.g."image/png"，"image/jpeg")。
    /// 对于非-image种类为空。
    std::string media_type;
    /// 符号名称为符号种类。否则为空。
    std::string name;
    /// 符号种类的1-based行号。否则为 0。
    int         line_number = 0;
    /// Pre-computed行+字节对于标签标题计数。
    /// 避免作曲家每次绘制 帧 时 re-walking 和 正文。
    std::size_t line_count = 0;
    std::size_t byte_count = 0;
    /// 图像-as-text粘贴在其完整之前就停止到达。
    /// 由作曲家档位检查设定；然后 标签 说再次复制它，
    /// 而不是永远复制 "Receiving"。
    bool stalled = false;
};

namespace attachment {

// 占位符边界字符。几乎永远不会出现在真实文本中——无论
// 是 user-typed 还是粘贴的文本。外包装为SOH
// -…-SOH；中间是 `ATT:` 后面是十进制索引。
inline constexpr char kSentinel = '\x01';

/// 构建附件索引`idx`的占位符标记。
[[nodiscard]] std::string make_placeholder(std::size_t idx);

/// 如果text[pos]开始占位符，则返回占位符的字节长度
/// （因此[pos，pos+len)是完整标记）。否则返回 0。
/// 调用者可以与 text.size() - pos
/// 进行比较以确保安全；该函数仅检查存在的 字节。
[[nodiscard]] std::size_t placeholder_len_at(std::string_view text,
                                             std::size_t pos) noexcept;

/// 如果text[pos-1]是占位符的结束标记，
/// 则返回以pos结尾的占位符的字节长度。否则返回 0。
/// 与 placeholder_len_at 配对，用于双向光标
/// -walks。
[[nodiscard]] std::size_t placeholder_len_ending_at(std::string_view text,
                                                    std::size_t pos) noexcept;

/// 解析是在从`pos`开始的占位符内编码的索引。
/// 对于格式错误的输入，返回 SIZE_MAX (调用者应将其视为
/// "no attachment")。 placeholder_len_at确认形状后使用。
[[nodiscard]] std::size_t placeholder_index(std::string_view text,
                                            std::size_t pos) noexcept;

/// 遍历`text`并用相应附件的正文替换每个占位符。
/// Out-of-range 索引会默默地删除（
/// 防御性的：使矢量不同步的光标编辑不应向模型显示垃圾）。
/// 相邻的 '\n' 行会折叠为单个段落分隔符，
/// 因此多个 标签 不会产生四个
/// -blank-line 间隙。
[[nodiscard]] std::string expand(std::string_view text,
                                 const std::vector<Attachment>& attachments);

/// 构建人类-readable 标签标题——e.g。 "Pasted text · 412 lines · 14 KB"
/// 或 "@src/auth/login.cpp"。该视图逐字使用此内联 渲染。
[[nodiscard]] std::string chip_label(const Attachment& a);

} // namespace attachment

} // namespace agentty
