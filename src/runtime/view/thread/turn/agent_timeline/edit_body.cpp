// tool_body_preview — `edit` 工具 正文.
//
// 以我们掌握的数据来选择的两条渲染路径:
//   (a) FENCE-PARSE 用于 终端-state 编辑、 拖动 diff 载荷 退出( O)
//       从 edit 工具写入结果的 ```diff … ``` 块中
//       文本。通过Kind::GitDiff的路由,用于互页 −/+
//       彩色+线锚。
//   (b) ARGS-ECHO — 流式传输 / 预 -run ,则尚未存在 diff
//       : 合成 a Kind::EditDiff 发件人 `tc.args.edits[*]`。
//       文件的 `before` 内容无法从视图中达到,所以这是我们
//       所能提供的最佳预览,直到执行土地.

#include "tool_body_common.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

bool edit_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    const bool streaming_now = !tc.is_terminal();

    // `replace` / `rewrite_structural`
    // 携带他们搜索的字面;在diff行内点亮,这样变化就容易被发现.
    out.match_needle = args_match_needle(tc.args);

    // FAILED 编辑: 将错误文本显示为 正文。 掉进树格 -echo
    // EditDiff 使"尝试" 差异块 并吞噬了实际失败的原因(第1条)
    // "old_text not found", "file changed since read"（old_text 未找到，或文件在读取后已变化。）
    // )——用户看到一个红色的QQ,没有解释,模型的自我.
    // -correction 提示只是出现在 传输 永远不要在屏幕上。
    if (tc.is_failed() && !card_output(tc).empty()) {
        out.kind = Kind::Failure;
        out.text = failure_body_text(tc);
        out.chrome_color = status_error;
        return true;
    }
    if (tc.is_terminal() && !tc.is_failed()) {
        // 从输出中的 ```diff … ``` 围栏提取差异载荷。
        // 工具的输出。返回到 EditDiff-from-args 如果
        // 没有找到围栏(老人) 线程 ,自定义工具包装器).
        const std::string_view body = card_output(tc);
        constexpr std::string_view kOpen = "```diff\n";
        constexpr std::string_view kClose = "\n```";
        auto a = body.find(kOpen);
        if (a != std::string::npos) {
            a += kOpen.size();
            auto b = body.find(kClose, a);
            if (b == std::string::npos) b = body.size();
            out.kind = Kind::GitDiff;
            // 已和解编辑 渲染 完整 diff ( show_all ——按
            // -event hash_id 存储单元-cache 高牌是画
            // -once 布莱特,那么满 正文 零成本(每笔) 帧 初结后.
            // BUT 盖状大病理性diffs:一个巨大的 deletion/rewrite
            // 就会丢掉几百条红线 和 (当编辑工具将其输出切换到模型的
            // 字符预算时)拖动模型 -facing
            // "[... N chars elided — refine your request ...]"（省略了 N 个字符，请缩小请求范围。）
            // 标记在用户屏幕上。既不属于审查卡。
            std::string diff{body.substr(a, b - a)};
            // 脱下 分派器 模式 -facing 产出 -budget
            // 如果编辑输出在 传输 ——它涉及模型(.
            // "refine your request" ),从不审查diff.
            if (auto m = diff.find("[... ");
                m != std::string::npos &&
                diff.find("chars elided", m) != std::string::npos) {
                // 从标记到结尾( 总是一个跟踪头剪辑进行编辑)。
                // 缩写留下的一条跟踪空白线。
                while (m > 0 && (diff[m - 1] == '\n' || diff[m - 1] == ' '))
                    --m;
                diff.resize(m);
            }
            out.text = std::move(diff);
            // 如果diff还很高,就回到 maya's head/tail
            // 消除( 4) show_all 所以卡片显示一个有
            // 正版的有界窗口 "⋯ N more" 标记而不是无
            // 约束的墙。门槛是宽大的:普通编辑(少数) 差异块)
            // 渲染 完全; 只有真正的大重写才被窗口打开。
            constexpr int kMaxDiffLines = 200;
            const int nlines = 1 + static_cast<int>(std::count(
                out.text.begin(), out.text.end(), '\n'));
            const bool oversized = nlines > kMaxDiffLines;
            out.show_all   = !oversized;
            out.tail_only  = false;
            if (oversized) {
                // 头 -heavy 窗口: diff( QQ) 的顶部 差异
                // 块 头文件 +首条更改的线路),携带的审核信号最多.
                out.code_head = kMaxDiffLines;
                out.code_tail = 0;
            }
            out.text_color = text_tertiary;
            return true;
        }
        // 失去围栏——掉到栅栏上 -based EditDiff 下图。
    }

    if (tc.args.is_object()) {
        // 镜像 Write '纪律:编辑时 流式传输 ,则 差异块 增长线条
        // -by-line (每个) `edits[i].new_text`
        // 三角洲中点到 -token ),它会气球 牌高在每 帧 并
        // 拆散任何已推向原生的行 回滚缓冲区。将 流式传输 预览到
        // 末尾 窗口( 窗口) show_all 虚实 虚实 maya's
        // tail_only 渲染器 仅显示每条N线的最后一行 差异块 边);
        // 扩展为 show_all 工具一旦解决,
        // 正文 最终决定。
        if (auto it = tc.args.find("edits");
            it != tc.args.end() && it->is_array() && !it->empty())
        {
            out.kind = Kind::EditDiff;
            // 编辑完毕后立即全速完成 终端 - 现场直播中 末尾 不仅冰冻的 快照。
            // 虽然 STREAMING 保持阴影 per-side/per-hunk
            // 预览( E) 差异块 增长线条 -by-line 气球
            // 高度会提高 帧 )),一旦定居. 差异块 最终,
            // 所以扩展 立即避免 "stub then sudden expand"
            // 时间滞后,匹配什么 freeze_range 将
            // 建造(无缝的接发),
            out.is_streaming = streaming_now;
            out.hunks.reserve(it->size());
            auto nested_string = [](const nlohmann::json& obj,
                                    const char* preferred,
                                    const char* legacy) -> std::string {
                for (const char* key : {preferred, legacy}) {
                    auto field = obj.find(key);
                    if (field != obj.end() && field->is_string())
                        return field->get<std::string>();
                }
                return {};
            };
            for (const auto& e : *it) {
                if (!e.is_object()) continue;
                auto ot = nested_string(e, "old_text", "old_string");
                auto nt = nested_string(e, "new_text", "new_string");
                if (ot.empty() && nt.empty()) continue;
                out.hunks.push_back({std::move(ot), std::move(nt)});
            }
            // WHILE STREAMING: maya's
            // edit_diff_streaming 渲染 一根
            // 钉子 标签 + a ROW-LEVEL 现场直播 末尾 上一个
            // 2x 的 edit_tail_per_side 横跨线条 全部 差异块。
            // 每餐一次,每餐一次 差异块 (无主机) -side 窗口)是保持
            // 末尾 窗口 完整 横跨 差异块 边框: 新鲜 差异块 弹出到
            // 中间的参数阵列 -token 近处 -zero 内容, 以及最新旧的
            // -hunk-only 窗口崩溃 正文 赤裸裸的, 标签 上面的这个
            // 帧 ——卡明显向上缩,然后重新 -grew 以横跨的字串
            // -hunk 线条 末尾 高度是单调的 -decreasing (内容
            // 只增长),所以卡片从来没有缩小中点 -stream 编辑 接口边界
            // 不变量 仍然在它的高度下
            //
            // 标记 流式传输 统计 标签 与 序号 会 议 日 程
            // 和 议 程 差异块 正在着陆( 4) :
            // "edit 3 · −6 / +6" 所以,我们 末尾 窗口
            // 仍然表示已应用的编辑次数——零加行,且勾选数字留在 可视区域 由
            // 下文预算供资。
            if (streaming_now)
                out.stream_hunk_no = static_cast<int>(it->size());
            // ...把现场卡片放进里面 STREAMING BODY BUDGET.
            // 预算是 加载-bearing 而不是化妆品: 事件 HEADER
            // 排在高处 正文 ,和它下面的一切( 正文 +页脚+边框+
            // composer/status 铬 + 15行)必须装在
            // term_h 所以,我们 头文件 留下来 内部 编号 可视
            // 区域 牌的时候 数据流. 正文 行数= 1 个统计数据
            // 标签 + 2×per_side 末尾 预算:18行 -row
            // 终端 说到这里 标签 + 2 行( 图形显示为安全);
            // 高一些 终端 编号 末尾 扩展至12行——用户观看最新行 数据
            // 流 过去的没有 头文件 过 接口边界.
            if (streaming_now) {
                const int per_side = std::clamp(
                    (stream_body_budget() - 1) / 2, 1, 6);
                out.edit_head_per_side = 0;
                out.edit_tail_per_side = per_side;
            }
            // 已解决 差异块 渲染 输入 完整 (show_all ) 中,
            // 两者 现场直播 末尾 和冻伤 快照 — 字节-identical
            // 冻结的交接是纯净的 缓存 仅限击出 STREAMING 保持
            // 平滑( 以 差异块 增长线条 -by-line 气球高度会提高 帧).
            // 高高的定居卡是油漆 -once 以每桶计,以千计 -event
            // hash_id ,所以一个满 正文 免费的每张 帧 初涂
            // 后结.
            out.show_all = !streaming_now;
            return true;
        }
        auto ot = safe_arg(tc.args, "old_text");
        if (ot.empty()) ot = safe_arg(tc.args, "old_string");
        auto nt = safe_arg(tc.args, "new_text");
        if (nt.empty()) nt = safe_arg(tc.args, "new_string");
        if (!ot.empty() || !nt.empty()) {
            out.kind = Kind::EditDiff;
            // 满 差异块 活体和冻体(已解决); 流式传输
            // 保持脚步,活在冰冷中 正文.
            out.show_all     = !streaming_now;
            out.is_streaming = streaming_now;
            // 一样 接口边界 预算作为编辑 -array 分支 : 流式传输 正文 =
            // 标签 + 2×per_side 划行必须划入 term_h 所以,
            // 我们 事件 头文件 永远不能跨越 可视区域 顶部。
            if (streaming_now) {
                const int per_side = std::clamp(
                    (stream_body_budget() - 1) / 2, 1, 6);
                out.edit_head_per_side = 0;
                out.edit_tail_per_side = per_side;
            }
            out.hunks.push_back({std::move(ot), std::move(nt)});
        }
    }
    return true;
}

} // namespace agentty::ui::detail
