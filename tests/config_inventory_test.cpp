// config_inventory_test - 库存是 仅。 声明。
//
// 该 RULE
// ========
// 每一个 scope::Layout 财务报告和已审计财务报表 dirs::Spec 输入
// agentty 已声明为 include/agentty/config/inventory.hpp ,
// 每个消费者都从那里读出来。没有文件可以建立自己的文件。
//
// 这很重要,因为 `agentty config`
// 存在可以告诉用户其配置来自何处。如果程序可以漂移,
// 描述程序的工具就毫无价值。 WRONG 解释比
// 没有更糟糕,因为现在你相信了。 唯一使漂移不可能
// 的方法就是有一个声明,所以命令和特征不能有分歧。
//
// 与方言修补相同: 错误是决定一个的两处决定地点(
// plan() 排放了6个源,a 下游 过滤器读为3),
// 而修补的就是删除第二个站点,而不是记录.
//
// 所以每个建筑都用机械方式 对照源头进行检查

#include "agtest.hpp"

#include "agentty/config/inventory.hpp"
#include "agentty/util/user_root.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include <unistd.h>   // 根可以读任何东西,所以跳过那里

namespace fs = std::filesystem;
namespace cfg = agentty::config;

namespace {

// 允许声明布局或 Spec 的 一个 文件。
const std::vector<std::string> kAllowed = {
    "include/agentty/config/inventory.hpp",
};

// 测试用合成信封进行纯代数的布局是合法地
// 编织出来的——这是范围纯度合同的全点,
// 这些不是真正的存储位置.
[[nodiscard]] bool is_test(const std::string& rel) {
    return rel.rfind("tests/", 0) == 0;
}

[[nodiscard]] bool allowed(const std::string& rel) {
    if (is_test(rel)) return true;
    for (const auto& a : kAllowed)
        if (rel == a) return true;
    return false;
}

[[nodiscard]] fs::path repo_root() {
    // 从cwd上到检查台, 同样的标记规则 dirs 使用。
    std::error_code ec;
    for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "cmake" / "AgenttySources.cmake", ec)) return p;
        if (p == p.parent_path()) break;
    }
    return {};
}

}  // 命名空间结束。

TEST_CASE("every Layout and Spec is declared in the inventory") {
    const fs::path root = repo_root();
    REQUIRE(!root.empty(), "found the checkout root");

    // 保险套 -init 清单以外的任何类型都是第二个申报。
    // `= config::kFoo` 化名是好的——它们只点
    // 出一个声明的名字,而不是复制它——所以这个模式需要一个支架。
    const std::regex decl(R"((scope::Layout|dirs::Spec)\s+\w+\s*\{)");

    std::vector<std::string> offenders;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        const std::string rel = fs::relative(e.path(), root, ec).generic_string();
        if (rel.rfind("build", 0) == 0 || rel.rfind("third_party", 0) == 0)
            continue;
        if (allowed(rel)) continue;

        std::ifstream in(e.path());
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            if (std::regex_search(line, decl))
                offenders.push_back(rel + ":" + std::to_string(n));
        }
    }

    for (const auto& o : offenders)
        MESSAGE("declares its own Layout/Spec: " << o);
    CHECK(offenders.empty(),
          "every Layout/Spec is declared in config/inventory.hpp");
}

// 存货必须与首先发运的存取器商定。如果 kThreadsSpec 解决
// 了除此之外的其他问题,则将使用。 user_threads_dir(),
// `agentty config threads` 将自信地打印出错误
// 的路径——这个整个设计所建的精确的失败,以预防.
TEST_CASE("write specs resolve where the real accessors do") {
    struct Pair { const char* name; fs::path actual; };
    const Pair pairs[] = {
        {"threads", agentty::util::user_threads_dir()},
        {"cache",   agentty::util::user_cache_dir()},
        {"logs",    agentty::util::user_logs_dir()},
    };
    for (const auto& p : pairs) {
        const cfg::Entry* e = cfg::find(p.name);
        REQUIRE(e != nullptr, "concern is in the inventory");
        REQUIRE(e->write != nullptr, "concern declares a write spec");
        const cfg::Report r = cfg::describe(*e);
        REQUIRE(r.write.has_value(), "report carries the write row");
        CHECK(r.write->error.empty(), "spec resolves");
        CHECK(r.write->path == p.actual.string(),
              "inventory path == the accessor's path");
    }
}

TEST_CASE("the read ladder is exactly what plan emits") {
    // 之间没有过滤 plan() 报告——即: 不变量
    // 本地关注得到两个来源, 即便携式六号(
    // projectx3 QQ userx3)。
    const cfg::Entry* mcp = cfg::find("mcp");
    REQUIRE(mcp != nullptr, "mcp is in the inventory");
    const cfg::Report rm = cfg::describe(*mcp);
    for (const auto& row : rm.reads)
        CHECK(row.dialect == agentty::scope::Dialect::Agentty,
              "mcp reads only the native dialect");
    // 报告它跳过的两个, 来源于同一个领域。
    CHECK(rm.skipped.size() == 2, "mcp names .agents and .claude as not read");

    const cfg::Entry* sk = cfg::find("skills");
    REQUIRE(sk != nullptr, "skills is in the inventory");
    CHECK(cfg::describe(*sk).skipped.empty(),
          "a portable concern skips nothing");
}

TEST_CASE("anchors are derived, and honest") {
    // 两个根,每个主播一个,主播是根的功能,
    // 所以一个新的类别不能不小心把自己作为一个来广告.
    CHECK(cfg::anchor_env(agentty::dirs::Root::User) == "AGENTTY_HOME");
    CHECK(cfg::anchor_env(agentty::dirs::Root::Project) == "AGENTTY_PROJECT_DIR");

    for (const cfg::Entry& e : cfg::inventory()) {
        if (!e.write) continue;
        CHECK(!cfg::moves_whole_root(*e.write),
              "no Spec's own variable relocates a whole root");
    }

    // 抹布和反馈共享项目根但并非一叶:一个可以
    // 重建并被扫荡,另一个会积累,永远不会.
    const cfg::Entry* rag = cfg::find("rag");
    const cfg::Entry* fb  = cfg::find("feedback");
    REQUIRE(rag != nullptr, "rag is in the inventory");
    REQUIRE(fb  != nullptr, "feedback is in the inventory");
    REQUIRE(rag->write != nullptr, "rag writes");
    REQUIRE(fb->write  != nullptr, "feedback writes");
    CHECK(rag->write->leaf != fb->write->leaf,
          "separate leaves — opposite retention must not share a directory");
    CHECK(rag->write->life.rebuildable);
    CHECK(!fb->write->life.rebuildable);
}

TEST_CASE("only executable concerns report trust") {
    // 该列的存在是因为 MCP 门是现在 scope::trust_of
    // 报告 调用 产卵路径的相同函数 调用。清单中没有任何
    // 其他内容执行,因此,其他内容都不得声称存在信任状态;
    // "trusted" 旁边的是一个技能目录 将是毫无
    // 意义的仪式 侵蚀了这个词的重要性。
    for (const cfg::Entry& e : cfg::inventory()) {
        if (!e.read) continue;
        const cfg::Report r = cfg::describe(e);
        for (const auto& row : r.reads) {
            if (cfg::executable(e.name)) {
                // 当前的可执行配置总是得到判决;
                // 缺失的文件没有字节来约束批准.
                CHECK(row.exists == !row.trust.empty(),
                      "executable: a verdict exactly when the file exists");
            } else {
                CHECK(row.trust.empty(),
                      "non-executable concerns claim no trust state");
            }
        }
    }
    CHECK(cfg::executable("mcp"), "mcp spawns processes");
    CHECK(!cfg::executable("skills"), "skills are data");
    CHECK(!cfg::executable("memory"), "memory is data");
}

TEST_CASE("user and explicit sources are trusted by placement") {
    // scope::trust_of 规则,浮出水面: 人的地方 ~/.agentty/mcp.json
    // 在那里,所以它不需要担保。 Project/Local 骑着克隆人进来的
    const cfg::Entry* mcp = cfg::find("mcp");
    REQUIRE(mcp != nullptr, "mcp is in the inventory");
    for (const auto& row : cfg::describe(*mcp).reads) {
        if (!row.exists) continue;
        if (row.locus == agentty::scope::Locus::User
            || row.locus == agentty::scope::Locus::Explicit)
            CHECK(row.trust == "trusted",
                  "a human placed it, so it is trusted");
    }
}

TEST_CASE("sizes are measured once, never double-counted") {
    // 58号的列是实际需要的:
    // 把所有非配置数据存放到另一条路径。
    // 是一个没有人能做出的决定, 而不看到哪些类别是大。 设置是 KB 财务
    // 报告和已审计财务报表 线程 可能是这样 GB ,直到存在 唯一的办法发现是
    // `du`.
    //
    // 陷阱是光谱 SHARE 已解决目录 -- -- 覆盖和
    // 反馈两个土地 <project>/.agentty 如果
    // 每个测量 源码树 ,该栏将相加到真实磁盘的两倍,
    // 超过2x的总数比没有总数还差。
    std::map<std::string, int> measured_per_path;
    for (const cfg::Entry& e : cfg::inventory()) {
        if (!e.write) continue;
        const cfg::Report r = cfg::describe(e);
        REQUIRE(r.write.has_value(), "a write spec yields a write row");
        if (!r.write->error.empty()) continue;

        // 计量和计量 shared_dir 建筑是相互排斥的:
        // 要么这行拥有号码 要么就交给兄弟会
        CHECK(!(r.write->measured && r.write->shared_dir),
              "a row either owns its size or defers, never both");
        if (r.write->measured) ++measured_per_path[r.write->path];
        if (r.write->shared_dir)
            CHECK(r.write->bytes == 0,
                  "a deferring row reports no bytes of its own");
    }
    for (const auto& [path, n] : measured_per_path)
        CHECK(n == 1, "each directory is measured by exactly one concern");
}

TEST_CASE("a concern that writes nothing claims no disk") {
    // 读取 -only 关注事项(mcp、技能、
    // 内存 以范围而不是用底盘来解决。报告大小将创造数字。
    for (const cfg::Entry& e : cfg::inventory()) {
        if (e.write) continue;
        CHECK(!cfg::describe(e).write.has_value(),
              "no write spec, no write row, no size");
    }
}

TEST_CASE("an unreadable subtree is flagged, not silently dropped") {
    // skip_permission_denied 使 chmod-000
    // 目录消失 SILENTLY - 没有 error_code 里面没有东西出现,
    // 一二 MB 子目录报告为 "0 B" 没有任何提示, 其它文件是用来防止的,
    // 但却没有遗漏任何东西, 这也是同样自信的谎言。 部分是好的;
    // 沉默不是。
    //
    // 根能读到任何东西,所以它会 空虚地通过那里。 Windows 无法
    // 表达 "unlistable" 结束 fs::permissions
    // 完全没有(仅指 切换 读取 -only 属性),所以先决条件不能被构建.
#if defined(_WIN32)
    return;
#else
    if (::geteuid() == 0) return;
#endif

    const fs::path base = fs::temp_directory_path()
                        / "agentty_cfg_partial";
    fs::remove_all(base);
    const fs::path home = base / "home";
    const fs::path locked = home / "threads" / "locked";
    fs::create_directories(locked);
    { std::ofstream f(locked / "big.bin"); f << std::string(200000, 'x'); }

    const std::string prev = std::getenv("AGENTTY_HOME")
                                 ? std::getenv("AGENTTY_HOME") : "";
    const bool had = std::getenv("AGENTTY_HOME") != nullptr;
    ::setenv("AGENTTY_HOME", home.string().c_str(), 1);

    const cfg::Entry* threads = cfg::find("threads");
    REQUIRE(threads != nullptr, "threads is in the inventory");

    // 可读: 字节 被计算,没有标记。
    {
        const cfg::Report r = cfg::describe(*threads);
        REQUIRE(r.write.has_value(), "write row");
        CHECK(r.write->bytes >= 200000, "the file is counted");
        CHECK(!r.write->partial, "nothing was unreadable");
    }

    // 无法读取:计数降至0,并宣布MUST.
    fs::permissions(locked, fs::perms::none);
    {
        const cfg::Report r = cfg::describe(*threads);
        REQUIRE(r.write.has_value(), "write row");
        CHECK(r.write->partial,
              "an unreadable subtree is reported as partial");
    }
    fs::permissions(locked, fs::perms::owner_all);

    if (had) ::setenv("AGENTTY_HOME", prev.c_str(), 1);
    else     ::unsetenv("AGENTTY_HOME");
    fs::remove_all(base);
}

TEST_CASE("a symlink loop in a measured tree terminates") {
    // 大小绝不能跟随符号链接: a 链接 进入别人的
    // 源码树 将他们的磁盘作为我们的, 循环会挂起命令。
    const fs::path base = fs::temp_directory_path() / "agentty_cfg_loop";
    fs::remove_all(base);
    const fs::path home = base / "home";
    fs::create_directories(home / "threads");
    { std::ofstream f(home / "threads" / "a.txt"); f << "hello"; }
    std::error_code ec;
    fs::create_directory_symlink("..", home / "threads" / "self", ec);

    const std::string prev = std::getenv("AGENTTY_HOME")
                                 ? std::getenv("AGENTTY_HOME") : "";
    const bool had = std::getenv("AGENTTY_HOME") != nullptr;
    ::setenv("AGENTTY_HOME", home.string().c_str(), 1);

    const cfg::Entry* threads = cfg::find("threads");
    REQUIRE(threads != nullptr, "threads is in the inventory");
    const cfg::Report r = cfg::describe(*threads);   // 绝对必须返回
    REQUIRE(r.write.has_value(), "write row");
    CHECK(r.write->bytes == 5, "counts the file once, does not follow the loop");

    if (had) ::setenv("AGENTTY_HOME", prev.c_str(), 1);
    else     ::unsetenv("AGENTTY_HOME");
    fs::remove_all(base);
}

TEST_CASE("every concern has a name and a description") {
    // 该表是混淆的用户所看到的第一件事;空白的存储单元有
    // 一个比缺失的功能更糟糕的bug.
    for (const cfg::Entry& e : cfg::inventory()) {
        CHECK(!e.name.empty(), "concern has a name");
        CHECK(!e.what.empty(), "concern has a description");
        CHECK((e.read != nullptr || e.write != nullptr),
              "concern does at least one of read/write");
        CHECK(cfg::find(e.name) == &e, "find() returns the same row");
    }
    CHECK(cfg::find("nope") == nullptr, "unknown concern is not found");
}
