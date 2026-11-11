#pragma once
// 技能查看器 — 一个 read-only 窗口，显示已安装的内容及其可以执行
// 的操作。
//
// 从命令选项板（Ctrl+K→"Skills"）打开它。 ↑/↓ 移动，
// Esc 关闭。这里没有任何东西会改变任何东西，这是故意的：
// 这个小组确实批准了 不 技能。
//
// ── 为什么没有批准钥匙 ──────────────────────────────────────────
//
// Anthropic测得Claude代码用户认可93%
// 的权限 提示词； Akhawe & Felt 测量
// Chrome 的 SSL 插页式广告的点击率为 70%。
// 距离您已经滚动的列表只需按一下键的批准是无人阅读
// 的批准 - 它成为习惯性的默认值，这正是安装流程旨在
// 避免的失败模式。
//
// 因此，小组 ANSWERS 提出问题（
// 安装了什么、声明了什么、检查发现了什么，以及是否获批。
// 并将一项行动提交给 `agentty skill approve NAME`，
// 其中同意屏幕可以显示调查结果和 正文。这里可见，
// 那里决策。
//
// 相同的推理排除了"trust all skills" 切换。
// 这样的开关成为每个安装指南的第一行。
//
// ── 住在哪里吗? ─────────────────────────────────────────────────────
//
//   tool/skills.hpp技能，EffectSet，trust_of，
//   screen_body panel/skills.hpp这个：选择+滚动，
//   没有别的view/panels/skills.cpp视图：Model→SkillsPane::Config
//
// 这些行是磁盘上的PROJECTION。与统计面板一样，
// 缓存 存在于打开的面板上，因为它的 生命周期 正是面板
// 的：打开它进行扫描，关闭它则免费，并且从不打开它的用户无需
// 支付任何费用。

#include "agentty/runtime/library.hpp"
#include "agentty/scope/scope.hpp"
#include "agentty/tool/skills.hpp"

#include <maya/core/scroll_state.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace agentty::skills_panel {

// 一排：一技能，已解决。视图所需的一切，在打开时计算一次，
// 而不是按 帧 计算 — screen_body() 逐行遍历 正文，
// 这很便宜，但不是免费的，而一个对每个 帧 上的每一项技能进行重新
// -screened 的面板将是免费的。
struct Row {
    std::string name;
    std::string description;   // 已经消毒——见下文
    std::string origin;        // "" 手-authored
    std::string source;        // "user" | "project"
    std::string dir;
    tools::EffectSet effects{};

    // 在扫描时解决了信任。
    bool gated   = false;      // 声明副作用，因此信任适用
    bool trusted = false;      // 批准，或散文，或用户-authored

    // 筛选发现什么在正文。几乎所有技能都是空的。
    std::vector<tools::skills::Finding> findings;
    bool has_critical = false;

    // 同一范围内的另一个目录声明了该名称，因此技能位于磁盘上，
    // 并且永远不会加载。失败的副本在任何地方都是不
    // 可见的——包括这里——所以获胜的行带有标志。
    bool shadow_conflict = false;

    std::size_t resource_count = 0;
};

struct Open {
    // 选择哪一行。 可变，因为当列表缩小到下方时，
    // 视图会更正它（另一个 终端 中删除了一项技能）
    // ——指向末尾会 渲染 一个空的细节 面板，
    // 没有任何解释。
    mutable int index = 0;

    // 正文滚动，OWNED BY 该 PANEL-与stats_panel::Open相同的推理：
    // 视图读取的卷轴必须是帧门看到的卷轴，并且visual_parts+
    // parts_cover_all在类型上证明了这一点。存在于 Model::UI 上
    // 使得 "panel ignored me, then caught up when I typed"
    // 类 bug 成为可能。
    mutable maya::ScrollState scroll = [] {
        maya::ScrollState s;
        s.auto_dispatch = false;   // 状态更新函数拥有该偏移量
        return s;
    }();

    // 扫描。打开时已充满；每 帧 不刷新。
    //
    // 这里的Author-controlled字符串是ALREADY清理的
    // (sanitize_author_text)，因为它们是在agentty
    // 自己的chrome内呈现的——Snyk的2026年2月审计中
    // 确认的-malicious技能中的91%进行了提示词注入，
    // 并且可以打开新行的描述可以模仿它周围的帧。
    std::vector<Row> rows;

    [[nodiscard]] bool empty() const noexcept { return rows.empty(); }

    [[nodiscard]] const Row* selected() const noexcept {
        if (rows.empty()) return nullptr;
        const auto i = static_cast<std::size_t>(
            index < 0 ? 0
                      : (static_cast<std::size_t>(index) >= rows.size()
                             ? rows.size() - 1
                             : static_cast<std::size_t>(index)));
        return &rows[i];
    }

    // 头文件行的计数——在此处计算，因此视图保持纯
    // 格式化程序并且可以在没有渲染的测试中断言数字。
    [[nodiscard]] std::size_t pending_count() const noexcept {
        std::size_t n = 0;
        for (const auto& r : rows) if (r.gated && !r.trusted) ++n;
        return n;
    }

    [[nodiscard]] std::size_t flagged_count() const noexcept {
        std::size_t n = 0;
        for (const auto& r : rows) if (r.has_critical || r.shadow_conflict) ++n;
        return n;
    }
};

// 将每个发现的技能扫描成行：解析信任、筛选 正文、
// 清理作者文本。对于 Model 来说是纯粹的——
// 它读取技能存储和批准文件，然后返回。
[[nodiscard]] Open scan(const Library& lib);

} // namespace agentty::skills_panel
