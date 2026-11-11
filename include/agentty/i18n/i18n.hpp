#pragma once
// agentty::i18n-翻译查找。
//
// 设计：docs/design/i18n.md. 四个决定的简短版本，
// 因此这里的读者不必去获取长的版本：
//
//   NO GETTEXT。它需要 libintl（我们提供一个静态 二进制程序，
//   包括 musl 和 Termux），其复数规则是从磁盘上的文件中在 运行时
//   处计算的 C 表达式，msgid-as-English 表示英文副本
//   -edit 默默地使每个翻译无效。我们键入 IDS。
//
//   NO ICU。测量：38 MB，其中 33 MB 是 libicudata，
//   携带约 600 个语言环境，为我们的 20 个船舶提供服务。它真正要买的
//   一件事——复数规则——在我们的语言集中折叠成五个规则形状（plural.hpp），
//   而真正困难的问题，存储单元网格中的double-width CJK，
//   已经被maya::string_width正确解决了。
//
//   不 EVERYTHING IS TRANSLATED。 源码树 中存在约
//   5000 个字符串文字，其中大多数是针对 MODEL 的，而不是用户：
//   工具描述、沙箱 拒绝说明、错误详细信息。这些是与提供商签订的合同的一部分，
//   或者是专门为了改变模型行为而存在的，并且该模型最好遵循英语。规则是：IF
//   IT CROSSES 该 传输、IT STAYS ENGLISH。
//
//   LIVE、不 SEALED。 沙箱 被设置为-once，
//   因为在运行进程下移动的边界是不可审计的。语言不是边界。
//
// ── 为什么这是指针交换 不是重建 ─────────────────────────
//
// t()通过原子指针读取不可变 目录并返回string_view
// INTO。这使得 Late-bound 的翻译与颜色 (
// docs/LATE_BINDING.md) 完全一样：没有任何
// 东西已经内置了英语，因为它在每个 帧 上绘制了 调用 t() 视图。
// 切换语言存储一个指针，并翻译下一个 帧。
//
// 在此形状上测量：
//
//     切换语言 52 ns 一个原子存储 1 个
//     t() 查找 19 ns 零 分配 a 帧
//     (~200) 相对于 16 ms 预算，约 4 us
//
// 这就是为什么渲染路径可以自由地调用 t()
// 而不是在需要无效的地方缓存翻译后的字符串。
//
// LIFETIME。只要生成它的 目录 存在，返回的视图就有效。
// 目录 永远不会变异，也永远不会释放，而 帧 可能持有视图（
// 交换使传出的 目录 在 shared_ptr 期间保持活动状态，
// 直到最后一个读取器将其丢弃），因此在 帧 期间捕获的视图
// 对于 帧 保持有效。不要在 帧 中存储一个 - 而是保留 id，
// 它是一个字符串文字，并且比所有内容都寿命更长。

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::i18n {

// ── 二十岁 ───────────────────────────────────────────────────────────
//
// 封闭的枚举而不是free-form标签，因为我们声称的每种
// 语言都需要复数规则、其自己的脚本中的名称以及目录——并且所有
// 三个都是编译-time事实。在这里，新语言是经过深思熟虑的编辑，
// 而不是某人放入目录中的文件。
//
// 该列表在此停止是有原因的：阿拉伯语、希伯来语、波斯语、
// 泰语和印度语脚本需要比迪或复杂的形状，而存储单元-grid TUI
// 无法正确完成。添加一个是一个真实的项目，而不是另一个 JSON 文件。
enum class Lang : std::uint8_t {
    en, zh_CN, es, pt_BR, ja, de, fr, ko, it, ru,
    tr, id, pl, nl, uk, sv, cs, da, no, hu,
};

inline constexpr int kLangCount = 20;

// BCP-47标签，如其出现在settings.json和--lang中。
[[nodiscard]] std::string_view tag_of(Lang l) noexcept;

// 解析标签。 Case-insensitive，并容忍真实环境产生的形状：
// "de_DE.UTF-8"、"pt-BR"、"zh_CN"、"EN"。
//
// 区域回退是故意且不对称的：
//   pt-PT -> pt-BR (足够接近以便为葡萄牙语
//   使用者提供服务）zh-TW -> 不 zh-CN。繁体不是简体，
//                       默默地为另一方服务比英语还糟糕。
[[nodiscard]] bool parse_tag(std::string_view tag, Lang& out) noexcept;

// The language's own name, in its own script ("Deutsch", "日本語"). What the
// 选择面板表明：寻找他们的语言的用户正在寻找他们将
// 使用的单词，而不是我们的英文名称。
[[nodiscard]] std::string_view endonym(Lang l) noexcept;

// 英文名称，用于日志和lint的错误消息。
[[nodiscard]] std::string_view english_name(Lang l) noexcept;

// ── 查询 ───────────────────────────────────────────────────────────────

// 主动语言。在 load() 运行之前默认为 en，
// 因此在 目录 着陆之前任何启动路径上的 t() 调用 返回
// 英语而不是 id ——启动顺序一定不能产生可见的垃圾。
[[nodiscard]] Lang active() noexcept;

// 交换活动的目录。当语言没有编译 目录 时返回 false，
// 使前一个保持活动状态 - 丢失的翻译绝不能将
// UI 留空。
bool set_active(Lang l);

// 一个翻译字符串。
//
// 回退ACTIVE->ENGLISH->id本身。
//
// 英语梯级在实践中是重要的：每个翻译都暂时是部分的，并且 60%
// -complete 德语 面板 必须显示其余部分的英语，
// 而不是原始 ID。 （渲染 测试发现了这一点——第一个版本直接
// 跳到 id 并将 `sandbox.ports` 绘制为行标签。）
//
// 仅当 ENGLISH 缺少 id 时才能到达最后一个梯级，
// 此时 lint 会导致构建失败。它返回 id 而不是空白，
// 因为明显的错误击败了不可见的错误：屏幕上显示 "sandbox.ports"
// 是错误报告，空行是用户想知道他们破坏了什么。
[[nodiscard]] std::string_view t(std::string_view id) noexcept;

// 带有计数的翻译字符串，选择 CLDR 复数类别作为活动语言。
// 请参阅 plural.hpp 了解为什么这不能成为后缀规则。
[[nodiscard]] std::string_view tn(std::string_view id, long long n) noexcept;

// ── 内插 ────────────────────────────────────────────────────────
//
// 仅 NAMED 占位符："{n} of {total}"。
// 跨语言的位置参数重新排序——德语将动词放在最后，日语将宾语放在
// 前面——并且不能指望翻译人员能够计数。名称在重新排序后仍然存在。
//
// 未知的占位符被逐字保留而不是被删除，因此目录中的拼写
// 错误在屏幕上显示为"{tatol}"而不是默默地消失。
struct Arg {
    std::string_view name;
    std::string      value;
};

[[nodiscard]] std::string format(std::string_view id, std::span<const Arg> args);
[[nodiscard]] std::string format_n(std::string_view id, long long n,
                                   std::span<const Arg> args);

// ── 目录 装入 ──────────────────────────────────────────────────────

// 从 JSON 文本安装 目录。由启动时的嵌入表和从 $AGENTTY_HOME
// /i18n/. 的 debug-only 热重载使用
//
// 对格式错误的输入返回假（并且不改变任何内容）：半-applied
// 目录在两种语言中将是UI。
bool install_catalog(Lang l, std::string_view json_text);

// 主动目录知道的每个id。用于棉绒和测试。
[[nodiscard]] std::vector<std::string> ids_of(Lang l);

// `l`被翻译了多少，0..1，以en作为分母。 选择面板
// 显示了这一点，因此选择 60%-complete 语言的
// 用户知道他们会得到什么，而不是一次在一个屏幕上发现它。
[[nodiscard]] double completeness(Lang l);

}  // namespace agentty::i18n
