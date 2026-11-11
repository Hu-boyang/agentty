#pragma once
// agentty::tls-OpenSSL胶水被分解，因此http层不必
// 知道SSL_CTX设置。单个 SSL_CTX 为进程 生命周期 生存：
// 它保存已验证的根存储、会话缓存 (TLS 1.3会话票证)
// 和ALPN广告。每个新连接都会从中获取 SSL 对象。
//
// System-root验证遵循Zed的rustls-platform-verifier模型：
//   Linux —OpenSSL的默认路径(/etc/ssl/certs)，已连线。
//   macOS-经由Security.framework从SecTrustSettings提取信任根。
//   Windows-枚举CertOpenSystemStoreW("ROOT")，将每个转换为
//             X509，推入SSL_CTX存储中。

#include <memory>
#include <string>
#include <string_view>

// Forward-declare OpenSSL类型，
// 因此该头文件不会将openssl/*.h拖到每个仅需要句柄类型的编译单元中。
struct ssl_ctx_st;
struct ssl_st;

namespace agentty::tls {

using SSL_CTX = ssl_ctx_st;
using SSL     = ssl_st;

// 初始化OpenSSL而不读取openssl.cnf。我们在代码中配置所有内容，
// 解析 系统配置每次启动的时间约为 0.4 毫秒。 幂等；
// 调用它在第一个libcrypto/libssl之前使用。
void init_openssl() noexcept;

// 具有进程生命周期的不透明句柄。首先 调用 执行 one-time 初始化（
// OPENSSL_init_ssl，构建 SSL_CTX，加载 系统根目录，安装 ALPN）。
// 线程-safe;内部 锁 意味着其他 线程 上的后续调用者只会
// 看到已经-built 上下文。
[[nodiscard]] SSL_CTX* shared_context(bool insecure = false);

// 创建绑定到给定fd+SNI主机名的客户端SSL，绑定到共享上下文。
// 准备好让呼叫者驾驶 SSL_connect。 分配 失败时返回
// nullptr。
[[nodiscard]] SSL* wrap_client(int fd, std::string_view sni_host);

// 摧毁SSL（干净地释放nghttp2/openssl管道）。
// 底层 fd 由调用者拥有，必须单独关闭。
void free_ssl(SSL* ssl) noexcept;

// 渲染 将给定 SSL 的最后一个 OpenSSL 错误队列转换为 human
// -readable 字符串。排空队列。由 http 层用于错误传播。
[[nodiscard]] std::string last_error(SSL* ssl = nullptr);

} // namespace agentty::tls
