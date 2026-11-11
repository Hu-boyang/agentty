// src/rpc/peer.cpp — jsonrpc引擎，在maya上驱动。参见 peer.hpp。
//
// 围绕两个所有者的三个任务，没有在 I/O 上执行过 锁:
//
//   读取器读取通道，分割行，步骤接收，将 调用 和 通知 交给
//           分派 写入器将发件箱排空到通道，
//   以便自动收报机休眠直到
//   next_deadline() 和步骤 Tick
//
//   引擎guarded<Shared>:该引擎，尚未收集完成情况，
//           已关闭标志。每个转变都发生在它之下。
//   发件箱guarded<Outbox>:帧写，按顺序。
//
// 锁订单：均为LEAVES。下面没有 正文 需要另一个 锁 或 I/O；
// 副作用（帧要发送，调用到分派）从正文中返回并在其之后执行。
// 每个任务都从新的 线程 的 `unlocked` 开始，
// 这就是让它等待（写入器、自动收录器）并赋予
// 分派 能力的原因。
//
// 任务co-own核心，因此陷入阻塞读取的读取器
// 可以在关闭时被放弃，而不接触死的对端。

#include "agentty/rpc/peer.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <istream>
#include <ostream>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "agentty/util/dbglog.hpp"
#include "agentty/util/sendable.hpp"   // nlohmann::json

// jsonrpc的值拥有它们所持有的一切。 jaal 看不到过去的私人成员或
// std::expected，因此他们在此处被选择加入。
MAYA_SENDABLE(jsonrpc::Engine);
MAYA_SENDABLE(jsonrpc::RpcError);
MAYA_SENDABLE(jsonrpc::Completed);
MAYA_SENDABLE(jsonrpc::Effects);
MAYA_SENDABLE(jsonrpc::Event);

namespace agentty::rpc {

namespace {
using namespace std::chrono_literals;

std::uint64_t thread_key() {
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

jsonrpc::Completed lost(std::string_view method) {
    return {jsonrpc::RequestId{0}, std::string(method),
            std::unexpected(jsonrpc::RpcError(jsonrpc::errc::ConnectionLost, "connection closed"))};
}
}  // 命名空间结束。

// ── 频道 ───────────────────────────────────────────────────────────────

Channel fd_channel(int in_fd, int out_fd) {
    Channel ch;
    ch.read = [in_fd](const maya::unlocked&, char* buf, std::size_t cap) -> std::size_t {
        for (;;) {
#if defined(_WIN32)
            const int n = ::_read(in_fd, buf, static_cast<unsigned>(cap));
#else
            const auto n = ::read(in_fd, buf, cap);
            if (n < 0 && errno == EINTR) continue;
#endif
            return n > 0 ? static_cast<std::size_t>(n) : 0;
        }
    };
    ch.write = [out_fd](const maya::unlocked&, std::string_view bytes) {
        while (!bytes.empty()) {
#if defined(_WIN32)
            const int n = ::_write(out_fd, bytes.data(), static_cast<unsigned>(bytes.size()));
#else
            const auto n = ::write(out_fd, bytes.data(), bytes.size());
            if (n < 0 && errno == EINTR) continue;
#endif
            if (n <= 0) return false;
            bytes.remove_prefix(static_cast<std::size_t>(n));
        }
        return true;
    };
    return ch;
}

Channel stream_channel(std::istream& in, std::ostream& out) {
    Channel ch;
    // 块一个字节，然后获取已经缓冲的任何其他内容。
    ch.read = [&in](const maya::unlocked&, char* buf, std::size_t cap) -> std::size_t {
        auto* sb = in.rdbuf();
        if (!sb || cap == 0) return 0;
        const auto c = sb->sbumpc();
        if (c == std::char_traits<char>::eof()) return 0;
        buf[0] = static_cast<char>(c);
        std::size_t n = 1;
        while (n < cap && sb->in_avail() > 0) {
            const auto more = sb->sgetn(buf + n, std::min<std::streamsize>(
                sb->in_avail(), static_cast<std::streamsize>(cap - n)));
            if (more <= 0) break;
            n += static_cast<std::size_t>(more);
        }
        return n;
    };
    ch.write = [&out](const maya::unlocked&, std::string_view bytes) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        return out.good();
    };
    return ch;
}

namespace {
// in-memory管道的一个方向。一片叶子：每个 正文 都比 缓冲区
// 多几行。
struct Pipe {
    std::string bytes;
    bool        closed = false;
};
using PipeLock = maya::guarded<Pipe>;
using PipePtr  = std::shared_ptr<PipeLock>;

std::size_t pipe_read(const maya::unlocked& cap, PipeLock& p, char* buf, std::size_t n) {
    const auto got = p.wait_with(cap,
        [](const Pipe& s, std::size_t) { return !s.bytes.empty() || s.closed; },
        [](Pipe& s, std::size_t k) {
            k = std::min(k, s.bytes.size());
            std::string out = s.bytes.substr(0, k);
            s.bytes.erase(0, k);
            return out;
        }, n);
    std::copy(got.begin(), got.end(), buf);
    return got.size();
}

bool pipe_write(const maya::below<maya::leaf>& cap, PipeLock& p, std::string_view b) {
    return p.with(cap, [](Pipe& s, std::string chunk) {
        if (s.closed) return false;
        s.bytes += chunk;
        return true;
    }, std::string(b));
}

void pipe_close(const maya::below<maya::leaf>& cap, PipeLock& p) {
    p.with(cap, [](Pipe& s) { s.closed = true; });
}
}  // 命名空间结束。

std::pair<Channel, Channel> channel_pair() {
    auto ab = std::make_shared<PipeLock>();   // a写入，b读取
    auto ba = std::make_shared<PipeLock>();   // b写入，a读取
    auto end = [](PipePtr in, PipePtr out) {
        Channel ch;
        ch.read = [in](const maya::unlocked& c, char* buf, std::size_t n) {
            return pipe_read(c, *in, buf, n);
        };
        ch.write = [out](const maya::unlocked& c, std::string_view b) {
            return pipe_write(c, *out, b);
        };
        ch.interrupt = [in, out](const maya::unlocked& c) {
            pipe_close(c, *in);
            pipe_close(c, *out);
        };
        return ch;
    };
    return {end(ba, ab), end(ab, ba)};
}

struct Peer::Core {
    struct Shared {
        jsonrpc::Engine engine;
        // 其调用者尚未通过请求ID收集完成。
        std::unordered_map<std::int64_t, jsonrpc::Completed> done;
        // 已取消 调用还在等待对端，什么时候放弃。
        std::unordered_map<std::int64_t, Clock::time_point> give_up;
        bool closed = false;
        bool stopped = false;   // stop()跑了
    };
    struct Outbox {
        std::vector<std::string> frames;   // 准备写入，按顺序
        // 字节由开放批次持有，根据线程，及其嵌套深度。
        std::unordered_map<std::uint64_t, std::string> held;
        std::unordered_map<std::uint64_t, int>         depth;
        bool closed = false;
    };

    // 在构造时设置一次，然后仅读取(并且通道的功能对于读
    // 取器和写入器的调用来说是安全的：读取和写入是分开的端，
    // 中断针对stop())。
    const Channel     ch;
    const Dispatch    dispatch;
    const std::string name;
    maya::guarded<Shared> s;
    maya::guarded<Outbox> out;

    void write(const maya::below<maya::leaf>& cap, std::string frame);
    void perform(const maya::unlocked& cap, jsonrpc::Effects fx);
    jsonrpc::Effects step(const maya::below<maya::leaf>& cap, jsonrpc::Event ev);
    void close(const maya::unlocked& cap, std::string reason);
    void reader(const maya::unlocked& cap, std::stop_token st);
    void writer(const maya::unlocked& cap);
    void ticker(const maya::unlocked& cap, std::stop_token st);
};

Peer::Peer(Channel ch, Dispatch d, std::string_view name)
    : core_(core_t::make(std::move(ch), std::move(d), std::string(name))) {}

// 对端是joins_on_destroy (peer.hpp)，因此没有任何guarded
// <T>可以拥有一个，并且该析构函数永远不会在锁 正文内部运行：线程不包含任何内容，
// 并且可以加入。已审核能力根 (tests/lint/allowlist.txt)。
Peer::~Peer() {
    maya::thread_root([this](const maya::unlocked& cap) { stop(cap); });
}

void Peer::start() {
    pool_.post_isolated(
        [](const maya::unlocked& cap, std::stop_token st, core_t c) { c->reader(cap, st); }, core_);
    pool_.post_isolated(
        [](const maya::unlocked& cap, std::stop_token, core_t c) { c->writer(cap); }, core_);
    pool_.post_isolated(
        [](const maya::unlocked& cap, std::stop_token st, core_t c) { c->ticker(cap, st); }, core_);
}

void Peer::stop(const maya::unlocked& cap) noexcept {
    const bool again =
        core_->s.with(cap, [](Core::Shared& s) { return std::exchange(s.stopped, true); });
    if (again) return;
    try { core_->close(cap, "stopped"); } catch (...) {}
    if (core_->ch.interrupt) {
        try { core_->ch.interrupt(cap); } catch (...) {}
    }
    // 一位读者卡在read()，过去的恩典被放弃了；它是-owns核心。
    // stop() 不例外； self-join（从 对端 自己
    // 的阅读器停止）以其原因中止，而不是在这里抛出终止。
    (void)pool_.shutdown_noexcept(std::chrono::seconds(2));
}

void Peer::wait_closed(const maya::unlocked& cap) {
    core_->s.wait_with(cap, [](const Core::Shared& s) { return s.closed; }, [](Core::Shared&) {});
}

void Peer::set_request_meta(const maya::below<maya::leaf>& cap, jsonrpc::Json meta) {
    core_->s.with(cap,
                  [](Core::Shared& s, jsonrpc::Json m) { s.engine.set_request_meta(std::move(m)); },
                  std::move(meta));
}

bool Peer::closed(const maya::below<maya::leaf>& cap) const {
    return core_->s.read(cap, [](const Core::Shared& s) { return s.closed; });
}

void Peer::write(const maya::below<maya::leaf>& cap, std::string frame) {
    core_->write(cap, std::move(frame));
}
void Peer::write_core(const maya::below<maya::leaf>& cap, const core_t& c, std::string frame) {
    c->write(cap, std::move(frame));
}

// ── 发件箱 ──────────────────────────────────────────────────────────────

void Peer::Core::write(const maya::below<maya::leaf>& cap, std::string frame) {
    if (dispatch.trace) dispatch.trace(false, frame);
    frame.push_back('\n');
    out.with(cap, [](Outbox& o, std::uint64_t key, std::string f) {
        if (o.closed) return;
        if (auto it = o.depth.find(key); it != o.depth.end() && it->second > 0)
            o.held[key] += f;               // 该线程正在批处理
        else
            o.frames.push_back(std::move(f));
    }, thread_key(), std::move(frame));
}

void Peer::begin_batch(const maya::below<maya::leaf>& cap) {
    core_->out.with(cap, [](Core::Outbox& o, std::uint64_t key) { ++o.depth[key]; }, thread_key());
}

void Peer::end_batch(const maya::below<maya::leaf>& cap) noexcept {
    try {
        core_->out.with(cap, [](Core::Outbox& o, std::uint64_t key) {
            if (--o.depth[key] > 0) return;
            o.depth.erase(key);
            if (auto it = o.held.find(key); it != o.held.end()) {
                if (!o.closed && !it->second.empty()) o.frames.push_back(std::move(it->second));
                o.held.erase(it);
            }
        }, thread_key());
    } catch (...) {}
}

void Peer::Core::writer(const maya::unlocked& cap) {
    for (;;) {
        // 休眠直到有东西要写，或者对端关闭。
        // 帧在关门前排队仍然出去。
        auto batch = out.wait_with(cap,
            [](const Outbox& o) { return !o.frames.empty() || o.closed; },
            [](Outbox& o) { return std::exchange(o.frames, {}); });
        if (batch.empty()) return;   // 关闭，一无所有
        std::string bytes;
        for (auto& f : batch) bytes += f;
        if (!ch.write(cap, bytes)) {
            util::dbglog("rpc.write", name + ": channel gone");
            // 不能再发送任何内容：现在使每个等待的调用失败，
            // 而不是将其搁置直到读者碰巧看到EOF。
            close(cap, "write failed");
            out.with(cap, [](Outbox& o) { o.frames.clear(); });
            return;
        }
    }
}

// ── 过渡和 副作用 ───────────────────────────────────────────────

// 所有者之下的一过渡。完成的任务停放在 `done`
// （这会唤醒调用者）；其余部分由调用者在 锁 之外执行。
jsonrpc::Effects Peer::Core::step(const maya::below<maya::leaf>& cap, jsonrpc::Event ev) {
    return s.with(cap, [](Shared& sh, jsonrpc::Event e) {
        auto fx = jsonrpc::step(sh.engine, std::move(e));
        for (auto& c : fx.completed) sh.done.insert_or_assign(c.id.get(), std::move(c));
        fx.completed.clear();
        return fx;
    }, std::move(ev));
}

// 副作用在每个锁之外运行：分派被交给线程的`unlocked`，
// 因此处理函数可以形成嵌套的调用或采取任何锁。
void Peer::Core::perform(const maya::unlocked& cap, jsonrpc::Effects fx) {
    for (auto& f : fx.send) write(cap, std::move(f));
    for (auto& n : fx.notifications) {
        if (!dispatch.on_notification) continue;
        try { dispatch.on_notification(cap, n); }
        catch (const std::exception& e) { util::dbglog("rpc.notification", e.what()); }
        catch (...) {}
    }
    for (auto& c : fx.calls) {
        jsonrpc::Maybe<std::string> frame;
        try {
            if (dispatch.on_call) frame = dispatch.on_call(cap, c);
            else frame = jsonrpc::reply(c.id, std::unexpected(jsonrpc::RpcError(
                jsonrpc::errc::MethodNotFound, "Method not found: " + c.method)));
        } catch (const std::exception& e) {
            frame = jsonrpc::reply(c.id, std::unexpected(
                jsonrpc::RpcError(jsonrpc::errc::InternalError, e.what())));
        }
        if (frame) write(cap, std::move(*frame));
    }
}

// 传输层结束(EOF，或stop())。每次等待调用都失败；
// 排队的 帧 仍然耗尽，然后作者离开。
void Peer::Core::close(const maya::unlocked& cap, std::string reason) {
    perform(cap, s.with(cap, [](Shared& sh, std::string r) {
        jsonrpc::Effects fx;
        if (sh.closed) return fx;
        sh.closed = true;
        fx = jsonrpc::step(sh.engine, jsonrpc::Closed{std::move(r)});
        for (auto& c : fx.completed) sh.done.insert_or_assign(c.id.get(), std::move(c));
        fx.completed.clear();
        return fx;
    }, std::move(reason)));
    out.with(cap, [](Outbox& o) { o.closed = true; });
}

// ── 读者 ─────────────────────────────────────────────────────────────

void Peer::Core::reader(const maya::unlocked& cap, std::stop_token st) {
    jsonrpc::LineSplitter lines;
    std::string buf(64 * 1024, '\0');
    // 一个在被处理时抛出的帧一定不能带走阅读
    // 器：死掉的阅读器让对端永远聋，没有错误。
    auto take = [this, &cap](std::string line) {
        try {
            if (dispatch.trace) dispatch.trace(true, line);
            perform(cap, step(cap, jsonrpc::Received{std::move(line)}));
        } catch (const std::exception& e) {
            util::dbglog("rpc.reader", name + ": dropped a frame: " + e.what());
        } catch (...) {
            util::dbglog("rpc.reader", name + ": dropped a frame");
        }
    };
    while (!st.stop_requested()) {
        const std::size_t n = ch.read(cap, buf.data(), buf.size());
        if (n == 0) break;   // EOF 或错误
        for (auto& line : lines.feed(std::string_view{buf.data(), n})) take(std::move(line));
    }
    if (!st.stop_requested())
        if (auto last = lines.finish()) take(std::move(*last));
    close(cap, "eof");
}

// ── 滴答键 ─────────────────────────────────────────────────────────────
//
// 步骤 当最近的最后期限过去时打勾，当 已取消 调用 的
// 宽限用完时打勾 取消。它会短时间休眠，因此可以立即遵守较近的时间，
// 并在没有任何时间时停在引擎上（任何 with()
// 都会将其唤醒以重新-check）。

namespace {
jsonrpc::Deadline next_due(const jsonrpc::Engine& e,
                           const std::unordered_map<std::int64_t, jsonrpc::Clock::time_point>& give_up) {
    auto next = e.next_deadline();
    for (const auto& [_, at] : give_up)
        if (!next || at < *next) next = at;
    return next;
}
}  // 命名空间结束。

void Peer::Core::ticker(const maya::unlocked& cap, std::stop_token st) {
    while (!st.stop_requested()) {
        struct Next { jsonrpc::Deadline at; bool closed; };
        const auto next = s.read(cap, [](const Shared& sh) {
            return Next{next_due(sh.engine, sh.give_up), sh.closed};
        });
        if (next.closed) return;
        if (!next.at) {
            s.wait_with(cap,
                [](const Shared& sh) { return sh.closed || next_due(sh.engine, sh.give_up).has_value(); },
                [](Shared&) {});
            continue;
        }
        const auto now = Clock::now();
        if (*next.at > now) {
            const auto wait = std::min<Clock::duration>(*next.at - now, 50ms);
            if (maya::delay_for(cap, st, std::chrono::ceil<std::chrono::milliseconds>(wait))) return;
            continue;
        }
        perform(cap, s.with(cap, [](Shared& sh, Clock::time_point t) {
            auto fx = jsonrpc::step(sh.engine, jsonrpc::Tick{t});
            for (auto it = sh.give_up.begin(); it != sh.give_up.end();) {
                if (it->second > t) { ++it; continue; }
                fx += jsonrpc::step(sh.engine, jsonrpc::Cancel{jsonrpc::RequestId{it->first}});
                it = sh.give_up.erase(it);
            }
            for (auto& c : fx.completed) sh.done.insert_or_assign(c.id.get(), std::move(c));
            fx.completed.clear();
            return fx;
        }, now));
    }
}

// ── calls ──────────────────────────────────────────────────────────────────

jsonrpc::Completed Peer::call_raw(const maya::unlocked& cap, std::string_view method,
                                  jsonrpc::Json params, CallOptions o) {
    const auto t = o.timeout.value_or(default_timeout_.load(std::memory_order_relaxed));
    const jsonrpc::Deadline deadline =
        t.count() > 0 ? jsonrpc::Deadline{Clock::now() + t} : jsonrpc::Deadline{};

    // 在所有者下启动它；关闭的 对端 立即失败。
    struct Started { std::int64_t id; jsonrpc::Effects fx; bool closed; };
    auto started = core_->s.with(cap, [](Core::Shared& s, std::string m, jsonrpc::Json p,
                                         jsonrpc::Deadline d) -> Started {
        if (s.closed) return {0, {}, true};
        auto [id, fx] = jsonrpc::request_raw(s.engine, m, std::move(p), d);
        return {id.get(), std::move(fx), false};
    }, std::string(method), std::move(params), deadline);

    if (started.closed) return lost(method);
    core_->perform(cap, std::move(started.fx));

    // 持有任何锁的任何线程可以请求在`cancel`上停止，
    // 因此其回调仅引发标志。这个 线程 没有任何内容，
    // 对其进行操作：运行 on_cancel，告诉 对端，然后立即或在宽
    // 限期结束后使 调用 失败。不管怎样，它都会被定为 已取消。
    const auto requested = std::make_shared<std::atomic<bool>>(false);
    std::optional<std::stop_callback<std::function<void()>>> on_stop;
    if (o.cancel.stop_possible())
        on_stop.emplace(o.cancel, std::function<void()>{[requested] {
            requested->store(true, std::memory_order_release);
        }});
    bool cancel_handled = false;

    // 等待该id完成：回复、超过截止日期的勾选、
    // 取消或关闭。然后拿走它，放下所有仍然武装的恩典。
    // 等待被分割，以便在几毫秒内处理 取消 请求。
    for (;;) {
        auto [got, ready] = core_->s.wait_with_for(cap, 10ms,
            [](const Core::Shared& s, std::int64_t id) { return s.done.contains(id); },
            [](Core::Shared& s, std::int64_t id) -> std::optional<jsonrpc::Completed> {
                auto it = s.done.find(id);
                if (it == s.done.end()) return std::nullopt;
                s.give_up.erase(id);
                auto node = s.done.extract(it);
                return std::move(node.mapped());
            }, started.id);
        (void)ready;
        if (got) return std::move(*got);

        if (cancel_handled || !requested->load(std::memory_order_acquire)) continue;
        cancel_handled = true;
        if (o.on_cancel) {
            try { o.on_cancel(cap); } catch (...) {}
        }
        if (o.notify_peer) {
            try { notify_raw(cap, "$/cancel_request", jsonrpc::Json{{"id", started.id}}); }
            catch (...) {}
        }
        if (o.cancel_grace.count() <= 0) {
            core_->perform(cap, core_->step(cap, jsonrpc::Cancel{jsonrpc::RequestId{started.id}}));
        } else {
            core_->s.with(cap, [](Core::Shared& s, std::int64_t i, Clock::time_point at) {
                if (s.engine.is_pending(jsonrpc::RequestId{i})) s.give_up.emplace(i, at);
            }, started.id, Clock::now() + o.cancel_grace);
        }
    }
}

void Peer::cancel(const maya::unlocked& cap, jsonrpc::RequestId id) {
    core_->perform(cap, core_->step(cap, jsonrpc::Cancel{id}));
}

}  // namespace agentty::rpc
