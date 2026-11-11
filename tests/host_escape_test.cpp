// host_escape_test - 合作者 -host 融合 OSC 载荷.
//
// ui::host 分为两部分:detect_integration()读取环境(
// 由read_launch_env 所称;答案存在于Model::
// env.host_integration),file_event_osc()是纯字符
// 串构建器。没有缓存的探测器,没有可隔离的,所以每个案例都在一个过程中运行,
// 在每个平台上。
#include "agentty/runtime/view/host_escape.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
static void set_env(const char* k, const char* v) { _putenv_s(k, v); }
static void unset_env(const char* k)              { _putenv_s(k, ""); }
#else
static void set_env(const char* k, const char* v) { ::setenv(k, v, 1); }
static void unset_env(const char* k)              { ::unsetenv(k); }
#endif

namespace host = agentty::ui::host;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; }           \
        else         { std::printf("  ok:   %s\n", msg); }                     \
    } while (0)

int main() {
    // 探测,再 -read 每一个 调用.
    unset_env("AGENTTY_HOST");
    unset_env("INSIDE_EMACS");
    CHECK(!host::detect_integration(), "integration off with no host env");
    set_env("AGENTTY_HOST", "emacs");
    CHECK(host::detect_integration(), "integration on with AGENTTY_HOST=emacs");
    unset_env("AGENTTY_HOST");
    set_env("INSIDE_EMACS", "30.1,vterm");
    CHECK(host::detect_integration(), "integration on with INSIDE_EMACS vterm");
    // 一颗彗星(non-vterm) Emacs前端不能运行我们的钩 ~ 关闭。
    set_env("INSIDE_EMACS", "30.1,comint");
    CHECK(!host::detect_integration(), "integration off with INSIDE_EMACS comint");
    unset_env("INSIDE_EMACS");

    // 载荷 :纯净,不论环境.
    auto edit = host::file_event_osc("edit", "/src/foo.cpp");
    CHECK(edit.has_value()
          && *edit == R"(agentty;{"event":"file","kind":"edit","path":"/src/foo.cpp"})",
          "edit OSC payload");

    auto read = host::file_event_osc("read", "/a b.txt", 42);
    CHECK(read.has_value()
          && *read == R"(agentty;{"event":"file","kind":"read","path":"/a b.txt","line":42})",
          "read OSC carries line + preserves spaces in path");

    // 一条带引文的道路必须是 JSON-escaped 所以它不能从 OSC.
    auto quoted = host::file_event_osc("write", "/weird\"name.txt");
    CHECK(quoted.has_value()
          && quoted->find(R"(\"name.txt)") != std::string::npos,
          "quote in path is JSON-escaped");

    CHECK(!host::file_event_osc("read", "").has_value(),
          "empty path yields no OSC");

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nPASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
