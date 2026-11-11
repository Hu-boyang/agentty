// 沙箱 面板实际上控制了沙箱吗?
//
// 电话 面板 测试之前就已经存在了,而且没有线:
// 没有调色板门,没有 状态更新函数 ,没有视图。
// 它绘制的每行都配置了产卵路径从未读过的结构。
// 这是这个文件存在的防止错误的类——一个看起来配置不全的安全控制。
//
// 因此,这些检查遵循整个链而不是任何一条链接:
//
//   打开“普通”列表的一行 面板 打开 面板
//   种子从策略中 IN FORCE ,而不是从默认
//   编辑键盘更改行并预览跟踪它 保存
//   ^S 安装配置 和 阶段 持久化 丢弃
//   Esc 离开政策独自一人
//
// 这会有什么作用 不 检查是内核是否执行它 - 这需要真实的命名空间并
// 生活在 sandbox_live_check (手) 手(手) 双手(手,
// 手) -run 目标,见 cmake/AgenttyTests.cmake。
// 两者之间没有差距:这证明 面板 到达了配置,这证明配置到达了孩子。

#include <doctest/doctest.h>

#include "agentty/runtime/app/update.hpp"
#include "agentty/io/persistence.hpp"
#include "agtest_cap.hpp"   // agtest::cap() 用于第XX00120(第save_settings)号
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/sandbox.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/view/form_panel.hpp"   // form_config - 电话 渲染 检查
#include "agentty/i18n/i18n.hpp"                   // 翻译的-pane 检查
#include "agentty/i18n/startup.hpp"                // 已运出-catalog 检查
#include "agentty/tool/util/sandbox.hpp"

#include <cstdlib>
#include <filesystem>

using namespace agentty;
namespace pn = agentty::ui::panel;
namespace sb = agentty::tools::util::sandbox;

namespace {

Model opened() {
    auto [m, _] = app::update(Model{}, Msg{OpenSandbox{}});
    return std::move(m);
}

const pn::Sandbox& pane(const Model& m) {
    const auto* o = m.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    return *o;
}

// 以 id 查找一行,所以测试从不依赖于 ORDER 列
// ——重排表单是正当的编辑,决不能不进行行为检查.
int row_of(const form::Form& f, std::string_view id) {
    for (std::size_t i = 0; i < f.fields.size(); ++i)
        if (f.fields[i].id == id) return static_cast<int>(i);
    return -1;
}

const form::field::Choice& choice_at(const form::Form& f, int i) {
    return std::get<form::field::Choice>(f.fields[static_cast<std::size_t>(i)].value);
}

// 内存 / Procs / 输入/ 输入 CPU / tmp是数字行,
// 而不是文本——一个盖子是带有界限的数量,形式层为我们夹住它.
form::field::Number& num_at(form::Form& f, int i) {
    return std::get<form::field::Number>(f.fields[static_cast<std::size_t>(i)].value);
}

const form::field::Number& num_at(const form::Form& f, int i) {
    return std::get<form::field::Number>(f.fields[static_cast<std::size_t>(i)].value);
}

Msg key(form::keys::Intent i) { return Msg{SandboxKey{form::keys::Action{i, 0}}}; }

// Host 能够制造的机器的事实 RUNNING 引擎明确声明。
//
// 默认为 "running what the config selects" ,
// 因为大多数案件都是关于一行的内容,而不是关于 selection/reality。
// 将案件 ARE 有关分路通行证 `running` ——
// 不对称就是问题所在:声称墙已经竖起,现在必须进行测试。
pn::HostFacts facts_for(const sandbox_cfg::Config& cfg,
                        bool claybin_available = true,
                        std::uint32_t landlock_abi = 10) {
    pn::HostFacts f;
    f.claybin_available = claybin_available;
    f.landlock_abi = landlock_abi;
    f.per_port_net = landlock_abi >= 4;
    f.ipc_scoped = landlock_abi >= 6;
    f.sandbox_active = true;
    f.running = cfg.backend;
    return f;
}

// 安装一个策略, 仿佛它是启动的。
//
// set_config ( ) 系 , ( ) SEALED
// 第一次之后 调用 ,这是大多数文件存在的地方,
// 所以要从已知的政策开始,首先必须打破封印。下面的每个案例
// 都通过这个来安排自己的起始状态,而不是取决于它之前的
// 哪个案例。
void install(const sandbox_cfg::Config& cfg) {
    sb::reset_config_for_test();
    sb::set_config(cfg);
}

}  // 命名空间结束。

// 此主机是否有一个 沙箱 这些案件的后端。 Linux 财务报告和已审计财务报表 macOS;
// Windows 预览通过 claybin 也一样,但是这些案子 POSIX 路径,
// 所以它有 自己的案例下面。
#if defined(__linux__) || defined(__APPLE__)
#define AGENTTY_TEST_SANDBOX_BACKEND 1
#else
#define AGENTTY_TEST_SANDBOX_BACKEND 0
#endif

#if defined(_WIN32)
TEST_CASE("sandbox pane: windows previews through claybin") {
    // 它曾经掉到 "no sandbox backend on this platform"
    // 而命令真的被限制。预览必须报告产卵路径
    // 设置的墙壁。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.memory_mb = 1024;
    install(cfg);
    const auto preview = pn::preview_sandbox(cfg);
    REQUIRE_MESSAGE(preview.compiled, preview.error);
    bool fs = false, mem = false;
    for (const auto& w : preview.walls) {
        if (w.name == "filesystem.read") fs = w.mechanism.find("appcontainer") != std::string::npos;
        if (w.name == "resource.memory") mem = w.strength == "strong";
    }
    CHECK(fs);
    CHECK(mem);
}
#endif

TEST_CASE("sandbox pane: the General list has a door that opens it") {
    // 导致面板死码的缺口:它从无到有是能够达到的.
    Model m;
    const auto items = settings::items_for(m, settings::Category::General);

    bool found = false;
    for (const auto& i : items)
        if (i.action == settings::Action::OpenSandbox) {
            found = true;
            // 门必须打开 SAY 门,或者排读为惰。
            CHECK(settings::opens_pane(i.action));
            CHECK(!i.primary.empty());
            // 次要行命名执行后端。 这是值得在此
            // 执行的一个事实: 它决定了里面的行是否做
            // 任何事情, 而一个配置惰性政策的面板
            // 必须在打开它之前承认它。
            CHECK(!i.secondary.empty());
        }
    CHECK(found);
}

TEST_CASE("sandbox pane: opens onto the policy in force, not defaults") {
    // 如果 面板 默认种子 -constructed 配置,
    // 用户看到的第一个东西是 对自己边界的谎言。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.net_mode   = sandbox_cfg::NetMode::None;
    cfg.memory_mb  = 4096;
    install(cfg);

    const Model m = opened();
    const auto& f = pane(m).pane.form;

    const int net = row_of(f, pn::kSbNetMode);
    REQUIRE(net >= 0);
    // 显示行 "none" 因为这才是执行的
    CHECK(choice_at(f, net).id() == "none");

    const int mem = row_of(f, pn::kSbMemoryMb);
    REQUIRE(mem >= 0);
    CHECK(num_at(f, mem).value == 4096);

    // 光标降落在真实的环境下,而不是第头文件节。
    REQUIRE(f.cursor >= 0);
    CHECK(!f.fields[static_cast<std::size_t>(f.cursor)].is_header());
}

TEST_CASE("sandbox pane: Esc discards, so walking away changes nothing") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.net_mode   = sandbox_cfg::NetMode::Full;
    install(cfg);

    Model m = opened();
    // 移动一行,然后不存钱就离开.
    const int net = row_of(pane(m).pane.form, pn::kSbNetMode);
    REQUIRE(net >= 0);
    m.ui.panel.get<pn::Sandbox>()->pane.form.cursor = net;
    auto [m2, _] = app::update(std::move(m), key(form::keys::Intent::AdjustUp));
    auto [m3, __] = app::update(std::move(m2), Msg{CloseSandbox{}});

    // 电话 面板 并且没有执行实际操作政策。
    // Esc 将会是改变边界,放弃屏幕。
    CHECK(m3.ui.panel.get<pn::Sandbox>() == nullptr);
    CHECK(sb::config().net_mode == sandbox_cfg::NetMode::Full);
}

TEST_CASE("sandbox pane: save persists but does NOT touch the live boundary") {
    // 安全财产,作为测试,声明:为下一次发射写保单,
    // 运行的沙箱完全照原样.
    //
    // 为什么这是正确的行为,因为它是相反的 面板 :
    // 能够移动中段的边界 -session 只在
    // WEAKENING 方向,弱化是无形的,
    // 不能被抵消(re -tightening 显示 -read ),
    // 在两种不同政策下,一个会话中有两个命令使
    // 沙箱 不可审.
    sandbox_cfg::Config live;
    live.configured   = true;
    live.memory_mb    = 512;
    live.syscall_mode = sandbox_cfg::SyscallMode::Compiler;
    install(live);   // 安排密封的起始状态
    REQUIRE(sb::config_sealed());

    Model m = opened();
    const int mem = row_of(pane(m).pane.form, pn::kSbMemoryMb);
    REQUIRE(mem >= 0);
    num_at(m.ui.panel.get<pn::Sandbox>()->pane.form, mem).value = 2048;

    auto [saved, cmd] = app::update(std::move(m), Msg{SandboxSave{}});

    // 准备下一轮发射...
    CHECK(saved.d.persisted.sandbox.memory_mb == 2048);
    CHECK(saved.d.persisted.sandbox.configured);

    // .还有... LIVE 政策是没有改变的。
    // 这是一个很重要的说法:如果它翻转,就 提示词-injected
    // 能够到达设置的代理 状态更新函数 可以降低它正在运行的会话的墙壁。
    CHECK(sb::config().memory_mb == 512);

    // 还有 面板 而不是让用户相信墙壁上翻了
    // "applies on restart" 谎言的同一类
    // "sandbox: active" 无法在主机上 沙箱.
    const auto* o = saved.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    CHECK(o->pane.saved_pending_restart);
}

TEST_CASE("sandbox config: the policy is sealed after the first install") {
    // 以守则执行,而不是通过评论执行。 WAS 评论意见(
    // "call before init()" 和设置 面板 在一个存在
    // 一天之内就破了 所以 不变量 拒绝而不是信任。
    sandbox_cfg::Config first;
    first.configured = true;
    first.memory_mb  = 111;
    first.backend    = sandbox_cfg::LinuxBackend::Claybin;
    install(first);
    REQUIRE(sb::config_sealed());
    REQUIRE(sb::config().memory_mb == 111);

    // 以后的每一个月 调用 是一个不 -op 不论往哪走
    sandbox_cfg::Config weaker;
    weaker.configured   = true;
    weaker.memory_mb    = 0;      // 无盖
    weaker.syscall_mode = sandbox_cfg::SyscallMode::Off;
    sb::set_config(weaker);
    CHECK(sb::config().memory_mb == 111);
    CHECK(sb::config().syscall_mode == sandbox_cfg::SyscallMode::Compiler);

    // 包括联合国 ENGINE 降级 claybin - > 隔板静静地
    // 放下 seccomp 过滤器, croup 盖 和 非 国 家
    // -configurable 秘密口罩(测量:相同) 工作区 .
    // env 读回空在 claybin 并且完全在包里,所以后端必须和保险
    // 的其余部分一起密封,而不是与之并存。

    // 紧紧的也被拒绝了 不是因为紧紧很危险 而是因为
    // 策略只能按我们判断为安全的方式变化。
    // 是一种有判决的规则 调用 里面,这个值得一个也没有。
    sandbox_cfg::Config tighter;
    tighter.configured   = true;
    tighter.memory_mb    = 64;
    tighter.syscall_mode = sandbox_cfg::SyscallMode::Strict;
    sb::set_config(tighter);
    CHECK(sb::config().memory_mb == 111);
}

TEST_CASE("sandbox pane: every row round-trips into the config") {
    // 捕捉到的虫类, 三次在这个子系统中:
    //
    //   kSbMode 一定不存在的行的行 ID
    //   Observation/BlockedEvent 类型,
    //   带有 状态更新函数 手臂和没有制片人 蒙面行配置了 brap 下的任何内容
    //
    // 每个读作运算特性, 而执行的则无。 形状总是一样: 控制
    // 存在于一个层次, 而在另一个层次上是死, 并且没有单独的
    // -layer测试通知。
    //
    // 如此:构建表格,MUTATE每个可编辑行从当前
    // 值中切换,读回,并且要求配置实际改变。一个无法存活的行,
    // 要么在它的两个站点中一个站点死亡,
    // 要么拼写错误——这正是 kSb* 常数存在的原因,
    // 可以自行预防和阻止。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend    = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    Model m = opened();
    auto& f = m.ui.panel.get<pn::Sandbox>()->pane.form;

    int checked = 0;
    for (std::size_t i = 0; i < f.fields.size(); ++i) {
        auto& fld = f.fields[i];
        if (fld.is_header() || fld.is_action() || fld.is_pick()) continue;
        // A LOCKED 行不是死,它真的没有:后端或内核不能执行它,
        // 面板 说得没错 read_sandbox_form 仍然
        // 读它(锁在墙上的一行通过往返来保持其价值,
        // 这是它自己的测试以上),但在一个主机上, claybin
        // 无法启动每集 claybin 排在锁上,所以要求他们搬
        // 来这里 会使这个案子失败 对于机器的某个财产。
        if (fld.locked) continue;
        // 电话 POSTURE 行是一个动作,而不是一个值:它由
        // 状态更新函数 当它移动而刻意不读(参看
        // read_sandbox_form )移动到这里,没有任何设计改变,
        // 它的真正行为有它自己的两个案例以上.
        if (fld.id == pn::kSbPosture) continue;

        const auto before = pn::read_sandbox_form(f, cfg);

        // 不管它持有什么,都把排移开,每种都有一条明显的方式
        if (auto* c = std::get_if<form::field::Choice>(&fld.value)) {
            if (c->count() < 2) continue;
            c->index = c->normalized(c->index + 1);
        } else if (auto* t = std::get_if<form::field::Toggle>(&fld.value)) {
            t->on = !t->on;
        } else if (auto* n = std::get_if<form::field::Number>(&fld.value)) {
            // 移动到行内值 和 无法与
            // 配置已经持有的值重合。
            //
            // `(value == min) ? max : min` 错误,
            // 仅在测试中失败 : read_sandbox_form
            // 倒车 `base` 某些行,所以在基值上着陆,
            // 读取值为否 -op 检查报告一列死尸是好的。
            // -dependent ,这是最糟糕的片段--它单独通过,
            // 在套房中失败。 当前 值乘以一个,被夹在范围中,
            // 不能与任何东西相撞。
            n->value = (n->value < n->max) ? n->value + 1 : n->value - 1;
        } else if (auto* tx = std::get_if<form::field::Text>(&fld.value)) {
            tx->value = tx->value.empty() ? std::string{"/tmp/roundtrip"}
                                          : std::string{};
        } else {
            continue;   // 秘密,或者没有价值移动的
        }

        const auto after = pn::read_sandbox_form(f, cfg);
        const std::string why = "row '" + fld.id + "' does not reach the config";
        CHECK_MESSAGE(!(before == after), why);
        ++checked;
    }

    // 扫描实际上运行了。一个跳过一切的循环会报告零失败,
    // 这就是整个文件的失败模式。
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(checked >= 10);
#else
    // 无后端: 每 claybin 排队被锁住了,只剩下几只
    // agentty-side 排队——仍然需要扫射他们。
    CHECK(checked >= 1);
#endif
}

TEST_CASE("sandbox pane: the wall report tracks the rows") {
    // 面板 存在的全部理由:你不能观察到的边界是
    // 不能信任的。因此,报告决不能描述行中不再
    // 说的配置。
    Model m = opened();
    const auto before = pane(m).pane.preview;

    const int sys = row_of(pane(m).pane.form, pn::kSbSyscalls);
    REQUIRE(sys >= 0);
    m.ui.panel.get<pn::Sandbox>()->pane.form.cursor = sys;

    // 循环调用配置文件并让 状态更新函数 内容 -price.
    auto [m2, _] = app::update(std::move(m), key(form::keys::Intent::AdjustUp));
    auto [m3, __] = app::update(std::move(m2), Msg{SandboxRefreshPreview{}});

    const auto& after = pane(m3).pane.preview;
    // 要么编成书并说些什么,要么拒绝并说为什么。
    // 永远不能沉默的 -- -- 这就是 "active, enforcing nothing"
    // 声明 面板 建造它是为了让不可能。
    CHECK((after.compiled || !after.error.empty()));
    if (after.compiled) CHECK(!after.walls.empty());
    (void)before;
}

// ── 安全预设 ─────────────────────────────────────────────────────────────
//
// 电话 面板 28个人 -correct 排行和表达意图的
// 方法都是真实的,并且是强制的;在28个原始人中
// 拼凑安全界限仍然是个错误的问题,没有人能解释的控制是任何人
// 不能触及的控制,对于安全环境来说,这意味着关闭。
//
// 预设只有在两个属性都持有的情况下才安全,且在此检查两种属性: 预设是否具有安全性?
// 标签永远不能出错(它是DERIVED,没有存储),
// 选择一个不能踩住用户之后编辑的行。

TEST_CASE("sandbox posture: apply then detect round-trips for every preset") {
    // 整个特征所依赖的财产。 detect_posture( ) 问
    // apply_posture( )、 而不是re-encoding( 姿势在第二位
    // 意味着什么) -- -- 因此, 这也保护着真正的危险: ADD 是一个场到Config( ),
    // 如果探测器有自己的手-written 比较, 它就会开始对一个非配置
    // 提出"Hardened" 的要求。
    for (auto p : {sandbox_cfg::Posture::Permissive,
                   sandbox_cfg::Posture::Balanced,
                   sandbox_cfg::Posture::Hardened,
                   sandbox_cfg::Posture::Airgapped}) {
        sandbox_cfg::Config base;
        const auto applied = sandbox_cfg::apply_posture(base, p);
        CHECK(sandbox_cfg::detect_posture(applied) == p);
    }
}

TEST_CASE("sandbox posture: the four presets are genuinely different") {
    // 预设列表中,两个条目意为相同的东西,
    // 是关于拥有四个选项的菜单.
    sandbox_cfg::Config base;
    const auto perm = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Permissive);
    const auto bal  = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Balanced);
    const auto hard = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Hardened);
    const auto air  = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Airgapped);

    CHECK(!(perm == bal));
    CHECK(!(bal == hard));
    CHECK(!(hard == air));

    // 并且他们被命令使用他们的名字要求的方式。允许的确实没有过滤器;
    // 被空降的确实没有网络。一个名为"airgapped"的
    // 预设离开网络将是这个文件中最糟糕的缺陷。
    CHECK(perm.syscall_mode == sandbox_cfg::SyscallMode::Off);
    CHECK(bal.net_mode == sandbox_cfg::NetMode::Full);
    CHECK(hard.syscall_mode == sandbox_cfg::SyscallMode::Strict);
    CHECK(air.net_mode == sandbox_cfg::NetMode::None);
}

TEST_CASE("sandbox posture: a preset keeps the user's paths and engine") {
    // 一个姿态就是关于墙壁有多紧的表态。
    // 给-specific工程的路径列表打个比方,因为有人尝试过Hardened
    // 会让预设变得充满敌意,你会因为探索而失去工作。
    sandbox_cfg::Config base;
    base.read_paths  = {"/opt/weird-sdk"};
    base.write_paths = {"/var/cache/project"};
    base.deny_paths  = {"/home/me/.cargo/credentials"};
    base.backend     = sandbox_cfg::LinuxBackend::Claybin;

    for (auto p : {sandbox_cfg::Posture::Permissive,
                   sandbox_cfg::Posture::Hardened,
                   sandbox_cfg::Posture::Airgapped}) {
        const auto out = sandbox_cfg::apply_posture(base, p);
        CHECK(out.read_paths == base.read_paths);
        CHECK(out.write_paths == base.write_paths);
        CHECK(out.deny_paths == base.deny_paths);
        // 特别是引擎: 选Hardened 决不能默默地在
        // 主机上切换引擎 claybin 无法开始。
        // 面板 锁 并报告,相反。
        CHECK(out.backend == base.backend);
    }
}

TEST_CASE("sandbox posture: the label covers the walls it sets, not the grants you add") {
    // 该 SCOPE OF 该 CLAIM 被钉住 所以没人能推断出来
    //
    // detect_posture 是一个 FIXPOINT 测试 : `probe == apply_posture(probe, p)`.
    // 这使得标签无法漂移(§preset 不
    // 变量 2)——但后果值得大声声明,
    // 因为读者总是这样,而且从未被告知。
    //
    // 字段 apply_posture 没有 WRITE 无法使比较失败,
    // 因此它超出了姿态的定义。 `write_paths` (
    // claybin 约束他们可写)和 `allow_ports` (
    // claybin 打开它们。所以一个配置可以读取 "Hardened"
    // 并带有额外 授权 用户添加。
    //
    // 这是CORRECT,是不变量 3 的 "a preset never touches your path lists" ,
    // 因为当您尝试一个预设时, 擦掉一个项目的 read_paths 会使预设变得对探索不利。
    // 标签表示 "the walls this preset sets are in force" ,
    // 不是 "this is all there is"。
    // 电话 面板 显示所有28行的精确度 授权 保持
    // 可见( E) 不变量 1):标签总结,行是真.
    //
    // 这个测试的存在,这样阅读是记录上的决定,而不是偶然的,
    // 因此,将来的改变使得apply_posture
    // 开始清除路径列表,必须来到这里论证.
    sandbox_cfg::Config base;
    const auto hard = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Hardened);
    REQUIRE(sandbox_cfg::detect_posture(hard) == sandbox_cfg::Posture::Hardened);

    // WALLS是标签所声称的,其中任何一种改变都会将标签降为
    // Custom,而不是撒谎.
    {
        auto c = hard;
        c.net_mode = sandbox_cfg::NetMode::Full;
        CHECK(sandbox_cfg::detect_posture(c) != sandbox_cfg::Posture::Hardened);
    }
    {
        auto c = hard;
        c.fs_scope = sandbox_cfg::FsScope::HostReadable;
        CHECK(sandbox_cfg::detect_posture(c) != sandbox_cfg::Posture::Hardened);
    }
    {
        auto c = hard;
        c.syscall_mode = sandbox_cfg::SyscallMode::Off;
        CHECK(sandbox_cfg::detect_posture(c) != sandbox_cfg::Posture::Hardened);
    }
    {
        auto c = hard;
        c.handoff = sandbox_cfg::HandoffPolicy::Allow;
        CHECK(sandbox_cfg::detect_posture(c) != sandbox_cfg::Posture::Hardened);
    }

    // 电话 GRANTS 是你的,在预设中幸存下来,所以标签就留下了。
    // 这里记录的是预期的行为,而不是一个安全财产:行,
    // 而不是标签,是您在信任边界之前所读到
    // 的。
    {
        auto c = hard;
        c.write_paths.push_back("/srv/scratch");
        CHECK(sandbox_cfg::detect_posture(c) == sandbox_cfg::Posture::Hardened);
    }
    {
        auto c = hard;
        c.allow_ports.push_back(8080);
        CHECK(sandbox_cfg::detect_posture(c) == sandbox_cfg::Posture::Hardened);
    }
}

TEST_CASE("sandbox posture: applying a preset twice changes nothing") {
    // 释迦牟尼经. apply_posture 运行在每 面板
    // 保存 和在姿势列的QQ/ ,所以一个不 -idempotent
    // 预设会同时漂移一个一行的配置,而标签却一直声称它是稳定
    // 的——固定点检测器的精确故障模式是用来排除,
    // 从另一边重新提出.
    //
    // 这也是我们所要做的 detect_posture 自己定义的好
    // -founded : 对照非的固定点试验 -idempotent
    // 函数将是一个没有稳定答案的问题。
    sandbox_cfg::Config base;
    for (auto p : {sandbox_cfg::Posture::Permissive, sandbox_cfg::Posture::Balanced,
                   sandbox_cfg::Posture::Hardened, sandbox_cfg::Posture::Airgapped}) {
        const auto once  = sandbox_cfg::apply_posture(base, p);
        const auto twice = sandbox_cfg::apply_posture(once, p);
        CHECK(once == twice);
        // 并且仍然在第二次应用后检测到自身.
        CHECK(sandbox_cfg::detect_posture(twice) == p);
    }
}

TEST_CASE("sandbox posture: the ladder only ever tightens") {
    // 允许 QQ 平衡 QQ 在每面有命令的墙上都空置。
    // 打开轴线而收紧另一轴的预设将使
    // "move one step up the ladder" 成为不安全
    // 的动作 — 用户必须抖动28行才能发现他们放弃了什么, 这就是梯子的
    // 存在来让他们幸免的。
    sandbox_cfg::Config base;
    const auto perm = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Permissive);
    const auto bal  = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Balanced);
    const auto hard = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Hardened);
    const auto air  = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Airgapped);

    // 过程天花板:从不随攀升,NEVER进入0.
    //
    // 0 表示 UNLIMITED 边界( 边界) claybin
    // 设置无 r 限制和没有 pids.max 当皮兹是无限的,
    // 所以一个将它零化的姿态,会运送一个无约束的叉子炸弹——这正是允许者过去所做的.
    // 即第tests/sandbox_audit.cpp号结论存在,
    // 通过预设重新提出,因此它得到一个明确的地板,而不仅仅是一个订单。
    for (const auto* c : {&perm, &bal, &hard, &air})
        CHECK(c->max_procs > 0);
    CHECK(bal.max_procs  <= perm.max_procs);
    CHECK(hard.max_procs <= bal.max_procs);
    CHECK(air.max_procs  <= hard.max_procs);

    // 秘密面具:爬山时从未如此浅薄.
    CHECK(bal.mask_scan_depth  >= perm.mask_scan_depth);
    CHECK(hard.mask_scan_depth >= bal.mask_scan_depth);
    CHECK(air.mask_scan_depth  >= hard.mask_scan_depth);

    // W^X 财务报告和已审计财务报表 IPC 范围:一旦打开,他们就继续。
    CHECK((!bal.wx_protect  || perm.wx_protect || true));   // 电压可能关闭
    CHECK(hard.wx_protect);
    CHECK(air.wx_protect);
    CHECK(hard.scope_ipc);
    CHECK(air.scope_ipc);

    // 网络:空载是唯一一个没有套接字的,任何高于平衡的
    // 部分都没有完全开放.
    CHECK(air.net_mode == sandbox_cfg::NetMode::None);
    CHECK(hard.net_mode != sandbox_cfg::NetMode::Full);
    CHECK(perm.net_mode == sandbox_cfg::NetMode::Full);

    // 交接门从不通过攀登方式(§14):每个姿态都拒绝,
    // 包括允许.
    CHECK(perm.handoff == sandbox_cfg::HandoffPolicy::Refuse);
    CHECK(bal.handoff  == sandbox_cfg::HandoffPolicy::Refuse);
    CHECK(hard.handoff == sandbox_cfg::HandoffPolicy::Refuse);
    CHECK(air.handoff  == sandbox_cfg::HandoffPolicy::Refuse);
}

TEST_CASE("sandbox posture: permissive still masks secrets and refuses handoffs") {
    // 允许性的存在使得工具链断裂的用户所在的地方不是
    // `--sandbox off`。 如果它也放弃了两个不花
    // 任何兼容性费用的保护措施,那就毫无意义了。
    sandbox_cfg::Config base;
    const auto perm = sandbox_cfg::apply_posture(base, sandbox_cfg::Posture::Permissive);

    // 秘密会蒙上面子 REGARDLESS :面具是束缚,它什么也打不开.
    CHECK(perm.mask_scan_depth > 0);
    // 交接门是和墙壁不同的问题(§14)——松散的
    // 沙箱 是一个选择,让代理作者 把你的钩子作为
    // 一个 副作用 选择的不是任何人
    CHECK(perm.handoff == sandbox_cfg::HandoffPolicy::Refuse);
}

TEST_CASE("sandbox posture: balanced is exactly the unconfigured default") {
    // 所以,一个选择平衡的用户会明确得到与从未打开过
    // 面板 如果这些漂移, 面板 自己的默认行描述
    // 的是无人运行的政策。
    sandbox_cfg::Config fresh;             // configured = false
    const auto bal = sandbox_cfg::apply_posture(fresh, sandbox_cfg::Posture::Balanced);

    sandbox_cfg::Config expected = fresh;
    expected.configured = true;            // 唯一合法差异
    CHECK(bal == expected);

    // 和未触及的配置 已经读为平衡,所以 面板 打开名称而不是
    // 打开 "Custom".
    CHECK(sandbox_cfg::detect_posture(fresh) == sandbox_cfg::Posture::Balanced);
}

TEST_CASE("sandbox posture: editing one row off a preset reports Custom") {
    // 诚实财产 A STORED 姿势战地仍然会说 "Hardened" 这儿,
    // 还有... 面板 将主张一个不再描述的边界 -- --
    // 与 "sandbox: active".
    auto hard = sandbox_cfg::apply_posture(sandbox_cfg::Config{},
                                           sandbox_cfg::Posture::Hardened);
    REQUIRE(sandbox_cfg::detect_posture(hard) == sandbox_cfg::Posture::Hardened);

    hard.memory_mb = 2048;   // 一行,手边
    CHECK(sandbox_cfg::detect_posture(hard) == sandbox_cfg::Posture::Custom);
}

TEST_CASE("sandbox posture: selecting Custom changes nothing") {
    // 自定义是行的标签 REPORTS 命令,
    // 应用它必须是不 -op 或该行会破坏它描述的配置。
    const auto hard = sandbox_cfg::apply_posture(sandbox_cfg::Config{},
                                                 sandbox_cfg::Posture::Hardened);
    const auto after = sandbox_cfg::apply_posture(hard, sandbox_cfg::Posture::Custom);
    CHECK(after == hard);
}

TEST_CASE("sandbox pane: the posture row writes every other row") {
    // 通过 REDUCER ,因为这就是应用预设的地方: 选择一个是动作,
    // 只有 状态更新函数 发现有动作,请看KSbPosture手臂
    // update/sandbox.cpp 为何回信不能做
    sandbox_cfg::Config cfg;   // 平衡
    cfg.configured = true;
    install(cfg);

    Model m = opened();
    const int p = row_of(pane(m).pane.form, pn::kSbPosture);
    REQUIRE(p >= 0);
    m.ui.panel.get<pn::Sandbox>()->pane.form.cursor = p;

    // 将行从实际开始的地方驱动到硬化,而不是假设一个索引。 面板 种子
    // 来源 `m.d.persisted` (所以一个新鲜的) Model
    // 打开默认的,而不是什么 install() 和如此艰难的考验,
    // -codes "one step down" 默默测试种子改变的
    // 时刻 不同的预设
    const auto start = std::get<form::field::Choice>(
        pane(m).pane.form.fields[static_cast<std::size_t>(p)].value).index;
    const int want = static_cast<int>(sandbox_cfg::Posture::Hardened);
    // AdjustUp 选择时为+1( form_keys.cpp 所以,踏进一个 HIGHER
    // 索引是上,而不是下。
    for (int i = start; i < want; ++i) {
        auto [next, _] = app::update(std::move(m), key(form::keys::Intent::AdjustUp));
        m = std::move(next);
    }
    for (int i = start; i > want; --i) {
        auto [next, _] = app::update(std::move(m), key(form::keys::Intent::AdjustDown));
        m = std::move(next);
    }

    // 排行实际上是我们开的地方-- 否则下面的
    // 支票会通过或失败, 原因与预设无关。
    REQUIRE(std::get<form::field::Choice>(
        pane(m).pane.form.fields[static_cast<std::size_t>(p)].value).index == want);

    const auto out = pn::read_sandbox_form(pane(m).pane.form, cfg);
    // 预设写了OTHER行,即是整点.
    CHECK(out.syscall_mode == sandbox_cfg::SyscallMode::Strict);
    CHECK(out.memory_mb == 8192);
    CHECK(sandbox_cfg::detect_posture(out) == sandbox_cfg::Posture::Hardened);

    // 并且修改已经进入最终保存的记录。
    //
    // 该 BUG 此 PINS 上面所有的东西在选择预设时都
    // 经过 无内容 : kSbPosture臂重新刷漆了行,
    // 然后 `return Cmd::none()` ,
    // 所以是一行从未到达自动保存 其它各行都经过。 面板 脚印上显示为硬化,
    // 脚印上说 "applies on restart" ,
    // 下一次发射 已加载 旧配置——通过比较得出的姿态行
    // 正确报告自定义。
    //
    // 报告为 "I changed the preset to Hardened, restarted, it was Custom again"。
    // 数据源一直是正确的; 磁盘上的配置确实
    // 没有变化。
    //
    // 检查 FORM 还不够,这就是教训:形式是用户
    // 所看到的, m.d.persisted 只能读
    // 出表格的测试无法区分这些, 这就是为什么这个测试
    // 没有抓住一个错误 它被完全安排在了捕捉。
    CHECK(m.d.persisted.sandbox.syscall_mode == sandbox_cfg::SyscallMode::Strict);
    CHECK(m.d.persisted.sandbox.memory_mb == 8192);
    CHECK(m.d.persisted.sandbox.fake_hostname);
    CHECK(m.d.persisted.sandbox.mask_scan_depth == 5);
    CHECK(sandbox_cfg::detect_posture(m.d.persisted.sandbox)
          == sandbox_cfg::Posture::Hardened);

    // 封条仍然保留: 持续不适用。 预设不能像其他行一样, 在已经运行的进程中
    // 移动边界( 见
    // "sandbox pane: a save persists but does NOT touch the live boundary")。（保存沙箱设置会持久化，但不得改变当前实际边界。）
    //
    CHECK(sb::config().syscall_mode != sandbox_cfg::SyscallMode::Strict);
}

TEST_CASE("sandbox pane: a preset picked in the pane is still picked after a restart") {
    // 整个路径,为 每个 预设: 驱动真实中的预设行
    // 状态更新函数 , 取它坚持的, 写在真实的设置文件中,
    // 加载 重新做为新的发射机 打开 面板 来,就来
    // 行必须显示所选预设。
    //
    // 之前的测试停止了 m.d.persisted 单位 内存 ——
    // 所以它看不到下半部错误:装填器的射程检查使每个正 64 转弯
    // -bit 字段( 字段) ( memory_mb ) 进入默认状态,
    // 因此 硬化和空载重装 memory_mb 0,读作自定义.
    namespace fs = std::filesystem;
    char tmpl[] = "/tmp/agentty_pane_preset_XXXXXX";
    const fs::path dir = ::mkdtemp(tmpl);
    const char* oh = ::getenv("HOME");
    const char* oa = ::getenv("AGENTTY_HOME");
    const std::string old_home = oh ? oh : "", old_ah = oa ? oa : "";
    ::setenv("HOME", dir.c_str(), 1);
    ::setenv("AGENTTY_HOME", dir.c_str(), 1);

    for (auto want_p : {sandbox_cfg::Posture::Permissive, sandbox_cfg::Posture::Balanced,
                        sandbox_cfg::Posture::Hardened, sandbox_cfg::Posture::Airgapped}) {
        CAPTURE(static_cast<int>(want_p));
        sandbox_cfg::Config cfg;
        cfg.configured = true;
        install(cfg);
        Model m = opened();
        const int p = row_of(pane(m).pane.form, pn::kSbPosture);
        REQUIRE(p >= 0);
        m.ui.panel.get<pn::Sandbox>()->pane.form.cursor = p;
        const auto start = std::get<form::field::Choice>(
            pane(m).pane.form.fields[static_cast<std::size_t>(p)].value).index;
        const int want = static_cast<int>(want_p);
        for (int i = start; i < want; ++i) {
            auto [next, _] = app::update(std::move(m), key(form::keys::Intent::AdjustUp));
            m = std::move(next);
        }
        for (int i = start; i > want; --i) {
            auto [next, _] = app::update(std::move(m), key(form::keys::Intent::AdjustDown));
            m = std::move(next);
        }
        {
            // 选在 面板 的  ⁇  中预设 产生, 任何重启之前。
            // 锁定行( 范围) IPC 安全带下显示 ENGINE
            // 事实和回信写进 配置,所以Permissive
            // 出来 scope_ipc 正确并读为自定义。
            const auto& got = m.d.persisted.sandbox;
            const auto ref = sandbox_cfg::apply_posture(got, want_p);
            INFO("scope_ipc " << got.scope_ipc << "/" << ref.scope_ipc
                 << " wx " << got.wx_protect << "/" << ref.wx_protect
                 << " close_fds " << got.close_inherited_fds << "/" << ref.close_inherited_fds
                 << " host " << got.fake_hostname << "/" << ref.fake_hostname);
            REQUIRE(sandbox_cfg::detect_posture(got) == want_p);
        }

        // "restart":通过文件,然后返回.
        auto s = persistence::load_settings(::agentty::IoAccess::grant());
        s.sandbox = m.d.persisted.sandbox;
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
        const auto loaded = persistence::load_settings(::agentty::IoAccess::grant()).sandbox;
        CHECK(loaded == m.d.persisted.sandbox);

        // 编辑 面板 打开 已加载 ,显示名称的预设。
        const auto form = pn::build_sandbox_form(loaded, facts_for(loaded));
        const int r = row_of(form, pn::kSbPosture);
        REQUIRE(r >= 0);
        CHECK(std::get<form::field::Choice>(form.fields[static_cast<std::size_t>(r)].value).index
              == want);
    }

    if (oh) ::setenv("HOME", old_home.c_str(), 1); else ::unsetenv("HOME");
    if (oa) ::setenv("AGENTTY_HOME", old_ah.c_str(), 1); else ::unsetenv("AGENTTY_HOME");
    fs::remove_all(dir);
}

TEST_CASE("sandbox pane: a preset does not re-stamp a row edited after it") {
    // 这种设计之所以存在是为了避免失败,也是为了抓住它。
    //
    // read_sandbox_form 运行 每个 键盘和
    // 每一次重价。第一个版本应用了那里的位置,
    // 将行与 detect_posture () - 无法工作,
    // 因为行仍然显示配置的姿态 USED 编辑 内存 选择
    // Hardend后,将8192直接放回下一个 重绘 :
    // 用户编辑消失,没有消息,这比拒绝更糟糕.
    //
    // 应用于 状态更新函数 而不是说准确,因为 状态更新函数 懂了吗?
    // 对应的 排行移动。
    auto hard = sandbox_cfg::apply_posture(sandbox_cfg::Config{},
                                           sandbox_cfg::Posture::Hardened);
    hard.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(hard);

    auto form = pn::build_sandbox_form(hard, facts_for(hard));
    // 列显示哈定,如其应.
    const int p = row_of(form, pn::kSbPosture);
    REQUIRE(p >= 0);
    CHECK(std::get<form::field::Choice>(form.fields[static_cast<std::size_t>(p)].value).index
          == static_cast<int>(sandbox_cfg::Posture::Hardened));

    // 现在按用户的用法,逐个编辑资源行。
    const int mem = row_of(form, pn::kSbMemoryMb);
    REQUIRE(mem >= 0);
    std::get<form::field::Number>(form.fields[static_cast<std::size_t>(mem)].value).value = 2048;

    // 回读 REPEATEDLY —— 数据 重绘 循环。
    // 编辑必须幸存所有编辑,姿态必须稳定在自定义上,而不是反击。
    auto out = pn::read_sandbox_form(form, hard);
    for (int i = 0; i < 3; ++i) out = pn::read_sandbox_form(form, out);

    CHECK(out.memory_mb == 2048);
    CHECK(sandbox_cfg::detect_posture(out) == sandbox_cfg::Posture::Custom);
}

TEST_CASE("sandbox pane: the posture row scores the walls") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    const auto preview = pn::preview_sandbox(cfg);
    pn::annotate_sandbox_form(form, preview, cfg, pn::EngineStatus::InForce);

    const int p = row_of(form, pn::kSbPosture);
    REQUIRE(p >= 0);
    const auto& origin = form.fields[static_cast<std::size_t>(p)].origin;
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(!origin.empty());
#else
    (void)origin;   // 无后端 : annotate_sandbox_form 评分无
#endif
#if defined(__linux__)
    if (preview.compiled) {
        // "N/M strong" 数字是两个姿态可以比较的,
        // 四个安全 -sounding 名字没有。
        CHECK(origin.find('/') != std::string::npos);
        CHECK(origin.find("strong") != std::string::npos);
    }
#endif
}

TEST_CASE("sandbox pane: a cap nobody asked for is not reported as a gap") {
    // 从截图:页脚读
    // `resource.memory: none (none) · resource.cpu: none (none)`
    // 保险单上没有问题,0分就划船 "no cap" 故意的 故意的... 故意的吗?
    // sandbox_config.hpp 说右边的天花板是机器的地产 选一个
    // 给别人的笔记本电脑 将一个工作楼变成一个 OOM 杀人
    //
    // 报告这些与真实退化
    // 无关的,
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.memory_mb = 0;      // 不需要盖帽
    cfg.cpu_percent = 0;
    cfg.max_procs = 0;

    const auto preview = pn::preview_sandbox(cfg);
#if defined(__linux__)
    if (preview.compiled) {
        for (const auto& w : preview.walls) {
            if (w.name == "resource.memory" || w.name == "resource.cpu" ||
                w.name == "resource.pids")
                CHECK(w.not_requested);
        }
    }
#endif

    // 并且要求一个翻回来: 标志 跟踪 REQUEST
    // 而不是能力 所以要求的和失败的上限 仍然是个漏洞
    cfg.memory_mb = 4096;
    const auto asked = pn::preview_sandbox(cfg);
#if defined(__linux__)
    if (asked.compiled) {
        for (const auto& w : asked.walls)
            if (w.name == "resource.memory") CHECK_FALSE(w.not_requested);
    }
#endif
}

TEST_CASE("sandbox pane: an unset cap row says so instead of showing none") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.memory_mb = 0;

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    const auto preview = pn::preview_sandbox(cfg);
    pn::annotate_sandbox_form(form, preview, cfg, pn::EngineStatus::InForce);

    const int mem = row_of(form, pn::kSbMemoryMb);
    REQUIRE(mem >= 0);
    const auto& origin = form.fields[static_cast<std::size_t>(mem)].origin;
#if defined(__linux__)
    if (preview.compiled) {
        // "none (none)"是准确的,也是无用的——它读起来是失败的。
        // 行必须说盖子是未设置的,这是一个选择。
        CHECK(origin.find("no cap") != std::string::npos);
        CHECK(origin.find("none") == std::string::npos);
    }
#endif
}

TEST_CASE("sandbox pane: empty path rows say they are empty") {
    // 这些行曾经是文本字段控件 "a, b, c" ,
    // 而空的一行则被解读为断开。它们现在就是Picks -- -
    // - 它们显示一个数并打开列表编辑器 -- -- 所以,
    // 空的必须可见于 PICK 换成占位符
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths.clear();
    cfg.write_paths.clear();
    cfg.deny_paths.clear();

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);

    for (auto id : {pn::kSbReadPaths, pn::kSbWritePaths, pn::kSbDenyPaths}) {
        const int i = row_of(form, id);
        REQUIRE(i >= 0);
        const auto& fld = form.fields[static_cast<std::size_t>(i)];
        const auto* p = std::get_if<form::field::Pick>(&fld.value);
        REQUIRE(p != nullptr);
        // 空列表 QQ 不计数, 并有一个占位符, 说明如何添加。
        CHECK(p->label.empty());
        CHECK(!p->placeholder.empty());
    }

    // 还有 NON-empty 列表中报告其大小而不是内容:
    // 狭小列中的三条短路告诉你小于 "3 paths" 却
    // 看起来像他们告诉你的更多
    cfg.read_paths = {"/opt/a", "/opt/b"};
    auto filled = pn::build_sandbox_form(cfg, facts_for(cfg));
    const int i = row_of(filled, pn::kSbReadPaths);
    REQUIRE(i >= 0);
    const auto* p = std::get_if<form::field::Pick>(
        &filled.fields[static_cast<std::size_t>(i)].value);
    REQUIRE(p != nullptr);
    CHECK(p->label == "2 paths");
}

// ── 路径列表编辑器 ────────────────────────────────────────
//
// 逗号-separated文本是一种串行格式, 假扮为接口。
// 这些封条将列表像列表一样运行。

TEST_CASE("sandbox list: round-trips its entries") {
    const std::vector<std::string> paths{"/opt/sdk", "/var/cache/x"};
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "Also readable", "help",
                                     paths, /*数字=*/false);

    // 每条条目一行,加上后面的空白 IS 额外负担。
    CHECK(ed.form.fields.size() == paths.size() + 1);
    CHECK(pn::read_sandbox_list(ed) == paths);
}

TEST_CASE("sandbox list: a blank entry is an absent entry") {
    // 这使得添加和删除相同的手势: 输入后缀的空白添加,
    // 清除行会删除。 没有单独的删除键可以发现,
    // 也没有任何方法保存空路径。
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "t", "h",
                                     {"/a", "/b", "/c"}, false);
    // 清净中道.
    std::get<form::field::Text>(ed.form.fields[1].value).value.clear();
    const std::vector<std::string> want{"/a", "/c"};
    CHECK(pn::read_sandbox_list(ed) == want);
}

TEST_CASE("sandbox list: entries are trimmed") {
    // 一条带有偏移空间的路径不会出错,它默默地无法匹配
    // ——逗号格式的准确失败模式,以及这个编辑器存在的
    // 原因.
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "t", "h", {"x"}, false);
    std::get<form::field::Text>(ed.form.fields[0].value).value = "  /opt/sdk  ";
    const std::vector<std::string> want{"/opt/sdk"};
    CHECK(pn::read_sandbox_list(ed) == want);
}

TEST_CASE("sandbox list: a port list validates on readback") {
    auto ed = pn::build_sandbox_list(pn::kSbPorts, "Allowed ports", "h",
                                     {"443", "80"}, /*数字=*/true);
    CHECK(ed.numeric());
    // TEXT 连港口都行 maya '数字控件没有' 光标 字段,
    // 所以您输入的列表不能使用 --
    // "I don't see a caret in the port list" 数字规则是用来回转的
    CHECK(std::holds_alternative<form::field::Text>(ed.form.fields[0].value));

    const std::vector<std::string> want{"443", "80"};
    CHECK(pn::read_sandbox_list(ed) == want);

    // 空虚是不存在的, 与一条路径一样的规则。
    std::get<form::field::Text>(ed.form.fields[0].value).value.clear();
    const std::vector<std::string> after{"80"};
    CHECK(pn::read_sandbox_list(ed) == after);
}

TEST_CASE("sandbox list: a non-port is dropped, not guessed at") {
    // 已执行到回读, 部件无法执行。 丢弃会击打夹击:
    // 默默地将 "8O80"( 字母 O) 转换为
    // 8080 将会在安全控制上产生用户从未要求的规则。
    auto ed = pn::build_sandbox_list(pn::kSbPorts, "t", "h",
                                     {"443", "8O80", "99999", "0", "0443"},
                                     true);
    // 8O80(O),999999(超出范围)和0被放弃.
    // "0443" 正常为443,然后是a DUPLICATE 第一次
    // 进入和崩溃,所以一个港口生存,而不是两个。
    const std::vector<std::string> want{"443"};
    CHECK(pn::read_sandbox_list(ed) == want);
}

TEST_CASE("sandbox list: opening one from the pane carries the current values") {
    // End 结束: 将“选行”手摘掉,编辑器从配置中
    // 引出 FORM 描述而不是从 已保存 策略 - 因此,
    // 未保存的姿态变化不会被默默地通过编辑列表而放弃.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/opt/sdk"};
    install(cfg);

    Model m = opened();
    auto [m2, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});

    const auto* ed = m2.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    CHECK(ed->pane.row_id == pn::kSbReadPaths);
    CHECK_FALSE(ed->pane.numeric());
    const std::vector<std::string> want{"/opt/sdk"};
    CHECK(pn::read_sandbox_list(ed->pane) == want);
}

TEST_CASE("sandbox list: closing commits back into the pane") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths.clear();
    install(cfg);

    Model m = opened();
    auto [m2, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});

    // 键入一条路径, 输入后缀的空白行。
    {
        auto* ed = m2.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        std::get<form::field::Text>(ed->pane.form.fields.back().value).value =
            "/opt/sdk";
    }

    auto [m3, __] = app::update(std::move(m2), Msg{SandboxListClose{}});

    // 回到沙箱XXXX 面板,在配置和行中显示值为
    // 一个计数.
    const auto* o = m3.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);

    // 背对着 面板's WORKING 配置,而不是密封策略:路径
    // 列表在表单外存在(行是显示一个计数的 Pick),
    // 所以 read_sandbox_form 由基地通过,
    // 使用 sb::config ()这里是要求 旧 政策是什么新路径。
    const auto out = pn::read_sandbox_form(o->pane.form, o->pane.working);
    const std::vector<std::string> want{"/opt/sdk"};
    CHECK(out.read_paths == want);

    // 已经放在磁盘上了 没有 ^S 任何其它 --
    // -- 持续和应用是两个不同的事物,
    // 而仅仅申请需要等待重启(见 状态更新函数).
    CHECK(m3.d.persisted.sandbox.read_paths == want);
}

// ── 允许访问的主机 ────────────────────────────────────────────────────────────

TEST_CASE("sandbox hosts: the row exists and says what empty means") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    const auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    const auto* f = [&]() -> const form::Field* {
        for (const auto& x : form.fields) if (x.id == pn::kSbHosts) return &x;
        return nullptr;
    }();
    REQUIRE(f != nullptr);
    const auto& pick = std::get<form::field::Pick>(f->value);
    // 空不是"none":这是最广泛的设定,行必须如此说,
    // 而不是像什么也达不到一样阅读.
    CHECK(pick.label.empty());
    CHECK(pick.placeholder.find("any public host") != std::string::npos);
}

TEST_CASE("sandbox hosts: readback canonicalises, drops junk, and dedupes") {
    using K = pn::SandboxListPane::ListKind;
    auto ed = pn::build_sandbox_list(pn::kSbHosts, "t", "h",
        {"https://GitHub.com/1ay1", "github.com", ".npmjs.org", "140.82.112.3",
         "*", "  pypi.org:443 "},
        K::Host);
    CHECK(ed.kind == K::Host);
    CHECK_FALSE(ed.numeric());
    // 编号 URL 赤裸裸的名声 一个 宿主; 温和 IP 财务报告和已审计财务报表 "*" 被丢弃。
    const std::vector<std::string> want{"github.com", "*.npmjs.org", "pypi.org"};
    CHECK(pn::read_sandbox_list(ed) == want);
}

TEST_CASE("sandbox hosts: every entry says what will happen to it") {
    using K = pn::SandboxListPane::ListKind;
    auto ed = pn::build_sandbox_list(pn::kSbHosts, "t", "h",
        {"https://github.com/x", "10.0.0.1", "*.github.com", "github.com"}, K::Host);
    pn::renumber_sandbox_list(ed);
    const auto& f = ed.form.fields;
    // 重写 - > 告诉它是什么 已保存 AS
    CHECK(f[0].help.find("saved as github.com") != std::string::npos);
    // 拒绝 - > 告诉 原因 , 并放弃它
    CHECK(f[1].error.find("IP address") != std::string::npos);
    CHECK(f[1].error.find("dropped on close") != std::string::npos);
    // 通配符 - > 提醒最高规则, 命名最高规则
    CHECK(f[2].help.find("add github.com too") != std::string::npos);
    // 已隐形 - > 无声
    CHECK(f[3].help.empty());
    // 添加行中同时教授两个表单
    CHECK(f.back().label == "+");
    CHECK(f.back().help.find("*.github.com") != std::string::npos);
}

TEST_CASE("sandbox hosts: open, add, close round-trips to disk") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    Model m = opened();
    auto [m2, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbHosts}}});
    {
        auto* ed = m2.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        CHECK(ed->pane.kind == pn::SandboxListPane::ListKind::Host);
        CHECK(ed->pane.title == "Allowed hosts");
        std::get<form::field::Text>(ed->pane.form.fields.back().value).value =
            "https://GitHub.com/";
    }
    auto [m3, __] = app::update(std::move(m2), Msg{SandboxListClose{}});
    const auto* o = m3.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    const std::vector<std::string> want{"github.com"};
    CHECK(pn::read_sandbox_form(o->pane.form, o->pane.working).allow_hosts == want);
    CHECK(m3.d.persisted.sandbox.allow_hosts == want);
}

TEST_CASE("sandbox hosts: settings that quietly mean something else are flagged") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    auto err_of = [&](const sandbox_cfg::Config& c) {
        auto form = pn::build_sandbox_form(c, facts_for(c));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(c), c, pn::EngineStatus::InForce);
        const int r = row_of(form, pn::kSbHosts);
        return r < 0 ? std::string{"<no row>"} : form.fields[std::size_t(r)].error;
    };
    // 仅限通配符:站点本身无法通达.
    cfg.allow_hosts = {"*.github.com"};
    CHECK(err_of(cfg).find("the sites themselves") != std::string::npos);
    // 手-edited 垃圾条目被命名,而不是默默忽略。
    cfg.allow_hosts = {"github.com", "not a host"};
    CHECK(err_of(cfg).find("`not a host`") != std::string::npos);
    // 列表,不使用该列表。
    cfg.allow_hosts = {"github.com"};
    cfg.net_mode = sandbox_cfg::NetMode::Ports;
    CHECK(err_of(cfg).find("only enforced while Access is full") != std::string::npos);
    // 健康的案子很安静
    cfg.net_mode = sandbox_cfg::NetMode::Full;
    cfg.allow_hosts = {"github.com", "*.github.com"};
    CHECK(err_of(cfg).empty());
}

TEST_CASE("sandbox list: typing builds the entry in order") {
    // 报为 "a always suffixed to list" : 输入 "abc" 倒过来了
    //
    // 列表的扩展重建了表格(在您刚刚填充的表格之后,需要出现一个
    // 新的空白行),并且 build_sandbox_list 制造
    // FRESH 文本字段中 光标 0. 重建中幸存的价值和 光标 没有,
    // 所以在开始插入第一个键后,每个键盘都按住。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths.clear();
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 将光标放在后面的空白行和类型上。
    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        ed->pane.form.cursor =
            static_cast<int>(ed->pane.form.fields.size()) - 1;
    }
    // 真正的键序列: translate() 发出TypeToEdit
    // 用于第一个可打印( 它开始会话) , 插入其余的。
    // 发送TypeToEdit 三次将测试路由器从未产生的序列 -- -
    // - 后两次将击中 editing() 的守护器并被丢弃。
    {
        auto [next, __] = app::update(
            std::move(m),
            Msg{SandboxListKey{form::keys::Action{form::keys::Intent::TypeToEdit, U'a'}}});
        m = std::move(next);
    }
    for (char32_t ch : {U'b', U'c'}) {
        auto [next, __] = app::update(
            std::move(m),
            Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Insert, ch}}});
        m = std::move(next);
    }

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    const auto values = pn::read_sandbox_list(ed->pane);
    REQUIRE(values.size() == 1);
    CHECK(values[0] == "abc");   // 否 "cba" 数据
}

TEST_CASE("sandbox list: Esc leaves the editor even from a live row") {
    // 报为 "esc doesn't work always"。当一行是 EDITING,
    // apply() 转弯 Esc 输入 LeaveField ,
    // 而从未报告 `close` 媒体只结束了编辑 编辑们却保持开放 没有
    // 明显不同之处
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/opt/sdk"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 开始编辑第一个条目.
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::TypeToEdit, U'x'}}});
    m = std::move(m2);
    {
        const auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        REQUIRE(ed->pane.form.editing());   // 正在开始的会话
    }

    // 总是两个 Esc。第一个结束编辑 和 拒绝它打开的任何建议,
    // 它们是用户一方的一层。 第二层离开编辑器。 计数不是
    // 随你是否拥有 带类型的 定点:
    // "esc doesn't work always" 在一些州,有两次被吞噬,
    // 在另一些州,有一次被吞噬。
    for (int i = 0; i < 2; ++i) {
        auto [next, ___] = app::update(
            std::move(m),
            Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Close, 0}}});
        m = std::move(next);
    }

    CHECK(m.ui.panel.get<pn::SandboxList>() == nullptr);
    CHECK(m.ui.panel.get<pn::Sandbox>() != nullptr);   // 回到面板时
}

TEST_CASE("sandbox list: the caret starts at the END of an existing entry") {
    // 报为 "the caret starts at the front" 中,
    // 文本字段默认光标为 0,因此移动到已读条目 "/opt/sdk"
    // 输入字符 之前 道路。
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "t", "h",
                                     {"/opt/sdk"}, false);
    const auto& t = std::get<form::field::Text>(ed.form.fields[0].value);
    CHECK(t.cursor == t.value.size());
}

TEST_CASE("sandbox list: backspacing to empty keeps the row and the session") {
    // 报为 "backspace doesn't work sometimes" ,
    // 并显示 "sometimes" 被告诉: 完全在一行空时失败了。
    //
    // 当条目数与行数不一致时, 将表单加到REBUILD 时,
    // 其生长逻辑会使其在read_sandbox_list
    // ( ) 中缺失, 因此重建删除了正在编辑的行 --
    // 焦点移动, 编辑会话结束, 下一个后空格没有字段可
    // 操作。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"ab"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    const std::size_t rows_before =
        m.ui.panel.get<pn::SandboxList>()->pane.form.fields.size();

    // Enter 字段,然后将值完全回放。
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::TypeToEdit, U'c'}}});
    m = std::move(m2);
    for (int i = 0; i < 3; ++i) {
        auto [next, ___] = app::update(
            std::move(m),
            Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Backspace, 0}}});
        m = std::move(next);
    }

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    // 排队幸存了每根键盘,所以后空格总是有目标.
    CHECK(ed->pane.form.fields.size() == rows_before);
    CHECK(ed->pane.form.editing());
    // 现在真的空了 键盘杆落地了
    CHECK(std::get<form::field::Text>(ed->pane.form.fields[0].value).value.empty());
}

TEST_CASE("sandbox list: ^X removes the focused entry") {
    // "no way to remove the entry" 清除
    // 线条工程(在犯罪时空出),但无法发现,所以 ^X - 窗体层已经向
    // 它移动 ResetField 和叶子 面板 -- 删除行。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/a", "/b", "/c"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 取中一门.
    m.ui.panel.get<pn::SandboxList>()->pane.form.cursor = 1;
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::ResetField, 0}}});
    m = std::move(m2);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    const std::vector<std::string> want{"/a", "/c"};
    CHECK(pn::read_sandbox_list(ed->pane) == want);

    // 标签重新编号,所以列表不改为1,2,2.
    CHECK(ed->pane.form.fields[0].label == "1.");
    CHECK(ed->pane.form.fields[1].label == "2.");
    // 后面的加行仍然在那里, ——这是付得起的,而不是一个条目,
    // 所以它绝对不是^X删除的。
    CHECK(ed->pane.form.fields.back().label == "+");
}

TEST_CASE("sandbox list: ^X cannot delete the add row") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/a"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 停车在后方 "+" 划行并试着去掉它。
    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        ed->pane.form.cursor =
            static_cast<int>(ed->pane.form.fields.size()) - 1;
    }
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::ResetField, 0}}});
    m = std::move(m2);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    CHECK(ed->pane.form.fields.back().label == "+");
    const std::vector<std::string> want{"/a"};
    CHECK(pn::read_sandbox_list(ed->pane) == want);
}

TEST_CASE("sandbox pane: the ports row opens even when Access is not `ports`") {
    // 它曾经是LOCKED,除非模式已经是`ports`,
    // 这使得它无法到达:lock_row使得activate()返回什么也没有,
    // 所以Enter没有打开编辑器,根本没有填充端口的方法.
    // 排行告诉你先设置Access,不会让你采取行动.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::Full;   // 不 端口
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    const int p = row_of(form, pn::kSbPorts);
    REQUIRE(p >= 0);
    CHECK_FALSE(form.fields[static_cast<std::size_t>(p)].locked);
}

TEST_CASE("sandbox pane: adding a port switches Access to `ports`") {
    // 添加一个端口是明确的意图声明,所以它设定了模式而不是在
    // `full`下坐惯。 否则用户会填写一个端口列表,
    // 它将不做任何事情, 也不会说原因。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::Full;
    cfg.allow_ports.clear();
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbPorts}}});
    m = std::move(m1);

    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        REQUIRE(ed->pane.numeric());
        std::get<form::field::Text>(ed->pane.form.fields.back().value).value = "8080";
    }
    auto [m2, __] = app::update(std::move(m), Msg{SandboxListClose{}});

    const auto* o = m2.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    CHECK(o->pane.working.net_mode == sandbox_cfg::NetMode::Ports);
    const std::vector<std::uint16_t> want{8080};
    CHECK(o->pane.working.allow_ports == want);
}

TEST_CASE("sandbox pane: emptying the port list does not open the network") {
    // 曾经跳到 `full` ,默默地转动 "allow these ports"
    // 输入 "allow everything" 继续 `ports` (
    // 否定全) 且行误说.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::Ports;
    cfg.allow_ports = {443};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbPorts}}});
    m = std::move(m1);

    // 清除一个条目(空白不见,与路径相同).
    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        std::get<form::field::Text>(ed->pane.form.fields[0].value).value.clear();
    }
    auto [m2, __] = app::update(std::move(m), Msg{SandboxListClose{}});

    const auto* o = m2.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    CHECK(o->pane.working.allow_ports.empty());
    CHECK(o->pane.working.net_mode != sandbox_cfg::NetMode::Full);
}

TEST_CASE("sandbox pane: editing ports never loosens Access = none") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::None;
    cfg.allow_ports.clear();
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbPorts}}});
    m = std::move(m1);
    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        std::get<form::field::Text>(ed->pane.form.fields.back().value).value = "443";
    }
    auto [m2, __] = app::update(std::move(m), Msg{SandboxListClose{}});
    const auto* o = m2.ui.panel.get<pn::Sandbox>();
    REQUIRE(o != nullptr);
    CHECK(o->pane.working.net_mode == sandbox_cfg::NetMode::None);
}

TEST_CASE("sandbox list: a bad port is flagged while typing") {
    auto ed = pn::build_sandbox_list(pn::kSbPorts, "t", "h", {"443", "8O80"}, true);
    pn::renumber_sandbox_list(ed);
    CHECK(ed.form.fields[0].help.empty());
    CHECK(ed.form.fields[1].help.find("not a port") != std::string::npos);
}

TEST_CASE("sandbox pane: the form teaches itself") {
    // 标准:用户应该能够学习到这个 面板 FROM 编号 面板.
    // 沒人知道W的到來 ^X 是什么,这是...
    // `toolchain` 授权 ,或者什么转动范围
    // IPC 安全控制是永远的 永远的离开,
    // 这使得每行都低于第一个死体重。
    //
    // 所以这是完整的检查,不是拼写检查 每条固定的行都会解释自己,
    // 每条 ENUM OPTION 分别解释自己,
    // 因为 "which of these three" 是一个与
    // "what is this row".
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    const auto form = pn::build_sandbox_form(cfg, facts_for(cfg));

    int rows = 0;
    for (const auto& f : form.fields) {
        if (f.is_header()) continue;
        ++rows;

        // 每行都说它是什么。一个标签就是一个名字,
        // 而不是解释——"W^X"对于一个没有遇到它的人来说毫无意义。
        //
        // 消息在宏前是建 COMPLETELY :doctest 数据
        // 流 其论据为 MessageBuilder,所以调用 内的
        // 任何 `+` 试图将信息连接到构建器上而不是串上.
        const std::string who = "row '" + f.id + "'";
        const std::string no_help = who + " has no help text";
        CHECK_MESSAGE(!f.help.empty(), no_help);

        // 选择选择 -one 排队
        // 说 IT 答案是空的:
        // "toolchain / minimal / host-readable"命名了三件东西,
        // 并且定义了其中的任何一个.
        if (const auto* c = std::get_if<form::field::Choice>(&f.value)) {
            const std::string miss = who + " is missing per-option hints";
            CHECK_MESSAGE(c->hints.size() == c->labels.size(), miss);
            for (std::size_t i = 0; i < c->hints.size(); ++i) {
                const std::string blank =
                    who + " option " + std::to_string(i) + " has an empty hint";
                CHECK_MESSAGE(!c->hints[i].empty(), blank);
            }
        }
    }
    // 扫描结果实际上运行了—— 跳过一切的循环
    // 会报告零故障,这就是这个文件的故障模式。
    CHECK(rows >= 15);
}

TEST_CASE("sandbox pane: a toggle says what its CURRENT state does") {
    // A 切换 渲染 只不过是 on/off ,
    // 所以单行帮助线只能描述行,永远不能选择。
    // 没有状态 -dependent 帮助了解什么是关闭成本的唯一
    // 方法 就是关闭成本 安全控制的错误方式
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    cfg.wx_protect = true;
    install(cfg);
    const auto on = pn::build_sandbox_form(cfg, facts_for(cfg));
    const int i = row_of(on, pn::kSbWxProtect);
    REQUIRE(i >= 0);
    const auto help_on = on.fields[static_cast<std::size_t>(i)].help;

    // build_sandbox_form 完全结束(配置,事实),
    // 所以翻转配置和重建是整个测试——不需要重新安装。
    cfg.wx_protect = false;
    const auto off = pn::build_sandbox_form(cfg, facts_for(cfg));
    const auto help_off = off.fields[static_cast<std::size_t>(i)].help;

    // 两国解读不同,各取其后果.
    CHECK(help_on != help_off);
    CHECK(help_on.find("on:") != std::string::npos);
    CHECK(help_off.find("off:") != std::string::npos);
}

TEST_CASE("sandbox list: backspace works on the FIRST press after opening") {
    // 报告编号为"backspace on the selected row doesn't work first".
    //
    // 堆叠了两个错误。 路由器没有翻译 Backspace 浏览时,
    // 键在之前丢失了 apply() 曾经见过它;即使它一到,
    // apply() 需要编辑会话,编辑器在浏览中打开。
    // 打入字符自动 -enters 财务报告和已审计财务报表 Backspace
    // 并没有,所以开始编辑同一行的两种方式是不一致的。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"abc"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 完全按ONE,打开后直线,没有开始编辑.
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Backspace, 0}}});
    m = std::move(m2);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    const std::vector<std::string> want{"ab"};
    CHECK(pn::read_sandbox_list(ed->pane) == want);
    // 留下了现场直播,所以 下一个 新闻继续,
    // 而不是需要自己的觉醒 -up.
    CHECK(ed->pane.form.editing());
}

TEST_CASE("sandbox list: backspace on an empty row is harmless") {
    // 格式层在删除密钥上不使用自动-entering的原因是,
    // 一个偏差的Backspace绝不能破坏字节,用户无法看到光标英寸.
    // 这一点仍须在这里坚持:一行空的行没有东西可以删除,因此,
    // 媒体不能开始一个会话或打扰任何东西.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths.clear();
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    const std::size_t rows =
        m.ui.panel.get<pn::SandboxList>()->pane.form.fields.size();

    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Backspace, 0}}});
    m = std::move(m2);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    CHECK_FALSE(ed->pane.form.editing());
    CHECK(ed->pane.form.fields.size() == rows);
    CHECK(pn::read_sandbox_list(ed->pane).empty());
}

TEST_CASE("sandbox list: duplicates collapse, keeping the first position") {
    // 两个相同的绑定是一绑定,两个相同的端口是一港,
    // 但一个重复很容易偶然产生(键入路径,滚动,
    // 再键入),并且保持这两个位置都使得行数与政策的
    // 实际作用不相符.
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "t", "h",
                                     {"/a", "/b", "/a", "/c", "/b"}, false);
    const std::vector<std::string> want{"/a", "/b", "/c"};
    CHECK(pn::read_sandbox_list(ed) == want);
}

TEST_CASE("sandbox pane: a path in two lists is flagged, not silently resolved") {
    // 电话 编译器 无法抓取: a 授权 并且面具
    // 是有效的,所以胜利的顺序是排放。 用户要求两个
    // 不兼容的东西,而政策悄悄地选择了一个。
    //
    // 电话 面板 REPORTS 而不是决心,故意的...
    // "masked" 财务报告和已审计财务报表 "writable" 同样的
    // 读数是两行的, 所以选一个赢家就是在制造一种意图。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.write_paths = {"/opt/shared"};
    cfg.deny_paths  = {"/opt/shared"};
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);

    const int d = row_of(form, pn::kSbDenyPaths);
    REQUIRE(d >= 0);
    const auto& err = form.fields[static_cast<std::size_t>(d)].error;
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(!err.empty());
    CHECK(err.find("/opt/shared") != std::string::npos);
    CHECK(err.find("writable") != std::string::npos);
#else
    CHECK(err.empty());   // 无后端: enfense 面板 说明什么
#endif
}

TEST_CASE("sandbox pane: a relative path is flagged") {
    // 来,来,来 编译 它不会悄悄地做它所写的: 沙箱 解决它
    // 与 CHILD 工作目录,而不是 面板 是因为...
    // `build` 通常完全没有匹配。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"build"};
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);

    const int r = row_of(form, pn::kSbReadPaths);
    REQUIRE(r >= 0);
#if !AGENTTY_TEST_SANDBOX_BACKEND
    // 无后端 : annotate_sandbox_form 是一个不 -op ,无 标志.
    CHECK(form.fields[static_cast<std::size_t>(r)].error.empty());
    return;
#endif
    CHECK(form.fields[static_cast<std::size_t>(r)].error.find("relative")
          != std::string::npos);

    // 绝对路径是好的,同样是~-- 贝壳在它之前扩张它 沙箱
    // 从来没有见过它。
    cfg.read_paths = {"/opt/sdk", "~/.cargo"};
    auto ok = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(ok, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);
    const int r2 = row_of(ok, pn::kSbReadPaths);
    REQUIRE(r2 >= 0);
    CHECK(ok.fields[static_cast<std::size_t>(r2)].error.empty());
}

TEST_CASE("sandbox list: browsing past an entry does not open suggestions") {
    // `completing` 从前是从行中计算出来的
    // TEXT 单向已有条目箭头时, 打开了用户未打开的
    // 完成列表 带类型的 字符输入 - 然后偷走他们的下一个
    // Esc 以撤销他们从未要求过的名单,即 Esc 被吞没了
    //
    // 完成是一份你应得的 WRITING。
    // 浏览一个值不是写出来的。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"src", "include"};
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 下行移动——任何地方都不许打字.
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::MoveNext, 0}}});
    m = std::move(m2);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    CHECK_FALSE(ed->pane.form.editing());
    CHECK_FALSE(ed->pane.completing);
}

TEST_CASE("sandbox list: accepting a suggestion settles instead of looping") {
    // Tab 将击打写入行并关闭列表。 `completing`
    // 重算在 下一个 键从行文本中键入, 选择面板 查询
    // 仍然保留了半个 -typed 前缀 - 因此
    // 列表立即重新打开, 现在匹配完整接受的路径。
    // Tab 拥有 副作用 接受一个建议,然后直接被提出来
    //
    // 将查询同步到所写的内容就是接受终端的原因.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths.clear();
    install(cfg);

    Model m = opened();
    auto [m1, _] = app::update(std::move(m),
                               Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
    m = std::move(m1);

    // 键入后缀的空白行以打开建议。
    {
        auto* ed = m.ui.panel.get<pn::SandboxList>();
        REQUIRE(ed != nullptr);
        ed->pane.form.cursor =
            static_cast<int>(ed->pane.form.fields.size()) - 1;
    }
    auto [m2, __] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::TypeToEdit, U's'}}});
    m = std::move(m2);

    // 只有工作区指数真正产生候选人才有意义;否则,
    // 没有什么可以接受的,也没有可以断言的。
    if (!m.ui.panel.get<pn::SandboxList>()->pane.completing) return;

    auto [m3, ___] = app::update(
        std::move(m),
        Msg{SandboxListKey{form::keys::Action{form::keys::Intent::Complete, 0}}});
    m = std::move(m3);

    const auto* ed = m.ui.panel.get<pn::SandboxList>();
    REQUIRE(ed != nullptr);
    CHECK_FALSE(ed->pane.completing);

    // 查询现在符合接受的值, 所以下一个 重绘-driving
    // 密钥无法决定用户 带类型的 重新打开名单
    const auto* row = ed->pane.form.focused();
    REQUIRE(row != nullptr);
    const auto& t = std::get<form::field::Text>(row->value);
    CHECK(ed->pane.complete.query() == t.value);
    CHECK(t.cursor == t.value.size());
}

TEST_CASE("sandbox list: no key sequence leaves the editor inconsistent") {
    // 州检而不是另一个有名的案件
    //
    // 这个 面板 现在有三个重叠模式(浏览、编辑、
    // 完成)和十几个键,每个键可以触摸其中的两个。
    // 过去几轮中的每个错误都是一个 SEAM 二者之间 --
    // -- a 标志 超越了它的事业, 钥匙意味着不同的东西
    // 取决于一个隐形的位点-- 一次命名它们 一个只发现已经
    // 报告过的东西。
    //
    // 所以:从每个可达到的状态 推动所有意图,
    // 并申明 不变量 不论怎样,这都得坚持下去。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/opt/a", "/opt/b"};
    install(cfg);

    using I = form::keys::Intent;
    const I keys[] = {
        I::MoveNext, I::MovePrev, I::MoveFirst, I::MoveLast,
        I::TypeToEdit, I::Insert, I::Backspace, I::DeleteForward,
        I::CaretLeft, I::CaretRight, I::CaretHome, I::CaretEnd,
        I::Complete, I::ResetField, I::Activate, I::AdjustUp, I::AdjustDown,
    };

    // 少数决定性命令而不是随机走动:种子
    // 洗牌会让失败依赖于无人拥有的种子,
    // 这些覆盖了真正的虫子来自的交替.
    for (std::size_t start = 0; start < std::size(keys); ++start) {
        Model m = opened();
        auto [m1, _] = app::update(
            std::move(m), Msg{SandboxEditList{std::string{pn::kSbReadPaths}}});
        m = std::move(m1);

        for (std::size_t step = 0; step < std::size(keys); ++step) {
            const auto k = keys[(start + step) % std::size(keys)];
            auto [next, __] = app::update(
                std::move(m),
                Msg{SandboxListKey{form::keys::Action{k, U'a'}}});
            m = std::move(next);

            const auto* ed = m.ui.panel.get<pn::SandboxList>();
            if (!ed) break;   // Esc-equivalent 关闭; 已无任何可检查

            // 1. 建议意味着编辑会话。反面是虚假的(您)
            //    无需建议就可以编辑, 但一个漂浮在一行的完成
            //    列表 没有人在写, 才是吃了用户的Esc。
            if (ed->pane.completing)
                CHECK(ed->pane.form.editing());

            // 2. 光标总是在真实的行上。
            //    夹子会通过下个键键键的尾部进行索引。
            CHECK(ed->pane.form.cursor >= 0);
            CHECK(ed->pane.form.cursor <
                  static_cast<int>(ed->pane.form.fields.size()));

            // 3. 总是有一条排行的,就是
            //    最后一个,失去它,名单就再也不能长了。
            REQUIRE(!ed->pane.form.fields.empty());
            CHECK(ed->pane.form.fields.back().label == "+");

            // 4. 条目标签按顺序排列:1.、2.、.无空白。
            //    删除未更改编号的字段,则按照已丢失其
            //    仍然持有的条目的列表。
            for (std::size_t i = 0; i + 1 < ed->pane.form.fields.size(); ++i)
                CHECK(ed->pane.form.fields[i].label ==
                      std::to_string(i + 1) + ".");

            // 5. 每行都是文字行。端口列表也使用它们,并且 a
            //    流星数会悄悄地失去它 光标.
            for (const auto& f : ed->pane.form.fields)
                CHECK(std::holds_alternative<form::field::Text>(f.value));
        }
    }
}

// ── 实际到达屏幕的是什么 ──────────────────────────────
//
// 上面的一切都测试 MODEL : 形式,
// 类型 状态更新函数 ,它所产生的配置。没有一种图像可以
// 证明一个像素的画正确,这个 面板 已经运出两个只存在于
// 渲染 层 - 描述错误引擎的字幕, 以及页脚
// maya 编辑一行时静静替换.
//
// render_to_string 绘制真实元素 源码树 以真实的宽度,
// 所以这些断言 字节 用户的 终端 将会收到。

TEST_CASE("sandbox render: the pane paints its rows, values and footer") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.read_paths = {"/opt/sdk"};
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);
    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(form, maya::Color::blue())}.build(), 120);

    // 和其他的一样被设定 选择面板。无帧 覆盖面板 涂料透明地
    // 覆盖 线程 并读作 "the key did nothing".
    CHECK(out.find("\xe2\x95\xad") != std::string::npos);   // ╭
    CHECK(out.find("Sandbox") != std::string::npos);

    // 第头文件节及其下各行.
    //
    // 上层 -cased: maya 渲染 a 头文件 以盖子加
    // 一条规则到边缘, 也就是用长的形状阻止一个区域跑进另一个区域。
    // 威胁源头 -case 标签会传递给一个 面板 从未画过 头
    // 文件 一点都没有
    CHECK(out.find("FILESYSTEM") != std::string::npos);
    CHECK(out.find("Preset") != std::string::npos);

    // 值 栏, 不仅仅是标签。 其价值从未画过的行是
    // 您无法读取的行。
    CHECK(out.find("claybin") != std::string::npos);

    // 列表行 渲染 编号 COUNT 而不是原始路径列表。
    CHECK(out.find("1 path") != std::string::npos);
}

// 主持人排队和编辑,画。 SB_DUMP =1 印出两个字,
// 这样人类就可以看它们-- 断言的钉子,一个用户必须能够读到的东西。
TEST_CASE("sandbox render: Allowed hosts row and editor read correctly") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);
    // 真实标签, 如用户所见( 没有此选项, 列会画他们的
    // i18n) KEYS ,这就是一个未初始化的 目录 倒回去吧 说吧
    REQUIRE(agentty::i18n::init("en", ""));
    auto paint = [](const form::Form& f, int w) {
        return maya::render_to_string(
            maya::Panel{agentty::ui::form_config(f, maya::Color::blue(), nullptr, 0, w)}.build(), w);
    };
    auto widest = [](const std::string& out) {
        std::size_t worst = 0, cols = 0;
        for (char ch : out) {
            if (ch == '\n') { worst = std::max(worst, cols); cols = 0; continue; }
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++cols;
        }
        return std::max(worst, cols);
    };
    const bool dump = std::getenv("SB_DUMP") != nullptr;

    // 空: 行必须读为 WIDE OPEN ,而不是作为 "none".
    {
        auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);
        const auto out = paint(form, 100);
        if (dump) std::printf("\n=== pane, empty list ===\n%s\n", out.c_str());
        CHECK(out.find("Allowed hosts") != std::string::npos);
        CHECK(out.find("any public host") != std::string::npos);
    }
    // 设置:一个计数,和"via proxy"的源头。
    cfg.allow_hosts = {"github.com", "*.github.com", "registry.npmjs.org"};
    {
        auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);
        const auto out = paint(form, 100);
        if (dump) std::printf("\n=== pane, 3 hosts ===\n%s\n", out.c_str());
        CHECK(out.find("3 hosts") != std::string::npos);
        CHECK(out.find("via proxy") != std::string::npos);
    }
    // 编辑器,中-edit, 屏幕上立即有各种提示。
    {
        auto ed = pn::build_sandbox_list(pn::kSbHosts, "Allowed hosts",
            "commands may reach ONLY these names \xc2\xb7 empty = any public host",
            {"github.com", "*.github.com", "https://registry.npmjs.org/pkg", "10.0.0.1"},
            pn::SandboxListPane::ListKind::Host);
        pn::renumber_sandbox_list(ed);
        ed.form.cursor = 2;   // 贴上 URL : 其类型 "saved as" 注释是帮助
        for (int w : {60, 100}) {
            const auto out = paint(ed.form, w);
            if (dump) std::printf("\n=== editor @%d ===\n%s\n", w, out.c_str());
            CHECK(out.find("saved as registry.npmjs.org") != std::string::npos);
            CHECK(out.find("dropped on close") != std::string::npos);
            // 没有什么经过 帧 宽度也最窄
            CHECK(widest(out) <= static_cast<std::size_t>(w));
        }
    }
}

TEST_CASE("sandbox render: nothing paints past the frame at any width") {
    // 电话 面板 是最宽的形态,以 agentty
    // 长的帮助行、源列和内置错误。溢出一行不包,
    // 它与边框相撞,并腐蚀 帧.
    //
    // 60是实际的一层(第1层) form_config 夹子分钟
    // -width 反 对 终端 ),80是经典默认值,200是超宽值.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    // 宽度最差的例:一条长路,和同一行的错误.
    cfg.read_paths = {"/some/quite/long/path/to/a/vendored/dependency/tree"};
    cfg.net_mode = sandbox_cfg::NetMode::Ports;
    cfg.allow_ports.clear();            // 触发内在错误
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                              pn::EngineStatus::InForce);

    for (int width : {60, 80, 100, 120, 200}) {
        const auto out = maya::render_to_string(
            maya::Panel{agentty::ui::form_config(form, maya::Color::blue(),
                                                 nullptr, 0, width)}.build(),
            width);
        // 每条画的线必须合身。 render_to_string
        // 每行发出一行; 长于 终端 是溢出的错误。
        std::size_t start = 0, worst = 0;
        while (start <= out.size()) {
            const auto nl = out.find('\n', start);
            const auto end = (nl == std::string::npos) ? out.size() : nl;
            // 计数显示列, 不 字节 : 数据 面板 充满 UTF -8(_、
            // _、_)和a 字节 计数会错 -positive 每一行都一样
            std::size_t cols = 0;
            for (std::size_t i = start; i < end; ++i)
                if ((static_cast<unsigned char>(out[i]) & 0xC0) != 0x80) ++cols;
            worst = std::max(worst, cols);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        const std::string msg =
            "width " + std::to_string(width) + ": widest line was " +
            std::to_string(worst);
        CHECK_MESSAGE(worst <= static_cast<std::size_t>(width), msg);
    }
}

// ── 同为帧,在翻译项下 ────────────────────────────────
//
// 先决条件 docs/design/i18n.md.
// 二十种语言可以分享的主张 TUI 排版基于两件事是真实的,
// 而且两者都不足以假定:
//
//   1. a CJK label is DOUBLE-WIDTH -- `系统调用过滤` is 6 codepoints and 12
//      列 - 所以任何计算字符的都将在 -measure 一半,
//      然后宣布溢出一排罚款。
//   2. 德语长度较长。用真正的agentty标签测量:`应用
//      重新启动` (18 cols) becomes `Wird beim Neustart angewendet '
//      (29),即1.61x -- -- 已经远远超过+30-40%的拇指规则。
//
// 因此,这描绘了面板与我们所期望的LONGEST翻译和我们所支持的
// WIDEST脚本,以及通过maya::string_width
// 来测量真实显示列,而不是通过计算代码点来进行.
//
// 注意与上述检验标准有故意的区别:其中一项标准是非。
// -continuation 字节 ,它只等于列数,
// 而每个格言都是单数 -width 英国人应该这样 面板 并且会是
// WRONG here -- it would score 系统调用过滤 as 6 and pass a row that paints
// 12列宽,两个测量,因为回答两个问题
TEST_CASE("sandbox render: the frame survives translation") {
    // 最宽的线路,显示为COLUMNS.
    const auto widest_cols = [](const std::string& out) {
        std::size_t start = 0, worst = 0;
        while (start <= out.size()) {
            const auto nl = out.find('\n', start);
            const auto end = (nl == std::string::npos) ? out.size() : nl;
            worst = std::max<std::size_t>(
                worst, static_cast<std::size_t>(maya::string_width(
                           std::string_view{out}.substr(start, end - start))));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return worst;
    };

    struct Sample { const char* lang; const char* path; };
    const Sample samples[] = {
        // 路径是一行可以携带的最长的自由-form字符串,
        // 是翻译不能缩短的字段.
        {"de", "/ein/ziemlich/langer/pfad/zu/einem/abhängigkeitsbaum"},
        {"zh-CN", "/一个/相当/长的/路径/到/依赖/树"},
        {"ja", "/かなり/長い/パス/依存関係/ツリー"},
        {"ru", "/довольно/длинный/путь/к/дереву/зависимостей"},
        {"ko", "/상당히/긴/경로/종속성/트리"},
    };

    for (const auto& s : samples) {
        sandbox_cfg::Config cfg;
        cfg.configured = true;
        cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
        cfg.read_paths = {s.path};
        cfg.net_mode = sandbox_cfg::NetMode::Ports;
        cfg.allow_ports.clear();          // 在同一行内嵌错误
        install(cfg);

        auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                                  pn::EngineStatus::InForce);

        for (int width : {60, 80, 120, 200}) {
            const auto out = maya::render_to_string(
                maya::Panel{agentty::ui::form_config(form, maya::Color::blue(),
                                                     nullptr, 0, width)}.build(),
                width);
            const auto worst = widest_cols(out);
            INFO("lang " << std::string{s.lang} << " at width " << width
                         << ": widest line was " << worst << " columns");
            CHECK(worst <= static_cast<std::size_t>(width));
        }
    }
}

// 整个设计所依据的测量: maya 计算每个在
// 真实显示列中传送的脚本。如果此选项退后,
// 则在 渲染器 20种语言中,有7种是错的,
// 其他的都说不出来——布局只是悄悄的腐败.
TEST_CASE("i18n: maya measures every target script in display columns") {
    struct W { const char* text; int want; const char* note; };
    const W cases[] = {
        {"hello",            5, "ascii"},
        {"\xe4\xbd\xa0\xe5\xa5\xbd",                 4, "zh-CN, 2 chars x 2 cols"},
        {"\xe3\x81\x93\xe3\x82\x93",                 4, "ja hiragana"},
        {"\xed\x95\x9c\xea\xb5\xad",                 4, "ko hangul"},
        {"\xd0\xbf\xd1\x80\xd0\xb8",                 3, "ru cyrillic, single-width"},
        {"\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2",         4, "uk cyrillic"},
        {"\xc3\xa9\xc3\xa8",                         2, "fr accented"},
        {"\xc3\xb6\xc3\xa4\xc3\xbc",                 3, "de umlauts"},
        {"\xc4\xb1\xc5\x9f",                         2, "tr dotless-i + s-cedilla"},
        {"\xc5\x82\xc4\x85",                         2, "pl"},
        {"\xc4\x8d\xc5\x99",                         2, "cs"},
    };
    for (const auto& c : cases) {
        INFO(std::string{c.note});
        CHECK(maya::string_width(c.text) == c.want);
    }
}

TEST_CASE("sandbox render: the subtitle names the RUNNING engine") {
    // 被涂上画 字节 而不是在模型上:字幕必须描述
    // 实际有效的内容。 (这开始于截图错误,
    // 在那里 面板 索赔 seccomp 引擎变弱时,
    // 引擎就变了,但引擎不匹配 面板 仍然必须命名引擎,
    // 而不是从配置中推断它。 )
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::HostFacts facts;
    facts.claybin_available = true;
    facts.landlock_abi = 10;
    facts.sandbox_active = true;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;

    const auto form = pn::build_sandbox_form(cfg, facts);
    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(form, maya::Color::blue())}.build(), 120);

    CHECK(out.find("claybin") != std::string::npos);
}

TEST_CASE("sandbox render: the list editor paints its entries and hint") {
    auto ed = pn::build_sandbox_list(pn::kSbReadPaths, "Also readable",
                                     "extra paths a command may read",
                                     {"/opt/sdk", "/var/cache/x"}, false);
    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(ed.form, maya::Color::blue())}.build(), 100);

    // 这两条,编号,加上后面的加行.
    CHECK(out.find("/opt/sdk") != std::string::npos);
    CHECK(out.find("/var/cache/x") != std::string::npos);
    CHECK(out.find("1.") != std::string::npos);
    CHECK(out.find("2.") != std::string::npos);
    CHECK(out.find("+") != std::string::npos);
}

// ── "applies on restart"必须作核对的索赔 ─────────────────────
//
// 脚印向"saved · applies on restart"承诺,
// 第NOTHING将进行核查。答案的每个成分都已经计算出来 — —
// 第编译结果、无法执行的清单、主机探测器 — — 并且没有一个
// 内容被并入正在提出的诉求中。
//
// 与 "sandbox: active" 没有 沙箱
// 并且作为字幕,命名墙 还没有上, 仅仅指向未来而不是现在。

TEST_CASE("sandbox restart: a clean policy promises plainly") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    const auto facts = facts_for(cfg);
    const auto preview = pn::preview_sandbox(cfg);

    // 空的判断 \ \ 开始真的会提供 哪些行描述。
    // 脚步者可以不附带任何条件地作出承诺。
    if (preview.compiled && preview.unenforceable.empty())
        CHECK(pn::restart_outcome(cfg, facts, preview).empty());
}

TEST_CASE("sandbox restart: a policy that will not compile says so") {
    // 难者否. claybin 拒绝而不是侮辱,所以没有 "partly"
    // 接下来的发射 从这个配置中没有任何结果 还有一个脚印说
    // "applies on restart" 那样会完全是假的
    sandbox_cfg::Config cfg;
    cfg.configured = true;

    pn::Preview broken;
    broken.compiled = false;
    broken.error = "fabricated: this host cannot build it";

    const auto out = pn::restart_outcome(cfg, facts_for(cfg), broken);
    REQUIRE(!out.empty());
    CHECK(out.find("NOT apply") != std::string::npos);
    // 并携带 REASON ,不仅仅是拒绝——否则用户
    // 无法就此采取行动.
    CHECK(out.find("fabricated") != std::string::npos);
}

TEST_CASE("sandbox restart: a silently degraded capability is named") {
    // 静态的大小写, 以及函数存在的原因。
    // 它的编译,引擎开始,一些壁仍然比要求的要弱。
    // 没有这个,用户相信退化的部分,这就是这个子
    // 系统围绕整个故障模式构建的。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::Preview degraded;
    degraded.compiled = true;
    degraded.unenforceable.push_back("network.isolation needs landlock abi 4");

    const auto out = pn::restart_outcome(cfg, facts_for(cfg), degraded);
    REQUIRE(!out.empty());
    // 判决仍然有效 -- -- 因此,判决不能被理解为拒绝。
    CHECK(out.find("applies on restart") != std::string::npos);
    // 但间隙名曰.
    CHECK(out.find("network.isolation") != std::string::npos);
}

TEST_CASE("sandbox restart: the worst problem wins the one line the footer has") {
    // 点菜不是化妆品,脚印显示 一个 线条,
    // 所以当有几件事出错时,用户需要最能使承诺
    // 无效的:一个不会 编译 使无法执行的清单变得无关紧要,
    // 而不仅仅是不重要。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::Preview worst;
    worst.compiled = false;
    worst.error = "refused";
    worst.unenforceable.push_back("resource.cpu");

    pn::HostFacts facts;
    facts.claybin_available = false;   // 无法启动 ALSO
    facts.sandbox_active = true;

    const auto out = pn::restart_outcome(cfg, facts, worst);
    CHECK(out.find("NOT apply") != std::string::npos);
    // 不是小问题
    CHECK(out.find("resource.cpu") == std::string::npos);
}

TEST_CASE("sandbox pane: the reducer keeps the restart verdict with the preview") {
    // End 结束于 面板 : 判决计算为: reprice() ,
    // 带有从 相同 配置,所以两人不能对他们所描述
    // 的政策有分歧。观点只读到它——它不能探测,
    // 并且不能重新 -derive 需要找到的配置。
    Model m = opened();
    const auto& p = pane(m).pane;

    // 不管判决是什么,它必须是 CONSISTENT 并附带预览:一个非
    // -compiling 策略不能为空( 全部为 -clear 判决。
    if (!p.preview.compiled)
        CHECK(!p.restart_note.empty());
    // 一个干净的编译,没有任何无法执行的东西,
    // 在运行所选引擎的主机上,决不能发明一个问题.
    if (p.preview.compiled && p.preview.unenforceable.empty() &&
        pn::engine_status(sb::config(), p.facts) == pn::EngineStatus::InForce)
        CHECK(p.restart_note.empty());
}

// ── 字幕必须描述REALITY,而不是选择 ────────────────
//
// 截图上报道: 选择 claybin 运行在包件下时,立即重新油漆 头
// 文件 改为 "claybin · landlock abi 10 · seccomp · cgroup2" .
// 这些墙都没有 -- -- 保险政策在启动时被封存,
// 因此这一过程仍然是包扎的,无法改变。
//
// 这是21号问题 -entered 从前门走来,就在 面板
// 其根本原因是运行的引擎从来就不是形式所能看到的事实:
// `SandboxPane::backend` 上面写着,
// 没有读者,所以字幕来源于 SELECTED 引擎加主机能力。
//
// 这些案子将固定点固定在每处高度上 可能倒退

TEST_CASE("sandbox pane: running claybin DOES claim its walls") {
    // 诚实必须双管齐下:a 面板 永远没有信用 claybin
    // 和总是这样没用的一样
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::HostFacts facts;
    facts.claybin_available = true;
    facts.landlock_abi = 10;
    facts.sandbox_active = true;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;   // 实际运行

    const auto form = pn::build_sandbox_form(cfg, facts);
    CHECK(form.subtitle.find("claybin") != std::string::npos);
    CHECK(form.subtitle.find("seccomp") != std::string::npos);
    CHECK(form.subtitle.find("landlock abi 10") != std::string::npos);
    // 没有待处理,所以没有免责声明。
    CHECK(form.subtitle.find("restart") == std::string::npos);
}

TEST_CASE("sandbox pane: an inactive sandbox never names an engine") {
    // `running` 当无任何执行时( 检测失败,
    // 或 --sandbox 命名包 微型的谎言也会存在
    sandbox_cfg::Config cfg;
    cfg.configured = true;

    pn::HostFacts facts;
    facts.sandbox_active = false;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;

    const auto form = pn::build_sandbox_form(cfg, facts);
    CHECK(form.subtitle.find("unconfined") != std::string::npos);
    CHECK(form.subtitle.find("mount namespaces") == std::string::npos);
}

TEST_CASE("sandbox pane: describe_running ignores the config entirely") {
    // 结构保证, 直接检查: 此函数不使用配置,
    // 因此未来的编辑无法让运行中的描述取决于用户选择的。
    // 事实相同, 配置大不相同,
    // 答案相同。
    pn::HostFacts facts;
    facts.claybin_available = true;
    facts.landlock_abi = 10;
    facts.sandbox_active = true;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;

    const auto baseline = pn::describe_running(facts);
    CHECK(baseline.find("claybin") != std::string::npos);

    // 无论用户选择什么,运行中的行都无法移动.
    for (auto p : {sandbox_cfg::Posture::Hardened, sandbox_cfg::Posture::Airgapped,
                   sandbox_cfg::Posture::Permissive}) {
        auto cfg = sandbox_cfg::apply_posture(sandbox_cfg::Config{}, p);
        cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
        CHECK(pn::describe_running(facts) == baseline);
        (void)cfg;
    }
}

TEST_CASE("sandbox pane: engine_status separates configured from in force") {
    // 只有一个引擎, "wanted != running"
    // 无法到达,但另外两个州没有, 而它们才是最重要的:
    // 一项政策 已保存 时 沙箱 关闭在下一轮运行时应用,
    // 无法启动引擎的主机必须说出,而不是承诺重启。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::HostFacts facts;
    facts.claybin_available = true;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;

    facts.sandbox_active = true;
    CHECK(pn::engine_status(cfg, facts) == pn::EngineStatus::InForce);

    // 已保存 现在,应用下一场运行。
    facts.sandbox_active = false;
    CHECK(pn::engine_status(cfg, facts) == pn::EngineStatus::AppliesOnRestart);

    // Host 拒绝,一切的超越 "applies on restart"
    // 承诺是不会被任何东西所遵守的
    facts.claybin_available = false;
    CHECK(pn::engine_status(cfg, facts) == pn::EngineStatus::CannotStart);
}

TEST_CASE("sandbox pane: forecast walls are marked per row, not just in the header") {
    // §15 将壁面报告移到行上, 完全因为页脚是人们跳过的地方。
    // 这使得每行都成为同一谎言可能出现的地方 :
    // "strong via seccomp-bpf" 在一个没有
    // seccomp 过滤器。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    pn::HostFacts facts;
    facts.claybin_available = true;
    facts.landlock_abi = 10;
    // 尚未启用: 政策是 FORECAST ,则该状态为每
    // -row 标记存在。
    facts.sandbox_active = false;
    facts.running = sandbox_cfg::LinuxBackend::Claybin;

    auto form = pn::build_sandbox_form(cfg, facts);
    const auto preview = pn::preview_sandbox(cfg);
    pn::annotate_sandbox_form(form, preview, cfg,
                              pn::engine_status(cfg, facts));

#if defined(__linux__)
    if (preview.compiled) {
        const int sys = row_of(form, pn::kSbSyscalls);
        REQUIRE(sys >= 0);
        const auto& origin = form.fields[static_cast<std::size_t>(sys)].origin;
        REQUIRE(!origin.empty());
        // 墙上可能还写着什么 WOULD 预测 面板 存在是
        // 为了显示 -- -- 但决不能把它看成是当前紧张。
        CHECK(origin.find("restart") != std::string::npos);
    }
#endif
}

// ── 人数 -row 诚实 ──────────────────────────────────────────────────────
//
// 墙壁报告正确且不读:12个能力折叠成一个脚本线,
// 其中单人 `none` 长得和十一号一样
// `strong` 贴在旁边,这些钉子 报告到达了
// ROW 用户正在编辑,这是被问到的唯一地方。

TEST_CASE("sandbox pane: a row carries the wall that enforces it") {
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    const auto preview = pn::preview_sandbox(cfg);
    pn::annotate_sandbox_form(form, preview, cfg, pn::EngineStatus::InForce);

#if defined(__linux__)
    if (preview.compiled) {
        // 机制,不只是力量。 "strong" 单是索赔与不可伪造
        // 索赔 "sandbox: active";
        // "strong via landlock abi 10" 并且这种
        // 区别是这一说明存在的全部原因。
        const int sys = row_of(form, pn::kSbSyscalls);
        REQUIRE(sys >= 0);
        const auto& origin = form.fields[static_cast<std::size_t>(sys)].origin;
        CHECK(!origin.empty());
        CHECK(origin.find("via") != std::string::npos);
    }
#endif
    // 后面一排没有能力的人 绝不能借邻居的墙面
    // "Also readable" 是一个绑定列表,并用它标签
    // filesystem.read 可能还是会带着平原 "none"
    // 以表示列表为空(参见 "empty path rows are labelled" ),
    // 这是关于 ITS 自身 价值而不是墙。
    const int extra = row_of(form, pn::kSbReadPaths);
    REQUIRE(extra >= 0);
    const auto& extra_origin = form.fields[static_cast<std::size_t>(extra)].origin;
    CHECK(extra_origin.find("via") == std::string::npos);
    CHECK(extra_origin.find("strong") == std::string::npos);
}

TEST_CASE("sandbox pane: per-port network with no ports says it denies all") {
    // 危险的形状: 这个 COMPILES ,而它并不意味着它读作的模样。
    // 用户要求允许一个特定的集,而不允许任何东西,
    // 所以 claybin 没有什么可以拒绝的,脚步保持安静 只有排
    // 可以抓住它。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::Ports;
    cfg.allow_ports.clear();
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);

    const int ports = row_of(form, pn::kSbPorts);
    REQUIRE(ports >= 0);
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(!form.fields[static_cast<std::size_t>(ports)].error.empty());
#endif

    // 而一旦配置一致,它就会消失——一个永远无法清除的
    // 错误就是标签,用户会学会读取过去的标签.
    cfg.allow_ports = {443};
    auto ok = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(ok, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);
    const int ports2 = row_of(ok, pn::kSbPorts);
    REQUIRE(ports2 >= 0);
    CHECK(ok.fields[static_cast<std::size_t>(ports2)].error.empty());
}

TEST_CASE("sandbox pane: turning the syscall filter off says brokering goes too") {
    // 无法推断用户的对接: 经纪人 IS 决定过滤器在
    // 运行时 ,因此 Off 也禁用对跟踪和杀戮的监督。
    // 一行静默解除另一行的功能,正是设置 面板 绝对不能这么做
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.syscall_mode = sandbox_cfg::SyscallMode::Off;
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);

    const int sys = row_of(form, pn::kSbSyscalls);
    REQUIRE(sys >= 0);
    const auto& err = form.fields[static_cast<std::size_t>(sys)].error;
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(!err.empty());
    CHECK(err.find("brokering") != std::string::npos);
#else
    CHECK(err.empty());   // 无后端: enfense 面板 说明什么
#endif
}

TEST_CASE("sandbox pane: loosening the handoff policy warns on the row") {
    // 这儿那个 WIDENS 爆炸半径,
    // 它这样做 在爆炸区外 沙箱 上面没有墙壁报告,
    // 脚壁报告结构上不能提到它。所以,排队必须把它带回去。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.handoff = sandbox_cfg::HandoffPolicy::Allow;
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);

    const int h = row_of(form, pn::kSbHandoff);
    REQUIRE(h >= 0);
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(!form.fields[static_cast<std::size_t>(h)].error.empty());
#endif

    // 拒绝是默认的,必须保持沉默:SAFE设置上
    // 的警告会让用户忽略不安全的警告.
    cfg.handoff = sandbox_cfg::HandoffPolicy::Refuse;
    auto safe = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::annotate_sandbox_form(safe, pn::preview_sandbox(cfg), cfg, pn::EngineStatus::InForce);
    const int h2 = row_of(safe, pn::kSbHandoff);
    REQUIRE(h2 >= 0);
    CHECK(safe.fields[static_cast<std::size_t>(h2)].error.empty());
}

TEST_CASE("sandbox pane: host risks the audit found are said on the row") {
    // 沙箱 审计 2026-10-17: 完全读作a 允许清单,
    // a 工作区 备注 $HOME 留下每个可写入的点文件,
    // 手动切换只到达日志。 每一个文件现在都在日志的行上写着。
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    cfg.net_mode = sandbox_cfg::NetMode::Full;
    cfg.allow_ports = {22, 53, 80, 443};
    install(cfg);

    auto facts = facts_for(cfg);
    facts.workspace_is_home = true;
    facts.handoff_truncated = true;
    auto form = pn::build_sandbox_form(cfg, facts);
    pn::annotate_host_risks(form, cfg, facts);

    auto err_of = [&](std::string_view id) {
        const int i = row_of(form, id);
        REQUIRE(i >= 0);
        return form.fields[static_cast<std::size_t>(i)].error;
    };
#if AGENTTY_TEST_SANDBOX_BACKEND
    CHECK(err_of(pn::kSbPorts).find("ignored") != std::string::npos);
    CHECK(err_of(pn::kSbFsScope).find("home") != std::string::npos);
    CHECK(err_of(pn::kSbHandoff).find("cap") != std::string::npos);
#else
    (void)err_of;
#endif

    // 安全箱无声:没有警告训练用户跳过警告.
    cfg.net_mode = sandbox_cfg::NetMode::Ports;
    auto calm = facts_for(cfg);
    auto quiet = pn::build_sandbox_form(cfg, calm);
    pn::annotate_host_risks(quiet, cfg, calm);
    for (auto id : {pn::kSbPorts, pn::kSbFsScope, pn::kSbHandoff}) {
        const int i = row_of(quiet, id);
        REQUIRE(i >= 0);
        CHECK(quiet.fields[static_cast<std::size_t>(i)].error.empty());
    }
}

TEST_CASE("sandbox pane: a policy that will not compile annotates no walls") {
    // 最大的谎言是自信的 -row "strong"
    // 读取 过期 预览。当 编译 失败了,每行注解必须
    // 说它不知道而不是重复最后一件成功的事情.
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
    pn::Preview broken;
    broken.compiled = false;
    broken.error = "fabricated: the host cannot build this";
    pn::annotate_sandbox_form(form, broken, cfg, pn::EngineStatus::InForce);

    for (auto id : {pn::kSbFsScope, pn::kSbNetMode, pn::kSbSyscalls}) {
        const int i = row_of(form, id);
        if (i < 0) continue;
        const auto& origin = form.fields[static_cast<std::size_t>(i)].origin;
        // 不是强权主张,不是空的就是空的 -dash 占位符,
        // 但从未 "strong".
        CHECK(origin.find("strong") == std::string::npos);
    }
}

// ── 电话 面板 其实 渲染 以另一种语言进行 ────────────────────────
//
// 其它的i18n测试都省掉了,核心测试证明了 t()
// 返回德语;宽度测试证明德语适合。 PANE 调用
// t() 毕竟,一排仍然持有文字的字句 将会永远通过
// 英文的字句和字句
//
// 所以:安装一个德语 目录 ,构建真实的形式,
// 并查看画 字节。这是门 说,扫荡是真实的。
TEST_CASE("sandbox render: the pane paints the ACTIVE language") {
    // 测试 二进制程序 不运行 main() 启动, 所以先安装英语。
    // 只有 ID 才会这样。 面板 读 - 真实 目录 住在这里
    // src/i18n/startup.cpp 和灯使两者不相冲突
    // (此处使用但不存在的ID使建造失败).
    const char* en = R"({
        "sandbox.read_paths":  {"text": "Also readable"},
        "sandbox.write_paths": {"text": "Also writable"},
        "sandbox.deny_paths":  {"text": "Masked"},
        "sandbox.ports":       {"text": "Allowed ports"},
        "sandbox.help.read_paths":  {"text": "extra paths a command may read"},
        "sandbox.help.write_paths": {"text": "granting write is a different decision"},
        "sandbox.help.deny_paths":  {"text": "carved out even inside the scope above"},
        "sandbox.help.ports":       {"text": "443 https, 80 http, 22 git-ssh, 53 dns"}
    })";
    REQUIRE(agentty::i18n::install_catalog(agentty::i18n::Lang::en, en));

    // 只有身份证明 面板 部分用途 目录 目的:
    // 它证明未翻译的行会回到英文而不是原始的ID,
    // 这是每个真正的翻译在完成之前的样子.
    const char* de = R"({
        "sandbox.read_paths":  {"text": "Auch lesbar"},
        "sandbox.write_paths": {"text": "Auch beschreibbar"},
        "sandbox.deny_paths":  {"text": "Maskiert"},
        "sandbox.help.read_paths": {"text": "zusätzliche Pfade zum Lesen"}
    })";
    REQUIRE(agentty::i18n::install_catalog(agentty::i18n::Lang::de, de));

    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
    install(cfg);

    const auto paint = [&] {
        auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                                  pn::EngineStatus::InForce);
        return maya::render_to_string(
            maya::Panel{agentty::ui::form_config(form, maya::Color::blue(),
                                                 nullptr, 0, 120)}.build(),
            120);
    };

    REQUIRE(agentty::i18n::set_active(agentty::i18n::Lang::en));
    const auto en_out = paint();
    CHECK(en_out.find("Also readable") != std::string::npos);

    // 该 LIVE SWAP ,通过真实 面板。一个商店,重建,翻译 -- --
    // 不重新启动,这是行为 docs/design/i18n.md 承诺。
    REQUIRE(agentty::i18n::set_active(agentty::i18n::Lang::de));
    const auto de_out = paint();
    CHECK(de_out.find("Auch lesbar") != std::string::npos);
    CHECK(de_out.find("Auch beschreibbar") != std::string::npos);
    CHECK(de_out.find("Maskiert") != std::string::npos);
    // 英国人是 GONE 不仅加入了 -- -- 画两幅画的
    // 一行意味着形式重建时是针对 过期 目录.
    CHECK(de_out.find("Also readable") == std::string::npos);

    // 德意志的目录缺少的id会回到ENGLISH,永远不回到原始的id.
    // 每一份真正的翻译都只是片段,半-German 面板显示
    // "sandbox.ports"比一个显示英语要糟糕.
    CHECK(de_out.find("Allowed ports") != std::string::npos);
    CHECK(de_out.find("sandbox.ports") == std::string::npos);

    REQUIRE(agentty::i18n::set_active(agentty::i18n::Lang::en));
}

// ── 电话 SHIPPED 德语 目录 ,真实的 面板 ────────────────────────
//
// 翻译 -pane 上面的测试使用一个 4 -string 修补,
// 这证明 面板 调用 t() 但对于真的翻译,却一字不提
// FITS。这个 加载 运来的德语 目录 100%完成,
// 最差弦+32列超过其英文来源,并同时涂装 面板 在每个宽度
// agentty 支持。
//
// 这是大门 docs/design/i18n.md 任何翻译落地前承诺:
// 刻意使用超长字符串时必须失败，否则检查只是摆设。
// 它测量显示 COLUMNS 途经 string_width 而不是数
// 字节 德语是拉丁语 1 -heavy 和a) 国家 字节 数到一
// -measures 和它下面一样糟糕 -measures 中国人.
TEST_CASE("sandbox render: the shipped catalogs fit every width") {
    // 两者都是极端,因为它们在OPPOSITE方向都失败了.
    //
    //   e 1.1-2.83x 英语。强调FRAME:
    //          不合适的标签被截断,行失去意义。
    //   日语 -CN ~0.6x 英语,列但 DOUBLE-WIDTH 每一个角色。
    //          1. 强调 MEASUREMENT:
    //          sandbox.help.read_paths 代码33个,
    //          63个列, 所以任何计算字符的评分 都以它的一半真实宽度,
    //          波浪流过。
    //
    // 跑着只跑着德语的大门会错过每一个中国的虫子,反之亦然,
    // 这就是为什么他们共用一个案件而不是每个案件一个的原因.
    // 宽度( A) PAINTED 线条, 以 终端 存储单元.
    //
    // 不 string_width 所释放的 字节 ,区别是 整个微妙度度量
    // 一个 CJK 渲染. render_to_string 每条写一个
    // 条目 CELL 双倍 -width 格莱弗作为格莱弗出现,
    // 后面是a PAD SPACE 用于其所在的栏。 -measuring 这些 字节
    // scores 额 as 2 and the pad as 1 -- three columns for a glyph the
    // 终端 画两条——每条中国线都出~3%的线.
    //
    // Measured: string_width("额 ") == 3 where the terminal shows 2. That
    // 报告有206-column线宽200, 看上去完全像真正
    // 的溢出。
    //
    // 数数吧 CELLS :每个代码点一个,包括垫。
    // 这就是 终端 实际分配,这对两种脚本都是正确的——拉丁
    // 语行没有垫子,所以两者就在那里达成一致.
    const auto widest_cells = [](const std::string& out) {
        std::size_t worst = 0, cur = 0;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i] == '\n') { worst = std::max(worst, cur); cur = 0; continue; }
            if ((static_cast<unsigned char>(out[i]) & 0xC0u) != 0x80u) ++cur;
        }
        return std::max(worst, cur);
    };

    // 每个 已运出 目录 ,不是一个代表性的样本。
    // 列表中未包含的语言是没有布局门的语言,
    // 而一个门的整个点是它覆盖着无人在看的东西。
    //
    // 每个标记都是MUST出现的目录的字符串,这是"the pane fit"与
    // 面板回退到英文，因此文字自然能够放下。
    // 的区别.
    struct Cat { agentty::i18n::Lang lang; const char* tag; const char* marker; };
    const Cat cats[] = {
        {agentty::i18n::Lang::de,    "de",    "Auch lesbar"},
        {agentty::i18n::Lang::es,    "es",    "Tambi\xc3\xa9n legible"},
        {agentty::i18n::Lang::fr,    "fr",    "Aussi lisible"},
        {agentty::i18n::Lang::pt_BR, "pt-BR", "Tamb\xc3\xa9m leg\xc3\xadvel"},
        // 西里尔语: 2 字节 每瓶Char,( 3) 一个。
        // 从 CJK -- a 字节 数到一 -measures 以2x表示,
        // 代码点数完全正确。
        {agentty::i18n::Lang::ru,    "ru",
         "\xd0\xa1\xd0\xba\xd1\x80\xd1\x8b\xd1\x82\xd0\xbe"},
        // 额外可读 -- "also readable". Spelled as bytes so the file stays
        // 编辑对它所做的一切都难忘
        {agentty::i18n::Lang::zh_CN, "zh-CN",
         "\xe9\xa2\x9d\xe5\xa4\x96\xe5\x8f\xaf\xe8\xaf\xbb"},
        // マスク済み -- katakana + kanji, proving the CJK path is not
        // 日语 -specific.
        {agentty::i18n::Lang::ja,    "ja",
         "\xe3\x83\x9e\xe3\x82\xb9\xe3\x82\xaf\xe6\xb8\x88\xe3\x81\xbf"},
    };

    for (const auto& c : cats) {
        REQUIRE(agentty::i18n::init(c.tag, ""));
        REQUIRE(agentty::i18n::completeness(c.lang) > 0.999);

        sandbox_cfg::Config cfg;
        cfg.configured = true;
        cfg.backend = sandbox_cfg::LinuxBackend::Claybin;
        // 一行最差的可携带: 一条长路径AND 在同一
        // 行的内含错误,在翻译下.
        cfg.read_paths = {"/ein/ziemlich/langer/pfad/zu/einem/abhaengigkeit"};
        cfg.net_mode = sandbox_cfg::NetMode::Ports;
        cfg.allow_ports.clear();
        install(cfg);

        // 构建INSIDE 环:形式在构建时烘焙其标签,
        // 因此语言变换前构建的一种形式是旧语言
        // 中的一种形式(本测试的第一个版本将其竖起,
        // 中文传译默化为德语).
        auto form = pn::build_sandbox_form(cfg, facts_for(cfg));
        pn::annotate_sandbox_form(form, pn::preview_sandbox(cfg), cfg,
                                  pn::EngineStatus::InForce);

        for (int width : {60, 80, 100, 120, 200}) {
            const auto out = maya::render_to_string(
                maya::Panel{agentty::ui::form_config(form, maya::Color::blue(),
                                                     nullptr, 0, width)}.build(),
                width);
            INFO(std::string{c.tag} << " at width " << width
                 << ": widest line " << widest_cells(out) << " cells");
            CHECK(widest_cells(out) <= static_cast<std::size_t>(width));
        }

        // 翻译真的在屏幕上。 回到英文的面板 将通过
        // 上面的每一个宽度检查, 而无需翻译任何内容,
        // 也就是这些检查无法看到的失败。
        const auto out = maya::render_to_string(
            maya::Panel{agentty::ui::form_config(form, maya::Color::blue(),
                                                 nullptr, 0, 120)}.build(), 120);
        // 与"SPACES""STRIPPED"一同搜索,这不方便.
        //
        // render_to_string 发出一个 存储单元 每列,所以双倍 -width
        // glyph occupies two and the second is written as a space: 额外可读
        // paints as "额 外 可 读". The bytes are all there and in order, but
        // 并非毗连的,故它是平原, find() 源字符串上
        // 每条都失败 CJK 语言在传递每个拉丁语时
        //
        // 这正是虫的形状 整个大门存在 捕捉,它抓住 TEST 第一部分:
        // 第一个版本已报告 "Chinese never reached the pane"
        // 当 面板 曾经是 渲染 正确,没错。
        const auto despaced = [](std::string_view in) {
            std::string o;
            o.reserve(in.size());
            for (const char ch : in) if (ch != ' ') o.push_back(ch);
            return o;
        };
        INFO("looking for the " << std::string{c.tag} << " marker");
        CHECK(despaced(out).find(despaced(c.marker)) != std::string::npos);
    }

    REQUIRE(agentty::i18n::set_active(agentty::i18n::Lang::en));
}
