#pragma once
// agentty::web_search_cfg-`web_search`工具的用户策略。
//
// ── 东道主决定什么 ────────────────────────────────────────────────
//
// web_search在mcp-cpp (third_party/mcp-cpp/src/tools/web.cpp)中实现：
// 服务目录、每个服务的请求和回复格式、HTML、
// 解析器和重复数据删除都存在于库、单独的仓库中。
// agentty拥有CHOICES并在三点接口边界移交它们：
//
//   *广告-tools::plan决定传输上发生什么
//   * 分派 — 桥的执行闭包重写了 调用 的参数
//                     (计数、排除)
//   *计划-tools::web_search_plan转动槽位、语言和per-
//                     服务拨打下面的 WebSearchPlan 后面是 mcp-cpp，
//                     每次搜索时请重新阅读
//
// API键不是该类型的一部分。它们是秘密的，因此它们存在于
// tool/web_search_secret.hpp（密钥库，否则是密封文件）中，
// 并且仅在构建计划时才加入到计划中；这里没有任何东西可以带人进入settings.json。
//
// ── 为什么这些论点被改写,而不是结果被过滤 ────────────
//
// 事后过滤结果需要解析的命中，只有mcp-cpp有。
// 重写请求只需要参数，引擎就永远不会返回被阻止的域，
// 因此没有什么可以过滤的——并且它用其他结果填充释放的槽，
// 这是 post-filtering 无法做到
// 的。
//
// 关于`-site:`支持已知和未知的内容：DuckDuckGo记录了它；
// Brave 尊重它（实时检查：返回 10 个 pinterest.com 命中的
// 查询不会返回任何 `-site:pinterest.com`；Brave 自己的
// 文档拼写运算符 `NOT site:`）。具有本机域过滤器（Tavily、
// Firecrawl、Exa、Perplexity）的服务将列表作为请求字段接收，
// mcp-cpp 通过将 `-site:` 令牌从查询中取出（最多 20 个；
// 更多保留在查询中）来实现。
//
// 纯粹：没有IO，没有全局变量。工具层安装一个该类型的值（
// tool/web_search_policy.hpp）； 运行时 像其他旋
// 钮一样通过设置 注册表 保持它。

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentty::web_search_cfg {

// 将注册表夹具限制到，并且将工具层re-clamps限制到。
// 此处命名是为了使表行和夹具永远不会不一致。
//
// 搜索实际上可以对每个服务的RETURN进行上限：mcp
// -cpp询问回答一个结果页的服务并将计数限制到该服务记录的最大值(
// WebSearchService::max_count:对于Serply为10，
// 对于Brave、Tavily、Serper和Perplexity为20，
// 对于DuckDuckGo为30，
// 对于Firecrawl和Exa为100)。因此，高于服务上限的上限被接受并正确限制，
// 但产生的收益不会超过该服务提供的值。上限是一个限制，而不是一个承诺。
inline constexpr int kCountMin       = 1;
inline constexpr int kCountMax       = 20;
inline constexpr int kCountDefault   = 10;   // ==mcp-cpp自己的默认值
inline constexpr int kMaxCountMin    = 1;
inline constexpr int kMaxCountMax    = 50;
inline constexpr int kMaxCountDefault = 20;

// 槽可以命名的服务：mcp-cpp的目录的id，
// 按菜单顺序（首先是免费，然后是API键）。这里拼写出来是
// 因为注册表需要编译时间的列表，而目录是运行时的值；
// 如果两者不一致，测试就会失败。主不能为空；另外两个可以。
inline constexpr std::string_view kServiceIds =
    "brave-free|ddg-free|tavily-free|firecrawl-free|"
    "brave-api|tavily-api|firecrawl-api|exa-api|serper-api|serply-api|"
    "perplexity-api";
inline constexpr std::string_view kOptionalServiceIds =
    "none|brave-free|ddg-free|tavily-free|firecrawl-free|"
    "brave-api|tavily-api|firecrawl-api|exa-api|serper-api|serply-api|"
    "perplexity-api";
// ISO 639-1，键控服务文档支持的语言。
inline constexpr std::string_view kLanguages =
    "auto|en|de|fr|es|it|pt|nl|sv|da|fi|pl|cs|ru|uk|tr|ja|ko|zh|ar|he|hi";

// web_search开关。 `Auto` 是 agentty 在
// 面板 存在之前的行为方式：该工具被宣传，MODEL 决定要请求多少
// 结果（mcp-cpp 自己的默认值是 10，当它没有说明时）。
// 主机上的任何东西（回合计数、智能模式工作）都无法满足这个数字。
// `On`另外应用用户设置的计数作为默认值和上限。
// `Off` 从 传输 上取下工具，并拒绝 调用 使用它。
// 排除同样适用于“自动”和“开启”。
enum class Mode : std::uint8_t { Auto, On, Off };

struct Config {
    // 存储为行的文本("auto" | "on" | "off")，因此注册表的
    // 枚举类型可以容纳它； mode() 是每个读者都使用的 带类型的 视图。
    std::string mode_text = "auto";

    // 当模型不要求数字时的结果。仅适用于 On。
    int count = kCountDefault;

    // 模型可能要求的上限。仅适用于 On。
    // 还捕获不会返回任何结果的退化请求（0，负数）。
    int max_count = kMaxCountDefault;

    // 域永远不会返回，空格或逗号-separated为带类型的 (
    // "pinterest.com, w3schools.com")。保留为用户的文本，而不是解析的列表，
    // 因此 面板 准确地显示了 带类型的 的内容，而 settings.json 则保留在
    // -editable 中； exclude_list() 是解析后的视图。
    std::string exclude_sites;

    // web_search尝试哪些服务，顺序为：第一个回答者获胜。
    // 存储为目录的服务id("brave-free"，"exa-api")；
    // 目录本身位于 mcp-cpp (mcp/tools/web_search.hpp)，
    // 因此此域类型仅包含 ID。 "none" 将插槽
    // 留空（主要不能是：无法运行没有服务的搜索）。默认不需要账户、
    // 卡或钥匙。
    std::string primary   = "brave-free";
    std::string secondary = "ddg-free";
    std::string fallback  = "tavily-free";

    // ISO 639-1 代码发送给各服务取一，或者"auto"
    // 为每个服务自己的默认值。无法接受某种语言的服务会忽略它。
    std::string language = "auto";

    // Per-SERVICE额外设置(新鲜度、区域……)、
    // 键控"<service-id>.<dial-id>"->值ID、
    // 或用于free-text表盘(Brave Goggle URL)
    // 的带类型的文本。每个服务而不是每个插槽，因此在尝试将其作为主要保留后选择
    // Exa 为 回退 保留设置。仅存储非 -default 值。此处不检查值：
    // mcp-cpp仅当其通过web_search_dial_value_ok时才发送值。
    std::map<std::string, std::string> dials;

    [[nodiscard]] bool operator==(const Config&) const = default;
};

// 存储的拨号条目必须是这样的：键"<service>.<dial>"和值，
// 两者都是可打印的，键短和kMaxDialValueBytes内的值。
// 应用在保存上的加载 AND上，
// 因此-edited settings.json不能握住面板不能产生的东西，
// 并且垃圾钥匙不能将真正的钥匙从盖子中挤出。
inline constexpr std::size_t kMaxDials = 64;
// 房间最长的空闲-text拨盘mcp-cpp声明：一个2048-byte护目镜URL。
inline constexpr std::size_t kMaxDialValueBytes = 2048;
[[nodiscard]] bool dial_entry_ok(std::string_view key, std::string_view value) noexcept;

// Config::mode_text的带类型的视图。任何无法识别的内容
// 都会读取为“自动”——注册表 只会写入这三个单词，而 hand-edited
// 拼写错误不得意外关闭或打开工具。
[[nodiscard]] Mode mode(const Config& c);

// 解析的、规范化的排除列表：lower-cased、
// 方案/"www."/路径/端口被剥离、清空和重复项被丢弃、
// 任何不是合理主机的被拒绝。顺序已保留，因此重写的查询对于
// 给定设置是稳定的（因此 缓存-friendly 上游）。
[[nodiscard]] std::vector<std::string> exclude_list(std::string_view text);

// 请求的有效结果计数：当模型给出可用整数时，模型自己的`count`，
// 否则默认，总是钳位到[1，
// max_count]。
[[nodiscard]] int effective_count(const Config& c, const nlohmann::json& args);

// 为查询尚未排除自身的每个排除域附加`-site:<domain>`。
// 模型编写的运算符是单独保留的，
// 因此查询永远不会两次给出相同的排除。
[[nodiscard]] std::string apply_exclusions(std::string query,
                                           const std::vector<std::string>& sites);

// 整个请求重写分派适用于接口边界。开启时：计数已解析并固定。
// 在自动模式下：模型的计数保持不变，与 面板 存在之前完全相同。
// 两者：附加排除项。它不拥有原样传递的参数（
// display_description，以及未来 mcp-cpp
// 添加的任何内容）。在此运行之前，调用者会拒绝关闭。
[[nodiscard]] nlohmann::json rewrite_args(const Config& c, nlohmann::json args);

} // namespace agentty::web_search_cfg
