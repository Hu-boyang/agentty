// src/mcp/connection.cpp——一台MCP服务器作为能力提供者。
// 参见 connection.hpp。
//
// ── 锁 顺序 ─────────────────────────────────────────────────────────────
//
// 连接实现mcp-cpp的CapabilityProvider，
// 其虚拟（执行、列表、read_resource、……）具有由第三-party
// 库固定的签名：它们不能被赋予能力。所以这个文件根本需要 NO jaal 锁，
// 并且其中没有任何内容可以是 锁-order 循环的一部分：
//
//   列出了jaal::snapshot (刷新时批量
//   替换）中毒的原子标志
//   一次忙一个调用：由compare-and-swap声明的原子。
//                调用者在 atomic::wait 中
//                丢失块直到持有者释放——等待原子，而不是 锁，
//                因此它不能与调用者持有的任何 锁 嵌套到
//                循环中（持有者从不等待调用者持有的任何东西：
//                它仅等待由对端自己的读取器线程传送的服务器答复。
//                当请求停止触发时，排队的 调用
//                就会离开。
//   重新连接相同，一次重新连接一个
//
// 阻塞工作——RPC轮-trip——在呼叫者的线程上运行。
// rpc::Peer需要`unlocked`，并且该能力是在下面的每
// 个库入口点（`entry_root`)（所审查的位置）处铸造的。
// 由于上述原因，这是合理的：对端的等待是针对其自己的
// 读取器的，其从不占用此类的调用者可以持有的任何锁。

#include "agentty/mcp/connection.hpp"
#include "agentty/util/callback.hpp"
#include "agentty/mcp/client.hpp"

#include <atomic>
#include <thread>

#include <mcp/mrtr.hpp>
#include <mcp/protocol.hpp>

#include "agentty/util/dbglog.hpp"
#include "agentty/util/sendable.hpp"

namespace agentty::mcp {

namespace m = ::mcp;
using Clock = std::chrono::steady_clock;

// 缓存的列表。读者使它们无效（list_changed）；
// 主机-side访问器在其自己的线程上刷新，而不是在读取器上刷新。
struct Lists {
    std::vector<m::Tool>             tools;
    Clock::time_point                tools_fresh_until{};   // 纪元：下次读取时刷新
    bool                             tools_stale = true;
    std::vector<m::Resource>         resources;
    std::vector<m::ResourceTemplate> templates;
    bool                             resources_stale = true;
    std::vector<m::Prompt>           prompts;
    bool                             prompts_stale = true;
};

}  // namespace agentty::mcp

MAYA_SENDABLE(agentty::mcp::Lists);
MAYA_FROZEN(agentty::mcp::Lists);
MAYA_SENDABLE(mcp::cap::CapabilityProvider::ListChangedFn);

namespace agentty::mcp {

namespace {
// CapabilityProvider入口点的能力根（参见该文件的顶部以了解
// 其为何合理）。列于tests/lint/allowlist.txt.
template <class F>
decltype(auto) entry_root(F&& f) {
    return maya::thread_root(std::forward<F>(f));
}

// One-call-at-a-time门：位0为"held"，
// 其余为历元取消凹凸。 atomic::wait 仅在值更改后返回，
// 因此 取消 必须更改它以唤醒服务员。
using Gate = std::atomic<unsigned>;
constexpr unsigned kHeld = 1;

// 认领门，阻塞（原子等待，不持有我们的任何东西），而其他人持有它。
// 在 `st` 上停止会结束等待：返回 false。
bool claim(const maya::unlocked& cap, Gate& gate, std::stop_token st = {}) {
    std::stop_callback wake(st, [&gate] {
        gate.fetch_add(2, std::memory_order_acq_rel);   // 纪元+ = 1;位 0 未受影响
        gate.notify_all();
    });
    unsigned v = gate.load(std::memory_order_acquire);
    for (;;) {
        if (st.stop_requested()) return false;
        if (!(v & kHeld)) {
            if (gate.compare_exchange_weak(v, v | kHeld, std::memory_order_acquire)) return true;
            continue;
        }
        maya::atomic_wait(cap, gate, v, std::memory_order_acquire);
        v = gate.load(std::memory_order_acquire);
    }
}
void release(Gate& gate) {
    gate.fetch_and(~kHeld, std::memory_order_release);
    gate.notify_all();
}
}  // 命名空间结束。

struct Connection::Live {
    std::shared_ptr<void>      holder;   // 通道的支持是什么；在 对端 之后掉落
    std::unique_ptr<rpc::Peer> peer;
    ::mcp::ServerCapabilities  caps;     // 在connect()期间设置，在-only之后读取
    std::string                instructions;   // 握手`instructions`，读取-only后
    maya::snapshot<Lists>      lists;
    std::atomic<bool>          poisoned{false};   // 已取消 调用使其不受信任
    std::atomic<unsigned>      busy{0};           // 调用正在飞行中； 调用 一次走一个（登机口）
    m::ClientHandlers          handlers;
    // 谁来介绍一下工具调用的进展。每个连接一次一个 调用，
    // 因此一个插槽就足够了。
    using Progress = util::Callback<void(std::string_view)>;
    maya::published<const Progress> progress;
    // 谁应答服务器的elicitation/create为in-flight 调用。
    using Elicit = util::Callback<m::Json(const m::Json&)>;
    maya::published<const Elicit> elicit;

    ~Live() {
        // ~对端停止并加入其任务本身；持有者追随它。
        peer.reset();
        holder.reset();
    }
};

}  // namespace agentty::mcp
// 用手检查（Live拥有对端，jaal无法行走）。 holder、
// caps和处理函数仅由connect()在live_发布对象之前写入，
// 并在之后读取；列表是 快照，poisoned/busy 是原子，
// 两个 回调 插槽已发布，对端 是同步。所以一旦发布，
// 很多线程可能会同时使用一个Live。
MAYA_SYNC(agentty::mcp::Connection::Live);
namespace agentty::mcp {

namespace {

using ElicitDone = util::Callback<void(const std::string&)>;
// 由可以打开URL引出的ACP编辑器设置。空：只有形式。
maya::published<const ElicitDone>& url_elicit_done() {
    static maya::published<const ElicitDone> d;
    return d;
}
}  // 命名空间结束。

void set_url_elicitation(std::function<void(const std::string&)> done) {
    url_elicit_done().publish(std::make_shared<const ElicitDone>(ElicitDone{std::move(done)}));
}

namespace {
// 每个连接一次一个 调用，如服务器所见：进度被路由到单
// 个 in-flight 调用，并且某些服务器无法交错。
struct Turn {
    Gate& busy;
    bool  held;
    Turn(const maya::unlocked& cap, Gate& b, std::stop_token st = {})
        : busy(b), held(claim(cap, busy, std::move(st))) {}
    ~Turn() { if (held) release(busy); }
};

template <class R>
R take(std::expected<R, m::RpcError> r) {
    if (!r) throw r.error();
    return std::move(*r);
}
}  // 命名空间结束。

Link stdio_link(::agentty::util::ChildProcess::Spawn spawn) {
    Link link;
    link.open = [spawn = std::move(spawn)]() -> Link::Opened {
        auto child = std::make_shared<::agentty::util::ChildProcess>(spawn);
        auto ch = rpc::stream_channel(child->out(), child->in());
        // 停止时：其标准输入上的 EOF，然后杀死它，以便其标准输出关闭
        // 并且驻留在其上的读卡器被唤醒。子对象一直存在于 `holder` 中，
        // 直到 对端 停止为止。
        ch.interrupt = [c = child.get()](const maya::unlocked&) {
            c->close_stdin();
            c->terminate();
            c->interrupt_output();
        };
        return {std::move(ch), child};
    };
    link.alive = [](const std::shared_ptr<void>& holder) {
        return holder && static_cast<::agentty::util::ChildProcess*>(holder.get())->alive();
    };
    return link;
}

Connection::Connection(ConnectionConfig cfg, Link link)
    : cfg_(std::move(cfg)), link_(std::move(link)), origin_("mcp:" + cfg_.name) {
    live_.publish(entry_root([this](const maya::unlocked& cap) { return connect(cap); }));
}

Connection::~Connection() {
    // 在我们的 线程 上停止 对端。否则，其读卡器上的 回调
    // 可能会保存最后一个引用，而 ~对端 将在那里加入自己的 池。
    if (auto l = live_.current())
        entry_root([&](const maya::unlocked& cap) { l->peer->stop(cap); });
    live_.publish(nullptr);
}

m::ClientHandlers Connection::handlers(std::weak_ptr<Live> weak) {
    m::ClientHandlers h;
    if (!cfg_.roots.empty())
        h.on_list_roots = [roots = cfg_.roots] { return m::ListRootsResult{roots, m::Json::object()}; };
    h.on_progress = [weak](const m::ProgressParams& u) {
        auto l = weak.lock();
        if (!l) return;
        auto sink = l->progress.current();
        if (!sink || !*sink) return;
        if (u.message && !u.message->empty()) { (*sink)(*u.message); return; }
        std::string text = "MCP progress: " + std::to_string(u.progress);
        if (u.total) text += " / " + std::to_string(*u.total);
        (*sink)(text);
    };
    h.on_resource_updated = cfg_.on_resource_updated;
    h.on_log              = cfg_.on_log;
    h.on_elicitation_complete = [](const m::ElicitationCompleteParams& p) {
        if (auto done = url_elicit_done().current(); done && *done) (*done)(p.elicitationId);
    };
    // 服务器向用户询问 mid-call 的内容：将其传递给 调
    // 用 来自的任何 UI（编辑器的表单）。无人问：谢绝。
    h.on_elicit = [weak](const m::ElicitParams& p) -> m::ElicitResult {
        auto l = weak.lock();
        auto ask = l ? l->elicit.current() : nullptr;
        if (!ask || !*ask) return m::ElicitResult{m::ElicitAction::Decline, m::Nothing, m::Json::object()};
        return m::from_json<m::ElicitResult>((*ask)(m::to_json(p)));
    };
    // list_changed仅标记缓存 过期（并告诉主机）；
    // 下一个主机-side读取刷新。这里刷新一下，对读者来说，
    // 等待只有读者才能提供的答复。
    //
    // 主机回调是通过槽的共享副本到达的，而不是`this`：
    // 飞行中的调用保持Live（以及读取器）运行经过〜连接，
    // 并且随后到达的list_changed不得接触它。
    auto changed = [slot = on_changed_, weak](auto mark) {
        if (auto l = weak.lock()) (void)l->lists.update(mark);
        if (auto fn = slot->current(); fn && *fn) (*fn)();
    };
    h.on_tools_changed     = [changed] {
        changed([](const Lists& s) { Lists o = s; o.tools_stale = true; return o; });
    };
    h.on_resources_changed = [changed] {
        changed([](const Lists& s) { Lists o = s; o.resources_stale = true; return o; });
    };
    h.on_prompts_changed   = [changed] {
        changed([](const Lists& s) { Lists o = s; o.prompts_stale = true; return o; });
    };
    return h;
}

std::shared_ptr<Connection::Live> Connection::connect(const maya::unlocked& cap) {
    auto opened = link_.open();
    auto l = std::make_shared<Live>();
    l->holder   = std::move(opened.holder);
    l->handlers = handlers(l);

    rpc::Dispatch d;
    d.on_call = [weak = std::weak_ptr<Live>(l)](const maya::unlocked&,
                                                const m::Call& c) -> std::optional<std::string> {
        if (auto live = weak.lock()) return live->handlers.handle(c);
        return m::reply(c.id, std::unexpected(m::RpcError(m::errc::InternalError, "connection closed")));
    };
    d.on_notification = [weak = std::weak_ptr<Live>(l)](const maya::unlocked&,
                                                         const m::Notification& n) {
        if (auto live = weak.lock()) live->handlers.handle(n);
    };
    l->peer = std::make_unique<rpc::Peer>(std::move(opened.channel), std::move(d), origin_);
    l->peer->set_default_timeout(cfg_.handshake_timeout);
    l->peer->start();
    // 与〜连接的原因相同：如果握手抛出，则在`l`下降
    // 之前停止对端，因此其读取器不能以最后的引用结束。
    struct StopOnThrow {
        const maya::unlocked& cap;
        Live& l;
        bool armed = true;
        ~StopOnThrow() { if (armed) l.peer->stop(cap); }
    } stop_on_throw{cap, *l};

    m::ClientCapabilities client_caps;
    if (!cfg_.roots.empty()) client_caps.roots = m::RootsCapability{true};
    // 表格引出在附加时被转发给编辑；否则，处理函数 会被拒绝，
    // 这是规范允许的。 URL模式只有在编辑器时
    // 才可以打开一个。
    const auto url_done = url_elicit_done().current();
    const bool url_ok = url_done && *url_done;
    client_caps.elicitation = m::ElicitationCapability{
        m::Json::object(), url_ok ? m::Maybe<m::Json>{m::Json::object()} : m::Nothing};

    // 2026-07-28 服务器从每个请求中读取协议元数据；
    // 年长的人忽略它。然后：server/discover（如果服务器有），
    // 否则遗留初始化握手。
    l->peer->set_request_meta(cap, m::modern_request_meta(cfg_.client_info, client_caps));
    if (auto disc = l->peer->call<m::to_server::Discover>(cap, m::DiscoverParams{})) {
        l->caps = disc->capabilities;
        if (disc->instructions) l->instructions = *disc->instructions;
    } else {
        // 2026年之前的服务器。一路向下说出它的方言：没有现代的 per
        // -request _meta，初始化中的遗留版本，并且（在 HTTP
        // 上）没有 MCP-Protocol-Version 头文件 直到它回答 -
        // 拒绝 2026-07-28 的服务器也会在那里拒绝它（问题＃95）。
        l->peer->set_request_meta(cap, m::Json::object());
        if (link_.set_protocol_version) link_.set_protocol_version(l->holder, {});
        m::InitializeParams ip;
        ip.protocolVersion = std::string(m::kLegacyProtocolVersion);
        ip.capabilities    = client_caps;
        ip.clientInfo      = cfg_.client_info;
        auto init = take(l->peer->call<m::to_server::Initialize>(cap, ip));
        l->caps = init.capabilities;
        if (init.instructions) l->instructions = *init.instructions;
        // 服务器选择的版本适用于以后的每个请求。
        if (link_.set_protocol_version)
            link_.set_protocol_version(l->holder, init.protocolVersion);
        l->peer->notify<m::to_server::Initialized>(cap, m::Unit{});
    }

    refresh_tools(cap, *l);
    if (l->caps.resources) { try { refresh_resources(cap, *l); } catch (...) {} }
    if (l->caps.prompts)   { try { refresh_prompts(cap, *l); } catch (...) {} }

    l->peer->set_default_timeout(cfg_.call_timeout);
    stop_on_throw.armed = false;
    return l;
}

bool Connection::alive() const {
    auto l = live_.current();
    if (!l || entry_root([&](const maya::unlocked& c) { return l->peer->closed(c); })) return false;
    return !link_.alive || link_.alive(l->holder);
}

::mcp::ServerCapabilities Connection::server_capabilities() const {
    auto l = live_.current();
    return l ? l->caps : ::mcp::ServerCapabilities{};
}

std::string Connection::instructions() const {
    auto l = live_.current();
    return l ? l->instructions : std::string{};
}

std::expected<std::shared_ptr<Connection::Live>, std::string>
Connection::ensure_live(const maya::unlocked& cap) {
    const auto healthy = [&](const std::shared_ptr<Live>& x) {
        return x && !x->peer->closed(cap) && (!link_.alive || link_.alive(x->holder))
               && !x->poisoned.load(std::memory_order_acquire);
    };
    auto l = live_.current();
    if (healthy(l)) return l;

    // 一次重连；谁失去了 竞态，谁就使用获胜者的。
    bool expected = false;
    const bool mine = reconnecting_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    if (!mine) {
        // 阻塞直到获胜者完成（原子等待，而不是睡眠-poll），
        // 然后使用剩下的任何东西，如果它是健康的。
        maya::atomic_wait(cap, reconnecting_, true, std::memory_order_acquire);
        if (auto now = live_.current(); healthy(now)) return now;
        return std::unexpected(std::string{"mcp server '" + origin_ + "' is not running"});
    }
    struct Done {
        Connection* c;
        ~Done() {
            c->reconnecting_.store(false, std::memory_order_release);
            c->reconnecting_.notify_all();
        }
    } done{this};
    // 之前的获胜者可能已经在我们的健康检查和CAS
    // 之间完成：不要拆掉它的新连接。
    if (auto now = live_.current(); now != l && healthy(now)) return now;
    try {
        // 首先建立新的连接，然后交换：竞态的读取器看到旧的Live（关闭，
        // 它将返回这里）或新的Live，从来没有一个空槽，
        // 它会报告为"not running"。
        auto old = live_.current();
        if (old) old->peer->stop(cap);
        live_.publish(connect(cap));
    } catch (const std::exception& e) {
        live_.publish(nullptr);
        return std::unexpected(std::string{"mcp reconnect failed: "} + e.what());
    }
    if (auto fn = on_changed_->current(); fn && *fn) (*fn)();
    return live_.current();
}

void Connection::set_on_list_changed(ListChangedFn fn) {
    on_changed_->publish(std::make_shared<const Changed>(Changed{std::move(fn)}));
}

// ── 列表 ───────────────────────────────────────────────────────────────────

void Connection::refresh_tools(const maya::unlocked& cap, Live& l) const {
    std::vector<m::Tool> all;
    m::Maybe<std::string> cursor;
    std::int64_t ttl = 0;
    do {
        m::ListToolsParams p;
        p.cursor = cursor;
        auto res = take(l.peer->call<m::to_server::ListTools>(cap, p));
        for (auto& t : res.tools) all.push_back(std::move(t));
        cursor = res.nextCursor;
        if (res.ttlMs > 0 && ttl == 0) ttl = res.ttlMs;   // 第一页的提示
    } while (cursor);
    const auto until = ttl > 0 ? Clock::now() + std::chrono::milliseconds(ttl) : Clock::time_point{};
    (void)l.lists.update([](const Lists& s, const std::vector<m::Tool>& t, const Clock::time_point& u) {
        Lists o = s;
        o.tools = t;
        o.tools_fresh_until = u;
        o.tools_stale = false;
        return o;
    }, std::move(all), until);
}

void Connection::refresh_resources(const maya::unlocked& cap, Live& l) const {
    std::vector<m::Resource> res;
    std::vector<m::ResourceTemplate> tpl;
    m::Maybe<std::string> cursor;
    do {
        m::ListResourcesParams p; p.cursor = cursor;
        auto r = take(l.peer->call<m::to_server::ListResources>(cap, p));
        for (auto& x : r.resources) res.push_back(std::move(x));
        cursor = r.nextCursor;
    } while (cursor);
    try {
        cursor = m::Nothing;
        do {
            m::ListResourceTemplatesParams p; p.cursor = cursor;
            auto r = take(l.peer->call<m::to_server::ListResourceTemplates>(cap, p));
            for (auto& x : r.resourceTemplates) tpl.push_back(std::move(x));
            cursor = r.nextCursor;
        } while (cursor);
    } catch (...) {}   // 模板可选
    (void)l.lists.update([](const Lists& s, const std::vector<m::Resource>& r,
                            const std::vector<m::ResourceTemplate>& t) {
        Lists o = s;
        o.resources = r;
        o.templates = t;
        o.resources_stale = false;
        return o;
    }, std::move(res), std::move(tpl));
}

void Connection::refresh_prompts(const maya::unlocked& cap, Live& l) const {
    std::vector<m::Prompt> all;
    m::Maybe<std::string> cursor;
    do {
        m::ListPromptsParams p; p.cursor = cursor;
        auto r = take(l.peer->call<m::to_server::ListPrompts>(cap, p));
        for (auto& x : r.prompts) all.push_back(std::move(x));
        cursor = r.nextCursor;
    } while (cursor);
    (void)l.lists.update([](const Lists& s, const std::vector<m::Prompt>& p) {
        Lists o = s;
        o.prompts = p;
        o.prompts_stale = false;
        return o;
    }, std::move(all));
}

std::vector<m::Tool> Connection::list() const {
    auto l = live_.current();
    if (!l) return {};
    const auto cur = l->lists.load();
    const auto now = Clock::now();
    const bool ttl_lapsed =
        cur->tools_fresh_until != Clock::time_point{} && now >= cur->tools_fresh_until;
    if (cur->tools_stale || ttl_lapsed) {
        try { entry_root([&](const maya::unlocked& c) { refresh_tools(c, *l); }); } catch (...) {}
    }
    return l->lists.load()->tools;
}

std::vector<m::Resource> Connection::resources() const {
    auto l = live_.current();
    if (!l || !l->caps.resources) return {};
    if (l->lists.load()->resources_stale) {
        try { entry_root([&](const maya::unlocked& c) { refresh_resources(c, *l); }); } catch (...) {}
    }
    return l->lists.load()->resources;
}

std::vector<m::ResourceTemplate> Connection::resource_templates() const {
    auto l = live_.current();
    if (!l || !l->caps.resources) return {};
    if (l->lists.load()->resources_stale) {
        try { entry_root([&](const maya::unlocked& c) { refresh_resources(c, *l); }); } catch (...) {}
    }
    return l->lists.load()->templates;
}

std::vector<m::Prompt> Connection::prompts() const {
    auto l = live_.current();
    if (!l || !l->caps.prompts) return {};
    if (l->lists.load()->prompts_stale) {
        try { entry_root([&](const maya::unlocked& c) { refresh_prompts(c, *l); }); } catch (...) {}
    }
    return l->lists.load()->prompts;
}

// ── calls ───────────────────────────────────────────────────────────────────

m::cap::Result Connection::execute(const m::cap::Request& req) {
    return entry_root([&](const maya::unlocked& cap) { return execute_impl(cap, req); });
}

m::cap::Result Connection::execute_impl(const maya::unlocked& cap, const m::cap::Request& req) {
    auto got = ensure_live(cap);
    if (!got) return m::cap::Result::error(std::move(got.error()));
    auto l = std::move(*got);
    Turn turn(cap, l->busy, req.stop);
    // 已取消 在此服务器上的另一个 调用 后面排队。
    if (!turn.held) return m::cap::Result::error("cancelled");

    l->progress.publish(std::make_shared<const Live::Progress>(Live::Progress{req.progress}));
    l->elicit.publish(req.elicit
        ? std::make_shared<const Live::Elicit>(Live::Elicit{req.elicit}) : nullptr);
    struct Clear { Live& l; ~Clear() { l.progress.publish(nullptr); l.elicit.publish(nullptr); } } clear{*l};

    // 取消 mid-call关闭连接（不能信任服务器处于
    // 干净状态）并且使调用失败；下一个 调用 重新连接。
    rpc::CallOptions o;
    o.cancel    = req.stop;
    o.on_cancel = [l](const maya::unlocked&) { l->poisoned.store(true, std::memory_order_release); };

    try {
        // MRTR:2026-07-28服务器可能回答input_required，
        // 要求我们采样/引出/列出根。用我们的 处理函数 和 重试 来实现它。
        const auto mrtr = l->handlers.mrtr();
        m::Json params = m::to_json(m::CallToolParams{req.tool, req.args, m::Nothing, m::Json::object()});
        for (int round = 0; round < m::kMrtrMaxRounds; ++round) {
            auto c = l->peer->call_raw(cap, m::to_server::CallTool::name, params, o);
            if (!c.outcome) {
                if (req.stop.stop_requested()) return m::cap::Result::error("cancelled");
                return m::cap::Result::error(std::string{"mcp call failed: "} + c.outcome.error().what());
            }
            auto retry = m::mrtr_retry(params, *c.outcome, mrtr);
            if (!retry) return m::cap::result_from_call(m::from_json<m::CallToolResult>(*c.outcome));
            params = std::move(*retry);
        }
        return m::cap::Result::error("mcp call failed: MRTR exceeded max rounds without a final result");
    } catch (const std::exception& e) {
        return m::cap::Result::error(std::string{"mcp call failed: "} + e.what());
    }
}

bool Connection::read_resource(const std::string& uri, std::vector<m::ResourceContents>& out,
                               std::string& err) {
    return entry_root([&](const maya::unlocked& cap) {
    auto got = ensure_live(cap);
    if (!got) { err = std::move(got.error()); return false; }
    Turn turn(cap, (*got)->busy);
    auto r = (*got)->peer->call<m::to_server::ReadResource>(cap, m::ReadResourceParams{uri, m::Json::object()});
    if (!r) { err = std::string{"resources/read failed: "} + r.error().what(); return false; }
    out = std::move(r->contents);
    return true;
    });
}

bool Connection::get_prompt(const std::string& name,
                            const std::vector<std::pair<std::string, std::string>>& args,
                            m::GetPromptResult& out, std::string& err) {
    return entry_root([&](const maya::unlocked& cap) {
    auto got = ensure_live(cap);
    if (!got) { err = std::move(got.error()); return false; }
    Turn turn(cap, (*got)->busy);
    m::GetPromptParams p;
    p.name = name;
    if (!args.empty()) p.arguments = args;
    auto r = (*got)->peer->call<m::to_server::GetPrompt>(cap, p);
    if (!r) { err = std::string{"prompts/get failed: "} + r.error().what(); return false; }
    out = std::move(*r);
    return true;
    });
}

}  // namespace agentty::mcp
