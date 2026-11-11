// acp_integration_test.cpp 结束 -to-end 行使 agentty's ACP 探员
//
// 驱动一个真正的agentty::acp::AgentServer(建于acp-cpp),
// 通过一个全程协议生命周期,周期为-memory传输层,拥有SCRIPTED供应商,
// 因此不需要network/auth。
//
//   • 初始化广告,宣传V1剂表面
//   • session/new 薄荷带模式的会话
//   • session/prompt 开一转 数据流 a 数据为: agent_message_chunk + a
//     tool_call ,请求 权限 (推迟) -response
//     + 出入境 回调 路径 DEADLOCK 屏蔽( B) 处理函数 ),
//     运行工具,反馈结果, 数据流 最终块,并解决 end_turn
//   • `write`工具实际编写文件
//   • session/set_mode + session/cancel + session/close 圆形 -trip
//
// 退出代码 0 = 所有持有的断言。

#include <acp/acp.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <vector>

#include <unistd.h>
#if defined(_WIN32)
#include <fcntl.h>   // _O_BINARY
#include <io.h>      // 管道(_P)
#endif

#include "agtest.hpp"

#include "agentty/acp/server.hpp"
#include "agentty/rpc/peer.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

using namespace acp;
namespace ag = agentty;

TEST_CASE("acp integration end-to-end") {
    namespace fs = std::filesystem;
    // 写作工具的 沙箱 拒绝在外的路径 工作区 根节。
    // 将根指向我们拥有的 tmp dir , 这样脚本 `write` 成功。
    // PID-unique 故套房在平行试验下保持隐形 -j).
    const fs::path tmp = fs::temp_directory_path() /
                         ("agentty_acp_it_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    // 沙箱 持久化 也为: AgentServer::persist () 写每转
    // persistence::threads_dir() = $HOME
    // /.agentty/threads. 不重定向 HOME ,每个测试运行 保存
    // 脚本 "please write the file" 线程 (加) 减
    // acp_sessions.json 条目) DEVELOPER 是真的,
    // 我爱她 线程 历史——骑自行车时他们出现 线程 真正的应用。 data_dir (a)
    // 调用 -reads 每个洞 调用 ,所以设置在这里(在任何 持久化 触摸)已经足够.
    setenv("HOME", tmp.string().c_str(), 1);
    setenv("USERPROFILE", tmp.string().c_str(), 1);   // win32 分支 data_dir
    ag::tools::util::set_workspace_root(::agentty::IoAccess::grant(), tmp);
    const fs::path target = tmp / "out.txt";
    fs::remove(target);

    // ── 脚本提供者 ──────────────────────────────────────────────────
    std::atomic<int> completions{0};
    std::atomic<bool> read_mode{false};
    std::atomic<bool> bash_mode{false};
    std::atomic<bool> parallel_mode{false};
    // 或 MODEL 上次转弯时实际收到。
    // ACP 图像块只有在它一直存活到请求上才有用——
    // 宣布能力,然后放下 字节 是因为这个守卫的失败
    std::atomic<int> saw_images{0};
    std::string      saw_media_type;
    std::string      saw_bytes;
    std::mutex       saw_mu;
    auto stream = [&](const maya::unlocked&, ag::provider::Request req, ag::provider::EventSink sink) {
        int n = completions.fetch_add(1);
        {
            std::lock_guard lk(saw_mu);
            for (const auto& m : req.messages) {
                if (m.images.empty()) continue;
                saw_images.store(static_cast<int>(m.images.size()));
                saw_media_type = m.images.front().media_type;
                saw_bytes      = m.images.front().bytes();
            }
        }
        if (parallel_mode.load() && n % 2 == 0) {
            // #116:三个写在一个回应中,三角洲互换,每个 End
            // 转弯后(如何) OpenAI 聊天 数据流 平行 调用).
            // 每一个 调用 必须保持自己的动力。
            sink(ag::Msg{ag::StreamStarted{}});
            const std::string base = "tc_par_" + std::to_string(n) + "_";
            const ag::ToolCallId ids[3] = {ag::ToolCallId{base + "a"}, ag::ToolCallId{base + "b"},
                                           ag::ToolCallId{base + "c"}};
            for (const auto& id : ids)
                sink(ag::Msg{ag::StreamToolUseStart{id, ag::ToolName{"write"}}});
            const char* names[3] = {"par_a.txt", "par_b.txt", "par_c.txt"};
            for (int i = 0; i < 3; ++i)
                sink(ag::Msg{ag::StreamToolUseDelta{ids[i], std::string("{\"path\":\"") + names[i] + "\","}});
            for (int i = 2; i >= 0; --i)
                sink(ag::Msg{ag::StreamToolUseDelta{ids[i], std::string("\"content\":\"from ") + names[i] + "\"}"}});
            for (const auto& id : ids)
                sink(ag::Msg{ag::StreamToolUseEnd{id}});
            sink(ag::Msg{ag::StreamUsage{1200, 40, 0, 0}});
            sink(ag::Msg{ag::StreamFinished{ag::StopReason::ToolUse}});
            return;
        }
        if (bash_mode.load() && n % 2 == 0) {
            // 贝什 -turn 第一次完成 : 数据流 a `bash` 工具 调用 ,
            // 这样我们就可以坚持 栅栏上的命令 -output 纸牌 正文 (
            // 无) "bash shows no output" 固定) (无)
            // 终端 使用这种控制,它会运行内部路径,结果会变成栅栏的文字。
            std::string tcid = "tc_bash_" + std::to_string(n);
            const ag::ToolCallId call_id{tcid};
            sink(ag::Msg{ag::StreamStarted{}});
            sink(ag::Msg{ag::StreamTextDelta{"Running. "}});
            sink(ag::Msg{ag::StreamToolUseStart{call_id, ag::ToolName{"shell"}}});
            sink(ag::Msg{ag::StreamToolUseDelta{call_id,
                "{\"command\":\"printf 'hello-stdout\\\\n'\"}"}});
            sink(ag::Msg{ag::StreamToolUseEnd{call_id}});
            sink(ag::Msg{ag::StreamUsage{1100, 30, 0, 0}});
            sink(ag::Msg{ag::StreamFinished{ag::StopReason::ToolUse}});
            return;
        }
        if (read_mode.load() && n % 2 == 0) {
            // 读取 -turn 第一次完成 : 数据流 a `read` 工具 调
            // 用 档案上,这样我们就可以 坚持线 -numbered 结果内容形状。
            std::string tcid = "tc_read_" + std::to_string(n);
            const ag::ToolCallId call_id{tcid};
            sink(ag::Msg{ag::StreamStarted{}});
            sink(ag::Msg{ag::StreamTextDelta{"Reading. "}});
            sink(ag::Msg{ag::StreamToolUseStart{call_id, ag::ToolName{"read"}}});
            // RELATIVE 模式 "note.txt" 远比
            // 绝对路径更常见, Zed 默默地将一个它无法
            // 解析到绝对位置的地方放下—— 因此服务器必须针对会话
            // cwd 取消。 使用。 target.string
            // () 此处(已经绝对)将使该固定无法检验。
            std::string args = std::string("{\"path\":\"")
                             + target.filename().string() + "\"}";
            sink(ag::Msg{ag::StreamToolUseDelta{call_id, args}});
            sink(ag::Msg{ag::StreamToolUseEnd{call_id}});
            sink(ag::Msg{ag::StreamUsage{1200, 40, 0, 0}});
            sink(ag::Msg{ag::StreamFinished{ag::StopReason::ToolUse}});
            return;
        }
        if (n % 2 == 0) {
            // 轮到第一个完成: 数据流 文本 + a 键 `write` 工具 调用.
            std::string tcid = "tc_write_" + std::to_string(n);
            sink(ag::Msg{ag::StreamStarted{}});
            sink(ag::Msg{ag::StreamTextDelta{"Writing the file. "}});
            const ag::ToolCallId call_id{tcid};
            sink(ag::Msg{ag::StreamToolUseStart{
                call_id, ag::ToolName{"write"}}});
            // RELATIVE —— 模型写作 "out.txt" 更经常的不是绝对的路,
            // 而Zed默默地 DROPS 无法解析为绝对的位置( Q)
            // resolve_location 倒车 open_local_buffer
            // 仅限 `if is_absolute`。使用时使用 target.string
            // ()这里可以使服务器的 absolutise() 不可考证.
            std::string args = std::string("{\"path\":\"")
                             + target.filename().string()
                             + "\",\"content\":\"hello from acp\\n\"}";
            sink(ag::Msg{ag::StreamToolUseDelta{call_id, args}});
            sink(ag::Msg{ag::StreamToolUseEnd{call_id}});
            sink(ag::Msg{ag::StreamUsage{1200, 40, 0, 0}});
            sink(ag::Msg{ag::StreamFinished{ag::StopReason::ToolUse}});
        } else {
            // 第二次完成:前调用的工具结果必须载入
            // 历史(无论成功还是被拒绝).
            // 第二次完成:前一个工具结果 调用 必须是历史(无论成功还是被拒绝)。
            // 调用 是一个写( 字迹) ( tc_write_ 或,
            // 在 read_mode ,读数( tc_read_).
            std::string want_w = "tc_write_" + std::to_string(n - 1);
            std::string want_r = "tc_read_"  + std::to_string(n - 1);
            std::string want_b = "tc_bash_"  + std::to_string(n - 1);
            std::string want_p = "tc_par_"   + std::to_string(n - 1) + "_";
            bool saw_tool_result = false;
            for (const auto& m : req.messages)
                for (const auto& tc : m.tool_calls)
                    if (tc.id.value == want_w || tc.id.value == want_r
                        || tc.id.value == want_b || tc.id.value.starts_with(want_p))
                        saw_tool_result = true;
            CHECK(saw_tool_result);
            sink(ag::Msg{ag::StreamTextDelta{"Done."}});
            sink(ag::Msg{ag::StreamUsage{1300, 10, 0, 0}});
            sink(ag::Msg{ag::StreamFinished{ag::StopReason::EndTurn}});
        }
    };

    // ── 传输 两位真人 OS 代理和客户端之间的管道 ────────────────────
    // c2a=客户_代理,a2c=代理_客户,双方运行于rpc::Peer.
    int c2a[2], a2c[2];
#if defined(_WIN32)
    // _O_NOINHERIT :_pipe的手柄默认可以继承,
    // 所以任何孩子一个工具产卵于中位 -test 写字的结尾会打开
    // 读者永远不会看到 EOF 以拆卸方式(然后在退出时挂起)进行。
    CHECK(::_pipe(c2a, 1 << 16, _O_BINARY | _O_NOINHERIT) == 0);
    CHECK(::_pipe(a2c, 1 << 16, _O_BINARY | _O_NOINHERIT) == 0);
#else
    CHECK(::pipe(c2a) == 0);
    CHECK(::pipe(a2c) == 0);
#endif

    ag::auth::AuthHeader cred = ag::auth::ApiKeyHeader{"sk-test-not-empty"};
    CHECK(!ag::auth::is_empty(cred));

    auto server = maya::co_owned<ag::acp::AgentServer>::make(
        ag::rpc::fd_channel(c2a[0], a2c[1]), stream, cred, "claude-test", ag::Profile::Ask);
    std::thread agent_thread([&]{ server->serve(agtest::cap()); });   // 返回到 c2a 上的 EOF 时

    // ── 客户端 ────────────────────────────────────────────────────────

    std::atomic<int> agent_text_chunks{0};
    std::atomic<int> tool_calls{0};
    // 每一个 ToolCallUpdate 编号必须参考 ToolCall 已经宣布了。
    //
    // 这不是一种风格规则——这是Zed客户端的DOES带有一个偏差更新:
    // acp_thread.rs的update_tool_call()将id向上看,
    // 什么也没有发现,渲染在用户的面板上是一张可见的"Tool call not found"
    // FAILURE卡,所以一个未发的更新没有默默忽略;
    // 它作为一个破碎的工具出现在了记录稿中.
    std::set<std::string> announced_tool_ids;
    std::atomic<int>      orphan_tool_updates{0};
    std::atomic<int>      relative_locations{0};
    std::atomic<int> tool_completed{0};
    std::atomic<int> tool_failed{0};
    std::string last_tool_text;
    std::string tc_title;
    std::string tc_name;
    std::string perm_title;
    std::atomic<bool> tc_is_edit_kind{false};
    std::atomic<bool> tc_announce_diff{false};
    std::atomic<bool> perm_announce_diff{false};
    std::atomic<int> usage_updates{0};
    std::atomic<int> perm_requests{0};
    std::atomic<int> avail_cmds{0};
    std::atomic<int> config_opts{0};
    std::atomic<int> session_infos{0};
    std::atomic<bool> saw_model_option{false};
    std::atomic<bool> saw_mode_option{false};
    std::atomic<bool> saw_skill_or_compact_cmd{false};
    std::string last_session_title;
    std::string transcript;
    std::mutex transcript_mu;

    struct Client {};
    jsonrpc::Router<Client> router;
    router.on<to_client::SessionUpdate>([&](Client&, const SessionUpdateMsg& m) {
        match(m.update,
            [&](const SU_AgentMessageChunk& c) {
                ++agent_text_chunks;
                match(c.content,
                    [&](const TextContent& t) {
                        std::lock_guard lk(transcript_mu); transcript += t.text;
                    },
                    [&](const auto&) {});
            },
            [&](const SU_ToolCall& t) {
                ++tool_calls;
                std::lock_guard lk(transcript_mu);
                announced_tool_ids.insert(t.toolCall.toolCallId.value);
                // 地点必须是 ABSOLUTE . Zed的电话 resolve_location
                // 尝试 project_path_for_absolute_path ,
                // 然后回到 open_local_buffer 仅限
                // `if is_absolute(path)` —— 相对路径匹配,
                // 既不匹配, 返回无, 位置静默地被丢弃。 follow
                // -along 停止移动,没有错误, 所以这是无形的,没有断言。
                for (const auto& l : t.toolCall.locations)
                    if (!l.path.empty() && !std::filesystem::path(l.path).is_absolute())
                        ++relative_locations;
                tc_title = t.toolCall.title;
                // 电话 PROGRAMMATIC 名称( spec 1.8.
                // 0) 不同于人类标题。客户端通过工具身份组合或过滤
                // 无法从中恢复 "Reading src/main.cpp".
                if (t.toolCall.name) tc_name = *t.toolCall.name;
                if (t.toolCall.kind == ToolKind::Edit) tc_is_edit_kind.store(true);
                for (const auto& cc : t.toolCall.content)
                    match(cc, [&](const TCC_Diff&) { tc_announce_diff.store(true); },
                              [&](const auto&) {});
            },
            [&](const SU_ToolCallUpdate& u) {
                { std::lock_guard lk(transcript_mu);
                  if (!announced_tool_ids.count(u.update.toolCallId.value))
                      ++orphan_tool_updates;
                  // 地点在 UPDATE ,不是初始数据 ToolCall:
                  // 宣布时, 参数还没有完成 流式传输 ,
                  // 所以还没有路径。这是绝对的。 -path
                  // 合同实际上必须持有。
                  if (u.update.locations)
                      for (const auto& l : *u.update.locations)
                          if (!l.path.empty() && !std::filesystem::path(l.path).is_absolute())
                              ++relative_locations; }
                if (u.update.status && *u.update.status == ToolCallStatus::Completed)
                    ++tool_completed;
                if (u.update.status && *u.update.status == ToolCallStatus::Failed)
                    ++tool_failed;
                if (u.update.title) {
                    std::lock_guard lk(transcript_mu); tc_title = *u.update.title;
                }
                if (u.update.kind && *u.update.kind == ToolKind::Edit)
                    tc_is_edit_kind.store(true);
                if (u.update.content)
                    for (const auto& cc : *u.update.content)
                        match(cc,
                            [&](const TCC_Diff&) { tc_announce_diff.store(true); },
                            [&](const TCC_Content& c) {
                                match(c.content,
                                    [&](const TextContent& t) {
                                        std::lock_guard lk(transcript_mu);
                                        last_tool_text = t.text;
                                    }, [&](const auto&){});
                            }, [&](const auto&){});
            },
            [&](const SU_Usage&) { ++usage_updates; },
            [&](const SU_AvailableCommands& ac) {
                ++avail_cmds;
                for (const auto& c : ac.availableCommands)
                    if (c.name == "compact" || c.name == "new")
                        saw_skill_or_compact_cmd.store(true);
            },
            [&](const SU_ConfigOptions& co) {
                ++config_opts;
                for (const auto& o : co.configOptions) {
                    if (o.id == "model") saw_model_option.store(true);
                    if (o.id == "mode") saw_mode_option.store(true);
                }
            },
            [&](const SU_SessionInfo& si) {
                ++session_infos;
                if (si.title) {
                    std::lock_guard lk(transcript_mu);
                    last_session_title = *si.title;
                }
            },
            [&](const auto&) {});
    });
    std::atomic<bool> reject_mode{false};
    // 被请求时批准写入( 或设定 reject_mode 时拒绝)。
    router.on<to_client::RequestPermission>([&](Client&, const RequestPermissionParams& p) {
        ++perm_requests;
        {
            std::lock_guard lk(transcript_mu);
            if (p.toolCall.title) perm_title = *p.toolCall.title;
            if (p.toolCall.content)
                for (const auto& cc : *p.toolCall.content)
                    match(cc, [&](const TCC_Diff&) { perm_announce_diff.store(true); },
                              [&](const auto&) {});
        }
        auto want = reject_mode.load() ? PermissionOptionKind::RejectOnce
                                       : PermissionOptionKind::AllowOnce;
        std::string chosen;
        for (const auto& o : p.options)
            if (o.kind == want) chosen = o.optionId;
        return RequestPermissionResult{
            RequestPermissionOutcome{PO_Selected{chosen, Json::object()}}, Json::object()};
    });

    Client client_ctx;
    ag::rpc::Dispatch dispatch;
    dispatch.on_call         = [&](const maya::unlocked&, const jsonrpc::Call& c) { return router.handle(client_ctx, c); };
    dispatch.on_notification = [&](const maya::unlocked&, const jsonrpc::Notification& n) { router.handle(client_ctx, n); };
    ag::rpc::Peer agent(ag::rpc::fd_channel(a2c[0], c2a[1]), std::move(dispatch), "acp-client");
    agent.start();
    // A 调用 ;失败将 RpcError 喜欢旧的未来 -based
    // 客户端确实如此, 所以错误 -code 下面的检查读作相同。
    auto must = [](auto r) {
        if (!r) throw r.error();
        return std::move(*r);
    };

    // ── 驱动协议 ─────────────────────────────────────────────────
    InitializeParams ip;
    ip.clientCapabilities.fs.readTextFile = true;
    ip.clientCapabilities.fs.writeTextFile = true;
    auto init = must(agent.call<to_agent::Initialize>(agtest::cap(), ip));
    CHECK(init.protocolVersion == kProtocolVersion);
    CHECK(init.agentCapabilities.loadSession == true);
    CHECK(init.agentCapabilities.promptCapabilities.embeddedContext == true);
    // 图像是 DECLARED 因为他们
    // 现在被解码为 提示词 声明我们放弃的能力
    // 比不宣布更糟糕:客户端显示发送的附件。
    CHECK(init.agentCapabilities.promptCapabilities.image == true);
    // 音频没有申报——没有我们船接受的供应商。
    CHECK(init.agentCapabilities.promptCapabilities.audio == false);
    CHECK(init.agentInfo.has_value() && init.agentInfo->name == "agentty");

    NewSessionParams nsp; nsp.cwd = tmp.string();
    auto ns = must(agent.call<to_agent::SessionNew>(agtest::cap(), nsp));
    CHECK(!ns.sessionId.value.empty());
    CHECK(ns.modes.has_value());
    // 选择者们在结果本身上,不仅仅在以下更新上:
    // JetBrains从结果上建立其下降.
    REQUIRE(ns.configOptions.has_value());
    CHECK(ns.modes->currentModeId.value == "ask");
    CHECK(ns.modes->availableModes.size() == 3);

    // 贝壳与Zed的本地代理商对等: session/new 广告斜线 -command
    // 菜单( 选项) compact/new + 技能)和模式 -picker 配置选项,
    // 如 session/update 通知.
    CHECK(avail_cmds.load() >= 1);
    CHECK(saw_skill_or_compact_cmd.load());
    CHECK(config_opts.load() >= 1);
    CHECK(saw_model_option.load());
    // 电话 权限 模式是每个连接上的配置选项: Zed 从
    // configOptions 单枪匹马一旦出现 `modes`
    // 因此没有它,就没有描述 选择面板 扎德的电影
    CHECK(saw_mode_option.load());

    // 电话 MODEL 选项是版本 -agnostic.
    // session/set_config_option 应用选项本身列表中的值,并响应完整的配置状态。
    {
        auto model_opt = std::find_if(ns.configOptions->begin(), ns.configOptions->end(),
                                      [](const ConfigOption& o) { return o.id == "model"; });
        REQUIRE(model_opt != ns.configOptions->end());
        std::string pick;
        match(model_opt->options,
              [&](const CSO_Ungrouped& u) { if (!u.items.empty()) pick = u.items.back().value; },
              [&](const CSO_Grouped& g) {
                  if (!g.items.empty() && !g.items[0].options.empty())
                      pick = g.items[0].options.back().value;
              });
        REQUIRE(!pick.empty());
        auto set = must(agent.call<to_agent::SessionSetConfig>(agtest::cap(), 
            SetConfigOptionParams{ns.sessionId, "model", Json(pick), Json::object()}));
        bool reflected = false;
        for (const auto& o : set.configOptions)
            if (o.id == "model" && o.currentValue == pick) reflected = true;
        CHECK(reflected);   // 结果是带有新状态的
    }
    // 一个没有签名的-in供应商的模型是客户端错误.
    {
        int code = 0;
        try {
            must(agent.call<to_agent::SessionSetConfig>(agtest::cap(), 
                SetConfigOptionParams{ns.sessionId, "model", Json("no-such-model-xyz"),
                                      Json::object()}));
        } catch (const RpcError& e) { code = e.code; } catch (...) {}
        CHECK(code == errc::InvalidParams);
    }
    // 未知的线索 configId 是个很严重的错误,不是沉默,不是 -op ——
    // 和 CODE 不只是它扔的 说不定是...
    //
    // 上面的Zed树枝( agent_servers/src/acp.rs):
    // AuthRequired 打开标志 -in 流动,任何其他非流动
    // -InternalError 浮出到用户的逐字记录中, InternalError
    // 被视为 "the agent broke" ——它会挖进 err.data
    // 寻找崩溃细节, 否则显示通用失败。 CLIENT 错误为
    // InternalError 将一个错误的请求归咎于代理人,并埋葬了可采取行动的信息.
    {
        int  code = 0;
        Json data;
        bool threw = false;
        try {
            must(agent.call<to_agent::SessionSetConfig>(agtest::cap(), 
                SetConfigOptionParams{ns.sessionId, "temperatuer", Json("9"),
                                      Json::object()}));
        } catch (const RpcError& e) {
            threw = true; code = e.code; data = e.data;
        } catch (...) { threw = true; }
        CHECK(threw);
        CHECK(code == errc::InvalidParams);      // 不 InternalError 国 务 院
        // 上面写着WOULD的工作原理,所以客户可以采取行动.
        CHECK(data.contains("supported"));
    }

    // 与未知的sessionId相同:客户端发送了坏ID,
    // 我们没有崩溃.
    {
        int  code = 0;
        try {
            must(agent.call<to_agent::SessionSetMode>(agtest::cap(), 
                SetModeParams{SessionId{"no-such-session"},
                              SessionModeId{"ask"}, Json::object()}));
        } catch (const RpcError& e) { code = e.code; }
          catch (...) {}
        CHECK(code == errc::InvalidParams);
    }

    // set_mode 圆-trip( 切换到最小, 然后回问)。 在提示词 之前,
    // 将会话保留在 Ask 中, 因此用户上方的 `write` 工具门。
    must(agent.call<to_agent::SessionSetMode>(agtest::cap(), SetModeParams{ns.sessionId, SessionModeId{"minimal"}, Json::object()}));
    must(agent.call<to_agent::SessionSetMode>(agtest::cap(), SetModeParams{ns.sessionId, SessionModeId{"ask"}, Json::object()}));

    PromptParams pp;
    pp.sessionId = ns.sessionId;
    pp.prompt.push_back(TextContent{"please write the file", Nothing, Json::object()});
    auto pr = must(agent.call<to_agent::SessionPrompt>(agtest::cap(), pp));   // 只有在完全回合后才能解决

    CHECK(pr.stopReason == StopReason::EndTurn);
    CHECK(completions.load() == 2);             // 两次模式完成运行
    // 第一个提示词将一个直播会话标题推到了Zed的侧边栏.
    CHECK(session_infos.load() >= 1);
    { std::lock_guard lk(transcript_mu);
      CHECK(!last_session_title.empty()); }
    CHECK(perm_requests.load() == 1);           // 写到过一次
    CHECK(tool_calls.load() == 1);
    // 没有更新引用未事先通知的编号。 Zed 渲染 以 " . "Tool
    // call not found" 失败卡, 所以这是用户 -visible 破碎,
    // 不是协议的好。
    CHECK(orphan_tool_updates.load() == 0);
    // 我们发射的每个地点都是绝对的(见上文收集器)。
    CHECK(relative_locations.load() == 0);
    if (tool_completed.load() != 1) {
        std::lock_guard lk(transcript_mu);
        std::fprintf(stderr, "tool_failed=%d last_tool_text=[%s]\n",
                     tool_failed.load(), last_tool_text.c_str());
    }
    CHECK(tool_completed.load() == 1);
    CHECK(usage_updates.load() == 2);
    CHECK(agent_text_chunks.load() >= 2);

    // 工具卡是第一个 -class : 标题是工程 -relative ("write out.txt" ,
    // 而不是绝对的 tmp 路径), 类型地图写入 – 编辑, 并发布 -time
    // diff卡在工具运行前显示——在两个工具上 tool_call 最新报告
    // 及 权限 请求。这是 Claude。 -code-acp 平等工作。
    { std::lock_guard lk(transcript_mu);
      CHECK(tc_title.rfind("Write ", 0) == 0);
      CHECK(tc_title.find('/') == std::string::npos);   // 相对, 无 dir sep
      // ... 程序名称会同时运行。 稳定在 1. 8. 0 个字符中;
      // 没有它, 客户端只能显示传言标题,
      // 所以 grouping/filtering/iconography 无法通过工具身份。
      CHECK(tc_name == "write");
      CHECK(perm_title.rfind("Write ", 0) == 0); }
    CHECK(tc_is_edit_kind.load());
    CHECK(tc_announce_diff.load());
    CHECK(perm_announce_diff.load());

    { std::lock_guard lk(transcript_mu);
      CHECK(transcript.find("Writing the file.") != std::string::npos);
      CHECK(transcript.find("Done.") != std::string::npos); }

    // 工具实际上写了文件.
    CHECK(fs::exists(target));
    { std::ifstream f(target); std::string body((std::istreambuf_iterator<char>(f)), {});
      CHECK(body.find("hello from acp") != std::string::npos); }

    // 轮次 2：客户端拒绝写入。工具必须标记为失败，
    //    模式作为工具结果获得拒绝,
    //    转弯仍然能干净地解决(end_turn). ) QQ
    fs::remove(target);
    reject_mode.store(true);
    int perms_before   = perm_requests.load();
    int failed_before  = tool_failed.load();

    PromptParams pp2;
    pp2.sessionId = ns.sessionId;
    pp2.prompt.push_back(TextContent{"write it again", Nothing, Json::object()});
    auto pr2 = must(agent.call<to_agent::SessionPrompt>(agtest::cap(), pp2));

    CHECK(pr2.stopReason == StopReason::EndTurn);
    CHECK(perm_requests.load() == perms_before + 1);   // 又问了
    CHECK(tool_failed.load()   == failed_before + 1);   // 拒绝 + 失败
    CHECK(!fs::exists(target));                          // 从未写

    // 轮次 3：read 结果必须带行号并用围栏包裹，
    //    匹配阴道 -code-acp 一张卡片 正文 (无)
    //    "tool UI looks different"。脚本提供者发布我们所写文件的读数,
    //    然后结束转折。 @ @ info -create 文件( 转弯 2)
    // rejected/removed ) , , —— —— , ) .
    { std::ofstream f(target); f << "alpha\nbeta *star* line\ngamma\n"; }
    reject_mode.store(false);
    read_mode.store(true);
    last_tool_text.clear();

    PromptParams pp3;
    pp3.sessionId = ns.sessionId;
    pp3.prompt.push_back(TextContent{"read it", Nothing, Json::object()});
    auto pr3 = must(agent.call<to_agent::SessionPrompt>(agtest::cap(), pp3));
    CHECK(pr3.stopReason == StopReason::EndTurn);
    { std::lock_guard lk(transcript_mu);
      // 读取卡片正文是带围栏和制表符行号的摘录，例如 "```\n1\talpha…"。
      CHECK(last_tool_text.rfind("```", 0) == 0);
      CHECK(last_tool_text.find("1\talpha") != std::string::npos);
      CHECK(last_tool_text.find("2\tbeta")  != std::string::npos); }

    // 轮次 4：bash 结果必须在卡片正文中展示实际输出，形式为
    //    栅栏状块( "bash shows no output"
    //    固定) (无) 终端
    //    完成必须携带抓获的铁丝网,
    read_mode.store(false);
    bash_mode.store(true);
    last_tool_text.clear();

    PromptParams pp4;
    pp4.sessionId = ns.sessionId;
    pp4.prompt.push_back(TextContent{"run it", Nothing, Json::object()});
    auto pr4 = must(agent.call<to_agent::SessionPrompt>(agtest::cap(), pp4));
    CHECK(pr4.stopReason == StopReason::EndTurn);
    { std::lock_guard lk(transcript_mu);
      // 击打卡正文是栅栏,包含命令的stdout。
      CHECK(last_tool_text.find("```")          != std::string::npos);
      CHECK(last_tool_text.find("hello-stdout") != std::string::npos); }

    // 轮次 5（#116）：同一响应中的三次并行写入，结束时批量完成。
    //    每个文件都用自己的内容写成。。 @ @ info
    bash_mode.store(false);
    parallel_mode.store(true);
    PromptParams pp5;
    pp5.sessionId = ns.sessionId;
    pp5.prompt.push_back(TextContent{"write three files", Nothing, Json::object()});
    auto pr5 = must(agent.call<to_agent::SessionPrompt>(agtest::cap(), pp5));
    CHECK(pr5.stopReason == StopReason::EndTurn);
    for (const char* name : {"par_a.txt", "par_b.txt", "par_c.txt"}) {
        const fs::path p = tmp / name;
        CHECK_MESSAGE(fs::exists(p), std::string(name) << " was not written: its args were lost");
        std::ifstream f(p);
        std::string body((std::istreambuf_iterator<char>(f)), {});
        CHECK(body == std::string("from ") + name);
        fs::remove(p);
    }
    parallel_mode.store(false);

    // ── 图像在一个 提示词 到达模型 ─────────────────────────────
    //
    // 服务器曾经有一个 空 访客臂 ImageContent ,
    // 所以一个截图贴在Zed的代理面板上 被默默地丢弃
    // 了—— 字句去,照片没有,在一块建筑上,
    // TUI 被丢弃的街区与一个无视图像的模型是分不开的,
    // 这就是为什么没有人报告它的原因。
    //
    // 坚持到底 MODEL 接收(不仅仅是宣布能力)
    // 是:宣布 `image` 然后丢掉 字节
    // 更糟糕的是,甚至没有宣布它。
    {
        // A 1x1 PNG, base64 最小的东西,一个清晰的图像
        // 并生存在解码 字节-for-byte.
        const std::string png_b64 =
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
        PromptParams ip2;
        ip2.sessionId = ns.sessionId;
        ip2.prompt.push_back(TextContent{"what is in this image?", Nothing, Json::object()});
        ip2.prompt.push_back(ImageContent{png_b64, "image/png", Nothing, Nothing,
                                          Json::object()});
        (void)must(agent.call<to_agent::SessionPrompt>(agtest::cap(), ip2));

        std::lock_guard lk(saw_mu);
        CHECK(saw_images.load() == 1);
        CHECK(saw_media_type == "image/png");
        // 已解码,未通过为 base64 : 模型层想要原始 字节 ,
        // 并设置一个 PNG 以 8 开头 -byte 签名。
        CHECK(saw_bytes.size() > 8);
        CHECK(saw_bytes.compare(1, 3, "PNG") == 0);
    }

    // 关闭 -trip。
    must(agent.call<to_agent::SessionClose>(agtest::cap(), CloseSessionParams{ns.sessionId, Json::object()}));

    // ── 拆开 ──────────────────────────────────────────────────────────
    ::close(c2a[1]);   // EOF ==== == 翻译=== serve() 返回时
    agent_thread.join();
    ::close(a2c[1]);   // EOF 阅读器
    agent.stop(agtest::cap());
    ::close(c2a[0]);
    ::close(a2c[0]);

    fs::remove(target);
    std::fprintf(stderr, "acp_integration: OK (turn=%d perms=%d toolcalls=%d "
                 "completed=%d usage=%d chunks=%d)\n",
                 completions.load(), perm_requests.load(), tool_calls.load(),
                 tool_completed.load(), usage_updates.load(), agent_text_chunks.load());
    std::printf("acp_integration OK\n");
}
