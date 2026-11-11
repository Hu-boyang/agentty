// web_search.cpp ——设置 ——网络搜索: web_search 工具政策。
//
// 打开设置列表的降序( E) Esc 返回, 键会通过共享窗体层,
// 并进行承诺的编辑 m.d.persisted.web_search 返回 保存 ,
// 并重建从 Model 所以,我们 面板 永远不能显示它没有的值 保存
// 告诉 TOOL LAYER 此文件的任务不是 : publish_derived
// 查看记录移动和返回 PublishWebSearchPolicy
// (update.cpp ),同样,Smart Mode的配置也
// 达到了子剂路由器.
//
// 编辑承诺的WHEN是表层的答案,而不是这个文件的:
//   * 切换 和 QQ/_____在数字上——按键键(按键) `changed` ,浏览)
//   * 带类型的 数字和文本—— 离开字段时( `left_field`)
// 每按键提交一个文本行将重建下方的窗体 光标 财务报告和已审计财务报表 保存 "p",
// "pi", "pin" ,改为 settings.json 前往 "pinterest.com".
//
// API 钥匙是这里唯一没有的东西 Model 状态。密钥存储是 IO (
// 密封文件,和 `secret-tool` / 键链 调用 当 OS 键盘打开了,
// 所以这个 状态更新函数 永远不要碰它:读取存在哪一个密钥,存储一个 带
// 类型的 键并删除每个键作为任务运行于 a 工作线程 (
// Cmd::task_isolated ),结果又回到
// WebSearchKeysRead / WebSearchKeySaved /
// WebSearchKeyRemoved 中键的嵌入形状 面板 没有留言,
// 祝酒或 Model 字段中始终带有键; 带类型的 文本只存在于密列中,直到字段被留下,
// 并且保存它的任务中。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <maya/core/overload.hpp>

#include <mcp/tools/web_search.hpp>

#include "agentty/runtime/panel/web_search_form.hpp"
#include "agentty/tool/web_search_secret.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

namespace {

// 电话 面板 每一个键的来历, 上次读取 UI 线程。
// 直到第一个读到答案,每个键行都这么说。
[[nodiscard]] web_search_form::KeyOrigin key_origin(const pn::WebSearch& o) {
    if (!o.keys_read)
        return [](std::string_view) { return std::string{"checking\xe2\x80\xa6"}; };
    return [&o](std::string_view service) {
        for (const auto& [id, origin] : o.key_origins)
            if (id == service) return origin;
        return std::string{};
    };
}

// 从“从”重建行 Model ,光标保留在同一行上(重建
// 可以添加和删除行:选择服务以替换其下行),
// 或打开 `prefer_id` 给与时。
void rebuild(const Model& m, pn::WebSearch& o, std::string_view prefer_id = {}) {
    o.rows_stale = false;
    form::rebuild(o.form,
                  web_search_form::build_form(m.d.persisted.web_search, m.env.settings,
                                              key_origin(o)),
                  prefer_id);
}

// Ask a 工作线程 哪些服务有键,从何处得到。
// 目录中每一个按键服务,不仅包括插槽中的服务:选择一个服务插槽,
// 一定不需要再读一次。
//
// 每个读数需要新一代,所以在飞行中两个读数中,
// 只有更新的答案被显示.
[[nodiscard]] Cmd read_key_origins(pn::WebSearch& o) {
    ++o.keys_gen;
    std::vector<std::string> services;
    for (const auto& svc : ::mcp::tools::web_search_services())
        if (svc.needs_key) services.push_back(svc.id);
    return Cmd::task_isolated(
        [](maya::Sink<Msg> out, std::stop_token, std::uint64_t gen,
           std::vector<std::string> ids) {
            WebSearchKeysRead r;
            r.gen = gen;
            for (auto& id : ids) {
                std::string origin = tools::web_search_secret::origin(id);
                r.origins.emplace_back(std::move(id), std::move(origin));
            }
            out.send(Msg{msg::WebSearchMsg{std::move(r)}});
        },
        o.keys_gen, std::move(services));
}

// 将粘贴拖动的粘贴物剪裁: 周围的白空间,以及一个 "Bearer "
// 卖家的证件在钥匙前打印
[[nodiscard]] std::string clean_typed_key(std::string key) {
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(0, 1);
    if (key.rfind("Bearer ", 0) == 0) key.erase(0, 7);
    return key;
}

[[nodiscard]] std::string label_of(std::string_view service) {
    const auto* svc = ::mcp::tools::find_web_search_service(service);
    return svc ? svc->label : std::string{service};
}

// 键 带类型的 将表格留在此处: 字行是空的( 文本先覆盖,
// 以免在自由中停留) 内存 ),键将转到一个 工作线程 永远
// 不要 Model 或 或 时 间 settings.json.
// 返回存储任务(或一个显然不是密钥的祝酒词),或者在没有密
// 钥为带类型的时没有.
[[nodiscard]] Cmd take_typed_keys(Model& m, pn::WebSearch& o) {
    std::vector<Cmd> out;
    for (auto& row : o.form.fields) {
        const std::string service{web_search_form::key_row_service(row.id)};
        if (service.empty() || row.locked) continue;
        auto* sec = std::get_if<form::field::Secret>(&row.value);
        if (!sec || sec->value.empty()) continue;
        std::string key = clean_typed_key(sec->value);
        std::fill(sec->value.begin(), sec->value.end(), '\0');
        sec->value.clear();
        sec->cursor = 0;
        if (!tools::web_search_secret::plausible(key)) {
            std::fill(key.begin(), key.end(), '\0');
            out.push_back(set_status_toast(m, label_of(service)
                + ": that does not look like an API key (8+ characters, no spaces)"));
            continue;
        }
        ++o.keys_gen;   // 已经读到的飞行中, 决不能取消它
        out.push_back(cmd::io_task_isolated(
            [](Io io, const maya::unlocked& cap, maya::Sink<Msg> sink, std::stop_token,
               std::string svc, std::string k) {
                const bool ok = tools::web_search_secret::store(io, cap, svc, k);
                std::fill(k.begin(), k.end(), '\0');
                sink.send(Msg{msg::WebSearchMsg{WebSearchKeySaved{std::move(svc), ok}}});
            },
            service, std::move(key)));
    }
    if (out.empty()) return Cmd::none();
    if (out.size() == 1) return std::move(out.front());
    return Cmd::batch(std::move(out));
}

// 坚持主义 `cfg` 重建联合国 面板 周围,光标保留在同一行上(
// 重建可以添加和删除行); form::rebuild 保持这个地方。
[[nodiscard]] Cmd commit(Model& m, pn::WebSearch& o, web_search_cfg::Config cfg) {
    const auto was = web_search_cfg::mode(m.d.persisted.web_search);
    m.d.persisted.web_search = cfg;
    Cmd out = save_record(m);

    rebuild(m, o);
    o.form.dirty = false;   // 只是已保存:不要说"unsaved"

    // 值得敬酒的更改:模式本身。 Auto 是静默的默认; 命名它保持
    // "why are the counts locked?" 的可答性。
    const auto now = web_search_cfg::mode(cfg);
    if (now != was) {
        const char* text = now == web_search_cfg::Mode::On   ? "Web search on"
                         : now == web_search_cfg::Mode::Off  ? "Web search off"
                                                         : "Web search auto";
        out = Cmd::batch(std::move(out), set_status_toast(m, text));
    }
    return out;
}

} // 命名空间结束。

form::Form web_search_form_for_search(const Model& m) {
    // 没有密钥来源: 构建它们读取密钥。 key lines still index.
    return web_search_form::build_form(m.d.persisted.web_search, m.env.settings);
}

Cmd web_search_update(Model& m, msg::WebSearchMsg wm) {
    return std::visit(overload{
        [&](OpenWebSearch& e) -> Cmd {
            pn::WebSearch o;
            // 同步建造(a) 面板 决不能拥有没有形式可操作的键盘;
            // 键行表示 "checking…" 直至
            // 下面的答案。
            o.form = web_search_form::build_form(m.d.persisted.web_search, m.env.settings, key_origin(o));
            // 打开第一真行(build()将其放在那里),
            // 除非搜索请求一行.
            (void)form::focus_row(o.form, e.focus);
            m.ui.panel.descend(std::move(o));
            return read_key_origins(*m.ui.panel.get<pn::WebSearch>());
        },
        [&](WebSearchKeysRead& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::WebSearch>();
            // 关闭以来,或因 store/removal :新读的书
            // 即将上路.
            if (!o || e.gen != o->keys_gen) return Cmd::none();
            o->key_origins = std::move(e.origins);
            o->keys_read   = true;
            // 中间 -edit 重建会把现在的 带类型的
            // : 标记行 过期 并重建编辑或下拉结束的瞬间。
            if (o->form.editing() || o->form.choosing()) o->rows_stale = true;
            else rebuild(m, *o);
            return Cmd::none();
        },
        [&](WebSearchKeySaved& e) -> Cmd {
            Cmd toast = set_status_toast(m, label_of(e.service) + (e.ok
                ? ": key saved"
                : ": key not saved \xe2\x80\x94 no secure store available"));
            auto* o = m.ui.panel.get<pn::WebSearch>();
            if (!o) return toast;
            return Cmd::batch(std::move(toast), read_key_origins(*o));
        },
        [&](WebSearchKeyRemoved& e) -> Cmd {
            Cmd toast = set_status_toast(m, label_of(e.service) + (e.ok
                ? ": key removed"
                : ": key NOT removed \xe2\x80\x94 the keystore refused; it is still stored"));
            auto* o = m.ui.panel.get<pn::WebSearch>();
            if (!o) return toast;
            return Cmd::batch(std::move(toast), read_key_origins(*o));
        },
        [&](CloseWebSearch) -> Cmd {
            ascend(m);
            return Cmd::none();
        },
        [&](WebSearchPaste& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::WebSearch>())
                (void)form::paste_into_focused(o->form, e.text);   // 守护 + 脏; 开始编辑如打字
            return Cmd::none();
        },
        [&](WebSearchKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::WebSearch>();
            if (!o) return Cmd::none();

            const auto applied = form::keys::apply(o->form, e.action);
            if (applied.close)
                return web_search_update(m, msg::WebSearchMsg{CloseWebSearch{}});

            // "Remove key" 是一个动作行; Enter 火烧它。
            if (applied.fired) {
                if (const auto* row = o->form.focused()) {
                    // A COPY: key_clear_service
                    // 返回行的 ID 视图,行随下面重建的窗体而死。
                    std::string service{web_search_form::key_clear_service(row->id)};
                    if (!service.empty()) {
                        // 立即显示结果( 行走, 键行说)
                        // "not set" 并降落在键行上;
                        // 清除本身,以及 -read 证明这一点,
                        // 运行在 工作线程.
                        ++o->keys_gen;
                        bool found = false;
                        for (auto& [id, origin] : o->key_origins)
                            if (id == service) { origin.clear(); found = true; }
                        if (!found) o->key_origins.emplace_back(service, std::string{});
                        rebuild(m, *o, std::string{web_search_form::kKeyPrefix} + service);
                        return cmd::io_task_isolated(
                            [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out,
                               std::stop_token, std::string svc) {
                                (void)tools::web_search_secret::erase(io, cap, svc);
                                // 由剩下的来判断,而不是由 erase() '返回 :
                                // "nothing was there" 是个成功。
                                const bool ok =
                                    tools::web_search_secret::origin(svc) != "stored";
                                out.send(Msg{msg::WebSearchMsg{
                                    WebSearchKeyRemoved{std::move(svc), ok}}});
                            },
                            std::move(service));
                    }
                }
                return Cmd::none();
            }

            const bool settled = applied.settled >= 0;
            // 中间 -edit 键盘键(插入 Backspace,
            // Delete, 光标 移动)报告 `changed`
            // 田间静静 EDITING。他们必须返回这里,
            // 留下表格:重建将重设行到 已保存 值和删除编辑焦点,
            // 它删除了第一个键盘之后的每一个键盘——一个数字行需要
            // 一位数字,以及 Backspace/Delete 文本
            // 行中似乎没有做任何事情,因为被删除的字符被立即恢复。
            // web_search_pane_test 针这个。
            if (!settled) {
                // 关键来源于中途 -edit : 让他们现在看到编辑
                // 或降级已经结束( ) Esc 在一个没有变化的字段里,
                // 没有解决任何问题,所以这是唯一会注意到的地方。
                if (o->rows_stale && !o->form.editing() && !o->form.choosing()) {
                    o->rows_stale = false;
                    rebuild(m, *o);
                }
                return Cmd::none();
            }

            // A 带类型的 API 键离开窗体(并转到 工作线程
            // )在读到之前。键盘商店不是其中的一部分。 Model ,
            // 因此这不是设置变化,也不在下面进行比较。
            Cmd keys = take_typed_keys(m, *o);

            web_search_cfg::Config cfg = m.d.persisted.web_search;
            web_search_form::apply_form(o->form, cfg);
            // 已记录的数值( 带类型的 并放回,
            // 一个 切换 翻转两次: 无 保存 重建,
            // 所以该行显示 注册表 已接受(已密闭、
            // 清理),其出处栏为时态。
            if (cfg == m.d.persisted.web_search) {
                rebuild(m, *o);
                return keys;
            }
            return Cmd::batch(commit(m, *o, std::move(cfg)), std::move(keys));
        },
    }, wm);
}

} // namespace agentty::app::detail
