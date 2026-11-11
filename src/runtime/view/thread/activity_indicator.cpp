#include "agentty/runtime/view/thread/activity_indicator.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/model_name.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/activity_indicator_words.hpp"

namespace agentty::ui {

namespace {

// 下调 -case 状态 -bar PhaseChip 想要标题
// -Case ("Streaming" 因为它住在一个环绕着
// 其他标题的铬带里 -Cased 标签 ; 输入 -thread
// 活动行需要一个更软的打印记录,因为它直接坐在助手转弯处 正文.
// "streaming\u2026" 读作环境 "still working" 铬;
// "Streaming\u2026" 读作一节
// 头文件.
//
// 我们很特别 -case "Streaming" 切换到最友好的
// 动词 "thinking" ——在 SSE 数据流 模型一般仍在
// 形成答复(工具决定、传言), "thinking" 匹配用户的
// 精神模型比协议更好 -level 单词 "streaming".
std::string soft_verb(std::string_view raw) {
    if (raw == "Streaming") return "thinking";
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // 命名空间结束。

const std::vector<std::string_view>& activity_indicator_words() {
    static const std::vector<std::string_view> pool = [] {
        std::vector<std::string_view> v;
        v.reserve(indicator_words::kPool.size());
        for (auto sv : indicator_words::kPool) v.push_back(sv);
        return v;
    }();
    return pool;
}

std::optional<maya::ActivityIndicator::Config>
activity_indicator_config(const Model& m) {
    if (!m.s.active()) return std::nullopt;
    if (m.d.current.messages.empty()) return std::nullopt;
    const auto& last = m.d.current.messages.back();
    if (last.role != Role::Assistant) return std::nullopt;
    bool tl_visible =
        !last.tool_calls.empty()
        && std::any_of(last.tool_calls.begin(), last.tool_calls.end(),
                       [](const auto& tc){ return !tc.is_terminal(); });
    if (tl_visible) return std::nullopt;

    // 边框颜色来自模型 -name SSOT 而不是本地家庭的分支。
    // 这个网站曾经载着它的 自身 is_opus/is_sonnet/is_haiku 链条,
    // 它与其它表面都不同: `accent` (方 红衣) 转弯 头文件 和
    // 徽章使用情况 bright_magenta 和海库是 `success` —
    // GREEN 状态 -ok 哼,这恰恰是碰撞
    // proofs::no_family_uses_a_status_hue ()现在禁止: 编译 时间,
    // 一个解码,一个表,看 domain/model_name.hpp.
    const maya::Color edge = model_name::decode(m.d.model_id.value).color;
    maya::ActivityIndicator::Config cfg;
    cfg.edge_color    = edge;
    cfg.spinner_glyph = std::string{m.s.spinner.current_frame()};
    cfg.label         = soft_verb(phase_verb(m.s.phase));
    cfg.words         = activity_indicator_words();

    // 活动阶段开始后的秒数——状态来源相同 -bar
    // PhaseChip 读取。显示为哑巴 "· 3.4s"
    // 末尾 因此用户会意识到模型工作了多长时间而
    // 不需浏览状态栏。当阶段刚刚开始( < 1s)
    // 时跳过以避免 "0.0s" 闪烁在第一 帧.
    if (const auto* a = active_ctx(m.s.phase);
        a && a->started.time_since_epoch().count() != 0) {
        auto now = maya::anim_now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - a->started).count();
        if (ms >= 1000) {
            float secs = static_cast<float>(ms) / 1000.0f;
            // 调理 format_elapsed_5 领先的垫子——这里我们想要一个紧凑的变量
            // -width 字符串( 键盘) "3.4s", "42s", "2m 10s").
            std::string e = format_elapsed_5(secs);
            std::size_t i = e.find_first_not_of(' ');
            if (i != std::string::npos) e.erase(0, i);
            cfg.detail = std::move(e);
        }
    }
    return cfg;
}

} // namespace agentty::ui
