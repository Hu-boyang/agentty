#pragma once
// agentty::ui — 提供者 选择面板 的行模型：一个 有序列表，构建一次，由 状态更新函数 (
// src/runtime/app/update/picker.cpp) 和视图 (
// src/runtime/view/pickers.cpp) 消耗。
//
// 在此之前，复合列表——内置-in预设，然后是外部ACP代理，然后是已保存自定义主机，
// 然后是"Custom host…"哨兵——隐式地存在于状态更新函数和视图中
// 重复的`n_presets + n_acp + n_custom + 1`算术中。
// 每个索引（open-at-active、move、jump、select、
// 渲染）都会手动重新计算相同的偏移量，并且搜索过滤器必须在两个位置重现该
// 重新映射而不会发生漂移。这正是 index-into-a-heterogeneous-list
// 引发的错误类别。
//
// 现在，复合列表是一个值：`build_provider_rows(...)`
// 按显示顺序返回行，每行都用IS（预设、ACP代理、
// 已保存主机或新-host哨兵）进行标记。光标只是 此 向量的索引；
// 解决选择是 `rows[index]`，没有偏移数学。
// 搜索过滤器应用于构建行的位置，因此 状态更新函数 和
// 视图永远不会在哪些行可见或光标落在哪里方面存在分歧。

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "agentty/provider/registry.hpp"      // ProviderPreset，providers()
#include "agentty/provider/acp_agents.hpp"     // AcpAgentSpec，enumerate_acp_agents()
#include "agentty/provider/selection.hpp"      // filter_provider_indices，saved_custom_hosts
#include "agentty/provider/auth_state.hpp"      // auth_source，AuthSource

namespace agentty::ui {

// 提供者选择面板中的一行，按种类标记。 载荷 是在其他地方拥有的单个
// reduce/render 通行证的 生命周期 的廉价视图/副本。
struct ProviderRow {
    // 内置-in 注册表提供者（anthropic、chatgpt、kimi、groq、……）。
    struct Preset { const provider::ProviderPreset* preset; };
    // 配置的外部ACP代理子进程行。
    struct Acp { provider::AcpAgentSpec agent; };
    // 已保存定制OpenAI-compatible主机（不是内置-in预设的Settings.provider_keys规格），
    // 无需re-entering其密钥即可切换。
    struct CustomHost { std::string spec; };
    // 打开 free-text 弹出面板 的尾随 "Custom host…" 哨兵。
    struct NewCustomHost {
        // 当QUERY本身看起来像端点时，哨兵不再是通用的"Custom host…"
        // 逃生舱口而变成具体的报价："Use yolo-auto.com as a custom host"。
        // 这里的 Non-empty 意味着该行被提升到 TOP 和
        // Enter 之前的 -fills 和 弹出面板，
        // 因此用户永远不会重新键入他们已经 带类型的 的内容。
        std::string prefill;
    };

    std::variant<Preset, Acp, CustomHost, NewCustomHost> kind;

    [[nodiscard]] const provider::ProviderPreset* preset() const {
        if (auto* p = std::get_if<Preset>(&kind)) return p->preset;
        return nullptr;
    }
    [[nodiscard]] const provider::AcpAgentSpec* acp() const {
        if (auto* a = std::get_if<Acp>(&kind)) return &a->agent;
        return nullptr;
    }
    [[nodiscard]] const std::string* custom_host() const {
        if (auto* c = std::get_if<CustomHost>(&kind)) return &c->spec;
        return nullptr;
    }
    [[nodiscard]] bool is_new_custom_host() const {
        return std::holds_alternative<NewCustomHost>(kind);
    }
    // "Custom host…"行的端点将在-fill之前，
    // 或者当它是普通逃生舱口时为空。让我们从一个地方查看 渲染 升级的
    // 短语和 状态更新函数 种子 弹出面板。
    [[nodiscard]] const std::string* new_host_prefill() const {
        if (auto* c = std::get_if<NewCustomHost>(&kind))
            return c->prefill.empty() ? nullptr : &c->prefill;
        return nullptr;
    }
};

// 该查询是否看起来像用户想要拨打的ENDPOINT，而不是内置的-in提供商的名称？
// 对于 "yolo-auto.com"、"localhost:8080"、
// "https://gw.internal/api" 为真，对于 "kimi"、"gpt-4o" 为假。
//
// 原因 此 EXISTS:agentty从永远以来已经与任何OpenAI
// -compatible主机通话(`--provider host:port`和"Custom host…"行)，
// 但是选择面板从未与SAID如此。某个主机名为 带类型的 的人看到每个熟悉的
// 提供商都消失了，留下了一个静音的灰色行，其读作 "no, you can't"。
// 现场证据是为通用路径已处理的服务添加注册表行的
// 拉取请求。这个功能就在那里； UI 隐藏了它。
//
// 该 两个 FAILURE MODES ARE 不 SYMMETRIC，其设置阈值：
//   假NEGATIVE-用户带类型的是真实端点，没有得到报价，
//                    得出结论agentty无法到达他们的服务器。增加该功能的成本。
//   false POSITIVE — 出现额外的行，
//                    提供拨打非主机的电话。花费一行，Enter 只会使
//                    探测失败并显示一条明确的消息。
// 如此精简的宽松——但是NOT如此宽松，以至于在寻找提供商时键入
// MODEL名称会将真正的提供商推倒。 "gpt-4o"、"v1.5"
// 和 "3.5" 都包含点或破折号，并且必须保持为 false，
// 因为用户不断地将这些内容键入此框中。
[[nodiscard]] inline bool query_looks_like_host(std::string_view q) {
    // 首先修剪：粘贴的端点通常带有空格到达，并且未修剪的形式在此处失败，
    // 而 canonical_spec() 接受了它 — i.e。
    // 粘贴在 CLI 上有效，但在 选择面板 上无效。
    while (!q.empty() && (q.front() == ' ' || q.front() == '\t')) q.remove_prefix(1);
    while (!q.empty() && (q.back()  == ' ' || q.back()  == '\t'
                       || q.back()  == '\r' || q.back() == '\n')) q.remove_suffix(1);

    if (q.size() < 4) return false;                       // "a.b"不是主机
    if (q.find(' ') != std::string_view::npos) return false;   // 散文

    // 显式方案是意图声明；没有其他需要检查的。
    if (q.starts_with("http://") || q.starts_with("https://")) return true;

    // IPv6文字必须用括号括起来以携带端口："[::1]:8080"。
    if (q.front() == '[') return q.find(']') != std::string_view::npos;

    // 主机：端口 — 冒号后跟数字。方案之后最强的信号，
    // 也是使每个本地主机服务器正常工作的信号。
    if (const auto colon = q.find(':');
        colon != std::string_view::npos && colon > 0 && colon + 1 < q.size()
        && q[colon + 1] >= '0' && q[colon + 1] <= '9') return true;

    // 点名称。要求 最后 dot-segment 看起来像 TLD 或路径 — i.e。
    // 以字母开头 - 因此版本和型号字符串（"v1.5"、"3.5"、"gpt-4.1"）
    // 不会读取为主机。 "yolo-auto.com" 和
    // "api.openai.com/v1" 可以。
    const auto dot = q.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= q.size())
        return false;
    const char after = q[dot + 1];
    const bool alpha = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z');
    if (!alpha) return false;
    // ...在点之前也有一些字母，所以 "1.com" 风格的噪音和裸露的
    // ".com" 不会出现。
    for (std::size_t i = 0; i < dot; ++i) {
        const char c = q[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    }
    return false;
}

// 按显示顺序构建提供者选择面板的行，并按`query`过滤。
//
//   使用 <query> 作为自定义主机的列表入口。
//                                        ]（首次，仅当查询为端点
//   -shaped时 - 见下文）[预设匹配
//   查询（模糊，排名）] [已保存自
//   定义主机匹配查询] [ACP代理]（仅当查询为空时显示）
//   ["Custom host…"]（始终在最后，因此可以到达逃生舱口）
//
// `saved_custom_hosts`被传入（调用方加载设置一次）。
//
// 两个 RULES EARN THEIR KEEP 此处:
//
// 1.主机-shaped向顶部哨兵PROMOTES查询并pre-fills它。
//    输入端点是明确的意图陈述，并且答案应该是您看到
//    的第一件事，而不是最后一件事——Enter
//    直接进入探测器而不是重新打开空文本字段。
//
// 2.已保存自定义主机是SEARCHABLE。它们曾经在你出现的那一刻就消失了
//    带类型的，这意味着到达您已经拥有 已保存 的主机的一种
//    方法是滚动经过每个预设。 已保存 主机是提供商；
//    它属于提供商搜索空间。
[[nodiscard]] inline std::vector<ProviderRow> build_provider_rows(
    const std::vector<std::string>& saved_custom_hosts,
    std::string_view query) {
    std::vector<ProviderRow> rows;
    const auto presets = provider::providers();
    rows.reserve(provider::providers().size() + saved_custom_hosts.size() + 4);

    // 此处将ONCE规范化，并提供规范形式。用户看到的行是将要写入的设置
    // 密钥的字节-identical，因此"  YOLO-AUTO.COM/  "
    // 和"yolo-auto.com"不能成为两个已保存主机。
    const std::string canon = provider::canonical_spec(query);

    // 与查询匹配的已保存主机已经可以作为其自己的行访问到；
    // 提供给 re-add 它将是重复的。比较 CANONICALLY
    // — 已保存 规格以规范方式存储，因此原始比较可能会丢失。
    const bool already_saved =
        std::find(saved_custom_hosts.begin(), saved_custom_hosts.end(), canon)
            != saved_custom_hosts.end();

    const bool offer_host = query_looks_like_host(query) && !already_saved;
    if (offer_host)
        rows.push_back({ProviderRow::NewCustomHost{canon}});

    // 预设，fuzzy-filtered+由共享SSOT过滤器排名，
    // 因此状态更新函数和视图看到完全相同的顺序。
    const std::vector<int> vis = provider::filter_provider_indices(query);
    for (int idx : vis)
        rows.push_back({ProviderRow::Preset{&presets[static_cast<std::size_t>(idx)]}});

    // 已保存自定义主机：case-insensitive子字符串与规范匹配
    // （它们没有标签或模糊-rank的简介，并且主机是您通过前缀记住的东西）。
    // Case-insensitive 因为 已保存 形式是小写的，
    // 但搜索它的用户可能不会这样输入。
    for (const auto& spec : saved_custom_hosts) {
        if (query.empty()
            || spec.find(canon) != std::string::npos) {
            rows.push_back({ProviderRow::CustomHost{spec}});
        }
    }

    // ACP代理不是提供商文本搜索的一部分。
    if (query.empty()) {
        for (auto& agent : provider::enumerate_acp_agents())
            rows.push_back({ProviderRow::Acp{std::move(agent)}});
    }

    // 简单的 "Custom host…" 哨兵总是可以到达
    // 的——除非它已经作为具体报价位于顶部。
    if (!offer_host)
        rows.push_back({ProviderRow::NewCustomHost{}});
    return rows;
}

// ── 已分解, 认证 -aware 变体 ────────────────────────────────────────
//
// 相同的列表，分组。 `^P` 是 MANAGE 后端的表面，
// 因此浏览视图会引导您实际可以使用的视图； 注册表 的其余部分折叠成
// 一条 "N more — type to search" 行。
// 键入会搜索所有内容，因此您尚未配置的提供程序只需按一下键即可，
// 而不是消失（型号 选择面板 的 sign-in 提供的相同 no-dead-end
// 合同提供保留）。
enum class ProviderSection { Yours, More, Signpost, Add };

struct SectionedProviderRow {
    ProviderRow     row;
    ProviderSection section = ProviderSection::Yours;
    // 您配置了 不 的 注册表 行，由于查询对其进行了命名而出现。
    // 渲染 暗淡，作为报价：Enter 开始其标志-in。
    bool            offer   = false;
    // "N more providers"路标。一个真实的、可选择的行，
    // 因此可以通过 Enter 来扩展，而不仅仅是通过您必须知道的密钥来实现。
    bool            more_signpost = false;
};

struct SectionedProviderRows {
    std::vector<SectionedProviderRow> rows;
    int  n_hidden = 0;       // 从浏览视图中保留 注册表 行
    bool searching = false;  // 查询非-empty
};

[[nodiscard]] inline SectionedProviderRows build_sectioned_provider_rows(
    const std::vector<std::string>& saved_custom_hosts,
    std::string_view query,
    const store::Settings& settings,
    const auth::AuthView& view,
    std::string_view active_id = {},
    bool show_all = false) {
    SectionedProviderRows out;
    out.searching = !query.empty();

    const auto presets = provider::providers();
    const std::string canon = provider::canonical_spec(query);
    const bool already_saved =
        std::find(saved_custom_hosts.begin(), saved_custom_hosts.end(), canon)
            != saved_custom_hosts.end();
    const bool offer_host = query_looks_like_host(query) && !already_saved;

    // 带类型的主机赢得顶部位置：它是查询可能意味着的最
    // 具体的事物，并且它是作用于文字文本的一行。
    if (offer_host)
        out.rows.push_back({{ProviderRow::NewCustomHost{canon}},
                            ProviderSection::Yours, false, false});

    // 浏览保留路标后面未配置的行；
    // 扩展 (^A) 或搜索要求 整个 注册表。
    const bool want_all = show_all || !query.empty();
    const auto filtered = provider::filter_provider_indices(query, settings, view);

    // 将注册表划分成两个带。 `configured` 是
    // 两个乐队和路标计数都同意的单一规则，因此它们不能漂移。
    std::vector<const provider::ProviderDescriptor*> mine, rest;
    {
        int i = 0;
        for (int idx : filtered.indices) {
            const auto* p = &presets[static_cast<std::size_t>(idx)];
            (i < filtered.n_configured ? mine : rest).push_back(p);
            ++i;
        }
        // 浏览隐藏这些； ^A 重新-admits 他们。搜索已经返回
        // 了整个注册表，所以没有什么可以重新-admit的。
        if (want_all && query.empty())
            for (const auto& p : presets)
                if (provider::auth_source(p, settings, view)
                    == provider::AuthSource::None)
                    rest.push_back(&p);
    }
    // 隐藏仅统计实际保留的RIGHTNOW。
    out.n_hidden = want_all ? 0 : filtered.n_hidden;

    // 活动 提供者总是领先 YOURS，甚至是 mid-sign-in：
    // 这是您正在运行的行，并且 ● 标记不得位于另一个带中。
    // 它的徽章仍然说明了 凭据 的真相。
    //
    // 折叠浏览保留未配置的行，因此缺少 凭据（首次运行、
    // 过期令牌、未设置环境变量）的活动提供程序位于 NEITHER
    // 列表中——将其直接从 注册表 中拉出，
    // 并且不将其视为隐藏。
    if (!active_id.empty()) {
        auto by_id = [&](const auto* p) { return std::string{p->id} == active_id; };
        if (auto it = std::find_if(rest.begin(), rest.end(), by_id);
            it != rest.end()) {
            mine.insert(mine.begin(), *it);
            rest.erase(it);
        } else if (auto m2 = std::find_if(mine.begin(), mine.end(), by_id);
                   m2 != mine.end()) {
            std::rotate(mine.begin(), m2, m2 + 1);
        } else {
            for (const auto& p : presets)
                if (std::string{p.id} == active_id) {
                    mine.insert(mine.begin(), &p);
                    if (out.n_hidden > 0) --out.n_hidden;
                    break;
                }
        }
    }

    // ── 集合,一个乐队一个 ────────────────────────────────────
    // 行以SECTION顺序发出，因此每个分隔线被绘制一次。
    // 视图只是列表中的一个；分组是这个函数的工作。
    auto emit = [&](ProviderRow r, ProviderSection s, bool offer, bool sign) {
        out.rows.push_back({std::move(r), s, offer, sign});
    };

    // YOURS：首先激活，然后是其余的预设，然后是 已保存 主机
    // 和 ACP 代理（根据定义是你的——你配置了它们）。
    for (const auto* p : mine) emit({ProviderRow::Preset{p}},
                                    ProviderSection::Yours, false, false);
    for (const auto& spec : saved_custom_hosts)
        if (query.empty() || spec.find(canon) != std::string::npos)
            emit({ProviderRow::CustomHost{spec}},
                 ProviderSection::Yours, false, false);
    if (query.empty())
        for (auto& agent : provider::enumerate_acp_agents())
            emit({ProviderRow::Acp{std::move(agent)}},
                 ProviderSection::Yours, false, false);

    // 不 CONFIGURED:提供，每一项操作远离可用。
    for (const auto* p : rest) emit({ProviderRow::Preset{p}},
                                    ProviderSection::More, true, false);

    // 当上面的带折叠时，路标代表上面的带。
    if (out.n_hidden > 0)
        emit({ProviderRow::NewCustomHost{}},
             ProviderSection::Signpost, false, true);

    if (!offer_host)
        emit({ProviderRow::NewCustomHost{}},
             ProviderSection::Add, false, false);
    return out;
}

} // namespace agentty::ui
