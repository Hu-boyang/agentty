#pragma once
// 面板视图——每个面板一个函数，名为<name>_panel，
// 在src/runtime/view/panels/<name>.cpp中定义。
// 统一规则：面板名称（与其状态头文件、状态更新函数文件、插槽替代和Msg系列同字）+_panel。
// 对于大多数人来说，view.cpp 的 pick_panel() 是唯一的调用者。
#include "agentty/runtime/model.hpp"

#include <maya/element/element.hpp>

namespace agentty::ui {

[[nodiscard]] maya::Element models_panel(const Model& m);
[[nodiscard]] maya::Element providers_panel(const Model& m);
[[nodiscard]] maya::Element thread_list_panel(const Model& m);
[[nodiscard]] maya::Element smart_mode_panel(const Model& m);
[[nodiscard]] maya::Element web_search_panel(const Model& m);
[[nodiscard]] maya::Element paste_settings_panel(const Model& m);
[[nodiscard]] maya::Element plugin_edit_panel(const Model& m);
[[nodiscard]] maya::Element appearance_panel(const Model& m);
[[nodiscard]] maya::Element sandbox_panel(const Model& m);
[[nodiscard]] maya::Element sandbox_list_panel(const Model& m);
[[nodiscard]] maya::Element palette_panel(const Model& m);
[[nodiscard]] maya::Element mention_panel(const Model& m);
[[nodiscard]] maya::Element symbol_panel(const Model& m);
[[nodiscard]] maya::Element code_blocks_panel(const Model& m);
[[nodiscard]] maya::Element code_block_result_panel(const Model& m);
[[nodiscard]] maya::Element tool_output_panel(const Model& m);
[[nodiscard]] maya::Element checkpoints_panel(const Model& m);
[[nodiscard]] maya::Element rag_panel(const Model& m);
[[nodiscard]] maya::Element stats_panel(const Model& m);
[[nodiscard]] maya::Element skills_panel(const Model& m);
[[nodiscard]] maya::Element settings_list_panel(const Model& m);
[[nodiscard]] maya::Element fork_panel(const Model& m);
[[nodiscard]] maya::Element todo_panel(const Model& m);

} // namespace agentty::ui
