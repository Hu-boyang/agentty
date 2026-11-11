// 沙箱代理主机允许清单:纯名配对者,按个案进行.
// 代理执行它时的宾名ASKED,因为在其解决任何
// 问题之前,这里的每一个规则都是整个规则——后面
// 没有DNS步骤可以挽救错误的配对.
#include "agtest.hpp"

#include "agentty/tool/util/sandbox_netproxy.hpp"

#include <string>
#include <vector>

namespace np = agentty::tools::util::sandbox::netproxy;

TEST_CASE("netproxy: an empty allowlist narrows nothing") {
    const std::vector<std::string> none;
    CHECK(np::host_allowed("github.com", none));
    CHECK(np::host_allowed("anything.example", none));
    // 字元也:没有列表的ADDRESS过滤器是唯一的规则,
    // 它运行在这条之后.
    CHECK(np::host_allowed("1.1.1.1", none));
}

TEST_CASE("netproxy: exact entries match exactly that name") {
    const std::vector<std::string> l{"github.com"};
    CHECK(np::host_allowed("github.com", l));
    CHECK(np::host_allowed("GitHub.COM", l));           // DNS 情况 -blind
    CHECK(np::host_allowed("github.com.", l));          // FQDN 跟踪点
    CHECK_FALSE(np::host_allowed("api.github.com", l)); // 无隐含的子域
    CHECK_FALSE(np::host_allowed("github.com.evil.io", l));
    CHECK_FALSE(np::host_allowed("evilgithub.com", l));
    CHECK_FALSE(np::host_allowed("", l));
}

TEST_CASE("netproxy: *.x matches strictly under x, not x, not lookalikes") {
    const std::vector<std::string> l{"*.githubusercontent.com"};
    CHECK(np::host_allowed("raw.githubusercontent.com", l));
    CHECK(np::host_allowed("a.b.githubusercontent.com", l));
    CHECK(np::host_allowed("RAW.GithubUserContent.com", l));
    // 最高额为NOT,包括: 两者都说,如果两者都指。
    CHECK_FALSE(np::host_allowed("githubusercontent.com", l));
    // 领先点就是它成为标签边界的原因。
    CHECK_FALSE(np::host_allowed("evilgithubusercontent.com", l));
    CHECK_FALSE(np::host_allowed("githubusercontent.com.evil.io", l));
}

TEST_CASE("netproxy: IP literals are refused whenever a list is set") {
    // a 数据为: 允许清单 备注 NAMES 无法保证地址,
    // 并且通过文字可以使每个名称规则可以绕过 `curl 140.82.x.x`.
    const std::vector<std::string> l{"github.com", "*.github.com"};
    CHECK_FALSE(np::host_allowed("140.82.112.3", l));
    CHECK_FALSE(np::host_allowed("2606:50c0:8000::153", l));
    CHECK_FALSE(np::host_allowed("[2606:50c0:8000::153]", l));
    CHECK_FALSE(np::host_allowed("127.0.0.1", l));
    // 即使有人列出一条:条目是名称规则,
    // 而字面主机从不走名称路径.
    const std::vector<std::string> lit{"1.1.1.1"};
    CHECK_FALSE(np::host_allowed("1.1.1.1", lit));
}

TEST_CASE("netproxy: several entries are a union") {
    const std::vector<std::string> l{"github.com", "*.github.com", "registry.npmjs.org", "pypi.org."};
    CHECK(np::host_allowed("github.com", l));
    CHECK(np::host_allowed("api.github.com", l));
    CHECK(np::host_allowed("registry.npmjs.org", l));
    CHECK(np::host_allowed("pypi.org", l));             // 条目上跟踪点
    CHECK_FALSE(np::host_allowed("npmjs.org", l));
    CHECK_FALSE(np::host_allowed("files.pythonhosted.org", l));
}

// ── normalize_host_entry : 设置 面板 接受,作为什么 ─────

namespace {
std::string norm(std::string_view s) { return np::normalize_host_entry(s).value; }
bool rejected(std::string_view s) {
    const auto e = np::normalize_host_entry(s);
    return e.value.empty() && !e.why.empty();
}
}  // 命名空间结束。

TEST_CASE("netproxy: pasted URLs and common spellings are trimmed to the host") {
    CHECK(norm("github.com") == "github.com");
    CHECK(norm("  GitHub.COM  ") == "github.com");
    CHECK(norm("https://github.com/1ay1/agentty?tab=readme#x") == "github.com");
    CHECK(norm("http://user:pw@registry.npmjs.org:8080/pkg") == "registry.npmjs.org");
    CHECK(norm("github.com:443") == "github.com");
    CHECK(norm("github.com.") == "github.com");
    // 卷曲 / 缩曲 no_proxy 拼写 "any subdomain".
    CHECK(norm(".github.com") == "*.github.com");
    CHECK(norm("*.GitHub.com") == "*.github.com");
    CHECK(norm("xn--bcher-kva.example") == "xn--bcher-kva.example");   // 复数是一个名称
}

TEST_CASE("netproxy: entries that cannot mean a host are rejected, with a reason") {
    CHECK(rejected(""));
    CHECK(rejected("   "));
    CHECK(rejected("*"));                     // 将允许一切
    CHECK(rejected("*.com"));                 // 整个 TLD
    CHECK(rejected("api.*.github.com"));      // 通配符不最左边
    CHECK(rejected("git*hub.com"));
    CHECK(rejected("140.82.112.3"));          // 地址从来不是列入允许清单
    CHECK(rejected("https://140.82.112.3/"));
    CHECK(rejected("[2606:50c0::153]"));
    CHECK(rejected("2606:50c0::153"));
    CHECK(rejected("github .com"));
    CHECK(rejected("github..com"));
    CHECK(rejected("github.com:http"));
    CHECK(rejected("https://"));
    CHECK(rejected("gith\xc3\xbcb.com"));      // 原始的UTF-8:先用密码编码
    CHECK(rejected(std::string(260, 'a') + ".com"));
}

TEST_CASE("netproxy: a normalised entry matches what the raw one meant") {
    // 面板 所依赖的往返: 接受原始 - > 商店的空穴
    // - > 代理匹配用户所想到的主机。
    for (const char* raw : {"https://GitHub.com/x", "github.com:443", "github.com."}) {
        const std::vector<std::string> l{norm(raw)};
        CHECK(np::host_allowed("github.com", l));
        CHECK_FALSE(np::host_allowed("api.github.com", l));
    }
    const std::vector<std::string> w{norm(".github.com")};
    CHECK(np::host_allowed("api.github.com", w));
    CHECK_FALSE(np::host_allowed("github.com", w));
}
