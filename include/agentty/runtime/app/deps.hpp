#pragma once
// agentty::app::Deps — 运行时 的 接口边界 的类型 -erased 句柄。
//
// AgenttyApp的静态方法需要访问与main()连线的Provider、
// Store和凭据。我们没有在三个类型参数上模板化 AgenttyApp
// （这迫使每个 编译单元 知道具体类型），而是使用一个微小的
// vtable-style 结构，每个 -domain 将代码 调用 更新
// 到该结构中。
//
// 具体依赖在启动时通过install_deps()存储一次。
// 任何满足相关概念的都可以安装；具体类型隐藏在 std::function
// -style 擦除之后。

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/runtime/app/settings_cache.hpp"
#include "agentty/store/store.hpp"
#include "agentty/util/io.hpp"

namespace agentty::app {

struct Deps {
    // ── Provider 接口边界 国 务 院 ────────────────────────────────────────────────────
    // 由轮到工作线程（cmd::io_task 正文)调用，其不持有锁；
    // 它将其 `unlocked` 传递给提供者。
    std::function<void(const maya::unlocked&, provider::Request, provider::EventSink)> stream;

    // ── Store 接口边界(仅是调用 update.cpp实际制作) 互联网档案馆的存檔,存档日期2013-09-02. ────────────
    std::function<void(const Thread&)>          save_thread;
    std::function<void(const ThreadId&)>        delete_thread;
    // 返回 选择面板 的 线程 *元数据*（空消息）。完整的 正文
    // 通过 load_thread 按需获取。
    std::function<std::vector<Thread>()>        load_threads;
    std::function<std::optional<Thread>(const ThreadId&)> load_thread;
    // 旧线程的选择面板摘要，已填充并编入索引（在UI 线程之外）。
    // 在不需要它的测试中取消设置。
    std::function<std::vector<Thread>(const std::vector<ThreadId>&)> backfill_summaries;
    std::function<store::Settings()>            load_settings;
    // WRITE-BEHIND。立即返回：该值被发布到 `load_settings`
    // 读取的 in-memory 缓存 中，而实际的 加载
    // -modify-fsync-rename 发生在后台 工作线程 上。
    //
    // 为什么这是 SEAM 问题而不是每个调用者的问题：设置是从 ~8
    // 状态更新函数 写入的（配置文件周期、智能模式 切换、插槽清除、
    // 提供商切换、模型选择、退出、RAG 提交……）。 状态更新函数 是
    // UI 线程 上的纯函数；其中一个同步磁盘 round-trip
    // 停止了 渲染 循环，这就是切换智能模式在 -animation 中
    // 发生故障的原因。对于每个 调用 站点来说，修复这个问题有八次机会被
    // 忘记——而每写入一个新设置，就有九次机会被忘记。修复 此处 意味着
    // 状态更新函数 无法阻止设置 IO，即使它尝试了。
    //
    // 排序：写入按提交顺序应用于单个 工作线程，因此较晚的
    // 保存 不能先于较早的。读取会立即看到最新提交的值，
    // 因此同一个 状态更新函数 中的 保存-then-load 会
    // 观察到它刚刚写入的内容。
    std::function<void(const store::Settings&)> save_settings;
    std::function<ThreadId()>                    new_thread_id;
    std::function<std::string(std::string_view)> title_from;
    // 将审阅的文件的决定内容保留到磁盘。当用户拒绝 差异块 时，
    // 由 diff-review 使用：重写文件，仅保留 ACCEPTED
    // 差异块（已拒绝的已恢复）。 （路径，内容）——一个小的，
    // user-initiated写入，所以它在状态更新函数中同步运行。
    std::function<void(const std::string&, const std::string&)> write_file;

    // auth 头文件 开始。 install_deps() 将其移动到下面的实时存储中；
    // 用 live_auth() 读取它，用 update_auth() 替换它。
    auth::AuthHeader auth;
};

// 接口边界执行IO(数据流、线程、设置)，因此到达它们需要I/O。
[[nodiscard]] const Deps& deps(Io);
void install_deps(Deps d);

// 将 凭据 解析为 活动 提供者的值。
//
// 调用这个在UI 线程上并将结果交给任何需要它的人。
// 它曾经被称为 FROM 工作线程 正文（目录 获取，
// 窗口探测），这就是为什么它完全需要 锁：工作线程 读取身份
// 验证 缓存 竞态 UI 线程 的交换中间提供程序-switch。
// 那些 正文 现在将 头文件 作为参数 - jaal 的
// 规则是任务需要的所有内容都是参数 - 因此其余调用者都在
// UI 线程 上，而 锁 仅保护登录流程的实时替换。
//
// 它通过 provider::credentials 进行解析，
// 而不是直接读取 缓存，因此 凭据 永远不会偏离它所针对的提供者 — 发送到
// Mistral 的 Anthropic OAuth 令牌背后的错误类别。
//
// 在归并内，传递MODEL的选择（m.d.selection)：
// 进程全局仅在归并之后发布，因此mid-switch它仍然命名留下的提供者。
// no-argument 形式用于脱离循环的代码，
// 它没有 Model 并读取已发布的副本。
[[nodiscard]] auth::AuthHeader auth_snapshot(Io, const provider::Selection& sel);
[[nodiscard]] auth::AuthHeader auth_snapshot(Io);

// live auth 头文件:由install_deps()安装，
// 由主机的InstallAuth 副作用代替。不受任何 线程（maya::guarded
// 值）的影响；副本问世，因此 数据流 终生拥有自己的 头文件。
[[nodiscard]] auth::AuthHeader live_auth();
void update_auth(Io, auth::AuthHeader auth);

// 方便：绑定Provider+Store满足上述概念。
template <provider::Provider P, store::Store S>
void install(P& p, S& s, auth::AuthHeader auth) {
    // 设置IO经过写入-behind 缓存。包装 此处（构建 Deps 的地方）使得
    // 状态更新函数绝不能因设置文件 I/O 而阻塞。
    // 成为框架的属性，而不是 8 个 调用 站点必须记住的规则。
    // 参见runtime/app/settings_cache.hpp.
    //
    // 锁订单：Deps是EFFECT 接口边界。它的函数仅由 副作用
    // 主机和 init() 在步骤之间的循环 线程 上调用，其中不保留 锁
    // （update() 和 view() 不能采用 锁）。
    // 所以 接口边界 re-states 作为一个能力根，在这个审查的地方（lint：
    // cap_roots)，并且检查其后面的所有内容。
    // 磁盘保存是存储的QUIET 保存，因此缓存自己的写入不会使其无效。
    auto seam = maya::thread_root([&](const maya::unlocked& cap) {
        return settings_cache::wrap(cap,
            [&s] { return s.load_settings(); },
            [&s](const maya::unlocked& c, const store::Settings& x) { s.save_settings_quiet(c, x); });
    });

    install_deps(Deps{
        .stream = [&p](const maya::unlocked& c, provider::Request req, provider::EventSink sink) {
            p.stream(c, std::move(req), std::move(sink));
        },
        .save_thread     = [&s](const Thread& t) { s.save_thread(t); },
        .load_threads    = [&s] { return s.load_threads(); },
        .load_thread     = [&s](const ThreadId& id) { return s.load_thread(id); },
        .backfill_summaries = [&s](const std::vector<ThreadId>& ids) {
            return s.backfill_summaries(ids);
        },
        .load_settings   = [l = std::move(seam.load)] {
            return maya::thread_root([&](const maya::unlocked& c) { return l(c); });
        },
        .save_settings   = [sv = std::move(seam.save)](const store::Settings& x) {
            maya::thread_root([&](const maya::unlocked& c) { sv(c, x); });
        },
        .new_thread_id   = [&s] { return s.new_id(); },
        .title_from      = [&s](std::string_view t) { return s.title_from(t); },
        .auth            = std::move(auth),
    });
}

} // namespace agentty::app
