// logx.cpp ——结构化诊断日志:过滤器. 解析 ,原子文件附件,
// 以及自动 -signal-safe 飞行记录器,
// 看 logx.hpp 设计合同。

#include "agentty/util/logx.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#include <maya/runtime.hpp>

#if defined(_WIN32)
  #include <io.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <direct.h>     // 微缩线(_m)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define AGT_WRITE  ::_write
  #define AGT_OPEN   ::_open
  #define AGT_LSEEK  ::_lseeki64
#else
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #define AGT_WRITE  ::write
  #define AGT_OPEN   ::open
  #define AGT_LSEEK  ::lseek
#endif

namespace agentty::logx {

namespace detail {
std::atomic<std::uint8_t> g_min_level[kChannels];   // 零( X) -init ——追踪; init() 提高
std::atomic<bool>         g_ring_all{false};
} // namespace detail

bool full_bodies() noexcept {
    static const bool on = [] {
        const char* v = std::getenv("AGENTTY_LOG_BODIES");
        return v && *v && !(v[0] == '0' && v[1] == '\0');
    }();
    return on;
}

namespace {

// ── 文件槽状态( 由 init 写一次, 后改为 -only) ───────────
// 逻辑 fd. ATOMIC 因为作者在读时没有旋转 XX00120
// 的 互斥量 —— 诊断绝不能在呼叫者的 高频路径 上取一个 锁.
// 旋转现在 re-points 这个 fd 通过 dup2 而不是替换它,
// 所以 值 已经稳定了 生命周期 打开的过程; 原子覆盖
// open/close 的过渡, 并使同时访问 well-defined
// 而不是 竞态 可能假设的 编译器 的  ⁇。
std::atomic<int> g_fd{-1};           // O_APPEND fd, -1 = 文件记录关闭
// 单数时 rotate_locked () 改为 -pointing
// g_fd 与 dup2(). Linux 使原子交换为同时
// write() ; darwin 不 - 与它重复的写入失败
// EBADF (测量:在锤子下上百万次) 线断了,一个打到的作家
// EBADF 等这个去平和 重试 ,所以线条会落在新文件中。
// 作家们从不拿 锁 永远不要等待 除非他们真的与旋转相撞
std::atomic<unsigned> g_rot_seq{0};
std::string g_path;                  // 已解析的log_file( ) 路径
// 中间 -run 旋转 : 字节 打开后写。启动 -only 旋转不足以用于 Dev
// 工作流程( trace) -by-default + 长 -lived 届会:
// 一周内 传输 块会生长一个没有绑定的文件。 检查后每写一个
// 轻松的添加; 实际旋转运行在内部 rotation() 与和
// 重现 -checks 在那里,所以同时的作者 旋转一次。
std::atomic<std::int64_t> g_bytes{0};
// 单身 -rotator 标志. 不 被看守着 <T>: emit()
// 从其他内部调用 锁' 正文 整个程序( 任何) AGT_LOG/dbglog
// 叶子被保护 使每条树枝都成为 jaal 锁-order 文件
// 越过门槛时有过失——扔进此选项 noexcept 路径, i.e.
// std::terminate 旋转是 幂等 和可选(失去
// 竞态 继续附着,所以试试看 -flag 这正是它所需要的:
// 不准等待,不准筑巢 不准检查者看到
struct Rotation {};
std::atomic<bool> g_rotating{false};
constexpr std::int64_t kRotateBytesDefault = 32ll * 1024 * 1024;

}  // 命名空间结束。

// 旋转门槛 故意 不是一个普通的,而不是在公众中 头文件 :
// 没有人配置这个,伐木应该保持一个你不需要设置的东西。
// 它只因为旋转而住在这里。 接口边界 无法测试的,必须写32
// MB 达到目标(测量时间:3分钟以上),这正是为什么fd
// -reuse 下方的bug 已运出, 但没有覆盖。
// logx_rotation_test 编译 这个 TU 直接并降低;
// 没有其他信息提及它。
namespace detail {
std::atomic<std::int64_t> g_rotate_bytes{kRotateBytesDefault};

// 目标设定于 `--log-file` 在第一个日志之前 调用。
// 非原子性, 且没有锁定 : main() 写的时候 解析 argv,
// 任何前的 线程 存在于并在此之前 惰性 init() 后写会
// 是个错误,所以 set_log_path ()大声说出来。
std::string g_path_override;
}

namespace {

// ── 秘密编辑 ──────────────────────────────────────────────────
//
// 应用于 一个 接口边界 每隔一段时间 事件 路过,所以不 调用
// 网站可以忘记它,没有未来的频道可以绕过它。
//
// 之所以存在,是因为日志的整个点是用户SEND.
// `wire=trace` 请求 正文 和提供者的答复; 授权 头文件 或一个
// `"api_key"` 字段粘贴到 Discord 中 线程 是个真正的事件,
// 而且 "remember to scrub it first" 控制不是有效的。
//
// 策略:找到高-signal秘密的SHAPES,
// 用标记取代秘密的VALUE,使周围的结构保持可读性,
// 而JSON则保持可诊断性. 我们故意不试图详尽无遗—
// —一个吃真载荷的超-eager重置器使日志变得无用,
// 这比错过的异国标志更糟糕. 下面的形状覆盖了凭据
// agentty实际上处理的功能.
//
// COST : 仅运行于一条已经要写好的线上, i.e.
// 仅在频道启用时。一个已禁用的日志语句从不会完全
// 达到emit()(宏门先到),所以在默认配置
// 中可以自由编辑。
std::atomic<unsigned long> g_redactions{0};

// 大小写 -insensitive 发现 `needle` 中 [p, p+n] 键入。
[[nodiscard]] const char* find_ci(const char* p, std::size_t n,
                                  std::string_view needle) noexcept {
    if (needle.empty() || n < needle.size()) return nullptr;
    const auto lower = [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    };
    for (std::size_t i = 0; i + needle.size() <= n; ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(p[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return p + i;
    }
    return nullptr;
}

[[nodiscard]] bool token_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
        || c == '+' || c == '/' || c == '=' || c == '~';
}

// 编辑到位。 返回新长度( 永远不增长 : 标记比我们
// 替换的任何秘密短, 因此这是 -buffer 重写的安全 )。
[[nodiscard]] std::size_t redact(char* buf, std::size_t n) noexcept {
    static constexpr std::string_view kMark = "<redacted>";
    // 前缀形状:指针本身始于对齐.
    static constexpr std::string_view kPrefixes[] = {
        "sk-", "ghu_", "ghp_", "gho_", "ghs_", "github_pat_",
        "xai-", "csk-", "tvly-", "AIza", "eyJ",   // eyJ = a JWT header
        // 通过检测形状发现的销售商前缀 供应商实际上会发出:Groq(gsk_),
        // Mistral/Fireworks-style (r8_), Anthropic 管理员(
        // sk) -ant- 由SK-覆盖, OpenRouter (或-),合起来(tgp_).
        "gsk_", "r8_", "or-v1-", "tgp_v1_", "hf_", "nvapi-",
    };
    // 键形:跳过键,然后编辑以下的VALUE.
    // 匹配为 a SUFFIX-tolerant 子弦,所以,( 双弦) "anthropic_api_key"
    // 财务报告和已审计财务报表 "AWS_SECRET_ACCESS_KEY" 击打通过 "api_key"
    // / "secret" 而不是每个卖家拼写一个条目。
    static constexpr std::string_view kKeys[] = {
        "authorization", "x-api-key", "api_key", "apikey", "api-key",
        // 勇敢的搜索 API 以此认证 头文件 ,而表中没有其他
        // 任何内容能与其价值相匹配(一个赤裸的标志没有销售商前缀).
        "x-subscription-token",
        "access_token", "refresh_token", "id_token", "session_token",
        // NOTE : 无赤色 "secret" / "private_key" 条目。
        // 一个通用词与普通的散文相匹配(
        // "/etc/secrets/config.yaml" ,
        // 指 private_key 吃着 载荷 您正在试图阅读 —— 一个
        // 重新定义, 腐蚀真实内容的失败比错过异国标志的失败还要严重。
        // 只有关键 -shaped 拼写后,然后被替换的值。
        "client_secret", "secret_access_key", "secret_key",
        "password", "passphrase", "code_verifier",
    };

    // 字节 开始编辑匹配,两种情况都一样。 两者 表格——千键(千键)
    // "authorization:", "api_key=" )和 kPrefixes
    // (空格) "sk-", "ghu_", a JWT's "eyJ" )—
    // —因为下面的扫描有两个独立的匹配者,跳过一个. 字节 都跳过。
    //
    // 光是KKey就能看到它了 完全就是这个错误 CI 抓获:赤裸裸
    // `ghu_16CharsOfTokenHere123456` 前面没有键,
    // 从头开始 'g', 'g' 没有钥匙 所以前过滤器跳过了 字
    // 节 和标志 -prefix 密探从不逃跑 秘密被记录下来了
    // 建于( 建于 编译 时间从表格本身,所以添加一个
    // 条目 要么不能离开此条目 过期.
    struct FirstBytes {
        bool b[256] {};
        constexpr void add(std::string_view s) {
            if (s.empty()) return;
            const unsigned char lo = static_cast<unsigned char>(s[0]);
            b[lo] = true;
            if (lo >= 'a' && lo <= 'z') b[lo - ('a' - 'A')] = true;
            if (lo >= 'A' && lo <= 'Z') b[lo + ('a' - 'A')] = true;
        }
        constexpr FirstBytes() {
            for (std::string_view k : kKeys)     add(k);
            for (std::string_view p : kPrefixes) add(p);
        }
    };
    static constexpr FirstBytes kFirstTbl{};
    constexpr const bool* kKeyFirst = kFirstTbl.b;

    std::size_t out = 0;
    std::size_t i = 0;
    unsigned hits = 0;

    const auto emit_mark = [&] {
        for (char c : kMark) if (out < n) buf[out++] = c;
        ++hits;
    };

    while (i < n) {
        bool matched = false;

        // 首次-BYTE PREFILTER。没有它,这个循环就是 O( 字节 x 密钥 :
        // 每隔一段时间 字节 对照所有15个关键拼写、
        // 大小写 -insensitively。测量到,
        // 扫描是 ~4 的 1.9 us。 事件 成本——
        // 单最大组件,大于格式化(85ns)和时间戳(
        // 56ns)组合,由每行支付,无论它是否包含一个
        // 秘密(几乎没有)。
        //
        // KKey First是256型机车 -entry 表格,
        // 按区域分列 字节 可能的话 START 两种情况都有一个
        // 索引 加载 拒绝在内环运行之前的绝大多数位置。 输出相同,
        // 1874ns - > 364ns。
        //
        // 这是一个纯加速器:下面的内环没有变化,
        // 仍然决定每对匹配,所以表格只能使MORE
        // 的工作被跳过,决断永远不另作别论.
        if (!kKeyFirst[static_cast<unsigned char>(buf[i])]) {
            if (out != i) buf[out] = buf[i];
            ++out; ++i;
            continue;
        }

        // 键 = 值 / "key": "value" —
        for (const auto& k : kKeys) {
            if (i + k.size() > n) continue;
            if (find_ci(buf + i, k.size(), k) != buf + i) continue;
            std::size_t j = i + k.size();
            // 复制密钥本身加分隔符运行( Q) ": ", "=" ,引文).
            std::size_t sep = j;
            while (sep < n && (buf[sep] == '"' || buf[sep] == ':'
                               || buf[sep] == '=' || buf[sep] == ' ')) ++sep;
            // A "Bearer "/"Basic " 方案是结构,而不是秘密——保留它。
            for (std::string_view scheme : {"Bearer ", "Basic ", "Token "}) {
                if (sep + scheme.size() <= n
                    && find_ci(buf + sep, scheme.size(), scheme) == buf + sep)
                    sep += scheme.size();
            }
            std::size_t end = sep;
            while (end < n && token_char(buf[end])) ++end;
            if (end == sep) continue;          // 无价值——不是秘密
            for (std::size_t c = i; c < sep && out < n; ++c) buf[out++] = buf[c];
            emit_mark();
            i = end;
            matched = true;
            break;
        }
        if (matched) continue;

        // —— 空信前缀(sk-, ghu_, a) JWT
        // )——只在象征边界,所以 "task-" 在一个字里,是没有被触及的。
        const bool at_boundary = (i == 0) || !token_char(buf[i - 1]);
        if (at_boundary) {
            for (const auto& p : kPrefixes) {
                if (i + p.size() > n) continue;
                if (find_ci(buf + i, p.size(), p) != buf + i) continue;
                std::size_t end = i + p.size();
                while (end < n && token_char(buf[end])) ++end;
                // 需要足够的 ⁇ 为凭据,而不是一个单词.
                if (end - i < p.size() + 12) continue;
                emit_mark();
                i = end;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        if (out != i) buf[out] = buf[i];
        ++out; ++i;
    }

    if (hits) g_redactions.fetch_add(hits, std::memory_order_relaxed);
    return out;
}


// 固定的预格式线圈。作家们声称有一个空档,
// 可以放松 fetch_add ,将线条中标,
// 然后用发行商店公布长度;倾销器获得. 撕开的槽(声称尚未公布)
// 读作len \\\0,被跳过——记录器最好. -effort 设计,
// 从不堵塞,从不分配。
//
// 环槽刻意为SMALL并固定:它来自一个崩溃的处理函数,
// 所以它必须是预先分配的,async-signal-safememcpy目标.
// 一个长的事件被剪掉的HERE,并且只在这里——下面的文件
// 槽写全线(见kStackLine / 堆积的回退),
// 所以一个4KB请求的正文在磁盘上完整着陆,而崩溃环则保留一个
// 可以读取的它的摘要.
constexpr std::size_t kRingSlots = 256;
constexpr std::size_t kSlotBytes = 384;

// 将预算叠加起来,用于格式化一行。 事件 没有 分配 ;
// 任何更长的( 缩写) 传输 正文 ,大块 JSON 倒回去
// 堆积 缓冲区 尺寸正好一次 选的太典型了 SSE 块(~1-
// 2) KB ) 仍然在堆栈上格式化。
constexpr std::size_t kStackLine = 4096;

// 里面装着什么新线 MESSAGE BODY 变成磁盘上。
//
// 记录是一行;a 正文 专断 字节 (SSE 帧 ,请求 正文 ,
// 当有人调试时,整个对话 agentty 与 agentty).
// 原始新线会分一个记录 跨多行 并打破每个
// grep 承诺支持。
//
// 一个空间,不是 '\\n' : 内置 a JSON 正文
// 每条真正的新线是 ALREADY 由两个字符反弹时逃出
// -n 留下的生的就是 生的就是这 帧 分隔器和结构白空间。
// 将这些空间变成直线反斜 -n 这将是谎言 关于 载荷's
// 字节 ——空间保持线条的诚实和可读性,从不与逃逸相撞.
// 载荷 合法地包含。
constexpr char kBodyNewline = ' ';

struct Slot {
    // 1 而作者拥有该插槽。 仍被写入插槽的
    // 作者跳过其环形复制, 而不是撕裂两行。
    std::atomic<bool>          busy{false};
    std::atomic<std::uint16_t> len{0};
    char bytes[kSlotBytes];
};
Slot                     g_ring[kRingSlots];
std::atomic<std::uint64_t> g_ring_head{0};

// ── 级别/过滤器解析 ────────────────────────────────────────────
Level parse_level(std::string_view s) noexcept {
    if (s == "trace") return Level::Trace;
    if (s == "debug") return Level::Debug;
    if (s == "info")  return Level::Info;
    if (s == "warn" || s == "warning") return Level::Warn;
    if (s == "error") return Level::Error;
    if (s == "off" || s == "none") return Level::Off;
    return Level::Warn;   // 未知的符号 ~ 保守默认值 ~
}

int channel_index(std::string_view s) noexcept {
    for (std::size_t i = 0; i < kChannels; ++i)
        if (s == kChannelNames[i]) return static_cast<int>(i);
    return -1;
}

// 解析 "$AGENTTY_LOG" — "level"和"chan=level"原子, 逗号-separated.
// 光级设置每个频道的默认值; chan=级别覆盖。
// 未知 channels/levels 跳过( 绝不致命 )
// 解析 用户在启动时输入, typeo 不得将日志系统下架。
void parse_filter(std::string_view spec) noexcept {
    Level def = Level::Off;
    std::array<Level, kChannels> per{};
    per.fill(Level::Off);
    std::array<bool, kChannels> set{};
    set.fill(false);

    std::size_t pos = 0;
    bool any_atom = false;
    while (pos <= spec.size()) {
        auto comma = spec.find(',', pos);
        if (comma == std::string_view::npos) comma = spec.size();
        std::string_view atom = spec.substr(pos, comma - pos);
        pos = comma + 1;
        // 修剪空格
        while (!atom.empty() && atom.front() == ' ') atom.remove_prefix(1);
        while (!atom.empty() && atom.back() == ' ')  atom.remove_suffix(1);
        if (atom.empty()) continue;
        any_atom = true;
        if (auto eq = atom.find('='); eq != std::string_view::npos) {
            const int ci = channel_index(atom.substr(0, eq));
            if (ci >= 0) {
                per[static_cast<std::size_t>(ci)] =
                    parse_level(atom.substr(eq + 1));
                set[static_cast<std::size_t>(ci)] = true;
            }
        } else {
            def = parse_level(atom);
        }
        if (comma == spec.size()) break;
    }
    // 仅一个 chan=级原子的光谱会让默认关闭( 静静其余部分);
    // 一个空光谱意味着日志会完全关闭。
    if (!any_atom) def = Level::Off;
    for (std::size_t i = 0; i < kChannels; ++i)
        detail::g_min_level[i].store(
            static_cast<std::uint8_t>(set[i] ? per[i] : def),
            std::memory_order_relaxed);
    // 如果出现 任意 频道接受在警告下方, 镜像那些 事件 进入
    // 飞行圈(追踪) -enabled 运行想要它的戒指匹配。
    bool ring_all = false;
    for (std::size_t i = 0; i < kChannels; ++i)
        if (detail::g_min_level[i].load(std::memory_order_relaxed)
            < static_cast<std::uint8_t>(Level::Warn))
            ring_all = true;
    detail::g_ring_all.store(ring_all, std::memory_order_relaxed);
}

// 打开一个日志文件作为附件。 在Windows上, CRT的_open 共享文件,
// 用于读取+写入但NOT删除, 并且一个没有FILE_SHARE_DELETE
// 的打开的手柄使文件的每个重命名都失败—— 所以旋转( 重命名
// live - >. 旧, 而在水槽打开时) , 并且日志从未默默地生长,
// 并且没有绑定。 通过CreateFile 打开, 完全共享, 并将手柄包裹
// 在CRT fd中, 所以其余的logx 保留 fd-based。
int open_append(const char* path) noexcept {
#if defined(_WIN32)
    HANDLE h = ::CreateFileA(path, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(h),
                                     _O_WRONLY | _O_APPEND | _O_BINARY);
    if (fd < 0) ::CloseHandle(h);
    return fd;
#else
    return AGT_OPEN(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
#endif
}

void open_sink(const char* path) noexcept {
    if (!path || !*path) return;
    const std::int64_t limit =
        detail::g_rotate_bytes.load(std::memory_order_relaxed);
    // 如果超大小, 启动时旋转一次—— 将名称重命名为. old( best-effort)。
    struct stat st{};
    if (::stat(path, &st) == 0 && st.st_size > limit) {
        std::string old = std::string{path} + ".old";
        std::remove(old.c_str());
        std::rename(path, old.c_str());
    }
    g_fd.store(open_append(path), std::memory_order_relaxed);
    if (g_fd.load(std::memory_order_relaxed) >= 0) g_path = path;
    // 以现有大小来种子旋转计数器,所以一个附件
    // -to文件仍然在同一绝对阈值旋转.
    if (g_fd.load(std::memory_order_relaxed) >= 0) {
        struct stat st2{};
        g_bytes.store(::stat(path, &st2) == 0 ? st2.st_size : 0,
                      std::memory_order_relaxed);
    }
}

// 超过旋转阈值后旋转活水槽: 将旧的( 上一个已丢弃) 重命名为 .
// .old (the previous .old is dropped) and reopen fresh. Called from emit()
// 对同时代的作者安全—— 作者 锁 序列化,再 -check 下方就是它,
// 下方就是它的地盘 幂等 作家们在最糟糕的地方 竞速
// 进行交换 并在前期 -rotation 文件 (E)
// O_APPEND 保持每条字都完好无损。 -signal-safe ;
// 从未从崩溃路径中呼叫。
void rotate_locked(Rotation&, std::int64_t limit) noexcept;

void rotate_if_needed() noexcept {
    const std::int64_t limit =
        detail::g_rotate_bytes.load(std::memory_order_relaxed);
    if (g_bytes.load(std::memory_order_relaxed) <= limit) return;
    bool expected = false;
    if (!g_rotating.compare_exchange_strong(expected, true,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed))
        return;   // 另外一个写器在旋转; 它重置大小的 -checks
    Rotation r;
    rotate_locked(r, limit);
    g_rotating.store(false, std::memory_order_release);
}

void rotate_locked(Rotation&, std::int64_t limit) noexcept {
    if (g_bytes.load(std::memory_order_relaxed) <= limit) return;
    if (g_fd.load(std::memory_order_relaxed) < 0 || g_path.empty()) return;
    const std::string old = g_path + ".old";
    std::remove(old.c_str());
    if (std::rename(g_path.c_str(), old.c_str()) != 0) {
        // 重命名失败 (E) 权限 十字路口 -device )——
        // 继续附加到大文件上而不是输掉. 事件; 重试 下一个十字架上
        g_bytes.store(0, std::memory_order_relaxed);
        return;
    }
    // 旋转到 -pointing 编号 相同 fd 号码,不是通过换到一个新的号码.
    //
    // 旧代码是`g_fd = nfd; close(oldfd);`,
    // 而其他的线程正在读g_fd解锁并写给它。
    //
    //   *一个在交换之前读过旧的作家写给一个 CLOSED 脚注-
    //     线被放下,沉默地, 确切的说,当一个旋转日志意味
    //     着应用程序繁忙, 诊断最重要的时候;
    //   * 更糟的是,fd数字被重复使用。在闭合(oldfd)和那个之间
    //     作家的书 write() ,任何一个 线程 打开套接字,
    //     a 线程 文件或工具管道可以被传递到相同的数字——
    //     然后我们写日志 字节 堕落的 对端 其原因为
    //     LOGGER 接近不可诊断。
    //
    // 数字2 (nfd, g_fd )在解剖学上使现有的
    // 描述符参考了新文件,所以 g_fd 永远不关闭,
    // 永远不重复使用,永远不 过期。一个作家在旋转中将线线
    // 放在旧文件或新文件上,两者都正确,都没有丢失。
    // 只有临时nfd关闭,其他的都从未见过。。
    const int nfd = open_append(g_path.c_str());
    if (nfd >= 0) {
#if defined(_WIN32)
        (void)::_dup2(nfd, g_fd.load(std::memory_order_relaxed));
        ::_close(nfd);
#else
        g_rot_seq.fetch_add(1, std::memory_order_acq_rel);   // odd: swapping
        while (::dup2(nfd, g_fd.load(std::memory_order_relaxed)) < 0
               && errno == EINTR) {}
        g_rot_seq.fetch_add(1, std::memory_order_acq_rel);   // even: done
        ::close(nfd);
#endif
    }
    g_bytes.store(0, std::memory_order_relaxed);
}

} // 命名空间结束。

namespace detail {

bool init() {
    // ── 默认策略 ──────────────────────────────────────────────
    //
    // NON-RELEASE 构建日志 EVERYTHING ,
    // 改为 一个 文件,没有env var可发现。开发者或运行
    // 调试架构的任何人默认都会获得完整的跟踪,因为替代方案——这
    // 是什么 代码库 被报告为照片的bug 终端 并且通过
    // 猜测诊断出来。 (副驾驶工具) -argument 错误
    // 就是: 修正被忽略了 SSE 事件 ,但症状读作弱模型,
    // 所以第一个本能是尝试不同的模型. )
    //
    // RELEASE 建筑保持安静,除非 AGENTTY_LOG 询问:伐木成本
    // I/O 和释放 二进制程序 不得在未邀请的情况下写入用户的磁盘。
    //
    // 无论是哪种方式,都有 一个 knob(AGENTTY_LOG) 和 一个
    // 文件 (AGENTTY_LOG_FILE). Per-module track
    // vars 是您最后如何用八种方式问同样的问题, 也不可能记住其中的任何一种.
    // RELEASE 保留 WARN + 而不是完全沉默。 每会话需要几行,
    // 并且是可用的第一个错误报告与完全没有的区别 : 没有它,
    // 用户会击中错误, 运行 `agentty diagnostics` ,
    // 并获得一个文件,其中写着 "logging was off"
    // ——失败已经结束,要求他们复制到下面.
    // AGENTTY_LOG 命令中存在的摩擦正是要去除的。
    //
    // 警告+ 总是被保存在坠机环中; 这仅仅是坚持不变 事件 所以他们
    // 活了下来 Trace/Debug (画 传输 字节 单位,
    // -event 聊天) 还需要 AGENTTY_LOG.
#if defined(NDEBUG)
    constexpr Level kDefault = Level::Warn;
#else
    constexpr Level kDefault = Level::Trace;
#endif
    for (auto& lv : g_min_level)
        lv.store(static_cast<std::uint8_t>(kDefault), std::memory_order_relaxed);

    const char* spec = std::getenv("AGENTTY_LOG");
    // WHERE 日志是一条 FLAG (`--log-file` ),
    // 而不是环境变量。它移动了两个原因:
    //
    //   * AGENTTY_LOG 说 内容 要抓取; 一条路径。 WHERE 说吧
    //     两个无关的决定不属于同一个机制,两者作为env vars
    //     意味着一个想法的三个拼写( AGENTTY_LOG_FILE,
    //     AGENTTY_DEBUG_LOG=<path>
    //     和隐性违约),二者之间没有明显的优先.
    //   * a 标志 出现于 --help . . env var 只有在您找到时才能找到
    //     已经知道它的存在,这就是一个有文件记录的旋钮如何
    //     仍然被使用。
    //
    // 环境现在有两个伐木变量 都回答 "what to capture":
    // AGENTTY_LOG 财务报告和已审计财务报表 AGENTTY_LOG_BODIES.
    // 退休在这里: AGENTTY_LOG_FILE (-> --log-file 和。 和 简 况
    // AGENTTY_DEBUG_LOG (-> --log-file 加号 AGENTTY_LOG =debug) (英语).
    const char* file = detail::g_path_override.empty()
                           ? nullptr
                           : detail::g_path_override.c_str();

    if (spec && *spec) parse_filter(spec);
    // 没有文件日志的过滤器 <user-root>/logs/agentty.log (
    // $AGENTTY_HOME /日志或 ~/.agentty/logs - 单人 -root 版式,
    // 见 util/user_root.hpp ),或改为 $AGENTTY_LOGS_DIR 等
    // 它被设定的时候, AGENTTY_LOG 单挑就成功了
    // 在一个非 -release 建一个相同的是真实的 NO 完全没有。
    const bool want_file = (spec && *spec) || kDefault != Level::Off;
    if (file && *file) open_sink(file);
    // 故意的手 故意的双手 -rolled env 读取( 无) <filesystem> ,
    // 没有 user_root 依赖性:logx必须保持与狭义的消毒器测试的连接
    // 编译单元 坠入地狱 -handler-safe.
    //
    // 这种独立性也是一种 HAZARD ,这个块就是它咬到的地方:
    // 这里的布局被拼写为 `root + "/logs"`
    // 而不是要求,所以当 $AGENTTY_LOGS_DIR
    // 添加到 user_root.cpp 木头,
    // 最大的东西 agentty 写 —— 是默默忽略它的一个类别。
    // 重复的布局知识漂移; 固定是重复 OVERRIDE 并且
    // 将两个拼写相邻的字句放在注释中,所以下次对两个字的修改是
    // 显而易见的。
    if (g_fd.load(std::memory_order_relaxed) < 0 && want_file) {
        std::string root;
        if (const char* ah = std::getenv("AGENTTY_HOME"); ah && *ah)
            root = ah;
        else if (const char* home = std::getenv("HOME"); home && *home)
            root = std::string{home} + "/.agentty";
#if defined(_WIN32)
        if (root.empty())
            if (const char* prof = std::getenv("USERPROFILE"); prof && *prof)
                root = std::string{prof} + "\\.agentty";
#endif
        // $AGENTTY_LOGS_DIR 胜过默认叶子。
        // 规则与 util::resolve_subdir (:绝对值
        // 视为 -is ,相对的解决方式为 ROOT 而不是说 CWD ,
        // 所以一个设置意味着一个目录而不是每个发射目录一个.
        const char* logs_env = std::getenv("AGENTTY_LOGS_DIR");
        const bool have_env = logs_env && *logs_env;
#if defined(_WIN32)
        const bool env_abs =
            have_env && (logs_env[0] == '\\' || logs_env[0] == '/' ||
                         (logs_env[0] != '\0' && logs_env[1] == ':'));
#else
        const bool env_abs = have_env && logs_env[0] == '/';
#endif
        // 绝对的超载不需要根,这就是在没有可用HOME
        // 的宿主上保持记录生命的情况.
        if (!root.empty() || env_abs) {
#if defined(_WIN32)
            if (!root.empty()) ::_mkdir(root.c_str());
            const std::string dir = !have_env  ? root + "\\logs"
                                    : env_abs  ? std::string{logs_env}
                                               : root + "\\" + logs_env;
            ::_mkdir(dir.c_str());
            open_sink((dir + "\\agentty.log").c_str());
#else
            if (!root.empty()) ::mkdir(root.c_str(), 0700);
            const std::string dir = !have_env  ? root + "/logs"
                                    : env_abs  ? std::string{logs_env}
                                               : root + "/" + logs_env;
            ::mkdir(dir.c_str(), 0755);
            open_sink((dir + "/agentty.log").c_str());
#endif
        }
    }
    return true;
}

} // namespace detail

std::string_view log_file() noexcept {
    (void)detail::inited();
    return g_path;
}

void set_log_path(std::string_view path) noexcept {
    // 故意的,故意的,有的,有的是,有的是... 不 调用 inited()
    // : 整个要点是降落覆盘 之前。一旦文件打开,在现场写作机下重新打开它
    // 就是 fd -reuse 危险 rotate_if_needed
    // ()存在是为了避免,所以迟到 调用 改为删除。
    if (g_fd.load(std::memory_order_relaxed) >= 0) return;
    try {
        detail::g_path_override.assign(path);
    } catch (...) {
        // 这里的 分配 失败表示没有覆盖;
        // 默认路径仍然适用。 绝不让日志员丢弃自己的设置。
    }
}

unsigned long redaction_count() noexcept {
    return g_redactions.load(std::memory_order_relaxed);
}

void emit(Channel ch, Level lv, std::string_view site,
          std::string_view msg) noexcept {
    (void)detail::inited();

    const bool to_file = (g_fd.load(std::memory_order_relaxed) >= 0) && enabled(ch, lv);
    const bool to_ring = lv >= Level::Warn
                      || detail::g_ring_all.load(std::memory_order_relaxed);
    if (!to_file && !to_ring) return;

    // ── 一次格式化全行 ─────────────────────────────────
    // 2026-08-28T01:23:45.678+0012345ms tid4 W 传输 遗址:msg  ⁇ .
    //
    // 线条在堆栈上,除非它不适合。 传输 正文 常规超过数 KB ,
    // 旧的固定 384 -byte 缓冲区 悄悄剪辑了它们——
    // a "verbatim" 传输 追踪每件请求中 90
    // %的被丢掉 正文 更糟糕的是没有痕迹 因为看起来很完整
    // 堆积 回退 保持长长 事件 整体; 常用短数 事件
    // 仍然为零格式 分配.
    char  stack_line[kStackLine];
    std::string heap_line;
    char* line = stack_line;
    std::size_t cap = sizeof(stack_line);

    // 头文件 受限(时间戳+tid+级别+通道+站点+___~120B);
    // `msg` 可能很大 所以只要一个尺寸的检查就够了
    constexpr std::size_t kHeaderMax = 160;
    if (msg.size() + kHeaderMax + 2 > cap) {
        heap_line.resize(msg.size() + kHeaderMax + 2);
        line = heap_line.data();
        cap  = heap_line.size();
    }

    std::size_t n = 0;
    {
        using namespace std::chrono;
        const auto now  = system_clock::now();
        const auto secs = system_clock::to_time_t(now);
        const int  ms   = static_cast<int>(
            duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &secs);
#else
        localtime_r(&secs, &tm);
#endif
        static const auto t0 = steady_clock::now();
        const long long mono_ms = duration_cast<milliseconds>(
            steady_clock::now() - t0).count();
        // 小马力 线程 标记: 散列 thread::id 4块六分钱
        const auto tid_hash = static_cast<unsigned>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffff);

        const std::size_t ci = static_cast<std::size_t>(ch);
        const auto& cn = kChannelNames[ci];   // 字义 -backed ,%.*s——无 Aloc
        int w = std::snprintf(
            line, cap,
            "%04d-%02d-%02dT%02d:%02d:%02d.%03d +%07lldms %04x %c %-7.*s %.*s: ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, ms,
            mono_ms, tid_hash,
            kLevelChar[static_cast<std::size_t>(lv)],
            static_cast<int>(cn.size()), cn.data(),
            static_cast<int>(std::min(site.size(), std::size_t{48})),
            site.data());
        if (w < 0) return;
        n = std::min(static_cast<std::size_t>(w), cap - 2);
        const std::size_t hdr_end = n;   // 消息之前的一切 正文
        const std::size_t room = cap - 2 - n;   // 保留\n + NUL
        const std::size_t take = std::min(msg.size(), room);
        std::memcpy(line + n, msg.data(), take);
        n += take;

        // 一个 EVENT IS 一个 LINE.
        //
        // 信息内容 正文 任意性: 原始 SSE 帧 ,
        // 请求 正文 ,和——当用户调试时 agentty 和
        // 他交谈 agentty ——整段对话。其中任何一段
        // 都可以包含新线,复制后将一则记录分解为多条物理线。
        //
        // 这违反了档案中心的承诺 `grep ' E '`
        // 错过错误 正文 包起来,每条续行 与真实
        // 记录是分不开的,和a 载荷 引用
        // "provider.select:" 开始时,
        // 它无法从中分辨 事件 - 这正是如何
        // `agentty diagnostics` 来报道一阵对话
        // 作为活跃的提供者。
        //
        // 逃到这里,在那边 BODY 仅 ,
        // 保留 头文件 解析和记录完整。
        // -for-one ——这在每一个日志上运行 调用 因此,
        // 它不能分配或转移 缓冲区.
        for (std::size_t i = hdr_end; i < n; ++i) {
            if (line[i] == '\n' || line[i] == '\r') line[i] = kBodyNewline;
        }

        // 将秘密脱开 任意 沉进看到线条,一个 接口边界 ,
        // 所以一个新的频道或 调用 网站不能通过省略漏漏漏。
        n = redact(line, n);
        line[n++] = '\n';
    }

    // ── 飞行记录器(通常用于Warn+,或ring_all时) ──────────
    // 切入到固定的槽中: 此 缓冲区 从崩溃中读取 处理函数 ,
    // 所以它必须保持 预先分配和同步 -signal-safe.
    if (to_ring) {
        const auto slot_idx = g_ring_head.fetch_add(1, std::memory_order_relaxed)
                              % kRingSlots;
        Slot& s = g_ring[slot_idx];
        if (!s.busy.exchange(true, std::memory_order_acquire)) {
            s.len.store(0, std::memory_order_release);        // 无效
            const auto take = std::min(n, sizeof(s.bytes));
            std::memcpy(s.bytes, line, take);
            s.len.store(static_cast<std::uint16_t>(take),
                        std::memory_order_release);            // 发布
            s.busy.store(false, std::memory_order_release);
        }
    }

    // ── 文件槽: O_APPEND fd上写一个(2) — 原子附件 ─────
    if (to_file) {
        // 最佳( 最佳) -effort ; 短写 / EINTR 诊断绝不能拖延呼叫者。
        // `!` (不是空的(void)) 标记使用的结果: glibc
        // 宣告 write() __wur 下级 _FORTIFY_SOURCE ,
        // 和 GCC 忽略一个纯空虚 -cast 费用
        // warn_unused_result 函数。
        //
        // 重审 EBADF: dup2() 将文件换到一个 live 下
        // write() 原子在 Linux 但当DARWIN上,
        // 一个写比赛 交换失败了 EBADF 而不是在两个文件中降落 --
        // -- 测量:8名作家在文件被击中时击打一个英尺 dup2() 达两
        // 万次 看见数千万次 EBADF ,并设定 重试 单是一岁还下降5%。
        // 电话 macOS CI 运行器损失了 ~450 的对数行中的
        // 2 条。 EBADF 与旋转同时进行( g_rot_seq 奇数,
        // 或自我们开始后更改)等待交换完成, 重试 ;
        // 一项 EBADF 飞行中没有旋转,是真正的糟糕的飞行,被抛下。
        const int fd = g_fd.load(std::memory_order_relaxed);
        for (;;) {
            const unsigned seq = g_rot_seq.load(std::memory_order_acquire);
            if (AGT_WRITE(fd, line, static_cast<unsigned>(n)) >= 0) break;
            if (errno != EBADF) break;
            if ((seq & 1u) == 0
                && g_rot_seq.load(std::memory_order_acquire) == seq) break;
            while (g_rot_seq.load(std::memory_order_acquire) & 1u)
                std::this_thread::yield();
        }
        g_bytes.fetch_add(static_cast<std::int64_t>(n),
                          std::memory_order_relaxed);
        rotate_if_needed();
    }
}

std::size_t dump_flight_recorder(int fd) noexcept {
    // 最新,只有Aync -signal-safe 原子操作: 加载 ,
    // 中度 -free 原始的预格式槽的写法。
    if (fd < 0) return 0;
    static const char hdr[] = "=== agentty flight recorder (last events, oldest first) ===\n";
    std::size_t total = 0;
    auto wr = [&](const char* p, std::size_t len) {
        const auto k = AGT_WRITE(fd, p, static_cast<unsigned>(len));
        if (k > 0) total += static_cast<std::size_t>(k);
    };
    wr(hdr, sizeof(hdr) - 1);
    const auto head = g_ring_head.load(std::memory_order_relaxed);
    const auto count = head < kRingSlots ? head : kRingSlots;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto idx = (head - count + i) % kRingSlots;
        const Slot& s = g_ring[idx];
        const auto len = s.len.load(std::memory_order_acquire);
        if (len == 0 || len > kSlotBytes) continue;   // torn/empty 档次
        wr(s.bytes, len);
    }
    return total;
}

bool dump_flight_recorder_to(const char* path) noexcept {
    if (!path || !*path) return false;
#if defined(_WIN32)
    int fd = AGT_OPEN(path, _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY,
                      _S_IREAD | _S_IWRITE);
#else
    int fd = AGT_OPEN(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
#endif
    if (fd < 0) return false;
    const auto n = dump_flight_recorder(fd);
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
    return n > 0;
}

void signal_mark() noexcept {
    // ASYNC-SIGNAL-SAFE 按建筑结构: 静态预格式 字节 ,
    // 原始写作(2)s,原子 加载 内 dump_flight_recorder
    // -没有 分配 ,没有 锁 ,不格式化。已安装 SIGUSR1 这样,
    // 一个 dev 可以从外部戳出活日志( )。 `kill -USR1 $(pgrep agentty)`
    // 他们的那一刻 SEE 虫子,不碰它,不碰它的,不碰它们 TUI
    // : 标记线加飞行记录器 快照 降落在精确的观测点。
    if (g_fd.load(std::memory_order_relaxed) < 0) return;
    static const char m[] =
        "\n=== MARK (SIGUSR1) — user flagged this moment ===\n";
    (void)!AGT_WRITE(g_fd.load(std::memory_order_relaxed), m, sizeof(m) - 1);
    (void)dump_flight_recorder(g_fd.load(std::memory_order_relaxed));
    static const char e[] = "=== END MARK ===\n\n";
    (void)!AGT_WRITE(g_fd.load(std::memory_order_relaxed), e, sizeof(e) - 1);
}

void session_banner(std::string_view info) noexcept {
    (void)detail::inited();   // 确定先行沉没的沉没区——可能打开g_fd
    if (g_fd.load(std::memory_order_relaxed) < 0) return;
    std::string line;
    line.reserve(info.size() + 32);
    line += "\n=== agentty session: ";
    // 标志上挂着 CWD ,而目录名称可能在法律上包含新线。复制了将横幅
    // 线提前结束的逐字记录,并允许 REST 路径在0栏中着陆,
    // 与真实记录无法区分。
    // `mkdir $'x\n2026-01-01T00:00:00.000 +0000000ms dead E general auth'` 然后,
    // 以 grep 为一个真实的认证错误。 已校验的比对
    // 已建的 二进制程序 在那名警卫存在之前
    //
    // 和规则一样 事件 正文 (见kBodyNewline):一
    // 则记录就是一行,所以这篇书写中唯一的新线就是我们放在那里的.
    for (const char c : info)
        line += (c == '\n' || c == '\r') ? kBodyNewline : c;
    line += " ===\n";
    (void)!AGT_WRITE(g_fd.load(std::memory_order_relaxed), line.data(), static_cast<unsigned>(line.size()));
    g_bytes.fetch_add(static_cast<std::int64_t>(line.size()),
                      std::memory_order_relaxed);
}

} // namespace agentty::logx
