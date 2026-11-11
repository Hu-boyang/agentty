#pragma once
// agentty::ui::panel-EXCLUSIVE面板插槽，作为和类型。
//
// Model::UI用于承载16个独立面板字段（此处为pick::OneAxis，
// 此处为ad-hoc Closed|Open 变体）。
// "Only one overlay open at a time" 是
// 一个 CONVENTION，由大约 17 个分散的 `m.ui.X = pick::Closed{}`
// 写入在每个 Open* 状态更新函数 内强制执行 - 忘记一个和两个 覆盖面板 同时打开，
// 路由器 (overlay.hpp) 在 分派 时间掩盖了歧义。
//
// 现在每个专用面板都位于ONE槽中：
//
//     m.ui.panel.descend(pn::Models{{.index = 3}});  // 打开 OVER (
//     Esc 展开) m.ui.panel.replace(pn::Providers{{2}});         // 跳
//     SIDEWAYS （保留父级） m.ui.panel.is<pn::Models>() // 打开？
//     m.ui.panel.get<pn::Models>() // 载荷* 或 nullptr
//     m.ui.panel.close<pn::Models>() // 关闭最顶层的 IF
//
// 一个槽，一个变体——所以"two exclusive panels open"不再是
// 我们防范的错误，它是UNREPRESENTABLE。所有对cipal
// -closing的写入都被删除，而不是重新定位。
//
// NOT给你的变体是母链，这就是每个导航错误存在的地方。
// 分配曾经是打开的方式，它会默默地丢弃链 -
// 因此从另一个面板打开的面板中的 Esc 完全离开了堆栈。
// 作业现在是 DELETED；
// 请参阅下面状态中的三个动作（下降/替换/恢复），
// 这是讲述故事的唯一地方。
//
// 有意的 NON-members (塑造设计的例外情况）：
//   ·登录——具有Origin-frame导航的9-state认证机；
//                  关闭的是ITS 变体的第一-class成员。
//   ·权限 —m.d.pending_permission为DOMAIN状态：数据流
//                  状态更新函数可能在覆盖面板打开时在
//                  -turn中期提高它。两者必须共存；
//                  路由器只是将键盘交给 权限。
//   ·todo — AMBIENT 面板，不是 弹出面板：它保持打开状态 UNDER
//                  选择面板和无人认领的密钥落入全局。其打开的 标志
//                  保留在 TodoState 上。
// panel::top()(overlay.hpp)将这四
// 个源——登录、权限、这个槽、待办事项——组合成一个优先级答案，
// 现在是四次检查而不是二十次。
//
// 每个备选INHERITS其覆盖面板的现有开放载荷 (
// pick::OpenAt、palette::Open、…)：相同的字段、
// 相同的不变量、相同的操作代码——加上不同的TYPE，
// 因此槽可以区分覆盖面板并且调用站点直接命名它们。
//
// 指针生命周期:get<K>()指向INTO槽。
// 分配插槽破坏了之前的替代方案 - 捕获您需要的 之前，
// 打开不同的 覆盖面板。 （对于单独的字段来说同样如此：
// 分配 pick::Closed{} 也会破坏 载荷。）

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <maya/core/scroll_state.hpp>   // WebSearch::scroll

#include "agentty/runtime/panel/common.hpp"
#include "agentty/domain/smart_mode.hpp"   // smart::OverlayRow
#include "agentty/runtime/panel/palette.hpp"
#include "agentty/runtime/panel/mention.hpp"
#include "agentty/runtime/panel/symbol.hpp"
#include "agentty/runtime/panel/code_blocks.hpp"
#include "agentty/runtime/panel/skills.hpp"
#include "agentty/runtime/panel/stats.hpp"
#include "agentty/runtime/panel/tool_output.hpp"
#include "agentty/runtime/panel/checkpoints.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/runtime/panel/settings/list.hpp"
#include "agentty/runtime/panel/fork.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/panel/sandbox.hpp"
#include "agentty/runtime/panel/form.hpp"

namespace agentty::ui::panel {

struct None {};

// ── 从: 面板 打开 OVER, 成为 XX00120 的全版 快照 ──────
//
// Esc必须解除ONE关卡，而"one level up"取决于你
// 如何到达这里——除非被告知，否则小组无法知道这一点。
// 旧的答案（settings_origin::Origin）将父级KIND
// 加上hand-picked字段（一个调色板行，一个设置类别+行）和
// back_to（）RECONSTRUCTED命名为它们的父级 - 重建
// 忘记的每个字段（调色板的half-typed查询）都是用户状态默默地丢弃，
// 并且每个想要该行为的面板在每个打开器上都需要自己的标记。
//
// From 改为携带父代的ENTIRE槽值。
// 恢复就是将其复制回来 - 无需重建，无需忘记 -
// 并且隐藏的父级包含 ITS，因此调色板 → 设置
// 列表 → 面板 逐级展开，无需任何堆栈来与插槽保持同步：
// 链存在于值内，以用户实际下降的深度为
// 界。
//
// shared_ptr<const>:Model在状态更新函数
// 循环中按值复制，因此快照必须廉价复制并且安全共享-不可变，
// 恢复BY COPY，从未就地变异。 快照是在变体之后定义的（通过这
// 一间接的类型是self-referential）。
//
// 恢复的状态可以是STALE——当子项打开时模型可能已经改变（数据流结束；
// 应用了设置）。 ascend() 的调用者拥有重新验证：夹紧游标、
// 重建表单。参见 app::detail::ascend()。
struct Snapshot;
class From {
public:
    From() = default;
    [[nodiscard]] bool empty() const noexcept { return !s_; }
    [[nodiscard]] const Snapshot* get() const noexcept { return s_.get(); }
    static From of(Snapshot s);
private:
    std::shared_ptr<const Snapshot> s_;
};

// 每个替代方案继承的基础：Esc去哪里。默认-empty = "opened over the thread"，
// 所以Esc关闭。
struct WithFrom { From from; };

// ── 排他性小组:各一个不同的类别, 载荷 继承 ───
struct Models     : pick::OpenAt, WithFrom {
    // Smart-Mode slot-assign模式：这个选择面板
    // 被打开BY一个SmartMode Pick行以将模型固定到
    // `assign_slot`中，并且Enter写入引脚而不是切换模型。
    // 携带 ON 面板 - 该模式是 此 选择面板 实例的属性，
    // 而不是应用程序的属性 - 因此放弃 选择面板（关闭，跳至提供商）
    // 从结构上放弃了该模式；没有停放标志记得重置。 SmartMode
    // 面板 必须像其他家长一样恢复 `from` 中的游乐设施（快照
    // 携带表格、高级 标志 和嵌套链 - 没有其他可以停放的）。
    std::optional<smart::ModelRole> assign_slot;
};
// 提供商选择面板。 `show_all` 显示您已配置 不
// 的提供程序，其浏览会折叠成单个 "N more" 路标。
// view-only 标志：它扩大了行列表，仅此而已。
struct Providers  : pick::OpenAt, WithFrom { bool show_all = false; };
// 线程 选择面板，加上^R标题编辑器。 `renaming` 保存正在重
// 命名的 线程 的 id（空 = 浏览）：一个 id，而不是一行，
// 因此重新排序行的异步列表刷新无法将编辑移动到另一个 线程。
// `rename_input` 为草稿； `rename_cursor` 是 光标，
// 是 字节 的偏移量，始终位于代码 -point 边界上。
struct ThreadList      : pick::OpenAt, WithFrom {
    std::string renaming;
    std::string rename_input;
    int         rename_cursor = 0;
    // Tab缩小到该文件夹​​的线程。从 全部:线程 已保存 开始，
    // 在记录文件夹之前没有任何文件夹，默认隐藏它们
    // 看起来历史记录已经消失了。
    bool        all_workspaces = true;
    // ^O仅显示来自其他工具的会话（Codex、Claude代码）。
    bool        only_foreign = false;
    // 窥视：其显示哪个线程，以及其视图从最新返回
    // 多少圈（0 =最新）。按照 线程 重置。
    std::string peek_id;
    int         peek_turn = 0;
};
// 智能模式携带带类型的 ROW，而不是索引。另一个 选择面板 列出
// 了可变数量的 运行时-derived 条目，因此 int 游标是
// 那里的诚实表示；智能模式的行是一个固定的命名集，将它们拼写为
// int 会让光标漂移到不存在的行上（有关完整的 post-mortem，
// 请参阅 smart::OverlayRow）。
// 智能模式携带其FORM，而不是int光标。行标识、导航、
// env-pin 锁 和 选择面板 hand-off 都来自
// 共享表单层，因此这个 面板 的行为与检索和每个未来的配置表面
// 相同 - 并且 out-of-range 游标不可表示。
struct SmartMode : WithFrom {
    agentty::form::Form form;
    // 显示高级路由-policy行(^A)。查看状态，
    // 而不是配置 - 它随 覆盖面板 一起终止并且不会持久化。
    // 保留 此处 是因为每次重建（插槽分配重新打开 面板）都必须保留它，
    // 否则在用户固定模型时这些行就会消失。
    bool advanced = false;
};
// 设置→网络搜索。带有FORM，就像智能​​模式一样：行、
// 光标、环境锁和文本编辑都来自共享表单层。这些行是设置 注册
// 表 的 web_search.* 行，因此这个 面板 没有
// 自己的配置 - 它是在每次提交后从 Model 重建的。
//
// 滚动偏移存在于 此处 中，而不是在 Model::UI 的滚动包中：槽
// 游走对其进行散列（因此 帧 门看到滚动），并且它与 面板 一起消亡（
// 因此从顶部开始重新打开）。形状 pn::Stats 已建立。
struct WebSearch : WithFrom {
    agentty::form::Form form;
    mutable maya::ScrollState scroll = [] {
        maya::ScrollState s;
        s.auto_dispatch = false;   // 表格层拥有运动
        return s;
    }();
    // 每个密钥服务的API密钥来自何处，如在面板打开时（
    // WebSearchKeysRead)读取UI 线程：服务ID->
    // "stored"、"env: ..."或""（无）。从来没有钥匙本身。
    // 在读取答案之前，`keys_read` 为假，关键行也是如此。
    std::vector<std::pair<std::string, std::string>> key_origins;
    bool          keys_read = false;
    // 受到每个密钥读取、存储和移除的影响，因此仅显示最新
    // 读取的答案。
    std::uint64_t keys_gen  = 0;
    // 关键源在字段被编辑时到达；
    // 编辑结束后立即重建行。
    bool          rows_stale = false;
};
// 设置→粘贴。每一行都是粘贴。* 注册表 行，因此与 WebSearch 一样，
// 它没有自己的配置，并且在提交后从 Model 重建。
struct PasteSettings : WithFrom {
    agentty::form::Form form;
    mutable maya::ScrollState scroll = [] {
        maya::ScrollState s;
        s.auto_dispatch = false;   // 表格层拥有运动
        return s;
    }();
};
struct Palette  : agentty::palette::Open, WithFrom {};
struct Mention         : agentty::mention::Open, WithFrom {};
struct Symbol          : agentty::symbol::Open, WithFrom {};
struct CodeBlocks      : agentty::code_blocks::Open, WithFrom {};
struct CodeBlockResult : agentty::code_blocks::Result, WithFrom {};
struct ToolOutput      : agentty::tool_output::Open, WithFrom {};
struct Checkpoints     : agentty::checkpoints::Open, WithFrom {};
struct Rag     : agentty::rag_settings::Open, WithFrom {};
struct SettingsList    : agentty::settings::ListOpen, WithFrom {};
// 插件 detail/add 编辑器 — 用于 两者 流程的 面板 的一种形式。
// `server` 空 == ADD 模式（当用户选择 stdio/http/passthrough 时，
// `kind` 选择行重建字段集...）； non-empty == 编辑该服务
// 器的条目（身份行已锁定，配置行可编辑，保存 强制-overwrites）。
// `project` 将写入路由到 ./.agentty/mcp.json 与用户文件。
struct PluginEdit : WithFrom {
    agentty::form::Form form;
    std::string server;      // ""=添加模式
    bool        project = false;
    // CURRENT现场装置是为这种类型而构建的。当 `kind` 选择更改时，
    // 状态更新函数 会重建新类型的表单，同时保留用户已经 带
    // 类型的 的 name/url 文本 — 与 SmartMode
    // 的高级 切换 相同的重建保留模式。
    std::string built_kind;
};
// 外观面板。诸如 SmartMode/PluginEdit 之类的表单
// ——分组行，在每次击键时都会存在。 `picking` +
// `picker` 是浮动 OVER 的主题浏览器：选择行不予理睬，
// 而不是将下拉菜单增长为更糟糕的 选择面板，并且 面板 保持在后面，
// 因此列表是其自己的预览。
//
// 组合，不双重-inherited:视觉门通过结构化绑定分解面板，
// 这两个带有构件的底座构成Ill-formed。
struct Appearance : WithFrom {
    agentty::ui::panel::AppearancePane pane;
};
// 沙箱 面板。出于相同的结构化-binding原因，
// 也由组合而不是双重-inherited组成。
// 携带整个面板（表单+实时墙预览+阻塞日志），因为预览是在每次编辑
// 时从表单重新计算的，所以两者必须一起旅行。
struct Sandbox : WithFrom {
    agentty::ui::panel::SandboxPane pane;
};
// path/port列表编辑器，打开FROM一沙箱 Pick行。
//
// 它自己的槽而不是沙箱的模式：它具有它自己的形式、它自己的密钥、
// 它自己的提交和它自己的完成器。将其折叠起来会给一个 面板 两种形式加上一个
// "which am I editing" 标志——两个 -bools-for-three-
// 状态形状已经在这个 面板 中产生了两个错误。
struct SandboxList : WithFrom {
    agentty::ui::panel::SandboxListPane pane;
};
struct Fork            : agentty::fork_panel::Open, WithFrom {};
struct DiffReview      : pick::OpenAtCell, WithFrom {};
struct Stats           : agentty::stats_panel::Open, WithFrom {};
struct Skills          : agentty::skills_panel::Open, WithFrom {};

using Variant = std::variant<
    None,
    Models, Providers, ThreadList, SmartMode, WebSearch, PasteSettings,
    Palette, Mention, Symbol,
    CodeBlocks, CodeBlockResult, ToolOutput, Checkpoints,
    Rag, SettingsList, PluginEdit, Appearance, Sandbox, SandboxList, Fork,
    DiffReview, Stats, Skills>;

// 让类型引用自身的一种间接方式：隐藏的父代
// 是从包含的整个槽值。
struct Snapshot { Variant v; };
inline From From::of(Snapshot s) {
    From f;
    f.s_ = std::make_shared<const Snapshot>(std::move(s));
    return f;
}

template <class K>
concept Alternative = requires(Variant v) { std::holds_alternative<K>(v); };

// 槽本身：薄包装，因此调用站点被读取为意图（
// `m.ui.panel.is<pn::Models>()`)而不是变体管道。
//
// ── 三个动作 ────────────────────────────────────────────────
//
// 导航是 STACK，您可以对其执行三件事。
// 它们与 仅 的不同之处在于母链发生的情况，
// 这正是 调用 网站不断出错的部分：
//
//   下降(k)DOWN。     k.from 快照 什么是开放的。 Esc 放松下来。
//                         对于公开*处理函数:"open k over here"。
//   替换(k)SIDEWAYS。 k INHERITS 当前面板的父面板。
//                         对于同级之间的跳跃（从型号 选择面板 到提供商
//                         选择面板 的 ^P）——您离开的面板消失了，
//                         但 CAME FROM 的位置没有改变。
//   恢复(k)UP。       k 被逐字放回，包括链。
//                         对于 ascend()，以及重建面板，
//                         您已经在里面了。
//
// 其中每一个都至少出现过一次，并且总是相同的形状：
// 调用网站，意思是一个，拼写另一个，没有类型来阻止它。
//
//   ·`operator=`是DROPPED链的第四个未命名的举动。它
//     被记录为 "use descend at OPEN sites" — 注释，
//     因此未强制执行：分配了 13 个开放* 状态更新函数 和 8 个下降。
//     调色板 → 提供者 → Esc 完全离开了堆栈。现在是DELETED。
//   ·侧向跳跃被拼写为`close<Old>(); descend(New);`，其
//     读起来像"swap panels"，意味着"drop the grandparent":
//     close()没有留下任何内容，所以descend()找不到任何可以隐藏的东西。
//     调色板 → 模型 → ^P → Esc 退出而不是返回到调色板。
//     这就是 replace() 存在的意义。
//
// 所以规则是：如果您要改变槽中的内容，您必须说出您指
// 的是三个中的哪一个。已经没有无名的路了。
class State {
public:
    State() = default;

    // 分配被删除：它默默地丢弃了父链。说下降（k）去DOWN，
    // 替换（k）去SIDEWAYS，
    // 或者恢复（k）将面板与它已经携带的链条放回去。
    template <Alternative K>
    State& operator=(K k) = delete;

    template <Alternative K>
    [[nodiscard]] bool is() const noexcept {
        return std::holds_alternative<K>(v_);
    }
    template <Alternative K>
    [[nodiscard]] K* get() noexcept { return std::get_if<K>(&v_); }
    template <Alternative K>
    [[nodiscard]] const K* get() const noexcept { return std::get_if<K>(&v_); }

    [[nodiscard]] bool any_open() const noexcept {
        return !std::holds_alternative<None>(v_);
    }

    // 关闭K IF为打开覆盖面板;保留任何其他 覆盖面板。
    // 镜像旧的per-field关闭语义：在用户跳到提供
    // 者选择面板之后到达的过期 CloseModels
    // 不得关闭提供者选择面板。
    template <Alternative K>
    void close() noexcept {
        if (std::holds_alternative<K>(v_)) v_ = None{};
    }
    // 无条件关闭（无论打开什么）。
    void close_all() noexcept { v_ = None{}; }

    // 打开 K OVER 当前的 覆盖面板：K 的 `from`
    // 变成现在打开的任何内容的 快照，因此 Esc 可以逐字恢复它 — 查询、
    // 游标、嵌套等等。打开 None 不会隐藏任何内容（从保持空），
    // Esc 只是关闭："the thread" 不需要 快照。
    //
    // Re-opening SAME类型是重建，而不是血统。
    // 如果没有这项检查，重新打开自己的面板以刷新它的
    // 状态更新函数（智能模式在每个插槽分配上都会执行此操作）会
    // 将面板隐藏为自己的父面板，而 Esc 会一次剥离一个相同
    // 的副本而不是离开。新面板保留了旧面板的链条。
    template <Alternative K>
    void descend(K k) {
        if (std::holds_alternative<K>(v_)) {
            k.from = std::move(std::get<K>(v_).from);
        } else if (!std::holds_alternative<None>(v_)) {
            k.from = From::of(Snapshot{std::move(v_)});
        }
        v_ = std::move(k);
    }

    // DOWN进入K，即使K是打开的：当前面板成为父面板。
    // 对于打开同类实例的面板，例如打开插件列表的设置中心。
    // descend() 会将其视为重建，
    // 而 Esc 将跳过集线器。
    template <Alternative K>
    void push(K k) {
        if (!std::holds_alternative<None>(v_))
            k.from = From::of(Snapshot{std::move(v_)});
        v_ = std::move(k);
    }

    // SIDEWAYS:将打开的面板交换为K，保留ITS父面板。
    //
    // 同级之间的跳跃——从型号选择面板到提供者选择面板或任何
    // "leave this, open that at the same level"的^P。
    // 您要离开的面板将被丢弃；你 CAME FROM 不是。
    //
    // 这过去被拼写为 `close<Old>(); descend(New);`，
    // 读起来像交换并且表现得像截断：close() 没有留下
    // 任何内容，因此后面的 descend() 找不到任何东西
    // 可以隐藏并默默地丢弃祖父母。调色板→模型→^P→Esc
    // 然后退出堆栈而不是返回到调色板。
    //
    // Over None 这只是一个没有父级的开放，
    // 这与 descend() 会做的事情是一样的——从无到有的跳跃。
    template <Alternative K>
    void replace(K k) {
        k.from = std::visit(
            [](auto& a) -> From {
                if constexpr (requires { a.from; }) return std::move(a.from);
                else return From{};
            },
            v_);
        v_ = std::move(k);
    }

    // 将 K 完全按照给定的方式放回 — 它的 `from` 已经是它应该的样子。
    // 对于 ascend() 恢复隐藏的父项，
    // 对于 状态更新函数 重建面板，它已经在里面了。
    //
    // 下降的对应部分：下降的恢复将把子项藏起来作为
    // 其自己的父项，并且Esc将循环而不是展开。
    template <Alternative K>
    void restore(K k) {
        v_ = std::move(k);
    }

    // 需要步行多少个面板Esc才能离开。 0 = 没有打开任何内容。
    // 暴露用于测试和诊断：深度是导航
    // 合同实际上涉及的属性。
    [[nodiscard]] int depth() const noexcept {
        int n = 0;
        const Variant* cur = &v_;
        while (!std::holds_alternative<None>(*cur)) {
            ++n;
            const From* f = std::visit(
                [](const auto& a) -> const From* {
                    if constexpr (requires { a.from; }) return &a.from;
                    else return nullptr;
                },
                *cur);
            if (!f || f->empty()) break;
            cur = &f->get()->v;
        }
        return n;
    }

    // 给CURRENTLY-OPEN 覆盖面板一个父代，
    // 如果它还没有的话。调用者模式：分派器（调色板选择，
    // 设置-list操作）快照本身，关闭，运行命令 - 然后采用，
    // 因此打开的命令WHATEVER继承分派器作为其Esc目标。
    // 通用：分派器 不需要 per-command 知识，
    // 并且不打开任何内容（无）或 re-opened 已
    // 具有父级的内容的命令将被保留。
    void adopt(From f) {
        std::visit(
            [&](auto& a) {
                if constexpr (requires { a.from; })
                    if (a.from.empty()) a.from = std::move(f);
            },
            v_);
    }

    // 恢复该覆盖面板被打开的父项。如果没有（通过 线程 或 pre
    // -descend 代码路径打开），则为 False — 调用
    // 者将关闭。恢复后的状态可能是STALE；调用者拥有重新验证（
    // app::detail::ascend 包装两者）。
    [[nodiscard]] bool ascend() {
        const From* f = std::visit(
            [](const auto& a) -> const From* {
                if constexpr (requires { a.from; }) return &a.from;
                else return nullptr;
            },
            v_);
        if (!f || f->empty()) return false;
        // 复制出BEFORE覆盖：快照位于v_内。
        auto keep = f->get();
        Variant restored = keep->v;
        v_ = std::move(restored);
        return true;
    }

    [[nodiscard]] const Variant& raw() const noexcept { return v_; }

private:
    Variant v_;
};

// ── kind: 分派器 共享的路由名称和视图 ────────────────
enum class Kind {
    None,
    Login,
    Permission,
    Palette,
    Mention,
    Symbol,
    CodeBlocks,
    CodeBlockResult,
    ToolOutput,
    Checkpoints,
    Rag,
    SettingsList,
    PluginEdit,
    Appearance,
    Sandbox,
    SandboxList,
    Fork,
    Models,
    Providers,
    ThreadList,
    SmartMode,
    WebSearch,
    PasteSettings,
    DiffReview,
    Todo,
    Stats,
    Skills,
};

// 槽替代方案→种类。一位详尽的访客：在这里添加一个没有手臂
// 的替代方案是一个 编译 错误，而不是无声的错误路线。
[[nodiscard]] inline Kind kind_of(const State& s) noexcept {
    struct V {
        Kind operator()(const None&)            const { return Kind::None; }
        Kind operator()(const Models&)     const { return Kind::Models; }
        Kind operator()(const Providers&)  const { return Kind::Providers; }
        Kind operator()(const ThreadList&)      const { return Kind::ThreadList; }
        Kind operator()(const SmartMode&)       const { return Kind::SmartMode; }
        Kind operator()(const WebSearch&)       const { return Kind::WebSearch; }
        Kind operator()(const PasteSettings&)   const { return Kind::PasteSettings; }
        Kind operator()(const Palette&)  const { return Kind::Palette; }
        Kind operator()(const Mention&)         const { return Kind::Mention; }
        Kind operator()(const Symbol&)          const { return Kind::Symbol; }
        Kind operator()(const CodeBlocks&)      const { return Kind::CodeBlocks; }
        Kind operator()(const CodeBlockResult&) const { return Kind::CodeBlockResult; }
        Kind operator()(const ToolOutput&)      const { return Kind::ToolOutput; }
        Kind operator()(const Checkpoints&)     const { return Kind::Checkpoints; }
        Kind operator()(const Rag&)     const { return Kind::Rag; }
        Kind operator()(const SettingsList&)    const { return Kind::SettingsList; }
        Kind operator()(const PluginEdit&)      const { return Kind::PluginEdit; }
        Kind operator()(const Appearance&)      const { return Kind::Appearance; }
        Kind operator()(const Sandbox&)         const { return Kind::Sandbox; }
        Kind operator()(const SandboxList&)     const { return Kind::SandboxList; }
        Kind operator()(const Fork&)            const { return Kind::Fork; }
        Kind operator()(const DiffReview&)      const { return Kind::DiffReview; }
        Kind operator()(const Stats&)           const { return Kind::Stats; }
        Kind operator()(const Skills&)          const { return Kind::Skills; }
    };
    return std::visit(V{}, s.raw());
}

} // namespace agentty::ui::panel
