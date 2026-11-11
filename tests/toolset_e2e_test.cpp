// toolset_e2e_test - 驱动器 每个 已建 -in 工具通过 agentty's 真实 生产
// 分派 路径 : tools::registry() → mcp_tools_bridge (ToolDef
// 内容 -wrap) → mcp-cpp 工具的落实 ∗∗ decode_result → ToolOutput.
//
// 为什么存在这个: mcp-cpp 自己的测试可以证明工具 BODIES ;
// 此测试证明 agentty's WIRING ——重命名的层,
// 一个 模式定义 漂流,一个失踪 HostServices 后端,
// 或破解码会让一个工具在每架引擎时静悄悄地消失或失败 单元测试
// 保持绿色,一个过程,一个临时 工作区 ,零网络(网络工具
// 只在其离线拒绝路径上使用; search_docs 指向无法到达
// 的嵌入式主机,从而它又回到 BM25-only).
//
// 沙箱隔离 纪律(汲取困难) acp_integration_test 曾经污染了开发
// 商的真品 ~/.agentty/threads): HOME/USERPROFILE
// 重定向到临时目录 之前 任何 agentty 代码运行,所以 remember/
// forget/wipe_memory 土地的贫瘠状态 沙箱 ,从不在用户的真店.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>   // 发作
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/tool/memory_store.hpp"
#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/mcp_tools_backends.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/spec.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agtest_cap.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace agentty;

namespace {

int g_checks = 0;
int g_fails  = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) { std::printf("ok:   %s\n", what.c_str()); }
    else    { std::printf("FAIL: %s\n", what.c_str()); ++g_fails; }
}

// 分派 生产途径——究竟是什么 cmd_factory 已经。
tools::ExecResult run(std::string_view name, json args) {
    const auto td = tools::find(name);
    if (!td) return std::unexpected(tools::ToolError::unknown(
        "tool not in registry: " + std::string{name}));
    return tool::DynamicDispatch::execute_with(agtest::cap(), td, name, args,
                                               agentty::tools::CallContext{});
}

std::string text_of(const tools::ExecResult& r) {
    return r ? r->text : r.error().detail;
}

bool has(const tools::ExecResult& r, std::string_view needle) {
    return r && r->text.find(needle) != std::string::npos;
}

// 执行工具运行在平台 shell: / bin/sh 打开 POSIX,
// cmd.exe 打开 Windows。一个练习弹壳的个案
// SEMANTICS 选择实际运行它的外壳的拼写; 断言保持不变。
#if defined(_WIN32)
constexpr const char* posix_or_cmd(const char*, const char* cmd) { return cmd; }
#else
constexpr const char* posix_or_cmd(const char* posix, const char*) { return posix; }
#endif

void write_file(const fs::path& p, std::string_view body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << body;
}

bool git_available() {
    return std::system("git --version >/dev/null 2>&1") == 0;
}

} // 命名空间结束。

int main() {
    std::printf("toolset_e2e_test\n");

    // 沙箱：所有内容位于同一临时根目录，并在首次 registry() 前设置。
    // 触摸( 触摸) 注册表 是一个进程 -lifetime 静态;
    // 工作区 根和 单数 HOME 建造前必须是最后的。
    // PID-unique 沙箱 : CTest 运行套房时使用 -j ,
    // 而这个测试会产出真正的子进程( bash/process_start ) 在
    // 全球范围内 HOME. A FIXED 共享路径让兄弟测试的撕裂 竞态 我们 源
    // 码树 ——断断续续的来源. toolset_e2e 失败 -j12。
    // 匹配套房的其余部分,并按键根在 getpid().
    auto root = fs::temp_directory_path() /
                ("agentty_toolset_e2e_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "src");
    root = fs::canonical(root);

    ::setenv("HOME",        root.c_str(), 1);
    ::setenv("USERPROFILE", root.c_str(), 1);
    // search_docs : 无法到达的嵌入主机 ~ 即时连接 -refused →
    // BM25-only 回退 ,无3 s Ollama 探测器档,没有网络。
    ::setenv("AGENTTY_OLLAMA_HOST", "127.0.0.1:1", 1);
    ::unsetenv("AGENTTY_MCP_CONFIG");   // 没有外部的MCP服务器
    ::unsetenv("AGENTTY_DOCS_DIR");

    tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);

    // ── 注册表 完整性: ach 目录 必须公布工具。 ──
    {
        const auto reg = tools::wire_tools();
        for (const auto& spec : tools::spec::kCatalog) {
            const auto td = tools::find(spec.name);
            check(static_cast<bool>(td),
                  std::string{"registry advertises '"} + std::string{spec.name} + "'");
            if (!td) continue;
            check(td->input_schema.is_object(),
                  std::string{spec.name} + ": input_schema is an object");
            check(td->effects.bits() == spec.effects.bits(),
                  std::string{spec.name} + ": effects match spec catalog");
            check(static_cast<bool>(td->execute),
                  std::string{spec.name} + ": execute closure installed");
        }
        // 回顾 -bias 命令( E) : read/edit 铅,主壳跟踪。
        check(!reg->empty() && reg->front().name.value == "read",
              "wire order: 'read' listed first");
    }

    // 模型可以到达本地和第三 -party MCP 执行器。
    // 不允许有例外类型交叉此选项 noexcept 进程边界。
    {
        tools::ToolDef throwing;
        throwing.name.value = "throwing_test_tool";
        throwing.execute = [](const maya::unlocked&, const json&, const tools::CallContext&) -> tools::ExecResult { throw 7; };
        auto guarded = tool::DynamicDispatch::execute_with(
            agtest::cap(), tools::Found::of(std::move(throwing)), "throwing_test_tool",
            json::object(), tools::CallContext{});
        check(!guarded && guarded.error().kind == tools::ErrorKind::Unknown,
              "dispatch: contains non-standard tool exceptions");
    }

    const auto file = root / "src" / "hello.txt";

    // ── □ 携带的FileChange ──────────────────────────────────────
    {
        auto r = run("write", {{"file_path", file.string()},
                               {"content", "alpha\nbeta\ngamma\n"}});
        check(r.has_value(), "write: succeeds");
        check(r && r->change.has_value(), "write: carries FileChange");
        check(r && r->change && r->change->added == 3, "write: 3 lines added");
    }

    // write→edit 提醒：用很小改动覆盖大文件时，提示
    //    编辑会更好; 完全改写后保持沉默。。 @ @ info
    {
        // 使用 a 时 DEDICATED 文件 —— 永远不共享 `file` 固定的,
        // 那个 read/edit/grep 下方的块取决于。
        auto nudge_file = root / "nudge_e2e.txt";
        std::string big;
        for (int i = 0; i < 60; ++i) big += "line " + std::to_string(i) + "\n";
        auto seed = run("write", {{"file_path", nudge_file.string()}, {"content", big}});
        check(seed.has_value(), "write nudge: seed big file");

        // 在60行中精确修改一行 - > 应朝编辑方向前进。
        std::string tiny = big;
        auto pos = tiny.find("line 30\n");
        if (pos != std::string::npos) tiny.replace(pos, 8, "line 30 CHANGED\n");
        auto small = run("write", {{"file_path", nudge_file.string()}, {"content", tiny}});
        check(small.has_value(), "write nudge: small overwrite succeeds");
        check(has(small, "edit") && has(small, "tip"),
              "write nudge: small overwrite of big file suggests edit");

        // 批发重写(大多数行不同)必须让NOT 向前冲动。
        std::string rewrite;
        for (int i = 0; i < 60; ++i) rewrite += "fresh " + std::to_string(i) + "\n";
        auto full = run("write", {{"file_path", nudge_file.string()}, {"content", rewrite}});
        check(full.has_value() && !has(full, "tip: this overwrote"),
              "write nudge: full rewrite stays silent");
    }

    // ── 读 ───────────────────────────────────────────────────────────────
    {
        auto r = run("read", {{"path", file.string()}});
        check(has(r, "beta"), "read: returns content");
    }
    {
        // #92: 一项不变的 -read 用于回答 仅
        // "File unchanged since last read ... refer to that instead"（文件自上次读取后没有变化，请参考先前结果。）
        // - 指针到 字节 模型可能不再有(比较, 传输 读 -collapse ,
        // 子剂。一个弱型号,通过阅读来遵循 AGAIN ,
        // 得到了相同的指针,然后循环。现在,每一次重复都必须携带文件本身。
        for (int i = 0; i < 3; ++i) {
            auto again = run("read", {{"path", file.string()}});
            check(has(again, "beta") && has(again, "gamma"),
                  "read #92: repeat " + std::to_string(i + 1) + " carries the bytes");
            check(!has(again, "refer to that instead"),
                  "read #92: repeat " + std::to_string(i + 1) + " is not a bare pointer");
        }
    }
    {
        // 读 0-byte 文件会给出一个明确的头-up,而不是一个空白的结果,
        // 模型可能会错误地读出一个失败的读数.
        auto empty = root / "empty_e2e.txt";
        auto made = run("write", {{"file_path", empty.string()}, {"content", ""}});
        check(made.has_value(), "read: 0-byte fixture written");
        auto r = run("read", {{"path", empty.string()}});
        check(has(r, "empty"), "read: 0-byte file is flagged as empty");
    }

    // ── 编辑 + 模糊的丝片 + FileChange 与 差异块 ──────────────────────
    {
        json e = {{"old_text", "beta"}, {"new_text", "BETA-EDITED"}};
        auto r = run("edit", {{"path", file.string()},
                              {"edits", json::array({e})}});
        check(r.has_value(), "edit: succeeds");
        check(r && r->change && !r->change->hunks.empty(),
              "edit: FileChange has recomputed hunks (diff-review feed)");
        std::ifstream f(file);
        std::string body((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        check(body.find("BETA-EDITED") != std::string::npos,
              "edit: change landed on disk");
    }
    // ── 编辑行位符为CORRECT和LIVE ─────────────────────────
    {
        // 已知布局:编辑后的行为3号线.
        auto lf = root / "locator_e2e.txt";
        write_file(lf, "one\ntwo\nthree\nfour\nfive\n");
        auto r = run("edit", {{"path", lf.string()},
            {"edits", json::array({{{"old_text", "three"},
                                    {"new_text", "THREE-X"}}})}});
        check(has(r, "at line 3"),
              "edit: locator names the changed line (3), not the hunk-context start");
        // 换乘情况确实在3号线上( 三个-X 替换 `three`).
        check(has(r, "+THREE-X"), "edit: the diff shows the change");
    }
    {
        // LIVE 跨行移:编辑 #1 在目标上方插入2行,
        // 因此编辑 #2的目标从第5行移到第7行.
        // 报告线必须反映 SHIFTED (最后)位置,
        // 证明它是根据 演变中 缓冲区 而不是原创的
        auto lf = root / "locator_shift_e2e.txt";
        write_file(lf, "a\nb\nc\nd\nTARGET\nf\n");   // TARGET是5号线
        auto r = run("edit", {{"path", lf.string()}, {"edits", json::array({
            {{"old_text", "a\nb"}, {"new_text", "a\nINS1\nINS2\nb"}},  // +2 线
            {{"old_text", "TARGET"}, {"new_text", "HIT"}},
        })}});
        check(r.has_value(), "edit: multi-edit with line shift applies");
        // 二者都编辑为一 差异块 ; 定位符名称为其第一次
        // 更改(第2行, INS1。生命通过 diff 证明:
        // TARGET->HIT 土地 之后 +2 转动, i.e 新建时 -side 7号线。
        check(has(r, " at line 2"),
              "edit: locator names the first changed line of the merged hunk");
        // 迪夫的新面必须显示 HIT 第7行(a)分段 INS1
        // INS2 b c d HIT ——确认第二次编辑被应用到对调,
        // 直播. 缓冲区 ,而不是原5行的词条.
        {
            auto t = text_of(r);
            auto hit = t.find("+HIT");
            auto ins = t.find("+INS1");
            check(hit != std::string::npos && ins != std::string::npos
                  && hit > ins,
                  "edit: second edit applied live after the first shifted lines");
        }
    }
    {
        // DEFINITIVE 活性:两部编辑 FAR 分开 分开
        // 差异块 编辑 #1 在顶端附近添加3行;编辑 #2的目标(原
        // 为20行)必须在23行上报告——证明第二个位置是在 缓冲区
        // AS SHIFTED 由第一个编辑,而不是原版。
        auto lf = root / "locator_far_e2e.txt";
        std::string body;
        for (int i = 1; i <= 25; ++i) body += "row" + std::to_string(i) + "\n";
        write_file(lf, body);
        auto r = run("edit", {{"path", lf.string()}, {"edits", json::array({
            {{"old_text", "row2"}, {"new_text", "row2\nADD_A\nADD_B\nADD_C"}}, // +3
            {{"old_text", "row20"}, {"new_text", "ROW20-HIT"}},
        })}});
        check(r.has_value(), "edit: far-apart multi-edit applies");
        // 第20行坐20号线;上方+3行后,在23号线落地.
        // 定位线(消息的第一行,diff栅栏前)必须包含23—
        // —只检查前缀,这样我们就不会在diff 正文中匹配
        // "row23".
        {
            auto t = text_of(r);
            auto fence = t.find("```");
            auto head = t.substr(0, fence == std::string::npos ? t.size() : fence);
            check(head.find("23") != std::string::npos,
                  "edit: far hunk's locator reflects the live +3 shift (20 -> 23)");
        }
    }

    // ── 移动/ 删除: 安全的 shell-free 文件系统突变 ─────────────
    {
        const auto renamed = root / "src" / "renamed.txt";
        auto moved = run("move", {{"source", file.string()},
                                  {"destination", renamed.string()}});
        check(moved.has_value() && fs::exists(renamed) && !fs::exists(file),
              "move: renames inside workspace");
        auto restored = run("move", {{"source", renamed.string()},
                                     {"destination", file.string()}});
        check(restored.has_value() && fs::exists(file), "move: can restore path");
        const auto disposable = root / "src" / "remove-me.txt";
        write_file(disposable, "temporary\n");
        auto removed = run("remove", {{"path", disposable.string()}});
        check(removed.has_value() && !fs::exists(disposable), "remove: deletes a file");
        // #91:删除已经消失的东西是理想状态,而不是失败
        // 一个弱型号读作"blocked",然后放弃。
        auto again = run("remove", {{"path", disposable.string()}});
        check(again.has_value() && has(again, "does not exist"),
              "remove: an already-absent path succeeds and says so");
        auto root_remove = run("remove", {{"path", root.string()}, {"recursive", true}});
        check(!root_remove.has_value(), "remove: refuses workspace root");
    }

    // ── 腺肿/ 肿块/ 缩写 list_dir / find_definition ─────────────────────────
    write_file(root / "src" / "code.cpp",
               "int answer() { return 42; }\nint other() { return 7; }\n");
    {
        auto r = run("grep", {{"pattern", "BETA-EDITED"}, {"path", root.string()}});
        check(has(r, "hello.txt"), "grep: finds the edited line");
    }
    {
        // 0-match搜索返回一个定制的,可操作的提示(而不是赤裸的
        // "No matches found."),所以模型知道下一步要尝试什么.
        auto r = run("grep", {{"pattern", "zzz_definitely_absent_token_qwerty"},
                              {"path", root.string()}});
        check(has(r, "No matches"), "grep: reports zero matches");
        check(has(r, "search_code") || has(r, "Try:"),
              "grep: zero-match output suggests next steps");
    }
    {
        auto r = run("glob", {{"pattern", "*.cpp"}, {"path", root.string()}});
        check(has(r, "code.cpp"), "glob: matches by extension");
    }
    {
        auto r = run("list_dir", {{"path", (root / "src").string()}});
        check(has(r, "hello.txt") && has(r, "code.cpp"),
              "list_dir: lists both files");
    }
    {
        auto r = run("find_definition", {{"symbol", "answer"},
                                         {"path", root.string()}});
        check(has(r, "code.cpp"), "find_definition: locates the function");
    }

    {
        // grep word=真吸收旧的 find_references 整个
        // -word 使用标识符,不使用下弦假阳性。
        auto r = run("grep", {{"pattern", "answer"},
                              {"word", true},
                              {"path", root.string()}});
        check(has(r, "code.cpp"), "grep word=true: locates exact identifier uses");
    }

    // ── repo_map : 将骨架排列在 沙箱 ────────────────────
    {
        auto r = run("repo_map", {{"path", root.string()}});
        check(has(r, "code.cpp"), "repo_map: surfaces the source file");
        check(has(r, "answer"), "repo_map: shows definition signatures");
    }

    // ── 外壳( 沙箱 关) ────────────────────────────────────────
    {
        auto r = run("shell", {{"command", "echo e2e-bash-ok"},
                              {"cd", root.string()}});
        check(has(r, "e2e-bash-ok"), "shell: runs and captures stdout");
    }
    // shell 防护：识别隐藏在替换表达式中的破坏性命令，
    //    `bash -c` ,在任何运行前,包装都被拒绝 QQ
    for (const char* evil : {"echo hi && x=$(rm -rf /)", "bash -c 'ls && rm -rf ~'",
                             "sudo env A=1 rm -rf /", "find ~ -exec rm -rf {} +"}) {
        auto r = run("shell", {{"command", evil}, {"cd", root.string()}});
        check(!r.has_value(), std::string{"shell guard refuses: "} + evil);
    }
    {
        // 普通的工作是没有被触及的
        auto r = run("shell", {{"command", posix_or_cmd("python3 -c 'print(40+2)'",
                                                      "python -c \"print(40+2)\"")},
                               {"cd", root.string()}});
        check(has(r, "42"), "shell guard allows `python3 -c`");
    }
    // shell 绕行：命令仍可执行，但纯文件检查操作
    //    调用 得到一个提示, 命名本地工具 + 准确的参数读取
    //    命令( 从未重写, 从未在命令位置运行) @ @ @ info
    {
        auto r = run("shell", {{"command", "sed -n '1,2p' code.cpp"},
                              {"cd", root.string()}});
        check(has(r, "`read` with `start_line: 1, end_line: 2`"),
              "shell tip: `sed -n 1,2p` → read with the exact range");
        auto g = run("shell", {{"command", "grep -rn answer . | head -5"},
                              {"cd", root.string()}});
        check(has(g, "`grep`") && has(g, "`limit:5`"),
              "shell tip: `grep -rn … | head -5` → grep with limit:5");
        check(has(g, "answer"), "shell tip: the shell still ran and its output is there");
        auto b = run("shell", {{"command", "echo build-step"},
                              {"cd", root.string()}});
        check(!has(b, "tip:"), "shell tip: no tip for non-inspection work");
    }
    // ── 遗产 `bash` 名称为 Canonicalises 到 `shell` 工具 ────────────────
    // 模型 调用 执行工具 `bash` (训练前) 内部化我们 `shell`
    // 模式定义; tools::find 化名所以第一个 -turn 调用 工作,
    // 而不是404个 -ing 并强迫人放荡, 重试。
    // 匹配为例 -insensitive — Claude 发射 "Bash"
    // (资本B) 最常见的, 原准确 -case 化名让失败者陷入困境
    {
        auto r = run("bash", {{"command", "echo e2e-alias-ok"},
                              {"cd", root.string()}});
        check(has(r, "e2e-alias-ok"), "bash alias: lowercase resolves to shell");
    }
    {
        auto r = run("Bash", {{"command", "echo e2e-Bash-ok"},
                              {"cd", root.string()}});
        check(has(r, "e2e-Bash-ok"), "bash alias: capital 'Bash' resolves to shell");
    }
    // bash 失败反馈：退出码解码与错误行摘要。
    {
        // 127=未找到命令(cmd.exe:9009)——解码提示必须出现.
        auto r = run("shell", {{"command", "this_binary_does_not_exist_e2e"},
                              {"cd", root.string()}});
        // cmd.exe 上层报告9009,但当 /S /C 包一条链
        // 时报告1,例如 Windows 解码提示是合同,而不是数字。
        check(has(r, posix_or_cmd("127", "exit code")), "bash: reports the numeric exit code");
        check(has(r, "not found"), "bash: decodes exit 127 as command-not-found");
    }
    {
        // 一个输出包含错误的失败命令:行应该得到主文摘,
        // 这样模型在完全倾卸之前就能看到原因.
        auto r = run("shell", {{"command", posix_or_cmd(
                                   "echo 'warming up'; echo 'error: something broke'; exit 2",
                                   "echo warming up& echo error: something broke& exit /b 2")},
                              {"cd", root.string()}});
        check(has(r, "exit code 2"), "bash: reports non-zero exit");
        check(has(r, "Key error line"), "bash: surfaces an error digest on failure");
        check(has(r, "something broke"), "bash: digest carries the error text");
    }

    // ── 击打: 终端 线条 -discipline 位于捕获边界 ────────
    // 释放出的孩子 CSI/OSC 越狱( SGR 颜色, a DECSTBM
    // 区域探测器, 一种 OSC 标题)加 CR 进步必须回流 CLEAN:
    // 无 ESC/CR/BS 字节 (他们会在工具卡上画成流浪的
    // 格言 并致力于 回滚缓冲区 - 电话 "r r" 腐败报告,
    // SGR 文本保存,以及 CR 进步线崩溃到最终状态.
    {
        // cmd不能发射生 ESC/CR 字节 本身; PowerShell's
        // `e and ` r 生成相同的 字节 数据流.
        auto r = run("shell",
            {{"command", posix_or_cmd(
              "printf 'p 1\\r'; printf 'p 2\\n';"
              " printf '\\033[1;32mgreen-e2e\\033[0m\\n';"
              " printf '\\033[3;24r'; printf '\\033]0;title\\007after-e2e\\n'",
              "powershell -NoProfile -c \"$e=[char]27; $o=[char]7; $c=[char]13; $n=[char]10; "
              "[Console]::Out.Write('p 1'+$c+'p 2'+$n+$e+'[1;32mgreen-e2e'+$e+'[0m'+$n+$e+'[3;24r'+$e+']0;title'+$o+'after-e2e'+$n)\"")},
             {"cd", root.string()}});
        const std::string body = text_of(r);
        bool clean = true;
        for (unsigned char c : body)
            if (c == 0x1b || c == '\r' || c == '\b'
                || (c < 0x20 && c != '\n' && c != '\t'))
                clean = false;
        check(clean, "bash: output free of ESC/CR/BS control bytes");
        check(has(r, "green-e2e"), "bash: SGR-wrapped text survives the strip");
        check(has(r, "after-e2e"), "bash: text after DECSTBM/OSC survives");
        check(has(r, "p 2"), "bash: CR progress collapses to final state");
        // 头文件与COMMAND相呼应;在Windows上,
        // 该文本字面上包含"`e[3;24r". 仅判断程序所印的内容.
        const auto out_at = body.find("green-e2e");
        const std::string printed =
            out_at == std::string::npos ? body : body.substr(body.rfind('\n', out_at) + 1);
        check(printed.find("[3;24r") == std::string::npos
                  && printed.find(";24r") == std::string::npos,
              "bash: no stray CSI parameter bytes (the 'r r' glyphs)");
    }

    // ── 重点测试+持久化进程会议 ─────────────────────
    {
        auto tested = run("test", {{"command", posix_or_cmd("printf native-test-ok",
                                                            "echo native-test-ok")}});
        check(has(tested, "PASS") && has(tested, "native-test-ok"),
              "test: runs an explicit focused command with pass status");

        // 一个失败运行的表面WHICH测试作为主文摘失败,
        // 所以模型不需要扫描整个跑者日志.
        auto failed = run("test", {{"command", posix_or_cmd(
            "printf '[  FAILED  ] Suite.CaseX\\n'; exit 1",
            "echo [  FAILED  ] Suite.CaseX& exit /b 1")}});
        check(has(failed, "FAIL"), "test: failing run reports FAIL status");
        check(has(failed, "Failing test") && has(failed, "Suite.CaseX"),
              "test: failing run digests the failing test name");

        auto started = run("process_start", {{"command", posix_or_cmd(
                "printf 'process-first\\n'; sleep 1; printf 'process-second\\n'; sleep 5",
                "echo process-first& ping -n 2 127.0.0.1 >nul& echo process-second& ping -n 6 127.0.0.1 >nul")},
                                              {"cwd", root.string()}});
        check(started.has_value() && has(started, "proc-"), "process_start: returns session id");
        if (started) {
            const auto marker = started->text.find("proc-");
            const auto end = started->text.find(' ', marker);
            const std::string id = started->text.substr(marker, end - marker);

            // 投票直到标记显示而不是赛跑固定窗口:
            // a 项下 已加载 CI 框(此测试共
            // -schedules 整个套房的孩子可以承受比
            // 任何单人更长的时间 wait_ms 排水管排水,
            // 总的限期很宽,所以检验结论 SEMANTICS
            // (初始输出到达,后输出到达一次,
            // 不重播)而不是子进程调度延迟.
            auto poll_until = [&](const char* needle, int budget_ms) {
                auto last = run("process_poll", {{"id", id}, {"wait_ms", 500}});
                int waited = 500;
                while (!has(last, needle) && waited < budget_ms) {
                    auto r = run("process_poll", {{"id", id}, {"wait_ms", 500}});
                    waited += 500;
                    // 民意调查只反映自上次民意调查以来的产出;
                    // 保持最新产出不 -empty 结果是当后来的
                    // 民意测验空空出来时, 打击不会丢失。
                    if (has(r, needle) || r) last = std::move(r);
                }
                return last;
            };

            // process_start 等待~300ms,
            // 并排出所有已经打印的孩子们的横幅 INTO 启动响应(
            // 所以第一次民意调查不会浪费在启动线上).
            // "process-first" 因此,行可能到达 START
            // 而不是在民意测验中, EITHER 位置,测试语义(
            // 初始输出是准确一次) 而不是哪个 调用 带着它
            const bool first_at_start = has(started, "process-first");
            auto first = first_at_start ? started : poll_until("process-first", 10000);
            check(has(first, "process-first"),
                  "process_start/poll: initial output is delivered");
            auto second = poll_until("process-second", 10000);
            check(has(second, "process-second"), "process_poll: waits for new output");
            check(second.has_value() && !has(second, "process-first"),
                  "process_poll: does not repeat previously delivered output");
            auto stopped = run("process_stop", {{"id", id}});
            check(stopped.has_value(), "process_stop: terminates and reaps session");
            check(stopped.has_value() && !has(stopped, "process-first")
                                      && !has(stopped, "process-second"),
                  "process_stop: does not replay output delivered by polls");
        }

        // 退出并完全排水的会话在下一个时被放弃 process_start
        // 破坏器会自行处理 锁 ,所以它必须死 在工具之外
        // -state 锁 (此终止为
        // lock_order_error).
        auto quick = run("process_start", {{"command", "sleep 0.3; printf quick-done"}});
        if (quick) {
            const auto m = quick->text.find("proc-");
            const std::string qid = quick->text.substr(m, quick->text.find(' ', m) - m);
            for (int i = 0; i < 20; ++i) {
                auto r = run("process_poll", {{"id", qid}, {"wait_ms", 300}});
                if (has(r, "exited")) break;
            }
        }
        auto next = run("process_start", {{"command", "sleep 5"}});
        check(next.has_value() && has(next, "proc-"),
              "process_start: reaping a drained session does not abort");
        if (next) {
            const auto m = next->text.find("proc-");
            (void)run("process_stop", {{"id", next->text.substr(m, next->text.find(' ', m) - m)}});
        }
    }

    // ── 工作区-root 边框: fs 工具拒绝逃逸 ─────────────────
    {
        auto r = run("read", {{"path", "/etc/hostname"}});
        check(!r.has_value(), "read: refuses path outside workspace root");
    }

    // ── 待办( 无状态 shell) ────────────────────────────────────────────
    {
        json item = {{"content", "prove the tools"}, {"status", "in_progress"}};
        auto r = run("todo", {{"todos", json::array({item})}});
        check(has(r, "prove the tools"), "todo: echoes the plan");
    }

    // ── 内存 : 项目 回退 + 记住 ~ 忘记 ~ 擦擦 ~ ~ 记得 ~ 忘了 ~ 抹擦 ~ ────────────
    {
        // `--workspace /` 指不受限制的工具,而不是项目 -at-root.
        // 内存项目仍必须锁定进程 cwd。
        const auto prior_cwd = fs::current_path();
        fs::current_path(root);
        tools::util::set_workspace_root(::agentty::IoAccess::grant(), "/");
        check(tools::memory::path_for(tools::memory::Scope::Project)
                  == root / ".agentty" / "memory.jsonl",
              "remember: project scope uses cwd under --workspace /");
        tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);
        fs::current_path(prior_cwd);

        const auto remember_def = tools::find("remember");
        const auto scope_enum = remember_def
            ? remember_def->input_schema["properties"]["scope"]["enum"]
            : json{};
        const bool schema_has_project = scope_enum.is_array()
            && std::find(scope_enum.begin(), scope_enum.end(), "project")
                != scope_enum.end();
        check(schema_has_project,
              "remember: schema advertises available project scope");

        auto defaulted = run("remember", {{"text", "default project sentinel"}});
        check(defaulted.has_value(), "remember: default project scope appends");

        auto r = run("remember", {{"text", "e2e sentinel fact alpha"},
                                  {"scope", "user"}});
        check(r.has_value(), "remember: appends (user scope, sandboxed HOME)");
        check(fs::exists(root / ".agentty" / "memory.jsonl"),
              "remember: wrote inside the sandbox, not the real HOME");

        auto p = run("forget", {{"substring", "sentinel fact"}, {"dry_run", true}});
        check(has(p, "alpha"), "forget: dry-run previews the record");

        auto d = run("forget", {{"substring", "sentinel fact"}});
        check(d.has_value(), "forget: removes by substring");

        (void)run("remember", {{"text", "wipe me"}, {"scope", "user"}});
        auto w = run("wipe_memory", {{"scope", "user"}, {"confirm", true}});
        check(w.has_value(), "wipe_memory: confirmed wipe succeeds");
    }

    // ── 内存:智能范围推论. ────────────────────────────────
    {
        using tools::memory::suggest_scope;
        using tools::memory::Scope;

        // 个人事实 – 用户,带有可命名提示.
        auto n1 = suggest_scope("my name is Ayush and I prefer fish shell");
        check(n1.confident() && n1.scope == Scope::User,
              "suggest_scope: personal identity/preference → user");
        auto n2 = suggest_scope("I use vim with relative line numbers");
        check(n2.confident() && n2.scope == Scope::User,
              "suggest_scope: personal tooling → user");

        // 代码库 事实  ⁇ 项目。
        auto p1 = suggest_scope("the build command is cmake --build build -j 8");
        check(p1.confident() && p1.scope == Scope::Project,
              "suggest_scope: build workflow → project");
        auto p2 = suggest_scope("in this project we put reducers under src/runtime");
        check(p2.confident() && p2.scope == Scope::Project,
              "suggest_scope: repo deixis + path → project");

        // 模糊 / 没有信号 / 不信任( 呼叫者保持默认)。
        auto a1 = suggest_scope("the quick brown fox");
        check(!a1.confident(), "suggest_scope: no signal is not confident");

        // End-to-end :个人事实 NO 明确范围必须是
        // 自动的 -corrected 从工程默认到用户,并说。
        auto corrected = run("remember",
            {{"text", "my name is Ayush, call me A"}});
        check(corrected.has_value(), "remember: personal fact appends");
        check(text_of(corrected).find("user") != std::string::npos,
              "remember: smart scope notes the project→user correction");
        // 降落在了这里 USER 内存 ,所以是一个用户 -scope 擦擦就收回来
        auto wc = run("wipe_memory", {{"scope", "user"}, {"confirm", true}});
        check(text_of(wc).find("0") == std::string::npos || wc.has_value(),
              "remember: corrected fact was written to user scope");

        // A 代码库 没有明确范围的项目(没有虚假的更正)
        // ——它不得 显示在用户中 内存 擦干净后。
        auto kept = run("remember",
            {{"text", "the build command for this project is make release"}});
        check(kept.has_value(), "remember: project fact appends");
        check(text_of(kept).find("→user") == std::string::npos,
              "remember: project fact is not redirected to user");
    }

    // ── 技能: 未知名称 ~ 恢复提示, 不是崩溃 ─────────────────
    {
        auto r = run("skill", {{"name", "no-such-skill-xyz"}});
        check(text_of(r).find("no skill named") != std::string::npos,
              "skill: unknown name yields recovery hint");
    }

    // ── search_docs: BM25-only RAG 在一个真正的文件目录 ──────────────
    // resolve_docs_root (a) 检查 AGENTTY_DOCS_DIR 先说,
    // 然后说 CWD/docs - 这个测试的cwd是建筑目录, 所以env
    // var必须指向 沙箱 实体。 -retrieve ,所以在这里设置是好的。
    write_file(root / "docs" / "zebra.md",
               "# Zebra habits\n\nThe zebra quagga migrates across the "
               "savanna every solstice season.\n");
    write_file(root / "docs" / "other.md",
               "# Unrelated\n\nNothing to see here about databases.\n");
    ::setenv("AGENTTY_DOCS_DIR", (root / "docs").c_str(), 1);
    {
        auto r = run("search_docs", {{"query", "zebra quagga migration"}});
        check(has(r, "zebra"), "search_docs: BM25 retrieval hits the passage");
        check(has(r, "hybrid") || has(r, "bm25"),
              "search_docs: reports truthful BM25 or hybrid mode");
    }

    // ── 重复查询:引擎单电源服务于SAME通道 ──
    // rag-cpp 保持指数的温暖 调用 ;
    // 一个相同的查询必须返回相同的顶端段落(决定性的排名).
    {
        auto r = run("search_docs", {{"query", "zebra quagga migration"}});
        check(has(r, "zebra"), "search_docs: repeat query returns same passage");
    }

    // 2. 纠错检索 CRAG：自然会话形式的查询，
    // 仍然包含内容的词必须检索。蒸馏器会剥离句子(
    // "can you tell me how the ... works" 所以,
    // 我们 重试 探测器降落在 "zebra quagga" . 信心 -gated ,
    // 所以在强的第一通道上 它根本不会开火—— 无论通过的方式。
    {
        auto r = run("search_docs",
                     {{"query", "can you tell me how the zebra quagga works"}});
        check(has(r, "zebra"),
              "search_docs: corrective/distilled retry still finds the passage");
    }

    // ── search_docs × 内存 : 所了解的事实是一个已融为一体的知识来源 ──
    // 必须通过查询——包括将6 KiB 提示词 预算中的OUT
    // 与内存 = 来源相滚动的事实——来获取一个被记住的事实。
    {
        (void)run("remember", {{"text", "the flux capacitor requires "
                                        "gigawatt plutonium calibration"},
                               {"scope", "user"}});
        auto r = run("search_docs", {{"query", "flux capacitor plutonium"}});
        check(has(r, "flux capacitor"),
              "search_docs: retrieves a remembered fact");
        check(has(r, "memory"),
              "search_docs: memory hit carries memory provenance");
        (void)run("wipe_memory", {{"scope", "user"}, {"confirm", true}});
    }

    // ── #1主动检索:预 -turn 活动 -RAG 路径 ─────────────
    // proactive_retrieve () 运行 相同
    // 管道断断续续,只有在信心清空时才能返回一个街区 HIGH 栏。
    // 通过网关降低栏杆,强烈的查询必须产生一个栅栏上下文块,
    // 标明源头并携带通道; 关闭 -topic 查询必须不
    // 产生任何结果( 没有未提示的符号开支 )。
    {
        ::setenv("AGENTTY_RAG_PROACTIVE_MIN", "0.0", 1);
        auto hit = tools::proactive_retrieve("zebra quagga migration", 3);
        check(hit.has_value(), "proactive_retrieve: strong query yields a hit");
        if (hit) {
            check(hit->block.find("<retrieved-context>") != std::string::npos,
                  "proactive_retrieve: emits a fenced context block");
            check(hit->block.find("zebra") != std::string::npos,
                  "proactive_retrieve: block carries the retrieved passage");
            check(hit->passages >= 1,
                  "proactive_retrieve: reports at least one passage");
        }
        // 酒吧为1.01(无法接触),任何东西都不应该注射.
        ::setenv("AGENTTY_RAG_PROACTIVE_MIN", "1.01", 1);
        auto none = tools::proactive_retrieve("zebra quagga migration", 3);
        check(!none.has_value(),
              "proactive_retrieve: an unreachable bar suppresses injection");
        ::unsetenv("AGENTTY_RAG_PROACTIVE_MIN");
    }

    // ── 单-execution 主动检索+解调 ────────────────
    // 兼容性入口和孤立的-worker入口共享一个漏斗;
    // 没有脱落的套接字,也没有重复的查询.
    {
        ::setenv("AGENTTY_RAG_PROACTIVE_MIN", "0.0", 1);
        // 专门医生,所以这个段落 绝不 于 解禁前注射过 FIFO 已
        // 按下源: path: line 键, 因此重用 zebra.md
        // (上面已经注射) 无法证明没有 -poison 属性。
        write_file(root / "docs" / "okapi.md",
                   "# Okapi range\n\nThe okapi bongo forages the Ituri "
                   "rainforest understory at dawn.\n");
        // 强制保护人证拿起新文件(索引来自
        // search_docs 调用 上,
        // 仅重 -scans 肯定可以收回 所以以后的失手只能意味
        // 着 解毒 而不是 过期 索引。
        {
            auto seed = run("search_docs", {{"query", "okapi bongo rainforest"}});
            check(has(seed, "okapi"),
                  "proactive_retrieve: okapi seed doc is indexed");
        }

        auto landed = tools::proactive_retrieve("okapi bongo rainforest", 3);
        check(landed.has_value(),
              "proactive_retrieve: single funnel yields the passage");
        if (landed)
            check(landed->block.find("okapi") != std::string::npos,
                  "proactive_retrieve_blocking: block carries the passage");

        // 注射后,重复阻断调用必须将其消除(模型
        // 已经看到这届会议)——证明承诺的发生。
        auto again = tools::proactive_retrieve_blocking("okapi bongo rainforest", 3);
        check(!again.has_value(),
              "proactive_retrieve_blocking: commits dedup after injecting");
        ::unsetenv("AGENTTY_RAG_PROACTIVE_MIN");
    }

    // ── #3 search_code : 语义代码检索 工作区 ───────
    // 寻人者走 CWD ,所以chdir进入 沙箱 (在之后被恢复),
    // 其中存在一个独特的源文件. BM25-only 这里( 没有
    // 嵌入主机); 概念 -query win需要嵌入,
    // 但词典路径必须已经索引 + refer + 引用路径:行。
    {
        write_file(root / "src" / "throttler.cpp",
                   "// Request rate limiting\n"
                   "int backoff_ms(int attempt) {\n"
                   "    return (1 << attempt) * 100; // exponential backoff\n"
                   "}\n");
        auto prev_cwd = fs::current_path();
        fs::current_path(root);
        auto r = run("search_code", {{"query", "exponential backoff rate"}});
        fs::current_path(prev_cwd);
        check(has(r, "backoff"), "search_code: BM25 retrieval hits the function");
        check(has(r, "throttler.cpp"), "search_code: result cites the source path");
        check(has(r, "hybrid") || has(r, "bm25"),
              "search_code: reports truthful BM25 or hybrid mode");
    }

    // ── 任务:没有安装子代理跑车 ~ 优雅的拒绝 ────────────
    {
        auto r = run("task", {{"prompt", "explore the codebase"}});
        check(!text_of(r).empty(), "task: unavailable runner answers, no crash");
        check(!r.has_value() || text_of(r).find("unavailable") != std::string::npos
              || text_of(r).find("not configured") != std::string::npos
              || text_of(r).find("no ") != std::string::npos,
              "task: names the missing backend");
    }

    // ── 代理人来源:a PROJECT-defined 标记为 ───────────
    // 一台仓库可以运载.agentty/agents/*.md,其作用提示词是
    // 攻击器-controllable,因此任务卡标记它"project agent".
    // 已建成 -ins 和(将) -be )用户代理没有标签.
    // 透明性只——代理仍然运行,工具保持守门.
    {
        write_file(root / ".agentty" / "agents" / "helper.md",
                   "---\ndescription: a project helper\n---\n"
                   "Your role: help with project tasks.\n");
        namespace sub = tools::subagent;
        // 用户 -agent 发现是 cwd -relative (project_root 因此,
        // 从内部运行此检查 沙箱 根,在后面恢复。
        auto prev_cwd = fs::current_path();
        fs::current_path(root);
        check(sub::agent_origin(agentty::IoAccess::grant(), "helper") == "project",
              "agent provenance: a project-shipped agent is 'project'");
        check(sub::agent_origin(agentty::IoAccess::grant(), "explorer") == "builtin",
              "agent provenance: a built-in agent is 'builtin' (no tag)");
        check(sub::agent_origin(agentty::IoAccess::grant(), "nonexistent") == "builtin",
              "agent provenance: an unknown name is 'builtin' (safe default)");
        fs::current_path(prev_cwd);
    }

    // ── 网络工具:仅离线的arg/error路径 ──────────────────────────
    {
        auto r = run("web_fetch", {{"url", "http://example.com"}});
        check(!r.has_value() || text_of(r).find("https") != std::string::npos,
              "web_fetch: refuses non-https without touching the network");
    }
    {
        auto r = run("web_fetch", {{"url", "not a url"}});
        check(!r.has_value() || !text_of(r).empty(),
              "web_fetch: malformed url yields an error, not a crash");
    }
    // SSRF 防护：拒绝私有地址、元数据地址和回环目标，包括
    //    IPv4-mapped IPv6拼写绕过天真的前缀过滤器.
    //    任何套接字之前都拒绝,所以这些都完全下线了.
    {
        auto blocked = [&](const char* url) {
            auto r = run("web_fetch", {{"url", url}});
            // 无论是硬错误,还是从未包含被获取的页面内容的信息,
            // 守护者都不得连接。我们断言工具
            // 没有返回成功的正文。
            return !r.has_value()
                || text_of(r).find("blocked") != std::string::npos
                || text_of(r).find("not allowed") != std::string::npos
                || text_of(r).find("private") != std::string::npos
                || text_of(r).find("refus") != std::string::npos
                || text_of(r).find("error") != std::string::npos;
        };
        check(blocked("http://169.254.169.254/latest/meta-data/"),
              "web_fetch SSRF: blocks cloud-metadata IPv4");
        check(blocked("http://127.0.0.1:8080/"),
              "web_fetch SSRF: blocks loopback");
        check(blocked("http://2130706433/"),
              "web_fetch SSRF: blocks decimal-encoded loopback");
        check(blocked("http://[::ffff:127.0.0.1]/"),
              "web_fetch SSRF: blocks IPv4-mapped IPv6 loopback");
        check(blocked("http://[::ffff:169.254.169.254]/"),
              "web_fetch SSRF: blocks IPv4-mapped IPv6 metadata");
    }

    // 在真实仓库上验证四种 Git 操作。
    if (git_available()) {
        const std::string q = "\"";
        std::string setup =
            "cd " + q + root.string() + q + " && git init -q"
            " && git config user.email e2e@test && git config user.name e2e"
            " && git add -A && git commit -qm seed";
        check(std::system(setup.c_str()) == 0, "git: seed repo created");

        // 父母一方的仓库内部缩小的工作区不得让Git重新发现该父母一方和expose/commit兄弟方文件.
        //
        tools::util::set_workspace_root(::agentty::IoAccess::grant(), root / "src");
        auto escaped = run("git_status", {{"path", (root / "src").string()}});
        check(!escaped && escaped.error().kind == tools::ErrorKind::OutOfWorkspace,
              "git_status: refuses repository rooted above workspace");
        tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);

        write_file(root / "src" / "hello.txt", "changed content\n");

        auto st = run("git_status", {{"path", root.string()}});
        check(has(st, "hello.txt"), "git_status: sees the modified file");

        // git_status 只扫描 子模块 当行更改时 IS 一,
        // 脏 子模块 仍然必须命名;干净的必须不费钱。
        {
            const auto inner = root.parent_path()
                / (root.filename().string() + "_inner");
            std::string mk =
                "rm -rf " + q + inner.string() + q + " && git init -q " + q + inner.string() + q
                + " && cd " + q + inner.string() + q
                + " && git -c user.email=e@t -c user.name=e commit -q --allow-empty -m i"
                + " && echo a > f && git add f"
                + " && git -c user.email=e@t -c user.name=e commit -qm f"
                + " && cd " + q + root.string() + q
                + " && git -c protocol.file.allow=always submodule add -q " + q + inner.string() + q + " sub"
                + " && git commit -qm sub";
            if (std::system(mk.c_str()) == 0) {
                auto clean = run("git_status", {{"path", root.string()}});
                check(clean && !has(clean, "submodules with uncommitted"),
                      "git_status: a clean submodule is not reported");
                write_file(root / "sub" / "f", "dirty\n");
                auto dirty = run("git_status", {{"path", root.string()}});
                check(has(dirty, "submodules with uncommitted"),
                      "git_status: names a dirty submodule");
                std::string undo = "cd " + q + root.string() + q
                    + " && git submodule --quiet deinit -f sub && git rm -qf sub"
                    + " && rm -rf .git/modules/sub && git commit -qm unsub"
                    + " && rm -rf " + q + inner.string() + q;
                (void)std::system(undo.c_str());
            }
        }

        auto df = run("git_diff", {{"path", root.string()}});
        check(has(df, "changed content"), "git_diff: shows the hunk");

        // 超过50的潜水艇 KB 盖子必须警告它不完整 告诉模型 不 改为
        // apply_patch ——不完全的 ——"不完全"——"不彻底"——"没有完整".
        // 补丁 中点可以结束 -hunk。生成一个巨大的变化来绊倒盖。
        {
            std::string huge;
            for (int i = 0; i < 4000; ++i)
                huge += "new line " + std::to_string(i) +
                        " with enough text to add up past fifty kilobytes\n";
            write_file(root / "big.txt", huge);
            std::string add = "cd " + root.string() + " && git add -A";
            (void)std::system(add.c_str());
            auto bigdf = run("git_diff", {{"path", root.string()},
                                          {"staged", true}});
            // 要么适合(小内),要么用升级提示切换.
            check(!has(bigdf, "[output truncated]"),
                  "git_diff: no bare truncation marker");
            if (text_of(bigdf).find("truncated") != std::string::npos)
                check(has(bigdf, "apply_patch") && has(bigdf, "stat_only"),
                      "git_diff: truncated patch gives an actionable hint");
        }

        auto cm = run("git_commit", {{"message", "e2e commit"},
                                     {"stage_all", true},
                                     {"path", root.string()}});
        check(cm.has_value(), "git_commit: stages + commits");

        auto lg = run("git_log", {{"path", root.string()}, {"count", 5}});
        check(has(lg, "e2e commit"), "git_log: shows the new commit");

        auto shown = run("git_show", {{"ref", "HEAD"}, {"path", root.string()}});
        check(has(shown, "e2e commit"), "git_show: shows commit metadata and patch");

        auto blame = run("git_blame", {{"path", (root / "src" / "hello.txt").string()},
                                        {"start_line", 1}, {"end_line", 1}});
        check(has(blame, "changed content"), "git_blame: annotates a focused line range");
    } else {
        std::printf("skip: git not available — git_* section skipped\n");
    }

    // ── 诊断:没有构建系统 + 结构化的答案,没有悬念 ─────
    {
        auto r = run("diagnostics", json::object());
        check(!text_of(r).empty(), "diagnostics: answers without a build system");
    }

    std::printf("%d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) { std::printf("PASSED\n"); return 0; }
    std::printf("FAILED\n");
    return 1;
}
