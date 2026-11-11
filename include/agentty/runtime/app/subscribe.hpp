#pragma once
// agentty::app::subscribe 负责定时器、工作线程，以及输入事件到 Msg 的转换。
//
// subscribe(m) 声明 Tick 定时器和登录工作任务。终端输入由
// on_key、on_paste、on_focus、on_resize 处理。jaal 调用这些函数时传
// 入事件到达那一刻的模型（jaal D43），因此输入路由不会读取过期副本。

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <maya/maya.hpp>

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/cmd.hpp"
#include "agentty/runtime/msg.hpp"

namespace agentty::app {

// 流式响应期间的 Tick 周期，
// 统一决定活动轮次中事件循环的唤醒频率。
//
// 下面两处必须使用相同的周期，否则会产生多余重绘，
// 或导致动画停住直到下一次按键。
//   1. subscribe() 使用它作为 `Sub::every(period, Tick{})` 的时间间隔。
//   2. Program::visual_hash() 使用它划分细粒度动画的时间桶。
//      时间桶必须与此周期保持相位同步，使渲染条件在每次循环唤醒时
//      恰好推进一次，详见 program.hpp。
// 两处都从同一个函数计算周期，使相位同步由
// 结构保证，无需跨文件手工维护不变量。
//
// 支持 DEC-2026 同步输出的终端使用 33 ms 周期（约 30
// fps），使加载动画保持平滑。其他终端使用 100 ms 周期（约
// 10 fps），减少逐步绘制造成的闪烁。SSH 场景将周期限制为至少
// 80 ms，因为瓶颈在传输链路。该函数只读取 init() 捕获的启动环境，
// 使 subscribe() 与 visual_hash() 得到一致结果。
[[nodiscard]] std::chrono::milliseconds
streaming_tick_period(const Model::Env& env) noexcept;

// ── 动画需求：统一定义“当前有动画正在运行” ──
//
// 以下三个条件共同控制基于时间的动画。
// 条件不一致时，动画可能停止或闪烁。
//   1. subscribe() 决定是否启用 Tick 定时器。
//   2. Program::visual_hash() 决定这次唤醒是否真正进入 view()。
//   3. meta.cpp 的 Tick 分支推进状态更新函数维护的动画状态。
// 过去各处分别内联维护一套动画判定，逐字显示、
// 光标和加载动画的历史故障都与这些判定
// 发生偏差有关，例如思考文字卡住、
// 逐字动画中途冻结、光标受两个时钟驱动而闪烁。
//
// 这些谓词只在此定义一次，
// 统一判断哪些动画正在运行。各处仍按自己
// 的职责决定启用定时器、选择哈希时间
// 桶或推进加载动画。

// 末尾消息仍有尚未合并到稳定正文的实时数据（streaming_text
// / pending_stream）。这是判断逐字显示
// 是否有待处理内容的最早、开销最低的信号。
[[nodiscard]] bool tail_has_live_bytes(const Model& m) noexcept;

// 逐字显示当前需要动画帧。答案通道或纯
// 思考阶段的思考通道仍在接收数据，
// 两者使用相同的逐字显示机制。
// 遗漏思考通道会造成动画卡住，直到下一次按键。
[[nodiscard]] bool reveal_needs_frames(const Model& m) noexcept;

// 传输已经停止，但逐字显示仍在完成收尾。轮次结束时，增量数据已经并入
// `text`，组件的显示位置仍在向末尾移动（is_finalizing
// / reveal_in_progress），或冻结与冷却阶段仍在等待
// 动画结束。此时 m.s.active() 与
// reveal_needs_frames() 都为 false，
// 帧需求完全来自动画状态。visual_hash 的快速时间桶使用此判定，
// 使每个已请求的 RAF 帧真正渲染，避免下次按键时正文突然跳到末尾。
// animation_demand 还将此需求传递给 Tick 分支。
[[nodiscard]] bool reveal_draining(const Model& m) noexcept;

// 存在基于时间的动画，Tick 时钟必须继续运行。
// 这包括活动轮次、加载动画、尚未显示完的实时内容，
// 以及等待逐字显示结束的延迟收尾。该联合条件供第一个
// 控制点使用，后两个控制点通过上述谓词使用其中各项。
[[nodiscard]] bool animation_demand(const Model& m) noexcept;

// 主界面是否启用 ^A / ^X，用于接受或拒绝全部改动。
[[nodiscard]] bool strip_chords_armed(const Model& m) noexcept;

[[nodiscard]] Sub subscribe(const Model& m);

// 终端输入处理函数读取当前模型。返回 nullopt 表示丢弃该事件。
[[nodiscard]] std::optional<Msg> on_key(const Model& m, const maya::KeyEvent& ev);
[[nodiscard]] Msg on_paste(const Model& m, const maya::PasteEvent& pe);
[[nodiscard]] Msg on_focus(const maya::FocusEvent& fe) noexcept;
[[nodiscard]] Msg on_resize(const maya::ResizeEvent& re) noexcept;

// ── subscribe() 读取的字段 ───────────────────────────────────────
//
// jaal 仅在此值变化时重新运行 subscribe()，对应 subs_key 钩子（jaal
// core/program.hpp）。调试构建会检查该值是否覆盖 subscribe() 的全部依赖。
// 输入路由不属于此键值，on_key 等函数始终读取当前模型。
// 这里保存实际值，因此哈希碰撞不会导致过期定时器或工作任务继续存活。
struct SubsKey {
    // 启用 Tick 定时器。
    bool animation_demand = false;
    // Tick 周期读取 init() 一次性设置的 m.env。
    // 仍将周期纳入键值，避免将来修改周期后定时器继续使用旧频率。
    std::chrono::milliseconds tick_period{0};
    // 登录工作任务的来源键为 (attempt, provider)。
    // 没有正在进行的登录时为空。
    std::optional<std::pair<std::uint64_t, std::string>> login_worker;

    [[nodiscard]] bool operator==(const SubsKey&) const = default;
};

[[nodiscard]] SubsKey subs_key(const Model& m) noexcept;

} // namespace agentty::app
