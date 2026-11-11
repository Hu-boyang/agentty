// 沙箱代理的解析器,与字节,GUEST所选择的XX002x.
//
// 原因 此 HARNESS EXISTS。其他的都在这里。 沙箱 读取输入
// agentty 这三个函数改为 a 字节 数据流 由盒子里的所有东西写,
// 在一个糟糕的日子, 是我们所要防御的。 他们是东道主一方唯一的攻击者
// -controlled 解析器 ,所以,我说 "looks careful"
// 不够好: libFuzzer + ASan + UBSan 带它们走.
//
// 或 不变量 如下所述:a 解析器 上面写着 `ok`
// 给我们一个宿主和港口 我们即将 DIAL ,所以一个空主机、
// 零端口或嵌入式的“确定” NUL 可能是牙齿的虫子
// `need_more` 必须是单调的——一个有效请求的前缀
// 必须要求更多而不是要求错误的,或者一个客户端在两包中写出其
// 请求会得到虚假的拒绝.
#include "agentty/tool/util/sandbox_netproxy.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace np = agentty::tools::util::sandbox::netproxy;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "invariant failed: %s at %s:%d\n", #cond,     \
                         __FILE__, __LINE__);                                  \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

namespace {

// 目标 解析器 被接受的是我们即将解决和连接的。
void check_target(const np::Target& t) {
    if (!t.ok) {
        // 不接受:没有承诺 host/port ,但一个 解析器 永远不能要求两者
        // "malformed" 财务报告和已审计财务报表 "incomplete" 立刻...
        // 打电话者会说出具体内容
        return;
    }
    CHECK(!t.need_more);                             // OK和need_more是独家的
    CHECK(!t.host.empty());                          // 我们不拨号
    CHECK(t.port != 0);                              // 端口 0 不是目标
    CHECK(t.host.find('\0') == std::string::npos);   // 会在解题处划掉
    CHECK(t.host.size() <= 255);                     // 主机名的硬度限制
}

// `need_more` 必须是向有用的方向单调: 前面的字节太短,
// 无法完整地请求。 need_more 而不是说
// "malformed" 否则,一个客户端在两包中写下其请求,
// 就会遭到虚假的拒绝,这看起来像是一个片面的代理,而不是一个 解析器 虫子,你这混蛋
//
// NOTE 这有什么用 不。有效的请求可以是: FOLLOWED 更多,
// 因为 字节 (客户管道和模糊器连接),所以一个完整的前
// 缀是完全合法的 -- -- 第一次发现这个绳子的崩溃是
// 测试中的错误假设,而不是测试中的错误错误。
// 解析器 真实的 不变量 是关于严格短于最小完整请求的前缀:
// 10 字节 用于IPv4,这是最短的表单.
void check_prefix_monotonic(std::string_view full) {
    const auto whole = np::parse_socks5_request(full);
    if (!whole.target.ok) return;
    for (std::size_t n = 1; n < full.size() && n < 7; ++n) {
        const auto part = np::parse_socks5_request(full.substr(0, n));
        // 7岁以下 字节 无事可完成( 05) CMD 00 ATYP
        // +1加器+2端口为底板),所以 解析器 肯定要求更多钱
        CHECK(!part.target.ok);
        CHECK(part.target.need_more);
    }
}

}  // 命名空间结束。

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const std::uint8_t which = data[0];
    const std::string_view in{reinterpret_cast<const char*>(data) + 1, size - 1};

    switch (which % 4) {
    case 0: {   // 编号 SOCKS5 问候
        const auto g = np::parse_socks5_greeting(in);
        if (g.ok) {
            CHECK(!g.need_more);
            CHECK(g.consumed >= 2);
            CHECK(g.consumed <= in.size());   // 从来没有过 缓冲区
        }
        break;
    }
    case 1: {   // 编号 SOCKS5 请求
        const auto r = np::parse_socks5_request(in);
        check_target(r.target);
        if (r.target.ok) {
            // 仅接受三种地址类型,仅限 CONNECT 已送达 -
            // - -- 但 解析器 报告Cmd而不是判断它,
            // 所以任何价值在这里是合法的。
            CHECK(r.atyp == 0x01 || r.atyp == 0x03 || r.atyp == 0x04);
        }
        check_prefix_monotonic(in);
        break;
    }
    case 2: {   // HTTP 请求行
        const auto h = np::parse_http_request(in);
        check_target(h.target);
        if (h.target.ok && h.connect) {
            // CONNECT 权威 -form :端口是明确的,从未默认.
            CHECK(in.find("CONNECT") == 0);
        }
        break;
    }
    case 3: {   // 地址过滤器, 显示于 解析器 文本中的任何文本
        const std::string s{in};
        const bool blocked = np::is_blocked_address(s);
        // 一条硬规则:回转和私人形式 始终 被封禁,
        // 但他们被拼写。 这里的回归是审计的#
        // 1 发现返回。
        static constexpr const char* kMustBlock[] = {
            "127.0.0.1", "127.1.2.3", "::1", "::ffff:127.0.0.1",
            "10.0.0.1", "172.16.0.1", "192.168.1.1", "169.254.169.254",
            "100.64.0.1", "fc00::1", "fe80::1", "0.0.0.0",
        };
        for (const char* m : kMustBlock) CHECK(np::is_blocked_address(m));
        // 公共地址没有被封锁,或者代理服务什么也没有。
        CHECK(!np::is_blocked_address("1.1.1.1"));
        CHECK(!np::is_blocked_address("93.184.216.34"));
        CHECK(!np::is_blocked_address("2606:2800:220:1:248:1893:25c8:1946"));
        // 任何无法解析的都须失败 CLOSED。
        if (!blocked) CHECK(!s.empty());
        // A NUL 切换字符串前 inet_pton 看见了,
        // 所以 字节 一个人在这里被忽略之后...
        // "A::\0junk" 地址A:而且合法允许。
        // PARSERS 拒绝包含 NUL 在它到达此功能之前,
        // 切换不能用来在过滤器上偷运第二个地址。
        // 请在它实际居住的地方确认此防御:
        {
            // 05 CONNECT 00 ATYP 名字叫伦 "1.1.1.1\0127.0.0.1" 端口=80个
            const std::string smuggle = std::string("\x05\x01\x00\x03", 4)
                + static_cast<char>(17) + std::string("1.1.1.1\0" "127.0.0.1", 17)
                + std::string("\x00\x50", 2);
            const auto r = np::parse_socks5_request(smuggle);
            CHECK(!r.target.ok);   // 拒绝为NUL提供,未解决
        }
        break;
    }
    }
    return 0;
}
