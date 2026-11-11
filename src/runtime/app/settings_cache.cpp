// settings_cache.cpp — 为设置 接口边界 写入 -behind。请参阅 头文件。
//
// 锁 订单（agentty/util/lock_levels.hpp)。一个 锁、
// 缓存 州、一个 LEAF：下面的每个 正文 都是州字段上的几行，
// 而 调用 与 锁 没什么区别。磁盘 IO 出现在 正文 之间，而不是在其中之一。
// 排水工作线程是锁中的NOT（它在破坏时加入，因此jaal在
// 受保护的内部拒绝它）：它存在于已发布的槽中，
// 由翻转`started`并以`unlocked`停止的一个调用者创建。

#include "agentty/runtime/app/settings_cache.hpp"
#include "agentty/util/callback.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "agentty/util/sendable.hpp"   // 设置内 Id<Tag>

#include "agentty/io/persistence.hpp"
#include "agentty/util/background.hpp"   // util::WorkerGroup
#include "agentty/util/lock_levels.hpp"
#include "agentty/util/teardown.hpp"

namespace agentty::app::settings_cache {

namespace {

// 排水 工作线程 及其调用者共享的所有内容。普通数据，因此它存在于受保护的环境中；
// flush() 和 工作线程 与 wait_with 一起等待。
struct State {
    // 权威的in-memory值。一旦播种，`load`就再也不会接触磁盘—
    // —这就是read-your-writes的原因，并且还删除了从执行加载
    // -modify-save的每个状态更新函数中读取的磁盘。
    std::optional<store::Settings> cached;

    // 最新值尚未写入。这里连续的 保存 COALESCE：所
    // 持有的 -down 密钥的中间状态并不重要，只有它停止的
    // 地方才有意义。无论 UI 产生写入的速度有多快，
    // 这也会将队列限制为 1。
    std::optional<store::Settings> pending;

    bool writing    = false;   // 写入正在进行中
    bool stopping   = false;
    // 被 invalidate() 碰撞。在碰撞之前启动的磁盘 加
    // 载 读取旧文件，并且不得发布它。
    std::uint64_t gen = 0;
    bool started    = false;   // 排水管工作线程已发布
    bool registered = false;   // 安装拆卸钩
};

maya::guarded<State>& state() {
    static maya::guarded<State> s;
    return s;
}

// 磁盘功能，由wrap()安装，由工作线程读取。
// 闭包不是通过 锁 传递的值，因此它们作为一个整体发布。
// 缓存自己的保存经过save_settings_quiet，
// 因此它确实NOT激发了"settings were written"
// 观察者（这将使缓存从其自己的写入中无效——这是旧的线程-id检查所防止的）。
struct Disk {
    util::Callback<store::Settings()>                                 load;
    util::Callback<void(const maya::unlocked&, const store::Settings&)> save;
};
maya::published<const Disk>& disk() {
    static maya::published<const Disk> d;
    return d;
}

// 排水管工作线程，外隔锁。
maya::published<util::WorkerGroup>& worker() {
    static maya::published<util::WorkerGroup> w;
    return w;
}

void save_now(const maya::unlocked& cap, const Disk& d, const store::Settings& value) noexcept {
    try {
        if (d.save) d.save(cap, value);
    } catch (...) { /* best-effort:失败的保存不得杀死应用程序 */ }
}

// 排水回路。在自己的 线程 上运行； 仅 位置设置 IO 发生。
void run(const maya::unlocked& cap) {
    for (;;) {
        // 睡觉直到有东西要写或者我们被告知停止。
        // 在同一步骤中获取该值并标记飞行中的写入。
        auto value = state().wait_with(cap,
            [](const State& s) { return s.pending.has_value() || s.stopping; },
            [](State& s) -> std::optional<store::Settings> {
                if (!s.pending) return std::nullopt;   // 停止，队列空
                auto v = std::move(*s.pending);
                s.pending.reset();
                s.writing = true;
                return v;
            });
        if (!value) return;

        // IO与锁 RELEASED：读取器绝不能等待磁盘写入，
        // 这是该文件的全部要点。
        if (auto d = disk().current()) save_now(cap, *d, *value);

        // flush()等待此："queue empty AND nothing in flight"。
        state().with(cap, [](State& s) { s.writing = false; });
    }
}

// 由声称`started`的一位呼叫者呼叫。
void start_worker(const maya::below<maya::leaf>& cap) {
    // 针对 abort-on-exit 的 该 修复：子系统在其首次拥有
    // 线程 时注册其自己的加入，而不是依赖于 main()。
    const bool reg = state().with(cap, [](State& s) {
        return !std::exchange(s.registered, true);
    });
    if (reg) util::teardown::on_shutdown("settings_cache",
                                         [](const maya::unlocked& c) { shutdown(c); });
    auto g = std::make_shared<util::WorkerGroup>("settings_cache.drain");
    g->post([](const maya::unlocked& job_cap, std::stop_token) { run(job_cap); });
    worker().publish(std::move(g));
}

} // 命名空间结束。

Seam wrap(const maya::unlocked& cap, std::function<store::Settings()> load_from_disk,
          std::function<void(const maya::unlocked&, const store::Settings&)> save_to_disk) {
    disk().publish(std::make_shared<const Disk>(
        Disk{{std::move(load_from_disk)}, {std::move(save_to_disk)}}));
    // re-install(提供商切换重建Deps)不得从
    // 先前存储中复活过期 缓存。
    state().with(cap, [](State& s) {
        s.cached.reset();
        s.stopping = false;
    });

    // 任何直接写入settings.json的人——凭据帮助器、
    // --model/--provider CLI路径——都会使我们无效，
    // 或者他们的写入将被下一个发布早于它的副本的保存撤消。我们自己的写入会经过
    // save_settings_quiet，因此它们永远不会到达这里。
    persistence::on_settings_written([](const maya::unlocked& c) { invalidate(c); });

    Seam out;

    out.load = [](const maya::below<maya::leaf>& c) {
        auto [hit, gen] = state().read(c, [](const State& s) {
            return std::pair{s.cached, s.gen};
        });
        if (hit) return *hit;

        // 首先从磁盘读取故障并发布锁，然后发布。
        store::Settings fresh;
        try {
            if (auto d = disk().current(); d && d->load) fresh = d->load();
        } catch (...) { /* 默认立场 */ }
        // 同时，另一个线程可能已经播种（或者保存可能已经
        // 发布了更新的值）；他们的胜利。如果运行无效，
        // 我们的读取可能早于它宣布的写入，因此返回它而不缓存。
        return state().with(c, [](State& s, store::Settings f, std::uint64_t g) {
            if (s.cached) return *s.cached;
            if (s.gen == g) s.cached = f;
            return f;
        }, std::move(fresh), gen);
    };

    out.save = [](const maya::below<maya::leaf>& c, const store::Settings& value) {
        // 发布给读者FIRST。一个状态更新函数，保存，然后加载必须
        // 看到它自己的写入，并且它必须在不等待磁盘的情况下看到它。
        const bool start = state().with(c, [](State& s, store::Settings v) {
            s.cached  = v;
            s.pending = std::move(v);
            if (s.started || s.stopping) return false;
            s.started = true;
            return true;
        }, value);
        if (start) start_worker(c);
    };

    return out;
}

void flush(const maya::unlocked& cap) noexcept {
    // 从未启动过工作线程，但值可能在其存在之前就已被
    // 发布：将其写在此处（仅在退出时到达）。否则等待队列清空，
    // 没有任何飞行内容。
    auto unstarted = state().with(cap, [](State& s) -> std::optional<store::Settings> {
        if (s.started || !s.pending) return std::nullopt;
        auto v = std::move(*s.pending);
        s.pending.reset();
        return v;
    });
    if (unstarted) {
        if (auto d = disk().current()) save_now(cap, *d, *unstarted);
        return;
    }
    state().wait_with(cap,
        [](const State& s) { return !s.started || (!s.pending && !s.writing); },
        [](State&) {});
}

void invalidate(const maya::unlocked& cap) noexcept {
    // 首先排水。排队写入保存的值早于我们
    // 被告知的绕过写入；之后让它着陆将撤消
    // 磁盘上的写入，这正是它要阻止的故障。
    flush(cap);
    state().with(cap, [](State& s) {
        s.cached.reset();
        ++s.gen;
    });
}

void shutdown(const maya::unlocked& cap) noexcept {
    // 排空BEFORE停止工作线程：在退出之前发布
    // 的值必须仍然到达磁盘，否则用户改变的最后一件事
    // 是不持久的一件事。
    flush(cap);

    const bool was_started = state().with(cap, [](State& s) {
        s.stopping = true;
        const bool was = s.started;
        s.started = false;
        return was;
    });
    if (!was_started) return;
    // run()在`stopping`退出(上面的with()唤醒了它)；
    // 该小组在每个 锁 之外加入它。
    if (auto g = worker().take()) g->stop(cap);
}

} // namespace agentty::app::settings_cache
