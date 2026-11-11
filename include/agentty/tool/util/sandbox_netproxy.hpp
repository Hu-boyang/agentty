// 沙箱的出站网络，已过滤——claybin的代理网络
// 命名空间的主机侧。
//
// 原因 此 EXISTS。
// 沙箱可以访问网络。
// 过去表示盒子共享 agentty 的网络命名空间，这不是边界：
// 代理运行的命令可以到达主机环回上的每个服务（开发服务器、数据库、
// 未经身份验证的管理端口、另一个代理的调试套接字）以及 LAN 上的每
// 台计算机（路由器的管理页面、NAS、同事的笔记本电脑）。 沙箱 审计
// 称这是一项重大发现，而且这是正确的：文件系统墙是坚固的，而网络却没有。
//
// Landlock可以在内核中过滤TCP连接，这比任何代理都强大——但只能过滤PORT。
// ":443 to the package registry" 和
// ":443 to the router" 是一条规则，因此端口规则无法表达重要的内容。
//
// 因此：claybin将访客放入其OWN网络命名空间中（
// 外部根本无法到达，由内核强制执行）并在访客的环回上
// 打开一个侦听器，将描述符发出。该文件接受该描述符并
// 使用整个工具链已知的两个代理协议 - HTTP
// CONNECT 和 SOCKS5 - 解析每个请求的
// 名称 此处 并拒绝作为实际漏洞的地址。
//
// 内容 IT REFUSES，以及为什么每一项：
//   127.0.0.0/8，::1主机自己的服务。的发现。
//   10/8、172.16/12、192.168/16、fc00::/7 LAN。的发现。
//   169.254/16，fe80::/10 链接-local，其包括云元数据
//                          (169.254.169.254分发IAM 凭据)。
//   100.64/10 CGNAT，这是 Tailscale 的范围——
//                        tailnet 是某人的专用网络的另一个名称。
//   0.0.0.0/8、224/4、240/4，多播不是目的地。
//
// 过滤器在实际拨打的地址上运行AFTER解析，
// 因此在地址上拒绝解析为127.0.0.1的名称，而不是在拼写上。
// 不存在重新绑定窗口：代理拨打其检查的地址。
//
// HONEST LIMITS。这是一堵策略墙，而不是内核墙：它
// 决定代理将拨打什么号码，因此它的优势在于该代码是正确的。
// KERNEL 所保证的是最重要的部分 - 如果没有来自此处的描述符，
// 则来宾无法从其自己的环回中路由出来，因此该文件中的错误
// 无法关闭（连接被拒绝）而不是打开。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <maya/runtime.hpp>   // maya::unlocked

namespace agentty::tools::util::sandbox::netproxy {

// 该地址是客人不得到达的地址吗？ `addr` 是呈现形式的解析
// 地址 (inet_ntop)，而不是名称。
//
// 纯粹并导出用于测试：上面列表中的每个条目都被固定。
[[nodiscard]] bool is_blocked_address(std::string_view addr) noexcept;

// 用户的允许清单上是否有`host`(如客人在解析之前
// 所要求的那样)？ 空 列表允许每个名称（上面的地址过滤
// 器仍然适用），因此范围只会缩小。
//
// 条目：`example.com`与该名称完全匹配； `*.example.com`
// 严格匹配其下的任何名称，但 不 匹配顶点（如果您指的是两者，请说两者：
// 常见的错误是允许 `*.github.com` 并想知道为什么
// github.com 本身失败，而相反的错误是在每个子域中默默地执行 授权)。
// Case-insensitive，尾随点被忽略。每当设置列表时，
// IP-literal 主机都会被拒绝：允许清单 名称不能保证地址，
// 并且允许文字将使每个名称规则都可绕过。
//
// 检查了 NAME，而不是地址：访客无法使用 DNS 来引导它，
// 因为代理解析了它所检查的名称。
[[nodiscard]] bool host_allowed(std::string_view host,
                                const std::vector<std::string>& allow) noexcept;

// 将一个允许清单条目规范化为用户带类型的，
// 或者说为什么它不是一个。设置编辑器和代理都会执行此操作，
// 因此 面板 接受的列表正是强制执行的列表。
//
// 原谅意图明确的地方——人们粘贴 URL：
//   "https://GitHub.com/foo" -> "github.com"
//   (方案、路径、大小写) "github.com:443" -> "github.com"
//   (端口：列表是名称) ".github.com" -> "*.github.com"
//   (curl/no_proxy 拼写)
// "github.com." -> "github.com" 不严格的地方：IP 文字、
// 裸 "*"、通配符除了最左边的标签 "*.com"（整个 TLD）或主机名不能包含的
// 字符之外的任何地方都是 REJECTED，其原因从未被猜测过。
struct HostEntry {
    std::string value;    // 规范形式；被拒绝时为空
    std::string_view why; // 拒绝原因（静态文本），接受时为空
};
[[nodiscard]] HostEntry normalize_host_entry(std::string_view raw);

// ── 解析器,作为纯函数 ──────────────────────────────────────
//
// 从套接字代码中分离出来，因为它们读取了 字节，而 GUEST 选择了：
// 他们是沙箱的主机侧中的唯一攻击者-controlled 解析器，
// 因此他们是模糊的（tests/fuzz/fuzz_netproxy.cpp)
// 而不是可信的。没有描述符，输出字符串之外没有 分配，没有 I/O.

struct Target {
    std::string host;        // 名称或文字，完全按照要求
    std::uint16_t port{0};
    bool ok{false};          // false = 格式错误，`need_more` 表示哪个
    bool need_more{false};   // 该请求为有效的PREFIX；阅读并 重试
};

// SOCKS5 问候语 (RFC 1928 §3): 05 NMETHODS METHODS...
// `ok`表示不提供-auth的完整问候语。
struct Socks5Greeting {
    bool ok{false};
    bool need_more{false};
    bool no_auth{false};
    std::size_t consumed{0};
};
[[nodiscard]] Socks5Greeting parse_socks5_greeting(std::string_view in) noexcept;

// SOCKS5请求(§4)：05 CMD 00 ATYP ADDR PORT。报告 `cmd`，
// 以便呼叫者可以使用正确的回复代码拒绝除 CONNECT 之外的任何内容。
// `consumed`是请求占用了多少字节：后面
// 的是客户端自己的数据，用它转发请求字节会破坏数据流
// （curl报TLS版本错误）。
struct Socks5Request {
    Target target;
    std::uint8_t cmd{0};
    std::uint8_t atyp{0};
    std::size_t consumed{0};
};
[[nodiscard]] Socks5Request parse_socks5_request(std::string_view in);

// HTTP请求行：CONNECT主机:端口，或绝对-form GET/POST/...
// `connect`区分这两者，因为一个得到200和隧道，
// 而另一个得到转发的头上游。
struct HttpRequest {
    Target target;
    bool connect{false};
};
[[nodiscard]] HttpRequest parse_http_request(std::string_view head);

// 正在运行的代理：拥有接受循环及其工作线程，在销毁时加入。
//
// 用交回的侦听器claybin(它需要所有权)构造它，
// 并且它服务连接直到停止。 `port` 是侦听器与 内部
// 来宾的命名空间绑定的位置，这是来宾的代理环境变量名称。
class Proxy {
  public:
    Proxy() = default;
    ~Proxy();
    Proxy(const Proxy&) = delete;
    Proxy& operator=(const Proxy&) = delete;

    // 取`listen_fd`中的所有权。当 工作线程 无法启动时返回 false
    // （并关闭它）。 `allow_hosts` 是名称 允许清单，
    // 固定为代理的 生命周期（请参阅 host_allowed）；空=任何公共主机。
    bool start(int listen_fd, std::vector<std::string> allow_hosts = {});

    // 停止接受并加入。 幂等。
    //
    // 采用 `unlocked`，因为加入可以等待连接作业需要的
    // 任何 锁：从 锁 正文 内部停止不能是 编译。
    // 析构函数不能要求一个，因此 OWNER 必须阻止它——这与
    // WorkerGroup 本身拥有的合同相同。
    void stop(const maya::unlocked& cap) noexcept;

    [[nodiscard]] bool running() const noexcept;

    // 连接被地址过滤器拒绝，用于审计跟踪。
    [[nodiscard]] std::uint64_t refused() const noexcept;
    [[nodiscard]] std::uint64_t served() const noexcept;

  private:
    struct Impl;
    Impl* impl_{nullptr};
};

}  // namespace agentty::tools::util::sandbox::netproxy
