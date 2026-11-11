// account_switch_refresh_test ——换成长篇. -idle
// 账户不能让你持有 EXPIRED 符号。
//
// 同一供应商的两个账户( Anthropic :如果你切换到一个已经闲置了
// 足够长的时间,以至于其OAuth访问令牌失效,则 account_select
// 状态更新函数 必须在背景符号刷新时踢出( S) oauth_refresh_in_flight
// )使用正义 -swapped 商店的 refresh_token
// ——而不是安装 过期 无记名逐字记录和401 -ing
// 转换到一个仍然新鲜的账户 不 启动刷新。
//
// 开车经过 真实 app::update 状态更新函数 点 XDG_CONFIG_HOME
// 临时目录中,键盘被禁用,所以 凭据 + 注册表 圆形 -trips 点击密封文件,
// 完全一样 oauth_proactive_refresh_test.
#include "agtest_cap.hpp"
#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/provider/selection.hpp"

#include <chrono>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>   // _getpid
#else
#  include <unistd.h>    // 发作
#endif

using namespace agentty;

namespace {

namespace acc = agentty::auth::accounts;
namespace fs  = std::filesystem;
namespace login = agentty::ui::login;
namespace msg = agentty::msg;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void save_oauth(std::int64_t expires_at_ms, const std::string& refresh) {
    auth::cred::OAuth o;
    o.access_token  = "at-" + refresh;
    o.refresh_token = refresh;
    o.expires_at_ms = expires_at_ms;
    (void)auth::save_credentials(::agentty::IoAccess::grant(), auth::Credentials{std::move(o)});
}

// 将此进程配置根点为 FRESH 临时 dir。
//
// 独一性必须活过两件事:(a) 在同一毫秒内开始的兄弟姐妹试验过程。
// `ctest -j` (赤色时间戳碰撞,两个过程共用 a 凭据 存储
// 失败 对方的断言非 -deterministically
// )和(b) 多重 CASES 在一个 二进制程序 每个人想要一个干净的
// 注册表 ,因为以前案件遗留的账户改变了什么 "the outgoing account"。
// 所以:时间戳+pid+每 -call 计数器,
// 没有 `static done` 锁锁.
void isolate_config_dir() {
    static std::atomic<int> seq{0};
    auto dir = fs::temp_directory_path()
             / ("agentty_acctsw_" + std::to_string(now_ms())
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
    _putenv_s("AGENTTY_USE_KEYSTORE", "0");
#else
    ::setenv("AGENTTY_HOME", dir.string().c_str(), 1);
    ::setenv("AGENTTY_USE_KEYSTORE", "0", 1);
#endif
}

store::Settings g_settings;
void install_stub_deps() {
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

// 登记两个人类账户(A = 新鲜账户,B = 长账户) -expired ,然后离开 A。
// 返回 AccountList 与光标在前面 -placed 打开 `on_label`.
ui::login::AccountList setup_two_accounts(const std::string& on_label) {
    // B第一: 抓长 -EXPIRED 代理账户 "B".
    save_oauth(/*过期*/ now_ms() - 60 * 60 * 1000, "rt-B");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "B");
    // 第二: 一种仍然新鲜、活跃的标志。
    save_oauth(/*过期*/ now_ms() + 60 * 60 * 1000, "rt-A");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "A");

    ui::login::AccountList al;
    al.provider       = "anthropic";
    al.provider_label = "Anthropic";
    int want = 0;
    for (const auto& a : acc::list_for(::agentty::IoAccess::grant(), "anthropic")) {
        ui::login::AccountRow row;
        row.provider = "anthropic";
        row.label    = a.label;
        row.active   = (acc::active_label(::agentty::IoAccess::grant(), "anthropic") == a.label);
        if (a.label == on_label) want = static_cast<int>(al.rows.size());
        al.rows.push_back(std::move(row));
    }
    al.cursor = want;
    return al;
}

} // 命名空间结束。

TEST_CASE("account switch refreshes a stale token") {
    isolate_config_dir();
    install_stub_deps();
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // ── 切换到长-idle账户 B – 踢出一个背景刷新 ──
    {
        Model m;
        m.ui.login = setup_two_accounts("B");
        auto [m2, cmd] = app::update(std::move(m),
                                     Msg{msg::LoginMsg{agentty::AccountSelect{}}});
        agtest::fx::run_credentials(cmd);   // 接下来主机会做什么

        CHECK(m2.s.oauth_refresh_in_flight,
              "switching to a long-idle account with an expired token kicks "
              "a background refresh instead of installing the stale bearer");
        CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "B",
              "the switch still activates account B");
        CHECK(std::holds_alternative<ui::login::Closed>(m2.ui.login),
              "the account picker closes after the switch");
    }

    // ── 切换回新账户 A ~ NO 刷新 ──
    {
        acc::activate(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "B");   // 使 A 成为不活动目标
        Model m;
        m.ui.login = setup_two_accounts("A");
        auto [m2, cmd] = app::update(std::move(m),
                                     Msg{msg::LoginMsg{agentty::AccountSelect{}}});
        agtest::fx::run_credentials(cmd);   // 接下来主机会做什么

        CHECK(!m2.s.oauth_refresh_in_flight,
              "switching to an account whose token is still fresh does NOT "
              "trigger a refresh");
        CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "A", "switch activated A");
    }
}

TEST_CASE("account switch invalidates the outgoing account's catalog") {
    // 应享权利 ACCOUNT-scoped : Pro和
    // 最大 事件订阅 服务不同的模式列表。 目录 手持的被拿来
    // 作为 OUTGOING 账户,所以在切换后它就错了。
    // 不无效,则在 ^/ 状态更新函数 跳过反切和 选择面板
    // 浏览 PREVIOUS 账户的模型。
    isolate_config_dir();
    install_stub_deps();
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    Model m;
    m.ui.login = setup_two_accounts("B");

    // A 目录 刚刚被取走的 -- 击败了 TTL 守护。
    // 动画时钟从0开始测试,所以明确标记它;
    // 任何非零值都是 "fetched" 至于新鲜
    // 度检查
    ProviderCatalog cat;
    cat.provider_id  = "anthropic";
    cat.label        = "Anthropic";
    cat.state        = ProviderCatalog::State::Ready;
    cat.loaded_at_ms = ::maya::anim_now_ms() + 1;
    m.d.provider_catalogs.push_back(std::move(cat));
    REQUIRE(m.d.provider_catalogs.front().loaded_at_ms != 0);

    auto [m2, cmd] = app::update(std::move(m),
                                 Msg{msg::LoginMsg{agentty::AccountSelect{}}});
    agtest::fx::run_credentials(cmd);

    REQUIRE(m2.d.provider_catalogs.size() == 1);
    CHECK(m2.d.provider_catalogs.front().loaded_at_ms == 0,
          "the switched-from catalog is marked stale so ^/ refetches it "
          "for the account that is now active");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "B");
}

TEST_CASE("re-login reuses the derived-label slot, no proliferation") {
    isolate_config_dir();
    install_stub_deps();
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // 从干净的人类学注册表开始: 放下任何一个先前案例剩下
    // 的空档( 这个测试坚持精确的计数)。
    for (const auto& a : acc::list_for(::agentty::IoAccess::grant(), "anthropic"))
        acc::remove(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", a.label);
    REQUIRE(acc::list_for(::agentty::IoAccess::grant(), "anthropic").empty());

    // 第一次 OAuth 登录 ~ 1 "OAuth login" 插槽( 从“ ” 中生成的标签) 凭据 ; OAuth 中
    // 没有 -account 身份). 互联网档案馆的存檔,存档日期2013-09-02..
    save_oauth(now_ms() + 3'600'000, "rt-1");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", acc::derive_current_label(::agentty::IoAccess::grant(), "anthropic"));
    REQUIRE(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 1);
    CHECK(acc::derive_current_label(::agentty::IoAccess::grant(), "anthropic") == "OAuth login");
    const std::string first_secret =
        acc::get(::agentty::IoAccess::grant(), "anthropic", "OAuth login")->secret;

    // 调 -login 三个 更多次——过期 -token 刷新,
    // 或者重新来过 -testing。每个 相同 "OAuth login" 标签,
    // 所以固定( snapshot_active 由(提供方,标签)提供)
    // UPDATES 预选档 -fix 状态更新函数 后缀 "OAuth login 2/3/4" ,
    // 报告累积。
    for (int i = 2; i <= 4; ++i) {
        save_oauth(now_ms() + 3'600'000, "rt-" + std::to_string(i));
        acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", acc::derive_current_label(::agentty::IoAccess::grant(), "anthropic"));
        CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 1,
              "re-login must not create a new slot");
    }

    // 单位现在有一个 不同 (新编). 凭据 而不是第一个
    // 登录的——这个秘密是密封的blob,所以比较一下
    // 字节 而不是寻找一个简单的符号。
    auto slot = acc::get(::agentty::IoAccess::grant(), "anthropic", "OAuth login");
    REQUIRE(slot.has_value());
    CHECK(slot->secret != first_secret,
          "the reused slot carries the most recent login's credential");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "OAuth login", "reused slot is active");

    // 一个DELIBERATE的第二个账户(显式标签)仍然共存
    // ——标签上的再利用规则键,因此不同的标签永远不会碰撞.
    save_oauth(now_ms() + 3'600'000, "rt-work");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "work laptop");
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 2,
          "a distinctly-labelled account is a separate slot");
}

// ── 调试 -rotation 跨开关的安全性 ────────────────────────────────
//
// Anthropic 在每个刷新时旋转刷新符牌, 并取消旧的
// 服务器 -side。该词 注册表 账户的槽是 SNAPSHOT
// 如果现场商店在之后刷新 快照 ,插槽是 过期。固定点:
// activate() 内容 -snapshots 编号 OUTGOING 账户
// 的插槽在覆盖前从直播商店中打开, 所以在后面切换会呈现最新鲜的( 仍然)
// -valid () 刷新符号—— 永远不要登录 -day 一个
TEST_CASE("switch A→B→A returns A's FRESHEST token, not the login-day one") {
    isolate_config_dir();
    install_stub_deps();

    // 登录为 A( r) -A-v1), 快照 ,然后模拟背景刷新 ROTATED
    // A在现场商店的代号(r) -A-v2 —— —— . 注册表 插槽
    // 仍然保留v1, 确切的 staleness 窗口。
    save_oauth(now_ms() + 3'600'000, "rt-A-v1");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "A");
    save_oauth(now_ms() + 3'600'000, "rt-A-v2");   // 轮调员额 -snapshot

    // 登记为B并登记。
    // (在应用程序中,登录) 保存 那店里就卖了 快照 ——这里的顺序相同.
    // ) 通过激活切换 A_B( ) "B" )必须 首次 内容 -snapshot
    // 现场商店的A(抓V2), THEN 安装 B.
    {
        // 登记B号从单独的实况凭据。
        auto keep_a = auth::load_credentials(::agentty::IoAccess::grant());
        REQUIRE(keep_a.has_value());
        save_oauth(now_ms() + 3'600'000, "rt-B-v1");
        acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "B");
        // 恢复 A 直播( A 一直作为活动账户)。
        auth::save_credentials(::agentty::IoAccess::grant(), *keep_a);
        acc::set_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "A");
    }

    REQUIRE(acc::activate(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "B"));
    // 现场商店现在载着B.
    {
        auto c = auth::load_credentials(::agentty::IoAccess::grant());
        REQUIRE(c.has_value());
        auto* o = std::get_if<auth::cred::OAuth>(&*c);
        REQUIRE(o != nullptr);
        CHECK(o->refresh_token == "rt-B-v1");
    }

    // 切换回A: 符号必须是v2(旋转的), 因为 activate() 内容
    // -snapshotted 来了 OUT 预览 -fix 这是v1——服务器
    // -invalidated ,所以每次刷新都失败,直到恢复 -login.
    REQUIRE(acc::activate(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "A"));
    {
        auto c = auth::load_credentials(::agentty::IoAccess::grant());
        REQUIRE(c.has_value());
        auto* o = std::get_if<auth::cred::OAuth>(&*c);
        REQUIRE(o != nullptr);
        CHECK(o->refresh_token == "rt-A-v2");
    }
}

TEST_CASE("activate to the SAME label does not self-snapshot-clobber") {
    isolate_config_dir();
    install_stub_deps();

    save_oauth(now_ms() + 3'600'000, "rt-same-v1");
    acc::snapshot_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "solo");
    // 直播店旋转...
    save_oauth(now_ms() + 3'600'000, "rt-same-v2");

    // 调 -activating 已经 -active 标签必须安装
    // SLOT 抄袭(v1)——出品的 ⁇ 警队跳过出品. 快照 ,
    // 所以这是真的 "restore from registry" 而不是
    // 拒绝 -op ——不先将v2提升到槽中(这样可以使恢复 -from-
    // 注册表 无法完成).
    REQUIRE(acc::activate(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "solo"));
    auto c = auth::load_credentials(::agentty::IoAccess::grant());
    REQUIRE(c.has_value());
    auto* o = std::get_if<auth::cred::OAuth>(&*c);
    REQUIRE(o != nullptr);
    CHECK(o->refresh_token == "rt-same-v1");
}
