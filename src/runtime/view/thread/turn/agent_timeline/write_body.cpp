// tool_body_preview — `write` 工具 正文.
//
// FileWrite : 行号 -numbered 正文 +
// lines/bytes 脚印。 已解决的文字 渲染 编号 完整 新建(
// N) -file 内容( ưμ㼯A) show_all 用户可以看到写的东西
// 流式传输 窗口为小 末尾 如此成长 正文 从来没有气球 牌高或碎片 已
// 承诺 回滚缓冲区 排行,见 接口边界 说明第1段,第3段。

#include "tool_body_common.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

bool write_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;

    // FAILED 写入: 错误 正文 ,与上文编辑的理由相同。
    if (tc.is_failed() && !card_output(tc).empty()) {
        out.kind = Kind::Failure;
        out.text = failure_body_text(tc);
        out.chrome_color = status_error;
        return true;
    }
    auto content = safe_arg(tc.args, "content");
    const bool streaming_now = !tc.is_terminal();
    out.match_needle = args_match_needle(tc.args);
    if (!content.empty()) {
        out.kind = Kind::FileWrite;
        out.text_color = text_primary;   // 纯输出读作普通文本
        out.show_footer_stats = true;
        // 虽曰. 流式传输 ,显示 SMALL 末尾 预览(
        // 至今为止所写内容的最后几行)—— show_all
        // 假的让 maya 向它飞去 `code_tail` 预算,
        // 契约 "watch it write" 看,
        // 那个复制品 -write 鬼从喂养一个 LARGE 末尾
        // 切片(64行):一旦这些行溢入原生 回滚缓冲区 被冻住了,
        // 但是在解决时卡换到头 -anchored show_all
        // 渲染 由1号线开始,所以承诺的行不再匹配,
        // 卡片是重新 -emitted 下方(两份)。
        // 流式传输 预览 SMALL (一个几乎是上面的切片)
        // maya's 末尾 预算) 可视区域 所以它从来不
        // 承诺 回滚缓冲区 - 仅是解决了 show_all
        // 渲染 达到 回滚缓冲区 画过一次
        out.is_streaming = streaming_now;
        // 已解决的写作 渲染 编号 完整 正文 (show_all 现场
        // 直播中 末尾 和 冰冻 快照 — 字节-identical ,
        // 所以冻结的交接键 两者在同一 hash_id 纯洁的,
        // 缓存 击打( 无执行) -row 值 = 无 duplicated/wiped。
        // 用户看到所写内容。高卡是油漆。 -once 以每桶计,
        // 以千计 -event hash_id (agent_timeline.
        // 来,所以,满 正文 零成本(每笔) 帧 第一次
        // 涂料之后——不需要窗口显示活卡。仅此而已。
        // STREAMING 保持窗口: 正文 还
        // 在成长 气球会飞高 帧.
        out.show_all = !streaming_now;
        if (streaming_now) {
            // 小点 末尾 切片 = O(窗口)每 帧 和有界牌高度。
            // show_all 虚构的 maya 渲染 刚刚的
            // `code_tail` 从此切片中划出线条;
            // 大小 末尾 编号 流式传输 正文 预算(3项,
            // 第18项) -row 终端 ——神谕 -proven
            // 楼层——高的上12层),所以 "watch it write"
            // 窗口使用可用的高度而不按下 头文件 横行过 可视区域
            // 顶部( 承诺) -row-rewrite
            // 接口边界 ; 见 stream_body_budget。
            // 部分脚注总数将是错误的。 正文 中点时
            // -stream (状态栏带有直播率);它随我们解决的
            // 瞬间真实的总和而返回.
            out.show_all = false;
            out.code_tail = std::clamp(stream_body_budget(), 3, 12);
            out.text = tail_window(content, kStreamTailLines);
            // 将沟渠锁定在切片的 TRUE 正在写入文件的
            // 第一道行——没有此选项 末尾 窗口 渲染
            // "1, 2, 3…" 每隔一段时间 帧 文件已深达数百行。
            // 已丢弃 = 内容行在切片上方。
            {
                std::size_t total = 1, kept = 1;
                for (char c : content)  if (c == '\n') ++total;
                for (char c : out.text) if (c == '\n') ++kept;
                if (!content.empty()  && content.back()  == '\n') --total;
                if (!out.text.empty() && out.text.back() == '\n') --kept;
                if (total > kept)
                    out.start_line = static_cast<int>(total - kept) + 1;
            }
            out.show_footer_stats = false;
        } else {
            // 终端 : 数据 完整 正文 活的和冻的一样
            out.text = std::move(content);
        }
    } else if (tc.is_running()) {
        out.kind = Kind::FileWrite;
        out.text_color = text_primary;   // 纯输出读作普通文本
        out.is_streaming = true;
    }
    return true;
}

} // namespace agentty::ui::detail
