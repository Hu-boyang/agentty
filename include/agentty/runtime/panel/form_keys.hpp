#pragma once
// agentty::form::keys——每个表格都遵循的ONE键映射。
//
// 表单具有三种交互模式，每种交互模式独占键盘；
// 哪一个处于活动状态是 `Form::focus`，
// 与视图读取的值相同。所以路由器是（焦点，键）->意图的纯函数，
// 并且没有面板编写自己的导航。这就是阻止 面板 像五只手
// -rolled 选择面板 那样偏离的原因——这意味着键映射被
// 记录在一个地方：
//
//   浏览↑↓/kj移动·←→调整到位·Enter edit/open/toggle/fire
//              x重置·^S 保存·Esc关闭
//   编辑可打印→插入·←→光标·^A/^E home/end·^U
//              清除Enter/Esc离开字段选择
//   ↑↓/kj 突出显示 · Enter 提交 · Esc 取消 （值未触及）
//
// 注意NOT的选择有什么：过滤器。下拉列表中包含 枚举，
// 而选项集足够大以需要搜索，这是一个选择行，该行将传递给
// 真正的 选择面板 覆盖面板。
//
// 路由器返回INTENT，而不是Msg：意图是面板-agnostic，
// 因此每个面板将它们映射到其自己的消息集，而不需要重新-deriving键映射。

#include <cstdint>
#include <optional>

#include <maya/terminal/input.hpp>

#include "agentty/runtime/panel/form.hpp"

namespace agentty::form::keys {

enum class Intent : std::uint8_t {
    None,
    // 浏览
    MovePrev, MoveNext,
    MoveFirst, MoveLast,       // Home/End — 跳转至 first/last 字段
    MovePageUp, MovePageDown,  // PgUp/PgDn — 可视区域-sized 步幅
    AdjustDown, AdjustUp,      // ←/→ 在 Toggle/Choice/Number/Slider 上
    Activate,                  // Enter
    ResetField,                // ^X
    Close,                     // Esc — 离开现场（提交）或关闭 面板
    // 浏览，仅文本-like行：在浏览STARTS编辑会话时
    // 可打印带类型的并插入——类型-to-edit，
    // 无Enter模态。与插入不同，因此插入上的 过期-batch
    // 防护（仅编辑 -focus）保持密封：针对 过期 编辑
    // 快照 翻译的粘贴的 末尾 字符仍然会死掉，而不是默默地
    // re-entering 字段。
    TypeToEdit,                // `ch`携带代码点
    // 编辑
    Insert,                    // `ch`携带代码点
    Backspace, DeleteForward,
    CaretLeft, CaretRight, CaretHome, CaretEnd,
    ClearField,
    LeaveField,                // 编辑时Enter/Esc
    // Tab。对于提供补全功能的窗格来说 "accept the suggestion"
    // 意味着什么，对于不提供补全功能的窗格则没有任何意义 - apply()
    // 会忽略它，因此没有补全功能的表单不受影响。
    //
    // Tab而不是Enter，因为Enter已经意味着该层中各处的
    // 提交当前字段，然后继续。
    // 窃取它来完成将使完成的一个字段的行为与其他所有字段不同。
    // Tab 在表格中没有任何先前含义，因此索赔无需任何费用。
    Complete,
    // 选择
    MenuPrev, MenuNext,
    MenuCommit, MenuCancel,
};

struct Action {
    Intent   intent = Intent::None;
    char32_t ch     = 0;       // 对于插入有意义/MenuInput
};

// 针对表单的 当前 焦点平移一个键。当键在这里没有任何
// 意义时返回 nullopt，因此调用者可以陷入环境处理
// （^C 退出等）。每种模式都会为其拥有的可打印键返回一个值，
// 因此击键永远不会从文本字段泄漏到全局导航中。
//
// 采用两个焦点 FLAGS 而不是表单，因为这是整个依赖关系 -
// 并且调用者位于 `subscribe()` 中，它每 帧 运行一次。
// 在那里传递表格意味着每个帧的每行字符串都深-copying，
// 这正是该路由器为了避免高频路径而存在的输入
// 延迟。
[[nodiscard]] std::optional<Action> translate(bool editing, bool choosing,
                                              const maya::KeyEvent& ev);

// 为已经持有表格（测试，状态更新函数)的呼叫者带来便利。
[[nodiscard]] std::optional<Action> translate(const Form& f, const maya::KeyEvent& ev);

// 将意图应用于表格。当表单的 VALUES 更改时返回 true（因此
// 面板 可以将自身标记为脏/使探针无效）。表单无法自行服务的意图 - 保存、
// 关闭、Activate-on-Action - 通过 `escaped`
// /`fired` 返回给调用者，而不是在此处处理。
struct Applied {
    bool changed  = false;     // 值被突变
    bool fired    = false;     // 操作行被激活
    bool hand_off = false;     // 拣选行想要打开其选择面板
    bool close    = false;     // Esc 在最外层
    // 编辑焦点结束THIS击键(Enter/Esc/arrow在文本字段之外)
    // AND字段的值在该编辑会话期间改变。
    // Commit-on-exit:留下字段IS和保存——没有单独的保存键。
    // 不进行更改就退出将保持错误状态，因此在字段中移动光标永远
    // 不会重写配置（PANELS.md 中的义务#4）。
    bool left_field = false;
    // 其值确定该击键的行，或-1。设置为 in-place 更改和
    // 带类型的 编辑结束。箭头在同一击键中移动光标，因此 面板
    // 提交 此 行，而不是 form.focused()。
    int settled = -1;
};
Applied apply(Form& f, Action a);

} // namespace agentty::form::keys
