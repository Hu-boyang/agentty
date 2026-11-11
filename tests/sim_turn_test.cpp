// 转子状态机,下为maya::sim.
//
// agentty 相位为 变体 - 闲着, 流式传输, AwaitingPermission,
// ExecutingTool ——和 状态更新函数 两人之间从十几个地方移动:
// a 数据流 完成, 一个工具请求 权限, Esc, a 重试 ,
// 在转弯途中排队发信。过渡覆盖一个 -by-one 手动
// -written 测试,正是这个覆盖形状错过了
// INTERLEAVINGS: a StreamError 来时
// 权限 提示词 正在上, 工具结果和下一个请求之间有一个 Tick 登陆。
//
// 图像 <P> 以模拟时间和种子随机任务驱动真实程序
// -completion 顺序,并重新 -checks 每隔一段
// 时间 不变量 每一个消息之后。 `explore` 并用同样的
// 方案 来购买一手的种子 -written 测试是没人想写下来的命令
//
// 电话 不变量 故意来这的 STRUCTURE
// 而不是行为 任何信息后必须持有的东西 这使得
// 他们可以安全地 主张反对随机的分离
#include "agtest.hpp"

#include <filesystem>

#include <maya/runtime.hpp>
#include <maya/device/events.hpp>

#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/io/blob_gc.hpp"
#include "agentty/util/modelsdev.hpp"

using namespace agentty;
using P = app::AgenttyApp;

// 模拟驱动器带有信息;它需要程序宣布的事件.
using Sim = maya::sim<P>;

namespace {

// 一个不触摸任何东西的商店。 模拟是针对状态机器的,
// 而不是持久化。 而写真实设置文件的测试将是
// 改变自己的机器的测试。
struct NullStore {
    std::vector<Thread> load_threads() { return {}; }
    std::optional<Thread> load_thread(const ThreadId&) { return std::nullopt; }
    std::vector<Thread> backfill_summaries(const std::vector<ThreadId>&) { return {}; }
    void save_thread(const Thread&) {}
    void delete_thread(const ThreadId&) {}
    store::Settings load_settings() { return {}; }
    void save_settings(const store::Settings&) {}
    void save_settings_quiet(const maya::unlocked&, const store::Settings&) {}
    ThreadId new_id() { return ThreadId{"sim"}; }
    std::string title_from(std::string_view t) { return std::string{t}; }
};

struct NullProvider {
    provider::StreamResult stream(const maya::unlocked&, provider::Request, provider::EventSink) {
        return provider::StreamResult::failed("sim");
    }
};

NullProvider g_provider;
NullStore    g_store;

void install() { app::install(g_provider, g_store, auth::AuthHeader{}); }

// init() 并提交探测器,检查cwd。 仓库 这是加的基特 -A
// 全体会议 源码树 每种子( 20分钟后) CI ),所以在空目录中运行。
struct InEmptyDir {
    TempDir dir{"sim_turn"};
    std::filesystem::path prev = std::filesystem::current_path();
    InEmptyDir() { std::filesystem::current_path(dir.path()); }
    ~InEmptyDir() { std::error_code ec; std::filesystem::current_path(prev, ec); }
};

// init() 开始自有背景 线程 (blob) 发音: GC ,
// 则 models.dev 刷新),那个 main() 出来时加入,测试 二进制程序
// 没有这样的关闭,和~ std::thread 静静地 -joinable
// 手柄 调用 std::terminate ——套房通过,然后以134号流产,每个sim
// TEST_CASE 运行 init() 加入他们的方式 main() 已经。
struct JoinsBackgroundThreads {
    ~JoinsBackgroundThreads() {
        modelsdev::join_background_refresh(agtest::cap());
        blobs::join_background_gc(agtest::cap());
    }
};

// 每一个 不变量 必须在后面保持 任意 留言。
void arm_invariants(Sim& s) {
    // 1. 转动的背景生活 内部 阶段,所以 "active" "有一种
    //    上下文“是同一语句。这是替换 过期-turn
    //    守卫:如果他们能分开,死转弯后传来的
    //    迟到信息,就会再次有腐朽的痕迹.
    s.check("active iff a turn context exists", [](const P::Model& m) {
        const bool has_ctx = agentty::active_ctx(m.s.phase) != nullptr;
        return m.s.active() == has_ctx;
    });

    // 2. 每一工作阶段都独家使用。 变体 给这个
    //    结构; 检查在这里, 因此一个未来阶段 添加到它的
    //    旁边的 BOOL (通常的回归) 立即失败。
    s.check("idle excludes streaming and permission", [](const P::Model& m) {
        if (!m.s.is_idle()) return true;
        return !m.s.is_streaming() && !m.s.is_awaiting_permission();
    });

    // 3. A 权限 提示词 意味着要返回,失去这个
    //    批准最终无任何可恢复。
    s.check("a permission prompt belongs to a turn", [](const P::Model& m) {
        if (!m.s.is_awaiting_permission()) return true;
        return agentty::active_ctx(m.s.phase) != nullptr;
    });

    // 4. 作曲者的光标总是在它的文本中。
    //    独立地维护它,这正是为什么它值得在
    //    全球坚持。
    s.check("composer cursor is in range", [](const P::Model& m) {
        const int n = static_cast<int>(m.ui.composer.text.size());
        return m.ui.composer.cursor >= 0 && m.ui.composer.cursor <= n;
    });

    // 5. 冻结前缀永远不能声称存在的信息更多。
    //    记录稿里有索引,从几个地方提前
    s.check("frozen prefix fits the transcript", [](const P::Model& m) {
        return m.ui.frozen_through <= m.d.current.messages.size();
    });
}

}  // 命名空间结束。

TEST_CASE("sim: the turn machine holds under a scripted interleaving") {
    InEmptyDir cwd;
    install();
    JoinsBackgroundThreads joiner;   // 参看其评论

    maya::sim_options opt;
    opt.max_steps = 2'000;
    Sim s(0xA9E77C0Dull, opt);
    arm_invariants(s);

    // 可能的话会话: 键入、提交、工具询问、
    // 用户回答 Esc 降落在中间的某个地方 POINT
    // 即由模拟决定每项任务结果何时到达。
    s.at(std::chrono::milliseconds{0},   Msg{msg::ComposerMsg{ComposerCharInput{U'h'}}});
    s.at(std::chrono::milliseconds{1},   Msg{msg::ComposerMsg{ComposerCharInput{U'i'}}});
    s.at(std::chrono::milliseconds{2},   Msg{msg::ComposerMsg{ComposerSubmit{}}});
    s.at(std::chrono::milliseconds{20},  Msg{msg::StreamMsg{StreamStarted{}}});
    s.at(std::chrono::milliseconds{30},  Msg{msg::StreamMsg{StreamTextDelta{"one"}}});
    s.at(std::chrono::milliseconds{40},  Msg{msg::StreamMsg{CancelStream{}}});
    s.at(std::chrono::milliseconds{50},  Msg{msg::StreamMsg{StreamTextDelta{"late"}}});
    s.at(std::chrono::milliseconds{60},  Msg{msg::StreamMsg{StreamFinished{}}});

    auto r = s.run();
    CHECK_MESSAGE(r.ok(), r.describe());
}

// 在许多种子上,情况是一样的。每个种子都重新排列任务结果,
// 并不同地断开联系,所以这是关于没有人
// 记下来的交叉的部分。
TEST_CASE("sim: the turn machine holds across 200 seeds") {
    InEmptyDir cwd;
    install();
    JoinsBackgroundThreads joiner;   // 参看其评论

    maya::sim_options opt;
    opt.max_steps = 2'000;
    // 晚到的结果是有趣的案例,所以让延迟发生大不相同。
    opt.task_latency_max = std::chrono::milliseconds{40};

    auto out = maya::explore<P>(1, 200, opt, [](Sim& s) {
        arm_invariants(s);
        s.at(std::chrono::milliseconds{0},  Msg{msg::ComposerMsg{ComposerCharInput{U'x'}}});
        s.at(std::chrono::milliseconds{1},  Msg{msg::ComposerMsg{ComposerSubmit{}}});
        s.at(std::chrono::milliseconds{10}, Msg{msg::StreamMsg{StreamStarted{}}});
        s.at(std::chrono::milliseconds{15}, Msg{msg::StreamMsg{StreamTextDelta{"a"}}});
        s.at(std::chrono::milliseconds{25}, Msg{msg::StreamMsg{CancelStream{}}});
        s.at(std::chrono::milliseconds{30}, Msg{msg::StreamMsg{StreamFinished{}}});
    });

    const std::string why =
        out.failure ? out.failure->describe() : std::string{"ok"};
    CHECK_MESSAGE(out.ok(), why);
}

// 错误注入: 无法崩溃或永远返回的任务不能离开机器的楔形。
// 这是一个碎片提供者实际走的路。
TEST_CASE("sim: a lost or crashing task leaves the machine consistent") {
    InEmptyDir cwd;
    install();
    JoinsBackgroundThreads joiner;   // 参看其评论

    maya::sim_options opt;
    opt.max_steps  = 2'000;
    opt.task_crash = 0.15;   // 编号 正文 掷出
    opt.task_lose  = 0.15;   // 编号 正文 永远没有完成

    auto out = maya::explore<P>(1, 100, opt, [](Sim& s) {
        arm_invariants(s);
        s.at(std::chrono::milliseconds{0}, Msg{msg::ComposerMsg{ComposerCharInput{U'q'}}});
        s.at(std::chrono::milliseconds{1}, Msg{msg::ComposerMsg{ComposerSubmit{}}});
        s.at(std::chrono::milliseconds{9}, Msg{msg::StreamMsg{CancelStream{}}});
    });

    const std::string why =
        out.failure ? out.failure->describe() : std::string{"ok"};
    CHECK_MESSAGE(out.ok(), why);
}
