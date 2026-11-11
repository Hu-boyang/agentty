#pragma once
// agentty::logx——结构化诊断日志。
//
// ─────────────────────────────────────────────────────────────────────────
// DESIGN
// ─────────────────────────────────────────────────────────────────────────
// 一种日志记录系统，具有 ad-hoc 前辈所缺乏的四个属性（dbglog 的 fopen
// -per-line、provider/debug.hpp 的原始转储、
// 分散的 AGENTTY_*_PROF fopen 站点）：
//
//   1.LEVELED+CHANNELED，如RUST_LOG一样过滤。每个 事件 都带有一个
//      (通道，级别)对；过滤器是从 $AGENTTY_LOG 解析一次的
//      per-channel 最低级别：
//
//          AGENTTY_LOG=debug # 调试中的所有内容+
//          AGENTTY_LOG=传输=trace # 一个通道全开 AGENTTY_LOG
//          =warn,传输=trace,
//                                              auth=debug # 默认警告，覆盖
//
//      门是ONE宽松原子加载+每个调用站点的比较——当
//      通道关闭时，日志语句花费约1ns并格式化NOTHING
//      (甚至在评估格式参数之前的宏short
//      -circuits)。
//
//   2.ATOMICAPPENDS，文件打开ONCE。 事件格式入栈
//      缓冲区并在O_APPEND fd上将文件作为单个写入(
//      2)击中——POSIX保证附加不会交错，因此在写入路径上
//      存在NO MUTEX。 （前身re-opened文件每行在全局锁下。）
//      当文件超过32 MB（重命名为.old）时，
//      文件在启动时轮转一次，因此启用long
//      -lived的日志不能无限增长。
//
//   3. FLIGHT RECORDER，常亮。最后一个 kRingSlots 事件 位于
//      警告+（或所有内容，当通道启用时）ALSO复制到固定
//      位置-process响铃 — 即使文件日志记录已关闭。
//      在 SIGSEGV/SIGABRT 上，崩溃 处理函数 调用
//      dump_flight_recorder()，它将预先格式化的环写入(
//      2)到崩溃文件：每个崩溃报告附带发生的最后约 256 件事情，
//      稳定的 -state 成本为零，超出每个警告的 memcpy。 Async
//      -signal-safe 通过构造（预格式化 字节，原始写入，无 分配）。
//
//   4.SPANS。 AGT_SPAN（通道，"name"）在跟踪和退出时记录条目
//      具有单调递增的持续时间-RAII，异常-safe，可嵌套。
//      bad-man 的跟踪配置文件取代了 one-off
//      AGENTTY_LOAD_PROF / AGENTTY_CACHE_PROF
//      fopen 站点（现在全部折叠到 `perf` 通道上 — 此 头
//      文件 要求的迁移已完成；不再有分析器写入自己的文件）。
//
// FORMAT(logfmt-ish，每事件一行，grep-friendly)：
//
//   2026-08-28T01:23:45.678 +0012345ms 1a2b W 传输 openai.stream:连接被拒绝主机=localhost:8080
//   └─ 日历时钟 ──────────┘ └ 单调时钟 ┘ 线程 等级 通道 位置: 消息
//
// 两个 VARIABLES、两者 ABOUT 内容 TO
// CAPTURE:AGENTTY_LOG(电平+通道滤波器)和AGENTTY_LOG_BODIES
// (包括载荷)。 WHERE 文件是 `--log-file`、标志，
// 因为目标不是捕获策略，而 标志 是 --help 中显示的部分。
// 已退役：AGENTTY_LOG_FILE和AGENTTY_DEBUG_LOG(两个目的地)，
// 加上AGENTTY_DEBUG_API/AGENTTY_DEBUG_FILE/AGENTTY_ACP_TRACE和四个AGENTTY_*_PROF分析器，
// 现在全部都在这里频道。旧的 util::dbglog(where,
// msg) API 仍然转发到这里 (General/Error) —
// 该函数保留了下来，只是它的环境变量消失了。原始 传输 字节 不是 一个
// 单独的系统：完整的 request/chunk 正文 也在这里，
// 位于 Trace 的 `wire` 通道上，因此 字节-level
// 问题和行为问题可以从同一文件中得到回答。
//
// THREADING：调用者堆栈上的格式；以宽松的 fetch_add
// 声明发布到环；追加一次写入(2)。无背景 线程 — 关闭时
// 没有任何内容可加入，无拆卸 竞态（请参阅 prewarm
// -thread 关闭错误以了解为什么 agentty 避免分离记录器）。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace agentty::logx {

// ── 频道 ──────────────────────────────────────────────────────────
// 每个子系统一个。添加一项：扩展 枚举、kChannelNames 和
// static_assert — 解析器 和过滤器会自动选取它。
enum class Channel : std::uint8_t {
    General,   // 未分类（dbglog shim 位于此处）
    Wire,      // HTTP/SSE 传输层、request/response生命周期
    Auth,      // OAuth流程、密钥解析、帐户切换
    Persist,   // settings/threads/memory磁盘IO
    Tool,      // 工具分派、权限、沙箱
    Ui,        // reducer/view异常
    Rag,       // 检索引擎
    Mcp,       // MCP桥+插件
    Acp,       // ACP server/adapter
    Smart,     // smart-mode路由决策
    Net,       // 套接字，TLS，代理，预热
    Model,     // 能力分辨率+provider/model异构性：
               // 哪方言一转就走，用什么力气夹住，学到能力事实，
               // 弱-model 回退，工具-call打捞。
               // 当型号 "behaves weird" 在
               // 一个提供商上而非另一提供商上时打开的通道。
    Perf,      // 计时和缓存记账：提示词-cache hit/miss每
               // 依次，线程-load和查看-build花费，
               // 数据流，帧起搏，检索延迟。
               // 回答 "why did that take so long"，
               // 这是与 "what did it do" 不同的问题，
               // 否则会淹没其他频道所需的级别。
    kCount_,   // 哨兵——保持最后
};
inline constexpr std::size_t kChannels =
    static_cast<std::size_t>(Channel::kCount_);

inline constexpr std::string_view kChannelNames[kChannels] = {
    "general", "wire", "auth", "persist", "tool", "ui",
    "rag", "mcp", "acp", "smart", "net", "model", "perf",
};

// ── 职等 ────────────────────────────────────────────────────────────
enum class Level : std::uint8_t {
    Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4,
    Off   = 5,   // 仅过滤器值 — 没有 事件 携带 Off
};
inline constexpr char kLevelChar[] = { 'T', 'D', 'I', 'W', 'E', '-' };

// ── 大门(高频路径) ───────────────────────────────────────────────
// Per-channel最低级别，从环境解析一次。宽松的 加载
// 是正确的：该数组在 init() 期间写入一次（在任何读者
// 可以观察到撕裂值之前 - init 从神奇的静态运行），
// 并且此后永远不会改变。
namespace detail {
extern std::atomic<std::uint8_t> g_min_level[kChannels];
extern std::atomic<bool>         g_ring_all;   // 环捕获<警告太
bool init();                                    // 幂等，便宜先后
inline bool inited() {
    static const bool done = init();
    return done;
}
} // namespace detail

// 整个请求正文是我们记录的最大的事情：长线程每轮发送一个multi
// -MB 正文，并且默认的debug-build级别（跟踪）
// 用于写入所有它，然后在请求发出之前在数据流 线程上运行
// redact()。 body() 返回要记录的文本：仅当设置
// AGENTTY_LOG_BODIES 时才返回整个内容（除
// "0" 之外的任何值），否则为有界头加上总大小。
// 比 调用 便宜：标志 被读取一次。
[[nodiscard]] bool full_bodies() noexcept;
[[nodiscard]] inline std::string_view body(std::string_view b) noexcept {
    constexpr std::size_t kHead = 4096;
    return (full_bodies() || b.size() <= kHead) ? b : b.substr(0, kHead);
}

// 当且仅当(ch，lv)上的事件将被写入FILE时为真。
// 宏 调用 在评估其格式参数之前。
[[nodiscard]] inline bool enabled(Channel ch, Level lv) noexcept {
    (void)detail::inited();
    return static_cast<std::uint8_t>(lv) >=
           detail::g_min_level[static_cast<std::size_t>(ch)]
               .load(std::memory_order_relaxed);
}

// 正确当且且当事件应当被记录为AT 全部(文件OR飞行环)。
// 即使文件注销，Warn+也始终记录到环。
[[nodiscard]] inline bool recorded(Channel ch, Level lv) noexcept {
    return lv >= Level::Warn || enabled(ch, lv)
        || detail::g_ring_all.load(std::memory_order_relaxed);
}

// ── 排放(冷路——通过宏观的调用) ────────────────────
// `site`是短静态标签("openai.stream"，"persistence.save")。
// `msg`是完全格式化的正文。写入文件行（如果启用）和
// Flight-recorder 插槽（如果已记录）。
// 从不抛出，从不在环路上分配。
void emit(Channel ch, Level lv, std::string_view site,
          std::string_view msg) noexcept;

// 仅使用 async-signal-safe 调用 将飞行记录器转储到
// `fd` (预格式化槽的原始写入，最早的在前)。
// 从 SIGSEGV/SIGABRT 处理函数 调用；也可以通过测试使用。返回写入的 字节。
std::size_t dump_flight_recorder(int fd) noexcept;

// 打开（创建）crash-dump文件并将带有小头文件的环转储到其中。
// 如果写入任何内容，则返回 true。路径 -building 部分
// 中的 不 信号 -safe — main.cpp 在启动时预先计算路径，
// 而 处理函数 调用 (int fd) 过载；这种便利性对于
// 测试和有序关闭路径来说是存在的。
bool dump_flight_recorder_to(const char* path) noexcept;

// Async-signal-safe日志标记：将"=== MARK ==="横幅
// 加上flight-recorder 快照写入实时日志文件中。安装在 main() 的
// SIGUSR1 上，因此 `kill -USR1 $(pgrep agentty)`
// 在错误是 OBSERVED 时标记日志 — 没有 TUI 交互，没有时间戳考古。
// 文件记录关闭时为 No-op。
void signal_mark() noexcept;

// Write是直接到文件接收器的会话横幅行（绕过级别过滤器——如果正在写入文件，
// 则每个进程运行必须在其中运行self-identify）。
// 使用 version/build/pid/ cwd 从 main() 调用一次；
// 使 multi-session 附加-mode 日志可导航：grep "=== agentty"
// 将其拆分为多个运行。文件记录关闭时为 No-op。
void session_banner(std::string_view info) noexcept;

// 已解析的 log-file 路径（"" = 文件日志记录已禁用）。对于状态 UI。
[[nodiscard]] std::string_view log_file() noexcept;

// 将日志指向`path`。在第一个日志 调用 之前调用
// `--log-file` 和 必须 — 接收器在第一次使用时会延迟解析，
// 因此后面的 调用 会被默默地忽略，而不是重新打开其他 线程
// 可能已经写入的文件。 main() 调用 它同时 解析 argv，
// 它在任何 线程 存在之前。空路径=无覆盖（用户 root
// 下的默认设置仍然适用）。
void set_log_path(std::string_view path) noexcept;

// 到目前为止，已经从日志行中剥离了多少秘密。当用户查找日志时，
// 他们会看到：看到 "redacted 47 secrets"
// 会让人们放心地将其附加到错误报告中。
[[nodiscard]] unsigned long redaction_count() noexcept;

// ── 西班牙语 ─────────────────────────────────────────────────────────────
// RAII范围定时器：在Trace处进入，在Trace处退出并具有持续时间。
// 当通道处于 Trace 时，退出线总是发出 —
// 包括通过提前返回或异常展开。
class Span {
public:
    Span(Channel ch, std::string_view name) noexcept
        : ch_(ch), name_(name), live_(enabled(ch, Level::Trace)) {
        if (live_) {
            t0_ = std::chrono::steady_clock::now();
            emit(ch_, Level::Trace, name_, "begin");
        }
    }
    ~Span() {
        if (!live_) return;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0_).count();
        char buf[48];
        const int n = std::snprintf(buf, sizeof(buf), "end (%lld.%03lld ms)",
                                    static_cast<long long>(us / 1000),
                                    static_cast<long long>(us % 1000));
        emit(ch_, Level::Trace, name_,
             std::string_view{buf, n > 0 ? static_cast<std::size_t>(n) : 0});
    }
    Span(const Span&)            = delete;
    Span& operator=(const Span&) = delete;

private:
    Channel          ch_;
    std::string_view name_;
    bool             live_;
    std::chrono::steady_clock::time_point t0_{};
};

// ── 格式化前方-end ──────────────────────────────────────────────
// std::format与编译-time检查格式字符串。
// enabled()/recorded() 门通过宏运行 之前 参数评估，
// 因此禁用的语句永远不会格式化、永远不会转换、永远不会分配。
template <class... Args>
void logf(Channel ch, Level lv, std::string_view site,
          std::format_string<Args...> fmt, Args&&... args) noexcept {
    try {
        emit(ch, lv, site, std::format(fmt, std::forward<Args>(args)...));
    } catch (...) { /* 诊断绝不能扔给呼叫者 */ }
}

} // namespace agentty::logx

// ── 宏 ────────────────────────────────────────────────────────────
// AGT_LOG(wire, Warn, "openai.stream", "connect refused host={}", host);
// 通道名称为裸枚举臂；将裸露的水平臂调平。 `__VA_ARGS__`
// 评估之前的门 Short-circuits — 禁用的日志行
// 花费一个原子 加载。
#define AGT_LOG(ch, lv, site, ...)                                          \
    do {                                                                    \
        if (::agentty::logx::recorded(::agentty::logx::Channel::ch,         \
                                      ::agentty::logx::Level::lv))          \
            ::agentty::logx::logf(::agentty::logx::Channel::ch,             \
                                  ::agentty::logx::Level::lv,               \
                                  site, __VA_ARGS__);                       \
    } while (0)

// AGT_LOGL —与 AGT_LOG 相同，但 LEVEL 是
// 运行时 表达式，用于通过一条语句记录成功和失败的站点（e.g。
// 一个轮次结果，干净时调试，不干净时警告）。防止那些人在
// if/else. 中复制整个 调用
#define AGT_LOGL(ch, lv_expr, site, ...)                                    \
    do {                                                                    \
        const ::agentty::logx::Level agt_lv_ = (lv_expr);                   \
        if (::agentty::logx::recorded(::agentty::logx::Channel::ch, agt_lv_))\
            ::agentty::logx::logf(::agentty::logx::Channel::ch, agt_lv_,    \
                                  site, __VA_ARGS__);                       \
    } while (0)

// AGT_SPAN(persist, "save_thread"); — names the local so two spans in one
// 范围需要不同的名称（有意：嵌套的跨度应该是可见的）。
#define AGT_SPAN(ch, name)                                                  \
    ::agentty::logx::Span agt_span_##ch{::agentty::logx::Channel::ch, name}

// ── AGT_LOG_HOT - 用于每座火力发射场 帧 或更快 时间 ─────────────
//
// 该 COST ASYMMETRY 此 EXISTS TO ENFORCE。
// 不执行 火灾的日志站点的成本约为 0.5 ns（一个宽松的原子 加载；
// 参数从未被评估）。一个 执行 火灾的网站花费了约 4.4 us — 格式化，
// 然后写入 (2)。相差四个数量级，两者的测量值均为 -O2 NDEBUG。
//
// 这个比率就是 "log everywhere" 在发布中免费的原因：默认为警告，
// 因此 Debug/Trace 站点的整个队列位于 0.5 ns 路径上，
// 并且健康会话写入大约两行。这就是 ALSO 为何一个粗心的站点可能
// 会破坏 帧 预算的原因：在 4.4 us 下，每个 -token 路径
// 中的调试日志每 1000 个令牌花费约 4.4 毫秒，而在 60fps
// 的 渲染 路径中，它自己会消耗 帧 的四分之一。
//
// 规则：per-frame-or-hotter路径上的任何内容都记录在跟踪处，
// 从不调试。即使在 AGENTTY_LOG=debug 下跟踪也会关闭，
// 因此对于没有明确要求的每个人来说，站点都会保持在 0.5 ns 路径上。
//
// 在这些站点上使用该宏，规则是 COMPILE ERROR，
// 而不是人们必须记住的约定：
//
//     AGT_LOG_HOT(Perf, Trace, "stream.frame", "build_us={}", us);  // ok
//     AGT_LOG_HOT(Perf, Debug, "stream.frame", "build_us={}", us);  // won't compile
#define AGT_LOG_HOT(ch, lv, site, ...)                                      \
    do {                                                                    \
        static_assert(::agentty::logx::Level::lv                            \
                          == ::agentty::logx::Level::Trace,                 \
            "AGT_LOG_HOT is for sites that fire per frame or faster, and "  \
            "those must log at Trace. An enabled site costs ~4.4us against "\
            "~0.5ns disabled, so a Debug site here would burn the frame "   \
            "budget for every user who ran AGENTTY_LOG=debug. Use Trace, "  \
            "or use plain AGT_LOG if this path is not actually hot.");      \
        AGT_LOG(ch, lv, site, __VA_ARGS__);                                 \
    } while (0)
