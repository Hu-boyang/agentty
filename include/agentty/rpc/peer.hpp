#pragma once
// agentty::rpc::Peer-一个JSON-RPC连接，由maya上的agentty驱动。
//
// jsonrpc-cpp的引擎是纯状态机：帧和时间进入，副作用出去，
// 任何地方都没有线程。这是它周围的 运行时，也是 agentty 中唯一的地方（
// docs/PROTOCOL_LIBRARIES.md §6）：
//
//   * 一个 所有者。发动机位于 maya::guarded 中；每次转换都会运行
//     其下。从传输读取，我们自己的请求、截止日期和关闭都
//     经过step()，所以它们是通过构造序列化的。
//   *读取器任务(maya::pool，隔离)读取入站字节通道，
//     分割行（jsonrpc::LineSplitter)和每个接收到的步骤；
//   * 截止时间任务在 maya::delay_for 休眠，直到 next_deadline() 且
//     步骤打勾；
//   * 写入任务会耗尽有序的发件箱，因此 帧 永远不会交错
//     并且在写入期间不保持锁。
//
// 出站请求对于调用者来说是阻塞-style（
// `call<M>(params)`返回带类型的结果），
// 因为agentty的两个集成已经从可能等待的工作线程 线程 调用：调
// 用者休眠直到其自己的完成被停放，从不保持引擎。
//
// 对端的调用和通知转到所有者供应的分派 回调；
// 它在读卡器 线程 上运行，在引擎 锁 之外，
// 因此 处理函数 本身可以 调用<> 对端 而不会出现死锁。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <maya/runtime.hpp>

#include <jsonrpc/jsonrpc.hpp>

namespace agentty::rpc {

// ── 锁 顺序 ─────────────────────────────────────────────────────────────
//
// 对端持有两个LEAF 锁 (其引擎及其发件箱）并且从不持有跨I/O.的一个因此，
// 按能力（agentty/util/lock_levels.hpp)：
//
//   下面<leaf>写-side 调用表示只排队一个帧或步进引擎：notify，
//                 notify_raw，reply，取消，Batch，
//                 set_request_meta，关闭。可从任何非 -leaf 锁
//                 正文 调用。
//   解锁调用，即WAIT为另一个线程：call/call_raw（等待回复），
//                 wait_closed，停止（加入任务）。
//
// 分派 回调在对端的读取器线程上运行，在帧之间，不持有任何东西，
// 因此它们被交给线程的`unlocked`:处理函数可以制作嵌
// 套的call()或按排名顺序获取任何锁。

// 到对端的字节通道。两个功能块；读取在 EOF（或出错
// 时）返回 0，写入在通道消失时返回 false。
//
// 读取在对端的读取器线程上运行，写入在其写入器线程上运行；每个通道都不
// 持有任何内容并传递其 `unlocked`（in-memory 通道等待
// 锁 内部读取）。中断被stop()调用，也被`unlocked`调用。
struct Channel {
    std::function<std::size_t(const maya::unlocked&, char* buf, std::size_t cap)> read;
    std::function<bool(const maya::unlocked&, std::string_view bytes)>            write;
    // 唤醒在`read`中阻塞的读取，以便读取器可以退出（关闭fd，
    // 关闭套接字）。停车时打过一次电话。
    std::function<void(const maya::unlocked&)>                                    interrupt;
};

// 两个 fd 上的原始读(2)/写(2)，e.g。我们自己的 stdin/stdout (0, 1)。
[[nodiscard]] Channel fd_channel(int in_fd, int out_fd);

// 儿童的管子为数据流 (util::ChildProcess的out()/in())。
// 所有者让 数据流 在 对端 的生命周期内保持活力。
[[nodiscard]] Channel stream_channel(std::istream& in, std::ostream& out);

// 两个in-memory通道彼此连接：一个写入，
// 另一个读取。中断任一端都会关闭两端。用于测试和环回。
[[nodiscard]] std::pair<Channel, Channel> channel_pair();

// 调用如何可能提前结束。 `cancel` 上的停止会运行 `on_cancel`（例如，
// 告诉 对端），如果 对端 到那时还没有应答，则稍后会失败 调用
// `cancel_grace`；零恩典立刻就失败了。 `timeout`为零
// 表示没有截止日期；未设置表示 对端 的默认值。
//
// 可以从持有任何东西的任何线程请求停止`cancel`，
// 因此停止回调仅引发标志。 CALLER——在 call()
// 中等待的 线程，什么也没有——在几毫秒内注意到它，
// 然后运行 ​​on_cancel，发送 取消 通知 并解决 调用。
struct CallOptions {
    std::optional<std::chrono::milliseconds> timeout;
    std::stop_token                          cancel;
    // 在调用线程上运行，其不包含任何内容。
    std::function<void(const maya::unlocked&)> on_cancel;
    std::chrono::milliseconds                cancel_grace{0};
    // 还发送带有该调用的id的$/cancel_request，
    // 因此对端可以丢弃该请求（权限对话框、表单）。
    bool                                     notify_peer = false;
};

// 所有者如何处理对端的调用和通知。返回 帧 以便
// 立即写入 调用，或者使用 Peer::reply
// （延迟方法）稍后再回复。
struct Dispatch {
    // 调用读取器线程，其没有任何内容：他们得到其`unlocked`。
    // 处理函数 可以嵌套 调用 或采用 锁。
    std::function<jsonrpc::Maybe<std::string>(const maya::unlocked&, const jsonrpc::Call&)>
        on_call;
    std::function<void(const maya::unlocked&, const jsonrpc::Notification&)> on_notification;
    // 每帧进出，用于传输踪迹。选修的。
    std::function<void(bool inbound, std::string_view frame)>        trace;
};

} // namespace agentty::rpc

// 对端、调用是一个通道的和一个分派的功能来自其自己的读取器、
// 写入器和股票代码线程，同时读取和写入。将一个交给 对端
// 就表明这是安全的； jaal 无法查看闭包内部来检查它。
// rpc/peer.cpp 中的每个通道都保留它：读和
// 写接触不同的端部，中断仅关闭它们。
MAYA_SYNC(agentty::rpc::Channel);
MAYA_SYNC(agentty::rpc::Dispatch);

namespace agentty::rpc {

class Peer {
public:
    using Clock = jsonrpc::Clock;

    Peer(Channel ch, Dispatch d, std::string_view name);
    // 停止并加入，如stop()。不能将 `unlocked` 交给析构函数；
    // 它可能会等待，因为 对端 被标记为 joins_on_destroy，
    // 因此 maya::guarded<T> 不能拥有它，并且它永远不会在 锁
    // 正文 内被破坏。最好先显式调用 stop(cap)。
    ~Peer();
    Peer(const Peer&)            = delete;
    Peer& operator=(const Peer&) = delete;

    // 开始阅读。与施工分开，以便业主可以在第一个
    // 帧 到达之前完成布线。
    void start();

    // 停止读取，每次等待调用与ConnectionLost失败，加入。
    // 加入：`unlocked`。
    void stop(const maya::unlocked&) noexcept;

    // 阻塞直到读者看到EOF。对于一个服务器来说，其生命周期就是连接的生命周期。
    // 等待：`unlocked`。
    void wait_closed(const maya::unlocked&);

    // 截止日期为调用，没有通过。零意味着没有截止日期。
    // 可以随时从任何 线程 调用（MCP 在握手后缩小范围，
    // 而读取器已经在运行）。
    void set_default_timeout(std::chrono::milliseconds d) noexcept {
        default_timeout_.store(d, std::memory_order_relaxed);
    }
    void set_request_meta(const maya::below<maya::leaf>&, jsonrpc::Json meta);

    // ── 编号 对端 ─────────────────────────────────────────────────────

    // 带类型的请求，等待。错误（对端、超时、关闭的连接、取消、
    // 未解码的结果）以 RpcError 的形式返回。
    // 等待回复：`unlocked`。
    template <jsonrpc::IsMethod M>
    std::expected<typename M::result, jsonrpc::RpcError>
    call(const maya::unlocked& cap, const typename M::params& p, CallOptions o = {}) {
        auto c = call_raw(cap, M::name, jsonrpc::to_json(p), std::move(o));
        return jsonrpc::result<M>(c);
    }

    // 无类型的形式：引擎返回时完成。
    jsonrpc::Completed call_raw(const maya::unlocked&, std::string_view method,
                                jsonrpc::Json params, CallOptions o = {});

    template <jsonrpc::IsNote N>
    void notify(const maya::below<maya::leaf>& cap, const typename N::params& p) {
        write(cap, jsonrpc::notify<N>(p));
    }
    void notify_raw(const maya::below<maya::leaf>& cap, std::string_view method,
                    jsonrpc::Json params) {
        write(cap, jsonrpc::notify_raw(method, std::move(params)));
    }

    // 答复调用，分派被推迟。
    void reply(const maya::below<maya::leaf>& cap, const jsonrpc::Id& id,
               std::expected<jsonrpc::Json, jsonrpc::RpcError> outcome) {
        write(cap, jsonrpc::reply(id, std::move(outcome)));
    }
    template <jsonrpc::IsMethod M>
    void reply(const maya::below<maya::leaf>& cap, const jsonrpc::Id& id,
               const typename M::result& r) {
        write(cap, jsonrpc::reply<M>(id, r));
    }

private:
    struct Core;   // 与任务共享，因此被遗弃的读者比我们活得更久
    using core_t = maya::co_owned<Core>;
    static void write_core(const maya::below<maya::leaf>& cap, const core_t& c, std::string frame);

public:

    // 延迟的调用的答复，最多发送一次。先ok()/error()获胜；
    // 后来的则被丢弃。它与 -owns 和 对端 的核心相连，
    // 因此持有一个 工作线程 的寿命可以比 对端 长：答案就被丢弃了。
    template <jsonrpc::IsMethod M>
    class Deferred {
    public:
        Deferred(Peer& p, jsonrpc::Id id) : c_(p.core_), id_(std::move(id)) {}
        Deferred(Deferred&&) noexcept            = default;
        Deferred& operator=(Deferred&&) noexcept = default;

        [[nodiscard]] const jsonrpc::Id& id() const { return id_; }
        void ok(const maya::below<maya::leaf>& cap, const typename M::result& r) {
            if (std::exchange(done_, true)) return;
            write_core(cap, c_, jsonrpc::reply<M>(id_, r));
        }
        void error(const maya::below<maya::leaf>& cap, int code, std::string msg) {
            if (std::exchange(done_, true)) return;
            write_core(cap, c_,
                       jsonrpc::reply(id_, std::unexpected(jsonrpc::RpcError(code, std::move(msg)))));
        }
    private:
        maya::co_owned<Core> c_;
        jsonrpc::Id id_;
        bool        done_ = false;
    };

    // 现在（本地）我们的调用之一失败，e.g。在用户 取消 上。
    void cancel(const maya::unlocked&, jsonrpc::RequestId id);

    // 将`body`内的THIS 线程写入的每个帧合并为一次写入。
    // 对于 通知 的突发（重播 线程）。范围是 调用，
    // 而不是 free-standing RAII 对象，
    // 因此批处理结束时具有与开始时相同的功能。嵌套批次展平。
    template <class F>
    decltype(auto) batch(const maya::below<maya::leaf>& cap, F&& body) {
        begin_batch(cap);
        struct End {
            Peer& p;
            const maya::below<maya::leaf>& c;
            ~End() { p.end_batch(c); }
        } end{*this, cap};
        return std::forward<F>(body)();
    }

    [[nodiscard]] bool closed(const maya::below<maya::leaf>&) const;

private:
    void write(const maya::below<maya::leaf>&, std::string frame);
    void begin_batch(const maya::below<maya::leaf>&);
    void end_batch(const maya::below<maya::leaf>&) noexcept;

    core_t                    core_;
    std::atomic<std::chrono::milliseconds> default_timeout_{std::chrono::milliseconds{0}};
    maya::pool                pool_{3};   // 读者、作者、自动收报机
};

}  // namespace agentty::rpc

// 对端在被销毁时加入其reader/writer/ticker：永远不会在
// maya::guarded<T>内部（jaal拒绝该类型），因此其析构函数可以等待。
MAYA_JOINS_ON_DESTROY(agentty::rpc::Peer);
// 每个成员对于任何线程都是安全的：核心是co_owned同步状态（
// 受保护的Shared/Outbox，常量通道），超时是原子的，池是同步的。
// 如此多的 线程 可能会同时 调用 和一个 对端。
MAYA_SYNC(agentty::rpc::Peer);

// 延迟答复是分派交给稍后答复者的句柄。
// Deferred拥有对端的核心(co_owned)、调用的id和
// 标志的句柄：将其移动到将应答的工作线程不共享任何内容。
template <jsonrpc::IsMethod M>
MAYA_SENDABLE_T(agentty::rpc::Peer::Deferred<M>);
