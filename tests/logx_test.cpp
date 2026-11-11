// logx_test 结构化诊断记录( agentty::logx).
//
// 封面: 过滤器 解析 (赤度,chan=平度,混音,垃圾),闸门语义学(conte
// semics). enabled/recorded 环 -always-on-Warn +),
// 文件槽(原子附着物陆地,格式形状),飞行记录器(环捕获,堆放定购,
// 包包),横跨,以及dbglogcompat shim.
//
// 信封( E) -sensitive : 套装 AGENTTY_LOG/_FILE。
// logx 将它配置在第一个位置 调用 (魔法静态),所以这个测试
// 依赖于自己的过程(独立) 二进制程序 )和配置 之前 任何伐木作业。

#include "agentty/util/logx.hpp"
#include "agentty/util/dbglog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int g_fail = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; }         \
        else         { std::printf("  ok:   %s\n", msg); }                   \
    } while (0)

using namespace agentty;

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 平台time dir("/tmp"在Windows上不存在)下的文件.
static std::string tmp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

int main() {
    // 配置( O) 之前 第一个对数 调用 : 默认警告, 传输 宽宽的,
    // 破烂的, 暂时的路径中的文件。
    const std::string log_path = tmp_file("agentty-logx-test.log");
    std::remove(log_path.c_str());
    setenv("AGENTTY_LOG", "warn,wire=trace,rag=off", 1);
    // AGENTTY_LOG_FILE 目的地被设定为同一地点 接口
    // 边界 `--log-file`。必须在第一个日志之前发生 调用 ,
    // 这正是合同 set_log_path (a) 文件。
    logx::set_log_path(log_path);

    // ── 门语义 ────────────────────────────────────────────────
    CHECK(logx::enabled(logx::Channel::Wire, logx::Level::Trace),
          "filter: wire=trace opens the wire channel at trace");
    CHECK(!logx::enabled(logx::Channel::General, logx::Level::Debug),
          "filter: default warn masks general/debug");
    CHECK(logx::enabled(logx::Channel::General, logx::Level::Warn),
          "filter: default warn passes general/warn");
    CHECK(!logx::enabled(logx::Channel::Rag, logx::Level::Error),
          "filter: rag=off silences even error ON THE FILE");
    CHECK(logx::recorded(logx::Channel::Rag, logx::Level::Error),
          "recorded: Warn+ always reaches the flight ring, even when the "
          "file filter silences the channel");

    // ── 文件槽+格式 ────────────────────────────────────────────
    AGT_LOG(Wire, Trace, "test.site", "hello {} {}", 42, "world");
    AGT_LOG(General, Debug, "test.site", "MUST NOT APPEAR {}", 1);
    AGT_LOG(General, Error, "test.err", "an error line");
    {
        const std::string got = slurp(log_path);
        CHECK(got.find("hello 42 world") != std::string::npos,
              "file: enabled trace line lands");
        CHECK(got.find("MUST NOT APPEAR") == std::string::npos,
              "file: masked debug line does NOT land");
        CHECK(got.find("an error line") != std::string::npos,
              "file: default-warn error line lands");
        CHECK(got.find(" T wire") != std::string::npos,
              "format: level char + channel name present");
        CHECK(got.find("test.site: ") != std::string::npos,
              "format: site prefix present");
        // 墙 -clock + 单调递增的 邮票。
        CHECK(got.find("ms ") != std::string::npos,
              "format: monotonic ms stamp present");
    }

    // ── logx::log_file 决议 ─────────────────────────────────────
    CHECK(logx::log_file() == log_path, "log_file() reports the sink path");

    // ── 飞行记录器:抓获+倾卸+订购 ────────────────────
    AGT_LOG(General, Warn, "ring.a", "first");
    AGT_LOG(General, Warn, "ring.b", "second");
    {
        const std::string dump_path = tmp_file("agentty-logx-ring.txt");
        std::remove(dump_path.c_str());
        CHECK(logx::dump_flight_recorder_to(dump_path.c_str()),
              "ring: dump writes bytes");
        const std::string got = slurp(dump_path);
        CHECK(got.find("flight recorder") != std::string::npos,
              "ring: dump carries the header");
        const auto a = got.find("ring.a: first");
        const auto b = got.find("ring.b: second");
        CHECK(a != std::string::npos && b != std::string::npos,
              "ring: both warn events captured");
        CHECK(a < b, "ring: dump is oldest-first");
        // 擦除为 rag=off 事件 仍然到达环( 3) recorded() 合同)。
        AGT_LOG(Rag, Error, "ring.rag", "silenced-on-file, ringed");
        std::remove(dump_path.c_str());
        (void)logx::dump_flight_recorder_to(dump_path.c_str());
        CHECK(slurp(dump_path).find("ring.rag") != std::string::npos,
              "ring: file-silenced error still recorded");
        std::remove(dump_path.c_str());
    }

    // ── 环绕环: > kRingSlots 事件 仅保留最新 ──────
    {
        for (int i = 0; i < 300; ++i)
            AGT_LOG(General, Warn, "wrap", "event {}", i);
        const std::string dump_path = tmp_file("agentty-logx-wrap.txt");
        std::remove(dump_path.c_str());
        (void)logx::dump_flight_recorder_to(dump_path.c_str());
        const std::string got = slurp(dump_path);
        CHECK(got.find("event 299") != std::string::npos,
              "wrap: newest event survives");
        CHECK(got.find("event 0\n") == std::string::npos,
              "wrap: oldest event evicted");
        std::remove(dump_path.c_str());
    }

    // ── 西班牙语 ─────────────────────────────────────────────────────────
    {
        { AGT_SPAN(Wire, "test.span"); }   // 跟踪时输入+ 退出
        const std::string got = slurp(log_path);
        CHECK(got.find("test.span: begin") != std::string::npos,
              "span: begin logged at trace");
        CHECK(got.find("test.span: end (") != std::string::npos,
              "span: end logged with duration");
        // 蒙面频道的一段间隙什么也没有
        const auto size_before = slurp(log_path).size();
        { AGT_SPAN(Rag, "rag.span"); }
        CHECK(slurp(log_path).size() == size_before,
              "span: masked channel emits nothing");
    }

    // ── dbglog compat shim 语句 ────────────────────────────────────────────
    {
        util::dbglog("legacy.site", "legacy message");
        const std::string got = slurp(log_path);
        CHECK(got.find("legacy.site: legacy message") != std::string::npos,
              "shim: util::dbglog lands as general/error");
        CHECK(util::dbglog_enabled(),
              "shim: dbglog_enabled true (warn default passes error)");
    }

    // 长消息截断应安全：不溢出，且仍有字符串终止符。
    {
        std::string big(2000, 'x');
        AGT_LOG(General, Warn, "trunc", "{}", big);
        const std::string dump_path = tmp_file("agentty-logx-trunc.txt");
        std::remove(dump_path.c_str());
        (void)logx::dump_flight_recorder_to(dump_path.c_str());
        CHECK(slurp(dump_path).find("trunc: xxx") != std::string::npos,
              "trunc: oversized message captured (clipped, not dropped)");
        std::remove(dump_path.c_str());
    }

    std::remove(log_path.c_str());
    if (g_fail == 0) std::printf("All logx tests passed\n");
    return g_fail == 0 ? 0 : 1;
}
