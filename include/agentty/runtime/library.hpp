#pragma once
// agentty::Library-安装在磁盘上的内容：技能、
// 其批准、斜线命令和钩子文件，作为值。
//
// 在 工作线程 (cmd::load_library) 上读取并在 Model
// 上保持，因此 状态更新函数 和面板在此处查找内容，而不是在 UI
// 线程 上遍历磁盘。在启动时和每回合后重新加载。

#include <string>
#include <vector>

#include "agentty/scope/scope.hpp"
#include "agentty/tool/commands.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/util/io.hpp"

namespace agentty {

struct Library {
    bool                                  loaded = false;
    std::vector<tools::skills::Skill>     skills;
    std::vector<tools::skills::Shadowed>  shadowed;
    scope::Approvals                      skill_approvals;
    std::vector<tools::commands::Command> commands;
    std::string                           hooks_file;      // "" 当没有时
    bool                                  hooks_pending = false;
    // 来自项目.agentty/agents/.的子代理类型任务卡对它们进行标记；
    // 从视图中读取目录将是磁盘 IO。
    std::vector<std::string>              project_agents;

    [[nodiscard]] bool is_project_agent(std::string_view name) const noexcept {
        for (const auto& a : project_agents)
            if (a == name) return true;
        return false;
    }

    [[nodiscard]] const tools::skills::Skill* skill(std::string_view name) const noexcept {
        for (const auto& s : skills)
            if (s.name == name) return &s;
        return nullptr;
    }
    // 相同的范围，相同的名称，两个目录：值得标记。
    [[nodiscard]] bool shadowed_within_scope(std::string_view name) const noexcept {
        const auto* w = skill(name);
        if (!w) return false;
        for (const auto& sh : shadowed)
            if (sh.name == name && sh.source == w->source) return true;
        return false;
    }
};

// 从磁盘读取以上所有内容。仅限 工作线程-side。
[[nodiscard]] Library read_library(Io);

}  // namespace agentty
