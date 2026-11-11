#pragma once
// agentty::ui_prefs::live —外观首选项，可从视图中读取。
//
// ── 为什么是接口边界而不是参数 ───────────────────────────────────────
//
// 偏好存在于 Model 上，并且视图是它的纯粹函数——因此
// 原则上每个消费者都应该将它们作为参数。实际上，消费者是像
// `panel_viewport_h()` 这样的地方，
// 这是一个由二十个面板制造商调用的免费函数，
// 而 StreamingMarkdown 设置则埋在 turn.cpp 中；
// 将 `const Prefs&` 线程化到每个线程意味着更改数十个与外观
// 无关的签名，并且会将首选项参数放在调用者也没有 Model 的函数上。
//
// 因此，每个 帧 的首选项为 PUBLISHED，与 maya 发布其主题
// 槽（app_set_theme)完全相同，并且出于完全相同的原因：交换必须
// 到达无法在不重写周围世界的情况下传递 Model 的代码。
// `view()` 调用 publish() 在构建任何东西之前； 下游 的所有内容
// 都显示为 current()。
//
// ── 这是什么 不 ─────────────────────────────────────────────────────
//
// 不是存放 可变 UI 状态的地方。它保存着 Model 的一个字
// 段的 COPY，每 帧 从 Model 刷新一次，除了 publish() 之外，
// 没有任何内容写入其中。 状态更新函数 绝不能是 调用
// publish()——Model 是事实，这是它的投影，
// 所以写在这里反而会把事实放在两个地方，让他们不同意。
//
// ── Thread safety ────────────────────────────────────────────────────────
//
// 发表于渲染 线程，阅读于渲染 线程。 锁 用于背景 工作线程，
// 构建工具预览 -frame：他们得到一致的 快照
// 而不是撕裂的阅读，这就是他们所需要的 - 他们不关心
// 他们看到的 对应的 帧 的首选项，只关心它是其中
// 之一。

#include <maya/runtime.hpp>

#include "agentty/domain/ui_prefs.hpp"

namespace agentty::ui_prefs {

namespace detail {
// 快照，不是锁：由view()每帧发布一次并从视图代码、
// 面板和背景预热中读取。这里的 锁 需要每个读者都有
// 一个等级和能力，而 view() 则故意不提供（它一定
// 不能接受 锁）。 快照 读取是一个原子 加载，
// 并且任何读取器都不能成为 死锁 的一部分。
inline maya::snapshot<Prefs>& slot() noexcept {
    static maya::snapshot<Prefs> s;
    return s;
}
}  // namespace detail

// 使`p`成为每个消费者从现在开始看到的偏好。在构建任何元素之前，
// view() 每 帧 调用一次。
inline void publish(const Prefs& p) { detail::slot().store(p); }

// 生效的偏好。默认值直到第一个 publish()，
// 因此在第一个 帧（测试、后台预热）之前运行的使用者将
// 获得记录的默认值而不是垃圾。
[[nodiscard]] inline Prefs current() { return detail::slot().get(); }

// ── 方便阅读器 ──────────────────────────────────────────────────
//
// 以每个消费者询问的QUESTION命名，而不是以其读取的字段命名。
// 消费者说，如果运动模型增长到第四级，`animations_on()`继续工作；
// 上面写着 `current().motion == Motion::Full`
// 必须找到并修复。

// 是否有任何东西可以移动：流式传输、逐步显示、加载动画、转变。
[[nodiscard]] inline bool animations_on() {
    return current().motion != Motion::Off;
}

// 与逐行剪辑相反，逐步显示的DECORATIVE层
// 是否可以运行(扰乱、渐变、闪烁光标)。减少保留文本
// 进入 - 这是有关进度的信息 - 并删除字形搅动，
// 而事实并非如此。
[[nodiscard]] inline bool reveal_decoration_on() {
    return current().motion == Motion::Full;
}

// self-driven动画可以重绘的频率，作为帧速率的除数。
// 1 = 每 帧； 4 = 四分之一 帧。
//
// 运动不是一个轴，而是两个轴，将它们混为一谈使得中间设置对最
// 需要它的人没有任何帮助。装饰大约是 VISUAL NOISE
// （一个可访问性问题 - 混乱和字形搅动对于前庭障碍来说是令人
// 不快的）。 重绘的速度大约是BANDWIDTH：在高
// -latency 链接上狂奔，你付出的代价是NUMBER
// OF FRAMES的变化，而不是每一个的风格。
//
// 在记录的 数据流 (tests/fixtures/anthropic_md_tour.jsonl)
// 上测量，在此之前：
//
//     全1753改变帧
//     减少1753改变帧←相同；装饰重新设计 字节
//                                      无论如何都会发送
//     Off 98 更改 帧
//
// 因此，滞后的 链接 上的用户恰好有两个选择：完全 重绘 流失，
// 或者根本没有 逐步显示。 "Reduced" 读起来像中间选项，
// 并提供了第一个选项的成本 - 即问题#36。
//
// 现在减少也使 重绘 速率变薄，因此它位于其名称所承诺
// 的位置：文本仍然在 帧 的四分之一处进入（进度仍然清晰）。
// Full 和 Off 不变。
[[nodiscard]] inline int motion_frame_divisor() {
    switch (current().motion) {
        case Motion::Full:    return 1;
        case Motion::Reduced: return 4;
        case Motion::Off:     return 1;   // 反正没什么 self-drives
    }
    return 1;
}

// 面板高度上限，以行为单位。仍然是 MAXIMUM——调用者
// 固定 终端 实际拥有的内容。
[[nodiscard]] inline int panel_rows() {
    return viewport_rows(current().density);
}

// ── 路径根 ──────────────────────────────────────────────────────────
//
// 其中工具卡显示相对于以下的路径：来自Model::env的启动cwd和主目录。
// 与首选项相同的 接口边界 和相同的原因：阅读器（pretty_path，
// 在每个工具卡的 头文件 下）是一个免费功能，位于任何持有 Model
// 的东西下面的许多层，并且 状态更新函数 的工具查看器 调用 是相同的助手。
// view() 每个 帧 发布它们； read_launch_env
// 捕获了它们一次，因此视图永远不会询问 OS。
struct PathRoots {
    std::string cwd;
    std::string home;
    // 由项目(Model::ui.库)定义的子代理类型。任务卡给他们
    // 贴上标签；它与 pretty_path 位于同一个免费助手下。
    std::vector<std::string> project_agents;
};

namespace detail {
inline maya::snapshot<PathRoots>& roots_slot() noexcept {
    static maya::snapshot<PathRoots> s;
    return s;
}
}  // namespace detail

inline void publish_path_roots(PathRoots r) { detail::roots_slot().store(std::move(r)); }
[[nodiscard]] inline PathRoots path_roots() { return detail::roots_slot().get(); }

}  // namespace agentty::ui_prefs
