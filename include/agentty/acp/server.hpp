#pragma once
// agentty::acp::AgentServer-ACP代理，其允许agentty
// 作为子进程Zed(或任何ACP客户端)通过stdio驱动器运行。
//
// acp-cpp声明方法和类型； rpc::Peer 运行连接。
// 该类通过jsonrpc::Router应答客户端的方法，
// 针对TUI使用的相同提供者+工具+权限策略驱动无头转向循环，
// 并将每个步骤转变为session/update 通知。
//
// session/prompt被推迟：在客户端可以应答之前将数据流更新和调
// 用返回给客户端(session/request_permission，
// terminal/*)，并且这些答复到达读者。因此，读者将回合交给
// 工作线程 和 Peer::Deferred，然后继续；当回合结束时，
// 工作线程 应答。
//
// 生命周期（ACP v1代理表面；通过`initialize`
// 中的匹配能力来通告每个可选方法）：
//   初始化/认证/注销session/new·
//   加载·恢复·列表·关闭·删除session/set_mode·set_config_option
//   session/prompt(
//   异步)·session/cancel(通知)

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <acp/protocol.hpp>

#include <maya/runtime.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/rpc/peer.hpp"
#include "agentty/util/io.hpp"

namespace agentty::acp {

// 提供商调用，类型-erased：（解锁，请求，EventSink）→无效。
// 匹配provider::Provider::stream。从回合 工作线程 开始调用，
// 该回合不持有 锁 并传递其 `unlocked`。
using StreamFn =
    std::function<void(const maya::unlocked&, provider::Request, provider::EventSink)>;
// 数据流到NAMED提供商（"anthropic"、"copilot"、
// 定制主机），以及该提供商自己的凭据。当会话的模型位于活动
// 提供者以外的提供者上时使用。
using StreamToFn =
    std::function<void(const maya::unlocked&, const std::string&, provider::Request,
                       provider::EventSink)>;

// Per-session状态：一个agentty 线程+客户端打开会话所针对的工作区 cwd，
// 加上in-flight轮的取消句柄。 `profile` 是实时 权限 层 (
// session/set_mode)。 `model` 覆盖此会话的服务器默认值
// (session/set_config_option)。
struct Session {
    std::string id;
    std::string cwd;
    std::vector<std::string> extra_roots;   // additionalDirectories 来自编辑
    Thread      thread;
    Profile     profile = Profile::Ask;
    std::string model;   // 空 = 使用服务器默认值
    std::string provider;   // 空=活动提供者；其他路线那里
    std::string effort;     // "" = 默认，"none" = 关闭，否则为 传输 级别
    bool show_reasoning = true;
    std::shared_ptr<http::CancelToken> cancel;
    // 到达顺序中的 提示词（请求 ID，取消 句柄）。
    // 前面的拥有会话；其余的人等待轮到他们。
    std::vector<std::pair<std::string, std::shared_ptr<http::CancelToken>>> queued;
    std::set<std::string> grants;   // session-scoped "always allow"，按工具
    std::set<std::string> denials;  // session-scoped "always reject"，按工具
    bool warned_context = false;    // "context nearly full" 通知已发送
};

// 每个直播会话加上in-flight 提示词地图，在ONE 锁后面。转弯循环在
// 工作线程 上运行，而阅读器 线程 为 load/list/set_mode/
// 取消 提供服务，因此所有这些都是共享的。没有任何东西可以通过网络 数据流
// 或工具运行来保存 锁：回合在 快照 上工作并按 id 写回结果，
// 因此在 -turn 中关闭的会话只会使 write-back 成为 no-op。
struct Sessions {
    std::unordered_map<std::string, Session> by_id;
    // in-flight session/prompt→其会话的JSON-RPC id
    // (id.dump())，因此通用$/cancel_request可以取消右转。
    // 设置在 on_prompt，回合结束时擦除。
    std::unordered_map<std::string, std::string> reqid_to_session;
};

class AgentServer : public std::enable_shared_from_this<AgentServer> {
    // ── 锁 顺序 ────────────────────────────────────────────────────────
    //
    // 一个锁、`sessions_`、一个LEAF：每个正文是会话
    // 映射上的几行，而调用与锁没什么关系。所以：
    //
    //   with_session/仅涉及以下会话的任何内容<leaf>
    //   acquire_turn（等待轮到）、run_turn、
    //   stream_completion、run_tools、ask_permission
    //   （在客户端上等待）、persist/index（文件通道），服务已解锁
    //
    // 请求处理函数在rpc读取器上运行，rpc读取器不持有任何东西，
    // 并通过路由器上下文（Ctx)传递其`unlocked`；转弯和压实在
    // 独立的 工作线程 上运行，其 正文 接收自己的。 auth 头
    // 文件 和 model-choice 缓存 是 快照（根本没有 锁）。
    struct Ctx {
        AgentServer&          self;
        const maya::unlocked& cap;
    };

public:
    // `stream`是提供者入口点（绑定AnthropicProvider::
    // stream或测试替身）。 `auth` 是已解析的 传输 凭据；
    // 可能为空（然后 提示词 报告身份验证-required）。 `model_id`
    // 是新会话的默认模型。
    //
    // 使用 maya::co_owned<AgentServer>::make(...) 构建
    // 它：回合 工作线程 握住它的句柄，因此它必须比任何仍在运行的回合寿命更长。
    //
    // 可选的 接口边界，在构造时固定，因此 工作线程 读取的每个字段都是常量（下面的
    // MAYA_SYNC 声明通过构造成立，而不是通过 "set it before serve()"
    // 成立）。
    struct Extras {
        // 路由其模型存在于另一提供者上的会话。如果没有它，
        // 型号列表就是活动提供商的。
        StreamToFn                        stream_to;
        // 在 终端 登录后，编辑器为我们运行 Re-reads 凭据。
        std::function<auth::AuthHeader()> reauth;
    };
    AgentServer(rpc::Channel      channel,
                StreamFn          stream,
                auth::AuthHeader  auth,
                std::string       model_id,
                Profile           profile = Profile::Ask,
                Extras            extras  = {});
    ~AgentServer();

    // 开始读取并阻塞直到客户端断开连接（标准输入上的EOF）。
    // 返回进程退出代码。等待读者：`unlocked`。
    int serve(const maya::unlocked& cap);

private:
    using Responder = rpc::Peer::Deferred<::acp::to_agent::SessionPrompt>;

    // ── 方法处理函数 ─────────────────────────────────────────────────
    // 全部在rpc读取器线程上运行并接收其`unlocked`。
    using Cap = maya::unlocked;
    ::acp::InitializeResult     on_initialize(const Cap&, const ::acp::InitializeParams&);
    ::acp::NewSessionResult     on_new_session(const Cap&, const ::acp::NewSessionParams&);
    ::acp::ListSessionsResult   on_list_sessions(const Cap&, const ::acp::ListSessionsParams&);
    ::acp::LoadSessionResult    on_load_session(const Cap&, const ::acp::LoadSessionParams&);
    ::acp::ResumeSessionResult  on_resume_session(const Cap&, const ::acp::ResumeSessionParams&);
    ::acp::NewSessionResult     on_fork_session(const Cap&, const ::acp::ForkSessionParams&);
    void                        on_close_session(const Cap&, const ::acp::CloseSessionParams&);
    void                        on_delete_session(const Cap&, const ::acp::DeleteSessionParams&);
    void                        on_set_mode(const Cap&, const ::acp::SetModeParams&);
    ::acp::SetConfigOptionResult on_set_config_option(const Cap&, const ::acp::SetConfigOptionParams&);
    void                        on_logout(const Cap&);
    void                        on_prompt(const Cap&, const ::acp::PromptParams&, Responder);
    void                        on_cancel(const Cap&, const ::acp::CancelParams&);
    // 通用$/cancel_request:取消，
    // 其session/prompt请求具有该JSON-RPC id的回合(对于未知id回退到no-op)。
    void                        on_cancel_request(const Cap&, const ::acp::RpcId& id);

    // 方法表，在构造函数中构建一次。
    using Router = ::jsonrpc::Router<Ctx>;
    static Router make_router();
    rpc::Dispatch make_dispatch();
    std::optional<std::string> on_call(const Cap&, const ::acp::Call& c);
    void on_notification(const Cap&, const ::acp::Notification& n);

    // ── 无头转圈 ───────────────────────────────────────────
    void run_turn(Io io, const Cap& cap, std::string session_id, std::string req_id_dump,
                  Responder resp, Message user_msg, std::string typed);
    void run_compact(Io io, const Cap& cap, std::string session_id, std::string req_id_dump,
                     Responder resp);
    // 轮流排队。 register_prompt 排队归档 提示词；
    // acquire_turn 会阻塞，直到它成为第一个，然后附加其消息
    // （如果有）并武装其 取消。没有返回：已取消 正在等待，或者会话已关闭。
    // release_turn将会话交给下一个提示词。
    struct Acquired { std::shared_ptr<http::CancelToken> cancel; std::string title; };
    void register_prompt(const maya::below<maya::leaf>& cap, const std::string& sid,
                         const std::string& rid);
    std::optional<Acquired> acquire_turn(const Cap& cap, const std::string& sid,
                                         const std::string& rid,
                                         std::optional<Message> msg = {},
                                         std::string typed = {});
    void release_turn(const maya::below<maya::leaf>& cap, const std::string& sid,
                      const std::string& rid);
    // 触发这些取消手柄，然后唤醒在acquire_turn中等待的任何人。
    void cancel_tokens(const maya::below<maya::leaf>& cap,
                       std::vector<std::shared_ptr<http::CancelToken>> toks);
    // 擦除会话和取消其运行+排队轮次（close/delete)。
    void drop_session(const Cap& cap, const std::string& sid);

    StopReason stream_completion(Io io, const Cap& cap, const std::string& session_id,
                                 bool& out_cancelled, std::string& out_error,
                                 bool suppress_tools = false);
    bool       run_tools(const Cap& cap, const std::string& session_id, bool& out_cancelled);

    // ── 帮助者 ──────────────────────────────────────────────────────────
    void send_update(const maya::below<maya::leaf>& cap, const std::string& session_id,
                     ::acp::SessionUpdate update);

    // 发出会话的slash-command菜单+model-picker配置选项，
    // 因此Zed的composer`/`菜单和模型下拉列表与本机代理匹配。
    // 作为 session/update 通知 发送； 调用 在会话 锁 之外。
    void emit_session_config(const Cap& cap, const std::string& session_id,
                             const std::string& model_id);
    // 完整的config-option状态（SSOT用于初始广告+每个post
    // -change回显）。读取实时会话 profile/model.
    void emit_config_state(const Cap& cap, const std::string& session_id,
                           const std::string& fallback_model);
    ::acp::List<::acp::ConfigOption> config_options(const Cap& cap,
                                                     const std::string& session_id,
                                                     const std::string& fallback_model);

    enum class PermissionOutcome { Deny, DenyAlways, AllowOnce, AllowAlways };
    PermissionOutcome ask_permission(const Cap& cap, const std::string& session_id,
                                     const ToolUse& tc,
                                     std::stop_token cancel = {});

    // Live-terminal通过客户端的ACP 终端执行`bash`工具调
    // 用(terminal/create→wait_for_exit→输出→发布)，
    // 因此命令数据流进入Zed的本机终端小部件而不是在内部运行并返回静态文本。
    // 仅当客户端通告 `terminal` 和 agentty 的 沙箱
    // 不是 包装命令时使用（否则内部执行将保留 沙箱 合同）。
    // 发出 InProgress/Completed/Failed
    // SU_ToolCallUpdate 本身（带有 终端 内容块）并设置 tc.status。
    // 返回结果，以便 run_tools 可以兑现 取消； std::nullopt
    // 表示
    // 不满足条件时，回退到内部执行。
    struct TerminalRun { bool ok; bool cancelled; std::string output; };
    // 工具运行从其会话中需要什么，每批复制一次。
    struct TurnCtx {
        std::string id, cwd, msg_id;
        std::shared_ptr<http::CancelToken> cancel;
        std::vector<std::string> extra_roots;
        std::string thread_id;   // 对于 CallContext::conversation
        [[nodiscard]] bool cancelled() const { return cancel && cancel->is_cancelled(); }
    };
    std::optional<TerminalRun> run_bash_via_terminal(const Cap& cap, const TurnCtx& ctx, ToolUse& tc);

    // 解决工具调用：将其设置在本地副本AND上，并将其写回到
    // 实时会话中（通过消息id+工具id匹配），一步完成。
    void settle_tool(const Cap& cap, const TurnCtx& ctx, ToolUse& tc, ToolUse::Status st,
                     std::chrono::steady_clock::time_point executing_since = {});

    // 在会话锁下的实时会话`id`上运行f(Session&，
    // args...)；如果没有这样的会话，则 nullopt。
    // f 是无捕获的，它的 args/result 被复制进复制出，
    // 与 Guarded<T>::with 的规则相同，因为这就是它的本质。
    // 没有任何东西会逃逸到会话中，因此并发关闭永远无法释放读取器下的会话。
    // 会话 锁 是一片叶子：可调用，可容纳除叶子之外的任何内容。
    template <class F, class... Args>
    auto with_session(const maya::below<maya::leaf>& cap, const std::string& id, F f,
                      Args... args) {
        (void)f;
        using R = std::invoke_result_t<F&, Session&, Args&&...>;
        return sessions_.with(cap, [](Sessions& ss, std::string k, Args... a) -> std::optional<R> {
            auto it = ss.by_id.find(k);
            if (it == ss.by_id.end()) return std::nullopt;
            return F{}(it->second, std::move(a)...);
        }, id, std::move(args)...);
    }

    // ── 会话模式 ────────────────────────────────────────────────────
    static ::acp::SessionModeState mode_state(Profile current);
    [[nodiscard]] ::acp::SessionModeState modes_for_client(Profile current) const;
    static Profile        profile_from_mode_id(const std::string& mode_id,
                                               Profile fallback);
    static const char*    mode_id_for(Profile p);

    // ── 惯性会话索引(cwd + 标题侧车) ────────────────────
    // 索引是共享-file车道：它们需要低于<file_lane>。
    void                  index_session(const Cap& cap, const std::string& id);
    void                  unindex_session(const Cap& cap, const std::string& id);
    nlohmann::json        load_session_index(const Cap& cap);

    void                  persist(const Cap& cap, const std::string& id);
    void                  adopt_client_mcp(const Cap& cap, const ::acp::List<::acp::McpServer>& servers);
  public:
    void                  refresh_command_menus(const Cap& cap);
  private:
    void                  replay_history(const Cap& cap, const std::string& session_id,
                                         const Thread& thread);
    void                  replay_history_body(const Cap& cap, const std::string& session_id,
                                              const Thread& thread, const std::string& scwd);

    // 仅限读取器 线程（Router::handle 不是 const）。
    Router                           router_;
    // 在构造函数中设置，则只能读取；把 工作线程 也读一遍。
    const std::unique_ptr<rpc::Peer> peer_;
    const StreamFn                   stream_;
    // 可选：路由其模型存在于另一提供商（Extras::stream_to)
    // 上的会话。
    const StreamToFn                 stream_to_;
    // 会话选择器跨签名的-in提供者提供的每个模型。
    // 首次使用时延迟构建并保留连接：列出 目录 可以
    // 访问网络，并且下拉菜单不应等待两次。
    // 已发布的插槽：在任何 锁 外部构建，整体交换。
    struct ModelChoice { std::string value, provider, model, name, group, group_name; };
    maya::published<const std::vector<ModelChoice>> model_choices_;
    [[nodiscard]] std::vector<ModelChoice> model_choices(const Cap& cap);
    [[nodiscard]] ::acp::List<::acp::ConfigOption> model_options(const Cap& cap,
                                                                 const std::string& provider,
                                                                 const std::string& model,
                                                                 const std::string& effort,
                                                                 bool show_reasoning);
    const std::string                model_id_;
    const Profile                    profile_;
    // on_logout(读取器线程)将其清除，而轮到可能读取它：一个快照，
    // 整个替换，读取锁-free。
    maya::snapshot<auth::AuthHeader> auth_;
    [[nodiscard]] auth::AuthHeader auth() const { return auth_.get(); }
    // Re-reads 凭据 (Extras::reauth);
    // 没有它`authenticate`只能报告状态。
    const std::function<auth::AuthHeader()> reauth_;
    void on_authenticate(const ::acp::AuthenticateParams&);

    // 客户端是否在初始化时通告`terminal`支持？当 true 时（
    // agentty 的 沙箱 不是 包装命令），`bash` 工具 调用 通过客户
    // 端的 ACP 终端（terminal/create → 等待 → 输出）而不是在
    // 内部执行，因此 Zed 渲染 是其本机 流式传输 终端 小部件 - 与
    // UX 相同克劳德-code-acp。在 on_initialize 中设置
    // 一次（读取器 线程，在任何 提示词 之前），之后读取 -only。
    std::atomic<bool>         client_supports_terminal_{false};

    // clientCapabilities.session (v1.11)，
    // 在on_initialize中设置一次。
    // 该规范禁止向未公布它们的客户端通知/压缩更新和布尔配置选项。
    std::atomic<bool>         client_notices_{false};
    std::atomic<bool>         client_compaction_{false};
    std::atomic<bool>         client_bool_config_{false};
    // clientCapabilities._meta约定(Zed，JetBrains)。
    std::atomic<bool>         client_terminal_meta_{false};    // terminal_output
    std::atomic<bool>         client_terminal_delta_{false};   // terminal_output_delta（追加）
    std::atomic<bool>         client_terminal_auth_{false};    // 终端-auth登录方法
    // JetBrains AIR (clientCapabilities._meta.jetbrains.air)。
    // air_ = 客户根本就是AIR；其余的是它所声明的功能。
    std::atomic<bool>         air_{false};
    std::atomic<bool>         air_diff_patch_{false};
    std::atomic<bool>         client_elicit_form_{false};      // elicitation.form
    std::atomic<bool>         client_elicit_url_{false};       // elicitation.url
    nlohmann::json elicit_via_client(const Cap& cap, const std::string& session_id, const std::string& tool_id,
                                     const nlohmann::json& mcp_params,
                                     std::stop_token cancel = {});
    // AIR 为 {"jetbrains":{"air":{"version":1, ...载荷}}}，其他为 {}。
    [[nodiscard]] nlohmann::json air_meta(nlohmann::json payload) const;
    [[nodiscard]] nlohmann::json air_tool_meta(const ToolUse& tc) const;

    // 用户-facing通知：当客户端渲染他们更新`notice`时，
    // 否则什么都没有（轮到自己的文本已经说明发生了什么）。
    void notice(const maya::below<maya::leaf>& cap, const std::string& session_id, std::string severity,
                std::string title, std::string description = {});

    // Zed 的 终端 频道，用于我们自己运行的 shell 工具。
    [[nodiscard]] bool terminal_channel() const noexcept;
    [[nodiscard]] static bool is_command_tool(std::string_view name);
    void send_terminal_output(const maya::below<maya::leaf>& cap, const std::string& session_id, const std::string& tool_id,
                              std::string_view data);
    [[nodiscard]] static nlohmann::json terminal_exit_meta(const std::string& tool_id,
                                                           std::string_view text, bool failed);

    // 在初始化时商定的ACP协议版本（我们的+他们的最小值）。
    // v1-vs-v2发射的clean-cut 接口边界：会话的配置表面以每个连接的
    // ONE方式呈现——v1客户端获得SessionModeState，
    // v2+客户端获得`mode`配置选项——决不能两者兼而有之。
    // 在 on_initialize 中设置一次（读取器 线程，在任何会话之前），此后读取 -only。
    std::atomic<int>          negotiated_version_{::acp::kProtocolVersion};
    [[nodiscard]] bool v2_config() const noexcept {
        return negotiated_version_.load(std::memory_order_relaxed) >= 2;
    }


    maya::guarded<Sessions>   sessions_;
};

} // namespace agentty::acp

// 手动检查（jaal无法遍历具有私有字段和基数的类）：
// 每个字段都是const、受保护的、snapshot/published或原子的，
// 除了router_，只有对端的读取器线程接触到它。转动工作线程和读写器通过
// co_owned<AgentServer>共享服务器。
MAYA_SYNC(agentty::acp::AgentServer);
