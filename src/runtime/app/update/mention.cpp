// mention_update — 状态更新函数 对于
// `msg::MentionMsg`。 @file 选择面板 读取
// 工作区 文件中的 快照 并按 -keystroke
// 过滤它。选择时，类型 FileRef 的附件将附加到
// composer.attachments，并在光标处插入内联 SOH 占位符；
// 文件的 字节 在提交时 (modal.cpp) 之后是 已加载，
// 因此将读取已编辑的文件及其最新内容。
//
// 电话 snapshot/memo/cursor 力学是共享的
// FilteredPicker's (panel/filtered_picker.hpp
// 特别是, COLD-OPEN REFILL :它曾经只生活在
// `MentionInput` 臂在后面 手放在后面 `o->files.empty()` 警卫,
// 所以 选择面板 索引发布前打开,然后箭头向上(而不是 带类型的 ——
// 一旦查询是非 -empty 警卫再也无法开火了, 钉住 选择面板
// 键盘中的任何部分列表。现在每一次都会出现重填。 READ 会 议 日
// 程 和 议 程 快照 ,按建筑计算共计。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/panel/mention.hpp"
#include "agentty/workspace/files.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

Cmd mention_update(Model& m, msg::MentionMsg mm) {
    return std::visit(overload{
        // (无) OpenMention 臂: 面板从 COMPOSER's `@` 处理函数 ,
        // 该结构将构建 pn::Mention 直接与文件列表一起——参见 composer.cpp .
        // . .. . ) . ...... . : . 无人寄来的信息在这里坐了几个月. )
        [&](CloseMention) -> Cmd {
            ascend(m);   // 通常 ~ 线程(通过打字打开@),或父
            return Cmd::none();
        },
        [&](MentionInput& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Mention>()) o->picker.type(e.ch);
            return Cmd::none();
        },
        [&](MentionBackspace) -> Cmd {
            auto* o = m.ui.panel.get<pn::Mention>();
            if (!o) return Cmd::none();
            // Backspace 空查询关闭 选择面板 ——与命令调
            // 色板+大多数聊天应用程序的同等价格。 选择面板 答案( T)
            // 此前是否有内容可删除？
            // 因此测试和突变不能分开。
            if (!o->picker.backspace()) m.ui.panel.close<pn::Mention>();
            return Cmd::none();
        },
        [&](MentionMove& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Mention>()) o->picker.move(e.delta);
            return Cmd::none();
        },
        [&](MentionSelect) -> Cmd {
            auto* o = m.ui.panel.get<pn::Mention>();
            if (!o) return Cmd::none();
            const auto* hit = o->picker.selected();
            if (!hit) {
                m.ui.panel.close<pn::Mention>();
                return Cmd::none();
            }
            // COPY ,不移动: 快照 共享,
            // 不可变 现在,所以从中偷走字符串会腐蚀所有
            // 其它读者的缓存索引(以及下一个 `@` 看见了,
            // 旧动作很安全,因为每次 选择面板 拥有一份私人
            // 的深层文件清单——这正是我们删除的复制件。
            std::string path = *hit;
            m.ui.panel.close<pn::Mention>();

            // 频率:这个路径刚刚被引用——在下个`@`
            // 的顶端排位. 便宜,有界的复发窗口.
            note_file_referenced(path);

            // 附录a FileRef 附件+插入其占位符,
            // 位于作曲光标。 正文 提交时填充(
            // modal.cpp )所以在选择和发送之间编辑
            // 的文件会以其当前状态到达模型 字节.
            Attachment att;
            att.kind = Attachment::Kind::FileRef;
            att.path = std::move(path);
            std::size_t idx = m.ui.composer.attachments.size();
            m.ui.composer.attachments.push_back(std::move(att));

            auto placeholder = attachment::make_placeholder(idx);
            m.ui.composer.text.insert(m.ui.composer.cursor, placeholder);
            m.ui.composer.cursor += static_cast<int>(placeholder.size());
            return Cmd::none();
        },
    }, mm);
}

} // namespace agentty::app::detail
