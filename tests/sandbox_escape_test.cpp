// sandbox_escape_test - 电话 macOS 沙箱-exec (SBPL ) 配置注射器护卫。
//
// 电话 工作区 根被插入到 SBPL 配置文件's (子路径) "<path>"
// 写入条款。该路径是 user/attacker-influenceable (--workspace
// / cwd)和 双双双双 macOS/APFS 许可
// 目录名中的双引号、反斜杠和控制字符必须转义，否则路径可能插入配置片段。
// (默认情况下)
// " would close the string early and inject SBPL syntax. sbpl_escape() must make the path inert (escape \ and " )或,
// 对于无法在一种 SBPL 字符串( 控制字符) , 返回空, 调用者省略此条款。
// FAILS CLOSED (无) 工作区 而不是排放 broken/injected
// 简介.

#include "agtest.hpp"

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // workspace_root

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace sb = agentty::tools::util::sandbox;

TEST_CASE("sandbox: JIT commands are recognised for the W^X exemption") {
    for (const char* c : {"node -e 1", "npm install", "cd web && pnpm test",
                          "/usr/bin/node22 x.js", "FOO=1 npx tsc -p .", "(java -jar a.jar)",
                          "./gradlew build", "bun run dev",
                          "git status | node fmt.js", "sh -c 'node x.js'",
                          "bash -lc \"cd a && npm test\"", "env NODE_ENV=1 node a.js",
                          "timeout 30s npm test", "nohup nice -n 5 node srv.js &",
                          "echo $(node -v)", "exec java -jar x.jar", "x=1; node a",
                          "if true; then node a; fi"})
        check(sb::command_needs_jit(c), c);
#if defined(_WIN32)
    check(sb::command_needs_jit("C:\\x\\node.exe a.js"), "windows path");
#endif
    for (const char* c : {"ls -la", "python3 x.py", "cargo build", "make -j8",
                          "cat node_modules/x", "grep -r nodejs.org .", "echo javascript", "",
                          // a JIT 名称,这只是一个参数 决不能抬起W ^X
                          "echo node; ./exploit", "echo \"npm\" | cat", "ls node java",
                          "python3 -c 'print(\"node\")'", "grep node *.js",
                          "git commit -m 'bump node'", "./run.sh node"})
        check(!sb::command_needs_jit(c), c);
}


TEST_CASE("sandbox escape") {
    std::printf("=== sandbox_escape_test ===\n");

    // 普通路径通过不变。
    check(sb::sbpl_escape("/Users/me/project") == "/Users/me/project",
          "plain path is unchanged");
    check(sb::sbpl_escape("/tmp/a-b_c.1") == "/tmp/a-b_c.1",
          "safe punctuation is unchanged");
    check(sb::sbpl_escape("") == "", "empty path stays empty");

    // 双引号是反斜拉 -escaped ——注射载体。
    // 没有这个, " 之后的一切将恢复为 SBPL.
    check(sb::sbpl_escape("/tmp/ws\"x") == "/tmp/ws\\\"x",
          "double quote is backslash-escaped");
    check(sb::sbpl_escape("/tmp/a\"b\"c") == "/tmp/a\\\"b\\\"c",
          "every double quote is escaped");

    // 实际攻击形状: 试图关闭子路径字符串并注入一个(
    // 默认默认) 的路径。 逃逸后, 引文是惰性的,
    // 因此整个过程都保持一个字符串的字节。
    {
        const std::string evil = "/tmp/ws\")(allow default)(\"";
        const std::string esc  = sb::sbpl_escape(evil);
        check(esc.find("\\\"") != std::string::npos, "attack quotes are escaped");
        // 任何未被忽略的引文都无法幸存:结果中的每个 " 都先于\。
        bool clean = true;
        for (std::size_t i = 0; i < esc.size(); ++i)
            if (esc[i] == '"' && (i == 0 || esc[i - 1] != '\\')) clean = false;
        check(clean, "no unescaped quote can terminate the SBPL string");
    }

    // 反斜拉线也逃脱了(这样它就无法逃脱我们的逃脱).
    check(sb::sbpl_escape("/tmp/a\\b") == "/tmp/a\\\\b",
          "backslash is doubled");
    // 引文之前的反弹不得让引文溜走:
    // "\\" + "\"" → "\\\\" + "\\\"" (两者都独立逃脱).
    check(sb::sbpl_escape("a\\\"b") == "a\\\\\\\"b",
          "backslash-then-quote: both escaped, quote stays inert");

    // Control 字符不能生活在一个 SBPL 字符串 ~ 失败 关闭( 空)。
    check(sb::sbpl_escape("/tmp/a\nb").empty(), "newline → empty (fail closed)");
    check(sb::sbpl_escape("/tmp/a\rb").empty(), "carriage return → empty");
    check(sb::sbpl_escape("/tmp/a\tb").empty(), "tab → empty");
    check(sb::sbpl_escape(std::string("/tmp/a\0b", 8)).empty(),
          "embedded NUL → empty");

#if defined(__linux__)
    // 电话 claybin 姿势带有等效的墙壁, sandbox_pane_test
    // 指控他们反对 claybin_posture_for_test () 曾经住在
    // 这里的bwrap argv检查是用bwraw本身进行的。
#endif

}
