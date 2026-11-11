#include "agentty/runtime/view/status_bar/phase_chip.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include <chrono>
#include <string>
#include <string_view>

#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/provider/selection.hpp"   // provider::active (当地) -host 动词)

namespace agentty::ui {

// ─────────────────────────────────────────────────────────────────────
// 阶段 标签 ——单身 -line "what is the app doing right now?" 读取。
//
// 电话 标签 携带 整个 以单格文字+10的形式运行会话
// 状态 -column 动词+ 已过期的位点。 优先级排序(
// 最高级的胜负) 反映了用户首先需要知道的 :
//
//   1. AwaitingPermission ——障碍进展;必须浮出工具的表面
//   2. 紧凑-长-running,取代历史
//   3. retry::Scheduled ——错误 退避 飞行中
//   4. retry::StallFired      — 数据流 沉默了下来, 看门狗 绊倒
//   5. ExecutingTool——显示运行工具的名称
//   6. Streaming              — "thinking"
//   7. oauth_refresh_in_flight——刷新时被封住的作曲队列
//   8. threads_loading ——背景历史. 加载
//   9. 队列已排队 + 闲置 – "+N queued" 提示
//  10. autocompact_disabled——退化模式(仅限)
//  11. 闲 -- -- "Ready"
//
// 下调 -priority 与活动阶段重叠的信号( Q) e.g
// 中队列中的消息 流式传输 ) , 即为 不 此处显示——
// 作曲家自己 `❚ N queued` 标签 带着它,
// 盗取相位动词来显示队列数会掩盖真实的活动。
// ─────────────────────────────────────────────────────────────────────

namespace {

// 移动最后一个助手消息并返回工具的名称 调用 现居
// `Running` 州,如果有的话。
std::string_view running_tool_name(const Model& m) {
    if (m.d.current.messages.empty()) return {};
    const auto& last = m.d.current.messages.back();
    if (last.role != Role::Assistant) return {};
    for (const auto& tc : last.tool_calls) {
        if (tc.is_running()) return tc.name.value;
    }
    return {};
}

} // 命名空间结束。

maya::PhaseChip::Config phase_chip_config(const Model& m) {
    const bool is_streaming = m.s.is_streaming() && m.s.active();
    const bool is_executing = m.s.is_executing_tool();
    const bool is_awaiting  = m.s.is_awaiting_permission();
    const bool phase_active = is_streaming || is_executing || is_awaiting;

    // 默认——由下面的哪个优先臂胜出而覆盖.
    std::string  glyph;
    std::string  verb;
    maya::Color  color     = phase_color(m.s.phase);
    bool         breathing = phase_active;
    float        elapsed   = -1.0f;

    // 自活动阶段开始以来就已经展开——只在转弯时才有意义。
    // 计算一次就前行,这样每个臂都能选择进入。
    float phase_elapsed = -1.0f;
    if (const auto* a = active_ctx(m.s.phase);
        a && phase_active && a->started.time_since_epoch().count() != 0) {
        auto now = maya::anim_now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - a->started).count();
        phase_elapsed = static_cast<float>(ms) / 1000.0f;
    }

    const auto spinner = std::string{m.s.spinner.current_frame()};

    // ── 1. AwaitingPermission ─────────────────────────────────────────
    // 最高优先级: 用户进度被封杀。 显示 对应的 工具需要批准,
    // 而不仅仅是通用的 "Awaiting" 全部内容 权限 提示词 住在这
    // -thread 弹出面板 ; 此选项 标签 给用户一个透视 -able
    // 当前需要用户作出决定。
    if (is_awaiting) {
        glyph     = "⚠";
        color     = status_warn;
        breathing = true;
        elapsed   = phase_elapsed;
        std::string tool;
        if (m.d.pending_permission) tool = m.d.pending_permission->tool_name.value;
        else                        tool = std::string{running_tool_name(m)};
        verb = tool.empty() ? std::string{"approve?"} : ("approve " + tool);
    }
    // ── 2. 紧凑 ─────────────────────────────────────────────────
    // 长 -running 历史总结 合成人身上的活体
    // user/assistant 双对和普通 流式传输 阶段,
    // 所以我们按键 专用 标志 而不是阶段 变体.
    else if (m.s.compacting) {
        glyph     = spinner;
        verb      = "compacting";
        color     = role_info;            // 蓝色 - context/history 轴
        breathing = true;
        elapsed   = phase_elapsed;
    }
    // ── 3. retry::Scheduled 指数 退避 尝试之间 ───
    else if (m.s.in_scheduled()) {
        glyph     = spinner;
        verb      = "retrying";
        color     = status_warn;
        breathing = true;
        elapsed   = phase_elapsed;
    }
    // ── 4. retry::StallFired 120s 沉默 120s silence. 120s please. 120 sir. 看门狗 绊倒 ─────────
    else if (m.s.in_stall_fired()) {
        glyph     = spinner;
        verb      = "stalled";
        color     = status_error;
        breathing = true;
        elapsed   = phase_elapsed;
    }
    // ── 5. ExecutingTool——显示运行工具的名称 ──────────────
    else if (is_executing) {
        glyph     = spinner;
        verb      = std::string{running_tool_name(m)};
        if (verb.empty()) verb = "running";
        color     = status_ok;            // 绿色——工具执行
        breathing = true;
        elapsed   = phase_elapsed;
    }
    // ── 6. 流式传输——模型正在编组中 ────────────────────────────────
    else if (is_streaming) {
        glyph     = spinner;
        // LOCAL 提供者处理 提示词 在一个20-30B型的
        // 能够是分钟的 20-30 B型号上 "Streaming"
        // 却一无所有 数据流 读起来像绞刑 驱使用户杀死健康
        // 会话(部分) "model locks" 说说到底
        // 发生了什么,直到第一次 字节 土地。
        verb = "Streaming";
        if (const auto* a = active_ctx(m.s.phase);
            a && a->first_delta_at.time_since_epoch().count() == 0) {
            const auto& sel = m.d.selection;
            if (sel.kind == provider::Kind::OpenAI
                && !sel.openai_endpoint.use_tls)
                verb = "processing\xe2\x80\xa6";
        }
        color     = ui::status_info;
        breathing = true;
        elapsed   = phase_elapsed;
    }
    // ── 7. OAuth在飞行中刷新(仅限,但提交文件加盖门) ──
    else if (m.s.oauth_refresh_in_flight) {
        glyph     = spinner;
        verb      = "auth…";
        color     = role_info;
        breathing = true;
    }
    // ── 8. 背景情况 线程-history 加载 ────────────────────────────
    // ── 8. 背景情况 线程-history 加载 ────────────────
    else if (m.s.threads_loading) {
        glyph     = spinner;
        verb      = "loading…";
        color     = muted;
        breathing = true;
    }
    // ── 8b. 飞行中的Single-thread互换 ─────────────────
    else if (m.s.thread_loading) {
        glyph     = spinner;
        verb      = "opening…";
        color     = muted;
        breathing = true;
    }
    // ── 9. 排队信件等待发送 ─────────────────
    // 作曲家还画了 `❚ N queued` 标签 ; 这是状态 -bar 镜像
    // 如此一目了然的照耀 证明
    // 输入的消息正在队列中，并未丢失。
    else if (!m.ui.composer.queued.empty()) {
        glyph     = "▸";
        verb      = "+" + std::to_string(m.ui.composer.queued.size()) + " queued";
        color     = role_brand_alt;
        breathing = false;
    }
    // 10. 空闲，但自动压缩已因快速重复填充保护而禁用。
    // 持久化退化模式——用户应知道,在没有干预的情况下,
    // 环境会溢出。
    else if (m.s.autocompact_disabled) {
        glyph     = "!";
        verb      = "no-compact";
        color     = status_warn;
        breathing = false;
    }
    // ── 11. 闲置 ───────────────────────────────────────────────
    else {
        glyph     = std::string{phase_glyph(m.s.phase)};
        verb      = "Ready";
        color     = muted;
        breathing = false;
    }

    maya::PhaseChip::Config cfg;
    cfg.glyph        = std::move(glyph);
    cfg.verb         = std::move(verb);
    cfg.color        = color;
    cfg.breathing    = breathing;
    cfg.frame        = m.s.spinner.frame_index();
    cfg.verb_width   = 10;
    cfg.elapsed_secs = elapsed;
    return cfg;
}

} // namespace agentty::ui
