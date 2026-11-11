// skills_panel::scan ——构建读物. -only 排行数.
//
// 一次公开完成,不是每 帧: screen_body
// (走着) 正文 逐行排列,然后重新 -screening
// 每一个技能 帧 与统计小组预测的相同推理——
// 即 缓存 因为它的 生命周期 这正是面板的。

#include "agentty/runtime/panel/skills.hpp"

#include "agentty/tool/skills.hpp"

#include <algorithm>
#include <variant>

namespace agentty::skills_panel {

Open scan(const Library& lib) {
    Open out;

    const auto& all = lib.skills;
    const auto& approvals = lib.skill_approvals;

    out.rows.reserve(all.size());
    for (const auto& s : all) {
        Row r;
        // 一次在边界处的桑尼蒂斯,一切 下游 这里安全 渲染
        // 内 agentty 自己的铬 - 这很重要 因为细节
        // 面板 直接在下面插入一个描述 agentty 标题,
        // 以及可以打开新行的作者文本可以冒充它们(确认的91
        // %) -malicious 技能携带注射)。
        r.name        = tools::skills::sanitize_author_text(s.name, 64);
        r.description = tools::skills::sanitize_author_text(s.description, 300);
        r.origin      = tools::skills::sanitize_author_text(s.origin, 96);
        r.source      = s.source;
        r.dir         = s.dir.string();
        r.effects     = s.effects;
        r.resource_count = s.resources.size();

        r.gated = tools::skills::needs_trust_gate(s.effects);
        r.trusted = std::holds_alternative<scope::Trusted>(
            tools::skills::trust_of(s, approvals));

        // 筛选每一种技能, 不只是效果好的。 -feature
        // 发现是整个原因: 正文 宣言中没有任何内容 出现在98k
        // 的 100%的先进攻击中 -skill 学习,
        // 所以 "declares nothing" 不是一个跳过的理由。
        r.findings = tools::skills::screen_body(s.body);
        r.has_critical = std::ranges::any_of(
            r.findings, [](const tools::skills::Finding& f) {
                return f.severity == tools::skills::Finding::Severity::Critical;
            });

        // 是否另一个范围相同的目录声称这个名字?
        // 丢失的复制品在任何地方都看不见——包括这个面板——
        // 所以胜利者必须携带警告,否则什么也做不了.
        r.shadow_conflict = lib.shadowed_within_scope(s.name);

        out.rows.push_back(std::move(r));
    }

    // 顺序: 需要注意的事情, 然后按字母顺序排列。
    // 一个将标注在四十个干净的技巧下的人,
    // 是一个回答错误问题的查看者。
    std::ranges::stable_sort(out.rows, [](const Row& a, const Row& b) {
        const auto rank = [](const Row& r) {
            if (r.has_critical)          return 0;
            if (r.shadow_conflict)       return 1;   // 东西是隐藏的
            if (r.gated && !r.trusted)   return 2;
            return 3;
        };
        const int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return a.name < b.name;
    });

    return out;
}

} // namespace agentty::skills_panel
