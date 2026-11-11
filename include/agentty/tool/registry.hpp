#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <stop_token>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/util/sendable.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/tool/effects.hpp"
#include "agentty/util/io.hpp"

#include <concepts>
#include <type_traits>

// 一工具调用者(tool.hpp):被下面的tools::DispatchKey加为好友。
namespace agentty::tool { struct DynamicDispatch; }

namespace agentty::tools {

// ── Tool 结果类型( std::expected-based) ──────────────────────────────

struct ToolOutput {
    std::string text;
    std::optional<FileChange> change;
    // MULTI-FILE工具的附加文件更改（替换）；上面的`change`是单个
    // -file的情况。两者都提供 diff-review 队列。
    std::vector<FileChange> changes;
    // 对视觉模型浮现的工具进行成像（在图像文件上读取）。
    // 这些成为回合tool_result中的图像块；文本工具为空。
    std::vector<ImageContent> images;
};

// 带类型的错误类型。让 UI 颜色/重试/根据类别而不是字符串-matching
// `detail` 建议。添加新的 变体
// 而不是将语义填充到详细消息中。
enum class ErrorKind : std::uint8_t {
    InvalidArgs,    // schema/validation 失败（缺少字段、空字符串、超出范围）
    NotFound,       // file/dir/symbol不存在
    NotAFile,       // 存在但不是常规文件
    NotADirectory,  // 存在但不是目录
    TooLarge,       // 输入超出大小上限（读取的 1 MiB 等）
    Binary,         // 拒绝将 二进制程序 文件视为文本
    Ambiguous,      // 需要一个的多个匹配（编辑的old_string）
    NoMatch,        // 模式没有匹配任何内容（编辑 old_string，grep）
    InvalidRegex,   // 正则表达式没有 编译
    Network,        // curl / HTTP transport failure
    Spawn,          // 子进程启动失败
    Subprocess,     // 子进程返回非-zero
    Io,             // 通用I/O(write_file失败等)
    OutOfWorkspace, // 路径在配置的 工作区 根之外
    // 被策略拒绝，而不是被文件系统拒绝。与 OutOfWorkspace
    // （模型通过选择另一条路径修复的路径错误）不同，
    // 因为补救措施不同：调用 很好 -formed，
    // 但答案仍然是否定的。 重试 逐字逐句总是错误的，
    // 而读作 I/O 打嗝的那种，恰恰会导致 重试。
    Denied,
    Unknown,        // 未捕获的异常/未知工具
};

[[nodiscard]] std::string_view to_string(ErrorKind k) noexcept;

struct ToolError {
    ErrorKind kind = ErrorKind::Unknown;
    std::string detail;

    // 工厂在调用站点读得很好：
    //     return std::unexpected(ToolError::not_found(path));
    [[nodiscard]] static ToolError invalid_args(std::string d)    noexcept { return {ErrorKind::InvalidArgs,   std::move(d)}; }
    [[nodiscard]] static ToolError not_found(std::string d)       noexcept { return {ErrorKind::NotFound,      std::move(d)}; }
    [[nodiscard]] static ToolError not_a_file(std::string d)      noexcept { return {ErrorKind::NotAFile,      std::move(d)}; }
    [[nodiscard]] static ToolError not_a_directory(std::string d) noexcept { return {ErrorKind::NotADirectory, std::move(d)}; }
    [[nodiscard]] static ToolError too_large(std::string d)       noexcept { return {ErrorKind::TooLarge,      std::move(d)}; }
    [[nodiscard]] static ToolError binary(std::string d)          noexcept { return {ErrorKind::Binary,        std::move(d)}; }
    [[nodiscard]] static ToolError ambiguous(std::string d)       noexcept { return {ErrorKind::Ambiguous,     std::move(d)}; }
    [[nodiscard]] static ToolError no_match(std::string d)        noexcept { return {ErrorKind::NoMatch,       std::move(d)}; }
    [[nodiscard]] static ToolError invalid_regex(std::string d)   noexcept { return {ErrorKind::InvalidRegex,  std::move(d)}; }
    [[nodiscard]] static ToolError network(std::string d)         noexcept { return {ErrorKind::Network,       std::move(d)}; }
    [[nodiscard]] static ToolError spawn(std::string d)           noexcept { return {ErrorKind::Spawn,         std::move(d)}; }
    [[nodiscard]] static ToolError subprocess(std::string d)      noexcept { return {ErrorKind::Subprocess,    std::move(d)}; }
    [[nodiscard]] static ToolError io(std::string d)              noexcept { return {ErrorKind::Io,            std::move(d)}; }
    [[nodiscard]] static ToolError out_of_workspace(std::string d) noexcept { return {ErrorKind::OutOfWorkspace, std::move(d)}; }
    [[nodiscard]] static ToolError denied(std::string d)          noexcept { return {ErrorKind::Denied,         std::move(d)}; }
    [[nodiscard]] static ToolError unknown(std::string d)         noexcept { return {ErrorKind::Unknown,       std::move(d)}; }

    // "[not found] path/to/file" — 当
    // UI 不关心类型分支时的默认字符串化。只需要原始消息的工具直接读取
    // `.detail`。
    [[nodiscard]] std::string render() const;
};

using ExecResult = std::expected<ToolOutput, ToolError>;

// ── Tool 定义 ──────────────────────────────────────────────────────

enum class ToolOrigin : std::uint8_t { Native, Mcp, Passthrough };
enum class OutputTruncation : std::uint8_t { Head, Tail, HeadTail };

// ── 工具调用的背景 ────────────────────────────────────────────────────
//
// 除了参数之外，工具调用携带的所有内容均显式传递给 ToolDef::execute。
// 调度程序构建它；该工具读取它；线程上没有停放任何内容。
// 这些曾经是由 RAII 作用域安装的 thread_locals，
// 每当工作转移到另一个线程时（shell 输出停止流式传输
// 并且 Esc 停止为某个版本工作），它们就会悄悄丢失。
// 副本携带的能力指针为NOT：复制上下文，
// 为新的调用生成了新的上下文，能力属于原来
// 设置的分派。移动保留它（相同调用）。
class CallCapability {
public:
    CallCapability() = default;
    CallCapability(const CallCapability&) noexcept {}
    CallCapability& operator=(const CallCapability&) noexcept { p_ = nullptr; return *this; }
    CallCapability(CallCapability&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    CallCapability& operator=(CallCapability&& o) noexcept {
        p_ = std::exchange(o.p_, nullptr);
        return *this;
    }
    void set(const maya::unlocked* c) noexcept { p_ = c; }
    [[nodiscard]] const maya::unlocked* get() const noexcept { return p_; }
private:
    const maya::unlocked* p_ = nullptr;
};

struct CallContext {
    using Sink = std::function<void(std::string_view snapshot)>;

    // 工具工作线程的`unlocked`，持续时间为ONE execute() 调用。
    //
    // mcp-cpp通过其修复的签名的接口运行agentty的本机工具和调用回到agentty
    // (内存存储、子代理运行器、exec)；唯一跨越该边界
    // 的是这个上下文（Call::host）。
    // DynamicDispatch::execute_with 从它
    // 所获得的能力中设置它并在返回之前清除它，因此它永远不会比证明它的
    // 调用 更长久，并且永远不会到达另一个 线程 （复制的上下文会
    // 删除它：CallCapability）。
    CallCapability cap;
    [[nodiscard]] const maya::unlocked& capability() const {
        if (!cap.get()) throw std::logic_error("tool callback outside a dispatched call");
        return *cap.get();
    }

    // 每次直播输出为UI、整个缓冲区。空：没有人看。
    Sink progress;
    // 调用服务于谁的上下文：主对话为空，子代理的运行ID在其中，
    // 因此子代理永远不会读取其父代理的
    // 此前已经读取过此文件。
    std::string reader;
    // 调用服务于线程。子代理将其作为其网关会话 ID 发送，
    // 因此其请求与父代理一致 (#118)。空：无。
    std::string conversation;
    // 当这些停止中的任何一个（Esc、较新的转动、关闭、
    // 封闭子代理运行）时，调用是已取消。
    std::vector<std::stop_token> cancel;
    // 正文已经存在于调用服务的会话中的技能。
    std::vector<std::string> active_skills;
    // 嵌套：有多少个子代理包围这个调用，以及它们中的任何
    // 一个设置的最早截止日期（嵌套运行只能缩短它）。
    int depth = 0;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    // 服务特定文件夹（ACP会话）的调用的工作区范围。
    // 空：进程-wide 工作区和cwd。
    std::string              workspace;     // 访问边界
    std::vector<std::string> extra_roots;   // 调用可能触及的更多根源
    std::string              project;       // 相对路径和外壳开始的位置
    // Ask用户通过主机具有的任何UI进行结构化输入(
    // MCP服务器的elicitation/create)。参数和结果为 MCP
    // -shaped JSON。空：无UI可询问，请求被拒绝。
    std::function<nlohmann::json(const nlohmann::json&)> elicit;
    // 子代理运行完成；这是它的文字记录（编辑者可以将其
    // 保留为自己的会话）。空：掉落。
    std::function<void(const ::agentty::Thread&)> on_subagent_done;

    void emit(std::string_view snapshot) const { if (progress) progress(snapshot); }
    [[nodiscard]] bool cancelled() const noexcept {
        for (const auto& t : cancel) if (t.stop_requested()) return true;
        return false;
    }
    // 等待至`d`，在调用为已取消（或`also`停止）的瞬间唤醒。
    // 当 已取消 时为真。
    [[nodiscard]] bool wait(std::chrono::milliseconds d, std::stop_token also = {}) const;
    // 当 `cancel` 中的任何一个停止时停止的一个令牌。
    // 返回的源拥有 回调，因此在使用令牌时使其保持活动状态。
    [[nodiscard]] std::shared_ptr<std::stop_source> merged_stop() const;
    [[nodiscard]] bool skill_active(std::string_view name) const noexcept {
        for (const auto& n : active_skills) if (n == name) return true;
        return false;
    }
};

// ── 工具( E) 正文 : 可调用 仅 由 分派器 ────────────────────────────────────
//
// 工具调用需要的每条安全规则被应用在ONE处、
// tool::DynamicDispatch::execute_with:
// 交给正文的能力、使两个写入器远离一个文件的写入通道、
// 异常遏制、输出预算、已取消-before-start检查。
// 调用 ToolDef 闭包的调用者直接跳过了
// 所有这些闭包；这就是并发编辑相互丢失的原因。
//
// 因此，闭包被包装在一个类型中，该类型的调用运算符采用只有分派器才能
// 生成的密钥（DispatchKey的构造函数对其来说是私有的）。
// 分配正文并测试是否安装如前； RUNNING
// 它在任何地方，但通过 分派 不会 编译。
class DispatchKey {
    DispatchKey() = default;
    friend struct ::agentty::tool::DynamicDispatch;
};

class ToolBody {
public:
    using Fn = std::function<ExecResult(const maya::unlocked& cap,
                                        const nlohmann::json& args,
                                        const CallContext& ctx)>;
    ToolBody() = default;
    template <class F>
        requires (!std::same_as<std::remove_cvref_t<F>, ToolBody>)
              && std::constructible_from<Fn, F>
    ToolBody(F f) : fn_(std::move(f)) {}          // NOLINT: implicit on purpose
    template <class F>
        requires (!std::same_as<std::remove_cvref_t<F>, ToolBody>)
              && std::constructible_from<Fn, F>
    ToolBody& operator=(F f) { fn_ = std::move(f); return *this; }

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(fn_); }

    // 仅分派：参见DispatchKey。
    ExecResult operator()(DispatchKey, const maya::unlocked& cap, const nlohmann::json& args,
                          const CallContext& ctx) const {
        return fn_(cap, args, ctx);
    }
private:
    Fn fn_;
};

struct ToolDef {
    ToolName    name;
    std::string description;
    nlohmann::json input_schema;

    // 来源稳定。动态 MCP 工具绝不允许伪装成本机工具；
    // 原产地还驱动 目录 替换和 UX 标签。
    ToolOrigin origin = ToolOrigin::Native;
    std::string origin_id; // 空为原生；否则配置 MCP 服务器 ID

    // 运行时元数据存在于ToolDef上，
    // 因此动态工具接受与编译-time本机工具相同的safety/performance处理。
    EffectSet scheduling_effects;
    int max_output_chars = 30'000;
    OutputTruncation output_truncation = OutputTruncation::HeadTail;
    std::chrono::milliseconds timeout{60'000};
    bool always_expose = false; // 原生的、固定的或MCP 目录发现

    // 广告 vs 分派 — 正式的两个不同的集合。
    // `advertise = false` 保留 传输 请求的工具数组
    // 中的工具 OUT，而 分派 (find/execute) 仍然解析它。
    // 消费者是实现 PROXY-advertised 模式定义 的直通
    // 工具（LiteLLM + 余量将 `headroom_retrieve`
    // 注入到我们的 pre-call 钩子中的请求中）：代理拥有广告，
    // agentty 拥有执行——我们自己做广告将使 模式定义 翻倍。
    // 所有其他工具都保留默认值，没有任何变化。
    bool advertise = true;

    // Non-empty：该工具的模式定义将使提供者拒绝WHOLE请求
    // （错误的参数名称，无效的JSON 模式定义），因此它永远不会继续
    // 在传输上。它保留在 目录 中，因此可以显示原因并且其服务器的其余
    // 部分继续工作。参见tool/schema.hpp.
    std::string skipped;
    // MCP:该工具在其服务器上的自己的名称(origin_id
    // 是服务器)。 传输 名称可能会被缩短和散列以适合提供商；
    // 这是用户和服务器所知道的名称。
    std::string origin_tool;

    // Anthropic的fine-grained工具流式传输 标志(Claude 4.6
    // 上的GA，在旧型号上由测试版`fine-grained-tool-streaming-2025-05-14`控制)。
    // 设置后，API 数据流 `input_json_delta` 事件
    // 令牌-by-token 作为模型发出工具输入，而不是以较大的块缓冲和滴
    // 流整个工具输入。决定性的`write`（multi-KB内容正文）：
    // 如果没有这个，大写操作会从〜60 tok/s下降到〜1 tok/s，因为Anthropic的边缘保留字节
    // 用于批处理。当启用 `tengu_fgts` statsig 或
    // `CLAUDE_CODE_ENABLE_FINE_GRAINED_TOOL_STREAMING=1` 时，CC 设置此项；
    // Zed 将其设置为通过 `supports_input_streaming()` 选择加入的 -tool。
    bool eager_input_streaming = false;

    // 描述该工具对世界的可观察影响的能力标签。 权限 策略仅读取这些
    // 内容 — 没有 per-tool 覆盖。在构建ToolDef（e.g.
    // `t.effects = {Effect::ReadFs};`）时设置；
    // 将其默认为空）表示
    // 此工具是纯操作，始终无需请求权限。
    EffectSet effects;

    // 在工具 工作线程 线程 上运行，其在工具 调用 之间不保留 锁：
    // 正文获取线程的`unlocked`。工具可能会阻塞（子进程、
    // MCP 轮-trip、帮助器范围）并按排名顺序获取任何 锁；
    // 该类型证明了 分派器 永远不会从 锁 正文 内部出现 调用。
    // 只有tool::DynamicDispatch可以运行它（ToolBody）。
    using ExecuteFn = ToolBody;
    ToolBody execute;

    // 同意决定所依赖的一切的稳定指纹：
    // 这是哪个工具，它声称要做什么，它接受什么，
    // 以及它可能达到什么目的。故意 不 执行闭包（不可散列）
    // 而不是调整旋钮（预算和 超时 不会改变您同意的内容）
    // to).
    //
    // 其存在原因：针对工具NAME记录了权限 授权，并且对于MCP工具，
    // 服务器拥有该名称。服务器可以随时发送 tools/list_changed
    // 并将 `mcp__x__search` 重新定义为完全不同的
    // 东西 - "rug pull" MCP 规范和 OWASP MCP
    // 备忘单都 调用 出来，其规定的缓解措施是在更改时通过哈希和
    // re-prompt 来固定已审阅的定义。相同的值接近较窄的
    // gate/execute 竞态，其中 目录 在 "user approved X"
    // 和 "run X" 之间重建。
    //
    // agentty已经在其规范哈希上门控了项目-scoped stdio SERVER。
    // 这就是工具上一层的想法。
    [[nodiscard]] std::uint64_t definition_hash() const {
        // FNV-1a优于规范的渲染。 nlohmann 的 dump() 将对象
        // 键排序为默认 (map-backed) JSON 类型，因此相同的
        // 模式定义 总是 渲染 相同的 字节。
        std::uint64_t h = 1469598103934665603ull;
        const auto feed = [&h](std::string_view s) {
            for (unsigned char c : s) {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\x1f';           // 字段分隔符："ab"+"c"!="a"+"bc"
            h *= 1099511628211ull;
        };
        feed(name.value);
        feed(description);
        feed(input_schema.is_null() ? std::string{} : input_schema.dump());
        feed(origin_id);
        const char origin_byte = static_cast<char>(origin);
        feed(std::string_view{&origin_byte, 1});
        const auto fx = static_cast<std::uint64_t>(effects.bits());
        feed(std::to_string(fx));
        return h;
    }
};

// 目录作为每个读者共享的一个不可变对象。只要读取器使用其中的 ToolDef，
// 它就会一直持有该句柄（分派 可能会运行几分钟）；重新加载会发布一个新的，
// 而旧的会随着最后一个读者而消亡。 maya::shared，
// 而不是 shared_ptr：只有 make() 构建了一个，
// 并且它检查矢量是否冻结，因此没有人可以为其保留 可变 别名。
using Catalog = maya::shared<std::vector<ToolDef>>;

[[nodiscard]] const std::vector<ToolDef>& native_registry();

// 在 传输 上为 此 回合做广告的工具集：本机工具加上 池 现在知道
// 的每个 MCP 工具（服务器的 `tools/list_changed`
// 落在下一个 调用 上）。第一个调用可能连接外部MCP，
// 需要几秒； find() 从来不会。
[[nodiscard]] Catalog wire_tools();
// 加热 目录（如果已配置，则连接 MCP）。与 wire_tools() 相同，
// 但结果被丢弃；对于支付冷成本off-thread的init任务。
inline void warm_registry() { (void)wire_tools(); }

// 按名称查找工具（旧别名解析）。 ToolDef 的
// 寿命与返回的 目录 的寿命一样长，因此这对会一起返还；
// 保留指针的调用者将句柄保留在其旁边。
struct Found {
    std::optional<Catalog> catalog;      // 未找到时为空
    const ToolDef* def = nullptr;
    [[nodiscard]] explicit operator bool() const noexcept { return def != nullptr; }
    const ToolDef* operator->() const noexcept { return def; }
    const ToolDef& operator*()  const noexcept { return *def; }
    // 不在目录中的定义（测试自己的，只是-built MCP定义）：
    // 复制到 one-entry 目录 中，因此句柄仍然拥有它所指向的内容。
    [[nodiscard]] static Found of(ToolDef d) {
        auto c = Catalog::make(std::vector<ToolDef>{std::move(d)});
        const ToolDef* p = &c->front();
        return Found{std::move(c), p};
    }
};
[[nodiscard]] Found find(std::string_view name);

// 可操作的"unknown tool"诊断。裸露的 "unknown tool: X"
// 使模型陷入困境——当 传输 前面的 LiteLLM-style 代理在我们的
// 旁边宣传 ITS 自身 工具（e.g.`headroom_retrieve`）时，
// 在现场观察到：模型调用了一个，出现了无用的错误，然后重试。命名真正的目录会将
// 失败变成一个-shot修正（"that tool isn't mine; here's what is"）。
// 在 registry.cpp 中定义，
// 位于它枚举的 快照 旁边。
[[nodiscard]] std::string unknown_tool_error(std::string_view name);

// 哪些工具处理请求，以及模型如何找到其余部分，是 tools::plan
// (tool_plan.hpp)。每个启用的工具都可以通过名称进行调用。

// agentty的设置决定了ANY消费者存在哪些其本机工具：
// 该模型通过`agentty mcp-serve`在传输(
// tools::plan)和其他MCP客户端上进行。一个谓词，因此两者永远不会不一致。
// 今天唯一的规则是设置 → Web 搜索：web_search 关闭，
// 保留 NATIVE web_search（碰巧共享该名称的外部
// MCP 工具不受 agentty 的管辖）。纯过载带开关；
// 另一个阅读实时政策。
[[nodiscard]] bool withheld_by_policy(const ToolDef& tool, bool web_search_enabled);
[[nodiscard]] bool withheld_by_policy(const ToolDef& tool);

// MCP工具-list生成计数器，通过工具命名空间浮出水面，因此调用者（ACP服务器、
// 传输行走）不需要链接 mcp TU或知道MCP是否被编译。当MCP被禁用
// 或没有服务器具有re-listed时，返回0。每个 `tools/list_changed`
// （和 resources/prompts）上都有碰撞。
[[nodiscard]] unsigned long mcp_generation() noexcept;

// Live-reload MCP插件从mcp.json到CURRENT会话中：
// 重建连接池(生成新的-added服务器，删除已删除的服务
// 器)并重新-project 传输 目录，因此新工具可以
// 立即使用——无需重新启动。返回重新加载后连接的服务器数量。
// 阻塞（服务器握手）； TUI 从 UI 线程 运行。
[[nodiscard]] std::size_t reload_mcp_plugins();

// 强制传输 目录在下次访问WITHOUT re-spawning任何服务器时re-project。
// 由 per-tool enable/disable 切换 使用：服务器保持连接；
// 只有投影过滤器（配置 tools.exclude，实时读取）发生变化，
// 因此 re-spawn 会很浪费，并且在快速 切换 下 — 竞态
// -prone。便宜且同步；从 UI 线程 到 调用 是安全的。
void invalidate_mcp_catalog(Io);


} // namespace agentty::tools

// ToolBody是仅被调用的闭包。它捕获的内容（提供者，
// MCP 池）自身同步，或者什么都没有；没有人从它建造的地方借用。
// 因此，一个 ToolDef 可以移动到另一个 线程，
// 并且许多 线程 可以一次读取一个。
MAYA_SENDABLE(agentty::tools::ToolBody);
MAYA_FROZEN(agentty::tools::ToolBody);
// Found的指针指向它所携带的冰雪奇缘目录，
// 因此移动该对会移动所有权：没有任何借用的东西被留下。
MAYA_SENDABLE(agentty::tools::Found);

// 拥有的值、stop_tokens (线程-safe句柄）和`progress`，
// 分派器构建的闭包以发布到接收器或受保护的缓冲区中。工具 调用 在
// 工作线程 上运行，因此上下文随之而来。
MAYA_SENDABLE(agentty::tools::CallContext);
