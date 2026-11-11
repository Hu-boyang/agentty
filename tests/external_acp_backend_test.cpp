// external_acp_backend_test - 电话 ExternalAcpBackend 核心,
// 驱动对内 -memory FAKE ACP 探员
//
// 机能:假代理机是自己的rpc::Peer,传输连接到后端的对端到rpc::
// channel_pair(20). 我们驱动prompt()端
// 端端没有子进程或沙箱.
//
// 覆盖范围:
//   1. 清转:代理数据流消息+思考+tool_call+使用
//      更新,然后返回 StopReason::EndTurn 我们坚持 SessionUpdate
//      到达 TurnSink 依次而行,圆圆成 EndTurn (确切的说,
//      一个) 终端 事件 ——回报值,从不虚构. "cancelled").
//   2. tool_use 映射: 代理返回 MaxTurnRequests ⁇   ⁇ EndTurn (无)
//      代理的转弯盖不是agentty的圆形理由.
//   3. 拒绝 ______________________ TurnError (回合失败,没有) 已取消).
//   4. 已取消 停止理由 – TurnResult::cancelled().
//   5. 用户 Esc (取消 ——我们发送 session/cancel 和解后, 已取消.
//   6. map_acp_stop_reason 纯 -function 表格中显示的表格。

#include "agentty/provider/external_acp_backend.hpp"

#include <acp/acp.hpp>
#include <acp/protocol.hpp>

#include "agentty/rpc/peer.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // set_workspace_root
#include "agtest_cap.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>   // 发作
#endif

using namespace acp;
namespace P   = agentty::provider;
namespace rpc = agentty::rpc;

namespace {

// 假的特工应该怎么结束? 提示词 ,是否为 数据流 先更新。
struct AgentScript {
    StopReason stop = StopReason::EndTurn;
    bool       stream_updates = false;   // 发送消息+ 想法+ tool_call +使用量
    // 封锁 提示词 一直持续到 session/cancel 到了,然后回答,我问 已取消.
    bool       block_until_released = false;
    // A BUGGY 代理: 接受 提示词 永远不回答,
    // 忽略 session/cancel. prompt() 仍须返回, 范围。
    bool       ignore_cancel = false;
    // 调用 返回客户端时 提示词: fs/write, fs/read,
    // terminal/create+output+release.
    bool       exercise_callbacks = false;
    // 回调 成果,由 提示词 工作线程.
    std::string read_back;
    std::string term_output;
    int         term_exit = -1;
};

// 假代理:自有对端,在频道对对的远端答话.
struct FakeAgent {
    AgentScript script;
    std::unique_ptr<rpc::Peer> peer;
    acp::List<acp::ContentBlock> last_prompt;

    std::mutex              mu;
    std::condition_variable cv;
    bool                    released = false;
    std::vector<std::thread> workers;

    void release() {
        { std::lock_guard lk(mu); released = true; }
        cv.notify_all();
    }

    ~FakeAgent() {
        release();
        if (peer) peer->stop(agtest::cap());
        for (auto& t : workers) if (t.joinable()) t.join();
    }

    template <jsonrpc::IsMethod M>
    typename M::result call(const typename M::params& p) {
        auto r = peer->call<M>(agtest::cap(), p);
        if (!r) throw r.error();
        return std::move(*r);
    }

    void send_update(const SessionId& sid, SessionUpdate u) {
        SessionUpdateMsg m; m.sessionId = sid; m.update = std::move(u);
        peer->notify<to_client::SessionUpdate>(agtest::cap(), m);
    }

    // 运行在工作线程上,所以代理的读者可以自由回信.
    void run_prompt(Id id, PromptParams p) {
        using Prompt = to_agent::SessionPrompt;
        if (script.ignore_cancel) return;   // 永远不回答
        if (script.block_until_released) {
            std::unique_lock lk(mu);
            cv.wait(lk, [&] { return released; });
            lk.unlock();
            peer->reply<Prompt>(agtest::cap(), id, PromptResult{StopReason::Cancelled});
            return;
        }
        if (script.exercise_callbacks) {
            const auto& sid = p.sessionId;
            WriteTextFileParams wp;
            wp.sessionId = sid; wp.path = "acp_probe.txt"; wp.content = "delegate-wrote-this\n";
            (void)call<to_client::FsWriteTextFile>(wp);
            ReadTextFileParams rp;
            rp.sessionId = sid; rp.path = "acp_probe.txt";
            script.read_back = call<to_client::FsReadTextFile>(rp).content;
            CreateTerminalParams cp;
            cp.sessionId = sid; cp.command = "printf"; cp.args = {"acp-term-ok"};
            auto term = call<to_client::TerminalCreate>(cp);
            TerminalRef ref; ref.sessionId = sid; ref.terminalId = term.terminalId;
            auto out = call<to_client::TerminalOutput>(ref);
            script.term_output = out.output;
            if (out.exitStatus && out.exitStatus->exitCode)
                script.term_exit = static_cast<int>(*out.exitStatus->exitCode);
            (void)call<to_client::TerminalRelease>(ref);
            peer->reply<Prompt>(agtest::cap(), id, PromptResult{StopReason::EndTurn});
            return;
        }
        if (script.stream_updates) {
            send_update(p.sessionId, SU_AgentMessageChunk{TextContent{"hello", Nothing, Json::object()}, Nothing});
            send_update(p.sessionId, SU_AgentThoughtChunk{TextContent{"thinking", Nothing, Json::object()}, Nothing});
            ToolCall tc;
            tc.toolCallId = ToolCallId{std::string("tc_1")};
            tc.title      = "shell";
            send_update(p.sessionId, SU_ToolCall{tc});
            send_update(p.sessionId, SU_Usage{/*使用过的*/123, /*大小*/200000, Nothing});
        }
        peer->reply<Prompt>(agtest::cap(), id, PromptResult{script.stop});
    }

    rpc::Dispatch dispatch() {
        rpc::Dispatch d;
        d.on_call = [this](const maya::unlocked&, const Call& c) -> std::optional<std::string> {
            if (c.method == to_agent::Initialize::name) {
                InitializeResult r;
                r.agentCapabilities.promptCapabilities.embeddedContext = true;
                return reply<to_agent::Initialize>(c.id, r);
            }
            if (c.method == to_agent::SessionNew::name) {
                return reply<to_agent::SessionNew>(c.id,
                    NewSessionResult{SessionId{std::string("sess_fake")}, Nothing, Nothing, Json::object()});
            }
            if (c.method == to_agent::SessionPrompt::name) {
                auto p = jsonrpc::from_json<PromptParams>(c.params);
                last_prompt = p.prompt;
                workers.emplace_back([this, id = c.id, p = std::move(p)]() mutable {
                    run_prompt(std::move(id), std::move(p));
                });
                return std::nullopt;
            }
            return reply(c.id, std::unexpected(RpcError(errc::MethodNotFound, c.method)));
        };
        d.on_notification = [this](const maya::unlocked&, const Notification& n) {
            if (n.method == to_agent::SessionCancel::name && !script.ignore_cancel) release();
        };
        return d;
    }
};

// 运行的假-agent + 后端对。 销毁以拆卸。
struct Harness {
    std::unique_ptr<P::ExternalAcpBackend> backend;
    std::unique_ptr<rpc::Peer>             client;   // agentty 侧
    std::unique_ptr<FakeAgent>             agent;

    const acp::List<acp::ContentBlock>& last_prompt() const { return agent->last_prompt; }
    AgentScript& script() { return agent->script; }
    ~Harness() {
        // 客户 对端 首份报告: 分派 后端的点。
        if (client) client->stop(agtest::cap());
        agent.reset();
        client.reset();
        backend.reset();
    }
};

std::unique_ptr<Harness> make_harness(AgentScript script,
                                     P::AcpClientDelegate delegate = {}) {
    auto h = std::make_unique<Harness>();
    auto [ours, theirs] = rpc::channel_pair();

    h->agent = std::make_unique<FakeAgent>();
    h->agent->script = std::move(script);
    h->agent->peer = std::make_unique<rpc::Peer>(std::move(theirs), h->agent->dispatch(), "fake-agent");
    h->agent->peer->start();

    P::ExternalAcpOptions opts;
    opts.cwd = "/tmp/proj";
    opts.reuse_session = true;
    opts.delegate = std::move(delegate);
    h->backend = std::make_unique<P::ExternalAcpBackend>(std::move(opts));

    h->client = std::make_unique<rpc::Peer>(std::move(ours), h->backend->make_dispatch(), "client");
    h->client->start();
    h->backend->connect(*h->client);

    InitializeParams ip;
    ip.clientCapabilities.fs.readTextFile = true;
    (void)h->client->call<to_agent::Initialize>(agtest::cap(), ip);
    return h;
}

P::Request make_req(const char* text) {
    P::Request r;
    agentty::Message m;
    m.role = agentty::Role::User;
    m.text = text;
    r.messages.push_back(std::move(m));
    return r;
}

int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::cerr << "CHECK failed: " #cond " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

void test_clean_turn_streams_and_settles() {
    auto h = make_harness(AgentScript{StopReason::EndTurn, /*stream*/true, false});

    std::vector<std::string> kinds;
    P::TurnSink sink;
    sink.update = [&](acp::SessionUpdate su) {
        if (std::holds_alternative<SU_AgentMessageChunk>(su)) kinds.push_back("message");
        else if (std::holds_alternative<SU_AgentThoughtChunk>(su)) kinds.push_back("thought");
        else if (std::holds_alternative<SU_ToolCall>(su))         kinds.push_back("tool_call");
        else if (std::holds_alternative<SU_Usage>(su))            kinds.push_back("usage");
        else kinds.push_back("other");
    };

    auto res = h->backend->prompt(agtest::cap(), make_req("Hi!"), sink, /*cancel*/nullptr);

    CHECK(res.ok());
    CHECK(res.stop == agentty::StopReason::EndTurn);
    CHECK(h->backend->session_id() == "sess_fake");
    // 四个更新型都来了,按顺序排列.
    CHECK(kinds.size() == 4);
    if (kinds.size() == 4) {
        CHECK(kinds[0] == "message");
        CHECK(kinds[1] == "thought");
        CHECK(kinds[2] == "tool_call");
        CHECK(kinds[3] == "usage");
    }
}

void test_prompt_forwards_text_and_image_content() {
    auto h = make_harness(AgentScript{StopReason::EndTurn, false, false});
    auto req = make_req("look at this");
    req.messages.back().images.push_back(
        agentty::ImageContent{"image/jpeg", std::string{"\x01\x02\x03", 3}});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};

    auto res = h->backend->prompt(agtest::cap(), req, sink, nullptr);

    CHECK(res.ok());
    CHECK(h->last_prompt().size() == 2);
    if (h->last_prompt().size() == 2) {
        const auto* text = std::get_if<acp::TextContent>(&h->last_prompt()[0]);
        const auto* image = std::get_if<acp::ImageContent>(&h->last_prompt()[1]);
        CHECK(text && text->text == "look at this");
        CHECK(image && image->mimeType == "image/jpeg");
        CHECK(image && image->data == "AQID");
    }
}

void test_prompt_forwards_image_only_turn() {
    auto h = make_harness(AgentScript{StopReason::EndTurn, false, false});
    auto req = make_req("");
    req.messages.back().images.push_back(
        agentty::ImageContent{"image/png", std::string{"PNG", 3}});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};

    auto res = h->backend->prompt(agtest::cap(), req, sink, nullptr);

    CHECK(res.ok());
    CHECK(h->last_prompt().size() == 1);
    if (h->last_prompt().size() == 1) {
        const auto* image = std::get_if<acp::ImageContent>(&h->last_prompt()[0]);
        CHECK(image && image->mimeType == "image/png");
        CHECK(image && image->data == "UE5H");
    }
}

void test_max_turn_requests_maps_to_endturn() {
    auto h = make_harness(AgentScript{StopReason::MaxTurnRequests, false, false});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};
    auto res = h->backend->prompt(agtest::cap(), make_req("go"), sink, nullptr);
    CHECK(res.ok());
    CHECK(res.stop == agentty::StopReason::EndTurn);
}

void test_refusal_is_error() {
    auto h = make_harness(AgentScript{StopReason::Refusal, false, false});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};
    auto res = h->backend->prompt(agtest::cap(), make_req("do bad thing"), sink, nullptr);
    CHECK(!res.ok());
    CHECK(res.error.has_value());
    CHECK(!res.error->user_cancel);
}

void test_agent_cancelled_stop_reason() {
    auto h = make_harness(AgentScript{StopReason::Cancelled, false, false});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};
    auto res = h->backend->prompt(agtest::cap(), make_req("x"), sink, nullptr);
    CHECK(!res.ok());
    CHECK(res.error.has_value() && res.error->user_cancel);
}

void test_user_cancel_via_token() {
    auto h = make_harness(AgentScript{StopReason::EndTurn, false, /*块*/true});
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};

    auto cancel = std::make_shared<agentty::http::CancelToken>();
    // 从另一个方向标出一个符号 线程 不久之后 prompt() 开始屏蔽。
    std::thread tripper([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        cancel->cancel();
    });

    auto res = h->backend->prompt(agtest::cap(), make_req("long task"), sink, cancel);
    tripper.join();

    CHECK(!res.ok());
    CHECK(res.error.has_value() && res.error->user_cancel);
}

// A BUGGY 从未解决过 已取消 提示词 没有后端的引擎
// -level 逃生舱门, prompt() 永远挡在黑暗中
// -join 我们断言它仍然返回 已取消 ,以墙为界 -clock.
void test_cancel_against_non_settling_agent_is_bounded() {
    AgentScript script;
    script.ignore_cancel = true;   // 自动同步 提示词 泄漏应答者; 取消 是一个不 -op
    auto h = make_harness(script);
    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};

    auto cancel = std::make_shared<agentty::http::CancelToken>();
    std::thread tripper([&]{
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        cancel->cancel();
    });

    auto t0 = std::chrono::steady_clock::now();
    auto res = h->backend->prompt(agtest::cap(), make_req("never answered"), sink, cancel);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    tripper.join();

    // 双胞胎 取消 最终期限 + 逃生舱门必须已经设定了界限。 CI.
    CHECK(ms < 6000);
    if (ms >= 6000)
        std::cerr << "cancel-wedge prompt took " << ms << "ms (escape hatch regressed?)\n";
    CHECK(!res.ok());
    CHECK(res.error.has_value() && res.error->user_cancel);
}

void test_map_stop_reason_pure() {
    CHECK(P::map_acp_stop_reason(StopReason::EndTurn)         == agentty::StopReason::EndTurn);
    CHECK(P::map_acp_stop_reason(StopReason::MaxTokens)       == agentty::StopReason::MaxTokens);
    CHECK(P::map_acp_stop_reason(StopReason::MaxTurnRequests) == agentty::StopReason::EndTurn);
}

// 探员,中场 -prompt, 调用 fs/write → fs/read →
// terminal/* 返回客户端; a STUB 代表记录下来,验证一下 处理函数 线线端至尾端.
void test_delegate_callbacks_wired() {
    // 内 -memory 代表书记
    auto files = std::make_shared<std::map<std::string, std::string>>();
    P::AcpClientDelegate del;
    del.write_text_file = [files](const std::string& path, const std::string& content) {
        (*files)[path] = content; return true;
    };
    del.read_text_file = [files](const std::string& path, std::optional<int>, std::optional<int>)
            -> std::optional<std::string> {
        auto it = files->find(path);
        return it == files->end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    del.run_terminal = [](const std::string& command, const std::vector<std::string>& args,
                          const std::optional<std::string>&) {
        P::AcpClientDelegate::TerminalResult r;
        r.output = command;
        for (const auto& a : args) r.output += " " + a;
        r.exit_code = 0;
        return r;
    };

    AgentScript script;
    script.exercise_callbacks = true;
    auto h = make_harness(script, std::move(del));

    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};
    auto res = h->backend->prompt(agtest::cap(), make_req("use tools"), sink, nullptr);

    CHECK(res.ok());
    CHECK((*files)["acp_probe.txt"] == "delegate-wrote-this\n");
    CHECK(h->script().read_back == "delegate-wrote-this\n");
    CHECK(h->script().term_output == "printf acp-term-ok");
}

// 真实 default_sandbox_delegate()对一个临时工作区:
// 一个写在工作区内部的磁盘上,一个读数把它拿回来,
// 一个出-of-workspace写字被拒绝.
void test_default_sandbox_delegate_roundtrip() {
    namespace fs = std::filesystem;
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() /
                ("acp_ws_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);

    auto del = P::default_sandbox_delegate();
    CHECK(static_cast<bool>(del.read_text_file));
    CHECK(static_cast<bool>(del.write_text_file));
    CHECK(static_cast<bool>(del.run_terminal));

    // 内 -workspace 写入 + 读取圆形 -trip。该词 ACP fs
    // 协议用途 ABSOLUTE 路径(即代理人发送的路径),所以以同样的方式锻炼代表.
    const std::string abs_hello = (root / "hello.txt").string();
    CHECK(del.write_text_file(abs_hello, "real-delegate\n") == true);
    CHECK(fs::exists(root / "hello.txt"));
    auto body = del.read_text_file(abs_hello, std::nullopt, std::nullopt);
    CHECK(body.has_value());
    if (body) CHECK(*body == "real-delegate\n");

    // Out-of-workspace 写字被写门拒绝.
    CHECK(del.write_text_file("/etc/acp_should_not_write", "nope") == false);

    fs::remove_all(root);
}

// ── 自己 -spawn AGENT MODE ────────────────────────────────────────────────────
// 援引为 `<this-binary> --acp-agent` 运行 真实 ACP
// 代理结束 stdin/stdout (rpc::fd_channel。
// 这是子进程 spawn_acp_agent 发射情况: -process 圆形 -trip
// 外部不依赖 Claude 的 -agent-acp / 代码 -acp 正在安装。
//
// 它回答初始化和 session/new, 数据流 单曲 "pong" 块数( E)
// 提示词 ,然后结束转弯。 `session` 名称,它所发出的会话。
std::unique_ptr<rpc::Peer> start_stdio_agent(std::string session, std::atomic<bool>& answered) {
    // 电话 分派 通过《公约》第2条 对端 属于它, 紧接着。
    auto self = std::make_shared<rpc::Peer*>(nullptr);
    rpc::Dispatch d;
    d.on_call = [self, session, &answered](const maya::unlocked&, const Call& c) -> std::optional<std::string> {
        if (c.method == to_agent::Initialize::name) {
            InitializeResult r;
            r.agentCapabilities.promptCapabilities.embeddedContext = true;
            return reply<to_agent::Initialize>(c.id, r);
        }
        if (c.method == to_agent::SessionNew::name)
            return reply<to_agent::SessionNew>(c.id,
                NewSessionResult{SessionId{session}, Nothing, Nothing, Json::object()});
        if (c.method == to_agent::SessionPrompt::name) {
            auto pp = jsonrpc::from_json<PromptParams>(c.params);
            SessionUpdateMsg m; m.sessionId = pp.sessionId;
            m.update = SU_AgentMessageChunk{TextContent{"pong", Nothing, Json::object()}, Nothing};
            (*self)->notify<to_client::SessionUpdate>(agtest::cap(), m);
            answered.store(true);
            return reply<to_agent::SessionPrompt>(c.id, PromptResult{StopReason::EndTurn});
        }
        return reply(c.id, std::unexpected(RpcError(errc::MethodNotFound, c.method)));
    };
    auto peer = std::make_unique<rpc::Peer>(rpc::fd_channel(0, 1), std::move(d), "stdio-agent");
    *self = peer.get();
    return peer;
}

int run_agent_mode() {
    std::atomic<bool> answered{false};
    auto peer = start_stdio_agent("e2e", answered);
    peer->start();
    peer->wait_closed(agtest::cap());   // 父母关上我们的臭味
    peer->stop(agtest::cap());
    return 0;
}

// ── 自己 -spawn WEDGED AGENT MODE ─────────────────────────────────────────────
// 援引为 `<this-binary> --acp-agent-wedged`
// 故意这样做 MISBEHAVES :它回答第一个 提示词 ,
// 则 IGNORES 标准 EOF 完全旋转,永远不离开。
// 一个只关闭我们的臭味,等待我们臭味结束的家长会挂起;产卵剂的撕裂必须杀死我们。
// 如果这倒退,下面的e2e测试将结束。 HANGS.
int run_wedged_agent_mode() {
    std::atomic<bool> answered{false};
    auto peer = start_stdio_agent("wedged", answered);
    peer->start();
    // 永远不要离开Stdin EOF: 永远旋转。只有外部杀戮才能阻止我们。
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

// ── End-to-end: spawn_acp_agent 驱动 a 真实 子进程 ────────────────────
void test_spawn_real_subprocess(const std::string& self_path) {
    // 建立后端先,得到其分派,产物与之,
    // 然后捆绑——有文件记载的生产流量.
    P::ExternalAcpOptions opts;
    opts.cwd = "/tmp";
    auto backend = std::make_unique<P::ExternalAcpBackend>(std::move(opts));

    std::vector<std::string> captured;
    P::TurnSink sink;
    sink.update = [&](acp::SessionUpdate su) {
        if (auto* mc = std::get_if<SU_AgentMessageChunk>(&su))
            if (auto* t = std::get_if<TextContent>(&mc->content)) captured.push_back(t->text);
    };

    InitializeParams init;
    std::string err;
    auto agent = P::spawn_acp_agent(agtest::cap(),
        // 被困在了内地 agentty_standalone_tests (argv) 语句
        // -dispatched 因此,儿童必须携带 分派 名字在孩子之前 -mode 标志:
        //   <exe> external_acp_backend_test --acp-agent  ⁇
        {self_path, "external_acp_backend_test", "--acp-agent"}, init,
                                    backend->make_dispatch(), err);
    if (!agent.ok()) {
        std::cerr << "spawn_acp_agent failed: " << err << "\n";
        ++failures;
        return;
    }
    backend->connect(*agent.connection);

    auto res = backend->prompt(agtest::cap(), make_req("ping"), sink, nullptr);
    CHECK(res.ok());
    CHECK(res.stop == agentty::StopReason::EndTurn);
    CHECK(backend->session_id() == "e2e");
    CHECK(captured.size() == 1);
    if (captured.size() == 1) CHECK(captured[0] == "pong");
    // 正在丢弃 `agent` 这儿,泪下 对端 和孩子,我们一起生活
}

// 小孩不理会小事 EOF 永远旋转 撕裂产物的药剂 不 吊: 持有者
// Dtor 的 看门狗 部队 -kills 我们断言,
// 拆解在几秒钟内就完成得非常顺利(dtor的优雅是~1s + 收获)。
void test_wedged_child_teardown_is_bounded(const std::string& self_path) {
    P::ExternalAcpOptions opts;
    opts.cwd = "/tmp";
    auto backend = std::make_unique<P::ExternalAcpBackend>(std::move(opts));

    P::TurnSink sink; sink.update = [](acp::SessionUpdate){};
    InitializeParams init;
    std::string err;
    auto agent = P::spawn_acp_agent(agtest::cap(),
        {self_path, "external_acp_backend_test", "--acp-agent-wedged"}, init,
                                    backend->make_dispatch(), err);
    if (!agent.ok()) {
        std::cerr << "spawn wedged agent failed: " << err << "\n";
        ++failures;
        return;
    }
    backend->connect(*agent.connection);

    // 整圈干净,让孩子全程直播 读者停在车旁
    auto res = backend->prompt(agtest::cap(), make_req("ping"), sink, nullptr);
    CHECK(res.ok());

    // 现在拆掉,孩子会的 IGNORE 脚踏实地
    // -EOF 时间到来,必须限制它。
    auto t0 = std::chrono::steady_clock::now();
    // 电话 对端 首份报告: 分派 后端的点。
    agent.reset();
    backend.reset();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // 格蕾丝是~1s;给慷慨的 头室为~ CI。
    // 回归旧的 "close_stdin + join" 将无限期地封锁这里(
    // 被测试所捕获) 超时 即使没有这个束缚。
    CHECK(ms < 5000);
    if (ms >= 5000)
        std::cerr << "wedged-child teardown took " << ms << "ms (watchdog regressed?)\n";
}

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--acp-agent")
        return run_agent_mode();
    if (argc > 1 && std::string(argv[1]) == "--acp-agent-wedged")
        return run_wedged_agent_mode();

    test_clean_turn_streams_and_settles();
    test_prompt_forwards_text_and_image_content();
    test_prompt_forwards_image_only_turn();
    test_max_turn_requests_maps_to_endturn();
    test_refusal_is_error();
    test_agent_cancelled_stop_reason();
    test_user_cancel_via_token();
    test_cancel_against_non_settling_agent_is_bounded();
    test_map_stop_reason_pure();
    test_delegate_callbacks_wired();
    test_default_sandbox_delegate_roundtrip();
    test_spawn_real_subprocess(argv[0]);
    test_wedged_child_teardown_is_bounded(argv[0]);

    if (failures == 0) {
        std::cout << "external_acp_backend_test OK\n";
        return 0;
    }
    std::cerr << "external_acp_backend_test FAILED (" << failures << " check(s))\n";
    return 1;
}
