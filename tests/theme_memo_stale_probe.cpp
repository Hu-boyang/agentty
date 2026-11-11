// theme_memo_stale_probe —— 主题切换实际重彩 a
// SETTLED 工具面板吗 ?
//
// agent_timeline.cpp 手持三盘 thread_local 元素备忘录 :
//   g_body_cache 键: 工具 ID | 状态 | 输出大小 | grep_sig g_panel_cache
//   键:工具 ids/status/sizes/render_keys | tool_output 预览
//   g_panel_render_memo 键 : msg_id | render_key
//
// 这些密钥中没有一个名称是 THEME ,而且主题改变时,
// 没有什么能澄清它们( restyle_sealed_turns 清除
// view_cache + 将冷冻分类账补水, 但绝不是这些。
// 如果缓存面板元素烤出任何 RESOLVED 颜色,在构建时,
// 记忆缓存 将一直服务于旧调色板, 时间与工具一样长 调用 内容不变——
// 对于一个已解决的工具来说 调用 永远
//
// 探测器 渲染 两种非常不同的方案下同一固定的工具面板,
// 并比较所排放的 SGR 字节。温暖对冷告诉我们,
// 区别(如果有的话)来自主题还是来自 记忆缓存.

#include <cstdio>
#include <string>
#include <vector>

#include <maya/maya.hpp>
#include <maya/style/schemes.hpp>
#include <maya/style/theme.hpp>

#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/agent_timeline.hpp"

using namespace agentty;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

// 一项已解决( 终端 () 工具 调用 ——记忆的形状.
std::vector<ToolUse> settled_batch() {
    ToolUse tc;
    tc.id     = ToolCallId{"toolu_probe_1"};
    tc.name   = ToolName{"edit"};
    tc.args   = nlohmann::json{{"path", "src/main.cpp"},
                               {"display_description", "fix the thing"}};
    tc.status = ToolUse::Done{.output = "applied 1 edit to src/main.cpp"};
    return {tc};
}

std::string render_panel(const maya::Theme& t, std::string_view msg_id,
                         std::uint64_t render_key) {
    ui_prefs::publish_theme(t);
    auto batch = settled_batch();
    maya::Element el = ui::agent_timeline_element_memoized(
        msg_id, render_key, batch, /*spinner_frame=*/0,
        maya::Color::slot(maya::ThemeSlot::Accent));
    return maya::render_to_string_ansi(el, 100);
}

} // 命名空间结束。

int main() {
    // 这些比较画 字节 跨调色板; a NO_COLOR 电话铃声的内脏
    // CI ,代理贝壳,一个控制台,没有 VT )将使每个方案都相同。
    // MAYA_COLOR 谓 maya 已经记录的定型 -test 覆盖( 见
    // detect_color_level).
    ::unsetenv("NO_COLOR");
    ::setenv("MAYA_COLOR", "truecolor", 1);
    std::printf("=== theme_memo_stale_probe ===\n\n");

    const maya::Theme* dracula = ui_prefs::find_scheme("Dracula");
    const maya::Theme* gruv    = ui_prefs::find_scheme("Gruvbox Light");
    if (!dracula || !gruv) {
        std::printf("could not find both schemes — abort\n");
        return 2;
    }

    // A. 冷缓存：每次使用不同 msg_id，因此不可能命中缓存。
    // 这就是事实:第SHOULD小组在每个主题下
    // 的样子。
    const std::string cold_a = render_panel(*dracula, "cold_msg_a", 1);
    const std::string cold_b = render_panel(*gruv,    "cold_msg_b", 2);

    std::printf("cold (memo bypassed via distinct keys)\n");
    check(cold_a != cold_b,
          "the two schemes DO paint a settled panel differently");
    std::printf("    dracula bytes=%zu  gruvbox bytes=%zu\n\n",
                cold_a.size(), cold_b.size());

    // B. 热缓存：切换主题时保持相同 msg_id 和 render_key。
    // 这是真实的主题切换对一个已经-settled工具面板所做的:
    // 内容没有改变,所以密钥是完全相同的.
    const std::string warm_a = render_panel(*dracula, "warm_msg", 7);
    const std::string warm_b = render_panel(*gruv,    "warm_msg", 7);

    std::printf("warm (same msg_id + render_key, theme swapped between)\n");
    check(warm_a != warm_b,
          "a theme switch recolours the SETTLED panel");

    if (warm_a == warm_b) {
        std::printf("\n  >>> STALE: the memo served the Dracula-built\n");
        std::printf("      Element after the theme became Gruvbox Light.\n");
        std::printf("      Byte-identical output across a real palette change.\n");
    }

    // C. 检查过期副本是否仍采用旧主题绘制。
    if (warm_b == warm_a && cold_a != cold_b) {
        std::printf("\n  the warm Gruvbox render equals the Dracula paint: %s\n",
                    warm_b == warm_a ? "yes" : "no");
    }

    std::printf("\n");
    if (failures == 0) {
        std::printf("PASS — theme switches reach settled tool panels\n");
        return 0;
    }
    std::printf("FAILED: %d check(s) — stale palette reproduced\n", failures);
    return 1;
}
