#pragma once
// agentty对话域——描述聊天的纯值类型。
//
// 没有I/O、没有UI、没有流式传输状态机。  `Thread` 是
// 被持久化、发送到提供者并显示的内容。  `Message` 和
// `ToolUse` 是其构建块。

#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/domain/id.hpp"
#include "agentty/domain/rag_mode.hpp"
#include "agentty/domain/lazy_bytes.hpp"
#include "agentty/domain/smart_mode.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/util/base64.hpp"
#include "agentty/util/sendable.hpp"

namespace agentty {

enum class Role : std::uint8_t { User, Assistant, System };

[[nodiscard]] constexpr std::string_view to_string(Role r) noexcept {
    switch (r) {
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::System:    return "system";
    }
    return "?";
}

// 用户消息OR和tool_result携带的视觉图像。 字节 被 RAW
// （不是 base64）并且惰性地持有——请参阅上面的 LazyBytes
// 了解原因以及确保其安全的正确性规则。
class ImageContent {
public:
    using Source = LazyBytes::Source;

    std::string media_type;  // "image/png"、"image/jpeg"、"image/webp"、"image/gif"

    ImageContent() = default;
    ImageContent(std::string mt, std::string raw_bytes)
        : media_type(std::move(mt)), data_(std::move(raw_bytes)) {}

    static ImageContent lazy(std::string mt, Source src) {
        ImageContent img;
        img.media_type = std::move(mt);
        img.data_      = LazyBytes::lazy(std::move(src));
        return img;
    }

    [[nodiscard]] const std::string& bytes() const { return data_.bytes(); }
    void set_bytes(std::string raw) {
        data_.set_bytes(std::move(raw));
        // 新的 存储单元，而不是重置：之前制作的副本保留它们仍然保留的 字节
        // 的编码（镜像 LazyBytes::set_bytes）。
        b64_ = std::make_shared<B64Cell>();
    }
    [[nodiscard]] const Source& source() const noexcept { return data_.source(); }
    [[nodiscard]] bool materialised() const noexcept { return data_.materialised(); }

    // bytes()的base64，编码ONCE并由该图像的每个副本共享。
    // 传输 编码器需要 base64（Anthropic 的 `data`、
    // OpenAI 的数据：URL），而模型状态保留原始 字节
    // 以避免 +33% 内存，因此每个请求都用于从头开始重新
    // -encode 每个图像：线程 中的一些屏幕截图是重建的
    // base64 的兆字节在 每个 轮上，它主导了任何包含图像的 线程
    // 上的 正文 编码。
    //
    // 存储单元是shared_ptr，就像LazyBytes自己的一样，
    // 因此per-turn 线程 快照与实时模型共享编码而不是复制或重新计算它。
    // 被 set_bytes() 无效； lazily-materialised
    // 图像在首次使用时进行编码，这也是其 字节 第一个 加载 时的情况。
    [[nodiscard]] const std::string& base64() const {
        // 存储单元是急切地创建的（便宜的，无编码），
        // 使得指针本身永远不会被同时写入——只有它内部的
        // once_flag是并发写入的，这就是它的用途。
        // 编码是纯粹的，因此并发读者都观察到相同的字节。
        maya::once(b64_->once, [&] { b64_->text = util::base64_encode(bytes()); });
        return b64_->text;
    }

    // 保留安装了图像解析器的现有调用站点仍然为编译；
    // 这两种类型共享一个解析器，因为两者都针对同一个 Blob
    // 存储解析相同类型的 Source。
    using Resolver = LazyBytes::Resolver;
    static void set_resolver(Resolver r) noexcept { LazyBytes::set_resolver(r); }

private:
    struct B64Cell {
        std::once_flag once;
        std::string    text;
    };
    LazyBytes data_;
    // 决不为空。副本共享 存储单元（以及编码）； set_bytes
    // 安装了 FRESH，以便其他副本保留其已-valid 文本。
    std::shared_ptr<B64Cell> b64_ = std::make_shared<B64Cell>();
};

// `ToolUse::Status`是和类型。每个替代方案都
// 拥有在该状态下实际有意义的数据 - `Running`
// 保存实时进度 缓冲区，`Done`/`Failed` 保存最终输出，
// 终端 状态保存完成时间等。将这些字段内联存储在 `ToolUse`
// （之前的设计）上意味着每个读者必须记住哪些字段在哪个
// 状态下有效 - 变体 替代方案使 不变量 牢不可破。
//
// Wall-clock标记使用steady_clock(不是system_clock)，
// 因此用户改变系统时钟mid-execution不会产生负的经过时间。
struct ToolUse {
    // 待处理 带有 started_at，因为卡在 args-streaming
    // 窗口期间也显示了实时已用计数器 — Anthropic 数据流，
    // 工具输入为增量，长 `content` 字段可能需要几秒钟，
    // 因此冻结计时器直到执行开始读取为 "stuck"。时间戳在 待处理
    // → 运行转换后仍然存在（kick_pending_tools 通过
    // started_at() 读取它）。
    struct Pending  { std::chrono::steady_clock::time_point started_at{}; };
    struct Approved { std::chrono::steady_clock::time_point started_at{}; };
    struct Running {
        std::chrono::steady_clock::time_point started_at{};
        // 运行工具的实时stdout+stderr 快照。
        // 当状态为“正在运行”时显示在卡中，
        // 以便用户立即看到进度，而不是等到整个命令完成。
        std::string progress_text;
        // Wall-clock的最新进展快照。 ung-syscall 楔形网
        // （update/meta.cpp Tick）从最大（
        // started_at，last_progress_at）而不是单独的
        // started_at 测量活跃度，因此持续发出进度的 long
        // -but-HEALTHY 工具（通过工具 调用 搅拌的子代理，
        // multi-minute 构建 流式传输 输出）永远不会被扁平楔形帽断头台
        // - 只有一个真正沉默过去的工具帽子绊倒了它。归零直至第一个
        // ToolExecProgress 落地；楔形网将零视为 "no progress yet"，
        // 然后回落至 started_at。
        std::chrono::steady_clock::time_point last_progress_at{};
        // EXECUTION实际开始的时间——与卡诞生的时间不同（
        // started_at，上面）。
        //
        // 这是两个不同的问题，一个字段无法同时回答这两个
        // 问题：
        //
        //   started_at：卡片已存在多久？用于显示。
        //                     标记为StreamToolUseStart，
        //                     因此经过的计时器也覆盖arg-streaming；
        //                     将其冻结在那里读取为 "stuck"。
        //   executing_since：实际运行了多久？用于存活判定。
        //                     工具实际发货时盖章。
        //
        // 将它们合并起来是问题#40：等待权限的工具没有运行，
        // 但楔形网是从started_at测量的，
        // 因此HUMAN花费的时间是根据该工具的330秒预算来计费的。
        // 离开六分钟，批准，第一个 Tick 杀死了一个没有执行
        // 任何指令的命令 - 报告为 "ran 400s with no progress"，
        // 这对于卡来说是正确的，对于工具来说是
        // 错误的。
        //
        // 零表示"not recorded"（较旧的线程，
        // 直接构建运行测试）；读者会回退到 started_at，
        // 这是 -fix 之前的行为，只要不存在批准差距，就会进行纠正。
        std::chrono::steady_clock::time_point executing_since{};
        // 每个 分派 (next_tool_exec_seq) 都是唯一的。 工作线程 在其
        // progress/result 上与其相呼应，因此 过期 工作线程 无法命中重复使用的 ID。
        std::uint64_t exec_seq = 0;

        // 应当测量"has this gone quiet?"的时刻。
        // 每个活跃消费者都会经历这个而不是到达字段，
        // 因此未来的第三时钟不能是half-adopted。
        [[nodiscard]] std::chrono::steady_clock::time_point
        liveness_origin() const noexcept {
            auto base = executing_since.time_since_epoch().count() != 0
                      ? executing_since : started_at;
            if (last_progress_at.time_since_epoch().count() != 0
                && last_progress_at > base)
                base = last_progress_at;
            return base;
        }
    };
    struct Done {
        // 字段ORDER被固定：跨测试和更新代码绑定{开始、完成、输出[、
        // 图像]}位置的约70个聚合-initialisation站点。
        // 因此，executing_since 会默认变为 最后 —
        // 旧形状仍然有效并将其绑定到 "not recorded"。
        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point finished_at{};
        std::string output;
        // 对选择呈现视觉模型的工具进行成像（图像文件上的e.g.
        // `read`）。原始字节； 传输 将它们编码为 调用 的
        // tool_result 内的图像块，就在文本之后。文本工具为空。
        std::vector<ImageContent> images;
        // EXECUTION窗口开始(分派)，
        // 从结算时的Running::executing_since复制。
        // 零 = "not recorded"（较旧的保留 线程，
        // 测试直接构建完成）；消费者会回落到 started_at——只要
        // 分派 立即发生，i.e 就是正确的。每当没有批准差距时。
        std::chrono::steady_clock::time_point executing_since{};
    };
    struct Failed {
        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point finished_at{};
        std::string output;
        // 与完成相同的约定：最后、默认、零=未记录。
        std::chrono::steady_clock::time_point executing_since{};
    };
    struct Rejected {
        std::chrono::steady_clock::time_point finished_at{};
    };
    using Status = std::variant<Pending, Approved, Running, Done, Failed, Rejected>;

    ToolCallId     id;
    ToolName       name;
    nlohmann::json args;
    // 工具调用的`extra_content`来自OpenAI-compatible 数据流，
    // 作为-is发回。 Gemini 将其思想签名放在这里（
    // extra_content.google.thought_signature）；
    // 在下一个请求中省略它是 Gemini 的 400。 3. 当提供商未发送任何内容时为空。
    nlohmann::json extra_content;
    std::string    args_streaming;
    // input_json_delta期间实时预览re-parse的节流。
    // 预览路径关闭部分JSON，在整个增长的缓冲区上运行`nlohmann::json::parse`，
    // 提取长字段（写入`content`，编辑`edits[*].old_text`
    // /`new_text`）；在每个微小增量上执行此操作的时间复杂度为 O(n²)，
    // 并且是 multi-KB 写入时的主要 CPU 成本 — 可见，
    // 当 传输 运行正常时，UI "hanging" 可见。如果自上一次预览
    // 以来过去的时间少于约 250 毫秒，状态更新函数 会跳过预览 re-parse。
    std::chrono::steady_clock::time_point last_preview_at{};
    // 字节偏移到`args_streaming`中，其中流式传输 long
    // -string字段的*值*的起始`"`开始——i.e。
    // 刚刚过去 `"content":"` 用于写入 / `"command":"`
    // 用于 bash。一旦我们找到它，后续的预览滴答声就会从这里恢复解码，
    // 而不是每次都从 字节 0 重新 -scanning 完整的 缓冲区。
    // Append-onlyargs_streaming的增长保持偏移有效。
    // 0表示"not located yet"；设置时偏移量始终 > 0，
    // 因为字段名称 + `":"` 至少为 4 字节。
    std::size_t    stream_sniff_offset = 0;
    // 缓存最后预览通道的 end-of-buffer 大小。如果 缓冲区 从
    // 那时起就没有增长，则没有任何新内容可显示 - 完全跳过嗅探 + set_arg 对。
    // 廉价的 "am I still the same?" 检查，
    // 当模型在 -stream 中期暂停时，可以消除大量 末尾-identical
    // re-renders。
    std::size_t    stream_sniff_size   = 0;
    // 增量解码缓存为流式传输 long-string值(写入的`content`，
    // 如果它变大则为未来的bash `command`，等等)。
    // 在每个增量上，args_streaming恰好穿过工具的生命周期一次，
    // 而不是re-decoding[stream_sniff_offset，结束)。
    // 在 数据流 期间，累积成本从 O(N2) 下降到 O(N)。
    //
    // stream_decoded_value保存解码的预览末尾(上限——参见
    // kStreamingPreviewCap)；一旦 末尾 超过 2× 上限，
    // 前缀就会被修剪，因此 内存 即使在 10 次 MB 写入时也保持有界。
    // stream_decode_through跟踪我们消耗的
    // args_streaming中的字节位置；下一个解码过程将在那里继续。
    mutable std::string stream_decoded_value;
    std::size_t         stream_decode_through = 0;
    bool                stream_decode_closed  = false;   // 价值收盘“见”
    // edit/todo 结构化预览的摊销游标。这两个分支不能使用 write
    // 的增量缓存-offset 解码（它们镜像不断增长的 ARRAY 对象，
    // 而不是一个长字符串），因此它们依赖于 try_parse_partial(
    // args_streaming)，即每个 调用 的 O(|args_streaming|)。
    // 每约 120 毫秒运行一次，与大型多重编辑 调用 相比是二次方。相反，
    // 只有当 缓冲区 自上次 解析 以来增长了 kStreamParseGrowth
    // 字节 时，我们才重新-parse - 预览最多滞后了 字节（难以察觉），
    // 但 解析 COUNT 从 one-per-tick 下降到 one
    // -per-growth-window，从而将累积成本削减了该因素。
    // 保留最后一个结构的 解析 处的 args_streaming 尺寸；
    // 0 = 从未解析。
    std::size_t         stream_parse_through = 0;
    // 当 StreamToolUseEnd / finalize_turn
    // 检测到 传输 在字符串值内结束时设置。 finalize_turn
    // 的 重试 循环将其视为缺失的 -required-field 截断：
    // 弹出 in-flight 辅助占位符并在同一个 ctx 上
    // 静默重新启动（由 kMaxTruncationRetries
    // 限制）。仅当 重试 预算用完后，工具才会显示为“失败”。
    // 流式传输-time仅划痕——不保留； 加载 上的默认-init
    // 是正确的。
    bool           stream_mid_string_truncated = false;
    // 同意的工具定义——或者由用户在 提示词 处给予同意，
    // 或者由策略在 auto-allowed 处给予同意。在执行前立即
    // 进行检查，因此在之间重建的 目录（tools/list_changed，
    // mcp 重新加载）无法在批准的名称下交换不同的工具。
    // 0表示没有记录任何内容并且跳过检查。
    std::uint64_t  approved_def_hash = 0;
    Status         status   = Pending{};

    // ── 国家上游 ─────────────────────────────────────────────────
    [[nodiscard]] bool is_pending()  const noexcept { return std::holds_alternative<Pending>(status);  }
    [[nodiscard]] bool is_approved() const noexcept { return std::holds_alternative<Approved>(status); }
    [[nodiscard]] bool is_running()  const noexcept { return std::holds_alternative<Running>(status);  }
    [[nodiscard]] bool is_done()     const noexcept { return std::holds_alternative<Done>(status);     }
    [[nodiscard]] bool is_failed()   const noexcept { return std::holds_alternative<Failed>(status);   }
    [[nodiscard]] bool is_rejected() const noexcept { return std::holds_alternative<Rejected>(status); }
    [[nodiscard]] bool is_terminal() const noexcept { return is_done() || is_failed() || is_rejected(); }

    // is_terminal()的详尽引脚。冻结门(run_is_freezable)
    // 和live-tail 缓存-key门通过调用is_terminal()来决定
    // 当前内容是否可以安全冻结为不可变的回滚历史？
    // 该谓词通过排除
    // -flight 中的三个状态 (Pending/Approved/Running)
    // 来枚举三个已确定状态 (Done/Failed/Rejected)。
    // 此处添加的第七个状态变体如果没有对其进行分类，将默认默认为non
    // -terminal——最好的情况是永远不会冻结（滞后）的运行，
    // 最坏的情况是，如果在其他地方分类为终端-by-accident，
    // 则加载动画永远固定在回滚缓冲区中。固定宽度，以便遗漏是构建错误，
    // 而不是 运行时 幽灵。
    static_assert(std::variant_size_v<Status> == 6,
                  "ToolUse::Status gained/lost a variant — re-derive "
                  "is_terminal() (Done/Failed/Rejected) and classify the "
                  "new state as terminal or in-flight before bumping this.");

    // ── 国 家 -safe 接入器 ─────────────────────────────────────────────
    // 返回当前状态的相关字段，或者当替代项不携带时返回 empty/default。
    // 视图可以依赖这些，而无需首先检查鉴别器。
    // 通过 const-ref 返回会保持在旧字段上执行
    // `.empty()` / `.substr(…)` 的现有
    // 调用 站点不变。
    [[nodiscard]] const std::string& output() const noexcept {
        static const std::string empty;
        if (auto* d = std::get_if<Done>(&status))   return d->output;
        if (auto* f = std::get_if<Failed>(&status)) return f->output;
        return empty;
    }
    // 对浮出水面的完整工具进行成像（在图像文件上读取）。对于每个非
    // -Done 状态和每个文本工具为空。 传输 渲染 这些作为 调用
    // 的 tool_result 内的图像块。
    [[nodiscard]] const std::vector<ImageContent>& done_images() const noexcept {
        static const std::vector<ImageContent> empty;
        if (auto* d = std::get_if<Done>(&status)) return d->images;
        return empty;
    }
    [[nodiscard]] const std::string& progress_text() const noexcept {
        static const std::string empty;
        if (auto* r = std::get_if<Running>(&status)) return r->progress_text;
        return empty;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point started_at() const noexcept {
        return std::visit([](const auto& s) -> std::chrono::steady_clock::time_point {
            if constexpr (requires { s.started_at; }) return s.started_at;
            else return {};
        }, status);
    }
    [[nodiscard]] std::chrono::steady_clock::time_point finished_at() const noexcept {
        return std::visit([](const auto& s) -> std::chrono::steady_clock::time_point {
            if constexpr (requires { s.finished_at; }) return s.finished_at;
            else return {};
        }, status);
    }

    // 当执行ACTUALLY开始时（分派)，与started_at
    // ()（卡诞生）不同。 Done/Failed 带有
    // 自己的 executing_since — 当工具稳定时从运行中复制；
    // 旧的 终端 状态回退到 started_at，
    // 匹配 #40 之前的行为，只要不存在批准差距，该行为就是正确的。
    [[nodiscard]] std::chrono::steady_clock::time_point executing_since() const noexcept {
        return std::visit([](const auto& s) -> std::chrono::steady_clock::time_point {
            if constexpr (requires { s.executing_since; } && requires { s.started_at; }) {
                return s.executing_since.time_since_epoch().count() != 0
                     ? s.executing_since : s.started_at;
            }
            else if constexpr (requires { s.started_at; }) return s.started_at;
            else return {};
        }, status);
    }

    // EXECUTION-window读取统计信息归并和持久化序列化器。
    // 这两个用例具有 OPPOSITE 语义：
    //
    //   executing_since()→活跃度显示：
    //                         这项操作实际运行了多久？
    //                         回退到旧状态的卡诞生（无批准间隙），
    //                         因此经过的计时器保持一个值。
    //   exec_window_at() → 会计：只有一个RECORDED
    //                         执行窗口是一个事实。从未发送过的工具（已拒绝，
    //                         已抢救）-as- "not run",
    //                         参数-parse失败）没有
    //                         executing_since且不得报告
    //                         任何内容 —
    //                         回到其卡上-birth邮票将账单批准等待/停放时间作为工具时间和重新-create2h20m
    //                         位于executing_since
    //                         修复已删除。
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
    exec_window_at() const noexcept {
        return std::visit([](const auto& s)
            -> std::optional<std::chrono::steady_clock::time_point> {
            if constexpr (requires { s.executing_since; }) {
                return s.executing_since.time_since_epoch().count() != 0
                     ? std::optional{s.executing_since} : std::nullopt;
            }
            return std::nullopt;
        }, status);
    }

    // ── 解决: 一个 将工具移动到 终端 状态 ──────────
    //
    // 每个终端转变必须携带执行窗口跨越状态覆盖，因为运行是保持它的
    // 唯一状态并且覆盖破坏运行。这是在大约 17 个站点上以
    // 先保存 executing_since()，覆盖状态后再写回。
    // 的形式手工完成的——每个站点都读取
    // WRONG 访问器。
    //
    // executing_since() 是 LIVENESS 读
    // 取：对于从未调度的工具，它会回退到 started_at，卡诞生，
    // 因此经过的计时器有一些内容可以显示。复制到Failed::
    // executing_since中，回退不再是显示方便，而是成为记录的
    // 事实："this tool started executing at card birth"。
    // 然后，exec_window_at() 将其报告为真实窗口，
    // 归并 将整个卡生命周期（权限 等待、排队、用户午餐）记为工具时间。
    //
    // 它只咬住解决BEFORE 分派的工具，这正是转弯出错
    // 的路径集：在提示词处被拒绝，排队时已取消，
    // 参数从未完成的看门狗-killed，数据流死亡，
    // 令牌刷新失败。在没有出现任何问题的情况下，每个工具都会
    // 首先发送，复制的值是真正的标记，并且数字是正确的——
    // 这就是为什么它在稳定的付费提供商上看起来很好，
    // 而在不稳定的提供商上却表现不佳。
    //
    // 这些帮助器读取exec_window_at()，因此"never dispatched"
    // 在整个结算过程中保持"never dispatched"。使用它们而不是将
    // Failed{}/Done{} 直接分配给实时工具。
    void settle_failed(std::chrono::steady_clock::time_point now,
                       std::string output) {
        const auto window = exec_window_at();   // 之前 覆盖
        status = Failed{started_at(), now, std::move(output)};
        if (window) std::get<Failed>(status).executing_since = *window;
    }
    void settle_done(std::chrono::steady_clock::time_point now,
                     std::string output,
                     std::vector<ImageContent> images = {}) {
        const auto window = exec_window_at();   // 之前 覆盖
        status = Done{started_at(), now, std::move(output), std::move(images)};
        if (window) std::get<Done>(status).executing_since = *window;
    }

    // 用于 序列化/记录的字符串标签。跨版本稳定；
    // 反向方向位于 persistence.cpp。
    [[nodiscard]] std::string_view status_name() const noexcept {
        return std::visit([](const auto& s) -> std::string_view {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::same_as<T, Pending>)       return "pending";
            else if constexpr (std::same_as<T, Approved>) return "approved";
            else if constexpr (std::same_as<T, Running>)  return "running";
            else if constexpr (std::same_as<T, Done>)     return "done";
            else if constexpr (std::same_as<T, Failed>)   return "failed";
            else                                          return "rejected";
        }, status);
    }

    // args.dump()的惰性 缓存用于视图。 args.dump() 对于 调用 来说是
    // O(args)，并且对于没有定制 渲染器 的工具来说，在 -frame 上运行，
    // 这使得 tool_use 数据流 O(帧 × args²) 很大。每当 `args`
    // 发生突变时，通过 mark_args_dirty() 无效。
    mutable std::string args_dump_cache;
    mutable bool        args_dump_valid = false;

    void mark_args_dirty() {
        args_dump_valid = false;
        args_dump_cache.clear();
    }
    const std::string& args_dump() const {
        if (!args_dump_valid) {
            args_dump_cache = args.dump();
            args_dump_valid = true;
        }
        return args_dump_cache;
    }

    // O(1)渲染密钥。每个可见工具每个 帧 通过 Message::compute_render_key
    // → turn_element/turn_config 缓存 谓词调用一次。
    // 这里对完整输出 字节 进行哈希处理意味着 帧 时间在长时间会话中增长了 O
    // （总成绩单 字节）——一些大的 Read/Bash 输出和 per
    // -frame 成本占主导地位。实践中，输出 字节 是 (status.index(),
    // output.size()) 元组中的append-only：运行会增长
    // progress_text，但 is_terminal 为 false
    // （我们不增长 缓存）； Done/Failed 登陆了 `output` 一次，
    // 之后就不再变异。假设的 re-execute 替换了整个 ToolUse
    // （新 ToolCallId、新 MessageId-keyed
    // 缓存 插槽），因此此处不需要 字节-level 消歧。
    [[nodiscard]] std::uint64_t compute_render_key() const {
        std::uint64_t k = 1469598103934665603ULL;
        auto mix = [&](std::uint64_t v) { k = (k ^ v) * 1099511628211ULL; };
        mix(output().size());
        mix(progress_text().size());
        mix(args_streaming.size());
        mix(static_cast<std::uint64_t>(status.index()));
        return k;
    }
};

struct Message {
    // 稳定的per-message身份。施工时生成；通过 持久化 往返，
    // 因此它可以重新加载。视图的 渲染 缓存 键是 (thread_id,
    // message.id) 而不是 (thread_id,
    // msg_idx) — 因此，压缩、删除或重新排序无法在
    // 同一位置为 now-different 消息返回 过期 缓存元素。
    // 通过 new_message_id() 的 Default
    // -init 意味着 每个 消息一旦存在就可以识别。
    MessageId   id = new_message_id();
    Role        role = Role::User;
    std::string text;
    /// 用户消息上的图像附件——传输层展平成
    /// Anthropic图像内容块的字节。
    /// 对于助理消息和提交时未携带任何图像的用户消息为空。
    /// 顺序与 `text` 中的 `[image: ...]`
    /// 标记相匹配，因此渲染的散文仍然在视觉上
    /// 锚定图像。
    std::vector<ImageContent> images;
    /// Non-image附件(粘贴/FileRef/符号)在提交
    /// 时由作曲家保留。 `text` 保留 标签- 形式占位符
    /// (`\x01ATT:N\x01`)； 渲染器 用
    /// `attachment::chip_label(...)`
    /// 替换每个占位符，因此记录显示一个紧凑的药丸，而不是内联 400
    /// 行粘贴，并且 传输层 调用 `attachment::expand(...)`
    /// 在序列化 传输 载荷 时将完整的 正文 拼接回去。
    /// 图像附件不是 存储在这里 - 它们的 字节 位于上面的
    /// `images` 上，并作为 Anthropic
    /// 图像内容块发送； `text` 中的 标签 仍然是 渲染
    /// 作为转录本中的药丸，通过相应 ImageContent
    /// 路径中的 标签-label 替换。
    std::vector<Attachment>   attachments;
    std::string streaming_text;
    // ACTUALLY服务于该助理回合的传输模型ID，以及它被路由
    // 的智能模式角色。空 = "whatever was selected"
    // （无智能模式，或此字段存在之前的回合）。
    //
    // 为什么这是在 -message 存储而不是在 渲染 时间从
    // Model::d.model_id 读取：在智能模式下，
    // 轮次被调度到解析的 ROLE 模型（launch_stream 中的
    // `strategic_profile.model`），这通常不是
    // 中的 选择面板 模型。因此，现场选择的 渲染 和 头文件 撒了谎
    // ——徽章上写着 "Mistral"，而每个 字节 都来自 GLM。
    // 它还追溯性地撒了谎：切换模型会重新标记记录中已有的每个回合。
    // 回合的来源是属性 OF 该 TURN，因此它存在于回合中。
    ModelId served_model;
    // 哪个角色槽产生了它。在 头文件 转弯处驱动强调色，
    // 使代表团一目了然。智能模式关闭时不存在——这与
    // "the empty role" 不同，
    // 这也是为什么它是可选的而不是哨兵枚举器的原因。
    //
    // ModelRole，不是字符串，并且这防止了 ALREADY 发生的
    // 漂移：作者仅发出 "strategic"，视图匹配 "strategic"
    // /"implementation"/"utility"（两个死分支），
    // 现有的 role_label() 帮助器生成 "impl" — 一个
    // 概念的三种拼写，没有经过 编译器 检查。在作者处使用 role_label()，
    // 明显的 tidy-up，会默默地破坏强调色。
    // 参见 docs/STRONG_TYPES_AUDIT.md §发现 2。
    std::optional<smart::ModelRole> served_role;
    // ── Extended/adaptive 思考(仅助理转身) ──────────────
    // 当努力进行时，Claude提供者启用自适应思维并且模型发出
    // 领先的`thinking`内容块(文本——在默认
    // `display:omitted`下通常为空——加上
    // 不透明的`signature`)。 Anthropic
    // REQUIRES 该块将在携带该助手的 tool_use 的后
    // 续 -up 轮上逐字重放，否则将请求 400 秒。我们在 流式
    // 传输 期间捕获这两个内容，将它们序列化为 传输 上助理消息
    // 的第一个内容块，并将它们保留下来，以便重新加载的 线程 仍然
    // 可以继续。对于用户回合和不假思索地产生的助理回合为空。
    //
    // MULTI-BLOCK:利用交错的-thinking测试版（
    // 以及在每个响应发出多个思维块的任何传输上）ONE辅助消息可以
    // 独立地携带SEVERAL-signed块。每个签名都覆盖其
    // 自己块的确切文本，因此合并它们（连接文本+保留最后一个
    // 签名）会损坏每个块并且重播 400 秒。
    // `thinking_blocks` 将（文本、签名）对分开，
    // 而 IN ORDER — 权威重放源。 `thinking`
    // 保留连接的 DISPLAY 文本（连接的块 \n\n），
    // `thinking_signature` 镜像最后一个块的签名，
    // 以便与较旧的持久 线程 向后兼容（当向量为空时，传输 重播回落到旧
    // 对）。
    struct ThinkingBlock {
        std::string text;
        std::string signature;
        // Non-empty=这是一个REDACTED思维块：Anthropic的安全系统将整个块加密成不透明的`data`
        // 载荷（在content_block_start中完成）。它必须在其前面的
        // tool_use 之前重播为 {"type":"redacted_thinking",
        // "data":…}，或者在 -up 400s (
        // "Expected thinking or redacted_thinking but found tool_use")（预期 thinking 或 redacted_thinking，却遇到 tool_use。）
        // 之后重播。从未渲染过。
        std::string redacted_data;
    };
    std::vector<ThinkingBlock> thinking_blocks;
    std::string thinking;
    std::string thinking_signature;
    // Wall-clock "Reasoned · ~N tokens · 3.2s"
    // 头文件仪表的推理时间，被保存为小型状态机而不是一个锁存数字：
    //
    //   reasoning_started_ms是OPEN相的稳定-clock标记，
    //                         0=无相开路。不持续（稳定时钟）。
    //   reasoning_ms时间ACCUMULATED结束了关闭阶段，
    //                         并且持续了一个，因此重新加载的线程
    //                         仍然显示了轮转思想多长时间。
    //
    // 两个字段因为一个回合可以思考不止一次：交错思维和
    // multi-item响应数据流在一个工具调用之后再次推理，
    // 所以阶段re-opens和总数相加。
    // 单个 "sealed once" 持续时间使 第二 思维阶段对
    // 视图不可见，然后视图必须根据打字机是否恰好处于 -glide
    // 中期来猜测。
    std::int64_t reasoning_started_ms = 0;
    std::int64_t reasoning_ms = 0;
    // ── Codex/Responses 推理重播( 仅限助理转折) ─────────
    // 响应API是Anthropic思维块的OpenAI类似物。
    // 当我们请求`include:["reasoning.encrypted_content"]`时，
    // 每个推理输出项都携带一个OPAQUE `encrypted_content`
    // blob。为了将跨工具轮次的 chain-of-thought 保持
    // 在 `store:false`（无状态 Codex 模式）下，
    // 该 blob 必须 将作为下一个请求的 `input[]` — 无需 其
    // 服务器 ID 中的 `reasoning` 项重播（回显该 ID 会
    // 触发失败的 server-side 查找）。我们在 流式传输 期间在这里捕获它，
    // 在本回合的 function_call 项目之前重播它，并保留它，以便重新加载
    // 的 线程 保持可连续性。 `reasoning_summary`是人类
    // -visible总结文本（显示为思维块）；发回的是NOT（只有
    // encrypted_content）。对于用户回合和未经推理产生的助理回合为空。
    // 一轮多个推理项以newline-separated依次连接。
    std::string reasoning_encrypted;
    // 对应的 站点铸造了这些斑点（`responses::Site::id` —
    // "chatgpt"、"copilot"，……）。空转，没有加密推理。
    //
    // 密文是不透明的AND账户-scoped：只有产生它的后端才能解密它。
    // 向 Copilot 重播 ChatGPT blob 不是 no-op，
    // 而是 400 — "Encrypted content item_id did not match the target item id"
    // — 并且它会失败整个请求，而不仅仅是推理项。这就是 openclaw
    // #72602 和 github/copilot-sdk#615，
    // 两者都针对这一点提出了申请：一个 线程 切换提供商 mid
    // -conversation 并传播其推理。
    //
    // 我们没有这样的标签，所以没有什么可以阻止它。
    // 在捕获了 Codex 推理的 线程 上切换具有 ^P 的提供商，
    // 并且每个后续回合都会发送新后端无法读取的密文。今天，
    // 这个问题被屏蔽了——Copilot 拒绝了 `include`，
    // 因此该字段只填充了 ChatGPT——这意味着该错误是潜在的，
    // 正在等待第二个站点开始返回斑点。潜伏期不固定。
    //
    // 记录起源使得重放可判定：仅将blob
    // 发送到创建它的站点，否则默默地丢弃它。
    // 降低跨提供商的 chain-of-thought 的连续性成本
    // switch, which the user just asked for anyway; sending costs the turn.
    std::string reasoning_site;
    std::string reasoning_summary;
    // 聊天传输：该消息的推理到达的字段("reasoning_content"
    // 或"reasoning")；当它没有的时候是空的。
    // 下一个请求会以该名称且仅以该名称发送回 `thinking`。
    // 镜像GitHub的客户端(runtime.node:per-message
    // `reasoning_wire_field`、model_protocol/conversation_reasoning.rs)。
    std::string reasoning_wire_field;

    // 跨每个提供商的DISPLAY的统一推理文本。所有三个推理线都通过
    // StreamThinkingDelta 将 可见 文本汇集到 `thinking`
    // 中：Anthropic `thinking_delta`、
    // Codex `reasoning_summary_text.delta` 和 OpenAI
    // -compat `reasoning_content` (DeepSeek / Grok
    // / o-series)。 `reasoning_summary` 是直接填充它的任何
    // 路径的旧版 回退。一个访问器保留视图、渲染 键和 切换 逻辑提供者-agnostic
    // (DRY)：UI 显示推理块，当且仅当这是非-empty。
    [[nodiscard]] std::string_view reasoning_display_text() const noexcept {
        if (!thinking.empty())          return thinking;
        return reasoning_summary;
    }
    [[nodiscard]] bool has_reasoning() const noexcept {
        return !reasoning_display_text().empty();
    }
    /// 现在传输上是否有思考阶段OPEN？由第一个思考增量标记，
    /// 当答案 字节 或工具 调用 密封它时清除。
    /// 这是对 "is it still thinking" 的诚实答案——
    /// 它在 数据流 中存在一个安静的间隙，而动画 标志 则没有。
    [[nodiscard]] bool thinking_open() const noexcept {
        return reasoning_started_ms != 0;
    }
    /// 迄今为止的总思考时间：封闭阶段加上开放阶段。
    [[nodiscard]] std::int64_t thinking_elapsed_ms(std::int64_t now_ms) const noexcept {
        if (!thinking_open()) return reasoning_ms;
        return reasoning_ms + std::max<std::int64_t>(0, now_ms - reasoning_started_ms);
    }
    // 平滑缓冲区。 Anthropic 的 SSE 以服务器的分词器速率
    // 批量处理增量 - 单个 content_block_delta 可以
    // 携带 50 多个字符，并且多个字符可以在一次 TCP 读取中到达。
    // 如果我们将每个增量直接附加到 `streaming_text`，
    // 用户将在每个 帧 看到大跳跃，而不是让 流式传输 感觉活跃的光
    // 标-paced 动画。
    //
    // StreamTextDelta现在改为附加到`pending_stream`。
    // Tick 处理函数 将 字节 从
    // `pending_stream` 滴入 `streaming_text`，
    // 其速度足以跟上实际生成速度（≥ 32 个字符/33 毫秒刻度 =
    // ~960 c/s，~3× 典型的 Sonnet 数据流），
    // 同时在块突发到达时仍然显示小增量。
    // 渲染 `streaming_text` 视图与之前完全相同 - 平滑对于 渲染
    // 器 来说是不可见的。
    std::string pending_stream;
    // 设置 传输 关闭此消息的 TEXT 内容块（文本块为 Anthropic
    // content_block_stop）的时刻 — 该块始终位于
    // tool_use content_block_start 之前。
    // 是权威的"the model is done typing prose"信号，
    // 到达BEFORE任何工具卡都推送到tool_calls。视图将其读取到 pre
    // -emptively，在真正安静的间隙期间将 逐步显示 光标滑到实时边缘，
    // 因此当卡出现时强制的 Hard-snap 已经是 no-op → 没有可见的
    // 逐步显示 突发 (
    // 第一个字符停住，其余内容直到下一次工具调用才突然出现。
    // 比 字节-quiet 计时启发式更稳健，后者快速 传输 out-runs。
    // 粘在 sub-turn 内；当为 post-tool 散文创建新的占位符消息时重置。
    bool text_block_closed = false;
    std::vector<ToolUse> tool_calls;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();

    // ── 人数 -turn 遥测( 仅辅助转弯) ──────────────────────
    //
    // 在 finalize_turn 密封一次，之后从未接触过。
    // 这与 `served_model` 获胜的论点相同：
    // 回合的出处是属性 OF 该 TURN，因此它存在于回合中，
    // 而不是从已被丢弃的会话状态重新计算。
    //
    // 它必须被存储，因为它捕获的计数器是VOLATILE。
    // `transient_retries`、
    // `mid_stream_failures`和朋友住在phase::
    // Active上，每回合开始时都会重置——当统计面板打开时，
    // 他们已经消失了，所以无论走多少笔记录都无法恢复他们。 缓存 分割
    // 也是如此：今天每回合都会计算比率并丢弃。
    //
    // 成本是每轮一次结构写入。每个令牌没有任何内容，
    // 每个 帧 没有任何内容：在已经携带千字节的消息上，~50 字节。
    struct Telemetry {
        // Wall-clock，分为两半，回答不同的问题。
        // ttft是"did the provider make me wait"，
        // 数据流是"was generation slow"——单个总数将
        // 排队问题与吞吐量问题混为一谈，并且它们具有不同的修复。
        std::uint32_t ttft_ms   = 0;   // 请求启动→第一个内容字节
        std::uint32_t stream_ms = 0;   // 第一字节→停止

        // 用途如传输所报告的那样。 input_tokens EXCLUDES 缓存 字段（
        // Anthropic 的约定），因此真正的前缀大小是输入 + cache_read
        // + cache_creation — 请参阅 cache_hit_ratio()。
        std::uint32_t input_tokens     = 0;
        std::uint32_t output_tokens    = 0;
        // 开具 WITHIN output_tokens，从未添加到其中。
        std::uint32_t reasoning_tokens = 0;
        std::uint32_t cache_read       = 0;
        std::uint32_t cache_creation   = 0;

        // 传输层健康。 Non-zero 表示该回合幸存下来。
        std::uint16_t transient_retries    = 0;
        std::uint16_t mid_stream_failures  = 0;
        std::uint16_t no_progress_failures = 0;

        // 字节、传输层实际交付。除以 stream_ms，
        // 这是诚实的吞吐量，其中令牌计数不是：
        // 令牌突发地到达并且仅在使用帧处结算。
        std::uint32_t wire_bytes = 0;

        // 从 缓存 提供的前缀的份额，或者当本轮报告根本没有前
        // 缀时为-1。哨兵而不是 0.0，
        // 因为 "no cache hit" 和 "nothing to hit"
        // 是不同的读数，将它们平均在一起会低估每个会话。
        [[nodiscard]] double cache_hit_ratio() const noexcept {
            const double prefix = static_cast<double>(input_tokens)
                                + static_cast<double>(cache_read)
                                + static_cast<double>(cache_creation);
            if (prefix <= 0.0) return -1.0;
            return static_cast<double>(cache_read) / prefix;
        }
        [[nodiscard]] bool degraded() const noexcept {
            return transient_retries || mid_stream_failures
                || no_progress_failures;
        }
    };
    // 在用户消息、早于该字段的辅助轮次以及从未到达
    // finalize_turn的轮次上不存在。可选的而不是归零的结构，
    // 因为 "not measured" 和
    // "measured as zero" 是不同的，并且将它们合并
    // 的哨兵会在每个平均翻转的分母中放置假零。
    std::optional<Telemetry> telemetry;

    std::optional<CheckpointId> checkpoint_id;
    // 当回合以数据流-level错误结束时设置（过载、5xx、网络掉线、
    // mid-stream 解析故障等）。仅携带 user-facing
    // 消息 — 无 "⚠" 前缀或格式；该视图添加了这些。
    // 将 SEPARATE 与 `text` 保留在一起，
    // 以便助手的实际部分输出（出错时保存到 `text` 中）和失败原因 渲染
    // 清晰可见。 Status-bar 横幅显示为
    // `m.s.status`；该字段是 per-message 内联副本。
    std::optional<std::string> error;
    // 对于在 post-compact 对话的头部保存
    // 压缩摘要的合成用户消息来说是这样。视图 渲染 是
    // 此消息上方的 "Conversation compacted"
    // 分隔线（而不是普通的扬声器导轨），
    // 因此边界在文字记录中可见。 传输 载荷 未更改
    // - 它作为携带摘要文本的普通用户消息发送到模型。
    // 将Claude代码的`isCompactSummary`字段镜像到合成的post
    // -compact消息上(二进制程序靠近偏移92759504)。
    bool is_compact_summary = false;

    // 主动RETRIEVED-CONTEXT注入，如果该消息是一个。
    //
    // 在携带PROACTIVELY RETRIEVED上下文（SOTA
    // "active retrieval"、FLARE/Self-RAG系列）
    // 的合成用户消息上设置：当用户提交知识-shaped问题时，
    // submit_message静默运行RAG管道，并且仅在HIGH
    // -confidence命中时，在辅助占位符之前插入其中之一。在 传输 上，
    // 这是一条普通的用户消息，其文本是模型可以基于的受围栏 <retrieved-context> 块；
    // 在记录中，查看 渲染 是一个紧凑的 one-line
    // "retrieved context" 可供性而不是原始块。
    // 从未显示为用户自己的话，并且由doom-loop/run-start行走跳过
    // (不是真正的人类转弯边界——参见
    // agent_loop_should_break)。
    //
    // 这是三个独立的字段——`bool proactive_context`，
    // 加上 `double proactive_confidence = -1.0`
    // 和记录为 "only meaningful when proactive_context is true"
    // 的 `bool proactive_expanded`。
    // 当只有两个形状为实数时，三个字段描述八种状态，并且置信度带有
    // 一个哨兵 内部 自己的域：该字段是 [0,1] 中的概率，
    // 因此 -1.0 是一个值，该类型表示是置信度，而读者必须知道不是。
    // 将它们分组再说一次：缺席=普通消息，参与=这些事实是真实的。
    //
    // 持久化的形状没有改变——它已经以这种方式嵌套（
    // `if (proactive_context) { … confidence … }`)，
    // 因此-disk和和类型存在并且只有in-memory类型不同意。
    struct ProactiveContext {
        // 检索置信度表明门控注射，来自RAG漏斗（
        // ProactiveHit.confidence)，因此成绩单卡
        // 可以渲染为真实的吧。当漏斗没有报告时缺席。 传输-inert。
        std::optional<double> confidence;
        // View-only:显示每个源的FULL段落文本而不是one-line片段。
        // 在最新的此类卡上由 Ctrl+U (ToggleRetrievedExpanded) 切换。
        // 传输-inert 和 不 仍然存在 - 重新加载时重
        // 置为折叠的显示首选项。折叠到 compute_render_key 中，
        // 因此翻转它会使缓存的元素无效。
        bool expanded = false;
    };
    std::optional<ProactiveContext> proactive;

    // 这是检索到的-context消息吗？每个消费者实际上都会问的问题；
    // 在大约 8 个只需要布尔值的站点上，其读取效果比
    // `.proactive.has_value()` 更好。
    [[nodiscard]] bool is_proactive_context() const noexcept {
        return proactive.has_value();
    }

    // 用户是否输入了此内容？分叉注释、压缩摘要、
    // 注入上下文和路由卡也是用户消息，但没有人写它们。
    [[nodiscard]] bool typed_by_user() const noexcept {
        return role == Role::User && !fork_note && !is_compact_summary
            && !proactive.has_value() && !smart_routing;
    }

    // 智能模式路由遥测（查看-only、传输-inert，不保留）。
    // 当`smart_routing`被设置时，这是一张合成的zero-text卡，
    // 渲染将每-turn ROUTING DECISION作为第一
    // -class 线程 事件(🧠"Smart Mode")：回合被路由
    // 到哪个模型+工作、缩放它的分类复杂性以及哪些层是活动的。这不是 一个
    // 真正的 user/assistant 转弯——每次 wire/loop
    // 行走都会跳过，就像 proactive_context 一样。
    // 实际的子代理委托渲染作为普通`task`工具卡；这张卡是DECISION，
    // 他们是执行者。
    bool        smart_routing        = false;
    std::string smart_route_model;    // 传输 id 战略回合已运行
    std::string smart_route_effort;   // 努力标签("off"/"high"/…)
    std::string smart_route_complexity; // "trivial"/"simple"/"standard"/"complex"
    std::string smart_route_note;     // 努力PROVENANCE，e.g。
                                      // "medium → complex · learned +1 · session -1"
                                      // -使自适应决策清晰。
    bool        smart_route_orchestrate = false;  // 授权指令激活
    bool        smart_route_subagents   = false;  // per-role子代理路由

    // 叉子来源卡(view-visible，传输-VISIBLE)。
    // 当设置 `fork_note` 时，这是一条合成的 Role::User 消息，
    // 播种在新的 -forked 线程 的头部。两个工作：
    //   1. VIEW:渲染为第一-class "\u2443 Forked" 事件卡(字形+
    //      "Forked"标签+一个-line 正文)，
    //      因此品牌-new分叉是NOT一个空白屏幕——用户看到，
    //      作为线程 事件，分叉发生了并且父记录可以按需读取。
    //   2. 传输:与smart_routing/proactive_context不同，该消息IS
    //      发送到模型(其`text`是转录指针)。这是故意的用户消息
    //      - ChatGPT/Responses 传输层 DROPS
    //      mid-thread 系统消息（折叠到 `instructions` 中），
    //      因此系统注释可能会消失，并且模型永远不会知道父记录的存在。
    //      用户消息在每个提供者路径中都存在。
    //      `fork_transcript` 保存卡的 正文
    //      的路径（传输 `text` 携带完整指令）。
    bool        fork_note        = false;
    std::string fork_transcript;   // 父成绩单.md的路径（可以为空）

    // FNV-1a 覆盖 turn_element /
    // turn_config 在构建渲染元素时消耗的字段。
    // 视图 缓存 在插入时用此键标记构建的元素，
    // 并在每次点击时重新-checks；任何更改 key
    // -relevant 字段（切换 扩展、附加工具输出、
    // 状态更改、附加错误、正文 字节 更改）的突变都会影响密
    // 钥并强制重建，而不是默默地返回 过期 缓存元素。
    //
    // pending_stream.size()被混合，
    // 因此仅落在Tickpacer的缓冲区中（在它滴入streaming_text
    // 之前）的增量仍然推进密钥。如果没有它，渲染 门 (
    // program.hpp::visual_hash) 会看到未更改的哈
    // 希并跳过 帧；实时 末尾 的 逐步显示 光标追上了
    // streaming_text，然后停止了 -arming 的动画 帧，
    // 因此 数据流 明显冻结，直到不相关的轴翻转 -
    // "md streaming gets stuck" 症状。
    //
    // 使其与 src/runtime/view/thread/turn/turn.cpp.
    // 中的实际读取保持同步
    [[nodiscard]] std::uint64_t compute_render_key() const {
        std::uint64_t k = 1469598103934665603ULL;
        auto mix = [&](std::uint64_t v) { k = (k ^ v) * 1099511628211ULL; };
        // 混合字符串的CONTENT，而不仅仅是其长度。单独的长度是
        // 这段较大的正文是否发生变化？
        // 的良好代理（流式传输 文本 缓冲区 只会增长），
        // 但对于从小集合中提取的短标识符来说这是错误的：两个长度相等的
        // 模型 ID 会发生冲突，而一回合的 头文件 会重用另一个的缓存元素。
        // FNV-1a 优于 字节，与 `mix` 相同。
        auto mix_str = [&](std::string_view s) {
            for (unsigned char c : s) k = (k ^ c) * 1099511628211ULL;
            mix(s.size());
        };
        mix(static_cast<std::uint64_t>(role));
        mix(text.size());
        mix(streaming_text.size());
        mix(pending_stream.size());
        mix(images.size());
        mix(attachments.size());
        for (const auto& a : attachments) {
            // 正文 字节 提交后不变；混合 字节 计数
            // 足以使 渲染 缓存 在假设的未来编辑中无效，
            // 并且足够便宜，可以保留在这里。
            //
            // `byte_count`和NOT
            // `body.bytes().size()`：这在重建回合
            // 的每个帧上运行，并且正文可以是未物化的斑点引用。询问它的大小
            // 将读取 2 MB 构建日志磁盘来计算哈希值 - 将 渲染
            // 路径精确地转换为 提前加载 加载 LazyBytes
            // 存在以避免。 byte_count 是存储的元数据，
            // 并且已经是 标签 显示的内容。
            mix(static_cast<std::uint64_t>(a.kind));
            mix(a.byte_count);
            mix(a.path.size());
            mix(a.name.size());
            mix(static_cast<std::uint64_t>(a.line_count));
        }
        mix(tool_calls.size());
        for (const auto& tc : tool_calls) mix(tc.compute_render_key());
        mix(error ? error->size() + 1 : 0ULL);   // 区分空与缺
        mix(is_compact_summary ? 1ULL : 0ULL);
        mix(proactive ? 2ULL : 0ULL);
        mix(proactive && proactive->expanded ? 4ULL : 0ULL);
        // 推理块：其文本在流式传输期间增长，并且一旦答案开始，
        // 该块就从实时思想数据流切换到固定的one-line摘要。
        // 混合长度 + "has answer body"
        // 位 re-renders 跨该转换的缓存元素。
        // 使用统一的访问器，因此所有提供者的密钥都相同。
        mix(reasoning_display_text().size());
        // 轮出处驱动头文件标签+角色标签，
        // 因此served_model与另一个不同的消息不得重用
        // 其缓存的元素。
        //
        // 值，不是它的尺寸。混合大小只能通过运气来区分这三个
        // 角色 - "strategic" 是 9 个字符，
        // 任何未来的 9-character 角色都会与它发生冲突，
        // 从而留下缓存的 头文件 涂上错误的强调色。
        // 两个长度相同的模型 ID 具有相同的危险。
        // 枚举 没有这样的歧义，并且模型 ID 已正确散列。
        mix_str(served_model.value);
        mix(served_role ? static_cast<std::uint64_t>(*served_role) + 1ULL : 0ULL);
        if (smart_routing) {
            mix(8ULL);
            mix(smart_route_model.size());
            mix(smart_route_effort.size());
            mix(smart_route_complexity.size());
            mix(smart_route_note.size());
            mix((smart_route_orchestrate ? 16ULL : 0ULL)
                | (smart_route_subagents ? 32ULL : 0ULL));
        }
        if (fork_note) {
            mix(64ULL);
            mix(fork_transcript.size());
        }
        // 将置信度量化到bar-relevant桶，
        // 使得置信度改变（re-injection)的卡使缓存无效，
        // 而不会因浮动噪声而搅动。只有主动消息才携带它。
        if (proactive && proactive->confidence)
            mix(static_cast<std::uint64_t>(*proactive->confidence * 100.0) + 1ULL);
        return k;
    }
};

struct Thread {
    ThreadId    id;
    std::string title;
    std::vector<Message> messages;
    // 出处：如果此线程是从另一个线程分叉（分支）的，则父线程的 ID。
    // 对于普通线程为空。保留，以便线程列表可以显示
    // "forked from …" 提示，并且历史记录保持可追溯。
    std::string forked_from;
    // 在来自另一代理会话的线程上设置（Codex、Claude代码）。
    // `source` 是工具密钥，`path` 是会话文件，
    // 读取该文件时的大小和运行时间，以便可以选择稍后的更改。
    // `messages` 是有多少 线程 消息来自会话；
    // 他们之后的任何内容都是在 agentty 中添加的。坚持了下来。
    struct Import {
        std::string   source;
        std::string   session_id;
        std::string   path;
        std::uint64_t size  = 0;
        std::int64_t  mtime = 0;
        std::uint32_t messages = 0;
    };
    std::optional<Import> imported;
    // 仅运行时：尚未打开的会话的选择面板行。它的 id 是它将
    // 使用的打开方式；它后面没有agentty文件。
    bool foreign_only = false;
    // 每-thread主动-RAG覆盖。缺席=继承全局RAG模式；
    // 参与=这个线程自己的行为。由货叉 选择面板 设置。
    //
    // 这是 `int rag_mode_override = -1`，
    // 注释必须拼出编码（
    // "-1 inherit, 0 On, 1 FirstTurnOnly, 2 Off"）
    // ——这就是说明：记录哪些整数合法的注释是一种尚未被写下来的类型。
    // 消费者会回过头来进行比较，因此 枚举 之外的值是可以代表的，
    // 并且默默地意味着 "inherit"。可选表示类型的两半：是否有覆盖，
    // 并且 - 如果有 - 它是 RagMode 而不是其他。
    //
    // 枚举存在于domain/rag_mode.hpp而不是store/中，
    // 因为store/store.hpp包括THIS 头文件——从这里开始依赖
    // 它将是一个循环。它是词汇，而不是存储，所以 domain/ 是它所属的地方；
    // store:: 保留一个别名，因此现有的拼写仍然是 编译。
    std::optional<RagMode> rag_mode_override;
    std::chrono::system_clock::time_point created_at = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point updated_at = std::chrono::system_clock::now();
    // 当用户最后一次重命名线程时（0=从不）。 保存 仅当其自己
    // 的重命名至少与新的相同时才将其标题写入存储的标题，
    // 因此另一个持有旧副本的 agentty 无法撤消重命名。
    std::int64_t renamed_at = 0;   // unix秒
    // 标记的标题是自动的，而不是选择的名称：用户清除了他们的重命名。
    // 它仍然带有 renamed_at，因此清除击败了 过期 副本，
    // 但它可能又是 auto-titled。
    bool name_is_auto = false;
    // 当标题是用户选择的标题时为真（因此不能被
    // 自动标题替换）。
    [[nodiscard]] bool user_named() const noexcept { return renamed_at > 0 && !name_is_auto; }
    // 标题来自模型(auto-titling)，因此不会再次生成。
    bool auto_titled = false;
    // 该线程可以是auto-titled。自标题存在以来，
    // 于 线程 开始设置；之前的 线程 没有 标志 并保留其第一个
    // -line 名称，因此继续旧聊天永远不会重命名它。
    bool auto_title_ok = false;

    // 对于^J 选择面板，保存在头文件中，以便列表可以在
    // 不加载消息的情况下显示它们。 工作区 是 线程 启动
    // 的文件夹（录制之前的 线程 上的 ""）；
    // 预览你第一件事就是带类型的，一行； message_count
    // 是您 带类型的 加上助理回复的消息数。
    std::string   workspace;
    std::string   preview;
    std::uint32_t message_count = 0;
    // 仅运行时，从不已保存:选择面板已经尝试
    // 填充此线程的预览并在本次会话的后台计数。
    bool          summary_tried = false;

    // 传输-only压缩记录。每个条目均显示
    // 从此会话构建的后续上游请求，应把 messages[0..up_to_index) 替换为一条携带 summary 的合成 User 消息。
    //
    //
    // 压缩NEVER变异`messages`:用户的转录本在压缩事件
    // 上是不可变。用户可以不断地看到他们曾经经历过的每一个回合；
    // 只有提供者的 传输 载荷 才会将前缀折叠到摘要中。该视图在消息[
    // up_to_index-1]和消息[up_to_index]
    // 之间绘制分隔线以指示
    // 模型已经看不到此行以上的轮次。
    //
    // 堆叠压缩：当用户压缩两次时，第二条记录的`up_to_index`
    // 覆盖了直到第二边界的所有内容，并且其`summary`是通过汇总
    // [first_summary_synth_user，
    // 消息[first.up_to_index..second.up_to_index]]生成的。
    // 传输替换总是读取LATEST记录——
    // 较早的记录被保留用于history/persistence和将来的表面处理(e.g.
    // 此会话已压缩三次。
    // 但不在高频路径上进行咨询。向量是按时间顺序排列的。
    struct CompactionRecord {
        std::size_t up_to_index = 0;     // 涵盖消息[0..up_to_index)
        std::string summary;             // 模型输出，<summary>…</summary> 被剥离
        std::chrono::system_clock::time_point created_at =
            std::chrono::system_clock::now();
        // 挤压ACHIEVED什么的，估计代币。
        //
        // 压缩的全部工作是使下一个请求适合。
        // 如果没有这两个数字，没有人可以区分回收
        // 180k 令牌的压缩和回收 400 个令牌的
        // 压缩 - 两者在记录中看起来相同，都留下摘要，
        // 第二个意味着下一回合再次压缩，并且 线程
        // 活锁总结自身。
        //
        // GitHub的运行时出于相同的原因追踪同一对（
        // `preCompactionTokens`/`postCompactionTokens`)：
        // 回收是要观看的RATE，而不是发生或未发生的事件。
        //
        // 0/0在此之前写入的记录存在——不存在，不是零回收。
        // `reclaimed()` 返回 nullopt 而不是发明一个数字，
        // 因此旧的 线程 读作 "unknown" 而不是
        // "this compaction did nothing"。
        int tokens_before = 0;
        int tokens_after  = 0;

        // 该压缩回收的令牌，或者当未测量时无效。
        [[nodiscard]] std::optional<int> reclaimed() const noexcept {
            if (tokens_before <= 0 || tokens_after <= 0) return std::nullopt;
            return tokens_before - tokens_after;
        }
    };
    std::vector<CompactionRecord> compactions;
};

// 对模型的 下一个 请求将携带的前缀大小（以令牌为单位）的本地估计，
// 直接从 `thread.messages` 计算。由 auto
// -compaction 触发器用作启动 数据流 之前的“主动”检查 -
// `Session::tokens_in` 是一个滞后信号（从 PRIOR
// 转弯的 StreamUsage 事件 更新），因此具有重型工具输出的转弯
// 可以将下一个请求推过 context-max，而不会发出任何警告。
//
// 估计为 字节 / 3.5，每个图像附件附加约 1500-token
// 费用（Anthropic 的图像内容块标记为独立于 字节 大小
// 的固定成本）。保守：每个 token 的代码散文平均约为 3.3 字节，
// 而工具 -call JSON 信封通常低于该值，
// 因此 3.5 错误稍微超过 -counting，这是这里的安全
// 方向 - 提前一圈触发压缩会花费一个往返；缺少触发器会花费整个
// 会话​​ ("no coming back")。
[[nodiscard]] inline int estimate_prefix_tokens(const Thread& t) noexcept {
    constexpr double kBytesPerToken    = 3.5;
    constexpr int    kTokensPerImage   = 1500;
    std::size_t bytes = 0;
    int images = 0;
    for (const auto& m : t.messages) {
        bytes += m.text.size();
        bytes += m.streaming_text.size();
        bytes += m.pending_stream.size();
        images += static_cast<int>(m.images.size());
        for (const auto& tc : m.tool_calls) {
            bytes += tc.name.value.size();
            bytes += tc.args_streaming.size();
            bytes += tc.output().size();
            bytes += tc.progress_text().size();
        }
    }
    auto from_bytes = static_cast<int>(static_cast<double>(bytes) / kBytesPerToken);
    return from_bytes + images * kTokensPerImage;
}

// 提供商针对最新测量的转弯（输入+两个缓存部分）报告
// 的提示词尺寸，或者当没有测量转弯或在其之后压缩线程时为
// 0（该读数是较长的，前-compaction 提示词）。
// 打开 线程 时播种上下文量表，因此它显示此 线程 的用法，
// 而不是最后一个的用法。
[[nodiscard]] inline int last_reported_prompt(const Thread& t) noexcept {
    const std::size_t cut = t.compactions.empty() ? 0 : t.compactions.back().up_to_index;
    for (std::size_t i = t.messages.size(); i-- > 0;) {
        const auto& msg = t.messages[i];
        if (!msg.telemetry) continue;
        if (i < cut) return 0;
        const auto& tm = *msg.telemetry;
        const std::uint64_t n = std::uint64_t{tm.input_tokens}
                              + tm.cache_read + tm.cache_creation;
        if (n) return n > 0x7fffffffu ? 0x7fffffff : static_cast<int>(n);
    }
    return 0;
}

// TEXT模型当前可以看到，作为对线程的视图——没有副本。
//
// 可见性规则是压缩的：最新的CompactionRecord丢弃消息[0..
// up_to_index)并且其摘要代表它们（格式错误的记录回落到整个记录，
// 如传输构建器所做的那样）。
// 这会回答 "is this string in what the model sees"
// — skills::active_in 背后的派生 — 因此它返回每条可见消息的文本和
// 每个工具结果的输出的 string_views，而不是深层副本。
//
// 住在这里，在线程旁边，因为这是关于线程的事实：TUI的工具
// 分派和ACP服务器都源自它，并且两者都不应该触及对方的层。
// 与传输建造者（cmd_factory.cpp
// wire_messages_for_impl）保持同步；两国一治。
//
// 该意见借鉴于`t`；使用它们时保持 `t` 处于活动状态且不被修改。
[[nodiscard]] inline std::vector<std::string_view> visible_text(const Thread& t) {
    std::size_t from = 0;
    std::vector<std::string_view> out;
    if (!t.compactions.empty()) {
        const auto& rec = t.compactions.back();
        if (rec.up_to_index != 0 && rec.up_to_index <= t.messages.size()) {
            from = rec.up_to_index;
            out.push_back(rec.summary);
        }
    }
    out.reserve(out.size() + (t.messages.size() - from) * 2);
    for (std::size_t i = from; i < t.messages.size(); ++i) {
        const auto& msg = t.messages[i];
        if (!msg.text.empty()) out.push_back(msg.text);
        // 技能正文作为TOOL RESULT（`skill`工具的输出）到达，
        // 因此工具输出是模型所看到的内容的一部分，而不仅仅是文本。
        for (const auto& tc : msg.tool_calls)
            if (const auto& o = tc.output(); !o.empty()) out.push_back(o);
    }
    return out;
}

// NOTE:压缩-AWARE估计在cmd_factory中作为
// `cmd::estimate_wire_tokens(Thread)`存在。
// 它使用与上述函数相同的 bytes/3.5 + ~1500-per-image
// 基础对替换的 传输 视图（摘要代替所覆盖的前缀）进行评分，因此两者具有可比性。
// 这里不要添加第二个——必须同意的三个估计器太多了，
// 并且CompactionRecord的before/after对只有当双方来自同一个记分器时才有意义。
//

struct PendingPermission {
    ToolCallId  id;
    ToolName    tool_name;
    std::string reason;
    // 定义用户正在被SHOWN、指纹识别。在启动时再次验证，
    // 因为 MCP 服务器可以在屏幕上显示 提示词
    // 且根据名称记录 授权 时重新定义工具名称。
    // 0表示"not recorded"，跳过检查。
    std::uint64_t def_hash = 0;
};

} // namespace agentty

// ImageContent拥有其字节 (LazyBytes)和共享的base64
// 存储单元，并且LazyBytes拥有内容-addressed源加上其解析为的字节。
// 移动到另一个线程是安全的：每个lazily-filled槽在std::
// call_once下只被写入一次，并在之后通过获取加载读取，
// 因此线程要么运行填充，要么等待它，然后看到完成的字节。
// （这个纪律并非偶然 - jaal 的 Sendable 在第一次
// 针对此代码运行时在 LazyBytes 中发现了真正的 竞态，
// 19 个 TSan 报告为零，这是 D7 的工作示例。
// 修复使得这些 opt-ins 诚实而不是消音器。）
//
// 可发送，不 冻结。冻结意味着通过 const T 可以到达的任何内容都不能改变，
// 这里，记忆的 字节 和记忆的 base64 都可以，位于 const 访问器后面。
// 这正是 D8 的区别——对于 MOVE 对于另一个 线程 来说是安全的，
// 对于两个之间的 SHARE 来说不安全——所以这些说只能发送，
// 而 maya::shared<ImageContent> 仍然正确地不可能。
MAYA_SENDABLE(agentty::LazyBytes);
MAYA_SENDABLE(agentty::ImageContent);
