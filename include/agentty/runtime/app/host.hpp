#pragma once
// agentty::app::Host — maya的终端主机，加上agentty自己的副作用。
//
// maya::run()构建terminal_host并将其交给jaal。
// 该主机知道每个 TERMINAL 副作用（回滚缓冲区、剪贴板、挂起等），
// 除此之外一无所知，这是正确的：将 线程 写入磁盘不是 终端
// 所做的事情。
//
// 所以 agentty 将其包装起来。 maya 的主机处理的所有内容都是继承的；
// 四个 持久化 副作用 在这里得到一个 handle()。 jaal 的
// HostFor 概念在 编译 时间检查联合 - agentty 的 Cmd
// 行中没有 处理函数 的 副作用 导致构建失败，NAMES 本身失败，
// 而不是默默地从不运行。
//
// 为什么是 maya::run 的包装器而不是分叉：终端 的一半是
// maya 的业务并且应该在那里继续发展。这仅增加了 maya
// 所不知道的一半。

#include <filesystem>
#include <utility>

#include <maya/host/terminal.hpp>

#include "agentty/auth/accounts.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/chatgpt/codex_oauth.hpp"   // SaveCodexCredentials
#include "agentty/auth/vault.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/store_fx.hpp"
#include "agentty/tool/subagent.hpp"            // PublishSubagent的目标
#include "agentty/tool/web_search_policy.hpp"       // PublishWebSearchPolicy的目标
#include "agentty/tool/tool_loading.hpp"            // PublishToolLoading的目标
#include "agentty/io/clipboard.hpp"                 // PublishPasteConfig的目标
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::app {

/// 实际运行的主机agentty。
///
/// 继承maya::terminal_host的handle()/start_source
/// ()集合并添加存储副作用。公共继承使得继承的重载对 jaal 的
/// `handles<H, D, Msg>` 探针可见。
template <class P>
struct Host : maya::terminal_host<P> {
    using maya::terminal_host<P>::terminal_host;

    // 在每个继承的重载中将 handle() 声明在 HIDES 之下 — 名称
    // 查找在第一个具有该名称的作用域处停止，因此 jaal 的
    // `h.handle(CommitScrollback{...})` 探测将失败，
    // 并且构建将报告 maya 自己的 副作用 为不可运行。这使它们回到了范围内。
    using maya::terminal_host<P>::handle;

    // ── 电话 host_context 钩子 ──────────────────────────────────────
    //
    // 这里没什么可写的——maya的attach()/on_ready()是在上下文类型
    // 上模板化的，因此它们匹配`host_context<Host>`并且被继承。
    //
    // 那不是免费的。他们过去准确地采用`host_context<terminal_host<P>>`，
    // jaal检测到带有`requires { host.attach(cx); }`的钩子，
    // 其中cx是host_context<此 host>。
    // 因此，派生主机未通过测试，内核跳过 attach()，
    // 终端 的输入从未向反应器注册——并且应用程序静默地不接受
    // 任何密钥。与 program.hpp 中的 init()
    // 错误形状相同：运行时 无法将 调用 读取为 "absent"
    // 而不是错误。已修复于maya；请参阅那里的注释。

    // ── agentty's effects ────────────────────────────────────────────────
    // 每一个调用进入持久化层。 状态更新函数 返回后，
    // 它们在循环 线程 上运行；那些会阻塞（设置）的是持久化
    // 本身内部的write-behind，所以这些都不会阻止
    // 帧。
    //
    // 当 状态更新函数 构建 副作用 时，载荷 被复制，
    // 因此这里没有任何东西借自此后继续前进的 Model。
    //
    // 锁顺序：在步骤之间的循环线程上的jaal 调用 handle()，
    // 其中不保持锁(update()和view()根本不能采取锁)。
    // 因此，每个 处理函数 与此处铸造的 `unlocked` 一起运行，每个 调用 运行一次。
    std::optional<Msg> handle(SaveThread e) {
        ThreadId id = e.delta.header.id;
        const bool queued = with_unlocked([&](const maya::unlocked& c) {
            return persistence::save_thread_delta(io_, c, std::move(e.delta));
        });
        if (queued) return std::nullopt;
        return Msg{msg::ThreadListMsg{SaveRefused{std::move(id)}}};
    }
    void handle(DeleteThread e) {
        with_unlocked([&](const maya::unlocked& c) { persistence::delete_thread(io_, c, e.id); });
    }
    void handle(RenameThread e) {
        with_unlocked([&](const maya::unlocked& c) {
            persistence::rename_thread(io_, c, e.id, std::move(e.title), e.renamed_at,
                                       e.name_is_auto);
        });
    }

    // 发布子代理路由器的视图(参见store_fx.hpp PublishSubagent)。
    // 互斥量-guarded复制到注册表——微秒——因此它
    // 在下一个归并之前在循环线程上运行。这个顺序就是重点：
    // 由 LATER 消息调度的 `task` 工具总是看到 Model
    // 被调度时所具有的视图，而不是 过期 视图。
    void handle(PublishSubagent e) {
        namespace sa = tools::subagent;
        sa::set_model(io_, std::move(e.model));
        sa::set_provider(io_, std::move(e.provider));
        sa::set_smart(io_, std::move(e.smart));
        sa::set_candidates(io_, std::move(e.candidates));
    }

    // 发布Model的活动提供者（参见store_fx.hpp PublishSelection)。
    // 启动后经过-global选拔的一作者。
    void handle(PublishSelection e) { provider::select(io_, std::move(e.selection)); }

    // 发布Model的web_search政策(参见
    // store_fx.hpp PublishWebSearchPolicy)。应用程序运行
    // 后工具层副本的唯一编写者；一个原子商店。
    void handle(PublishWebSearchPolicy e) {
        tools::web_search_policy::install(std::move(e.policy));
    }

    // 将Model的粘贴设置发布到剪贴板读取器(参见store_fx.hpp
    // PublishPasteConfig)。一次原子交换。
    void handle(PublishPasteConfig e) { clipboard_configure(e.config); }

    // MCP工具加载(#98):一次原子交换轮到工作线程读取。
    void handle(PublishToolLoading e) {
        tools::tool_loading::install(tools::plan::loading_from(e.mode));
    }

    // 凭据 (参见 store_fx.hpp)。小本地 reads/writes，
    // 在循环 线程 上按顺序运行，这样下一个 归并 的 数据流 启动就能看到它们。
    // 使用新的 AuthView 进行回答：每个 凭据 改变都以 InstallAuth 结束，
    // 因此 Model 的视图遵循它，而无需每个 状态更新函数 必须
    // 记住 LoadAuthView。
    std::optional<Msg> handle(InstallAuth e) {
        update_auth(io_, e.clear ? auth::AuthHeader{}
                            : provider::credentials::resolve(io_, e.provider));
        return handle(LoadAuthView{});
    }
    std::optional<Msg> handle(LoadAuthView) {
        // 通过设置接口边界，可以看到同一批AccountOp
        // 写入的密钥。
        auto s = deps(io_).load_settings();
        auto view = provider::load_auth_view(io_, s);
        return Msg{msg::LoginMsg{AuthViewLoaded{
            std::move(view), std::move(s.provider_keys)}}};
    }

    void handle(AccountOp e) {
        namespace cr = provider::credentials;
        using K = AccountOp::Kind;
        with_unlocked([&](const maya::unlocked& cap) {
            switch (e.kind) {
                case K::Activate:    (void)cr::activate(io_, cap, e.provider, e.label); break;
                case K::Remove:      (void)cr::remove(io_, cap, e.provider, e.label); break;
                case K::Register:
                    (void)auth::accounts::snapshot_active(io_, cap, e.provider, e.label);
                    break;
                case K::AddKey:      (void)cr::add_key(io_, cap, e.provider, e.key); break;
                case K::SignOut:     auth::vault::sign_out(io_, cap, e.provider); break;
                case K::ClearActive: cr::clear_active(io_, cap, e.provider); break;
            }
        });
    }

    void handle(SaveCredentials e) {
        auth::save_credentials(io_, e.creds);
        namespace acc = auth::accounts;
        const std::string provider = "anthropic";
        // 同一账户的Re-login重复使用其标签； "+ Add another"
        // 选择下一个免费的。
        std::string base = acc::derive_current_label(io_, provider);
        if (base.empty()) base = "account";
        std::string label = base;
        if (e.as_new_account)
            for (int n = 2; acc::get(io_, provider, label).has_value() && n < 100; ++n)
                label = base + " " + std::to_string(n);
        with_unlocked([&](const maya::unlocked& cap) {
            acc::snapshot_active(io_, cap, provider, label);
        });
    }

    // ChatGPT登录的凭据写入(参见store_fx.hpp)。
    // 循环 线程，步骤之间：`unlocked`。记录失败；
    // 接下来的提供商切换没有找到 凭据，下一回合报告
    // "not signed in"，这与用户将看到的状态相同。
    void handle(SaveCodexCredentials e) {
        with_unlocked([&](const maya::unlocked& cap) {
            if (!provider::chatgpt::save_codex_credentials(cap, e.creds))
                AGT_LOG(Auth, Warn, "codex.save",
                        "signed in, but encrypted credentials could not be saved");
        });
    }

    // 设置通过write-behind、接口边界、NOT直接写入磁盘。
    //
    // 一个保存是加载-modify-fsync-rename。 副作用 在两个
    // 帧 之间的循环 线程 上运行，每次击键时都会出现 面板 保存 — 因此，
    // 在这里调用 persistence::save_settings 会在输入
    // 路径中放置一个磁盘 round-trip，
    // 这正是 runtime/app/settings_cache.hpp 存在需要消除的故障。
    // `deps().save_settings` 是 接口边界（install_deps
    // 将存储包含在其中，因此没有 opt-in 需要忘记）：它发布到 内存，返回，
    // 并让一个 工作线程 执行 IO，将持有的 -down 密钥合并为一次写入。
    //
    // 耗尽在拆卸时发生一次——缓存将自身注册到util::
    // teardown——因此由退出臂排队的保存仍然到达磁盘。
    //
    // 这是经过Deps而不是直接到达持久化的副作用，
    // 并且值得不对称：替代方案是存在于主机中的
    // 第二个write-behind实现。
    void handle(SaveSettings e) { deps(io_).save_settings(e.settings); }

    void handle(WriteFile e) {
        // Best-effort，与 Deps 接口边界
        // 完全相同：diff-review 拒绝重写文件，
        // 仅保留已接受的 差异块。下次读取时会出现错误（面板
        // 显示文件未更改），并且抛出 副作用 会导致循环停止，
        // 以便用户可以从中恢复。
        (void)tools::util::write_file(std::filesystem::path{e.path},
                                      e.contents);
    }

private:
    // 步骤之间的循环线程没有任何内容；请参阅上面的注释。
    // 该文件是副作用主机的能力根(lint:cap_roots)。
    template <class F>
    static decltype(auto) with_unlocked(F&& f) {
        return maya::thread_root(std::forward<F>(f));
    }

    // 主机运行副作用；这就是 I/O 的用途。
    Io io_ = IoAccess::grant();
};

}  // namespace agentty::app
