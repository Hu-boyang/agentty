// context_gauge_wire_test - 上下文显示器读取真实的 提示词
// 大小在每张 传输 ,缓存与否(#96).
//
// OpenAI-style 用法计算缓存的符号 内部 编号 提示词 (
// prompt_tokens / input_tokens 包括 cached_tokens).
// Anthropic 数着他们旁边的 agentty 都读过一样的,所以,任何 OpenAI
// -shaped 带有温暖的服务器 缓存 显示的测量仪 提示词 + 缓存: 一个33k
// 上下文读作65k(Strata, llama.cpp ,vLLM,,(英语: ChatGPT/Codex 来,
// 这里是真实的 SSE 每个服务器发送的都是真实的 传输层 和真实的 状态更新函数
// 测量仪必须降落在真实的 提示词 大小。

#include <string>
#include <vector>

#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/io/http.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/store/store.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/responses/responses.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/app/update/internal.hpp"

using namespace agentty;

namespace {

store::Settings g_settings;

void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const Thread&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& s) { g_settings = s; },
        .new_thread_id  = [] { return ThreadId{"t-gauge"}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = {},
    });
}

// 给每封信都喂一个 传输层 生产到的 状态更新函数 并返回
// 测量仪读数。
int gauge_after(const std::vector<Msg>& msgs) {
    Model m;
    Message user;
    user.role = Role::User;
    user.text = "hello";
    m.d.current.messages.push_back(std::move(user));
    Message asst;
    asst.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(asst));
    m.s.context_max = 65536;
    for (const auto& msg : msgs) {
        auto [next, _] = app::update(std::move(m), msg);
        m = std::move(next);
    }
    return m.s.tokens_in;
}

} // 命名空间结束。

TEST_CASE("gauge: OpenAI chat usage with a warm cache is the prompt, not prompt + cache") {
    install_stub_deps();
    // 斯特拉塔最后的谈话 帧 (serve/server.py 块形 Openai :
    // 提示词 33,000,32,000 已经在其谈话中 缓存.
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"usage\":{\"prompt_tokens\":33000,\"completion_tokens\":2,\"total_tokens\":33002,"
        "\"prompt_tokens_details\":{\"cached_tokens\":32000}}}\n\n"
        "data: [DONE]\n\n";
    CHECK(gauge_after(provider::openai::parse_sse_for_test(sse)) == 33000);
}

TEST_CASE("gauge: Responses (ChatGPT/Codex) usage with a warm cache is the prompt") {
    install_stub_deps();
    // 斯特拉塔的 , , 和他一起来 serve/responses.py response.completed 使用量。
    const std::vector<std::string> sse = {
        R"({"type":"response.output_text.delta","delta":"ok"})",
        R"({"type":"response.completed","response":{"status":"completed",)"
        R"("usage":{"input_tokens":33000,"input_tokens_details":{"cached_tokens":32000},)"
        R"("output_tokens":2,"total_tokens":33002}}})",
    };
    CHECK(gauge_after(provider::responses::parse_sse_for_test(sse)) == 33000);
}

TEST_CASE("gauge: Anthropic usage keeps adding the cache, which it reports beside input") {
    install_stub_deps();
    // 斯特拉塔的 , , 和他一起来 Anthropic 形状 : message_start 拥有整个 提示词 ,
    // 最终 message_delta 将其分割为输入 + cache_read.
    const std::vector<std::pair<std::string, std::string>> sse = {
        {"message_start", R"({"type":"message_start","message":{"id":"m","type":"message",)"
                          R"("role":"assistant","content":[],"usage":{"input_tokens":33000,"output_tokens":0}}})"},
        {"message_delta", R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},)"
                          R"("usage":{"input_tokens":1000,"cache_read_input_tokens":32000,"output_tokens":2}})"},
        {"message_stop",  R"({"type":"message_stop"})"},
    };
    CHECK(gauge_after(provider::anthropic::parse_sse_for_test(sse)) == 33000);
}

namespace {

Message measured_turn(std::uint32_t input, std::uint32_t cache_read) {
    Message a;
    a.role = Role::Assistant;
    a.text = "ok";
    Message::Telemetry t;
    t.input_tokens = input;
    t.cache_read   = cache_read;
    a.telemetry    = t;
    return a;
}

Model model_with_gauge(int used) {
    Model m;
    m.s.context_max = 65536;
    m.s.tokens_in   = used;
    return m;
}

} // 命名空间结束。

TEST_CASE("gauge: opening a thread shows that thread's last prompt, not the old one") {
    install_stub_deps();
    Thread t;
    t.id = ThreadId{"other"};
    Message u; u.role = Role::User; u.text = "hi";
    t.messages.push_back(u);
    t.messages.push_back(measured_turn(1000, 11000));   // 12k 提示词 号
    t.messages.push_back(u);
    Message tail; tail.role = Role::Assistant; tail.text = "unmeasured";
    t.messages.push_back(tail);                        // 最新转弯没有用

    auto start = model_with_gauge(60000);
    start.s.thread_loading = true;
    start.s.loading_thread = t.id;   // 与ThreadListSelect一样
    auto [m, _] = app::update(std::move(start), Msg{ThreadLoaded{std::move(t)}});
    CHECK(m.s.tokens_in == 12000);
}

TEST_CASE("thread load: a result landing after ^N doesn't replace the new thread") {
    install_stub_deps();
    auto start = model_with_gauge(60000);
    start.s.thread_loading = true;
    start.s.loading_thread = ThreadId{"other"};
    auto [fresh, c1] = app::update(std::move(start), Msg{NewThread{}});
    const ThreadId fresh_id = fresh.d.current.id;
    CHECK(!fresh.s.thread_loading);

    Thread late; late.id = ThreadId{"other"};
    Message u; u.role = Role::User; u.text = "old";
    late.messages.push_back(u);
    auto [m, c2] = app::update(std::move(fresh), Msg{ThreadLoaded{std::move(late)}});
    CHECK(m.d.current.id == fresh_id);
    CHECK(m.d.current.messages.empty());
}

TEST_CASE("^N mid-stream cancels the old stream's token") {
    install_stub_deps();
    auto start = model_with_gauge(60000);
    auto tok = std::make_shared<http::CancelToken>();
    phase::Active ctx; ctx.cancel = tok;
    start.s.phase = phase::Streaming{std::move(ctx)};
    auto [m, _] = app::update(std::move(start), Msg{NewThread{}});
    CHECK(m.s.is_idle());
    CHECK(tok->is_cancelled());
}

TEST_CASE("submit during a thread load keeps the draft and sends nothing") {
    install_stub_deps();
    auto start = model_with_gauge(60000);
    start.s.thread_loading = true;
    start.s.loading_thread = ThreadId{"other"};
    start.ui.composer.text = "hello";
    const auto before = start.d.current.messages.size();
    auto [m, _] = app::update(std::move(start), Msg{ComposerEnter{}});
    CHECK(m.ui.composer.text == "hello");
    CHECK(m.d.current.messages.size() == before);
    CHECK(m.s.is_idle());
}

TEST_CASE("vision 400: an image from a read tool result is stripped and the turn retried") {
    install_stub_deps();
    auto start = model_with_gauge(60000);
    Message u; u.role = Role::User; u.text = "look at this";
    start.d.current.messages.push_back(u);
    Message a; a.role = Role::Assistant;
    ToolUse tc; tc.id = ToolCallId{"r1"}; tc.name = ToolName{"read"};
    ToolUse::Done done{};
    done.output = "[image /tmp/x.png]";
    ImageContent img; img.media_type = "image/png";
    done.images.push_back(img);
    tc.status = std::move(done);
    a.tool_calls.push_back(std::move(tc));
    start.d.current.messages.push_back(std::move(a));
    Message next; next.role = Role::Assistant;   // 失败的续作
    start.d.current.messages.push_back(next);
    start.s.phase = phase::Streaming{phase::Active{}};

    StreamError err;
    err.message = "Could not process image";
    err.http_status = 400;
    auto [m, cmd] = app::update(std::move(start), Msg{msg::StreamMsg{err}});

    bool any_image = false;
    for (const auto& msg : m.d.current.messages)
        for (const auto& t : msg.tool_calls)
            any_image = any_image || !t.done_images().empty();
    CHECK(!any_image);
    CHECK(!m.s.is_idle());      // 重试 预定,不是死转
    CHECK(!cmd.is_none());
}

// #117:400人死了,他们死了 -ended 模型切换后的一个会话
// 现在学习模型需要什么 -runs 同一转弯; 下一个转弯 正文 带着它
TEST_CASE("reasoning_content 400: learned per model, persisted, turn retried") {
    install_stub_deps();
    set_reasoning_echoes({});
    auto start = model_with_gauge(60000);
    start.d.selection.kind = provider::Kind::OpenAI;
    start.d.selection.openai_endpoint =
        provider::openai::Endpoint::from_spec("https://gw.example.org/v1");
    start.d.model_id = ModelId{"some/strict-thinker"};   // 未播种
    Message u; u.role = Role::User; u.text = "go on";
    Message prior; prior.role = Role::Assistant; prior.text = "from the last model";
    start.d.current.messages = {u, prior, u};
    Message next; next.role = Role::Assistant;   // 失败的续作
    start.d.current.messages.push_back(next);
    start.s.phase = phase::Streaming{phase::Active{}};

    const std::string host = host_key("gw.example.org", 443);
    REQUIRE(reasoning_echo_for(host, "some/strict-thinker") == 0);

    StreamError err;
    err.message = "Missing required parameter: 'messages[1].reasoning_content'. This "
                  "model needs the reasoning of every earlier assistant message: send "
                  "back the 'reasoning_content' returned with that message.";
    err.http_status = 400;
    auto [m, cmd] = app::update(std::move(start), Msg{msg::StreamMsg{err}});

    CHECK(reasoning_echo_for(host, "some/strict-thinker") == 2);       // 学习:每一转
    CHECK(m.d.persisted.reasoning_echo.size() == 1);                   // 并坚持
    CHECK(reasoning_echo_for(host, "some/other-model") == 0);          // 只为那个模型
    CHECK(!m.s.is_idle());      // 重试 预定,不是死转
    CHECK(!cmd.is_none());

    // 同样的400个(一个想要别的东西的网关)没有被
    // 两次学习: 范围已经是全部,所以它会永远地通过到
    // 正常的错误路径而不是重试.
    auto [m2, cmd2] = app::update(std::move(m), Msg{msg::StreamMsg{err}});
    CHECK(reasoning_echo_for(host, "some/strict-thinker") == 2);
    CHECK(m2.d.persisted.reasoning_echo.size() == 1);
    set_reasoning_echoes({});
}

TEST_CASE("gauge: a new thread starts at zero") {
    install_stub_deps();
    auto [m, _] = app::update(model_with_gauge(60000), Msg{NewThread{}});
    CHECK(m.s.tokens_in == 0);
}

TEST_CASE("gauge: a reading from before the latest compaction is not reused") {
    Thread t;
    Message u; u.role = Role::User; u.text = "hi";
    t.messages.push_back(u);
    t.messages.push_back(measured_turn(50000, 0));
    t.messages.push_back(u);
    Thread::CompactionRecord rec;
    rec.up_to_index = 2;   // 测量的转弯现在被总结掉
    t.compactions.push_back(rec);
    CHECK(last_reported_prompt(t) == 0);

    t.messages.push_back(measured_turn(3000, 1000));
    CHECK(last_reported_prompt(t) == 4000);
}

TEST_CASE("gauge: per-turn usage takes a frame's input and cache parts together") {
    install_stub_deps();
    // Anthropic-style: message_start 说
    // cache_read 30k,决赛 帧 说整个 提示词 转弯时不能保留30k。
    Model m;
    Message user; user.role = Role::User; user.text = "hi";
    m.d.current.messages.push_back(user);
    Message asst; asst.role = Role::Assistant;
    m.d.current.messages.push_back(asst);
    StreamUsage first;  first.input_tokens = 2000;  first.cache_read_input_tokens = 30000;
    StreamUsage last;   last.input_tokens  = 32000; last.output_tokens = 5;
    for (const auto& su : {first, last}) {
        auto [next, _] = app::update(std::move(m), Msg{su});
        m = std::move(next);
    }
    CHECK(m.s.tokens_in == 32000);
    const auto& tm = m.d.current.messages.back().telemetry;
    CHECK(tm.has_value());
    CHECK(tm->input_tokens == 32000);
    CHECK(tm->cache_read == 0);
}

TEST_CASE("gauge: a finished compaction drops the gauge to the compacted size") {
    install_stub_deps();
    Model m;
    m.s.context_max = 65536;
    // 长 线程 上回报告60千 提示词.
    const std::string big(40000, 'x');
    for (int i = 0; i < 6; ++i) {
        Message u; u.role = Role::User; u.text = big;
        m.d.current.messages.push_back(u);
        m.d.current.messages.push_back(measured_turn(60000, 0));
    }
    m.s.tokens_in = 60000;
    auto step = [&](Msg msg) {
        auto [next, _] = app::update(std::move(m), std::move(msg));
        m = std::move(next);
    };
    step(Msg{CompactContext{}});
    CHECK(m.s.compacting);
    step(Msg{StreamTextDelta{"<summary>short</summary>"}});
    step(Msg{StreamFinished{StopReason::EndTurn}});
    CHECK_FALSE(m.s.compacting);
    CHECK(!m.d.current.compactions.empty());
    CHECK(m.s.tokens_in > 0);
    CHECK(m.s.tokens_in < 60000);
}

TEST_CASE("compaction: auto_compact off keeps a full thread from compacting") {
    install_stub_deps();
    // 超过门槛,所以一个完成的转弯会自己收紧.
    auto full_thread = [] {
        Model m;
        m.s.context_max = 65536;
        const std::string big(40000, 'x');
        for (int i = 0; i < 6; ++i) {
            Message u; u.role = Role::User; u.text = big;
            m.d.current.messages.push_back(u);
            m.d.current.messages.push_back(measured_turn(60000, 0));
        }
        m.s.tokens_in = 60000;
        return m;
    };
    // 完成一个回合,并报告是否要求压缩。
    auto turn_compacts = [](Model m) {
        m.d.current.messages.back().role = Role::Assistant;
        m.s.phase = phase::Idle{};
        const Cmd c = app::detail::finalize_turn(m, StopReason::EndTurn);
        bool asked = false;
        agtest::fx::for_each(c, [&](const auto& e) {
            if constexpr (requires { e.msg; }) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(e.msg)>, Msg>)
                    if (const auto* meta = std::get_if<msg::MetaMsg>(&e.msg))
                        if (std::holds_alternative<CompactContext>(*meta)) asked = true;
            }
        });
        return asked;
    };

    Model on = full_thread();
    CHECK(on.d.persisted.auto_compact);
    CHECK(turn_compacts(std::move(on)));

    Model off = full_thread();
    off = app::update(std::move(off), Msg{ToggleAutoCompact{}}).first;
    CHECK_FALSE(off.d.persisted.auto_compact);
    CHECK_FALSE(turn_compacts(std::move(off)));

    const auto rows = settings::items_for(off, settings::Category::General);
    CHECK(std::ranges::any_of(rows, [](const auto& r) { return r.primary == "Auto-compact"; }));
}

TEST_CASE("window: the ollama row clamps to num_ctx only on a real Ollama (#73)") {
    install_stub_deps();
    namespace oai = provider::openai;
    Model m;
    // 别人都不用的港口 所以这个测试是自己的 缓存 输入。
    m.d.selection = provider::parse_selection("ollama");
    m.d.selection.openai_endpoint.port = 18999;
    m.d.model_id = ModelId{"qwen2.5-coder-7b"};
    ModelInfo mi;
    mi.id = m.d.model_id;
    mi.context_window = 131072;     // 服务器报告的内容
    m.d.available_models.push_back(mi);

    // 尚未询问:作为 Ollama ,最多只能起到 num_ctx.
    CHECK(app::detail::resolved_context_max(m, "ollama") < 131072);

    // 问了,它是一个真正的Ollama守护进程:仍然被夹住。
    oai::set_speaks_native_for_test(m.d.selection.openai_endpoint, true);
    CHECK(app::detail::resolved_context_max(m, "ollama") < 131072);

    // 问了,这是 llama.cpp 端口上是它服务的窗口
    oai::set_speaks_native_for_test(m.d.selection.openai_endpoint, false);
    CHECK(app::detail::resolved_context_max(m, "ollama") == 131072);
}
