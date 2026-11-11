// foreign_open_test ——继续a Codex / Claude 代码会话从 ^J.
//
//   1. 在磁盘显示中发现的与agentty的线程相邻的选择面板行,
//      最新, 并且是本地人重新装入的。
//   2. Enter 进口的:a 已保存 线程 轮到会议了,
//      每次都一样, 排就不再是占位符了.
//   3. 关于 -opening 在另一个工具中多说了之后 -reads 继续
//      转弯在进口转弯后,以agentty加起来。
//   4. "continued in agentty" 接口边界号在第一个agentty号轮机之前。
//   5. 关闭引爆装置后,会隐藏各行。

#include "agentty/foreign/sessions.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/panel/thread_filter.hpp"
#include "agentty/runtime/view/thread/seam.hpp"
#include "agtest.hpp"
#include "agtest_fx.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pn = agentty::ui::panel;
namespace detail = agentty::app::detail;
namespace fg = agentty::foreign;
using namespace agentty;

namespace {

std::optional<Thread> g_stored;          // load_thread 返回什么
std::vector<Thread>   g_saved;           // 什麽 open_foreign 已保存

void install_stub_deps() {
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread& t) { g_saved.push_back(t); },
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return g_stored; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"fresh"}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .auth          = {},
    });
}

std::string codex_session(const char* id, int turns) {
    std::string s = std::string{R"({"timestamp":"2026-07-27T13:00:00.000Z","type":"session_meta","payload":{"id":")"}
                  + id + R"(","cwd":"/proj","originator":"codex-tui"}})" + "\n";
    for (int i = 0; i < turns; ++i) {
        s += R"({"timestamp":"2026-07-27T13:00:0)" + std::to_string(i + 1)
           + R"(.000Z","type":"response_item","payload":{"type":"message","role":"user","content":[{"type":"input_text","text":"q)"
           + std::to_string(i) + R"("}]}})" + "\n";
        s += R"({"timestamp":"2026-07-27T13:00:0)" + std::to_string(i + 1)
           + R"(.500Z","type":"response_item","payload":{"type":"message","role":"assistant","content":[{"type":"output_text","text":"a)"
           + std::to_string(i) + R"("}]}})" + "\n";
    }
    return s;
}

fs::path write_session(const fs::path& home, const std::string& body) {
    const fs::path dir = home / ".codex" / "sessions" / "2026" / "07" / "27";
    fs::create_directories(dir);
    const fs::path f = dir / "rollout-2026-07-27T13-00-00-s1.jsonl";
    std::ofstream(f, std::ios::binary) << body;
    return f;
}

// 开一 线程-list 消息,运行它返回内置的任务,
// 并以同样的方式反馈结果。
void drive(Model& m, msg::ThreadListMsg msg) {
    Cmd c = detail::thread_list_update(m, std::move(msg));
    for (int round = 0; round < 4; ++round) {
        auto out = agtest::fx::run_tasks(std::move(c));
        c = Cmd::none();
        bool any = false;
        for (auto& o : out)
            if (auto* tl = std::get_if<msg::ThreadListMsg>(&o)) {
                c = Cmd::batch(std::move(c), detail::thread_list_update(m, std::move(*tl)));
                any = true;
            }
        if (!any) return;
    }
}

struct HomeGuard {
    std::string saved_home, saved_codex;
    bool had_home = false, had_codex = false;
    fs::path dir;
    explicit HomeGuard(const char* leaf) {
        if (const char* h = std::getenv("HOME")) { had_home = true; saved_home = h; }
        if (const char* c = std::getenv("CODEX_HOME")) { had_codex = true; saved_codex = c; }
        dir = fs::temp_directory_path() / leaf;
        fs::remove_all(dir);
        fs::create_directories(dir);
        ::setenv("HOME", dir.string().c_str(), 1);
        ::unsetenv("CODEX_HOME");
        ::setenv("CLAUDE_CONFIG_DIR", (dir / "no-claude").string().c_str(), 1);
    }
    ~HomeGuard() {
        if (had_home) ::setenv("HOME", saved_home.c_str(), 1); else ::unsetenv("HOME");
        if (had_codex) ::setenv("CODEX_HOME", saved_codex.c_str(), 1);
        ::unsetenv("CLAUDE_CONFIG_DIR");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // 命名空间结束。

TEST_CASE("foreign sessions: listed in ^J, opened, refreshed, and hideable") {
    install_stub_deps();
    HomeGuard home{"agentty_foreign_open"};
    const fs::path file = write_session(home.dir, codex_session("s1", 2));

    Model m;
    m.d.current.id = ThreadId{"native-current"};
    Thread native; native.id = ThreadId{"native-old"}; native.title = "older native";
    native.updated_at = std::chrono::system_clock::time_point{};   // 1970: oldest
    m.d.threads = {native};

    // 1. 开幕 选择面板 扫描其他工具的会话。
    drive(m, OpenThreadList{});
    REQUIRE(m.d.foreign.sessions.size() == 1);
    const ThreadId sid = fg::thread_id_for(fg::Source::Codex, "s1");
    auto row = std::ranges::find(m.d.threads, sid, &Thread::id);
    REQUIRE(row != m.d.threads.end());
    CHECK(row->foreign_only);
    CHECK(m.d.threads.front().id == sid, "newer than the native thread, so it sorts first");

    // 本地重装保留了外国的排位.
    drive(m, ThreadsLoaded{{native}});
    CHECK(std::ranges::find(m.d.threads, sid, &Thread::id) != m.d.threads.end());

    // Tab 周期 全部 —— Codex & Claude □ 全部(无) 线程 在此文件夹中,
    // 从而跳过此步骤 ; Shift+Tab 回去吧 继续
    {
        auto* pp = m.ui.panel.get<pn::ThreadList>();
        REQUIRE(pp != nullptr);
        drive(m, ThreadListScopeToggle{+1});
        CHECK(pp->only_foreign);
        const auto only = ui::visible_threads(m.d.threads, "", "", true, true);
        REQUIRE(only.size() == 1);
        CHECK(m.d.threads[static_cast<std::size_t>(only[0])].id == sid);
        drive(m, ThreadListScopeToggle{+1});
        CHECK(!pp->only_foreign);
        CHECK(pp->all_workspaces);
        drive(m, ThreadListScopeToggle{-1});
        CHECK(pp->only_foreign, "Shift+Tab steps back");
        drive(m, ThreadListScopeToggle{-1});
        CHECK(!pp->only_foreign);
        // 跳跃至下而复至顶.
        drive(m, ThreadListJump{ThreadListJump::Where::End});
        CHECK(pp->index == 1);
        drive(m, ThreadListJump{ThreadListJump::Where::Home});
        CHECK(pp->index == 0);
    }

    // 2. Enter 进口的
    auto* p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    p->index = 0;
    g_stored.reset();
    g_saved.clear();
    drive(m, ThreadListSelect{});
    CHECK(m.d.current.id == sid);
    CHECK(m.d.current.messages.size() == 4);
    REQUIRE(m.d.current.imported.has_value());
    CHECK(m.d.current.imported->messages == 4);
    CHECK(g_saved.size() == 1, "the import is saved as an agentty thread");
    row = std::ranges::find(m.d.threads, sid, &Thread::id);
    REQUIRE(row != m.d.threads.end());
    CHECK(!row->foreign_only, "the row is a real thread now");

    // 4. 无 接口边界 一直持续到 agentty 增加转弯;然后它就坐落在球场上。
    CHECK(ui::import_seam_index(m.d.current) == SIZE_MAX);
    Thread mine = m.d.current;
    Message u; u.role = Role::User; u.text = "mine";
    Message a; a.role = Role::Assistant; a.text = "agentty reply";
    mine.messages.push_back(u);
    mine.messages.push_back(a);
    CHECK(ui::import_seam_index(mine) == 4);

    // 3. 更多内容载于 Codex 导入后: re -read ,保留 agentty 转弯了
    std::ofstream(file, std::ios::binary | std::ios::trunc) << codex_session("s1", 3);
    g_stored = mine;               // 磁盘上有什么 线程
    g_saved.clear();
    m.d.current = Thread{};        // 用户转到另一个 线程
    m.d.current.id = ThreadId{"elsewhere"};
    m.d.threads = {persistence::thread_header(mine)};
    drive(m, OpenThreadList{});     // 重新扫描; 会话现在更大了
    p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    p->index = 0;
    drive(m, ThreadListSelect{});
    REQUIRE(m.d.current.id == sid);
    REQUIRE(m.d.current.messages.size() == 8, "6 from Codex + 2 from agentty");
    CHECK(m.d.current.messages[5].text == "a2", "Codex's new turn is in");
    CHECK(m.d.current.messages[6].text == "mine", "agentty's turns follow");
    CHECK(m.d.current.imported->messages == 6);
    CHECK(ui::import_seam_index(m.d.current) == 6);

    // 5. 外观:占位者行走。
    Thread other = fg::to_thread(fg::Source::Codex, fg::Parsed{.session_id = "other"},
                                 "/x.jsonl", 1, 1);
    other.foreign_only = true;
    m.d.foreign.sessions.push_back(other);
    m.d.ui().foreign_sessions = false;
    drive(m, ThreadsLoaded{{}});
    for (const auto& t : m.d.threads) CHECK(!t.foreign_only);
}
