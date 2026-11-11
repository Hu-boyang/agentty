// smart_tuning_settings_test ——数字路由键为 PERSISTED 排行数.
//
// 这三种(复杂门槛,深 -band 边距, 自行 -correction
// 上限)只能通过 AGENTTY_SMART_ * 环境变量。
// 这使得它们无法发现(你必须读取) domain/smart_tuning.hpp
// 学习他们的存在),会话 -only ,且未验证。它们都是设置
// -registry 也就是说,现在有四样东西要同时持有, 并且这个文件
// 声称每个东西:
//
//   1. 所达到的行数 store::Settings ,没有 store::RagConfig - 和一行 - 和一条
//      属于OTHER的配置结构是无法达到的,
//      而不是默默地写作,
//   2. 每一入境点的值为每行的值为CLAMPED,所以
//      配置不能持有UI无法生成的值,
//   3. 连续生存 保存 -> 加载,
//   4. 环境变量仍大于存储值的WINS(即:
//      设置 UI 承诺中的锁定行),在进入时被夹住,
//      一个不正确的设置会让配置的值处于状态,
//      而不是重置到已发送的默认值。

#include "agtest.hpp"

#include "agtest_fx.hpp"
#include "agentty/domain/smart_mode.hpp"
#include "agentty/domain/smart_tuning.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"            // publish_derived
#include "agentty/runtime/app/update/internal.hpp"   // apply_smart
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/runtime/settings_registry.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/store/store.hpp"
#include "agentty/tool/subagent.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace {

namespace fs  = std::filesystem;
namespace reg = agentty::settings::registry;
namespace tun = agentty::smart::tuning;
using agentty::store::Settings;

// 路由线条,命名 一次 测试通过 注册表 IDs——表上宣布的
// 和表上宣布的是相同的字符串 面板 抬头看,所以这里出现一个名字,
// 是一个失败的 find() 而不是作为一次测试 静静地
// 检查一行 已经不存在。
constexpr const char* kCut  = "smart.complex_threshold";
constexpr const char* kDeep = "smart.deep_margin";
constexpr const char* kBias = "smart.bias_clamp";

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// 私人数据根数( $1) 调用 设置在 $AGENTTY_HOME (
// 见A/C.5/61/L.48,第16段) util/user_root.hpp) —
// 不 AGENTTY_CONFIG_DIR ,此文件曾经设置的,以及 持久化 不读,
// 所以 "isolated" 测试是悄悄分享开发商的真实性 ~/.agentty. 返回根,
// 以便测试可以将固定文件写入其中。
//
// 与账户测试相同的独有性纪律:兄弟测试过程可以在相同的毫秒开始,
// 一个二进制程序中的若干个案例每个需要一个干净
// 的商店.
fs::path isolate_config_dir() {
    static std::atomic<int> seq{0};
    auto dir = fs::temp_directory_path()
             / ("agentty_smarttune_" + std::to_string(now_ms())
                + "_" + std::to_string(
#if defined(_WIN32)
                      static_cast<long>(::_getpid())
#else
                      static_cast<long>(::getpid())
#endif
                  )
                + "_" + std::to_string(seq.fetch_add(1)));
    fs::create_directories(dir);
#if defined(_WIN32)
    _putenv_s("AGENTTY_HOME", dir.string().c_str());
#else
    setenv("AGENTTY_HOME", dir.string().c_str(), 1);
#endif
    return dir;
}

void clear_env() {
#if !defined(_WIN32)
    unsetenv("AGENTTY_SMART_COMPLEX_THRESHOLD");
    unsetenv("AGENTTY_SMART_DEEP_MARGIN");
    unsetenv("AGENTTY_SMART_BIAS_CLAMP");
#endif
}

} // 命名空间结束。

TEST_CASE("smart tuning: rows bind to the config struct that owns them") {
    Settings s;

    const auto* cut = reg::find(kCut);
    REQUIRE(cut != nullptr);
    CHECK(cut->owner() == reg::Owner::Smart);
    CHECK(reg::is_default(s.smart, *cut));

    CHECK(reg::set(s.smart, *cut, "5"));
    CHECK(reg::get(s.smart, *cut) == "5");
    CHECK(s.smart.complex_threshold == 5);
    CHECK_FALSE(reg::is_default(s.smart, *cut));

    reg::reset(s.smart, *cut);
    CHECK(reg::is_default(s.smart, *cut));
    CHECK(s.smart.complex_threshold == tun::kComplexDefault);

    // 另一半表格无法通过设置到达, 屏障比一个更坚固 运行时 检查
    // : `reg::set(settings, rag_row, …)`
    // 没有 COMPILE. get/set 每一个主人都超
    // 载了 所以 调用 命名错误的结构没有可行的超载性——错误
    // 完全无法达到测试.
    //
    // 仍然值得坚持的是,行尸的树枝上的标记,以及两个半身都是非的。
    // -empty (单位:千美元) owner() 答案是: 已经 `Rag`
    // 因为一切都会 编译 并默默地从中放下每条路由线 保存, 加载 财务报告和
    // 已审计财务报表 apply_env).
    const auto* rag_row = reg::find("rag.mmr");
    REQUIRE(rag_row != nullptr);
    CHECK(rag_row->owner() == reg::Owner::Rag);

    int settings_rows = 0, rag_rows = 0, web_search_rows = 0;
    for (const auto& d : reg::kSettings) {
        switch (d.owner()) {
            case reg::Owner::Smart:  ++settings_rows; break;
            case reg::Owner::Rag:    ++rag_rows;      break;
            case reg::Owner::WebSearch: ++web_search_rows;   break;
        }
    }
    // 两者均将非 -empty 实际 不变量 - 一个 owner()
    // 答案是: 已经 `Rag` 因为一切都会 编译 并默默地从中
    // 放下每条路由线 保存, 加载 财务报告和已审计财务报表
    // apply_env。准确的计算不是 编号 不变量 : 它被
    // 固定在 3 上, 并且第一次添加一个路由旋钮时必须编辑,
    // 这是一种测试, 因为它不对。 请指定必须存在的行。
    CHECK(settings_rows > 0);
    CHECK(rag_rows > 0);
    CHECK(web_search_rows > 0);
    for (auto id : {"smart.complex_threshold", "smart.deep_margin",
                    "smart.bias_clamp", "smart.route_main_turn",
                    "smart.main_turn_floor"}) {
        const auto* row = reg::find(id);
        REQUIRE(row != nullptr);
        CHECK(row->owner() == reg::Owner::Smart, id);
    }
}

TEST_CASE("smart tuning: every entry point clamps to the row's range") {
    Settings s;
    const auto* cut = reg::find(kCut);
    REQUIRE(cut != nullptr);

    // 被打得团团转,从未被拒绝 -then-forgotten : 一只手 : -edited
    // settings.json 一定不能将配置设置到 UI 永远无法生产。
    (void)reg::set(s.smart, *cut, "999");
    CHECK(s.smart.complex_threshold == tun::kComplexMax);
    (void)reg::set(s.smart, *cut, "-4");
    CHECK(s.smart.complex_threshold == tun::kComplexMin);

    // 注册表的幅度和smart::tuning的幅度是相同的数字——有
    // static_assert的表示,但也有动态地断言它,因此,
    // 这个文件的读者可以看到不变量所表述的.
    CHECK(cut->min == tun::kComplexMin);
    CHECK(cut->max == tun::kComplexMax);
}

TEST_CASE("smart tuning: a configured value survives save and load") {
    isolate_config_dir();
    clear_env();

    Settings s;
    s.smart.complex_threshold = 6;
    s.smart.deep_margin       = 7;
    agentty::persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    const Settings back = agentty::persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(back.smart.complex_threshold == 6);
    CHECK(back.smart.deep_margin == 7);
    // 留下的一行不写, 并返回为已运出的默认值。
    CHECK(back.smart.bias_clamp == tun::kBiasClampDefault);
}

TEST_CASE("smart tuning: the Retrieval pane does not carry the routing rows") {
    // 它们是被短暂地放在这里的—— 面板 刚刚走过
    // 注册表 ——使它们同时可以到达,无法找到.
    // 握手者属于它所管辖的事物的旁;这些支配着智能模式。
    namespace rs = agentty::rag_settings;
    agentty::rag::embed::EmbedConfig cfg;
    const Settings s;

    for (bool advanced : {false, true}) {
        const auto f = rs::build_form(cfg, agentty::store::RagMode::On, s, {},
                                      advanced);
        for (const char* id : {kCut, kDeep, kBias})
            CHECK(f.find(id) == nullptr);
        // 检索行仍然在那里——这并非"the walk broke".
        CHECK(f.find("rag.mmr") != nullptr);
    }
}

TEST_CASE("smart tuning: the rows live in the SMART MODE pane") {
    // 设置生命是它是否起作用的一部分。 这些旋钮决定了智能模式的路线,
    // 因此它们属于开通路由的开关和它所填的空档边。
    // 升级模型的积极程度如何？
    // 打开 ^S ,而不是检索 面板 把它们放在回收系统之下,
    // 使得它们技术上可以达到,实际上是看不见的,
    // 这与将它们留在一个掩体中一样。
    //
    // 高级-gated:四行重要事项不应与三行竞争,
    // 而三行大多不应触碰.
    namespace sf = agentty::smart_form;

    sf::Inputs in;
    in.enabled = true;
    in.smart.complex_threshold = 5;
    in.smart.deep_margin       = 6;
    in.smart.bias_clamp        = 4;

    const auto basic = sf::build_form(in);
    in.advanced = true;
    const auto adv = sf::build_form(in);

    for (const char* id : {kCut, kDeep, kBias}) {
        CHECK(basic.find(id) == nullptr);
        CHECK(adv.find(id) != nullptr);
    }

    // 显示数字行 CONFIGURED 值,
    // 带有 注册表 范围——全部从表格中预测,
    // 没有重新排入 面板.
    const auto* cut = adv.find(kCut);
    REQUIRE(cut != nullptr);
    CHECK_FALSE(cut->locked);
    CHECK(cut->label == "Complexity cut");     // 编号 注册表 标签
    const auto* num = std::get_if<agentty::form::field::Number>(&cut->value);
    REQUIRE(num != nullptr);
    CHECK(num->value == 5);
    CHECK(num->min == tun::kComplexMin);
    CHECK(num->max == tun::kComplexMax);
}

#if !defined(_WIN32)
TEST_CASE("smart tuning: an env override locks the row in the pane") {
    // 证明和 加锁 是一个 REGISTRY 而不是说 面板 说,
    // 我... 面板 用于计算 "is an env var shadowing this?" 自己,
    // 通过三 锁 字符串中 —— 表格已经对
    // 每行执行规则的第二个执行。 走到表格中意味着
    // 智能模式行得到与检索行一贯相同的待遇, 免费。
    namespace sf = agentty::smart_form;

    clear_env();
    setenv("AGENTTY_SMART_COMPLEX_THRESHOLD", "5", 1);

    sf::Inputs in;
    in.enabled  = true;
    in.advanced = true;
    in.env      = agentty::settings::registry::read_env(::agentty::IoAccess::grant());
    const auto f = sf::build_form(in);

    const auto* row = f.find(kCut);
    REQUIRE(row != nullptr);
    CHECK(row->locked);
    CHECK(row->origin == "env: AGENTTY_SMART_COMPLEX_THRESHOLD");

    // 排队 不 俯冲可编辑。 锁 是每行,而不是模式。
    const auto* other = f.find(kDeep);
    REQUIRE(other != nullptr);
    CHECK_FALSE(other->locked);

    clear_env();
}
#endif

TEST_CASE("smart tuning: the pane advertises the key that reveals them") {
    // 隐藏的行加一个未广告的密钥是带有额外台阶的env var.
    // 该注释是唯一在屏幕上说行存在的东西,
    // 因此它是特征的一部分而不是装饰——在BOTH的状态中断言,
    // 因此回路也是可见的.
    //
    // 钥匙是空信 不是 ^A: ^A 窗体层 光标-home 和默认 tmux 前缀,
    // 所以一个和弦从没有到达应用程序。 这就是为什么它将 TEXT
    // 而不是仅仅 "a note exists".
    namespace sf = agentty::smart_form;

    sf::Inputs in;
    in.enabled = true;

    const auto basic = sf::build_form(in);
    CHECK(basic.note.find("a") != std::string::npos);
    CHECK(basic.note.find("advanced") != std::string::npos);
    CHECK(basic.note.find("^A") == std::string::npos);

    in.advanced = true;
    const auto adv = sf::build_form(in);
    CHECK(adv.note.find("hide") != std::string::npos);
}

TEST_CASE("smart tuning: an existing settings.json still loads its pins") {
    // 进 -memory 形状从11个平地改为1个平地 RoleConfig.
    // 电话 FILE 格式没有——这就是证明,因为错误的代价是用户的
    // 固定模型在升级时悄悄地恢复到自动,他们发现这种模式是
    // 为什么使用了错误模型？
    //
    // 以原始形式写 JSON 目的:圆形 -tripping 结束
    // save() 即使双方一起改变 也都会通过 这正是虫子
    const auto dir = isolate_config_dir();
    clear_env();

    {
        std::ofstream f{dir / "settings.json"};
        f << R"({
          "smart": {
            "enabled": true,
            "strategic_model": "claude-opus-4-5",
            "strategic_effort": "high",
            "strategic_provider": "anthropic",
            "impl_model": "claude-sonnet-4-5",
            "utility_model": "claude-haiku-4-5",
            "complex_threshold": 6,
            "route_internal": true,
            "learn_routing": false
          }
        })";
    }

    const Settings s = agentty::persistence::load_settings(::agentty::IoAccess::grant());

    CHECK(s.smart.enabled);
    // 针是模型+努力+提供者,以及 `set` 是什么使 使 resolve_role
    // 尊重它,失去其中任何一个都是沉默 -revert 虫子,你这混蛋
    CHECK(s.smart.strategic.set);
    CHECK(s.smart.strategic.model == "claude-opus-4-5");
    CHECK(s.smart.strategic.provider == "anthropic");
    // 带有模型但没有明确的effort/provider的插槽仍然算作被钉住的
    // ——在这些字段存在之前所写的设置必须继续工作。
    CHECK(s.smart.implementation.set);
    CHECK(s.smart.implementation.model == "claude-sonnet-4-5");
    CHECK(s.smart.implementation.provider.empty());
    CHECK(s.smart.utility.set);
    // 调音行骑在同一个物体中.
    CHECK(s.smart.complex_threshold == 6);
    // 删除层的密钥被忽略,而不是致命的.
}

TEST_CASE("smart tuning: a pinned slot survives a save/load round trip") {
    isolate_config_dir();
    clear_env();

    Settings s;
    s.smart.enabled = true;
    s.smart.strategic = agentty::smart::SlotOverride{
        .model = "gpt-5", .effort = agentty::Effort::High,
        .set = true, .provider = "openai"};
    s.smart.deep_margin = 7;
    agentty::persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    const Settings back = agentty::persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(back.smart.enabled);
    CHECK(back.smart.strategic.set);
    CHECK(back.smart.strategic.model == "gpt-5");
    CHECK(back.smart.strategic.effort == agentty::Effort::High);
    CHECK(back.smart.strategic.provider == "openai");
    CHECK(back.smart.deep_margin == 7);
    // 空洞的空穴子没有固定 而不是成为空穴子
    CHECK_FALSE(back.smart.utility.set);
}

TEST_CASE("smart tuning: a config change reaches all three holders") {
    // 风扇 -out 智能模式配置中 三个 即时设置
    // settings.json ,则 Model 编号 UI 线程 读取,
    // 子剂路由器的 工作线程-thread 复制 - 系列中的每
    // 个错误都是 调用 网站更新了其中一两个。
    //
    // apply_smart 唯一能知道这三者的东西,这三者都这样,
    // 所以未来的道路是打开的 -codes "save and install"
    // 忘记路由器在这里失败, 而不是在用户的 `task` 向一个他们没有的模型
    // 的路径 -pinned 十分钟前
    isolate_config_dir();
    clear_env();

    // apply_smart RETURNS 编号 保存 以 副作用 ;
    // 测试 二进制程序 无法运行它,所以运行在这里。这就是一个可以按预期
    // 运行的断言 — 记录只有在存储时才能到达商店。 调用 实际返回 保存.
    static agentty::store::Settings persisted;
    persisted = agentty::store::Settings{};
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, auto, auto) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const auto&) {},
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const agentty::ThreadId&) {
                             return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return persisted; },
        .save_settings = [](const agentty::store::Settings& s) { persisted = s; },
        .new_thread_id = [] { return agentty::ThreadId{"t-smart"}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .auth          = {},
    });

    // 新鲜的 INSTALLED 路由器复制件。
    // set_smart ( )否 否 -ops 路由器未安装时(
    // 生产中正确 : `task` ——所以不在这里安装一个,
    // 第三个断言就会空洞地通过一个没有人保存的假象。
    agentty::tools::subagent::install(agentty::tools::subagent::Config{
        .model = "stub", .installed = true});
    agentty::tools::subagent::set_smart(::agentty::IoAccess::grant(), agentty::smart::RoleConfig{});
    REQUIRE_FALSE(agentty::tools::subagent::current().smart.enabled);

    agentty::Model m;
    agentty::smart::RoleConfig cfg;
    cfg.enabled = true;
    cfg.complex_threshold = 6;
    cfg.strategic = agentty::smart::SlotOverride{
        .model = "gpt-5", .effort = agentty::Effort::High,
        .set = true, .provider = "openai"};

    {
        // 通过 接口边界 ,像一个真实的 归并: apply_smart 只更改 Model,
        // publish_derived 路由器的信号
        auto save = agentty::app::publish_derived(
            m, agentty::app::detail::apply_smart(m, cfg));
        agtest::fx::Store store;
        store.settings = persisted;
        agtest::fx::run(save, store);
        persisted = store.settings;
    }

    // 1. 受困——在重启后幸存。
    CHECK(persisted.smart.enabled);
    CHECK(persisted.smart.complex_threshold == 6);
    CHECK(persisted.smart.strategic.model == "gpt-5");

    // 2. 安装在Model上——下个转弯时分类者读到的内容。
    CHECK(m.d.smart.enabled);
    CHECK(m.d.smart.complex_threshold == 6);
    CHECK(m.d.smart.strategic.model == "gpt-5");

    // 3. 推向子剂路由器——什么 `task` 工作线程 运行路线。
    const auto router = agentty::tools::subagent::current().smart;
    CHECK(router.enabled);
    CHECK(router.complex_threshold == 6);
    CHECK(router.strategic.model == "gpt-5");
}

TEST_CASE("smart tuning: registry::apply_env is the one resolution rule") {
    // 启动和设置 -pane 保存 双解析器 -over-stored
    // 输入 RoleConfig。它们 调用 编号 相同 函数,
    // 因为两者都写出的规则是 面板 最终应用了与重启不同的程序
    // ——用户更改了一个 knob, 看到了它需要 副作用 ,
    // 重新启动,并获得不同的答案。
    //
    // 没有 保存-side 调用 手柄一直存在,但没 LIVE :
    // 分类器读取 m.d.smart ,所以一个变化似乎没有做 直到下
    // 一次发射。
    namespace sm = agentty::smart;

    clear_env();
    {
        sm::RoleConfig c;
        c.deep_margin = 6; c.bias_clamp = 3; c.complex_threshold = 7;
        agentty::settings::registry::apply_env(::agentty::IoAccess::grant(), c);
        CHECK(c.deep_margin == 6);
        CHECK(c.bias_clamp == 3);
        CHECK(c.complex_threshold == 7);
    }
#if !defined(_WIN32)
    // 一个env 覆盖比存储值要高——同样层层地将
    // 面板广告中锁定的行.
    {
        setenv("AGENTTY_SMART_COMPLEX_THRESHOLD", "2", 1);
        sm::RoleConfig c;
        c.deep_margin = 6; c.bias_clamp = 3; c.complex_threshold = 7;
        agentty::settings::registry::apply_env(::agentty::IoAccess::grant(), c);
        CHECK(c.complex_threshold == 2);   // env 赢了
        CHECK(c.deep_margin == 6);         // 其他未触及
        clear_env();
    }
#endif
}

#if !defined(_WIN32)
TEST_CASE("smart tuning: an env var overrides the stored value") {
    isolate_config_dir();
    clear_env();

    Settings s;
    s.smart.complex_threshold = 6;
    s.smart.deep_margin       = 7;
    agentty::persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    // 关闭锁定的设置行广告。
    setenv("AGENTTY_SMART_COMPLEX_THRESHOLD", "2", 1);
    const Settings with_env = agentty::persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(with_env.smart.complex_threshold == 2);
    CHECK(with_env.smart.deep_margin == 7);      // 未调整行数

    // 进来的路上被撞了,就像UI编辑。
    setenv("AGENTTY_SMART_COMPLEX_THRESHOLD", "999", 1);
    CHECK(agentty::persistence::load_settings(::agentty::IoAccess::grant()).smart.complex_threshold
          == tun::kComplexMax);

    // shell配置文件中的typo不能默默地将配置的值
    // 重置到已运算的默认值上——最不令人惊讶的响应是
    // 用户配置的站点.
    setenv("AGENTTY_SMART_COMPLEX_THRESHOLD", "garbage", 1);
    CHECK(agentty::persistence::load_settings(::agentty::IoAccess::grant()).smart.complex_threshold == 6);

    clear_env();
    CHECK(agentty::persistence::load_settings(::agentty::IoAccess::grant()).smart.complex_threshold == 6);
}

TEST_CASE("smart tuning: an env var applies to a config with no smart block") {
    // 在用户更改某物之前,每个配置都不存在 "smart" 区域。
    // 导出时必须使用 副作用 里面也有,当洞穴被围住时,
    // 情况并非如此。 解析 就是这样
    isolate_config_dir();
    clear_env();

    agentty::persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), Settings{});   // 没有什么可以坚持的

    setenv("AGENTTY_SMART_DEEP_MARGIN", "6", 1);
    CHECK(agentty::persistence::load_settings(::agentty::IoAccess::grant()).smart.deep_margin == 6);
    clear_env();
}
#endif
