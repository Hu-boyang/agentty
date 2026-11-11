// custom_host_key_prompt_test ——自定义主机
// 是如何被添加的。每个主机首先被探测到, TLS 或不是,因为许多远程主机
// 不需要钥匙。 提示词 只有在探测器得到一个时才打开 401/403 (
// 需要键)或主机已经有一个 已保存 键( 检查或更改它)。
// 在可选的键上 提示词 空的,一个空的 Enter 探测无键而不是失败。
// Esc 键位 提示词 取消开关。
//
// 纯 状态更新函数 测试: 根 deps() 与一个英寸 -memory store::Settings
// 组合 XDG_CONFIG_HOME 临时目录 auth::load_credentials
// ()返回无选择(无) -disk 证书干扰非 -TLS。测试是否检测 commit_provider_switch
// 运行通过 m.s.models_loading (哪个) commit_provider_switch
// 设定 modal.cpp :691)——同为间接.
// codex_login_flow_test 用于生产 ChatGPT 路径。
//
// 运行: 构建`custom_host_key_prompt_test`目标, 执行。 退出 0 = 通过。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/panel/slot.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/store/store.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "agtest.hpp"
#include "agtest_fx.hpp"

namespace fs = std::filesystem;
namespace login = agentty::ui::login;
namespace msg = agentty::msg;
namespace app = agentty::app;

// 帮助者: 安装 deps() 与一个英寸 -memory 引用时捕获的设置。
// 另一边 Deps 成员被扭曲为否 -ops 已撤销 login_submit's CustomHostInput
// 仅指臂 调用 deps().load_settings (a) 与 deps() . 奥斯.
static void install_stub_deps(agentty::store::Settings& s) {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<agentty::Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<agentty::Thread> { return std::nullopt; },
        .load_settings  = [&s] { return s; },
        .save_settings  = [&s](const agentty::store::Settings& x) { s = x; },
        .new_thread_id  = [] { return agentty::ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = agentty::auth::AuthHeader{agentty::auth::ApiKeyHeader{std::string{}}},
    });
}

TEST_CASE("setting up a provider does not switch to it") {
    // AUTHENTICATING IS 不 SELECTING 中
    // 添加主机或粘贴用于结尾的密钥 commit_provider_switch ,
    // 移动活动提供者 和 选择一个模型- 因此在中点注册备份
    // 密钥 -task 默默地改变了你的模型
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-noswitch-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    // ── 在配置 ALREADY 时添加了主机, 将会话单独离开 ──
    {
        agentty::store::Settings s;
        s.provider_keys["anthropic"] = "sk-ant-existing";   // 已经设置
        install_stub_deps(s);

        agentty::Model m;
        m.d.persisted = s;
        m.d.auth.loaded = true;
        m.d.auth.providers["anthropic"] =
            agentty::auth::ProviderAuth{.stored = true};
        m.d.model_id = agentty::ModelId{"claude-opus-4-5"};
        const auto before_model = m.d.model_id;
        const auto before_kind  = m.d.selection.kind;
        // host_probed 放弃一个结果,其尝试ID 与现场不符
        // HostProbing 州,所以, 选择面板 一定要去那儿 一定要去 一定要去
        m.ui.login = login::HostProbing{.spec = "localhost:11434",
                                        .attempt_id = 1};

        agentty::HostProbed r;
        r.attempt_id = 1;
        r.spec = "localhost:11434";
        r.ok = true;
        r.model_count = 7;
        r.latency_ms = 12;
        auto [m2, cmd] = ::agentty::app::detail::step(
            ::agentty::app::detail::login_update, std::move(m),
            msg::LoginMsg{std::move(r)});

        check(m2.d.model_id == before_model,
              "adding a host must not change the active model");
        check(m2.d.selection.kind == before_kind,
              "adding a host must not change the active provider");
        // 但它却 IS 已保存 ,所以,请 选择面板 能够提供它。
        check(m2.d.persisted.provider_keys.count("localhost:11434") == 1,
              "the host is still registered");
        check(m2.s.status.find("^P") != std::string::npos,
              "the toast names how to switch");
    }

    // ── 第一跑: 没有配置, 所以采用 IS 右移 ──
    {
        agentty::store::Settings s;   // 空: 任何地方都没有密钥
        install_stub_deps(s);

        agentty::Model m;
        m.d.persisted = s;
        m.d.auth.loaded = true;
        m.ui.login = login::HostProbing{.spec = "localhost:11434",
                                        .attempt_id = 1};

        agentty::HostProbed r;
        r.attempt_id = 1;
        r.spec = "localhost:11434";
        r.ok = true;
        r.model_count = 3;
        r.latency_ms = 9;
        auto [m2, cmd] = ::agentty::app::detail::step(
            ::agentty::app::detail::login_update, std::move(m),
            msg::LoginMsg{std::move(r)});

        check(m2.d.selection.kind == agentty::provider::Kind::OpenAI,
              "first run adopts the host -- there is no session to protect");
        check(m2.s.status.find("^P") == std::string::npos,
              "and does not tell you to switch to what you are already on");
    }
    // ── 粘贴一个 API KEY 给另一个供应商,你放的也留下 ──
    {
        agentty::store::Settings s;
        s.provider_keys["anthropic"] = "sk-ant-existing";
        install_stub_deps(s);

        agentty::Model m;
        m.d.persisted = s;
        m.d.auth.loaded = true;
        m.d.auth.providers["anthropic"] =
            agentty::auth::ProviderAuth{.stored = true};
        m.d.model_id = agentty::ModelId{"claude-opus-4-5"};
        const auto before_model = m.d.model_id;
        const auto before_kind  = m.d.selection.kind;

        login::ApiKeyInput api;
        api.provider       = "groq";
        api.provider_label = "Groq";
        api.key_input      = "gsk_testkey";
        m.ui.login = std::move(api);

        auto [m2, cmd] = ::agentty::app::detail::step(
            ::agentty::app::detail::login_update, std::move(m),
            msg::LoginMsg{agentty::LoginSubmit{}});

        check(m2.d.model_id == before_model,
              "pasting a Groq key must not change the model you are on");
        check(m2.d.selection.kind == before_kind,
              "nor the active provider");
        check(m2.d.persisted.provider_keys.count("groq") == 1,
              "the key is saved — Groq is ready when you want it");
        check(m2.d.persisted.provider != "groq",
              "and the persisted active provider is untouched");
    }
}

TEST_CASE("first-run host adopt leaves provider and model agreeing") {
    // 报告的一半 -switch : 输入本地主机更改了
    // 提供者 标签 却离开了模型 标签 空的,
    // 并开除自动 -opened 选择面板 无法命名模型的开关
    // 不能让提供商自己承担一半的责任。
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-halfswitch-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    agentty::store::Settings s;          // 无配置: 第一次运行
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;
    m.ui.login = login::HostProbing{.spec = "localhost:11434",
                                    .attempt_id = 1};

    agentty::HostProbed r;
    r.attempt_id = 1;
    r.spec = "localhost:11434";
    r.ok = true;
    r.model_count = 4;
    r.latency_ms = 10;
    auto [m2, cmd] = ::agentty::app::detail::step(
        ::agentty::app::detail::login_update, std::move(m),
        msg::LoginMsg{std::move(r)});

    // 初行采用,故提供方SHOULD已移动.
    check(m2.d.selection.kind == agentty::provider::Kind::OpenAI,
          "first run adopts the host");
    // 电话 目录 还没降落,所以...
    // model_id 刻意空置 - 携带出厂商的模型
    // 会将其存档于新厂商的召回之下。 TRANSIENT
    // 但必须解决那一刻 目录 到了,不要等用户选。
    check(m2.d.model_id.empty(),
          "model is empty until the catalog lands (not the outgoing one)");
    check(m2.s.models_loading,
          "and the badge can say so: a fetch is in flight");

    // 电话 目录 到了,即使用户解开这辆汽车 -opened
    // 选择面板 无需选择, 模型必须填入,
    // 否则提供方 标签 命名一个后端, 后面没有模型。
    agentty::ModelsLoaded loaded;
    loaded.provider_id = m2.d.selection.catalog_id();
    for (const char* id : {"qwen3:30b-a3b", "qwen3:8b"}) {
        agentty::ModelInfo mi;
        mi.id = agentty::ModelId{id};
        mi.display_name = id;
        mi.context_window = 32768;
        loaded.models.push_back(std::move(mi));
    }
    auto [m3, cmd3] = ::agentty::app::detail::step(
        ::agentty::app::detail::models_update, std::move(m2),
        msg::ModelsMsg{std::move(loaded)});

    check(!m3.d.model_id.empty(),
          "the catalog landing resolves the model without a user pick");
    check(!m3.s.models_loading, "and the fetch is no longer in flight");
}

TEST_CASE("a second local host does not adopt over the one in use") {
    // 守护端-to-end行为而非上游:一个针对一个
    // 本地框工作的用户会添加另一个并保持它们的会话.
    // (上游的边缘——无键主机,即 不 活动 -- -- 是
    // "saved-but-unusable" 下个案例,这就是旧的
    // provider_keys.empty ()测试和这个实际上不同意。 )
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-2ndhost-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    agentty::store::Settings s;
    s.provider_keys["localhost:11434"] = "";   // 无键, 已添加
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;
    // ...这就是用户 CURRENTLY 与我们对抗
    m.d.selection = agentty::provider::parse_selection("localhost:11434");
    m.d.model_id  = agentty::ModelId{"qwen3:8b"};
    const auto before_sel   = m.d.selection.catalog_id();
    const auto before_model = m.d.model_id;

    m.ui.login = login::HostProbing{.spec = "localhost:8080",
                                    .attempt_id = 2};
    agentty::HostProbed r;
    r.attempt_id = 2;
    r.spec = "localhost:8080";
    r.ok = true;
    r.model_count = 2;
    r.latency_ms = 8;
    auto [m2, cmd] = ::agentty::app::detail::step(
        ::agentty::app::detail::login_update, std::move(m),
        msg::LoginMsg{std::move(r)});

    check(m2.d.selection.catalog_id() == before_sel,
          "the host you are working on stays active");
    check(m2.d.model_id == before_model,
          "and your model is untouched");
    check(m2.d.persisted.provider_keys.count("localhost:8080") == 1,
          "the new host is still saved for later");
    check(m2.s.status.find("^P") != std::string::npos,
          "with the switch offered, not taken");
}

TEST_CASE("a saved-but-unusable host still counts as unconfigured") {
    // 之前两个前提的 事实和旧的错在 两者 方向。
    // 无密钥主机被存储为空 -string 数值,
    // 所以 "provider_keys.empty()"
    // 名为配置( ) -- 尽管地图中没有任何内容可以服务于转弯,
    // 且活动提供者没有 凭据 某个在那个状态下添加了
    // 真正的后端的人 没有收养,而且会死
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-unusable-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    agentty::store::Settings s;
    // 曾经添加过一个无键主机, 但它不是 选择哪些,
    // 以及选择的提供者( Anthropic )没有 凭据.
    s.provider_keys["localhost:11434"] = "";
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;          // 已加载, 空: 无存储
    m.ui.login = login::HostProbing{.spec = "localhost:8080",
                                    .attempt_id = 3};
    agentty::HostProbed r;
    r.attempt_id = 3;
    r.spec = "localhost:8080";
    r.ok = true;
    r.model_count = 2;
    r.latency_ms = 7;
    auto [m2, cmd] = ::agentty::app::detail::step(
        ::agentty::app::detail::login_update, std::move(m),
        msg::LoginMsg{std::move(r)});

    check(m2.d.selection.catalog_id() == "localhost:8080",
          "nothing usable was configured, so the new host is adopted");
}

TEST_CASE("switching provider drops the previous turn's live stamp") {
    // 状态徽章更喜欢 m.s.smart_turn_model 以图示
    // 你们所剩的章子, OUTGOING 提供者绘制一对不可能的配对
    // : "Ollama" 边上 "Opus 4.5"。通常,
    // 转弯时会清除印章,但之后随时可能发生开关 -- -- 而且,
    // 由于#120每转都设置,而不仅仅是Smart -Mode-routed
    // 其中一个,所以 过期 价值现在是常见情况,而不是罕见的情况。
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-stamp-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    agentty::store::Settings s;
    s.provider_keys["localhost:11434"] = "";
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;
    m.d.model_id = agentty::ModelId{"claude-opus-4-5"};
    // 转动一下就跑开 Anthropic 并留下了它的印记。
    m.s.smart_turn_model = agentty::ModelId{"claude-opus-4-5"};

    auto cmd = ::agentty::app::detail::commit_provider_switch(
        m, "localhost:11434", "Ollama");
    (void)cmd;

    check(m.s.smart_turn_model.empty(),
          "the outgoing provider's live stamp is dropped on the switch");
    check(!m.s.smart_turn_role.has_value(), "and so is its role");
    // 徽章首先读为smart_turn_model;随着它被清除,
    // 它又回到了选择,这是东道主自己的(尚未解决)模式.
    check(m.d.model_id.value != "claude-opus-4-5",
          "so the badge cannot still name the old provider's model");
}

TEST_CASE("picking a new model on the same provider updates the badge now") {
    // 报告:切换Opus 5.5 - > Opus 5,
    // 徽章在发送下一条信息之前会一直说5.5。徽章更喜欢现场的
    // 路由图章而不是选择,徽章则会显示: 相同-PROVIDER
    // 模型开关从未清理过 - 所以 标签 一直给上次转弯的模型
    // 命名 直到新的转弯翻转邮票为止
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-modelstamp-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);

    agentty::store::Settings s;
    s.provider_keys["anthropic"] = "sk-ant-x";
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;
    m.d.selection = agentty::provider::parse_selection("anthropic");
    m.d.model_id  = agentty::ModelId{"claude-opus-5-5"};
    // 转弯5.5秒就结束了,留下了它的邮票.
    m.s.smart_turn_model = agentty::ModelId{"claude-opus-5-5"};

    // 在画上选一个不同的模型 相同 提供者,通过真实 选择
    // 面板 路径:打开,将光标放在另一行, Enter.
    m.d.available_models = {
        [] { agentty::ModelInfo i; i.id = agentty::ModelId{"claude-opus-5-5"};
             i.display_name = "Opus 5.5"; i.provider = "anthropic";
             i.context_window = 200000; return i; }(),
        [] { agentty::ModelInfo i; i.id = agentty::ModelId{"claude-opus-5"};
             i.display_name = "Opus 5"; i.provider = "anthropic";
             i.context_window = 200000; return i; }(),
    };
    auto [m1, c1] = app::update(std::move(m), agentty::Msg{agentty::OpenModels{}});
    auto* pk = m1.ui.panel.get<agentty::ui::panel::Models>();
    REQUIRE(pk != nullptr);
    int target = -1;
    for (int i = 0; i < static_cast<int>(m1.d.fused_rows.size()); ++i)
        if (m1.d.fused_rows[static_cast<std::size_t>(i)].model.id.value
                == "claude-opus-5") { target = i; break; }
    REQUIRE(target >= 0);
    pk->index = target;
    auto [m2, c2] = app::update(std::move(m1),
                                agentty::Msg{agentty::ModelsSelect{}});

    check(m2.d.model_id.value == "claude-opus-5", "the selection moved");
    check(m2.s.smart_turn_model.empty(),
          "and the stale stamp is gone, so the badge names the new model");
}

TEST_CASE("working on Ollama counts as configured") {
    // Ollama 是一个 REGISTRY 预设为 is_local =
    // real,不是自定义主机。通过检查跳过本地认证( Ollama
    // 安装不是证据 agentty 被设置),只留下一个无键 CUSTOM
    // 主机在活动时 - 所以用户实际上正在工作 Ollama 预
    // 设看起来没有配置, 添加任何提供者 采纳了它, 并把它们拉下来。
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-ollama-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(tmp_xdg, ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    agentty::store::Settings s;          // 任何地方都没有密钥
    install_stub_deps(s);

    agentty::Model m;
    m.d.persisted = s;
    m.d.auth.loaded = true;
    m.d.selection = agentty::provider::parse_selection("ollama");
    m.d.model_id  = agentty::ModelId{"qwen3:8b"};
    const auto before = m.d.selection.catalog_id();

    m.ui.login = login::HostProbing{.spec = "localhost:8080", .attempt_id = 9};
    agentty::HostProbed r;
    r.attempt_id = 9;
    r.spec = "localhost:8080";
    r.ok = true;
    r.model_count = 1;
    r.latency_ms = 5;
    auto [m2, cmd] = ::agentty::app::detail::step(
        ::agentty::app::detail::login_update, std::move(m),
        msg::LoginMsg{std::move(r)});

    check(m2.d.selection.catalog_id() == before,
          "the Ollama session you were working in stays active");
    check(m2.d.model_id.value == "qwen3:8b", "and so does its model");
}

TEST_CASE("an empty provider key is not a credential, Anthropic included") {
    // auth_source 选中非 -empty 用于每个密钥提供者
    // EXCEPT Anthropic ,该词使用了 count() - 这么
    // 空 -string 条目改为签名。 选择面板 本来会把它放在 YOURS
    // 收养支票会称之为配置
    agentty::store::Settings s;
    s.provider_keys["anthropic"] = "";
    agentty::auth::AuthView view;
    view.loaded = true;
    const auto* p = agentty::provider::preset_for("anthropic");
    REQUIRE(p != nullptr);
    CHECK(agentty::provider::auth_source(*p, s, view)
          == agentty::provider::AuthSource::None);

    s.provider_keys["anthropic"] = "sk-ant-real";
    CHECK(agentty::provider::auth_source(*p, s, view)
          == agentty::provider::AuthSource::Saved);
}

TEST_CASE("custom host key prompt transitions") {

    // 重定向 XDG_CONFIG_HOME 改为临时目录,无 agentty/ 子目录为此
    // auth::load_credentials ()返回无选择(无) credentials.json ) , 已存在).
    // 这保持了非-TLS路径决定论,而不管主机的内涵.
    fs::path tmp_xdg = fs::temp_directory_path() /
        ("agentty-test-" + std::to_string(::getpid()));
    std::error_code mkdir_ec;
    fs::create_directories(tmp_xdg, mkdir_ec);
    ::setenv("AGENTTY_HOME", tmp_xdg.c_str(), 1);
    // 保证 parse_selection's custom_auth_header () 改为 " 决定 "。
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");

    // ── 案例1: TLS 东道主,没有 已保存 – 探究它,没有钥匙 提示词 ──
    {
        agentty::store::Settings s;
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;   // 记录,作为 init() 种子,种子,种子
        m.ui.login = login::CustomHostInput{.host_input = "https://chat.example.org/api"};
        auto [m2, cmd] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        auto* hp = std::get_if<login::HostProbing>(&m2.ui.login);
        check(hp != nullptr,
              "1: TLS host with no key is probed (a RunPod pod needs none)");
        check(hp && hp->spec == "https://chat.example.org/api",
              "1: probing the full URL spec");
        check(!m2.s.models_loading, "1: no commit while probing");

        // 服务器回答 401 ~ 键 提示词 打开,需要标记.
        agentty::HostProbed needs;
        needs.attempt_id = hp ? hp->attempt_id : 0;
        needs.spec = "https://chat.example.org/api";
        needs.ok = false;
        needs.needs_key = true;
        needs.error = "HTTP 401";
        auto [m3, cmd3] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m2), msg::LoginMsg{std::move(needs)});
        auto* api = std::get_if<login::ApiKeyInput>(&m3.ui.login);
        check(api != nullptr, "1: a 401 opens the key prompt");
        check(api && api->provider == "https://chat.example.org/api",
              "1: ApiKeyInput.provider is the full URL spec (persistence key)");
        check(api && api->key_required, "1: the key is marked required");

        // Enter 空键上需要的键 提示词.
        auto [m4, cmd4] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m3), msg::LoginMsg{agentty::LoginSubmit{}});
        check(std::holds_alternative<login::ApiKeyInput>(m4.ui.login),
              "1: empty submit on a required key stays on the prompt");
        check(!m4.s.models_loading, "1: still no commit");
        (void)cmd; (void)cmd3; (void)cmd4;
    }

    // ── 判例1b:可选密钥 提示词 空的,请检查date=中的日期值 (帮助) Enter □ 无键探测器 ──
    {
        agentty::store::Settings s;
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;
        m.ui.login = login::ApiKeyInput{
            .provider = "https://pod-8888.proxy.runpod.net/v1",
            .provider_label = "pod-8888.proxy.runpod.net"};
        auto [m2, cmd] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        auto* hp = std::get_if<login::HostProbing>(&m2.ui.login);
        check(hp != nullptr, "1b: empty key on a custom host probes keyless");
        check(hp && hp->spec == "https://pod-8888.proxy.runpod.net/v1",
              "1b: probing the host the prompt was for");
        (void)cmd;
    }

    // ── 案例2: TLS 东道主, 已保存 键存在 − ApiKeyInput 预览 -filled ──
    {
        agentty::store::Settings s;
        s.provider_keys["https://chat.example.org/api"] = "sk-test-key";
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;   // 记录,作为 init() 种子,种子,种子
        m.ui.login = login::CustomHostInput{.host_input = "https://chat.example.org/api"};
        auto [m2, cmd] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        auto* api = std::get_if<login::ApiKeyInput>(&m2.ui.login);
        check(api != nullptr,
              "2: TLS host with a saved key opens the key prompt to check it");
        check(api && api->key_input == "sk-test-key",
              "2: key_input pre-filled with the saved key (2a)");
        check(api && api->cursor == 11,
              "2: cursor at end of pre-filled key (sk-test-key = 11 chars)");
        check(!m2.s.models_loading,
              "2: no commit (waiting for key submit)");
        (void)cmd;
    }

    // 用例 3：未启用 TLS 的 host:port，先进入 HostProbing 探测，再提交
    //    仅打开 HostProbed ⁇
    {
        agentty::store::Settings s;
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;   // 记录,作为 init() 种子,种子,种子
        m.d.available_models.push_back(agentty::ModelInfo{
            .id = agentty::ModelId{"old-model"}, .display_name = "old"});
        m.ui.login = login::CustomHostInput{.host_input = "localhost:8080"};
        auto [m2, cmd] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        auto* hp = std::get_if<login::HostProbing>(&m2.ui.login);
        check(hp != nullptr,
              "3: non-TLS host enters HostProbing (probe before commit)");
        check(hp && hp->spec == "localhost:8080",
              "3: probing the canonical spec");
        check(!m2.s.models_loading,
              "3: not committed while probing");
        // 成功探测结果 − 承诺( models_loading 翻转, 弹出面板 )),"("关").
        agentty::HostProbed ok;
        ok.attempt_id = hp->attempt_id;
        ok.spec = "localhost:8080";
        ok.ok = true;
        ok.models_path = "/v1/models";
        ok.model_count = 2;
        auto [m3, cmd3] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m2), msg::LoginMsg{std::move(ok)});
        // 键盘的-hostXX保存是EFFECT;播放主机并运行它,
        // 或者下面的断言是检查没有执行的字迹.
        {
            agtest::fx::Store store;
            store.settings = s;
            agtest::fx::run(cmd3, store);
            s = store.settings;
        }
        check(std::holds_alternative<login::Closed>(m3.ui.login),
              "3: probe success commits (modal closes)");
        check(m3.s.models_loading,
              "3: commit_provider_switch ran (models_loading true)");
        check(m3.d.available_models.empty(),
              "3: commit_provider_switch cleared available_models");
        // 无键主机作为已保存行(空键)一直存在.
        check(s.provider_keys.count("localhost:8080") == 1,
              "3: keyless host persisted into provider_keys");
        (void)cmd; (void)cmd3;
    }

    // ── 案例4: 探测器FAILURE 返回输入, 并恢复光谱 ──
    {
        agentty::store::Settings s;
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;   // 记录,作为 init() 种子,种子,种子
        m.ui.login = login::CustomHostInput{.host_input = "http://10.0.0.5:5000/custom"};
        auto [m1, cmd1] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        auto* hp = std::get_if<login::HostProbing>(&m1.ui.login);
        check(hp != nullptr, "4: http:// host enters HostProbing");
        agentty::HostProbed bad;
        bad.attempt_id = hp ? hp->attempt_id : 0;
        bad.spec = "http://10.0.0.5:5000/custom";
        bad.ok = false;
        bad.error = "nothing listening";
        auto [m2, cmd2] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m1), msg::LoginMsg{std::move(bad)});
        auto* ch = std::get_if<login::CustomHostInput>(&m2.ui.login);
        check(ch != nullptr,
              "4: probe failure returns to the host input");
        check(ch && ch->host_input == "http://10.0.0.5:5000/custom",
              "4: typed spec restored for re-editing");
        check(!m2.s.models_loading, "4: no commit on failure");
        // 投放STALE探测结果(错误尝试id).
        agentty::HostProbed stale;
        stale.attempt_id = 9999;
        stale.ok = true;
        auto [m3, cmd3] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m2), msg::LoginMsg{std::move(stale)});
        check(std::holds_alternative<login::CustomHostInput>(m3.ui.login),
              "4: stale probe result is a no-op");
        (void)cmd1; (void)cmd2; (void)cmd3;
    }

    // ── 案例5: Esc 键位 提示词 → 取消 整个开关 ──
    {
        agentty::store::Settings s;
        s.provider_keys["https://chat.example.org/api"] = "sk-test-key";
        install_stub_deps(s);
        agentty::Model m;
        m.d.persisted = s;   // 记录,作为 init() 种子,种子,种子
        // 主持人 已保存 关键土地 ApiKeyInput.
        m.ui.login = login::CustomHostInput{.host_input = "https://chat.example.org/api"};
        auto [m1, cmd1] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginSubmit{}});
        check(std::holds_alternative<login::ApiKeyInput>(m1.ui.login),
              "5: setup — host with saved key entered ApiKeyInput");
        // 现在按 Esc (CloseLogin).
        auto [m2, cmd2] = ::agentty::app::detail::step(::agentty::app::detail::login_update, std::move(m1), msg::LoginMsg{agentty::CloseLogin{}});
        check(std::holds_alternative<login::Closed>(m2.ui.login),
              "5: Esc at key prompt closes the modal");
        check(!m2.s.models_loading,
              "5: no commit happened (models_loading false)");
        (void)cmd1; (void)cmd2;
    }

    // 清理临时的XDG dir.
    std::error_code ec;
    fs::remove_all(tmp_xdg, ec);
}
