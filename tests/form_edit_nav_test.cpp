// form_edit_nav_test 箭头 NAVIGATE 退出编辑字段。
//
// 退后,换成 "panel froze" 报告: Enter 文本上的内容
// -like 行数( Model, Host , Port...)开始编辑,
// 编辑键表用于吞进QQ/QQ——唯一出口是 Enter/Esc 屏幕上没有广告
// 驱动REAL 状态更新函数(更新)和真景(渲染),
// 使用看门狗,因此任何真悬挂都会以42号出口而不是楔形测试失败.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <maya/print.hpp>
#include <maya/core/render_context.hpp>
#include <maya/element/builder.hpp>

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/store/store.hpp"

using namespace agentty;
namespace pn = agentty::ui::panel;

static agentty::store::Settings g_settings;

static Model step(Model m, Msg msg, const char* what) {
    std::fprintf(stderr, "[step] %s\n", what);
    auto st = app::update(std::move(m), std::move(msg));
    return std::move(st.first);
}

static void render_once(const Model& m, const char* what) {
    std::fprintf(stderr, "[render] %s\n", what);
    maya::RenderContext ctx{100, 30, maya::render_generation(), false};
    maya::RenderContextGuard g(ctx);
    auto el = ui::view(m);
    (void)maya::render_to_string(el, 100);
    std::fprintf(stderr, "[render-ok] %s\n", what);
}

int main() {
    // 看门狗 : 任何吊 - > 出口 42. 离子 线程 而不是说
    // alarm()/SIGALRM 所以它的工作在 Windows ;
    // 当主返回时, 它随进程而死亡。
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::fprintf(stderr, "HANG DETECTED (watchdog fired)\n");
        std::_Exit(42);
    }).detach();

    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const Thread&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const agentty::store::Settings& s) { g_settings = s; },
        .new_thread_id  = [] { return ThreadId{"t-repro"}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = {},
    });

    Model m;
    m = step(std::move(m), Msg{OpenSmartMode{}}, "open smart mode");
    render_once(m, "smart mode open");

    // Enter 以焦点字段为主 切换 ) (中文(简体) ).
    auto* o = m.ui.panel.get<pn::SmartMode>();
    if (!o) { std::fprintf(stderr, "no smart pane!\n"); return 1; }
    using K = maya::SpecialKey;
    if (auto act = form::keys::translate(o->form, maya::KeyEvent{K::Enter}))
        m = step(std::move(m), Msg{SmartModeKey{*act}}, "enter on focused field");
    render_once(m, "after enter");

    // 逐行走动,按 Enter 每个人, 渲染 各随后.
    for (int i = 0; i < 12; ++i) {
        auto* s = m.ui.panel.get<pn::SmartMode>();
        if (!s) break;
        if (auto down = form::keys::translate(s->form, maya::KeyEvent{K::Down}))
            m = step(std::move(m), Msg{SmartModeKey{*down}}, "down");
        s = m.ui.panel.get<pn::SmartMode>();
        if (!s) break;
        if (auto enter = form::keys::translate(s->form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{SmartModeKey{*enter}}, "enter");
        render_once(m, "after enter walk");
    }

    // ── rag 面板: Enter 打开模式下拉/编辑文本字段 ───
    m = step(std::move(m), Msg{OpenRag{}}, "open rag");
    render_once(m, "rag open");
    // 截图场景：在 Choice 行按 Enter 打开下拉框，
    // 然后,Up/Down必须移动菜单突出显示。
    {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "no rag pane for dropdown test\n"); return 1; }
        // 光标位于第0行( 主动, a Choice )。 Enter - > 滴水。
        if (auto enter = form::keys::translate(r->embed.form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*enter}}, "dropdown: enter");
        r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "pane gone after enter\n"); return 1; }
        std::fprintf(stderr, "choosing=%d\n", r->embed.form.choosing() ? 1 : 0);
        if (!r->embed.form.choosing()) {
            std::fprintf(stderr, "FAIL: Enter did not open the dropdown\n");
            return 1;
        }
        const auto hl_before = [&] {
            const auto* d = r->embed.form.dropdown();
            return d ? d->highlighted : -999;
        }();
        if (auto down = form::keys::translate(r->embed.form, maya::KeyEvent{K::Down}))
            m = step(std::move(m), Msg{RagEmbedKey{*down}}, "dropdown: down");
        else
            std::fprintf(stderr, "FAIL: Down not translated while choosing\n");
        r = m.ui.panel.get<pn::Rag>();
        const auto hl_after = [&] {
            const auto* d = r ? r->embed.form.dropdown() : nullptr;
            return d ? d->highlighted : -999;
        }();
        std::fprintf(stderr, "highlight %d -> %d\n", hl_before, hl_after);
        if (hl_after == hl_before) {
            std::fprintf(stderr, "FAIL: Down did not move the dropdown highlight\n");
            return 1;
        }
        render_once(m, "dropdown after down");
        // 关上它,再走下去。
        if (auto esc = form::keys::translate(r->embed.form, maya::KeyEvent{K::Escape}))
            m = step(std::move(m), Msg{RagEmbedKey{*esc}}, "dropdown: esc");
    }

    // ── THE reported freeze: Enter on a text field starts editing; ↑/↓
    // 仍然必须导航( 离开字段+移动) , 而不是去死。 @ @ info: status
    {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "no rag pane for edit test\n"); return 1; }
        // 下行到Model行(一个文本字段).
        for (int i = 0; i < 2; ++i)
            if (auto down = form::keys::translate(r->embed.form, maya::KeyEvent{K::Down})) {
                m = step(std::move(m), Msg{RagEmbedKey{*down}}, "to text row");
                r = m.ui.panel.get<pn::Rag>();
            }
        const int at = r->embed.form.cursor;
        if (auto enter = form::keys::translate(r->embed.form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*enter}}, "enter on text row");
        r = m.ui.panel.get<pn::Rag>();
        std::fprintf(stderr, "editing=%d at=%d\n", r->embed.form.editing() ? 1 : 0, at);
        // 现在的截图键盘:在编辑时下,
        if (auto down = form::keys::translate(r->embed.form, maya::KeyEvent{K::Down}))
            m = step(std::move(m), Msg{RagEmbedKey{*down}}, "down while editing");
        else
            std::fprintf(stderr, "note: Down not translated while editing\n");
        r = m.ui.panel.get<pn::Rag>();
        std::fprintf(stderr, "cursor %d -> %d, editing now=%d\n",
                     at, r->embed.form.cursor, r->embed.form.editing() ? 1 : 0);
        if (r->embed.form.cursor == at) {
            std::fprintf(stderr, "FAIL: Down while editing did not move the cursor\n");
            return 1;
        }
        render_once(m, "after down-while-editing");
    }

    for (int i = 0; i < 16; ++i) {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) break;
        if (auto enter = form::keys::translate(r->embed.form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*enter}}, "rag enter");
        render_once(m, "rag after enter");
        r = m.ui.panel.get<pn::Rag>();
        if (!r) break;
        // Esc 任何一项 edit/dropdown Enter 开始,然后继续前进。
        if (auto esc = form::keys::translate(r->embed.form, maya::KeyEvent{K::Escape}))
            m = step(std::move(m), Msg{RagEmbedKey{*esc}}, "rag esc");
        r = m.ui.panel.get<pn::Rag>();
        if (!r) break;
        if (auto down = form::keys::translate(r->embed.form, maya::KeyEvent{K::Down}))
            m = step(std::move(m), Msg{RagEmbedKey{*down}}, "rag down");
        render_once(m, "rag after down");
    }

    // 过期快照批次：subscribe.cpp 会翻译整批输入，
    // 批(一面糊)对 一个 FormFocus 快照。
    // 模拟 末尾 备注 "x\nzz" : 数据 Enter 离开字段,
    // 但以下字符仍然作为“插入”的意向到达。它们不得 变异行。
    {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "no rag pane for race test\n"); return 1; }
        // 步行到光标在文本上 -like 排行(以前各段不管
        // 他们走完哪里都留下了) 切换 ,其名称为 Enter
        // 将 FLIP 而不是编辑).
        for (int i = 0; i < 20; ++i) {
            const auto* fr = r->embed.form.focused();
            if (fr && fr->is_text_like() && !fr->locked) break;
            if (auto down = form::keys::translate(r->embed.form, maya::KeyEvent{K::Down})) {
                m = step(std::move(m), Msg{RagEmbedKey{*down}}, "race: seek text row");
                r = m.ui.panel.get<pn::Rag>();
            }
        }
        // 开始编辑焦点化的文本行, 插入 'x' ,则 Enter (释).
        if (auto enter = form::keys::translate(r->embed.form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*enter}}, "race: start edit");
        r = m.ui.panel.get<pn::Rag>();
        // 快照 已经取用 此处 (编辑=真实)——像 subscribe.cpp 被俘虏了
        const bool snap_editing  = r->embed.form.editing();
        const bool snap_choosing = r->embed.form.choosing();
        auto tr = [&](maya::KeyEvent ev) {
            return form::keys::translate(snap_editing, snap_choosing, ev);
        };
        auto text_of = [](const agentty::form::Field* fld) -> std::string {
            if (!fld) return {};
            if (const auto* t = std::get_if<agentty::form::field::Text>(&fld->value))
                return t->value;
            if (const auto* p = std::get_if<agentty::form::field::Path>(&fld->value))
                return p->value;
            return {};
        };
        if (auto a = tr(maya::KeyEvent{maya::CharKey{U'x'}}))
            m = step(std::move(m), Msg{RagEmbedKey{*a}}, "race: insert x");
        if (auto a = tr(maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*a}}, "race: enter (leaves)");
        // 编辑结束值 之后:
        r = m.ui.panel.get<pn::Rag>();
        const auto val_after_leave = text_of(r->embed.form.focused());
        // 卫生: 内 -edit 插入必须 LANDED ,
        // 否则测试中检查的不是它。 (批判探测器读错了) 变体.)
        if (val_after_leave.find('x') == std::string::npos) {
            std::fprintf(stderr, "FAIL: setup insert never landed (val='%s')\n",
                         val_after_leave.c_str());
            return 1;
        }
        // 贴面 末尾 : 仍与 STALE 快照.
        if (auto a = tr(maya::KeyEvent{maya::CharKey{U'z'}}))
            m = step(std::move(m), Msg{RagEmbedKey{*a}}, "race: stale insert z");
        r = m.ui.panel.get<pn::Rag>();
        const auto val_now = text_of(r->embed.form.focused());
        std::fprintf(stderr, "race: '%s' -> '%s' (editing=%d)\n",
                     val_after_leave.c_str(), val_now.c_str(),
                     r->embed.form.editing() ? 1 : 0);
        if (val_now != val_after_leave) {
            std::fprintf(stderr, "FAIL: stale Insert mutated a left field\n");
            return 1;
        }
    }

    // ── Home/End/PgDn:在浏览时跳动键工作,并夹住. ────
    {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "no rag pane for jump test\n"); return 1; }
        auto key = [&](maya::SpecialKey k) {
            if (auto a = form::keys::translate(r->embed.form, maya::KeyEvent{k})) {
                m = step(std::move(m), Msg{RagEmbedKey{*a}}, "jump key");
                r = m.ui.panel.get<pn::Rag>();
            }
        };
        key(K::End);
        const int at_end = r->embed.form.cursor;
        key(K::Down);   // 关闭 -guard: End 然后将包装下至顶端 move();
        key(K::End);    // 回到结尾
        if (r->embed.form.cursor != at_end) {
            std::fprintf(stderr, "FAIL: End not idempotent (%d vs %d)\n",
                         r->embed.form.cursor, at_end);
            return 1;
        }
        key(K::PageDown);   // 结尾:必须CLAMP,不能包扎
        if (r->embed.form.cursor != at_end) {
            std::fprintf(stderr, "FAIL: PgDn at the end wrapped (%d vs %d)\n",
                         r->embed.form.cursor, at_end);
            return 1;
        }
        key(K::Home);
        const int at_home = r->embed.form.cursor;
        if (at_home >= at_end) {
            std::fprintf(stderr, "FAIL: Home did not go above End (%d vs %d)\n",
                         at_home, at_end);
            return 1;
        }
        std::fprintf(stderr, "jumps: home=%d end=%d ok\n", at_home, at_end);
    }

    // 返回链：命令面板 → 命令对应的面板 → Esc，必须恢复
    // 调色板及其 带类型的 查询完好无损。 ThreadList
    // : 打开时即使为零 线程 (渲染 —— —— 链条总是运行。
    {
        Model m2;
        m2 = step(std::move(m2), Msg{OpenPalette{}}, "chain: open palette");
        for (char c : std::string{"threads"})
            m2 = step(std::move(m2), Msg{PaletteInput{static_cast<char32_t>(c)}},
                      "chain: type");
        m2 = step(std::move(m2), Msg{PaletteSelect{}}, "chain: select");
        if (!m2.ui.panel.is<pn::ThreadList>()) {
            std::fprintf(stderr, "FAIL: 'threads' did not open the thread list\n");
            return 1;
        }
        m2 = step(std::move(m2), Msg{CloseThreadList{}}, "chain: esc");
        if (!m2.ui.panel.is<pn::Palette>()) {
            std::fprintf(stderr, "FAIL: Esc from thread list did not restore the palette\n");
            return 1;
        }
        const auto* p = m2.ui.panel.get<pn::Palette>();
        if (p->query != "threads") {
            std::fprintf(stderr, "FAIL: restored palette lost the query ('%s')\n",
                         p->query.c_str());
            return 1;
        }
        std::fprintf(stderr, "chain: palette restored with query='threads'\n");
        // 而EscXXAGAIN关闭于线程——链条完全具有
        // 用户所构建的深度,没有幽灵级.
        m2 = step(std::move(m2), Msg{ClosePalette{}}, "chain: esc 2");
        if (!m2.ui.panel.is<pn::None>()) {
            std::fprintf(stderr, "FAIL: second Esc did not reach the thread\n");
            return 1;
        }
        std::fprintf(stderr, "chain: second esc closed to thread\n");
    }

    // ── Async-probe race: Test launches a worker; the user EDITS the
    // 完成前的配置。 过期 TestDone 不得
    // 印花图案 Ok/dim 探测器从未见过的配置。
    {
        auto* r = m.ui.panel.get<pn::Rag>();
        if (!r) { std::fprintf(stderr, "no rag pane for probe race\n"); return 1; }
        // 步行到可编辑的文本行——上一节将光标留在 End (
        // 单位:千美元) Action/locked 插入无 -ops ,
        // 这会使这个测试站不住脚。
        for (int i = 0; i < 24; ++i) {
            const auto* fr = r->embed.form.focused();
            if (fr && fr->is_text_like() && !fr->locked) break;
            if (auto up = form::keys::translate(r->embed.form, maya::KeyEvent{K::Up})) {
                m = step(std::move(m), Msg{RagEmbedKey{*up}}, "probe race: seek");
                r = m.ui.panel.get<pn::Rag>();
            }
        }
        const auto gen_at_launch = ++r->embed.probe_gen;   // 模拟发射
        r->embed.probe = agentty::rag_settings::EmbedForm::Testing{};
        // 用户编辑一个字段 ^ invalidate_probe 碰碰生成。
        if (auto enter = form::keys::translate(r->embed.form, maya::KeyEvent{K::Enter}))
            m = step(std::move(m), Msg{RagEmbedKey{*enter}}, "probe race: edit");
        if (auto a = form::keys::translate(true, false, maya::KeyEvent{maya::CharKey{U'x'}}))
            m = step(std::move(m), Msg{RagEmbedKey{*a}}, "probe race: type");
        // 电话 工作线程 答案来了,印在 旧 生成。
        m = step(std::move(m),
                 Msg{RagEmbedTestDone{true, 768, 42, "", gen_at_launch}},
                 "probe race: stale done");
        r = m.ui.panel.get<pn::Rag>();
        if (std::holds_alternative<agentty::rag_settings::EmbedForm::Ok>(r->embed.probe)) {
            std::fprintf(stderr,
                         "FAIL: stale probe completion verified an edited config\n");
            return 1;
        }
        // 还有 当前-generation 完成工作仍然在进行。
        const auto gen_now = ++r->embed.probe_gen;
        r->embed.probe = agentty::rag_settings::EmbedForm::Testing{};
        m = step(std::move(m),
                 Msg{RagEmbedTestDone{true, 768, 42, "", gen_now}},
                 "probe race: fresh done");
        r = m.ui.panel.get<pn::Rag>();
        if (!std::holds_alternative<agentty::rag_settings::EmbedForm::Ok>(r->embed.probe)) {
            std::fprintf(stderr, "FAIL: fresh probe completion was dropped\n");
            return 1;
        }
        std::fprintf(stderr, "probe race: stale dropped, fresh landed\n");
    }

    // 过滤器粘贴：命令面板打开时，粘贴文本进入
    // QUERY (重播为 带类型的 输入——相同的过滤语义学,
    // 不在后面的作曲中. Control 字符被剥光。
    {
        Model m2;
        m2 = step(std::move(m2), Msg{OpenPalette{}}, "fpaste: open");
        m2 = step(std::move(m2), Msg{PanelFilterPaste{"set\ntings"}},
                  "fpaste: paste");
        const auto* p = m2.ui.panel.get<pn::Palette>();
        if (!p) { std::fprintf(stderr, "FAIL: palette gone after paste\n"); return 1; }
        if (p->query != "settings") {
            std::fprintf(stderr, "FAIL: paste query '%s' (want 'settings')\n",
                         p->query.c_str());
            return 1;
        }
        if (!m2.ui.composer.text.empty()) {
            std::fprintf(stderr, "FAIL: paste leaked into the composer\n");
            return 1;
        }
        std::fprintf(stderr, "fpaste: query='%s', composer clean\n",
                     p->query.c_str());
    }

    std::fprintf(stderr, "ALL OK\n");
    return 0;
}
