// mcp_bridge_test 结束 -to-end 烟雾 agentty↔mcp-cpp
// 能力桥 喷出 mcp-cpp 实例 MCP 服务器( 已建入) 子模块 )通过生成 mcp.json
// 驱动器 agentty::mcp::mcp_tools ()——证明整个链条:配置 解析 →
// mcp::Connection 产卵+手摇 − cap::Registry ·
// 合成 ToolDef → execute() 圆形 -trips a tools/call.
//
// 当示例服务器二进制程序没有建成时,SKIPS(Exit 0),
// 所以套房在没有建成mcp-cpp的机器上保持绿色.
// Set AGENTTY_MCP_E2E_SERVER在服务器二进制程序上明确指向.

#include "agentty/tool/tool.hpp"
#include "agtest_cap.hpp"
#include "agentty/mcp/client.hpp"
#include "agentty/runtime/slash.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>   // 发作
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace agentty;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// 查找示例 MCP 服务器: env 覆盖,然后构建一些 -tree 相对于常见的
// CWDs的猜测( 仓库 根或 键 build/).
static std::string find_server() {
    if (const char* e = std::getenv("AGENTTY_MCP_E2E_SERVER"); e && e[0]) {
        if (fs::exists(e)) return e;
    }
    const char* candidates[] = {
        // agentty 自己的建筑 源码树: add_subdirectory(
        // third_party/mcp-cpp ) 镜像源路径,所以例子在下面
        // build/third_party/mcp-cpp/. 检查从 仓库 根和 单数 build/.
        "build/third_party/mcp-cpp/examples/mcp_server_example",
        "third_party/mcp-cpp/examples/mcp_server_example",
        "../build/third_party/mcp-cpp/examples/mcp_server_example",
        // mcp-cpp 自己的独立建筑 源码树 内侧 子模块.
        "third_party/mcp-cpp/build/examples/mcp_server_example",
        "../third_party/mcp-cpp/build/examples/mcp_server_example",
        "third_party/mcp-cpp/build/examples/Release/mcp_server_example",
    };
    std::error_code ec;
    for (const char* c : candidates)
        if (fs::is_regular_file(c, ec)) return fs::absolute(c, ec).string();
    return {};
}

}  // 名称空间( 归并)

int main() {
    // 健壮性：服务器命令不存在时必须跳过该服务器，
    // 干净( 没有产卵, 没有挂链, 没有崩溃) , 返回零工具。
    // 这是 mcp.json 中错误路径的准确失败。
    // 它绝不能将会话缩小。 运行是无条件的( 不需要服务
    // 器的例子 ): 假的绝对路径无法在任何机器上解决。
    {
        auto btmp = fs::temp_directory_path() /
                    ("agentty_mcp_badpath_" + std::to_string(::getpid()));
        std::error_code bec; fs::remove_all(btmp, bec);
        fs::create_directories(btmp, bec);
        auto bcfg = btmp / "mcp.json";
        {
            std::ofstream f(bcfg);
            f << "{ \"mcpServers\": { \"ghost\": { \"command\": "
              << "\"/nonexistent/definitely/not/here/date_server\" } } }\n";
        }
        ::setenv("AGENTTY_MCP_CONFIG", bcfg.string().c_str(), 1);
        CHECK(mcp::mcp_config_present());
        mcp::PoolHandle bpool;
        auto btools = mcp::mcp_tools(bpool);
        std::printf("mcp_bridge_test: bad-path config → %zu tool(s) "
                    "(expect 0)\n", btools.size());
        CHECK(btools.empty());   // 跳过,没有产出;会话存活
        ::unsetenv("AGENTTY_MCP_CONFIG");
        fs::remove_all(btmp, bec);
    }

    std::string server = find_server();
    if (server.empty()) {
        std::printf("mcp_bridge_test: SKIP (no example server built; "
                    "build mcp-cpp with -DMCP_BUILD_EXAMPLES=ON or set "
                    "AGENTTY_MCP_E2E_SERVER)\n");
        return 0;
    }

    // Write 临时 mcp.json 指向示例服务器,并瞄准桥对它通过
    // AGENTTY_MCP_CONFIG.
    // PID-unique 故套房在平行试验下保持隐形 -j).
    auto tmp = fs::temp_directory_path() /
               ("agentty_mcp_e2e_" + std::to_string(::getpid()));
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec);
    auto cfg = tmp / "mcp.json";
    {
        std::ofstream f(cfg);
        f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
          << server << "\" } } }\n";
    }
    ::setenv("AGENTTY_MCP_CONFIG", cfg.string().c_str(), 1);

    CHECK(mcp::mcp_config_present());

    mcp::PoolHandle pool;
    auto tools = mcp::mcp_tools(pool);
    std::printf("mcp_bridge_test: bridge returned %zu tool(s)\n", tools.size());
    for (const auto& t : tools)
        std::printf("  - %s\n", t.name.value.c_str());

    // 示例服务器至少会发布广告 "add" 财务报告和已审计财务报表 "now".
    CHECK(!tools.empty());
    CHECK(static_cast<bool>(pool));   // 保留 -alive 操作

    const tools::ToolDef* add = nullptr;
    for (const auto& t : tools)
        if (t.name.value.find("add") != std::string::npos) add = &t;
    CHECK(add != nullptr);

    if (add) {
        auto r = agentty::tool::DynamicDispatch::execute_with(agtest::cap(), tools::Found::of(*add),
            add->name.value, nlohmann::json{{"a", 17}, {"b", 25}}, agentty::tools::CallContext{});
        CHECK(r.has_value());
        if (r) {
            std::printf("  add(17,25) -> %s\n", r->text.c_str());
            // 示例服务器返回文本中某个地方的42元总和。
            CHECK(r->text.find("42") != std::string::npos);
            // 结构化产出( structuredContent {"sum" :42})
            // 作为保存和渲染物. JSON 块。
            CHECK(r->text.find("\"sum\"") != std::string::npos);
        } else {
            std::fprintf(stderr, "  add() failed: %s\n", r.error().render().c_str());
        }
    }

    // ── 未信任的注释未关闭 ────────────────────────────────
    // 服务器说明是提示,不是执行边界。除非配置明确设定
    // trustAnnotations 连一个声称读的 -only
    // 工具接收到保守的完整 副作用 设定。
    if (add) {
        CHECK(add->effects.has(tools::Effect::ReadFs));
        CHECK(add->effects.has(tools::Effect::WriteFs));
        CHECK(add->effects.has(tools::Effect::Exec));
    }
    // `now` 没有 readOnlyHint ——保守全文 副作用 设置(任务定值)。
    const tools::ToolDef* now = nullptr;
    for (const auto& t : tools)
        if (t.name.value == "now" || t.name.value.find("__now") != std::string::npos) now = &t;
    if (now) CHECK(now->effects.has(tools::Effect::Exec));

    // ── 资源 ─────────────────────────────────────────────────────────
    auto resources = mcp::mcp_resources();
    std::printf("mcp_bridge_test: %zu resource(s)\n", resources.size());
    CHECK(!resources.empty());
    bool found_motd = false;
    for (const auto& r : resources) {
        std::printf("  resource: %s (%s)\n", r.uri.c_str(), r.title.c_str());
        if (r.uri.find("motd") != std::string::npos) found_motd = true;
    }
    CHECK(found_motd);
    {
        std::string err;
        auto body = mcp::mcp_read_resource("file:///motd", err);
        CHECK(body.has_value());
        if (body) {
            std::printf("  read motd -> %s\n", body->c_str());
            CHECK(body->find("Welcome") != std::string::npos);
        } else {
            std::fprintf(stderr, "  read_resource failed: %s\n", err.c_str());
        }
    }
    // 通用的mcp_read_resource工具已经存在,并列出资源。
    const tools::ToolDef* read_res = nullptr;
    for (const auto& t : tools) if (t.name.value == "mcp__resources__read") read_res = &t;
    CHECK(read_res != nullptr);
    if (read_res) {
        auto listing = agentty::tool::DynamicDispatch::execute_with(agtest::cap(),
            tools::Found::of(*read_res), read_res->name.value, nlohmann::json::object(),
            agentty::tools::CallContext{});
        CHECK(listing.has_value());
        if (listing) CHECK(listing->text.find("motd") != std::string::npos);
    }

    // ── prompts ───────────────────────────────────────────────────────────
    auto prompts = mcp::mcp_prompts();
    std::printf("mcp_bridge_test: %zu prompt(s)\n", prompts.size());
    CHECK(!prompts.empty());
    bool found_summarize = false;
    for (const auto& p : prompts) {
        std::printf("  prompt: %s\n", p.name.c_str());
        if (p.name.find("summarize") != std::string::npos) found_summarize = true;
    }
    CHECK(found_summarize);
    {
        std::string err;
        auto rendered = mcp::mcp_get_prompt("summarize", {{"text", "hello world"}}, err);
        CHECK(rendered.has_value());
        if (rendered) {
            std::printf("  render summarize -> %s\n", rendered->c_str());
            CHECK(rendered->find("hello world") != std::string::npos);
        } else {
            std::fprintf(stderr, "  get_prompt failed: %s\n", err.c_str());
        }
    }
    const tools::ToolDef* get_prompt = nullptr;
    for (const auto& t : tools) if (t.name.value == "mcp__prompts__get") get_prompt = &t;
    CHECK(get_prompt != nullptr);

    // 离线后留下的必需的参数是使用错误,因此仅`/name`必须作为
    // 命名参数的用法说明回来——而不是服务器自己的拒绝(
    // "Missing required prompt arguments"),
    // 它告诉用户既不是哪个参数,也不是线应该是什么样子.
    // 例服务器的`summarize` 提示词需要一个需要的`text`.
    {
        const auto* with_required = [&]() -> const mcp::PromptInfo* {
            for (const auto& p : prompts)
                for (const auto& a : p.arguments)
                    if (a.required) return &p;
            return nullptr;
        }();
        CHECK(with_required != nullptr);
        if (with_required) {
            // 电话 传输 名称是 带类型的 线条也一样( 3)
            // mcp_expand_prompt 接受任意一种拼写),因此不需要名称解析.
            const std::string typed = "/" + with_required->name;
            bool usage = false;
            auto note = mcp::mcp_expand_prompt(typed, &usage);
            CHECK(note.has_value());
            CHECK(usage);           // 举杯 不 提交的
            if (note) {
                std::printf("  bare %s -> %s\n", typed.c_str(), note->c_str());
                CHECK(note->find("needs ") != std::string::npos);
                CHECK(note->find("try /") != std::string::npos);
                CHECK(note->find('\n') == std::string::npos);   // 一杯面包线
                bool named = false;
                for (const auto& a : with_required->arguments)
                    if (a.required && note->find(a.name) != std::string::npos) named = true;
                CHECK(named);
            }
            // 填充所需参数时,NOT必须产生一个用法说明:
            // 真实的会转到服务器上
            bool served = false;
            (void)mcp::mcp_expand_prompt(typed + " hello", &served);
            CHECK(!served);
            // 拖曳空格仅是"missing",不是空参数.
            bool blank = false;
            (void)mcp::mcp_expand_prompt(typed + "   \t", &blank);
            CHECK(blank);
            // 用户使用说明: slash::expand (ACP,
            // `agentty run` )不得将其交给模型作为信息.
            {
                const Io io = IoAccess::grant();
                const auto cat = slash::discover(io);
                if (slash::resolve(typed, cat).what == slash::Resolved::What::Prompt) {
                    auto chk = slash::expand_checked(io, typed, cat);
                    CHECK(chk.has_value() && chk->usage);
                    CHECK(!slash::expand(io, typed, cat).has_value());
                }
            }
        }
    }

    // ── 百分比 -tool 生活被排斥(即 enable/disable 切换 路径) ───────────
    // 拆卸工具必须从预测的目录XXWITHOUT中掉下一个re
    // -spawn:project_tools读到配置的tools.exclude活,
    // mcp_bump_generation()强迫下个投影.
    // 重写配置,排除"add",re-list,确认它已经消失;
    // 然后清除排除,确认它回来了——证明re
    // -spawn-free切换既过滤器又不挂挂的un-filters.
    {
        auto has_add = []{
            for (auto& t : mcp::mcp_tools_live())
                if (t.name.value.find("__add") != std::string::npos
                    || t.name.value == "add") return true;
            return false;
        };
        CHECK(has_add());                       // 先前已存在 切换
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\", \"tools\": { \"exclude\": [\"add\"] } } } }\n";
        }
        mcp::mcp_bump_generation();
        CHECK(!has_add());                      // 已禁用 ~ 已过滤
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\" } } }\n";
        }
        mcp::mcp_bump_generation();
        CHECK(has_add());                       // 内容 -enabled 回去,别挂
        std::printf("mcp_bridge_test: per-tool live exclude toggles cleanly\n");

        // Anti-vanishing:mcp_server_tools列出ALL
        // 的广告工具,而不管是否被排除在外,因此选择面板总是可以显示一个被禁用的工具
        // (以re-enable它). Deputy "add",然后确认它是
        // STILL在未过滤的服务器-tools列表中.
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\", \"tools\": { \"exclude\": [\"add\"] } } } }\n";
        }
        mcp::mcp_bump_generation();
        auto all = mcp::mcp_server_tools("demo");
        bool add_listed = false;
        for (auto& n : all) if (n == "add") add_listed = true;
        CHECK(add_listed);   // 仍然无法想象的已禁用工具 ~ 永远不会消失 ~
        // 恢复
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\" } } }\n";
        }
        mcp::mcp_bump_generation();

        // ── plugin_model:真理的统一来源 ────────────────────
        // 禁用 "add" ; 模型必须仍然 LIST 标记它,
        // 并报告已启用的右边数。然后, -enable
        // 并证实它反转。这是权威视图,
        // 选择面板 渲染.
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\", \"tools\": { \"exclude\": [\"add\"] } } } }\n";
        }
        mcp::mcp_bump_generation();
        {
            auto pm = mcp::plugin_model();
            const mcp::ServerState* demo = nullptr;
            for (auto& sv : pm.servers) if (sv.name == "demo") demo = &sv;
            CHECK(demo != nullptr);
            if (demo) {
                CHECK(demo->connected);
                bool add_listed = false, add_enabled = true;
                for (auto& t : demo->tools)
                    if (t.name == "add") { add_listed = true; add_enabled = t.enabled; }
                CHECK(add_listed);        // 仍在模型中( 没有消失)
                CHECK(!add_enabled);      // 正确标记已禁用
                CHECK(demo->tools.size() >= 2);  // 添加 + 现在仍然在那里
            }
        }
        {
            std::ofstream f(cfg, std::ios::trunc);
            f << "{ \"mcpServers\": { \"demo\": { \"command\": \""
              << server << "\" } } }\n";
        }
        mcp::mcp_bump_generation();
        {
            auto pm = mcp::plugin_model();
            const mcp::ServerState* demo = nullptr;
            for (auto& sv : pm.servers) if (sv.name == "demo") demo = &sv;
            CHECK(demo != nullptr);
            if (demo) {
                bool add_enabled = false;
                for (auto& t : demo->tools) if (t.name == "add") add_enabled = t.enabled;
                CHECK(add_enabled);       // 内容 -enabled
            }
        }
        std::printf("mcp_bridge_test: plugin_model unifies config + live cleanly\n");
    }

    if (g_failures == 0) { std::printf("mcp_bridge_test: all checks passed\n"); return 0; }
    std::fprintf(stderr, "mcp_bridge_test: %d failure(s)\n", g_failures);
    return 1;
}
