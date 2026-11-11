// mcp_tools_bridge.cpp - 看见了 mcp_tools_bridge.hpp.
//
// 适应 mcp-cpp's make_provider ( 工具套入 agentty
// ToolDefs。提供方拥有每个 Tier-1 工具的操作;此文件拥有细的 re
// -wrap : 构建一个 ToolDef 每一个广告工具, 分派 execute() 输入提供者,
// 并读取 带类型的 副作用 财务报告和已审计财务报表
// FileChanges 返回到 agentty's ToolOutput.

#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/util/exec.hpp"   // 管理程序的方法之一
#include "agentty/tool/mcp_tools_backends.hpp"

#include "agentty/diff/diff.hpp"
#include "agentty/domain/sandbox_config.hpp"   // kAlways MaskedNames: 文件 拒绝读取工具
#include "agentty/io/http.hpp"
#include "agentty/rag/rag_adapter.hpp"   // rag::feedback::note_file_opened(学习循环)
#include "agentty/tool/registry.hpp"   // tools::progress::emit
#include "agentty/tool/web_search_plan.hpp"     // web_search:按先后顺序排列的哪些服务.
#include "agentty/tool/web_search_policy.hpp"   // web_search:设置 ~ 网络搜索
#include "agentty/runtime/app/update/stream_args.hpp"  // canonify_tool_args (`cmd`→`command`)
#include "agentty/tool/spec.hpp"       // 光谱 目录 — 副作用 权力
#include "agentty/tool/util/disk_files.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/util/home_dir.hpp"   // agentty workspace_root()  ⁇

#include <mcp/tools/toolset.hpp>
#include <mcp/tools/host.hpp>
#include <maya/runtime.hpp>

#include <cstdlib>   // std::getenv
#include <mcp/tools/util/fs_helpers.hpp>
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/util/handoff_gate.hpp"
#include <mcp/cap/capability.hpp>
#include <mcp/cap/local.hpp>
#include <mcp/codec.hpp>

#include <algorithm>
#include <array>
#include <cctype>    // std::isalnum - 内 -name 验证 沙箱 钩子
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace agentty::tools {

namespace {

namespace mt = ::mcp::tools;

// ── HttpClient 适配器 ─────────────────────────────────────────────────
//   mcp 的网络工具 调用 HttpClient::send(HttpRequest {
//   url,...} 并期望客户端处理 TLS + 重定向键 -following +
//   4xx/5xx 执行作为答复(不是 传输层 错误). agentty's http::Client
//   返回时 4xx/5xx 以 HttpError::Status 并且确实 不 自动 -follow
//   重定向, 因此这个适配器 解析 编号 URL ,手动跟踪 3xx,并将状态错误映射回
//   mcp HttpResponse 工具可以读取。

struct AgenttyHttpClient final : mt::HttpClient {
    static constexpr int kMaxRedirects = 5;

    // SSRF 适用于 每个 跳( 初始状态) URL 和 每个方向目标).
    // mcp-cpp 网络 shell 仅验证第一个 URL 因为它跟随3xx本身,
    // 一个公众 URL 改为169.254.169.254(云形元数据)、
    // 127.0.0.1或a RFC1918 原主机会直航,Mcp的镜像
    // is_blocked_host: 归并 众多合法的IPv4拼写法(
    // decimal/hex/octal/short-form 至点形四边形前区域
    // -checking 附加时 loopback/link-local/ULA IPv6 (英语).
    static bool host_blocked(std::string_view host) {
        if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        std::string h{host};
        for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (h.empty()) return true;
        if (h == "localhost" || h.ends_with(".localhost")) return true;
        if (h == "metadata" || h == "metadata.google.internal") return true;
        if (h == "0") return true;
        if (h == "::1" || h == "::") return true;
        if (h.starts_with("fc") || h.starts_with("fd")) return true;   // ULA
        if (h.starts_with("fe8") || h.starts_with("fe9")
            || h.starts_with("fea") || h.starts_with("feb")) return true; // 链接-local

        // IPv4 调用 -mapped / -compatible IPv6(:
        // ffff:127.0.0.1, ^ff:a9fe:a9 fe,
        // *127.0.0.1) 解析嵌入式IPv4,是经典的. SSRF 过滤绕行:
        // 上面的前缀检查错过了它们和点数 -quad 解析器 不信真主,
        // 他们有 ':' 如果一个IPv6字段带有一个后继IPv4(被点,
        // 或一个 ffff:映射的后两个六重子组), 归并 由它来点定表, 让 IPv4
        // 范围检查下面决定。 一个真正的 IPv6 (2606: 4700: : 1111)
        // 还没有这样的嵌入式 v4 , 并且要通过。 我们不覆盖 -block v6.
        if (h.find(':') != std::string::npos
            && (h.starts_with("::ffff:") || h.starts_with("::"))) {
            auto last_colon = h.rfind(':');
            std::string tail = h.substr(last_colon + 1);
            if (tail.find('.') != std::string::npos) {
                h = tail;   // ffff:127.0.0.1 − "127.0.0.1"
            } else {
                // ffff:7f00:0001——最后两个六重奏组编码v4.
                auto prev_colon = (last_colon == 0) ? std::string::npos
                                                    : h.rfind(':', last_colon - 1);
                std::string g_hi = (prev_colon != std::string::npos)
                                       ? h.substr(prev_colon + 1, last_colon - prev_colon - 1)
                                       : std::string{};
                auto hex4 = [](const std::string& g, unsigned& v) -> bool {
                    if (g.empty() || g.size() > 4) return false;
                    v = 0;
                    for (char ch : g) {
                        unsigned d;
                        if (ch >= '0' && ch <= '9') d = static_cast<unsigned>(ch - '0');
                        else if (ch >= 'a' && ch <= 'f') d = static_cast<unsigned>(ch - 'a' + 10);
                        else return false;
                        v = v * 16 + d;
                    }
                    return true;
                };
                unsigned hi, lo;
                if (h.starts_with("::ffff:") && !g_hi.empty()
                    && hex4(g_hi, hi) && hex4(tail, lo)) {
                    h = std::to_string((hi >> 8) & 0xff) + "." + std::to_string(hi & 0xff)
                      + "." + std::to_string((lo >> 8) & 0xff) + "." + std::to_string(lo & 0xff);
                }
            }
        }

        // 归并 IPv4拼音inet_aton/getaddrinfo 接受为1u32.
        auto parse_part = [](std::string_view p, unsigned long& v) -> bool {
            if (p.empty()) return false;
            int base = 10;
            if (p.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                base = 16; p.remove_prefix(2);
                if (p.empty()) return false;
            } else if (p.size() >= 2 && p[0] == '0') { base = 8; p.remove_prefix(1); }
            v = 0;
            for (char ch : p) {
                unsigned dig;
                if (ch >= '0' && ch <= '9') dig = static_cast<unsigned>(ch - '0');
                else if (base == 16 && ch >= 'a' && ch <= 'f') dig = static_cast<unsigned>(ch - 'a' + 10);
                else return false;
                if (dig >= static_cast<unsigned>(base)) return false;
                v = v * static_cast<unsigned long>(base) + dig;
                if (v > 0xffffffffUL) return false;
            }
            return true;
        };
        std::vector<unsigned long> parts;
        std::size_t start = 0;
        bool numeric = true;
        for (std::size_t i = 0; i <= h.size() && numeric; ++i) {
            if (i == h.size() || h[i] == '.') {
                unsigned long v;
                if (!parse_part(std::string_view{h}.substr(start, i - start), v)) { numeric = false; break; }
                parts.push_back(v);
                start = i + 1;
            }
        }
        if (numeric && !parts.empty() && parts.size() <= 4) {
            std::uint32_t ip = 0;
            bool ok = true;
            switch (parts.size()) {
                case 1: ip = static_cast<std::uint32_t>(parts[0]); break;
                case 2: if (parts[0] > 0xff || parts[1] > 0xffffff) { ok = false; break; }
                        ip = static_cast<std::uint32_t>((parts[0] << 24) | parts[1]); break;
                case 3: if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xffff) { ok = false; break; }
                        ip = static_cast<std::uint32_t>((parts[0] << 24) | (parts[1] << 16) | parts[2]); break;
                case 4: if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xff || parts[3] > 0xff) { ok = false; break; }
                        ip = static_cast<std::uint32_t>((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]); break;
            }
            if (ok) {
                unsigned a = (ip >> 24) & 0xff, b = (ip >> 16) & 0xff;
                if (a == 127 || a == 0 || a == 10) return true;
                if (a == 169 && b == 254) return true;
                if (a == 172 && b >= 16 && b <= 31) return true;
                if (a == 192 && b == 168) return true;
                if (a == 100 && b >= 64 && b <= 127) return true;
                if (a >= 224) return true;
                return false;   // 数字但公开
            }
        }
        return false;
    }

    struct Parsed { std::string host; uint16_t port = 443; std::string path = "/"; bool ok = false; };

    static Parsed parse(std::string_view url) {
        Parsed out;
        constexpr std::string_view k = "https://";
        if (!url.starts_with(k)) return out;
        url.remove_prefix(k.size());
        auto slash = url.find('/');
        auto authority = url.substr(0, slash);
        out.path = (slash == std::string_view::npos) ? "/" : std::string{url.substr(slash)};
        if (auto colon = authority.find(':'); colon != std::string_view::npos) {
            out.host.assign(authority.substr(0, colon));
            try {
                int port_int = std::stoi(std::string{authority.substr(colon + 1)});
                if (port_int <= 0 || port_int > 65535) return out;   // 坏的端口 —— 不好 ——
                out.port = static_cast<uint16_t>(port_int);
            }
            catch (...) { return out; }
        } else {
            out.host.assign(authority);
        }
        out.ok = !out.host.empty();
        return out;
    }

    static std::string resolve_redirect(const Parsed& base, std::string_view loc) {
        while (!loc.empty() && (loc.front() == ' ' || loc.front() == '\t')) loc.remove_prefix(1);
        while (!loc.empty() && (loc.back() == ' ' || loc.back() == '\t')) loc.remove_suffix(1);
        if (loc.empty()) return {};
        if (loc.starts_with("https://")) return std::string{loc};
        if (loc.starts_with("http://")) return {};   // TLS-only
        if (loc.starts_with("//")) return "https:" + std::string{loc};
        std::string out = "https://" + base.host;
        if (base.port != 443) out += ":" + std::to_string(base.port);
        if (loc.starts_with("/")) { out += std::string{loc}; return out; }
        auto last = base.path.rfind('/');
        out += (last == std::string::npos) ? std::string{"/"} : base.path.substr(0, last + 1);
        out += std::string{loc};
        return out;
    }

    mt::HttpResponse send(const mt::HttpRequest& in) override {
        mt::HttpResponse out;
        std::string url = in.url;
        std::vector<std::string> visited;

        for (int hop = 0; hop <= kMaxRedirects; ++hop) {
            auto p = parse(url);
            if (!p.ok) { out.status = 0; out.error = "could not parse url: " + url; return out; }
            if (host_blocked(p.host)) {
                out.status = 0;
                out.error  = "blocked host (SSRF guard): " + p.host;
                return out;
            }

            http::Request req;
            req.method = (in.method == "POST") ? http::HttpMethod::Post
                       : (in.method == "HEAD") ? http::HttpMethod::Head
                                               : http::HttpMethod::Get;
            req.host = p.host;
            req.port = p.port;
            req.path = p.path;
            req.body = in.body;
            // 防御深度: host_blocked (b) 上文拒绝明显的内部
            // NAMES/literals 快速, 但一个公共主机名可以 DNS-rebind
            // 改为内部 IP 名字检查看不到。 ssrf_guard 内容 -checks
            // 编号 RESOLVED 拨号时,拒绝 loopback/private/
            // link-local/metadata 目标——权威后援.
            req.ssrf_guard = true;
            for (const auto& [k, v] : in.headers) req.headers.push_back({k, v});

            http::Timeouts tos{
                .connect = std::chrono::milliseconds(10'000),
                .total   = std::chrono::milliseconds(30'000),
            };
            auto r = http::default_client(::agentty::IoAccess::grant()).send(req, tos);
            if (!r) {
                // A 4xx/5xx 抵达时是 HttpError::Status -
                // 表面是真实的 HttpResponse 这样网络工具就可以报告代码.
                if (r.error().kind == http::HttpErrorKind::Status
                    && r.error().http_status > 0) {
                    out.status = r.error().http_status;
                    return out;
                }
                out.status = 0;
                out.error  = r.error().render();
                return out;
            }

            // 手动重定向 agentty 客户端不自动 -follow).
            if (r->status >= 300 && r->status < 400 && in.method != "HEAD") {
                std::string loc;
                for (const auto& h : r->headers)
                    if (h.name == "location") { loc = h.value; break; }
                if (!loc.empty()) {
                    visited.push_back(url);
                    auto nxt = resolve_redirect(p, loc);
                    if (nxt.empty()
                        || std::find(visited.begin(), visited.end(), nxt) != visited.end()) {
                        out.status = r->status;   // 给工具我们拥有的
                        out.body   = std::move(r->body);
                        for (const auto& h : r->headers) out.headers.push_back({h.name, h.value});
                        return out;
                    }
                    url = std::move(nxt);
                    continue;
                }
            }

            out.status = r->status;
            out.body   = std::move(r->body);
            for (const auto& h : r->headers) out.headers.push_back({h.name, h.value});
            return out;
        }
        out.status = 0;
        out.error  = "too many redirects";
        return out;
    }
};

// ── 结果 − ExecResult 解码 ─────────────────────────────────────────
//   将 mcp cap::Result 地图回映到 agentty 的 ExecResult 中。
//   在错误中将文本包裹在 ToolError 中; 在成功时, 将文本 + (解码) FileChange 中。
//   医生的 FileChange 没有 差异块 (主机重算),所以请输入 before/after
//   结束 diff::compute 重建整个 FileChange diff (英语) -review
//   UI 消费。

ToolError decode_tool_error(std::string text) {
    using K = ErrorKind;
    static constexpr std::array kinds = {
        std::pair{"invalid args", K::InvalidArgs},
        std::pair{"not found", K::NotFound},
        std::pair{"not a file", K::NotAFile},
        std::pair{"not a directory", K::NotADirectory},
        std::pair{"too large", K::TooLarge},
        std::pair{"binary", K::Binary},
        std::pair{"ambiguous", K::Ambiguous},
        std::pair{"no match", K::NoMatch},
        std::pair{"invalid regex", K::InvalidRegex},
        std::pair{"network", K::Network},
        std::pair{"spawn failed", K::Spawn},
        std::pair{"subprocess failed", K::Subprocess},
        std::pair{"io", K::Io},
        std::pair{"out of workspace", K::OutOfWorkspace},
        std::pair{"denied", K::Denied},
        std::pair{"unknown", K::Unknown},
    };
    for (const auto& [label, kind] : kinds) {
        const std::string prefix = "[" + std::string{label} + "] ";
        if (text.starts_with(prefix))
            return ToolError{kind, text.substr(prefix.size())};
    }
    return ToolError::unknown(std::move(text));
}

ExecResult decode_result(const std::string& tool_name, ::mcp::cap::Result r) {
    if (r.is_error)
        return std::unexpected(decode_tool_error(std::move(r.text)));

    ToolOutput out;
    out.text = std::move(r.text);

    // 脱 mcp-cpp ' 输出 -budget 到达前的切换标记 UI - 但是,
    // 我说, 仅 用于文件 -mutating 工具,既有噪音,又安全。 分派器 (
    // apply_output_budget 附加一条线
    //   "[... N chars elided — output exceeded tool's budget; refine your request to see more ...]"
    //    约束 传输 载荷 用于
    // edit/write/apply_patch/replace 文字是人类
    // 的 -readable diff/confirmation 和 编号
    // COMPLETE 改变是结构上的( r.changes 下图,
    // before/after ),因此标记对于模型是多余的,在审查卡中也积极
    // 错误(它曾经是 渲染 灰色 "refine your request"。
    // 用于 read/grep/bash 标记没有被修改 – 模型真的需要
    // 知道输出被剪辑了, 以便它能够 page/refine.
    {
        const bool mutating =
            tool_name == "edit" || tool_name == "write" ||
            tool_name == "apply_patch" || tool_name == "replace";
        if (mutating) {
            if (auto m = out.text.rfind("[... "); m != std::string::npos) {
                const auto tail = std::string_view{out.text}.substr(m);
                if (tail.find("chars elided") != std::string_view::npos &&
                    tail.find("...]") != std::string_view::npos) {
                    while (m > 0 &&
                           (out.text[m - 1] == '\n' || out.text[m - 1] == ' '))
                        --m;
                    out.text.resize(m);
                }
            }
        }
    }

    // 工具更改的每个文件, 与 path/before/after ; 重建结构化
    // 差异块 diff (英语) -review 需要帮助。 out.change 任何单曲都
    // 保留第一个 -file 消费者。
    for (const auto& ch : r.changes) {
        FileChange fc = diff::compute(ch.path, ch.before, ch.after);
        fc.original_contents = ch.before;
        fc.new_contents      = ch.after;
        out.changes.push_back(std::move(fc));
    }
    if (!out.changes.empty()) out.change = out.changes.front();

    // 图像工具浮出水面( 在图像文件中读取), 原始 字节。
    // 它们成为图像块 调用's tool_result 用于视觉模型;
    // 非 -vision 模型只是看到 [图像...] 音符在 out.text ,
    // 并显示 传输 层会把它们丢弃给无法看到模型。
    for (auto& im : r.images)
        out.images.push_back(ImageContent{std::move(im.media_type), std::move(im.bytes)});

    (void)tool_name;
    return out;
}

// mcp-cpp的副作用在agentty的术语中设定了.
// 拼写出每个标记,所以在两侧添加的标记就是这里的编译错误,而不是沉默的
// 位错配.
[[nodiscard]] EffectSet from_mcp(::mcp::cap::EffectSet m) noexcept {
    using ME = ::mcp::cap::Effect;
    EffectSet e;
    if (m.has(ME::ReadFs))  e |= EffectSet{Effect::ReadFs};
    if (m.has(ME::WriteFs)) e |= EffectSet{Effect::WriteFs};
    if (m.has(ME::Net))     e |= EffectSet{Effect::Net};
    if (m.has(ME::Exec))    e |= EffectSet{Effect::Exec};
    return e;
}

// 电话 mcp-cpp 工具( 工具) : 内存 介于 调用 (
// 读) 缓存 打开 快照 ,背景过程, 仓库 图表),列在一个下
// maya::guarded。该词 工作区 根和读根为 agentty '
// '(主集一个,技能扫描仪加另一个);每个访问中都复制它们,所以有一个真理的来源,
// 没有东西可以镜像.
//
// 工具让我们在状态上运行一个功能。它的形状是 库's (a
// std::function<void(ToolState&)> 可能捕捉到任何东西,
// 所以它会穿过 guarded::with_foreign ,一个 jaal 运行
// 借入的条目 回调 a 项下 锁。声音因为 mcp-cpp 持有否 锁 开始( 其
// 货币) 允许清单 和每个 with() 正文 里面只有读写 ToolState (
// process.cpp, repomap.cpp, fs.cpp,
// call_state.hpp ),一个被堵塞的会绊倒 held_top () 在等待。 (笑声)
//
// 锁 顺序: 状态是 LEAF. StateAccess::with 谓
// mcp-cpp 虚拟, 从工具内调用 正文 签字后它就修复了 所以它不能
// 被赋予能力 -states 单个被审查的根用于 mcp-cpp '状态
// 回调 (tests/lint/allowlist.txt。
// 声音是因为唯一的呼叫者是 mcp-cpp 工具代码, 其不为 锁 我们
// 的:每 agentty 锁 正文 是一个无法捕捉到的功能, 超越它自身的值,
// 无法到达工具 正文.
//
// 里面没有东西 ToolState 可以阻断或 锁 销毁时在此下 锁 : 进程
// 条目是 shared_ptr<const Proc> ,而最后一个仅引用会话
// 的引用 Session::stop (原子和事件fd写作;见 JaalSession
// 输入 tool/util/exec.cpp。所以删除一个条目 with()
// 即使它放弃了会话, 也还是安全的。
using StateFn = std::function<void(mt::ToolState&)>;

class GuardedToolState final : public mt::StateAccess {
  public:
    explicit GuardedToolState(std::filesystem::path home) {
        maya::thread_root([&](const maya::unlocked& cap) {
            st_.with(cap, [](mt::ToolState& s, std::filesystem::path h) {
                s.home = std::move(h);
                // agentty本身的拓宽边界的方法,为拒绝文本.
                s.widen_hint = "Restart agentty in a parent directory or pass "
                               "--workspace <dir> to widen the scope.";
                // 本地读取工具拒绝什么 沙箱 命令的口罩(.env、密钥、
                // 符牌),如 Zed 的 private_files.
                for (const char* n : sandbox_cfg::kAlwaysMaskedNames)
                    s.private_names.emplace_back(n);
            }, std::move(home));
        });
    }
    void with(const StateFn& f) override {
        auto ws    = util::workspace_root();
        auto roots = util::read_roots_now();
        // 一种方式:范围领域和 正文 解剖学上见彼此.
        auto body = [&](mt::ToolState& s) {
            s.workspace_root = std::move(ws);
            s.read_roots     = std::move(roots);
            f(s);
        };
        maya::thread_root([&](const maya::unlocked& cap) { st_.with_foreign(cap, body); });
    }
  private:
    maya::guarded<mt::ToolState> st_;
};

// 进程 -lifetime 保留 -alive 提供方: ToolDef::execute
// 关闭 抓取 a shared_ptr 把它停在这里,
// 所以它 HostServices 适配器(包括 HttpClient 超过每一次关闭。
struct ProviderKeepAlive {
    std::shared_ptr<::mcp::cap::CapabilityProvider> provider;
    std::shared_ptr<mt::HttpClient>                 http;
    std::shared_ptr<mt::Exec>                       exec;
    std::shared_ptr<mt::StateAccess>                state;
};
ProviderKeepAlive& keep_alive() { static ProviderKeepAlive k; return k; }

} // 命名空间结束。

std::vector<ToolDef> build_mcp_tool_defs() {
    auto& ka = keep_alive();
    ka.http = std::make_shared<AgenttyHttpClient>();
    // 运行另一个程序是HOST供应能力,
    // 这与上面的HttpClient完全一样:mcp-cpp在jaal进程
    // +反应堆中陈述了它想要的东西,我们做了它。 在一个地方,
    // 一个执行是两个旧的民意测验循环所不同意的闲置的-vs-wall政策。
    ka.exec = util::make_exec();
    // 平行扫描也 : mcp-cpp 拥有无 线程 ,所以它的grep /结构
    ka.state = std::make_shared<GuardedToolState>(::agentty::util::home_dir_or_empty());

    mt::HostServices svc;
    svc.http = ka.http;
    // 哪些服务 web_search 尝试是用户的选择( 设置 )。
    // 传输 工具); 源代码为 调用 ,所以编辑到达下一个搜索时,没有重新启动,
    // 也没有重新 -registration.
    svc.web_search_plan = tools::web_search_plan::make_plan_source();
    svc.exec = ka.exec;
    // 每个文件工具的触摸 : mcp-cpp 没有 IO 自己,主人会的。
    svc.files = std::make_shared<util::DiskFiles>();
    // 搜索/ 检查 仓库 地图风扇在外 maya::scope.
    svc.split = util::make_splitter();
    svc.state    = ka.state;
    // 输入主机 -coupled 后端( 3) memory/skill/retriever/subagent).
    // 拖放待办事宜将保持无效。 其 shell 渲染 文本与主机状态相同。
    install_host_backends(svc);

    mt::ToolsetConfig cfg;   // 默认情况下所有第一级家庭
    // agentty 自己的方向盘,是 库 描述每个工具的用途; 哪个工具
    // agentty 想要一个模型 找到的是 agentty's 调用.
    cfg.description_notes = {
        {"shell", "Use for builds, tests, git, etc. Do NOT use for file IO — use "
                  "the write/edit/read tools instead (no cat/echo/sed/heredoc "
                  "to create or modify files)."},
    };
    // AGENTTY_NO_TRANSFORMS =1 个 transform/aggregate/structured-data
    // 家庭( extract/aggregate/replace/read_filter/json_query)
    // — ~5 KB 备注 模式定义 取消每一项要求——最低或暂时性要求 -sensitive 剖析器,
    // 无需 输 grep/read/edit. 镜像 AGENTTY_NO_HOOKS 公约
    if (const char* v = std::getenv("AGENTTY_NO_TRANSFORMS");
        v && v[0] && v[0] != '0' && v[0] != 'f' && v[0] != 'F' && v[0] != 'n' && v[0] != 'N')
        cfg.transforms = false;
    // AGENTTY_NO_JINA =1个站点 web_fetch 内容 -rendering SPA 弹壳通过 r.jina.ai.
    if (const char* v = std::getenv("AGENTTY_NO_JINA"); v && v[0] && v[0] != '0')
        cfg.jina = false;
    auto provider = mt::make_provider(svc, cfg, "local");
    ka.provider = provider;

    std::vector<ToolDef> defs;
    for (const auto& spec : provider->list()) {
        ToolDef def;
        def.name        = ToolName{spec.name};
        def.description  = spec.description.has_value() ? *spec.description : std::string{};
        def.input_schema = ::mcp::to_json(spec.inputSchema);
        def.origin = ToolOrigin::Native;
        // 副作用: agentty 光谱 目录 是那个 AUTHORITY 用于建造
        // -in 工具—— 权限 政策为: ToolDef::effects ,
        // 平行调度器读取 spec::lookup()->副作用 ,并显示 目录'
        // s 编译-time 证明( E) only_known_exec_tools,
        // no_writefs_and_exec_ 组合 readonly_invariants
        // ) 被指为反对。 mcp-cpp's effects_for_builtin
        // 表格漂移( 上下) remember/forget/wipe 曾经
        // "pure" 那儿 - 没有 提示词 尽管写了 memory.jsonl
        // ——而且任务缺乏Exec,尽管有副代理能够运行狂轰滥炸,它把门从排程器中分割开来。
        // 目录 ; Mcp 表格只包含工具 目录 不知所终.
        if (const auto* sp = tools::spec::lookup(spec.name)) {
            def.effects                = sp->effects;
            def.scheduling_effects     = tools::spec::sched_effects(*sp);
            def.eager_input_streaming  = sp->eager_input_streaming;
            def.max_output_chars       = sp->max_output_chars;
            def.timeout                = std::chrono::duration_cast<std::chrono::milliseconds>(sp->max_seconds);
            using NativeTrunc = tools::spec::ToolSpec::TruncStrategy;
            def.output_truncation = sp->trunc_strategy == NativeTrunc::Head
                ? OutputTruncation::Head
                : sp->trunc_strategy == NativeTrunc::Tail
                    ? OutputTruncation::Tail
                    : OutputTruncation::HeadTail;
        } else if (const auto fx = mt::effects_for_builtin(spec.name)) {
            def.effects = from_mcp(*fx);
            def.scheduling_effects = def.effects;
        } else {
            // 不是一个建的-in:像排程器一样对待写作者.
            def.effects = EffectSet{Effect::WriteFs};
            def.scheduling_effects = def.effects;
        }

        std::string tool_name = spec.name;
        def.execute = [provider, tool_name](const maya::unlocked& cap,
                                            const nlohmann::json& args_in,
                                            const CallContext& ctx) -> ExecResult {
            // 标记别名密钥 此处 ,在 分派 边界。
            //
            // PR 66号被封禁 数据流-parse 站点,用于修复已报告
            // 的错误(a) `cmd`-shaped 调用 未达到要求
            // -field 依星格的来历,而留下财产。任何不能经过的路,
            // 解析器 - 重播的游戏 线程 ,工具 调用 由该定义注入
            // ACP 客户端, 一个未来 传输层 - 到达这个羊肉田
            // `cmd` 设置和 `command` 缺席,
            // 以及 mcp-cpp's ArgReader 已经 NO 化
            // 名表(它是 str( key)/) require_str (键,
            // 无更),所以执行者读取一个空命令并运行.
            //
            // 单点做,每条径道漏斗 不变量
            // 执行器始终能够看到 command 字段。
            // 建筑而不是记忆中的 调用 N 调用 网站。
            // 解析-site 调用 停留: 权限 纸牌、
            // 活输出面板和外壳 -detour 检查全部
            // 已读 tc.args["command"] 之前
            // 分派 需要警犬钥匙,这是支援,不是替补
            nlohmann::json args = args_in;
            (void)::agentty::app::detail::canonify_tool_args(tool_name, args);

            // 设置 ~ 网络搜索。 Name 传输 接口边界 已经保存
            // web_search 取消请求, 取消请求;
            // 这是每个请求的后台 调用 而不是从一个——重播 线程 ,
            // 工具 调用 a 数据为: ACP 客户端注入,
            // 一种会话中较早时记得工具的模型。 调用 ,所以一个 面板 编辑到达
            // 下一个搜索时没有 目录 重建,我们把整个工程都重建了
            if (tool_name == "web_search") {
                const auto policy = tools::web_search_policy::current();
                if (web_search_cfg::mode(policy) == web_search_cfg::Mode::Off)
                    return std::unexpected(ToolError::denied(
                        "web_search is turned off (Settings \xe2\x86\x92 Web "
                        "Search). Answer from what you already know, or ask "
                        "the user to turn it back on."));
                args = web_search_cfg::rewrite_args(policy, std::move(args));
            }

            // LEARNING LOOP (双侧):代理人 ACTING 不久后,
            // 文件上就写着 search_docs 从中浮出一段是隐含的
            // 关联信号——这一段指向值得采取行动的某个地方。 `read`
            // 是基线信号; `edit`/`write` 已经 STRONGER
            // (一个确认的突变, 不只是一看),所以信用这些路径。
            // -microsecond 无 -op 那时,最近没有露出什么。
            if (tool_name == "read" || tool_name == "edit" ||
                tool_name == "write") {
                if (auto it = args.find("path"); it != args.end() && it->is_string())
                    rag::feedback::note_file_opened(cap, it->get_ref<const std::string&>());
                else if (auto fp = args.find("file_path");
                         fp != args.end() && fp->is_string())
                    rag::feedback::note_file_opened(cap, fp->get_ref<const std::string&>());
            }
            // TRUST HANDOFF GATE。在 字节 土地,不是后。
            //
            // 这是执行的一半 沙箱 面板's
            // "Agent writes host-executed files" 排队,
            // 它一直坚持一项不读的政策。它坐在这里而不是坐在里面。
            // 沙箱 因为 沙箱 错误的高度: 写入
            // .vscode/tasks.json 是一个普通的写到一个
            // 路径的文字, 代理者被允许写, 越狱发生在以后, 外面,
            // 当 VS 密码运行,没有挂载的命名空间看到
            //
            // 所有四个编写工具,而不仅仅是`write`:`edit`
            // 可以在现有的.git/config中添加凭据的帮助器,
            // `apply_patch`可以在diff中完成,`move`
            // 可以将一个惰性文件行走到一个可信任的名称中——这是Cursor 3.0.0
            // bug(一个.git目录在另一个名称下)倒置. Gating只留下
            // `write`会打开三个门并读作覆盖.
            if (tool_name == "write" || tool_name == "edit" ||
                tool_name == "apply_patch" || tool_name == "move") {
                // `move` 命名目标 `destination` ;
                // 其余使用时 `path` 或 或 时 间 `file_path` 中
                // 检查 DESTINATION 是一个移动点-- 源是惰性就是攻击。
                for (const char* key : {"destination", "path", "file_path"}) {
                    auto it = args.find(key);
                    if (it == args.end() || !it->is_string()) continue;
                    auto v = tools::util::handoff::check(
                        it->get_ref<const std::string&>(), tool_name);
                    if (!v.allowed)
                        return std::unexpected(
                            ToolError::denied(std::move(v.reason)));
                    break;   // 每调用有一个路径;第一个键位获得胜利
                }
            }

            // SHELL-PATH DETECTION。上面的大门需要一个路径参数,
            // `shell` 没有--它把不透明的绳子交出来,这就是如何
            // `cat > .vscode/tasks.json` 穿过了一条
            // 正确知道这条路很危险的政策。
            //
            // 解析 命令被拒绝(语法武器) 竞态 ;
            // 见 docs/design/shell-write-gate.md
            // 和长音符在 handoff_gate.hpp。所以,相反:观察
            // FILESYSTEM. 快照 信任的那个人 -shape 路径,运行 调用
            // 比较一下,没有引用的诡计可以逃避它,因为它从来不看命令。
            //
            // 侦测,而不是预防:在我们能够比较的时候,
            // 字节 拒绝之后将是戏剧 - 诚实的结果是响亮的,
            // 可归结于 事件 上,在饲料 沙箱 面板 已经 渲染。
            // 读数 -only 绑定是预防的一半,
            // 需要挂载命名空间,所以它不能成为硬化主机的唯一答案
            // claybin 已经存在以服务。
            const bool watchable =
                tool_name == "shell" || tool_name == "process_start" ||
                tool_name == "test" || tool_name == "diagnostics";
            tools::util::handoff::TrustedSnapshot before;
            if (watchable)
                before = tools::util::handoff::snapshot_trusted(
                    ctx.workspace.empty() ? tools::util::workspace_root().string()
                                          : ctx.workspace);

            // 电话 调用 上下文的顺序 : mcp-cpp 手执 `host` 返回到
            // agentty '是执行 / SkillResolver /
            // SubagentRunner 还没被察觉 快照 随它去,所以 沙箱 不会再走动了
            const HostCall host{&ctx, watchable && !before.empty() ? &before : nullptr};
            ::mcp::cap::Request req{tool_name, args};
            req.progress = ctx.progress;
            req.reader   = ctx.reader;
            req.host     = &host;
            req.scope_workspace   = ctx.workspace;
            req.scope_extra_roots = ctx.extra_roots;
            req.scope_project     = ctx.project;
            const auto merged = ctx.merged_stop();
            req.stop      = merged->get_token();
            req.cancelled = [&ctx] { return ctx.cancelled(); };
            // Write 车道由 分派器 ,围绕整个 调用 (tool.hpp,
            // write_lanes.hpp ),不是这里。
            auto r = provider->execute(req);

            if (watchable && !before.empty()) {
                // 命令的名称,而不是工具:"shell"对一个
                // 用户没有说明发生了什么。这是不透明的字符串能
                // 保持它的地方 — 事后解释,永远不要决定。
                std::string who = tool_name;
                if (auto c = args.find("command");
                    c != args.end() && c->is_string())
                    who = c->get_ref<const std::string&>();
                (void)tools::util::handoff::review_trusted(before, who);
            }

            return decode_result(tool_name, std::move(r));
        };
        defs.push_back(std::move(def));
    }

    // 重排为 agentty 记得 -bias 布局:直接工作工具先行(
    // 编辑在写作前——最便宜的反整块) -file 重写,
    // 主机 -coupled 工具( 工具) memory/task/skill/search_docs
    // ) 最后,所以模型的强先 -listed 偏差保持在边
    // read/edit/bash. 提供者列表主机 -coupled
    // 贝壳先(登记顺序),所以没有这个操作工具将沉入底部 传输 载荷.
    static const std::vector<std::string_view> kOrder = {
        "read", "edit", "apply_patch", "write", "move", "remove", "shell",
        "process_start", "process_poll", "process_stop",
        "grep", "glob", "list_dir",
        "search_structural",
        "extract", "aggregate", "read_filter", "replace",
        "json_query",
        "outline", "repo_map",
        "todo", "web_fetch", "web_search", "find_definition",
        "diagnostics", "test",
        "git_status", "git_diff", "git_log", "git_show", "git_blame", "git_commit",
        "git_branch", "git_stash", "git_rebase", "git_cherry_pick",
        "remember", "forget", "wipe_memory", "task", "skill", "search_docs",
        "search_code",
    };
    auto rank = [](std::string_view n) -> std::size_t {
        for (std::size_t i = 0; i < kOrder.size(); ++i)
            if (kOrder[i] == n) return i;
        return kOrder.size();   // 未知 = 已知的一组之后,稳定
    };
    std::stable_sort(defs.begin(), defs.end(),
        [&](const ToolDef& a, const ToolDef& b) {
            return rank(a.name.value) < rank(b.name.value);
        });
    return defs;
}

} // namespace agentty::tools
