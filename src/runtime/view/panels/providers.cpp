// providers.cpp ——提供者 选择面板 (^P + 账户钻探 -down.
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {

Element providers_panel(const Model& m) {
    auto* picker = m.ui.panel.get<pn::Providers>();
    if (!picker) return nothing();

    Panel::Config cfg;
    cfg.title      = " Providers ";
    cfg.accent     = highlight;
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.provider_picker_scroll;
    cfg.selected   = picker->index;

    const std::string active_id = m.d.selection.catalog_id();

    // 订的,问吧 -filtered 行列表—— 缩写 相同 列表
    // 状态更新函数 解决选择对齐(见 build_provider_rows。
    // 光标是一个简单的索引,两边都没有数学偏移。
    //
    // 关闭( O) RECORD ,而不是 接口边界. view() 是一个纯粹的函数 Model:
    // 覆盖到 deps() 这里的意思是一样的 Model 画两个
    // 不同的 帧 取决于商店的店面 也取决于它是否 visual_hash's
    // 缓存-skip 病态。 `m.d.persisted`
    // 设置与该设置相同 状态更新函数 读。
    const auto& settings = m.d.persisted;
    const std::vector<std::string> saved_custom_hosts =
        provider::saved_custom_hosts(settings.provider_keys);
    const auto sectioned = ui::build_sectioned_provider_rows(
        saved_custom_hosts, picker->query, settings, m.d.auth, active_id,
        picker->show_all);
    const auto& srows = sectioned.rows;

    // 现场搜索 头文件 (模仿模型) 选择面板). Backspace 修剪;
    // 输入窄处。 隐藏 ACP/custom 查询被清除后行返回。
    cfg.header.push_back(filter_header(picker->query, "providers"));
    cfg.header.push_back(sep);

    // 拖曳认证 -status 已建栏 -in 预设,一个地方,
    // 所以每个供应商的徽章都保持一致。
    auto preset_note = [&](const provider::ProviderPreset& p, bool active)
        -> std::pair<std::string, maya::Color> {
        auto signed_badge = [&](std::string signed_label) {
            return std::pair<std::string, maya::Color>{
                active ? std::string{"\xe2\x9c\x93 signed in \xc2\xb7 accounts"}
                       : std::move(signed_label),
                active ? success : muted};
        };
        // 拥有自己生命的奥奥特供应商 传输层 : 由Auth金库回答的
        // 一条统一状态线。 -identical 名称 -keyed
        // 调用三个不同的块 signed_in (a) 探测器;
        // vault::signed_in 发送到同一个表格上,
        // 所以一个新的 OAuth 提供者可以免费获得它的状态行。
        if (p.token_in_transport) {
            if (provider::signed_in(p.id, m.d.persisted, m.d.auth))
                return signed_badge(std::string{p.label} + " (signed in)");
            return {"\xe2\x9a\xa0 sign in with " + std::string{p.label}, warn};
        }
        if (p.is_local || p.auth == provider::AuthStyle::None)
            return {"\xe2\x97\x8f local", info};
        if (p.kind() == provider::Kind::Anthropic) {
            // 电话 凭据 商店,不是现场 头文件 ( 做 ) 不 读 deps() 这儿,
            // 作为 已加载 输入 Model's AuthView.
            if (m.d.auth.stored(p.id))
                return {active ? "\xe2\x9c\x93 signed in \xc2\xb7 accounts" : "\xe2\x9c\x93 signed in", success};
            return {"\xe2\x9a\xa0 sign in", warn};
        }
        // 已主机 API-key 提供者: 区分 a SAVED (贴)
        // 密钥——哪些 ^D 显示出——从 ENV 键,无法删除 -app ,
        // 所以徽章不能暗示 ^D 将会努力。
        switch (provider::auth_source(p, settings, m.d.auth)) {
            case provider::AuthSource::Saved:
                return {active ? "\xe2\x9c\x93 signed in \xc2\xb7 key"
                               : "\xe2\x9c\x93 key saved", success};
            case provider::AuthSource::Env: {
                return {"\xe2\x97\x8f key from " + m.d.auth.env_var(p.id), info};
            }
            case provider::AuthSource::Local:
                return {"\xe2\x97\x8f local", info};
            case provider::AuthSource::None:
            default: break;
        }
        std::string_view want = p.auth_env.front();
        return {want.empty() ? "\xe2\x9a\xa0 no key" : "\xe2\x9a\xa0 " + std::string{want}, warn};
    };

    cfg.items.reserve(srows.size() + 3);
    std::optional<ui::ProviderSection> cur_section;
    int visual_selected = 0;
    // 人数 -section 计算右边 -pinned 在每个分区上显示
    int yours_count = 0, more_count = 0;
    for (const auto& sr : srows) {
        if (sr.section == ui::ProviderSection::Yours) ++yours_count;
        else if (sr.section == ui::ProviderSection::More) ++more_count;
    }
    // 是否有任何一行真的携带琥珀"no key"徽章?
    // 驱动脚步传奇,这只能解释屏幕上的内容,
    // 路标不是:是暗动作行,不是警告.
    const bool any_unconfigured = more_count > 0;

    // 名称列宽: 屏幕上实际最长的提供者标签,
    // 所以每个描述都从 相同 列表垂直扫描。
    // -host 光谱不能把每一个模糊的
    // 边缘都推向右侧.
    int name_w = 0;
    for (const auto& sr : srows) {
        if (sr.more_signpost) continue;
        std::string nm;
        if (const auto* p = sr.row.preset())            nm = std::string{p->label};
        else if (const auto* a = sr.row.acp())          nm = a->id;
        else if (const auto* s = sr.row.custom_host())  nm = *s;
        name_w = std::max(name_w, static_cast<int>(maya::string_width(nm)));
    }
    name_w = std::min(name_w, 22);
    // 纸 `nm`。一个比盖子长的名字保留其全文,
    // 并简单地将自己的描述右推 - 将提供者名称
    // 切换为 保存 校正是错的交易。
    const auto pad_name = [&](const std::string& nm) {
        const int w = static_cast<int>(maya::string_width(nm));
        return nm + std::string(static_cast<std::size_t>(std::max(1, name_w - w + 2)), ' ');
    };

    for (std::size_t ri = 0; ri < srows.size(); ++ri) {
        const auto& sr = srows[ri];
        const auto& r  = sr.row;
        const int   i  = static_cast<int>(ri);

        // 每个带一个分割符。 只有在带子没有时才绘制 -empty ,
        // 所以新安装( 配置无) 从未显示空 "YOURS".
        // 路标站立在单行之上,没有分隔符本身
        // 就是标签。
        if ((!cur_section || *cur_section != sr.section)
            && sr.section != ui::ProviderSection::Signpost) {
            SectionHeader sh;
            switch (sr.section) {
                case ui::ProviderSection::Yours:
                    sh.label = sectioned.searching ? "configured" : "yours";
                    sh.hue   = accent;
                    sh.count = yours_count;
                    break;
                case ui::ProviderSection::More:
                    sh.label = "not configured";
                    sh.hue   = muted;
                    sh.count = more_count;
                    break;
                case ui::ProviderSection::Signpost:
                    break;                       // 无法到达,在上方守护
                case ui::ProviderSection::Add:
                    sh.label = "add";
                    sh.hue   = muted;
                    break;
            }
            cfg.items.push_back(section_header(std::move(sh)));
        }
        cur_section = sr.section;
        if (i == picker->index)
            visual_selected = static_cast<int>(cfg.items.size());

        Panel::Item row;

        // 电话 "N more providers" 路标, 做成真正的一行
        // Enter 到了,迪姆,并说是一种行动,而不是一种状态。
        if (sr.more_signpost) {
            // 一个动作,而不是一个目的地:徽章栏中的暗QQ标记,
            // 将其与上面的提供者行区分开来,
            // 而不借用表示"active"的%.
            row.badge       = "\xe2\x80\xba";          // ›
            row.badge_style = fg_dim(muted);
            row.leading = "Show " + std::to_string(sectioned.n_hidden)
                        + " more providers";
            row.leading_style  = fg_dim(muted);
            row.trailing       = "Enter  \xc2\xb7  ^E";
            row.trailing_style = fg_dim(muted);
            row.trailing_secondary = true;
            cfg.items.push_back(std::move(row));
            continue;
        }

        if (const auto* p = r.preset()) {
            const bool active = (p->id == active_id);
            const bool confirming = (picker->confirm_remove == std::string{p->id});
            auto [note, note_color] = preset_note(*p, active);
            // · 在徽章栏中,如型号选择面板:标记
            // 生活在部件保留的铬中,因此每个提供者的
            // 名字都在同一栏中开始,而不是将活动栏
            // 缩进一个格莱弗,与其邻居不相符合。
            //
            // A SPACE 非活动时, 而非空字符串 :
            // maya 只保留徽章 存储单元 用于设置一行,
            // 所以留空 将其他行拉出左侧的格莱弗,并解开对齐。
            row.badge       = active ? "\xe2\x97\x8f" : " ";   // ●
            row.badge_style = fg_of(accent);
            row.leading = pad_name(std::string{p->label})
                        + std::string{p->blurb};
            // 主要内容 渲染 完全前方(与模型相同的规则) 选择面板
            // : 暗淡( 暗淡) 每个 无 -active 排行让
            // 一个突出的等级倒置——它使得整个列表在后端状态下读作
            // 不可用 标签 退出( O) -shouts 名字的号码。
            //
            // 一个OFFER列是一个例外:它是一个你没有
            // 设置的提供者,因为你搜索它而显示,所以它读得暗淡,
            // 并声明它的单项动作,而不是警告你从未
            // 要求拥有的钥匙.
            row.leading_style  = sr.offer ? fg_dim(muted)
                                          : (active ? fg_bold(fg) : fg_of(fg));
            if (confirming) {
                // Del/d 装备在预设有 已保存 键 —— 第二次
                // 按键显示( 清除键) , 预设本身会保留。
                row.trailing       = "\xe2\x9c\x97 press again to sign out";
                row.trailing_style = fg_of(warn);
            } else if (sr.offer) {
                row.trailing       = "Enter to set up";
                row.trailing_style = fg_dim(muted);
            } else {
                row.trailing       = note;
                row.trailing_style = fg_of(note_color);
            }
            row.active         = active;
        } else if (const auto* agent = r.acp()) {
            const bool active = (agent->id == active_id);
            row.badge       = active ? "\xe2\x97\x8f" : " ";   // ●
            row.badge_style = fg_of(accent);
            row.leading        = pad_name(agent->id)
                               + "external ACP agent (" + agent->command + ")";
            row.leading_style  = active ? fg_bold(fg) : fg_of(fg);
            row.trailing       = "agent";
            row.trailing_style = fg_dim(info);
            row.active         = active;
        } else if (const auto* spec = r.custom_host()) {
            const bool active = (*spec == active_id);
            const bool confirming = (picker->confirm_remove == *spec);
            row.badge       = active ? "\xe2\x97\x8f" : " ";   // ●
            row.badge_style = fg_of(accent);
            row.leading        = pad_name(*spec) + "custom OpenAI-compatible host";
            row.leading_style  = active ? fg_bold(fg) : fg_of(fg);
            if (confirming) {
                row.trailing       = "\xe2\x9c\x97 press again to remove";
                row.trailing_style = fg_of(warn);
            } else {
                // 主持人最后告诉我们的 不是毯子 "ready"
                // :它拒绝的密钥是用户来此修补的事物.
                std::string err;
                for (const auto& c : m.d.provider_catalogs)
                    if (c.provider_id == *spec) err = c.error;
                if (active && err.empty()) err = m.s.models_error;
                const bool auth_err = err.find("HTTP 401") != std::string::npos
                                   || err.find("HTTP 403") != std::string::npos;
                if (auth_err) {
                    row.trailing       = "\xe2\x9a\xa0 key rejected \xc2\xb7 Enter to fix";
                    row.trailing_style = fg_of(warn);
                } else if (!err.empty()) {
                    row.trailing       = "\xe2\x9a\xa0 couldn't list models";
                    row.trailing_style = fg_of(warn);
                } else {
                    row.trailing       = "\xe2\x9c\x93 ready";
                    row.trailing_style = fg_of(success);
                }
            }
            row.active         = active;
        } else if (const std::string* prefill = r.new_host_prefill()) {
            // PROMOTED 发送点: 查询本身是终点 -shaped ,
            // 所以这行不再是一个通用的逃生舱 成为具体报价 的
            // 东西用户只是 带类型的 完全前缘,而不是哑巴,
            // 哑巴就是这个 TUI 用于 "unavailable" ,
            // 读取一个行,解决您因禁用而存在的问题,
            // 正是用户如何得出 agentty 无法到达他们的服务器。
            row.badge          = "+";
            row.badge_style    = fg_of(success);
            row.leading        = "Use " + *prefill + "  as a custom OpenAI-compatible host";
            row.leading_style  = fg_of(fg);
            row.trailing       = "\xe2\x8f\x8e connect";
            row.trailing_style = fg_of(info);
        } else {   // NewCustomHost 哨兵,没有宣传
            // `+` 标记一行 CREATES 而不是选择一个,
            // 所以 "add" 可见而不读取标签。
            row.badge          = "+";
            row.badge_style    = fg_of(success);
            row.leading        = pad_name("Custom host\xe2\x80\xa6")
                               + "any OpenAI-compatible server (host:port)";
            row.leading_style  = fg_of(fg);
            row.trailing       = "\xe2\x9c\x8e edit";
            row.trailing_style = fg_dim(info);
        }
        // 在宽度压力下,STATUSXX标签让位,而不是提供者
        // 名称——名称是您选择的;"✓ signed in · accounts"
        // 是参考数据. 与命令调色板和型号选择面板相同的策略;
        // maya的默认(领先产量先)是对于后方的存储单元更
        // 重要的行,比如文件选择面板的diffstat.
        row.trailing_secondary = true;
        cfg.items.push_back(std::move(row));
    }

    cfg.selected = visual_selected;

    // Enter 打开账户操作 -down 账户 -capable
    // OAuth 提供者, 否则它会切换。 直接从突出显示的行读取。
    const bool row_has_accounts = [&] {
        if (picker->index < 0 || picker->index >= static_cast<int>(srows.size()))
            return false;
        const auto& sr = srows[static_cast<std::size_t>(picker->index)];
        // 开价一行 Enter 开始符号 -in ,它不钻入账户。
        if (sr.offer) return false;
        const auto& row = sr.row;
        // 自定义主机持有多个密钥( 账户) – Enter 钻入.
        if (const auto* spec = row.custom_host())
            return provider::credentials::add_method(*spec)
                   != provider::credentials::AddMethod::None;
        const auto* p = row.preset();
        if (!p) return false;
        // 每个账户 -capable 提供者( OAuth + 主机) API 键)
        // 显示账户钻 -down 打开 Enter ;只有无密钥的本地服务器没有.
        return provider::credentials::add_method(p->id)
               != provider::credentials::AddMethod::None;
    }();

    cfg.footer.push_back(text(""));

    // 脚印的顶部是CONTEXTUAL,因为一个静态
    // 传说在用户卡住时不教任何东西. 三是:
    //
    //   带类型的 终点 + 确认什么 Enter 将用
    //   它 带类型的 a 非国家 -match 说通用路径存在,
    //                        用文字, AT 列表看起来是空的(
    //                        用户在此下结论) agentty
    //                        无法到达他们的服务器,然后前往 补丁
    //   编号 注册表 否则 徽章传说
    const bool host_shaped = ui::query_looks_like_host(picker->query);
    const bool no_presets  = !picker->query.empty()
                           && provider::filter_provider_indices(picker->query).empty();
    if (host_shaped) {
        cfg.footer.push_back(h(
            text("\xe2\x8f\x8e", fg_of(info)),
            text(" connects to ", fg_dim(muted)),
            text(picker->query, fg_of(fg)),
            text(" \xc2\xb7 agentty probes it and adopts the dialect it finds",
                 fg_dim(muted))
        ).build());
    } else if (no_presets) {
        cfg.footer.push_back(h(
            text("no built-in provider matches ", fg_dim(muted)),
            text("\xe2\x80\x9c" + picker->query + "\xe2\x80\x9d", fg_of(fg)),
            text(" \xc2\xb7 type a ", fg_dim(muted)),
            text("host:port", fg_of(info)),
            text(" to use any OpenAI-compatible server", fg_dim(muted))
        ).build());
    } else {
        // 传说中,人们解释徽章 ACTUALLY 屏幕上。
        // 在浏览视图中,每行都可以使用,所以没有可解释的;
        // 扩展后,未配置的行说 "Enter to set up"
        // 而不是给一个阴道命名 所以传说中必须说同样的话
        if (any_unconfigured)
            cfg.footer.push_back(h(
                text("\xe2\x9c\x93", fg_of(success)), text(" ready  ", fg_dim(muted)),
                text("\xc2\xb7", fg_dim(muted)),
                text("  Enter on a dim row adds it", fg_dim(muted))
            ).build());
        else
            cfg.footer.push_back(h(
                text("\xe2\x9c\x93", fg_of(success)), text(" ready  ", fg_dim(muted))
            ).build());
    }
    std::vector<Hint> hints{
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},        // ↑↓
        {"type", host_shaped ? "endpoint" : "filter", 4},
        {"Enter", host_shaped ? "connect"
                              : (row_has_accounts ? "accounts" : "switch"), 5},
    };
    // 只发布^E的广告, 当它会做一些事情: 在搜索时,
    // 每行都已经显示。
    if (!sectioned.searching && (sectioned.n_hidden > 0 || picker->show_all))
        hints.push_back({"^E", picker->show_all ? "fewer" : "all", 3});
    hints.push_back({"^D", picker->confirm_remove.empty() ? "remove" : "confirm", 2});
    hints.push_back({"^/", "models", 3});           // 横 -hint : 型号 选择面板
    hints.push_back({"Esc", "close", 4});
    cfg.footer.push_back(key_hints(std::move(hints)));

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
