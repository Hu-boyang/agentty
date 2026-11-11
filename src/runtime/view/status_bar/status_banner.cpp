#include "agentty/runtime/view/status_bar/status_banner.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include <chrono>
#include <string_view>

namespace agentty::ui {

namespace {

// 切换到瞬态状态信息。将烤面包带分成三个含义,
// 而不是旧的 二进制程序 error/non-error:
//   错误 ~ 红色 ( "error: …", 终端 失败) 警告 ~
//   黄色( "retrying …", "awaiting …", "transient — …"
//   信息 − 阶段(其他所有: "context compacted", "cancelled", …)
//
// 旧代码已路由 全部 无 -error 消息通过 流式传输
// 相位颜色( R) bright_cyan 与
// bright_white 文本 – 大部分主题上无法读取,
// 它没有告诉用户严重性. 重试 特别是看起来和平静完全一样
// "compacted" 尽管这是用户可能想做的事,
maya::StatusBanner::Kind classify(std::string_view status) noexcept {
    if (status.rfind("error:", 0) == 0) return maya::StatusBanner::Kind::Error;

    constexpr std::string_view warn_prefixes[] = {
        "retrying",          // "retrying (upstream cut off)…"
        "transient",         // "transient — retrying in 21s (attempt 5/6)…"
        "rate limit",        // "rate limit — retrying …"
        "awaiting",
    };
    for (auto p : warn_prefixes) {
        if (status.size() >= p.size()
         && std::string_view(status.data(), p.size()) == p) {
            return maya::StatusBanner::Kind::Warn;
        }
    }
    return maya::StatusBanner::Kind::Info;
}

} // 命名空间结束。

// 敬酒( 敬酒) 通知 骑在车上 StatusBanner 线条,
// 哪个 maya::StatusBar 现在用来接管 activity_row
// 全部槽( 完整) -width ,口音背景的明亮前景) `text`
// 非为 -empty。快捷键行已退出;快捷键移动到欢迎屏幕。
//
// 我们浮出水面 `m.s.status` 只要是两样都不行 -empty 和 还在里面
// TTL 窗口( 窗口) `status_active`). "ready" 是
// 那个 calm/idle 伪 -status 不应该拖着烤面包乐队, 所以它被过滤了。
// `kind` 信件文本的键切换到此部件选择右侧的锡( 红色为错误,
// 黄色为 retry/awaiting ,阶段 -color 其它信息信息 -
// "context compacted", "cancelled", …).
maya::StatusBanner::Config status_banner_config(const Model& m) {
    maya::StatusBanner::Config cfg;
    if (m.s.status.empty() || m.s.status == "ready") return cfg;
    if (!m.s.status_active(maya::anim_now())) return cfg;
    cfg.text     = m.s.status;
    cfg.kind     = classify(m.s.status);
    cfg.is_error = (cfg.kind == maya::StatusBanner::Kind::Error);
    return cfg;
}

} // namespace agentty::ui
