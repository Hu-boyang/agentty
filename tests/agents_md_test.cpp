// agents_md_test — 锁 编号 wire::agents_md_block 读取
// AAIF-stewarded AGENTS.md 标准文件( E) https://agents.md)
// 并把它包在一块专注的 <agents-md> 系统 -prompt 块,
// 从现有的 CLAUDE.md <memory> 块。
//
// 根据公布的谱系, AGENTS.md 是项目 -scoped 仅( 没有用户级,
// 没有本地级)。 根文件位于 <workspace_root>/AGENTS.md 上限为
// 64 KiB 由共享的 wire::read_capped_file 输油管。
//
// 光谱还描述了嵌入式单列波文件:
// 每个包放置 AGENTS.md，代理读取目录树中距离最近的文件，并以它为优先。
// 何时 search_from (毒剂的阴茎 夹在内部) 工作区 )是 .
// . ) 的一个子目录,其内容为 workspace_root
// 帮手上楼找一秒钟 AGENTS.md 以白银和白银,分别发售,
// <agents-md-package> 块。
//
// 锁: 百度百科全书 互联网档案馆的存檔,存档日期2013-09-02.
//   • 缺少文件 —— 空块(从提示词中删除)
//   • 提交文件 + 包裹为 <agents-md>…</agents-md> 带内线
//   • >64 KiB 正文 折减为64 KiB(上限与CLAUDE.md相同)
//   • 从workspace_root()中解决路径,而不是原始过程 cwd,
//     这么说吧 --workspace 超音速被尊重 !
//   • 将AGENTS.md 放在第<agents-md-package>号排放的子包中
//   • 何时没有嵌入文件 search_from == workspace_root
//   • 在工作区边界上步行停留(从未在外侧读)
//   • 巢文件也上限为64 KiB
//   • 解开:与根相同峡谷路径 —— 无第二块
//   • 全球 AGENTS.md (~/.agentty/ 或 或 时 间 ~/.agents/) → <agents-md-global> 块
//   • ~/.agentty/AGENTS.md 赢过 ~/.agents/AGENTS.md
//   • 全球 -only (无根文件) ——只 <agents-md-global>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>   // ⁇ (POSIX)

#include "agentty/provider/msg_shared.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

namespace fs = std::filesystem;
namespace wire = agentty::provider::wire;
using agentty::tools::util::project_root;
using agentty::tools::util::set_workspace_root;
using agentty::tools::util::workspace_root;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// 删除测试 源码树 ,先离开它: Windows 拒绝
// 删除属于任何进程当前目录的目录,并且将大小写 chdir
// 输入到它们的目录中 工作区 . 永不抛弃——清理不得杀跑.
static void rm_tree(const fs::path& p) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec && !p.empty()) {
        const auto rel = cwd.lexically_relative(p);
        if (!rel.empty() && *rel.begin() != "..")
            fs::current_path(fs::temp_directory_path(), ec);
    }
    fs::remove_all(p, ec);
}

static fs::path make_workspace() {
    const fs::path tmp =
        fs::temp_directory_path() / "agentty_agents_md_test_workspace";
    rm_tree(tmp);
    fs::create_directories(tmp);
    return tmp;
}

static void write_file(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << std::string(content);
}

static void test_missing_returns_empty() {
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    const std::string r = wire::agents_md_block("intro", workspace_root());
    CHECK(r.empty());
    rm_tree(ws);
}

static void test_wraps_content() {
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md",
               "## Build\n- cmake --build build\n## Tests\n- ctest\n");
    const std::string r =
        wire::agents_md_block("Standard project guidance.", workspace_root());
    CHECK(!r.empty());
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("</agents-md>") != std::string::npos);
    CHECK(r.find("Standard project guidance.") != std::string::npos);
    CHECK(r.find("## Build") != std::string::npos);
    CHECK(r.find("cmake --build build") != std::string::npos);
    CHECK(r.find("## Tests") != std::string::npos);
    CHECK(r.find("ctest") != std::string::npos);
    // 没有嵌入式文件 ~ 没有 <agents-md-package> 块。
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    rm_tree(ws);
}

static void test_truncates_at_cap() {
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    // 64 KiB+1000 字节——必须剪到第wire::read_capped_file
    // 执行的64 KiB(与第CLAUDE.md相同的上限)的上限.
    std::string huge(64u * 1024u + 1000u, 'x');
    write_file(ws / "AGENTS.md", huge);
    // 使用一个独特的内置标记,这样我们就可以隔离文件
    // -content部分(Intro +内容+关闭标记
    // 一起会增加所测量的盖子过宽,掩盖盖子是否被应用).
    // 帮助者插入的正则 '\n' 介于内语和内容之间,
    // 所以内语 无需 线索 '\n' 使抵消的算术干净。
    const std::string intro = "INTRO_MARKER_FOR_TRUNCATION";
    const std::string r = wire::agents_md_block(intro, workspace_root());
    const auto intro_pos = r.find(intro);
    const auto close_pos = r.find("\n</agents-md>");
    CHECK(intro_pos != std::string::npos);
    CHECK(close_pos != std::string::npos);
    // +1 跳过单 '\n' 帮助器在内语和内容之间插入.
    const auto content_start = intro_pos + intro.size() + 1;
    const auto body = r.substr(content_start, close_pos - content_start);
    // 盖子,加上短的标记 短的插件。
    CHECK(body.size() <= 64u * 1024u + 128u);

    // 和它一样 SAYS 被切伤了 AGENTS.md 说明书,
    // 读者是模型 所以无声剪断意味着 写在底部的一条规则...
    // 未经询问，不要删除文件。
    // 这正是人们放的地方 - - 从来没有到达, 而作者看着他们的
    // 档案顶端 副作用 并合理地假设 整个事情做到了。
    CHECK(body.find("NOT read") != std::string::npos);
    rm_tree(ws);
}

static void test_uses_project_root_not_process_cwd() {
    // 设定 workspace_root ,但 chdir 变成 "other"
    // 含有一个 AGENTS.md. project_root ()返回 工作区
    // 根在此( 边界外的 cwd 返回到边界 / mcp-cpp's project_root
    // 合同),所以Ws( 合同) AGENTS.md 读取和 "other"
    // 一个不是.
    const auto ws    = make_workspace();
    const auto other = fs::temp_directory_path() / "agentty_agents_md_test_other";
    rm_tree(other);
    fs::create_directories(other);
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    write_file(ws / "AGENTS.md",    "workspace-root-AGENTS");
    write_file(other / "AGENTS.md", "should-not-be-read");
    // 颜色 OUTSIDE 编号 工作区 边界 project_root () 倒数为ws。
    fs::current_path(other);
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   project_root());
    CHECK(r.find("workspace-root-AGENTS") != std::string::npos);
    CHECK(r.find("should-not-be-read") == std::string::npos);
    // search_from 外面是 门 工作区 迪尔! root_canon 立即失败( E)
    // project_root ( ) 倒计时 workspace_root 所以没有筑巢区
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    fs::current_path(fs::temp_directory_path());
    rm_tree(ws);
    rm_tree(other);
}

// ── 单列波纹样步行测试 ───────────────────────────────────────────

static void test_nested_overrides_root() {
    // 根号 AGENTS.md + 子包件中嵌入一个. search_from 是子包目录。
    // 帮助者应发出 两者 块: 根 <agents-md> 筑巢,筑巢,
    // <agents-md-package>.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root-level guidance");
    const auto pkg = ws / "packages" / "auth";
    fs::create_directories(pkg);
    write_file(pkg / "AGENTS.md", "auth-package guidance");
    // search_from = 子包装目录(模拟在那里工作的代理)
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   pkg);
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root-level guidance") != std::string::npos);
    CHECK(r.find("<agents-md-package>") != std::string::npos);
    CHECK(r.find("auth-package guidance") != std::string::npos);
    CHECK(r.find("</agents-md-package>") != std::string::npos);
    rm_tree(ws);
}

static void test_no_nested_when_at_root() {
    // search_from == workspace_root 没有筑巢区,只有根
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root-only guidance");
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   workspace_root());
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root-only guidance") != std::string::npos);
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    rm_tree(ws);
}

static void test_walk_stops_at_workspace_boundary() {
    // 工作区以外的AGENTS.md永远不应被读取,
    // 即使search_from深处位于工作区,步行经过靠近外部的
    // AGENTS.md.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root guidance");
    // 创建嵌入式目录, 但其中没有 AGENTS.md。
    const auto pkg = ws / "packages" / "auth";
    fs::create_directories(pkg);
    // search_from =深层子包;走到ws,发现没有巢 AGENTS.md 停止。
    // 没有 <agents-md-package> 块。
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   pkg);
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root guidance") != std::string::npos);
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    rm_tree(ws);
}

static void test_nested_truncates_at_cap() {
    // 被粘起来 AGENTS.md 上限为64 KiB.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root");
    const auto pkg = ws / "packages" / "auth";
    fs::create_directories(pkg);
    std::string huge(64u * 1024u + 1000u, 'x');
    write_file(pkg / "AGENTS.md", huge);
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   pkg);
    CHECK(r.find("<agents-md-package>") != std::string::npos);
    // 查找嵌套块内容并验证其封顶.
    const auto pkg_start = r.find("<agents-md-package>");
    const auto pkg_end   = r.find("</agents-md-package>");
    CHECK(pkg_start != std::string::npos);
    CHECK(pkg_end   != std::string::npos);
    // 块内容 = 打开标记线和关闭标记之间的全部内容。
    // 打开标记包括一个内线 + '\n', 然后是文件内容。
    // 我们只是验证块的大小不大于盖子 + 顶部。
    const auto block_size = pkg_end - pkg_start;
    // 64 KiB 内容+~300 字节 用于标记+内线。
    CHECK(block_size <= 64u * 1024u + 500u);
    rm_tree(ws);
}

static void test_dedup_when_same_file() {
    // 何时 search_from 决定 workspace_root (同样是条条条条),
    // 走着就会找到同样的东西 AGENTS.md 作为根文件。解密检查( R)
    // candidate_canon != root_agents )防止重复块。
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "only-one-file");
    // search_from 是一个子目录,它有 不 有自己的东西 AGENTS.md ,
    // 所以行走到达 workspace_root 和循环条件 dir !
    // = root_canon 终止它,没有筑巢区
    const auto sub = ws / "src";
    fs::create_directories(sub);
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   sub);
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("only-one-file") != std::string::npos);
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    rm_tree(ws);
}

static void test_nearest_wins_not_deepest() {
    // 多个巢 AGENTS.md 中的文件。行走会找到 NEAREST
    // 单曲( 关闭到 search_from ),不是最深的或根部.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root guidance");
    fs::create_directories(ws / "packages");
    write_file(ws / "packages" / "AGENTS.md", "packages-level guidance");
    const auto auth = ws / "packages" / "auth";
    fs::create_directories(auth);
    write_file(auth / "AGENTS.md", "auth-level guidance");
    // search_from = auth/src - 走起来: auth/src ∗ 认证(已) AGENTS.md - 停下来!
    const auto deep = auth / "src";
    fs::create_directories(deep);
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   deep);
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root guidance") != std::string::npos);
    CHECK(r.find("<agents-md-package>") != std::string::npos);
    // 最接近的就是认证 -level, 不 软件包 -level.
    CHECK(r.find("auth-level guidance") != std::string::npos);
    CHECK(r.find("packages-level guidance") == std::string::npos);
    rm_tree(ws);
}

// ── 全球范围测试 ───────────────────────────────────────────────────

static void test_global_agents_md() {
    // 全球 AGENTS.md 明确通过 global_path → <agents-md-global> 块。
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root guidance");
    // 在临时目录中创建一个假全局文件(模拟 ~/.agentty/AGENTS.md)。
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home / ".agentty");
    const auto global_file = fake_home / ".agentty" / "AGENTS.md";
    write_file(global_file, "global-level guidance");
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   workspace_root(),
                                                   global_file);
    CHECK(r.find("<agents-md-global>") != std::string::npos);
    CHECK(r.find("global-level guidance") != std::string::npos);
    CHECK(r.find("</agents-md-global>") != std::string::npos);
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root guidance") != std::string::npos);
    // 环球块应该来了 之前 根块。
    CHECK(r.find("<agents-md-global>") < r.find("<agents-md>"));
    rm_tree(ws);
    rm_tree(fake_home);
}

static void test_global_only_no_root() {
    // 全球存在但根 AGENTS.md 缺少 ~ 只缺少 ~ <agents-md-global>.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    // 没有 AGENTS.md 时间 工作区 根节。
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home / ".agentty");
    const auto global_file = fake_home / ".agentty" / "AGENTS.md";
    write_file(global_file, "global-only guidance");
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   workspace_root(),
                                                   global_file);
    CHECK(r.find("<agents-md-global>") != std::string::npos);
    CHECK(r.find("global-only guidance") != std::string::npos);
    CHECK(r.find("<agents-md>") == std::string::npos);  // 无根块
    CHECK(r.find("<agents-md-package>") == std::string::npos);
    rm_tree(ws);
    rm_tree(fake_home);
}

static void test_no_global_when_not_provided() {
    // global_path 空 —— 没有 <agents-md-global> 块。
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root guidance");
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   workspace_root());
    CHECK(r.find("<agents-md>") != std::string::npos);
    CHECK(r.find("root guidance") != std::string::npos);
    CHECK(r.find("<agents-md-global>") == std::string::npos);
    rm_tree(ws);
}

static void test_global_truncated_at_cap() {
    // 全球 AGENTS.md 上限为64 KiB.
    const auto ws = make_workspace();
    set_workspace_root(::agentty::IoAccess::grant(), ws);
    fs::current_path(ws);
    write_file(ws / "AGENTS.md", "root");
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home / ".agentty");
    const auto global_file = fake_home / ".agentty" / "AGENTS.md";
    std::string huge(64u * 1024u + 1000u, 'x');
    write_file(global_file, huge);
    const std::string r = wire::agents_md_block("intro", workspace_root(),
                                                   workspace_root(),
                                                   global_file);
    const auto glb_start = r.find("<agents-md-global>");
    const auto glb_end   = r.find("</agents-md-global>");
    CHECK(glb_start != std::string::npos);
    CHECK(glb_end   != std::string::npos);
    const auto block_size = glb_end - glb_start;
    // 64 KiB 内容+~300 字节 用于标记+内线。
    CHECK(block_size <= 64u * 1024u + 500u);
    rm_tree(ws);
    rm_tree(fake_home);
}

static void test_resolve_global_agentty_wins() {
    // resolve_global_agents_md(): ~/.agentty/AGENTS.md 赢过 ~/.agents/AGENTS.md.
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home / ".agentty");
    fs::create_directories(fake_home / ".agents");
    write_file(fake_home / ".agentty" / "AGENTS.md", "agentty-global");
    write_file(fake_home / ".agents"  / "AGENTS.md", "agents-global");
    // 临时设定 HOME 去我们假的家
    //
    // AGENTTY_HOME 也得脱身: user_root ()检查一下
    // 之前 HOME ,独立 归并's 分派器 设为每架 -run 沙箱 所以折叠
    // 的测试不能写成真实的 ~/.agentty. 有了这个测试,就是假的 HOME
    // 被忽略,并且 resolve_global_agents_md (看在
    // 那个 沙箱 ,没有文件。
    const char* orig_home = std::getenv("HOME");
    const char* orig_agentty_home = std::getenv("AGENTTY_HOME");
    const std::string saved_agentty_home = orig_agentty_home ? orig_agentty_home : "";
    unsetenv("AGENTTY_HOME");
    setenv("HOME", fake_home.string().c_str(), 1);
    const auto resolved = wire::resolve_global_agents_md();
    CHECK(!resolved.empty());
    // 应解决~/.agentty/版本。
    CHECK(resolved.filename() == "AGENTS.md");
    std::string content = wire::read_capped_file(resolved);
    CHECK(content.find("agentty-global") != std::string::npos);
    CHECK(content.find("agents-global") == std::string::npos);
    // 恢复 HOME.
    if (orig_home) setenv("HOME", orig_home, 1);
    else unsetenv("HOME");
    if (!saved_agentty_home.empty())
        setenv("AGENTTY_HOME", saved_agentty_home.c_str(), 1);
    rm_tree(fake_home);
}

static void test_resolve_global_fallback_to_agents_dir() {
    // resolve_global_agents_md (:何时 ~/.agentty/AGENTS.md 失踪,
    // 倒回去 ~/.agents/AGENTS.md.
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home / ".agents");
    write_file(fake_home / ".agents" / "AGENTS.md", "agents-fallback");
    const char* orig_home = std::getenv("HOME");
    setenv("HOME", fake_home.string().c_str(), 1);
    const auto resolved = wire::resolve_global_agents_md();
    CHECK(!resolved.empty());
    std::string content = wire::read_capped_file(resolved);
    CHECK(content.find("agents-fallback") != std::string::npos);
    if (orig_home) setenv("HOME", orig_home, 1);
    else unsetenv("HOME");
    rm_tree(fake_home);
}

static void test_resolve_global_both_missing() {
    // resolve_global_agents_md( ): 当文件不存在 ~ 空路径。
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home);  // 家存在, 但任何地方都没有 AGENTS.md
    const char* orig_home = std::getenv("HOME");
    setenv("HOME", fake_home.string().c_str(), 1);
    const auto resolved = wire::resolve_global_agents_md();
    CHECK(resolved.empty());
    if (orig_home) setenv("HOME", orig_home, 1);
    else unsetenv("HOME");
    rm_tree(fake_home);
}

static void test_resolve_global_codex_fallback() {
    // Codex用户的~/.codex/AGENTS.md(或$CODEX_HOME/
    // AGENTS.md)在不存在agentty文件时读作.
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_codex_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home);
    write_file(fake_home / ".codex" / "AGENTS.md", "codex-global");
    write_file(fake_home / "alt" / "AGENTS.md", "codex-home-env");
    const char* orig_home = std::getenv("HOME");
    const char* orig_ah = std::getenv("AGENTTY_HOME");
    const std::string saved_ah = orig_ah ? orig_ah : "";
    unsetenv("AGENTTY_HOME");
    unsetenv("CODEX_HOME");
    setenv("HOME", fake_home.string().c_str(), 1);
    CHECK(wire::read_capped_file(wire::resolve_global_agents_md()) == "codex-global");
    setenv("CODEX_HOME", (fake_home / "alt").string().c_str(), 1);
    CHECK(wire::read_capped_file(wire::resolve_global_agents_md()) == "codex-home-env");
    unsetenv("CODEX_HOME");
    if (orig_home) setenv("HOME", orig_home, 1);
    else unsetenv("HOME");
    if (orig_ah) setenv("AGENTTY_HOME", saved_ah.c_str(), 1);
    rm_tree(fake_home);
}

static void test_user_claude_md_path() {
    // Claude 代码保存用户文件。 ~/.claude/CLAUDE.md;
    // ~/CLAUDE.md 是那个 回退 用于设置的 agentty 这条老路
    const auto fake_home = fs::temp_directory_path() / ("agentty_test_home_claude_" +
                          std::to_string(::getpid()));
    fs::create_directories(fake_home);
    const char* orig_home = std::getenv("HOME");
    unsetenv("CLAUDE_CONFIG_DIR");
    setenv("HOME", fake_home.string().c_str(), 1);
    CHECK(wire::resolve_user_claude_md() == fake_home / "CLAUDE.md");
    write_file(fake_home / ".claude" / "CLAUDE.md", "claude-user");
    CHECK(wire::resolve_user_claude_md() == fake_home / ".claude" / "CLAUDE.md");
    CHECK(wire::join_project_claude_md("a", "") == "a");
    CHECK(wire::join_project_claude_md("", "b") == "b");
    CHECK(wire::join_project_claude_md("a", "b") == "a\n\nb");
    if (orig_home) setenv("HOME", orig_home, 1);
    else unsetenv("HOME");
    rm_tree(fake_home);
}

int main() {
    test_missing_returns_empty();
    test_wraps_content();
    test_truncates_at_cap();
    test_uses_project_root_not_process_cwd();
    test_nested_overrides_root();
    test_no_nested_when_at_root();
    test_walk_stops_at_workspace_boundary();
    test_nested_truncates_at_cap();
    test_dedup_when_same_file();
    test_nearest_wins_not_deepest();
    test_global_agents_md();
    test_global_only_no_root();
    test_no_global_when_not_provided();
    test_global_truncated_at_cap();
    test_resolve_global_agentty_wins();
    test_resolve_global_fallback_to_agents_dir();
    test_resolve_global_both_missing();
    test_resolve_global_codex_fallback();
    test_user_claude_md_path();
    if (g_failures == 0) {
        std::printf("agents_md_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "agents_md_test: %d check(s) failed\n", g_failures);
    return 1;
}
