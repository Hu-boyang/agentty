// SPDX-License-Identifier: same as project.
//
// ExternalAcpBackend —参见 include/agentty/provider/external_acp_backend.hpp.

#include "agentty/provider/external_acp_backend.hpp"
#include "agentty/util/callback.hpp"

#include <chrono>
#include <algorithm>
#include <optional>
#include <maya/runtime.hpp>
#include <thread>
#include <utility>
#include <variant>

#include <acp/caps.hpp>          // negotiate_version
#include <acp/content.hpp>       // acp::TextContent，ContentBlock
#include <acp/methods.hpp>       // params/results
#include <acp/tools.hpp>         // RequestPermissionOutcome，PO_*

#include "agentty/util/child_process.hpp"   // util::ChildProcess (便携式产卵)

#include "agentty/domain/conversation.hpp"  // agentty::Message，角色
#include "agentty/util/base64.hpp"
#include "agentty/tool/util/fs_helpers.hpp"  // read_file/write_file+路径门
#include "agentty/tool/util/sandbox.hpp"     // sandbox::run_argv/run_shell_command
#include "agentty/tool/util/subprocess.hpp"  // SubprocessResult

namespace agentty::provider {

// 将该TU中的不合格的`acp::`固定到GLOBAL ACP 库命名空间。
// agentty has its OWN `agentty::acp` (the ACP *server* — src/acp/server.cpp),
// src/acp/server.cpp)，因此在agentty::provider内部，
// 裸露的`acp::`是不明确的；在统一构建下（使用打开 agentty::acp 的
// TU 进行批处理），它错误地绑定到 agentty::acp。此别名使下面的每个 `acp::…`
// 无条件解析为 ::acp — 无论批处理如何，都是正确的。
namespace acp = ::acp;

namespace {

// ── 当前主机转动 __ ACP 提示词 块 ───────────────────────────────────
// 委托代理跨轮保留其自己的转录本，因此每个提示词仅
// 携带最新的REAL用户轮次以及附加到该轮次的合成上下文。
// 主动 RAG 作为本机型号线路的用户消息存储；
// 此处将其视为用户问题会删除实际请求。
[[nodiscard]] acp::List<acp::ContentBlock> prompt_blocks_from(const Request& req) {
    acp::List<acp::ContentBlock> out;

    auto has_image = [](const agentty::Message& m) {
        return std::any_of(m.images.begin(), m.images.end(),
                           [](const agentty::ImageContent& image) {
                               return !image.bytes().empty();
                           });
    };

    std::size_t user_index = req.messages.size();
    for (std::size_t i = req.messages.size(); i-- > 0;) {
        const auto& m = req.messages[i];
        if (m.role == agentty::Role::User && !m.is_proactive_context() && !m.fork_note
            && (!m.text.empty() || has_image(m))) {
            user_index = i;
            break;
        }
    }

    auto append_message = [&out](const agentty::Message& message) {
        if (!message.text.empty()) {
            acp::TextContent text;
            text.text = message.text;
            out.emplace_back(std::move(text));
        }
        for (const auto& image : message.images) {
            if (image.bytes().empty()) continue;
            acp::ImageContent block;
            block.data = image.base64();
            block.mimeType = image.media_type.empty() ? "image/png" : image.media_type;
            out.emplace_back(std::move(block));
        }
    };

    if (user_index < req.messages.size()) {
        // 分叉出处：fork_note是在分叉的线程、
        // BEFORE用户的第一个提示词的HEAD处播种的合成用户消息。
        // 它携带指向模型按需读取的父转录本的指针。
        // 在前面添加任何此类前导注释，以便叉子的第一圈仍然告诉代理其
        // 先前上下文所在的位置（下面的 active-context
        // 循环仅扫描 之后 user_index）。
        for (std::size_t i = 0; i < user_index; ++i) {
            if (req.messages[i].fork_note)
                append_message(req.messages[i]);
        }
        append_message(req.messages[user_index]);
        for (std::size_t i = user_index + 1; i < req.messages.size(); ++i) {
            const auto& m = req.messages[i];
            if (m.role == agentty::Role::User && m.is_proactive_context())
                append_message(m);
        }
    }

    // 没有内容的提示词无效；发送一个空文本块，以便代理收到一个 well
    // -formed 请求，而不是我们默默地 no-op'ing。
    if (out.empty()) {
        acp::TextContent t;
        t.text = "";
        out.emplace_back(std::move(t));
    }
    return out;
}

} // 命名空间结束。

// ── Turn-level StopReason 绘图 ────────────────────────────────────────────
StopReason map_acp_stop_reason(acp::StopReason r) noexcept {
    switch (r) {
        case acp::StopReason::EndTurn:         return StopReason::EndTurn;
        case acp::StopReason::MaxTokens:       return StopReason::MaxTokens;
        // "too many tool rounds"为转-level护栏；
        // 从 agentty 的回合视图来看，模型刚刚完成了这一轮 - EndTurn
        // 是诚实的回合 -level 原因（代理循环自己的回合上限管辖）。
        case acp::StopReason::MaxTurnRequests: return StopReason::EndTurn;
        // 已取消/拒绝被呼叫者处理为TurnErrors并且永远不会到达这里；
        // 防御性地映射到 EndTurn。
        case acp::StopReason::Refusal:         return StopReason::EndTurn;
        case acp::StopReason::Cancelled:       return StopReason::EndTurn;
    }
    return StopReason::EndTurn;
}

// ── 默认 沙箱-wired 代表 ───────────────────────────────────
AcpClientDelegate default_sandbox_delegate() {
    namespace util = agentty::tools::util;
    AcpClientDelegate d;

    // fs/read_text_file → 读取门（工作区 + 技能 read
    // -allowlist；符号链接转义被阻止）。 out-of-bounds
    // 路径或丢失的文件返回 nullopt，处理函数 映射到空读取 - 代理
    // 永远不会逃脱边界，也永远不会收到原始文件系统错误。
    d.read_text_file = [](const std::string& path,
                          std::optional<int> line,
                          std::optional<int> limit) -> std::optional<std::string> {
        auto wp = util::make_readable_path_checked(path, "acp");
        if (!wp) return std::nullopt;
        std::string body = util::read_file(*wp);

        // ACP line/limit是1-based行窗口(类似于agentty的读取工具)。
        // 不存在⇒整个文件。
        if (!line && !limit) return body;

        std::vector<std::string> lines;
        std::string cur;
        for (char c : body) {
            if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(std::move(cur));

        const int start = line ? std::max(1, *line) : 1;         // 1-based
        const int count = limit ? std::max(0, *limit)
                                : static_cast<int>(lines.size());
        std::string out;
        for (int i = start - 1;
             i < static_cast<int>(lines.size()) && i < start - 1 + count; ++i) {
            out += lines[static_cast<std::size_t>(i)];
            out += '\n';
        }
        return out;
    };

    // fs/write_text_file→写门（工作区 仅-从不读允许清单）。
    // 在 out-of-workspace 路径或写入错误上拒绝 (false)。
    d.write_text_file = [](const std::string& path, const std::string& content) -> bool {
        auto wp = util::make_workspace_path_checked(path, "acp");
        if (!wp) return false;
        return util::write_file(*wp, content).empty();   // ""==成功
    };

    // terminal/create → OS-native 沙箱 (bwrap/沙箱-exec；
    // Windows 上无 -op)。运行命令直至完成；后端 缓存 是异步
    // output/wait_for_exit 跟随 -ups 的结果。
    d.run_terminal = [](const std::string& command,
                        const std::vector<std::string>& args,
                        const std::optional<std::string>& cwd)
            -> AcpClientDelegate::TerminalResult {
        using namespace std::chrono_literals;
        constexpr std::size_t kMax = 64'000;
        constexpr auto        kTimeout = 120s;

        util::SubprocessResult r;
        if (cwd && !cwd->empty()) {
            // 请求cwd——通过shell表单运行，以便我们可以首先`cd`。
            // 最少引用 cwd 和每个 arg（单个 -quote 换行）。
            auto shq = [](const std::string& s) {
                std::string q = "'";
                for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; }
                q += "'";
                return q;
            };
            std::string cmd = "cd " + shq(*cwd) + " && " + shq(command);
            for (const auto& a : args) cmd += " " + shq(a);
            r = util::sandbox::run_shell_command(cmd, kMax, kTimeout);
        } else {
            std::vector<std::string> argv;
            argv.reserve(args.size() + 1);
            argv.push_back(command);
            for (const auto& a : args) argv.push_back(a);
            r = util::sandbox::run_argv(argv, kMax, kTimeout);
        }

        AcpClientDelegate::TerminalResult out;
        out.output    = std::move(r.output);
        out.exit_code = r.started ? r.exit_code : 127;   // 127==生成失败
        out.truncated = r.truncated;
        if (!r.started && out.output.empty()) out.output = r.start_error;
        return out;
    };

    // request_permission故意留下UNSET：外部代理
    // 运行其自己的权限门，并且上面的每个fs/terminal 调用已经
    // 是这里的边界-checked，因此我们默认允许用户进行相同的操作，
    // 而不是双重-prompting。
    return d;
}

// ── 建筑业 ────────────────────────────────────────────────────
ExternalAcpBackend::ExternalAcpBackend(ExternalAcpOptions opts)
    : opts_(std::move(opts)), router_(make_router()) {}

void ExternalAcpBackend::connect(rpc::Peer& peer) noexcept {
    peer_ = &peer;
}

std::string ExternalAcpBackend::session_id() const {
    return session_.get().value_or(std::string{});
}

// ── 入境 调用 ───────────────────────────────────────────────────────────
// 代理的调用和通知在对端的阅读器上应答。该回合的接收器
// 在 in-flight 回合的生命周期内发布，
// 否则为空，因此结算后的后期更新会被丢弃，而不是崩溃。
//
// 读者将每个回调交给其`unlocked`；路由器的 处理函数 通过
// 其上下文 (Ctx) 获取它，因此会检查采用 锁 的 处理函数。
rpc::Dispatch ExternalAcpBackend::make_dispatch() {
    rpc::Dispatch d;
    d.on_call = [this](const maya::unlocked& cap, const acp::Call& c) {
        Ctx ctx{*this, cap};
        return router_.handle(ctx, c);
    };
    d.on_notification = [this](const maya::unlocked& cap, const acp::Notification& n) {
        Ctx ctx{*this, cap};
        router_.handle(ctx, n);
    };
    return d;
}

ExternalAcpBackend::Router ExternalAcpBackend::make_router() const {
    namespace m = acp::to_client;
    using B = Ctx;
    Router r;

    // session/update → 将 SessionUpdate 归一化至该轮的下沉。
    r.on<m::SessionUpdate>([](B& c, const acp::SessionUpdateMsg& msg) {
        auto round = c.self.round_.current();
        if (!round) return;
        if (auto member = round->inflight.join()) round->sink(msg.update);
    });

    // request_permission→代表决定；默认允许。当允许时，
    // 我们用代理人自己的第一个选项来回答，否则 已取消。
    r.on<m::RequestPermission>([](B& c, const acp::RequestPermissionParams& p) {
        auto& b = c.self;
        bool allow = true;
        if (b.opts_.delegate.request_permission) allow = b.opts_.delegate.request_permission(p);
        acp::RequestPermissionResult res;
        if (allow && !p.options.empty()) {
            acp::PO_Selected sel;
            sel.optionId = p.options.front().optionId;
            res.outcome  = sel;
        } else {
            // 拒绝或允许但没有任何选择。
            res.outcome = acp::PO_Cancelled{};
        }
        return res;
    });

    // fs/*→代表。取消设置⇒MethodNotFound，告诉代理不要使用它。
    if (opts_.delegate.read_text_file) {
        r.on<m::FsReadTextFile>([](B& c, const acp::ReadTextFileParams& p) {
            auto& b = c.self;
            std::optional<int> line  = p.line  ? std::optional<int>(static_cast<int>(*p.line))  : std::nullopt;
            std::optional<int> limit = p.limit ? std::optional<int>(static_cast<int>(*p.limit)) : std::nullopt;
            acp::ReadTextFileResult res;
            // 拒绝或丢失的读取是空读取，而不是协议错误。
            if (auto body = b.opts_.delegate.read_text_file(p.path, line, limit))
                res.content = std::move(*body);
            return res;
        });
    }
    if (opts_.delegate.write_text_file) {
        r.on<m::FsWriteTextFile>([](B& c, const acp::WriteTextFileParams& p) {
            auto& b = c.self;
            b.opts_.delegate.write_text_file(p.path, p.content);
            return acp::Unit{};
        });
    }

    // terminal/*→同步执行器。 create() 通过委托运行
    // 命令直至完成，缓存 在新 ID 下运行结果；
    // output/wait_for_exit 读回来； release() 掉落它。
    if (opts_.delegate.run_terminal) {
        r.on<m::TerminalCreate>([](B& c, const acp::CreateTerminalParams& p) {
            auto& b = c.self;
            std::optional<std::string> cwd = p.cwd ? std::optional<std::string>(*p.cwd)
                                                   : std::nullopt;
            auto res = b.opts_.delegate.run_terminal(p.command, p.args, cwd);
            acp::CreateTerminalResult out;
            out.terminalId = b.terminals_.with(c.cap, [](Terminals& t, TerminalState st) {
                std::string tid = "term_" + std::to_string(t.next_id++);
                t.by_id[tid] = std::move(st);
                return tid;
            }, TerminalState{std::move(res.output), res.exit_code, res.truncated});
            return out;
        });
        r.on<m::TerminalOutput>([](B& c, const acp::TerminalOutputParams& p) {
            acp::TerminalOutputResult res;
            const auto term = c.self.terminals_.read(c.cap, [](const Terminals& t, std::string id) {
                auto it = t.by_id.find(id);
                return it == t.by_id.end() ? std::optional<TerminalState>{}
                                           : std::optional<TerminalState>{it->second};
            }, p.terminalId);
            if (term) {
                res.output    = term->output;
                res.truncated = term->truncated;
                acp::TerminalExitStatus st;
                st.exitCode = term->exit_code;
                res.exitStatus = st;   // 命令已经运行完成
            }
            return res;
        });
        r.on<m::TerminalWaitForExit>([](B& c, const acp::TerminalWaitForExitParams& p) {
            acp::TerminalExitStatus st;
            st.exitCode = c.self.terminals_.read(c.cap, [](const Terminals& t, std::string id) {
                auto it = t.by_id.find(id);
                return it == t.by_id.end() ? std::optional<int>{}
                                           : std::optional<int>{it->second.exit_code};
            }, p.terminalId);
            return st;   // 已经退出(同步执行器)
        });
        // 命令已经运行；没有任何信号。
        r.on<m::TerminalKill>([](B&, const acp::TerminalKillParams&) { return acp::Unit{}; });
        r.on<m::TerminalRelease>([](B& c, const acp::TerminalReleaseParams& p) {
            c.self.terminals_.with(c.cap, [](Terminals& t, std::string id) { t.by_id.erase(id); },
                              p.terminalId);
            return acp::Unit{};
        });
    }
    return r;
}

// ── 届会周期 ────────────────────────────────────────────────────────
std::optional<acp::SessionId>
ExternalAcpBackend::ensure_session_(const maya::unlocked& cap, const Request& req,
                                    std::optional<TurnError>& err) {
    (void)req;
    if (opts_.reuse_session) {
        if (auto have = session_.get()) return acp::SessionId{std::move(*have)};
    }
    if (peer_ == nullptr) {
        err = TurnError{"ExternalAcpBackend: connect() not called (no connection bound)"};
        return std::nullopt;
    }

    acp::NewSessionParams np;
    np.cwd = opts_.cwd;
    np.mcpServers = opts_.mcp_servers;

    auto r = peer_->call<acp::to_agent::SessionNew>(cap, np);
    if (!r) {
        err = TurnError{std::string("session/new failed: ") + r.error().what()};
        return std::nullopt;
    }
    if (!opts_.reuse_session) return r->sessionId;
    // 回合一次，但如果有两轮比赛，则第一个获胜。
    const auto kept = session_.update(
        [](const std::optional<std::string>& s, const std::string& id) {
            return s ? s : std::optional<std::string>{id};
        }, r->sessionId.value);
    return acp::SessionId{**kept};
}

// ── 圆 ────────────────────────────────────────────────────────────────
TurnResult ExternalAcpBackend::prompt(const maya::unlocked&       cap,
                                      const Request&              req,
                                      const TurnSink&             sink,
                                      const http::CancelTokenPtr& cancel) {
    // 打开（或重用）会话。
    std::optional<TurnError> sess_err;
    auto sid = ensure_session_(cap, req, sess_err);
    if (!sid) return TurnResult::failed(*sess_err);

    // 发布本轮的接收器，以便入站session/update 通知（在读
    // 取器线程上）到达它。在范围退出时清除 - 较晚的更新将变为 no-op，
    // 而不是第二个 终端 事件。
    {
        auto round = std::make_shared<Round>(util::Callback<void(acp::SessionUpdate)>{
            [&sink](acp::SessionUpdate su) { sink(std::move(su)); }});
        round_.publish(std::move(round));
    }
    struct RoundGuard {
        ExternalAcpBackend*   self;
        const maya::unlocked& cap;   // 提示词的:这个线程什么也没有
        ~RoundGuard() {
            if (auto r = self->round_.take()) (void)r->inflight.stop_and_wait(cap, std::nullopt);
        }
    } round_guard{this, cap};

    acp::PromptParams pp;
    pp.sessionId = *sid;
    pp.prompt    = prompt_blocks_from(req);

    // 等待PromptResult，同时session/update、
    // 数据流到`sink`。 Esc，发送session/cancel：
    // 协作座席在一拍内应答已取消；没有的代理会在 2 秒宽限后在本地失败，
    // 因此无论代理的行为如何，等待都是有限的。回合没有最后期限。
    using namespace std::chrono_literals;
    bool sent_cancel = false;
    rpc::CallOptions o;
    o.timeout = 0ms;
    if (cancel) o.cancel = cancel->token();
    o.on_cancel = [this, &sent_cancel, sid = *sid](const maya::unlocked& c) {
        sent_cancel = true;
        acp::CancelParams cp;
        cp.sessionId = sid;
        peer_->notify<acp::to_agent::SessionCancel>(c, cp);
    };
    o.cancel_grace = 2s;
    auto result = peer_->call<acp::to_agent::SessionPrompt>(cap, pp, std::move(o));

    if (sent_cancel) return TurnResult::cancelled();

    // 终端 事件 — 发出 恰好 一次，这里作为返回值。
    if (!result) {
        // 如果代理在其上游上进行401，则消息携带它-表面为auth_expired，
        // 因此OAuth-refresh 重试路径可以re-run。
        const std::string msg = result.error().what();
        TurnError te{std::string("agent error: ") + msg};
        if (msg.find("401") != std::string::npos || msg.find("403") != std::string::npos ||
            msg.find("unauthor") != std::string::npos || msg.find("Unauthor") != std::string::npos)
            te.auth_expired = true;
        return TurnResult::failed(te);
    }

    // 回合-level StopReason→回合结果。
    switch (result->stopReason) {
        case acp::StopReason::Cancelled:
            return TurnResult::cancelled();
        case acp::StopReason::Refusal:
            return TurnResult::failed(TurnError{"agent refused to continue"});
        default:
            return TurnResult::finished(map_acp_stop_reason(result->stopReason));
    }
}

// ── 子加工厂 ───────────────────────────────────────────────────────
// 使用剩余参数生成 `argv[0]` 作为 ACP 代理，在其 stdio 上
// 运行 rpc::Peer，运行 `initialize`，并返回连接的句柄。
// ::agentty::util::ChildProcess 是 POSIX 上的 fork/exec/pipe 和
// Windows 上的 CreateProcess，将子进程的标准输出暴露为 istream，将其标准输入暴露为
// ostream。
namespace {

// 拥有所产生的孩子。 对端 停止后被拆除：到那时，读取器要么已完成，
// 要么被放弃（它处于 -owns 状态，而不是子级的 数据流，
// 因此子级必须比它可能停驻的读取寿命长——请参阅
// SpawnedAcpAgent::reset 中的顺序）。
struct AgentProcessHolder {
    std::unique_ptr<::agentty::util::ChildProcess> child;
    ~AgentProcessHolder() { if (child) child->terminate(); }
};

} // 命名空间结束。

void SpawnedAcpAgent::reset() noexcept {
    // 首先销毁 对端：~对端 停止它（关闭子进程的标准输入——
    // 合作代理退出——并且中断杀死子进程，以便驻留在其标准
    // 输出上的读者看到 EOF）并加入。只有到那时，
    // 孩子（及其 数据流）才会被销毁。
    connection.reset();
    process.reset();
}

SpawnedAcpAgent spawn_acp_agent(const maya::unlocked&           cap,
                                const std::vector<std::string>& argv,
                                const acp::InitializeParams&    init,
                                rpc::Dispatch                   dispatch,
                                std::string&                    err) {
    if (argv.empty()) {
        err = "spawn_acp_agent: empty argv";
        return {};
    }

    auto holder = std::make_shared<AgentProcessHolder>();
    try {
        ::agentty::util::ChildProcess::Spawn spawn;
        spawn.command = argv.front();
        spawn.args.assign(argv.begin() + 1, argv.end());
        holder->child = std::make_unique<::agentty::util::ChildProcess>(spawn);
    } catch (const std::exception& e) {
        err = std::string("spawn_acp_agent: cannot start '") + argv.front() + "': " + e.what();
        return {};
    }

    // 读取子进程的标准输出，写入其标准输入。停止时：在其标准输入上
    // 礼貌地使用 EOF，这是合作退出的短暂恩典，然后杀死它，
    // 以便其标准输出关闭并唤醒读者。
    auto ch = rpc::stream_channel(holder->child->out(), holder->child->in());
    ch.interrupt = [child = holder->child.get()](const maya::unlocked&) {
        using namespace std::chrono_literals;
        child->close_stdin();
        child->terminate(1s);   // 先是EOF，最多1s退出，然后是源码树
        child->interrupt_output();
    };

    SpawnedAcpAgent out;
    out.process    = holder;
    out.connection = std::make_unique<rpc::Peer>(std::move(ch), std::move(dispatch), "acp-agent");
    out.connection->start();

    // 谈判。握手失败（版本不兼容、代理在启动时死亡、
    // 回复格式错误）是一个生成错误，而不是 half-open 对端。
    auto r = out.connection->call<acp::to_agent::Initialize>(cap, init);
    if (r) {
        try { (void)acp::negotiate_version(acp::kProtocolVersion, r->protocolVersion); }
        catch (const std::exception& e) { r = std::unexpected(acp::RpcError(acp::errc::InternalError, e.what())); }
    }
    if (!r) {
        err = std::string("spawn_acp_agent: initialize failed: ") + r.error().what();
        return {};   // `out`撕毁了对端和孩子
    }
    return out;
}

} // namespace agentty::provider
