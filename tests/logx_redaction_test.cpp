// logx_redaction_test - 木头必须是 SAFE TO SEND 财务报告和已审计财务报表 COMPLETE.
//
// 两个属性,都学到了难的方法:
//
//  1. 不 TRUNCATED 用于分享飞行记录器固定的预览器
//     384-byte 插槽,所以,( 插槽) `wire=trace`
//     ——记录为: "verbatim and untruncated" ——
//     静态放弃了每条请求的~90%. 正文。一个看起来完整但并不比没有痕迹更
//     糟糕的痕迹: 它让你在错误的地方寻找虫子。
//
//  2. REDACTED。日志的整个要点是用户发送。 正文
//     携带授权 头文件, api_key 字段和 OAuth 符号;
//     "remember to scrub it first" 不是一个有效的控制。
//     编辑发生在单曲 emit() 接口边界 所以没有 调用 网站可以忘记它。

#include <cstdlib>
#include <fstream>
#include <string>

#include "agtest.hpp"
#include "agentty/util/logx.hpp"

using namespace agentty;

namespace {

// 释放一行,然后归还文件里真正落下的东西。
std::string emit_and_read(const std::string& msg) {
    const std::string path = std::string{std::getenv("TMPDIR") ?
        std::getenv("TMPDIR") : "/tmp"} + "/agentty-redact-probe.log";
    std::remove(path.c_str());
    // 水槽每个过程解决一次,所以驱动真正的发射路径,
    // 并通过公共存取器读回,而不是re-opening a sink.
    logx::emit(logx::Channel::Wire, logx::Level::Error, "test.probe", msg);
    const auto lf = logx::log_file();
    // 一定有水槽不见了 FAIL ,不返回 "" —— 这里的
    // 每个断言都是以一个空字符串满足的表单 find(
    // secret) = = npos。 当 ctest 的内存
    // 停止命名日志文件时( ) AGENTTY_LOG_FILE
    // 整间剪辑室都变绿色了 并且没有检查 漏水也真的发了
    REQUIRE_MESSAGE(!lf.empty(),
                    "no log sink: the probe would pass vacuously");
    std::ifstream in{std::string{lf}, std::ios::binary};
    std::string all{std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()};
    REQUIRE_MESSAGE(!all.empty(), "log file is empty: nothing was emitted");
    auto end = all.find_last_not_of('\n');
    REQUIRE(end != std::string::npos);
    auto start = all.rfind('\n', end);
    return all.substr(start == std::string::npos ? 0 : start + 1);
}

// 这些是作为 STANDALONE 二进制程序 谁是考验 ENVIRONMENT 套装
// AGENTTY_LOG + AGENTTY_LOG_FILE (见A/C.5/61/L.48,
// 第16段) cmake/AgenttyTests.cmake 如果伐木被切断,
// 则会错位。 REQUIRE 而不是跳过,因为一个测试 沉默地拒绝 -ops
// 比失败者更糟糕的是:它报告绿色,而声称什么都不存在,这正是他们生活在综合体中时所发生的情况。
// 二进制程序.
void require_logging() {
    REQUIRE_MESSAGE(!logx::log_file().empty(),
                    "AGENTTY_LOG/_FILE must be set for this binary "
                    "(ctest sets them; see cmake/AgenttyTests.cmake)");
}

} // 命名空间结束。

TEST_CASE("logx: a long body is not truncated") {
    require_logging();
    const std::string body(8000, 'x');
    const auto line = emit_and_read("raw=" + body);
    // 旧定型缓冲区封顶全线384字节.
    CHECK(line.size() > 8000);
    CHECK(line.find(body) != std::string::npos);
}

TEST_CASE("logx: bearer tokens and api keys are redacted") {
    require_logging();
    struct Case { const char* msg; const char* leak; };
    const Case cases[] = {
        {R"(authorization: Bearer sk-proj-AbCdEf0123456789XYZ)", "sk-proj-AbCdEf"},
        {R"({"api_key":"sk-ant-api03-SECRETVALUE12345"})",       "SECRETVALUE"},
        {R"(github token ghu_16CharsOfTokenHere123456)",          "16CharsOfToken"},
        {R"({"access_token":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.aaa"})", "eyJhbGciOi"},
        {R"({"refresh_token":"rt_abcdefghijklmnop"})",            "abcdefghijklmnop"},
        {R"(client_secret=abcdef1234567890abcdef)",               "abcdef1234567890"},
    };
    for (const auto& c : cases) {
        INFO("input = " << c.msg);
        const auto line = emit_and_read(c.msg);
        CHECK(line.find(c.leak) == std::string::npos);   // 秘密不见了
        CHECK(line.find("<redacted>") != std::string::npos);
    }
}

TEST_CASE("logx: redaction keeps the line diagnosable") {
    require_logging();
    // 结构必须生存下来——一个吃周围的JSON的
    // 重置器使木头变得无用,这比失掉的符号更糟糕.
    const auto line = emit_and_read(
        R"({"model":"gpt-5.6-luna","api_key":"sk-abcdefghijklmnop","stream":true})");
    CHECK(line.find("gpt-5.6-luna") != std::string::npos);
    CHECK(line.find("\"stream\":true") != std::string::npos);
    CHECK(line.find("abcdefghijklmnop") == std::string::npos);
}

TEST_CASE("logx: ordinary payload is never mangled") {
    require_logging();
    // 假阳性是真正的风险: 完 -eager 编辑会
    // 悄悄地腐蚀 载荷 你试图阅读。
    const auto line = emit_and_read(
        R"({"pattern":"foo-bar_baz","path":"src/task-runner.cpp","sk":"short"})");
    CHECK(line.find("foo-bar_baz") != std::string::npos);
    CHECK(line.find("src/task-runner.cpp") != std::string::npos);
    CHECK(line.find("<redacted>") == std::string::npos);
}

// 现实的漏泄路径: 错误中回應请求的提供者 正文 ,或 a ,
// 或 config/tool 载荷 里面嵌着一把钥匙 头文件
// 从来没有记录,所以 此 真正的钥匙会浮出水面
TEST_CASE("logx: a key embedded in a body is redacted") {
    require_logging();
    const auto line = emit_and_read(
        R"({"error":{"message":"Incorrect API key provided: sk-proj-Abc123Def456Ghi789. )"
        R"(You can find your API key at https://platform.openai.com/account/api-keys."}})");
    CHECK(line.find("Abc123Def456Ghi789") == std::string::npos);
    CHECK(line.find("<redacted>") != std::string::npos);
    // 信息中可操作的部分必须生存下去。
    CHECK(line.find("Incorrect API key provided") != std::string::npos);
    CHECK(line.find("platform.openai.com") != std::string::npos);
}

// ── 销售商密钥形状 ────────────────────────────────────────────────────
//
// 通过检测形状提供者找到 ACTUALLY 而不是我想象的 格鲁克的 `gsk_`
// 财务报告和已审计财务报表 `AWS_SECRET_ACCESS_KEY=`
// 都通过第一个版本的重排机泄露了出来.
TEST_CASE("logx: real vendor key shapes are redacted") {
    require_logging();
    struct Case { const char* msg; const char* leak; };
    const Case cases[] = {
        {R"(x-api-key: sk-ant-api03-REALLOOKINGKEY0123456789)", "REALLOOKINGKEY"},
        {R"(Authorization: Basic dXNlcjpwYXNzd29yZDEyMzQ1Ng==)", "dXNlcjpwYXNz"},
        {R"(AWS_SECRET_ACCESS_KEY=wJalrXUtnFEMIbKxKEY0123456789EXAMPLEKEY)", "wJalrXUtnFEM"},
        {R"({"anthropic_api_key":"sk-ant-VALUE9876543210abc"})", "VALUE9876543210"},
        {R"(gsk_GroqKeyLooksLikeThis0123456789abcd)", "GroqKeyLooksLike"},
        // 勇敢的搜索 API 头文件 :一个赤裸的标志,没有卖家前缀来匹配.
        {R"(X-Subscription-Token: BSAk1234567890abcdefghijKEYBODY)", "BSAk1234567890"},
        {R"(export ANTHROPIC_API_KEY=sk-ant-api03-abcdefghij0123456789)", "abcdefghij0123"},
        // 粘贴到 CONVERSATION ,而不是一个 头文件 —
        // —实际到达日志的路径,自此. 头文件 从来没有记录。
        {R"({"messages":[{"role":"user","content":"my key is sk-proj-LEAK123456789abc"}]})",
         "LEAK123456789"},
    };
    for (const auto& c : cases) {
        INFO("input = " << c.msg);
        const auto line = emit_and_read(c.msg);
        CHECK(line.find(c.leak) == std::string::npos);
        CHECK(line.find("<redacted>") != std::string::npos);
    }
}

// 反向的,更重要的: 结束 -eager 默默地重新编辑 CORRUPTS
// 编号 载荷 您正在阅读。 AWS 键添加空 "secret" 键 -word ,
// 该食用 `/etc/secrets/config.yaml` 财务报告和已
// 审计财务报表 `{"pattern":"secret_sauce"}`
// ——并且由于标记是写在原位的,因此产生了重复的垃圾而不是干净的替换.
TEST_CASE("logx: ordinary prose that merely mentions secrets is untouched") {
    require_logging();
    const char* clean[] = {
        R"({"pattern":"secret_sauce","path":"src/secrets_test.cpp"})",
        R"({"tool":"grep","args":{"pattern":"password","glob":"*.env"}})",
        R"(error: file not found: /etc/secrets/config.yaml)",
        R"({"model":"gpt-5.6-luna","messages":[{"content":"explain sk-learn"}]})",
        R"({"display_description":"Read the private_key docs section"})",
    };
    for (const auto* c : clean) {
        INFO("input = " << c);
        const auto line = emit_and_read(c);
        CHECK(line.find("<redacted>") == std::string::npos);
        // 还有 载荷 存活下来 字节-for-byte.
        CHECK(line.find(c) != std::string::npos);
    }
}
