// sandbox.cpp - 电话 沙箱 面板 (文件系统、网络、系统、限制)
//
// 排位是平面形式,通过共享的form_config和所有
// 其他的面板一样进行预测,因此这个面板没有贡献自己的铬.
//
// 或 IS 具体内容是字幕和注: 两者合在一起是
// 现场墙面报告,而报告是造成 面板 已存在。
//
// ── 为何要报告,而不只是开关 ────────────────────────────
//
// 您无法观察到的边界是您无法信任的边界。 失败模式永远不会 "the switch was set wrong"
// - 就是这样 "the switch said active and nothing was enforced" ,
// 和 agentty 已经运出过该批( 问题 # 21 :
// "sandbox: active" 每一个命令都死于一个 uid_map
// 建筑时又出现了两个相同的形状: claybin 后端从默认构建中编译出来,
// 因此这里的每行都配置了死代码; 而一个缺少的 Syscall 配置
// 图中的 Rnetemap 则编译了卷曲报告 "out of memory"
// 免费的机器上 RAM ,它读起来像一个网络否认,
// 而不是一个。
//
// 因此,第面板号报告,根据能力,哪些措施实际上在claybin的GuaranteeReport
// 中直接执行,哪些措施在MECHANISM中命名,因为仅"strong"
// 就是一种索赔,而第"strong via landlock abi 10"
// 号是索赔,随附收据。
//
// 是个 编译 而不是产卵: claybin's compile()
// 纯度超过一个描述的主机, 因此报告会跟踪每根键盘上的行,
// 而不运行任何操作。 状态更新函数 每次编辑后重算它,
// 这使得它无法描述这些行不再说的配置。

#include "panels_prologue.hpp"
#include "agentty/runtime/view/form_panel.hpp"
#include "agentty/runtime/panel/sandbox.hpp"
#include "agentty/tool/util/sandbox_broker.hpp"   // blocked_feed( ) (中文( ) ).
#include "agentty/tool/util/handoff_gate.hpp"     // handoff_feed( ) (中文( ) ).

#include <string>

namespace agentty::ui {

namespace {

// 墙壁,每能力折叠成一行。 `note` (无) 面板
// -specific 而不是当手 -built 要素:
// form_config 否为 面板 自己画染色体
[[nodiscard]] std::string wall_summary(const pn::SandboxPane& p) {
    if (!p.preview.compiled) {
        // 屏幕上最重要的东西是不会是编译的
        // 政策:行描述的边界完全无法构建.
        // claybin拒绝而不是贬低,因此错误是具体的,
        // 值得逐字显示.
        return "cannot enforce: " + p.preview.error;
    }

    // 只有墙壁 不 强壮。
    //
    // 这曾经是所有十二个都列出的,而这是个错误。每个能力都叠成一个
    // 运行 -on 线条是一段,一个表单旁边的一段是眼睛跳过的
    // 东西——所以是一段 `none` 名单上的字句与11个字完全
    // 一样 `strong` 报告全文现在都写在 ROWS (
    // annotate_sandbox_form ),
    // 其中它回答了用户实际存在的问题,也就是他们刚刚改变的行.
    //
    // 留在这的就是没有一排人拥有的部分:墙壁比要求的要弱。
    // 面板 '存在的理由,在十二个列表没有的
    // 地方读取一个简短的三者列表.
    //
    // "Weaker than ASKED" 是那个 加载-bearing 字词。
    // 用户没有要求的能力不是不能执行 -- `resource.memory: none`
    // 与 memory_mb =0 表示数 "no cap wanted" ,
    // 并把它列在一个真正的退化的旁边,使用户将这行读作噪音。这就是截图显示的内容:
    // `resource.memory: none (none) · resource.cpu: none (none)`
    // 在一个没有问题的政策下
    std::string out;
    for (const auto& w : p.preview.walls) {
        if (w.strength == "strong") continue;
        // host.kernel_isolation 谓 `none`
        // 定义上,每个过程后端上, 而不是作为退化 -- 见
        // docs/design/ sandbox-boundary.md §13.
        // 将它在这里列出,每次都会训练用户忽略此线,这与点相反.
        if (w.name == "host.kernel_isolation") continue;
        // 不需要QQ 而不是空隙。 资源上限默认为 0(
        // 合适的上限取决于具体机器。
        // sandbox_config.hpp) , 因此默认政策是常见的,
        // 并且会支配一条意在解决实际问题的线条。
        if (w.not_requested) continue;
        if (!out.empty()) out += "  \xc2\xb7  ";
        out += w.name + ": " + w.strength;
        if (!w.mechanism.empty()) out += " (" + w.mechanism + ")";
    }
    if (!out.empty()) out = "weaker than asked \xe2\x80\xa2 " + out;
    else
        out = "every wall this policy asks for is enforced strongly";

    // 任何政策 ASKED 永远不要与上面的
    // 墙叠合: 沉默退化的能力就是谎言 面板
    // 存在是为了防止,所以它得到了自己的条款。
    if (!p.preview.unenforceable.empty()) {
        out += "\nthis host cannot enforce: ";
        for (std::size_t i = 0; i < p.preview.unenforceable.size(); ++i) {
            if (i) out += ", ";
            out += p.preview.unenforceable[i];
        }
    }
    return out;
}

// 或 沙箱 实际停止了,这个会话。
//
// 改变行为的线条,墙壁报告说 WOULD 执行,这说明是什么。
// "cargo tried ptrace ATTACH pid=1"（cargo 尝试用 ptrace 附加 pid=1。）
// 告诉你你的工具链在哪里 "your build failed" 告诉你什么,
// 派人来转 沙箱 关机
//
// 最新数据 首次 这里, 改变种子的存储顺序,
// 因为脚印有一条线或两条房间, 最近的拒绝就是您正在调试的一条。
[[nodiscard]] std::string blocked_summary() {
    const auto feed = tools::util::sandbox::broker::blocked_feed();
    if (feed.empty()) return {};

    std::string out = "blocked: ";
    // 最多3个, 时间最久的投放。 整个种子可以是64个条目,
    // 脚脚不是日志查看器。 这个摘要可以包罗 面板 已停止归纳。
    // 日志中包含全部(通道) `tool` 站点
    // `sandbox.broker` ),这是你去的地方,当三不够。
    std::size_t shown = 0;
    for (auto it = feed.rbegin(); it != feed.rend() && shown < 3; ++it, ++shown) {
        if (shown) out += "  \xc2\xb7  ";
        out += it->syscall + " " + it->detail;
        // 计数只在大于1时: "× 1" 声音很响亮
        // "× 4096" 是一个流浪者的区别 调用 和一圈敲击 否认。
        if (it->count > 1) out += " \xc3\x97 " + std::to_string(it->count);
    }
    if (feed.size() > shown)
        out += "  (+" + std::to_string(feed.size() - shown) + " more in the log)";
    return out;
}

// 探员写到的 HOST 以后会信任的
//
// 结构上与 blocked_summary ()以上,
// 这就是重点,而不是粗体: 被堵塞的系统呼唤是 沙箱 工作,
// 一个信托是 沙箱 被* 被* 通过* -- 代理在目录
// 中写了一个普通文件,允许它写,以及一些外部的东西 沙箱
// 上面的墙上没有看到,因为到那时 VS 代码运行
// .vscode/tasks.json 没有 沙箱 照片里写着
//
// 因此,这是页脚上的一个线, 报告壁壁报告无法
// 描述的边界, 这就是它为什么得到自己的线 而不是
// 被折叠。
[[nodiscard]] std::string handoff_summary() {
    const auto feed = tools::util::handoff::handoff_feed();
    if (feed.empty()) return {};

    std::string out = "host-trusted writes: ";
    std::size_t shown = 0;
    for (auto it = feed.rbegin(); it != feed.rend() && shown < 2; ++it, ++shown) {
        if (shown) out += "  \xc2\xb7  ";
        // 底名,不是全径:脚印窄,有趣的一半
        // "/home/x/repo/.vscode/tasks.json" 结束。
        std::string_view p{it->write.path};
        if (const auto slash = p.rfind('/'); slash != std::string_view::npos)
            p.remove_prefix(slash + 1);
        // 电话 KIND 不只是名字,我记得 "tasks.json" 对大多数人来说毫无意义;
        // 编辑器任务：VS Code 可能在打开目录时执行。
        // 才是关心一切的理由
        out += std::string{p} + " (" +
               std::string{sandbox_cfg::explain(it->kind)} + ")";
    }
    if (feed.size() > shown)
        out += "  (+" + std::to_string(feed.size() - shown) + " more)";
    return out;
}

}  // 命名空间结束。

maya::Element sandbox_panel(const Model& m) {
    auto* o = m.ui.panel.get<ui::panel::Sandbox>();
    if (!o) return nothing();

    // 形式由 状态更新函数 ,但报告是 VIEW 关切:
    // 它就是衍生出来的,它只在这里读过, 通过模型的线
    // 程会给两个所有者一个字符串。
    auto form = o->pane.form;
    form.note = wall_summary(o->pane);

    // 拒绝去 上方 墙壁报告,因为比它高:墙壁
    // 描述了将要执行的内容,这就是实际发生的情况。
    if (const auto blocked = blocked_summary(); !blocked.empty())
        form.note = blocked + "\n" + form.note;

    // 和信任的交接 上面 THOSE 因为他们的成绩比
    // 否认要高 沙箱 握着; 交接是这里唯一能逃脱而不
    // 折断的东西, 这是Pridge2026年7月系列中
    // 实际使用的每一个事件都属于逃生类.
    if (const auto hand = handoff_summary(); !hand.empty())
        form.note = hand + "\n" + form.note;

    // 唯一一件事情是,我对这个 面板 绝不能离开,
    // 不说:a 保存 这儿有 不 更改运行 沙箱 -- 和 下一轮
    // 发射是否真的能提供 列描述的。
    //
    // 策略在进程开始时被封存( Q) : tool/util/sandbox.hpp ),
    // 因此编辑磁盘上着陆并带走 副作用 下一轮发射。
    // agentty 保存表示 "in force now" ,
    // 所以必须说明而非假设例外——一个收紧了线程过滤器并
    // 不断工作的用户,否则会相信一堵尚未上架的墙.
    //
    // 下半部分不见了,脚印答应了 "applies on restart"
    // 无法检查该声明,而预览已经知道三种可能不正确的方式(
    // 不会) 编译 ,引擎无法启动,一种能力无声地退化。
    // 未来的承诺没有后退,就是与错误相同的错误。
    // "sandbox: active" 没有 沙箱 ,
    // 刚刚指出 -- 所以 `restart_outcome` 咨询其裁决;
    // REPLACES 光是承诺,而不是坐在旁边。
    const auto& outcome = o->pane.restart_note;

    if (o->pane.saved_pending_restart) {
        form.note = (outcome.empty()
                        ? std::string{"saved \xc2\xb7 applies on restart, verified against "
                                      "this host (the running sandbox is unchanged)"}
                        : "saved \xc2\xb7 " + outcome)
                  + "\n" + form.note;
    } else if (!outcome.empty()) {
        // 没什么 已保存 , 但磁盘上已经存在的策略是这个
        // 主机无法完全实现的。 值得说, 是不急切的 — 否则
        // 唯一能找出答案的方法就是编辑一些东西。
        form.note = outcome + "\n" + form.note;
    }

    return maya::Panel{form_config(form, info,
                                   &m.ui.sandbox_scroll,
                                   panel_viewport_h(),
                                   panel_terminal_cols())}.build();
}

maya::Element sandbox_list_panel(const Model& m) {
    auto* o = m.ui.panel.get<ui::panel::SandboxList>();
    if (!o) return nothing();

    auto form = o->pane.form;

    // 帮助进入了 SUBTITLE ,其中,每个形式 面板 设置(
    // E) "what am I looking at" 数字。
    // -entry 行无自在的帮助 -- -- 编号就够了,
    // 每行重复解释就会将清单埋在自己的文件中。
    form.subtitle = o->pane.help;

    // 补品名单,作为页脚.
    //
    // 以文字而不是浮动的 覆盖面板 格式渲染: 条目已经
    // 是一个垂直列表, 因此在它们上面的第二个浮动列表将是两个
    // 在屏幕上竞争的光标。 表格下面的几行, 带有突出显示
    // 的一条标记, 保留一个位置可供查看。
    if (o->pane.completing) {
        // visible_entries () 向 快照 用于顶部
        // N 匹配, 已夹住 -- -- 相同 调用 彼此间 选择
        // 面板 视线能制成,所以窗口算术有一个主人.
        //
        // 来,来,来 WINDOWS 绕着选择,
        // 而不是总是显示顶部,现在箭头移动亮点很重要:固定
        // 的顶部-3会让选择的尾部走动,让脚部看起来冷冻。
        // Tab 接受一些隐形的东西。
        const auto hits = o->pane.complete.visible_entries(3);
        if (hits.empty()) {
            form.note = "no match in the workspace \xc2\xb7 the path is still "
                        "accepted (it may be outside, or not exist yet)";
        } else {
            const auto total = o->pane.complete.filtered().size();
            const auto win = o->pane.complete.visible(3);
            const auto sel = o->pane.complete.index();
            std::string line = "\xe2\x86\x91\xe2\x86\x93 pick \xc2\xb7 Tab completes \xc2\xb7 ";
            for (std::size_t i = 0; i < hits.size(); ++i) {
                if (i) line += "   ";
                // 与窗口的绝对位置相比,
                // 而非循环指数:两个只有同意,
                // 而窗口处于顶端,这正是过去唯一同意的情况.
                if (win.first + i == static_cast<std::size_t>(sel))
                    line += "\xe2\x96\xb8 ";
                line += *hits[i];
            }
            if (total > hits.size())
                line += "   (" + std::to_string(sel + 1) + "/" +
                        std::to_string(total) + ")";
            form.note = std::move(line);
        }
        form.note_replaces_grammar = true;
    } else {
        form.note = "type a path \xc2\xb7 ^X clears a line (removes it) \xc2\xb7 "
                    "esc saves and goes back";
    }

    return maya::Panel{form_config(form, info,
                                   nullptr,
                                   panel_viewport_h(),
                                   panel_terminal_cols())}.build();
}

}  // namespace agentty::ui
