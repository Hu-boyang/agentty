// models.cpp-融合交叉-provider型号选择面板的状态更新函数:open/
// filter/select、槽-assign、MRU环、目录新鲜度、
// 以及switch_to_model_ref/open_login_for辅助器其他臂分派。

#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/core/anim_clock.hpp>   // anim_now_ms(目录新鲜度时钟)
#include <maya/platform/io.hpp>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/acp_agents.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/catalog_sources.hpp"
#include "agentty/auth/vault.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/fused_models.hpp"
#include "agentty/runtime/mem.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/provider_rows.hpp"
#include "agentty/runtime/view/cache.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/subagent.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace pick = agentty::ui::pick;
using maya::overload;

namespace {
// 单调递增的毫秒来自maya的动画时钟——通过maya::
// testing::advance_anim_clock_ms测试-controllable，因此目录新鲜度(
// loaded_at_ms)在测试中是可再现的/可推进的。
[[nodiscard]] std::int64_t now_ms() {
    return ::maya::anim_now_ms();
}

// 比这更旧的 fused-picker 目录 在下一次打开时重新获取。
// 足够长，快速 re-opens 不会 re-hammer 提供程序，
// 足够短，添加 上游 的模型会在正常会话中显示。
constexpr std::int64_t kCatalogTtlMs = 60'000;   // 60s


// 进一步向下定义（使用 fused-picker MRU 帮助器）；这里转发-declared，
// 这样上面的经典型号-picker 状态更新函数就可以喂同一个环。
[[nodiscard]] Cmd record_recent(Model& m, const std::string& provider_id,
                   const std::string& model_id);
void hydrate_recents(Model& m);
void rebuild_fused_rows(Model& m, bool sync_sources = true);
} // 命名空间结束。


// ── 折叠的十字架 -provider 模型 选择面板 ────────────────────────────────────
namespace {

constexpr int kRecentCap = 6;

// MRU的FRONT处的记录（提供商、模型）、已删除重复数据、已封顶。持续到 Settings.recent_models，
// 因此 RECENT + ^Tab 在重新启动后仍然有效。
// 镜像到 m.d.recent_models 以进行实时 选择面板 构建。
[[nodiscard]] Cmd record_recent(Model& m, const std::string& provider_id,
                   const std::string& model_id) {
    if (provider_id.empty() || model_id.empty()) return Cmd::none();
    ModelRef ref{provider_id, model_id};
    auto& mru = m.d.recent_models;
    // 环的身份是capkey-FOLDED id:提供者在多个别名
    // 拼写下列出相同的模型(Mistral的目录携带mistral
    // -medium-3-5 和 -3.5——两者都是规范的mistral
    // -medium-latest)，并且在不同时间选择两种拼写不得为
    // 一个模型增加两个RECENT行。最新选择的 SPELLING
    // 获胜（它取代了旧的批发条目），因此戒指显示了用户最后选择的内容。
    const std::string folded = capkey::norm_row_id(model_id);
    std::erase_if(mru, [&](const ModelRef& r) {
        return r.provider_id == provider_id
            && capkey::norm_row_id(r.model_id) == folded;
    });
    mru.insert(mru.begin(), std::move(ref));
    if (static_cast<int>(mru.size()) > kRecentCap) mru.resize(kRecentCap);

    auto& s = m.d.persisted;
    s.recent_models.clear();
    for (const auto& r : mru)
        s.recent_models.push_back(r.provider_id + "\t" + r.model_id);
    return save_record(m);
}

// 从设置中水化m.d.recent_models（每个条目"<provider>\t<model>"）。
void hydrate_recents(Model& m) {
    if (m.d.recent_models.empty()) {
        const auto& s = m.d.persisted;
        for (const auto& e : s.recent_models) {
            auto tab = e.find('\t');
            if (tab == std::string::npos) continue;
            ModelRef ref{e.substr(0, tab), e.substr(tab + 1)};
            // 一个模型的 归并 遗留重复 SPELLINGS (在通过 capkey
            // 身份去重 record_recent 之前持续存在)：第一个
            // (最新的) 拼写获胜，后来的别名被删除。
            const std::string folded = capkey::norm_row_id(ref.model_id);
            bool dup = false;
            for (const auto& r : m.d.recent_models)
                if (r.provider_id == ref.provider_id
                    && capkey::norm_row_id(r.model_id) == folded) {
                    dup = true;
                    break;
                }
            if (!dup) m.d.recent_models.push_back(std::move(ref));
        }
    }
    // 始终确保 活动（提供商、型号）存在，以便 ^Tab 有
    // 一个可以返回的家，并且戒指永远不会是一个单独的 过期 条目。
    // 保留的历史记录可能早于当前模型（e.g。尚未记录的新交换机，
    // 或第一个-ever会话），因此如果丢失，请附加它。
    // 折叠比较：作为环条目的别名拼写的活动
    // ID不得附加视觉重复。
    if (!m.d.model_id.value.empty()) {
        const ModelRef active{active_provider_id(m), m.d.model_id.value};
        const std::string folded = capkey::norm_row_id(active.model_id);
        bool present = false;
        for (const auto& r : m.d.recent_models)
            if (r.provider_id == active.provider_id
                && capkey::norm_row_id(r.model_id) == folded) {
                present = true;
                break;
            }
        if (!present) m.d.recent_models.push_back(active);
    }
}

// MRU（提供商、型号）参考仍然可以切换吗？当其提供者被授权为 和 时，
// 确实如此 - 如果我们持有该提供者的 目录 - 模型 ID 仍会列出。
// 如果没有目录是已加载，但我们无法反驳它，所以我们乐观地允许它（交换机
// 自己的重新获取+ 过期-model保护self-heal）。
// 用于跳过 ^Tab 上的死环条目，因此已除名/签名的-out 模型永远不会
// 成为切换目标。
[[nodiscard]] bool mru_ref_is_live(const Model& m, const ModelRef& ref,
                                   const store::Settings& settings) {
    if (!provider::provider_is_authed(ref.provider_id, settings, m.d.auth))
        return false;
    for (const auto& c : m.d.provider_catalogs) {
        if (c.provider_id != ref.provider_id) continue;
        if (c.models.empty()) return true;          // 目录 还不是 已加载
        for (const auto& mi : c.models)
            if (mi.id.value == ref.model_id) return true;
        return false;                                // 提供商已知，模型消失
    }
    return true;   // 该提供商还没有 目录 — 不要反驳
}

// 将 选择面板 的 SOURCES 刷新到 Model — CHEAP、
// in-memory-only 通行证（一个设置读取 + 提供者枚举 +
// stat-cached 身份验证检查），在 选择面板 打开时运行。
// 它确实 不 接触网络或构建任何提供商的模型列表：活动提供商从已在手的 目录 (
// available_models) 中播种，每个其他经过身份验证的提供商都会
// 获得一个空的加载条目，异步获取 (fetch_models_for) 稍后会
// 填充一个或两个 帧。这就是即使后端速度较慢（Ollama /自定义主机，
// 其列表探测将阻止 UI 线程 几秒钟）也能保持 INSTANT 开放的原因。
void refresh_fused_sources(Model& m) {
    const auto& settings = m.d.persisted;
    const std::string active_pid = active_provider_id(m);

    // 修剪其提供者不再被授权的目录（e.g。在选择面板打开时通过^D签出）。
    // 如果没有这个，过期 目录 会一直存在，并且其模型会一直
    // 显示在融合列表中，直到重新启动。 id 重载也可以处理自定义
    // 主机（如果它们的 已保存 密钥仍然存在则进行身份验证）。
    std::erase_if(m.d.provider_catalogs, [&](const ProviderCatalog& c) {
        return !provider::provider_is_authed(c.provider_id, settings, m.d.auth);
    });

    auto find_cat = [&](std::string_view id) -> ProviderCatalog* {
        for (auto& c : m.d.provider_catalogs)
            if (c.provider_id == id) return &c;
        return nullptr;
    };
    m.d.fused_offers.clear();

    // ONE传递ONE枚举。 provider::catalog_sources
    // () 是 "every backend that can answer a models query"
    // — 注册表 预设加上 已保存 自定义主机（原始主机：端口、
    // Ollama、llama.cpp、私有网关）的单个语句，
    // 采用已经解决，因此命名已知后端的规范会出现一次，作为该后端。
    //
    // 这曾经是两个 hand-written 循环，有一段时间它是一个：
    // 仅枚举了预设，因此已保存定制主机从未获得
    // 目录并且其型号根本从未出现。将列表拆分到
    // 调用 站点会导致忘记整个来源，
    // 因此该列表现在位于一个位置，每个消费者都从那里读取它。
    for (const auto& src : provider::catalog_sources(settings, m.d.auth)) {
        if (src.needs_signin) {
            // Un-authed 预设 → QUERY-GATED 标志 -in
            // 优惠：它永远不会使浏览视图混乱（build_fused_rows 在
            // 查询为空时隐藏优惠），但在没有 Mistral 帐户的情况下键入
            // "mistral" 会显示 "Mistral — sign in" 行，
            // 而不是 DEAD END。 Enter 路由到其身份验证流程 (
            // origin::Models) 并在此处返回 目录 加载 — 搜索是
            // 单个动词：查找模型 → 可能登录 → 选择。
            m.d.fused_offers.push_back(SigninOffer{src.id, src.label});
            continue;
        }
        ProviderCatalog* c = find_cat(src.id);
        if (!c) {
            // 多个 SAVED 自定义主机可以共享一个主机名，并且仅在它们的
            // "#name" 帐户标签上有所不同（"ollama.com#main"
            // 与 "ollama.com#work"）。
            // provider_display_name() 剥离 URL
            // 路径 - 以及位于 "/" 后面的任何标签 - 因此它们的标签
            // 将在 选择面板 中出现 IDENTICAL。将标签放在括号中，
            // 这样每一行都保持唯一，用户可以知道它指向哪个帐户。预设源跳过此：
            // 他们的ID不携带标签并且他们的注册表标签
            // 已经明确。
            std::string label = src.label;
            if (!src.is_preset) {
                if (auto h = src.id.rfind('#'); h != std::string::npos
                                                && h + 1 < src.id.size()) {
                    const std::string tag = src.id.substr(h + 1);
                    // 仅当标签未ALREADY显示该片段时才追加。
                    // URL-form 规范逐字保留 "#tag"，
                    // 因此请查找确切的 "#tag" 标记 - 而不是
                    // 标签的裸子字符串，当标签文本也出现在 HOST
                    // (e.g.txt) 中时，这会错误地抑制括号。
                    // "main.ollama.com#main"，其中普通查找(
                    // "main")命中主机并使得该行不明确)。
                    if (label.find('#' + tag) == std::string::npos)
                        label += " [" + tag + "]";
                }
            }
            m.d.provider_catalogs.push_back(ProviderCatalog{
                .provider_id = src.id, .label = std::move(label),
                .state = ProviderCatalog::State::Idle});
            c = &m.d.provider_catalogs.back();
        }
        // 活跃后端：MIRROR，我们已经持有的实时 目录（
        // available_models 是活跃提供商的 SSOT）。
        // Re-seed 在 每个 刷新上，而不仅仅是在空时 - 否则融合的
        // 目录 会冻结在 首次 打开的任何 available_models
        // 上（通常是捆绑种子，在实时 /v1/models 获取落地之前），
        // 然后随着 available_models 的增长而发散（新的
        // -listed 旗舰永远不会出现）。其他人都保持空+空闲状态，
        // 因此 Open 通过 cmd::fetch_models_for(
        // id) 触发后台获取，这会解析预设的身份验证或自定义主机的 已保存 密钥；
        // 行列表只是随着每个解析而增长。
        if (src.id == active_pid) c->error = m.s.models_error;
        if (src.id == active_pid && !m.d.available_models.empty()) {
            if (c->models != m.d.available_models) {
                c->models = m.d.available_models;
                c->invalidate_derived();    // 模型组已更改 — 所有 缓存 过期
            }
            c->state = ProviderCatalog::State::Ready;
        }
    }
}


} // 命名空间结束。

// 两者、状态更新函数和fused_picker视图使用共享构建器，
// 因此它们作用的行列表永远不会不一致(SSOT)。于 internal.hpp 声明。
// PURE：仅从已经-refreshed源（provider_catalogs
// + fused_offers）+实时查询构建——没有磁盘，没有枚举，
// 所以它足够便宜，可以在每次击键时运行。
std::vector<FusedRow> fused_rows_for_model(const Model& m) {
    ui::FusedInputs in;
    in.catalogs   = &m.d.provider_catalogs;
    in.offers     = &m.d.fused_offers;
    in.recents    = &m.d.recent_models;
    in.favorites  = &m.d.persisted.favorite_models;
    in.active     = ModelRef{active_provider_id(m), m.d.model_id.value};
    in.recent_cap = kRecentCap;
    // 规范的provider-uniform标签使得读取的融合行AND与per-provider
    // 选择面板 (ui::model_display_label)完全相同地匹配。
    in.label_fn   = &ui::model_display_label;
    // 智能模式slot-assign固定将被分派给ACTIVE提供商的模型，
    // 因此仅可能出现其模型。请参阅选择臂。
    if (auto* c = m.ui.panel.get<pn::Models>()) {
        if (c->assign_slot) in.only_provider = active_provider_id(m);
        in.query = c->query;
        // ^/范围：将列表限制为一个提供商的型号。当两者都设置时，
        // Smart-assign 范围（上面）会获胜 - 槽约束
        // 比用户的rill-in更强。
        if (!c->provider_scope.empty() && in.only_provider.empty())
            in.only_provider = c->provider_scope;
    }
    return ui::build_fused_rows(in);
}

namespace {

// 将缓存行列表重建为m.d.fused_rows。由 状态更新函数 仅 在其输入
// 更改时调用（打开/过滤/目录-loaded/收藏夹），因此视图 + 光标数学永远
// 不会在每次 帧 或每次击键时 re-enumerate 提供程序或
// re-read settings.json。
//
// `sync_sources`控制EXPENSIVE前导码(设置读取+提供商枚举+per
// -provider认证统计+修剪+active-catalog镜像)。仅当 AUTH 或
// available_models 可能已更改时才需要 — 在开放状态、
// 在 ModelsLoaded（活动提供商）或最喜欢的 切换 上。
// NON-active 提供商的普通异步 目录-arrival (FusedCatalogLoaded)
// 仅更改了 目录 的模型；在 re-ranks 处传递 sync_sources=false
// + 重建密钥而无需 auth-stat 搅动，因此大量提供者解析不会 re-enumerate
// + re-stat 身份验证 N 次。
void rebuild_fused_rows(Model& m, bool sync_sources) {
    // 快照 仅 光标所在行的突出显示行的标识 re-anchor
    // 下面 — 不 整个行列表。旧的完整深拷贝（每行的字符串 +
    // ModelInfo）每次击键都会运行；两个字符串携带
    // 相同的信息。
    std::string prev_provider, prev_model;
    if (auto* c = m.ui.panel.get<pn::Models>();
        c && c->index >= 0
        && c->index < static_cast<int>(m.d.fused_rows.size())) {
        const auto& r = m.d.fused_rows[static_cast<std::size_t>(c->index)];
        prev_provider = r.provider_id;
        prev_model    = r.model.id.value;
    }
    // Re-sync源FIRST:将活动提供商的现场
    // available_models镜像到其目录+拾取任何新的-authed提供商。
    // 如果没有这个重建re-ranks STALE 目录，那么刚刚添加的实时/v1/models获取的模型
    // (ModelsLoaded)永远不会到达打开的选择面板——
    // "fused pane doesn't update"缺陷。
    if (sync_sources) refresh_fused_sources(m);
    // 保持每个目录的预先计算的小写模糊键与其模型集同步。这是过滤器消耗
    // 目录 的 唯一 地方，因此这里构建的键在每次击键时都会重复使用
    // - per-key 过滤器永远不会为每个模型 re-allocates
    // 或 re-lowercases 干草堆。 model-set
    // 更改会清除突变位点的 search_keys（此处大小不匹配），
    // 因此仅当 目录 实际更改时，此重建才运行 O(models)。
    for (auto& c : m.d.provider_catalogs) {
        if (c.search_keys.size() != c.models.size()) {
            c.search_keys.clear();
            c.search_keys.reserve(c.models.size());
            // 同一遍中的折叠行标识（相同的失效）：
            // build_fused_rows’别名-dedup探测这些，
            // 而不是每次击键每个(候选×所见)对调用capkey::norm_row_id。
            c.row_keys.clear();
            c.row_keys.reserve(c.models.size());
            c.display_labels.clear();
            c.display_labels.reserve(c.models.size());
            // 前面几个后端命名空间的网关其ID为ROUTING LANE：Bifrost
            // 服务于`dgpu/granite-4.0-h-tiny` (
            // 离散GPU)和`utils/granite-4.0-h-tiny`
            // (CPU)。解码器会删除命名空间，因为对于托管提供程序而言，
            // 它是噪音，因此 渲染 "Granite 4.0 H Tiny"
            // 和 选择面板 行都显示了两台截然不同的机器的两条
            // 相同的行。
            //
            // 检测该 目录 内的冲突并仅限定需要它的行。
            // 如果对所有内容进行资格认证，则 "openai/"
            // 会排在每个托管模型的前面，但不会带来任何好处；
            // 没有任何资格是错误。 （他们的设置通过重命名模型
            // 在网关上解决了这个问题 - 客户端显示怪癖不应该让
            // 用户花费别名表。）
            std::unordered_map<std::string, int> label_counts;
            for (const auto& mi : c.models)
                ++label_counts[ui::model_display_label(mi.id.value,
                                                       mi.display_name)];
            for (const auto& mi : c.models) {
                // 规范标签（ui::model_display_label)
                // 作为名称段——SAME字符串build_fused_rows
                // 分数和视图渲染——因此缓存的干草堆不会偏离
                // 屏幕上的内容。
                std::string label =
                    ui::model_display_label(mi.id.value, mi.display_name);
                if (label_counts[label] > 1)
                    label = ui::model_display_label_qualified(mi.id.value,
                                                              mi.display_name);
                std::string key = ui::detail::fused_haystack(
                    c.label, label, mi);
                for (char& ch : key)
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                c.search_keys.push_back(std::move(key));
                c.row_keys.push_back(capkey::norm_row_id(mi.id.value));
                c.display_labels.push_back(std::move(label));
            }
        }
        // 记住 per-model 推理 标签:resolved_caps 是
        // shared_mutex 后面的 3 个 注册表 查找 — 对于在大型
        // 目录 上构建的 per-keystroke 行来说太热了。当模型集更改时重新
        // 构建 OR 自我们上次构建以来写入的任何功能 注册表（caps_epoch：
        // models.dev刷新、习得拒绝、^E覆盖）。
        const std::uint64_t epoch = caps_epoch();
        if (c.reason_flags.size() != c.models.size()
            || c.reason_epoch != epoch) {
            c.reason_flags.clear();
            c.reason_flags.reserve(c.models.size());
            for (const auto& mi : c.models)
                c.reason_flags.push_back(effort_capable(
                    resolved_caps(mi.id.value, c.provider_id)));
            c.reason_epoch = epoch;
        }
    }
    m.d.fused_rows = fused_rows_for_model(m);

    // 在重建过程中将 CURSOR 保留在 相同 模型上。
    // 重建可以重新-rank行（异步目录登陆，或者查询更改），
    // 因此旧索引处的行现在可能是DIFFERENT模型 - 后续的
    // ^E/^F/effort编辑将命中错误的行。 Re-find
    // 重建后的快照（提供者、模型）；回落至固定索引。
    if (auto* c = m.ui.panel.get<pn::Models>()) {
        const int n = static_cast<int>(m.d.fused_rows.size());
        if (n == 0) { c->index = 0; return; }
        if (!prev_model.empty()) {
            for (int i = 0; i < n; ++i) {
                const auto& r = m.d.fused_rows[static_cast<std::size_t>(i)];
                if (r.model.id.value == prev_model
                    && r.provider_id == prev_provider) {
                    c->index = i; return;
                }
            }
        }
        if (c->index >= n) c->index = n - 1;
        if (c->index < 0)  c->index = 0;
    }
}

// THE原子开关：使（提供者、模型）激活。
//   ·同一提供商→改变适当的模型（努力re-clamp，坚持，
//     重新获取），没有提供者跳跃。
//   ·跨提供商→commit_provider_switch与型号PRE-STASHED所以
//     漏斗准确地安装了它（原子提供者+模型+认证）。
// 以任一方式将目标记录在MRU中（除非`record`为假，e.g.
// ^Tab环-walk，其不得对MRU mid-cycle重新排序），
// 并且激发切换吐司。
Cmd switch_to_model_ref(Model& m, const ModelRef& ref, bool record = true) {
    const std::string cur_pid = active_provider_id(m);

    // 回合正在运行：其延续和重试必须保留在其开始的模型和提供者上，
    // 或者半回合转到一个模型，一半转到另一个模型（并且交叉
    // -provider选择将旧提供者的模型ID发送到新提供者）。
    // 握住镐并在转弯结束时应用它（publish_derived）。
    // MRU 现在记录了它，所以 ^Tab 看到了它。
    if (m.s.active()) {
        const bool same = ref.provider_id == cur_pid && ref.model_id == m.d.model_id.value;
        if (same) m.ui.pending_switch.reset();   // 所选运行模式：无关
        else      m.ui.pending_switch = ref;
        auto mru = record ? record_recent(m, ref.provider_id, ref.model_id) : Cmd::none();
        auto toast = same ? set_status_toast(m, "already on " + ui::pretty_model_label(ref.model_id),
                                             std::chrono::seconds{3})
                          : set_status_toast(m,
                                ui::pretty_model_label(ref.model_id)
                                    + " \xc2\xb7 from the next turn (this one keeps its model)",
                                std::chrono::seconds{4});
        return Cmd::batch(std::move(mru), std::move(toast));
    }
    m.ui.pending_switch.reset();

    if (ref.provider_id == cur_pid) {
        // 相同的提供商——纯粹的模型改变。
        m.d.model_id    = ModelId{ref.model_id};
        // 丢弃已完成转弯的路由标记。状态徽章更喜欢它而不是选择（在
        // 智能模式转弯期间，路由模型是实际服务的模型），
        // 因此保留它设置意味着选择一个新模型不会改变屏幕上的任何内容，
        // 直到 下一个 转弯覆盖它。下面的 cross
        // -provider 分支从 commit_provider_switch
        // 获取这个；这个分支没有这样的漏斗。
        m.s.smart_turn_model = ModelId{};
        m.s.smart_turn_role.reset();
        m.s.context_max = resolved_context_max(m, ref.provider_id);
        // m.d.effort保持拾取状态；显示并按型号 传输 夹紧它。
        auto save = persist_settings(m);
        auto mru  = record ? record_recent(m, ref.provider_id, ref.model_id)
                           : Cmd::none();
        auto toast = set_status_toast(m,
            ui::pretty_model_label(m.d.model_id.value) + " \xc2\xb7 "
                + provider::provider_display_name(m.d.selection),
            std::chrono::seconds{3});
        // 本地路由器可以以与目录所述不同的大小来服务该模型（或者什么也不说，
        // 直到它是已加载）。现在Re-check；一旦第一个请求 加载，
        // post-turn 探针就会捕获它。
        auto probe = cmd::probe_model_window(m, m.d.model_id.value);
        return Cmd::batch(std::move(save), std::move(mru),
                          std::move(toast), std::move(probe));
    }

    // Cross-provider-通过ONE漏斗的原子开关，型号pre-stashed。
    const provider::ProviderPreset* p = provider::preset_for(ref.provider_id);
    const std::string label = p ? std::string{p->label} : ref.provider_id;
    auto mru = record ? record_recent(m, ref.provider_id, ref.model_id)
                      : Cmd::none();
    return Cmd::batch(std::move(mru),
                      commit_provider_switch(m, ref.provider_id, label,
                                             ref.model_id));
}

// 路由至`provider_id`的登录流程，在Esc上弹出至`origin`。
// 由融合标志-in报价（un-authed提供商行）使用。
// 镜像 ProvidersSelect 用于每种身份验证样式的入口点。
Cmd open_login_for(Model& m, const std::string& provider_id,
                    const std::string& label, ui::login::Origin origin) {
    const provider::ProviderPreset* p = provider::preset_for(provider_id);
    if (p && p->oauth_native) {
        // ChatGPT/Copilot/Kimi:OAuth
        // device/browser流通过范围限定于此提供者的方法菜单。
        ui::login::Picking pk;
        pk.provider = provider_id;
        pk.origin   = std::move(origin);
        m.ui.login = std::move(pk);
        return Cmd::none();
    }
    // 托管API-key(或Anthropic键)：API-key输入，
    // 成功则返回融合选择面板。
    m.ui.login = ui::login::ApiKeyInput{
        .provider       = provider_id,
        .provider_label = label,
        .origin         = std::move(origin),
    };
    return Cmd::none();
}

} // 命名空间结束。

void refresh_open_model_picker(Model& m) {
    if (!m.ui.panel.get<pn::Models>()) return;
    rebuild_fused_rows(m);   // 首先同步源
}

// 在回合期间选取的模型，一旦回合结束就应用。在每个 归并 之后从
// publish_derived 调用，因此它会在 归并 将阶段返回
// 到空闲状态（完成、错误、取消、线程 切换）时触发，而 状态更新函数
// 不必记住它。
Cmd apply_pending_switch(Model& m) {
    if (!m.ui.pending_switch || m.s.active()) return Cmd::none();
    const ModelRef ref = std::move(*m.ui.pending_switch);
    m.ui.pending_switch.reset();
    // 拾取时已记录在MRU中。
    return switch_to_model_ref(m, ref, /*记录=*/false);
}

Cmd models_update(Model& m, msg::ModelsMsg pm) {
    using namespace agentty::msg;

    // 在任何列表改变之后将光标固定到当前行计数。
    auto clamp_cursor = [](Model& mm) {
        if (auto* c = mm.ui.panel.get<pn::Models>()) {
            const int n = static_cast<int>(mm.d.fused_rows.size());
            if (n == 0) { c->index = 0; return; }
            if (c->index < 0)  c->index = 0;
            if (c->index >= n) c->index = n - 1;
        }
    };

    return std::visit(overload{
        [&](OpenModels) -> Cmd {
            hydrate_recents(m);
            if (auto* c = m.ui.panel.get<pn::Models>()) {
                // 已经打开(smart-mode手-off descend()
                // 面板并且THEN调度OpenModels；或^/ re
                // -pressed):保留实例——其assign_slot和`from`
                // 快照——但重置拾取状态，因此re-open从不显示过期过滤器。
                c->index = 0;
                c->query.clear();
            } else {
                // 冷开放：下降，采用任何开放的作为父级。
                m.ui.panel.descend(pn::Models{{0, ""}});
            }
            // 一个 昂贵的通行证：枚举提供商、读取设置、
            // 从其捆绑列表中播种每个经过身份验证的提供商的 目录，
            // 以便 选择面板 立即打开。
            refresh_fused_sources(m);
            rebuild_fused_rows(m);       // 种子 缓存 视图读取
            // PRIORITIZE 活动 提供者：现在单独重新获取
            // ITS 实时 目录，因此您最有可能选择的模型会快速刷新（
            // → ModelsLoaded 重建打开的行）。
            // 其他经过身份验证的提供程序稍后刷新 LAZILY (
            // FusedRefreshOthers)，因此活动结果不会在有
            // 界 工作线程 池 上的较慢提供程序后面排队。仅当延迟通行证
            // 触发时才标记其他人正在加载 - 在这里他们不断展示他们捆绑的种子。
            // 如果其 目录 仍然新鲜，则跳过主动重新获取（快速
            // re-open)，但始终为其他人安排 惰性 波。
            const std::string apid0 = active_provider_id(m);
            bool active_fresh = false;
            for (const auto& cat : m.d.provider_catalogs)
                if (cat.provider_id == apid0) {
                    active_fresh = cat.loaded_at_ms != 0
                                && (now_ms() - cat.loaded_at_ms) <= kCatalogTtlMs;
                    break;
                }
            std::vector<Cmd> boot;
            if (!active_fresh) boot.push_back(cmd::fetch_models(m));
            boot.push_back(Cmd::after(std::chrono::milliseconds{120},
                                                 Msg{FusedRefreshOthers{}}));
            return Cmd::batch(std::move(boot));
        },
        [&](ModelsRefresh) -> Cmd {
            // ^L-强制完全实时刷新：重置每个目录的新鲜度，
            // 以便活动+延迟波全部重新获取，无论TTL如何。
            // 当用户想要最新的时手动逃生舱口。
            if (!m.ui.panel.get<pn::Models>()) return Cmd::none();
            for (auto& c : m.d.provider_catalogs) c.loaded_at_ms = 0;
            const std::string apid = active_provider_id(m);
            std::vector<Cmd> boot;
            boot.push_back(cmd::fetch_models(m));   // 现在有效
            for (auto& c : m.d.provider_catalogs) {
                if (c.provider_id == apid) continue;
                c.state = ProviderCatalog::State::Loading;
                boot.push_back(cmd::fetch_models_for(c.provider_id));
            }
            auto toast = set_status_toast(m, "refreshing models\xe2\x80\xa6",
                                          std::chrono::seconds{2});
            boot.push_back(std::move(toast));
            return Cmd::batch(std::move(boot));
        },
        [&](FusedRefreshOthers) -> Cmd {
            // 惰性第二波：刷新其实时目录为STALE（早于TTL）、
            // FAILED或从未获取的每个OTHER授权提供商。
            // 最近 -fetched 就绪的目录会被单独保留，
            // 因此 re-opening 选取器不会快速重新 -hammer 每个
            // 提供者 - 而是会重新获取已就绪一段时间 IS 的目录，因此列表会保持
            // 最新状态，而不是在首次加载后冻结。如果关闭，则为 No-op。
            if (!m.ui.panel.get<pn::Models>()) return Cmd::none();
            const std::string active_pid = active_provider_id(m);
            const std::int64_t t = now_ms();
            std::vector<Cmd> fetches;
            for (auto& c : m.d.provider_catalogs) {
                if (c.provider_id == active_pid) continue;   // 完成打开
                if (c.state == ProviderCatalog::State::Loading) continue;
                const bool never = c.loaded_at_ms == 0;
                const bool failed = c.state == ProviderCatalog::State::Failed;
                const bool stale = c.loaded_at_ms != 0
                                && (t - c.loaded_at_ms) > kCatalogTtlMs;
                if (!never && !failed && !stale) continue;   // 依然新鲜
                c.state = ProviderCatalog::State::Loading;
                fetches.push_back(cmd::fetch_models_for(c.provider_id));
            }
            if (fetches.empty()) return Cmd::none();
            return Cmd::batch(std::move(fetches));
        },
        [&](CloseModels) -> Cmd {
            m.d.fused_rows.clear();       // 关闭时释放缓存
            Cmd save = Cmd::none();
            if (m.ui.effort_dirty) {
                save = persist_settings(m);
                m.ui.effort_dirty = false;
            }
            // Esc展开一级——ONE路径现在也覆盖了slot-assign：
            // 分配-mode 选择面板的`from` 快照 IS SmartMode
            // 面板（形式、高级、嵌套链），因此ascend()逐字恢复它；
            // Cold-opened 选择面板 恢复调色板或关闭。
            // 放弃的分配模式消失WITH 选择面板值——无需重置。
            // 重新验证（形式可能是 过期）是 ascend() 的工作，
            // 每个 SmartMode 恢复都有一个位置。
            ascend(m);
            return save;
        },
        [&](ModelsMove e) -> Cmd {
            if (auto* c = m.ui.panel.get<pn::Models>())
                c->index = ui::step(c->index, e.delta,
                                    static_cast<int>(m.d.fused_rows.size()));
            return Cmd::none();
        },
        [&](ModelsJump e) -> Cmd {
            if (auto* c = m.ui.panel.get<pn::Models>()) {
                const int n = static_cast<int>(m.d.fused_rows.size());
                // 页面经过完整的 可视区域，因此 PageUp/Down 登陆一个屏幕（
                // 与其他 选择面板' kPage 匹配），而不是在 -shoots
                // ~14-row 可视区域 下的固定 10。
                constexpr int page = 14;  // 与 pickers.cpp 中的 kViewportH 匹配
                using W = ModelsJump::Where;
                switch (e.where) {
                    case W::Home:     c->index = 0; break;
                    case W::End:      c->index = n - 1; break;
                    case W::PageUp:   c->index -= page; break;
                    case W::PageDown: c->index += page; break;
                }
                clamp_cursor(m);
            }
            return Cmd::none();
        },
        [&](ModelsFilterInput e) -> Cmd {
            if (auto* c = m.ui.panel.get<pn::Models>()) {
                // 每个可打印的——包括数字——输入到查询中。
                // 仅 ASCII — model/provider id 实际上是 ASCII。
                if (e.ch >= 0x20 && e.ch < 0x7f) {
                    c->query.push_back(static_cast<char>(e.ch));
                    c->index = 0;
                    // 查询已改变——仅re-rank； auth 未更改，因此跳过源 re-sync
                    // 包括清理和逐提供方的认证状态查询。
                    rebuild_fused_rows(m, /*sync_sources=*/false);
                    clamp_cursor(m);
                }
            }
            return Cmd::none();
        },
        [&](ModelsFilterBackspace) -> Cmd {
            if (auto* c = m.ui.panel.get<pn::Models>(); c && !c->query.empty()) {
                c->query.pop_back();
                c->index = 0;
                rebuild_fused_rows(m, /*sync_sources=*/false);   // 仅re-rank
                clamp_cursor(m);
            }
            return Cmd::none();
        },
        [&](FusedCatalogLoaded e) -> Cmd {
            // 就地合并，由 provider_id 保护（在 -fetch
            // 中签出的提供商根本不再出现在列表中）。
            for (auto& c : m.d.provider_catalogs) {
                if (c.provider_id != e.provider_id) continue;
                c.error = std::move(e.error);
                if (e.ok && !e.models.empty()) {
                    c.models = std::move(e.models);
                    // 与活动提供者的加载相同的烘烤。如果没有它，
                    // 来自 NON-active 提供商的行仅包含
                    // 其网关所宣传的内容，因此融合的 选择面板
                    // 对于窗口 models.dev 声明的模型显示
                    // "auto" — 并且状态栏将在您切换到它时
                    // 显示真实的数字。范围是 ROW 的提供商，
                    // 而不是活动提供商：两个网关后面的相同 ID 可以
                    // 以两种不同的大小提供服务。
                    {
                        const auto& settings = m.d.persisted;
                        for (auto& mi : c.models)
                            ui::bake_context_window(mi, c.provider_id, settings,
                                                    m.env.max_context_tokens);
                    }
                    c.invalidate_derived();  // ID 已更改 — 所有 缓存 过期
                    c.state  = ProviderCatalog::State::Ready;
                    c.loaded_at_ms = now_ms();   // 标记新鲜
                } else {
                    c.state  = e.ok ? ProviderCatalog::State::Ready
                                    : ProviderCatalog::State::Failed;
                    if (e.ok) c.loaded_at_ms = now_ms();
                }
                break;
            }
            // 只有融合后的选择面板自身的列表依赖于合并后的目录；如果它是开放的，
            // 则重建它（便宜，每个解析提供者一次）。 目录 的到来并没有改变 AUTH，
            // 因此跳过源 re-sync（修剪 + per-provider
            // auth stat） - 只需 re-rank + 重建密钥。
            if (m.ui.panel.is<pn::Models>()) {
                rebuild_fused_rows(m, /*sync_sources=*/false);
                clamp_cursor(m);
            }
            return Cmd::none();
        },
        [&](ModelsToggleFavorite) -> Cmd {
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c || c->index < 0
                || c->index >= static_cast<int>(m.d.fused_rows.size()))
                return Cmd::none();
            const auto& row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            if (row.is_signin_offer()) return Cmd::none();
            auto& s = m.d.persisted;
            const std::string key = favorite_key(row.provider_id, row.model.id.value);
            const bool was_fav = is_favorite(s.favorite_models, row.provider_id,
                                             row.model.id.value);
            const bool now_fav = !was_fav;
            if (now_fav) {
                s.favorite_models.push_back(ModelId{key});
            } else {
                // 取消该提供者模型的星号。每个提供商的副本都
                // 来自较旧版本的裸露条目；转换它，
                // 以便其他人保留他们的星星，只有这个人失去它。
                const std::string bare = row.model.id.value;
                const bool had_bare = std::erase_if(s.favorite_models,
                    [&](const ModelId& f) { return f.value == bare; }) > 0;
                std::erase_if(s.favorite_models,
                    [&](const ModelId& f) { return f.value == key; });
                if (had_bare)
                    for (const auto& c : m.d.provider_catalogs)
                        if (c.provider_id != row.provider_id)
                            for (const auto& mi : c.models)
                                if (mi.id.value == bare)
                                    s.favorite_models.push_back(
                                        ModelId{favorite_key(c.provider_id, bare)});
            }
            auto save = save_record(m);
            const std::string active_pid = active_provider_id(m);
            for (auto& mi : m.d.available_models)
                mi.favorite = is_favorite(s.favorite_models, active_pid, mi.id.value);
            // 加星将模型移入（或移出）顶部的FAVORITES。
            // rebuild_fused_rows将光标保持在相同的（提供商、
            // 型号）上，因此它跟随您刚刚加星标的行。
            rebuild_fused_rows(m, /*sync_sources=*/false);
            clamp_cursor(m);
            return save;
        },
        [&](ModelsCycleEffort e) -> Cmd {
            // ←/→沿着HIGHLIGHTED模型的推理-effort阶梯（关→低→
            // 中→高......在其大写以内），突变GLOBAL m.d.effort
            // LIVE——与经典模型选择面板的ModelsCycleEffort完全相同。
            // 两个表面共享 m.d.effort，因此此处的更改同时显示
            // 在各处（没有分段拆分，没有 "off in one, on in the other"）。
            // 通过effort_dirty懒惰地坚持。
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c || c->index < 0
                || c->index >= static_cast<int>(m.d.fused_rows.size()))
                return Cmd::none();
            const auto& row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            if (row.is_signin_offer()) return Cmd::none();
            // 在 ROW 的提供者下解决 — 当 Mistral 处于活动状态时突出显示的
            // Groq 行必须走 Groq 的梯子，而不是 Mistral 的梯子。
            const auto caps = resolved_caps(row.model.id.value,
                                            row.provider_id);
            if (!effort_capable(caps)) return Cmd::none();
            m.d.effort = cycle_effort(clamp_effort(m.d.effort, caps),
                                      e.delta, caps);
            m.ui.effort_dirty = true;
            return Cmd::none();
        },
        [&](ModelsCycleContext e) -> Cmd {
            // ^W沿着kContextLadder步进HIGHLIGHTED模型的
            // context-window覆盖，环绕。 "auto" 为梯级 0，
            // 因此整圈始终返回到 "let agentty decide"
            // — 控制装置会自行清除，无需单独重置。
            //
            // 范围限定于ROW的提供商，而不是活动的提供商：
            // 两个网关后面的相同模型ID可以由两个不同的窗口提供服务，
            // 并且覆盖必须指定哪个窗口。
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c || c->index < 0
                || c->index >= static_cast<int>(m.d.fused_rows.size()))
                return Cmd::none();
            const auto& row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            if (row.is_signin_offer()) return Cmd::none();

            // 复制我们需要的任何重建的BEFORE：`row`是对m.d.fused_rows的引用，
            // 并且下面的rebuild_fused_rows
            // ()重新分配该向量。
            const ModelId    row_model    = row.model.id;
            const std::string row_provider = row.provider_id;

            auto& settings = m.d.persisted;
            const std::string key =
                ui::context_override_key(row_provider, row_model.value);
            const auto it = settings.context_overrides.find(key);
            const int cur = it == settings.context_overrides.end() ? 0 : it->second;

            constexpr int n = static_cast<int>(std::size(ui::kContextLadder));
            int idx = 0;
            for (int i = 0; i < n; ++i)
                if (ui::kContextLadder[i] == cur) { idx = i; break; }
            idx = ((idx + e.delta) % n + n) % n;
            const int next = ui::kContextLadder[idx];

            if (next <= 0) settings.context_overrides.erase(key);
            else           settings.context_overrides[key] = next;
            auto save = save_record(m);

            // 将变化反映在LIVE 目录中，然后重建视图读取的行。
            // 如果没有这个设置，直到下一次 目录 获取之前，
            // 该设置都是不可见的 - 用户按下 ^W，该列不会移动，
            // 并且该功能看起来已损坏。
            //
            // 选择面板视图渲染 m.d.fused_rows，
            // 其中build_fused_rows从每个提供商的CATALOG(
            // c->模型)组装而成——NOT从available_models组装。
            // available_models是ACTIVE提供商的SSOT（路由池，
            // 状态栏），但它只是目录的深层-copied SOURCE，
            // 在打开时镜像一次。单独改变它（或改变 目录 而不重建）正是
            // "^W does nothing" 错误：视图绘制的行永远
            // 不会改变。因此：保持 available_models 与
            // 活动提供者保持同步，将突出显示的行的 自身 目录 进行突变，
            // 然后重建一次。
            auto apply_to = [&](ModelInfo& mi) {
                if (next > 0) {
                    mi.context_window = next;
                } else {
                    // 清除：通过 完整 梯子的 re-resolve（覆盖
                    // 现在已删除，此处公布的数字不可恢复 — 它在 加载 被
                    // 覆盖 — 因此传递 0 并让 models.dev
                    // / 端点 -origin / from_id /
                    // 默认填写）。真正的-advertised值在下次刷新时返回-^L，
                    // 或下次切换。
                    mi.context_window = ui::resolve_context_window(
                        row_provider, mi.id.value, 0, settings,
                        m.env.max_context_tokens);
                }
            };

            const bool row_is_active = row_provider == active_provider_id(m);
            if (row_is_active) {
                // SSOT对于活动提供商：路由候选池和状态
                // -bar仪表读取该信息，因此保持同步。
                for (auto& mi : m.d.available_models)
                    if (mi.id == row_model) { apply_to(mi); break; }
            }

            // 实际读取的融合行为目录。改变该行的 自身
            // 提供程序 目录，以便 选择面板 的 ctx
            // 列立即移动，无论突出显示的行属于哪个主机。
            for (auto& cat : m.d.provider_catalogs) {
                if (cat.provider_id != row_provider) continue;
                for (auto& mi : cat.models)
                    if (mi.id == row_model) { apply_to(mi); break; }
                break;
            }
            rebuild_fused_rows(m, /*sync_sources=*/false);

            // 当该行是实际使用的模型时应用LIVE，
            // 因此ctx-%仪表随设置移动而不是在重新启动后移动。
            if (row_is_active && row_model == m.d.model_id)
                m.s.context_max = resolved_context_max(m, row_provider);

            // 将窗口命名为 和，它现在来自哪里。
            // "auto"本身是不明确的——它没有说明
            // 自动解析为什么，这是用户实际试图检查的数字。
            std::string note = ui::pretty_model_label(row_model.value)
                             + " \xc2\xb7 context ";
            if (next > 0) {
                note += ui::context_window_label(next);
            } else {
                note += "auto";
                // 使用advertisement = 0而不是行
                // 的窗口进行解析：该行已经将OLD覆盖纳入其中（
                // 在目录 加载处应用），因此传递它将使"auto"
                // 报告刚刚清除的数字。 id-inference
                // 和默认层是自动回退到此处的层；
                // 公布的数字将于下一个 目录 加载 返回。
                const int auto_win = ui::resolve_context_window(
                    row_provider, row_model.value, 0, settings,
                    m.env.max_context_tokens);
                if (auto_win > 0)
                    note += " (" + ui::context_window_label(auto_win) + ")";
            }
            return Cmd::batch(std::move(save), set_status_toast(m, std::move(note)));
        },
        [&](ModelsToggleReasoning) -> Cmd {
            // ^E通过其tri-state(自动→ON→OFF→自动)翻
            // 转突出显示模型的per-model推理OVERRIDE。
            // 镜像模型 选择面板，因此调整可以在移动到融合表面后继续进行。
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c || c->index < 0
                || c->index >= static_cast<int>(m.d.fused_rows.size()))
                return Cmd::none();
            const auto& row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            if (row.is_signin_offer()) return Cmd::none();
            const std::string id = row.model.id.value;
            // Claude/GPT是FAMILY-GATED：他们的努力
            // 阶梯是从模型系列解码的并且不是用户-editable，
            // 因此这里的覆盖将是存储的no-op，其默默地遮蔽真正的阶梯。
            // 相反，提示和保释——←/→是适合他们的控制。
            // （经典的选择面板有这个门；当手臂被移植时它被丢弃，
            // 让^E坚持Opus/GPT.的垃圾覆盖）
            {
                const auto base = ModelCapabilities::from_id(id);
                if (base.is_known_family()
                    || base.family == ModelCapabilities::Family::Gpt) {
                    auto toast = set_status_toast(m,
                        "reasoning effort is model-managed here "
                        "(\xe2\x86\x90/\xe2\x86\x92 to set the tier)");
                    return toast;
                }
            }
            // 范围限定于ROW的提供商：Groq和自定义
            // 网关背后的相同id可能在推理上不一致，
            // 并且裸键翻转两者。首次编辑时会迁移较旧的裸覆盖。
            const std::string key = scoped_caps_key(id, row.provider_id);
            const int cur = reasoning_override_for(id, row.provider_id);
            auto& s = m.d.persisted;
            // 应用于每个提供商的遗留裸覆盖（任何拼写）；
            // 删除它，以便该提供商的编辑独立存在。
            const std::string bare = capkey::norm_tail(id);
            std::erase_if(s.reasoning_effort_overrides, [&](const auto& kv) {
                return kv.first.find('/') == std::string::npos
                    && norm_caps_key(kv.first) == norm_caps_key(bare);
            });
            clear_reasoning_override(norm_caps_key(bare));
            const char* label = nullptr;
            if (cur < 0) {
                s.reasoning_effort_overrides[key] = true;
                set_reasoning_override(key, true);
                label = "reasoning: forced ON for this model";
            } else if (cur == 1) {
                s.reasoning_effort_overrides[key] = false;
                set_reasoning_override(key, false);
                label = "reasoning: forced OFF for this model";
            } else {
                s.reasoning_effort_overrides.erase(key);
                clear_reasoning_override(key);
                label = "reasoning: auto (catalog default)";
            }
            auto save = save_record(m);
            auto toast = set_status_toast(m, label);
            return Cmd::batch(std::move(save), std::move(toast));
        },
        [&](SwitchToPreviousModel) -> Cmd {
            // ^Tab 最近使用循环：沿最近使用环移动到
            // progressively OLDER models — A → B → C → D → A — not
            // a single A↔B 切换. The switch does 不 reorder the
            // 不会重写该环，record=false，因此每次查找当前模型的
            // 索引都会自然前进一步。
            // step;跑完一圈就可以回家了。 （TUI 无法看到 Ctrl 版本，并且没有空闲滴答，
            // 因此稳定的 no-reorder 遍历是此处执行 Alt-Tab 语义的可靠方法 - 不需要提交截止日期。）
            hydrate_recents(m);
            const auto& ring = m.d.recent_models;
            if (ring.size() < 2) return Cmd::none();  // 没什么可循环的
            const ModelRef active{active_provider_id(m), m.d.model_id.value};
            const int n = static_cast<int>(ring.size());
            int cur = 0;
            for (int i = 0; i < n; ++i)
                if (ring[static_cast<std::size_t>(i)] == active) { cur = i; break; }
            // 前进到下一个 LIVE 条目 — 跳过其提供商不再
            // 经过验证或其模型已被除名的环成员，因此 ^Tab
            // 永远不会切换到失效 ID（每个请求 400 秒）。
            // 最多一整圈；如果我们回到活动行则停止。
            const auto& settings = m.d.persisted;
            ModelRef target;
            for (int step = 1; step <= n; ++step) {
                const ModelRef& cand =
                    ring[static_cast<std::size_t>((cur + step) % n)];
                if (cand == active) break;                 // 搭接，无活体
                if (cand.empty()) continue;
                if (mru_ref_is_live(m, cand, settings)) { target = cand; break; }
            }
            if (target.empty()) return Cmd::none();
            return switch_to_model_ref(m, target, /*记录=*/false);
        },
        [&](ModelsSelect) -> Cmd {
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c || c->index < 0
                || c->index >= static_cast<int>(m.d.fused_rows.size()))
                return Cmd::none();
            const FusedRow row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            // 将分配模式复制出 之前 任何关闭：`c` 指向 变体，
            // 关闭会破坏替代方案（旧的顺序通过悬空
            // 指针读取它 — 与调色板的 o->index
            // 错误的形状相同）。
            const std::optional<smart::ModelRole> assigning = c->assign_slot;
            m.d.fused_rows.clear();
            // ←/→已突变m.d.effort活； the switch
            // 下方会持久化设置，无需单独应用。
            m.ui.effort_dirty = false;

            if (row.is_signin_offer()) {
                // 登录该提供商的路由，之后返回此处。
                m.ui.panel.close<pn::Models>();
                return open_login_for(m, row.provider_id,
                                      row.label,
                                      ui::login::origin::Models{});
            }

            // 智能模式slot-assign模式：将所
            // 选模型写入目标角色槽而不是切换活动模型，
            // 然后弹回父智能模式选择面板。
            //
            // 老虎机模型的范围为ACTIVE-PROVIDER：smart::resolve_role将固定的ID直接交给回合的请求，
            // 该请求被分派给任何活动的提供者。 Pinning a row
            // from a different provider would
            // 因此会把未知的模型标识发送到
            // 当前端点。槽位分配
            // 模式将行列表过滤到活动提供程序（请参阅 rebuild_fused_rows），
            // 因此这样的行从一开始就无法选择 - 验证了无法表示
            // 的节拍。
            if (assigning) {
                const smart::ModelRole assigned = *assigning;
                // 角色到字段的唯一映射，供所有
                // 读写位置共享，见 RoleConfig::slot。
                // 曾经住在这里的 0/1/2 上的开关是它的第三个副本。
                smart::RoleConfig cfg = m.d.smart;
                smart::SlotOverride& slot = cfg.slot(assigned);
                slot.model = row.model.id.value;
                slot.set   = true;
                // 记录设置此固定模型时对应的提供方。
                // 模型标识只有对实际提供它的
                // 端点才有意义，因此 resolve_role
                // replays the pin 仅 under this provider
                // (see SlotOverride::provider).更喜欢该行自己
                // 的提供者 - 在 slot-assign 模式下，列表已经过滤到活动的提供者，
                // 因此他们同意，但该行是更直接的事实。
                slot.provider = row.provider_id.empty()
                                  ? active_provider_id(m)
                                  : row.provider_id;
                cfg.enabled = true;   // 固定槽意味着"on"

                // 一个 entry point: persists, installs on the UI 线程 and
                // 发布到子代理路由。固定模型后 task 却继续
                // 使用旧模型，正是过去各处手写
                // persist_settings here used to do.
                auto slot_save = apply_smart(m, std::move(cfg));
                m.d.fused_rows.clear();
                // 弹回父智能模式面板——选择面板的`from` 快照，
                // 由ascend()恢复+重新验证（其从刚刚
                // 应用的配置重建表单）——光标位于我们刚刚设置的槽上。
                // 您可能也想要同级插槽；每个插槽的 re
                // -opening 智能模式是此修复的
                // 单调乏味。
                ascend(m);
                if (auto* sm = m.ui.panel.get<pn::SmartMode>())
                    smart_form::focus_role(sm->form, assigned);
                auto toast = set_status_toast(m, "Smart Mode slot set");
                return Cmd::batch(std::move(slot_save), std::move(toast));
            }

            // 普通选择：选择完成后直接关闭，
            // 而不返回上一层。Esc 表示返回；
            // 若选中模型后又退回命令面板，
            // 会让用户以为选择没有生效。
            m.ui.panel.close<pn::Models>();
            return switch_to_model_ref(m, row.ref());
        },
        [&](ModelsLoaded& e) -> Cmd {
            // STALENESS GATE: only accept a 载荷 fetched FOR the provider
            // that is active 当前. Two quick switches interleave their
            // slow fetches; without this, provider A's late 目录 lands under
            // provider B and the 选择面板 offers models B cannot 数据流.
            // (空 provider_id = legacy/synthetic 分派 — 接受。)
            if (!e.provider_id.empty()
                && e.provider_id != active_provider_id(m)) {
                return Cmd::none();   // 保留models_loading:
                                             // 更新的获取仍在进行中
            }
            // 获取结束后，无论成功还是失败，都必须清除
            // the in-flight 标志 so the 选择面板 leaves "Loading models…".
            m.s.models_loading = false;
            // 记住它失败的原因，以便选择面板可以这么说。
            const std::string loaded_for =
                e.provider_id.empty() ? active_provider_id(m) : e.provider_id;
            for (auto& c : m.d.provider_catalogs)
                if (c.provider_id == loaded_for) c.error = e.error;
            m.s.models_error = e.error;
            // 失败或空的获取不得将先前的列表保留在适当的位置：
            // 它可以是另一个提供者的（自定义主机下的Anthropic
            // 启动种子），并且从中挑选将提供者的
            // 模型ID发送到这里。
            if (e.models.empty() && !m.d.available_models.empty()) {
                m.d.available_models.clear();
                for (auto& c : m.d.provider_catalogs)
                    if (c.provider_id == loaded_for) {
                        c.models.clear();
                        c.invalidate_derived();
                        c.state = e.error.empty() ? ProviderCatalog::State::Ready
                                                  : ProviderCatalog::State::Failed;
                    }
                rebuild_fused_rows(m);
            }
            // 失败的提取将其原因表现为短暂的吐司——从来不作为
            // StreamError，这将供给实时回合的 重试
            // 机器（参见 ModelsLoaded 消息注释）。
            if (!e.error.empty() && e.models.empty()) {
                std::string who = provider::provider_display_name(m.d.selection);
                std::string text = e.error.starts_with(who)
                    ? std::move(e.error) : who + ": " + e.error;
                auto toast = set_status_toast(m, std::move(text),
                                              std::chrono::seconds{8});
                return toast;
            }
            if (e.models.empty()) return Cmd::none();
            auto& settings = m.d.persisted;
            // 由下面的 persist-on-success 分支设置；
            // 批量进入该臂的返回，因此经过验证的 --provider 规范实际上已到达磁盘。
            Cmd proven_save = Cmd::none();
            // PERSIST-ON-SUCCESS：在启动时注册为未经验证的
            // 自定义 --provider 规范变得粘性 当前 — 主机回答了
            // 非 -empty 模型获取，因此它是真正的端点，而不是拼写错误。
            // 预设一如既往地保留在解析时间；这仅针对原始
            // host/URL 规格触发，每个进程最多触发一次。
            if (m.ui.unproven_spec && m.ui.unproven_spec->first == active_provider_id(m)) {
                auto proven = std::exchange(m.ui.unproven_spec, std::nullopt);
                settings.provider = proven->first;
                if (!proven->second.empty())
                    settings.provider_models[proven->first] = proven->second;
                // 也给它一个 选择面板 行，与在应用程序中添加主机相同：
                // saved_custom_hosts()从 provider_keys 构建行，
                // 因此如果没有这个，--provider 主机永远不会显示其模型。
                // 预设被过滤掉，因此这对他们来说是安全的。
                settings.provider_keys.try_emplace(proven->first, "");
                proven_save = save_record(m);
            } else if (const std::string pid = active_provider_id(m);
                       !e.models.empty() && provider::preset_for(pid) == nullptr
                       && m.d.selection.kind == provider::Kind::OpenAI
                       && !settings.provider_keys.contains(pid)) {
                // 在设置中手动（或通过旧版本）设置为提供者的自定义
                // 主机回答：给它一个 已保存 行，以便 选择面板 列出它，
                // 并且状态栏不会显示 "sign in"。
                settings.provider_keys.try_emplace(pid, "");
                proven_save = save_record(m);
            }
            m.d.available_models.clear();
            for (auto& mi : e.models) {
                // DISCOVERED权利：该账户已经在context-1m
                // beta (
                // "long context beta is not available for this subscription")（当前订阅未开放长上下文测试功能。）
                // 上400，因此提供`[1m]`行只会出售传输将拒绝的窗口。
                // 仅 OAuth 无法告诉我们（令牌不包含权利字段）——
                // 标志 是从第一次拒绝中获知的，并在 sign-out/account
                // 交换机上清除。
                if (entitlement_blocked(
                        settings, m.d.auth, domain::entitlement::Fact::Context1M,
                        active_provider_id(m), wire_model_id(mi.id.value))
                    && mi.id.value.find("[1m]") != std::string::npos)
                    continue;
                mi.favorite = is_favorite(settings.favorite_models,
                                          e.provider_id.empty() ? active_provider_id(m)
                                                                : e.provider_id,
                                          mi.id.value);
                // 标记该行将实际显示的窗口。此处应用了 一次，
                // 因此每个 下游 读取器（选择面板 列、
                // 融合行、路由候选 池）都可以看到有效编号，
                // 而无需每个人都记住梯子。
                //
                // FULL梯子，不仅仅是覆盖：网关不做广告的
                // 一行用来保留0和渲染 "auto"，
                // 而状态栏显示同一型号的models.dev数字，
                // 因此两个屏幕在用户试图验证的数字方面
                // 相互矛盾(sail3r，PR#39)。
                ui::bake_context_window(mi,
                                        e.provider_id.empty()
                                            ? active_provider_id(m)
                                            : e.provider_id,
                                        settings, m.env.max_context_tokens);
                m.d.available_models.push_back(std::move(mi));
            }
            // 子代理路由器的候选池、
            // 智能模式策略和提供商遵循m.d.available_models/m.d.smart和活动提供商：
            // 分派 接口边界在此归并 (update.cpp，
            // publish_derived)之后发布它们，
            // 因此读取-only角色路由到THIS提供商提供的最便宜的有能力的模型，
            // 而无需状态更新函数记住推动他们。
            //
            // 用户的模型是该提供商提供的吗？
            //
            // 这用于比较 字节 的 ids 字节，并且如果未命中，
            // 则切换到 目录 的 首次 型号和作为提供商回忆的 保存。
            // 遗漏是常见的并且不是切换的理由：上面的权利过滤
            // 器丢弃的`[1m]` 变体、不同的-spelled别名、
            // 仅列出其一些型号的网关。因此，一秒钟后，
            // 镐默默地变成了另一个模型，并在重新启动后
            // 保持这种状态。
            //
            // 现在：
            //   ·匹配折叠id(大小写、分隔符、`[1m]`)，相同
            //     选择面板行使用的标识；
            //   ·账户不能使用的`[1m]`回退到其自己的基础
            //     模型，不以任何排序为先；
            //   ·任何其他未列出的型号是KEPT（它可能很好地工作：-m，a
            //     fine-tune，部分列表）并且用户被告知一次；
            //     真正的 "no such model" 在转牌圈出现时有其自身的错误；
            //   ·仅填写EMPTY型号(尚未选择任何东西)。
            const auto folded = [](std::string_view id) { return capkey::norm_model(id); };
            const std::string want = m.d.model_id.value;
            const ModelInfo* exact = nullptr;
            const ModelInfo* same_model = nullptr;   // 同型号，其他变体
            for (const auto& mi : m.d.available_models) {
                if (mi.id.value == want) { exact = &mi; break; }
                if (!same_model && !want.empty() && folded(mi.id.value) == folded(want))
                    same_model = &mi;
            }
            Cmd unlisted_note = Cmd::none();
            const auto adopt = [&](const ModelId& id) {
                m.d.model_id = id;
                // 与选择面板路径相同的原因：徽章更喜欢
                // 实时路由标记，因此来自前一回合的
                // 标记在目录纠正后将继续命名OLD模型。
                m.s.smart_turn_model = ModelId{};
                m.s.smart_turn_role.reset();
                proven_save = Cmd::batch(std::move(proven_save), persist_settings(m));
            };
            // 本地服务器(Ollama、LM Studio)准确地
            // 列出了它具有已加载的内容，因此它未列出的模型不能在那里运行。
            // 通常是其他提供商留下的召回。
            const bool list_is_complete =
                m.d.selection.row && m.d.selection.row->is_local;
            // Ollama将`llama3`用作`llama3:latest`。
            const bool listed_as_latest = list_is_complete && !want.empty()
                && std::any_of(m.d.available_models.begin(), m.d.available_models.end(),
                               [&](const ModelInfo& mi) { return mi.id.value == want + ":latest"; });
            if (exact || listed_as_latest || m.d.available_models.empty()) {
                // 提供为-is（或没有可比较的）：保留它。
            } else if (want.empty() || (list_is_complete && !same_model)) {
                // 尚未为此主机选择型号。原始 /v1/models
                // 转储首先从任何类型开始（SovereignEG：
                // 4k-window Qwen3-0.6B，
                // 然后嵌入模型），因此选择代理可以实际驱动的第一个：具有
                // 系统 提示词 和工具空间的聊天模型。退回到第一个条目。
                const ModelInfo* pick = nullptr;
                for (int min_ctx : {32000, 8192, 0}) {
                    for (const auto& mi : m.d.available_models) {
                        if (!is_dispatchable_model(mi.id.value)) continue;
                        if (mi.context_window > 0 && mi.context_window < min_ctx) continue;
                        pick = &mi;
                        break;
                    }
                    if (pick) break;
                }
                adopt(pick ? pick->id : m.d.available_models.front().id);
                if (!want.empty())
                    unlisted_note = set_status_toast(m,
                        ui::pretty_model_label(want) + " isn't on "
                            + provider::provider_display_name(m.d.selection)
                            + " \xc2\xb7 using " + ui::pretty_model_label(m.d.model_id.value),
                        std::chrono::seconds{6});
            } else if (same_model) {
                // 以其他名称或窗口规格提供：
                // 仅当当前持有的是该模型的 [1m] 版本时切换，
                // account can't use.普通别名已在 传输 上使用，因此无需处理。
                const bool blocked_1m =
                    want.find("[1m]") != std::string::npos
                    && entitlement_blocked(settings, m.d.auth,
                                           domain::entitlement::Fact::Context1M,
                                           active_provider_id(m), wire_model_id(want));
                if (blocked_1m) adopt(ModelId{wire_model_id(want)});
            } else if (m.ui.unlisted_model_noted != want) {
                m.ui.unlisted_model_noted = want;
                unlisted_note = set_status_toast(m,
                    ui::pretty_model_label(want) + " isn't in "
                        + provider::provider_display_name(m.d.selection)
                        + "'s model list \xc2\xb7 keeping it",
                    std::chrono::seconds{6});
            }
            proven_save = Cmd::batch(std::move(proven_save), std::move(unlisted_note));
            // 活动模型可能仍然有效，在这种情况下旧分支没有刷新其上下文上限。 Codex publishes
            // 272K 上下文窗口，而非 Agentty 的通用
            // 200K 回退), and the status-bar gauge must reflect
            // 应立即采用该值。经由
            // resolver so a user override outranks the 目录:
            // 网关少报或不报告上限时，
            // 不得覆盖用户显式设置的值。
            m.s.context_max = resolved_context_max(m, active_provider_id(m));
            // 如果FUSED 选择面板打开，则活动提供者的目录刚刚改变（
            // available_models是其源），因此其行是过期——重建它们。
            // rebuild_fused_rows re-mirrors
            // available_models 进入活动的 目录 和 re-ranks，
            // 因此会出现一个新的 -listed 模型（e.g.刚刚添加的实时 /
            // v1/models 获取之一），而无需重新打开 选择面板。将光标锁定到新的行数。
            if (auto* c = m.ui.panel.get<pn::Models>()) {
                // 活动提供者的实时目录刚刚完成——将其目录
                // 标记为新鲜的，以便TTL刷新不会立即重新获取它，
                // 然后重建打开的行。
                const std::string apid = active_provider_id(m);
                for (auto& cat : m.d.provider_catalogs)
                    if (cat.provider_id == apid) { cat.loaded_at_ms = now_ms(); break; }
                rebuild_fused_rows(m);
                const int n = static_cast<int>(m.d.fused_rows.size());
                if (n == 0) c->index = 0;
                else if (c->index >= n) c->index = n - 1;
                else if (c->index < 0) c->index = 0;
            }
            return proven_save;
        },
        [&](ModelWindowProbed& e) -> Cmd {
            // 过期（提供商同时切换）或没有测量到：保留
            // 我们拥有的。 0 仅表示模型尚未驻留。
            if (e.window <= 0 || e.provider_id != active_provider_id(m))
                return Cmd::none();
            bool changed = false;
            for (auto& mi : m.d.available_models)
                if (mi.id.value == e.model_id && mi.context_window != e.window) {
                    mi.context_window = e.window;
                    changed = true;
                }
            // 即使数字没有移动也重新计算：我们对主机的了解可能
            // 有（第一轮发现"ollama"行实际上是llama.cpp，
            // #73），并且这改变了窗口的读取方式。
            if (m.d.model_id.value == e.model_id)
                m.s.context_max = resolved_context_max(m, e.provider_id);
            if (!changed) return Cmd::none();
            if (auto* c = m.ui.panel.get<pn::Models>()) {
                rebuild_fused_rows(m);
                const int n = static_cast<int>(m.d.fused_rows.size());
                if (n == 0) c->index = 0;
                else if (c->index >= n) c->index = n - 1;
            }
            return Cmd::none();
        },
        [&](ModelsScopeProvider&) -> Cmd {
            // ^/ — 将列表限制为 仅 突出显示的行的提供者，或者清除范围
            // （如果它已经处于活动状态）。所选行的提供者是用户表示的
            // "the current selection's provider"。
            auto* c = m.ui.panel.get<pn::Models>();
            if (!c) return Cmd::none();
            if (!c->provider_scope.empty()) {
                // Already scoped — 切换 back to all providers.
                c->provider_scope.clear();
                rebuild_fused_rows(m);
                clamp_cursor(m);
                auto toast = set_status_toast(m, "scope: all providers");
                return toast;
            }
            const int n = static_cast<int>(m.d.fused_rows.size());
            if (c->index < 0 || c->index >= n) return Cmd::none();
            const auto& row = m.d.fused_rows[static_cast<std::size_t>(c->index)];
            // 登录入口或没有提供方的行不能用于限定范围。
            if (row.provider_id.empty()) return Cmd::none();
            const std::string pid   = row.provider_id;
            const std::string label = row.label.empty() ? pid : row.label;
            // rebuild_fused_rows 重新分配向量后，row 引用
            // 已经失效；pid/label 是副本，因此后续不再访问 row。
            c->provider_scope = pid;
            // rebuild_fused_rows re
            // -finds是通过标识突出显示的型号，并且它始终在其自己的提供
            // 商列表中，因此光标停留在其上而不是跳到顶部。
            rebuild_fused_rows(m);
            clamp_cursor(m);
            auto toast = set_status_toast(
                m, "scope: " + label + " only (^/ to clear)");
            return toast;
        },
    }, pm);
}

} // namespace agentty::app::detail
