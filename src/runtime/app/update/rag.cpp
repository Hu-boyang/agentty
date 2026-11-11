// rag_settings_update — 状态更新函数 财务报告和审定财务报表 RAG 模式 选择面板.
//
// 一项决定:如何积极主动(前3年) -turn ) 检索行为 – 在 /
// 仅首转 / 关闭。 光标行 IS 选择; 选择它设置 store::RagMode ,
// 从主动的大门,坚持 settings.json ,
// 并活下来 -applies 输入进程 -wide 高级把手按他们的默认值

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"

#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/rag/embed_secret.hpp"
#include "agentty/tool/mcp_tools_backends.hpp"
#include "agentty/tool/subagent.hpp"   // set_smart:任务路由器本身的复制件

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace rs = agentty::rag_settings;
namespace eb = agentty::rag::embed;
using maya::overload;

namespace {

// 持久性+活性 -apply 选择模式。 `proactive` 是从模式(Off QQ没有
// 预设)衍生出来的 -turn 注射; 首个 -turn 门已强制安装于 modal.cpp).
//
// 两者都比比分 EFFECTS ,两者均返回,但均未执行: 保存 正在写入
// -behind 现场 Deps 接口边界 ,而检索器则
// -sync 谓 cmd::apply_rag_settings ,内核运行在
// 工作线程。此函数只编辑 Model 并说接下来会发生什么。
//
// 坚持检索模式.
//
// 编辑块 `m.d.persisted` ,则 保存 结束 persist_settings
// 而不是把记录交给 接口边界 直接:仅 `ui` 财务报告和已审计
// 财务报表 `rag` 记录中已编辑,而 `effort`,
// `model_id`, `profile`, `smart` 而活动提供
// 者是 SEPARATE 域域。 一个原始 保存 将写那些他们 站立在
// init() 并返回所有自 —— 切换检索模式将取消模型切换。
// `configured = true` 第一次是 . . .. -run 锁定—
// —一旦用户选择了模式,即登机 提示词 别再问了
[[nodiscard]] Cmd commit_mode(Model& m, store::RagMode mode) {
    auto& rag = m.d.persisted.rag;
    rag.configured = true;
    rag.mode       = mode;
    rag.proactive  = (mode != store::RagMode::Off);
    const auto applied = rag;   // persist_settings 可能重写记录
    auto save = persist_settings(m);
    return Cmd::batch(std::move(save), cmd::apply_rag_settings(applied));
}

// 项目a EmbedConfig 中,该选项将自动打开。
// API 密钥是故意的 不 这部分——它转到键盘。
void write_embed_into(store::RagConfig& r, const eb::EmbedConfig& c) {
    r.embed_backend        = std::string{eb::id_of(c.backend)};
    r.embed_model          = c.model;
    r.embed_host           = c.host;
    r.embed_port           = c.port;
    r.embed_tls            = c.tls;
    r.embed_path           = c.path;
    r.embed_model_path     = c.model_path;
    r.embed_tokenizer_path = c.tokenizer_path;
    r.embed_dim            = c.dim;
}

// 嵌入的配置用于表单初始状态:发射env默认在持续记录下的默认状态.
// The. API 密钥不在此; 它由一个 副作用 之后 面板
// 打开( O) RagEmbedKeyLoaded).
[[nodiscard]] eb::EmbedConfig current_embed_config(const Model::Env& env,
                                                   const store::Settings& s) {
    eb::EmbedConfig c = env.embed_defaults;
    if (!s.rag.embed_backend.empty()) {
        c.backend = eb::backend_from_id(s.rag.embed_backend);
        if (!s.rag.embed_model.empty()) c.model = s.rag.embed_model;
        if (!s.rag.embed_host.empty())  c.host  = s.rag.embed_host;
        if (s.rag.embed_port != 0)      c.port  = s.rag.embed_port;
        c.tls            = s.rag.embed_tls;
        c.path           = s.rag.embed_path;
        c.model_path     = s.rag.embed_model_path;
        c.tokenizer_path = s.rag.embed_tokenizer_path;
        c.dim            = s.rag.embed_dim;
    }
    return c;
}

// 更改后重建行 对应的 存在行,
// 将光标保留在同一逻辑字段中。 `advanced`
// 放弃它会无声地瓦解 面板 返回到基本行,
// 下一次任何字段更改行集时。
void resync_rows(rs::EmbedForm& f, const store::Settings& settings,
                 const Model::Env& env, store::RagMode mode, bool advanced) {
    f.cfg = rs::config_from_form(f.cfg, f.form);
    form::rebuild(f.form, rs::build_form(f.cfg, mode, settings, env.settings, advanced));
}

// 将编辑从行中拉出,然后返回到cfg(不重建行)中.
void sync_cfg(rs::EmbedForm& f) {
    f.cfg = rs::config_from_form(f.cfg, f.form);
}

// 任何编辑都会使先前的探测结果无效:回答的终点
// 可能不是现在配置的终点.
void invalidate_probe(rs::EmbedForm& f) {
    // 替换任何探测器 IN FLIGHT : 撞倒一代人,
    // 就是一个 工作线程 完成该 竞态 此编辑区域为否 -op
    // 而不是在它从未测试过的配置上印上Ok
    ++f.probe_gen;
    if (!std::holds_alternative<rs::EmbedForm::Idle>(f.probe))
        f.probe = rs::EmbedForm::Idle{};
}

// 将探测器状态投射到动作行+页脚注释上,
// 结果是 渲染。 每次转换后都会呼叫一个地方。
// 面板-specific 状态说明。 `advanced` 是那个 面板 当前的 切换 状态 :
// ^E翻转,因此注必须注明NEXT报社的所作所为,
// 而不是总说"advanced"(一行出局,它就读作-op).
void refresh_status(rs::EmbedForm& f, bool advanced = false) {
    using A = form::field::Action;
    if (auto* row = f.form.find(rs::kFieldTest))
        if (auto* act = std::get_if<A>(&row->value)) {
            std::visit([&](const auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, rs::EmbedForm::Testing>) {
                    act->status = "testing";
                    act->tone   = A::Tone::Busy;
                } else if constexpr (std::is_same_v<T, rs::EmbedForm::Ok>) {
                    act->status = "ready · " + std::to_string(p.dim) + "d · "
                                + std::to_string(p.latency_ms) + "ms";
                    act->tone   = A::Tone::Good;
                } else if constexpr (std::is_same_v<T, rs::EmbedForm::Failed>) {
                    act->status = p.why;
                    act->tone   = A::Tone::Bad;
                } else {
                    act->status.clear();
                    act->tone = A::Tone::Neutral;
                }
            }, f.probe);
        }

    f.form.subtitle = eb::describe(f.cfg);

    // 验证是 值：将其内联显示出来，而不是在 保存 时间默默拒绝。
    // NOTE：共享密钥语法 + "unsaved" 标记由
    // form_config (SSOT) 呈现 - 此注释仅包含
    // 面板-specific 的内容：验证摘要、in-process 保证、
    // ^T（此 面板 的探测和弦）和高级 切换。
    if (auto v = eb::validate(f.cfg); const auto* bad = std::get_if<eb::Invalid>(&v))
        f.form.note = bad->why;
    else if (eb::is_in_process(f.cfg.backend))
        f.form.note = "no daemon required — the model runs in-process";
    else if (f.form.dirty)
        f.form.note = "^T test";
    else
        // RESTING音符宣传高级和弦。 ^E 是安全的：表
        // 单层仅声明它 (光标-end)，而 EDITING 和
        // 面板 处理函数 仅在浏览模式下触发它。
        // 仅当没有更紧急的事情要说时才显示。
        f.form.note = advanced ? "^E  hide advanced" : "^E  advanced";
}

// 为 `mode` 构建 面板 的表单，以活体检索器已经知道
// 的内容作为种子，因此打开它会立即显示嵌入是否有效，
// 而不是空的 "untested" 状态。
[[nodiscard]] rs::EmbedForm make_embed_form(const Model::Env& env,
                                            const store::Settings& settings,
                                            store::RagMode mode,
                                            bool advanced = false) {
    rs::EmbedForm f;
    f.cfg  = current_embed_config(env, settings);
    f.form = rs::build_form(f.cfg, mode, settings, env.settings, advanced);
    refresh_status(f, advanced);
    return f;
}

[[nodiscard]] rs::EmbedForm* form_of(Model& m) {
    auto* o = m.ui.panel.get<pn::Rag>();
    return o ? &o->embed : nullptr;
}

// 面板的实时高级-rows 标志，用于状态说明。
[[nodiscard]] bool adv_of(Model& m) {
    const auto* o = m.ui.panel.get<pn::Rag>();
    return o && o->advanced;
}

} // 命名空间结束。

form::Form rag_form_for_search(const Model& m) {
    const auto& st = m.d.persisted;
    const auto mode = st.rag.configured ? st.rag.mode : store::RagMode::On;
    return make_embed_form(m.env, st, mode, /*高级=*/true).form;
}

Cmd rag_settings_update(Model& m, msg::RagMsg rm) {
    return std::visit(overload{
        [&](OpenRag& e) -> Cmd {
            // 一个 面板。 覆盖面板 用于打开三个-row
            // 模式列表，嵌入器设置隐藏在第二个按键后面，
            // 没有任何广告。模式和嵌入器是一个问题的两半，
            // 因此它们是一个表格的两行。
            //
            // 该表格被构建为SYNCHRONOUSLY。它过去是通过
            // Zero-delay Cmd 推迟的，这意味着 覆盖面板
            // 存在于至少一个没有任何形式的 帧 中，而没有形式的 面板 形式
            // 声明了每个密钥，但没有回答任何一个，因此应用程序看起来冻结了。
            // 覆盖面板绝不能在其拥有键盘但不能对其
            // 进行操作的状态下被表示。
            const auto& st = m.d.persisted;
            const auto mode = st.rag.configured ? st.rag.mode : store::RagMode::On;
            // 对高级行的搜索命中打开并显示这些行。
            const auto* d = settings::registry::find(e.focus);
            const bool advanced = d && d->tier == settings::registry::Tier::Advanced;
            pn::Rag open{{mode, mode, make_embed_form(m.env, m.d.persisted, mode, advanced)}};
            open.advanced = advanced;
            (void)form::focus_row(open.embed.form, e.focus);
            m.ui.panel.descend(std::move(open));
            // 活体猎犬已经知道的，在 工作线程 上阅读（它与工具 线程
            // 共享）并由 RagEmbedStatusRead 折叠起来。
            Cmd status = cmd::read_rag_embed_status(m.ui.panel.get<pn::Rag>()->embed.probe_gen);
            // 密钥存在于安全存储中，这可能意味着产生秘密
            // -tool：在工作线程上读取它。
            const auto& cfg = m.ui.panel.get<pn::Rag>()->embed.cfg;
            if (!eb::needs_api_key(cfg.backend)) return status;
            return Cmd::batch(std::move(status), Cmd::task_isolated(
                [](maya::Sink<Msg> out, std::stop_token, std::string slot) {
                    std::string key = eb::load_key(slot);
                    out.send(Msg{RagEmbedKeyLoaded{std::move(slot), std::move(key)}});
                }, eb::endpoint_key(cfg)));
        },
        [&](CloseRag) -> Cmd {
            // Esc展开ONE级别：父快照 (调色板或设置列表，
            // 完整状态完整）或当没有时线程。
            // （选择下面的模式，提交并删除到 线程 — 即 "done"，
            // 而不是 "back"。）
            ascend(m);
            return Cmd::none();
        },
        [&](RagAdvanced) -> Cmd {
            // Reveal/hide Tier::Advanced行。
            // 由于SET行发生变化，表单被重建；光标保留在原来的位置，
            // 因此 切换 也不会将选择从用户下方移出。
            if (auto* o = m.ui.panel.get<pn::Rag>()) {
                o->advanced = !o->advanced;
                form::rebuild(o->embed.form,
                              rs::build_form(o->embed.cfg, o->cursor, m.d.persisted,
                                             m.env.settings, o->advanced));
                // 该注释命名了下一次按压，因此它必须跟随翻转。
                refresh_status(o->embed, o->advanced);
            }
            return Cmd::none();
        },
        // ── 嵌入行数 ─────────────────────────────────────────
        [&](RagEmbedClose) -> Cmd {
            // Esc一次展开一层（菜单→字段→面板）；
            // 表单层拥有该顺序，因此每个 面板 的行为都是相同的。
            //
            // 在OUTERMOST级别，这关闭了整个覆盖面板。
            // 它过去常常清除 `embed`，这使得 面板 处于开放状态，
            // 没有任何形式——这种状态会吞噬所有密钥，包括本来可以
            // 逃脱的 Esc。
            if (auto* o = m.ui.panel.get<pn::Rag>())
                if (!form::escape(o->embed.form))
                    return Cmd::none();   // 展开关卡
            return rag_settings_update(m, msg::RagMsg{CloseRag{}});
        },

        // 每个navigation/editing钥匙都到达这里。
        // 共享的 状态更新函数 完成这项工作；该手臂仅处理真正的 面板-specific：
        // 后端改变改变了存在的WHICH行，并且触发操作行意味着
        // "probe"。
        [&](RagEmbedKey& e) -> Cmd {
            auto* f = form_of(m);
            if (!f) return Cmd::none();

            const auto* before = f->form.focused();
            const std::string before_id = before ? before->id : std::string{};
            const bool on_backend = before_id == rs::kFieldBackend;
            const bool on_mode    = before_id == rs::kFieldMode;
            const auto* def       = settings::registry::find(before_id);
            const bool on_pipeline = def && def->owner() == settings::registry::Owner::Rag;

            const auto applied = form::keys::apply(f->form, e.action);

            Cmd mode_save = Cmd::none();
            if (applied.changed) {
                // 模式行立即提交——它是一个策略切换，
                // 没有任何要验证的内容，也没有探测要无效的内容，
                // 因此让用户按下 保存 键将是一种仪式。
                if (on_mode) {
                    if (auto* o = m.ui.panel.get<pn::Rag>()) {
                        o->cursor = rs::mode_from_form(f->form, o->cursor);
                        o->active = o->cursor;
                        mode_save = commit_mode(m, o->cursor);
                    }
                } else if (!on_pipeline) {
                    invalidate_probe(*f);
                    if (on_backend) {
                        auto* o = m.ui.panel.get<pn::Rag>();
                        const auto mode = o ? o->cursor : store::RagMode::On;
                        resync_rows(*f, m.d.persisted, m.env, mode, o && o->advanced);
                    } else {
                        sync_cfg(*f);
                    }
                }
            }
            refresh_status(*f, adv_of(m));

            if (applied.fired)
                return Cmd::batch(
                    std::move(mode_save),
                    Cmd::after(std::chrono::milliseconds{0}, Msg{RagEmbedTest{}}));
            // 当 in-place 变化登陆（切换、←/→、
            // 下拉菜单）或 带类型的 编辑结束时，值稳定；那是保存。
            const bool settled = applied.settled >= 0;
            if (settled && on_pipeline) {
                // 检索旋钮不依赖于嵌入器，
                // 因此它们自行保存并且从不等待嵌入器验证。
                rs::apply_form_to_settings(f->form, m.d.persisted);
                m.d.persisted.rag.configured = true;
                const auto rag = m.d.persisted.rag;   // persist_settings可以重写它
                auto save = persist_settings(m);
                return Cmd::batch(std::move(mode_save), std::move(save),
                                  cmd::apply_rag_settings(rag));
            }
            // 嵌入器行：RagEmbedSave首先验证，
            // 因此半-typed配置表面为探针的失败注释而不是写入。
            if (settled && !on_mode)
                return Cmd::batch(
                    std::move(mode_save),
                    Cmd::after(std::chrono::milliseconds{0}, Msg{RagEmbedSave{}}));
            if (applied.close)
                return Cmd::batch(
                    std::move(mode_save),
                    Cmd::after(std::chrono::milliseconds{0}, Msg{RagEmbedClose{}}));
            return mode_save;
        },

        [&](RagEmbedPaste& e) -> Cmd {
            auto* f = form_of(m);
            if (!f) return Cmd::none();
            // SSOT守卫+脏活在paste_into；
            // 只有 面板-specific 结果（更改的字段使探测无效）保留在此处。
            if (form::paste_into_focused(f->form, e.text)) {
                invalidate_probe(*f);
                refresh_status(*f, adv_of(m));
            }
            return Cmd::none();
        },

        // 在工作线程上运行探测：它拨打网络端点（或加载
        // 模型文件）并且绝不能阻止UI 线程。
        [&](RagEmbedTest) -> Cmd {
            auto* f = form_of(m);
            if (!f) return Cmd::none();
            sync_cfg(*f);
            if (auto v = eb::validate(f->cfg); auto* bad = std::get_if<eb::Invalid>(&v)) {
                f->probe = rs::EmbedForm::Failed{bad->why};
                return Cmd::none();
            }
            f->probe = rs::EmbedForm::Testing{};
            const std::uint64_t gen = ++f->probe_gen;

            store::RagConfig probe_cfg = m.d.persisted.rag;
            write_embed_into(probe_cfg, f->cfg);
            std::string key = f->cfg.api_key;
            // task_isolated:探针拨打网络端点或
            // 内存-maps模型文件，其中任何一个都可以阻塞数秒。
            // 使其远离共享 BG 池 意味着楔入端点不能
            // 阻碍其他后台工作。
            return Cmd::task_isolated(
                        [](maya::Sink<Msg> out, std::stop_token,
                           store::RagConfig probe_cfg, std::string key,
                           std::uint64_t gen) {
                            const auto r = tools::rag_probe_embedder(probe_cfg, key);
                            out.send(Msg{RagEmbedTestDone{r.ok, r.dim,
                                                          r.latency_ms, r.error,
                                                          gen}});
                        },
                        std::move(probe_cfg), std::move(key), gen);
        },
        [&](RagEmbedTestDone& e) -> Cmd {
            auto* f = form_of(m);
            if (!f) return Cmd::none();
            // STALENESS GATE(与登录的attempt_id
            // 相同形状):只有完成LATEST发射才能着陆.
            // 编辑或re-test撞出probe_gen,
            // 使这个答案对一个已经不存在的配置——
            // 采用它的暗/Ok将验证探测器从未见过的字节.
            if (e.gen != f->probe_gen)
                return Cmd::none();
            if (e.ok) {
                // 通过 MEASURED 维度。 这是唯一的地方 `dim`
                // 设置为: 用户 -supplied 值将默默地跳过
                // rag-cpp's HNSW 错配
                f->cfg.dim = e.dim;
                f->probe   = rs::EmbedForm::Ok{e.dim, e.latency_ms};
                f->form.dirty = false;
            } else {
                f->probe = rs::EmbedForm::Failed{e.error.empty() ? "probe failed" : e.error};
            }
            return Cmd::none();
        },
        [&](RagEmbedSave) -> Cmd {
            auto* f = form_of(m);
            if (!f) return Cmd::none();
            sync_cfg(*f);
            if (auto v = eb::validate(f->cfg); auto* bad = std::get_if<eb::Invalid>(&v)) {
                f->probe = rs::EmbedForm::Failed{bad->why};
                return Cmd::none();
            }

            // 电话 凭据 键盘(或密封文件),按端点键——从不输入
            // settings.json。写在 工作线程 ;
            // 失败会重新出现 RagEmbedKeySaved ⁇ (?
            Cmd store_key = Cmd::none();
            if (eb::needs_api_key(f->cfg.backend)) {
                store_key = cmd::io_task_isolated(
                    [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token,
                       std::string slot, std::string key) {
                        const bool ok = eb::store_key(io, cap, slot, key) || key.empty();
                        out.send(Msg{RagEmbedKeySaved{ok}});
                    }, eb::endpoint_key(f->cfg), f->cfg.api_key);
            }

            // 编辑记录 Model 等一会 保存 读本替换(
            // `auto s = deps().load_settings()`
            // ) 仅仅因为 Model 没有设置可 补丁.
            m.d.persisted.rag.configured = true;
            write_embed_into(m.d.persisted.rag, f->cfg);
            // 管道的把手,通过同样的表格读回,
            // 产生他们的行。
            rs::apply_form_to_settings(f->form, m.d.persisted);
            // 和我们的原因一样 commit_mode: 保存 结束
            // persist_settings 故散域先达于记录.
            auto save = persist_settings(m);

            const std::string label = eb::describe(f->cfg);
            return Cmd::batch(
                std::move(save),
                std::move(store_key),
                cmd::apply_rag_settings(m.d.persisted.rag),
                set_status_toast(m, "Embeddings: " + label,
                                 std::chrono::seconds{4}));
        },
        [&](RagEmbedKeyLoaded& e) -> Cmd {
            // 只有当面板 还在端点上时, 键才被读取,
            // 而用户没有同时编辑它。 存储的键会像它一直
            // 一样胜过 env 的键。
            auto* f = form_of(m);
            if (!f || f->form.dirty || e.key.empty() || eb::endpoint_key(f->cfg) != e.endpoint)
                return Cmd::none();
            f->cfg.api_key = std::move(e.key);
            auto* o = m.ui.panel.get<pn::Rag>();
            form::rebuild(f->form, rs::build_form(f->cfg, o ? o->cursor : store::RagMode::On,
                                                  m.d.persisted, m.env.settings,
                                                  o && o->advanced));
            refresh_status(*f, o && o->advanced);
            return Cmd::none();
        },
        [&](RagEmbedKeySaved& e) -> Cmd {
            if (e.ok) return Cmd::none();
            return set_status_toast(m, "Embeddings: key not saved (no secure store)",
                                    std::chrono::seconds{4});
        },
        [&](RagEmbedStatusRead& e) -> Cmd {
            // 只有输入它被读取的表格,并且只有从那时起没有
            // 更新的(编辑,测试)取代了未测试的状态.
            auto* f = form_of(m);
            if (!f || f->probe_gen != e.gen
                || !std::holds_alternative<rs::EmbedForm::Idle>(f->probe))
                return Cmd::none();
            if (e.ready)       f->probe = rs::EmbedForm::Ok{e.dim, e.latency_ms};
            else if (e.failed) f->probe = rs::EmbedForm::Failed{std::move(e.reason)};
            refresh_status(*f, adv_of(m));
            return Cmd::none();
        },
    }, rm);
}

} // namespace agentty::app::detail
