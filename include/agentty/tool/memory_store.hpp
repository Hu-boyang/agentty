#pragma once
// agentty::tools::memory-`remember`和
// `forget`工具背后的存储层，以及system
// -prompt构建器每次读取的加载器。
//
// 存储形状：换行-delimited JSON("JSONL")。每行一条记录：
//
//   {"id":"a1b2c3d4","ts":1731860000,"scope":"project","text":"prefer fish"}
//
// JSONL 被选中而不是单个 JSON 文档有以下三个原因：
//
//   1. 追加一条新记录是对 `<record>\n` 进行一次 `O_APPEND` 写入 —
//      整个文件没有read-modify-write，没有atomic
//      -rename舞蹈，没有破坏并发编辑的风险。
//   2.文件为line-addressable:`forget`条带匹配行
//      并重写了幸存者。损坏的行仅影响该记录；加载程序在
//      解析 失败时跳过 -and-continue。
//   3.on-disk格式为grep-friendly，用于人工审核什么
//      代理已经存储了关于它们的信息。
//
// 两个范围：
//
//   用户 ~/.agentty/memory.jsonl 在 工作区 项目 <workspace>
//   /.agentty/memory.jsonl per-project 中共享，gitignored
//
// `local`范围是故意通过这些工具暴露的NOT——
// agentty的现有内存层次结构中的等价物是人类拥有的用户
// -authored CLAUDE.local.md。
// 让模型写入 "local" 感觉就像模糊了边界。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/util/lock_levels.hpp"

namespace agentty::tools::memory {

namespace fs = std::filesystem;

enum class Scope : std::uint8_t { User, Project };

[[nodiscard]] constexpr std::string_view to_string(Scope s) noexcept {
    return s == Scope::User ? "user" : "project";
}

// 如果字符串不是可识别的范围，则返回 std::nullopt。
[[nodiscard]] std::optional<Scope> parse_scope(std::string_view s) noexcept;

struct Record {
    std::string id;          // 8个十六进制字符；在追加时分配
    std::int64_t ts;         // unix 秒 (UTC) — Last-touched（重复数据删除命中时的碰撞）
    Scope scope;
    std::string text;        // kMaxTextBytes 上限为 `append`
    bool pinned = false;     // 固定记录永远不会在上限溢出时滚动
    std::vector<std::string> tags;  // 可选的分组标签（lower-case、排序、重复数据删除）
    std::int32_t hits = 0;   // 去重附加上的碰撞； "this fact keeps coming up" 信号
};

// 硬上限。超出这些的任何内容都会在附加时被拒绝，
// 因此失控模型无法填充磁盘或用兆字节的 "memory"
// 毒害每个后续系统 提示词。
inline constexpr std::size_t kMaxTextBytes        = 2u * 1024u;    // 2 KiB/条
inline constexpr std::size_t kMaxFileBytes        = 256u * 1024u;  // 256KiB/文件
inline constexpr std::size_t kMaxRecordsPerScope  = 200;           // 线路硬上限
inline constexpr std::size_t kTailLoadCount       = 50;            // 加载 末尾-N 进入 提示词

// ── 提示词-budget 上限(针对 内存 吃上下文)  ⁇ . ──
//
// load_recent_*()返回末尾-N记录，但是系统提示
// 词不能随着存储填满而无限制地增长——否则，存储数百个事实
// 的用户将在EVERY轮上支付提示词的约100KiB，
// 从而将会话更快地推向压缩。 select_for_prompt 在
// 末尾-N 之上应用了严格的 BYTE 预算，
// 因此排名最高的 -signal 事实在预算紧张时仍然存在：
//
//   1.始终保留固定记录（设计为cap-exempt）；
//   2.按排名填充的剩余预算=(点击数，然后新近度)；
//   3.任何长于kPromptRecordCap的单个记录被显示为截断的
//      带有省略号注释（on-disk记录保持完整——仅
//      提示词副本被剪裁）；
//   4.不适合的记录从提示词中删除（仍在磁盘上，
//      仍然可编辑，报告丢弃的计数，以便加载器可以发出
//      "+N more stored"页脚）。
//
// 预算大小使得WHOLE学习的-memory块（两个范围
// 组合）保持几个KiB——相对于200K-token窗口的舍入误差
// ——无论磁盘上累积了多少。
inline constexpr std::size_t kPromptByteBudget = 6u * 1024u;   // ~6 KiB/范围块
inline constexpr std::size_t kPromptRecordCap  = 400;          // 将任意一条记录剪辑到该-prompt中

// 解析范围的on-disk路径。在第一个 `append`
// 上延迟创建父目录 - 这个函数是纯粹的，
// 从不触及文件系统。
//
//   用户 $HOME/.agentty/memory.jsonl (或 Windows 上的%USERPROFILE%)
//   项目 <workspace_root>/.agentty/memory.jsonl
//
// 如果无法解析相关基目录（POSIX上没有$HOME，
// 初始化之前workspace_root()为空），则返回空路径。
[[nodiscard]] fs::path path_for(Scope s);

// 将记录附加到范围文件。生成一个 id，标记当前时间。
// 成功时返回指定的 id；失败时返回 human-readable 错误。
// 强制执行：
//
//   ·修剪后的文本非-empty
//   ·文本大小≤kMaxTextBytes(用注释截断而不是失败）
//   ·追加后的文件大小≤kMaxFileBytes(滚动最旧的未固定记录)
//   ·附加后的记录计数≤kMaxRecordsPerScope(滚动最旧的未固定的)
//
// Dedup:如果`text`(空白规范化之后)与SAME范围
// 内的现有记录紧密匹配，则不写入新记录。相反，现有记录的
// `ts` 会被刷新，`hits` 会递增，并且 `tags`
// / `pinned` 会与传入值合并。返回的 id 是现有
// 记录的 id（将 `note` 设置为 标志，
// 对调用者的 UX 进行重复数据删除）。这是针对模型在会话中重复
// re-asserting 同一事实的主要防御措施。
//
// Pin:`pinned=true`将记录标记为cap-exempt。
// cap-rollover 路径遍历oldest-first，
// 但会跳过固定记录，因此即使在数百个后续追加之后，一个关键事实（构建命令、
// 硬项目约定）仍保持不变。固定记录仍可以通过 `forget` 或
// `wipe` 删除。
//
// 取代：如果`supersedes_id`不是-empty，
// 则在添加新记录的同一重写中自动删除指定记录。
// `forget`-then-`remember`将具有相同的
// 副作用但失去原子性（它们之间的崩溃使存储变成一半-state）。
// 如果取代id不存在，则仍然写入新记录并且
// 注释标志未命中——我们从不拒绝对过期引用的
// 追加。
//
// 标签：无论如何都通过；标准化为 lower-case ASCII，
// 删除重复数据，排序。空标记字符串将被删除。 system-prompt
// 加载程序使用标签对 <learned-memory> 内的标题下的记录进行分组，
// 以便模型可以按主题而不是按时间顺序进行扫描。
//
// 并发性：追加路径采用per-process 互斥量。
// Multi-process 并发写入不同步——唯一现实的写入
// 者是代理本身；人类在文本编辑器 竞态 中编辑 JSONL
// 与代理的下一个写入，就像他们使用任何工具 竞态 一样。
struct AppendOptions {
    bool pinned = false;
    std::vector<std::string> tags;
    std::string supersedes_id;   // 空 ⇒ 无取代
    // 当为假（默认）时，允许去重路径；即使存在near-duplicate，
    // 设置 true 也会强制创建新记录。为明确需要新 ID
    // 的 `forget`+`remember` 呼叫者保留。
    bool no_dedup = false;
};

// MIRROR 的 mcp::tools::MemoryAppendResult (third_party/mcp-cpp/include/mcp/tools/host.hpp):
// 两个承载相同的字段并且桥接AgenttyMemoryStore::append
// (src/tool/mcp_tools_backends.cpp)中的字段-by-field，
// 其持有static_assert，如果它们的大小漂移则跳闸。每个字段都需要一个
// in-class 默认值：no-rollover 路径返回时不会触及 `rolled`，
// 因此未初始化的 `rolled` 被读取为垃圾并打印为 e.g。
// "(140696944861232 old record(s) rolled.)"。
// Add/remove 这里有一个字段，您可以更新镜像+桥接映射。
struct AppendResult {
    std::string id;          // 成功时为 8 个十六进制字符（可能是 dedup 上的现有 ID）
    std::string error;       // 清空成功
    std::string note;        // 当文本被截断、去重、取代、遗漏等时为非-empty。
    std::size_t rolled = 0;  // 为适应上限而丢弃的旧记录的数量
    bool deduped = false;    // true ⇒ 达到现有记录； id 是现有的
};
[[nodiscard]] AppendResult append(const locks::below_memory_store& cap, Scope s,
                                  std::string_view text,
                                  AppendOptions opts = {});

// 读取范围内的所有记录，最早的记录在前。跳过未能到达
// 解析 的行。返回丢失文件的空向量。
[[nodiscard]] std::vector<Record> load_all(const locks::below_memory_store& cap, Scope s);

// 读取 两者 范围内的 末尾-N 最新记录，每个范围内最旧的记录在前。
// 由 system-prompt 构建器使用。 mtime-cached 与
// CLAUDE.md 相同。
[[nodiscard]] std::vector<Record> load_recent_user();
[[nodiscard]] std::vector<Record> load_recent_project();

// 预算提示词选择。获取为 一个 范围返回的记录 load_recent_
// *() 并将其削减以适合 kPromptByteBudget，
// 保留所有固定记录，然后保留最高的 -signal 余数（按点击数，
// 然后是新近度）。以oldest-first稳定顺序返回所选记录
// （因此提示词仍然按时间顺序读取），加上丢弃的号码，以便调用者可以
// 发出"+N more stored"页脚。返回文本超过
// kPromptRecordCap 的记录，其中文本被剪裁 + 省略号标记；
// on-disk 记录未受影响。这是 该 绑定，可阻止不断增长
// 的 内存 商店使每个系统 提示词 膨胀。
struct PromptSelection {
    std::vector<Record> records;   // 已剪裁，最旧-first，在预算范围内
    std::size_t         dropped = 0;   // 记录被删除以符合预算
};
[[nodiscard]] PromptSelection select_for_prompt(
    std::vector<Record> recent,
    std::size_t byte_budget = kPromptByteBudget);

// 忘记准确的ID。返回删除的计数（如果未找到则返回 0）。
[[nodiscard]] std::size_t forget_by_id(const locks::below_memory_store& cap,
                                       std::string_view id);

// 通过跨两个范围的子字符串（case-sensitive)忘记。
// 返回已删除的计数。拒绝以 empty/whitespace 模式运行，
// 因此流浪的 `forget {}` 不会摧毁一切。
[[nodiscard]] std::size_t forget_by_substring(const locks::below_memory_store& cap,
                                              std::string_view needle);

// 预览子串忘记而无需写入。返回WOULD被删除的记录。
// 由 `forget` 工具的 `dry_run` 路径使用，
// 以便模型可以在提交之前确认非 -trivial 擦除。
[[nodiscard]] std::vector<Record> preview_forget_by_substring(
    const locks::below_memory_store& cap, std::string_view needle);

// 擦拭整个瞄准镜。
// 硬 "start afresh on this codebase"
// 开关 — 删除范围文件中的每条记录（文件本身被截断，而不是删除，
// 因此后续追加不会 竞态 重建目录）。返回已删除记录的计数，
// 如果范围路径无法解析（无 $HOME、无 工作区 等），
// 则返回 std::nullopt。 `confirm`门位于工具层；此功能始终擦除。
[[nodiscard]] std::optional<std::size_t> wipe(const locks::below_memory_store& cap, Scope s);

// 渲染为系统提示词中的<learned-memory>块的记录。
// 格式：`[<id>]`+(固定的可选`★ `)+文本。
[[nodiscard]] std::string render_for_prompt(const Record& r);

// 通过id在两个范围内查找。返回记录+其解析范围（可能
// 与任何调用方-assumed范围不同），如果没有记录
// 携带该id，则返回nullopt。由取代路径用来
// 查找前任（无论其位于何处）。
[[nodiscard]] std::optional<std::pair<Record, Scope>>
    find_by_id(const locks::below_memory_store& cap, std::string_view id);

// ── 智能范围推论 ─────────────────────────────────────────────────
// 事实BELONG属于哪个范围？ 提示词 要求模型仅在事实涉及人类时
// 默认投射并接触用户。但是 "project vs user" 是
// 一种语言 调用，模型在 加载 下出错——它将 "I use fish"
// 存储为项目，或将构建命令存储为用户——一旦错误-scoped，
// 事实就会消失（每个 仓库 中都会出现个人偏好 re-asserted，
// 或者当你离开 `cd` 时会丢失项目约定）。
//
// suggest_scope()为工具提供GROUNDED默认值：事实文本上的小评
// 分信号模型，而不是关键字grab-bag。它权衡了实际上区分两个类别的信号——
// 第一人称自我指代，例如 I prefer（我更喜欢），
// "my …"、"call me"）向用户拉动； codebase/repo deixis
// （"this project"、"the build"、"we use"、一条路径、
// 一条命令）向 Project 拉动 — 并报告其信心 + 获胜提示，
// 以便呼叫者可以根据 STRONG 信号采取行动，并在弱信号时保持距离。它永远不会决定：
// 模型的显式`scope`总是获胜；这只能填补模型留下的空白，
// 而且只有在自信的情况下。
struct ScopeSuggestion {
    Scope       scope = Scope::Project;  // 倾向（Project是中立先验）
    float       confidence = 0.0f;       // 0=无信号……1=明确
    std::string reason;                  // 倾斜的提示（对于 notes/tests）

    // 在此之下，将文本视为真正不明确的——回退到
    // 调用者的静态默认值而不是覆盖噪声。
    static constexpr float kActThreshold = 0.30f;
    [[nodiscard]] bool confident() const noexcept {
        return confidence >= kActThreshold;
    }
};
[[nodiscard]] ScopeSuggestion suggest_scope(std::string_view text) noexcept;

} // namespace agentty::tools::memory
