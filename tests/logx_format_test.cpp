// logx_format_test - 记录 LINE FORMAT 合同。
//
// 用户会用它粘贴,我们请他们粘贴它,
// `agentty diagnostics` 解析。一个看起来无害
// 的格式变化( 放下频道列, 重新排序关卡) 静默地打破了所有三个关卡。
// 这些测试将形状固定在。

#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

#include "agtest.hpp"
#include "agentty/util/logx.hpp"

using namespace agentty;

namespace {

std::string last_line() {
    const auto lf = logx::log_file();
    if (lf.empty()) return {};
    std::ifstream in{std::string{lf}, std::ios::binary};
    std::string all{std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()};
    auto end = all.find_last_not_of('\n');
    if (end == std::string::npos) return {};
    auto start = all.rfind('\n', end);
    return all.substr(start == std::string::npos ? 0 : start + 1);
}

// 见logx_redaction_test中的注:跳过日志测试是绿色谎言.
void require_logging() {
    REQUIRE_MESSAGE(!logx::log_file().empty(),
                    "AGENTTY_LOG/_FILE must be set for this binary "
                    "(ctest sets them; see cmake/AgenttyTests.cmake)");
}

} // 命名空间结束。

TEST_CASE("logx: the line shape is stable") {
    require_logging();
    logx::emit(logx::Channel::Wire, logx::Level::Warn, "fmt.probe", "k=v");
    const auto line = last_line();

    // 2026-08-28T01:23:45.678+0012345ms
    // 1a2bW 传输    fmt.probe : k=v 只在开头时安乔:
    // MESSAGE 可能包含任何内容( raw ) 传输 正文 包括新线,
    // 所以将 头文件 前缀是合同,而不是整行。
    const std::regex header{
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3} )"   // 墙钟
        R"(\+\d+ms )"                                        // monotonic
        R"([0-9a-f]{4} )"                                    // 线程 标签
        R"([TDIWE] )"                                        // 级别字符
        R"(\w+ +)"                                           // 频道
        R"([\w.]+: )"};                                       // 网站
    INFO("line = " << line);
    CHECK(std::regex_search(line, header, std::regex_constants::match_continuous));
}

TEST_CASE("logx: every channel and level renders") {
    require_logging();
    // kChannelNames 中缺失的通道会打印垃圾或崩溃;
    // 这条通道会横跨整个枚举, 添加一个而不命名它在这里失败。
    for (std::size_t i = 0; i < logx::kChannels; ++i) {
        const auto ch = static_cast<logx::Channel>(i);
        logx::emit(ch, logx::Level::Error, "fmt.chan", "i={}");
        const auto line = last_line();
        INFO("channel index = " << i);
        CHECK(line.find(std::string{logx::kChannelNames[i]}) != std::string::npos);
    }
    struct LV { logx::Level lv; char ch; };
    for (auto [lv, c] : {LV{logx::Level::Trace, 'T'}, LV{logx::Level::Debug, 'D'},
                         LV{logx::Level::Info,  'I'}, LV{logx::Level::Warn,  'W'},
                         LV{logx::Level::Error, 'E'}}) {
        logx::emit(logx::Channel::General, lv, "fmt.lv", "x");
        const auto line = last_line();
        INFO("level char = " << c);
        CHECK(line.find(std::string{" "} + c + " ") != std::string::npos);
    }
}

TEST_CASE("logx: a site name quoted inside a payload is not a site field") {
    require_logging();
    // `agentty diagnostics` 通过扫描日志重建会话
    // `startup` 财务报告和已审计财务报表 `provider.select`
    // 事件 曾经用赤裸的 find() ——与“......
    // ”一词相对应。 REQUEST BODY 引用了字符串,
    // 并打印了用户作为活动提供者的一块对话。在真实日志中看到了。
    //
    // 区别在于 POSITION : 站点字段在行中 头文件,
    // a 载荷 被击中的生活深处 JSON 夹住那个
    // 头文件 保持足够短的扫描仪柱 将它们分开。
    logx::emit(logx::Channel::Wire, logx::Level::Trace, "anthropic.request.body",
               "raw={{\"text\":\"provider.select: provider=bogus\"}}");
    const auto line = last_line();

    const auto at = line.find("provider.select");
    REQUIRE(at != std::string::npos);      // 编号 载荷 痷
    CHECK(at > 64);                        // 却远过了 头文件

    // 真实的 事件 把它的场地 放在里面 头文件 窗口中选择一个窗口。
    logx::emit(logx::Channel::Wire, logx::Level::Warn, "provider.select",
               "provider=anthropic kind=anthropic");
    const auto real = last_line();
    const auto site = real.find("provider.select");
    REQUIRE(site != std::string::npos);
    CHECK(site <= 64);
    CHECK(real.compare(site + std::string_view{"provider.select"}.size(), 1, ":") == 0);
}

TEST_CASE("logx: the session banner cannot forge a record") {
    require_logging();
    // 横幅带了进程 CWD ,目录名称可以合法地包含一条新线(
    // `mkdir $'x\nfoo'` 工作在每一个 POSIX 系统).
    // 书面逐字记录, 结束横幅的早期, 将其它路径降为第
    // 0 栏—— 没有任何东西能区分它与真实记录。
    // 选择的路径看起来像一个时间戳错误, 然后将 grep
    // 变成真实的。
    //
    // 与建筑相抗衡的可验证开发 二进制程序 守卫存在之前:
    // `cd $'…/x\n2026-01-01T00:00:00.000 +0000000ms dead E general auth' && agentty --version`
    // 在日志中添加一个伪造的认证错误。
    // 规则与 事件 正文 - 一则记录就是一行
    logx::session_banner(
        "1.0 pid=1 cwd=/tmp/x\n"
        "2026-01-01T00:00:00.000 +0000000ms dead E general auth");

    std::ifstream in{std::string{logx::log_file()}, std::ios::binary};
    std::string all{std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()};

    // 每个 发生, 不只是第一个。 日志文件被附加在横跨,
    // 所以 find() 一个人会很快乐地检查 前面写
    // 好的一条线,已经 -fixed 运行并报告成功,
    // 而当前构建则伪造记录。 (准确的抓取方式: 本测试的
    // 第一个版本通过一个故意返回的守护。 )
    const std::string needle = "2026-01-01T00:00:00.000";
    std::size_t at = all.find(needle);
    REQUIRE(at != std::string::npos);   // 文字真的在里面,你看看,我看看,你有没有看到
    int checked = 0;
    while (at != std::string::npos) {
        INFO("occurrence at byte " << at);
        CHECK(at > 0);
        CHECK(all[at - 1] != '\n');     // 永远不要开始行
        ++checked;
        at = all.find(needle, at + 1);
    }
    CHECK(checked > 0);
}

TEST_CASE("logx: a message with newlines stays one grep-able record") {
    require_logging();
    // Raw 传输 正文包含新线(SSE 帧是\n\n分离).
    // 他们不能分割记录:文件是行 -oriented 每一个
    // 消费者... `grep ' E '` ,诊断会话扫描仪,
    // 用户粘贴一行成问题——假设一个 事件 单行。
    //
    // 这曾经只是断言 末尾 碎片是 "recoverable" ,
    // 从技术上说,分立记录是令人满意的。
    // 正文 将错误的级别隐藏在 grep 中,
    // a 载荷 引用一个站点名称 在它自己的线条开头 与真实的,
    // 是无法区分的 事件 (这就是诊断结果 报告大量
    // 谈话 作为活跃的提供者)。
    logx::emit(logx::Channel::Wire, logx::Level::Error, "fmt.multiline",
               "data: {{\"a\":1}}\n\ndata: {{\"b\":2}}");
    const auto line = last_line();

    // 两者 帧 位于ONE线上,读者返回.
    CHECK(line.find("\"a\":1") != std::string::npos);
    CHECK(line.find("\"b\":2") != std::string::npos);
    // 仍然带着它 头文件 ,所以该级别是可擦除的。
    CHECK(line.find(" E ") != std::string::npos);
    CHECK(line.find("fmt.multiline:") != std::string::npos);

    // 车厢返回也绝不能幸存——一条流浪的\r让线路
    // 在寻呼机中显得短路.
    logx::emit(logx::Channel::Wire, logx::Level::Error, "fmt.crlf",
               "one\r\ntwo");
    const auto crlf = last_line();
    CHECK(crlf.find('\r') == std::string::npos);
    CHECK(crlf.find("one") != std::string::npos);
    CHECK(crlf.find("two") != std::string::npos);
}
