// form_keys.cpp——每个表都服从一个关键地图.
//
// 纯度:(焦点,密钥) - > 意图,然后是意图 - > 突变。
// 否 面板 内容 -derives 导航,所以 面板 无法分解,
// 整个相互作用模型 通过喂食合成 KeyEvents 没有 终端 随函附上。

#include "agentty/runtime/panel/form_keys.hpp"

#include "agentty/runtime/panel/nav.hpp"

namespace agentty::form::keys {

namespace {

using maya::KeyEvent;
using maya::SpecialKey;

[[nodiscard]] const SpecialKey* special(const KeyEvent& ev) {
    return std::get_if<SpecialKey>(&ev.key);
}

} // 命名空间结束。

std::optional<Action> translate(const Form& f, const KeyEvent& ev) {
    return translate(f.editing(), f.choosing(), ev);
}

std::optional<Action> translate(bool editing, bool choosing, const KeyEvent& ev) {
    const auto sk = special(ev);
    const auto ch = ui::nav::char_view(ev);   // 归并 原始 ctrl 数据 字节 转到字母+ctrl

    // ── 选择:浮列表拥有所有东西 ──────────────────
    // 降水只持有枚举,所以密钥地图是故意小的:
    // 没有过滤器, 没有查询。 需要搜索的集合是打开一个真实的
    // 选择面板 的 Pick 列。
    if (choosing) {
        if (sk) switch (*sk) {
            case SpecialKey::Escape: return Action{Intent::MenuCancel};
            case SpecialKey::Enter:  return Action{Intent::MenuCommit};
            case SpecialKey::Up:     return Action{Intent::MenuPrev};
            case SpecialKey::Down:   return Action{Intent::MenuNext};
            default: break;
        }
        // 吞下其他一切——包括裸体打印(一个关键政策:
        // 字母永远不行动)——所以开放的菜单不能漏出其下方
        // 的表单的密钥.
        return Action{Intent::None};
    }

    // ── 编辑: 可打印密钥编辑, 它们不会导航 ─────────
    if (editing) {
        if (sk) switch (*sk) {
            case SpecialKey::Escape:
            case SpecialKey::Enter:     return Action{Intent::LeaveField};
            // Tab 问 PANE 表格中没有完整字的字段忽略
            // ( apply() 因此,这是无所事事的,
            // 它不是被通缉的,而且无条件翻译并不花费什么。
            case SpecialKey::Tab:       return Action{Intent::Complete};
            // QQ/QQ 离开外地 和 移动( E) apply() 结束编辑第一个。
            // 没有这个,编辑表就吞下他们了。 Enter 文本行上,
            // 然后读作箭头 "the panel froze"
            // 唯一的出口是两把钥匙 没有任何广告
            case SpecialKey::Up:        return Action{Intent::MovePrev};
            case SpecialKey::Down:      return Action{Intent::MoveNext};
            case SpecialKey::Backspace: return Action{Intent::Backspace};
            case SpecialKey::Delete:    return Action{Intent::DeleteForward};
            case SpecialKey::Left:      return Action{Intent::CaretLeft};
            case SpecialKey::Right:     return Action{Intent::CaretRight};
            case SpecialKey::Home:      return Action{Intent::CaretHome};
            case SpecialKey::End:       return Action{Intent::CaretEnd};
            default: break;
        }
        if (ch && ch->ctrl) {
            if (ch->c == U'u') return Action{Intent::ClearField};
            if (ch->c == U'a') return Action{Intent::CaretHome};
            if (ch->c == U'e') return Action{Intent::CaretEnd};
            return Action{Intent::None};    // 不要把^X泄露给导航
        }
        if (ch) return Action{Intent::Insert, ch->c};
        return Action{Intent::None};
    }

    // ── 浏览 ─────────────────────────────────────────────────────
    if (sk) switch (*sk) {
        case SpecialKey::Escape:   return Action{Intent::Close};
        case SpecialKey::Enter:    return Action{Intent::Activate};
        case SpecialKey::Up:       return Action{Intent::MovePrev};
        case SpecialKey::Down:     return Action{Intent::MoveNext};
        case SpecialKey::Left:     return Action{Intent::AdjustDown};
        case SpecialKey::Right:    return Action{Intent::AdjustUp};
        case SpecialKey::Home:     return Action{Intent::MoveFirst};
        case SpecialKey::End:      return Action{Intent::MoveLast};
        case SpecialKey::PageUp:   return Action{Intent::MovePageUp};
        case SpecialKey::PageDown: return Action{Intent::MovePageDown};
        // Backspace,而BROWSING仍然是Backspace.
        //
        // 曾经跌落到无,默默失落,这就是为什么在打开名单后,
        // 第一家媒体什么也没做: 面板 在浏览中打开,
        // 因此密钥从未到达 apply() 用户不得不按两次,
        // 却从未知道为什么。
        //
        // 翻译它确实有NOT使它具有破坏性 -- --
        // apply()在触摸字节之前仍然需要编辑会话(见
        // 它的Backspace臂,那里关于永远不破坏值的
        // 注释无法在光标中看到),这所购买的是按键的ARRIVES,
        // 所以面板可以给它一个意义,默认会保留一个
        // -op而不是路由器中的一个洞.
        case SpecialKey::Backspace: return Action{Intent::Backspace};
        // Delete 有个洞 Backspace 也一样,
        // 也一样的关闭: 翻译它 apply() (或 减
        // 减 面板 )可以赋予它一个意义,而不是在路由器中丢失,
        // 让用户去想为什么一个删除键到达字段,
        // 另一个则没有.
        case SpecialKey::Delete:    return Action{Intent::DeleteForward};
        default: break;
    }
    if (ch && ch->ctrl) {
        // 没有 ^S: Esc IS 编号 保存 离开一个领域,它承诺( Esc 箭头,
        // Enter 和。 和 简 况 Esc 关闭行关卡 面板 - 这么
        // 说吧 "write it and get out" 谓 Esc ,
        // 或 ,或 Esc-Esc 同一行为第二键是学习的第二键,
        // 第二键是保持同步的第二键。
        if (ch->c == U'x') return Action{Intent::ResetField};
        return std::nullopt;                // ^C 朋友们要保持环境
    }
    if (ch) {
        // ── 空可打印 绝不 行为(无) -surprises 关键政策) ───
        // 他们 TYPE 文本上的内容 -like 行( 类型)
        // -to-edit ——编辑会话从第一个字符开始,否 Enter
        // 并且别无他法。
        //
        // 排行类型由 apply(), 不 在此。 路由器翻译
        // 了整个输入 BATCH 反对 一个 焦点 快照 原记录 帧 启动,
        // 因此任何光标 -dependent 做决定 过期 即刻,
        // 批次包含一个动作:按下QQ,然后在同一批次中有一个字母将
        // 该字母翻译为对面的 旧 排行和 DROPPED ——报道.
        // "input doesn't register".
        // 进取 TypeToEdit 无条件保留路由器光标
        // -independent (其创始合同:三公尺,
        // 无排); apply() 看见真正的焦点行, 忽略意图,
        // 当它不是文本 -like.
        return Action{Intent::TypeToEdit, ch->c};
    }
    return std::nullopt;
}

Applied apply(Form& f, Action a) {
    Applied out;
    Field* row = f.focused();
    const int row_at = f.cursor;

    // End 编辑会话(如果有)并记录承诺 -on-exit:
    // 离开值改变的字段 IS 编号 保存 信号,
    // 一个帮手 Enter/Esc/arrows/PgUp/Home 大家都同意——这里的
    // 一条不同的退出道路是一个从未默默承诺的领域。
    auto leave_edit = [&] {
        if (!f.editing()) return;
        (void)escape(f);
        if (f.edit_dirty) {
            out.left_field = true;
            out.settled = row_at;
            f.edit_dirty = false;
        }
    };

    switch (a.intent) {
        case Intent::None: break;

        case Intent::MovePrev:
        case Intent::MoveNext:
            // 箭头时 EDITING : 离开实地(承诺 带类型的
            // ——文本编辑在 -place ,所以,
            // 我说 "commit" 刚刚掉下来 光标 ,然后移动。
            // 结束编辑首先保留 不变量 编辑焦点总是指光标行。
            leave_edit();
            move(f, a.intent == Intent::MovePrev ? -1 : +1);
            break;

        case Intent::MoveFirst: leave_edit();
                                 move_edge(f, /*最后一个=*/false); break;
        case Intent::MoveLast:  leave_edit();
                                 move_edge(f, /*最后一个=*/true);  break;
        // 可视区域-sized 脚步。表单不知道 可视区域 (部件关切),
        // 所以要固定的步法,以匹配默认 panel_viewport_h
        // 保留 PgUp/PgDn 具有意义,但不能返回布局 -channel;
        // move_page CLAMPS 边缘( 不包) 和跳过
        // 头文件.
        case Intent::MovePageUp:   leave_edit();
                                   move_page(f, -10); break;
        case Intent::MovePageDown: leave_edit();
                                   move_page(f, +10); break;

        case Intent::AdjustDown:
        case Intent::AdjustUp:
            if (row && row->editable()) {
                adjust(row->value, a.intent == Intent::AdjustUp ? +1 : -1);
                f.dirty = true;
                out.changed = true;
            }
            break;

        case Intent::Complete:
            // 故意什么也没有,完成就是 PANE 关注 - 它需要候选的组合,
            // 而表层对此没有任何了解。 面板 拨打电话前检查此意图
            // apply() (见A/C.5/61/L.48,第16段)
            // update/sandbox.cpp 'list editor;
            // 到达这里意味着没有提供完整的, 而键盘是惰性的。
            break;

        case Intent::Activate: {
            const auto r = activate(f);
            out.fired    = (r == Activated::FiredAction);
            out.hand_off = (r == Activated::HandOff);
            out.changed  = (r == Activated::Changed);
            break;
        }

        case Intent::ResetField:
            // 电话 面板 拥有什么 "default" 表示一行,
            // 因此此只表示意图; a 面板 绝对不会忽略它
            break;

        case Intent::Close:
            // Esc 编辑时离开字段,并像其他出口一样承诺(在
            // -place 编辑表示 Esc 永远没有退路, 让它一个没有
            // 承诺的退出将是沉默 -drift )),""(""").
            if (f.editing()) { leave_edit(); break; }
            out.close = escape(f);
            break;

        // ── 编辑-only意图 ──────────────────────────────────
        // 由警卫队负责 TRUE 焦点,不是打电话者的信念。
        // subscribe.cpp 将整批输入翻译为 一个
        // FormFocus 快照 (在排水口排水后才重建),
        // 所以,用糊片 "abc\ndef" 翻译 d/e/f 插入,
        // 即使\n 已经离开字段 —— 没有这个守护,
        // 用户将不再编辑这些字符。。 状态更新函数 是唯一一个
        // 看到真实模式的图层; 意图来自 过期 快照 死在这里。
        case Intent::TypeToEdit:
            // 类型 -to-edit : 可打印 带类型的 时 BROWSING
            // 文本 -like 行开始编辑会话,并登陆作为它的第插入。
            // 仅 translate() 发出此信息, 且仅在路由
            // 器说焦点行是文本时 -like 和 焦点是浏览——所以
            // 不能从一个 过期 批次( 以插入方式到达并在编辑时死亡)
            // -focus 删除密钥时故意这样做。
            // 不 自动 -enter : 一个流浪者 Backspace
            // 绝不能破坏价值 字节 用户无法看到 光标 进来吧
            if (!f.editing() && row && row->is_text_like()
                && row->editable() && !row->locked) {
                // ACCEPT 行是这个字符吗?
                //
                // 数字行只取数字,输入一个用于输入编辑会话的字母:
                // 焦点翻转到编辑中, edit_dirty 是真的,
                // 而且,我还得去 number_insert 默然放下了角色,
                // 然后这排就活下来了 "changed" 屏幕上没有显示它,
                // 所以下一个 Esc 承诺不 -op 编辑,
                // 箭头表现为 光标 移动而不是行移动, 面板 读作
                // 吞下键盘。
                // "alpha char misbehaves everywhere".
                //
                // 已检查 之前 任何状态动作,所以一个被拒绝的字符完全是
                // 一个不 -op 而不是半个 -entered 届会。
                const bool numeric_row =
                    std::holds_alternative<field::Number>(row->value);
                if (numeric_row && (a.ch < U'0' || a.ch > U'9')) break;

                begin_edit(f);
                // 毫不含糊地是会话的第一个键盘,
                // 所以一个数字行将这个数字作为整数值,
                // 而不是将其附加到已保存 one上.
                insert(row->value, a.ch, /*新鲜=*/true);
                out.changed = true;
                f.edit_dirty = true;
            }
            break;
        case Intent::Insert:
            if (f.editing() && row && row->editable()) {
                // 数字行只取数字。插入一个字母是否定的 -op
                // 报告 `changed` 武装犯罪
                // -on-exit 用于一个从未发生过的编辑 --
                // -- 同一半 -state 使数字行上的一字母
                // 读作 面板 吞下钥匙。
                //
                // 检查过了 KIND 而不是通过比较数值: FieldValue
                // 是一个 变体 类型,非平等 -comparable ,
                // 而Number是唯一可以拒绝一个字符的种类。
                if (std::holds_alternative<field::Number>(row->value)
                    && (a.ch < U'0' || a.ch > U'9'))
                    break;

                // 新鲜到THIS会话中某物改变值。
                // 这么说 Enter-then-type 表现得像类型 -to-edit 以上:
                // 首位数字替换,其余的附加。
                insert(row->value, a.ch, /*新鲜=*/!f.edit_dirty);
                out.changed = true;
                f.edit_dirty = true;
            }
            break;
        case Intent::Backspace:
            if (f.editing() && row && row->editable()) {
                backspace(row->value);
                out.changed = true;
                f.edit_dirty = true;
            }
            break;
        case Intent::DeleteForward:
            if (f.editing() && row && row->editable()) {
                delete_forward(row->value);
                out.changed = true;
                f.edit_dirty = true;
            }
            break;
        case Intent::CaretLeft:
            if (f.editing() && row) move_cursor(row->value, -1);
            break;
        case Intent::CaretRight:
            if (f.editing() && row) move_cursor(row->value, +1);
            break;
        case Intent::CaretHome:
            if (f.editing() && row) cursor_home(row->value);
            break;
        case Intent::CaretEnd:
            if (f.editing() && row) cursor_end(row->value);
            break;
        case Intent::ClearField:
            if (f.editing() && row && row->editable()) {
                clear(row->value);
                out.changed = true;
                f.edit_dirty = true;
            }
            break;
        case Intent::LeaveField:
            leave_edit();
            break;

        case Intent::MenuPrev:      dropdown_move(f, -1); break;
        case Intent::MenuNext:      dropdown_move(f, +1); break;
        case Intent::MenuCommit:    out.changed = dropdown_commit(f); break;
        case Intent::MenuCancel:    (void)escape(f); break;
    }

    if (out.changed) f.dirty = true;
    // 进一进 -place 变化一次结算; a 带类型的 一个安顿下来 leave_edit.
    if (out.changed && !f.editing() && out.settled < 0) out.settled = row_at;
    return out;
}

} // namespace agentty::form::keys
