#pragma once
// agentty::tools::effects——描述工具对世界
// 的可观察影响的能力-set标签。 权限策略读取ONLY这些标签；
// 工具实现无法决定自己的信任级别。这是之前设计的类型反转，
// 每个工具都带有一个定制的 `needs_permission(Profile)`
// lambda — 容易忘记，容易不一致（read
// -only git_* 历史上表示 "never ask"，
// 而 bash 表示 "always ask in non- Write"，
// 即使两者都生成子进程）。
//
// 挑选副作用是通过什么工具DOES走向世界的，而不是它是如何实现的。
// `git_status` 是 `ReadFs`，即使它是 `git`；
// `bash` 是 `Exec`，因为*型号*选择了命令。
// "we know what this subprocess does" 和
// "the model picked it" 之间的区别才是 权限 流程真正关心的。

#include <cstdint>
#include <initializer_list>
#include <string>

namespace agentty::tools {

// 单一能力标签。 Bit-encoded 所以 EffectSet 是一个微不足道的 uint8_t。
//
// ReadFs — 读取文件系统状态（打开、统计、readdir、扫描）。
// WriteFs — 改变文件系统状态（create/modify/delete 文件、
//            写入 .git、在该过程中幸存的任何内容）。
// 网络——向网络发送字节或从网络接收字节。
// Exec —运行任意型号-chosen程序(`bash`、
//            `diagnostics`)。绝对比 `WriteFs`
//            更危险，因为模型控制“什么”运行，
//            而不仅仅是更改哪些文件。
// 纯净——没有可观察到的副作用(没有任何类型的IO)。
//            今天只有 `todo` 符合资格；保留标签，以便策略可以
//            干净地缩短-circuit。
enum class Effect : std::uint8_t {
    ReadFs   = 1u << 0,
    WriteFs  = 1u << 1,
    Net      = 1u << 2,
    Exec     = 1u << 3,
};

// 一组副作用，包装成uint8_t。简单可复制，
// 所有操作 constexpr — 可用作 constexpr
// 构造的 ToolDef-likes 的值成员，并可用作策略中的立即操作数。
class EffectSet {
    std::uint8_t bits_ = 0;
public:
    constexpr EffectSet() noexcept = default;
    constexpr EffectSet(std::initializer_list<Effect> es) noexcept {
        for (auto e : es) bits_ |= static_cast<std::uint8_t>(e);
    }
    constexpr explicit EffectSet(std::uint8_t bits) noexcept : bits_(bits) {}

    [[nodiscard]] constexpr bool has(Effect e) const noexcept {
        return (bits_ & static_cast<std::uint8_t>(e)) != 0;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr std::uint8_t bits() const noexcept { return bits_; }

    [[nodiscard]] constexpr EffectSet operator|(EffectSet o) const noexcept {
        return EffectSet{static_cast<std::uint8_t>(bits_ | o.bits_)};
    }
    constexpr EffectSet& operator|=(EffectSet o) noexcept {
        bits_ |= o.bits_; return *this;
    }
    constexpr bool operator==(const EffectSet&) const noexcept = default;
};

// 便利性：一套完整的-pure 副作用（无副作用）。
// 用作仅在模型可见的 -memory 状态下进行操作的工具的默认
// 设置（当前仅为 `todo`）。
inline constexpr EffectSet pure_effects{};

// ── 平行-safety规则 ────────────────────────────────────────────────
// 控制具有 `want` 副作用 的工具是否可以在一
// 组 `active` 副作用 正在飞行时启动。
// 以 副作用 标签表示，因此调度策略源自能力模型，
// 而不是通过 运行时 散布。
//
//   WriteFs/Exec要求*独占*访问：
//     * WriteFs-tagged工具可能会改变任何文件系统状态
//       兄弟姐妹可能正在读取（ReadFs)、
//       写入（WriteFs)或攻击（Exec)。
//       对不同文件的两次编辑看起来是独立的，直到模型选择重叠的路径。
//     * Exec 严格来说更强大——*模型*选择什么
//       子进程确实如此，因此我们必须假设最坏的情况并进行序列化。
//
//   纯/ReadFs/网自由创作。读-read，绝不读竞态；
//   Net 既不接触 FS 也不接触进程状态； in-memory 纯
//   工具 (todo) 对模型无法同时观察的数据进行操作。
//
// 在工具规范中反映为编译-time 不变量(参见agentty::
// tools::spec::proofs::parallel_rule_is_well_founded)。
[[nodiscard]] constexpr bool is_parallel_safe(EffectSet active, EffectSet want) noexcept {
    constexpr auto exclusive = [](EffectSet e) noexcept {
        return e.has(Effect::WriteFs) || e.has(Effect::Exec);
    };
    if (exclusive(active) || exclusive(want)) {
        // 一侧需要独占访问；仅当另一侧空时才安全。 `active == {}`
        // 覆盖 "nothing is running yet"。
        return active.empty();
    }
    return true;
}

// 规则的编译-time点-checks——比散文注释更清晰，
// 并且如果有人重写了错误的实现，则构建失败。
namespace proofs {
static_assert( is_parallel_safe(EffectSet{}, EffectSet{Effect::ReadFs}));
static_assert( is_parallel_safe(EffectSet{Effect::ReadFs}, EffectSet{Effect::ReadFs}));
static_assert( is_parallel_safe(EffectSet{Effect::ReadFs}, EffectSet{Effect::Net}));
static_assert( is_parallel_safe(EffectSet{Effect::Net},    EffectSet{Effect::Net}));
static_assert( is_parallel_safe(EffectSet{},               EffectSet{Effect::WriteFs}));
static_assert( is_parallel_safe(EffectSet{},               EffectSet{Effect::Exec}));
static_assert(!is_parallel_safe(EffectSet{Effect::WriteFs}, EffectSet{Effect::ReadFs}));
static_assert(!is_parallel_safe(EffectSet{Effect::ReadFs},  EffectSet{Effect::WriteFs}));
static_assert(!is_parallel_safe(EffectSet{Effect::Exec},    EffectSet{Effect::ReadFs}));
static_assert(!is_parallel_safe(EffectSet{Effect::WriteFs}, EffectSet{Effect::WriteFs}));
static_assert(!is_parallel_safe(EffectSet{Effect::Exec},    EffectSet{Effect::Exec}));
} // namespace proofs

// "ReadFs, Net"-排序-by-bit，逗号-separated。
// 用于 权限 UI 和日志记录；没有在任何地方解析。
[[nodiscard]] std::string to_string(EffectSet e);

// 单个-effect标签，e.g。 "ReadFs"。 Capital-cased 用于
// 散文用途 ("Tool needs WriteFs permission")；
// 较低的-case "read"、"exec"形式没有在任何地方使用，因为它们与动词冲突。
[[nodiscard]] std::string_view to_string(Effect e) noexcept;

} // namespace agentty::tools
