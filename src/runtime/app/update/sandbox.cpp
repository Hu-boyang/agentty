// sandbox_update — 状态更新函数 财务报告和审定财务报表 沙箱 面板.
//
// 命令可能涉及什么：文件系统范围、网络、系统调用配置文件、
// 资源上限和trust-handoff策略。
//
// ── 为何这样 面板 谓 不 活着,当外表是 ────────────────────────
//
// 外观在每一个键盘上都写着,因为一个主题通过看来判断,
// 在选择和看之间一定没有任何东西.
//
// 边界正好相反。它有明确保存的三个理由:
//
//   1. A 沙箱 由 READING 承诺的不是看
//      电话 面板 编译 每编辑为壁报告 -- -- " filesystem.write:
//      坚固通过地锁 ABI 10"——所以你
//      评价的就是报告,报告在事发前就已经可以提供.
//
//   2. 半-applied政策是没有人可以解释的。
//      一次列出一个数字 将会, 生活,通过 "allow port 4",
//      "allow port 44", "allow port 443"
//      - 前两种是从未要求过的真正政策。
//
//   3. 不得在已经运行的命令下改变。
//      配置在产卵时间安装了-once(见sandbox.cpp's
//      g_cfg),所以一个-flight的中段编辑会在一个会话中给出两个命令,
//      两个不同的边界,没有说得到哪个。
//
// 因此编辑会累积在表单中,预览会跟踪它们,
// 保存会同时输入整个配置.
//
// ── 为什么预览会在每次编辑中重新编译 ──────────────────────
//
// 电话 面板 存在的全部原因是,你不能观察到的边界是
// 不能信任的边界—— agentty 已运出
// "sandbox: active" 每一个命令都死于
// 一个 uid_map 否认(问题21).
// claybin's compile() 纯洁的在描述的宿主之上,
// 所以墙壁可以重构而无需产卵:它是一个 编译 运行它就是
// 不让报告 描述一行不再说的配置

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/tool/util/sandbox.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/sandbox.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/handoff_gate.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

namespace {

namespace sb = agentty::tools::util::sandbox;

// 或 HOST 它可以做,决定 哪些行甚至提供。
// 面板 显示的每 -port 内核上无地锁的网络
// ABI 4 将提供一个无声无息的控制-所以需要
// 能力行的标记是无法显示的而不是隐藏的。
// 隐藏它们将使限制变得隐蔽,这就是整个失败模式。
// 面板 用于避免。
[[nodiscard]] bool claybin_here() {
#if defined(__linux__) || defined(__APPLE__)
    // 真正的探测器在两个平台上, 和真实的相同的意义: Linux 叉子和
    // 尝试 uid_map 写入; 打开 macOS 叉子和实际上 调用
    // sandbox_init () 无法从特性中猜测 标志 因为猜想
    // 21号问题是怎么发生的... "sandbox: active"
    // 每一个命令都因否认而死。
    return sb::claybin_backend::available();
#elif defined(_WIN32)
    // 自 appcontainer/restricted- 令牌集成以来，
    // Windows 后端 APPLIES 策略：available()
    // 运行真正的探测器（它创建和删除应用程序容器配置文件并在墙下运行子项），
    // 其结论是 sb::init() 已用于将启动横幅放在
    // "active (claybin)" 处。旧的手臂从集成之前无
    // 条件返回错误 - 这使得这个 面板 在主机上声明
    // "claybin cannot start here"，而横幅和每个
    // 执行 调用 都不同意它。 面板 和发动机现在响应相同的探测。
    return sb::claybin_backend::available();
#else
    // claybin 编译是针对每个平台的计划，但仅适用于Linux、
    // macOS和Windows。
    return false;
#endif
}

[[nodiscard]] std::uint32_t landlock_abi_here() {
#if defined(__linux__)
    return sb::claybin_backend::landlock_abi();   // 0=不存在，探测一次
#else
    return 0;
#endif
}

// 面板需要了解的关于现实的一切都集中在一处。
//
// `running`字段是缺失的并且导致字幕撒谎的字段：它
// 来自`sb::detected_backend()`，
// 引擎实际上将命令限制在THIS进程中，密封件使该命令成为不可变。
// 它不是从配置派生的，也决不能派生——配置是用户想要的，
// 这就是他们所拥有的。
[[nodiscard]] pn::HostFacts host_facts_here() {
    pn::HostFacts f;
    f.claybin_available = claybin_here();
    f.landlock_abi = landlock_abi_here();
#if defined(__APPLE__)
    f.per_port_net = f.claybin_available
                     && sb::claybin_backend::host().sbpl_network_filters;
    f.ipc_scoped = f.claybin_available;
#elif defined(__linux__)
    f.per_port_net = f.landlock_abi >= 4;
    f.ipc_scoped = f.landlock_abi >= 6;
#endif
    f.sandbox_active = sb::is_active();
    f.mode_off = sb::requested_mode() == sb::Mode::Off;
    f.running = sandbox_cfg::LinuxBackend::Claybin;
    f.workspace_is_home = sb::workspace_is_home();
    f.handoff_truncated = tools::util::handoff::last_snapshot_truncated();
    return f;
}

// 根据表格重新计算墙壁。每次编辑后运行，
// 因此报表和行不会不一致。
//
// 注释发生在HERE而不是在build_sandbox_form，
// 因为它需要编译结果，并且编译需要完成的配置。使用与 编译
// 相同的功能来防止 per-row 墙走向 过期：没有 re
// -annotating 就没有重新定价的路径。
void reprice(pn::Sandbox& o) {
    const auto cfg = pn::read_sandbox_form(o.pane.form, o.pane.working);
    // 这些行是 此 配置的投影，因此它是下一次读回
    // 必须构建的——否则，存在于表单之外的设置（路径和端
    // 口列表）将是来自 过期 基础的 re-read，
    // 并且每个列表编辑都将在下一次击键时消失。
    o.pane.working = cfg;
    o.pane.preview = pn::preview_sandbox(cfg);
    // 状态决定每个注释的TENSE：未运行的
    // 引擎的墙是预测并且必须读作一个。
    pn::annotate_sandbox_form(o.pane.form, o.pane.preview, cfg,
                              pn::engine_status(cfg, o.pane.facts));
    pn::annotate_host_risks(o.pane.form, cfg, o.pane.facts);
    // 以及"will this actually work when I restart"的答案。
    // 计算了 此处，预览版是根据相同的 cfg 构建的，
    // 因此结论和它所判断的墙不能不一致。
    o.pane.restart_note = pn::restart_outcome(cfg, o.pane.facts, o.pane.preview);
}

// 从配置重建行，保留用户在列表中的位置——光
// 标所在的位置不属于改变的部分。
void reproject(pn::Sandbox& o, const sandbox_cfg::Config& cfg) {
    o.pane.working = cfg;
    // Re-measure和STORE。视图需要这些来回答 "will this work on restart"
    // 并且不能自行探测 - uid_map 分叉 mid-render
    // 不是纯视图可以做的事情。
    o.pane.facts = host_facts_here();
    form::rebuild(o.pane.form, pn::build_sandbox_form(cfg, o.pane.facts));
    reprice(o);
}

}  // 命名空间结束。

form::Form sandbox_form_for_search(const Model& m) {
    const auto& seed = m.d.persisted.sandbox.configured ? m.d.persisted.sandbox
                                                         : sb::config();
    return pn::build_sandbox_form(seed, host_facts_here());
}

Cmd sandbox_update(Model& m, msg::SandboxMsg sm) {
    return std::visit(overload{

        [&](OpenSandbox& e) -> Cmd {
            pn::Sandbox o;
            const auto facts = host_facts_here();
            o.pane.facts = facts;
            o.pane.claybin_available = facts.claybin_available;
            // 保留用于任何想要将运行的引擎作为字符串的东西。
            // 注意，FORM不再从选择中导出其副标题——它询问
            // `facts`——这就是阻止头文件在bwrap进程上
            // 声明seccomp的原因。
            o.pane.backend = facts.sandbox_active ? "claybin" : "none";

            // 从 SAVED 保单（m.d.persisted)中种子，而不是实时保单。
            //
            // 这些不同，并且仅在策略被密封之后：实时配置被
            // 冻结在该过程开始时的任何位置，而`persisted`
            // 保存在该会话期间进行的任何保存。如果 面板 在实
            // 时版本上打开，它会默默地丢弃早期的 保存 ——您
            // 将设置一个系统调用配置文件，重新打开，
            // 然后发现您的编辑消失了。
            //
            // `configured`是鉴别器：未触及的
            // 配置意味着没有人来过这里，
            // 因此回退到实际执行的内容而不是构造默认值。
            const auto& seed = m.d.persisted.sandbox.configured
                                   ? m.d.persisted.sandbox
                                   : sb::config();
            o.pane.form = pn::build_sandbox_form(seed, facts);
            o.pane.working = seed;

            // -saved-this-session已经值得带入重新打开的面板中，
            // 因此"applies on restart"页脚不会
            // 仅仅因为覆盖面板关闭并返回而消失。
            o.pane.saved_pending_restart =
                m.d.persisted.sandbox.configured &&
                !(m.d.persisted.sandbox == sb::config());

            (void)form::focus_row(o.pane.form, e.focus);
            reprice(o);
            m.ui.panel.descend(std::move(o));
            m.ui.sandbox_scroll.y = 0;
            return Cmd::none();
        },

        [&](CloseSandbox&) -> Cmd {
            // Esc丢弃。故意的，与外观相反：
            // 在进入的过程中没有应用任何内容，
            // 因此没有什么可以撤消——并且在Esc上提交的
            // 边界将是通过离开屏幕而改变的边界。
            ascend(m);
            return Cmd::none();
        },

        [&](SandboxKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Sandbox>();
            if (!o) return Cmd::none();

            // 键落在哪一行，捕获BEFORE apply()
            // ——方向键可以移动光标，我们需要的是实际
            // 编辑的行。
            const std::string row_id =
                (o->pane.form.cursor >= 0 &&
                 o->pane.form.cursor < static_cast<int>(o->pane.form.fields.size()))
                    ? o->pane.form.fields[static_cast<std::size_t>(o->pane.form.cursor)].id
                    : std::string{};

            const auto applied = form::keys::apply(o->pane.form, e.action);

            if (applied.close)
                return sandbox_update(m, msg::SandboxMsg{CloseSandbox{}});

            // POSTURE行是唯一写入OTHER行的行，
            // 并且它被应用HERE而不是在回读中，因为它是ACTION，
            // 而不是值。
            //
            // 回读只看到值, 而此行的值是配置的姿态 - 所以它无法区分。
            // 用户刚刚选择 Hardened。
            // 发件人
            // 用户此前选择 Hardened，之后又修改了 Memory。
            // 推断它在那里 -stamped 用户编辑的下一篇预设 重绘 ,
            // 则编辑时消失,没有消息。 状态更新函数 上面有行号,
            // 所以这里的区别是自由的,准确的。
            //
            // 重置工程,而不是重定价,原因比引擎行要强:引擎
            // 改变哪个行 LOCKED ,这改变了他们的 VALUES
            // 单是重复,就会留下26行显示旧政策,而脚下则描述
            // 了新的政策,而新政策恰恰是 rows/report
            // 不同意这个 面板 已经存在,以预防。
            if (applied.changed && row_id == pn::kSbPosture) {
                const auto chosen = static_cast<sandbox_cfg::Posture>(
                    std::get<form::field::Choice>(
                        o->pane.form.fields[static_cast<std::size_t>(
                            o->pane.form.cursor)].value).index);
                // 自定义是行的标签 REPORTS ,
                // 而不是命令; apply_posture 返回未为它设置的配置,
                // 因此在循环时降落在自定义上。
                const auto cfg = sandbox_cfg::apply_posture(
                    pn::read_sandbox_form(o->pane.form, o->pane.working), chosen);
                reproject(*o, cfg);
                // 和 SAVE. 这回转Cmd::none(),
                // 这意味着每OTHER行重写一行是从未到达下方自动保存的
                // 一行 -- -- 所以,选Hardened重新油漆的26行,
                // 说"applies on restart", 并且一无所有。
                // 下一次发射已加载 旧配置和姿态行,
                // 通过比较推导,正确报告自定义.
                //
                // 报告为"I changed the preset to Hardened, restarted, it was Custom again",
                // 并且衍生出的全部时间是正确的:
                // 磁盘上的配置真的没有变化。
                //
                // 不是一个特殊的案子 保存 路径 -
                // 相同 SandboxSave 早期的
                // 返回是整个缺陷。
                return sandbox_update(m, msg::SandboxMsg{SandboxSave{}});
            }

            // 一个选行要求它的编辑器。这里只有路径和端口行,
            // 每个选择都打开同一个列表编辑器,这就是为什么
            // 信息带有行 ID 而不是有四个近-identical
            // 消息的原因。
            if (applied.hand_off && !row_id.empty())
                return sandbox_update(m, msg::SandboxMsg{SandboxEditList{row_id}});

            // 没有 `fired` ,而这是形式财产而不是
            // 遗漏: 面板 没有动作行( 唯一一件是 DO
            // 谓 保存 如果再加一个, apply()
            // 需要处理的,请看 plugin_edit.cpp
            // 被开除
            //
            // 调 -price 打开 任意 变异. `changed`
            // 覆盖一个下拉移动和一个 切换; `left_field`
            // 盖子留下a text/number 排行,即这些排行所犯
            // (义务4英寸) PANELS.md : 半个 -typed 值
            // 绝不重写配置。 窄于此, 墙壁报告描述了一行不再描述的配置
            // -- -- 这是全部内容 面板 已经存在,以预防。
            // 重新定价, 墙壁报告跟踪行,
            // 但 保存( 重置表格) 只在值结算后:
            // 保存中间 -edit 重构文本下的窗体 带类型的.
            if (applied.changed || applied.left_field) reprice(*o);
            if (applied.settled >= 0) {
                // 自动保存。 下一轮发射的政策依然有效, 它不会安装
                // 现场( set_config 仍被封存)。
                return sandbox_update(m, msg::SandboxMsg{SandboxSave{}});
            }
            return Cmd::none();
        },

        [&](SandboxRefreshPreview&) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Sandbox>()) reprice(*o);
            return Cmd::none();
        },

        [&](SandboxSave&) -> Cmd {
            auto* o = m.ui.panel.get<pn::Sandbox>();
            if (!o) return Cmd::none();

            auto cfg = pn::read_sandbox_form(o->pane.form, o->pane.working);
            // `configured` 是什么阻止了
            // 升级 改变任何人的边界: 持久化 仅适用于
            // a 已保存 按钮。保存是设置它的行为。
            cfg.configured = true;

            // PERSIST 仅. 第set_config()号,此处故意.
            //
            // 启动时将现场政策封存, set_config
            // ( ) 将拒绝这样做( 它的日志和返回),
            // 所以它会叫做剧场 -- 但拒绝的
            // 原因是: 边界不能在已经运行的进程中移动。
            // 切换中 claybin - > 中间 -session 悄悄地
            // 放下了这 seccomp 过滤器, 组盖和秘密面具,
            // 以及 -tightening 后不脱 -read 已经留下的密钥。
            //
            // 写下政策 下一个 发射并说。 面板 脚印载着
            // "applies on restart"
            // (参见视图),所以用户从来不会离开,
            // 认为墙壁没有翻墙。重新启动是应用步骤;
            // 这是您可以信任的整个会话的边界成本。
            m.d.persisted.sandbox = cfg;

            // 调 -derive ,所以它们显示的是正常化
            // 的结果(端口排序,空白数字为0),
            // 而不是生成它的原始文本。 `cfg`,
            // 不 发件人 sb::config ()--现场政策是旧的,
            // 重新预测会把编辑工作扔掉。
            reproject(*o, cfg);
            o->pane.saved_pending_restart = true;
            return persist_settings(m);
        },

        [&](SandboxEditList& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Sandbox>();
            if (!o) return Cmd::none();

            // 种子从 CONFIG 表格中当前描述的表格,
            // 而不是来自 已保存 策略:用户可能已经改变姿态(
            // 重新写入路径) , 尚未保存, 并编辑 a 过期
            // 列表会默默地丢弃它。
            const auto cfg = pn::read_sandbox_form(o->pane.form, o->pane.working);

            const bool ports = e.row_id == pn::kSbPorts;
            const bool hosts = e.row_id == pn::kSbHosts;
            const std::vector<std::string>* src = nullptr;
            std::vector<std::string> port_strings;
            std::string title, help;

            if (ports) {
                for (auto p : cfg.allow_ports) port_strings.push_back(std::to_string(p));
                src = &port_strings;
                title = "Allowed ports";
                help  = "only these ports may be reached \xc2\xb7 53 is DNS, and "
                        "forgetting it breaks name lookup for everything";
            } else if (hosts) {
                src = &cfg.allow_hosts;
                title = "Allowed hosts";
                // 列表所做的,而其中的一条规则是人们错误的,
                // 按照他们需要的顺序。
                // Mode-specific所以它从来不声称过滤它不是过滤的东西。
                help  = cfg.net_mode == sandbox_cfg::NetMode::Full
                    ? "commands may reach ONLY these names \xc2\xb7 empty = any public "
                      "host \xc2\xb7 *.x.com is subdomains, add x.com for the site "
                      "itself \xc2\xb7 paste a URL and it is trimmed to its host"
                    : "saved now, enforced once Access is full \xc2\xb7 *.x.com is "
                      "subdomains, add x.com for the site itself";
            } else if (e.row_id == pn::kSbReadPaths) {
                src = &cfg.read_paths;
                title = "Also readable";
                help  = "extra paths a command may READ, beyond the scope "
                        "setting \xc2\xb7 one per line";
            } else if (e.row_id == pn::kSbWritePaths) {
                src = &cfg.write_paths;
                title = "Also writable";
                help  = "extra paths a command may WRITE \xc2\xb7 the workspace is "
                        "already writable";
            } else if (e.row_id == pn::kSbDenyPaths) {
                src = &cfg.deny_paths;
                title = "Masked";
                help  = "made unreadable even inside the scope above \xc2\xb7 "
                        "credential files are masked whether listed or not";
            } else {
                return Cmd::none();   // 非列表行
            }

            pn::SandboxList ed;
            using K = pn::SandboxListPane::ListKind;
            ed.pane = pn::build_sandbox_list(e.row_id, std::move(title), std::move(help), *src,
                                             ports ? K::Port : hosts ? K::Host : K::Path);
            // 记住 面板 我們是從這來的 Esc 返回到它,而不是关闭 覆盖
            // 面板 堆栈 -- 相同 WithFrom 藏了其他巢穴,还藏了其他窝
            // 穴 面板 使用时。
            // descend() 而不是空手而归 藏着 沙箱 面板
            // 作为这个编辑的家长,所以 Esc 返回到 面板 而不是
            // 离开 覆盖面板 堆栈。
            m.ui.panel.descend(std::move(ed));
            return Cmd::none();
        },

        [&](SandboxPaste& e) -> Cmd {
            // 像打字一样落在聚焦行；编辑结束时提交。
            if (auto* o = m.ui.panel.get<pn::Sandbox>())
                if (form::paste_into_focused(o->pane.form, e.text)) reprice(*o);
            return Cmd::none();
        },
        [&](SandboxListPaste& e) -> Cmd {
            if (auto* ed = m.ui.panel.get<pn::SandboxList>())
                // 贴纸 URL 是最常用的寄件者到达方式;
                // 显示 "saved as github.com" 直接放在下面,
                // 别在下一个键盘上
                if (form::paste_into_focused(ed->pane.form, e.text) &&
                    ed->pane.kind != pn::SandboxListPane::ListKind::Path)
                    pn::renumber_sandbox_list(ed->pane);
            return Cmd::none();
        },
        [&](SandboxListKey& e) -> Cmd {
            auto* ed = m.ui.panel.get<pn::SandboxList>();
            if (!ed) return Cmd::none();

            // 该 不变量,re-established 前面的文字是:
            // 只有在一个字段启用时才存在建议。
            //
            // apply() 可以在该编辑段结束 PREVIOUS
            // 键盘中键( 键盘键) ( MoveFirst,
            // MoveLast, PageUp/PageDown 并激活
            // 全部输入场并离开),而此臂的任何早期返回都无法运行。 标志 然后,
            // 它比它更持久, 下一个键被解释 与列表,没有人能看到。
            // Up/Down 引导一个隐形选择,而不是移动行,
            // Tab 翻写一行 没人在编辑。
            //
            // 逼迫他们 此处 而不是每个出口 规则和
            // 习惯的区别: 处理函数 下面有一个
            // 一致的状态,一个新的状态不能忘记恢复它。
            // 状态扫描发现了这一点;读取控制流没有。
            if (!ed->pane.form.editing()) ed->pane.completing = false;

            // Tab 接受突出的建议。
            //
            // Tab 而不是说 Enter ,因为 Enter 已经表示
            // 提交此字段并继续。
            // 随处可见的表层 并在这里窃取它 就会使一个完成的列表
            // 变得与其它字段不同 Tab 没有任何其他意义,
            // 所以它没有代价。
            //
            // editing() 与下面箭头相同: 提前返回,
            // 所以 过期 标志 将会让 Tab 覆盖一行无人
            // 编辑。
            if (ed->pane.completing && ed->pane.form.editing() &&
                e.action.intent == form::keys::Intent::Complete) {
                if (const auto* hit = ed->pane.complete.selected()) {
                    if (auto* row = ed->pane.form.focused())
                        if (auto* t = std::get_if<form::field::Text>(&row->value)) {
                            t->value = *hit;
                            t->cursor = t->value.size();
                            ed->pane.form.edit_dirty = true;
                            // 同步选择面板的查询 与我们刚刚写的东西。
                            //
                            // 否则 下一个 键盘中看到与查询
                            // 不同的行文字,决定用户 带类型
                            // 的 重新打开列表,现在匹配完全
                            // 接受的路径,所以 Tab 拥有
                            // 副作用 接受建议并立即提出回馈。
                            // 接受必须解决,而不是循环。
                            ed->pane.complete.clear_query();
                            ed->pane.complete.type(std::string_view{t->value});
                        }
                }
                ed->pane.completing = false;
                return Cmd::none();
            }

            // Esc,位于一处,其取消内容的顺序固定.
            //
            // 三件事可以同时打开(建议,一个活的场,
            // 编辑)和 Esc 每张胶片都要剥掉一粒,
            // 最外侧的 apply() '结果和后续
            // -up 检查是何以不可靠:媒体可能会驳回
            // 建议 和 被田野吞噬,所以编辑从一些州取了3
            // 个ESC,从其他州取了2个ESC.
            if (e.action.intent == form::keys::Intent::Close) {
                if (ed->pane.form.editing() || ed->pane.form.choosing()) {
                    // 1. 现场, 和 任何建议
                    //
                    // 两种都按一个,因为它们是从用户的一侧分
                    // 到一层的: 完成列表只在一个字段运行时存在,
                    // 所以要单独收费。 Esc 因为它
                    // 只是让编辑 采取三压离开 计数随
                    // 你有 带类型的 什么都行,随便
                    (void)form::keys::apply(ed->pane.form, e.action);
                    ed->pane.completing = false;
                    return Cmd::none();
                }
                if (ed->pane.completing) {
                    // 2. 无活字段的建议。
                    //    未来路径在浏览时打开它们; 撤销和保留,
                    //    而不是在屏幕上留下过期列表.
                    ed->pane.completing = false;
                    return Cmd::none();
                }
                // 3,编辑本身.
                return sandbox_update(m, msg::SandboxMsg{SandboxListClose{}});
            }

            const auto applied = form::keys::apply(ed->pane.form, e.action);
            if (applied.close)
                return sandbox_update(m, msg::SandboxMsg{SandboxListClose{}});

            // ^X将重点条目完全删除。
            //
            // 清除一条线删除它( 被丢弃在犯罪上) , 但它不是
            // DISCOVERABLE --
            // "no way to remove the entry" 报告,
            // 从一个没有删除负担的列表中查找。表格层已经向上行走
            // ^X 以 ResetField 并给人类留下意义 面板 ,因此,
            // 这不需要新的密钥和新的模式。
            //
            // 删除 ROW , 而不仅仅是其文本,
            // 因为留下一行空将使得列表每次删除某个东西都会增大一个缺口。
            // 后面的加行永远不会被删除: 这是一种负担,
            // 而不是一个条目。
            if (e.action.intent == form::keys::Intent::ResetField) {
                const int i = ed->pane.form.cursor;
                const int last = static_cast<int>(ed->pane.form.fields.size()) - 1;
                if (i >= 0 && i < last) {
                    form::end_edit(ed->pane.form);
                    ed->pane.form.fields.erase(
                        ed->pane.form.fields.begin() + i);
                    ed->pane.completing = false;
                    pn::renumber_sandbox_list(ed->pane);
                    // 同一地点,从来没有在头文件上或过端。
                    ed->pane.form.cursor = i;
                    form::settle_cursor(ed->pane.form);
                }
                return Cmd::none();
            }

            // 箭头在打开时移动SUGGESTIONS.
            //
            // 守在editing()和`completing`上,
            // 因为这个位置是BEFOREXXapply(),
            // 并且返回时间很早:一个结束于前一个按键的会话的过期标志会让Up/Down引导一个隐形的列表而不是在行间移动,
            // 面板看起来会冷冻. 不变量在apply
            // ()之后是re-asserted,
            // 但是一个早期的返回永远不会达到这个位置.
            if (ed->pane.completing && ed->pane.form.editing() &&
                (e.action.intent == form::keys::Intent::MovePrev ||
                 e.action.intent == form::keys::Intent::MoveNext)) {
                ed->pane.complete.move_wrapping(
                    e.action.intent == form::keys::Intent::MoveNext ? +1 : -1);
                return Cmd::none();
            }

            // 编辑会话时,如果有ENDED,则会用它结束
            // 建议。
            //
            // 多个意图结束内部编辑 apply() 而不为 Esc
            // -- MoveFirst, MoveLast,
            // PageUp/PageDown 并激活全部输入场并离开。
            // 此臂底部的再同步重算 `completing` 并且会抓住它,
            // 但只在路径上 REACH 以至在他们之前,
            // 标志 以超过证明它正当的会期。
            //
            // 紧接着这里 apply() ,使 不变量
            // 显示建议意味着已进入编辑会话。
            // 在每个出口而不是大多数出口都保持住。一个状态扫描发现了这个,
            // 读取控制流时没有。
            if (!ed->pane.form.editing()) ed->pane.completing = false;

            // Backspace 浏览行 ENTERS 编辑会话。
            //
            // 报告编号为"backspace on the selected row doesn't work first":
            // 编辑器在浏览中打开, 所以第一次点击的媒体 apply()
            // 编辑守护器并消失。 打入字符自动 -enters (表层类型)
            // -to-edit 和。 和 简 况 Backspace 并没有,
            // 所以开始编辑同一行的两种方式是不一致的。
            //
            // 形式层的原因 不 自动 -entering 键上的声音,
            // 其应用位置是声音 -- -- 一个偏差 Backspace
            // 永远不能毁灭 字节 用户无法看到 光标 中 --
            // 但此处不适用: 此行中的每行 面板 IS 可编辑值,
            // 没有其他 Backspace 可能意味着,
            // 和 光标 已经放在了焦点列上。它会编辑,一排没有东西
            // 的列就被抛在一边。
            if (e.action.intent == form::keys::Intent::Backspace &&
                !ed->pane.form.editing()) {
                if (auto* row = ed->pane.form.focused())
                    if (auto* t = std::get_if<form::field::Text>(&row->value);
                        t && !t->value.empty()) {
                        form::begin_edit(ed->pane.form);
                        // 光标 编号 END 首先:浏览行有它
                        // 所搭建的光标,从位置0删除会默默无闻地
                        // 对非光标无所作为 -empty 值。
                        t->cursor = t->value.size();
                        // 礛 DO 删除。 apply() 已经把
                        // 钥匙放在了它的编辑守护上, 所以不采取行动
                        // 进入会话 就会让第一关只武装第二关--
                        // 相同 "doesn't work first"
                        // 少一步,就别走一步
                        (void)form::keys::apply(ed->pane.form, e.action);
                    }
            }

            // 保持准确的 一个 仅次于空白行, ADD 就是这样
            //
            // 每当条目数与行数不一致时,
            // 这用于重建整个表单,这让三个单独的bug: .
            //
            //   * 倒移一行空置使其达到"absent", 所以重建
            //     DELETED 编辑的行——焦点跳过,下一个后空格没有可
            //     操作的字段。
            //     "backspace doesn't work sometimes".
            //   * 清除行以删除,因此无法工作:
            //     复行(从不再拥有的数值重建)或消失于
            //     -keystroke.
            //   *每一次重建都创造了新田地,所以 caret/edit 状态必须
            //     被手抄回来 和任何遗漏的读取 作为
            //     编辑器重置自己。
            //
            // 生长是唯一安全的编辑, 而一个字段是活的 : 附加
            // 一行不能打扰您所在的。 REMOVAL 发生在承诺时,
            // read_sandbox_list () 空投 -
            // 所以清除一条线仍然可以删除条目, 它只是不会在
            // 您输入时对表格进行调整。
            const bool last_row_used = [&] {
                if (ed->pane.form.fields.empty()) return true;
                // 每个条目行现在都是文本,包括端口 -- 一个
                // 数字部件不能是渲染 a 光标,所以您输入的列表
                // 不能使用。
                const auto& back = ed->pane.form.fields.back().value;
                if (const auto* t = std::get_if<form::field::Text>(&back))
                    return !t->value.empty();
                return false;
            }();
            if (last_row_used) {
                const auto id = "e" + std::to_string(ed->pane.form.fields.size());
                ed->pane.form.fields.push_back(pn::list_add_row(id, ed->pane.kind));
                pn::renumber_sandbox_list(ed->pane);
            } else if (ed->pane.kind != pn::SandboxListPane::ListKind::Path) {
                pn::renumber_sandbox_list(ed->pane);   // 刷新每-entry提示的直播
            }

            // 使建议清单与带类型的保持一致。
            //
            // 路径: 端口是关闭的一组四位数,
            // 且没有可完成的对数。 空文本关闭列表而不是
            // 提供整个 工作区 — 空行就是您如何添加
            // 一个您要全部输入的路径, 并且将它埋在
            // 一千个候选人下面, 将使常见的文本变得响亮。
            if (ed->pane.kind == pn::SandboxListPane::ListKind::Path) {
                // 由 VIEW。这在每一个键盘上运行,包括纯导航,
                // 并分配一个新 std::string 每根键,
                // 只要把它比作一个键 选择面板 已经持有
                // 的是一种成本,它显示为打字滞后 很久之前它
                // 出现在一个剖面图。
                std::string_view q;
                if (const auto* row = ed->pane.form.focused())
                    if (const auto* t = std::get_if<form::field::Text>(&row->value))
                        q = t->value;

                // 建议书需要EDIT SESSION,
                // 而不只是非-empty行。
                //
                // 没有 editing() 以句式键,
                // 箭头向已有条目箭头,将完成列表打开,
                // 供用户未打开的行使用 带类型的 字符输入 -
                // 然后偷走他们的下一个 Esc 以撤销他们从未
                // 要求过的名单,即 Esc 完成是买得起的东西
                // WRITING ;浏览一个数值不是写出来的。
                ed->pane.completing = ed->pane.form.editing() && !q.empty();
                if (ed->pane.completing && q != ed->pane.complete.query()) {
                    // 只有在实际的CHANGED时.
                    //
                    // 幼稚的版本( Name clear_query
                    // ()然后 type() ) 无效 记忆
                    // 缓存 每按键重置两次, 每次重置选择
                    // - 因此 O( N x 查询) 过滤器
                    // 重新 -ran 注意不平等意味着导航键
                    // 不花费任何费用,过滤器在每次实际编辑时
                    // 都会运行一次。
                    ed->pane.complete.clear_query();
                    ed->pane.complete.type(q);
                }
            }
            return Cmd::none();
        },

        [&](SandboxListClose&) -> Cmd {
            auto* ed = m.ui.panel.get<pn::SandboxList>();
            if (!ed) return Cmd::none();

            const auto row_id = ed->pane.row_id;
            const auto values = pn::read_sandbox_list(ed->pane);
            const bool ports  = ed->pane.numeric();

            // ascend() 恢复隐藏的父文件, 即 沙箱 面板
            // descend() 放在那边,别 close() :这样就无一例外,
            // 而整个将消失 覆盖面板 堆栈,因此编辑路径列表会
            // 完全退出设置。
            //
            // 维亚 detail::ascend, 不 赤裸的 m.ui.panel.ascend
            // () 生 物 质 调用 其虚假分支表示 "no stashed parent" ,
            // 哪个 slot.hpp 文档作为呼叫者的提示 CLOSE。
            // 以早归的方式纪念这头鸟, 留下这个 覆盖面板 屏幕上,
            // 没有下面的东西,所以 Esc 变成不 -op 编辑 面板
            // 绝对不能离开 -- -- 结果是比引起变化的警告更糟糕的(
            // PR #64). detail::ascend 拥有两个半部分:
            // 关闭 -on-no-parent ,
            // 重新确认为恢复 快照 需要,因为模型可能已经移动 下来。
            detail::ascend(m);

            auto* o = m.ui.panel.get<pn::Sandbox>();
            if (!o) return Cmd::none();

            // Write ,然后从中重建行。
            // 列表行显示 COUNT ,所以母行只能通过
            // 重投影来改变——在形式上没有值可以戳.
            auto cfg = pn::read_sandbox_form(o->pane.form, o->pane.working);
            if (ports) {
                cfg.allow_ports.clear();
                for (const auto& v : values) {
                    const auto n = std::strtoul(v.c_str(), nullptr, 10);
                    if (n > 0 && n <= 65535)
                        cfg.allow_ports.push_back(static_cast<std::uint16_t>(n));
                }
                std::sort(cfg.allow_ports.begin(), cfg.allow_ports.end());
                cfg.allow_ports.erase(
                    std::unique(cfg.allow_ports.begin(), cfg.allow_ports.end()),
                    cfg.allow_ports.end());
                // 仅在列表实际更改时切换访问,并且因为列表
                // 被空出而永远不松动`none`或打开网络.
                const auto before = o->pane.working.allow_ports;
                const bool changed = cfg.allow_ports != before;
                const bool ports_ok = o->pane.facts.claybin_available
                                      && o->pane.facts.per_port_net;
                if (changed && !cfg.allow_ports.empty() && ports_ok
                    && cfg.net_mode == sandbox_cfg::NetMode::Full)
                    cfg.net_mode = sandbox_cfg::NetMode::Ports;
            } else if (row_id == pn::kSbReadPaths) {
                cfg.read_paths = values;
            } else if (row_id == pn::kSbWritePaths) {
                cfg.write_paths = values;
            } else if (row_id == pn::kSbDenyPaths) {
                cfg.deny_paths = values;
            } else if (row_id == pn::kSbHosts) {
                cfg.allow_hosts = values;   // 已取消的语句 +
            }

            reproject(*o, cfg);
            // 自动保存,与该页面中的其他编辑相同 面板 -
            // 名单承诺并非特殊情况,需要在 Esc 这会使
            // 它成为一个。
            return sandbox_update(m, msg::SandboxMsg{SandboxSave{}});
        },

    }, sm);
}

}  // namespace agentty::app::detail
