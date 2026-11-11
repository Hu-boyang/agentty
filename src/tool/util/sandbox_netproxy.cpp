// 电话 沙箱 过滤出网。见 头文件 为什么?
#include "agentty/tool/util/sandbox_netproxy.hpp"

#include "agentty/util/background.hpp"
#include "agentty/util/logx.hpp"

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace agentty::tools::util::sandbox::netproxy {

namespace {

// ── 地址过滤器 ───────────────────────────────────────────────────
//
// 写在 PARSED 字节 而非字符串: 文本检查是怎样 "127.1", "0x7f.1",
// "::ffff:127.0.0.1" 财务报告和已审计财务报表 "2130706433" 通关.
// inet_pton 显示 解析 所以每个地址的拼写都在同
// 一个(或16个)上 字节.

bool v4_blocked(const unsigned char b[4]) noexcept {
    // 0.0.0.0/8 — "this network" .不是目的地,而0.0.0.0本身意味
    // 着: "the local host" 以连接(2)。
    if (b[0] == 0) return true;
    // 127/8 ——宿主自身回旋. 该 寻找
    if (b[0] == 127) return true;
    // 10/8, 172.16/12, 192.168/16 — RFC1918。该 LAN.
    if (b[0] == 10) return true;
    if (b[0] == 172 && (b[1] & 0xf0) == 16) return true;
    if (b[0] == 192 && b[1] == 168) return true;
    // 169.254/16 — 链接-local ,并随其而出
    // 169.254.169.254: 云元数据端点 出出实例 凭据.
    if (b[0] == 169 && b[1] == 254) return true;
    // 100.64/10 — CGNAT ,这也是Tailscale的分布范围。
    // 尾网是一个私人网络,名称不同。
    if (b[0] == 100 && (b[1] & 0xc0) == 64) return true;
    // 192.0.0/24 (IETF 协议任务, 192.0.2/24, 198.51.100/24,
    // 203.0.113/24 (文件编制), 198.18/15 (标本).
    if (b[0] == 192 && b[1] == 0 && b[2] == 0) return true;
    if (b[0] == 192 && b[1] == 0 && b[2] == 2) return true;
    if (b[0] == 198 && b[1] == 51 && b[2] == 100) return true;
    if (b[0] == 203 && b[1] == 0 && b[2] == 113) return true;
    if (b[0] == 198 && (b[1] & 0xfe) == 18) return true;
    // 224/4 多媒体和 240/4 保留:不是独播目的地。
    if ((b[0] & 0xf0) == 224) return true;
    if ((b[0] & 0xf0) == 240) return true;
    return false;
}

bool v6_blocked(const unsigned char b[16]) noexcept {
    // * 128(未具体说明)和1/128(落网)。
    bool all_zero_but_last = true;
    for (int i = 0; i < 15; ++i)
        if (b[i] != 0) { all_zero_but_last = false; break; }
    if (all_zero_but_last && (b[15] == 0 || b[15] == 1)) return true;
    // v4-mapped (:ffff: ) (中文(简体) ). a.b.c.d )和v4 数据 -compatible
    // : 应用 v4 规则, 或 ffff: 127. 0.0.1 直行通过 IPv6 -only 检查。
    bool v4_mapped = true;
    for (int i = 0; i < 10; ++i)
        if (b[i] != 0) { v4_mapped = false; break; }
    if (v4_mapped && ((b[10] == 0xff && b[11] == 0xff) || (b[10] == 0 && b[11] == 0))) {
        unsigned char v4[4] = {b[12], b[13], b[14], b[15]};
        return v4_blocked(v4);
    }
    // fc00::/7——独特的本地,IPv6 LAN.
    if ((b[0] & 0xfe) == 0xfc) return true;
    // fe80:10——  ⁇ . 链接-local.
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true;
    // ff00::8 — 多播.
    if (b[0] == 0xff) return true;
    // 2002:16 (6to4)和2001:32 (Teredo) 隧道
    // 到V4地址,我们无法从这里看到,因此它们可以携带连接LAN的线路.
    if (b[0] == 0x20 && b[1] == 0x02) return true;
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0 && b[3] == 0) return true;
    return false;
}

}  // 命名空间结束。

bool is_blocked_address(std::string_view addr) noexcept {
#if defined(__linux__) || defined(__APPLE__)
    // A string_view 不是一个nul -terminated; inet_pton 需要一个 C 字符串。
    char buf[64];
    if (addr.empty() || addr.size() >= sizeof buf) return true;
    std::memcpy(buf, addr.data(), addr.size());
    buf[addr.size()] = '\0';

    unsigned char v6[16];
    if (::inet_pton(AF_INET6, buf, v6) == 1) return v6_blocked(v6);
    unsigned char v4[4];
    if (::inet_pton(AF_INET, buf, v4) == 1) return v4_blocked(v4);
    // 根本不是地址。 拒绝 : 这里的每个呼叫者都已经解决了,
    // 因此无法解析的字符串是错误, 而错误必须关闭。
    return true;
#else
    (void)addr;
    return true;
#endif
}

namespace hostmatch {

bool ascii_ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lo = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
        if (lo(a[i]) != lo(b[i])) return false;
    }
    return true;
}

// "1.2.3.4" 或任何有 ':' (IPv6,可能括弧).
// 名字在上一个标签中有一个字母; IPv4 字面是数字和点。
bool looks_like_ip_literal(std::string_view h) noexcept {
    if (h.find(':') != std::string_view::npos) return true;
    if (h.empty()) return false;
    for (char c : h)
        if (!((c >= '0' && c <= '9') || c == '.')) return false;
    return true;
}

}  // namespace hostmatch

HostEntry normalize_host_entry(std::string_view raw) {
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    };
    std::string_view s = trim(raw);
    if (s.empty()) return {{}, "empty"};
    // 一个粘贴的URL:保留权威,滴放方案,用户信息,路径,查询.
    if (auto at = s.find("://"); at != std::string_view::npos) s.remove_prefix(at + 3);
    if (auto at = s.find_first_of("/?#"); at != std::string_view::npos) s = s.substr(0, at);
    if (auto at = s.rfind('@'); at != std::string_view::npos) s.remove_prefix(at + 1);
    if (s.empty()) return {{}, "no host name in that"};
    // IPv6 字形(被裂开,或赤裸着结肠)——在端口条之前,
    // 否则在第一端结肠时切开.
    if (s.front() == '[' || std::count(s.begin(), s.end(), ':') > 1)
        return {{}, "an IP address \xc2\xb7 names only"};
    if (auto at = s.rfind(':'); at != std::string_view::npos) {
        const auto port = s.substr(at + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string_view::npos)
            return {{}, "not a host name"};
        s = s.substr(0, at);   // 列表为姓名; 端口为 Access 的任务
    }
    if (!s.empty() && s.back() == '.') s.remove_suffix(1);
    std::string v;
    v.reserve(s.size() + 1);
    if (!s.empty() && s.front() == '.') v = "*";   // ".github.com" == "*.github.com"
    for (char c : s) v.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c);

    if (v == "*" || v == "*.") return {{}, "allows everything \xc2\xb7 leave the list empty"};
    const bool wild = v.starts_with("*.");
    const std::string_view name = wild ? std::string_view{v}.substr(2) : std::string_view{v};
    if (name.empty()) return {{}, "not a host name"};
    if (name.find('*') != std::string_view::npos)
        return {{}, "wildcard must lead, as in *.example.com"};
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return {{}, "not a host name"};
    if (name.size() > 253 || name.front() == '.' || name.back() == '.' ||
        name.find("..") != std::string_view::npos)
        return {{}, "not a host name"};
    if (hostmatch::looks_like_ip_literal(name))
        return {{}, "an IP address \xc2\xb7 names only"};
    // "*.com" : 通配符后的一个标签是整体 TLD.
    if (wild && name.find('.') == std::string_view::npos)
        return {{}, "a whole top-level domain \xc2\xb7 too broad"};
    return {std::move(v), {}};
}

bool host_allowed(std::string_view host, const std::vector<std::string>& allow) noexcept {
    if (allow.empty()) return true;
    if (!host.empty() && host.back() == '.') host.remove_suffix(1);
    if (host.empty() || hostmatch::looks_like_ip_literal(host)) return false;
    for (std::string_view e : allow) {
        // 条目到达了cononical(Proxy::start使列表一次正常化),
        // 但后面的点对直接呼叫者来说是便宜的宽大。
        if (!e.empty() && e.back() == '.') e.remove_suffix(1);
        if (e.starts_with("*.")) {
            const std::string_view suffix = e.substr(1);   // ".example.com"
            // 严格地说 UNDER: a.example.com 对 example.com 没有,
            // 还有 badexample.com 不(主要点是它是什么 使它成为一个标签)。
            if (host.size() > suffix.size() &&
                hostmatch::ascii_ieq(host.substr(host.size() - suffix.size()), suffix))
                return true;
        } else if (hostmatch::ascii_ieq(host, e)) {
            return true;
        }
    }
    return false;
}

}  // namespace agentty::tools::util::sandbox::netproxy

// ── 编号 解析器 ─────────────────────────────────────────────────────────
//
// 平台外#if:这些触摸没有描述符和平台 头文件
// 他们在那里建造许多房屋,每座房屋的长度,都要比照他们的住宅,
// 缓冲区 使用前 - 客人选择这些 字节.
namespace agentty::tools::util::sandbox::netproxy {

namespace parse_detail {

// 一台主机名的硬限(RFC 1035:255octets 包括长度字节).
// 执行中 解析器 而不是留给解脱者:4 KB
// "host" 拒绝它,它不会让一个愚蠢的字符串 出
// Geaddrinfo 和记录拒绝的日志线。
inline constexpr std::size_t kMaxHostLen = 255;

// A 16-bit 大块 -endian。 呼叫者已经限制 -checked `at`.
[[nodiscard]] inline std::uint16_t be16(std::string_view s, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<unsigned char>(s[at]) << 8) | static_cast<unsigned char>(s[at + 1]));
}

// 其传输 字节的地址文字,呈呈文形式.
// Hand-rolled而非inet_ntop,因此解析器不受平台头文件的限制.
[[nodiscard]] inline std::string v4_text(std::string_view b, std::size_t at) {
    std::string out;
    for (int i = 0; i < 4; ++i) {
        if (i) out += '.';
        out += std::to_string(static_cast<unsigned char>(b[at + std::size_t(i)]));
    }
    return out;
}

[[nodiscard]] inline std::string v6_text(std::string_view b, std::size_t at) {
    // 完整,不压缩的表格。获得addrinfo接受它, "correct but verbose"
    // 击败了一只手 -written * 压缩机,带有一个关机 -by-one 里面有,我记得
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (int g = 0; g < 8; ++g) {
        if (g) out += ':';
        const auto hi = static_cast<unsigned char>(b[at + std::size_t(g) * 2]);
        const auto lo = static_cast<unsigned char>(b[at + std::size_t(g) * 2 + 1]);
        out += kHex[hi >> 4];
        out += kHex[hi & 0xf];
        out += kHex[lo >> 4];
        out += kHex[lo & 0xf];
    }
    return out;
}

// 一个十进制的端口,拒绝任何不完全是一个端口的事物.
// 斯特图尔会采取 "80abc", "+80", " 80" 并包在溢出处。
[[nodiscard]] inline bool parse_port(std::string_view s, std::uint16_t& out) noexcept {
    if (s.empty() || s.size() > 5) return false;
    std::uint32_t v = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + static_cast<std::uint32_t>(ch - '0');
        if (v > 65535) return false;
    }
    if (v == 0) return false;   // 端口 0 不是目标
    out = static_cast<std::uint16_t>(v);
    return true;
}

}  // namespace parse_detail

Socks5Greeting parse_socks5_greeting(std::string_view in) noexcept {
    Socks5Greeting g;
    if (in.size() < 2) { g.need_more = true; return g; }
    if (static_cast<unsigned char>(in[0]) != 0x05) return g;   // 否 SOCKS5 数据
    const std::size_t n = static_cast<unsigned char>(in[1]);
    if (in.size() < 2 + n) { g.need_more = true; return g; }
    for (std::size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(in[2 + i]) == 0x00) g.no_auth = true;
    g.consumed = 2 + n;
    g.ok = true;
    return g;
}

Socks5Request parse_socks5_request(std::string_view in) {
    using namespace parse_detail;
    Socks5Request r;
    // 05 CMD 00 ATYP + 至少一个地址 字节 + 2 端口 字节
    if (in.size() < 7) { r.target.need_more = true; return r; }
    if (static_cast<unsigned char>(in[0]) != 0x05) return r;
    r.cmd  = static_cast<std::uint8_t>(in[1]);
    r.atyp = static_cast<std::uint8_t>(in[3]);

    std::size_t port_at = 0;
    if (r.atyp == 0x01) {            // IPv4: 4 字节 (英语).
        if (in.size() < 4 + 4 + 2) { r.target.need_more = true; return r; }
        r.target.host = v4_text(in, 4);
        port_at = 8;
    } else if (r.atyp == 0x03) {     // 名称: 字节, 然后是那么多
        const std::size_t len = static_cast<unsigned char>(in[4]);
        if (in.size() < 5 + len + 2) { r.target.need_more = true; return r; }
        if (len == 0) return r;      // 空主机不是目标
        r.target.host.assign(in.substr(5, len));
        // A "name" 带 NUL 或用刀打人,不是一个名言。
        // 你们不要把它交给判决者。
        if (r.target.host.find('\0') != std::string::npos
            || r.target.host.find('/') != std::string::npos)
            return r;
        // SOCKS5 以255 的长度盖起名称 字节 ,
        // 所以这是皮带和牙套 - 但 HTTP 边没有这样的盖子,
        // 和两个去同一个解析器。
        if (r.target.host.size() > kMaxHostLen) return r;
        port_at = 5 + len;
    } else if (r.atyp == 0x04) {     // IPv6: 16 字节 互联网档案馆的存檔,存档日期2013-09-02.
        if (in.size() < 4 + 16 + 2) { r.target.need_more = true; return r; }
        r.target.host = v6_text(in, 4);
        port_at = 20;
    } else {
        return r;                    // 未知地址类型
    }

    r.target.port = be16(in, port_at);
    if (r.target.port == 0) return r;
    r.consumed = port_at + 2;
    r.target.ok = true;
    return r;
}

HttpRequest parse_http_request(std::string_view head) {
    using namespace parse_detail;
    HttpRequest h;
    // "METHOD SP TARGET SP VERSION";这里只有前两个字段才重要.
    const auto sp1 = head.find(' ');
    if (sp1 == std::string_view::npos) {
        h.target.need_more = head.size() < 8192;
        return h;
    }
    const std::string_view method = head.substr(0, sp1);
    const auto sp2 = head.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) {
        h.target.need_more = head.size() < 8192;
        return h;
    }
    const std::string_view target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    if (method.empty() || target.empty()) return h;

    std::string_view authority;
    if (method == "CONNECT") {
        h.connect = true;
        authority = target;          // 权威-form:主机:端口,需要端口
    } else {
        // 绝对-form: machem://host[:port]/path
        const auto scheme = target.find("://");
        if (scheme == std::string_view::npos) return h;
        const auto start = scheme + 3;
        if (start >= target.size()) return h;
        const auto slash = target.find('/', start);
        authority = target.substr(start, slash == std::string_view::npos
                                             ? std::string_view::npos
                                             : slash - start);
        // 用户info@host是合法;主机是关键部分。
        if (const auto at = authority.rfind('@'); at != std::string_view::npos)
            authority = authority.substr(at + 1);
    }
    if (authority.empty()) return h;

    if (authority.front() == '[') {
        // 括号内为 IPv6 字节,括号外为任何端口
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return h;
        h.target.host.assign(authority.substr(1, close - 1));
        const auto rest = authority.substr(close + 1);
        if (rest.empty()) {
            if (h.connect) return h;   // CONNECT 需要一个明确的端口
            h.target.port = 80;
        } else if (rest.front() == ':') {
            if (!parse_port(rest.substr(1), h.target.port)) return h;
        } else {
            return h;
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            if (h.connect) return h;
            h.target.host.assign(authority);
            h.target.port = 80;
        } else {
            h.target.host.assign(authority.substr(0, colon));
            if (!parse_port(authority.substr(colon + 1), h.target.port)) return h;
        }
    }
    if (h.target.host.empty() || h.target.host.find('\0') != std::string::npos)
        return h;
    if (h.target.host.size() > parse_detail::kMaxHostLen) return h;
    h.target.ok = true;
    return h;
}

}  // namespace agentty::tools::util::sandbox::netproxy

#if !(defined(__linux__) || defined(__APPLE__)) || defined(AGENTTY_NETPROXY_PARSERS_ONLY)

// 电话 解析器 以上为完整文件,当 AGENTTY_NETPROXY_PARSERS_ONLY
// 被设定(模糊的绳子)或当它不是 Linux。代理服务器仍然必须
// LINK ,所以它存在,拒绝开始。
namespace agentty::tools::util::sandbox::netproxy {
Proxy::~Proxy() = default;
bool Proxy::start(int, std::vector<std::string>) { return false; }
void Proxy::stop(const maya::unlocked&) noexcept {}
bool Proxy::running() const noexcept { return false; }
std::uint64_t Proxy::refused() const noexcept { return 0; }
std::uint64_t Proxy::served() const noexcept { return 0; }
}  // namespace agentty::tools::util::sandbox::netproxy

#else

namespace agentty::tools::util::sandbox::netproxy {

namespace {

#if defined(__APPLE__)
// 达文既不接受4也不接受 SOCK_CLOEXEC ,其套接字会上升 SIGPIPE
// 写给一个封闭的 对端 而不是荣誉 MSG_NOSIGNAL. agentty
// 忽略 SIGPIPE 进程 -wide ,但一个 库 千万不要靠这个,
// 所以每个套接字都选择自己退出( SO_NOSIGPIPE。。 内容。 标志 创建
// 后立即应用; 叉 -without-exec 窗口关闭 claybin's
// close_inherited_fds 随便找客人
void sock_prep(int fd) {
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
}
#endif

int sock_stream(int family) {
#if defined(__APPLE__)
    const int s = ::socket(family, SOCK_STREAM, 0);
    if (s >= 0) sock_prep(s);
    return s;
#else
    return ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
#endif
}

// 决 定 `host` 并返回第一个通过过滤器的地址,
// 作为连接的套接字。 `refused` 说吧
//
// 拨号使用 相同 袜子检查过滤器,
// 因此没有窗口可供第二个查询返回不同的答案。
int dial_filtered(const std::string& host, std::uint16_t port, bool& refused) {
    refused = false;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string svc = std::to_string(port);
    if (::getaddrinfo(host.c_str(), svc.c_str(), &hints, &res) != 0 || !res)
        return -1;

    int fd = -1;
    bool any_blocked = false;
    for (addrinfo* a = res; a; a = a->ai_next) {
        char txt[64]{};
        const void* src = nullptr;
        if (a->ai_family == AF_INET)
            src = &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr;
        else if (a->ai_family == AF_INET6)
            src = &reinterpret_cast<sockaddr_in6*>(a->ai_addr)->sin6_addr;
        else
            continue;
        if (!::inet_ntop(a->ai_family, src, txt, sizeof txt)) continue;
        if (is_blocked_address(txt)) {
            any_blocked = true;
            AGT_LOG(Tool, Warn, "sandbox.proxy.refused", "host={} addr={} port={}",
                    host, txt, port);
            continue;
        }
        const int s = sock_stream(a->ai_family);
        if (s < 0) continue;
        if (::connect(s, a->ai_addr, a->ai_addrlen) == 0) { fd = s; break; }
        ::close(s);
    }
    ::freeaddrinfo(res);
    if (fd < 0 && any_blocked) refused = true;
    return fd;
}

// 接受环与所有者之间共享,所以每个字段都是原子:
// co_owned<T> 需要同步, 即类型系统询问
// "is this safe from all of them at once" 而不是承诺
struct ProxyState {
    // 首次 ,所以汇总输入 `ProxyState{hosts}` 设置:
    // 建筑师会让它成为非 -aggregate jaal 无法再看到里面。
    const std::vector<std::string> allow_hosts;
    std::atomic<int> listen_fd{-1};
    std::atomic<bool> live{false};
    std::atomic<std::uint64_t> refused{0};
    std::atomic<std::uint64_t> served{0};
    // allow_hosts 谓 CONST ,固定在建筑: 工作线程 读
    // 在每一个 CONNECT 没有 锁 ,这仅仅是声音,因为没有东西可以写出来。
    // ( jaal '同步检查是这么说的——一个平面字段失败了. )
};

// ── 一种连接, 作为一种状态机器 ──────────────────────────────────
//
// 原因 不 A THREAD EACH, 或每条连接一个
// 阻断的处理函数。 第一版读、拨和抽入内线, 意味着接受环
// 线在整个连接期间都繁忙:`npm install`一次
// 打开十几个套接字,它们彼此排队。 每条线程将固定这个位置,
// 每条接头花费线程, 加上每条的stop/join路径。
//
// 所以每个连接都是小型的国产机器, 一个 民意测验驱动着
// 他们所有人 再加上听众 唯一的阻断 调用 代理中留下的就是
// 民调 `dial_filtered` 是个例外,是故意的:它
// 解决了一个名字,意思是它阻断,一个非 -blocking
// 解析器比这个文件要大得多。 连接器是 DNS 查查可以拖住其他人;
// 它的数据永远不会。
struct Conn {
    enum class Phase : std::uint8_t {
        greet,      // SOCKS5: 读取方法列表
        request,    // 读取 SOCKS5 请求/ 请求 HTTP 头
        relay,      // 两边都打开,移动字节
        done,
    };

    int client{-1};
    int up{-1};
    Phase phase{Phase::greet};
    bool socks{false};        // 由第字节号决定决定
    bool decided{false};      // 我们是否见过 字节 还没到
    bool http_connect{false}; // CONNECT (a) 隧道与绝对 -form (前导)
    std::string in;           // 客户寄来的东西,我们还没分析
    std::string to_up;        // 字节 排队时间 上游 (无) HTTP 头).
    bool client_open{true};
    bool up_open{true};

    void close_all() {
        if (up >= 0) { ::close(up); up = -1; }
        if (client >= 0) { ::close(client); client = -1; }
        phase = Phase::done;
    }
};

// 阅读任何可用的,而不屏蔽。 fulse=客户端已消失。
bool conn_fill(Conn& c) {
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(c.client, buf, sizeof buf);
        if (n > 0) {
            if (c.in.size() + static_cast<std::size_t>(n) > 64 * 1024) return false;
            c.in.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) return false;                     // 有序关闭
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
}

// 将所有我们能做的都写成NON-blocking;剩下的都排队。
void conn_flush(int fd, std::string& out, bool& open) {
    while (!out.empty()) {
        const ssize_t w = ::write(fd, out.data(), out.size());
        if (w > 0) {
            out.erase(0, static_cast<std::size_t>(w));
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        open = false;
        return;
    }
}

void set_nonblocking(int fd) {
    const int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0) ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// 推进一个连接到 字节 已经允许了
void conn_step(Conn& c, std::atomic<std::uint64_t>& refused_count,
               const std::vector<std::string>& allow_hosts) {
    if (c.phase == Conn::Phase::done) return;

    if (!c.decided) {
        if (c.in.empty()) return;
        c.socks = static_cast<unsigned char>(c.in[0]) == 0x05;
        c.decided = true;
        if (!c.socks) c.phase = Conn::Phase::request;   // HTTP 没有什么问候
    }

    if (c.phase == Conn::Phase::greet) {
        const auto g = parse_socks5_greeting(c.in);
        if (g.need_more) return;
        if (!g.ok || !g.no_auth) {
            const char none[2] = {'\x05', '\xff'};   // 无可接受的方法
            std::string reply{none, sizeof none};
            bool open = true;
            conn_flush(c.client, reply, open);
            c.close_all();
            return;
        }
        {
            const char ok[2] = {'\x05', '\x00'};
            std::string reply{ok, sizeof ok};
            bool open = true;
            conn_flush(c.client, reply, open);
        }
        c.in.erase(0, g.consumed);
        c.phase = Conn::Phase::request;
    }

    if (c.phase == Conn::Phase::request) {
        std::string host;
        std::uint16_t port = 0;
        if (c.socks) {
            const auto r = parse_socks5_request(c.in);
            if (r.target.need_more) return;
            auto fail = [&](char code) {
                const char rep[10] = {'\x05', code, '\x00', '\x01', 0, 0, 0, 0, 0, 0};
                std::string reply{rep, sizeof rep};
                bool open = true;
                conn_flush(c.client, reply, open);
                c.close_all();
            };
            if (!r.target.ok) { fail('\x08'); return; }   // 地址类型/ 错误
            if (r.cmd != 0x01) { fail('\x07'); return; }  // 仅限 CONNECT
            host = r.target.host;
            port = r.target.port;
            // 放弃请求本身: 之后的一切都是客户的 载荷 ,
            // 并转发请求 字节 上游 堕落 数据流 (
            // TLS 失败, 且版本错误。
            c.in.erase(0, r.consumed);
        } else {
            const auto h = parse_http_request(c.in);
            if (h.target.need_more) return;
            if (!h.target.ok) {
                std::string reply = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
                bool open = true;
                conn_flush(c.client, reply, open);
                c.close_all();
                return;
            }
            host = h.target.host;
            port = h.target.port;
            c.http_connect = h.connect;
            // 绝对 -form 请求为转发标题和全部; CONNECT
            // 是个隧道,所以头就停在这里
            if (!h.connect) c.to_up = c.in;
            c.in.clear();
        }

        bool refused = false;
        // 电话 NAME 允许清单 首先,在客人要求的名字上:
        // 里面的地址过滤器 dial_filtered 仍然在后面,
        // 所以一个可以解决10.0.0.1的允许名称被完全拒绝.
        if (!host_allowed(host, allow_hosts)) {
            refused = true;
            c.up = -1;
            AGT_LOG(Tool, Warn, "sandbox.proxy.refused", "host={} port={} reason=not-allowlisted",
                    host, port);
        } else {
            c.up = dial_filtered(host, port, refused);
        }
        if (c.up < 0) {
            if (refused) refused_count.fetch_add(1, std::memory_order_relaxed);
            std::string reply;
            if (c.socks) {
                // 0x02 = 规则不允许,这是拒绝的诚实代码;
                // 0x05 = 连接拒绝了其他.
                const char rep[10] = {'\x05', refused ? '\x02' : '\x05', '\x00', '\x01',
                                      0, 0, 0, 0, 0, 0};
                reply.assign(rep, sizeof rep);
            } else {
                reply = refused
                    ? "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n"
                      "X-Agentty-Sandbox: host or address refused by the sandbox proxy\r\n\r\n"
                    : "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
            }
            bool open = true;
            conn_flush(c.client, reply, open);
            c.close_all();
            return;
        }
        set_nonblocking(c.up);

        std::string ack;
        if (c.socks) {
            const char ok[10] = {'\x05', '\x00', '\x00', '\x01', 0, 0, 0, 0, 0, 0};
            ack.assign(ok, sizeof ok);
        } else if (c.http_connect) {
            ack = "HTTP/1.1 200 Connection established\r\n\r\n";
        }
        if (!ack.empty()) {
            bool open = true;
            conn_flush(c.client, ack, open);
        }
        // 请求背后的客户端属于上游.
        c.to_up += c.in;
        c.in.clear();
        c.phase = Conn::Phase::relay;
    }
}

}  // 命名空间结束。

struct Proxy::Impl {
    explicit Impl(std::vector<std::string> hosts)
        : st(::maya::co_owned<ProxyState>::make(std::move(hosts))) {}
    ::maya::co_owned<ProxyState> st;
    ::agentty::util::WorkerGroup group{"sandbox.netproxy"};
};

Proxy::~Proxy() {
    // 组加入自己的破坏器,这是连接任务使用ProxyState
    // 安全的障碍. stop(cap)是明确的路径;
    // 这是给一个忘记的主人的后盾.
    if (impl_) {
        const int lfd = impl_->st->listen_fd.exchange(-1, std::memory_order_relaxed);
        if (lfd >= 0) {
            ::shutdown(lfd, SHUT_RDWR);
            ::close(lfd);
        }
    }
    delete impl_;
    impl_ = nullptr;
}

bool Proxy::start(int listen_fd, std::vector<std::string> allow_hosts) {
    if (listen_fd < 0) return false;
    if (impl_) { ::close(listen_fd); return false; }
    // 条形图案 一次 ,通过相同的正常设置 面板 使用:
    // 手 手 脚 -edited settings.json 条目( "https://GitHub.com/",
    // ".npmjs.org" 意思是说, 带类型的 输入 面板.
    //
    // FAIL CLOSED 列表中, 将“ 普通化” 变为“ 无效” :
    // 用户要求限制, 并且每个无效的条目都不得默默地将其扩展为“ 无效 ”。
    // "any host"。一个占位符名称无法匹配列表中不包含 -empty.
    std::vector<std::string> canon;
    for (const auto& raw : allow_hosts) {
        auto ne = normalize_host_entry(raw);
        if (!ne.value.empty()) canon.push_back(std::move(ne.value));
        else AGT_LOG(Tool, Warn, "sandbox.proxy.bad_host_entry", "entry={} why={}", raw, ne.why);
    }
    if (!allow_hosts.empty() && canon.empty()) canon.emplace_back("invalid.");
    impl_ = new Impl{std::move(canon)};
    // 听众来了 BLOCKING: claybin 设置创建它
    // 时没有 SOCK_NONBLOCK ,而下方的接受环将它排干,
    // 直到 EAGAIN。在一个第二接受4 的屏蔽的听器上,
    // 代理服务器在连接第一次后和以后每次请求时间都停止返回。
    // (正好如此) (Found exactly that way.) (Fond exist) (Fund existly that. ) (这才是你所看到的)
    set_nonblocking(listen_fd);
    impl_->st->listen_fd.store(listen_fd, std::memory_order_relaxed);
    impl_->st->live.store(true, std::memory_order_relaxed);

    // 一个 工作,以及 一个 内部的民意调查, 覆盖听众和每个直播连接。
    // 见 Conn 评论: 一个屏蔽 处理函数 每条连接
    // `npm install` 串连其后接物.
    impl_->group.post(
        [](const maya::unlocked&, std::stop_token stop, ::maya::co_owned<ProxyState> st) {
            std::vector<Conn> conns;
            std::vector<pollfd> fds;
            while (!stop.stop_requested()) {
                const int lfd = st->listen_fd.load(std::memory_order_relaxed);
                if (lfd < 0) break;

                fds.clear();
                fds.push_back(pollfd{lfd, POLLIN, 0});
                for (auto& c : conns) {
                    if (c.phase == Conn::Phase::done) continue;
                    short cev = 0;
                    // 趁我们还想读的时候从客户端读取 字
                    // 节 并且没有坐在积压的 上游.
                    if (c.client_open && c.to_up.size() < 256 * 1024) cev |= POLLIN;
                    fds.push_back(pollfd{c.client, cev, 0});
                    if (c.up >= 0) {
                        short uev = 0;
                        if (c.up_open) uev |= POLLIN;
                        if (!c.to_up.empty()) uev |= POLLOUT;
                        fds.push_back(pollfd{c.up, uev, 0});
                    }
                }

                const int r = ::poll(fds.data(), fds.size(), 300);
                if (r < 0) {
                    if (errno == EINTR) continue;
                    break;
                }

                // 聆听器先, 因此新连接不会等待到
                // 已有连接后面的勾选。
                if (fds[0].revents & (POLLERR | POLLNVAL)) break;
                if (fds[0].revents & POLLIN) {
                    for (;;) {
#if defined(__APPLE__)
                        const int c = ::accept(lfd, nullptr, nullptr);
                        if (c >= 0) {
                            sock_prep(c);
                            const int fl = ::fcntl(c, F_GETFL, 0);
                            if (fl >= 0) ::fcntl(c, F_SETFL, fl | O_NONBLOCK);
                        }
#else
                        const int c = ::accept4(lfd, nullptr, nullptr,
                                                SOCK_CLOEXEC | SOCK_NONBLOCK);
#endif
                        if (c < 0) break;   // EAGAIN: drained
                        st->served.fetch_add(1, std::memory_order_relaxed);
                        if (conns.size() >= 256) {   // 上限,不是政策
                            ::close(c);
                            continue;
                        }
                        Conn nc;
                        nc.client = c;
                        conns.push_back(std::move(nc));
                    }
                }

                // 然后是每个连接。 fd - > resrevents
                // 映射是重建每个勾选, 所以请检查每个勾选符,
                // 而不是在刚刚生长的矢量上记住一个索引。
                auto revents_of = [&](int fd) -> short {
                    for (const auto& p : fds)
                        if (p.fd == fd) return p.revents;
                    return 0;
                };
                for (auto& c : conns) {
                    if (c.phase == Conn::Phase::done) continue;
                    const short cr = revents_of(c.client);
                    const short ur = c.up >= 0 ? revents_of(c.up) : short{0};

                    if (cr & POLLIN) {
                        if (!conn_fill(c)) c.client_open = false;
                    }
                    if (c.phase != Conn::Phase::relay) {
                        conn_step(c, st->refused, st->allow_hosts);
                        if (c.phase == Conn::Phase::done) continue;
                        // 客户在完成请求前挂断了 没有
                        // 什么好说的了
                        if (!c.client_open && c.phase != Conn::Phase::relay) {
                            c.close_all();
                            continue;
                        }
                    }
                    if (c.phase != Conn::Phase::relay) continue;

                    // 中继. `in` 持有客户端 - >
                    // 上游 字节, `to_up` 尚未写入的;
                    // 反方向直接写入, 因为客户端是
                    // 要求数据的人。
                    if (!c.in.empty()) { c.to_up += c.in; c.in.clear(); }
                    if (ur & POLLOUT) conn_flush(c.up, c.to_up, c.up_open);
                    if (ur & POLLIN) {
                        char buf[16 * 1024];
                        for (;;) {
                            const ssize_t n = ::read(c.up, buf, sizeof buf);
                            if (n > 0) {
                                std::string out{buf, static_cast<std::size_t>(n)};
                                conn_flush(c.client, out, c.client_open);
                                if (!out.empty()) {
                                    // 客户端比上游慢.
                                    // 封锁这里是诚实的: 另一种
                                    // 选择是无限制的 缓冲区 ,
                                    // 而这个只有摊位 此 连接。
                                    pollfd w{c.client, POLLOUT, 0};
                                    if (::poll(&w, 1, 2000) == 1)
                                        conn_flush(c.client, out, c.client_open);
                                    else
                                        c.client_open = false;
                                }
                                continue;
                            }
                            if (n == 0) { c.up_open = false; break; }
                            if (errno == EINTR) continue;
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            c.up_open = false;
                            break;
                        }
                    }
                    if ((cr | ur) & (POLLERR | POLLNVAL)) {
                        c.close_all();
                        continue;
                    }
                    // 挂在两边,没有留下移动它。
                    const bool client_gone = !c.client_open || (cr & POLLHUP);
                    const bool up_gone = !c.up_open || (ur & POLLHUP);
                    if ((client_gone && c.to_up.empty()) || (up_gone && !c.client_open)
                        || (up_gone && client_gone))
                        c.close_all();
                    else if (client_gone && c.to_up.empty() && c.up >= 0)
                        ::shutdown(c.up, SHUT_WR);
                }

                std::erase_if(conns, [](const Conn& c) {
                    return c.phase == Conn::Phase::done;
                });
            }
            for (auto& c : conns) c.close_all();
            st->live.store(false, std::memory_order_relaxed);
        },
        impl_->st);
    return true;
}

void Proxy::stop(const maya::unlocked& cap) noexcept {
    if (!impl_) return;
    // 关闭听众 立刻唤醒接受投票 而不是等待
    // 300毫秒的滴答。
    const int lfd = impl_->st->listen_fd.exchange(-1, std::memory_order_relaxed);
    if (lfd >= 0) {
        ::shutdown(lfd, SHUT_RDWR);
        ::close(lfd);
    }
    impl_->group.stop(cap);
    impl_->st->live.store(false, std::memory_order_relaxed);
}

bool Proxy::running() const noexcept {
    return impl_ && impl_->st->live.load(std::memory_order_relaxed);
}
std::uint64_t Proxy::refused() const noexcept {
    return impl_ ? impl_->st->refused.load(std::memory_order_relaxed) : 0;
}
std::uint64_t Proxy::served() const noexcept {
    return impl_ ? impl_->st->served.load(std::memory_order_relaxed) : 0;
}

}  // namespace agentty::tools::util::sandbox::netproxy

#endif  // (__linux__ || __APPLE__) && !AGENTTY_NETPROXY_PARSERS_ONLY
