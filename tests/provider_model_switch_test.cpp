// provider_model_switch_test - 电话 provider/model 切换状态机器。
//
// 递归 锁 外地投诉
// 模型切换不正常：模型变了，提供方却没变。
//
//   1. STALE-FETCH GATE. fetch_models () 所获取的供应商的邮票 FOR;
//      编号 ModelsLoaded 状态更新函数 丢弃 a
//      载荷 其 人 名 provider_id 不符合活跃于
//      DELIVERY 没有门,两个快开关 将慢速分开 目录
//      获取和提供者 A 的迟到 目录 安装在提供者B下—— 选择
//      面板 然后提供无法运行的后端的模型 数据流 (选择一个和400s的要求,
//      或默默地) 数据流 错误的后端的模型)。
//
//   2. ACCEPTED FETCH INSTALLS 该 CATALOG 并保留用户的模型;
//      空模型从中填入。
//
// 开车经过 真实 app::update 状态更新函数 ,没有嘲讽 状态更新函数 路径。

#include "agtest_cap.hpp"
#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"  // app::detail::fused_rows_for_model
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/provider_rows.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/catalog_sources.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/io/persistence.hpp"

#include <cstdlib>
#include <unistd.h>

#include <optional>
#include <string>
#include <vector>

namespace pn = agentty::ui::panel;

using namespace agentty;

// ModelsLoaded 状态更新函数 触及deps().load_settings(最
// 亲近)和deps().save_settings(长期使用自动-corrected模式),
// 用-memory设置来固定它们。
static store::Settings g_settings;
static void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<Thread> { return std::nullopt; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& x) { g_settings = x; },
        .new_thread_id  = [] { return ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = auth::AuthHeader{auth::ApiKeyHeader{std::string{}}},
    });
}

// 将一个阶梯的副作用应用到树根商店.
//
// 持久化 是一个 EFFECT 现在—— 状态更新函数 返回描述写入
// 和主机执行的值。这些测试坚持 `g_settings`, i.e.
// 店里有什么东西,所以他们必须扮演主人的角色。
// 保存 只有手臂真的还回来时才会降落在这里
static void apply_fx(const Cmd& c) {
    agtest::fx::Store s;
    s.settings = g_settings;
    agtest::fx::run(c, s);
    g_settings = s.settings;
}

// A Model 与设置记录种子,方式 init() 种子它。 种子它,种子它。
//
// `m.d.persisted` IS 设置记录到现在...
// 状态更新函数 而不是再读一遍 接口边界 ,这就是阻止了两本书的漂移。
// init() 启动时指定一次; 构建空的测试 Model 要么
// 用空白记录开始生命 要么就用书根店的 内容来做 这正是拆分
// -brain 记录的存在是为了防止(此处读作
// "no provider is authed" ,所以保险丝 选择面板 空着
// 就来了).
//
// 活动提供者的播种方式相同: init() 复制
// 一次全球发射选择,然后从此开始 Model 拥有它。
//
// 调用 之后 设置 g_settings 财务报告和已审计财务报表 provider::select().
[[nodiscard]] static Model seeded_model() {
    Model m;
    m.d.persisted = g_settings;
    m.d.selection = provider::active();
    m.published_selection = m.d.selection;
    return m;
}

static ModelInfo mi(const char* id, const char* prov) {
    ModelInfo m;
    m.id = ModelId{id};
    m.display_name = id;
    m.provider = prov;
    m.supports_tools = true;
    return m;
}

TEST_CASE("provider model switch") {
    install_stub_deps();
    // 使主动提供者具有决定性: Anthropic 路径,其名称 "anthropic"
    // 是什么 是什么 , 是什么 ) active_provider_id (a) 报告。
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // ── 1: a ModelsLoaded 印花为 a 不同 提供者已丢弃 ──
    {
        Model m = seeded_model();
        m.s.models_loading = true;
        m.d.model_id = ModelId{"claude-opus-4-5"};

        ModelsLoaded stale;
        stale.models      = { mi("gpt-4o", "openai"), mi("o4-mini", "openai") };
        stale.provider_id = "openai";   // 获取为 Openai; 人类活动

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(stale)});
        apply_fx(cmd);
        CHECK(m2.d.available_models.empty(),
              "stale catalog must NOT be installed");
        CHECK(m2.d.model_id.value == "claude-opus-4-5",
              "active model untouched by a stale catalog");
        CHECK(m2.s.models_loading,
              "loading stays armed — the newer fetch is still in flight");
    }

    // ── 2: a ModelsLoaded 印有该图章 活动 供应商安装 ──
    //    并且即使没有列出,也保留用户的模型。
    //    用第一个模型(旧行为)来取代它,在后面将一个选择
    //    变成另一个模型,然后作为提供者的召回的已保存。
    {
        Model m = seeded_model();
        m.s.models_loading = true;
        m.d.model_id = ModelId{"some-unlisted-model"};

        ModelsLoaded fresh;
        fresh.models      = { mi("claude-opus-4-5", "anthropic"),
                              mi("claude-sonnet-4-5", "anthropic") };
        fresh.provider_id = "anthropic";

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(fresh)});
        apply_fx(cmd);
        CHECK(m2.d.available_models.size() == 2, "fresh catalog installed");
        CHECK(!m2.s.models_loading, "loading cleared on accepted fetch");
        CHECK(m2.d.model_id.value == "some-unlisted-model",
              "an unlisted model is kept, not swapped for the first one");
        CHECK(m2.s.status.find("keeping it") != std::string::npos,
              "and the user is told it isn't in the list");
    }

    // ── 2b: 选择NO型号,第一个型号填写 ──
    {
        Model m = seeded_model();
        m.s.models_loading = true;
        m.d.model_id = ModelId{""};

        ModelsLoaded fresh;
        fresh.models      = { mi("claude-opus-4-5", "anthropic"),
                              mi("claude-sonnet-4-5", "anthropic") };
        fresh.provider_id = "anthropic";

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(fresh)});
        apply_fx(cmd);
        CHECK(m2.d.model_id.value == "claude-opus-4-5",
              "an empty model is filled from the catalog");
    }

    // ── 2c: 列出的模型的不同拼写代号是同一个模型 ──
    {
        Model m = seeded_model();
        m.s.models_loading = true;
        m.d.model_id = ModelId{"Claude_Sonnet.4.5"};

        ModelsLoaded fresh;
        fresh.models      = { mi("claude-opus-4-5", "anthropic"),
                              mi("claude-sonnet-4-5", "anthropic") };
        fresh.provider_id = "anthropic";

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(fresh)});
        apply_fx(cmd);
        CHECK(m2.d.model_id.value == "Claude_Sonnet.4.5",
              "an alias of a listed model is left alone");
        CHECK(m2.s.status.find("keeping it") == std::string::npos,
              "and is not reported as unlisted");
    }

    // ── 2d: 清单上缺少的[1m] 变体 保留[1m。 ──
    //    (只有1M窗口被拒绝) 账户倒置, 回到它的 自身 基型,
    //    从来没有先到任何种类)
    {
        Model m = seeded_model();
        m.s.models_loading = true;
        m.d.model_id = ModelId{"claude-sonnet-4-5[1m]"};

        ModelsLoaded fresh;
        fresh.models      = { mi("claude-opus-4-5", "anthropic"),
                              mi("claude-sonnet-4-5", "anthropic") };
        fresh.provider_id = "anthropic";

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(fresh)});
        apply_fx(cmd);
        CHECK(m2.d.model_id.value == "claude-sonnet-4-5[1m]",
              "a [1m] pick isn't silently swapped for another model");
    }

    // ── 3: legacy/synthetic 分派 (空闲 provider_id ) 仍被接受 ──
    {
        Model m = seeded_model();
        m.s.models_loading = true;

        ModelsLoaded legacy;
        legacy.models = { mi("claude-opus-4-5", "anthropic") };
        // 空的 provider_id 号

        auto [m2, cmd] = app::update(std::move(m), Msg{std::move(legacy)});
        apply_fx(cmd);
        CHECK(m2.d.available_models.size() == 1,
              "unstamped payload accepted (back-compat)");
        CHECK(!m2.s.models_loading, "loading cleared");
    }
}

// ^E 模型中 选择面板 切换 a/ 数 -model 推理 -effort
// 覆盖和 必须 反馈: 翻转 目录 注册表 ,坚持设置,并设置状态敬酒
// (用于后退的守护) -after-move 默默地放下了土司 开车
// 经过 真实 状态更新函数 途经 app::update.
TEST_CASE("model picker ^E toggles reasoning override + feedback") {
    namespace pick = agentty::ui::pick;
    install_stub_deps();
    g_settings = store::Settings{};
    agentty::clear_reasoning_overrides();

    // 非 国 家 -chatgpt 真正无提供商 -reasoning 模型(代码、
    // 代码模型)在打开时突出显示 选择面板 - 推论确实如此 不 点燃它,
    // 所以 ^E 部队 -on 需要证明
    // 电话 选择面板 读取其缓存行,所以打开它通过 真实 状态更新函数 (种子)
    // provider_catalogs + fused_rows 而不是手 而不是手
    // -placing 编号 覆盖面板 如此的有名无实 目录 种子没有打开 -disk 证明书。
    g_settings.provider_keys["mistral"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("mistral"));
    Model m0 = seeded_model();
    m0.d.available_models = { mi("codestral-latest", "mistral") };
    m0.d.model_id = ModelId{"codestral-latest"};
    auto [m, _open] = app::update(std::move(m0), Msg{OpenModels{}});
    apply_fx(_open);
    REQUIRE(!m.d.fused_rows.empty());

    // 基线:推论说NOT是一个理性,所以没有压倒性,没有努力.
    CHECK(agentty::reasoning_override_for("codestral-latest") == -1);
    CHECK(!agentty::resolved_caps("codestral-latest").supports_effort());

    // 第1次 7 ^E : 自动 - > 强制 ON.
    auto [m1, c1] = app::update(std::move(m), Msg{ModelsToggleReasoning{}});
    apply_fx(c1);
    CHECK(agentty::reasoning_override_for("codestral-latest") == 1,
          "^E forces the override on");
    CHECK(agentty::resolved_caps("codestral-latest").supports_effort(),
          "effort capability now open for the model");
    CHECK(g_settings.reasoning_effort_overrides.count("mistral/codestral-latest") == 1,
          "override persisted to Settings, under this provider");
    CHECK(g_settings.reasoning_effort_overrides.at("mistral/codestral-latest"),
          "persisted value is ON");
    CHECK(g_settings.reasoning_effort_overrides.count("codestral-latest") == 0,
          "not as a bare id that would also flip other providers");
    CHECK(!m1.s.status.empty(), "a status toast is set as feedback");

    // 第二届 ^E: ON - > 强度 OFF.
    auto [m2, c2] = app::update(std::move(m1), Msg{ModelsToggleReasoning{}});
    apply_fx(c2);
    CHECK(agentty::reasoning_override_for("codestral-latest") == 0,
          "^E again forces the override off");
    CHECK(!agentty::resolved_caps("codestral-latest").supports_effort(),
          "effort suppressed under force-off");
    CHECK(!m2.s.status.empty(), "force-off also gives feedback");

    // 第三届 ^E: OFF - > 回到推论(已清除)。
    auto [m3, c3] = app::update(std::move(m2), Msg{ModelsToggleReasoning{}});
    apply_fx(c3);
    CHECK(agentty::reasoning_override_for("codestral-latest") == -1,
          "^E a third time clears the override (auto)");
    CHECK(g_settings.reasoning_effort_overrides.count("mistral/codestral-latest") == 0,
          "cleared override removed from Settings");
    CHECK(!m3.s.status.empty(), "clear-to-auto also gives feedback");

    agentty::clear_reasoning_overrides();   // 保持全球状态清洁
}

// ^E 时间: FAMILY-GATED 模型( 4) Claude )是无
// -op 用于覆盖但仍然提供反馈(提示),并且从不写覆盖。
TEST_CASE("model picker ^E on family-gated model is a hinted no-op") {
    namespace pick = agentty::ui::pick;
    install_stub_deps();
    g_settings = store::Settings{};
    agentty::clear_reasoning_overrides();

    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));
    Model m0 = seeded_model();
    m0.d.available_models = { mi("claude-opus-4-5", "anthropic") };
    m0.d.model_id = ModelId{"claude-opus-4-5"};
    auto [m, _open] = app::update(std::move(m0), Msg{OpenModels{}});
    apply_fx(_open);
    REQUIRE(!m.d.fused_rows.empty());
    // ^E 行为 HIGHLIGHTED 排队,种子 目录 携带 Anthropic
    // 捆绑的线条 -up ,所以第0行不一定是 opus——指向它。
    {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(m.d.fused_rows.size()); ++i)
            if (m.d.fused_rows[static_cast<std::size_t>(i)].model.id.value
                == "claude-opus-4-5") { idx = i; break; }
        REQUIRE(idx >= 0);
        if (auto* c = m.ui.panel.get<pn::Models>()) c->index = idx;
    }

    auto [m1, c1] = app::update(std::move(m), Msg{ModelsToggleReasoning{}});
    apply_fx(c1);
    CHECK(agentty::reasoning_override_for("claude-opus-4-5") == -1,
          "family-gated model gets no override");
    CHECK(g_settings.reasoning_effort_overrides.empty(),
          "nothing persisted for a family-gated model");
    CHECK(!m1.s.status.empty(), "still shows a hint toast");
    CHECK(agentty::resolved_caps("claude-opus-4-5").supports_effort(),
          "Claude keeps its own family-gated effort");

    agentty::clear_reasoning_overrides();
}

TEST_CASE("provider filter: auth-aware browse hides unconfigured") {
    namespace P = agentty::provider;
    const auto ps = P::providers();

    store::Settings st;
    agentty::auth::AuthView view;

    auto id_at = [&](int i) { return std::string{ps[static_cast<std::size_t>(i)].id}; };
    auto has = [&](const std::vector<int>& v, std::string_view id) {
        for (int i : v) if (id_at(i) == id) return true;
        return false;
    };

    // 无配置:浏览视图是 空 被扣的每行货款都按
    // UI 能够提供他们。 Local/no-auth
    // 后端总是可以使用的,所以可以合法地出现.
    {
        auto r = P::filter_provider_indices("", st, view);
        CHECK(!has(r.indices, "cerebras"), "unconfigured vendor is not browsed");
        CHECK(!has(r.indices, "groq"));
        CHECK(r.n_hidden > 0, "withheld rows are counted");
        CHECK(r.n_configured == static_cast<int>(r.indices.size()));
        CHECK(static_cast<std::size_t>(r.n_hidden) + r.indices.size()
                  == ps.size(), "every registry row is either shown or counted");
    }

    // 配置 : 它出现, 隐藏的计数 掉到 完全。
    {
        auto before = P::filter_provider_indices("", st, view);
        st.provider_keys["groq"] = "gsk_test";
        auto after = P::filter_provider_indices("", st, view);
        CHECK(has(after.indices, "groq"), "a keyed provider is mine");
        CHECK(after.n_hidden == before.n_hidden - 1);
        CHECK(!has(after.indices, "cerebras"), "others stay hidden");
    }

    // SEARCH 仍然到达整个 注册表 隐藏是浏览
    // -view 决断,永远不能实现。 -dead-end 合同。
    {
        auto r = P::filter_provider_indices("cerebras", st, view);
        CHECK(has(r.indices, "cerebras"), "unconfigured vendor still searchable");
        CHECK(r.n_hidden == 0, "search withholds nothing");
    }

    // 使用查询,配置的行将引导未配置的行.
    {
        store::Settings s2;
        s2.provider_keys["cerebras"] = "k";
        auto r = P::filter_provider_indices("e", s2, view);
        REQUIRE(r.n_configured >= 1);
        for (int i = 0; i < r.n_configured; ++i)
            CHECK(P::auth_source(ps[static_cast<std::size_t>(r.indices[static_cast<std::size_t>(i)])],
                                 s2, view) != P::AuthSource::None,
                  "the configured prefix really is configured");
    }
}

TEST_CASE("provider filter: fuzzy narrows + ranks") {
    namespace P = agentty::provider;

    // 空查询 = 每个提供者,按注册表顺序.
    {
        auto all = P::filter_provider_indices("");
        CHECK(all.size() == P::providers().size());
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            CHECK(all[static_cast<std::size_t>(i)] == i);
    }

    // 特定的ID漂浮在前面.
    {
        auto r = P::filter_provider_indices("kimi");
        CHECK(!r.empty());
        const auto ps = P::providers();
        CHECK(ps[static_cast<std::size_t>(r.front())].id == "kimi");
    }
    {
        auto r = P::filter_provider_indices("deepseek");
        CHECK(!r.empty());
        CHECK(P::providers()[static_cast<std::size_t>(r.front())].id == "deepseek");
    }

    // 一个标签单词即使不是id("grok" - > xai行)也匹配.
    {
        auto r = P::filter_provider_indices("grok");
        bool found_xai = false;
        for (int i : r) if (P::providers()[static_cast<std::size_t>(i)].id == "xai") found_xai = true;
        CHECK(found_xai);
    }

    // 胡言乱语根本没用
    CHECK(P::filter_provider_indices("zzqzzq").empty());
}

// canonical_spec 是那个 一个 自定义的正常化点 -host
// 身份: 身份: CLI (--provider 和 和 数据
// TUI 弹出面板 两种拼写都通过它,然后才能持续或比较。
// 已保存 主持人,两个 provider_keys 插槽和两个
// 选择面板 行——即虫子系列(# 8/#10/#11/#17/ 上面写着,
// 它没有直接的覆盖,这些是规则的定律。
TEST_CASE("canonical_spec: one endpoint has exactly one spelling") {
    namespace P = agentty::provider;

    // 白空间和一副尾翼的斜线是噪音。
    CHECK(P::canonical_spec("  yolo-auto.com  ") == "yolo-auto.com");
    CHECK(P::canonical_spec("yolo-auto.com/")    == "yolo-auto.com");
    CHECK(P::canonical_spec("\tlocalhost:8080\n") == "localhost:8080");

    // DNS 情况 -insensitive ,所以,请 HOST 归并.
    CHECK(P::canonical_spec("YOLO-AUTO.COM")  == "yolo-auto.com");
    CHECK(P::canonical_spec("LocalHost:8080") == "localhost:8080");
    CHECK(P::canonical_spec("HTTPS://API.OPENAI.COM") == "https://api.openai.com");

    // ..但是,我们... PATH。一个网关可以服务/ V1
    // 和/v1不同,所以折叠它会默默地拨错端点.
    CHECK(P::canonical_spec("HOST.COM/V1") == "host.com/V1");

    // ... 也一样 "#name" 账户标签,
    // 用户名 -chosen 文字和部分身份设计。
    CHECK(P::canonical_spec("HOST.COM#Work") == "host.com#Work");

    // 计划 "//" 一定要在刀刃下幸存下来 -stripping.
    CHECK(P::canonical_spec("https://host.com") == "https://host.com");
    CHECK(P::canonical_spec("https://host.com/") == "https://host.com");

    // 预设通过未受干扰(它们是ID,不是主机).
    CHECK(P::canonical_spec("anthropic") == "anthropic");
    CHECK(P::canonical_spec("ollama")    == "ollama");

    // IDEMPOTENT : 运行两次不能改变答案,
    // 或者每次绕过一次就可能漂移 -trips。通过设置。
    for (const char* s : {"  YOLO-AUTO.COM/  ", "https://Host.com/V1#A",
                          "localhost:11434", "kimi"}) {
        const std::string once  = P::canonical_spec(s);
        const std::string twice = P::canonical_spec(once);
        CHECK(once == twice);
    }
}

TEST_CASE("provider rows: one ordered list, saved hosts searchable") {
    namespace ui = agentty::ui;
    const std::vector<std::string> hosts = {"my-host.example:8443"};

    // 空查询:预设,然后是自定义主机,然后是哨兵LAST.
    {
        auto rows = ui::build_provider_rows(hosts, "");
        CHECK(!rows.empty());
        CHECK(rows.back().is_new_custom_host());
        bool saw_host = false, saw_preset = false;
        for (const auto& r : rows) {
            if (r.preset()) saw_preset = true;
            if (const auto* c = r.custom_host()) saw_host = *c == hosts[0];
        }
        CHECK(saw_preset);
        CHECK(saw_host);
    }

    // 由 a 过滤 PRESET 名称: 匹配预设 + 监视器。
    // object 已保存 主机不匹配 "kimi" 因此,
    // 它之所以缺席,是因为 已保存 主机在过滤时隐藏。
    {
        auto rows = ui::build_provider_rows(hosts, "kimi");
        CHECK(rows.size() >= 2);          // ++1 预设 + 哨兵
        CHECK(rows.back().is_new_custom_host());
        CHECK(rows.front().preset() != nullptr);
        CHECK(rows.front().preset()->id == "kimi");
        for (const auto& r : rows)
            CHECK(r.custom_host() == nullptr);   // "kimi" 与主机不符
    }

    // A SAVED HOST IS SEARCHABLE 它曾经
    // 消失的那一刻 带类型的 所以回到你已经拥有的宿主身边的唯一
    // 办法 已保存 是要滚动过去每个预设。 已保存 主机是提供者;
    // 属于提供者搜索空间.
    {
        auto rows = ui::build_provider_rows(hosts, "my-host");
        bool found = false;
        for (const auto& r : rows)
            if (const auto* c = r.custom_host()) found = (*c == hosts[0]);
        CHECK(found);
    }

    // 没有预设匹配, 查询不是 结束点 -shaped :
    // 仍然完全就是哨兵,所以逃生舱门总是可以到达的.
    {
        auto rows = ui::build_provider_rows(hosts, "zzqzzq");
        CHECK(rows.size() == 1);
        CHECK(rows.front().is_new_custom_host());
        CHECK(rows.front().new_host_prefill() == nullptr);   // 未晋升
    }
}

// 结束点 -shaped 查询 PROMOTES 习惯 -host 排到顶部和前列
// -fills 和原本一样 带类型的.
//
// 原因 此 IS A TEST 和 不 A TWEAK:
// agentty 总是跟任何人说话 OpenAI-compatible
// 主持人,但 选择面板 输入主机名后, 每个熟悉的提供者都消失了,
// 留下一条哑巴的灰色行, 上面写着 "no"。该 UI 空白导致拉动请求,
// 添加一个 注册表 已处理的通用路径服务行。 这些声称将固定 :
// 答案是,我们没有 首次 ,这是 CONCRETE ,它携带 带类型的 文本向前移动。
TEST_CASE("provider rows: an endpoint-shaped query offers the custom host first") {
    namespace ui = agentty::ui;
    const std::vector<std::string> none;

    // 何谓终点-shaped.
    CHECK(ui::query_looks_like_host("yolo-auto.com"));
    CHECK(ui::query_looks_like_host("localhost:8080"));
    CHECK(ui::query_looks_like_host("https://gw.internal/api"));
    CHECK(ui::query_looks_like_host("192.168.1.9:1234"));
    CHECK(ui::query_looks_like_host("api.openai.com/v1"));
    CHECK(ui::query_looks_like_host("host.local:11434/v1"));
    CHECK(ui::query_looks_like_host("yolo-auto.com/v1#work"));   // # 账户标签
    CHECK(ui::query_looks_like_host("YOLO-AUTO.COM"));           // DNS 情况 -insensitive
    CHECK(ui::query_looks_like_host("  yolo-auto.com  "));       // 粘贴白空间
    CHECK(ui::query_looks_like_host("[::1]:8080"));              // 括号中的IPv6

    // 绝对不能误认供应商的名字,
    // 否则要价行会把真正的供应商推到每次搜索中。
    CHECK(!ui::query_looks_like_host("kimi"));
    CHECK(!ui::query_looks_like_host("openrouter"));
    CHECK(!ui::query_looks_like_host("gpt"));
    CHECK(!ui::query_looks_like_host(""));
    CHECK(!ui::query_looks_like_host("my server"));   // 空格:是传言

    // MODEL IDS ARE 该 DANGEROUS CLASS。
    // 人们在打猎时会不断地把它们输入提供者的盒子,有几个则包含一个点
    // 或一个破折号。如果这些内容中有任何一个读作主机,那么当用户试图
    // 找到一个时,要约行就将真正的提供者替换。
    CHECK(!ui::query_looks_like_host("gpt-4o"));
    CHECK(!ui::query_looks_like_host("gpt-4.1"));
    CHECK(!ui::query_looks_like_host("qwen3.8-flash"));    // a 真实 型号 id
    CHECK(!ui::query_looks_like_host("claude-opus-4-5"));
    CHECK(!ui::query_looks_like_host("v1.5"));
    CHECK(!ui::query_looks_like_host("3.5"));
    CHECK(!ui::query_looks_like_host("a.b"));             // 实在太短了

    // 晋升: 首次 排,带 带类型的 文本,而后面的重复的哨兵
    // 已经消失(一个提议,而不是两个).
    {
        auto rows = ui::build_provider_rows(none, "yolo-auto.com");
        REQUIRE(!rows.empty());
        CHECK(rows.front().is_new_custom_host());
        REQUIRE(rows.front().new_host_prefill() != nullptr);
        CHECK(*rows.front().new_host_prefill() == "yolo-auto.com");
        int sentinels = 0;
        for (const auto& r : rows) sentinels += r.is_new_custom_host() ? 1 : 0;
        CHECK(sentinels == 1);
    }

    // 寄件者: 端口查询仍然将报价排在首位, 尽管预设可能模糊
    // -match位数。
    {
        auto rows = ui::build_provider_rows(none, "localhost:11434");
        REQUIRE(!rows.empty());
        REQUIRE(rows.front().new_host_prefill() != nullptr);
        CHECK(*rows.front().new_host_prefill() == "localhost:11434");
    }

    // ALREADY SAVED : 不主动重新 -add 已经是
    // 自己一行的宿主, 更往下。 一个终点的两行是纸板切除器,
    // 使 选择面板 看上去很破烂。
    {
        const std::vector<std::string> saved = {"my-host.example:8443"};
        auto rows = ui::build_provider_rows(saved, "my-host.example:8443");
        CHECK(rows.front().new_host_prefill() == nullptr);
        bool saw_saved = false;
        for (const auto& r : rows)
            if (const auto* c = r.custom_host()) saw_saved = (*c == saved[0]);
        CHECK(saw_saved);
    }

    // 该 OFFER IS CANONICAL。该行显示的是 被坚持的,
    // 所以一个混乱的贴纸不能为您已经拥有的宿主创造第二个身份。
    // 没有这个,"  YOLO-AUTO.COM/  "和"yolo-auto.com"
    // 成为两个已保存主机,有两个独立的密钥插槽——用户将一个密钥粘贴在其中一个,
    // 另一个仍表示"no key".
    {
        auto rows = ui::build_provider_rows(none, "  YOLO-AUTO.COM/  ");
        REQUIRE(!rows.empty());
        REQUIRE(rows.front().new_host_prefill() != nullptr);
        CHECK(*rows.front().new_host_prefill() == "yolo-auto.com");
    }
    // 和脱衣舞步对抗 已保存 主机使用同样的犬形,
    // 所以不同 -spelled 查询仍然识别您已经拥有的主机。
    {
        const std::vector<std::string> saved = {"yolo-auto.com"};
        auto rows = ui::build_provider_rows(saved, "YOLO-AUTO.COM");
        CHECK(rows.front().new_host_prefill() == nullptr);   // 无重复报价
    }
}

// 由第REAL 状态更新函数号驱动的横跨-provider 选择面板号:
// 打开种子 目录 + 火灾获取; 相同 -provider 选择修改当前模式并
// 记录 MRU; a FusedCatalogLoaded 由 id 合并.
TEST_CASE("fused picker open, merge, same-provider switch, MRU") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    // 名词: 不要依赖真实 -disk Anthropic 证明(未出席) CI).
    // A provider_keys 输入 provider_is_authed("anthropic" ) 真实的。
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic"),
                            mi("claude-opus-4", "anthropic")};

    // 打开:选择面板打开,活动提供者的目录从available_models
    // (Ready)种子,其他获得授权的提供者得到loading + a figure.
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    CHECK(m1.ui.panel.is<pn::Models>());
    bool anthropic_seeded = false;
    for (const auto& c : m1.d.provider_catalogs)
        if (c.provider_id == "anthropic") {
            anthropic_seeded = (c.state == ProviderCatalog::State::Ready
                                && c.models.size() == 2);
        }
    CHECK(anthropic_seeded);

    // 保险线包括两条 Anthropic 模型(主动封钉先行).
    auto rows = app::detail::fused_rows_for_model(m1);
    CHECK(rows.size() >= 2);
    CHECK(rows[0].active);
    CHECK(rows[0].model.id.value == "claude-sonnet-4-6");

    // 移动到 opus 列并选择: 相同的-provider 型号更改, 不跳.
    // 在保险名单上找到Opus的索引。
    int opus_idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<std::size_t>(i)].model.id.value == "claude-opus-4") {
            opus_idx = i; break;
        }
    REQUIRE(opus_idx >= 0);
    if (auto* c = m1.ui.panel.get<pn::Models>()) c->index = opus_idx;

    auto [m2, c2] = app::update(std::move(m1), Msg{ModelsSelect{}});
    apply_fx(c2);
    CHECK(m2.d.model_id.value == "claude-opus-4");
    CHECK(!m2.ui.panel.is<pn::Models>());       // 选择面板 关闭
    // MRU记录了开关(front =刚刚选中的模型).
    REQUIRE(!m2.d.recent_models.empty());
    CHECK(m2.d.recent_models.front().provider_id == "anthropic");
    CHECK(m2.d.recent_models.front().model_id == "claude-opus-4");
    // 被困在设置中。
    CHECK(!g_settings.recent_models.empty());
}

// A FusedCatalogLoaded 用于将提供者合并到 provider_catalogs
// 以 id 和 翻转 状态准备。
TEST_CASE("fused catalog loaded merges by provider id") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";  // 母体亲和
    g_settings.provider_keys["openai"] = "sk-test";   // oFC 认证 − 目录
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    // 模拟Openai的目录解析.
    FusedCatalogLoaded loaded;
    loaded.provider_id = "openai";
    loaded.models = {mi("gpt-5-codex", "openai"), mi("gpt-4o", "openai")};
    loaded.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(loaded)});
    apply_fx(c2);

    bool openai_ready = false;
    for (const auto& c : m2.d.provider_catalogs)
        if (c.provider_id == "openai")
            openai_ready = (c.state == ProviderCatalog::State::Ready
                            && c.models.size() == 2);
    CHECK(openai_ready);
}

// 保险丝 选择面板 缓存 其行数( m.d.fused_rows 低价(每美元) -frame
// /每笔 -keystroke 渲染 : 露天居住,清空 close/select.
TEST_CASE("fused picker caches rows and clears them on close") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    g_settings.provider_keys["xai"]       = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic")};

    // 打开是INSTANT:只有活动提供者(种子来自available_models)有模型;
    // 其他所有授权提供者都是空的+装入和数据流通过ASync抓取。
    // 缓存在打开时准确地反映了这个模型。
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    CHECK(!m1.d.fused_rows.empty());                 // 正在显示提供者
    bool anthropic_ready = false, xai_pending = false;
    for (const auto& c : m1.d.provider_catalogs) {
        if (c.provider_id == "anthropic")
            anthropic_ready = (c.state == ProviderCatalog::State::Ready
                               && !c.models.empty());
        if (c.provider_id == "xai")
            xai_pending = c.models.empty();          // 未同步种子
    }
    CHECK(anthropic_ready);
    CHECK(xai_pending);

    // 斋的 目录 数据流 重建以包含它。
    FusedCatalogLoaded xai;
    xai.provider_id = "xai";
    xai.models = {mi("grok-4", "xai"), mi("grok-3", "xai")};
    xai.ok = true;
    auto [m1b, c1b] = app::update(std::move(m1), Msg{std::move(xai)});
    apply_fx(c1b);
    bool xai_now = false;
    for (const auto& c : m1b.d.provider_catalogs)
        if (c.provider_id == "xai") xai_now = (c.models.size() == 2);
    CHECK(xai_now);

    // 过滤重建已设置的 缓存( 已夹住, 仍开放)。
    auto [m2, c2] = app::update(std::move(m1b),
                                Msg{ModelsFilterInput{U'c'}});
    CHECK(m2.ui.panel.is<pn::Models>());

    // 关闭释放 缓存.
    auto [m3, c3] = app::update(std::move(m2), Msg{CloseModels{}});
    apply_fx(c3);
    CHECK(!m3.ui.panel.is<pn::Models>());
    CHECK(m3.d.fused_rows.empty());
}

// 数字是普通的过滤器输入到已连接列表中——没有快速的数字
// -select 键入 "3" 搜索 "3" 永远不要跳到第三排
TEST_CASE("fused picker digits type into the filter") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    g_settings.provider_keys["xai"]       = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    FusedCatalogLoaded xai;
    xai.provider_id = "xai";
    xai.models = {mi("grok-4", "xai"), mi("grok-3", "xai")};
    xai.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(xai)});
    apply_fx(c2);
    REQUIRE(m2.d.fused_rows.size() >= 3);

    // '3' 空查询输入查询——它确实 不 跳到一排。
    auto [m3, c3] = app::update(std::move(m2), Msg{ModelsFilterInput{U'3'}});
    apply_fx(c3);
    const auto* c = m3.ui.panel.get<pn::Models>();
    REQUIRE(c != nullptr);
    CHECK(c->query == "3");           // 数字进入查询

    // 数字像其它搜索文本一样继续附加(因此"g3"仍然有效).
    auto [m4, c4] = app::update(std::move(m3), Msg{ModelsFilterInput{U'g'}});
    apply_fx(c4);
    const auto* c2p = m4.ui.panel.get<pn::Models>();
    REQUIRE(c2p != nullptr);
    CHECK(c2p->query == "3g");        // 后面的两个字符顺序
}

// 过滤后的引信列表携带每-row模糊-match字节抵消入型号为NAME,
// 因此视图可以突出显示每行匹配的WHY(fzf-style).
TEST_CASE("fused rows expose name match positions for highlight") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic"),
                            mi("claude-opus-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    // 无查询 – 没有突出显示偏移。
    for (const auto& r : m1.d.fused_rows)
        CHECK(r.match_positions.empty());

    // "son" − 音素列增益比对抵消为它的名称.
    Model m2 = std::move(m1);
    for (char ch : std::string{"son"}) {
        auto [n, c] = app::update(std::move(m2),
                                  Msg{ModelsFilterInput{static_cast<char32_t>(ch)}});
        m2 = std::move(n);
    }
    bool sonnet_has_hl = false;
    for (const auto& r : m2.d.fused_rows)
        if (r.model.id.value == "claude-sonnet-4-5" && !r.match_positions.empty())
            sonnet_has_hl = true;
    CHECK(sonnet_has_hl);
}

// ^Tab 走路,走着 整个 MRU 环(A_B_C_A),
// 逐渐老化,不重排环形,没有一个A_B 切换.
TEST_CASE("^Tab cycles the MRU ring without reordering") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-a"};
    m.d.available_models = {mi("claude-a", "anthropic"),
                            mi("claude-b", "anthropic"),
                            mi("claude-c", "anthropic")};
    // MRU环:活跃在前方,然后逐渐变老.
    m.d.recent_models = {ModelRef{"anthropic", "claude-a"},
                         ModelRef{"anthropic", "claude-b"},
                         ModelRef{"anthropic", "claude-c"}};

    auto step = [](Model mm) {
        auto [n, c] = app::update(std::move(mm), Msg{SwitchToPreviousModel{}});
        apply_fx(c);
        return std::move(n);
    };

    m = step(std::move(m));
    CHECK(m.d.model_id.value == "claude-b");            // A → B
    m = step(std::move(m));
    CHECK(m.d.model_id.value == "claude-c");            // B → C
    m = step(std::move(m));
    CHECK(m.d.model_id.value == "claude-a");            // 中 + A(包装)

    // 戒指本身从未重新订购——这就是使行走稳定的原因.
    REQUIRE(m.d.recent_models.size() == 3);
    CHECK(m.d.recent_models[0].model_id == "claude-a");
    CHECK(m.d.recent_models[1].model_id == "claude-b");
    CHECK(m.d.recent_models[2].model_id == "claude-c");
}

// 旋转时不能移动这个转弯:它的工具 -loop 续
// 续 重试 内容 -read 每一个请求的模型,
// 所以切换一次将半转转到一个模型,半转到另一个模型。选择等待,
// 然后在转弯结束的时刻着陆,不管它结束什么。
TEST_CASE("a model picked during a turn applies when the turn ends") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-a"};
    m.d.available_models = {mi("claude-a", "anthropic"), mi("claude-b", "anthropic")};
    m.d.recent_models = {ModelRef{"anthropic", "claude-a"}, ModelRef{"anthropic", "claude-b"}};
    m.s.phase = phase::Streaming{};   // 转弯正在运行

    auto [m1, c1] = app::update(std::move(m), Msg{SwitchToPreviousModel{}});
    apply_fx(c1);
    CHECK(m1.d.model_id.value == "claude-a", "the running turn keeps its model");
    REQUIRE(m1.ui.pending_switch.has_value());
    CHECK(m1.ui.pending_switch->model_id == "claude-b", "the pick is held");
    CHECK(m1.s.status.find("next turn") != std::string::npos, "and the user is told when");

    // 任意 归并 轮到摇摆时,它就会被应用
    m1.s.phase = phase::Idle{};
    auto [m2, c2] = app::update(std::move(m1), Msg{Tick{}});
    apply_fx(c2);
    CHECK(m2.d.model_id.value == "claude-b", "applied once the turn ended");
    CHECK(!m2.ui.pending_switch.has_value(), "and cleared");
}

TEST_CASE("picking the running model mid-turn cancels a held pick") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-a"};   // 运行转弯的模型
    m.d.available_models = {mi("claude-a", "anthropic"), mi("claude-b", "anthropic")};
    m.d.recent_models = {ModelRef{"anthropic", "claude-a"}, ModelRef{"anthropic", "claude-b"}};
    m.s.phase = phase::Streaming{};

    // 选 B 中 -turn 中点,然后选择一个(运行中的模型) -turn.
    auto [m1, c1] = app::update(std::move(m), Msg{SwitchToPreviousModel{}});
    apply_fx(c1);
    REQUIRE(m1.ui.pending_switch.has_value());
    auto [mo, co] = app::update(std::move(m1), Msg{OpenModels{}});
    apply_fx(co);
    int a_row = -1;
    for (int i = 0; i < static_cast<int>(mo.d.fused_rows.size()); ++i)
        if (mo.d.fused_rows[static_cast<std::size_t>(i)].model.id.value == "claude-a") a_row = i;
    REQUIRE(a_row >= 0);
    mo.ui.panel.get<pn::Models>()->index = a_row;
    auto [m2, c2] = app::update(std::move(mo), Msg{ModelsSelect{}});
    apply_fx(c2);
    CHECK(!m2.ui.pending_switch.has_value(), "choosing the running model drops the held pick");

    m2.s.phase = phase::Idle{};
    auto [m3, c3] = app::update(std::move(m2), Msg{Tick{}});
    CHECK(m3.d.model_id.value == "claude-a", "nothing changes when the turn ends");
}

// 在CLASSIC型选择面板型中选择一个模型,输入MRU环,因此^Tab有一个以上
// 的循环条目(所报告的"recents only has one model, ^Tab does nothing"
// bug——以前只记录了被保险的选择面板).
TEST_CASE("classic model picker feeds the MRU ring") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-a"};
    m.d.available_models = {mi("claude-a", "anthropic"),
                            mi("claude-b", "anthropic")};

    // 打开经典 选择面板 ,移动到 Claude 中 -b ,选择它。
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    if (auto* p = m1.ui.panel.get<pn::Models>()) p->index = 1;
    auto [m2, c2] = app::update(std::move(m1), Msg{ModelsSelect{}});
    apply_fx(c2);
    CHECK(m2.d.model_id.value == "claude-b");
    // 采摘者落在环(前方=新).
    REQUIRE(!m2.d.recent_models.empty());
    CHECK(m2.d.recent_models.front().model_id == "claude-b");

    // 现在^Tab有2个条目进行循环——它从活动模型中移走.
    auto [m3, c3] = app::update(std::move(m2), Msg{SwitchToPreviousModel{}});
    apply_fx(c3);
    CHECK(m3.d.model_id.value != "claude-b");   // 已循环,而非第-op号
}

// 单项捆绑 目录 谓 该 每个供应商的离线地板——一个来源,
// 不 -site 流着, 卫生 -check 其形状+ Anthropic
// 旗舰(Fable)及其[1m]同伴在场.
TEST_CASE("bundled catalog is the single provider floor") {
    using agentty::catalog::bundled;

    const auto anth = bundled("anthropic");
    bool fable = false, fable_1m = false, opus = false;
    for (const auto& mi : anth) {
        if (mi.id.value == "claude-fable-5")     fable = true;
        if (mi.id.value == "claude-fable-5[1m]") fable_1m = true;
        if (mi.id.value == "claude-opus-4-5")    opus = true;
        CHECK(mi.provider == "anthropic");
    }
    CHECK(fable);        // 旗舰 保险丝 选择面板 丢失
    CHECK(fable_1m);     // add_1m_variants 伴奏( 双关)
    CHECK(opus);

    // 其他提供者通过SAME函数解决(没有单独的表格).
    CHECK(!bundled("xai").empty());
    CHECK(!bundled("chatgpt").empty());
    CHECK(!bundled("copilot").empty());
    CHECK(!bundled("kimi").empty());
    // 未知的/用户-definedXX 目录没有地板.
    CHECK(bundled("openrouter").empty());
    CHECK(bundled("some-custom-host").empty());
}

// 活动提供者的保险丝 目录 MIRRORS available_models
// 每一个打开,所以一个模型出现在 available_models
// 以后( e.g 活的 / 活的 v1/models 第一次公开播种后,
// 将新的旗舰移到陆地上。 选择面板 - 决不能冻结第一个 快照.
TEST_CASE("fused active catalog re-seeds when available_models grows") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-opus-4-5"};
    m.d.available_models = {mi("claude-opus-4-5", "anthropic"),
                            mi("claude-sonnet-4-5", "anthropic")};

    // 熔化的第一粒开放种子 目录 从当前( 有线) -less ) 列表(英文).
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    auto [m2, c2] = app::update(std::move(m1), Msg{CloseModels{}});
    apply_fx(c2);

    // 现场收买新货 -listed 旗舰进入 available_models.
    m2.d.available_models.push_back(mi("claude-fable-5", "anthropic"));

    // 调 -open : 活动 目录 必须 MIRROR 成年人 available_models.
    auto [m3, c3] = app::update(std::move(m2), Msg{OpenModels{}});
    apply_fx(c3);
    bool fable_listed = false;
    for (const auto& cat : m3.d.provider_catalogs)
        if (cat.provider_id == "anthropic")
            for (const auto& mo : cat.models)
                if (mo.id.value == "claude-fable-5") fable_listed = true;
    CHECK(fable_listed);

    bool fable_row = false;
    for (const auto& r : m3.d.fused_rows)
        if (r.model.id.value == "claude-fable-5") fable_row = true;
    CHECK(fable_row);
}

// 现场直播 目录 着陆( 4) ModelsLoaded ) 引信时 选择面板 谓
// OPEN 需要刷新其行的位置——活动提供者的模型通过 ModelsLoaded
// (无) FusedCatalogLoaded ),以及 选择面板 显示一个 过期 快照
// 直到重新打开。 "fused pane doesn't update" 虫子,你这混蛋
TEST_CASE("ModelsLoaded refreshes the open fused picker") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-opus-4-5"};
    m.d.available_models = {mi("claude-opus-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    // 更新的模型没有在公开 选择面板 还没呢
    bool before = false;
    for (const auto& r : m1.d.fused_rows)
        if (r.model.id.value == "claude-fable-5") before = true;
    CHECK(!before);

    // 活跃的供应商的直播/v1/models 找一个新的旗舰。
    ModelsLoaded ml;
    ml.provider_id = "anthropic";
    ml.models = {mi("claude-opus-4-5", "anthropic"),
                 mi("claude-fable-5", "anthropic")};
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(ml)});
    apply_fx(c2);

    // 电话 OPEN 选择面板 排行刷新到原位——不需要重新开放.
    CHECK(m2.ui.panel.get<pn::Models>());
    bool after = false;
    for (const auto& r : m2.d.fused_rows)
        if (r.model.id.value == "claude-fable-5") after = true;
    CHECK(after);
}

// 哈拉诺的bug:一个自定义的主持人,它的 / v1/models
// 离开401号 Anthropic 发射种子 选择面板 并挑选一个
// 送来的克劳德 -opus-4-5 无法获取的要清除列表并说明原因。
TEST_CASE("ModelsLoaded failure clears another provider's list and keeps the reason") {
    using namespace agentty::msg;
    install_stub_deps();
    const std::string host = "https://backend.example.com/v1";
    g_settings = store::Settings{};
    g_settings.provider_keys[host] = "sk-wrong";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection(host));

    Model m = seeded_model();
    m.d.available_models = {mi("claude-opus-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    ModelsLoaded ml;
    ml.provider_id = host;
    ml.error = "HTTP 401: Invalid API key";
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(ml)});
    apply_fx(c2);

    CHECK(m2.d.available_models.empty());
    for (const auto& r : m2.d.fused_rows)
        CHECK(r.model.id.value != "claude-opus-4-5");
    CHECK(m2.s.models_error == "HTTP 401: Invalid API key");
    bool shown = false;
    for (const auto& c : m2.d.provider_catalogs)
        if (c.provider_id == host && c.error == "HTTP 401: Invalid API key") shown = true;
    CHECK(shown);

    // 固定键的获取可以清除错误。
    ModelsLoaded ok;
    ok.provider_id = host;
    ok.models = {mi("Qwen3-32B", host.c_str())};
    auto [m3, c3] = app::update(std::move(m2), Msg{std::move(ok)});
    apply_fx(c3);
    CHECK(m3.s.models_error.empty());
    for (const auto& c : m3.d.provider_catalogs)
        if (c.provider_id == host) CHECK(c.error.empty());
}

// 发送信件时 Anthropic 信号探测器被放出来 "1 queued"
// 永远当探测器发现没有什么新鲜的。
TEST_CASE("a message queued behind the OAuth probe is sent when it settles") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));
    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-opus-4-5"};
    m.s.oauth_refresh_in_flight = true;
    m.ui.composer.text = "hi";
    m.ui.composer.cursor = 2;
    auto [m1, c1] = app::update(std::move(m), Msg{ComposerEnter{}});
    apply_fx(c1);
    REQUIRE(m1.ui.composer.queued.size() == 1);
    CHECK(m1.s.is_idle());

    auto [m2, c2] = app::update(std::move(m1),
                                Msg{msg::LoginMsg{OAuthRefreshNotDue{}}});
    apply_fx(c2);
    CHECK(m2.ui.composer.queued.empty());
    CHECK(!m2.s.is_idle());   // 转弯开始
}

// 电话 传输层 学习拒绝的主机字段(关闭) -thread )和重发。
// 当转弯结束,或者下一次发射再次支付时,记录必须收回。
TEST_CASE("a host rejection learned by the transport is saved when the turn ends") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));
    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-opus-4-5"};
    for (int i = 0; i < 3; ++i) {
        Message u; u.role = Role::User; u.text = "question " + std::to_string(i);
        m.d.current.messages.push_back(u);
        Message a; a.role = Role::Assistant; a.text = "answer";
        m.d.current.messages.push_back(a);
    }
    auto [m1, c1] = app::update(std::move(m), Msg{CompactContext{}});
    REQUIRE(m1.s.compacting);

    agentty::set_rejected_fields({});
    agentty::add_rejected_field("old-vllm.example:8000",
        agentty::provider::bit(agentty::provider::OptionalField::EffortNone));
    m1.ui.composer.text.clear();
    auto [m2, c2] = app::update(std::move(m1), Msg{StreamTextDelta{"<summary>s</summary>"}});
    auto [m3, c3] = app::update(std::move(m2), Msg{StreamFinished{StopReason::EndTurn}});
    CHECK(m3.d.persisted.rejected_fields["old-vllm.example:8000"]
          == agentty::provider::bit(agentty::provider::OptionalField::EffortNone));
    agentty::set_rejected_fields({});
}


TEST_CASE("same model id on two providers: a reasoning override is per provider") {
    agentty::clear_reasoning_overrides();
    agentty::set_reasoning_override("groq/acme-r1-test", true);
    CHECK(agentty::reasoning_override_for("acme-r1-test", "groq") == 1);
    CHECK(agentty::reasoning_override_for("acme-r1-test", "openrouter") == -1);
    // 旧建筑的全重覆覆二十万二十(已保存)仍然适用于各地。
    agentty::set_reasoning_override("acme-bare-test", false);
    CHECK(agentty::reasoning_override_for("acme-bare-test", "groq") == 0);
    CHECK(agentty::reasoning_override_for("acme-bare-test", "openrouter") == 0);
    agentty::clear_reasoning_overrides();
}

// 两个供应商都提供相同的型号代号, 都公开 选择面板. ^F 财务报告和
// 已审计财务报表 ^E 排行必须留下另一个供应商的复制件 完全如原样。
TEST_CASE("same model id on two providers: ^F and ^E touch only the highlighted one") {
    using namespace agentty::msg;
    install_stub_deps();
    agentty::clear_reasoning_overrides();
    g_settings = store::Settings{};
    g_settings.provider_keys["groq"] = "sk-a";
    g_settings.provider_keys["openrouter"] = "sk-b";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("groq"));
    Model m0 = seeded_model();
    m0.d.available_models = {mi("acme-twin-model", "groq")};
    m0.d.model_id = ModelId{"acme-twin-model"};
    auto [m, c] = app::update(std::move(m0), Msg{OpenModels{}});
    apply_fx(c);
    // 给OpenRouter同样的id.
    FusedCatalogLoaded fl;
    fl.provider_id = "openrouter";
    fl.models = {mi("acme-twin-model", "openrouter")};
    auto [m1, c1] = app::update(std::move(m), Msg{std::move(fl)});
    apply_fx(c1);

    auto row_of = [](const Model& mm, const char* pid) {
        for (std::size_t i = 0; i < mm.d.fused_rows.size(); ++i)
            if (mm.d.fused_rows[i].provider_id == pid
                && mm.d.fused_rows[i].model.id.value == "acme-twin-model")
                return static_cast<int>(i);
        return -1;
    };
    REQUIRE(row_of(m1, "groq") >= 0);
    REQUIRE(row_of(m1, "openrouter") >= 0);

    // 突出OpenRouter的复制品和明星+在上面的力推理.
    m1.ui.panel.get<pn::Models>()->index = row_of(m1, "openrouter");
    auto [m2, c2] = app::update(std::move(m1), Msg{ModelsToggleFavorite{}});
    apply_fx(c2);
    m2.ui.panel.get<pn::Models>()->index = row_of(m2, "openrouter");
    auto [m3, c3] = app::update(std::move(m2), Msg{ModelsToggleReasoning{}});
    apply_fx(c3);

    for (const auto& r : m3.d.fused_rows)
        if (r.model.id.value == "acme-twin-model")
            CHECK_MESSAGE(r.model.favorite == (r.provider_id == "openrouter"),
                          "star on " << r.provider_id);
    CHECK(agentty::reasoning_override_for("acme-twin-model", "openrouter") == 1);
    CHECK(agentty::reasoning_override_for("acme-twin-model", "groq") == -1);

    // Enter 打开 OpenRouter 复制开关到 OpenRouter
    // 而不是格罗克的同名模特
    m3.ui.panel.get<pn::Models>()->index = row_of(m3, "openrouter");
    auto [m4, c4] = app::update(std::move(m3), Msg{ModelsSelect{}});
    apply_fx(c4);
    CHECK(app::detail::active_provider_id(m4) == "openrouter");
    CHECK(m4.d.model_id.value == "acme-twin-model");
    agentty::clear_reasoning_overrides();
}

// 将模型列表失败的提供者留下, 绝不能将错误带入下
// 一个列表。
TEST_CASE("provider switch drops the old provider's fetch error") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["https://bad.example.com/v1"] = "sk-wrong";
    g_settings.provider_keys["groq"] = "sk-a";
    provider::select(::agentty::IoAccess::grant(),
                     provider::parse_selection("https://bad.example.com/v1"));
    Model m = seeded_model();
    m.s.models_error = "HTTP 401: Invalid API key";
    (void)app::detail::commit_provider_switch(m, "groq", "Groq");
    CHECK(m.s.models_error.empty());
}

// 手写到设置中的自定义主机( 从未通过 ^P )没有
// 已保存 条目,所以它读作签名 并错过了 选择面板.
TEST_CASE("a settings-only custom host gets a saved row once it answers") {
    using namespace agentty::msg;
    install_stub_deps();
    const std::string host = "http://127.0.0.1:18461/v1";
    g_settings = store::Settings{};
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection(host));
    Model m = seeded_model();
    REQUIRE(!m.d.persisted.provider_keys.contains(host));
    ModelsLoaded ml;
    ml.provider_id = host;
    ml.models = {mi("twin-model", host.c_str())};
    auto [m1, c1] = app::update(std::move(m), Msg{std::move(ml)});
    apply_fx(c1);
    CHECK(m1.d.persisted.provider_keys.contains(host));
    CHECK(agentty::provider::signed_in(host, m1.d.persisted, m1.d.auth));
}

TEST_CASE("no model yet: the default pick is a usable chat model, not the first row") {
    using namespace agentty::msg;
    install_stub_deps();
    const std::string host = "https://backend.example.com/v1";
    g_settings = store::Settings{};
    g_settings.provider_keys[host] = "sk-good";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection(host));
    Model m = seeded_model();
    m.d.model_id = ModelId{};

    ModelsLoaded ml;
    ml.provider_id = host;
    auto row = [&](const char* id, int ctx) {
        ModelInfo x = mi(id, host.c_str());
        x.context_window = ctx;
        return x;
    };
    // SovereignEG的真令:先排序4k玩具模型和嵌入器.
    ml.models = {row("Qwen/Qwen3-0.6B", 4096), row("all-MiniLM-L6-v2", 512),
                 row("BAAI/bge-reranker-base", 512),
                 row("SovereignEG/Qwen3.8-27B-FP8", 131072)};
    auto [m1, c1] = app::update(std::move(m), Msg{std::move(ml)});
    apply_fx(c1);
    CHECK(m1.d.model_id.value == "SovereignEG/Qwen3.8-27B-FP8");
}

TEST_CASE("sending with no model after a failed list says why") {
    using namespace agentty::msg;
    install_stub_deps();
    const std::string host = "https://backend.example.com/v1";
    g_settings = store::Settings{};
    g_settings.provider_keys[host] = "sk-wrong";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection(host));
    Model m = seeded_model();
    m.d.model_id = ModelId{};
    m.s.models_error = "HTTP 401: Invalid API key";
    m.ui.composer.text = "Hi";
    m.ui.composer.cursor = 2;
    auto [m1, c1] = app::update(std::move(m), Msg{ComposerEnter{}});
    CHECK(m1.s.status.find("HTTP 401: Invalid API key") != std::string::npos);
    CHECK(m1.ui.composer.text == "Hi");   // 什么都没有丢失
    CHECK(m1.s.is_idle());
}

// OpenModels 刷新该 活动 提供者立即推迟 OTHER
// 提供者( FusedRefreshOthers ) 因此,
// 活动结果不会排在较慢的提供者后面。延迟的通过标志着其他的加载。
TEST_CASE("fused open prioritizes active provider, defers others") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";
    g_settings.provider_keys["openai"]    = "sk-o";   // 第二个被认证的提供者
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    // 打开非 -active 提供者为 不 却在装入( 延迟)。
    for (const auto& c : m1.d.provider_catalogs)
        if (c.provider_id == "openai")
            CHECK(c.state != ProviderCatalog::State::Loading);

    // 延时波标志着OTHER供应商加载,而不是活动波.
    auto [m2, c2] = app::update(std::move(m1), Msg{FusedRefreshOthers{}});
    apply_fx(c2);
    for (const auto& c : m2.d.provider_catalogs) {
        if (c.provider_id == "openai")
            CHECK(c.state == ProviderCatalog::State::Loading);
    }
}

// 保险单上留下 当前 : 提供者 目录 年龄大于 TTL (或失败)在下次打开
// 时被重新调试; 重新调试 -loaded 一个人一个人 如此迅速 -opens 别
// 乱来 -hammer 提供者。这是 "always updated but fast"
// 合同。
TEST_CASE("fused refetches stale/failed catalogs, skips fresh") {
    using namespace agentty::msg;
    maya::testing::freeze_anim_clock(1'000'000);   // 无 -zero 基数(0=) "never loaded")
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";
    g_settings.provider_keys["openai"]    = "sk-o";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    // openai 加载 现时已存在(t=0).
    FusedCatalogLoaded oa; oa.provider_id = "openai";
    oa.models = {mi("gpt-5", "openai")}; oa.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(oa)});
    apply_fx(c2);

    auto openai_state = [](const Model& mm) {
        for (const auto& c : mm.d.provider_catalogs)
            if (c.provider_id == "openai") return c.state;
        return ProviderCatalog::State::Idle;
    };
    CHECK(openai_state(m2) == ProviderCatalog::State::Ready);

    // FRESH (刚刚) 已加载 : 推迟波不得 调换一下
    auto [m3, c3] = app::update(std::move(m2), Msg{FusedRefreshOthers{}});
    apply_fx(c3);
    CHECK(openai_state(m3) == ProviderCatalog::State::Ready);   // 不装入

    // 过往 TTL 现在 STALE 延迟波浪会把它反射出来
    maya::testing::advance_anim_clock_ms(61'000);
    auto [m4, c4] = app::update(std::move(m3), Msg{FusedRefreshOthers{}});
    apply_fx(c4);
    CHECK(openai_state(m4) == ProviderCatalog::State::Loading);  // 补丁

    maya::testing::unfreeze_anim_clock();
}

// ^L (ModelsRefresh 强制进行全程直播,
// 无论 TTL : 它重排每个 目录 新鲜度和反射非 -active 现在就提供
TEST_CASE("fused ^L forces a full refresh") {
    using namespace agentty::msg;
    maya::testing::freeze_anim_clock(1'000'000);
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";
    g_settings.provider_keys["openai"]    = "sk-o";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    FusedCatalogLoaded oa; oa.provider_id = "openai";
    oa.models = {mi("gpt-5", "openai")}; oa.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(oa)});
    apply_fx(c2);

    auto openai_state = [](const Model& mm) {
        for (const auto& c : mm.d.provider_catalogs)
            if (c.provider_id == "openai") return c.state;
        return ProviderCatalog::State::Idle;
    };
    REQUIRE(openai_state(m2) == ProviderCatalog::State::Ready);   // 新鲜

    // ^L 连一个都无法回溯 FRESH 目录 (无) TTL 等一等).
    auto [m3, c3] = app::update(std::move(m2), Msg{ModelsRefresh{}});
    apply_fx(c3);
    CHECK(openai_state(m3) == ProviderCatalog::State::Loading);
    for (const auto& c : m3.d.provider_catalogs)
        CHECK(c.loaded_at_ms == 0);   // 全部重置新内容

    maya::testing::unfreeze_anim_clock();
}

// 签名 -in 提议 QUERY-GATED 装在保险丝上的 选择面板 备忘
// -authed 供应商作为报价(因此一个查询可以显示一个命名的报价——没有死角)
// 进行播种,但 BROWSE 视图( 空查询) 渲染 没有一个
TEST_CASE("fused browse view hides sign-in offers; query surfaces them") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";   // 只有 一个 供应商被认证
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);

    CHECK(!m1.d.fused_offers.empty());  // 取消对 -authed 提供者 ARE 种子...
    for (const auto& r : m1.d.fused_rows)
        CHECK(!r.is_signin_offer());    // ...但浏览 渲染 无

    // 打字一个un-authed的提供者的名字 完全暴露出它的报价行。
    Model m2 = std::move(m1);
    for (char ch : std::string{"mistral"}) {
        auto [mm, cc] = app::update(std::move(m2),
                                    Msg{ModelsFilterInput{
                                        static_cast<char32_t>(ch)}});
        m2 = std::move(mm);
    }
    bool offer_shown = false;
    for (const auto& r : m2.d.fused_rows)
        if (r.is_signin_offer() && r.provider_id == "mistral")
            offer_shown = true;
    CHECK(offer_shown);
}

// 引信安装时从供应商中签名 选择面板 打开的笔划 目录 ,
// 因此其模型停止出现(没有) 过期 列直到重新启动为止。
TEST_CASE("fused prunes a signed-out provider's catalog") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";
    g_settings.provider_keys["openai"]    = "sk-o";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    m.d.available_models = {mi("claude-sonnet-4-5", "anthropic")};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    FusedCatalogLoaded oa; oa.provider_id = "openai";
    oa.models = {mi("gpt-5", "openai")}; oa.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(oa)});
    apply_fx(c2);
    bool had_openai = false;
    for (const auto& c : m2.d.provider_catalogs)
        if (c.provider_id == "openai") had_openai = true;
    CHECK(had_openai);

    // 以Openai方式签字,然后重建源头(如任何更新).
    //
    // 通过 RECORD ,这就是真正的标志 -out 路径变异( E) login.cpp's
    // sign_out 财务报告和已审计财务报表 providers.cpp's
    // ^D 两者均从 m.d.persisted.provider_keys ,
    // 则 保存 只能从树枝店中清除,才能测试应用无法达到的状态——记录是什么?
    // refresh_fused_sources 读。
    g_settings.provider_keys.erase("openai");
    m2.d.persisted.provider_keys.erase("openai");
    auto [m3, c3] = app::update(std::move(m2), Msg{OpenModels{}});
    apply_fx(c3);  // 内容 -open 内容 -syncs
    for (const auto& c : m3.d.provider_catalogs)
        CHECK(c.provider_id != "openai");            // 目录 已刷新( P)
    for (const auto& r : m3.d.fused_rows)
        CHECK(r.provider_id != "openai");            // 无 过期 行数
}

// ^Tab 跳过一个死 MRU 条目(供应商签署或从清单中删除模型),
// 而不是转换到在下次请求时会达到400的ID。
TEST_CASE("^Tab skips a dead MRU entry") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";   // xai 不 已认证
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-a"};
    m.d.available_models = {mi("claude-a", "anthropic"),
                            mi("claude-c", "anthropic")};
    // 环: 活动阴极 -a ,然后是 DEAD xai 条目( 未认证), 然后是 claude -c.
    m.d.recent_models = {ModelRef{"anthropic", "claude-a"},
                         ModelRef{"xai", "grok-4"},          // 已死亡
                         ModelRef{"anthropic", "claude-c"}};

    auto [m1, c1] = app::update(std::move(m), Msg{SwitchToPreviousModel{}});
    apply_fx(c1);
    // 一定要跳过死Xai进入和降落在Claude上 -c.
    CHECK(m1.d.model_id.value == "claude-c");
}

// 提供商选择面板的^D(Mac-reachable为-in,
// 用于前方-Delete)在预设有已保存键的标志:第一是^D武器,
// 第二是犯罪(清除钥匙),报告案件是开路器。
TEST_CASE("provider picker: ^D signs out of a keyed preset (two-press)") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"]  = "sk-a";
    g_settings.provider_keys["openrouter"] = "sk-or";   // 键入预设
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenProviders{}});
    apply_fx(c1);

    // 将光标指向开路特行,确定它位于已建行列表中,
    // 然后设置 选择面板 索引。必须使用 相同 构建者
    // 状态更新函数 已,或索引表示不同的行。
    auto* p = m1.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    const auto sectioned = ui::build_sectioned_provider_rows(
        agentty::provider::saved_custom_hosts(g_settings.provider_keys), "",
        g_settings, m1.d.auth, m1.d.selection.catalog_id());
    int or_idx = -1;
    for (int i = 0; i < static_cast<int>(sectioned.rows.size()); ++i)
        if (const auto* pr = sectioned.rows[static_cast<std::size_t>(i)].row.preset();
            pr && std::string{pr->id} == "openrouter") { or_idx = i; break; }
    REQUIRE(or_idx >= 0);
    p->index = or_idx;
    p->query.clear();
    Model m2 = std::move(m1);

    // 第一届 ^D 手持标志 -out ; 键仍然存在。
    auto [m3, c3] = app::update(std::move(m2), Msg{ProvidersDelete{}});
    apply_fx(c3);
    CHECK(g_settings.provider_keys.count("openrouter") == 1);

    // 第二届 ^D 在同一行上输入标志 -out.
    auto [m4, c4] = app::update(std::move(m3), Msg{ProvidersDelete{}});
    apply_fx(c4);
    CHECK(g_settings.provider_keys.count("openrouter") == 0);  // 签名退出
    CHECK(g_settings.provider_keys.count("anthropic") == 1);   // 其它的没有受伤
}

// ACTIVE供应商行上的^D也必须0 有效方位头
// 文件——否则会话保留流式传输,用户刚刚撤销的凭据,
// "signed out"土司谎言.
TEST_CASE("provider picker: ^D on the ACTIVE provider zeroes live auth") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["openrouter"] = "sk-or";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("openrouter"));
    app::update_auth(::agentty::IoAccess::grant(), auth::AuthHeader{auth::ApiKeyHeader{"sk-or"}});

    Model m = seeded_model();
    auto [m1, c1] = app::update(std::move(m), Msg{OpenProviders{}});
    apply_fx(c1);
    auto* p = m1.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    const auto sectioned = ui::build_sectioned_provider_rows(
        agentty::provider::saved_custom_hosts(g_settings.provider_keys), "",
        g_settings, m1.d.auth, m1.d.selection.catalog_id());
    int or_idx = -1;
    for (int i = 0; i < static_cast<int>(sectioned.rows.size()); ++i)
        if (const auto* pr = sectioned.rows[static_cast<std::size_t>(i)].row.preset();
            pr && std::string{pr->id} == "openrouter") { or_idx = i; break; }
    REQUIRE(or_idx >= 0);
    p->index = or_idx;
    p->query.clear();

    auto [m2, c2] = app::update(std::move(m1), Msg{ProvidersDelete{}});
    apply_fx(c2);
    auto [m3, c3] = app::update(std::move(m2), Msg{ProvidersDelete{}});
    apply_fx(c3);
    CHECK(g_settings.provider_keys.count("openrouter") == 0);
    // 活的头文件被零化了——下一个转弯不能再使用死钥匙.
    CHECK(agentty::auth::is_empty(app::live_auth()));
}

// Enter 打开一个 ACCOUNT-CAPABLE 已经激活的提供者打开其账户操作
// -down (Esc 从那里关闭整个 选择面板 ——处理者. login_back →
// close_login 费用 AccountList。无 -account 供应商切换。
TEST_CASE("provider picker: Enter opens accounts on active OAuth provider") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-a";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-5"};
    auto [m1, c1] = app::update(std::move(m), Msg{OpenProviders{}});
    apply_fx(c1);

    // 将光标降落在(活跃的)人类行上。
    auto* p = m1.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    const auto rows = ui::build_provider_rows(
        agentty::provider::saved_custom_hosts(g_settings.provider_keys), "");
    int a_idx = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (const auto* pr = rows[static_cast<std::size_t>(i)].preset();
            pr && std::string{pr->id} == "anthropic") { a_idx = i; break; }
    REQUIRE(a_idx >= 0);
    p->index = a_idx;

    // Enter 打开账户列表( 关闭提供者) 选择面板).
    auto [m2, c2] = app::update(std::move(m1), Msg{ProvidersSelect{}});
    apply_fx(c2);
    CHECK(std::holds_alternative<ui::login::AccountList>(m2.ui.login));
    CHECK(!m2.ui.panel.get<pn::Providers>());  // 选择面板 关闭

    // Esc 从账户列表步骤中 BACK 向提供者提供 选择面板
    // (不是完全接近),保持等级账户 ~ 提供者 ~ 聊天.
    auto [m3, c3] = app::update(std::move(m2), Msg{LoginBack{}});
    apply_fx(c3);
    CHECK(std::holds_alternative<ui::login::Closed>(m3.ui.login));  // 账户已丢失
    CHECK(m3.ui.panel.get<pn::Providers>());                 // 回到提供者处
}

// 自定义主机可以持有 MULTIPLE 已保存 密钥(账户):端点
// 光谱上的账户层密钥,将每个密钥存储为账户密钥. 快照 然后激活必须绕
// -trip 将无记名密钥通过 provider_keys [标].
TEST_CASE("custom host supports multiple accounts") {
    using namespace agentty::msg;
    namespace acc = agentty::auth::accounts;
    // account_switch reads/writes 编号 真实 设置 + accounts.json
    // (通过) config_dir()/data_dir() → home_dir (),
    // 而不是道具的支点。 HOME 在一个丢弃目录中,所以这个测试永远不会触及用户的真实配置.
    const auto tmp = std::filesystem::temp_directory_path()
                   / ("agentty_acct_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    const char* old_home = ::getenv("HOME");
    ::setenv("HOME", tmp.c_str(), 1);

    install_stub_deps();
    const std::string spec = "my-host:8080/v1";   // 不是预设的QQ自定义主机

    auto set_key = [&](const std::string& k) {
        auto s = agentty::persistence::load_settings(::agentty::IoAccess::grant());
        s.provider_keys[spec] = k;
        agentty::persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    };
    auto get_key = [&]() {
        return agentty::persistence::load_settings(::agentty::IoAccess::grant()).provider_keys[spec];
    };

    // 账户 A 活动 : 快照 进入了 注册表.
    set_key("sk-aaaa1111");
    CHECK(acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), spec, "A"));

    // 将活动键切换到 B 和 快照。
    set_key("sk-bbbb2222");
    CHECK(acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), spec, "B"));

    // 这两家账户都为这个主机列出.
    CHECK(acc::list_for(::agentty::IoAccess::grant(), spec).size() == 2);

    // 激活 A 将键恢复为 provider_keys[scee]; 然后是 B.
    CHECK(acc::activate(::agentty::IoAccess::grant(), agtest::cap(), spec, "A"));
    CHECK(get_key() == "sk-aaaa1111");
    CHECK(acc::activate(::agentty::IoAccess::grant(), agtest::cap(), spec, "B"));
    CHECK(get_key() == "sk-bbbb2222");

    // 两把钥匙 SHARE 同样的最后4 仍然从 DISTINCT
    // 标签(标签混合了前缀+后缀+长度,而不仅仅是后缀)—
    // —因此第二个键不能与/覆盖首键在后缀中相撞. 注册表.
    set_key("sk-prefix1-SAME9999");
    const std::string l1 = acc::derive_current_label(::agentty::IoAccess::grant(), spec);
    set_key("sk-prefix2-SAME9999");
    const std::string l2 = acc::derive_current_label(::agentty::IoAccess::grant(), spec);
    CHECK(!l1.empty());
    CHECK(l1 != l2);

    if (old_home) ::setenv("HOME", old_home, 1); else ::unsetenv("HOME");
    std::filesystem::remove_all(tmp);
}

// 保险丝 选择面板 调谐在突出显示的模型上的推理努力(QQ/ ),
// 从旧模型中移植 选择面板 引信表面已经完全覆盖
TEST_CASE("fused picker cycles reasoning effort") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    // 努力的尝试 -capable 模型( 4) Claude Opus支持推理阶梯).
    m.d.model_id = ModelId{"claude-opus-4-5"};
    m.d.effort = Effort::None;
    m.d.available_models = {mi("claude-opus-4-5", "anthropic")};

    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    // 活动( 只) 模版行上的光标。
    if (auto* cur = m1.ui.panel.get<pn::Models>()) cur->index = 0;
    const Effort before = m1.d.effort;
    auto [m2, c2] = app::update(std::move(m1), Msg{ModelsCycleEffort{+1}});
    apply_fx(c2);
    // ⁇ /  ⁇   ⁇  变种 GLOBAL m.d.effort LIVE ——
    // 与经典型号完全相同. 选择面板 ,因此两个表面共享一个状态,不能有异议.
    CHECK(m2.d.effort != before);
    CHECK(m2.ui.effort_dirty);
    const Effort after = m2.d.effort;

    // 变化已经是全球性的;选择只持续 + 开关。
    auto [m3, c3] = app::update(std::move(m2), Msg{ModelsSelect{}});
    apply_fx(c3);
    CHECK(m3.d.effort == after);
    CHECK(!m3.ui.panel.get<pn::Models>());  // 选中的选择面板 关闭

    // 关闭冲洗脏工作编辑(与经典的选择面板相等)。
    auto [m4, c4] = app::update(std::move(m3), Msg{OpenModels{}});
    apply_fx(c4);
    if (auto* cur = m4.ui.panel.get<pn::Models>()) cur->index = 0;
    auto [m5, c5] = app::update(std::move(m4), Msg{ModelsCycleEffort{+1}});
    apply_fx(c5);
    auto [m6, c6] = app::update(std::move(m5), Msg{CloseModels{}});
    apply_fx(c6);
    CHECK(!m6.ui.effort_dirty);            // 持续关闭
}

// ^/ 切换 介于保险(all-providers) 选择面板 和经典单机
// -provider 选择面板之间: 打开一个泪水,另一个泪水清净地流下.
// Τ 一个 模型 选择面板 : 关于 -issuing
// OpenModels 必须重新 -open 干净( 重新查询、
// 重播行) 而不是堆叠秒 覆盖面板 或者离开前一个 缓存 后面,
// 这曾经是 切换 介于引信和经典之间 选择面板 经典的不见了
TEST_CASE("the model picker re-opens cleanly, never stacks") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic")};

    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    CHECK(m1.ui.panel.is<pn::Models>(), "^/ opens the picker");
    REQUIRE(!m1.d.fused_rows.empty());
    // 键入查询,然后重新 -open : 数据 覆盖面板 替换,
    // 而不是堆叠,并 过期 查询无法幸存。
    if (auto* c = m1.ui.panel.get<pn::Models>()) c->query = "zzz";
    auto [m2, c2] = app::update(std::move(m1), Msg{OpenModels{}});
    apply_fx(c2);
    CHECK(m2.ui.panel.is<pn::Models>(), "still exactly one picker");
    if (auto* c = m2.ui.panel.get<pn::Models>())
        CHECK(c->query.empty(), "re-open resets the filter");
    CHECK(!m2.d.fused_rows.empty(), "rows reseeded on re-open");

    // Esc 关闭并释放行 缓存.
    auto [m3, c3] = app::update(std::move(m2), Msg{CloseModels{}});
    apply_fx(c3);
    CHECK(!m3.ui.panel.is<pn::Models>(), "Esc closes the picker");
    CHECK(m3.d.fused_rows.empty(), "closing releases the row cache");
}

// 签名退出 一个 提供者当其他人仍然被认可时,应该退到静态
// -authed 提供者 – 不将用户拖到标志上 -in 弹出面板.
TEST_CASE("SignOut falls back to another authed provider") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["openrouter"] = "sk-or";
    g_settings.provider_keys["groq"]       = "sk-gr";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("openrouter"));

    Model m = seeded_model();
    m.d.recent_models = { ModelRef{"groq", "llama-3.3-70b"} };  // MRU 回退 国 务 院
    auto [m1, c1] = app::update(std::move(m), Msg{LoginMsg{SignOut{}}});
    apply_fx(c1);

    // 打开路由器的密钥被丢弃...
    CHECK(g_settings.provider_keys.count("openrouter") == 0);
    // ...我们做到了 不 将用户绑在标志上 -in 弹出面板.
    CHECK(!std::holds_alternative<agentty::ui::login::Picking>(m1.ui.login));
    // 开关瞄准静电 -authed 回退 (groq) (英语).
    CHECK(provider::active().openai_endpoint.label == "groq");
}

// 签名出局 仅 认证提供者打开标志 -in 弹出面板 .
// (当运行测试的机器恰好有真实的 -disk Anthropic 证明,
// 回退 正确找到他们,因此, CONTRACT : 要么 弹出
// 面板 打开,或者我们换成真正的 -authed 不同的供应商,
// 但是 绝不 左开路器的键被丢弃作为活动 凭据.)
TEST_CASE("SignOut with no saved fallback opens sign-in") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["openrouter"] = "sk-or";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("openrouter"));

    Model m = seeded_model();
    auto [m1, c1] = app::update(std::move(m), Msg{LoginMsg{SignOut{}}});
    apply_fx(c1);
    CHECK(g_settings.provider_keys.count("openrouter") == 0);
    const bool opened_signin =
        std::holds_alternative<agentty::ui::login::Picking>(m1.ui.login);
    const bool switched_away =
        provider::active().openai_endpoint.label != "openrouter";
    CHECK((opened_signin || switched_away));
}

// ── 退航: 装配型号为选择面板的自定义-host型号 ───────────────────────
// cb5847aa 退休的经典的每 -provider 模型 选择面板 (它直接
// 读取了活动提供者列表) available_models 并做了保险丝十字架
// -provider 选择面板 编号 仅 模型菜单,但引信 选择面板 读取
// provider_catalogs ,和 refresh_fused_sources
// 列出 仅 注册表 预设——a 已保存 自定义主机从未获得过 目录 ,
// 所以它的活 / i v1/models 获取降落在 available_models
// 菜单上没有显示任何内容。这些案例将恢复的行为贯穿于 真实 状态更新函数:
// ,活动自定义主机的目录镜像available_models(rows出现),
// 一个非-active 已保存自定义主機得到一个目录,一个FusedCatalogLoaded
// 可以合并成.
TEST_CASE("fused picker shows an active custom host's models") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    // A 已保存 自定义主机( O) provider_keys 键不是一个
    // 注册表 预设)是 活动 提供商,有活的 目录 已经掌握在手了
    g_settings.provider_keys["my-box.lan:8080"] = "";   // 无密钥本地主机
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("my-box.lan:8080"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"qwen3:32b"};
    m.d.available_models = {mi("qwen3:32b", "my-box.lan:8080"),
                            mi("llama3.3:70b", "my-box.lan:8080")};

    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    CHECK(m1.ui.panel.is<pn::Models>());

    // 自定义主机有一个 目录 ,这是 活动 一张,
    // 它反映了活列表(Ready, no -empty )——预设的
    // 种子合同准确性打开.
    bool host_ready = false;
    for (const auto& c : m1.d.provider_catalogs) {
        if (c.provider_id == "my-box.lan:8080") {
            host_ready = c.state == ProviderCatalog::State::Ready
                       && c.models.size() == 2;
        }
    }
    CHECK(host_ready, "active custom host catalog mirrors available_models");

    // ...因此,ROWS:模型菜单实际上列出了主机的模型.
    CHECK(!m1.d.fused_rows.empty());
    bool saw_qwen = false, saw_llama = false;
    for (const auto& r : m1.d.fused_rows) {
        if (r.provider_id == "my-box.lan:8080") {
            if (r.model.id.value == "qwen3:32b") saw_qwen = true;
            if (r.model.id.value == "llama3.3:70b") saw_llama = true;
        }
    }
    CHECK(saw_qwen);
    CHECK(saw_llama);
}

TEST_CASE("fused picker gives a non-active saved custom host a mergeable catalog") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    // Anthropic 活动; 自定义主机是 已保存 不过 不 活动。
    // provider_keys 成员身份是自定义主机的认证测试,所以它必须作为一个源
    // 出现——空的,准备 cmd::fetch_models_for 以充满.
    g_settings.provider_keys["anthropic"]     = "sk-test";
    g_settings.provider_keys["api.my-gw.com"] = "sk-gw";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));

    Model m = seeded_model();
    m.d.model_id = ModelId{"claude-sonnet-4-6"};
    m.d.available_models = {mi("claude-sonnet-4-6", "anthropic")};

    auto [m1, c1] = app::update(std::move(m), Msg{OpenModels{}});
    apply_fx(c1);
    bool gw_idle = false;
    for (const auto& c : m1.d.provider_catalogs) {
        if (c.provider_id == "api.my-gw.com")
            gw_idle = c.state == ProviderCatalog::State::Idle
                   && c.models.empty();
    }
    CHECK(gw_idle, "saved non-active custom host has an Idle catalog");

    // 延波采地:由provider_id合并,再排成行显示.
    FusedCatalogLoaded gw;
    gw.provider_id = "api.my-gw.com";
    gw.models = {mi("some-model-x", "api.my-gw.com")};
    gw.ok = true;
    auto [m2, c2] = app::update(std::move(m1), Msg{std::move(gw)});
    apply_fx(c2);
    bool gw_ready = false;
    for (const auto& c : m2.d.provider_catalogs)
        if (c.provider_id == "api.my-gw.com")
            gw_ready = c.state == ProviderCatalog::State::Ready
                    && c.models.size() == 1;
    CHECK(gw_ready);
    bool saw_gw_model = false;
    for (const auto& r : m2.d.fused_rows)
        if (r.provider_id == "api.my-gw.com"
            && r.model.id.value == "some-model-x") saw_gw_model = true;
    CHECK(saw_gw_model, "merged custom-host catalog yields rows");
}

// ── 列举:收养不能产生重复 ─────────────────
// "api.githubcopilot.com" 是一个 provider_keys
// 没有预设的条目 SPEC ,不过 parse_selection () 将其
// 提交副驾驶 ROW 故可适用副驾驶端点和认证. saved_custom_hosts
// ()用来测试原始的谱,所以宿主回来作为 "custom" AS WELL
// AS 是一个预设——每个消费者都列出两次。 选择面板 显示两个
// "GitHub Copilot" 排,一旦引信 选择面板 已获取自定义 -host 目录
// 长了一秒钟 目录 并复制模型与之。
//
// 测试在 RESOLVED ID,这些针头,在两个消费者身上。
TEST_CASE("an adopted host is not also a custom host") {
    store::Settings s;
    s.provider_keys["api.githubcopilot.com"] = "tok";
    s.provider_keys["api.my-gw.com"]         = "k";   // 真实习惯

    const auto hosts = provider::saved_custom_hosts(s.provider_keys);
    CHECK(hosts.size() == 1, "only the genuine custom host is 'custom'");
    CHECK(hosts.front() == "api.my-gw.com");

    // 提供商选择面板将其列出一次,作为它决心的预设.
    const auto rows = ui::build_provider_rows(hosts, "");
    int copilot_rows = 0, gw_rows = 0;
    for (const auto& r : rows) {
        if (auto* p = std::get_if<ui::ProviderRow::Preset>(&r.kind))
            if (std::string_view{p->preset->id} == "copilot") ++copilot_rows;
        if (auto* c = std::get_if<ui::ProviderRow::CustomHost>(&r.kind)) {
            if (provider::parse_selection(c->spec).provider_id() == "copilot")
                ++copilot_rows;
            if (c->spec == "api.my-gw.com") ++gw_rows;
        }
    }
    CHECK(copilot_rows == 1, "provider picker shows Copilot exactly once");
    CHECK(gw_rows == 1, "the genuine custom host still gets its row");
}

// 电话 目录 计数是 相同 列表,因此它不能与 选择面板 是
// 关于哪个后端的。 这是属性。
// "we forgot to enumerate X" 无法形容,而不是只是固定的。
TEST_CASE("catalog_sources enumerates presets and custom hosts, once each") {
    store::Settings s;
    s.provider_keys["api.githubcopilot.com"] = "tok";   // + 副驾驶
    s.provider_keys["api.my-gw.com"]         = "k";     // 真习惯
    s.provider_keys["localhost:11434"]       = "x";     // 本地终点
    // 退步(问题30):a 完整-URL 带有多功能的自定义主机 -segment
    // 路径( 选项) z.ai's GLM 编码计划)必须是自己的 目录 源
    // —— 这不是一个预设, 也不被采纳到一个, 所以 选择面板 什么都没有显示。
    s.provider_keys["https://api.z.ai/api/coding/paas/v4"] = "zkey";

    const auto srcs = provider::catalog_sources(s, agentty::auth::AuthView{});

    auto count_id = [&](std::string_view id) {
        int n = 0;
        for (const auto& c : srcs) if (c.id == id) ++n;
        return n;
    };
    CHECK(count_id("copilot") == 1, "adopted host does not double the preset");
    CHECK(count_id("api.githubcopilot.com") == 0, "adopted spec is not its own source");
    CHECK(count_id("api.my-gw.com") == 1, "a custom gateway is a source");
    CHECK(count_id("localhost:11434") == 1, "a LOCAL endpoint is a source too");
    CHECK(count_id("https://api.z.ai/api/coding/paas/v4") == 1,
          "a full-URL custom host is a source");

    // 每一ID都是独特的——财产为每 -caller 点数一直输掉
    std::set<std::string> ids;
    for (const auto& c : srcs) {
        CHECK(ids.insert(c.id).second, "catalog source ids are unique");
        CHECK(!c.label.empty(), "every source has a label to render");
    }
    // 预设仍然全部存在(计数没有降低简单的一半).
    for (const auto& p : provider::providers())
        CHECK(count_id(p.id) == 1);
    // 自定义主机从不签名 -in 提供: 持有 已保存 键 IS 认证。
    for (const auto& c : srcs)
        if (!c.is_preset) CHECK(!c.needs_signin);
}

// 凭据现在是副作用的主机:一个开关说WHICH供应商的头
// 文件安装,主机解决;标志-out说清除.
// 状态更新函数从来没有读过凭据文件本身.
TEST_CASE("provider switch and sign-out return InstallAuth, not a resolved header") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    g_settings.provider_keys["openrouter"] = "sk-or";
    g_settings.provider_keys["groq"]       = "sk-gr";
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("openrouter"));

    Model m = seeded_model();
    m.d.recent_models = { ModelRef{"groq", "llama-3.3-70b"} };
    auto [m1, c1] = app::update(std::move(m), Msg{LoginMsg{SignOut{}}});

    // 签名 -out 清除它留下的供货商,然后 回退 切换安装
    // groq 的 头文件.
    bool cleared = false, installed_groq = false;
    agtest::fx::for_each(c1, [&](const auto& e) {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(e)>,
                                   agentty::InstallAuth>) {
            if (e.clear && e.provider == "openrouter") cleared = true;
            if (!e.clear && e.provider == "groq") installed_groq = true;
        }
    });
    CHECK(cleared);
    CHECK(installed_groq);
}

TEST_CASE("OAuth login: the PKCE pair is minted by a Cmd, and a stale mint is dropped") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    Model m = seeded_model();
    m.ui.login = ui::login::Picking{};

    // 回复随薄荷验证器移动到 OAuthCode。
    auth::PkceVerifier v{"verifier"};
    auth::OAuthState   st{"state"};
    auto [m1, c1] = app::update(std::move(m), Msg{LoginMsg{LoginOAuthMinted{
        v, st, "https://claude.ai/oauth/authorize?x", {}}}});
    auto* oc = std::get_if<ui::login::OAuthCode>(&m1.ui.login);
    REQUIRE(oc != nullptr);
    CHECK(oc->verifier.value == "verifier");
    CHECK(oc->authorize_url == "https://claude.ai/oauth/authorize?x");

    // 用户已经关闭 弹出面板: 晚期薄荷会改变什么.
    m1.ui.login = ui::login::Closed{};
    auto [m2, c2] = app::update(std::move(m1), Msg{LoginMsg{LoginOAuthMinted{
        v, st, "https://late", {}}}});
    CHECK(std::holds_alternative<ui::login::Closed>(m2.ui.login));
    CHECK(c2.is_none());
}

// 该账号列表及其编辑工作与Model的AuthView无关;注册表的写法为
// AccountOp 副作用,视图回溯为AuthViewLoaded.
TEST_CASE("accounts: list from the view, removal promotes the newest other") {
    using namespace agentty::msg;
    install_stub_deps();
    g_settings = store::Settings{};
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));
    Model m = seeded_model();
    m.d.auth.loaded = true;
    m.d.auth.providers["anthropic"] = agentty::auth::ProviderAuth{
        .stored = true, .accounts = {"work", "home"}, .active_account = "work"};

    auto [m1, c1] = app::update(std::move(m), Msg{LoginMsg{OpenAccounts{"anthropic"}}});
    auto* al = std::get_if<ui::login::AccountList>(&m1.ui.login);
    REQUIRE(al != nullptr);
    REQUIRE(al->rows.size() == 2);
    CHECK(al->rows[0].label == "work");
    CHECK(al->rows[0].active);
    CHECK(al->cursor == 0);

    // 删除活动 : 2 个按键, 然后删除 + 激活( "home")。
    auto [m2, c2] = app::update(std::move(m1), Msg{LoginMsg{AccountRemove{}}});
    auto [m3, c3] = app::update(std::move(m2), Msg{LoginMsg{AccountRemove{}}});
    bool removed = false, promoted = false, reload = false;
    agtest::fx::for_each(c3, [&](const auto& e) {
        using U = std::remove_cvref_t<decltype(e)>;
        if constexpr (std::same_as<U, agentty::AccountOp>) {
            if (e.kind == agentty::AccountOp::Kind::Remove && e.label == "work")
                removed = true;
            if (e.kind == agentty::AccountOp::Kind::Activate && e.label == "home")
                promoted = true;
        } else if constexpr (std::same_as<U, agentty::LoadAuthView>) {
            reload = true;
        }
    });
    CHECK(removed);
    CHECK(promoted);
    CHECK(reload);

    // 回来的视图重建了开放列表.
    agentty::auth::AuthView v;
    v.loaded = true;
    v.providers["anthropic"] = agentty::auth::ProviderAuth{
        .stored = true, .accounts = {"home"}, .active_account = "home"};
    auto [m4, c4] = app::update(std::move(m3),
        Msg{LoginMsg{AuthViewLoaded{std::move(v), {}}}});
    auto* al4 = std::get_if<ui::login::AccountList>(&m4.ui.login);
    REQUIRE(al4 != nullptr);
    REQUIRE(al4->rows.size() == 1);
    CHECK(al4->rows[0].label == "home");
    CHECK(al4->rows[0].active);
}

// 回顾 已保存 在一个古老的标志下 仍然必须找到它们
TEST_CASE("model recall: legacy provider spellings find the saved model") {
    agentty::store::Settings s;
    s.provider_models["openai"] = "gpt-5.4";
    CHECK(app::detail::model_for_provider(s, "openai") == "gpt-5.4");
    CHECK(app::detail::model_for_provider(s, "codex") == "gpt-5.4");
    // 旧建筑的条目 已保存 原始的光谱仍然算数
    agentty::store::Settings old;
    old.provider_models["codex"] = "gpt-5.4-mini";
    CHECK(app::detail::model_for_provider(old, "codex") == "gpt-5.4-mini");
}
