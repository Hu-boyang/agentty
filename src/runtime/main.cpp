// agentty — 终端 Claude 创建于代码克隆 maya.
//
// main.cpp 仅是线条 :
//   1. 解析 argv(子命令+选项)
//   2. 决 定 凭据
//   3. 建造 Provider + Store 满足 I/O 概念
//   4. 安装Deps,以便update/cmd_factory能够到达它们。
//   5. 手 AgenttyApp至maya的运行时

// 全球 malloc/free 和运算符 new/delete 路由通过 mimalloc
// 静态 库 由 CMake 何时 -DAGENTTY_USE_MIMALLOC=ON (默认).

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>        // CommandLineToArgvW (UTF -8 argv 恢复)
#  include <mmsystem.h>          // timeBeginPeriod / timeEndPeriod 数据
#  include <io.h>               // 设置模式(_S)
#  include <fcntl.h>            // _O_BINARY
#  if defined(_MSC_VER)
//   MSVC自动消耗了普拉格玛和链接 winmm.lib. GCC以警告
//   忽略它——我们链接通过target_link_libraries在
//   CMakeLists.txt为MinGW的建筑建造,所以普拉格玛在那里是
//   毫无意义的噪音.
#    pragma comment(lib, "winmm.lib")
#  endif
#endif

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>   // 异构( 无头的 `run` stdin 检测)
#include <poll.h>     // 民意调查(non-blocking stdin 偷看,请见运行的子命令)
#else
#include <io.h>
#include <process.h>  // _getpid
#define isatty _isatty
#define fileno _fileno
#endif

// 便携式工艺 -id 帮助者 (E) POSIX 被罚/ Windows 来点热闹的吧
[[nodiscard]] inline long agentty_pid() {
#ifdef _WIN32
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

#ifdef _WIN32
// 我们是在酒下跑,而不是真实的 Windows?
//
// 检测了Wine本身的文档方式:ntdll导出 wine_get_version
// () 和没有真正的 Windows 便宜,没有 注册表 戳,没有 副作用.
[[nodiscard]] inline bool running_under_wine() {
    auto* ntdll = ::GetModuleHandleA("ntdll.dll");
    return ntdll && ::GetProcAddress(ntdll, "wine_get_version");
}
#else
[[nodiscard]] inline bool running_under_wine() { return false; }
#endif

#include <maya/maya.hpp>
#include <maya/host/run.hpp>   // maya::run : 数据 jaal 东道主
#include <maya/platform/io.hpp>   // query_terminal_size (沙箱 横幅)
#include "agentty/runtime/app/host.hpp"   // agentty::app::Host
#include "agentty/runtime/mem.hpp"        // tune_allocator
#include "agentty/util/startup_trace.hpp"

#include "agentty/acp/server.hpp"
#include "agentty/version.hpp"
#include "agentty/airgap/airgap.hpp"
#include "agentty/util/logx.hpp"   // 坠机中飞机记录器倾覆 处理函数
#include "agentty/util/teardown.hpp"
#include "agentty/util/storage_lock.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/view/palette.hpp"   // ui::status_warn — 主题标志
#include "agentty/runtime/app/program.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/i18n/startup.hpp"   // i18n::init ——事前的什么情况. 渲染
#include "agentty/io/blob_gc.hpp"
#include "agentty/io/clipboard.hpp"   // agentty 粘贴
#include "agentty/runtime/paste_cli.hpp"
#include "agentty/io/http.hpp"
#include "agentty/mcp/serve.hpp"
#include "agentty/mcp/oauth.hpp"
#include "agentty/mcp/client.hpp"   // mcp::release_servers
#include "agentty/rag/rag_adapter.hpp"
#include "agentty/rag/embed_secret.hpp"
#include "agentty/util/update.hpp"
#include "agentty/util/home_dir.hpp"   // window_title's ~/ 缩略语
#include "agentty/util/dbglog.hpp"     // window_title's 回退 日志
#include "agentty/tool/util/utf8.hpp"  // window_title的UTF-8 洗涤器
#include "agentty/util/user_root.hpp"   // user_logs_dir (stderr.log 家庭) 家庭(家庭)
#include "agentty/provider/anthropic/provider.hpp"
#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/provider/copilot/provider.hpp"
#include "agentty/provider/kimi/provider.hpp"
#include "agentty/provider/openai/provider.hpp"
#include "agentty/provider/ollama/provider.hpp"
#include "agentty/provider/acp_provider_adapter.hpp"
#include "agentty/provider/dispatch.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/tool/web_search_policy.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/config/inventory.hpp"   // config::cmd_config
#if defined(AGENTTY_MCP)
#include "agentty/mcp/import.hpp"         // mcp::import_::cli
#include "agentty/foreign/sessions.hpp"   // foreign::cli
#endif
#include "agentty/tool/commands.hpp"
#include "agentty/runtime/slash.hpp"   // `agentty run "/name"`
#include "agentty/tool/hooks.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/workspace/files.hpp"
#include "agentty/workspace/checkpoint.hpp"
#include "agentty/workspace/symbols.hpp"
#include "agentty/util/modelsdev.hpp"       // join_background_refresh
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/runtime/run_cli.hpp"

// 转发调试器( E) mcp_tools_backends.hpp 停车 <mcp/tools/host.hpp> ,
// 其 MCP 命名空间与 agentty::mcp 内 main.cpp 正在使用).
namespace agentty::tools {
// 在任何工作发生前验证 `--agent`(参见运行的子命令)。
bool is_known_agent_type(std::string_view name);
std::string known_agent_types();
// 停止( E) RAG 检索器的背景在撕裂时温暖(见 调用 网站地址如下)。
void rag_shutdown();
}

namespace {

// 编译 -in 项目版本: * agentty::version() (agentty/version.hpp).

// ── 除错崩溃 处理函数 ───────────────────────────────────────────────────
// 在调试构建中,安装一个 SIGSEGV/SIGABRT
// 处理函数 打印一个后溯追踪以显示 之前 进程会死。 ASAN
// 无法拦截( N) e.g. mimalloc 内部断层),
// 并给用户一个堆栈跟踪而不需要 GDB.
#if !defined(NDEBUG) && !defined(_WIN32)
#include <execinfo.h>
#include <unistd.h>

void crash_handler(int sig) {
    // Write 改为 STDERR ,而不是 studout: , 而不是 Studout ,
    // 用于计算 agentty 现场直播是 TUI 渲染 于是,在Stdout的土地上的
    // 回溯 被夹在中间 帧 定子是传统的诊断通道,可以分离 `2>crash.log`
    // 原始数据 write() 仅指flprintf和 backtrace_symbols
    // 双双分配,再分配 -entering 可能是一个 -corrupted
    // 分配器.
    if (sig == SIGSEGV) {
        const char msg[] = "\n=== agentty: SIGSEGV (segmentation fault) ===\n";
        (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
    } else if (sig == SIGABRT) {
        const char msg[] = "\n=== agentty: SIGABRT (abort) ===\n";
        (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }
    // backtrace()/backtrace_symbols_fd()
    // 并非严格意义上的async-signal-safe(它们可以分配),
    // 而是在调试-only 处理函数中,额外的帧值它。如果他们在损坏的堆栈上崩溃,
    // 默认处置(通过maya的紧急处理函数或SIG_DFL在下面链条)仍然会给一个
    // 核心堆放。
    void* frames[32];
    int n = backtrace(frames, 32);
    if (n > 0) {
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
    // 飞行记录器:最后记录的~256 事件 (警告+总是,更多当 AGENTTY_LOG
    // 自动通道, 预格式化, 时间发布 —— 垃圾堆是原始的已准备好的写(2)s 字
    // 节 自动自动连接 -signal-safe .
    // 这艘船 "what was happening right before the crash"
    // 即使文件记录关闭, 也都会有每份报告。
    (void)agentty::logx::dump_flight_recorder(STDERR_FILENO);
    const char end[] = "==================\n";
    (void)!write(STDERR_FILENO, end, sizeof(end) - 1);
    // 退出( 不退出) 所以没有退出(_O) 处理函数 运行在损坏的进程中。
    // 注意 maya's 终端 安装自己的缩略语 -based 紧急恢复,
    // 一旦 TUI 已经上线了; 因为 此 处理函数 将首先安装(主机顶部),
    // maya 锁在它上 之后 恢复Tty,所以一个中点 -session
    // 崩溃同时恢复 终端 并打印这个反向跟踪。
    _exit(128 + sig);
}

void install_crash_handler() {
    // 最佳( 最佳) -effort : 如果 signal() 我们只是
    // 没有额外的回溯追踪就跑不掉了... maya 紧急恢复( 后安装) 仍在运行。
    (void)std::signal(SIGSEGV, crash_handler);
    (void)std::signal(SIGABRT, crash_handler);
}
#else
// 发布/ Windows: a MINIMAL 处理函数 只会丢掉飞行记录器
// 调试处理函数的backtrace()可以分配(在被破坏的进程中不安全),
// 但logx::dump_flight_recorder通过构建(预制字节,
// 原始写)是async-signal-safe——所以即使释放用户也会在一次
// 崩溃中得到"the last ~256 events",这恰恰是它最重要的时候.
// Windows: std::signal(SIGSEGV/SIGABRT ) 得到 CRT; write()
// 地图通过 io.h 输入 logx.cpp.
extern "C" void agentty_release_crash_handler(int sig) {
    // RE-ENTRANCY GUARD - 看见匹配的一英寸
    // maya's emergency_signal_handler. maya 安装其 tty
    // -restore 处理函数 使用同一致命信号的 SA_NODEFER 并且,在离开的路上,
    // 恢复 PRIOR 复写前的 sigaction( 此函数) -raising。
    // 所以, `signal(sig, SIG_DFL)` 下方可以被恢复所覆盖,
    // 再 -raise 回到原地 回到原处 maya's 处理函数 ,它回到这里:
    // 双 -handler 叮当 -pong 从未到达默认动作。
    //
    // 症状是不可混淆的,在现场也可以看到:千千万万的
    // "=== agentty crash ===" 头文件共用ONE pid
    // 和ONE倍的印章,每张印章之后又有一个完整的飞行-recorder倾覆,
    // 将实际发生的第一次坠毁埋在了兆字节重复的下.
    //
    // 在第二个条目上,停止合作:立即采取默认动作,
    // 使进程随正确的状态而死,
    // 并可以丢弃核心.
    static volatile std::sig_atomic_t in_handler = 0;
    if (in_handler) {
        std::signal(sig, SIG_DFL);
        std::raise(sig);
#if !defined(_WIN32)
        _exit(128 + sig);   // 如果默认被忽略, 请不要旋转
#endif
        return;
    }
    in_handler = 1;

#if defined(_WIN32)
    (void)agentty::logx::dump_flight_recorder(2);
#else
    static const char hdr[] = "\n=== agentty crash ===\n";
    (void)!write(2, hdr, sizeof(hdr) - 1);
    (void)agentty::logx::dump_flight_recorder(2);
#endif
    std::signal(sig, SIG_DFL);
    std::raise(sig);   // 内容 -raise 默认处置(核心堆放等)
}
void install_crash_handler() {
#if defined(_WIN32)
    (void)std::signal(SIGSEGV, agentty_release_crash_handler);
    (void)std::signal(SIGABRT, agentty_release_crash_handler);
#else
    // 斜体, 不 std::signal : 我们需要 SA_ONSTACK ,
    // 和 std::signal 没办法要求它。
    //
    // A 处理函数 安装了平面运行 FAULTING 堆栈。
    // 对一个空的来说是罚款的 -deref 堆积的溢出物毫无
    // 用处——没有空间推进 处理函数's 帧 ,
    // 因此它存在条目上的断层,崩溃报告中此函数的存在从未出现过。测量到:
    // 0条诊断线在堆式-overflow SIGSEGV上没有备用堆式,
    // 1条与1条.
    //
    // maya 为 tty 安装自己的 Altstack -restore 处理函数 秒;
    // 秒数 sigaltstack() 调用 换一个,所以只要没人提供过就问一个
    // SA_ONSTACK 安全,但无论哪种方式,它只是意味着
    // 使用当前线程已配置的备用信号栈。
    struct sigaction sa{};
    sa.sa_handler = agentty_release_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    stack_t cur{};
    const bool have_alt =
        ::sigaltstack(nullptr, &cur) == 0 && !(cur.ss_flags & SS_DISABLE);
    if (have_alt) {
        sa.sa_flags |= SA_ONSTACK;
    } else {
        static char alt_stack[256 * 1024];
        stack_t ss{};
        ss.ss_sp    = alt_stack;
        ss.ss_size  = sizeof(alt_stack);
        ss.ss_flags = 0;
        if (::sigaltstack(&ss, nullptr) == 0) sa.sa_flags |= SA_ONSTACK;
    }
    (void)::sigaction(SIGSEGV, &sa, nullptr);
    (void)::sigaction(SIGABRT, &sa, nullptr);
#endif
}
#endif

void fputs_utf8(FILE* f, std::string_view s) {
#if defined(_WIN32)
    const int fd = fileno(f);
    if (fd >= 0 && ::_isatty(fd)) {
        const intptr_t raw = ::_get_osfhandle(fd);
        if (raw != -1) {
            HANDLE h = reinterpret_cast<HANDLE>(raw);
            DWORD mode = 0;
            if (::GetConsoleMode(h, &mode)) {
                const int need = ::MultiByteToWideChar(
                    CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                    nullptr, 0);
                if (need > 0) {
                    std::wstring wide(static_cast<size_t>(need), L'\0');
                    if (::MultiByteToWideChar(
                            CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                            wide.data(), need) == need) {
                        DWORD written = 0;
                        if (::WriteConsoleW(h, wide.data(),
                                            static_cast<DWORD>(wide.size()),
                                            &written, nullptr)) {
                            return;
                        }
                    }
                }
            }
        }
    }
#endif
    std::fwrite(s.data(), 1, s.size(), f);
}

void print_version() {
    std::printf("agentty %s\n", ::agentty::version());
    // 诊断结果在哪里, 以及他们是否被抓住了。
    // 用户无法发送无法找到的日志,以及 `--version` 别人问起就
    // 先跑 "what are you on?" 所以它就是虫子所在的地方
    // 鬼子 -reporting 工作流程可以依靠被看到.
    const auto lf = agentty::logx::log_file();
    if (!lf.empty()) {
        std::printf("log: %.*s\n", static_cast<int>(lf.size()), lf.data());
        if (const auto n = agentty::logx::redaction_count(); n > 0)
            std::printf("     (%lu secret%s redacted)\n", n, n == 1 ? "" : "s");
    } else {
        std::printf("log: off \u2014 set AGENTTY_LOG=trace to capture diagnostics\n");
    }
}

// ── `agentty diagnostics` ─────────────────────────────────────────────────
//
// 一个生成ONE文件的命令,一个用户可以附加在错误报告上.
//
// 我们的替代办法是四。 -step 仪式:知道
// AGENTTY_LOG 存在, 设置 之前 复制( 之后注意到错误),
// 查找 ~/.agentty/logs ,并记住同时说明你的版本,
// OS 和提供者。每个步骤都会失去人员;实地证据表明,
// 用户报告 传输 拍摄屏幕时出现错误。
//
// 纯文本,不是拉链:没有依赖性,用户可以 READ
// 这一点不仅仅是紧凑的, 能够看清他们分享的东西
// 的人会发出它。 秘密已经被剥离到日志中。
// emit() 接口边界 ,然后打印出编辑数,以便可见。
int cmd_diagnostics() {
    namespace fs = std::filesystem;
    std::string out;
    out.reserve(1 << 16);
    const auto dir = agentty::util::user_logs_dir();

    const auto section = [&](const char* name) {
        out += "\n===== "; out += name; out += " =====\n";
    };

    section("agentty");
    out += std::string{"version: "} + ::agentty::version() + "\n";
    out += std::string{"os: "} +
#if defined(_WIN32)
        "windows"
#elif defined(__APPLE__)
        "macos"
#else
        "linux"
#endif
        + "  arch: " +
#if defined(__aarch64__) || defined(_M_ARM64)
        "arm64"
#else
        "x86_64"
#endif
        + "  build: " +
#if defined(NDEBUG)
        "release"
#else
        "debug"
#endif
        + "\n";

    // NOTE : 没有直播提供者区域。此命令作为它的 自身 进程,
    // 所以 provider::active ()是默认 -constructed 选择,
    // 而不是失败的会话所使用的——打印它会表明一种自信的谎言。
    // 真实的 provider/model 是在日志的
    // `provider.select` 线条,该 末尾 事实,从重要的会议开始
    section("retrieval (embeddings)");
    {
        // 与上面的提供者部分不同,这不是 在一个单独的进程中的
        // 谎言:嵌入器由 settings.json + env,
        // 失败的会话读作相同的两个源,所以 -deriving
        // 探测器被明确标记为运行中 -now ,因为可达性正是
        // 用户存档的内容 "retrieval is bad"
        // 需要得到回答——一个沉默的字回落到关键字 -only
        // 搜索是这个子系统最常见的隐形失败.
        namespace eb = agentty::rag::embed;
        eb::EmbedConfig ec;
        eb::apply_env(::agentty::IoAccess::grant(), ec);
        try {
            const auto s = agentty::persistence::load_settings(::agentty::IoAccess::grant());
            if (!s.rag.embed_backend.empty()) {
                ec.backend = eb::backend_from_id(s.rag.embed_backend);
                if (!s.rag.embed_model.empty()) ec.model = s.rag.embed_model;
                if (!s.rag.embed_host.empty())  ec.host  = s.rag.embed_host;
                if (s.rag.embed_port != 0)      ec.port  = s.rag.embed_port;
                ec.tls            = s.rag.embed_tls;
                ec.path           = s.rag.embed_path;
                ec.model_path     = s.rag.embed_model_path;
                ec.tokenizer_path = s.rag.embed_tokenizer_path;
                ec.dim            = s.rag.embed_dim;
            }
        } catch (...) { /* 内置配置符 */ }

        out += "config: " + eb::describe(ec) + "\n";
        out += "identity: " + eb::identity_tag(ec) + "\n";
        // 电话 凭据 永远不要打印,只打印一个。
        if (eb::needs_api_key(ec.backend)) {
            const auto slot = eb::endpoint_key(ec);
            out += std::string{"api key: "}
                 + (eb::load_key(slot).empty() ? "not set" : "set (stored securely)") + "\n";
        }
        if (auto v = eb::validate(ec); const auto* bad = std::get_if<eb::Invalid>(&v)) {
            out += "invalid: " + bad->why + "\n";
        } else {
            if (!ec.api_key.empty()) { /* 未打印 */ }
            const auto r = eb::probe(ec);
            if (const auto* ok = std::get_if<eb::ProbeOk>(&r))
                out += "probe: ok · " + std::to_string(ok->dim) + "d · "
                     + std::to_string(ok->latency_ms) + "ms\n";
            else
                out += "probe: FAILED — " + std::get<eb::ProbeErr>(r).why
                     + "\n         (retrieval falls back to keyword-only/BM25)\n";
        }
    }

    section("session (from the log)");
    {
        const auto lf = agentty::logx::log_file();
        std::error_code sec;
        if (!lf.empty() && std::filesystem::exists(
                std::filesystem::path{std::string{lf}}, sec)) {
            std::ifstream in{std::string{lf}, std::ios::binary};
            std::string ln, startup, provider;
            // 匹配 SITE FIELD ,没有线的子弦。
            //
            // 日志互页 事件 带生 传输 字节 ,并请求 正文 经常
            // 引用用户的文本 带类型的 ——包括用户调试时 agentty ,
            // 字形字符串 "provider.select:".
            // 赤裸裸的 find() 因此,
            // 与请求一致 正文 并且报告了大量的
            // 对话作为活跃的提供者。
            //
            // 每行为"<ts> <uptime> <tid> <level> <channel> <site>: …",
            // 因此站台前有空间和频道名称,
            // 需要级别字母和后面的结肠将其钉在字段上.
            const auto is_event = [](const std::string& line,
                                     std::string_view site) {
                const auto at = line.find(site);
                if (at == std::string::npos) return false;
                // A 载荷 被击中的深处 JSON blob;
                // 位于该区的一个真实的站点 头文件 ,在前一~64列内.
                if (at > 64) return false;
                // 紧接着是 ':' 格式要求
                return line.compare(at + site.size(), 1, ":") == 0;
            };
            while (std::getline(in, ln)) {
                if (is_event(ln, "startup"))         startup  = ln;
                if (is_event(ln, "provider.select")) provider = ln;
            }
            // 两者都是Warn——默认的释放建筑所保持的级别—
            // —所以 "missing" 已经 不 表示关卡太低,
            // 这意味着日志被关闭, 或者日志被旋转过( 启动时
            // 它们被写过一次, 并且是32) MB 旋转会话中会
            // 掉下文件的首部分。 向您建议 AGENTTY_LOG
            // =fo here, 和它以前一样, 发送用户更改一个
            // 从来不是原因的设置。
            out += (startup.empty()
                        ? std::string{"startup: (not in the log \u2014 logging was off, "
                                      "or the file rotated past the banner)"}
                        : startup) + "\n";
            out += (provider.empty()
                        ? std::string{"provider: (not in the log \u2014 logging was off, "
                                      "or no provider was selected this session)"}
                        : provider) + "\n";
        } else {
            out += "(no log captured \u2014 see the NOTE below)\n";
        }
    }

    // 电话 FLIGHT RECORDER ——无任何设置的功能部分.
    //
    // 警告 -and-above 事件 总是被保存在 -process 环,
    // 即使文件记录已关闭, 已经发生的失败仍然被捕获。
    // 没有此部分, 常见的大小写是无用的: 释放用户点击错误,
    // 运行 `diagnostics` ,然后得到文件 说
    // "logging was off" 虫子已经完了,
    // 叫他们再生 AGENTTY_LOG 完全就是这个命令要消除的摩擦。
    //
    // 相同的过程, 所以这个只覆盖一个被错误击中的 此 运行。
    // 这是交互大小写( 撞击错误, 运行同一 shell 的诊断);
    // 日志 -file 路径下面覆盖所有更长的 -lived.
    // 只有在没有日志文件时: 带有一个, 同一警告+
    // 事件 已经进入 末尾 下面和重复这些内容只会使报告
    // 更难阅读。
    if (agentty::logx::log_file().empty()) {
    section("recent warnings + errors (in-memory flight recorder)");
    {
        const auto fr = dir / "agentty-flight.txt";
        std::error_code fec;
        fs::create_directories(dir, fec);
        if (agentty::logx::dump_flight_recorder_to(fr.string().c_str())) {
            std::ifstream in{fr, std::ios::binary};
            std::string body{std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>()};
            out += body.empty() ? std::string{"(nothing recorded this run)\n"}
                                : body;
            fs::remove(fr, fec);
        } else {
            out += "(flight recorder unavailable)\n";
        }
    }
    }

    // 在哪里,我们在哪里 字节 活的,还有多少人,从... 相同 库存
    // `agentty config` 打印,所以错误报告永远不能描述布局 二
    // 进制程序 没有 -- -- 和 "my disk is full" /
    // "can I move this" 已经到来,而不是需要来回旅行
    section("storage");
    for (const auto& e : agentty::config::inventory()) {
        const auto r = agentty::config::describe(e);
        if (!r.write) continue;
        out += std::string{e.name};
        out += ": ";
        if (!r.write->error.empty()) {
            out += "unresolved (" + r.write->error + ")\n";
            continue;
        }
        out += r.write->path;
        if (r.write->measured) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "  (%.1f MB)",
                          static_cast<double>(r.write->bytes) / (1024.0 * 1024.0));
            out += buf;
        } else if (r.write->shared_dir) {
            out += "  (shared)";
        }
        if (r.write->origin != agentty::dirs::Origin::Default)
            out += "  [overridden]";
        out += "\n";
    }

    section("log");
    {
        const auto lf = agentty::logx::log_file();
        if (lf.empty()) {
            out += "logging: OFF at collection time\n";
            out += "NOTE: the warnings above were captured automatically.\n"
                   "      For a FULL byte-level trace (raw provider traffic),\n"
                   "      re-run with AGENTTY_LOG=trace, reproduce, then\n"
                   "      run `agentty diagnostics` again.\n";
        } else {
            out += "file: " + std::string{lf} + "\n";
            out += "secrets redacted so far: "
                 + std::to_string(agentty::logx::redaction_count()) + "\n";
        }
    }

    // 木头本身, 末尾-limited : 长会话可以持续到数
    // 十次 MB 并且没有人贴上。 末尾 坚持失败,这是最重要的。
    section("log tail (last 4000 lines)");
    {
        const auto lf = agentty::logx::log_file();
        std::error_code ec;
        if (!lf.empty() && fs::exists(fs::path{std::string{lf}}, ec)) {
            std::ifstream in{std::string{lf}, std::ios::binary};
            std::vector<std::string> lines;
            std::string ln;
            while (std::getline(in, ln)) {
                lines.push_back(std::move(ln));
                if (lines.size() > 4000) lines.erase(lines.begin());
            }
            for (const auto& l : lines) { out += l; out += '\n'; }
        } else {
            out += "(no log file — see the NOTE above)\n";
        }
    }

    // 已修改,但仍然是这台机器的日志: 出生于0600年.
    const auto path = dir / "agentty-diagnostics.txt";
    (void)agentty::persistence::write_atomic(::agentty::IoAccess::grant(), path, out,
        {.durability = agentty::persistence::Durability::Atomic, .mode = 0600});

    std::printf("agentty %s \u2014 diagnostics written\n", ::agentty::version());
    if (const auto n = agentty::logx::redaction_count(); n > 0)
        std::printf("  %lu secret%s redacted\n", n, n == 1 ? "" : "s");
    if (agentty::logx::log_file().empty())
        std::printf("  note: logging was OFF \u2014 for a full trace run\n"
                    "        AGENTTY_LOG=trace agentty   (reproduce, then re-run this)\n");
    std::printf("\n  %s\n\n  Attach that file to your report.\n",
                path.string().c_str());
    return 0;
}

// ── 帮助, 铺设 ─────────────────────────────────────────────────
//
// 这是在一张指纹中 ~97 字符串字节, 并用手对齐描述
// 列 -counted 宽度在~78列烤成这样 终端 双倍
// -wrapped 每个条目和宽的一块都浪费了一半的屏幕。
// 标志 意思是说 -counting 空格,
// 请输入空格。
//
// maya 渲染 此形状没有 运行时 (墨水的药) renderToString ),
// 所以源头携带 CONTENT 和布局引擎决定几何。
//
// 这里刻意使用纯文本输出。maya
// 这里用作 FORMATTER ,不是一种风格: 没有颜色,
// 没有胆量,没有逃跑的顺序, 和相同的 字节 是否为 Stdout
// 是一个 终端 烟斗,或者文件。 CLI 输出是一个接口——
// 某种东西将其粘贴,某种东西粘贴成一个问题——而造型只会增加打破的方法。
// 一个布局引擎给我们的一件事是,打印不能是一个向“问题
// ”倒流的描述列。 实际 终端 宽度,这是此修正的bug。
//
// (maya::print ()被故意不使用:它写光标和tmux
// -passthrough 连一个管子的序列都
// `agentty --help | less` 这将是垃圾。 )
namespace help_ui {

using namespace maya::dsl;

// 一行:名称在左沟,描述在右围.
maya::Element entry(std::string name, std::string desc, int gutter) {
    // width() 一个人就是个 REQUEST ,
    // 而不是夹子:比沟槽长的名字(p.
    // "  mcp-logout <srv>" )将描述栏向右推,
    // 排行停止与邻居排队. 平缓分秒和最大到相同值使沟槽准确,
    // 因此描述——包括包裹的续行——全部从一列开始.
    //
    // 一个不使用 FIT 的字眼,这个字眼就是一个
    // 不同的问题,而夹子本身就使它更糟糕:这个
    // 字眼在自己的栏内包裹,第二行则与描述相拼。
    // 这是一个内容错误(缩短名称),但它应该降解
    // 成一个可读的字,而不是一个拼音,所以一个超长
    // 的名称会得到它自己与下面的描述缩进的线条。
    const int name_w = static_cast<int>(maya::unicode::str_width(name));
    if (name_w >= gutter) {
        auto indent = maya::hstack();
        indent.width(maya::Dimension::fixed(gutter));
        indent.min_width(maya::Dimension::fixed(gutter));
        indent.max_width(maya::Dimension::fixed(gutter));
        auto rest = maya::hstack();
        rest.grow();
        auto second = maya::hstack();
        auto stack = maya::vstack();
        return stack(text(std::move(name)),
                     second(indent(text("")), rest(text(std::move(desc)))));
    }

    auto left = maya::hstack();
    left.width(maya::Dimension::fixed(gutter));
    left.min_width(maya::Dimension::fixed(gutter));
    left.max_width(maya::Dimension::fixed(gutter));
    auto right = maya::hstack();
    right.grow();
    auto row = maya::hstack();
    return row(left(text(std::move(name))), right(text(std::move(desc))));
}

maya::Element heading(std::string s) { return text(std::move(s)); }
maya::Element blank() { return text(""); }

} // namespace help_ui

void print_usage() {
    using namespace help_ui;
    constexpr int kCmdGutter  = 20;   // "  mcp-logout <srv>  "
    constexpr int kOptGutter  = 23;   // "  -w, --workspace DIR " + a 空间

    auto doc = maya::vstack();
    doc.gap(0);
    auto body = doc(
        text(std::string{"agentty "} + ::agentty::version()) | Bold,
        blank(),
        text("usage: agentty [subcommand] [options]") | Dim,
        blank(),
        heading("subcommands"),
        entry("  login", "Authenticate (API key, or OAuth via claude.ai)", kCmdGutter),
        entry("  logout", "Remove saved credentials", kCmdGutter),
        entry("  status", "Show current auth status", kCmdGutter),
        entry("  airgap", "Launch agentty on an air-gapped host via SSH tunnel "
                          "(`agentty airgap --help` for details)", kCmdGutter),
        entry("  acp", "Run as an ACP agent over stdio (for Zed et al.)", kCmdGutter),
        entry("  paste [cmd]", "Clipboard tools (richpaste). `paste setup` once on your "
                             "laptop makes Ctrl+V images work over ssh; `paste` prints "
                             "the clipboard, `paste doctor` explains the route, "
                             "`paste config` lists and sets the paste settings, "
                             "`paste help` lists the rest", kCmdGutter),
        entry("  run [PROMPT]", "Headless one-shot: run PROMPT through the full agent "
                                "loop (tools, sandbox) and print only the answer to "
                                "stdout. PROMPT `-` or absent reads stdin: "
                                "`git diff | agentty run \"review this\"`. Flags: "
                                "--agent TYPE (general, explorer, reviewer, tester, "
                                "coder), --json, -o FILE, --max-turns N, --timeout "
                                "5m, --effort LEVEL, --read-only, --no-network, "
                                "--save, --resume ID|last, --events jsonl (tools, "
                                "reasoning, text on stderr), -v. Exit 0 ok, 1 failed "
                                "or out of turns, 2 bad usage, 3 provider/auth, 4 "
                                "no such thread, 5 context full, 124 timeout, 130 "
                                "Ctrl-C.",
              kCmdGutter),
        entry("  mcp-serve", "Serve agentty's native tools over MCP (stdio). Point any "
                             "MCP client at `agentty mcp-serve`.", kCmdGutter),
        entry("  mcp import", "Copy another tool's MCP servers into "
                             "~/.agentty/mcp.json (`--from junie|claude|cursor|"
                             "vscode|codex`, `--dry-run`). No args lists what each "
                             "tool offers.", kCmdGutter),
        entry("  mcp-login <srv>", "Authorize an OAuth-gated MCP server from mcp.json "
                                   "(2026-07-28 OAuth 2.1 + PKCE via your browser).",
              kCmdGutter),
        entry("  mcp-logout <srv>", "Remove a stored MCP server token.", kCmdGutter),
        entry("  mcp-status", "List MCP servers and their authorization state.", kCmdGutter),
        entry("  diagnostics", "Collect a redacted diagnostic bundle for a bug report "
                               "(build info, logs, config \u2014 no secrets)", kCmdGutter),
        entry("  skills", "List discovered skills with spec-lint diagnostics "
                          "(exit 1 on warnings \u2014 CI-friendly validate)", kCmdGutter),
        entry("  config [what]", "Every folder agentty reads or writes, where it is and "
                                "how big. `config <name>` for one, `config env` for the "
                                "storage variables", kCmdGutter),
        entry("  config move", "`config move <dir> <path>`: move a folder's data "
                              "safely, then print the export line to add", kCmdGutter),
        entry("  config doctor", "Check storage permissions and variables", kCmdGutter),
        entry("  config clean", "List files old versions left behind (--yes deletes)",
              kCmdGutter),
        entry("  hooks [list]", "Show configured lifecycle hooks + approval state", kCmdGutter),
        entry("  hooks approve", "Inspect + approve the active hooks file (hooks NEVER "
                                 "run unapproved; any change re-gates)", kCmdGutter),
        entry("  plugin <verb>",
              "Manage plugins (add | list | remove | approve) \u2014 a plugin IS an MCP "
              "server, any language, one mcp.json entry: `agentty plugin add today "
              "--python today.py`, `--uvx pkg`, `--npx pkg`, `--http <url>`, or "
              "`-- cmd args`; add --project for the repo config (approve it with "
              "`plugin approve <name> --project`). Docs: /docs/plugins", kCmdGutter),
        entry("  rag-bench [dir]", "Benchmark search_docs retrieval on your own corpus "
                                   "(recall@k / MRR / nDCG per pipeline stage)", kCmdGutter),
        entry("  update", "Update agentty to the latest release "
                          "(--check: only report, don't install)", kCmdGutter),
        entry("  version", "Print the agentty version and exit", kCmdGutter),
        entry("  help", "Show this message", kCmdGutter),
        blank(),
        heading("options"),
        entry("  -k, --key KEY", "API-key override for this session", kOptGutter),
        entry("  -m, --model ID", "Model id (e.g. claude-opus-4-5)", kOptGutter),
        entry("  -w, --workspace DIR", "Sandbox filesystem tools to this directory "
                                       "(default: cwd). Tools refuse paths outside it. "
                                       "Pass `--workspace /` to disable the gate.", kOptGutter),
        entry("      --sandbox MODE", "Wrap bash/diagnostics in an OS-native sandbox "
                                      "(claybin; macOS falls back to sandbox-exec). "
                                      "MODE = auto "
                                      "(default: use if available), on (require backend; "
                                      "fail otherwise), off (disable wrapping).", kOptGutter),
        entry("      --sandbox-backend B", "Sandbox implementation: claybin (the default "
                                            "and only engine on Linux and Windows; Linux "
                                            "adds seccomp + landlock + cgroup2, macOS uses "
                                            "seatbelt + rlimits). On macOS a host that "
                                            "cannot run claybin falls back to sandbox-exec "
                                            "(one fixed profile), which ignores most Sandbox "
                                            "settings. `bwrap` is accepted and runs claybin.",
              kOptGutter),
        entry("      --log-file PATH", "Write the diagnostic log here instead of "
                                       "~/.agentty/logs/agentty.log (or "
                                       "$AGENTTY_LOGS_DIR, to move just the logs off "
                                       "the config disk). What gets captured "
                                       "is AGENTTY_LOG (default: warnings and errors) \u2014 "
                                       "e.g. AGENTTY_LOG=debug, or AGENTTY_LOG=wire=trace "
                                       "for raw HTTP bytes.", kOptGutter),
        entry("      --events jsonl", "Headless `run`: one JSON object per line on stderr: "
                                      "`turn` at each model turn, `reasoning` and `text` "
                                      "as the model writes them, `tool` per tool call "
                                      "(tool, ms, ok, err, args_sha). stdout keeps the "
                                      "answer, so both can be captured.", kOptGutter),
        entry("  -p, --profile MODE", "ACP permission tier (Zed shows the prompts): "
                                      "ask (default: prompt write/exec/net), minimal "
                                      "(also prompt reads), write (never prompt reads).",
              kOptGutter),
        entry("      --provider P", "LLM backend. anthropic (default, OAuth/Pro/Max) or an "
                                    "OpenAI-compatible one: openai | codex | groq | "
                                    "openrouter | together | cerebras | deepseek | xai | "
                                    "mistral | gemini | fireworks | ollama | llama.cpp, or "
                                    "a raw host[:port] for any other OpenAI-compatible "
                                    "server. Reads OPENAI_API_KEY (or the "
                                    "provider-specific *_API_KEY) / -k for the key; local "
                                    "backends need no key. Persisted like -m. (Switch live "
                                    "in-app with Ctrl-P \u2014 the picker has a "
                                    "\"Custom host\u2026\" entry too.) chatgpt talks to ChatGPT "
                                    "natively via the reverse-engineered OAuth login "
                                    "(`agentty login` \u2192 ChatGPT); no Codex binary is "
                                    "needed. copilot and kimi sign in the same way via "
                                    "their device-flow OAuth (`agentty login` \u2192 GitHub "
                                    "Copilot / Kimi) \u2014 no API key needed. Note: hosted "
                                    "Claude output is watermarked per the EU AI Act "
                                    "(invisible, no opt-out, set server-side). "
                                    "Local/open-weight backends (ollama, llama.cpp) carry "
                                    "no provider watermark.", kOptGutter),
        entry("  -V, --version", "Print the agentty version and exit.", kOptGutter),
        entry("      --auth-header N", "Auth header NAME for OpenAI-compatible backends "
                                       "whose gateway doesn't accept `Authorization: "
                                       "Bearer` (e.g. X-API-Key). The key (-k / "
                                       "OPENAI_API_KEY) is sent raw under that name. "
                                       "Session-scoped like -k; default: Bearer.", kOptGutter),
        entry("  -h, --help", "Show this message.", kOptGutter),
        blank());

    // 一个 输出路径, 平坦, 到处都是。
    // 唯一不同的是 我们所要的宽度: 一个真实的
    // 终端 得到自己的,其他的都得到80。
    // 因为两个极端都读得很好—— 在60以下的描述栏太窄,
    // 不值得拥有, 超过100,眼睛在返回的路上失去线条。
    int w = 80;
    if (::isatty(fileno(stderr))) {
        const int tw = maya::detail::detect_terminal_width();
        if (tw > 0) w = std::clamp(tw, 60, 100);
    }
    std::fputs(maya::render_to_string(body, w).c_str(), stderr);
}

struct Args {
    std::string subcommand;
    std::string cli_key;
    std::string cli_model;
    std::string cli_workspace;
    std::string cli_sandbox;   // "auto" | "on" | "off" ; 空 = 自动默认
    // 仅 "claybin" 意味着现在的一切; "bwrap" 仍然 解析 运行
    // claybin 带个字条 Linux 仅此而已。
    std::string cli_sandbox_backend;
    // UI 语言 此 运行,作为 BCP -47 标签( "de", "pt-BR", "zh-CN").
    // 胜过 已保存 环境与环境, 与其他环境一样 标志:
    // 用户 带类型的 运行不是第二 -guessed.
    std::string cli_lang;
    std::string cli_profile;   // "write" | "ask" | "minimal"; ACP 仅限
    std::string cli_provider;  // "anthropic" | "openai" | "ollama" | "llama.cpp" | 东道主 [: port]
    std::string cli_auth_header; // 自定义认证 头文件 NAME (e.g. "X-API-Key")
    std::string cli_bench_root;  // rag-bench: docs root 覆盖(位置)
    std::string cli_mcp_server;  // mcp-login/logout:服务器名称(位置)
    std::string cli_mcp_metadata; // 微缩 -login : 明确资源 -metadata URL
    std::string cli_mcp_client_id; // 微缩 -login 预览 -registered / CIMD client_id
    std::string cli_run_prompt;    // 运行: 其中一个 -shot 提示词 (职位)
    bool cli_run_prompt_claimed = false;  // 扫描是否用到一个
                                          // (空的提示词仍是索赔)
    std::string cli_run_agent;     // 运行: --agent 探险家 | Reviewer |__| 常规
    std::string cli_run_output;    // 运行 : -o FILE
    std::string cli_run_max_turns; // 运行: --max-turns N( 稍后得到确认)
    std::string cli_run_timeout;   // 运行:--timeout 90s|5m|1h
    bool cli_run_json = false;     // 运行: --json
    bool cli_run_verbose = false;  // 运行: -v
    bool cli_run_read_only = false;   // 运行: --read-only
    bool cli_run_no_network = false;  // 运行: --no-network
    bool cli_run_save = false;        // 运行: --save
    std::string cli_run_effort;       // 运行 : --effort LEVEL
    std::string cli_run_resume;       // 运行 : --resume ID| 最后一个
    std::vector<std::string> plugin_argv;  // 插件:动词+末尾,逐字记录
    int         airgap_argc = 0;
    char**      airgap_argv = nullptr;   // 从主的 argv 借来的
    bool        bad = false;
};

Args parse_args(int argc, char** argv) {
    Args out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "login" || a == "logout" || a == "status" || a == "help"
         || a == "acp" || a == "skills" || a == "mcp-serve"
         || a == "diagnostics") {
            out.subcommand = std::move(a);
        } else if (a == "config") {
            // `agentty config [concern]` - 选择性考虑 骑在
            // plugin_argv ,同样的逐字记录 -tail 频道 `plugin` 使用时。
            out.subcommand = std::move(a);
            while (i + 1 < argc) out.plugin_argv.emplace_back(argv[++i]);
        } else if (a == "update") {
            // `agentty update [--check]` — --check 骑马乘车
            // cli_run_agent (重用刮伤;更新没有代理).
            out.subcommand = std::move(a);
            if (i + 1 < argc && std::string{argv[i + 1]} == "--check") {
                out.cli_run_agent = "--check";
                ++i;
            }
        } else if (a == "hooks") {
            // `agentty hooks [list|approve]` 动词会进入
            // cli_run_agent (重用刮;钩无剂.
            out.subcommand = std::move(a);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                out.cli_run_agent = argv[++i];
        } else if (a == "mcp") {
            // `agentty mcp import [...]` 动词+ 末尾 以逐字记录方式乘车进入 plugin_argv ,
            // 同一频道 `plugin` 财务报告和已审计财务报表 `config` 使用时使用。
            out.subcommand = std::move(a);
            while (i + 1 < argc) out.plugin_argv.emplace_back(argv[++i]);
        } else if (a == "mcp-login" || a == "mcp-logout" || a == "mcp-status") {
            // `agentty mcp-login <server> [--metadata <url>] [--client-id <id>]`
            out.subcommand = std::move(a);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                out.cli_mcp_server = argv[++i];
            while (i + 1 < argc) {
                std::string opt = argv[i + 1];
                if (opt == "--metadata" && i + 2 < argc)       out.cli_mcp_metadata  = argv[i += 2];
                else if (opt == "--client-id" && i + 2 < argc) out.cli_mcp_client_id = argv[i += 2];
                else break;
            }
        } else if (a == "rag-bench") {
            // 可选位置docs根:`agentty rag-bench [dir]`.
            out.subcommand = std::move(a);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                out.cli_bench_root = argv[++i];
        } else if (a == "rag-eval") {
            // `agentty rag-eval <cases.json>` : 判断 search_code 爱华.
            out.subcommand = std::move(a);
            if (i + 1 < argc) out.cli_bench_root = argv[++i];
        } else if (a == "run") {
            // 头无一 -shot: `agentty run "prompt" [--agent TYPE]`.
            // 缺失/ `-` 提示词 读取 stdin( 管道使用量 :
            // `git diff | agentty run -` 或 或 时 间 `… | agentty run`).
            out.subcommand = std::move(a);
            // 扫描 整个 末尾 不只是主要争论
            //
            // 这曾经是 `break` 初选时它没有自己 所以外环可以
            // 看到 -m/-w /... 这意味着一个 提示词 在一个
            // 从未在此提出权利主张的情形下——它掉到外圈,死亡。
            // "unknown arg"。所以,我们想让您知道
            //
            //     agentty run --agent coder "prompt"        worked
            //     agentty run -w dir "prompt"                did not
            //
            // 规则和规则(A/C.5/52/L.10) "a positional may not follow a global flag"
            // )是隐形的: 错误命名为 提示词 ,这是罚款。
            //
            // 相反,我们只走每一个剩余参数,
            // 只取这个子命令拥有的东西,而留下剩下的东西。
            // 我们跳过 argv 来保留外环,
            // 而外环已经写成了忽略它消耗的东西的选项 — —
            // 因此命令停止重要,而不需要了解另一环。
            for (int j = i + 1; j < argc; ++j) {
                const std::string opt = argv[j];
                if (opt == "--agent" && j + 1 < argc) {
                    out.cli_run_agent = argv[++j];
                    continue;
                }
                if ((opt == "-o" || opt == "--output") && j + 1 < argc) {
                    out.cli_run_output = argv[++j];
                    continue;
                }
                if (opt == "--max-turns" && j + 1 < argc) {
                    out.cli_run_max_turns = argv[++j];
                    continue;
                }
                if (opt == "--timeout" && j + 1 < argc) {
                    out.cli_run_timeout = argv[++j];
                    continue;
                }
                if (opt == "--json")              { out.cli_run_json = true;    continue; }
                if (opt == "-v" || opt == "--verbose") { out.cli_run_verbose = true; continue; }
                if (opt == "--read-only")  { out.cli_run_read_only = true;  continue; }
                if (opt == "--no-network") { out.cli_run_no_network = true; continue; }
                if (opt == "--save")       { out.cli_run_save = true;       continue; }
                if (opt == "--effort" && j + 1 < argc) {
                    out.cli_run_effort = argv[++j];
                    continue;
                }
                if (opt == "--resume" && j + 1 < argc) {
                    out.cli_run_resume = argv[++j];
                    continue;
                }
                // 非 国 家 -flag (或赤裸裸的) "-" ,意思是 stdin)是: ,
                // 是"(")",意思是"(sdin)",是""("")". 提示词。
                // 只限 首次 一,第二个职位是值得报告的错误,而不是第二个 提示
                // 词 静静地凝聚
                if (opt.empty() || opt[0] != '-' || opt == "-") {
                    // `claimed`,不是`!prompt.empty()`:
                    // 一个EMPTY(`agentty run ""`)位置
                    // 仍然是提示词,扫描取走了所有权. 键入空心意味着它永远
                    // 无法在下文未知的-arg检查中匹配自己,
                    // 因此`agentty run ""`报告为`unknown arg:`,
                    // 其名称为空白,而不是真正的"no prompt"错误.
                    if (!out.cli_run_prompt_claimed) {
                        out.cli_run_prompt = opt;
                        out.cli_run_prompt_claimed = true;
                    }
                    continue;
                }
                // A 标志 此子命令不拥有 — 也可能没有它的值。
                // 跳过值的大小 : in `-w dir "prompt"`
                // 编号 `dir` 是非对齐的 -flag ,所以没有这个,
                // 它会被视为 提示词 和真实的 提示词 忽略了这些
                //
                // 任意 `-x VALUE` 形状符合条件,
                // 这是安全的,因为假阳性只让我们失去一个
                // 候选人 提示词 后期位置取代了——外环是
                // 实际验证这些位置的环. 标志.
                if (j + 1 < argc && argv[j + 1][0] != '-') ++j;
            }
        } else if (a == "paste") {
            // `agentty paste [cmd] …` — richpaste自己的CLI拥有末尾.
            out.subcommand = std::move(a);
            while (i + 1 < argc) out.plugin_argv.emplace_back(argv[++i]);
            break;
        } else if (a == "plugin" || a == "skill") {
            // `agentty plugin <verb> …` / `agentty skill <verb> …`
            // - 整个手 末尾 切换到子命令 CLI 逐字记录(每个都有自己的 标志:
            // --uvx/--python/--npx /- 用于插件, --force/
            // --yes 以技能为目的。 NOTE: `skill` (安装一个)与 `skills`
            // (lint every discovered one)——不同的动词,
            // 保持分开,所以两个模式都无法生长 标志.
            out.subcommand = std::move(a);
            for (int j = i + 1; j < argc; ++j)
                out.plugin_argv.emplace_back(argv[j]);
            return out;
        } else if (a == "sessions") {
            // `agentty sessions [--dump N]` ——名单 Codex /
            // Claude 代码会话 agentty 还可以继续
            out.subcommand = std::move(a);
            for (int j = i + 1; j < argc; ++j)
                out.plugin_argv.emplace_back(argv[j]);
            return out;
        } else if (a == "airgap") {
            // 交出剩下的方格 末尾 以至逐字显示的空格子命令,
            // 以便它能够运行自己的 标志 解析 无需重写 -implementing
            // 停止扫描——顶部 -level 标志 别申请了
            out.subcommand   = std::move(a);
            out.airgap_argc  = argc - (i + 1);
            out.airgap_argv  = argv + (i + 1);
            return out;
        } else if ((a == "-k" || a == "--key") && i + 1 < argc) {
            out.cli_key = argv[++i];
        } else if ((a == "-m" || a == "--model") && i + 1 < argc) {
            out.cli_model = argv[++i];
        } else if ((a == "-w" || a == "--workspace") && i + 1 < argc) {
            out.cli_workspace = argv[++i];
        } else if (a == "--sandbox" && i + 1 < argc) {
            out.cli_sandbox = argv[++i];
        } else if (a == "--sandbox-backend" && i + 1 < argc) {
            out.cli_sandbox_backend = argv[++i];
        } else if (a == "--lang" && i + 1 < argc) {
            out.cli_lang = argv[++i];
        } else if (a == "--events" && i + 1 < argc) {
            // 选择无头机器 -readable 事件
            // 数据流 用于环境而不是Args,
            // 因为排放者生活在工具后端,远远低于 main()
            // 读过一次,懒惰的,所以在这里,
            // 在argv 解析 每一个消费者都早到了
            const std::string_view fmt = argv[++i];
            if (fmt != "jsonl") {
                std::fprintf(stderr,
                    "unknown --events format: %.*s (only 'jsonl')\n",
                    static_cast<int>(fmt.size()), fmt.data());
                std::exit(2);
            }
#if defined(_WIN32)
            _putenv_s("AGENTTY_EVENTS", "jsonl");
#else
            ::setenv("AGENTTY_EVENTS", "jsonl", 1);
#endif
        } else if (a == "--log-file" && i + 1 < argc) {
            // 立即应用, 而不是隐藏在 args 上:
            // 日志沉淀器可以懒散地解决其 首次 用途,
            // 而在这里到 解析 结束之间, 某些东西可能会日志。
            // 设定它就是在文件打开前保证覆盖区域。
            ::agentty::logx::set_log_path(argv[++i]);
        } else if ((a == "-p" || a == "--profile") && i + 1 < argc) {
            out.cli_profile = argv[++i];
        } else if (a == "--provider" && i + 1 < argc) {
            out.cli_provider = argv[++i];
        } else if (a == "--auth-header" && i + 1 < argc) {
            out.cli_auth_header = argv[++i];
        } else if (a == "-h" || a == "--help") {
            out.subcommand = "help";
        } else if (a == "-V" || a == "--version" || a == "version") {
            // 独立版本子命令 / 标志。作为顶级 -level
            // 分派 路径所以短 -circuits 其余的舰队...
            // `agentty --version -k garbage`
            // 不该抱怨那些没用的 -k.
            out.subcommand = "version";
            return out;
        } else {
            // 参数 `run` 已经声称的扫描并非未知——
            // 他们来到这里是因为他们坐在一个全球之后。
            // 标志 ,扫描时故意将一切留在原位上,
            // 而不是重排argv.
            //
            // 如此,所有安置工作都这样:
            //
            //     agentty run --agent coder "prompt"
            //     agentty run -w dir "prompt"
            //     agentty run --agent coder -w dir "prompt"
            //
            // 之前扫描在第一次停止 标志 它没有,所以 提示词
            // 之后 -w/-m 从未被声称并死在这里
            // "unknown arg" ——隐形规则,
            // 因为错误命名为 提示词 而不是命令。
            if (out.subcommand == "run") {
                if (a == "--agent" || a == "-o" || a == "--output"
                    || a == "--max-turns" || a == "--timeout"
                    || a == "--effort" || a == "--resume") { ++i; continue; }
                if (a == "--json" || a == "-v" || a == "--verbose" || a == "--read-only"
                    || a == "--no-network" || a == "--save") continue;
                if (out.cli_run_prompt_claimed && a == out.cli_run_prompt)
                    continue;
            }
            std::fprintf(stderr, "unknown arg: %s\n\n", a.c_str());
            out.bad = true;
            return out;
        }
    }
    return out;
}

} // 命名空间结束。

#if defined(_WIN32)
// RAII 守护 Windows-specific 进程退出时必须取消的调试 :
//   - timeBeginPeriod (1): 冲击系统 -wide 计时器中断
//     默认为 15.625 ms ms。 每张睡眠, WaitForSingleObject
//     超时 和 std::this_thread::sleep_for 都尊重这个层,
//     因此 加载动画 勾选 / 流式传输 帧 间隔 / 输入 -poll cadence
//     变得平滑, 而不是踩上 ~16 ms 网格。 副作用 是一个全球级的,
//     所以我们必须在 拆卸时将其与 timeEndPeriod (1) 配对。
//   - SetPriorityClass(ABOVE_NORMAL): interactive TUI — we want our
//     render/input 循环以预置背景 编译 或对用户
//     的黑 CPU。不影响 quescent 进程;
//     只购买争议 -time 反应能力。
struct Win32PerfTuning {
    bool hi_res_timer = false;
    Win32PerfTuning() {
        if (::timeBeginPeriod(1) == TIMERR_NOERROR) hi_res_timer = true;
        ::SetPriorityClass(::GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }
    ~Win32PerfTuning() {
        if (hi_res_timer) ::timeEndPeriod(1);
    }
};

// Windows 进程内提供窄方格V ANSI/OEM 代码页,
// 所以不 -ASCII 路径参数( 参数) ( `-w C:\Users\Ünïcode` ,
// 一个 @ -mentioned Unicode 文件名)作为摩吉贝和每枚
// 下游 fs::path / 获取比较失败。 恢复 真实 命令行通过
// GetCommandLineW + CommandLineToArgvW (总是) UTF
// 16),还原 -encode 每个参数改为( E) UTF -8,
// 这就是全部 代码库 已假定窄字符串是。返回的存储拥有字符串;调用者将 argv 指向
// c_str () 活得像它一样长。
struct Utf8Argv {
    std::vector<std::string> store;
    std::vector<char*>       ptrs;
    int                      argc = 0;
    char**                   argv = nullptr;
};

Utf8Argv recover_utf8_argv() {
    Utf8Argv out;
    int wargc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
    if (!wargv || wargc <= 0) { if (wargv) ::LocalFree(wargv); return out; }
    out.store.reserve(static_cast<size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        int need = ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                         nullptr, 0, nullptr, nullptr);
        std::string s;
        if (need > 1) {
            s.resize(static_cast<size_t>(need - 1));  // 放弃NUL
            ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(),
                                  need, nullptr, nullptr);
        }
        out.store.push_back(std::move(s));
    }
    ::LocalFree(wargv);
    out.ptrs.reserve(out.store.size() + 1);
    for (auto& s : out.store) out.ptrs.push_back(s.data());
    out.ptrs.push_back(nullptr);
    out.argc = static_cast<int>(out.store.size());
    out.argv = out.ptrs.data();
    return out;
}
#endif

namespace {
// "agentty ~/path" : 数据 工作区 (--workspace ,
// 默认 cwd),在窗口标题中,可以区分并行实例。 $HOME 显示为~;
// 赤底名称会碰撞到相同的位置 -named 5 项目。
std::string window_title() {
    namespace fs = std::filesystem;
    try {
        const fs::path& root = agentty::tools::util::workspace_root();
        fs::path shown = root;
        // 回家去,买 weakly_canonical set_workspace_root
        // ()给根,所以一个连结 $HOME 仍然匹配,一个家 "/" 被跳过,
        // 作为bash's 提示词 已执行;任何其他不匹配(驱动,大小写)都显示全路径。
        if (const fs::path home = agentty::util::home_dir_or_empty(); !home.empty()) {
            std::error_code ec;
            const fs::path canon_home = fs::weakly_canonical(home, ec);
            if (!ec && canon_home.has_relative_path()) {
                const fs::path rel = root.lexically_relative(canon_home);
                if (!rel.empty() && *rel.begin() != "..")
                    shown = rel == "." ? fs::path{"~"} : fs::path{"~"} / rel;
            }
        }
        // generic_u8string(): '/' 每个平台上的分隔器,
        // 所以标题在任何地方读得都一样(C:/). Users/me/proj 打开 Windows).
        // UTF -8号,不是 string() 打开 Windows 穿过了 ANSI
        // 密码页和 MSVC 扔出它无法代表的字符。
        const auto u8 = shown.generic_u8string();
        const std::string text = agentty::tools::util::sanitize_utf8(
            {reinterpret_cast<const char*>(u8.data()), u8.size()});
        // maya 将标题逐字写入内部 OSC 零,以及 5,6,6,7,6,8,6,5,6 6,6 7,6 5,7,7,8,7,5,7
        // Linux/macOS 允许名称中的控制字符: a ESC 或 或 时 间 BEL 结束 OSC 早期和注射序列。
        // C0, DEL 财务报告和已审计财务报表 C1 (UTF-8 C2 80.9F)成为: '?'.
        std::string out = "agentty ";
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            const bool c1 = c == 0xC2 && i + 1 < text.size()
                && static_cast<unsigned char>(text[i + 1]) >= 0x80
                && static_cast<unsigned char>(text[i + 1]) <= 0x9F;
            if (c < 0x20 || c == 0x7F || c1) {
                out += '?';
                if (c1) ++i;
                continue;
            }
            out += text[i];
        }
        return out;
    } catch (const std::exception& e) {
        // 一样 回退 prompt.cpp 用于不可转换的 cwd:
        // 登录并保留固定标题, 而不是中止启动。
        agentty::util::dbglog("startup.window_title", e.what());
        return "agentty";
    }
}

// 程序,从一个 线程 开始持有否 锁。所有内容 main()
// 在这儿,没有改变,除了它可以 PROVE 坚持不 锁:
// `cap` 是根能力 每一个 锁 ,然后核对下面的组合。
// main() (在本文件底部) 薄荷,一次。
int run_main(int argc, char** argv, const maya::unlocked& cap) {
    using namespace agentty;
    startup::mark("run_main");

    // 任何定律都发生在这里,在... 线程 存在: setenv 竞速在另一个游戏上
    // 获得一个 getenv 线程。旧选项 -in 拼写 maya 门中止
    // MAYA_NO_GATE_ABORT=0/false ; 翻译.
    if (const char* g = std::getenv("MAYA_NO_GATE_ABORT");
        g && (std::string_view{g} == "0" || std::string_view{g} == "false")) {
#if defined(_WIN32)
        _putenv_s("MAYA_GATE_ABORT", "1");
#else
        setenv("MAYA_GATE_ABORT", "1", /*覆盖=*/0);
#endif
    }

    install_crash_handler();

    // AGENTTY_FORCE_EFFORT 财务报告和已审计财务报表
    // AGENTTY_MAX_OUTPUT_TOKENS 进入能力 注册表 ,一次,每种模式( TUI。
    // 运行, acp。域名不读取环境。
    set_effort_force(parse_effort_force(std::getenv("AGENTTY_FORCE_EFFORT")));
    set_max_output_override(parse_max_output(std::getenv("AGENTTY_MAX_OUTPUT_TOKENS")));

    // ── 倒闭, 走EVERY 出口路径 ────────────────────────────────────
    //
    // main() 拥有 ~20 的早期返回( 帮助, 版本, 登录, 状态,
    // 技能, 插件, mcp- *, airgap,...) TUI 路径的清晰
    // run() 只能用一个瞄准镜来掩护他们, 而不是在每一个人身上加一条线。
    // "remember to add a line" 这正是失败留下的
    // settings_cache 写入 -behind 工作线程 无法结合,
    // 所以 std::thread 退出时中止了进程。
    //
    // 警卫在任何静态毁灭器之前就跑 也就是...
    // 不变量 具有重要性的方面:背景 线程 必须在
    // 静态静态还存在时停止触摸其捕获的静态态态。
    // 线程 (util::teardown 因此,
    // 随着子系统被添加,这一点仍然正确——没有人需要更新一个列表.
    //
    // 警卫员参照run_main的能力:在run_main
    // 的帧的末端销毁,该能力仍然有效,
    // 没有保留锁(每锁XX正文是一个巢穴范围,已经结束).
    struct TeardownGuard {
        const maya::unlocked& cap;
        ~TeardownGuard() {
            // 这四个运行在EVERY的退出路径上,而不只是正常的一条.
            //
            // 他们以前坐进来的 jaal::run () 返回,包括一个干净的退出,
            // 而其他的:一个早期 `return` 从子命令中,
            // 失败 Screen::open ,或例外的解开 main() 都跳过,
            // 留下了背景 线程 运行中 CRT/OpenSSL/mimalloc
            // 即0xC00000005 的拆卸, 即为0xC000005
            // MSYS2 烟斗 -stdin 抽烟试验
            // 断断续续地进行: `type NUL | agentty.exe`
            // 在下图中关闭 stdin TUI 已经抽了,
            // 所以预温拨号和 工作区 散步还在开始呢 main() 已经出发了
            // 是否有问题取决于谁赢了, 这就是为什么它失败了 也许
            // 一跑三 字节-identical 代码。
            //
            // 命令事项:在运行注册表之前停止LOG的工作线程,
            // 因为logx自己的工作线程是注册的拆解物
            // 之一.
            if (auto* c = http::default_client_if_made()) c->join_prewarm(cap);
            modelsdev::join_background_refresh(cap);
            blobs::join_background_gc(cap);
            util::teardown::run(cap);
        }
    } teardown_guard{cap};

#if defined(_WIN32)
    Win32PerfTuning win32_perf;
    // 换掉 ANSI-codepage 窄方格为 a UTF -8个从真
    // 货中找回来的 UTF - 16条命令线,所以不 -ASCII 路径参数存活。
    // 保持存储全部 main(); argc/argv 下指其内.
    Utf8Argv win32_argv = recover_utf8_argv();
    if (win32_argv.argv && win32_argv.argc > 0) {
        argc = win32_argv.argc;
        argv = win32_argv.argv;
    }
#endif

    // ── 忽略 SIGPIPE 进程 -wide ────────────────────────────────────
    // agentty 写给许多管子,它不能完全控制:产卵 MCP 插件服务器
    // ( stdio), 即 sandbox/bash 子进程,剪贴板帮助。
    // 如果 任意 中途死亡的,他们中途死亡 -write ——一个在启动握手时崩溃的插件,
    // 一个服务器指向一个不良路径,它产物然后退出——写给现在的 -closed
    // 管道发送 SIGPIPE ,其名称为 DEFAULT 动作为 KILL 过程,
    // 这是真的 "agentty suddenly crashes"
    // 原因: 一个片面插件在没有中止消息的情况下将整个应用程序下架。
    // 单个使用的网站 MSG_NOSIGNAL / 当地 SIG_IGN ,
    // 但是stdio(英语: -pipe 写( A) MCP 传输层 不能,
    // 所以忽略它 全球,处理写作失败 通过 EPIPE 返回代码。必须运行 首次
    // 在任何孩子出生之前 都不要离开
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
    // Dev bug-marker:`kill -USR1 $(pgrep agentty)`用
    // MARK旗标+飞行-recorder 快照在目前一个bug是SEEN时,将直播日志印上.
    // signal_mark () 是同步 -signal-safe (静态) 字节 ,原始写作(2)s).
    std::signal(SIGUSR1, [](int) { agentty::logx::signal_mark(); });
#endif

    auto args = parse_args(argc, argv);
    startup::mark("args parsed");

    // `agentty paste …` 运行在日志横幅之前, 并在下面每个输入 :
    // 运行 `agentty paste ssh-hook` ,所以必须迅速安静。
    // 它尊重设置 ~ 粘贴像 Ctrl +V 显示,
    // 和 `agentty paste config` 编辑这些设置。
    if (args.subcommand == "paste")
        return paste_cli::run(::agentty::IoAccess::grant(), cap, args.plugin_argv);

    // 在(附录-mode, multi-session)日志中,每一次运行的自-identifies:
    // 版本+构建类型+pid + cwd. `grep "=== agentty"`
    // 将文件拆分为运行; 共享日志名称精确 二进制程序 导致它。
    {
        std::string banner;
        banner += ::agentty::version();
#if defined(NDEBUG)
        banner += " release";
#else
        banner += " debug";
#endif
        banner += " pid=";
        banner += std::to_string(agentty_pid());
        std::error_code cwd_ec;
        const auto cwd = std::filesystem::current_path(cwd_ec);
        if (!cwd_ec) { banner += " cwd="; banner += cwd.string(); }
        agentty::logx::session_banner(banner);
    }
    if (args.bad)                    { print_usage(); return 2; }

    // ── 启动横幅 ────────────────────────────────────────────────
    // ONE线,在每个日志中第一,命名建设和机器.
    //
    // 没有它,日志片段就无法解答:每份错误报告头三个问题都是
    // 哪个版本、哪个操作系统、哪个提供方？
    // 而已经移动的用户很少回答全部三个。在此输入它——在参数之后
    // 解析 ,在任何工作之前——表示用户发送的每个日志都是自我的.
    // -describing 包括一个在坠机中结束的
    //
    // 信息级别: 以任何启用的配置显示, 无需跟踪。
    // 警告, 不是Info: 这是任何错误报告里最有用的单行( )
    // version/OS/build ),而发布默认仅保留Warn+。
    // 不是错误——此处的关卡被选用 RETENTION ,
    // 这是诚实交易,每个过程一行。
    // 解决 OS / 拱 / 建 / pid 中包含 plain #
    // if 块,而不是嵌入 AGT_LOG(...) 宏参数中的指令 –
    // 宏参数中的指示不便携,并行程 -Wpedantic。
#if defined(_WIN32)
    const char* os_name   = "windows";
#elif defined(__APPLE__)
    const char* os_name   = "macos";
#else
    const char* os_name   = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch_name = "arm64";
#else
    const char* arch_name = "x86_64";
#endif
#if defined(NDEBUG)
    const char* build_kind = "release";
#else
    const char* build_kind = "debug";
#endif
    const long long pid_ll = static_cast<long long>(
#if defined(_WIN32)
        _getpid()
#else
        ::getpid()
#endif
    );

    AGT_LOG(General, Warn, "startup",
            "agentty {} os={} arch={} build={} pid={}",
            ::agentty::version(), os_name, arch_name, build_kind, pid_ll);

    if (args.subcommand == "help")    { print_usage();   return 0; }
    if (args.subcommand == "version") { print_version(); return 0; }
    if (args.subcommand == "diagnostics") return cmd_diagnostics();
    // 密码句 提示词 读 / dev/tty. Ask 这儿,在任何之前 工作线程 或
    // 时间 TUI 拥有 终端 ,所以它从来没有 竞态 maya '输入读取器。
    (void)auth::crypt::passphrase_active();
    if (args.subcommand == "update") {
        const bool check_only = args.cli_run_agent == "--check";
        std::printf("agentty %s — checking for updates…\n",
                    update::current_version().c_str());
        auto c = update::check_latest(/*力=*/true);
        if (!c.error.empty()) {
            std::fprintf(stderr, "check failed: %s\n", c.error.c_str());
            return 1;
        }
        if (!c.update_available) {
            if (c.pending)
                // #119:最新发布存在,但这个平台 二进制程序 还在建设中。
                // "up to date" ,而不是错误 -- -- 等等。
                std::printf("✓ up to date — v%s is out, but its %s binary is still "
                            "building (usually under an hour). Run `agentty update` again "
                            "later.\n  %s\n",
                            c.latest.c_str(), update::platform_asset().c_str(), c.url.c_str());
            else
                std::printf("✓ up to date (latest is v%s)\n", c.latest.c_str());
            return 0;
        }
        std::printf("⬆ v%s available (you have v%s)\n  %s\n",
                    c.target.c_str(), c.current.c_str(), c.url.c_str());
        if (c.pending)
            std::printf("  (v%s is out too; its %s binary is still building)\n",
                        c.latest.c_str(), update::platform_asset().c_str());
        if (check_only) return 0;

        std::string why;
        if (!update::self_update_possible(why)) {
            std::fprintf(stderr, "%s\n", why.c_str());
            return 1;
        }
        std::printf("downloading %s…\n", update::platform_asset().c_str());
        auto err = update::perform_update(c.target,
            [](std::size_t got, std::size_t total) {
                if (total > 0)
                    std::printf("\r  %zu / %zu KiB (%d%%)   ",
                                got / 1024, total / 1024,
                                (int)(got * 100 / total));
                else
                    std::printf("\r  %zu KiB   ", got / 1024);
                std::fflush(stdout);
            }, c.asset_sha256);
        std::printf("\n");
        if (!err.empty()) {
            std::fprintf(stderr, "update failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("✓ updated to v%s — restart agentty to use it\n",
                    c.target.c_str());
        return 0;
    }
    if (args.subcommand == "login")  return auth::cmd_login(IoAccess::grant(), cap);
    if (args.subcommand == "logout") return auth::cmd_logout(IoAccess::grant(), cap);
    if (args.subcommand == "status") return auth::cmd_status(IoAccess::grant(), cap);
    if (args.subcommand == "skills") return tools::skills::cmd_skills(IoAccess::grant());
    if (args.subcommand == "config") return config::cmd_config(args.plugin_argv);
#if defined(AGENTTY_MCP)
    if (args.subcommand == "mcp") {
        if (args.plugin_argv.empty() || args.plugin_argv.front() != "import") {
            std::fprintf(stderr, "usage: agentty mcp import [--from <tool>]"
                                 " [--dry-run] [--force]\n");
            return 1;
        }
        return mcp::import_::cli(::agentty::IoAccess::grant(), cap, {args.plugin_argv.begin() + 1,
                                  args.plugin_argv.end()});
    }
#endif
    if (args.subcommand == "skill")  return tools::skills::cli(::agentty::IoAccess::grant(), cap, args.plugin_argv);
    if (args.subcommand == "hooks")  return tools::hooks::cli(::agentty::IoAccess::grant(), cap, args.cli_run_agent);
    if (args.subcommand == "plugin") return tools::plugin::cli(::agentty::IoAccess::grant(), cap, args.plugin_argv);
    if (args.subcommand == "mcp-login")
        return mcp::oauth::cmd_mcp_login(::agentty::IoAccess::grant(), args.cli_mcp_server, args.cli_mcp_metadata,
                                         args.cli_mcp_client_id);
    if (args.subcommand == "mcp-logout")
        return mcp::oauth::cmd_mcp_logout(args.cli_mcp_server);
    if (args.subcommand == "mcp-status")
        return mcp::oauth::cmd_mcp_status();
    if (args.subcommand == "rag-bench")
        return rag::bench::run(args.cli_bench_root);
    if (args.subcommand == "rag-eval") {
        // AGENTTY_RAG_EVAL_K =32表示回想起了更大的 池.
        int k = 8;
        if (const char* e = std::getenv("AGENTTY_RAG_EVAL_K"); e && std::atoi(e) > 0) k = std::atoi(e);
        return rag::bench::eval(args.cli_bench_root, k);
    }
    if (args.subcommand == "sessions")
        return foreign::cli(::agentty::IoAccess::grant(), args.plugin_argv);
    if (args.subcommand == "airgap") {
        // --clipboard-relay 启动粘贴代理: 给它设置 → 粘贴。
        clipboard_configure(persistence::load_settings(::agentty::IoAccess::grant()).paste);
        return airgap::cmd_airgap(args.airgap_argc, args.airgap_argv);
    }
    // 从这里开始,这是一场会议( TUI ,运行, acp, mcp -serve ),
    // 它写商店。 `agentty config move` 等着我们
    const auto store_lock = util::hold_store_shared();
    startup::mark("store lock");

    // 缺少的证书不再是一个致命错误: 我们安装一个空的认证 头文件,
    // init.cpp 打开该选项 -app 登录 弹出面板 ,用户则在 TUI。
    // 该词 状态更新函数's LoginExchanged / LoginSubmit
    // 处理函数 调用 auth::update_auth ( ) 活 着
    // -swaps 信条在 Deps 不需要重新启动进程。
    auto creds = auth::resolve(IoAccess::grant(), args.cli_key);
    startup::mark("creds resolved");

    // 将-m作为新的默认,但只用于交互式的TUI. 无头的`run`,
    // `acp`或`mcp-serve`只使用-m来进行这一过程;
    // 保存它可以改变打开的TUI的型号.
    const bool interactive = args.subcommand.empty();
    if (!args.cli_model.empty() && args.subcommand != "acp") {
        if (interactive) {
            auto s = persistence::load_settings(::agentty::IoAccess::grant());
            s.model_id = ModelId{args.cli_model};
            persistence::save_settings(::agentty::IoAccess::grant(), cap, s);
        }
        // 明确 --model 绑定每个角色, 不只是主转弯。
        // TUI's init() 也看到了--它不需要 argv (#70).
        smart::tuning::model_pinned() = true;
    }

    // ── 文件系统沙箱边界 ─────────────────────────────────────
    // 默认 工作区 = 处理 cwd. 触摸文件系统的工具( )
    // read/write/edit/list_dir/grep/glob/find_definition/git_*
    // /bash's `cd` ) 拒绝此根以外的路径并有明显的错误。
    // 通过( pass) `--workspace <dir>` 扩大 -- --
    // `--workspace /` 完全禁用门,供明确希望不受限制的用户使用。
    if (!args.cli_workspace.empty()) {
        std::filesystem::path req{args.cli_workspace};
        std::error_code ec;
        if (!std::filesystem::is_directory(req, ec)) {
            std::fprintf(stderr,
                "agentty: --workspace path is not a directory: %s\n",
                args.cli_workspace.c_str());
            return 2;
        }
        tools::util::set_workspace_root(::agentty::IoAccess::grant(), std::move(req));
    } else {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) tools::util::set_workspace_root(::agentty::IoAccess::grant(), std::move(cwd));
    }

    // ── UI语言 ─────────────────────────────────────────────────────
    startup::mark("workspace root");
    // 之前,我说 ANYTHING 渲染 或打印,所以一个状态行,
    // 错误,或第一个涂料无法用错误的语言逃避。 解析 一个
    // 嵌入 JSON 目录 并存储一个指针。
    //
    // 通过load_settings()读取已保存设置,
    // 这是下面的沙箱块所编的调用——settings.json读一次,
    // 这是它上的一个字段,而不是第二个文件.
    //
    // 这里的失败意味着EMBEDDED英语目录没有解析,
    // 这是一个建构缺陷而不是运行时条件.
    // Not致命:t()在没有安装目录时返回ids,
    // 所以应用程序仍然运行,屏幕上的ID是不可缺的bug报告.
    // 拒绝从标签表开始是更糟糕的交易.
    {
        const auto saved = persistence::load_settings(::agentty::IoAccess::grant());
        // Ctrl +V 读取剪贴板时用这些( 设置 ~ 粘贴)。
        clipboard_configure(saved.paste);
        if (!i18n::init(args.cli_lang, saved.ui.lang)) {
            std::fprintf(stderr,
                "agentty: the embedded English string catalog failed to "
                "parse; labels will show as ids. This is a build defect.\n");
        }
    }

    // ── 基准/诊断 沙箱 ──────────────────────────────────────
    startup::mark("i18n + settings");
    // 将 shell 命令包入 claybin (沙箱-exec 作为 macOS 回退 )所以
    // 一个批准 的狂欢 调用 读不出来 ~/.ssh ,写/etc,或 `rm -rf ~`.
    // `auto` (默认):如果可用,则使用日志警告。 `on`:
    // 如果后端缺失, 则会大声失败—— 对于不愿运行未装箱
    // 的用户来说. `off` : 明确选择 -out 用于
    // 用户有外部隔离的环境(Docker, VM 随便你
    {
        auto mode = tools::util::sandbox::Mode::Auto;
        if (args.cli_sandbox == "off")       mode = tools::util::sandbox::Mode::Off;
        else if (args.cli_sandbox == "on")   mode = tools::util::sandbox::Mode::On;
        else if (args.cli_sandbox == "auto"
              || args.cli_sandbox.empty())   mode = tools::util::sandbox::Mode::Auto;
        else {
            std::fprintf(stderr,
                "agentty: --sandbox must be auto, on, or off (got '%s')\n",
                args.cli_sandbox.c_str());
            return 2;
        }
        // 电话 已保存 政策,在 init() —— 内测,
        // 政策决定了值得探究的东西。一个从未触及过人类的配置
        // 面板 携带已运出默认值, 因此此为否 -op 以
        // 没有打开它的人发誓,
        //
        // 已加载 这里不是线条化的: 这个块在启动时运行一次,
        // load_settings ()是缓存的,
        // 通过两个中间的瞄准镜,比一个读数费用要多。
        //
        // ORDER: set_config () 也应用了政策本身的后端选择,
        // 所以它必须运行 之前 编号 CLI 标志 或明示
        // --sandbox-backend 将默默地被 已保存 一个
        // A 标志 用户 带类型的 这跑步比设定要好 已保存 一次,我记得,我说过,我...
        {
            auto sb = persistence::load_settings(::agentty::IoAccess::grant()).sandbox;
            // `run --no-network`:没有网络来指挥这个运行的产卵.
            // 只有这一过程;已保存设置没有受到影响.
            if (args.subcommand == "run" && args.cli_run_no_network) {
                sb.net_mode   = sandbox_cfg::NetMode::None;
                sb.configured = true;
            }
            tools::util::sandbox::set_config(sb);
        }

        // 保留了 --sandbox-backend, 但不再选择任何内容 :
        // claybin 是唯一一个 Linux 引擎。不要拒绝,
        // 而是拒绝这样一个脚本或别名,并携带 `--sandbox-backend=bwrap`
        // 继续工作,它说它做了什么 而不是默默地忽略 标志.
        if (!args.cli_sandbox_backend.empty()) {
            if (args.cli_sandbox_backend == "bwrap") {
                std::fprintf(stderr,
                    "agentty: bwrap has been removed; using claybin, which is "
                    "stronger on every measured capability (seccomp, landlock, "
                    "cgroup2) and available wherever bwrap was.\n");
            } else if (args.cli_sandbox_backend != "claybin") {
                std::fprintf(stderr,
                    "agentty: --sandbox-backend only accepts 'claybin' (got '%s')\n",
                    args.cli_sandbox_backend.c_str());
                return 2;
            }
        }
        bool ok = tools::util::sandbox::init(mode);
        if (!ok) {
            std::fprintf(stderr,
                "agentty: --sandbox=on but no backend available. %s\n",
                tools::util::sandbox::describe_state().c_str());
            return 2;
        }
        // 仅 claybin 网络可以从 shell 命令中移除。
        // 没有它 --no-network 承诺是没有人遵守的,所以拒绝。
        if (args.subcommand == "run" && args.cli_run_no_network
            && !(tools::util::sandbox::is_active()
                 && tools::util::sandbox::detected_backend()
                        == tools::util::sandbox::Backend::Claybin)) {
            std::fprintf(stderr,
                "agentty run: --no-network needs the claybin sandbox, which isn't active (%s)\n",
                tools::util::sandbox::describe_state().c_str());
            return 2;
        }
        // 克莱宾正在 活动 并不是承诺。它用一个空网子和拒绝非
        // 特权用户命名空间的主机将网络带走( Ubuntu
        // 24.04's AppArmor 默认, 许多容器)
        // 无法获得网格... claybin 降解
        // network.isolation 命令会用全网络运行 Ask
        // 除非墙壁坚固,否则它实际上能够执行的和拒绝的状态。
        if (args.subcommand == "run" && args.cli_run_no_network) {
#if defined(__linux__) || defined(__APPLE__)
            const auto rep = tools::util::sandbox::claybin_backend::describe(
                tools::util::sandbox::claybin_posture_for_test());
            bool net_strong = false;
            std::string why = rep.ok ? "no report" : rep.detail;
            for (const auto& w : rep.walls)
                if (w.name == "network.isolation") {
                    net_strong = w.strength == "strong";
                    why = w.strength + (w.mechanism.empty() ? "" : " via " + w.mechanism);
                }
            if (!net_strong) {
                std::fprintf(stderr,
                    "agentty run: --no-network can't be enforced on this host "
                    "(network isolation: %s). Network namespaces are "
                    "unavailable -- often unprivileged user namespaces are "
                    "disabled (sysctl kernel.apparmor_restrict_unprivileged_userns).\n",
                    why.c_str());
                return 2;
            }
#endif  // claybin 主机; 其他 is_active () 已经被拒绝的大门
        }
        // 状态线, 用户知道自己得到了什么。 Stdout是好的。
        // —— maya 返回后运行, 不得乱动。
        //
        // OFF 获得一个横幅,而不是一条线。
        // 这里的所有其他州都提供信息( "which wall is up" );
        // 关闭是下一个发生的事情是模型运行命令与真实文件系统对开,
        // 它们之间没有任何关系。 "sandbox: off"
        // 和我们坐在同一个视觉重量
        // "sandbox: active (claybin)" 并经过
        // 第一次的滚动 提示词 ——帆船3号要求你不能错过的东西,
        // 不对称是真实的:错的 ON 是个化妆品虫子, 错在 OFF
        // 整个机器。
        //
        // 建为 maya 元素和制造 maya::print ,而不是原始的
        // 逃生代码。这就是颜色的颜色。 THEME TOKEN (
        // ui::status_warn → ThemeSlot::Warning
        // )而不是硬码黄色:它通过用户的主动方案解决,所以它可以在灯光上
        // 清晰可见 终端 服从,服从, NO_COLOR ,
        // 和 theme_discipline_test 而不是
        // 需要一个例外的 刻在其中。
        //
        // maya::print 静态 -output 半数 maya (print.hpp
        // )——它(英语: 渲染 a 源码树 返回;无备选案文 -screen ,
        // 没有 事件 循环,没有 终端 状态恢复。这里安全,在 TUI 已存在。
        //
        // 穿过楼梯 render_to_string 因此它不能被误认为 Stdout 上
        // 的工具输出, 因为它 `agentty run` 呼叫者 解析.
        if (mode == tools::util::sandbox::Mode::Off) {
            using namespace maya::dsl;
            using maya::BorderStyle;
            using maya::BorderTextPos;
            using maya::BorderTextAlign;
            const auto warn = ui::status_warn;
            auto banner =
                vstack()
                    .padding(1, 2)
                    .border(BorderStyle::Double)
                    .border_color(warn)
                    .border_text(" \xe2\x9a\xa0  SANDBOX IS OFF ",
                                 BorderTextPos::Top, BorderTextAlign::Center)(
                        text("Tool commands run UNCONFINED \xe2\x80\x94 "
                             "they can read and write anything your user "
                             "account can: every file, the network, your "
                             "credentials.",
                             ui::fg_bold(warn)),
                        text(""),
                        text("Re-enable by starting agentty without  --sandbox off  "
                             "(the Settings pane can't turn it back on).",
                             ui::fg_dim(ui::muted)));
            // render_to_string_ansi ,没有 maya::print:
            // print() 写到 STDOUT ,和 `agentty run` 呼叫者
            // 解析 站出来,那面标志会毁了那面 数据流 脚本读作。
            //
            // 仅在需要时才显示颜色: ansi 变体 始终 发射
            // SGR ,所以,我说 NO_COLOR 且非 国家
            // -tty 斯德勒必须在这里受到尊重 而不是被 渲染器
            // 平原上 变体 绘制相同的文本框, 仅是未设置的文本
            // -- -- 日志文件或日志文件中的正确之处 CI 抓获,
            // 逃到哪里 字节 声音和画框仍然很好
            const char* no_color = std::getenv("NO_COLOR");
            const bool paint = ::isatty(fileno(stderr)) != 0
                            && !(no_color && no_color[0]);
            // 终端的宽度(stder's,因为这里就是它去
            // 的地方),封顶为74,以免在宽的上伸展;
            // 文本在盒子内包裹在狭的上.
            int width = 74;
            if (::isatty(fileno(stderr))) {
                // NativeHandle 是一个fd 上 POSIX 但不为 HANDLE 打开 Windows ,
                // 所以,我说 fileno() 在那里不会转换。
#if defined(_WIN32)
                const maya::platform::NativeHandle err_h = ::GetStdHandle(STD_ERROR_HANDLE);
#else
                const maya::platform::NativeHandle err_h = fileno(stderr);
#endif
                const auto sz = maya::platform::query_terminal_size(err_h);
                if (sz.width.raw() > 0) width = std::clamp(sz.width.raw(), 24, 74);
            }
            auto painted = paint
                ? maya::render_to_string_ansi(std::move(banner), width)
                : maya::render_to_string(std::move(banner), width);
            // 两者 变体 将每行都粘贴到 `width` ,
            // 所以最后一行是空白的。从最后一个边框格言中剪去——框图是横幅
            // 的真正结束,并排入 -whitespace 修剪错过了样式
            // 大小写( 结尾为 ESC[0m).
            if (const auto end = painted.rfind("\xe2\x95\x9d");   // ╝
                end != std::string::npos) {
                const auto nl = painted.find('\n', end);
                if (nl != std::string::npos) painted.resize(nl);
            }
            std::string framed;
            framed.reserve(painted.size() + 2);
            framed += '\n';
            framed += painted;
            framed += '\n';
            fputs_utf8(stderr, framed);
        } else {
            std::fprintf(stderr, "agentty: %s\n",
                         tools::util::sandbox::describe_state().c_str());
        }

        // 并说清楚,当 已保存 政策不是运行中的事情。
        // 电话 面板 能够 保存 循环配置文件、端口规则和组合盖,
        // 且仅限 claybin 说出这种语言——在brap
        // 下这些行是惰性的。在这里保持安静会让用户相信一
        // 堵没有的墙,这是一种失败模式 a 沙箱 绝对
        // 没有。
        if (!tools::util::sandbox::config_enforceable()) {
            std::fprintf(stderr,
                "agentty: a saved sandbox policy exists but the active backend "
                "cannot enforce it — only claybin can. Pass "
                "--sandbox-backend claybin to apply it.\n");
        }
    }

    // ── 解决活动提供者 ──────────────────────────────
    // --provider 胜负; 否则 已保存 设置; 否则 Anthropic.
    // "anthropic" (违约)保留 OAuth/Pro/Max 路径。任何其他值( $ "openai"
    // 提供方取值还包括 groq、openrouter、ollama 或 host[:port]。
    // 穿过公路的路线 OpenAI-compatible 传输层.
    std::string provider_spec = args.cli_provider;
    startup::mark("sandbox + rest");
    if (provider_spec.empty()) {
        auto s = persistence::load_settings(::agentty::IoAccess::grant());
        provider_spec = s.provider;          // 空 ~ 人类
    } else if (args.subcommand != "acp") {
        // 条形图案 之前 光谱变成设置密钥: 同一端点的两段拼写( 斜线, 偏斜的白空)
        // 绝不能分开 provider_keys / provider_models
        // / 选择面板 列。 与 TUI 自定义 -host 弹出面板 —
        // CLI/TUI 平等(PIF), 包括: 平等(FIF) 平等(VIF).
        provider_spec = provider::canonical_spec(provider_spec);
        // PERSIST-ON-SUCCESS 用于自定义主机。 A KNOWN
        // PRESET ("groq", "ollama" ,......)不能被错误地打成断裂
        // 的终点——立即坚持下去,和以前一样。 CUSTOM 光谱( raw host:
        // port /) URL )为生 INTENT : 坚持 解析 时间让一个字'd
        // --provider 毒药 settings.json 故未来每一次光滑的发射,
        // 都会继承错误。 "skeptical of settings.json" 报告。
        // 届会还在继续 当前 (标志 命令, 从未被屏蔽 ;
        // ModelsLoaded 状态更新函数 坚持到
        // 主机通过回答模型获取来证明自己的时刻。 -m 召回
        // 获得同样的待遇(与证据上的规格一起提交)。
        const bool is_preset = provider::preset_for(provider_spec) != nullptr;
        if (!interactive) {
            // 无头:此过程运行于光谱上,无物是已保存.
        } else if (is_preset) {
            auto s = persistence::load_settings(::agentty::IoAccess::grant());
            s.provider = provider_spec;
            if (args.cli_model.empty()) {
                if (auto it = s.provider_models.find(provider_spec);
                    it != s.provider_models.end() && !it->second.empty())
                    s.model_id = ModelId{it->second};
            } else {
                // -m 附带提供 --provider : 将模型作为
                // 这个提供者的召回文件存档, 所以稍后的光复会恢复它。
                s.provider_models[provider_spec] = args.cli_model;
            }
            persistence::save_settings(::agentty::IoAccess::grant(), cap, s);
        } else {
            // 持久性注册表 -on-success : 数据
            // ModelsLoaded 状态更新函数 写入此主机回答非时的
            // 设置 -empty 模型获取。 -m 将(如果有)提交委员会。
            provider::set_unproven_spec(::agentty::IoAccess::grant(), provider_spec, args.cli_model);
        }
    }
    // 会话 -scoped --auth-header 覆盖; 必须安装
    // 之前 parse_selection 故初选(和每一次直播) -switch
    // 将它印在地图上 OpenAI-family 终处.
    provider::set_custom_auth_header(::agentty::IoAccess::grant(), args.cli_auth_header);
    auto selection = provider::parse_selection(provider_spec);
    provider::select(::agentty::IoAccess::grant(), selection);
    startup::mark("provider selected");

    // 主持人在过去的课中教给我们的(场次400'd, 努力集,推理回声)。
    // TUI's init() 加载 以上这些也属于; `run`,
    // ACP 副特工也需要他们 或者每次跑步都要付400块
    {
        const auto learned = persistence::load_settings(IoAccess::grant());
        set_learned_effort_sets(learned.learned_effort_sets);
        set_rejected_fields(learned.rejected_fields);
        set_reasoning_echoes(learned.reasoning_echo);
    }

    // 认证 头文件 每个提供者, 注册表-driven. Anthropic 使用该
    // OAuth/key 上文已解决的证明; OpenAI-family 后端读取其提供者
    // -specific 内脏( var) GROQ_API_KEY ,然后,...) ) OPENAI_API_KEY ,
    // 或 ,或 -k ; 本地后端( Q) Ollama )接受空键。
    // 见 provider::credentials::resolve ——唯一一个了解这幅地图的地方.
    // 每个供应商通过中央解析器(凭据型号)获得头文件号认证。`-k`号认证
    // (在CLI号认证上有一个-shot键);否则,
    // credentials::resolve号认证统一读取了供应商的存储账户/
    // env/provider_keys。Anthropic号启动证明已经是上文
    // 的已保存号认证,因此("anthropic")可以取回。
    auth::AuthHeader provider_auth;
    if (!args.cli_key.empty()) {
        provider_auth = auth::AuthHeader{auth::ApiKeyHeader{args.cli_key}};
    } else {
        provider_auth = provider::credentials::resolve(IoAccess::grant(), provider_spec);
    }

    // ── 传输 编号 Provider + Store 接口边界 ─────────────────────────────────
    // 长 -lived 提供者为 SHARED-OWNED ,而不是堆栈被引用
    // 所捕获的对象。
    //
    // 他们以前靠主堆积生活 以主活的推理为主 maya::run。
    // 这代表了每一个 线程 编号 运行时 等待着 副剂就是 线程 才没有 A
    // `task` 运行在 a Cmd::task_isolated 线程 ,
    // 哪个 jaal DETACHES (kernel/pool.hpp:
    // 单独 线程 已经 "asked to stop but never waited for" 所以,
    // 一个仍然在里面 cfg.stream ()在退出时,
    // 在主未受伤时,继续呼叫这些物体。关闭现在要求它停止等待1.5 s,
    // 但等待是故意的。 线程 被一个连锁电话里织的 被抛弃了
    // 并且用堆栈捕捉到 遗弃是一种用途
    // -after-free.
    //
    // shared_ptr 关闭它永远: 数据流 羊肉
    // 有它自己的强势参考, 所以一个被遗弃的 工作线程
    // 只要供应商还在摸它 就能活下来 最后的就毁了它
    // 没有命令要正确, 没有宽限期调和, 没有命令要竞态。
    // 成本是一 分配 每个提供者在启动时。
    auto anthropic_provider =
        std::make_shared<provider::anthropic::AnthropicProvider>();
    auto chatgpt_provider =
        std::make_shared<provider::chatgpt::ChatGptProvider>();
    auto copilot_provider =
        std::make_shared<provider::copilot::CopilotProvider>();
    auto kimi_provider = std::make_shared<provider::kimi::KimiProvider>();
    io::FsStore                            store{::agentty::IoAccess::grant()};
    startup::mark("providers built");

    // 电话 接口边界 : 单人 std::function 编号 运行时 调用 它发送到
    // provider::active() AT CALL TIME (不是在冻的
    // 树枝上),所以提供商 选择面板 还能活下来 -switch 后端
    // 中间 -session (provider::select() +
    // app::switch_provider ()),下一个请求针对的是新的提供者——否
    // 接口边界 重建,不重新启动。 OpenAI-family 切换,我们重建每 -call
    // 活动选择的结束点 host/path/tls 更改需要 副作用 立刻开始
    // 电话 接口边界 : 单人 std::function 编号 运行时 调用.
    // `dispatch_stream` (provider/dispatch.cpp
    // 即是无边无边无际无边无尽无边无诸无边无界无边无等无边无极无边无有边无边. 一个 路由点
    // ——读作: provider::active() AT CALL TIME 所以,
    // 我们 选择面板 还能活下来 -switch 后端中间 -session 下一个请求针对它,
    // 没有 接口边界 重建。两个长的 -lived 抓获本地供应商 BY 值 以 shared_ptr
    // (见上文:脱落的副剂可能比主剂活,参考捕获表明使用 -after-free ;
    // 短数 -lived OpenAI-compat / Ollama 传输层 按每架车
    // 建造 调用 内 分派 从活动端点。
    // 路由器,每组 调用 从长 -lived 供养者.
    //
    // 考虑好了,因为现在有 两个 接口边界 誓以报答:
    // `stream_fn` 向任何活跃的提供者的路径,
    // 以及 `stream_to_fn` 路径到 a NAMED。
    // 两者必须提供相同的一组 传输层 ,所以它们从一个地方建造,
    // 而不是每个地方拼写出四臂.
    auto build_router =
        [anthropic_provider, chatgpt_provider, copilot_provider, kimi_provider] {
            provider::ProviderRouter router;
            router.set(provider::LongLived::Anthropic,
                       [anthropic_provider](const maya::unlocked& c, provider::Request r,
                                            provider::EventSink s) {
                           return anthropic_provider->stream(c, std::move(r), std::move(s));
                       })
                  .set(provider::LongLived::ChatGpt,
                       [chatgpt_provider](const maya::unlocked& c, provider::Request r,
                                          provider::EventSink s) {
                           return chatgpt_provider->stream(c, std::move(r), std::move(s));
                       })
                  .set(provider::LongLived::Copilot,
                       [copilot_provider](const maya::unlocked& c, provider::Request r,
                                          provider::EventSink s) {
                           return copilot_provider->stream(c, std::move(r), std::move(s));
                       })
                  .set(provider::LongLived::Kimi,
                       [kimi_provider](const maya::unlocked& c, provider::Request r,
                                       provider::EventSink s) {
                           return kimi_provider->stream(c, std::move(r), std::move(s));
                       });
            router.external_acp = [](const maya::unlocked& c, const std::string& agent_id,
                                     provider::Request r, provider::EventSink s) {
                return provider::stream_external_acp(c, agent_id, std::move(r),
                                                     std::move(s));
            };
            return router;
        };

    // 两者 接口边界 转弯 工作线程 (无) TUI 转动任务,一个副代理,
    // 一个 ACP ),该规定没有 锁 并经过它的 `unlocked` 进来吧
    std::function<provider::StreamResult(const maya::unlocked&, provider::Request,
                                         provider::EventSink)> stream_fn =
        [build_router](const maya::unlocked& c, provider::Request req, provider::EventSink sink) {
            return provider::dispatch_stream(c, build_router(), std::move(req),
                                             std::move(sink));
        };

    // 分派 改为: NAMED 提供者——一个Smart Mode角色,
    // 其插槽被固定在活动点以外的端点上.
    //
    // 认证解决 此处 ,来自目标提供者,即整个要点:a
    // `req.auth` 来电的就是 PARENT 供应商 凭据
    // 并且会401对抗任何人。 credentials::resolve
    // () 已经对任意提供方 ID 的答案,
    // 返回空的 oauth_native 和本地行的 传输层 拥有自己的
    // 象征——所以空洞的答案被单独地放在一边,而不是被当作失败.
    std::function<provider::StreamResult(const maya::unlocked&, const std::string&,
                                         provider::Request,
                                         provider::EventSink)> stream_to_fn =
        [build_router](const maya::unlocked& c, const std::string& provider_id,
                       provider::Request req, provider::EventSink sink) {
            const auto sel = provider::parse_selection(provider_id);
            auto cred = provider::credentials::resolve(IoAccess::grant(), provider_id);   // 数据流 条目: a 工作线程
            const bool have = !auth::bearer_token(cred).empty()
                || std::holds_alternative<auth::BearerHeader>(cred);
            if (have)
                req.auth = sel.kind == provider::Kind::Anthropic
                             ? auth::fresh_auth_header(IoAccess::grant(), c, cred)
                             : std::move(cred);
            AGT_LOG(Smart, Debug, "route.cross_provider",
                    "provider={} model={} auth={}", provider_id, req.model,
                    have ? "resolved" : "transport-owned");
            return provider::dispatch_stream(c, build_router(), sel,
                                             std::move(req), std::move(sink));
        };
    // 通过写入设置 -behind 缓存 ,作为 app::install 是否
    // 为: 状态更新函数 永远不要在磁盘上等待。 锁 顺序 : Deps 是
    // 一个 副作用 接口边界 ,由 副作用 主办单位和 init() 循环 线程 步数
    // 之间( 无) 锁 ),所以, 接口边界 内容 -states 以作为这里的能力根
    auto settings_seam = maya::thread_root([&](const maya::unlocked& cap) {
        startup::mark("settings seam");
        return app::settings_cache::wrap(cap,
            [&store] { return store.load_settings(); },
            [&store](const maya::unlocked& c, const store::Settings& x) {
                store.save_settings_quiet(c, x);
            });
    });
    app::install_deps(app::Deps{
        .stream        = [stream_fn](const maya::unlocked& c, provider::Request r,
                                     provider::EventSink s) {
            (void)stream_fn(c, std::move(r), std::move(s));
        },
        .save_thread   = [&store](const Thread& t) { store.save_thread(t); },
        .delete_thread = [&store](const ThreadId& id) { store.delete_thread(id); },
        .load_threads  = [&store] { return store.load_threads(); },
        .load_thread   = [&store](const ThreadId& id) { return store.load_thread(id); },
        .backfill_summaries = [&store](const std::vector<ThreadId>& ids) {
            return store.backfill_summaries(ids);
        },
        .load_settings = [l = std::move(settings_seam.load)] {
            return maya::thread_root([&](const maya::unlocked& c) { return l(c); });
        },
        .save_settings = [sv = std::move(settings_seam.save)](const store::Settings& x) {
            maya::thread_root([&](const maya::unlocked& c) { sv(c, x); });
        },
        .new_thread_id = [&store] { return store.new_id(); },
        .title_from    = [&store](std::string_view t) { return store.title_from(t); },
        .write_file    = [](const std::string& path, const std::string& contents) {
            // diff (英语). -review 拒绝: 仅接受重写
            // 文件 差异块 最好还是留着吧 -effort —— 下读错误出现,
            // 审查已经将其决定应用到 -model.
            (void)tools::util::write_file(std::filesystem::path{path}, contents);
        },
        .auth          = provider_auth,
    });
    startup::mark("deps installed");

    // ── 传输 子代理( 键盘) `task` 工具) , 用于 接口边界 ────────────────────
    // 进程 -global 配置 `task` 工具读取以旋转一个孤立的子 -agent 循环。
    // 以同样的方式解析模型 TUI / ACP 路径已实现( E) -m 覆盖 ~ 已保存
    // 设置 + 已建 -in 默认值。 数据流 风雨是 相同 提供者 分派 Deps::stream
    // 用途( 路线 provider::active (a) 将所有工作人员
    // 都纳入《公约》的范畴; 调用 时间,所以副剂工作 Anthropic,
    // OpenAI-compat ,和 Ollama 一样,不只是 Anthropic 传输层.
    {
        auto sa_settings = persistence::load_settings(::agentty::IoAccess::grant());
        std::string sa_model =
            !args.cli_model.empty()       ? args.cli_model
          : !sa_settings.model_id.empty() ? sa_settings.model_id.value
          :                                 std::string{"claude-opus-4-5"};
        // 智能模式配置, 解析方式相同 init() 已经。 TUI 内容 -pushes
        // 这个来自 init() 一次 Model 存在( 并再次出现在任意 覆盖面板
        // 编辑/ 目录 加载 ),但,我们没有为它提供其他支持。 `agentty run`
        // 财务报告和已审计财务报表 `agentty acp` 永远不要建
        // Model 没有这种安排,他们就默认地委托他们 -constructed
        // RoleConfig, i.e。智能模式默默关闭,每个被绑定的角色槽被忽略。
        // 无头运行正是角色的路由最要紧的地方(它们都是代表团)。
        //
        // "The same way init() does" 和现在
        // 的三行字一样 因为那句持续着的 IS a RoleConfig。
        // 这曾经是第二只手 -written 复制的插槽映射,
        // 正是这两个漂移。
        smart::RoleConfig sa_smart = sa_settings.smart;
        const auto sa_env = settings::registry::read_env(::agentty::IoAccess::grant());
        if (sa_env.smart_enabled) sa_smart.enabled = *sa_env.smart_enabled;
        settings::registry::apply_env(sa_smart, sa_env);
        // 明确 --model 是一个契约,而不是暗示:每个角色
        // 都在上面。没有这个, `--model X` 将主转弯
        // 在紧凑和读取时绑定 -only 子剂仍然被引导到任何
        // 目录 说便宜一点,一个没有命名的模型 被传输带开销
        // (#70).
        sa_smart.pinned_model = smart::tuning::model_pinned();
        // 型号为CATALOG,原因相同,历史也相同.
        //
        // 电话 TUI 推这个从 init() 并且再次在每一次 目录 加载 (set_candidates
        // 输入 init.cpp / models.cpp ),但,我们没有为它提供其他支持。
        // `agentty run` 财务报告和已审计财务报表 `agentty acp`
        // 永远不要建 Model - 所以他们跑与一个 空 以及所有默默地贬低名单的内容:
        //
        //   • context_window 决心在每条无头的转弯上 都以0为单位,所以
        //     Ollama 传输层无法大小为options.num_ctx,
        //     倒回到守护进程~2k/4k的默认值,截断长跑.
        //   • cheapest_capable_model()没有选择,因此
        //     读作-only 子代理角色从不向下引导到廉价模型.
        //   • Smart Mode的角色解析器没有候选人,因此固定的插槽
        //     回到母模型上——上面的评论所描述的RoleConfig,
        //     一个字段的准确失败.
        //
        // ASK 该 PROVIDER 别猜了 list_models_for
        // 发送到与轴线相同的 数据流 路径( 选项) 传输 方言+
        // 语句 oauth_native ),所以, 目录 这里
        // 不能不同意 传输层 将迎合轮回, CUSTOM HOST
        // 这是唯一能学到东西的方法 因为没有捆绑 目录 能够知道
        // 私人部署的模型或窗口。这就是整个点 / v1/models
        // 梯形: 自定义 -hosted 网关的窗口只能从网关
        // 得知.
        //
        // HEADLESS 仅。该词 TUI 自己拿这个...
        // init() 种子捆绑的地板和 ModelsLoaded 推着现场
        // 目录 到达时,两边都 set_candidates - 所以在这里
        // 取钱也会在第一次网络旅行前 进行同步网络往返 帧 生成即将被替换的列表。
        // `agentty run` 财务报告和已审计财务报表
        // `agentty acp` 没有什么新鲜的,正因为如此,
        // 他们现在需要它。
        std::vector<ModelInfo> sa_candidates;
        const bool headless = args.subcommand == "run"
                           || args.subcommand == "acp"
                           || args.subcommand == "mcp-serve";
        if (headless) {
            // 没有 Model 出版日期: web_search 政策: 加载 是从设置。
            tools::web_search_policy::seed(::agentty::IoAccess::grant());
            // MCP工具加载相同(#98).
            tools::tool_loading::seed(::agentty::IoAccess::grant());
            // 当认证为空或网络无法到达时, 返回提供者的
            // 静态种子, 因此离线运行仍然会得到一个地板。
            // 无论如何, 已包装 : 目录 抓取绝对
            // 不会是停止无头运行的开始。
            try {
                sa_candidates = provider::list_models_for(::agentty::IoAccess::grant(), cap,
                                                          provider::active(), provider_auth);
            } catch (...) { /* 掉到下面的捆绑地板上 */ }
        }
        if (sa_candidates.empty())
            sa_candidates = catalog::bundled(
                provider::parse_selection(provider_spec).provider_id());
        // 发布窗口进程 -wide 同样的 TUI's
        // ModelsLoaded 手臂是的, 捆绑的地板 首次 ,
        // 然后现场 目录 过它。
        //
        // 联合,不是替换,是直播 v1/models 答案是天花板 你
        // 能够 REACH 但这不是你所能做的超能力 NAME:
        // Anthropic 现场直播 目录 以模拟船的身份行动,
        // 所以 `-m claude-opus-4-5` 被绑在设置( 或
        // 通过命令行) 中可以超过排线, 而捆绑的地板仍然知道它的窗口。
        // 将地板埋在下面意味着一个退休的 -but-known id解决它的
        // 真实窗口而不是 0 — 0 并不是 "unknown" 下游 ,
        // 则 Ollama 传输层 读起来是 "use my tiny default"
        // 并中断运行。
        // SKIP 编号 选择面板-only `[1m]`/`[2m]`
        // 变体 排队,他们带着 相同 带有标记后缀的模型 id,
        // 和地图中带有后缀的关键正常条条—— 因此种子它们会在底码 id
        // 下写上 1000 000 , 而每一份对平原模型的要求都声称它
        // 没有资格使用 1M 窗口。 扩展窗口是选择的。 -in 每转弯,
        // 不是身份证明的财产。
        const auto seed_windows = [](const std::vector<ModelInfo>& rows) {
            agentty::CatalogBatch batch;   // 一份,不是每个模型一份
            for (const auto& mi : rows) {
                if (mi.id.value != agentty::wire_model_id(mi.id.value)) continue;
                batch.set_context_window(mi.id.value, mi.context_window);
            }
            batch.commit();
        };
        seed_windows(catalog::bundled(
            provider::parse_selection(provider_spec).provider_id()));
        seed_windows(sa_candidates);
        tools::subagent::install(tools::subagent::Config{
            .auth = provider_auth,
            .model = std::move(sa_model),
            .installed = true,
            .candidates = std::move(sa_candidates),
            .stream = stream_fn,
            .stream_to = stream_to_fn,
            .smart = std::move(sa_smart)});
    }

    // ── MCP 服务器模式: 服务 agentty 本地工具结束 MCP (stdio) (英语). ──
    // 没有 maya ,没有 终端 UI 外部 MCP 客户端( 4) Claude 桌面,
    // 一个 IDE 驱动器 tools/list + tools/call 结束 stdin/stdout ;
    // 诊断结果转到 Stderr。重用 provider/subagent/sandbox
    // 接口边界 上面有线 所以 `bash`, `task` ,GitQQ家族等等,
    // 行为与他们在 TUI (文件系统工具保持沙箱到 --workspace).
    startup::mark("subagent + tools wired");
    if (args.subcommand == "mcp-serve") {
        provider::prewarm_active_provider(::agentty::IoAccess::grant());   // `task`/网络工具再利用温暖的会话
        int rc = mcp::serve_stdio(cap);
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), cap);
        return rc;
    }

    // ── 头无一 -shot: `agentty run "prompt"` ───────────────────
    // 没有 maya ,没有 终端 UI 运行 提示词 通过 相同 代理循环 `task`
    // 工具使用——全工具集, 沙箱 ,末日( 末日) -loop 断开器, 带边的转弯
    // —— 打印最终的报表以显示。 完美到 scripting/CI:
    // `git diff | agentty run "review this change"`.
    if (args.subcommand == "run") {
        // 拒绝一个打字角色 之前 读 stdin 或与提供者交谈。
        // resolve_agent_type ( ) 倒计时
        // `general` ——一个命名一个不存在的角色的模型应该仍然能够
        // 完成工作——但是在 CLI 默默奔跑的那个人
        // `--agent reviwer` 以 `general` 并
        // 报告成功,所以用户得到了错误的代理 完全没有信号。
        if (!args.cli_run_agent.empty()
            && !tools::is_known_agent_type(args.cli_run_agent)) {
            std::fprintf(stderr, "agentty run: unknown --agent '%s'\n  known: %s\n",
                         args.cli_run_agent.c_str(),
                         tools::known_agent_types().c_str());
            return 2;
        }
        run_cli::Options ro;
        ro.agent   = args.cli_run_agent;
        ro.output  = args.cli_run_output;
        ro.json    = args.cli_run_json;
        ro.verbose = args.cli_run_verbose;
        ro.read_only  = args.cli_run_read_only;
        ro.no_network = args.cli_run_no_network;
        ro.save       = args.cli_run_save;
        if (const char* ev = std::getenv("AGENTTY_EVENTS"); ev && std::string_view{ev} == "jsonl")
            ro.events = true;
        if (!args.cli_run_effort.empty()) {
            if (!run_cli::valid_effort(args.cli_run_effort)) {
                std::fprintf(stderr, "agentty run: --effort wants off, low, medium, high, xhigh, max or auto, got '%s'\n",
                             args.cli_run_effort.c_str());
                return 2;
            }
            ro.effort = args.cli_run_effort;
        }
        if (!args.cli_run_resume.empty()) {
            if (!run_cli::valid_resume(args.cli_run_resume)) {
                std::fprintf(stderr, "agentty run: --resume wants a thread id (16 hex digits) or 'last', got '%s'\n",
                             args.cli_run_resume.c_str());
                return 2;
            }
            ro.resume = args.cli_run_resume;
        }
        if (!args.cli_run_max_turns.empty()) {
            const auto n = run_cli::parse_count(args.cli_run_max_turns);
            if (!n) {
                std::fprintf(stderr, "agentty run: --max-turns wants a number from 1 to 500, got '%s'\n",
                             args.cli_run_max_turns.c_str());
                return 2;
            }
            ro.max_turns = *n;
        }
        if (!args.cli_run_timeout.empty()) {
            const auto t = run_cli::parse_duration(args.cli_run_timeout);
            if (!t) {
                std::fprintf(stderr, "agentty run: --timeout wants a duration like 90, 90s, 5m or 1h, got '%s'\n",
                             args.cli_run_timeout.c_str());
                return 2;
            }
            ro.timeout = *t;
        }
        std::string prompt = args.cli_run_prompt;
        // `-` 明确要求标准;同样,完全不给予任何职位。
        // 来,来,来 EXPLICIT 空 提示词 (`agentty run ""`
        // )没有:扫描声称它,所以用户确实提供了一个,只是恰好是空的.
        // 不加此区分 `run ""` 掉进非 -tty 树枝
        // 下面挂着
        const bool want_stdin =
            prompt == "-" || (prompt.empty() && !args.cli_run_prompt_claimed);
        std::string piped;
        // 存在时附加有管状的 stdin。 通常是 `git diff | agentty run "review"`
        // —— diff( 音译) 字节 成为后的背景 提示词 -
        // 所以这必须触发任何非 -tty 斯丁,有数据。
        //
        // 不过 `isatty == false` 光靠孩子是不足以完成的,
        // 孩子继承了孩子的 字节 永远到来,而且 `cin.rdbuf()`
        // 上面有几条线,它会永远等待。 `cli_arg_order_test`
        // 跌落到这个形状,在60秒时计时测试, 15次测试随后被加到Not
        // Run中, 每一个较早的试验案例都附在后面 `--help`
        // (在触摸Stdin之前离开)或通过管道 `head` (早点接近),
        // 所以 一个 光是援引( labour trials) (
        // `agentty run ""` 那次旅行是一场旅行。
        //
        // poll()以0××××的超时不读回答
        // "is there data or EOF now?". 空空且开放
        // ×× 跳过;字节准备就绪或写作关闭 × 排水口(一个闭管返回POLLHUP,
        // cin一次击中EOF,匹配一个贝壳给我们一个tty,
        // 没有东西可以排水的交互式案例). want_stdin绕过看,
        // 所以`agentty run -`仍然等待,这是它的整个点.
        //
        // Windows 没有 poll() ; 保持原有的允许行为(排入任何非允许
        // 行为) -tty。。 难得 WaitForSingleObject 舞能可靠
        // 地区分空的 -but-open 烟斗从一个准备就绪的 每一个 Windows
        // console/redirect 组合,和 Windows CI 从来没有
        // 打到这个形状。
#ifndef _WIN32
        auto stdin_has_data = []() noexcept {
            pollfd pfd{fileno(stdin), POLLIN, 0};
            return ::poll(&pfd, 1, 0) > 0
                && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        };
        if (want_stdin || (!isatty(fileno(stdin)) && stdin_has_data())) {
#else
        if (want_stdin || !isatty(fileno(stdin))) {
#endif
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            piped = ss.str();
        }
        if (want_stdin) {
            prompt = std::move(piped);
            piped.clear();
        }
        // 调制: `echo "" | agentty run` 管一条单
        // 线——即 不 a 提示词 ,而意外空跑的账单是真实的 API 调用.
        while (!prompt.empty() && (prompt.back() == '\n' || prompt.back() == '\r'
                                   || prompt.back() == ' ' || prompt.back() == '\t'))
            prompt.pop_back();
        std::size_t lead = 0;
        while (lead < prompt.size() && (prompt[lead] == '\n' || prompt[lead] == '\r'
                                        || prompt[lead] == ' ' || prompt[lead] == '\t'))
            ++lead;
        prompt.erase(0, lead);
        if (!piped.empty()) {
            prompt += "\n\n<piped-input>\n";
            prompt += piped;
            prompt += "\n</piped-input>";
        }
        if (prompt.empty()) {
            std::fprintf(stderr,
                "agentty run: no prompt (pass one, or pipe stdin)\n");
            return 2;
        }
        // 刀法也在这里工作: `agentty run "/review src/x.cpp"` ,
        // 技能,或 MCP 提示词 ,解决的方式与 TUI 财务报告和已审计财务报表 ACP.
        if (prompt.starts_with('/')) {
            const Io io = IoAccess::grant();
            if (auto expanded = slash::expand_checked(io, prompt, slash::discover(io))) {
                // 缺少一个必要论据的提示词是一个使用错误:
                // 说吧,像其他坏命令一样,停止
                if (expanded->usage) {
                    std::fprintf(stderr, "agentty run: %s\n", expanded->text.c_str());
                    return 2;
                }
                prompt = std::move(expanded->text);
            }
        }

        provider::prewarm_active_provider(::agentty::IoAccess::grant());
        ro.prompt = std::move(prompt);
        const int code = run_cli::run(cap, ro);
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), cap);
        return code;
    }

    // ── ACP模式:作为无头剂运行于stdio之上(Zed等人) ───────
    // 没有 maya ,没有 终端 UI. stdin/stdout 携带新线
    // -delimited JSON-RPC ;所有诊断都转到 Stderr , 协议通道保持干净。
    // 重复使用相同的 provider/tools/sandbox 上面有线
    if (args.subcommand == "acp") {
        auto settings = persistence::load_settings(::agentty::IoAccess::grant());
        // -m 为该子进程赢得 无需 坚持到设置( 一种 ACP
        // 特工不应该乱打 TUI's 已保存 否则就回到原点。
        // 已保存 模型,然后是构建 -in 默认。
        std::string model_id =
            !args.cli_model.empty()    ? args.cli_model
          : !settings.model_id.empty() ? settings.model_id.value
          :                              std::string{"claude-opus-4-5"};

        // 预温 TLS/DNS 编号 活动 提供者的主机所以第一个 提示词 重用该
        // SSL 会话+连接 缓存 而不是支付~150~300毫秒握手。
        // Anthropic 财务报告和已审计财务报表 ChatGPT/Codex 一样,
        // 我... TUI 发射前的相同行为 maya.
        provider::prewarm_active_provider(::agentty::IoAccess::grant());

        // 权限 触发 Zed 批准的工具配置门 提示词.
        // 默认 Ask: 提示词 费用 write/exec/net 自动 -run 读。
        // `minimal` 资源 提示词 读取; `write` 永远 提示词 读到的
        Profile profile = Profile::Ask;
        if      (args.cli_profile == "write")   profile = Profile::Write;
        else if (args.cli_profile == "minimal") profile = Profile::Minimal;
        else if (args.cli_profile == "ask" || args.cli_profile.empty())
                                                profile = Profile::Ask;
        else {
            std::fprintf(stderr,
                "agentty: --profile must be write, ask, or minimal (got '%s')\n",
                args.cli_profile.c_str());
            return 2;
        }

        // 原始读作(2)/write (2) on stdin( 0)/stdout(1).
        // Flush 任何缓冲的 C++ 数据流 输出先行,所以不能与原始写作进行互换.
        std::cout.flush();
#if defined(_WIN32)
        // 打开 Windows 缩略语 0/1 默认为 TEXT 模式,
        // 用于翻译 '\n' 编号 JSON-RPC 框架发射到 "\r\n" 并操纵 传输.
        _setmode(_fileno(stdin),  _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        auto server = maya::co_owned<agentty::acp::AgentServer>::make(
            agentty::rpc::fd_channel(0, 1),
            [stream_fn](const maya::unlocked& c, provider::Request r, provider::EventSink s) {
                (void)stream_fn(c, std::move(r), std::move(s));
            },
            provider_auth,
            std::move(model_id),
            profile,
            agentty::acp::AgentServer::Extras{
                // 让我们在任何已签名的-in提供商上选择一个模型,
                // 而不只是活动中的(Zed的模型降下列出了全部).
                .stream_to = [stream_to_fn](const maya::unlocked& c, const std::string& pid,
                                            provider::Request r, provider::EventSink s) {
                    (void)stream_to_fn(c, pid, std::move(r), std::move(s));
                },
                // 编辑运行后 `agentty login` 为我们 `authenticate`
                // 内容 -reads 店里有这个。
                .reauth = [provider_spec] {
                    return provider::credentials::resolve(IoAccess::grant(), provider_spec);
                }});
        std::fprintf(stderr, "agentty: ACP agent ready on stdio (profile=%s)\n",
                     std::string(to_string(profile)).c_str());
        int rc = server->serve(cap);
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), cap);
        return rc;
    }

    // 预览 -warm TLS 编号 活动 提供者在脱开背景上的宿主 线程。
    // 第一个 提示词 用户类型将重新使用 SSL 届会+ DNS + 连接 缓存 ,
    // 跳过~150–300 ms 首个 -byte 握手 握手吧 握手啊 握手
    // 统一:用户是否在 Claude 或 或 时 间 ChatGPT/Codex ,
    // 活动后端获得头开始——没有提供者享有特权.
    provider::prewarm_active_provider(::agentty::IoAccess::grant());
    startup::mark("prewarm queued");

    // ── 继续诊断 OFF 已交付货物 终端 ──────────────────────
    // agentty's TUI 运行在 INLINE 模式(不是备选案文)
    // -screen 写成的字都直接写在了 viewport/scrollback
    // 和腐败者, maya's 渲染 — e.g。一个 MCP 无法产卵的服务
    // 器用于涂抹 "mcp::cap: exec … failed" 横跨
    // 帧 绊倒 回滚缓冲区 门。将 Stderr 重定向为每根
    // -session 记录每个子系统的诊断( MCP ,供应商, 沙箱 )
    // 保存但绝不触摸屏幕。 AGENTTY_NO_STDERR_REDIRECT
    // =1 (e.g . 当调试启动时)。 ACP / mcp 数据 -serve
    // /运行时保持Stderr作为他们的诊断通道,并在此时之前被处理.
    //
    // SKIPPED 酒类下: freopen() 重定向
    // 会耗尽我们全部 二进制程序 保留 帧 干净的,
    // 酒是 development/CI 目标,
    // 不是用户 -facing 一个,一个有点脏 帧 拍拍没有开始。
    // 电话 真实 定点器, 保持到下方方向。 如果 maya
    // 无法承受 终端 没有 帧 保护,以及一个只降落在
    // stderr.log 是一个沉默的退出 70 从用户的椅子。
#if defined(_WIN32)
    const int real_stderr = _dup(_fileno(stderr));
#else
    const int real_stderr = ::dup(fileno(stderr));
#endif
    if (!running_under_wine()) {
        if (const char* off = std::getenv("AGENTTY_NO_STDERR_REDIRECT");
            !(off && off[0] && off[0] != '0')) {
            std::error_code lec;
            std::filesystem::path logdir = util::user_logs_dir();   // ~/.agentty/logs
            std::filesystem::create_directories(logdir, lec);
            std::filesystem::path logpath = logdir / "stderr.log";
            // 附加一个崩溃的后遗症会在不同会话中存活下来;
            // 截断会失去您想要的线条 -mortem.
            if (std::freopen(logpath.string().c_str(), "a", stderr)) {
                // 大小必须是非 -zero: MSVC's UCRT 验证 `size - 2`
                // 反对 INT_MAX 并拒绝 0 是一个无效参数,该参数运行无效
                // -parameter 处理函数 -> __fastfail(
                // FAST_FAIL_INVALID_ARG ) - > 0xC000409
                // 之前 TUI 曾经画过 帧 C标准允许 NULL 缓冲区 大小为 0(大小随后
                // 被忽略),但 UCRT 而不是,只有 MSVC 释放构建 链接 说 CRT
                // - 这就是为什么... MinGW/clang64 同一来源的建筑幸存下来。
                std::setvbuf(stderr, nullptr, _IOLBF, BUFSIZ);   // -buffered 线
                std::fprintf(stderr, "\n=== agentty session %ld ===\n",
                             static_cast<long>(agentty_pid()));
            }
        }
    }

    // FPS =0 = 纯度 事件-driven: maya 仅限 渲染 打开 Msg /输入/计时器.
    // 电话 加载动画-tick 事件订阅 (拟议时间: stream.active () 用品 帧
    // while streaming; idle agentty costs zero CPU.
    //
    // 网格后端: 当由合作编辑器主机进行画报时 存储单元 本地( 4) AGENTTY_HOST
    // =emacs—— 数据 agentty-mode 爱玛克 渲染 模块——或
    // AGENTTY_HOST =vscode - 密码 agentty-vscode 网页
    // 视图网格 渲染器 ), 发射 二进制程序 网格 帧 改为 ANSI ,跳过 终端-emulator
    // ANSI encode/reparse 来回旅行,其他主机都归到 ANSI.
    maya::RenderBackend backend = maya::RenderBackend::Ansi;
    if (const char* host = std::getenv("AGENTTY_HOST")) {
        const std::string_view h{host};
        if (h == "emacs" || h == "vscode") {
            // 选择 -out: AGENTTY_GRID =0(或遗产) AGENTTY_EMACS_GRID =0)
            // 保持平原(Plain) -ANSI 路径。
            const char* g  = std::getenv("AGENTTY_GRID");
            const char* ge = std::getenv("AGENTTY_EMACS_GRID");
            const bool off = (g && g[0] == '0') || (ge && ge[0] == '0');
            if (!off)
                backend = maya::RenderBackend::Grid;
        }
    }
    // 无标记 -palette 订阅者 继续,刻意的
    //
    // Markdown 将其颜色保留在一个平面的投影调色板上,
    // 而不是读取主题每张 -frame (其数量) 渲染 路径是热的,
    // 并且是自动同步的 解析 工作线程 把它拿下来 -thread ),
    // 所以必须重新 -derived。每次主题交换。
    // on_theme_changed ()注册——注册是错误:
    // 忘记它看起来完全像一个从未改变过的主题,这就是为什么在选
    // 一个方案重新油漆铬时,在播音、代码跨度和表上保留旧调色板。
    //
    // maya 内容 -derives 读到现在( theme::projected 因此,
    // 签署会比多余的更糟糕: set_markdown_palette()
    // OVERRIDES 投影带一个明确的调色板, 将其固定到下一次互换。

    // ── 帧-gate 追踪 ──────────────────────────────────────────────
    // 仅在 `ui` 追踪记录在进行中 因为这次火灾是每一次 事件
    // -loop 重复,重复。
    //
    // 电话 `frame` 一半已经走了,不是偶然的: maya's
    // set_frame_trace 是一个全球的钩子,旧的运行循环 所谓的,
    // 和新的主机报告 相同的事物 RETURN 值 而不是说...
    // Screen::present_if 手还原 `Presented{.skipped = true}`
    // 当视觉散列匹配时。这是更好的设计(没有全局性,没有无效检查) 帧 ),
    // 它只是无法从 main() : 主持人读它,而不是程序。如果我们想要的话。
    // "model changed but the screen didn't" 线回,
    // 它属于 maya 位于 present() 调用 这儿不行
    //
    // 发射方生存下来 因为它会钩住 RENDERER ,
    // 程序仍然如此 链接 直接: `emit` 说,
    // 是否由组成 帧 任何一个 字节 ,
    // 在模型更改后空出就是diff 完成 传输 已经显示。
    if (logx::recorded(logx::Channel::Ui, logx::Level::Trace)) {
        maya::set_emit_trace([](const char* state, std::size_t bytes,
                                bool empty) noexcept {
            AGT_LOG(Ui, Trace, "emit",
                    "state={} bytes={}{}", state, bytes,
                    empty ? " EMPTY" : "");
        });
    }

    // 运行于agentty的主办方:maya的terminal_host+agentty
    // 自己的副作用(runtime/app/host.hpp). maya::run打开终端,
    // 安装maya的执行器,运行于jaal上的程序;agentty从不运行调用XXjaal.
    {
        const std::string title = window_title();
        maya::Options cfg{.title = title, .fps = 0,
                          .mode = maya::Mode::Inline, .backend = backend};
        startup::mark("maya::run");
        if (const int rc = maya::run<app::AgenttyApp, app::Host>(cfg);
            rc == 70) {
            // 无法接受终端(或一个错误阻止了程序).
            // 为何去stderr.log; 说到用户正在寻找的地方.
            if (real_stderr >= 0) {
                const std::string msg =
                    "agentty: couldn't start the terminal UI (exit 70). "
                    "Is stdin/stdout a terminal? Details: "
                    + (util::user_logs_dir() / "stderr.log").string() + "\n";
#if defined(_WIN32)
                (void)_write(real_stderr, msg.data(), static_cast<unsigned>(msg.size()));
#else
                (void)!::write(real_stderr, msg.data(), msg.size());
#endif
            }
            return 70;
        }
    }

    // 拆卸连接 MCP 插件服务器 首次 关闭每个服务器的
    // stdin( ) EOF 解锁任何 -flight
    // 工具 -call 工作线程 还在读,所以工具是中 -call
    // 退出时间不能绕过退出路径。井 -behaved stdio
    // 服务器退出 EOF 单位~1ms; 被焊接的一方以
    // ChildProcess's SIGTERM→SIGKILL
    // 快速完成(不是静态销毁)是退出的感觉,而不是被吊到第二秒
    // Ctrl-C.
    mcp::release_servers();

    // 斋戒 -exit 加入( 共进) TLS 预拨, 即 工作区 `@`/
    // `#` 走路,模特儿, 新鲜,blob GC 生活于 生活于
    // TeardownGuard 顶端的毁灭者 main() ,所以他们运行 每个
    // 退出路径, 而不是仅此一条。 查看他们防御的 0xC00000005
    // 的注释。
    //
    // rag_shutdown ( ) 留在这, 并且 之前 护卫火:
    // 已预热的工作线程 活在某个函数上 -local 静态,
    // 否则只可在 main() 返回, 屏蔽 ^C 4 -10s在一个大体。
    // 正在快速地进行移动
    tools::rag_shutdown();

    // 加入这个小块 GC 冲洗前: 双脚走 线程 dir,
    // 在关闭时允许它们重叠是 竞态 保护者也会加入,
    // 但这个命令是运行的 之后 3⁄4 ̄ ̧漯
    // B 正文 - 冲浪来不及了
    blobs::join_background_gc(cap);

    // 排出自动排水 持久化 队列。退出 状态更新函数 臂部排队完成一个最终
    // save_thread () 紧接着 maya 返回; 直至该时为止的
    // 此块( 以及任何较早的块) -still-queued 将土地写在磁盘上。
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), cap);
    // 将任何缓存的外部拆卸 ACP 代理子进程( 即使是通过 Weded agent
    // 绑定) ExternalAcpBackend 拆掉时 看门狗).
    provider::release_acp_agents(cap);

    // 一切,我都会说 上方 此行是一个子系统 main() 恰巧知道;
    // 有关 注册表 包括登记的所有项目 ITSELF 当它开始
    // 一个 线程。注意,这是 TUI 路径的清晰 调用 ——在顶部
    // 安装的瞄准镜护卫. main() 20号机上发生同样的事情
    // CLI 子命令路径, 返回之前, 从未到过这里。
    // 呼叫 run() 两次是拒绝 -op ; 缩写 注册表 自己来清理
    util::teardown::run(cap);
    return 0;
}

}  // 命名空间结束。

// 过程的根能力. main() 是一个地方,
// a 线程 开始 jaal 还没开始,它还什么也没有。
// job/task 条目,或从 锁 正文.
// (lint: cap_roots 列出此文件. )
int main(int argc, char** argv) {
    agentty::startup::mark("main");
    agentty::tune_allocator();
    const int rc = maya::thread_root(
        [&](const maya::unlocked& cap) { return run_main(argc, argv, cap); });
    // run_main 拆解完成。 如果一个有界限的
    // 关闭放弃了 线程 返回会破坏它下面的静态,
    // 所以跳过:冲掉我们写的东西,然后离开。
    if (maya::strays() != 0) {
        std::fflush(nullptr);
        std::_Exit(rc);
    }
    return rc;
}
