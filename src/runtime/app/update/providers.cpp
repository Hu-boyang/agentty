// providers.cpp — 提供者 选择面板 的 状态更新函数:选择活动后
// 端的 live-switches 行（不重新启动）。从 panels.cpp 拆分；
// self-contained（没有 fused/MRU 机器）。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/platform/io.hpp>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/acp_agents.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/auth/vault.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/provider_rows.hpp"
#include "agentty/runtime/view/cache.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/subagent.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace {
// Re-enter整个-Msg 状态更新函数，就位。
//
// 这些行分派跨FIVE不同的域（配置文件、抹布、智能、外观、
// 元），这正是它们遍历top-level 状态更新函数
// 而不是每个命名域状态更新函数的原因：行的操作IS在分派表中。
// update() 仍然返回一对，所以在这里解压它——
// 一个地方而不是每一行。
[[nodiscard]] Cmd reenter(Model& m, Msg msg) {
    auto [next, cmd] = agentty::app::update(std::move(m), std::move(msg));
    m = std::move(next);
    return std::move(cmd);
}
} // 命名空间结束。


namespace pick = agentty::ui::pick;
using maya::overload;
// ── Provider 选择面板 国 务 院 ────────────────────────────────────────────────────────
// 选择活动后端的 live-switches 行：解析，
// 将预设 ID 放入选择中，安装它（process-global），
// 保留它，将 Deps 身份验证交换到新提供者解析的 凭据，
// 并启动新的模型获取，以便模型列表反映新的后端。没有重新启动。
Cmd providers_update(Model& m, msg::ProvidersMsg pm) {
    // 选择面板的行是ONE有序列表（预设+ACP代理+已保存自
    // 定义主机+"Custom host…"哨兵）。光标是 此
    // 列表的索引 - 没有偏移数学，视图 渲染 的列表也是如此。
    //
    // 计算ON DEMAND，从未快照。 Arms 变异 p
    // ->query 和 p->show_all，进入时捕获的
    // 列表将描述状态 之前 变异：`n` 会将光标固定到旧长度，
    // 因此在 ^A 扩展列表后，向下箭头提前停止，显示行上的
    // Enter/^D 不执行任何操作。读起来就像一把脆弱的钥匙。
    // 直播Model的函数不能走过期。
    const auto& settings = m.d.persisted;
    const std::vector<std::string> saved_custom_hosts =
        provider::saved_custom_hosts(settings.provider_keys);
    auto build = [&] {
        const auto* p = m.ui.panel.get<pn::Providers>();
        return ui::build_sectioned_provider_rows(
            saved_custom_hosts, p ? p->query : std::string{},
            m.d.persisted, m.d.auth, m.d.selection.catalog_id(),
            p && p->show_all);
    };
    auto plain_rows = [&](const ui::SectionedProviderRows& s) {
        std::vector<ui::ProviderRow> out;
        out.reserve(s.rows.size());
        for (const auto& sr : s.rows) out.push_back(sr.row);
        return out;
    };

    return std::visit(overload{
        [&](OpenProviders) -> Cmd {
            // 如果用户从模型-hopped跨越这里(模型选择面板中的^P)，
            // 则关闭模型选择面板。如果没有这个，模型 选择面板 保持打开状态
            // 并赢得 pick_panel 的优先级顺序（首先检查），
            // 因此 渲染 跃点将没有什么新内容。首先刷新任何 待处理 努力 -tier
            // 更改 - 与 CloseModels 在 Esc 上所做的相同，
            // 因此跃点不会默默地丢弃它。
            Cmd effort_save = Cmd::none();
            if (m.ui.effort_dirty) {
                effort_save = persist_settings(m);
                m.ui.effort_dirty = false;
            }
            // 放弃待处理 Smart-Mode插槽分配：从型号选择
            // 面板跳出mid-assign不得让模式处于待命状态。
            // 该模式现在处于 ON 和 选择面板 值，因此替换它就是
            // 重置 — 无需单独清除。
            //
            // ONE移动，不关闭<Models>()+descend()。
            // 这对读起来像一个交换，表现得像一个截断：close() 将
            // 插槽留空，因此后面的 descend() 没有父级可以存储，
            // 并默默地删除了 GRANDparent。调色板→模型→
            // ^P→Esc然后完全离开堆栈而不是返回到调色板。
            // replace() 说 "sideways"：我们要离开的 选择面板
            // 会消失，打开的 IT 会留下来。
            //
            // 在与当前-active提供者匹配的行处打开。
            // 带有空查询的新行（因此每个提供者都存在匹配）。
            const auto fresh_s = build();
            const auto fresh = plain_rows(fresh_s);
            const auto& sel = m.d.selection;
            const std::string active_label =
                sel.kind == provider::Kind::ExternalAcp ? sel.acp_agent_id
                : sel.kind == provider::Kind::OpenAI    ? sel.openai_endpoint.label
                : std::string{provider::default_provider_id()};
            int idx = 0;
            for (int i = 0; i < static_cast<int>(fresh.size()); ++i) {
                const auto& row = fresh[static_cast<std::size_t>(i)];
                if (const auto* pr = row.preset(); pr && pr->id == active_label) { idx = i; break; }
                if (const auto* ag = row.acp();    ag && ag->id == active_label) { idx = i; break; }
                if (const auto* ch = row.custom_host(); ch && *ch == active_label) { idx = i; break; }
            }
            // 我们来自选择面板的一跳REPLACES；对任何存在的东西（
            // 通常什么都没有）开放冷 DESCENDS。
            if (m.ui.panel.is<pn::Models>()) m.ui.panel.replace(pn::Providers{{idx}});
            else                             m.ui.panel.descend(pn::Providers{{idx}});
            return effort_save;
        },
        [&](CloseProviders) -> Cmd {
            ascend(m);   // Esc:返回到打开此的任何内容，或关闭
            return Cmd::none();
        },
        [&](ProvidersMove& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            const int n = static_cast<int>(build().rows.size());
            if (!p || n == 0) return Cmd::none();
            p->confirm_remove.clear();   // 导航解除 待处理 删除
            p->index = ui::step(p->index, e.delta, n);
            return Cmd::none();
        },
        [&](ProvidersJump& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            const int n = static_cast<int>(build().rows.size());
            if (!p || n == 0) return Cmd::none();
            p->confirm_remove.clear();   // 导航解除 待处理 删除
            using W = ProvidersJump::Where;
            constexpr int kPage = 14;  // 与 pickers.cpp 中的 kViewportH 匹配
            switch (e.where) {
                case W::Home:     p->index = 0; break;
                case W::End:      p->index = n - 1; break;
                case W::PageUp:   p->index = std::max(0, p->index - kPage); break;
                case W::PageDown: p->index = std::min(n - 1, p->index + kPage); break;
            }
            return Cmd::none();
        },
        [&](ProvidersFilterInput& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            if (!p) return Cmd::none();
            p->confirm_remove.clear();   // 输入解除 待处理 ^D 删除
            // 附加带类型的代码点（UTF-8)并将光标重置到新的
            // -narrowed列表的顶部。
            char32_t cp = e.codepoint;
            if (cp < 0x80) { p->query.push_back(static_cast<char>(cp)); }
            else {
                // Minimal UTF-8编码用于多字节输入（在提供者
                // 名称中很少见，但绝不会破坏缓冲区）。
                if (cp < 0x800) {
                    p->query.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    p->query.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    p->query.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    p->query.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    p->query.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    p->query.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    p->query.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    p->query.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    p->query.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            p->index = 0;
            return Cmd::none();
        },
        [&](ProvidersFilterBackspace) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            if (!p || p->query.empty()) return Cmd::none();
            // 弹出一个UTF-8代码点（修剪延续字节然后领先）。
            while (!p->query.empty()
                   && (static_cast<unsigned char>(p->query.back()) & 0xC0) == 0x80)
                p->query.pop_back();
            if (!p->query.empty()) p->query.pop_back();
            p->index = 0;
            return Cmd::none();
        },
        [&](ProvidersToggleAll) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            if (!p) return Cmd::none();
            p->show_all = !p->show_all;
            p->confirm_remove.clear();
            // 重建AFTER翻转——build()读取实时标志。
            const auto next = build();
            if (p->show_all) {
                // 将光标停放在FIRST显示的行上：您
                // 要求查看它们，因此支付属于光标下方
                // 而不是您碰巧站立的任何地方。
                for (int i = 0; i < static_cast<int>(next.rows.size()); ++i)
                    if (next.rows[static_cast<std::size_t>(i)].section
                        == ui::ProviderSection::More) { p->index = i; break; }
            }
            // 不管怎样，永远不要让光标超过末尾。
            const int last = static_cast<int>(next.rows.size()) - 1;
            if (p->index > last) p->index = last < 0 ? 0 : last;
            if (p->index < 0) p->index = 0;
            return Cmd::none();
        },
        [&](ProvidersDelete) -> Cmd {
            auto* p = m.ui.panel.get<pn::Providers>();
            const auto rows = plain_rows(build());
            const int n = static_cast<int>(rows.size());
            if (!p || p->index < 0 || p->index >= n)
                return Cmd::none();
            const ui::ProviderRow& row =
                rows[static_cast<std::size_t>(p->index)];

            // 两个可移动事物共享Del键：
            //  (a) SAVED CUSTOM HOST —完全移除主机。
            //  (b) 带有 SAVED API KEY (e.g.openrouter)
            //      的 PRESET — 预设本身是内置的 -in 并保留下来，
            //      但 Del 清除其 已保存 键（符号 -out），
            //      这就是 "delete openrouter" 在实践中的含义。没有 已保存 密钥、
            //      ACP 代理和 "Custom host…" 哨兵的预设不可删除。
            std::string target;          // 设置键擦除
            bool is_custom_host = false;
            if (const std::string* spec = row.custom_host()) {
                target = *spec;
                is_custom_host = true;
            } else if (const auto* pr = row.preset()) {
                const auto& s = m.d.persisted;
                const std::string pid{pr->id};
                if (s.provider_keys.count(pid)) target = pid;  // 有 已保存 钥匙
            }
            if (target.empty()) {        // 这一行没有可移动的东西
                p->confirm_remove.clear();
                return Cmd::none();
            }
            // 二-press:首先按下ARMS(在该目标上标记confirm_remove)，
            // 第二次按下SAME行COMMITS。镜子 ThreadListDelete
            // / AccountRemove。
            if (p->confirm_remove != target) {
                p->confirm_remove = target;
                return Cmd::none();
            }
            const std::string removed = target;
            Cmd key_save = Cmd::none();
            {
                m.d.persisted.provider_keys.erase(removed);
                if (is_custom_host) m.d.persisted.provider_models.erase(removed);
                key_save = save_record(m);
            }
            // 还删除任何存储的帐户凭据作为预设标志-out（自定义主机
            // 将所有内容保留在provider_keys中，如上所述）。
            if (!is_custom_host) {
                std::vector<Cmd> ops;
                if (const auto* pa = m.d.auth.find(removed))
                    for (const auto& l : pa->accounts)
                        ops.push_back(Cmd(AccountOp{
                            .kind = AccountOp::Kind::Remove,
                            .provider = removed, .label = l}));
                ops.push_back(Cmd(LoadAuthView{}));
                key_save = Cmd::batch(std::move(key_save),
                                      Cmd::batch(std::move(ops)));
            }
            p->confirm_remove.clear();
            // 退出ACTIVE提供者不得让会话指向死凭据——
            // 调色板SignOut将活动头文件和提示词 re
            // -auth归零，并且该路径是拼写不同的相同动作。
            // 将头文件归零（下一回合不得重复使用已擦除的密
            // 钥）并说出下一步要做什么； 选择面板 保持打开状态，
            // 以便用户可以选择另一提供商（或 re
            // -enter 重新登录）。
            const bool was_active = (removed == active_provider_id(m));
            Cmd clear_auth = was_active
                ? Cmd(InstallAuth{.provider = removed, .clear = true})
                : Cmd::none();
            // 重建行列表，使得删除的自定义主机消失；夹钳。
            // build() re-reads m.d.persisted，上面的key
            // -save已经更新到了Model中。
            const auto fresh = plain_rows(build());
            if (!fresh.empty() && p->index >= static_cast<int>(fresh.size()))
                p->index = static_cast<int>(fresh.size()) - 1;
            auto toast = set_status_toast(m,
                was_active
                    ? "signed out of " + removed
                          + " (active) — pick a provider to continue"
                    : (is_custom_host ? "removed custom host: "
                                      : "signed out of ") + removed);
            return Cmd::batch(std::move(key_save), std::move(clear_auth),
                              std::move(toast));
        },
        [&](ProvidersSelect) -> Cmd {
            // 在关闭之前捕获光标：分配 Closed 会破坏
            // OpenAt 替代项，因此将指针保留在其中会悬空。
            const auto* p = m.ui.panel.get<pn::Providers>();
            const int selected = p ? p->index : -1;
            const auto sectioned = build();
            const auto rows = plain_rows(sectioned);
            const int n = static_cast<int>(rows.size());

            // "N more providers"路标上的Enter就位，
            // EXPANDS就位——选择面板保持开放，它不是目的地。在收盘前检查如下，
            // 因为它与 "Custom host..." 哨兵共享
            // NewCustomHost 载荷。
            if (selected >= 0 && selected < static_cast<int>(sectioned.rows.size())
                && sectioned.rows[static_cast<std::size_t>(selected)].more_signpost)
                return providers_update(m, ProvidersToggleAll{});

            m.ui.panel.close<pn::Providers>();
            if (selected < 0 || selected >= n) return Cmd::none();
            const ui::ProviderRow& chosen = rows[static_cast<std::size_t>(selected)];

            // "Custom host…"哨兵：切换到空闲-text端点弹出面板。
            // 如果该行是PROMOTED(查询是端点
            // -shaped)，则将带类型的文本直接带入，
            // 光标位于末尾，因此Enter转到探针而不是
            // 重新打开空字段。重新输入主机名 带类型
            // 的 会让人觉得该功能不存在。
            if (chosen.is_new_custom_host()) {
                ui::login::CustomHostInput ch;
                if (const std::string* prefill = chosen.new_host_prefill()) {
                    ch.host_input = *prefill;
                    ch.cursor     = static_cast<int>(ch.host_input.size());
                }
                ch.origin = ui::login::origin::Providers{};  // Esc=后退一步
                m.ui.login = std::move(ch);
                return Cmd::none();
            }

            // 外部ACP代理行：agentty驱动代理子进程，
            // 该子进程执行其OWN身份验证——这里没有关键解析。
            if (const provider::AcpAgentSpec* agent = chosen.acp()) {
                return commit_provider_switch(m, agent->id, agent->id);
            }

            // 已保存定制OpenAI-compatible主机行：规范字符串是进入
            // Settings.provider_keys的关键。解析 已保存 密钥
            // 并直接提交 — 没有 re-entry，因为该密钥已经在磁盘上。
            if (const std::string* spec_ptr = chosen.custom_host()) {
                const std::string spec = *spec_ptr;
                // 如果该自定义主机是ALREADY活动提供者，
                // Enter打开其帐户drill-down（自定义主机
                // 可以持有多个已保存密钥，可像OAuth提供者一样进行切换——
                // account_provider_id返回自定义OpenAI
                // 端点的规范）。从该列表中的 Esc 返回到此 选择面板。
                const auto& active = m.d.selection;
                const bool is_active =
                    active.kind == provider::Kind::OpenAI
                    && active.openai_endpoint.label == spec;
                if (is_active)
                    return reenter(m, Msg{OpenAccounts{}});

                // 不活动——通过中央解析器切换到它（密钥已经在磁盘上；
                // 解析读取provider_keys[规格]）。
                return commit_provider_switch(m, spec, spec);
            }

            const auto& preset = *chosen.preset();
            // ACCOUNT
            // -CAPABLE提供商上的Enter在拥有账户的每个提供商之间打开其账户管理器DIRECTLY-UNIFORM(OAuth:
            // Anthropic/ChatGPT/Copilot/Kimi;托管 API 密
            // 钥：Mistral/Groq/…；自定义主机），无论它是否是活动的提供商。否
            // switch-first (that popped the model picker); OpenAccounts targets
            // 该提供者通过id并从解析的选择构建其列表。
            // 仅无密钥本地服务器（add_method==无）直接
            // 跳至交换机。帐户列表中的 Esc 返回到此 选择面板。
            if (provider::credentials::add_method(preset.id)
                    != provider::credentials::AddMethod::None) {
                m.ui.panel.close<pn::Providers>();
                return reenter(m, Msg{OpenAccounts{std::string{preset.id}}});
            }

            const std::string spec{preset.id};

            // 拒绝将处于静默-broken状态的交换机。
            // 密钥是否存在是从设置记录（和环境链）中回答的，
            // 与-out的回退使用相同的检查标志。
            const bool has_key =
                provider::auth_source(preset, m.d.persisted, m.d.auth)
                    != provider::AuthSource::None;

            // 没有可解析密钥的托管（非-local)OpenAI-family提供商不能数据流。
            // 打开THIS提供商的in-app key-entry 弹出
            // 面板而不是dead-end错误；一旦按键落地，
            // login_submit 就会提交切换。
            const bool needs_key =
                preset.kind() == provider::Kind::OpenAI && !preset.is_local
                && preset.auth != provider::AuthStyle::None;
            if (needs_key && !has_key) {
                m.ui.login = ui::login::ApiKeyInput{
                    .key_input      = {},
                    .cursor         = 0,
                    .provider       = spec,
                    .provider_label = std::string{preset.label},
                    .origin         = ui::login::origin::Providers{},
                };
                return Cmd::none();
            }

            // OAuth提供者：如果未登录，
            // 则启动登录流程而不是切换到在第一回合就会失败的后端。
            //
            // 已转机 注册表 + 认证金库,而不是提供者名称。 SPECIFIC
            // 标志 将首先测试 : `device_login` 标记共享设备的
            // 提供者 -code 发射器(驾驶员,Kimi),
            // 而 `oauth_native` 仅指
            // 通过登录认证，并使用自身长期保持的传输连接。
            // - 两者也是这样,因此无法识别 Codex 自己流动。
            // 测试一下,先派副驾驶和Kimi进入 ChatGPT 浏览器流,
            // i.e 选择打开 OpenAI 签名 -in 面板,请检查
            // vault::signed_in 答案( T) "has this provider a live token"
            // 用于每个 OAuth 排行通过一个表格。
            if (preset.token_in_transport
                && !provider::signed_in(spec, m.d.persisted, m.d.auth)) {
                // 仅国家 -- -- subscribe() 运行 工作线程 ,
                // 按键在尝试,所以离开州取消它。
                const auto attempt_id = cmd::next_codex_login_attempt_id();
                if (preset.codex_login()) {
                    m.ui.login = ui::login::ChatGptWaiting{
                        .attempt_id = attempt_id,
                        .device_auth =
                            provider::chatgpt::codex_device_auth_preferred(),
                    };
                    return Cmd::none();
                }
                m.ui.login = ui::login::DeviceWaiting{
                    .provider = std::string{spec},
                    .provider_label = std::string{preset.label},
                    .attempt_id = attempt_id,
                };
                return Cmd::none();
            }

            // 每个切入点都会通过切换器将实际切换器漏出 一个 帮助
            // 提供者 + 每名 -provider 模型召回 + 努力夹
            // + 认证交换 + 反射 永远无法漂移 调用 网站。
            return commit_provider_switch(m, spec, std::string{preset.label});
        },
    }, pm);
}

} // namespace agentty::app::detail
