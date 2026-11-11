// 电话 jaal 钩子 AgenttyApp 一定要真正曝光。
//
// 每个都是jaal中的OPTIONAL:用`requires`测试检测到,
// 因此一个签名不能是调用不是一个错误——它读作
// "this program doesn't have that hook",而内核没有它就悄悄地做了.
//
// 就是这样 jaal 迁移中断启动。 `init` 留着旧的 运行时's
// `static std::pair<Model, Cmd> init()`
// 改为 jaal's `Cmd init(Model&)` ,所以,
// 我说 `has_init` 错误, 内核值 -initialised 编号
// Model ,以及所有东西 init() 加载 — 已保存 设置, 设置 线程 列表,
// 活动提供者 —— 被建并丢弃。 agentty 无法在重启和显示第一个时
// 记住任何内容 -run 每次发射都开卡,没有诊断
//
// A 编译-time 唯一能捕捉到这种虫子的是...
// 没有失败的 运行时 沉默的就更没有效果了
#include "agtest.hpp"

#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/host.hpp"

#include <maya/host/sources.hpp>
#include <maya/host/terminal.hpp>

using P = agentty::app::AgenttyApp;

// ── 输入 ─────────────────────────────────────────────────────────────────
// THE回归。 必须是 jaal 调用 它: 填入,
// 返回第一个 Cmd。
static_assert(requires(P::Model& m) {
                  { P::init(m) } -> std::convertible_to<P::Cmd>;
              },
              "AgenttyApp::init must be `Cmd init(Model&)`. jaal detects init "
              "with a requires-test, so a different signature silently means "
              "'no init' — the kernel hands the app a blank Model and every "
              "saved setting and thread is dropped at startup.");

// ── 合同的其余部分 ─────────────────────────────────────────────
static_assert(maya::RuntimeProgram<P>,
              "AgenttyApp must satisfy maya::RuntimeProgram.");
static_assert(maya::Program<P>,
              "AgenttyApp must satisfy maya::Program (RuntimeProgram + view()).");
static_assert(maya::Subscribing<P>,
              "AgenttyApp::subscribe must be `Sub subscribe(const Model&)`; "
              "otherwise the app runs with NO event sources — no keys, no "
              "timers — and simply sits there.");
static_assert(maya::HasVisualHash<P>,
              "AgenttyApp::visual_hash must be `std::uint64_t (const Model&)`; "
              "without it the host re-runs view() every wakeup.");
static_assert(maya::HasNeedsWarmup<P>,
              "AgenttyApp::needs_warmup must be `bool (const Model&)`.");

// 实际运行的宿主必须能够执行每个 副作用 在应用程序 Cmd 划线
// `require_host_for` 无法做到时, 给罪犯起名。
//
// app::Host ,没有 maya::terminal_host: agentty
// 单行携带自己的 持久化 副作用 (save_thread, write_file ,
// ) 在顶部 maya's 终端 来点,还有 app::Host 是处理两者的包装器。
// Asserting maya 这儿的主人会说些什么 agentty 永远不要继续
TEST_CASE("program: agentty's Cmd row is runnable by its host") {
    maya::require_host_for<agentty::app::Host<P>, P>();
    CHECK(true);   // 到达这里意味着静态检查 最重要的是通过
}

// ── 东道主的 自身 可选钩 ─────────────────────────────────────
//
// 同样的需要-means-absent陷阱与上面的程序钩一样,
// 一个层下——它花费了一个真正的调试会话.
//
// jaal 调用 宿主的电话 attach() 仅限 `if constexpr (requires { host.attach(cx) })` ,
// 则Cx所在位置 `host_context<TheHost>`. maya's
// terminal_host 原申报 `attach(host_context<terminal_host>&)`
// - 准确的类型。 agentty's Host DERIVES 从它
// 添加 持久化 副作用 ,因此其背景是: `host_context<Host>` ,
// 要求 -test 失败, 内核静悄悄跳过 attach()。
// 该词 终端 其输入从未在反应堆中注册: agentty 打开、涂漆、接受不键。
// 没有记录、没有失败,它只是坐在那里。
//
// 由 maya 的钩子在上下文类型上调温和而固定。
// 这些钩子上印有固定提供的财产,因为没有其他东西会发现它丢失。
using H = agentty::app::Host<P>;

static_assert(requires(H& h, maya::host_context<H>& cx) { h.attach(cx); },
              "Host::attach must accept host_context<Host>. jaal detects it "
              "with a requires-test, so a signature it can't call means the "
              "terminal's input is never registered — the app runs but takes "
              "no keys, silently.");

static_assert(requires(H& h, maya::host_context<H>& cx, maya::readiness r) {
                  h.on_ready(cx, r);
              },
              "Host::on_ready must accept host_context<Host>, or input that "
              "IS registered is never read.");

static_assert(requires(H& h, maya::host_context<H>& cx, maya::sig s) {
                  h.on_signal(cx, s);
              },
              "Host::on_signal must accept host_context<Host>, or SIGWINCH "
              "never reaches the program and a resize is ignored.");
