// agentty::i18n-实施。
//
// 设计参见i18n.hpp，CLDR规则参见plural.hpp。
// 这个文件是存储和查找的，它有三个值得命名的工作：
//
//   1. 不变地持有目录，以便t()可以将string_views分发给他们
//   2.原子地交换活动的，因此语言改变是指针
//      存储和下帧被翻译(docs/LATE_BINDING.md)
//   3. 解析 和 目录 不具有 JSON 对 高频路径 的依赖性
//
// 关于(3):目录是我们自己的文件，嵌入在二进制程序中，
// 由docs/design/i18n.md.中的模式定义塑造，它们在安装时被解析为ONCE。
// nlohmann已经是依赖性并且在这里使用而不是hand-rolling
// a 解析器——要避免的是RENDER路径上的解析器，并且没有：在
// install_catalog之后，该结构是平面哈希图。

#include "agentty/i18n/i18n.hpp"
#include "agentty/i18n/plural.hpp"

#include <maya/runtime.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace agentty::i18n {

namespace {

// ── 语言表 ───────────────────────────────────────────────────
//
// 每种语言一行，按照枚举的顺序，具有两者不能漂移的
// static_assert。内名是该语言的 自身 脚本
// 中的名称，因为用户扫描 选择面板 查找其语言正在寻找
// word they would use -- a Japanese speaker finds 日本語, not "Japanese".
struct Row {
    Lang             lang;
    std::string_view tag;
    std::string_view endonym;
    std::string_view english;
};

constexpr std::array<Row, kLangCount> kLangs{{
    {Lang::en,    "en",    "English",     "English"},
    {Lang::zh_CN, "zh-CN", "简体中文",      "Simplified Chinese"},
    {Lang::es,    "es",    "Español",     "Spanish"},
    {Lang::pt_BR, "pt-BR", "Português (Brasil)", "Portuguese (Brazil)"},
    {Lang::ja,    "ja",    "日本語",        "Japanese"},
    {Lang::de,    "de",    "Deutsch",     "German"},
    {Lang::fr,    "fr",    "Français",    "French"},
    {Lang::ko,    "ko",    "한국어",        "Korean"},
    {Lang::it,    "it",    "Italiano",    "Italian"},
    {Lang::ru,    "ru",    "Русский",     "Russian"},
    {Lang::tr,    "tr",    "Türkçe",      "Turkish"},
    {Lang::id,    "id",    "Bahasa Indonesia", "Indonesian"},
    {Lang::pl,    "pl",    "Polski",      "Polish"},
    {Lang::nl,    "nl",    "Nederlands",  "Dutch"},
    {Lang::uk,    "uk",    "Українська",  "Ukrainian"},
    {Lang::sv,    "sv",    "Svenska",     "Swedish"},
    {Lang::cs,    "cs",    "Čeština",     "Czech"},
    {Lang::da,    "da",    "Dansk",       "Danish"},
    {Lang::no,    "no",    "Norsk",       "Norwegian"},
    {Lang::hu,    "hu",    "Magyar",      "Hungarian"},
}};

// 该表由枚举值索引，因此错误位置的行将给其后的每
// 种语言提供错误的名称。证明成本低廉，不可能用肉眼
// 注意到。
consteval bool table_is_ordered() {
    for (std::size_t i = 0; i < kLangs.size(); ++i)
        if (static_cast<std::size_t>(kLangs[i].lang) != i) return false;
    return true;
}
static_assert(table_is_ordered(),
              "kLangs must be indexed by Lang -- row N describes Lang(N)");

// ── 储存 ──────────────────────────────────────────────────────────────
//
// 目录一旦建成就是不可变。 `Entry` 包含单个字符串或复数形式；
// 两者都拥有 std::strings，因此 string_view
// 在 目录 的生命周期内保持有效。
struct Entry {
    std::string                                     text;    // 非-plural
    std::array<std::string, 4>                      forms{};  // 由复数
    bool                                            plural = false;
};

// 异构查找，因此t("some.id")不分配std::
// string来探测图。这是 19 ns 和每个 调用 的 分配
// 之间的差异，而 t() 被称为 帧 的几百次。
struct Hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};
struct Eq {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view a,
                                  std::string_view b) const noexcept {
        return a == b;
    }
};

using Table = std::unordered_map<std::string, Entry, Hash, Eq>;

struct Catalog {
    Lang  lang = Lang::en;
    Table table;
};

using CatalogPtr = std::shared_ptr<const Catalog>;

// 每个安装的目录，按语言。在启动时和调试热重载时写入；
// 由 set_active 阅读并完整。
//
// shared_ptr的普通数组而不是映射：二十个槽，由枚举索引，
// 没有查找，也没有分配。
std::array<maya::published<const Catalog>, kLangCount>& store() {
    static std::array<maya::published<const Catalog>, kLangCount> s;
    return s;
}

// 活动 目录。一个已发布的插槽——这就是整个 live-switch 机制。
// 传出的 目录 在 shared_ptr 期间一直保持活动状态，
// 直到最后一个读者将其丢弃，这使得在 帧 期间分发的 string_view
// 是安全的，即使语言在 -frame 中期发生变化。
maya::published<const Catalog>& active_catalog() {
    static maya::published<const Catalog> c;
    return c;
}


// 仅下部-case ASCII。故意将 不 std::tolower 与
// 语言环境：在土耳其语语言环境下，将 'I' 映射到无点 'ı'，
// 并且随用户语言环境改变含义的标签比较是经典的 i18n self-own。
[[nodiscard]] char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] std::string normalize_tag(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        // "de_DE.UTF-8"和"de_DE@euro"都是“德”的意思。
        // 在编码或修饰符处停止；保留 parse_tag 可能想要的区域。
        if (c == '.' || c == '@') break;
        out.push_back(c == '_' ? '-' : ascii_lower(c));
    }
    return out;
}

// 活动 目录不携带的id：放弃之前尝试英语。
//
// 该 CASE 此 EXISTS FOR，并且 渲染 测试捕获了它而不是读取器：
// 每一个真实的翻译都会有片刻的偏差。 60% 的德国人
// 目录 曾经将 `sandbox.ports` 绘制为行
// 标签（原始 ID），紧邻完美的德语。这在各方面都比英语更糟糕：
// 它不是任何语言中的单词，它比它所取代的标签更宽，
// 并且它除了告诉用户某些东西坏了之外什么也不告诉用户。
//
// 英语是保证安装的目录（i18n::init 加载，
// 无论选择什么），并且它是书写ID的语言。
// 所以阶梯是：活动 -> 英语 -> id。
// 仅当英语 目录 本身缺少条目时才可以到达最后一个梯级，
// 而 lint 会导致构建失败。
[[nodiscard]] std::string_view fallback(std::string_view id) noexcept;

}  // 命名空间结束。

std::string_view tag_of(Lang l) noexcept {
    return kLangs[static_cast<std::size_t>(l)].tag;
}

std::string_view endonym(Lang l) noexcept {
    return kLangs[static_cast<std::size_t>(l)].endonym;
}

std::string_view english_name(Lang l) noexcept {
    return kLangs[static_cast<std::size_t>(l)].english;
}

bool parse_tag(std::string_view tag, Lang& out) noexcept {
    const std::string norm = normalize_tag(tag);
    if (norm.empty()) return false;

    // 首先在完整标签上精确匹配("pt-br"->pt_BR)，
    // 因此我们DO携带的区域语言永远不会被其基数所遮蔽。
    for (const auto& r : kLangs) {
        std::string lower;
        lower.reserve(r.tag.size());
        for (const char c : r.tag) lower.push_back(ascii_lower(c));
        if (lower == norm) { out = r.lang; return true; }
    }

    // 然后是基础子标签。 "de-AT" -> 德、"es-MX" -> 西班牙。
    const auto dash = norm.find('-');
    const std::string base = norm.substr(0, dash);

    // zh是例外，赢得了自己的分支。我们只提供简体字，
    // 而繁体字不是它的方言——向 zh-TW 用户提供 zh-CN
    // 比英语更糟糕，因为它 LOOKS 正确，但事实并非如此。
    // 对于使用繁体的zh-HK和zh-MO来说也是如此。
    if (base == "zh") {
        if (dash == std::string_view::npos) { out = Lang::zh_CN; return true; }
        const std::string region = norm.substr(dash + 1);
        if (region == "cn" || region == "hans" || region == "sg") {
            out = Lang::zh_CN;
            return true;
        }
        return false;   // tw、hk、mo、hant -> 转为英语
    }

    // pt-PT->pt-BR。不完全相同，但足够接近，
    // 以至于讲葡萄牙语的人比讲英语的人得到的服务要好得多。 zh 的不对称性是
    // 关键：相互可理解性是一个 per-language 事实，而不是规则。
    if (base == "pt") { out = Lang::pt_BR; return true; }

    // nb/nn对于我们的目的来说都意味着挪威语； CLDR 分割它们，我们不分割。
    if (base == "nb" || base == "nn") { out = Lang::no; return true; }

    for (const auto& r : kLangs) {
        const auto rdash = r.tag.find('-');
        std::string rbase{r.tag.substr(0, rdash)};
        for (auto& c : rbase) c = ascii_lower(c);
        if (rbase == base) { out = r.lang; return true; }
    }
    return false;
}

// 主动的目录携带其语言，因此两者永远不会不一致。
Lang active() noexcept {
    const auto cat = active_catalog().current();
    return cat ? cat->lang : Lang::en;
}

namespace {
std::string_view fallback(std::string_view id) noexcept {
    const auto en = store()[static_cast<std::size_t>(Lang::en)].current();
    if (!en) return id;
    const auto it = en->table.find(id);
    if (it == en->table.end()) return id;
    if (it->second.plural)
        return it->second.forms[static_cast<std::size_t>(Plural::Other)];
    return it->second.text;
}
}  // 命名空间结束。

bool set_active(Lang l) {
    auto cat = store()[static_cast<std::size_t>(l)].current();
    if (!cat) return false;     // 没有 目录 编译进去：保留我们所拥有的
    active_catalog().publish(cat);
    return true;
}

std::string_view t(std::string_view id) noexcept {
    const auto cat = active_catalog().current();
    if (!cat) return id;        // load()之前：id，不能为空
    const auto it = cat->table.find(id);
    if (it == cat->table.end()) return fallback(id);
    // 通过t()读取的多个条目没有可供选择的计数。
    // `other`是每条规则都能产生的一个类别，所以它是诚实的回退。
    if (it->second.plural)
        return it->second.forms[static_cast<std::size_t>(Plural::Other)];
    return it->second.text;
}

std::string_view tn(std::string_view id, long long n) noexcept {
    const auto cat = active_catalog().current();
    if (!cat) return id;
    const auto it = cat->table.find(id);
    if (it == cat->table.end()) return fallback(id);
    if (!it->second.plural) return it->second.text;

    const Plural p = select(cat->lang, n);
    const auto& picked = it->second.forms[static_cast<std::size_t>(p)];
    // 目录不提供的类别。 lint 使得这个无法访问，
    // 但是回退到 `other` 比返回空要好——并且
    // `other` 是通过构造为每个规则形状填充的。
    if (picked.empty())
        return it->second.forms[static_cast<std::size_t>(Plural::Other)];
    return picked;
}

namespace {

// 替换{name}占位符。
//
// 未知名称被留下VERBATIM而不是被删除：目录中的
// 拼写错误在屏幕上显示为"{tatol}"，这是错误报告。
// 删除它会产生一个有漏洞的句子，没有人可以追溯。
//
// "{{"和"}}"是文字大括号，因此字符串可以包含大括号。
// 需要 两者 一半，第一个版本只有开场白："a {{literal}} brace"，
// 然后产生 "a {literal}} brace"，
// 因为结束语落到了下面的逐字副本。被测试抓住了，
// 这是它没有发货的唯一原因——一根弦上的杂散大括号正是那种能
// 通过审查的东西。
[[nodiscard]] std::string interpolate(std::string_view tmpl,
                                      std::span<const Arg> args) {
    std::string out;
    out.reserve(tmpl.size() + 16);
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '}') {
            // "}}"->"}"。一个单独的 '}' 被复制为
            // -is：这几乎肯定是一个拼写错误，展示它胜过吞下它。
            if (i + 1 < tmpl.size() && tmpl[i + 1] == '}') ++i;
            out.push_back('}');
            continue;
        }
        if (tmpl[i] != '{') { out.push_back(tmpl[i]); continue; }
        if (i + 1 < tmpl.size() && tmpl[i + 1] == '{') { out.push_back('{'); ++i; continue; }

        const auto close = tmpl.find('}', i + 1);
        if (close == std::string_view::npos) { out.push_back('{'); continue; }

        const auto name = tmpl.substr(i + 1, close - i - 1);
        const auto hit = std::ranges::find_if(
            args, [&](const Arg& a) { return a.name == name; });
        if (hit != args.end()) out += hit->value;
        else { out.push_back('{'); out += name; out.push_back('}'); }
        i = close;
    }
    return out;
}

}  // 命名空间结束。

std::string format(std::string_view id, std::span<const Arg> args) {
    return interpolate(t(id), args);
}

std::string format_n(std::string_view id, long long n, std::span<const Arg> args) {
    // `n`始终可用作{n}，而无需调用者重述它：每个
    // 复数字符串都需要该数字，并且使每个调用站点通过
    // 它是一个永远被忘记的步骤。
    std::vector<Arg> all;
    all.reserve(args.size() + 1);
    all.push_back(Arg{"n", std::to_string(n)});
    all.insert(all.end(), args.begin(), args.end());
    return interpolate(tn(id, n), all);
}

bool install_catalog(Lang l, std::string_view json_text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (...) {
        return false;
    }
    if (!j.is_object()) return false;

    auto cat = std::make_shared<Catalog>();
    cat->lang = l;

    for (const auto& [key, val] : j.items()) {
        if (key.starts_with('_')) continue;   // _meta和朋友们

        Entry e;
        if (val.is_string()) {
            e.text = val.get<std::string>();
        } else if (val.is_object()) {
            // 复数条目。对象内的 string-valued "text" 是携带
            // width_hint 的非 -plural 形式，lint 会读取该字符串，
            // 而 运行时 则不会。
            if (val.contains("text") && val["text"].is_string()) {
                e.text = val["text"].get<std::string>();
            } else {
                e.plural = true;
                const auto take = [&](const char* k, Plural p) {
                    if (val.contains(k) && val[k].is_string())
                        e.forms[static_cast<std::size_t>(p)] =
                            val[k].get<std::string>();
                };
                take("one", Plural::One);
                take("few", Plural::Few);
                take("many", Plural::Many);
                take("other", Plural::Other);
                // `other`是每个回退所登陆的，
                // 因此没有它的复数条目是不可用的。拒绝整个目录，
                // 而不是安装一个可以返回空的。
                if (e.forms[static_cast<std::size_t>(Plural::Other)].empty()) {
                    // 没有`other` (ru/uk/pl)的规则形状合法地省略它；
                    // 来自 `many` 的种子，因此 回退 路径有
                    // 一些 true 可以返回。
                    const auto& many = e.forms[static_cast<std::size_t>(Plural::Many)];
                    if (many.empty()) return false;
                    e.forms[static_cast<std::size_t>(Plural::Other)] = many;
                }
            }
        } else {
            continue;   // 目录 中的数字或 null 是拼写错误，不是数据
        }
        cat->table.emplace(key, std::move(e));
    }

    store()[static_cast<std::size_t>(l)].publish(cat);
    // 安装ACTIVE语言会重新发布它，因此调试版本中的
    // 热重载需要副作用，而无需第二个调用。
    if (active() == l) active_catalog().publish(cat);
    return true;
}

std::vector<std::string> ids_of(Lang l) {
    const auto cat = store()[static_cast<std::size_t>(l)].current();
    if (!cat) return {};
    std::vector<std::string> out;
    out.reserve(cat->table.size());
    for (const auto& [k, _] : cat->table) out.push_back(k);
    std::ranges::sort(out);
    return out;
}

double completeness(Lang l) {
    const auto en = store()[static_cast<std::size_t>(Lang::en)].current();
    if (!en || en->table.empty()) return 0.0;
    if (l == Lang::en) return 1.0;

    const auto cat = store()[static_cast<std::size_t>(l)].current();
    if (!cat) return 0.0;

    // 针对en的id设置，而不是针对目录自身的大小：带有en
    // 的ids的翻译已经被丢弃，不是更完整，而是过期。
    std::size_t have = 0;
    for (const auto& [k, _] : en->table)
        if (cat->table.contains(k)) ++have;
    return static_cast<double>(have) / static_cast<double>(en->table.size());
}

}  // namespace agentty::i18n
