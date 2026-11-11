#pragma once
// agentty::settings::registry —每个用户-facing设置，作为一张表。
//
// 该 PROBLEM。 agentty 增加了大约 35 个仅作为
// 环境变量存在的真正策略旋钮。每个都在四个地方声明——配置结构上的一个字段、
// `from_env()` 中的一行、加载 设置中的一行、保存 设置中的
// 一行——但没有出现。它们是不可发现的（你必须阅读源代码才能知道它们的存在），
// session-only（导出它或丢失它），并且未经验证（
// `env_float("...", 0.65f)` 很乐意接受 900）。
//
// 该 FIX。设置是 ROW。下表是唯一的事实来源，
// 其他所有内容都是通过步行得出的 派生：
//
//   *环境读取——读取`.env`，夹至`.range`
//   * settings.json — load/save 在 `.id` 上键入
//   *设置UI —SettingDef->form::Field
//   * 验证——`.range`仅在一处强制执行
//   * `agentty config` — list/get/set，免费
//
// 添加旋钮是添加行，而不是跨三个文件的四次编辑。与 `kProviders` 相同的承诺：
// 添加提供方只需增加一行表项。
//
// ── 电话 绑定 属于行 ───────────────────────────────────────
// `slot`是-to-member到store::RagConfig的指针，
// 因此该行知道如何READ和WRITE其自身的值。如果没有它，每个步行者都
// 需要在 `id` 上有自己的开关——这正是该表要删除的重复项。
// 指针-to-member是constexpr-constructible，
// 所以整个表保持编译-time常量。
//
// ── 等级是编辑判断,它很重要 ───────────────────────
// 并不是CAN被暴露于SHOULD的一切。
// `autocut_sensitivity` 是一个引擎-tuning 常量，
// 用户无法评估；让它变得突出会让一个令人困惑的东西变得容易被发现
// 而不是有用。高级行隐藏在按键后面。基本的标准：一个理性的用户
// 会合理地选择任何一种方式吗？他们能说出他们更喜欢哪种方式吗？

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "agentty/domain/web_search_config.hpp"  // web_search.*行：边界+结构
#include "agentty/domain/paste_config.hpp"       // 粘贴.*行：边界+结构
#include "agentty/domain/smart_tuning.hpp"   // 发货默认值+范围
#include "agentty/runtime/panel/form.hpp"           // 建造者(add_rows)
#include "agentty/store/store.hpp"
#include "agentty/util/io.hpp"

namespace agentty::settings::registry {

// 设置UI中出现一行。
enum class Group : std::uint8_t {
    Sources,      // 哪些语料库被索引
    Pipeline,     // 检索阶段
    Fusion,       // 词汇+密集分数如何结合
    Proactive,    // pre-turn注射
    Infra,        // 持久化，追踪，反馈
    Routing,      // 智能模式的数值策略
    WebSearch,    // web_search:可用性、结果计数、排除
    WebSearchServices, // web_search:哪些服务应答，按什么顺序
    PasteRoutes,  // 粘贴：Ctrl+V可以从其中读取剪贴板
    PasteFiles,   // 粘贴：大小限制，粘贴后的文件会变成什么
    PasteExpert,  // 粘贴：针脚一代理套接字
};

[[nodiscard]] constexpr std::string_view label_of(Group g) noexcept {
    switch (g) {
        case Group::Sources:   return "Sources";
        case Group::Pipeline:  return "Pipeline";
        case Group::Fusion:    return "Fusion";
        case Group::Proactive: return "Proactive";
        case Group::Infra:     return "Infrastructure";
        case Group::Routing:   return "Routing";
        case Group::WebSearch: return "Search";
        case Group::WebSearchServices: return "Services";
        case Group::PasteRoutes: return "Where to read from";
        case Group::PasteFiles:  return "Images and files";
        case Group::PasteExpert: return "Expert";
    }
    return "";
}

// 基本行始终显示；高级隐藏在按键后面。请参阅 头文件
// 注释 - 这是关于用户是否可以对旋钮进行操作的判断，
// 而不是关于它有多晦涩。
enum class Tier : std::uint8_t { Basic, Advanced };

// 该行是什么类型的控制。故意 不 和 变体 的值：该值
// 存在于配置结构中，并通过 `slot` 到达，
// 因此一行仅包含其 TYPE 及其边界。
enum class Type : std::uint8_t { Bool, Real, Int, Enum, Text };

// WHICH配置结构行绑定到。
//
// 该表开始生命RAG-only，并且每个访问者获取RagConfig。
// 智能模式的数字策略具有与 RAG 相同的问题 - 仅作为环境
// 变量存在的旋钮，因此无法发现，session-only 且
// 未经验证 - 并且值得相同的解决方案。
//
// 两个所有者都是 store::Settings 组成的 DOMAIN 类型，
// 因此一行绑定到 运行时 实际读取的内容。这就是 UI 编辑采用 副作用
// 的原因：面板 写入路由器保存的相同结构，没有可在之间转换的形状，
// 也没有需要忘记的 fan-out 步骤。
//
// 搜索是第三个，同理：web_search的策略是域类型（
// web_search_cfg::Config)，工具层读为-is。
// 它的行是唯一的 Web 搜索 面板 渲染 和唯一的 Type::Text
// — 排除列表是自由文本，而不是数字或 枚举。
//
// 粘贴第四：Ctrl+V和`agentty paste`如何读取剪贴板(paste_cfg::Config，
// 由io/clipboard.cpp交给richpaste)。
enum class Owner : std::uint8_t { Rag, Smart, WebSearch, Paste };

// 将指针-to-member放入持久化配置中。该行是 read/write 绑定。
// 每对（类型、所有者）一个替代方案； `Owner` 表示哪一半是实时的。
//
// 成员-pointer TYPE总是通过下面的别名来拼写，从不内联：MSVC
// 不能解析命名空间-qualified指针-to-member直接作为模板参数写入
// ——`std::variant<bool store::RagConfig::*>`
// 在`*`处随着C3083/C2059消亡(它只知道声明器上下文中的
// `<tag>::*`，这就是`&store::RagConfig::field`
// 初始化器进一步到处都是 编译）。通过命名别名路由每个使用，
// 将每个使用变成一个普通类型参数：相同的类型，可由所有三个 编译器
// 解析。
template <class C, class M>
using MemberPtr = M C::*;

using Slot = std::variant<
    MemberPtr<store::RagConfig, bool>,
    MemberPtr<store::RagConfig, int>,
    MemberPtr<store::RagConfig, float>,
    MemberPtr<store::RagConfig, double>,
    MemberPtr<store::RagConfig, std::string>,
    MemberPtr<smart::RoleConfig, bool>,
    MemberPtr<smart::RoleConfig, int>,
    MemberPtr<smart::RoleConfig, float>,
    MemberPtr<smart::RoleConfig, double>,
    MemberPtr<smart::RoleConfig, std::string>,
    MemberPtr<web_search_cfg::Config, bool>,
    MemberPtr<web_search_cfg::Config, int>,
    MemberPtr<web_search_cfg::Config, std::string>,
    MemberPtr<paste_cfg::Config, bool>,
    MemberPtr<paste_cfg::Config, int>,
    MemberPtr<paste_cfg::Config, std::string>>;

// struct a成员指针指向：`bool web_search_cfg::Config::*`→
// web_search_cfg::Config。 owner()就是由此衍生出来的。
template <class M> struct member_class;
template <class C, class T> struct member_class<T C::*> { using type = C; };
template <class M>
using member_class_t = typename member_class<M>::type;

struct SettingDef {
    std::string_view id;      // settings.json键AND表单字段ID("rag.mmr")
    std::string_view env;     // 旧版环境名称； "" = 无环境别名
    std::string_view label;   // UI
    std::string_view help;    // one-line说明
    Group            group;
    Tier             tier;
    Type             type;
    Slot             slot;

    // Real/Int.的界限否则被忽略。 ENFORCED — 超出
    // 范围的值会被限制在每个入口点（env、JSON、UI），
    // 因此配置永远无法保留该值。
    double           min = 0.0;
    double           max = 1.0;
    double           step = 0.05;   // Real:滑块粒度

    // 枚举选项，管道-separated ("convex|rrf")。
    // 解析一次到表单的 Choice 行；存储的值是选项字符串本身。
    std::string_view options;

    // `slot`指向哪个配置结构体。 派生 来自槽，
    // 而不是存储在它旁边：单独声明其所有者的行
    // 可能会错误地声明它，然后步行者将跳过可到达
    // 的行或访问不可访问的行。
    //
    // 键控在成员指针的CLASS上，而不是在每个（类、类型）对上：
    // 对于三个所有者，旧的 "Rag if one of these five, else Smart"
    // 链会默默地在 Smart 下归档每个搜索行。
    [[nodiscard]] constexpr Owner owner() const noexcept {
        return std::visit([]<class M>(M) constexpr {
            using C = member_class_t<M>;
            if constexpr (std::is_same_v<C, store::RagConfig>)
                return Owner::Rag;
            else if constexpr (std::is_same_v<C, smart::RoleConfig>)
                return Owner::Smart;
            else if constexpr (std::is_same_v<C, web_search_cfg::Config>)
                return Owner::WebSearch;
            else {
                static_assert(std::is_same_v<C, paste_cfg::Config>,
                              "a Slot alternative names a config struct "
                              "owner() does not know");
                return Owner::Paste;
            }
        }, slot);
    }
};

// ── 桌子上写着 ────────────────────────────────────────────────────────────
// 按组排序，然后按用户想要它的可能性排序。阅读这个 top
// -to-bottom 应该像设置屏幕一样阅读，
// 因为它 IS 设置屏幕。
// 尺寸为DEDUCED，未声明。 hand-maintained
// 计数是与行本身保持同步的另一件事 - 正是该表存在要删除的
// 重复项 - 错误的是 "too many initializers"
// 的墙而不是有用的消息。
inline constexpr std::array kSettings = std::to_array<SettingDef>({
    // ── 资料来源 ─────────────────────────────────────────────────────────
    {"rag.skills", "AGENTTY_RAG_SKILLS", "Index skills",
     "let retrieval search your installed skills",
     Group::Sources, Tier::Basic, Type::Bool, &store::RagConfig::skills},
    {"rag.memory", "AGENTTY_RAG_MEMORY", "Index memory",
     "let retrieval search remembered facts",
     Group::Sources, Tier::Basic, Type::Bool, &store::RagConfig::memory},
    {"rag.mcp_resources", "AGENTTY_RAG_MCP", "Index MCP resources",
     "also index resources exposed by MCP servers",
     Group::Sources, Tier::Basic, Type::Bool, &store::RagConfig::mcp_resources},

    // ── 管道 ────────────────────────────────────────────────────────
    {"rag.contextual", "AGENTTY_RAG_CONTEXTUAL", "Contextual chunks",
     "give each chunk a short situating preamble at index time",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::contextual},
    {"rag.mmr", "AGENTTY_RAG_MMR", "Diversity (MMR)",
     "spread results across the corpus instead of one dense cluster",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::mmr},
    {"rag.mmr_lambda", "AGENTTY_RAG_MMR_LAMBDA", "Diversity balance",
     "1.0 = pure relevance, 0.0 = pure diversity",
     Group::Pipeline, Tier::Advanced, Type::Real, &store::RagConfig::mmr_lambda,
     0.0, 1.0, 0.05},
    {"rag.dedup", "AGENTTY_RAG_DEDUP", "Fold duplicates",
     "collapse near-identical passages so the window isn't spent twice",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::dedup},
    {"rag.dedup_threshold", "AGENTTY_RAG_DEDUP_THRESHOLD", "Duplicate threshold",
     "similarity above which two passages count as the same",
     Group::Pipeline, Tier::Advanced, Type::Real, &store::RagConfig::dedup_threshold,
     0.5, 1.0, 0.01},
    {"rag.autocut", "AGENTTY_RAG_AUTOCUT", "Trim weak tail",
     "drop the low-relevance tail at the score knee",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::autocut},
    {"rag.autocut_sensitivity", "AGENTTY_RAG_AUTOCUT_SENS", "Trim aggressiveness",
     "lower cuts more; higher keeps more of the tail",
     Group::Pipeline, Tier::Advanced, Type::Real, &store::RagConfig::autocut_sensitivity,
     0.5, 5.0, 0.1},
    {"rag.stitch", "AGENTTY_RAG_STITCH", "Stitch neighbours",
     "join adjacent chunks so a passage isn't cut mid-thought",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::stitch},

    // 电源模式：每种模式都会产生延迟或型号 round-trips，
    // 因此每种模式默认关闭并说明其成本。
    {"rag.expand", "AGENTTY_RAG_EXPAND", "Multi-query",
     "ask the model for query variants — better recall, one extra round trip",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::expand},
    {"rag.hyde", "AGENTTY_RAG_HYDE", "HyDE",
     "search with a hypothetical answer — helps vague questions, costs a round trip",
     Group::Pipeline, Tier::Basic, Type::Bool, &store::RagConfig::hyde},
    {"rag.prf", "AGENTTY_RAG_PRF", "Feedback expansion",
     "re-query using the top hits' terms; can drift off-topic",
     Group::Pipeline, Tier::Advanced, Type::Bool, &store::RagConfig::prf},
    {"rag.corrective", "AGENTTY_RAG_CORRECT", "Corrective grading",
     "grade and re-retrieve weak results; lexical proxy can reject good matches",
     Group::Pipeline, Tier::Advanced, Type::Bool, &store::RagConfig::corrective},
    {"rag.graph", "AGENTTY_RAG_GRAPH", "Graph expansion",
     "follow entity links between chunks; quadratic build on large corpora",
     Group::Pipeline, Tier::Advanced, Type::Bool, &store::RagConfig::graph},

    // ── 组合 ──────────────────────────────────────────────────────────
    {"rag.fusion", "AGENTTY_RAG_FUSION", "Fusion",
     "how keyword and semantic scores combine",
     Group::Fusion, Tier::Basic, Type::Enum, &store::RagConfig::fusion,
     0.0, 0.0, 0.0, "convex|rrf"},
    {"rag.adaptive_fusion", "AGENTTY_RAG_ADAPTIVE", "Adaptive weighting",
     "lean on whichever retriever is more confident per query (convex only)",
     Group::Fusion, Tier::Basic, Type::Bool, &store::RagConfig::adaptive_fusion},
    {"rag.dense_weight", "AGENTTY_RAG_DENSE_WEIGHT", "Semantic weight",
     "weight of embedding search in RRF mode",
     Group::Fusion, Tier::Advanced, Type::Real, &store::RagConfig::dense_weight,
     0.0, 3.0, 0.1},
    {"rag.bm25_weight", "AGENTTY_RAG_BM25_WEIGHT", "Keyword weight",
     "weight of keyword search in RRF mode",
     Group::Fusion, Tier::Advanced, Type::Real, &store::RagConfig::bm25_weight,
     0.0, 3.0, 0.1},

    // ── 基础设施 ──────────────────────────────────────────────────
    {"rag.persist", "AGENTTY_RAG_PERSIST", "Cache index",
     "keep the built index under .agentty/ so restarts are warm",
     Group::Infra, Tier::Basic, Type::Bool, &store::RagConfig::persist},
    {"rag.trace", "AGENTTY_RAG_TRACE", "Trace stages",
     "fold per-stage timings into the retrieval label",
     Group::Infra, Tier::Advanced, Type::Bool, &store::RagConfig::trace},

    // ── 运行( 智能模式) ─────────────────────────────────────
    // 智能模式的FEATURE表面(主开关和三个型号插槽)是
    // Ctrl+S 覆盖面板——这些是关于WHICH MODEL的选择，
    // 并且属于型号选择面板的旁边。这三个是其数字 POLICY，
    // 它们在这里的原因与 RAG 调整行的原因相同：它们仅作为
    // 环境变量存在，这使得它们不可发现、session
    // -only 且未经验证。
    //
    // 所有高级。 "At what feature score does a turn become Complex"
    // 不是一个用户可以冷酷回答的问题，但它是 IS 支出拨号，因此它必须是可访问的，
    // 而不是一个 source-code 常量。
    {"smart.complex_threshold", "AGENTTY_SMART_COMPLEX_THRESHOLD",
     "Complexity cut",
     "feature score at which a turn routes as Complex; lower spends more",
     Group::Routing, Tier::Advanced, Type::Int,
     &smart::RoleConfig::complex_threshold,
     smart::tuning::kComplexMin, smart::tuning::kComplexMax, 1},
    {"smart.deep_margin", "AGENTTY_SMART_DEEP_MARGIN", "Deep-band margin",
     "how far into a tier a turn must sit to earn the extra effort step",
     Group::Routing, Tier::Advanced, Type::Int,
     &smart::RoleConfig::deep_margin,
     smart::tuning::kDeepMarginMin, smart::tuning::kDeepMarginMax, 1},
    {"smart.bias_clamp", "AGENTTY_SMART_BIAS_CLAMP", "Self-correction cap",
     "how far this session's own corrections may drift effort from baseline",
     Group::Routing, Tier::Advanced, Type::Int,
     &smart::RoleConfig::bias_clamp,
     smart::tuning::kBiasClampMin, smart::tuning::kBiasClampMax, 1},

    // 这两行决定智能模式保存是否给你钱。
    //
    // 基础层，与上面三个不同：
    // 只有一个词的请求是否也要使用最昂贵模型？
    // IS是用户可以冷酷回答的问题，这就是智能模式工作和智能模式
    // 装饰之间的区别。在这些存在之前，主要转弯被固定到 分派 路径中的战略，
    // 无法更改它 - 用户的 577-turn 跟踪有 142 个
    // trivial/simple 转弯，全部按旗舰费率计费。
    {"smart.route_main_turn", "AGENTTY_SMART_ROUTE_MAIN", "Route the main turn",
     "let a turn's complexity choose its model, not just its effort",
     Group::Routing, Tier::Basic, Type::Bool,
     &smart::RoleConfig::route_main_turn},
    {"smart.main_turn_floor", "AGENTTY_SMART_MAIN_FLOOR", "Cheapest main-turn role",
     "how far down a simple turn may be routed; strategic = never route down",
     Group::Routing, Tier::Basic, Type::Enum,
     &smart::RoleConfig::main_turn_floor,
     0.0, 0.0, 0.0, "utility|implementation|strategic"},

    // ── web_search(安排 ) – 网络搜索(Web搜索) ─────────────────────────────
    // 应用于两个接口边界 agentty拥有——传输广告和
    // 分派关闭——从未在实现该工具的mcp-cpp内部应用。
    // 参见domain/web_search_config.hpp了解其达到和未达到的内容。
    {"web_search.mode", "AGENTTY_WEB_SEARCH", "Web search",
     "auto: the model decides the count, as before \xc2\xb7 on: your counts apply \xc2\xb7 off: no web search",
     Group::WebSearch, Tier::Basic, Type::Enum, &web_search_cfg::Config::mode_text,
     0.0, 0.0, 0.0, "auto|on|off"},
    {"web_search.count", "AGENTTY_WEB_SEARCH_COUNT", "Results per search",
     "how many results a search returns when the model does not say",
     Group::WebSearch, Tier::Basic, Type::Int, &web_search_cfg::Config::count,
     static_cast<double>(web_search_cfg::kCountMin),
     static_cast<double>(web_search_cfg::kCountMax), 1.0},
    {"web_search.max_count", "AGENTTY_WEB_SEARCH_MAX", "Most results allowed",
     "ceiling on what the model may ask for in one search",
     Group::WebSearch, Tier::Basic, Type::Int, &web_search_cfg::Config::max_count,
     static_cast<double>(web_search_cfg::kMaxCountMin),
     static_cast<double>(web_search_cfg::kMaxCountMax), 1.0},
    {"web_search.exclude_sites", "AGENTTY_WEB_SEARCH_EXCLUDE", "Never return",
     "domains to leave out, separated by spaces or commas: pinterest.com w3schools.com",
     Group::WebSearch, Tier::Basic, Type::Text, &web_search_cfg::Config::exclude_sites},

    // 哪个服务回答，先成功者获胜。选项为mcp-cpp的目录id（
    // mcp/tools/web_search.hpp）； 面板 显示其标签。
    // tests/web_search_services_test.cpp将该列表固定到目录。
    {"web_search.primary", "AGENTTY_WEB_SEARCH_PRIMARY", "Primary",
     "asked first. Free needs no account; API key needs one, stored encrypted",
     Group::WebSearchServices, Tier::Basic, Type::Enum, &web_search_cfg::Config::primary,
     0.0, 0.0, 0.0, web_search_cfg::kServiceIds},
    {"web_search.secondary", "AGENTTY_WEB_SEARCH_SECONDARY", "Secondary",
     "asked when the primary fails or is blocked",
     Group::WebSearchServices, Tier::Basic, Type::Enum, &web_search_cfg::Config::secondary,
     0.0, 0.0, 0.0, web_search_cfg::kOptionalServiceIds},
    {"web_search.fallback", "AGENTTY_WEB_SEARCH_FALLBACK", "Fallback",
     "asked last, when both of the above fail",
     Group::WebSearchServices, Tier::Basic, Type::Enum, &web_search_cfg::Config::fallback,
     0.0, 0.0, 0.0, web_search_cfg::kOptionalServiceIds},
    {"web_search.language", "AGENTTY_WEB_SEARCH_LANGUAGE", "Language",
     "result language for the services that take one; auto leaves it to them",
     Group::WebSearchServices, Tier::Basic, Type::Enum, &web_search_cfg::Config::language,
     0.0, 0.0, 0.0, web_search_cfg::kLanguages},

    // ── 粘贴( 安排 ) – 粘贴) ─────────────────────────────────────────────
    // Ctrl+V和`agentty paste`如何读取剪贴板(richpaste)。 io/clipboard.cpp 阅读；
    // 参见 domain/paste_config.hpp.
    {"paste.forwards", "AGENTTY_PASTE_FORWARDS", "Laptop over ssh",
     "over ssh, read your laptop's clipboard through `agentty paste setup`",
     Group::PasteRoutes, Tier::Basic, Type::Bool, &paste_cfg::Config::forwards},
    {"paste.display", "AGENTTY_PASTE_DISPLAY", "This machine",
     "read this machine's clipboard: Wayland, X11, macOS or Windows",
     Group::PasteRoutes, Tier::Basic, Type::Bool, &paste_cfg::Config::display},
    {"paste.terminal", "AGENTTY_PASTE_TERMINAL", "Ask the terminal",
     "auto: OSC 5522 (images) if the terminal has it, else OSC 52 (text) \xc2\xb7 off: never ask",
     Group::PasteRoutes, Tier::Basic, Type::Enum, &paste_cfg::Config::terminal,
     0.0, 0.0, 0.0, paste_cfg::kTerminalModes},
    {"paste.server_display", "AGENTTY_PASTE_SERVER_CLIPBOARD", "Server's clipboard over ssh",
     "over ssh, fall back to the server's own clipboard (not the one you copied into)",
     Group::PasteRoutes, Tier::Advanced, Type::Bool, &paste_cfg::Config::server_display},
    {"paste.decode_text", "AGENTTY_PASTE_DECODE_TEXT", "Images pasted as text",
     "turn a pasted richpaste:1: line back into the image or file",
     Group::PasteFiles, Tier::Basic, Type::Bool, &paste_cfg::Config::decode_text},
    {"paste.max_mb", "AGENTTY_PASTE_MAX_MB", "Largest paste (MB)",
     "bigger clipboard contents are refused instead of read",
     Group::PasteFiles, Tier::Basic, Type::Int, &paste_cfg::Config::max_mb,
     static_cast<double>(paste_cfg::kMaxMbMin), static_cast<double>(paste_cfg::kMaxMbMax), 1.0},
    {"paste.inline_kb", "AGENTTY_PASTE_INLINE_KB", "Text file as text up to (KB)",
     "a copied text file this small goes in as its text; bigger ones are saved \xc2\xb7 0: always save",
     Group::PasteFiles, Tier::Basic, Type::Int, &paste_cfg::Config::inline_kb,
     static_cast<double>(paste_cfg::kInlineKbMin), static_cast<double>(paste_cfg::kInlineKbMax), 1.0},
    {"paste.save_dir", "AGENTTY_PASTE_SAVE_DIR", "Save pasted files in",
     "empty: ~/.agentty/paste/pasted",
     Group::PasteFiles, Tier::Advanced, Type::Text, &paste_cfg::Config::save_dir},
    {"paste.socket", "AGENTTY_PASTE_SOCKET", "Only this agent socket",
     "ask just this agent (a relay, a test); empty: find the forwarded ones",
     Group::PasteExpert, Tier::Advanced, Type::Text, &paste_cfg::Config::socket},
});

inline constexpr int kCount = static_cast<int>(kSettings.size());

// ── 查询 ───────────────────────────────────────────────────────────────
[[nodiscard]] constexpr const SettingDef* find(std::string_view id) noexcept {
    for (const auto& s : kSettings)
        if (s.id == id) return &s;
    return nullptr;
}

// ── 编译-time 证明 ──────────────────────────────────────────────────
// 读者不必相信该表是well-formed；
// 检查 不变量 的写入位置。
namespace proofs {

consteval bool ids_unique() {
    for (std::size_t i = 0; i < kSettings.size(); ++i)
        for (std::size_t j = i + 1; j < kSettings.size(); ++j)
            if (kSettings[i].id == kSettings[j].id) return false;
    return true;
}

consteval bool envs_unique() {
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
        if (kSettings[i].env.empty()) continue;
        for (std::size_t j = i + 1; j < kSettings.size(); ++j)
            if (kSettings[i].env == kSettings[j].env) return false;
    }
    return true;
}

consteval bool ranges_sane() {
    for (const auto& s : kSettings) {
        if (s.type != Type::Real && s.type != Type::Int) continue;
        if (!(s.min < s.max)) return false;
        if (s.step <= 0.0) return false;
    }
    return true;
}

// 没有选项的枚举行是其中没有任何内容的下拉列表。
consteval bool enums_have_options() {
    for (const auto& s : kSettings)
        if (s.type == Type::Enum && s.options.empty()) return false;
    return true;
}

// 槽的类型必须与声明的类型相匹配，否则遍历器将写入错误的成员
// 并悄悄地破坏不相关的设置。对 两者 所有者进行了检查，
// 因此 Settings-owned 行与 RagConfig-owned
// 行保持相同的标准。
consteval bool slots_match_types() {
    for (const auto& s : kSettings) {
        switch (s.type) {
            case Type::Bool:
                if (!std::holds_alternative<MemberPtr<store::RagConfig, bool>>(s.slot)
                 && !std::holds_alternative<MemberPtr<smart::RoleConfig, bool>>(s.slot)
                 && !std::holds_alternative<MemberPtr<web_search_cfg::Config, bool>>(s.slot)
                 && !std::holds_alternative<MemberPtr<paste_cfg::Config, bool>>(s.slot))
                    return false;
                break;
            case Type::Real:
                if (!std::holds_alternative<MemberPtr<store::RagConfig, float>>(s.slot)
                 && !std::holds_alternative<MemberPtr<store::RagConfig, double>>(s.slot)
                 && !std::holds_alternative<MemberPtr<smart::RoleConfig, float>>(s.slot)
                 && !std::holds_alternative<MemberPtr<smart::RoleConfig, double>>(s.slot))
                    return false;
                break;
            case Type::Int:
                if (!std::holds_alternative<MemberPtr<store::RagConfig, int>>(s.slot)
                 && !std::holds_alternative<MemberPtr<smart::RoleConfig, int>>(s.slot)
                 && !std::holds_alternative<MemberPtr<web_search_cfg::Config, int>>(s.slot)
                 && !std::holds_alternative<MemberPtr<paste_cfg::Config, int>>(s.slot))
                    return false;
                break;
            case Type::Enum:
                if (!std::holds_alternative<MemberPtr<store::RagConfig, std::string>>(s.slot)
                 && !std::holds_alternative<MemberPtr<smart::RoleConfig, std::string>>(s.slot)
                 && !std::holds_alternative<MemberPtr<web_search_cfg::Config, std::string>>(s.slot)
                 && !std::holds_alternative<MemberPtr<paste_cfg::Config, std::string>>(s.slot))
                    return false;
                break;
            // 自由文本像枚举一样绑定字符串，
            // 但不携带验证选项——因此它保留给需要它的所有者
            // （排除列表、路径），而不是提供给每个字符串槽。
            case Type::Text:
                if (!std::holds_alternative<MemberPtr<web_search_cfg::Config, std::string>>(s.slot)
                 && !std::holds_alternative<MemberPtr<paste_cfg::Config, std::string>>(s.slot))
                    return false;
                if (!s.options.empty()) return false;
                break;
        }
    }
    return true;
}

// 每个id都是命名空间的，因此settings.json键不能与不
// 相关的top-level字段冲突——并且PREFIX必须与槽指向的所有者一致。
// 后半部分是通过读取 id 让 "which struct holds this"
// 负责的原因：写入 store::Settings 的名为
// "rag.…" 的行将是步行者无法捕捉到的谎言，因为每个步行
// 者只能看到它可以到达的行。
consteval bool ids_namespaced() {
    for (const auto& s : kSettings) {
        const bool rag    = s.id.starts_with("rag.");
        const bool smart  = s.id.starts_with("smart.");
        const bool web    = s.id.starts_with("web_search.");
        const bool paste  = s.id.starts_with("paste.");
        if (!rag && !smart && !web && !paste) return false;
        if (rag    && s.owner() != Owner::Rag)      return false;
        if (smart  && s.owner() != Owner::Smart)    return false;
        if (web    && s.owner() != Owner::WebSearch) return false;
        if (paste  && s.owner() != Owner::Paste)    return false;
    }
    return true;
}

// 注册表和smart::tuning都命名了出厂默认值和范围。
// 调音是源；这些行反映了它。将它们固定在一起，
// 这样对一个的更改就是 编译 错误，而不是 UI 所钳制的
// 内容和裸环境读取所钳制的内容之间无声的分歧。
consteval bool smart_rows_match_tuning() {
    for (const auto& s : kSettings) {
        if (s.id == "smart.complex_threshold")
            if (s.min != smart::tuning::kComplexMin
             || s.max != smart::tuning::kComplexMax) return false;
        if (s.id == "smart.deep_margin")
            if (s.min != smart::tuning::kDeepMarginMin
             || s.max != smart::tuning::kDeepMarginMax) return false;
        if (s.id == "smart.bias_clamp")
            if (s.min != smart::tuning::kBiasClampMin
             || s.max != smart::tuning::kBiasClampMax) return false;
    }
    return true;
}

// 搜索行的边界是web_search_cfg的常数，
// 并且工具层在分派处钳位到相同的常数。将两者固定在一起，
// 这样一行就不能公布 分派 接口边界 不同意的范围。
consteval bool web_search_rows_match_config() {
    for (const auto& s : kSettings) {
        if (s.id == "web_search.count")
            if (s.min != web_search_cfg::kCountMin
             || s.max != web_search_cfg::kCountMax) return false;
        if (s.id == "web_search.max_count")
            if (s.min != web_search_cfg::kMaxCountMin
             || s.max != web_search_cfg::kMaxCountMax) return false;
    }
    return true;
}

// 粘贴行的边界是paste_cfg的常数，
// 并且io/clipboard.cpp钳位到相同的常数。将它们钉在一起。
consteval bool paste_rows_match_config() {
    for (const auto& s : kSettings) {
        if (s.id == "paste.max_mb")
            if (s.min != paste_cfg::kMaxMbMin || s.max != paste_cfg::kMaxMbMax) return false;
        if (s.id == "paste.inline_kb")
            if (s.min != paste_cfg::kInlineKbMin || s.max != paste_cfg::kInlineKbMax) return false;
    }
    return true;
}

static_assert(ids_unique(),        "duplicate setting id");
static_assert(envs_unique(),       "duplicate env var name");
static_assert(ranges_sane(),       "a numeric row has an empty or inverted range");
static_assert(enums_have_options(),"an Enum row has no options");
static_assert(slots_match_types(), "a row's slot type disagrees with its Type");
static_assert(ids_namespaced(),    "setting ids must be namespaced, and the "
                                  "prefix must match the slot's owner");
static_assert(smart_rows_match_tuning(),
              "a smart.* row's range disagrees with smart::tuning");
static_assert(web_search_rows_match_config(),
              "a web_search.* row's range disagrees with web_search_cfg");
static_assert(paste_rows_match_config(),
              "a paste.* row's range disagrees with paste_cfg");

} // namespace proofs

// ── 衍生业务(settings_registry.cpp) ───────────────────────────
// 每个人都走在桌子上。他们都没有指定单独的设置。
//
// 每个访问器对于每个所有者来说都是过载的。一行只能通过
// 所绑定的结构体，因此传错对象会在编译时被发现，避免
// 一个无声的no-op——每个步行者自然有SKIPS行它无法到达。

// 注册表的环境覆盖，读取一次。环境变量在 exec 处是固定的，
// 因此 状态更新函数 或视图从 Model::env 获取它，而不是调用 getenv。
struct EnvSnapshot {
    // (变量，值)对于其变量被设置且非-empty的每一行。
    std::vector<std::pair<std::string, std::string>> set;
    // AGENTTY_SMART_MODE：主交换机上的会话引脚。
    std::optional<bool> smart_enabled;
    // AGENTTY_SMART_NO_INTERNAL/_NO_ORCHESTRATE/_NO_SUBAGENTS。
    bool smart_no_internal    = false;
    bool smart_no_orchestrate = false;
    bool smart_no_subagents   = false;

    [[nodiscard]] const std::string* find(std::string_view var) const noexcept {
        for (const auto& [k, v] : set) if (k == var) return &v;
        return nullptr;
    }
};

// 读取进程环境。
[[nodiscard]] EnvSnapshot read_env(Io);

// 在`c`之上应用环境覆盖。钳位到每行的范围。
void apply_env(store::RagConfig& c, const EnvSnapshot& env);
void apply_env(smart::RoleConfig& c, const EnvSnapshot& env);
void apply_env(web_search_cfg::Config& c, const EnvSnapshot& env);
void apply_env(paste_cfg::Config& c, const EnvSnapshot& env);
// 同样，正在读取现在的环境。对于没有 Model 的 CLI 路径。
void apply_env(Io, store::RagConfig& c);
void apply_env(Io, smart::RoleConfig& c);
void apply_env(Io, web_search_cfg::Config& c);
void apply_env(Io, paste_cfg::Config& c);

// Read/write 行的值作为字符串——形状 `agentty config get/set`
// 和 JSON 步行者都想要。对于未知的 id 或该行无法容纳的值，
// 返回 false。
[[nodiscard]] std::string get(const store::RagConfig& c, const SettingDef& d);
[[nodiscard]] std::string get(const smart::RoleConfig& c, const SettingDef& d);
[[nodiscard]] std::string get(const web_search_cfg::Config& c, const SettingDef& d);
[[nodiscard]] std::string get(const paste_cfg::Config& c, const SettingDef& d);
[[nodiscard]] bool        set(store::RagConfig& c, const SettingDef& d,
                              std::string_view value);
[[nodiscard]] bool        set(smart::RoleConfig& c, const SettingDef& d,
                              std::string_view value);
[[nodiscard]] bool        set(web_search_cfg::Config& c, const SettingDef& d,
                              std::string_view value);
[[nodiscard]] bool        set(paste_cfg::Config& c, const SettingDef& d,
                              std::string_view value);

// 当该行仍然保持其出厂默认值时为真——用于保持 settings.json
// 干净（仅写入非 -default 行）。
[[nodiscard]] bool is_default(const store::RagConfig& c, const SettingDef& d);
[[nodiscard]] bool is_default(const smart::RoleConfig& c, const SettingDef& d);
[[nodiscard]] bool is_default(const web_search_cfg::Config& c, const SettingDef& d);
[[nodiscard]] bool is_default(const paste_cfg::Config& c, const SettingDef& d);

// 该行现在的环境变量是 OVERRIDING，或者当没有设置时是 ""。
// 覆盖渲染 LOCKED下的行并对变量进行命名，
// 而不是看起来可编辑并在下次读取时默默地丢失编辑。
[[nodiscard]] std::string env_override(const SettingDef& d, const EnvSnapshot& env);

// 将行恢复为其出厂默认值。
void reset(store::RagConfig& c, const SettingDef& d);
void reset(smart::RoleConfig& s, const SettingDef& d);
void reset(web_search_cfg::Config& c, const SettingDef& d);
void reset(paste_cfg::Config& c, const SettingDef& d);

// 一行：标签、帮助、控制类型、范围、环境锁和出处，全部来自表。
// 从 add_rows 中拆分出一个窗格，该窗格必须将自己的行
// BETWEEN 注册表行（Web 搜索将每个服务的拨号盘挂在其插槽下）。
template <class C>
inline void add_row(form::Builder& b, const C& cfg, const EnvSnapshot& env, const SettingDef& d) {
    {   // （范围保持在 add_rows' 循环 正文 范围内，因此差异保持移动）
        const std::string id{d.id};
        const std::string label{d.label};
        const std::string help{d.help};

        switch (d.type) {
            case Type::Bool:
                b.toggle(id, label, get(cfg, d) == "true", help);
                break;
            case Type::Real: {
                double v = d.min;
                try { v = std::stod(get(cfg, d)); } catch (...) {}
                b.slider(id, label, v, d.min, d.max, d.step, help);
                break;
            }
            case Type::Int: {
                long long v = 0;
                try { v = std::stoll(get(cfg, d)); } catch (...) {}
                b.number(id, label, v, static_cast<std::int64_t>(d.min),
                         static_cast<std::int64_t>(d.max), help);
                break;
            }
            case Type::Enum: {
                std::vector<std::string> opts;
                std::string_view rest = d.options;
                while (!rest.empty()) {
                    const auto bar = rest.find('|');
                    opts.emplace_back(rest.substr(0, bar));
                    if (bar == std::string_view::npos) break;
                    rest.remove_prefix(bar + 1);
                }
                b.choice(id, label, opts, {}, get(cfg, d), help);
                break;
            }
            case Type::Text:
                // 自由文本：该值是用户自己的逐字字符串。
                // 标准化发生在USED (web_search_cfg)处，
                // 因此该行准确地显示了带类型的。
                b.text(id, label, get(cfg, d), help);
                break;
        }

        // 覆盖该行的环境变量使其成为 READ-ONLY 并命名该变量。
        // 否则，该行看起来是可编辑的，并且在下一次读取时默默
        // 地丢失编辑 - 分层配置最严重的失败，
        // 以及 `origin` 存在的原因。
        if (const std::string var = env_override(d, env); !var.empty()) {
            b.origin("env: " + var);
            b.lock("env: " + var);
        }
        // 出处：其发货价值上的一行如此说明，这是"I never touched this"
        // 和"I set it to that"之间的差异。
        else if (is_default(cfg, d)) b.origin("default");
    }
}

// ── 行数 ───────────────────────────────────────────────────
//
// add_row的逆：通过表将表单行的值写回到`cfg`中。
// 每个 面板 都通过此提交，因此字段类型会在一个位置转换
// 为设置字符串。会跳过锁定的行（env 覆盖），
// 因此 shell 导出永远不会被覆盖。
template <class C>
inline bool read_row(C& cfg, const SettingDef& d, const form::Field& row) {
    if (row.locked) return false;
    return std::visit([&](const auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, form::field::Toggle>)
            return set(cfg, d, v.on ? "true" : "false");
        else if constexpr (std::is_same_v<T, form::field::Number>)
            return set(cfg, d, std::to_string(form::number_value(v)));
        else if constexpr (std::is_same_v<T, form::field::Slider>)
            return set(cfg, d, std::to_string(v.value));
        else if constexpr (std::is_same_v<T, form::field::Choice>)
            return set(cfg, d, std::string{v.id()});
        else if constexpr (std::is_same_v<T, form::field::Text>)
            return set(cfg, d, v.value);
        else
            return false;
    }, row.value);
}

// Write表格将`owner`的每一行保留成`cfg`。
template <class C>
inline void read_rows(C& cfg, const form::Form& f, Owner owner) {
    for (const auto& d : kSettings) {
        if (d.owner() != owner) continue;
        if (const auto* row = f.find(d.id)) (void)read_row(cfg, d, *row);
    }
}

// 将该表的行投影到表单上。 该 方式的 面板 渲染 设置 — 标签、
// 帮助、控制类型、范围、组 头文件、环境 锁 和出处都来自该行，
// 因此 面板 仅贡献其拥有的 对应的 行。
//
// 这存在于此处而不是存在于一个 面板 的 .cpp 中，因为两个 面板 都需要它。
// 它对于RAG 面板来说是私有的，因此智能模式面板将
// -rolled改为它的三行：标签、帮助字符串和从它们已经
// 声明的表中重新键入的范围，
// 并且env-lock/provenance处理悄悄地重新实现并且略有不同。
// 两个面板 渲染设置两种方式正是将表存在的重复项删除。
//
// `first`和`last_group` 线程穿过调用，因此当面板行走
// 超过一个所有者时，组头文件保持正确。必须放置其自己的 BETWEEN
// 注册表 行的 面板 会遍历表本身和 调用 add_row（上图）。
template <class C>
inline void add_rows(form::Builder& b, const C& cfg, const EnvSnapshot& env,
                     Owner owner, bool advanced, bool& first, Group& last_group) {
    for (const auto& d : kSettings) {
        if (d.owner() != owner) continue;

        // 高级行被隐藏，而不是被禁用：用户无法判断其副作用的
        // 旋钮是主屏幕上的噪音，但它仍然必须是可到达的（
        // `a`)而不是env-only。
        if (d.tier == Tier::Advanced && !advanced) continue;

        // 组头文件分隔各部分。它是一种真正的字段类型，
        // 而不是锁定的文本行：伪造它会将占位符泄漏到值列中，
        // 并使光标落在不执行任何操作的标签上。
        if (first || d.group != last_group) {
            b.header(std::string{label_of(d.group)});
            last_group = d.group;
            first = false;
        }
        add_row(b, cfg, env, d);
    }
}

} // namespace agentty::settings::registry
