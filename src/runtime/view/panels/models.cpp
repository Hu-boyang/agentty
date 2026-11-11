// models.cpp ——引信十字架 -provider 模型 选择面板 (^/).
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {



// ── 折叠的十字架 -provider 模型 选择面板 ───────────────────────────────────
// 单子上一个 每个 获得认证的提供者( E) docs/design/unified-model-picker.md).
// 行是`provider · model`,上下文为-window,后为存储单元;
// 各节(最近/所有提供方/在)为渲染,为is_header的分隔符.
// 选择交换机提供者+模型解析;一个暗的"sign in to X"行路径进行登录.
Element models_panel(const Model& m) {
    auto* picker = m.ui.panel.get<pn::Models>();
    if (!picker) return text("");

    // 读读 状态更新函数-maintained 缓存 ——每座永远不重建. 帧.
    const auto& rows = m.d.fused_rows;

    Panel::Config cfg;
    // 槽 -assign 模式:重新标题,这样可以清晰地显示,
    // 该选择将填补一个智能模式角色,而不是切换您正在聊天的模型,并说该列表被
    // 限定给哪个提供者(参见 fused_rows_for_model).
    // role_display_name 它们是这些标签的单拼——这曾经是第四份位置复制
    // (第3页). `slot == 0 ? "Strategic" : ...`).
    cfg.title = picker->assign_slot
        ? std::string{" Smart Mode \xc2\xb7 pick "}
          + std::string{smart::role_display_name(*picker->assign_slot)}
          + " model "
        : (!picker->provider_scope.empty()
               // ^/ 范围活动: 指定列表所标注的提供者,
               // 使用任何带该列表的行的标签。
               ? [&] {
                     std::string label = picker->provider_scope;
                     for (const auto& r : rows)
                         if (r.provider_id == picker->provider_scope
                             && !r.label.empty()) { label = r.label; break; }
                     return " Models \xc2\xb7 " + label + " only ";
                 }()
               : std::string{" Models \xc2\xb7 all providers "});
    cfg.accent   = accent;
    // 活跃 -row 边框共享 选择面板 口音,所以,
    // 我说 "you are here" 读作一个带有标题和查询
    // 的视觉语言 光标 光标栏(亮色)在重叠上仍然获胜,
    // 这是正确的——你在这里 ARE 比你以前高得多 都比现在高
    cfg.active_color = accent;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.fused_picker_scroll;

    // 占位符必须指定范围 ACTUALLY 已经生效。它只知道空档 -assign ,
    // 所以一个 ^/ 标题为范围列表 "Anthropic only"
    // 仍然邀请您过滤 "across providers" ——
    // 数据 头文件 和搜索框在同一个屏幕上相互矛盾。
    cfg.header.push_back(
        filter_header(picker->query,
                      (picker->assign_slot || !picker->provider_scope.empty())
                          ? "this provider"
                          : "across providers",
                      accent));
    cfg.header.push_back(sep);

    // 惰性-load 提示: 当任何提供者 目录 还在 流式传输 显示暗色 加
    // 载动画-ish 注意(最初活动) -provider-only ( 列表
    // 明显读作 "more coming" ,没有 "that's all"。
    // 失败的提供者被分开浮出水面,所以网络的blip看起来不像 "that's all".
    {
        std::vector<std::string_view> pending;
        std::vector<const ProviderCatalog*> failed;
        for (const auto& c : m.d.provider_catalogs) {
            if (c.state == ProviderCatalog::State::Loading)
                pending.push_back(c.label);
            else if (!c.error.empty() || c.state == ProviderCatalog::State::Failed)
                failed.push_back(&c);
        }
        if (!pending.empty()) {
            // 说出我们在等谁 "⋯ loading Groq, Mistral…" 答案
            // ( T) "where's X?" 准确的说,一个光是数就可以让用户猜到的。
            std::string who;
            const std::size_t shown = std::min<std::size_t>(pending.size(), 3);
            for (std::size_t i = 0; i < shown; ++i) {
                if (i) who += ", ";
                who += pending[i];
            }
            if (pending.size() > shown)
                who += " +" + std::to_string(pending.size() - shown);
            cfg.header.push_back(text(
                "  " + std::string{m.s.spinner.current_frame()}
                    + " loading " + who + "\xe2\x80\xa6",
                fg_italic(muted)));
        }
        // 每个失败的提供者有服务器自身的原因,所以一个被拒绝的密钥读作这样,
        // 而不是"this host has no models".
        for (std::size_t i = 0; i < failed.size() && i < 3; ++i) {
            const auto& c = *failed[i];
            const std::string why = c.error.empty()
                ? std::string{"couldn't load models"} : c.error;
            cfg.header.push_back(text(
                "  \xe2\x9a\xa0 " + c.label + " \xc2\xb7 " + why,
                fg_of(warn)));
        }
        if (failed.size() > 3)
            cfg.header.push_back(text(
                "  \xe2\x9a\xa0 +" + std::to_string(failed.size() - 3)
                    + " more failed \xc2\xb7 ^L to retry",
                fg_of(warn)));
        else if (!failed.empty())
            cfg.header.push_back(text(
                "    ^L retry \xc2\xb7 ^P fix the key or address",
                fg_dim(muted)));
    }

    if (rows.empty()) {
        // 死胡同应该说 原因 空的,还有下一步要做什么
        // "no models match" 是否您错误地
        // 打入了相同的信息, 是否每个 目录 还在装货,
        // 或者是否你被签出所有东西——三种截然不同的情况,
        // 还有三种不同的下一步动作。它也在脚底建成之前返回,
        // 所以 选择面板 没有任何暗示 Esc 甚至还工作过
        const bool loading = std::any_of(
            m.d.provider_catalogs.begin(), m.d.provider_catalogs.end(),
            [](const ProviderCatalog& c) {
                return c.state == ProviderCatalog::State::Loading;
            });
        const bool any_authed = std::any_of(
            m.d.provider_catalogs.begin(), m.d.provider_catalogs.end(),
            [](const ProviderCatalog& c) { return !c.models.empty(); });

        const bool any_failed = std::any_of(
            m.d.provider_catalogs.begin(), m.d.provider_catalogs.end(),
            [](const ProviderCatalog& c) { return !c.error.empty(); });

        Panel::Item nr;
        if (loading) {
            nr.leading = "  " + std::string{m.s.spinner.current_frame()}
                       + " loading model catalogs\xe2\x80\xa6";
        } else if (any_failed && picker->query.empty()) {
            nr.leading = "  no models to pick \xc2\xb7 see the error above";
        } else if (!any_authed) {
            nr.leading = "  no providers signed in \xc2\xb7 "
                         "^P to add one";
        } else if (!picker->query.empty()) {
            nr.leading = "  no model matches \xe2\x80\x9c" + picker->query
                       + "\xe2\x80\x9d \xc2\xb7 Backspace to widen";
        } else {
            nr.leading = "  no models available";
        }
        nr.leading_style = fg_italic(muted);
        cfg.items.push_back(std::move(nr));
        cfg.selected = 0;
        // 保留页脚:一个空列表正是用户最需要被告知
        // 如何离开或如何到达提供者选择面板的时间.
        cfg.footer.push_back(key_hints({
            {"^P", "providers", 1},
            {"Esc", "close", 4},
        }));
        return Panel{std::move(cfg)}.build();
    }

    // 分区分隔符 : RECENT 数字 全部 PROVIDERS ,
    // 加查询 -gated SIGN IN 节 段 段 —— un
    // -authed 名称符合查询的提供者 渲染 寻找它永远不是死路
    enum class Section { Favorites, Recent, Others, SignIn };
    auto section_of = [](const FusedRow& r) {
        if (r.is_signin_offer()) return Section::SignIn;
        if (r.favorite_section) return Section::Favorites;
        if (r.recent) return Section::Recent;
        return Section::Others;
    };
    std::optional<Section> cur;
    int visual_selected = 0;
    // 人数 -section 大小, 右侧 -aligned 显示在每一处的计数 头文件.
    int fav_count = 0, recent_count = 0, all_count = 0, signin_count = 0;
    for (const auto& r : rows) {
        switch (section_of(r)) {
            case Section::Favorites: ++fav_count; break;
            case Section::Recent: ++recent_count; break;
            case Section::Others: ++all_count;    break;
            case Section::SignIn: ++signin_count;  break;
        }
    }

    // ── 标杆列宽度 ──────────────────────────────────────────
    // maya的选择面板要求呼叫者到"pad badges to a common width"
    // 进行列向对齐,这个选择面板从来没有这样做过——因此,
    // 提供商的标签运行在3.14字符("Groq".
    // "GitHub Copilot")中,每个型号的NAME开始于不同的列向,
    // 列表无法垂直扫描,提供商徽章是平整的十字-provider列表中的分组信号,
    // 因此一个被标记的列会挫败整个布局.
    //
    // 测量 DISPLAY COLUMNS 途经 maya::string_width ,
    // 没有 字节: 注册表 标签是 ASCII 今天, 但自定义主机是用户
    // -named 并可能持有 CJK 或爱莫吉,其中 字节 数到此为止
    // -pad 和关于 -break 用于创建的对齐。 (提供者的帮助者相同)
    // 选择面板 和已使用的提示条。 )
    int badge_w = 0;
    for (const auto& r : rows)
        if (!r.is_signin_offer())
            badge_w = std::max(badge_w, maya::string_width(r.label));
    badge_w = std::min(badge_w, panel_badge_max_cols());
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        const Section sec = section_of(r);
        if (!cur || *cur != sec) {
            cur = sec;

            // 当范围扩大到一个供应商时( ^/ ),浏览部分是提供商的模型——标题.
            // "<Provider> models" 而不是十字架。 -provider
            // "all providers" 分数。
            std::string others_label = "all providers";
            if (!picker->provider_scope.empty()) {
                std::string plabel = picker->provider_scope;
                for (const auto& rr : rows)
                    if (rr.provider_id == picker->provider_scope
                        && !rr.label.empty()) { plabel = rr.label; break; }
                others_label = plabel + " models";
                // 多个 -account 提供者: 名称 IDENTITY 其
                // 人 名 目录 权利是账号 -scoped ,所以,
                // 我说 "which account" 这正是两个本来相同的名单不同的原因。
                // 上场 头文件 ,不是行 - 账户不是模型的一部分 IS
                // (docs/IDENTITY_CAPABILITY_ENTITLEMENT.md ),
                // 每行重复会从名称中窃取宽度.
                const auto* pa = m.d.auth.find(picker->provider_scope);
                if (pa && pa->accounts.size() > 1 && !pa->active_account.empty())
                    others_label += "  \xc2\xb7  " + pa->active_account;
            }

            // 统一区划器——标签+hue+右 -pinned 数,
            // 看 panel_detail::section_header (由各组分摊) 选择面板).
            SectionHeader sh;
            sh.label = sec == Section::Favorites ? "favorites"
                     : sec == Section::Recent    ? "recent"
                     : sec == Section::Others    ? others_label
                                                 : "not signed in";
            sh.hue   = sec == Section::Favorites ? highlight
                     : sec == Section::Recent    ? info
                     : sec == Section::Others    ? accent
                                                 : warn;
            sh.count = sec == Section::Favorites ? fav_count
                     : sec == Section::Recent    ? recent_count
                     : sec == Section::Others    ? all_count
                                                 : signin_count;
            cfg.items.push_back(section_header(std::move(sh)));
        }
        if (i == picker->index)
            visual_selected = static_cast<int>(cfg.items.size());
        const bool selected = (i == picker->index);

        // 签名 -in 提供行 : "<Provider>  — Enter to sign in" 暗,
        // 单行动,没有后遗症 标签 (还没有模型).
        if (r.is_signin_offer()) {
            Panel::Item row;
            row.leading       = "  " + r.label;
            row.leading_style = selected ? fg_bold(fg) : fg_of(muted);
            row.trailing       = "Enter to sign in \xe2\x86\x92";   // →
            row.trailing_style = fg_dim(muted);
            cfg.items.push_back(std::move(row));
            continue;
        }

        Panel::Item row;
        const bool active = r.active;
        const int bw = maya::string_width(r.label);
        row.badge         = bw < badge_w
                              ? r.label + std::string(
                                    static_cast<std::size_t>(badge_w - bw), ' ')
                              : r.label;
        // 活动行保持品牌口音( 是 "you are here"
        // 标记);其他各行都按能力等级排列,
        // 所以最强 -first 浏览命令可以一目了然,而不是解释。
        row.badge_style   = fg_dim(
            active ? accent
                   : tier_hue(static_cast<ModelCapabilities::Tier>(r.tier)));
        row.leading       = "  " + r.model_label;
        // ACTIVE行的标志是maya的原生边栏(第0,
        // `active_color`栏中一个彩色的`▎`)
        // 而不是`● `文字前缀. 有两个原因: 栏码不花
        // 名字宽度——它住在已保留给光标的部件的铬中——它保留
        // 了每个模型名称,从SAME栏开始,所以列表垂直扫描.
        // 文本前缀缩进正排行中。
        row.active        = active;
        // 模型( 模型) NAME 内容是主要内容——你们在
        // 选择的东西——所以 渲染 完全前缘,过去是 `muted`
        // 用于每个非 -active 线条,使整个列表变得
        // 暗淡 一个 列突出; 倒转了等级( 参考 标签 退出
        // ( O) -shouted ),并做了一个长列表读取,
        // 但不能统一使用。活动行保持粗体加边栏,足以找到它。
        row.leading_style = active ? fg_bold(fg) : fg_of(fg);
        // 长 -style 匹配突出显示: 在名称中绘制匹配的查询字符,
        // 从而显示大过滤列表 原因 使用相同的 `highlight` 主题
        // hue 作为经典模型 选择面板 (无) `info` )所以两者
        // 选择面板 读作相同。 match_positions 由下列员额
        // 抵销: NAME ; 将制服二翻过 -space 缩进。
        // (此选项曾在 `active` ,它携带一个更广泛的 "● "
        // 前缀 ——边缘栏替换了它,所以现在每行都有相同的偏移. )
        if (!r.match_positions.empty()) {
            constexpr int kPrefix = 2;
            row.highlight.reserve(r.match_positions.size());
            for (int p : r.match_positions) row.highlight.push_back(kPrefix + p);
            row.highlight_fg = highlight;   // 与其它的选择面板号相同
        }
        // 拖曳 存储单元 :上下文窗口,然后是两个标记。 RIGHT
        // -ALIGNED 作为固定的 -width 栏中——数字为
        // MEASUREMENT ,而且不能将不共享小数列的测量值一览比较(
        // "1M" 旁边 "200k" 旁边 "8k" 被打乱时读作噪音。
        //
        // 标记坐在后面的两个固定位置上,
        // 所以 QQ和QQ总是在同一
        // 位置降落: 一排没有最爱的留下一个洞,
        // 而不是滑进栏,
        // 上下文栏显示将 ACTUALLY 使用。
        // 该行 context_window 已经携带过
        // 任何用户的覆盖(当行构建时应用),因为设置值是
        // 设置不显示的表面,是用户无法验证的值.
        std::string ctx;
        if (const int win = r.model.context_window; win > 0) {
            ctx = ui::context_window_label(win);
        } else {
            // UNKNOWN。一个没有广告的网关(股票) LiteLLM,
            // LM 工作室,一个空的 OpenAI-compat 服务器)
            // 离开每一行窗口 -less ,空白栏读作
            // 这一列不适用于当前条目。
            // ——所以用户从不学习数字是猜想,并得出结论,他们在状态栏看到的
            // 200k是一个硬盖. "auto" 将州命名。
            //
            // 不错 不 键位点 : ^W 被考虑和故意不受约束(见
            // subscribe.cpp ),因为窗口现在从 目录 探测器,
            // 或者 models.dev 几乎每个供应商都使用。
            // AGENTTY_MAX_CONTEXT_TOKENS.
            ctx = "auto";
        }
        // 最宽的现实上下文标签为5列("200k","1M+").
        std::string trailing = ctx.size() < 5
            ? std::string(5 - ctx.size(), ' ') + ctx
            : ctx;
        trailing += r.model.favorite ? "  \xe2\x98\x85" : "   ";      // ★
        // 说明理由的徽章(预编) build_fused_rows )——标记可以思考的模型,
        // 所以 "which of these reason" 可在供应商
        // 之间辨别。
        trailing += r.reasons ? " \xe2\x9c\xa6" : "  ";               // ✦
        row.trailing       = std::move(trailing);
        // 默认暗淡: 后端 存储单元 谓 REFERENCE 数据,
        // 而不是你所选择的。它曾经用模型名的强调
        // 来分享作曲家的温和口音,所以每行都高喊。
        // 活动行保持重音,所以目前的模型仍然可以一目了然地
        // 阅读.
        row.trailing_style = active ? fg_of(accent) : fg_dim(muted);
        // NO-TOOLS: a WORD 附录 NAME 而不是后面
        // 的又一个格言 存储单元 三个原因:(1) 是一个解析符,
        // 而不是参考数据,以及线索 存储单元 暗淡的。 -grey
        // 完全因为 IS 参考文献 —— 被绘为参考文献的警告读作装饰;
        // (2) 无法解释的格莱弗是一个谜题, 而这个 选择面板 没有
        // 什么地方可以传奇;(3) 后方 存储单元 是那个 首次
        // 东西在宽度压力下下降( ) yields_trailing 因此,
        // 一个标志 决定模型是否完全工作 消失在一个狭小的 终端.
        // "chat only" 属于自身 -explanatory ,
        // 不需要钥匙,旅行时会使用它会降低质量的名称。
        if (!r.tool_capable) row.leading += " \xc2\xb7 chat only";
        // 宽度下的压力 CHIPS 放弃,而不是模型名称—
        // —名称是您正在选择的内容;上下文窗口和标记是参考数据。
        // 没有此选项,默认策略(先导输出)将切换
        // "Claude Sonnet 4.6" 保留
        // "200k ★ ✦" 完全完全倒着
        row.trailing_secondary = true;
        cfg.items.push_back(std::move(row));
    }
    cfg.selected = visual_selected;

    // 理由说明 -effort 用于控制突出显示的模型的 —— 共享
    // reasoning_effort_footer .  ⁇ / ⁇   ⁇   ⁇
    // m.d.effort 活的(没有舞台),所以 标签 脚印和脚印 传输 不可不决.
    if (picker->index >= 0 && picker->index < static_cast<int>(rows.size())) {
        const auto& hl = rows[static_cast<std::size_t>(picker->index)];
        if (!hl.is_signin_offer())
            for (auto& row : reasoning_effort_footer(m, hl.model.id.value,
                                                     hl.provider_id))
                cfg.footer.push_back(std::move(row));
    }

    // 页脚提示遵循模式: 在槽中 -assign Enter PINS 作用和作用 Esc 继续
    // BACK 至智能模式, 如此有希望 "switch"/"close" 可能错写了密钥的用途。
    // ^/ 财务报告和已审计财务报表 ^Tab 正在切换 -only 付钱
    // 写成两个分支而不是一个 `slot >= 0 ? key_hints({...}) : key_hints({...})`
    // 已经生产了三胞胎 GCC's -Wdangling-pointer 开火
    // 警告是... 警告是警告 警告是告密 FALSE POSITIVE ,而不是一个 生命周期 值得保存的错误 :
    // key_hints 带走它的矢量 BY 值 把它
    // 移到组件的捕捉中 所以在表达结束之前 固定的
    // 温度会消耗掉 并且没有什么比它更久远 GCC
    // 失去临时工的踪迹 所有权 横跨两条三角臂。
    // 树枝是等效的代码,读得更好,并符合周围的脚步样式—
    // —所以这是静默的重写,而不是固定的。恢复三角臂是
    // 正确的C++,只会使噪音回落。
    // ^/ 是一个 TOGGLE : 范围, 它会向每个供应商扩展。
    // 说 "provider" 两个州都留下了一张缩小了的列表,没有回头路。
    const bool scoped = !picker->provider_scope.empty();
    // ^F 是一个 切换 连着,就是 ALREADY 最喜欢的,
    // 不明星,所以广告 "favorite" 上面描述的是州,而不是行动。
    // 在-in号的报价上,根本没有任何模型可以显示.
    const auto* hl_row =
        (picker->index >= 0 && picker->index < static_cast<int>(rows.size()))
            ? &rows[static_cast<std::size_t>(picker->index)] : nullptr;
    const bool hl_offer = hl_row && hl_row->is_signin_offer();
    const char* fav_hint = (hl_row && hl_row->model.favorite) ? "unfavorite"
                                                              : "favorite";
    if (picker->assign_slot)
        cfg.footer.push_back(key_hints({
            {"\xe2\x86\x91\xe2\x86\x93", "move", 5},
            {"Enter", "pin to role", 5},
            {"^F", hl_offer ? "" : fav_hint, 1},
            {"^/", scoped ? "all providers" : "provider", 2},
            {"^L", "refresh", 2},
            {"Esc", "back", 4},
          }));
    else
        cfg.footer.push_back(key_hints({
            {"\xe2\x86\x91\xe2\x86\x93", "move", 5},
            {"Enter", hl_offer ? "sign in" : "switch", 5},
            {"^F", hl_offer ? "" : fav_hint, 1},
            {"^/", scoped ? "all providers" : "provider", 2},
            {"^L", "refresh", 2},
            {"^Tab", "prev", 2},
            {"Esc", "close", 4},
          }));
    return Panel{std::move(cfg)}.build();
}

// ── Provider 选择面板 帮助者 ──
// (reasoning_effort_footer 住在这里 pickers_common.hpp.)


} // namespace agentty::ui
