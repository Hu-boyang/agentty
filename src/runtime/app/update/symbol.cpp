// symbol_update-状态更新函数为`msg::SymbolMsg`.
// 与mention.cpp并行(即@file 选择面板);唯一的区别是候选
// 类型(SymbolEntry vs string)和所选类型的标签(
// Attachment::Symbol vs FileRef).
//
// 现在每只手臂都是单臂 调用 输入共享 FilteredPicker :
// 数据 记忆缓存 寒冷的风 -open 重新填充和光标夹是原始人的工作,
// 不是每个手臂都重做 -derives (在补注的情况下,只有 一个
// 手臂曾经是用来做,这就是为什么感冒 -opened 选择面板 箭头不是
// 箭头 带类型的 继续 "indexing…" 永远。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/panel/symbol.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

Cmd symbol_update(Model& m, msg::SymbolMsg sm) {
    return std::visit(overload{
        // (无) OpenSymbol 臂: 面板从 COMPOSER's `#` 处理函数 ,
        // 该结构将构建 pn::Symbol 直接——见 composer.cpp.)
        [&](CloseSymbol) -> Cmd {
            ascend(m);   // 通常 ~ 线程(通过打字打开#),或父
            return Cmd::none();
        },
        [&](SymbolInput& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Symbol>()) o->picker.type(e.ch);
            return Cmd::none();
        },
        [&](SymbolBackspace) -> Cmd {
            auto* o = m.ui.panel.get<pn::Symbol>();
            if (!o) return Cmd::none();
            // Backspace 空查询关闭 选择面板。该词 选择面板
            // 报告 "there was nothing to erase"
            // 所以测试和突变不能分开
            if (!o->picker.backspace()) m.ui.panel.close<pn::Symbol>();
            return Cmd::none();
        },
        [&](SymbolMove& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Symbol>()) o->picker.move(e.delta);
            return Cmd::none();
        },
        [&](SymbolSelect) -> Cmd {
            auto* o = m.ui.panel.get<pn::Symbol>();
            if (!o) return Cmd::none();
            // `selected()` 归并 空的-list和 exout
            // -of-range 检查进入类型:无效结果是唯一的失败模式.
            const auto* sym = o->picker.selected();
            if (!sym) {
                m.ui.panel.close<pn::Symbol>();
                return Cmd::none();
            }
            Attachment att;
            att.kind        = Attachment::Kind::Symbol;
            att.name        = sym->name;
            att.path        = sym->path;
            att.line_number = sym->line_number;
            m.ui.panel.close<pn::Symbol>();

            std::size_t idx = m.ui.composer.attachments.size();
            m.ui.composer.attachments.push_back(std::move(att));
            auto placeholder = attachment::make_placeholder(idx);
            m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
            m.ui.composer.cursor += static_cast<int>(placeholder.size());
            return Cmd::none();
        },
    }, sm);
}

} // namespace agentty::app::detail
