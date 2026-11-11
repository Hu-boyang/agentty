// subagent_report_test — 锁 编号 `task` 工具报告 -extraction 合同。
//
// 回归目标:一个直接探索其转弯预算的子代理(每个完成都是工具) 调用
// 从来没有人写过信条) 曾经有它的报告 SALVAGED 以天真反转
// -walk 回到了这里 首次 无 -empty 辅助文本 - i.e
// 轮到1的叙述( "I'll start by mapping...").
// 父母随后看到一个可信的-looking one-liner,
// 完成了第"hit its turn cap without a report"号子代理,
// 并自己完成了工作。 四个平行的探险家都带着他们的开头句而不是答案回来了。
//
// 这个测试驱动 真实 AgenttySubagentRunner (通过 注册表 `task`
// 工具),带有 SCRIPTED 数据流 接口边界 并声称:
//
//   A. 工具 -only-to-the-cap 报告是 "hit its turn budget" 横幅,
//      不 转动一号,和 tool_result 标记 is_error.
//   B. 在结尾写作 \ 报告正是最后的 流言,干净。
//   C. 结 围 -up 猛烈的火力在盖子前燃烧 如此的模型
//      执行 回应它, 发布真实的报告 上转。
//   D. a 运行时 数据流 使用空请求认证( E) 工作 ChatGPT/local 提供方)。
//   E. 静静地空置完成被重新检验,而不是被接受为成功。
//   F. 取消 中断提供者 重试 退避 立刻开始
//   G. 永久性错误没有经过反复审查。
//   H. 互页工具 -call 三角洲由 ToolCallId.
//   I. MaxTokens 部分文本显示为 incomplete/error.
//   J. 取消 到达活动提供者请求符.

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <unistd.h>   // 鸡鸡,快来

namespace fs = std::filesystem;

#include <nlohmann/json.hpp>

#include "agentty/provider/selection.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/runtime/msg.hpp"
#include "agtest_cap.hpp"

using nlohmann::json;
using namespace agentty;

namespace {

int g_checks = 0;
int g_fails  = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) std::printf("ok:   %s\n", what.c_str());
    else  { std::printf("FAIL: %s\n", what.c_str()); ++g_fails; }
}

bool has(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

// 开车 `task` 工具,通过生产 注册表 ,与 状态更新函数 已经。
// `is_error` 设置于 分派 浮出水面a ToolError (跑
// 者们) is_error 路径重置 -wraps 以示意外之过桥).
struct TaskOut { std::string text; bool is_error = false; };

TaskOut run_task(const std::string& prompt, const tools::CallContext& ctx = {}) {
    const auto td = tool::DynamicDispatch::resolve("task");
    if (!td) return {"[task tool missing from registry]", true};
    json args; args["prompt"] = prompt;
    // 生产 分派 ,作为 cmd_factory 是否为: execute_with
    // 邮票 调用 其下潜物导体所需的上下文中的能力 provider/auth
    // 回调 (CallContext::capability()).
    auto r = tool::DynamicDispatch::execute_with(agtest::cap(), td, "task", args, ctx);
    if (!r) return {r.error().detail, true};
    return {r->text, false};
}

// 脚本 数据流 接口边界. `script(turn, req)`
// 决定什么 此 完成释放; 它驱动循环 确定没有网络。
using Script = std::function<void(int turn, const provider::Request&,
                                  const provider::EventSink&)>;

void install_scripted_stream(Script script, bool with_auth = true) {
    auto counter = std::make_shared<std::atomic<int>>(0);
    tools::subagent::Config cfg;
    if (with_auth) cfg.auth = auth::ApiKeyHeader{"sk-test-not-real"};
    cfg.model = "test-model";
    cfg.stream = [counter, script](const maya::unlocked&, provider::Request req,
                                   provider::EventSink sink) {
        int turn = counter->fetch_add(1);
        provider::StreamResult result;
        provider::EventSink recording = [&](Msg m) {
            if (auto* sm = std::get_if<msg::StreamMsg>(&m)) {
                std::visit([&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::same_as<T, StreamFinished>) {
                        result.end = provider::StreamEnd::AlreadyTerminated;
                        result.stop = e.stop_reason;
                    } else if constexpr (std::same_as<T, StreamError>) {
                        result.end = provider::StreamEnd::TransportError;
                        result.error = e.message;
                        result.retry_after = e.retry_after;
                        result.http_status = e.http_status;
                        result.non_replayable = e.non_replayable;
                    }
                }, *sm);
            }
            sink(std::move(m));
        };
        script(turn, req, recording);
        return result;
    };
    tools::subagent::install(std::move(cfg));
}

// 方便发射器,以反射 传输层 → Msg 绘制了跑步者读数。
void emit_text(const provider::EventSink& sink, const std::string& t) {
    sink(Msg{msg::StreamMsg{StreamTextDelta{t}}});
}
void emit_tool_call(const provider::EventSink& sink, const std::string& id,
                    const std::string& name, const json& jargs) {
    sink(Msg{msg::StreamMsg{StreamToolUseStart{ToolCallId{id}, ToolName{name}}}});
    sink(Msg{msg::StreamMsg{StreamToolUseDelta{ToolCallId{id}, jargs.dump()}}});
    sink(Msg{msg::StreamMsg{StreamToolUseEnd{ToolCallId{id}}}});
}
void emit_finish(const provider::EventSink& sink,
                 StopReason r = StopReason::EndTurn) {
    sink(Msg{msg::StreamMsg{StreamFinished{r}}});
}
void emit_error(const provider::EventSink& sink, std::string message,
                bool non_replayable = false) {
    sink(Msg{msg::StreamMsg{StreamError{
        .message = std::move(message),
        .non_replayable = non_replayable,
    }}});
}

// 执行者是否将完成的 -up 块递给过它?
// 块是最后一个用户消息; 用它特有的文字来检测它。
bool req_has_nudge(const provider::Request& req) {
    for (const auto& m : req.messages)
        if (m.role == Role::User && has(m.text, "almost out of turn budget"))
            return true;
    return false;
}

} // 命名空间结束。

int main() {
    // 从一个 空 临时目录。 此测试驱动 真实 子代理导师,
    // 执行 真实 grep 工具( 图像工具) 调用 每一个都产出
    // riggrep 与工具的 工作区 root (进程 cwd)。
    // 左转 源码树 在所有情景中,每几十个grep 扫描整个14 GB
    // 工作状态 源码树 ; 单声道为~60秒, 下 `ctest -j`
    // 过了60秒就爆炸了 超时 看上去像是绞刑 血滴是故意的
    // -match 探测器( "zzz_no_match_expected" ),
    // 所以空的cwd测试同一报告 -extraction 合同,
    // 而每个参数以微秒返回——快速 和 离套房的运行地点不远
    {
        std::error_code ec;
        const auto dir = fs::temp_directory_path(ec)
            / ("agentty-subagent-test-" + std::to_string(::getpid()));
        fs::create_directories(dir, ec);
        if (!ec) fs::current_path(dir, ec);
    }
    // 选择非 -Anthropic 提供者:仅运行者 调用 磁盘 -reading fresh_auth_header
    // ( ) 当 active() . . 有点 . Anthropic。该词 OpenAI-family
    // 类型(覆盖) Ollama/OpenAI-compat ) 使用量 cfg.auth 逐字,
    // 还有我们的剧本 数据流 完全无视它。
    {
        provider::Selection sel;
        sel.kind = provider::Kind::OpenAI;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // ── A. Tool-only 写不出来 ─────────────────────
    // 每一次完成都发出 一个 读取 调用 (无伤,读作 -only
    // ) 带有空文本,所以 ran_a_tool 继续真实,
    // 循环会运行到角色预算。回合-1完成会额外释放一个描述线—— 准确
    // 诱饵旧反面 -walk 本来会回来的
    {
        install_scripted_stream([](int turn, const provider::Request&,
                                   const provider::EventSink& sink) {
            if (turn == 0)
                emit_text(sink, "I'll start by mapping the codebase structure.");
            // 模式必须 VARY 跑者对6个相同
            // SUCCEEDING 调用 (按名称+args键,
            // 不 调用 id)作为一个卡住的循环,
            // 在末日的早期停止 -loop 与这个情景的转折预算不同。
            //
            // 有了固定的模式,结果取决于每个grep是否
            // 偶然成功:本地循环运行到预算,测试通过;
            // 而CI则在6号弯时绊倒了末日检查,
            // 在1.9秒而不是11秒时失败。 模式的变换将固定在预定路径上,
            // 不管怎样。
            emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                           json{{"pattern", "zzz_no_match_expected_"
                                             + std::to_string(turn)}});
            emit_finish(sink, StopReason::ToolUse);
        });
        auto out = run_task("explore everything in exhaustive detail");
        check(has(out.text, "turn budget"),
              "A: report is the turn-budget banner");
        // 回归:必须进行回合-1描述 不 报告是单独提出的。
        // 如果它出现的话 必须 位于 过期-salvage 标签,
        // 在不完整的标志之后——永远不要作为整个答案.
        if (has(out.text, "I'll start by mapping")) {
            check(has(out.text, "early, incomplete")
                  && out.text.find("turn budget")
                       < out.text.find("I'll start by mapping"),
                  "A: narration only appears labeled as a stale partial, after the banner");
        } else {
            check(true, "A: turn-1 narration is not surfaced at all");
        }
        // 横幅必须明确表示不完整,这样父母就信任它而不是
        // 过期 单曲 -liner.
        check(has(out.text, "incomplete") || has(out.text, "without producing"),
              "A: banner marks the report incomplete");
    }

    // ── B. 写作词尾: 干净的最后报告。 ─────────────────
    // 探索几轮,然后在第三轮停止调用工具并写
    // 出真正的答案。跑者必须准确返回该传言。
    {
        install_scripted_stream([](int turn, const provider::Request&,
                                   const provider::EventSink& sink) {
            if (turn < 3) {
                emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                               json{{"pattern", "zzz_no_match_expected"}});
                emit_finish(sink, StopReason::ToolUse);
            } else {
                emit_text(sink, "FINAL_ANSWER: the corpus has three modules.");
                emit_finish(sink, StopReason::EndTurn);
            }
        });
        auto out = run_task("summarize the corpus");
        check(has(out.text, "FINAL_ANSWER: the corpus has three modules."),
              "B: clean final prose is returned verbatim");
        check(!has(out.text, "turn budget"),
              "B: no budget banner when the agent finished properly");
        check(!out.is_error, "B: a clean finish is not an error");
    }

    // ── P. 预算规模 ROLE 工作形态 ─────────
    // 单一全球上限不能同时满足两个条件: -only 扫描快速汇合,
    // 而执行循环则读 – 编辑 – 构建 – 读 错误 – 修正
    // – re -run ,即6-10转动 一个 真实任务需要
    // 诚实的周期和几个周期。旧的24号平面意味着一个编码员花费了
    // 整个预算方向和报告 "I ran out of turns"
    // 而不是做工作—— 确切的失败是 N/O 渔获量以上的案例。
    //
    // 这些是 CAPS 而不是配额:5个回合完成 成本5.
    // 因此,支付最高上限的唯一运行是那些否则会拥有
    // FAILED 来点吧
    {
        namespace sa = agentty::tools::subagent;
        check(sa::max_turns_for(/*read_only=*/false)
                  > sa::max_turns_for(/*read_only=*/true),
              "P: write roles get a strictly larger budget than read-only ones");
        // 编码器必须能够运行多个完整的编辑_建设_验证周期.
        // ~8转弯时,每轮轮到下层 "more than a token attempt"
        // ——在它下面,不是任务,而是决定结果.
        check(sa::max_turns_for(/*read_only=*/false) >= 48,
              "P: a write role can run several edit→build→verify cycles");
        // 读取 -only 工作仍然需要空间 真正的扫荡(
        // repo_map 10个greps,目标读),而不为其自身
        // 的缘故慷慨——一个仍然在40转的探险家丢失了,而不是彻底的.
        check(sa::max_turns_for(/*read_only=*/true) >= 24,
              "P: read-only roles keep at least the historical budget");
        // 天花板是逃跑的守卫,必须约束每个角色.
        check(sa::kMaxTurns >= sa::max_turns_for(/*read_only=*/false),
              "P: the global ceiling bounds the largest role budget");
    }

    // ── N. 自我 -reported 失败不得 渲染 成为成功。 ───────────
    // 该 错误此案件存在: 代理器每转弯烧伤, 然后用井签下 -formed
    // 报告内容未显示。旧的退出路径只检查是否改信 EXISTED 如此,
    // 这掉进每一个守卫,没有标志, is_error 未设置——和 UI
    // 画一幅绿色 "✓ DONE" 结束
    // "Task NOT completed … made zero edits".（任务未完成，没有进行任何修改。）
    //
    // 两个独立信号必须各自足够:击出封顶,
    // 以及报告自己的判断.
    {
        install_scripted_stream([](int turn, const provider::Request&,
                                   const provider::EventSink& sink) {
            // 将预算用调用工具燃烧,然后用一份承认失败的整洁
            // 报告——就是所观察到的形状——结束。
            if (turn < agentty::tools::subagent::max_turns_for(/*read_only=*/false) - 1) {   // 最终转弯后写作
                emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                               json{{"pattern", "zzz_unique_" + std::to_string(turn)}});
                emit_finish(sink, StopReason::ToolUse);
            } else {
                emit_text(sink,
                          "## OUTCOME\n\n**Task NOT completed.** I exhausted my "
                          "turn budget on file reading and made **zero edits** — "
                          "no files were created or modified.");
                emit_finish(sink, StopReason::EndTurn);
            }
        });
        auto out = run_task("extract the codec into a shared module");
        check(out.is_error,
              "N: a self-declared failure is flagged is_error (no green ✓ DONE)");
        // 代理商自己的言语仍然值得上浮——呼叫者需要知道
        // HOW失败,而不仅仅是它失败了.
        check(has(out.text, "zero edits"),
              "N: the agent's own account is preserved, not discarded");
        check(has(out.text, "turn budget"),
              "N: exhausting the budget is stated explicitly");
    }

    // ── O. 清洁的改信者+用尽的预算仍未成功。 ───────
    // 相同的盖盖疲劳,但关闭的口音听起来是POSITIVE.
    // 报告是逐字保存的(很可能有用),然而光是转弯盖
    // 意味着任务不能被假定完成:一个冲出房间的代理人
    // 与一个完成的代理人不同.
    {
        install_scripted_stream([](int turn, const provider::Request&,
                                   const provider::EventSink& sink) {
            if (turn < agentty::tools::subagent::max_turns_for(/*read_only=*/false) - 1) {   // 最终转弯后写作
                // 每轮变化——6次相同 调用 将末日推向深渊
                // -loop 上限前的检查和退出(见情景A)。
                emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                               json{{"pattern", "zzz_no_match_expected_"
                                                 + std::to_string(turn)}});
                emit_finish(sink, StopReason::ToolUse);
            } else {
                emit_text(sink, "I mapped the modules and it all looks fine.");
                emit_finish(sink, StopReason::EndTurn);
            }
        });
        auto out = run_task("map the modules");
        check(out.is_error,
              "O: hitting the cap is an error even when the prose sounds happy");
        check(has(out.text, "I mapped the modules"),
              "O: the final prose is preserved for the caller to judge");
        check(has(out.text, "incomplete") || has(out.text, "turn budget"),
              "O: the caller is told the result may be incomplete");
    }

    // ── C. 结 论 -up 盖前击火. ──────────────────────
    // 保留工具-only,但记录是否将任何完成交给了推力.
    // 尊重报告的模式将发表报告;我们证明,
    // 这一努力是完全实现的(使报告成为可能的机制)。
    {
        auto saw_nudge = std::make_shared<std::atomic<bool>>(false);
        install_scripted_stream([saw_nudge](int turn, const provider::Request& req,
                                            const provider::EventSink& sink) {
            if (req_has_nudge(req)) saw_nudge->store(true);
            // 每一转弯变化——靠近 CAP ,所以这个循环
            // 决不能在末日早期退出 -loop 检查(见情景A)。
            emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                           json{{"pattern", "zzz_no_match_expected_"
                                             + std::to_string(turn)}});
            emit_finish(sink, StopReason::ToolUse);
        });
        (void)run_task("keep exploring forever");
        check(saw_nudge->load(),
              "C: a wrap-up nudge is delivered before the turn cap");
    }

    // ── D. Native/local 提供者 数据流 不需要请求认证。 ──────
    {
        install_scripted_stream([](int, const provider::Request& req,
                                   const provider::EventSink& sink) {
            check(auth::is_empty(req.auth),
                  "D: authless provider receives an empty request header");
            emit_text(sink, "AUTHLESS_OK");
            emit_finish(sink);
        }, /*with_auth=*/false);
        auto out = run_task("answer using the native provider");
        check(!out.is_error && has(out.text, "AUTHLESS_OK"),
              "D: task runs with native/local auth resolved by transport");
    }

    // ── E. 重新测试空供应商的完成情况。 ─────────────────────────
    {
        install_scripted_stream([](int turn, const provider::Request&,
                                   const provider::EventSink& sink) {
            if (turn == 0) {
                emit_finish(sink);  // 无文本、无工具、无StreamError
            } else {
                emit_text(sink, "RECOVERED_AFTER_EMPTY");
                emit_finish(sink);
            }
        });
        auto out = run_task("recover from a dropped response");
        check(!out.is_error && has(out.text, "RECOVERED_AFTER_EMPTY"),
              "E: silently empty completion is retried");
    }

    // ── F. 取消 中断 重试 退避. ────────────────────────
    {
        auto cancel = std::make_shared<std::stop_source>();
        install_scripted_stream([cancel](int, const provider::Request&,
                                         const provider::EventSink& sink) {
            emit_error(sink, "temporary network failure");
            cancel->request_stop();
        });
        tools::CallContext ctx;
        ctx.cancel.push_back(cancel->get_token());
        const auto began = std::chrono::steady_clock::now();
        auto out = run_task("cancel during retry backoff", ctx);
        const auto elapsed = std::chrono::steady_clock::now() - began;
        check(out.is_error && has(out.text, "cancelled"),
              "F: cancellation returns an actionable retry error");
        check(elapsed < std::chrono::milliseconds(500),
              "F: cancellation does not wait through retry backoff");
    }

    // ── G. 永久性错误立即停止,不需重试。 ──────────────
    {
        auto calls = std::make_shared<std::atomic<int>>(0);
        install_scripted_stream([calls](int, const provider::Request&,
                                        const provider::EventSink& sink) {
            calls->fetch_add(1);
            StreamError e{.message = "invalid request payload"};
            e.http_status = 400;
            sink(Msg{msg::StreamMsg{std::move(e)}});
        });
        const auto began = std::chrono::steady_clock::now();
        auto out = run_task("do not retry a bad request");
        check(out.is_error && calls->load() == 1,
              "G: permanent HTTP errors are attempted exactly once");
        check(std::chrono::steady_clock::now() - began
                  < std::chrono::milliseconds(500),
              "G: permanent error returns without retry backoff");
    }

    // ── H. 多个互页 调用 保留他们自己的论据 数据流. ──
    {
        auto assembled = std::make_shared<std::atomic<bool>>(false);
        install_scripted_stream([assembled](int turn, const provider::Request& req,
                                            const provider::EventSink& sink) {
            if (turn == 0) {
                sink(Msg{msg::StreamMsg{StreamToolUseStart{
                    ToolCallId{"a"}, ToolName{"grep"}}}});
                sink(Msg{msg::StreamMsg{StreamToolUseStart{
                    ToolCallId{"b"}, ToolName{"grep"}}}});
                sink(Msg{msg::StreamMsg{StreamToolUseDelta{
                    ToolCallId{"a"}, "{\"pattern\":\"alpha"}}});
                sink(Msg{msg::StreamMsg{StreamToolUseDelta{
                    ToolCallId{"b"}, "{\"pattern\":\"beta"}}});
                sink(Msg{msg::StreamMsg{StreamToolUseDelta{
                    ToolCallId{"a"}, "\"}"}}});
                sink(Msg{msg::StreamMsg{StreamToolUseEnd{ToolCallId{"a"}}}});
                sink(Msg{msg::StreamMsg{StreamToolUseDelta{
                    ToolCallId{"b"}, "\"}"}}});
                sink(Msg{msg::StreamMsg{StreamToolUseEnd{ToolCallId{"b"}}}});
                emit_finish(sink, StopReason::ToolUse);
                return;
            }
            for (const auto& m : req.messages) {
                if (m.role != Role::Assistant || m.tool_calls.size() != 2) continue;
                assembled->store(
                    m.tool_calls[0].args.value("pattern", "") == "alpha"
                    && m.tool_calls[1].args.value("pattern", "") == "beta");
            }
            emit_text(sink, "INTERLEAVED_OK");
            emit_finish(sink);
        });
        auto out = run_task("assemble parallel calls");
        check(assembled->load() && has(out.text, "INTERLEAVED_OK"),
              "H: interleaved tool deltas are assembled by call id");
    }

    // ── I. MaxTokens 传言是片面的,从来不是干净的报告. ─────────────
    {
        install_scripted_stream([](int, const provider::Request&,
                                   const provider::EventSink& sink) {
            emit_text(sink, "PARTIAL_NOT_A_REPORT");
            emit_finish(sink, StopReason::MaxTokens);
        });
        auto out = run_task("produce an oversized report");
        check(out.is_error && has(out.text, "output limit"),
              "I: MaxTokens completion is reported as an error");
        check(!has(out.text, "\n\nPARTIAL_NOT_A_REPORT"),
              "I: partial MaxTokens prose is not returned as a clean report");
    }

    // ── J. 父母取消 旅行活动提供者请求符。 ──
    {
        // 父母亲 调用's 取消 符号, 如 cmd_factory 把它
        // 放进里面 CallContext。该词 数据流 反应它行走的瞬间。
        auto parent = std::make_shared<std::stop_source>();
        auto request_cancelled = std::make_shared<std::atomic<bool>>(false);
        tools::CallContext parent_ctx;
        parent_ctx.cancel.push_back(parent->get_token());
        install_scripted_stream([parent, request_cancelled](
                                    int, const provider::Request& req,
                                    const provider::EventSink& sink) {
            check(static_cast<bool>(req.cancel),
                  "J: active subagent request has a cancellation token");
            parent->request_stop();
            const auto until = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < until
                   && !req.cancel->is_cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            request_cancelled->store(req.cancel->is_cancelled());
            emit_error(sink, "cancelled");
        });
        auto out = run_task("cancel active stream", parent_ctx);
        check(request_cancelled->load() && out.is_error,
              "J: parent cancellation reaches provider and stops the task");
    }

    // ── K. 一项被接受的非-idempotent请求从未被重播。 ─────────
    {
        auto calls = std::make_shared<std::atomic<int>>(0);
        install_scripted_stream([calls](int, const provider::Request&,
                                        const provider::EventSink& sink) {
            calls->fetch_add(1);
            emit_error(sink, "connection closed after server accepted request",
                       /*non_replayable=*/true);
        });
        auto out = run_task("do not duplicate this generation");
        check(calls->load() == 1,
              "K: accepted provider request is attempted exactly once");
        check(out.is_error,
              "K: accepted-request interruption surfaces instead of replaying");
    }

    // L. 子代理请求应节省开销：复用缓存前缀并限制输出。
    // 每一个转动的副剂必须携带 相同 session_key (所以,
    // 重型系统 提示词 + 工具 模式定义 + 工具结果 提示词-cache
    // 横跨环路而不是重线 -billing 每一转折的全价),
    // 以及 max_tokens 被封顶在它曾经要求的32k以下(报告不需要它)。
    {
        auto first_key   = std::make_shared<std::string>();
        auto key_stable  = std::make_shared<std::atomic<bool>>(true);
        auto cap_ok      = std::make_shared<std::atomic<bool>>(true);
        auto key_nonempty= std::make_shared<std::atomic<bool>>(false);
        install_scripted_stream([first_key, key_stable, cap_ok, key_nonempty](
                                    int turn, const provider::Request& req,
                                    const provider::EventSink& sink) {
            if (req.max_tokens > 8192) cap_ok->store(false);
            if (turn == 0) {
                *first_key = req.session_key;
                key_nonempty->store(!req.session_key.empty());
            } else if (req.session_key != *first_key) {
                key_stable->store(false);
            }
            if (turn < 2) {
                emit_tool_call(sink, "t" + std::to_string(turn), "grep",
                               json{{"pattern", "zzz_no_match_expected"}});
                emit_finish(sink, StopReason::ToolUse);
            } else {
                emit_text(sink, "REPORT_OK");
                emit_finish(sink, StopReason::EndTurn);
            }
        });
        auto out = run_task("investigate the module layout");
        check(key_nonempty->load(),
              "L: subagent request carries a stable session_key for caching");
        check(key_stable->load(),
              "L: session_key is identical across the subagent's turns");
        check(cap_ok->load(),
              "L: subagent max_tokens is capped (<=8192), not the old 32k");
        check(!out.is_error && has(out.text, "REPORT_OK"),
              "L: the economical config still produces a clean report");
    }

    // M. 子代理系统提示词应精简，不携带完整父代理提示词。
    // 携带一个大 内存-tools 协议+技能 目录 亚
    // 剂永远不能使用(不是在其 允许清单 ,
    // 没有事实 持久化 运货时会夸大其账单和缓存前缀。
    // 变体 必须在保持其所需业务纪律的同时,放弃这些限制。
    {
        const std::string full = provider::default_system_prompt(::agentty::IoAccess::grant(), false);
        const std::string lean = provider::default_system_prompt(::agentty::IoAccess::grant(), true);
        auto has = [](const std::string& s, const char* n) {
            return s.find(n) != std::string::npos;
        };
        check(lean.size() < full.size(),
              "M: lean subagent prompt is smaller than the full parent prompt");
        check(has(full, "<memory-tools>") && !has(lean, "<memory-tools>"),
              "M: lean prompt drops the parent-only memory-tools protocol");
        check(has(lean, "<file-editing>"),
              "M: lean prompt keeps the operational file-editing discipline");
        check(has(lean, "<environment>"),
              "M: lean prompt keeps the environment block");
    }

    // N. 同一角色的顺序子代理不得共享
    // 改为-dedup上下文。
    //
    // 跑步瞄准镜为`read`每副剂的脱落(tools::reader),
    // 因为缓存是共享的,并且与"refer to the earlier tool_result"
    // 重复了同样的(路径,范围)——这一说法只针对实际收到字
    // 节的上下文,过去id来源于`&thread`,
    // run()中的LOCAL.
    //
    // 看上去很独特,但并不如此 潜水员在孤立的状态下运行
    // 工作线程 线程 ,并按顺序运行重复使用相同的堆栈,
    // 所以 `&thread` 回来啦 字节-identical 每次
    // 都(按先后顺序衡量:6次) 线程 因此,两个连续的探险家分享了一个背景,
    // 第二个被拒绝了第一个文件读到的每一个文件——它看到了
    // "File unchanged since last read"（文件自上次读取后没有变化。）
    // 因为它从未打开过文件, 烧掉它的转折 -asking ,
    // 而报告无任何用处。正是存在防止的缺陷,才重新用于顺序处理。
    //
    // 被锁定在可见的后果中: 两者都读取相同的文件,
    // 第二必须重新读取, 而不是标记为缓存。
    {
        {
            std::ofstream f("n_probe.txt");
            f << "probe line\n";
        }
        // 每跑一次输出:读数的tool_result,如模型所见.
        std::vector<std::string> outputs;
        install_scripted_stream(
            [&](int, const provider::Request& req, const provider::EventSink& sink) {
                std::string seen;
                bool have = false;
                for (const auto& m : req.messages)
                    for (const auto& tc : m.tool_calls)
                        if (tc.name.value == "read") { seen = std::string{tc.output()}; have = true; }
                if (!have) {
                    emit_tool_call(sink, "n_read", "read", json{{"path", "n_probe.txt"}});
                    emit_finish(sink, StopReason::ToolUse);
                    return;
                }
                outputs.push_back(seen);
                emit_text(sink, "REPORT_N");
                emit_finish(sink);
            });

        (void)run_task("same role, run one");
        (void)run_task("same role, run two");

        check(outputs.size() == 2,
              "N: both sequential subagent runs read the file");
        check(outputs.size() == 2 && has(outputs[0], "probe line")
                  && has(outputs[1], "probe line"),
              "N: both reads returned the file");
        check(outputs.size() == 2 && !has(outputs[1], "[cached"),
              "N: the second run's read is fresh, not the first run's "
              "cache entry (distinct read contexts)");
    }

    // O. 总运行截止时间必须真正终止任务，并明确说明。
    //
    // kMaxTurns 约束 COMPLETIONS, 不限时间, 其它情况也没有:
    // a 数据流得到30分钟的总和,90 s闲置的超时,
    // 并且只闲置对真实寂静的火—— 后端, 将一个恒定的且从未完成的拖动,
    // 看上去对下面的每一层都健康。 堆积在转弯和重试的盖子上,
    // 也就是在环向放弃之前80 x 3 x 30分钟, i.e。
    // 一张永不回的`task`卡
    //
    // 开车经过 真实 故意慢跑的跑步器 数据流 和1个 -second 预算
    // ( AGENTTY_SUBAGENT_MAX_SECONDS 15)}这
    // 完全是为了这个而存在的 -minute 恒定不能测试,
    // 未经测试的停止路径静静地停止工作).
    {
        ::setenv("AGENTTY_SUBAGENT_MAX_SECONDS", "1", 1);
        std::atomic<int> completions{0};
        install_scripted_stream(
            [&](int, const provider::Request&, const provider::EventSink& sink) {
                const int n = completions.fetch_add(1) + 1;
                // 慢于预算 / 转弯, 因此最后的结束是结束
                // 运行而不是回合上限。 永远不要发布最终
                // 报告: 运行必须从 OUTSIDE 停止。
                //
                // 参数必须 VARY 跑步者还有一次
                // 重复 -success 断开器( 相同
                // 的) 调用 接连不断的重复也是循环,
                // 而先前的草案则重新 -read 同一路径绊倒
                // 该 3号转弯而不是时钟——测试通过了计时检查,
                // 但并没有证明最后期限。
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                emit_tool_call(sink, "t" + std::to_string(n), "read",
                               json{{"path", "README.md"},
                                    {"offset", n * 10}});
                emit_finish(sink, StopReason::ToolUse);
            });

        const auto t0 = std::chrono::steady_clock::now();
        auto out = run_task("loop until the clock runs out");
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        ::unsetenv("AGENTTY_SUBAGENT_MAX_SECONDS");

        const auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        // 界限:没有期限,这个界限将一直到转弯盖(32,一个读为-only角色),
        // 每根400ms=12s+,或永远挂在真正的吊上.
        check(secs < 8,
              "O: the run is stopped by the clock, not left to the turn cap");
        check(completions.load() < 32,
              "O: it stopped before exhausting the turn budget");
        // 报告必须 NAME 钟声。 "out of time" 财务
        // 报告和已审计财务报表 "out of turns" 点在不同的
        // 原因(一个慢后端对一个任务太大),读者只能根据我们所说的不同来行动.
        check(has(out.text, "wall-clock"),
              "O: the report names the wall-clock limit as the reason");
        check(out.is_error,
              "O: a deadline stop is an error, not a green success");
    }

    // P. 只读子代理可以向较低成本路由，但不能直接降到底层。
    //
    // Read-only角色(explorer/reviewer)
    // 运行在比母星更便宜的模型上,因为旗舰上的四位探险家的扇-out是大量大量的钱,
    // 大部分是阅读的. Read Tier::Cheap的地板曾经是XX003xXX,
    // 这在Anthropic上意味着每位探险家在
    // 父母是Opus时跑在海库上——第1级是第3级的
    // 调查.
    //
    // 这是一种无声的悬崖 也是最相似的原因 `task`
    // 回归瘦小:探索不是机械的,而是决定40个grep
    // 命中中哪一个很重要,一个弱小的模型读错了文件,
    // 然后自信地总结它们。 UI 说为什么。
    //
    // Zed完全不降级(Thread::new_subagent复制了母型号),
    // 中位是中间位置——真正的节省,没有悬崖.
    // 被钉在这里是因为地板是一 枚举 说到这个,
    // 然后把它扔回廉价的, 看上去就是一个无害的简化。
    {
        std::vector<ModelInfo> rows;
        for (const char* id : {"claude-haiku-4-5", "claude-sonnet-4-5",
                               "claude-opus-4-5"}) {
            ModelInfo mi;
            mi.id = ModelId{id};
            mi.supports_tools = true;
            rows.push_back(std::move(mi));
        }
        const auto floor = ModelCapabilities::Tier::Mid;

        check(cheapest_capable_model("claude-opus-4-5", rows, floor)
                  == "claude-sonnet-4-5",
              "P: a flagship parent routes read-only work to Mid, not Cheap");
        // 永远不要走UP, 也永远不要走在不必要的开关。
        check(cheapest_capable_model("claude-sonnet-4-5", rows, floor)
                  == "claude-sonnet-4-5",
              "P: a Mid parent keeps its own model");
        check(cheapest_capable_model("claude-haiku-4-5", rows, floor)
                  == "claude-haiku-4-5",
              "P: a Cheap parent is never routed UP");
        // 旧的楼层就是它原来的样子: 这记录了
        // 变化而不是重复。
        check(cheapest_capable_model("claude-opus-4-5", rows,
                                     ModelCapabilities::Tier::Cheap)
                  == "claude-haiku-4-5",
              "P: the old Cheap floor really did land on the weakest model");
    }

    // Q. 关闭时停止正在运行的子代理，并等待其结束。
    //
    // 副剂运行在 a 上 Cmd::task_isolated 线程 ,哪个 jaal 离线 :
    // "isolated threads are asked to stop but never waited for" 因此,
    // 没有 注册表, main() 可以在子剂还在内时返回
    // cfg.stream () ——和 数据流 以主堆栈的参考值
    // 保存提供者对象。 只有当副代理物恰好是时才有错误。
    // 流式传输 退出时, 用户放弃慢步并点击逃逸, 所以它呈现为
    // "subagents crash sometimes".
    //
    // Zed避免了这种结构上的:一个子代理是`running_subagents`
    // 列表中拥有的实体,取消母公司等待每个实体.
    // 我们一直这样 jaal 分离(一个楔形的音节不能使
    // 进程打开)并添加 注册表 + a BOUNDED 等等,
    // 这就是转动 竞态 成为握手为每 数据流 有点慢
    {
        std::atomic<bool> in_stream{false};
        std::atomic<bool> left_stream{false};
        install_scripted_stream(
            [&](int, const provider::Request& req,
                const provider::EventSink& sink) {
                // Model a 数据流 活着但还没完:
                // 坐在这里直到请求自己完成 取消 签个字,
                // 就是个真正的旅行 传输层 当桥取消时
                in_stream = true;
                for (int i = 0; i < 400; ++i) {
                    if (req.cancel && req.cancel->is_cancelled()) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                left_stream = true;
                emit_text(sink, "REPORT_Q");
                emit_finish(sink);
            });

        std::thread runner([&] { (void)run_task("hold a stream open"); });
        // 等待,直到它真正在 数据流 所以我们在测试这个
        // 有趣的案子 而不是 竞态 我们靠运气赢了
        for (int i = 0; i < 200 && !in_stream.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(in_stream.load(), "Q: the subagent reached its provider stream");

        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t stuck = tools::subagent::shutdown_running(
            agtest::cap(), std::chrono::milliseconds(3000));
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        runner.join();

        check(stuck == 0,
              "Q: shutdown reports every run left within the grace");
        check(left_stream.load(),
              "Q: the run actually left the provider stream, not just "
              "got marked cancelled");
        // 关键在于 RETURNS 而不是赛车主的风速——并
        // 迅速返回:跑道的停站现在到达了 数据流 以电子邮件方式
        // 发送 stop_callback 20米的民调
        check(waited < 2000,
              "Q: the wait is bounded, not an indefinite join");
    }

    // R. 开始AFTER关闭的运行已经开始,但被拒绝,没有错过。
    //
    // 旧的注册表拍了它的运行列表,然后在快照上等待,
    // 所以一个瞬间的记录运行从未被要求停下来,
    // 也从未等待过——它被撞倒,没有人跟踪它。
    // stop_group关闭了与伯爵相同的锁下的接收。
    {
        // 关闭已经被上面的Q调用,所以该组关闭.
        tools::subagent::RunRegistration late;
        check(late.cancelled(),
              "R: a run registering after shutdown starts already cancelled");
        check(late.token().stop_requested(),
              "R: and its stop_token is already stopped, so no branch is "
              "needed at the call site");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
