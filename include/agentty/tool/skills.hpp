#pragma once
// agentty::tools::skills —特工技能（agentskills.io)引擎。
//
// 实施开放代理技能标准，具有完整的三
// -tier渐进公开：
//
//   第 1 层 目录：乘坐系统 提示词 的每个技能的名称 + 描述（
//           每个技能约 50-100 个代币）。始终存在，从不庞大。
//   第2层指令：当模型决定任务匹配时，通过`skill`
//           工具按需提供完整的SKILL.md、正文、已加载。
//   第3层资源：激活结果中列举的捆绑文件（scripts/、
//           references/、assets/)，
//           但NEVER急切地读取——当针对它的指令调用时，
//           模型`read`是特定文件。技能目录是 列入允许清单 用于读取，
//           因此 工作区 之外的第 3 层提取不会超出边界。
//
// 技能是包含`SKILL.md`和YAML frontmatter的目录：
//
//     ---
//     名称：pdf
//     -extract 描述：使用pdfplumber从PDF文件中提取文本+表格。
//     兼容性：需要Python 3.10+和pdfplumber#
//     可选 allowed-tools:bash read#可选
//     disable-model-invocation:true#可选
//     ---
//
// 该目录可以位于发现根以下的任意深度；嵌套组文件夹与 `-` 连接
// 到技能名称中（`skills/ embedded/startup/`
// → 名称 `embedded-startup`，
// 请参阅下面的发现表）。
//     完整 Markdown 正文，包括详细指令、代码片段等。
//
// 发现根，按优先顺序（首先击中名称获胜）：
//
//   项目 <cwd>/.agentty/skills/<slug>/SKILL.md
//   (本机) 项目 <cwd>/.agents/skills/<slug>/SKILL.md (跨 -client
//   约定) 项目 <cwd>/.claude/skills/<slug>/SKILL.md (Claude
//   代码兼容) 用户~/.agentty/skills/<slug>/SKILL.md
//   （本机）用户 ~/.agents/skills/<slug>/SKILL.md（跨-client约定）
//   用户 ~/.claude/skills/<slug>/SKILL.md（Claude 代码兼容）
//
// `<slug>`可以嵌套到根以下的`kMaxSkillDepth`
// 级别（`skills/embedded/startup/SKILL.md`
// 是名为`embedded-startup`的技能）：组文件夹组织库，
// 并且技能的名称是其根以下的路径，SANITIZED到规范的
// `a-z0-9-`字符集（参见slug_from_path)—
// —该名称是用户键入的`/name`，因此它是强制执行的，
// 而不是假设的。隐藏目录（以 `.` 开头）永远不会下降；
// 在行走过程中无需遵循符号链接即可发现符号链接的技能目录。
// 跨根或深度的名称冲突：表中较早的根获胜，而在根中，SHALLOWER
// 技能获胜 - 因此，即使存在分组的 `embedded/startup/`，
// root-level `startup/` 也会保留其名称，
// 并且平面 `a-b/` 不会被不相关的 `a/b/` 组
// 文件夹取代。 Flat single-level 布局
// 保持其现有名称不变。
//
// `.agents/skills/`约定意味着由
// 任何兼容客户端安装的技能(Claude代码、Codex、
// 光标……)对于agentty自动可见，反之亦然。
// 项目技能影子同名的用户技能（通用约定）。
//
// 解析 故意是 LENIENT（根据规范的 client
// -implementation 指导）：描述 解析 中未加引号的
// 冒号很好，与其目录不匹配的名称仍然是 加载，并且缺少的描述回落到
// 正文 的第一个非 -blank 行。只有不可读或空的 SKILL.md
// 才会跳过该条目。

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/scope/scope.hpp"
#include "agentty/tool/effects.hpp"
#include "agentty/util/io.hpp"

#include <maya/runtime.hpp>

namespace agentty::tools::skills {

struct Skill {
    std::string name;          // frontmatter `name` (或 slug 回退)
    std::string description;   // frontmatter `description` (或 正文 第一行)
    std::string body;          // frontmatter后的markdown
    std::string source;        // "project" | "user"（目录 的出处）
    std::string compatibility; // frontmatter `compatibility`（环境要求）
    std::string allowed_tools; // frontmatter `allowed-tools`（实验性）
    std::string license;       // 前事`license`
    // Frontmatter `effects:`-该技能将告诉代理什么给DO，
    // 在SAME中，四个-bit词汇工具已经声明（effects.hpp)，
    // 因此技能的影响与工具的影响在同一轴上读出，并且权限 UI具有
    // 到渲染的一个单词列表。 ABSENT 是常见情况，意味着散文
    // -only：今天存在的每一项技能 解析 都是一个空集，
    // 并采取未触及的路径（参见 needs_trust_gate）。
    EffectSet   effects{};
    // Frontmatter `source:`-其中技能CAME FROM ("github.com/…")。
    // 出处，本着 scope::Source 的精神：用户获取
    // 的技能与他们编写的技能不同，并且安装 提示词 是这样说的。
    // 手-authored技能为空。
    std::string origin;
    bool        user_only = false; // `disable-model-invocation`:隐藏
                                   // model-facing 目录，
    // 当 SKILL.md 是 PRESENT 但大于 kMaxBodyBytes 时，
    // 可显式加载 Non-zero，因此其中不可能有 已加载。
    // 该条目的存在纯粹是为了报告：一项过大的技能曾经随着
    // `0 warning(s)` 一起消失，而作者无法从路径拼写错误中辨别出来。
    // 这样的技能永远无法使用——它没有名称、描述或正文。
    std::uintmax_t oversized_bytes = 0;
    std::filesystem::path dir; // 绝对技能目录(三级基本路径)
    // Spec-derived名称：发现根下面的路径，其中包含由'
    // -'连接的段——对于root-level技能，该IS是叶目录名称。
    // 与 `name` 放在一起，因为 frontmatter
    // 可能会覆盖该名称； lint 比较两者（嵌套技能永远不会
    // 单独匹配其叶目录，因此这将是错误的不匹配）。
    std::string slug;
    // Frontmatter `metadata:`嵌套映射，
    // 按文件顺序展平为key/value对。规格：任意 client-defined 属性。
    std::vector<std::pair<std::string, std::string>> metadata;
    // 捆绑资源文件，相对于`dir`(e.g."scripts/run.py"，
    // "references/REFERENCE.md")。在发现时枚举，
    // 上限为 kMaxResources，从不热切地阅读。 SKILL.md 本身被排除在外。
    std::vector<std::string> resources;
};

// 发现+解析项目下的每一项技能+用户根。有界：
// 最多 目录 上限（默认为 kMaxSkills；AGENTTY_MAX_SKILLS
// 环境变量在每次发现过程中覆盖它 — 参见 skills.cpp
// 中的 catalog_cap()），每个 正文 上限为
// kMaxBodyBytes。结果缓存在每个 SKILL.md 的 mtime
// 中由根的 和 键控的进程-wide（下一回合将拾取对技能的 in-place
// 编辑）。项目技能影子同名的用户技能。
[[nodiscard]] std::vector<Skill> all(Io);

// 按确切名称查找一项技能。如果不存在则为 nullptr。
// 也查找 `disable-model-invocation` 技能（允许显式查找；
// 只有 目录 隐藏它们）。
[[nodiscard]] std::optional<Skill> find(Io, std::string_view name);

// 渲染用于系统提示词的紧凑层1目录块：
//   <skills>
//   可用技能（加载使用`skill`工具的完整说明）：
//   -名称：描述
//   ...
//   </skills>
// 标记为 `disable-model-invocation` 的技能被完全省略（
// 列出了 -but-blocked 的隐藏节拍：模型绝不会在它们上浪费一回合）。
// 当不存在合格技能时为空字符串（不发出块）。
[[nodiscard]] std::string catalog_block(Io);

// 渲染一项技能的第2层激活载荷:包裹在
// <skill_content name="...">中的正文、
// 绝对技能目录(因此模型可以解析相对引用)、存在的兼容性注释以及捆绑
// 文件的<skill_resources>列表。这是 `skill`
// 工具返回的内容。
[[nodiscard]] std::string activation_payload(Io, const Skill& s);
// 针对调用者已有的批准存储的相同操作：纯。
[[nodiscard]] std::string activation_payload(const Skill& s, const scope::Approvals& approvals);

// ── 激活:从记录中得出,未储存 ──────────────
// 一旦技能 正文 出现在对话中，re-injecting 就会使零
// 信号的令牌成本加倍，因此第二个 加载 会返回一个短的
// "already active" 哨兵。
//
// "Already active"表示：消息中的<skill_content name="X">
// 块的模型仍然可以是SEE。这是关于转录本的一个事实，
// 所以它是根据转录本计算出来的——从来没有保存在一个单独的集合中。
//
// 它曾经是一个process-global集合(note_activated/reset_activations)，
// 并且仅近似转录本的集合从中漂移：
//   *在/compact之后，持有正文的tool_result被总结掉，
//     但该集合仍然显示"active"，因此模型被告知指令
//     是"in an earlier tool_result"，
//     不再存在并且从未取回它们；
//   * 恢复 线程 开始设置为空，因此 正文 仍然是普通的
//     视图被第二次注入；
//   * 每一个线程开关，新的线程和前叉都必须记住调用
//     reset_activations()，来自状态更新函数内部。
// 导出它会删除所有三个：没有任何需要重置的内容，
// 并且答案是模型实际看到的任何内容。
//
// `visible`是传输-visible消息的文本：发送的
// 模型是什么（应用了最新的压缩）。调用者从 线程 构建它；
// 技能 TOOL 在没有 线程 的 工作线程 上运行，
// 接收派生集作为其 分派 的一部分。
[[nodiscard]] bool is_active_in(std::string_view name,
                                const std::vector<std::string_view>& visible);

// 每个技能名称在`visible`中带有正文，对于一次需要整套技能的
// 调用者（工具分派）。与 is_active_in 相同的规则。
[[nodiscard]] std::vector<std::string>
active_in(const std::vector<std::string_view>& visible);

// 技能工具的"already active"组与调用一起移动：
// tools::CallContext::active_skills，源自可见转录本。

// ── 斯佩克验证(谱系的`skills-ref validate`等值) ────
// 针对agentskills.io限制检查一项技能。加载停留在
// LENIENT（警告-don't-block，
// 根据客户端-implementation 指导）；此表面供作者使用：
// `agentty skills` 打印每项发现的技能及其诊断。
// 检查：名称字符集（小写字母+单个连字符，无边缘连字符），名称≤64个字符，
// 名称与spec-derived slug匹配（发现根目录下的'-'
// -joined路径 - 对于作为父目录的平面布局），描述存在且≤1024个字符，
// 正文≤500行（规范建议 -
// 将详细信息移动到references/）。
[[nodiscard]] std::vector<std::string> lint(const Skill& s);

// ── 副作用:从前题分析的已宣布的能力 ────────────
// 像 解析器 的其余部分（规范的 client-implementation
// 指导）一样宽松：`[exec, net]`、`exec net`、
// `exec,net` 和 `write-fs`/`write_fs`
// /`writefs` 都是 解析。未知单词是 IGNORED 而不是
// 致命的——未来的 副作用 名称不得使技能在较旧的 二进制程序 上卸载。
// 空值或缺失值会产生空集，这正是当今每种技能的行为。
[[nodiscard]] EffectSet parse_effects(std::string_view value) noexcept;

// 渲染和EffectSet作为前题拼写("exec, net")，
// 用于`agentty skill list`和安装提示词。规范名称上
// parse_effects 的反转。
[[nodiscard]] std::string effects_to_frontmatter(EffectSet e);

// ── 信任:装入这种技能是否需要经过慎重批准? ─────
// 技能是INSTRUCTIONS，不是沙盒代码——但是说明
// 运行 npx、创建远程租户，并把凭据写进仓库。
// 与项目-local MCP服务器具有相同的威胁类别，
// 并且该类别已经被解决（scope::Trust，内容-bound批准——
// MCPoison教训：将信任固定到CONTENT，而不是名称）。
//
// 门在声明的 副作用 上触发 仅。 Prose-only 技能（即
// 当今存在的所有技能）永远不会受到限制，因此采用此技能不会给
// 现有用户带来任何成本。该声明是下面的 static_assert，
// 而不是评论。
[[nodiscard]] constexpr bool needs_trust_gate(EffectSet e) noexcept {
    return !e.empty();
}

namespace proofs {
// "Off is a byte-for-byte no-op" 作为构建错误而不是承诺：
// 散文-only技能绝不能达到批准路径。
static_assert(!needs_trust_gate(EffectSet{}));
static_assert( needs_trust_gate(EffectSet{Effect::Exec}));
static_assert( needs_trust_gate(EffectSet{Effect::Net}));
static_assert( needs_trust_gate(EffectSet{Effect::WriteFs}));
// ReadFs独尚门："read every file under ~"是实问。
static_assert( needs_trust_gate(EffectSet{Effect::ReadFs}));
} // namespace proofs

// 总计：（技能、批准）→信任。纯粹，所以 reducer/test
// 可以用一个虚构的商店来驱动它。
//
//   副作用 空 → 受信任（散文；今天的路径）副作用 + 内容哈
//   希已批准 → 受信任 副作用 + 已
//   获取（`source:`） → 待处理（用户未创作）副作用，
//   hand-authored，项目 → 待处理（仓库 无法为自己
//                                                 提供保证 — 克隆配置）
//   副作用，hand-authored，用户→可信（人写的）
//
// 改变正文并且散列改变，因此v1获得的批准不会转移到
// 获得`exec`的v2。 re-gate 就是重点。
[[nodiscard]] scope::Trust trust_of(const Skill& s,
                                    const scope::Approvals& approvals) noexcept;

// 解析 一个 SKILL.md 的文本。暴露后，`skill add`
// 可以描述一个即将安装WITHOUT的文件，巧妙地-different
// 解析器——CLI短暂地有一个，并且它在块标量上默默地失败了（`描述：
// |`)，向用户显示一个裸露的 "|"，其中应该有描述。
// 一个解析器，一组怪癖。
//   raw — 文件内容
//   slug — 目录名称，当 `name:` 不存在时标识 回退 source —
//   "user" | "project" | ""
[[nodiscard]] Skill parse_skill_text(const std::string& raw,
                                     const std::string& slug,
                                     const std::string& source);

// 名称碰撞而丢失的技能。跟踪 ACROSS 范围是一项
// 功能 - 项目技能凌驾于用户技能之上，这是记录在案的规则。
// 在 相同 范围内声称相同名称的两个目录并非如此：这要么是
// 一个错误，要么是一种在其他东西使用您的名字时坐在磁盘上的方式。
// 无论哪种方式，用户都应该能够看到它。
//
// 与 已加载 集合分开，因此 下游 不会意外地将
// 隐藏技能视为活动技能 — 它是诊断，而不是技能。
struct Shadowed {
    std::string name;          // 有争议的名称
    std::string dir;           // 丢失副本所在的地方
    std::string source;        // 其范围
    EffectSet   effects{};     // 它所宣称的——隐藏有效
                               // 技能是有趣的案例
    std::string winner_dir;    // 得名目录
};

// 在上次发现期间由于名称冲突而掉落的每项技能。
// 绝大多数安装都是空的。
[[nodiscard]] std::vector<Shadowed> shadowed(Io);

// 这一名称是否被 相同 范围内的多个目录所声明？
// 这就是值得标记的情况——cross-scope阴影是有意的。
[[nodiscard]] bool shadowed_within_scope(Io, std::string_view name);

// 这个名称用作目录名称安全吗？
//
// 技能的`name:`是作者-controlled并且`skill add`从它构
// 建INSTALL PATH——在其存在之前这是任意-filesystem-write原语。
// 声明 `name: ../../../../tmp/PWNED` 的技能安装
// 在技能根目录之外，并且是从 PROSE 技能执行的，而该技能甚至从来没有 提示词。
// `name: /etc/cron.d/x` 完全忽略了根。
//
// 一个普通路径组成部分：字母、数字、'-'、'_'、'.'，没有前导点或破折号，
// 最多 64。其他任何东西都是 REFUSED 而不是重写——当
// 文件说 "../../foo" 时，默默地安装 "foo"，
// 用一个惊喜换另一个惊喜。
[[nodiscard]] bool safe_skill_name(std::string_view name) noexcept;

// 解析后`child`是否真的在`root`内部？
// 第二个是对安装路径的独立检查，因此遏制并不
// 依赖于详尽的字符集规则。
[[nodiscard]] bool path_inside(const std::filesystem::path& root,
                               const std::filesystem::path& child) noexcept;

// ── 不信任的作者文本 ───────────────────────────────────────
// 技能的`description`由编写该技能的人编写，
// 并且它被呈现在不能被劫持的TWO位置：同意提示词
// (其中agentty应该是说话的人)和模型
// -facing 目录(距指令一行)。
//
// Snyk在2026年2月对3,984个技能的审计发现，
// 91%的已确认的-malicious技能携带提示词注入；
// arXiv 98k-skill 研究记录了 HTML 注释中的隐藏指
// 令和不可见的 Unicode。因此，作者文本在显示之前经过清理：
// 控制字符、bidi/zero-width 走私和换行符被删除，
// 长度为-capped。这确实使 不 的文本可以安全地服从——它使其无法
// 模仿周围的 帧。
[[nodiscard]] std::string sanitize_author_text(std::string_view raw,
                                              std::size_t max_len = 160);

// ── 注射检查 ────────────────────────────────────────────
// 一项发现推动了这一点：每个人都批准的提示词不是对照。
// Anthropic测得对Claude代码权限 提示词的批准率为93%；
// Akhawe & Felt 测得 Chrome SSL 插页式广告的点击率为 70%。
// 习惯是统一的提示词的DEFAULT结果。
//
// 所以agentty不会每次都问同样的问题。它会筛选 正文
// 中实际出现在野外的模式，以及 RAISES 中的 提示
// 词（当它们存在时）——这是一种有区别的警告，
// 这是习惯化文献发现有效的干预措施（BYU fMRI 研究：
// 多态警告显着降低了注意力的习惯性）。
//
// REVIEW AID，从来没有判决。干净的结果就是证据之一。
struct Finding {
    enum class Severity : std::uint8_t { Note, Warn, Critical };
    Severity    severity;
    std::string code;      // 稳定ID，e.g。 "curl-pipe-shell"
    std::string detail;    // 所看到的，用agentty的话来说
    int         line = 0;  // 1-based线中的正文; 0 = 整个文件
};

// 筛选技能正文以获取记录的攻击模式。
// 按严重程度排序，然后行。绝大多数技能为空。
[[nodiscard]] std::vector<Finding> screen_body(std::string_view body);

[[nodiscard]] constexpr std::string_view to_string(Finding::Severity s) noexcept {
    switch (s) {
        case Finding::Severity::Critical: return "critical";
        case Finding::Severity::Warn:     return "warn";
        case Finding::Severity::Note:     return "note";
    }
    return "note";
}

// 审批存储叶，在USER根下——克隆的仓库不能在此处写入，
// 因此它不能预置-approve自己的技能。
inline constexpr std::string_view kApprovalsLeaf = "skills_approved.json";
// NOTE：config::kSkillsApprovals是相同的名称，在清单中声明。
// skills_cli.cpp中的static_assert防止两者漂移。

// 加载/保留技能批准存储（用户root下的kApprovalsLeaf）。
// 故意与 hooks 和 MCP 的商店分开：批准
// 技能不得默默批准 MCP 服务器。
[[nodiscard]] scope::Approvals load_approvals(Io);
void                           save_approvals(Io, const maya::unlocked&, const scope::Approvals&);

// 便利性：解决技能对持久存储的信任。
[[nodiscard]] scope::Trust trust_of(Io, const Skill& s) noexcept;

// 确切的字节批准通过以下方式键入：正文+声明的副作用。
// 公开后，同意 UI 可以向用户显示他们正在批准什么，
// 因此测试可以断言 re-gate，而无需深入实现。
[[nodiscard]] std::string content_sha_of(const Skill& s);

// ── `agentty skill` CLI号 ────────────────────────────────────────────────
// 动词∈{添加、列出、删除、批准}。每个源都有一个安装路径：
//
//   agentty 技能添加 github.com/user/repo 已
//   agentty skill add ./path/to/skill-dir       local copy
//   ./path/to/skill-dir 本地副本 agentty 技能列表，
//   包含 副作用 + 信任列
//   agentty 技能删除 NAME agentty 技能阅读后批准 NAME
//
// 故意将NOT设置为第一-run屏幕，将NOT设置为菜单条目：
// 因为用户通过名称请求了能力，所以能力到达。返回进程退出代码；
// 打印它做了什么。
int cli(Io, const maya::unlocked&, const std::vector<std::string>& argv);

// CLI条目：通过lint诊断列出每个发现的技能（范围、目录、
// 资源计数）。当每个技能都干净时返回 0，当任何诊断触发时返回 1
// — 可在 CI 中使用，就像 `skills-ref validate` 一样，
// 无需额外工具。
int cmd_skills(Io);

inline constexpr std::size_t kMaxSkills     = 64;
inline constexpr std::size_t kMaxBodyBytes  = 64 * 1024;
inline constexpr std::size_t kMaxResources  = 32;
// 走到嵌套技能根的最深组。组文件夹是一个 ORGANIZING 设备
// - `embedded/startup/`，最多是一个类别和
// 一个子 -category - 所以 4 已经很慷慨了，
// 而无上限的行走将恰好包含大 源码树（一次杂散结帐，
// 一个 node_modules）的技能根变成了每次 缓存 失误时的无限工作。
// 它还限制派生名称：每个级别附加一个路径段，并且 40-deep 嵌套生成
// 238-character "name"，`/command` 无法调用该名称。
inline constexpr std::size_t kMaxSkillDepth = 4;
// 最长的派生技能名称。名称为 带类型的 为 `/name`，
// 因此不能为 带类型的 的名称不是名称。
inline constexpr std::size_t kMaxSlugLen    = 64;

// 测试接口边界：自进程开始以来，目录上限已从
// 环境中解析了多少次。
//
// 上限限制了 scan_root 的行走，因此曾经根据 DIRECTORY
// ENTRY 进行了咨询——这使得畸形的 AGENTTY_MAX_SKILLS
// 在每个条目中记录其面包屑（测量：120 ERROR-level 行，
// 每次超过 40 种技能的单次传递进入坠机飞行记录器）。现在，每 all()
// 遍都会解析 一次 并通过线程。
//
// 测试可以直接断言：log-line计数将需要logx的文件接收器，
// 其在第一次使用时锁存，因此在共享测试二进制程序
// 内是不可靠的。计数分辨率在其源处断言相同的属性，
// 而不依赖于接收器状态。
[[nodiscard]] std::size_t debug_cap_resolutions() noexcept;

} // namespace agentty::tools::skills
