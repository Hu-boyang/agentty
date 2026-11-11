// stats.cpp - 统计查看器 状态更新函数.
//
// 三条手臂,也就是整个表面:打开、步骤标签、关闭。
// 面板是记录的投影,因此没有选择、没有承诺和坚持的
// 余地——见domain/stats.hpp.。
//
// 统计本身不是 此处计算。从现场记录(用印章刻印)中得出,
// 该记录保存了“E”字。 状态更新函数 免责 快照
// 可能去的 可能去 过期 下一转弯的那一刻 着陆

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/skills.hpp"
#include "agentty/runtime/panel/stats.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using namespace agentty::msg;

Cmd stats_update(Model& m, msg::StatsMsg sm) {
    return std::visit(maya::overload{
        [&](OpenStats) -> Cmd {
            // descend() ,而不是赤裸的开口:从命令
            // 调色板上可以到达查看器,所以 Esc 必须返回
            // 用户的来处,而不是关闭所有。
            // 共享 ascend() 包装器在出门时处理。
            pn::Stats pane{};
            m.ui.panel.descend(std::move(pane));
            // 不需要卷轴重置: 抵消寿命 ON 和面板是新鲜的。
            // Model::UI 都长了 open/close ,
            // 所以重新打开后,将上次关闭时的偏移恢复到
            // 不同的标签和标签上。 max_y 描述两
            // 个错误——需要在此明确重置以修复的bug,
            // 以及 所有权 使无法写作。
            return Cmd::none();
        },

        [&](CloseStats) -> Cmd {
            ascend(m);
            return Cmd::none();
        },

        [&](StatsTab e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Stats>();
            if (!o) return Cmd::none();
            // 步骤 可见 设置,所以 Tab 这条带子
            // 从未在任何视图上降落。这需要《事实》,
            // 因为数据是否可用是一个问题。 refresh()
            // 同一摊销 -O(1) 调用 图像显示,
            // 投影是共享的,所以这不会增加成本。
            const auto& f = o->projection.refresh(m.d.current);
            o->tab = stats::tab_step(o->tab, e.delta, f);
            // 一个新的标签从顶端开始。 背负上一个标签的
            // 偏移意味着从一个高标签转换为一个短标签打开它
            // 滚动过自己的内容。 用户看到一个空白的正文,
            // 没有理由这样做。
            //
            // max_y 故意的 故意的,故意的,是故意的,
            // 这是故意的 不 重置此选项。 该字段为
            // max 所拥有 渲染器 写回,所以 状态更新函数
            // 无法将新标签的高度。 过期 完全一个 帧 ,
            // 它同义 -batch 滚动到旧标签的范围之内,
            // 而不是新标签的;油漆再将其夹住。将它零到这里
            // "be safe" 将完全吞下键盘,
            // 这是比用户无法看到的瞬间更糟糕的交易。
            o->scroll.scroll_to_origin();
            // 预测数为每 -transcript 单人 -tab ,
            // 因此切换视图 -folds 没什么
            return Cmd::none();
        },
        [&](StatsScroll e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Stats>();
            if (!o) return Cmd::none();
            // 滚动 正文 直接来 scroll_by (c)
            // 打击 两者 结束,所以三角洲过去要么在边缘上安顿,
            // 而不是卷成空白的行——这就是导航发送的±1000000
            // 用来 Home/End 依赖。
            //
            // 通关 ScrollState 而不是说 `y += delta` 手,
            // 手,手 手,双手,手,双手 -rolled 版本只守
            // 护下方的束缚,所以 End y = 1000 000,
            // 模型带有自己的值 不变量 禁止直到下一张油漆夹住它。
            // -clamp 打开 渲染 ,这就是为什么看起来没有破损
            // —— 但这使得模型的正确性依赖于一个 帧 发生,
            // 任何 状态更新函数 在此之前读作y( 相同)
            // -batch StatsScroll 之后的 StatsTab ,
            // 还有斋戒 终端 提供数个密钥 一个 读起来,
            // 它永远是合法的。
            //
            // max_y仍然是最后的帧在这里:它是由渲染器的回
            // 写书写的,所以在标签切换后的第一个卷轴上它描述了
            // PREVIOUS标签. Clamping 逆着过期绑定
            // 和selm-correcting;完全不夹击,
            // 因为同样的原因,StatsTab 重新ets y 至 0.
            o->scroll.scroll_by(0, e.delta);
            return Cmd::none();
        },

        // ── 技能查看器 ──────────────────────────────────────────
        // 三臂,没有第四臂:没有批准的信息。
        // msg.hpp ——您已经滚动的列表中可以
        // 得到的核准是习惯的"是",决定属于调查结果和
        // 结果所在的 正文。可以显示。
        [&](OpenSkills) -> Cmd {
            // 扫描结果 此处 一次,不是在视图中,
            // 视图是纯的预兆;筛选每个技能 正文 时值
            // 帧 生成一个无法在面板打开时改变的值的值,
            // 将按 -frame 代表 快照.
            pn::Skills pane{};
            static_cast<skills_panel::Open&>(pane) = skills_panel::scan(m.ui.library);
            m.ui.panel.descend(std::move(pane));
            return Cmd::none();
        },

        [&](CloseSkills) -> Cmd {
            ascend(m);
            return Cmd::none();
        },

        [&](SkillsMove e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Skills>();
            if (!o || o->rows.empty()) return Cmd::none();
            // 与每个列表(单步)一样环绕;页止于结尾。
            o->index = ui::step(o->index, e.delta, static_cast<int>(o->rows.size()));
            // 新选择意味着一个新的细节脚步;
            // 出于StatsTab同样的原因从顶部开始.
            o->scroll.scroll_to_origin();
            return Cmd::none();
        },
    }, std::move(sm));
}

} // namespace agentty::app::detail
