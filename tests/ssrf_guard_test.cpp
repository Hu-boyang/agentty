// SSRF 解决 -IP 警卫 - 单元测试 费用 agentty::http::ssrf_ip_blocked.
//
// 网络工具(web_fetch / web_search)拨任意型号-supplied URL.
// mcp-cpp 名称 -based host_blocked ()拒绝明显的内部 * 名称 *,
// 但一个公共主机名可以 DNS-rebind 改为内部 IP 名字检查从不看。
// ssrf_ip_blocked ()是权威的后盾:它运行在
// RESOLVED 拨号器即将到来 connect()。这个定点值是
// 它必须拒绝的幅度(和它必须允许的公开范围),所以未来的编辑不能悄悄
// 打开一个洞。
#undef NDEBUG
#include "agtest.hpp"

#include "agentty/io/http.hpp"

using agentty::http::ssrf_ip_blocked;

TEST_CASE("ssrf: loopback + unspecified are blocked") {
    CHECK(ssrf_ip_blocked("127.0.0.1"));
    CHECK(ssrf_ip_blocked("127.1.2.3"));     // 整体 127/8
    CHECK(ssrf_ip_blocked("0.0.0.0"));       // 0/8 "this host" 国 务 院
    CHECK(ssrf_ip_blocked("::1"));           // v6 循环
    CHECK(ssrf_ip_blocked("::"));            // v6 未指定
    CHECK(ssrf_ip_blocked("[::1]"));         // 括号字数
}

TEST_CASE("ssrf: cloud metadata + link-local are blocked") {
    CHECK(ssrf_ip_blocked("169.254.169.254"));   // 编号 AWS/GCP/Azure 元数据 IP
    CHECK(ssrf_ip_blocked("169.254.0.1"));       // 整体 169.254/16 链接-local
    CHECK(ssrf_ip_blocked("fe80::1"));           // v6 link-local
    CHECK(ssrf_ip_blocked("[fe80::abcd]"));
}

TEST_CASE("ssrf: RFC1918 private ranges are blocked") {
    CHECK(ssrf_ip_blocked("10.0.0.1"));          // 10/8
    CHECK(ssrf_ip_blocked("10.255.255.255"));
    CHECK(ssrf_ip_blocked("172.16.0.1"));        // 172.16/12 下边
    CHECK(ssrf_ip_blocked("172.31.255.254"));    // 172.16/12 上缘
    CHECK(ssrf_ip_blocked("192.168.1.1"));       // 192.168/16
}

TEST_CASE("ssrf: CGNAT, ULA, multicast are blocked") {
    CHECK(ssrf_ip_blocked("100.64.0.1"));        // 100.64/10 CGNAT
    CHECK(ssrf_ip_blocked("100.127.255.255"));
    CHECK(ssrf_ip_blocked("fc00::1"));           // fc00::/7 ULA
    CHECK(ssrf_ip_blocked("fd12:3456::1"));
    CHECK(ssrf_ip_blocked("224.0.0.1"));         // 多播/保留
    CHECK(ssrf_ip_blocked("ff02::1"));           // v6 多重广播
}

TEST_CASE("ssrf: IPv4-mapped IPv6 is re-checked as its v4 address") {
    // 绕行一个天真"starts with fc/fe80/::1" v6 过滤器漏掉。
    CHECK(ssrf_ip_blocked("::ffff:127.0.0.1"));
    CHECK(ssrf_ip_blocked("::ffff:169.254.169.254"));
    CHECK(ssrf_ip_blocked("::ffff:10.0.0.1"));
    CHECK(ssrf_ip_blocked("[::ffff:192.168.0.1]"));
    // ...... 和地图 PUBLIC 地址必须仍然允许。
    CHECK(!ssrf_ip_blocked("::ffff:8.8.8.8"));
}

TEST_CASE("ssrf: public addresses are allowed") {
    CHECK(!ssrf_ip_blocked("8.8.8.8"));          // 谷歌( G) DNS
    CHECK(!ssrf_ip_blocked("1.1.1.1"));          // 云飞
    CHECK(!ssrf_ip_blocked("140.82.121.4"));     // github.com-ish
    CHECK(!ssrf_ip_blocked("172.15.0.1"));       // 刚刚 下方 172.16/12
    CHECK(!ssrf_ip_blocked("172.32.0.1"));       // 刚刚 上方 172.16/12
    CHECK(!ssrf_ip_blocked("100.63.255.255"));   // 刚刚下方 CGNAT
    CHECK(!ssrf_ip_blocked("100.128.0.0"));      // 刚刚以上 CGNAT
    CHECK(!ssrf_ip_blocked("2606:4700:4700::1111")); // 云飞 v6
}

TEST_CASE("ssrf: unparseable / non-numeric input fails closed") {
    CHECK(ssrf_ip_blocked(""));                  // 空
    CHECK(ssrf_ip_blocked("example.com"));       // a NAME ,而不是一个 IP
    CHECK(ssrf_ip_blocked("not-an-ip"));
    CHECK(ssrf_ip_blocked("999.999.999.999"));   // 超出范围
    CHECK(ssrf_ip_blocked("[]"));
}
