#pragma once
#include <optional>
#include <string_view>
#include <vector>
#include <maya/widget/activity_indicator.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 选择底部-of-thread "still working…"指标配置，如果有的话。
// 当主动助手回合已显示时间线加载动画时被抑制——其
// in-progress卡+状态栏的加载动画已携带
// "still working"信号；堆在它下面的
// 第二个只是复制的镀铬物。
[[nodiscard]] std::optional<maya::ActivityIndicator::Config>
    activity_indicator_config(const Model& m);

// 共享主机-owned轮换字池为ActivityIndicator。
// 由 const-ref 返回； 生命周期 是静态的。底部-of- 线
// 程指示器和in-Turn占位符指示器传输都变成了ActivityIndicator::
// Config::words，因此小部件保持内容
// -agnostic。
[[nodiscard]] const std::vector<std::string_view>& activity_indicator_words();

} // namespace agentty::ui
