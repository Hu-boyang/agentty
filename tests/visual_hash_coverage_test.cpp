// visual_hash_coverage_test - 证明 渲染 闸门
// 不能默默地放下可见的改变。
//
// 本测试防范的缺陷类别。
// =========================
// maya 运行循环跳过 view()+
// render() 任何时候 `Program::visual_hash(model)`
// 匹配上一个 帧 '值 (% 1) app.hpp 门是30码
// FPS 廉价地勾选火: 一种没有改变可见的勾选产生同样的散列,
// 成本为零 layout/paint. 危险是反向的——
// 如果是一个模型字段的话。 执行 影响已作出的 帧 但其实
// 不 混合为 visual_hash 一种变异的气体产生同样的
// 散列 门火 变化直到一些 UNRELATED 散列轴碰巧翻转。
// 失败是 SILENT : 没有崩溃, 没有日志, 仅仅是一个区域 UI 停止更新,
// 直到下一个键盘 / 加载动画 勾选。
// 这是生产 "picker arrow keys register once per 4-5 presses"
// 回归( 返回) CHANGELOG): 选择面板 选择索引不在散列中,
// 因此光标移动被关闭。
//
// 该 CONTRACT 国 务 院
// ============
// 影响所显示视图的每一个字段 必须 预发
// visual_hash 字段,相反,字段 DON
// 影响像素(计时器、记号计数器、 取消 手柄) 不得 前进,
// 否则城门被击败,每滴滴都响 重绘 ,重新引入单数
// -frame 排版成本 门存在以避开。
//
// HOW 此 TEST ENFORCES IT
// =========================
// 两份声明表:
//   · kVisualAxes——{名称,变异}。每架:快照 散列,
//                      应用视觉 -meaningful 变异,
//                      表示散列 CHANGED。一个新的视图
//                      -affecting 字段,作者忘记混合到
//                      visual_hash 在此失败, 当他们把它
//                      添加到这个表格时—— 而这个表格是它自然添加的地方,
//                      所以忽略被捕获在同一编辑中。
//   · kInvarantAxes – {name, mutate}。 对于每个: 表示散列已执行
//                      不 换,警卫队对面 -hashing (混合)
//                      last_tick 将做每滴 重绘).
//
// 表格(表) ARE。当您添加视图所读的模型字段时,
// 请在 kVisualAxes 中添加一行;然后测试需要匹配 `mix()`
// 线条在 program.hpp 添加时 临时 中,将视图忽略,
// 在 kInvariantAxes 中添加一行。

#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <map>

#include <maya/style/schemes.hpp>

#include <maya/core/anim_clock.hpp>

#include "agtest.hpp"

#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/model.hpp"

namespace pn = agentty::ui::panel;
namespace smart = agentty::smart;

using agentty::Model;
using agentty::app::AgenttyApp;

namespace {

int g_checks = 0;


std::uint64_t hash_of(const Model& m) { return AgenttyApp::visual_hash(m); }

// 基线模型,即 "settled and idle" ——无活动
// 数据流 ,空作曲家,没有 弹出面板 打开,
// 几条消息 frozen/live 轴是有意义的,每个轴测试都从这个新副本开始,
// 这样轴就不会干扰.
Model baseline() {
    Model m;
    // 发了两封已解密的消息 messages.size() / render_key
    // 斧头有东西可以移动对抗.
    agentty::Message u;
    u.role = agentty::Role::User;
    u.text = "hello";
    m.d.current.messages.push_back(u);
    agentty::Message a;
    a.role = agentty::Role::Assistant;
    a.text = "hi there";
    m.d.current.messages.push_back(a);
    m.s.phase = agentty::phase::Idle{};
    return m;
}

// ── 一种变异,以视觉-meaningful的方式改变一个轴. ──
struct Axis {
    const char*                 name;
    std::function<void(Model&)> mutate;
};

// 字段( W) AFFECT 已交付货物 帧。每个变异必须移动散列。
// 镜像轴 program.hpp::visual_hash 混合;
// 这些表格与函数之间的一个空白是此测试要抓的bug。
const std::vector<Axis>& visual_axes() {
    static const std::vector<Axis> axes = {
        {"messages.size (append a turn)", [](Model& m) {
            agentty::Message x; x.role = agentty::Role::User; x.text = "new";
            m.d.current.messages.push_back(x);
        }},
        {"live tail message text (render_key)", [](Model& m) {
            m.d.current.messages.back().text += " more";
        }},
        {"live tail pending_stream (render_key)", [](Model& m) {
            // 三角洲上只降落在滴滴计时器 缓冲区 仍然必须推进散列,
            // 或者 渲染 门跳过 帧 和活的, 末尾's
            // 逐步显示 停止对 -arming - 电话
            // 数据流 冻结,直到不相关的轴翻转。
            m.d.current.messages.back().pending_stream += "buffered";
        }},
        {"profile cycle", [](Model& m) {
            m.d.profile = (m.d.profile == agentty::Profile::Write)
                        ? agentty::Profile::Ask : agentty::Profile::Write;
        }},
        {"reasoning effort tier", [](Model& m) {
            // 努力层次 渲染 模具徽章(和 选择面板 )),
            // 必须 重绘 . 高 —— 低是一个明显的改变.
            m.d.effort = (m.d.effort == agentty::Effort::High)
                       ? agentty::Effort::Low : agentty::Effort::High;
        }},
        {"model_id swap", [](Model& m) {
            m.d.model_id = agentty::ModelId{std::string{"claude-haiku-4-5"}};
        }},
        {"pending_permission appears", [](Model& m) {
            m.d.pending_permission = agentty::PendingPermission{};
        }},

        // ── 外观 ────────────────────────────────────────────────
        //
        // 每一个 重绘 屏幕,这个门跳过 view()
        // 完全当大麻不移动。 全部 缺少,
        // 这就是为什么选择一个主题看起来毫无作用:
        // 模式改变了,选择坚持,正确的主题得到
        // 解决——以及 帧 从未重建过 所以旧调色
        // 板一直保持 直到一个无关的键盘移动了散列。
        // 板块的脚步保证 "changes apply immediately"
        // 这些行就是它所坚持的
        {"ui theme swap", [](Model& m) {
            m.d.ui().theme = "Dracula";
        }},
        {"ui color tier override", [](Model& m) {
            m.d.ui().tier = agentty::ui_prefs::ColorTier::Ansi16;
        }},
        {"ui background polarity override", [](Model& m) {
            m.d.ui().polarity = agentty::ui_prefs::Polarity::Light;
        }},
        {"ui density", [](Model& m) {
            m.d.ui().density = agentty::ui_prefs::Density::Compact;
        }},
        {"ui prose width cap", [](Model& m) {
            m.d.ui().prose_width = 88;
        }},
        {"ui compact turns", [](Model& m) {
            m.d.ui().compact_turns = true;
        }},
        {"ui motion", [](Model& m) {
            m.d.ui().motion = agentty::ui_prefs::Motion::Off;
        }},
        {"ui syntax highlighting", [](Model& m) {
            m.d.ui().syntax = false;
        }},
        {"ui tool output", [](Model& m) {
            m.d.ui().tool_output = agentty::ui_prefs::ToolOutput::Collapsed;
        }},
        {"ui thinking", [](Model& m) {
            m.d.ui().thinking = agentty::ui_prefs::Thinking::Hidden;
        }},
        {"ui timestamps", [](Model& m) {
            m.d.ui().timestamps = agentty::ui_prefs::Timestamps::Absolute;
        }},
        {"phase Idle -> Streaming", [](Model& m) {
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        }},
        {"status banner text", [](Model& m) {
            m.s.status = "something happened";
        }},
        {"status expiry (100ms bucket)", [](Model& m) {
            m.s.status = "x";
            m.s.status_until = std::chrono::steady_clock::now()
                             + std::chrono::seconds(5);
        }},
        {"spinner frame (while active)", [](Model& m) {
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
            // 电话 加载动画 是时钟 -driven : 将共享动画时钟提前超过一个
            // 完整 帧-interval 这么说吧 frame_index (a) 变动。
            maya::testing::advance_anim_clock_ms(1000);
        }},
        {"composer text", [](Model& m) {
            m.ui.composer.text = "typing";
        }},
        {"composer cursor", [](Model& m) {
            m.ui.composer.text = "abc";
            m.ui.composer.cursor = 2;
        }},
        {"composer attachment count", [](Model& m) {
            m.ui.composer.attachments.push_back(agentty::Attachment{});
        }},
        {"composer queued count", [](Model& m) {
            m.ui.composer.queued.push_back({"queued msg", {}});
        }},
        {"composer expanded toggle", [](Model& m) {
            m.ui.composer.expanded = true;
        }},
        {"frozen prefix grows", [](Model& m) {
            m.ui.frozen.seal(maya::Element{}, 1);
        }},
        {"frozen_turn advances", [](Model& m) {
            m.ui.frozen_turn = 7;
        }},
        {"model_picker opens", [](Model& m) {
            m.ui.panel.descend(pn::Models{{0, ""}});
        }},
        {"model_picker cursor move", [](Model& m) {
            m.ui.panel.descend(pn::Models{{3}});
        }},
        {"model_picker query", [](Model& m) {
            agentty::ui::pick::OpenAt o; o.index = 0; o.query = "free";
            m.ui.panel.descend(pn::Models{std::move(o)});
        }},
        {"provider_picker opens", [](Model& m) {
            m.ui.panel.descend(pn::Providers{{0}});
        }},
        {"provider_picker cursor move", [](Model& m) {
            m.ui.panel.descend(pn::Providers{{2}});
        }},
        {"thread_list opens", [](Model& m) {
            m.ui.panel.descend(pn::ThreadList{{0}});
        }},
        {"thread_list cursor move", [](Model& m) {
            m.ui.panel.descend(pn::ThreadList{{4}});
        }},
        {"thread_list delete confirm", [](Model& m) {
            auto o = agentty::ui::pick::OpenAt{2};
            o.confirm_remove = "abc123";
            m.ui.panel.descend(agentty::ui::panel::ThreadList{std::move(o)});
        }},
        {"thread_list rename draft", [](Model& m) {
            pn::ThreadList t{{0}};
            t.renaming     = "abc123";
            t.rename_input = "new name";
            m.ui.panel.descend(std::move(t));
        }},
        {"thread_list rename caret", [](Model& m) {
            pn::ThreadList t{{0}};
            t.rename_cursor = 3;
            m.ui.panel.descend(std::move(t));
        }},
        {"diff_review opens at cell", [](Model& m) {
            m.ui.panel.descend(pn::DiffReview{{0, 0}});
        }},
        {"diff_review hunk move", [](Model& m) {
            m.ui.panel.descend(pn::DiffReview{{1, 2}});
        }},
        {"command_palette opens", [](Model& m) {
            m.ui.panel.descend(pn::Palette{{}});
        }},
        {"command_palette query", [](Model& m) {
            m.ui.panel.descend(pn::Palette{{"git", 0}});
        }},
        {"command_palette index", [](Model& m) {
            m.ui.panel.descend(pn::Palette{{"git", 5}});
        }},
        {"mention opens", [](Model& m) {
            m.ui.panel.descend(agentty::ui::panel::Mention{});
        }},
        {"mention query", [](Model& m) {
            // 电话 选择面板 现在拥有查询+光标; 驱动它通过自己的动词,
            // 因此测试会练习相同的路径 状态更新函数 已经。
            agentty::mention::Open o;
            o.picker.type(std::string_view{"src"});
            m.ui.panel.descend(pn::Mention{std::move(o)});
        }},
        {"mention index", [](Model& m) {
            agentty::mention::Open o;
            o.picker.type(std::string_view{"src"});
            o.picker.jump_to(3);
            m.ui.panel.descend(pn::Mention{std::move(o)});
        }},
        {"symbol opens", [](Model& m) {
            m.ui.panel.descend(agentty::ui::panel::Symbol{});
        }},
        {"symbol query", [](Model& m) {
            agentty::symbol::Open o;
            o.picker.type(std::string_view{"foo"});
            m.ui.panel.descend(pn::Symbol{std::move(o)});
        }},
        {"symbol index", [](Model& m) {
            agentty::symbol::Open o;
            o.picker.type(std::string_view{"foo"});
            o.picker.jump_to(2);
            m.ui.panel.descend(pn::Symbol{std::move(o)});
        }},
        {"todo modal opens", [](Model& m) {
            m.ui.todo.open = agentty::ui::pick::OpenModal{};
        }},
        {"tool viewer opens", [](Model& m) {
            m.ui.panel.descend(pn::ToolOutput{{{}, 0, false}});
        }},
        {"tool viewer list cursor move", [](Model& m) {
            m.ui.panel.descend(pn::ToolOutput{{{}, 2, false}});
        }},
        {"tool viewer list -> body stage", [](Model& m) {
            m.ui.panel.descend(pn::ToolOutput{{{}, 0, true}});
        }},
        {"tool viewer body scroll", [](Model& m) {
            m.ui.panel.descend(pn::ToolOutput{{{}, 0, true}});
            m.ui.tool_viewer_scroll.y = 5;
        }},
        {"tool viewer live tail toggled", [](Model& m) {
            m.ui.panel.descend(pn::ToolOutput{{
                {agentty::tool_output::Entry{}}, 0, /*查看=( ====*/true}});
            m.ui.tool_viewer_tail = false;
        }},
        {"code block picker opens", [](Model& m) {
            m.ui.panel.descend(pn::CodeBlocks{{{}, 0}});
        }},
        {"code block picker cursor move", [](Model& m) {
            m.ui.panel.descend(pn::CodeBlocks{{{}, 3}});
        }},
        {"login modal opens", [](Model& m) {
            m.ui.login = agentty::ui::login::Picking{};
        }},
        {"login chatgpt waiting", [](Model& m) {
            m.ui.login = agentty::ui::login::ChatGptWaiting{};
        }},
        {"settings_list opens (Plugins)", [](Model& m) {
            agentty::settings::ListOpen o;
            o.concern = agentty::settings::Category::Plugins;
            m.ui.panel.descend(agentty::ui::panel::SettingsList{o});
        }},
        {"settings_list cursor move", [](Model& m) {
            agentty::settings::ListOpen o;
            o.concern = agentty::settings::Category::Plugins;
            o.index = 3;
            m.ui.panel.descend(agentty::ui::panel::SettingsList{o});
        }},
        {"settings_list add-mode input", [](Model& m) {
            agentty::settings::ListOpen o;
            o.concern = agentty::settings::Category::Plugins;
            o.input_active = true;
            o.input = "date -- /path/date_server";
            o.cursor = 4;
            m.ui.panel.descend(agentty::ui::panel::SettingsList{o});
        }},
        {"plugins loading flag", [](Model& m) {
            m.ui.plugins_loading = true;
        }},
        {"plugins snapshot lands (server connected)", [](Model& m) {
            agentty::mcp::ServerState s;
            s.name = "date";
            s.connected = true;
            s.tools.push_back({"current_date", "", true, false});
            m.ui.plugins.servers.push_back(std::move(s));
        }},
        {"plugins server disabled toggles the hash", [](Model& m) {
            agentty::mcp::ServerState s;
            s.name = "date";
            s.disabled = true;   // 与已断开的普通
            s.tools.push_back({"current_date", "", true, false});
            m.ui.plugins.servers.push_back(std::move(s));
        }},
        {"smart_mode overlay opens", [](Model& m) {
            agentty::smart_form::Inputs in;
            in.enabled = true;
            m.ui.panel.descend(pn::SmartMode{{}, agentty::smart_form::build_form(in)});
        }},
        {"smart_mode cursor move", [](Model& m) {
            agentty::smart_form::Inputs in;
            in.enabled = true;
            auto f = agentty::smart_form::build_form(in);
            agentty::smart_form::focus_role(f, agentty::smart::ModelRole::Utility);
            m.ui.panel.descend(pn::SmartMode{{}, std::move(f)});
        }},
        {"rag picker opens", [](Model& m) {
            agentty::rag_settings::Open o;
            m.ui.panel.descend(agentty::ui::panel::Rag{o});
        }},
        {"rag picker cursor move", [](Model& m) {
            agentty::rag_settings::Open o;
            o.cursor = agentty::store::RagMode::Off;
            m.ui.panel.descend(agentty::ui::panel::Rag{o});
        }},
        {"fork picker opens", [](Model& m) {
            m.ui.panel.descend(pn::Fork{{agentty::fork_panel::Choice::RagPerTurn}});
        }},
        {"fork picker cursor move", [](Model& m) {
            m.ui.panel.descend(pn::Fork{{agentty::fork_panel::Choice::RagOff}});
        }},
    };
    return axes;
}

// 显示视图 不 修改这些 不得 移动散列,或 渲染 闸门被
// 击败( 每一次勾选) 重绘 这些是合同的双重性:
// visual_hash 整个价值在于它是什么 OMITS.
const std::vector<Axis>& invariant_axes() {
    static const std::vector<Axis> axes = {
        {"last_tick clock", [](Model& m) {
            m.s.last_tick = std::chrono::steady_clock::now()
                          + std::chrono::hours(1);
        }},
        {"token counters", [](Model& m) {
            m.s.tokens_in  = 12345;
            m.s.tokens_out = 67890;
        }},
        {"pending rehydrate trim", [](Model& m) {
            m.ui.pending_rehydrate_trim = true;
        }},
    };
    return axes;
}

}  // 名称空间 (帮助)

TEST_CASE("visual axes advance hash") {
    std::printf("visual_hash: each view axis advances the hash\n");
    for (const auto& ax : visual_axes()) {
        Model before = baseline();
        const std::uint64_t h0 = hash_of(before);
        Model after = baseline();
        ax.mutate(after);
        const std::uint64_t h1 = hash_of(after);
        check(h0 != h1,
              std::string("axis '") + ax.name +
              "' did NOT change visual_hash — the view reads it but "
              "program.hpp::visual_hash forgot to mix() it. Renders for "
              "this change will be gated away (silent dead region).");
    }
}

TEST_CASE("invariant axes preserve hash") {
    std::printf("visual_hash: non-visual axes do NOT advance the hash\n");
    for (const auto& ax : invariant_axes()) {
        Model before = baseline();
        const std::uint64_t h0 = hash_of(before);
        Model after = baseline();
        ax.mutate(after);
        const std::uint64_t h1 = hash_of(after);
        check(h0 == h1,
              std::string("axis '") + ax.name +
              "' CHANGED visual_hash but the view ignores it — every "
              "Tick will now defeat the render gate and repaint. Remove "
              "its mix() from program.hpp::visual_hash.");
    }
}

// 卫生:基线本身在两个方面是稳定的 调用 (无时间性泄漏,
// 用于 settled/idle 模式——制度(c):无动画。
TEST_CASE("idle settled is stable") {
    // 将动画钟按住,这些案例表明,两条散列的动画
    // UNCHANGED 模式同意,只有在时间不能在两者之间
    // 移动的情况下才有意义。 调用. visual_hash
    // 混合时间 BUCKET 每当有动画的时候, Model
    // 没有任何信息,所以欢迎的bob算作动画。
    // 调用 可能跨越一个桶边缘,但不同意:可靠地说,
    // `ctest -j12` ,在其中 加载 时间被冻结,
    // 不只是读一次,所以这个断言测试了 MODEL 而不是排程器。
    maya::testing::freeze_anim_clock(1000000);
    std::printf("visual_hash: settled idle model is hash-stable across calls\n");
    Model m = baseline();
    const std::uint64_t a = hash_of(m);
    // 无变异,无睡眠——闲置的定居模式(无活动) 数据流 ,
    // 一个非 -empty 线程 所以欢迎的bob关闭,
    // 空队列) 一定不能贡献时间期限,所以两个读对了.
    const std::uint64_t b = hash_of(m);
    check(a == b,
          "idle settled model produced two different hashes — a time "
          "term is leaking into a regime (c) state (should be none).");
}

// ── 加载动画 门 : 事件订阅 ,推进和散列必须同意 ────────────
//
// 一次,我给你带了个小礼物 -based 此应用程序中的动画通过
// 三个 独立大门,所有三个必须命名相同的条件:
//
//   1. subscribe.cpp - 滴答已经送来了吗?
//   2. update/meta.cpp - 做吗? 加载动画 提克的车?
//   3. program.hpp - 新的吗? 帧 更改视线散列
//                        (i.e。 做任何事情。 重绘)?
//
// 分歧是 SILENT: a 帧 以无所见的、无所见地的、
// 无足轻重的、无有所见的; 渲染 装在未变的格子上 选择
// 面板's "loading …" 加载动画 需要全部
// 三个 `models_loading` 被设定——冷冻
// 的格言读作绞刑,这与它现在所说的完全相反.
TEST_CASE("visual hash: spinner advances the hash while models load") {
    // 冻得如此 仅 时间动议是下面的明显进步。
    maya::testing::freeze_anim_clock(2000000);
    Model m;
    REQUIRE(!m.s.active());          // 闲置: 流式传输 关机了
    m.s.models_loading = true;

    const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
    // 电话 加载动画 获取其 帧 从共享时钟中移动, 全部通过 帧-interval
    // 这么说吧 frame_index (a) 变动。
    maya::testing::advance_anim_clock_ms(1000);
    const auto h1 = agentty::app::AgenttyApp::visual_hash(m);
    CHECK(h0 != h1);
    maya::testing::unfreeze_anim_clock();
}

TEST_CASE("visual hash: an idle picker with no load does NOT animate") {
    maya::testing::freeze_anim_clock(2000000);
    // 补充:无 models_loading (而不是) 简洁的 流式传输 ) ,
    // 或 , / 或。 加载动画 不得进入散列或闲置 agentty 重
    // 绘 永远的,一个歌词 没有人在看。 STRONGER 比旧的
    // advance()-based 检查:带有时钟 -driven 加载
    // 动画 我们断言散列是时间 -independent 直接——移动时钟(
    // 移动时钟) 加载动画's 帧 ) 不得移动散列.
    Model m;
    REQUIRE(!m.s.active());
    m.s.models_loading = false;

    const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
    // 均等 -neutral 前进:530 ms闪烁周期的倍数,
    // 所以 光标 等值桶( 4 个) now/265 & 1 不变,
    // 而 加载动画 (80 ms/frame )通过~13移动 帧。在测试中隔离轴线。
    maya::testing::advance_anim_clock_ms(1060);
    CHECK(agentty::app::AgenttyApp::visual_hash(m) == h0);
    maya::testing::unfreeze_anim_clock();
}

TEST_CASE("visual hash: spinner animates while a PICKER catalog loads") {
    // 冻:这说明大麻 CHANGES 一人的电话 加载动画 步骤,
    // 即一个活时钟可以偶然满足(一个桶翻)而不是由
    // 加载动画 帧 案子实际上就快了
    maya::testing::freeze_anim_clock(2000000);
    // 电话 选择面板 向每个获得认证的提供者发出风扇,并跟踪每个获取
    // ProviderCatalog::state - 它从来没有设置
    // Session::models_loading (涵盖 活动 仅提供商:
    // 提供商切换/ 启动。 gating the 加载动画 错的 标志 把它
    // 冻在它存在的表面, 它读起来像绞刑。
    Model m;
    REQUIRE(!m.s.active());
    REQUIRE(!m.s.models_loading);
    agentty::ProviderCatalog c;
    c.provider_id = "openai";
    c.state = agentty::ProviderCatalog::State::Loading;
    m.d.provider_catalogs.push_back(std::move(c));
    m.ui.panel.descend(agentty::ui::panel::Models{});
    REQUIRE(m.loading_spinner_visible());

    const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
    maya::testing::advance_anim_clock_ms(1000);   // 移动 加载动画's 帧
    CHECK(agentty::app::AgenttyApp::visual_hash(m) != h0);
    maya::testing::unfreeze_anim_clock();
}

TEST_CASE("visual hash: a READY catalog does not animate") {
    maya::testing::freeze_anim_clock(2000000);
    Model m;
    agentty::ProviderCatalog c;
    c.provider_id = "openai";
    c.state = agentty::ProviderCatalog::State::Ready;
    m.d.provider_catalogs.push_back(std::move(c));
    m.ui.panel.descend(agentty::ui::panel::Models{});
    REQUIRE(!m.loading_spinner_visible());

    const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
    // 均等 -neutral 高级(530毫秒的倍数闪烁周期)——见
    // 闲置 -picker 上方的大小写; 移动 加载动画 ~13
    // 帧 时 光标 等价桶留在原地
    maya::testing::advance_anim_clock_ms(1060);
    CHECK(agentty::app::AgenttyApp::visual_hash(m) == h0);
    maya::testing::unfreeze_anim_clock();
}

// ── 一个部件 -paced 视觉得到 NO 寄件者的时间期限 ────────────
//
// 设计规则在这里执行,而不是记住: 视觉就是 EITHER 东道主 -paced
// (我们手持计时器, 并桶 散列在那段时间) OR 部件 -paced
// (该词 调用 request_animation_frame (后)和
// maya 运行循环 渲染 绝对不会是两者兼有
//
// 两者都是破解的: agentty 桶装了 RAF-driven
// 欢迎屏幕 流式传输-tick 周期(超过sh的80毫秒),
// 而 welcome_screen.hpp 要求110毫秒,
// 两个独立的时钟 一个视觉,所以 渲染 着陆部分 -way 通过动画步骤——
// 每30个请求42个散列值 帧 和一副闲置的欢迎屏 闪烁了而不是一闪而过
//
// maya::animation_pending (a) 报告 "a widget already owns the next frame".
// 虽说如此, visual_hash 不得提供任何时间期限: maya's
// RAF 覆盖 渲染 这些 帧 不论这种大麻,我们加的桶 都只是一秒钟,
// 出去 -of-phase 钟声。
TEST_CASE("visual hash: a widget-paced frame gets no host time bucket") {
    Model m;
    m.d.current.messages.clear();          // 欢迎屏幕 : RAF-driven
    REQUIRE(!m.s.active());

    // 模拟一个要求下一个部件的部件 帧 ,
    // 完全与 welcome_screen.hpp 挂载时 build().
    maya::request_animation_frame_after(110);
    REQUIRE(maya::animation_pending());

    // 以帧 待处理,散列必须是时间-INDEPENDENT:
    // 扫射比任何可信的桶要宽得多,要求一个单一值。
    std::set<std::uint64_t> distinct;
    for (std::int64_t t = 0; t < 3300; t += 7) {
        maya::testing::freeze_anim_clock(1000000 + t);
        distinct.insert(agentty::app::AgenttyApp::visual_hash(m));
    }
    CHECK(distinct.size() == 1);

    // 和对白: NO 部件 -paced 帧 待处理 ,
    // 一个主机 -paced 动画仍然得到它的桶—— 推迟不能禁用散列本身的动画。
    // 活动转弯为主机。 -paced (键 -driven).
    maya::detail::animation_requested_ = false;
    REQUIRE(!maya::animation_pending());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    REQUIRE(m.s.active());
    std::set<std::uint64_t> active_hashes;
    for (std::int64_t t = 0; t < 3300; t += 7) {
        maya::testing::freeze_anim_clock(1000000 + t);
        active_hashes.insert(agentty::app::AgenttyApp::visual_hash(m));
    }
    CHECK(active_hashes.size() > 1);

    maya::testing::freeze_anim_clock(1000000);
}

// ── 闲着 WELCOME 屏幕必须是散列 -stable ───────────────────────
//
// 欢迎屏幕是没有消息的状态,
// 所以它比 baseline() 用户在
// 打字前的视线最长,而输入的视线最大。
// 重绘 他们没要求的是最明显的
//
// 动画钟被冻结了 visual_hash 一定是个
// PURE 函数: 2 调用 在一个未受污染的
// Model 不同之处在于散列读取模型以外的东西,
// "the screen changed" ,
// 所以它会 重绘 ,并显示 光标 正在无故重画用户的下方。
TEST_CASE("visual hash: an idle welcome screen is hash-stable") {
    maya::testing::freeze_anim_clock(1000000);

    Model m;                          // 无消息: 欢迎屏幕
    REQUIRE(!m.s.active());
    REQUIRE(m.d.current.messages.empty());

    const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
    for (int i = 0; i < 16; ++i) {
        CHECK(agentty::app::AgenttyApp::visual_hash(m) == h0);
    }

    // 推进时钟不能移动散列 WHEN 该 TERMINAL
    // OWNS 该 CARET。这就是要用硬件来标注的 光标
    // maya 画画无 光标 存储单元 和附表编号 帧 ,
    // 所以没有明显的步骤 一个时间桶跟踪, 混合一个使循环 重绘
    // 反 对 终端 自己眨眼的 闪烁的
    //
    // 与 PAINTED 光标 恰恰相反, maya 眨眼问她 帧
    // - 所以,一个对等的术语是正确的,散列 SHOULD 移动。
    // 无条件使用时间-independence将是一个错误。
    if (agentty::ui::composer_uses_hardware_caret(m)) {
        maya::request_animation_frame_after(110);
        for (std::int64_t t = 0; t < 2000; t += 37) {
            maya::testing::freeze_anim_clock(1000000 + t);
            CHECK(agentty::app::AgenttyApp::visual_hash(m) == h0);
        }
    }
    maya::detail::animation_requested_ = false;
    maya::testing::freeze_anim_clock(1000000);
}

// ── 3-gate协议,结构上 ───────────────────────
//
// animation_demand / reveal_needs_frames /
// reveal_draining (subscribe.hpp )是: "something is moving"
// 本案认定,这些前提物的存在是为了保证:
//
//   ARMED ⇒ HASHED : 任何勾选状态 事件订阅 手持计
//   时器 REVEAL 理由必须是: visual_hash 带一个
//   时间期限——否则循环唤醒,散列门会丢弃每个钟:武装 帧 ,
//   冷冻像素(中间) -glide 反面(带武器)是故意的 不
//   主张: 部件 -paced 财务报告和已审计财务报表 光标
//   -parity 条件为: maya 属于自己的 帧 消息来源,
//   武器在Tick外
//
// 伪造: 杀死排水桶 visual_hash 三个排水管都
// 失效了 -shaped 两种诚实的限制,通过尝试加以核实:
//   • reveal_needs_frames' REASONING 术语为cadence -only 今天... . . .. .
//     fine_anim_live (m.s.active ()坐在
//     链条下面,为同样的状态混合一个较宽的桶,所以将这个词掉下来会降解
//     16ms= 33/100ms 同步时的平滑度 终端 无法冻结散列。
//     -in-depth 故意的;这种试验不能(也不应该)在
//     粗鲁的情形下失败。
//   • 排水状态是: 加载-bearing 其中一个:在那扇窗口里 两者
//     m.s.active (a) 与 fine_anim_live 假的,
//     所以排水桶是... ... 仅 时间期限——确切原因,杀它在这里失败。
TEST_CASE("visual hash: every reveal-armed state carries a time term") {
    struct Case {
        const char* name;
        std::function<void(Model&)> setup;
    };
    const Case cases[] = {
        {"streaming answer bytes", [](Model& m) {
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
            agentty::Message a; a.role = agentty::Role::Assistant;
            a.streaming_text = "partial answer";
            m.d.current.messages.push_back(std::move(a));
        }},
        {"pure reasoning phase", [](Model& m) {
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
            m.d.show_reasoning = true;
            agentty::Message a; a.role = agentty::Role::Assistant;
            a.thinking = "chain of thought so far";
            m.d.current.messages.push_back(std::move(a));
        }},
        {"undrained bytes after active dropped", [](Model& m) {
            agentty::Message a; a.role = agentty::Role::Assistant;
            a.pending_stream = "tail not yet drained";
            m.d.current.messages.push_back(std::move(a));
        }},
        {"settle-freeze pending", [](Model& m) {
            agentty::Message a; a.role = agentty::Role::Assistant;
            a.text = "done";
            m.d.current.messages.push_back(std::move(a));
            m.ui.pending_settle_freeze = true;
        }},
        {"settle cooldown", [](Model& m) {
            agentty::Message a; a.role = agentty::Role::Assistant;
            a.text = "done";
            m.d.current.messages.push_back(std::move(a));
            m.ui.settle_cooldown_ticks = 2;
        }},
    };

    for (const auto& c : cases) {
        Model m;
        c.setup(m);
        // 仅对以下声明 事件订阅 实际武器用于 逐步显示
        // 理由——正在检验的协议。
        if (!agentty::app::animation_demand(m)) continue;

        maya::testing::freeze_anim_clock(3000000);
        const auto h0 = agentty::app::AgenttyApp::visual_hash(m);
        // 一个 逐步显示 桶是16-33ms;300ms跨越数个,
        // 不管 终端 同步模式。 如果没有时间词混合,
        // 散列被冻结, 武装唤醒 渲染 没什么
        maya::testing::freeze_anim_clock(3000000 + 300);
        const auto h1 = agentty::app::AgenttyApp::visual_hash(m);
        maya::testing::unfreeze_anim_clock();

        INFO(c.name);
        CHECK_MESSAGE(h0 != h1,
            c.name << ": the Tick subscription arms for this state but "
            "visual_hash mixes no time term — armed frames would be gated "
            "away and the animation freezes until a keypress");
    }
}

// 每个建的-in计划名称必须明确散开。
//
// 视轴只被守护,如果 不同 值产生不同的散列。
// 主题轴通过 "advances the hash"
// 因为在上文中,由于这一测试将一个主题与基线进行比较,
// 因此从未将两个主题相互比较。
//
// mix_str ()样品(长度+第一+最后+中间) 字节 ),
// 这是正确的一个50 KB 工具输出和错误的系统名称。
// 615个名称中的76个在它下面相撞,并且由于该选项
// 上的样本键 ENDS 相撞的邻居之间 —— 正是您
// 通过浏览器浏览的对:
//
//   酸液 - >冒险,玫瑰松明 - >玫瑰松月,.
//
// 一次着陆改变了模型, 发表了主题, 离开大门说 "nothing visual moved"。
// 无 重绘。主题仅在一些无关的密钥移动散列后出现 --
// 按键直到再按一次才生效。
//
TEST_CASE("visual_hash: every scheme name hashes distinctly") {
    std::printf("visual_hash: %zu scheme names must not collide\n",
                std::size(maya::theme::schemes));

    std::map<std::uint64_t, std::string> seen;
    std::vector<std::string> collisions;

    for (const auto& s : maya::theme::schemes) {
        Model m = baseline();
        m.d.ui().theme = s.name;
        const std::uint64_t h = hash_of(m);
        if (auto it = seen.find(h); it != seen.end())
            collisions.push_back(it->second + " == " + s.name);
        else
            seen.emplace(h, s.name);
    }

    if (!collisions.empty()) {
        std::printf("  %zu collision(s):\n", collisions.size());
        for (std::size_t i = 0; i < collisions.size() && i < 10; ++i)
            std::printf("    %s\n", collisions[i].c_str());
    }
    check(collisions.empty(),
          "two schemes share a visual_hash — switching between them is a "
          "silent no-repaint. Hash the theme name in FULL rather than "
          "sampling it (program.hpp).");
}
