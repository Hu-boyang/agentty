// codeblock_update —状态更新函数为Ctrl+G代码-block
// 选择面板("run what the AI just suggested"流程)。
// 打开扫描围栏区块的最新辅助消息； Select 暂停 TUI 并在
// 真实的 终端 上运行所选块 INTERACTIVELY — sudo
// 密码 提示词 工作，实时输出 数据流 — 而 tee
// 捕获每个输出 字节； RunFinished 打开结果卡，
// 用户可以在其中决定如何处理捕获的副本：作为输出 标签 附加到作曲家、
// 将其复制干净或丢弃。
//
// 深思熟虑的范围决定：
//   ·Interactive-first:运行发生在maya的Cmd::suspend下——
//     TUI分解为熟的tty，子进程继承了stdin的REAL
//     终端（sudo从/dev/tty读取密码，无论我们的stdout
//     管道如何，它都保持真实的tty），并且stdout+
//     stderr流过Tee管道：每个字节实时命中用户屏幕，
//     AND落在捕获缓冲区中。 Ctrl+C 使用经典的
//     system() 语义——父进程在持续时间内忽略
//     SIGINT/SIGQUIT，子进程执行默认操作并死亡；
//     agentty 干净地恢复。
//   • 捕获的输出仅达到COMPOSER当用户
//     在结果卡上按 `a` — 它作为输出附件 (标签) 登陆，
//     而不是对话消息：与大粘贴相同的
//     collapse/expand 合同。用户在（如果）希望
//     模型看到它时进行注释并提交。
//   ·仅shell-ish块是可运行的（参见is_shell_language)。对于一个
//     python/js块选择显示吐司；编辑/复制仍然有效。
//   ·在空闲会话上门控打开：消息列表mid-stream
//     正在不断变化并且"latest assistant reply"仍在增长。
//   ·Windows:无fork/tcsetattr —回落至非-interactive
//     捕获的跑步者（与bash工具使用的跑步者相同）。无论如何，
//     sudo 并不是一个 Windows 概念；诚实的退化。

#include "agentty/runtime/app/update/internal.hpp"

#include <algorithm>
#include <utility>


#include <maya/core/overload.hpp>
#include <maya/style/theme.hpp>

#include "agentty/runtime/panel/code_blocks.hpp"
#include "agentty/io/clipboard.hpp"
#include "agentty/runtime/app/cmd_code_block_run.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;
namespace cbp = agentty::code_blocks;

namespace {

// 产生至少一个围栏块的最新辅助消息。
// 消息内的文档顺序被保留（块1=最上面），
// 匹配用户在屏幕上视觉索引回复的方式。
[[nodiscard]] std::vector<CodeBlock> latest_assistant_blocks(const Model& m) {
    for (auto it = m.d.current.messages.rbegin();
         it != m.d.current.messages.rend(); ++it) {
        if (it->role != Role::Assistant) continue;
        if (it->text.empty()) continue;
        auto blocks = cbp::extract_code_blocks(it->text);
        if (!blocks.empty()) return blocks;
        // 继续走：带有散文但没有围栏的助理转弯不应该掩盖
        // HAS可运行块的早期回复（常见形状：回复N具有命令，
        // 回复N+1是简短的"let me know how it goes"
        // 跟随-up）。
    }
    return {};
}

// 阻止来自 still-streaming 回复的可用 RIGHT 当前。
// 实时文本落在 `streaming_text`（已解决的散文仅在回合结束时移动到
// `text`），因此我们在 -turn 中读取实时助理消息携带的内容并使用
// 仅提取已闭合代码块：结束 ``` 尚未到达的块
// 仍然是带类型的并且不能被提供（运行半-line是不安全的）。
// `saw_open`报告块是mid-fence，因此调用者可以说"still streaming"
// 而不是"no blocks"。
[[nodiscard]] std::vector<CodeBlock>
streaming_reply_closed_blocks(const Model& m, bool& saw_open) {
    saw_open = false;
    for (auto it = m.d.current.messages.rbegin();
         it != m.d.current.messages.rend(); ++it) {
        if (it->role != Role::Assistant) continue;
        // 优选直播流式传输 缓冲区;回退到固定文本（在
        // 相位滞后清除时，最终确定可能已经移动了它）。
        std::string_view live = !it->streaming_text.empty()
                                    ? std::string_view{it->streaming_text}
                                    : std::string_view{it->text};
        if (live.empty()) continue;
        bool open_here = false;
        auto blocks = cbp::extract_closed_code_blocks(live, &open_here);
        saw_open = saw_open || open_here;
        if (!blocks.empty()) return blocks;
    }
    return {};
}
// 块RUNNER(真实tty上的fork/exec，或者Windows子进程回
// 退)是副作用并且存在于cmd_code_block_run.cpp中。该文件仅决定
// 运行哪个块并返回 cmd::run_code_block(...)。

} // 命名空间结束。

Cmd codeblock_update(Model& m, msg::CodeBlockMsg cm) {
    return std::visit(overload{
        [&](OpenCodeBlocks) -> Cmd {
            // Mid-stream是允许的：网络数据流在后台工作线程
            // 上运行（StreamDelta回发到UI 线程），
            // 因此暂停TUI来运行块会导致NOT暂停回复——当你返回时，
            // 增量不断到达并着陆。虽然 流式传输 我们提供
            // blocks whose ``` fence has ALREADY closed in the live reply; a
            // 仍为带类型的的块被保留，直到其栅栏落地。
            std::vector<CodeBlock> blocks;
            if (m.s.active()) {
                bool saw_open = false;
                blocks = streaming_reply_closed_blocks(m, saw_open);
                if (blocks.empty()) {
                    auto cmd = set_status_toast(m,
                        saw_open
                          ? "a code block is still streaming \xe2\x80\x94 try again in a moment"
                          : "no complete code blocks yet");
                    return cmd;
                }
            } else {
                blocks = latest_assistant_blocks(m);
                if (blocks.empty()) {
                    auto cmd = set_status_toast(m,
                        "no code blocks in the last reply");
                    return cmd;
                }
            }
            m.ui.panel.descend(pn::CodeBlocks{{std::move(blocks), 0}});
            m.ui.code_blocks_scroll.y = 0;
            return Cmd::none();
        },
        [&](CloseCodeBlocks) -> Cmd {
            // 结果 覆盖面板 打开 → 两种选择都失败，我们展开到父项；
            // 单独的列表 → 相同。 ascend() 恢复 选择
            // 面板 打开的任何内容（调色板 → ^K 状态完好）。
            ascend(m);
            return Cmd::none();
        },
        [&](CodeBlocksMove& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::CodeBlocks>()) {
                int sz = static_cast<int>(o->blocks.size());
                if (sz <= 0) return Cmd::none();
                o->index = ui::step(o->index, e.delta, sz);
                return Cmd::none();
            }
            if (m.ui.panel.get<pn::CodeBlockResult>()) {
                // 读取-only结果卡：移动增量直接滚动捕获可视区域。
                // max_y 是 选择面板 小部件绘制的
                // -written-back；夹紧它（第一次绘制之前
                // 为 0 — 无害，写回落在下一个 帧）。
                auto& sc = m.ui.code_blocks_scroll;
                sc.y = std::clamp(sc.y + e.delta, 0, std::max(0, sc.max_y));
            }
            return Cmd::none();
        },
        [&](CodeBlocksSelect& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::CodeBlocks>();
            if (!o) return Cmd::none();
            const int idx = e.index.value_or(o->index);
            if (idx < 0 || idx >= static_cast<int>(o->blocks.size()))
                return Cmd::none();
            CodeBlock block = o->blocks[static_cast<std::size_t>(idx)];
            const cbp::BlockShell shell = cbp::shell_for_language(block.language);
            if (shell == cbp::BlockShell::None) {
                // 无法在这个平台上运行——向那些行动前进 DO
                // 说得通 选择面板 保持开放,这样 `e` /
                // `y` 是一个键盘打掉。
                const std::string tag = block.language.empty()
                    ? std::string{"this"} : "'" + block.language + "'";
                auto cmd = set_status_toast(m,
                    tag + " block isn't runnable here — "
                    "press e to edit or y to copy");
                return cmd;
            }
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            return cmd::run_code_block(std::move(block.body), shell);
        },
        [&](CodeBlocksEdit) -> Cmd {
            auto* o = m.ui.panel.get<pn::CodeBlocks>();
            if (!o) return Cmd::none();
            const int idx = o->index;
            if (idx < 0 || idx >= static_cast<int>(o->blocks.size()))
                return Cmd::none();
            std::string body = o->blocks[static_cast<std::size_t>(idx)].body;
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            // 光标上的切片,而不是替换—— 与 @
            // file / # 相同的常规 符号 标签。常见的情况是,
            // 一个空的作曲家,这里 IS a 替换。
            m.ui.composer.text.insert(
                static_cast<std::size_t>(m.ui.composer.cursor), body);
            m.ui.composer.cursor += static_cast<int>(body.size());
            return Cmd::none();
        },
        [&](CodeBlocksCopy) -> Cmd {
            auto* o = m.ui.panel.get<pn::CodeBlocks>();
            if (!o) return Cmd::none();
            const int idx = o->index;
            if (idx < 0 || idx >= static_cast<int>(o->blocks.size()))
                return Cmd::none();
            std::string body = o->blocks[static_cast<std::size_t>(idx)].body;
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            // Write 通过本地工具( Name pbcopy/wl-copy/xclip
            // 同步进行 和 发射 OSC 52 Cmd. OSC 52是选择 -in
            // 输入 Terminal.app / iTerm2 默默地表示没有
            // -ops 在那里,当地写的东西是 当地复制品。 OSC 五十二号还带着它
            // 穿过一个 SSH/tmux 跳转到远程剪贴板。
            (void)write_clipboard_text(body);
            auto toast = set_status_toast(m, "copied clean block to clipboard");
            return Cmd::batch(
                        cmd::write_clipboard(std::move(body)),
                        std::move(toast));
        },
        [&](CodeBlockRunFinished& e) -> Cmd {
            // 别自動 -stage - 打开门 RESULT。
            // 用户已经看到输出在真实中运行 终端 ;
            // 这是决定节拍: 将抓取的副本附加到
            // 作曲家身上, 复制干净, 或者丢弃。
            // 作曲者只收到用户明确要求的输出。
            // descend() ,而不是任务:运行完成 ASYNC
            // ——用户现在可能还有另一个面板打开,结果卡不得破坏.
            // Esc 从卡片上恢复它 逐字恢复它。
            m.ui.panel.descend(pn::CodeBlockResult{{
                std::move(e.command), std::move(e.output),
                e.exit_code, e.timed_out}});
            m.ui.code_blocks_scroll.y = 0;
            return Cmd::none();
        },
        [&](CodeBlockResultAttach) -> Cmd {
            auto* r = m.ui.panel.get<pn::CodeBlockResult>();
            if (!r) return Cmd::none();
            // 归并 将捕获的输出作为输出附件输入作曲家中—— 相同
            // 崩溃 -to-chip / 扩展 -on-submit
            // 大型的贴面用机械。无论日志有多大,作曲家都显示一种药丸(
            // "Output: sudo mkfs… · 1240 lines · 48 KB"（输出摘要示例：命令名称、1240 行、48 KB。）
            // ) 整个 正文 仅限 按需加载 自动调试
            // 传输 当用户实际提交时。
            std::string out = std::move(r->output);
            std::size_t lines = out.empty() ? 0 : 1;
            for (char c : out) if (c == '\n') ++lines;

            Attachment att;
            att.kind       = Attachment::Kind::Output;
            att.name       = std::move(r->command);   // 标签 标题
            att.line_count = lines;
            att.byte_count = out.size();
            att.body.set_bytes(std::move(out));
            std::size_t idx = m.ui.composer.attachments.size();
            m.ui.composer.attachments.push_back(std::move(att));
            auto placeholder = attachment::make_placeholder(idx);
            m.ui.composer.text.insert(
                static_cast<std::size_t>(m.ui.composer.cursor), placeholder);
            m.ui.composer.cursor += static_cast<int>(placeholder.size());
            m.ui.composer.expanded = true;
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            auto toast = set_status_toast(m, "output attached to composer");
            return toast;
        },
        [&](CodeBlockResultCopy) -> Cmd {
            auto* r = m.ui.panel.get<pn::CodeBlockResult>();
            if (!r) return Cmd::none();
            std::string body = std::move(r->output);
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            (void)write_clipboard_text(body);   // 原生的 pbcopy/wl-copy/xclip
            auto toast = set_status_toast(m, "output copied to clipboard");
            return Cmd::batch(
                        cmd::write_clipboard(std::move(body)),
                        std::move(toast));
        },
        [&](CodeBlockResultDiscard) -> Cmd {
            m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
            return Cmd::none();
        },
    }, cm);
}

} // namespace agentty::app::detail
