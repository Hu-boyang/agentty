#pragma once
// build_fused_rows——统一交叉-provider模型选择面板的纯排名核心。
// 给定每个经过身份验证的提供商的 目录（加上为 un-authed 提供商
// 提供的可选标志-in）、最近的（MRU）、活动（提供商、型号）和实时查询，
// 它会生成 ORDERED、SECTIONED FusedRow 列表
// （状态更新函数 从中选择）和视图 渲染。
//
// Pure 和 头文件-only 因此它单元 -tests 没有
// Model/deps/network. 状态更新函数 通过 deps/registry
// 接口边界 组装输入（哪些提供商经过验证，他们的 目录）； RANKING 住在这里，
// 所以 状态更新函数 和视图一致（SSOT 纪律 选择面板 持有）。
//
// 订购：
//   0.FAVORITES-每个提供商中的每个加星标的模型，第一个（#99）。
//                 像其余的一样通过查询过滤；在搜索中，
//                 他们仍然处于领先地位，并按相关性进行排名。
//   1. RECENT — MRU(provider,model)用户实际上在切换之间，
//                 最-recent在前，主动固定在`active`的顶部。
//   2.所有提供商——每个通过查询验证的目录、fuzzy-ranked；
//                 平局中断提供者顺序→上下文窗口。
//   3.登录……——un-authed提供商优惠（仅当查询是
//                 空或fuzzy-matches提供者名称)，暗淡，在末尾。
//
// 模型出现一次：收藏夹显示在FAVORITES中，
// 不再显示在RECENT或以下。活动模型无论在何处仍然会被标记。
//
// 区段边界由`FusedRow::favorite_section`、
// `FusedRow::recent`和`FusedRow::is_signin_offer()`传达；
// 其他一切都是第 2 部分。当部分更改时，视图会插入 头文件。

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "agentty/domain/catalog.hpp"
#include "agentty/runtime/fuzzy.hpp"

namespace agentty::ui {

using agentty::SigninOffer;   // 移至域(catalog.hpp)；呼叫者的别名

struct FusedInputs {
    const std::vector<ProviderCatalog>* catalogs = nullptr;  // 授权提供商
    const std::vector<SigninOffer>*     offers   = nullptr;  // un-authed
    const std::vector<ModelRef>*        recents  = nullptr;  // MRU，最新的在前
    // 用户的加星号模型 ID (设置 favorite_models)。
    // 明星和 FAVORITES 部分的唯一真相来源：从未通过
    // active-provider 加载 的 目录 不携带 标志。
    const std::vector<ModelId>*         favorites = nullptr;
    ModelRef    active;                                       // 当前省份+型号
    std::string query;
    int         recent_cap = 6;
    // 将列表限制为ONE提供商。空（默认）= 融合的 cross
    // -provider 视图。通过智能模式 slot-assign
    // 模式设置为提供者 ID，其中固定模型将被传递给在轮流中处于活动
    // 状态的任何提供者 - 因此来自另一个提供者的行一定不能被选择。
    // 这里过滤而不是在选择处验证使无效选择
    // 不可表示而不是仅仅被拒绝。 Sign-in
    // 优惠也被抑制：您无法固定未登录的模型。
    std::string only_provider;
    // 规范模型标签+fuzzy-match锚。
    // 由视图 (ui::model_display_label) 注入，因此融合的 选择
    // 面板 的行读取 和 与 per-provider 选择面板 的行完全相同。
    // 默认为原始 display_name-or-id，因此 单元测试 可以 调用
    // build_fused_rows，而无需拉入视图层。
    std::string (*label_fn)(std::string_view id, std::string_view name) =
        [](std::string_view id, std::string_view name) -> std::string {
            return std::string{name.empty() ? id : name};
        };
};

namespace detail {

// (提供商，模型）行的可搜索文本：
// 显示格式：提供方名称、规范模型名称、原始标识。
// 规范标签 (ui::model_display_label) 是视图
// 显示的内容，因此匹配和突出显示偏移索引相同的字符串；附加原始 ID，
// 因此即使在美化之后，"gpt-5-codex" 仍然匹配。
[[nodiscard]] inline std::string fused_haystack(std::string_view label,
                                                std::string_view model_label,
                                                const ModelInfo& mi) {
    std::string h;
    h.reserve(label.size() + 1 + model_label.size() + 1
              + mi.id.value.size());
    h += label;
    h += ' ';
    h += model_label;
    h += ' ';
    h += mi.id.value;
    return h;
}

// 通过 传输 id 查找 目录 中的模型。首先精确匹配；如果错过，
// 则回退到 ROW IDENTITY (capkey::norm_row_id)，
// 因此最近幸存下来的提供商 re-spelling 其 id —
// "mistral-medium-3.5" 与 "mistral-medium-3-5"，
// 案例更改，杂散分隔符。如果没有 回退，即使该模型仍在使用，
// 但在下一次 目录 刷新时，这样最近的模型也会从 MRU 中悄然消失。
//
// 行标识(不是norm_model)，因此`[1m]`上下文变体
// 仍然只匹配其自己的行，而不是其基本模型。
[[nodiscard]] inline const ModelInfo* find_model(const ProviderCatalog& c,
                                                 std::string_view model_id) {
    for (const auto& mi : c.models)
        if (mi.id.value == model_id) return &mi;
    const std::string want = capkey::norm_row_id(model_id);
    if (want.empty()) return nullptr;
    for (const auto& mi : c.models)
        if (capkey::norm_row_id(mi.id.value) == want) return &mi;
    return nullptr;
}

[[nodiscard]] inline const ProviderCatalog*
find_catalog(const std::vector<ProviderCatalog>& cats, std::string_view pid) {
    for (const auto& c : cats)
        if (c.provider_id == pid) return &c;
    return nullptr;
}

} // namespace detail

// 构建有序、分段的融合行列表。 `catalogs`/`offers`/
// `recents` 可能为空（视为空）。
[[nodiscard]] inline std::vector<FusedRow> build_fused_rows(const FusedInputs& in) {
    using detail::find_catalog;
    using detail::find_model;
    using detail::fused_haystack;

    static const std::vector<ProviderCatalog> kNoCatalogs;
    static const std::vector<SigninOffer>     kNoOffers;
    static const std::vector<ModelRef>        kNoRecents;
    const auto& catalogs = in.catalogs ? *in.catalogs : kNoCatalogs;
    const auto& offers   = in.offers   ? *in.offers   : kNoOffers;
    const auto& recents  = in.recents  ? *in.recents  : kNoRecents;
    // 已加星标？从给定的设置列表中，否则该行自己的 标志（
    // 手动构建 目录 的 单元测试）。
    auto is_fav = [&](const ModelInfo& mi, std::string_view provider_id) {
        if (!in.favorites) return mi.favorite;
        return is_favorite(*in.favorites, provider_id, mi.id.value);
    };

    const std::string& q = in.query;
    const bool no_query = q.empty();
    // Single-provider模式(智能模式slot-assign)。
    // 控制所有三个部分：来自其他提供商的 RECENT 条目、他们的 目录 行
    // 以及每个标志-in 报价。
    const bool one_provider = !in.only_provider.empty();
    auto in_scope = [&](std::string_view pid) {
        return !one_provider || pid == in.only_provider;
    };

    std::vector<FusedRow> out;
    out.reserve(recents.size() + 32);

    auto matches = [&](std::string_view label, const ModelInfo& mi) -> bool {
        return no_query ||
               fuzzy::matches(fused_haystack(label,
                                             in.label_fn(mi.id.value, mi.display_name),
                                             mi), q);
    };

    // 视图的可见模型NAME放入行的前导存储单元——规范的提供者
    // -uniform标签（ui::model_display_label通过in.label_fn)，
    // 因此匹配+突出显示索引SAME字符串和视图渲染。
    // 按值返回（label_fn 标准化），
    // 因此调用者在对其进行偏移之前将其绑定到本地。
    auto name_of = [&](const ModelInfo& mi) -> std::string {
        return in.label_fn(mi.id.value, mi.display_name);
    };
    // 查询在NAME内的字节偏移量，用于fzf-style突出显示。
    // 当没有查询时或者当行仅与提供者名称匹配时为空。
    auto name_positions = [&](std::string_view name) -> std::vector<int> {
        if (no_query) return {};
        return fuzzy::score(name, q).positions;
    };

    // ── 第1节: RECENT (MRU) ──────────────────────────────────────────
    // 活动行被首先固定，即使实际上不是最新的 MRU 条目，因此 `●`
    // 总是领先。 De-dup 与我们在这里发出的内容相对应，
    // 因此 "all providers" 部分永远不会重复最近的行。
    // 身份是 capkey-FOLDED（提供者，模型）：提供者在多种拼写（
    // mistral-medium-3-5 / -3.5）下给一个模型起别名，
    // 并且最近选择的一种拼写必须抑制 目录 的双胞胎 - 而不是位于它之上。
    //
    // norm_ROW_id，而不是norm_model:后者归
    // 并 `[1m]`/`[2m]`离开(对于能力查找是正确的，
    // 对于行标识是错误的)，这使得每个1M-context 变体
    // 看起来像其基本模型的别名并且从列表中消失。行标识保留标记，
    // 同时折叠真正的拼写别名。
    //
    // "<provider>\n<folded-id>"上的Hash-set —旧的
    // O(已见)线性扫描re-folded BOTH侧每个探针(两个分配×候选×已见：
    // 450-model列表上的〜200k allocs/keystroke)。
    // 目录 行使用预先计算的 c.row_keys 归并（如果存在）。
    std::unordered_set<std::string> seen;
    int emitted_recent = 0;
    std::vector<FusedRow> recent_rows;
    // 用于探测键的一个临时缓冲区——contains()通过常量
    // 引用来获取它，因此一旦缓冲区的容量变热，探测就不再分配。
    std::string probe;
    auto seen_key = [&probe](std::string_view pid,
                             std::string_view folded) -> const std::string& {
        probe.clear();
        probe.reserve(pid.size() + 1 + folded.size());
        probe += pid; probe += '\n'; probe += folded;
        return probe;
    };
    // 第二个去重轴，以具有不同分隔符（\x01 而不是 \n）的相同可见
    // 集为键控，因此一次散列查找涵盖两个探测器。 Row-id 折叠捕获
    // 拼写别名 (foo-3-5/foo-3.5)；标签折叠捕获规范显示标签
    // 冲突的 DISTINCT id - Copilot 的 /models
    // 可以以同样的方式发出 `claude-opus-4-5` 和
    // snapshot/preview 兄弟，即 normalize_id 渲染，
    // 这就是 "Opus" 和 "Kimi K2 (GitHub)" 最终
    // 在 选择面板（问题 #68）中列出两次的原因。幸存的行是确定性的：
    // stable_sort 下游是密钥-stable。
    auto label_seen_key = [&probe](std::string_view pid,
                                   std::string_view label_fold) -> const std::string& {
        probe.clear();
        probe.reserve(pid.size() + 1 + label_fold.size());
        probe += pid; probe += '\x01'; probe += label_fold;
        return probe;
    };
    auto already = [&](const ModelRef& r) {
        return seen.contains(seen_key(r.provider_id,
                                      capkey::norm_row_id(r.model_id)));
    };
    auto push_recent = [&](const ModelRef& r) -> bool {
        if (already(r)) return false;
        if (!in_scope(r.provider_id)) return false;
        const ProviderCatalog* c = find_catalog(catalogs, r.provider_id);
        if (!c) return false;                              // 提供商已退出
        const ModelInfo* mi = find_model(*c, r.model_id);
        if (!mi) return false;                             // 型号从 目录 消失
        if (is_fav(*mi, r.provider_id)) return false;      // 所示为FAVORITES
        if (!matches(c->label, *mi)) return false;
        FusedRow row;
        row.provider_id = r.provider_id;
        row.label       = c->label;
        row.model       = *mi;
        row.model.favorite = false;
        row.model_label = name_of(*mi);
        row.authed      = true;
        row.active      = (r == in.active);
        row.recent      = true;
        row.reasons     = effort_capable(
            resolved_caps(mi->id.value, r.provider_id));
        row.tier        = static_cast<std::uint8_t>(
            ModelCapabilities::tier_for(mi->id.value));
        row.tool_capable  = mi->supports_tools.value_or(true);
        row.match_positions = name_positions(row.model_label);
        recent_rows.push_back(std::move(row));
        seen.insert(seen_key(r.provider_id, capkey::norm_row_id(r.model_id)));
        // 还围栏标签轴：最近的"Opus 4.5"必须抑制第2部分
        // 中的目录的`claude-opus-4-5-20250929`
        // 快照同级（相同标签，不同折叠id）。
        seen.insert(label_seen_key(r.provider_id,
            capkey::norm_row_id(recent_rows.back().model_label)));
        ++emitted_recent;
        return true;
    };

    if (!in.active.empty()) push_recent(in.active);
    for (const auto& r : recents) {
        if (emitted_recent >= in.recent_cap) break;
        push_recent(r);
    }

    // RECENT以MRU顺序发出：首先是活动模型(push_recent
    // (in.active)在任何MRU条目之前运行)，然后是最新的-first。
    // IS 该部分的含义，因此没有什么可排序的。
    //
    // 过去在浏览时按字母顺序排列，首先是活动提供商的最近内容。
    // 这使得你十秒前选择的模型低于你一周前最后一次接触的模型，
    // 这使得你到达的大多数阅读列表都是任意的——
    // 该部分显示 "recent" 并且没有按新近度排序。
    // 按构造排序还意味着未来的比较器无法替换活动
    // 行：它从未接收到的排序无法对其重新排序。
    for (auto& r : recent_rows) out.push_back(std::move(r));

    // ── 第2节:所有供应商,模糊-ranked ───────────────────────
    // HOT LOOP —在每个 目录 的每个模型上每次击键运行（仅
    // OpenRouter 目录 就超过 300）。三项费用是刻意保留的
    // OUT的吧：
    //   ·ModelInfo份：得分第一，按需加载、FusedRow仅用于
    //     MATCH 的行（没有每行匹配的查询，但该路径每次打开运行一次，
    //     而不是每次击键运行一次）。
    //   ·模糊re-scoring:针对缓存的干草堆得分一次；重用
    //     当匹配位于名称前缀（常见情况）内时，
    //     SAME结果的位置而不是re-scoring裸名称。
    //   ·resolved_caps:每个shared_mutex后面有3个注册表映射-lookups
    //     调用-在该构建期间按（提供者、模型）记忆；
    //     目录 在重建过程中重复 ID，但绝不在重建过程中重复 ID。
    // `tier`是按行预先计算的，而不是在比较器内部导出的：
    // ModelCapabilities::tier_for标记id并运行多次子字符串扫描，
    // 并且比较器被调用O(n log n)次。
    // 在聚合器的few-hundred-model 目录上，
    // 每次击键有数千次冗余扫描。 0 当浏览关闭（未使用）时。
    struct Scored { FusedRow row; int score; int prov_ord; int tier; std::string akey; };
    std::vector<Scored> scored;
    scored.reserve(64);
    int prov_ord = 0;
    for (const auto& c : catalogs) {
        if (!in_scope(c.provider_id)) continue;
        const std::size_t nkeys = c.search_keys.size();
        for (std::size_t i = 0; i < c.models.size(); ++i) {
            const auto& mi = c.models[i];
            // 通过 目录 的预先计算的 归并（当存在时）进行 Dedup
            // （在 RECENT 中，或者发出的行的别名拼写 — 目录 列出孪生，
            // 如 mistra-medium-3-5 和 -3.5）：
            // per-keystroke 路径上的零 分配。
            // （两个-step，不是三元：混合的lvalue/prvalue条件按需加载和缓存密钥的COPY，
            // 这会将分配放回去。）
            std::string folded_tmp;
            std::string_view folded;
            if (i < c.row_keys.size()) folded = c.row_keys[i];
            else folded = (folded_tmp = capkey::norm_row_id(mi.id.value));
            if (seen.contains(seen_key(c.provider_id, folded))) continue;
            int mscore = 0;
            std::vector<int> positions;
            // 目录有时缓存的规范标签（生产）； 按需加载 仅
            // 在 回退 路径上通过 label_fn。
            // 推迟通过模糊门，因此狭窄的查询永远不会为已消除的
            // 行支付标签成本。
            const bool have_label_cache = i < c.display_labels.size();
            std::string model_label;
            if (!no_query) {
                // 优选 目录 的 PRECOMPUTED 小写干草堆（每次
                // 目录 更改构建一次），因此击键永远不会为每个模型 re
                // -allocates 或 re-lowercases 干草堆。
                // 仅当 缓存 未填充时（直接 调用 build_fused_rows
                // 的 e.g.单元测试），才回退到内联编写它。
                std::string tmp;
                std::string_view h;
                if (i < nkeys) {
                    h = c.search_keys[i];
                } else {
                    model_label = name_of(mi);
                    h = (tmp = fused_haystack(c.label, model_label, mi));
                }
                const auto sc = fuzzy::score(h, q);
                if (!sc.matched()) continue;
                mscore = sc.score;
                if (model_label.empty())
                    model_label = have_label_cache ? c.display_labels[i]
                                                   : name_of(mi);
                // 高亮偏移索引可见标签，其是已知开始处的干草堆(
                // "<provider> <label> <id>")
                // 的SUBSTRING。当每个匹配的字符落在标签范围内时，
                // 按该偏移量进行映射；否则，查询与提供者名称或
                // 原始 ID 匹配 - re-score 只是标签，
                // 因此我们只突出显示屏幕上的内容（空结果很好：
                // 没有任何东西可以点亮）。
                const std::size_t label_off = c.label.size() + 1;  // "<provider> "
                const std::size_t label_end = label_off + model_label.size();
                bool all_in_label = !sc.positions.empty();
                for (int p : sc.positions) {
                    const auto up = static_cast<std::size_t>(p);
                    if (up < label_off || up >= label_end) { all_in_label = false; break; }
                }
                if (all_in_label) {
                    positions.reserve(sc.positions.size());
                    for (int p : sc.positions)
                        positions.push_back(p - static_cast<int>(label_off));
                } else {
                    positions = name_positions(model_label);
                }
            } else {
                model_label = have_label_cache ? c.display_labels[i]
                                               : name_of(mi);
            }
            FusedRow row;
            row.provider_id = c.provider_id;
            row.label       = c.label;
            row.model       = mi;
            row.model.favorite = is_fav(mi, c.provider_id);
            row.model_label = model_label;
            row.authed      = true;
            row.active      = (c.provider_id == in.active.provider_id
                               && mi.id.value == in.active.model_id);
            row.recent      = false;
            row.favorite_section = row.model.favorite;
            // 优选目录的MEMOISED推理标志（每次catalog/capability变化构建一次）；
            // 仅当未填充 缓存 时才实时解析（单元测试 调用
            // build_fused_rows 原始）。
            row.reasons     = i < c.reason_flags.size()
                ? static_cast<bool>(c.reason_flags[i])
                : effort_capable(resolved_caps(mi.id.value, c.provider_id));
            row.match_positions = std::move(positions);
            // 层：通过浏览-mode读取，按视图排序在AND下方（通过它对
            // 提供商徽章进行调色，因此最强的-first排序清晰可见）。
            // 此处计算一次，而不是 per-comparison 或 per
            // -frame — tier_for 标记 id 并运行子字符串扫描。
            const int tier = static_cast<int>(
                ModelCapabilities::tier_for(mi.id.value));
            row.tier = static_cast<std::uint8_t>(tier);
            row.tool_capable  = mi.supports_tools.value_or(true);
            // 注册查询门AFTER：filtered
            // -out孪生不得抑制其匹配的兄弟。
            // Label-axis dedup：一个不同的 ID，
            // 其规范标签 归并 与我们已经发出的（在同一
            // 提供者内）是 Copilot-catalog 重复模式。
            // 在模糊匹配之后完成，因此被消除的行仍然无法阻止
            // 其可见的-label同级。
            const std::string label_fold = capkey::norm_row_id(model_label);
            if (seen.contains(label_seen_key(c.provider_id, label_fold))) continue;
            seen.insert(seen_key(c.provider_id, folded));
            seen.insert(label_seen_key(c.provider_id, label_fold));
            scored.push_back({std::move(row), mscore, prov_ord, tier,
                              no_query ? capkey::norm_row_id(model_label)
                                       : std::string{}});
        }
        ++prov_ord;
    }
    std::stable_sort(scored.begin(), scored.end(),
        [no_query](const Scored& a, const Scored& b) {
            // FAVORITES 领先于每个模式（#99）：带星号的
            // 模型是用户在其中选择的模型，因此它们位于其他所有模型之上。
            if (a.row.favorite_section != b.row.favorite_section)
                return a.row.favorite_section;
            // CAN'T-DO-THE-JOBSINKS。提供商表示没有
            // 工具支持的模型根本无法驱动代理，因此它的排名绝不能
            // 超过可以驱动代理的模型——排名约为 "what should I pick"，
            // 并且这些只能用于普通聊天。
            if (a.row.tool_capable != b.row.tool_capable)
                return a.row.tool_capable;
            if (no_query) {
                // 浏览：一个平面"all providers"部分，
                // 按规范化模型标签按字母顺序排序；上下文窗口最终是
                // tie-break。
                if (a.akey  != b.akey)  return a.akey < b.akey;
                return a.row.model.context_window > b.row.model.context_window;
            }
            // FILTERING:相关性顺序——模糊分数是意图信号并且不能
            // re-ranked在其他任何东西后面。 Provider
            // 注册表 命令然后更广泛的上下文断开联系。
            if (a.score != b.score) return a.score > b.score;
            if (a.prov_ord != b.prov_ord) return a.prov_ord < b.prov_ord;
            return a.row.model.context_window > b.row.model.context_window;
        });
    // FAVORITES先行，领先于RECENT(#99)；其余的遵循最近的情况。
    auto fav_end = std::stable_partition(scored.begin(), scored.end(),
        [](const Scored& s) { return s.row.favorite_section; });
    std::vector<FusedRow> favs;
    favs.reserve(static_cast<std::size_t>(fav_end - scored.begin()));
    for (auto it = scored.begin(); it != fav_end; ++it) favs.push_back(std::move(it->row));
    out.insert(out.begin(), std::make_move_iterator(favs.begin()),
               std::make_move_iterator(favs.end()));
    for (auto it = fav_end; it != scored.end(); ++it) out.push_back(std::move(it->row));

    // ── 第3节:签 -in 提议(不计 -authed 供应商) ────────────────────
    // QUERY-GATED:在没有查询的情况下，浏览视图保持干净（没有
    // "sign in"混乱）； fuzzy-matches 和 un
    // -authed 提供商的名称会显示其报价，因此搜索您尚未添加的提供
    // 商永远不会是死胡同 - 下一步是 IS 行。
    for (const auto& off : offers) {
        if (one_provider) break;    // 无法固定您未登录的模型
        if (no_query || !fuzzy::matches(off.label, q)) continue;
        FusedRow row;
        row.provider_id = off.provider_id;
        row.label       = off.label;
        row.authed      = false;               // model.id 留空 ⇒ 报价
        out.push_back(std::move(row));
    }

    return out;
}

} // namespace agentty::ui
