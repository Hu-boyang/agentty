// smart_slot_panel_stack_test — 选择面板 导航是一个 STACK 而不是一个陷阱门。
//
// 降级智能模式 → 模式 选择面板 指定一个角色档,然后退出(
// Esc )或实施( Enter )必须 POP 一个关卡——返回
// 智能模式 选择面板 - 永远不要每次核弹 覆盖面板 回到原点 线程.
// 导航到子站 -setting 击打 Esc 让你继续配置兄弟会的插槽 状态更新函数
// 路径进入 src/runtime/app/update/picker.cpp
// (ModelsSelect 财务报告和已审计财务报表 CloseModels
// 插槽 -assign , 分部门)。
//
// 开车经过 真实 app::update 状态更新函数 ,没有嘲讽 状态更新函数 路径。
//
// 自从 选择面板 合并情况 一个 模型表面(引信,全部) -providers 选择面板 ),
// 这个守护它的槽 -assign 模式—— 模式 状态更新函数 路径进入
// src/runtime/app/update/picker.cpp (
// ModelsSelect 财务报告和已审计财务报表 CloseModels 插槽 -assign
// )加活动 -provider 保持无法流化的交叉范围 -provider 针不代表。

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"  // app::detail::fused_rows_for_model
#include "agentty/runtime/panel/common.hpp"
#include "agentty/domain/smart_mode.hpp"   // kSmartModeRows 移动
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/provider/selection.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pn = agentty::ui::panel;

using namespace agentty;

namespace {

store::Settings g_settings;
void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<Thread> { return std::nullopt; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& x) { g_settings = x; },
        .new_thread_id  = [] { return ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = auth::AuthHeader{auth::ApiKeyHeader{std::string{}}},
    });
}

ModelInfo mi(const char* id, const char* prov) {
    ModelInfo m;
    m.id = ModelId{id};
    m.display_name = id;
    m.provider = prov;
    m.supports_tools = true;
    return m;
}

// A Model 带小 目录 ,打开 真实 OpenModels 路径在槽中
// -assign 模式 `slot` (0战略/1执行/2效用) 镜像 meta.cpp
// 手的手,手的手的手 手的双手 -off 准确:a SmartMode 面板 打开,
// 然后指定 -mode 模型 descend() 被它压住, 面板 骑着马在
// 选择面板's `from` 快照.
Model in_slot_assign(int slot) {
    // 保险丝 选择面板 树 目录 其供养者未经认证。
    // refresh_fused_sources -> provider_is_authed ),
    // 所以测试,只有种子a 目录 得到一个 空 行列表中找不到自己的模型。
    // 给 harmic 键: 这就是 "signed in"
    // 指键预设, 并且它是真正的流 拥有的时间a 选择面板
    // 打开。
    g_settings.provider_keys["anthropic"] = "sk-test";

    Model m;
    // RECORD的设置是refresh_fused_sources现在读作的认证,
    // 而不是接口边界——所以一个手-builtModel必须携带它,这是init
    // ()在启动时的种子方式.
    m.d.persisted = g_settings;
    m.d.model_id = ModelId{"claude-opus-4-5"};
    m.d.available_models = { mi("claude-opus-4-5", "anthropic"),
                             mi("claude-haiku-4-5", "anthropic") };
    // 电话 面板 手 手 脚 -off 留下( 留下什么 Esc。在真实流中,
    // 用户的光标是 ON 时段行 Enter 开枪打手 -off - 电话
    // 快照 随身携带窗体光标 -and-all - 所以在降级前集中作用,
    // 确切地说 meta.cpp 找到它了 找到它了吗?
    auto [m0, _open] = app::update(std::move(m), Msg{OpenSmartMode{}});
    m = std::move(m0);
    if (auto* sm = m.ui.panel.get<pn::SmartMode>())
        smart_form::focus_role(sm->form, static_cast<smart::ModelRole>(slot));
    pn::Models picker{{0, ""}, {}, static_cast<smart::ModelRole>(slot)};
    m.ui.panel.descend(std::move(picker));
    auto [m1, _] = app::update(std::move(m), Msg{OpenModels{}});
    return std::move(m1);
}

} // 命名空间结束。

TEST_CASE("smart slot picker stack") {
    install_stub_deps();
    // 母体:装有引信的选择面板只为AUTHED的供应商配有目录种子,
    // 一个裸试验洞没有-disk凭据(也不取决于任何). provider_keys
    // 条目使provider_is_authed("anthropic")是真的,
    // 所以OpenModels充塞了真行.
    g_settings = store::Settings{};
    g_settings.provider_keys["anthropic"] = "sk-test";
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // ── Esc 位于 -assign 流行回 Smart Mode, 运行 不 退出 ──
    for (int slot = 0; slot <= 2; ++slot) {
        Model m = in_slot_assign(slot);
        auto [m2, cmd] = app::update(std::move(m), Msg{CloseModels{}});

        CHECK(!m2.ui.panel.is<pn::Models>(),
              "Esc closes the model picker");
        CHECK(m2.ui.panel.is<pn::SmartMode>(),
              "Esc RE-OPENS Smart Mode — navigation is a stack, not a trapdoor");
        // 电话 待处理 指定已死亡 WITH 编号 选择面板
        // 值——结构;没有停放 标志 左侧检查,这就是重点。
        if (auto* o = m2.ui.panel.get<pn::SmartMode>()) {
            const auto* row = o->form.focused();
            CHECK(row && row->id == smart_form::field_of_role(
                             static_cast<smart::ModelRole>(slot)),
                  "cursor lands back on the slot row you descended from");
        } else {
            CHECK(false, "smart_mode must be OpenAt after Esc");
        }
        // Esc 不得 已经写了任何东西在插槽。
        const smart::SlotOverride& s =
            slot == 0 ? m2.d.smart.strategic
          : slot == 1 ? m2.d.smart.implementation
                      : m2.d.smart.utility;
        CHECK(!s.set, "Esc abandons the assignment — slot stays unset");
    }

    // ── Enter 在 Lot-assign 中将 和 流行重写为 Smart Mode。 ──
    for (int slot = 0; slot <= 2; ++slot) {
        Model m = in_slot_assign(slot);
        // 将光标明确放在opus行上——由(主动的,最喜欢的,
        // 家族的)而不是原始的目录命令来设定的熔化列表命令.
        const auto rows = app::detail::fused_rows_for_model(m);
        int opus = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (rows[static_cast<std::size_t>(i)].model.id.value == "claude-opus-4-5") {
                opus = i; break;
            }
        REQUIRE(opus >= 0);
        if (auto* c = m.ui.panel.get<pn::Models>()) c->index = opus;

        auto [m2, cmd] = app::update(std::move(m), Msg{ModelsSelect{}});

        CHECK(!m2.ui.panel.is<pn::Models>(),
              "Enter closes the model picker");
        CHECK(m2.ui.panel.is<pn::SmartMode>(),
              "Enter returns to Smart Mode so sibling slots stay one step away");
        // (分数) -assign 结构消耗: 选择面板 价值——及其价值
        // assign_slot ——已不存在).
        if (auto* o = m2.ui.panel.get<pn::SmartMode>()) {
            const auto* row = o->form.focused();
            CHECK(row && row->id == smart_form::field_of_role(
                             static_cast<smart::ModelRole>(slot)),
                  "cursor on the slot just set");
        }

        const smart::SlotOverride& s =
            slot == 0 ? m2.d.smart.strategic
          : slot == 1 ? m2.d.smart.implementation
                      : m2.d.smart.utility;
        CHECK(s.set, "Enter pins the slot");
        CHECK(s.model == "claude-opus-4-5",
              "the highlighted catalog model is written into the slot");
        CHECK(m2.d.smart.enabled,
              "pinning a slot implicitly enables Smart Mode");
        CHECK(m2.d.model_id.value == "claude-opus-4-5",
              "slot-assign must NOT switch the active model");
    }

    // ── 槽 -assign 将列表范围缩放到 活动 提供者 ────────────
    // 固定档模式被发送到任何在转弯时间活跃的提供者,
    // 因此从另一个提供者中排出一行是无法流化的.
    // 名单中不得提供一份——也不得提供一份...... "sign in to X" 划线
    {
        Model m = in_slot_assign(0);
        const auto rows = app::detail::fused_rows_for_model(m);
        REQUIRE(!rows.empty());
        for (const auto& r : rows) {
            CHECK(r.provider_id == "anthropic",
                  "slot-assign lists ONLY the active provider's models");
            CHECK(!r.is_signin_offer(),
                  "slot-assign never offers a provider you aren't signed into");
        }
    }

    // ── 范围界定是 CONDITIONAL 插槽时段 -assign ,非永久的 ──────
    // (十字架) (转动 -provider 广度本身是 fused_models_test 工作,
    // 我们在这里只证明 only_provider 以槽方式应用 -assign
    // 模式和模式 不 应用在它外部——这个过滤器可以合理引入的bug. )
    {
        auto rows_for = [](int slot) {
            Model m;
            m.d.persisted = g_settings;   // 记录,作为 init() 种子,种子,种子
            m.d.model_id = ModelId{"claude-opus-4-5"};
            m.d.available_models = { mi("claude-opus-4-5", "anthropic"),
                                     mi("claude-haiku-4-5", "anthropic") };
            if (slot >= 0) {
                pn::Models picker{{0, ""}, {},
                                  static_cast<smart::ModelRole>(slot)};
                m.ui.panel.descend(std::move(picker));
            }
            auto [m1, _] = app::update(std::move(m), Msg{OpenModels{}});
            return app::detail::fused_rows_for_model(m1);
        };
        const auto plain = rows_for(-1);
        const auto scoped = rows_for(0);
        REQUIRE(!plain.empty());
        REQUIRE(!scoped.empty());
        // 槽 -assign 永远不显示 MORE 而不是没有被扫描的列表,
        // 从没有迹象 -in 提供(你不能给一个你没有签到的模特贴上标签)
        CHECK(scoped.size() <= plain.size(),
              "slot-assign narrows the list, never widens it");
        for (const auto& r : scoped) {
            CHECK(r.provider_id == "anthropic",
                  "slot-assign lists ONLY the active provider's models");
            CHECK(!r.is_signin_offer(),
                  "slot-assign never offers a provider you aren't signed into");
        }
    }

    // ── A NON-slot 模型 -picker Esc 仍然干净退出( 无回归) ──
    {
        Model m;
        m.d.available_models = { mi("claude-opus-4-5", "anthropic") };
        // 普通型号开关——无指定 -mode 选择面板 降职
        auto [m1, _] = app::update(std::move(m), Msg{OpenModels{}});
        auto [m2, cmd] = app::update(std::move(m1), Msg{CloseModels{}});
        CHECK(!m2.ui.panel.is<pn::Models>(), "ordinary Esc closes picker");
        CHECK(!m2.ui.panel.is<pn::SmartMode>(),
              "ordinary model-switch Esc does NOT spuriously open Smart Mode");
    }

    // ── 导航在行类型上关闭 ──────────────────────
    // 电话 覆盖面板 光标曾经是原始的英寸, 四个站点独立地得出了
    // 布局: 列表被切成四行, 两个插槽后, 移动器包裹的modulo
    // 11 -assign 返回返回返回时 -opened 时间
    // `8 + slot` ,并清除 -slot 将每个索引 。
    // \ 3 绘制到工具。 REPRESENTABLE。
    // 光标现在是一个 smart::OverlayRow ,
    // 所以唯一可以检查的是,通过盘点返回的一圈——不能构造一个错误的行来测试。
    {
        Model m;
        auto [m1, _] = app::update(std::move(m), Msg{OpenSmartMode{}});
        Model cur = std::move(m1);

        auto row_id = [](const Model& mm) -> std::string {
            auto* o = mm.ui.panel.get<pn::SmartMode>();
            if (!o) return {};
            const auto* r = o->form.focused();
            return r ? r->id : std::string{};
        };
        auto rows_in = [](const Model& mm) -> int {
            auto* o = mm.ui.panel.get<pn::SmartMode>();
            return o ? static_cast<int>(o->form.fields.size()) : 0;
        };
        auto move = [](Model mm, int d) {
            return app::update(std::move(mm),
                Msg{SmartModeKey{form::keys::Action{
                    d > 0 ? form::keys::Intent::MoveNext
                          : form::keys::Intent::MovePrev}}});
        };

        CHECK(row_id(cur) == smart_form::kFieldEnabled,
              "the overlay opens on the master switch");
        const int n = rows_in(cur);
        CHECK(n > 1, "the pane has a switch plus its slots");

        // 整个圈子都回到开始,每行一次
        //
        // 一步一步走,直到我们回来 而不是为了一个 FIXED 计数 :
        // fields.size () 计数组 头文件 和页脚注,
        // 光标跳过,所以它是可航行行的上方绑定,而不是圈长。
        // -navigable 装饰 面板 恰巧是发射的
        // ——当一个基本物质被打破时,它就被打破了。
        // -tier 路由行添加组 头文件.
        const std::string first_id = row_id(cur);
        std::set<std::string> seen;
        std::string last_seen = first_id;
        int steps = 0;
        for (; steps < n + 1; ++steps) {
            auto [next, c] = move(std::move(cur), +1);
            cur = std::move(next);
            if (row_id(cur) == first_id) break;   // 包起来
            seen.insert(row_id(cur));
            last_seen = row_id(cur);
        }
        CHECK(row_id(cur) == first_id,
              "a full lap down returns to the first row");
        CHECK(seen.size() == static_cast<std::size_t>(steps),
              "one distinct row per step");
        CHECK(steps > 0, "the lap visits at least one row");

        // 从第一排开始,最后一排——之间没有幽灵的排.
        // 电话 最后 排队是不管什么的 注册表 步尾上,
        // 不是一个命名字段: 用于表示 k FieldUtity,
        // 使它成为行的测试 ORDER 而不是包装 -around ,
        // 它第一次打破了 一条路由的把手 被附加在槽后。
        // `last_seen` 是在包装前 上方的行。
        auto [up, c1] = move(std::move(cur), -1);
        cur = std::move(up);
        CHECK(row_id(cur) == last_seen,
              "up from the master switch wraps to the last row");
    }
}
