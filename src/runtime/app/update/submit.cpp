// submit.cpp ——作曲家. -submission 设置 -persistence
// 帮助 状态更新函数. submit_message 是 3⁄4 ̄ ̧
// 漯B ComposerEnter / ComposerSubmit 并且从
// finalize_turn 冲洗作曲者的队列时 -message 缓冲区
// 这就是为什么它生活在一个共同的内部 头文件 而不是匿名的地名空间。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/auth/accounts.hpp"   // 应享权利范围: 活动账户标签
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/io/clipboard.hpp"   // armored_partial
#include "agentty/util/image_dims.hpp"   // image_dimensions 升降机 -time 跟踪
#include "agentty/tool/commands.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/slash.hpp"
#include "agentty/runtime/app/update.hpp"   // -enter update() 用于/行动
#include "agentty/io/persistence.hpp"   // title_from_first_message(纯度)
#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/provider/copilot/provider.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/auth_state.hpp"   // auth_source，AuthSource
#include "agentty/store/store.hpp"
#include "agentty/tool/mcp_tools_backends.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/workspace/checkpoint.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace {

// 调 -enter 顶端 -level 状态更新函数 与 `msg` 调色板的风格
[[nodiscard]] Cmd emit_msg(Model& m, Msg msg) {
    auto [next, cmd] = agentty::app::update(std::move(m), std::move(msg));
    m = std::move(next);
    return std::move(cmd);
}

// 清除作曲家在一次斜线动作后跑掉,使其远离历史.
void clear_composer(Model& m) {
    m.ui.composer.text.clear();
    m.ui.composer.cursor = 0;
    m.ui.composer.attachments.clear();
    m.ui.composer.slash = {};
}

// 运行一个已建 -in `/action`。无选项:未处理(保留文本)。
[[nodiscard]] std::optional<Cmd> run_slash_action(Model& m, const slash::Entry& e,
                                                  const std::string& args) {
    using A = slash::Action;
    if (!e.takes_args && !args.empty())
        return set_status_toast(m, "/" + e.name + " takes no arguments");
    clear_composer(m);
    switch (e.action) {
        case A::New:      return emit_msg(m, Msg{NewThread{}});
        case A::Compact:  return emit_msg(m, Msg{CompactContext{}});
        case A::Model:    return emit_msg(m, Msg{OpenModels{}});
        case A::Fork:     return emit_msg(m, Msg{OpenFork{}});
        case A::Rewind:   return emit_msg(m, Msg{OpenCheckpoints{}});
        case A::Review:   return emit_msg(m, Msg{OpenDiffReview{}});
        case A::Threads:  return emit_msg(m, Msg{OpenThreadList{}});
        case A::Settings: return emit_msg(m, Msg{OpenSettingsList{settings::Category::General}});
        case A::Tools:    return emit_msg(m, Msg{OpenSettingsList{settings::Category::Plugins}});
        case A::Login:    return emit_msg(m, Msg{OpenLogin{}});
        case A::Quit:     return emit_msg(m, Msg{Quit{}});
        case A::Rename:
            if (args.empty()) {
                // 无标题:打开 选择面板 打开 线程 '重
                // 命名开始,所以您会编辑当前名称而不是重制版.
                Cmd open = emit_msg(m, Msg{OpenThreadList{}});
                return Cmd::batch(std::move(open), emit_msg(m, Msg{ThreadListRenameStart{}}));
            }
            return emit_msg(m, Msg{RenameCurrentThread{args}});
    }
    return std::nullopt;
}

}  // 命名空间结束。

Cmd submit_message(Model& m) {
    // 记录稿即将被替换(线程 加载)或切(检查
    // 点倒转),现在发送的转弯将到左转或倒转
    // 指数和切转之间的线程。不要改变草稿。
    if (m.s.thread_loading)
        return set_status_toast(m, "wait for the thread to finish loading");

    // 佩尔夫邮票 : `AGENTTY_LOG=perf=debug` 显示第一个
    // -turn 键盘中键 -to-stream 迟钝会消失(婴儿产卵,
    // slash/skill 磁盘扫描, 凭据 决心, 传输-tool 每个阶段都记录它的三角洲。
    // STOPWATCH,不是归并的时间,它测量这个状态更新函数
    // 本身运行了多久,所以它需要两个读数一个真实的时钟;
    // m.now是整个归并的一个瞬间,并且会使每个间隔为零,
    // 它只输入调试日志行,从不计算Model,所以不能影响重播.
    // 电话 elm_purity 允许清单 携带 submit.cpp's `clock` 授权 给这个
    const auto t_submit0 = std::chrono::steady_clock::now();
    auto perf_stamp = [&](const char* stage) {
        const auto dt = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_submit0).count();
        AGT_LOG(Perf, Debug, "submit.stage", "stage={} ms={:.2f}", stage, dt);
    };
    // 如果有带类型的文本OR作为附件标签,则作曲者为非-empty.
    // 甚至一个 "empty-looking" 缓冲区 与 标签 ——应
    // 提交 标签 ARE 信件( 单个已丢弃@ file 或 paste,
    // 但没有周围的旁白)。 下面的扩展传球将拉动每个 标签's 正文 输入
    // 传输 文本中选择一个
    if (m.ui.composer.text.empty() && m.ui.composer.attachments.empty())
        return Cmd::none();

    // ── 斜线名称 ────────────────────────────────────────────────
    // 解决者( 1) runtime/slash.hpp 决定什么是领先者
    // `/name` 同样的 ACP 财务报告和已审计财务报表 `agentty run`
    // 已建成的 -in 行动在这里,在下面的警卫之前: `/model`
    // 工作必须没有模型集, `/review` 不得清除打开的更改。
    // 用户命令在下面扩展; 技能或 MCP 提示词 处理进一步减少; 任何其他(/
    // etc/hosts 以 带类型的.
    {
        const slash::Catalog cat = slash::catalog_of(m);
        const slash::Resolved hit = slash::resolve(m.ui.composer.text, cat);
        if (hit.what == slash::Resolved::What::Action)
            if (auto ran = run_slash_action(m, *hit.entry, hit.args))
                return std::move(*ran);
        // 来,来,来 MCP 提示词 询问它的服务器,这不可能在 UI 线程.
        // 渲染 以任务完成; SlashPromptRendered
        // 提交结果。 带类型的 线条在此之前始终可见。
        if (hit.what == slash::Resolved::What::Prompt && !m.ui.composer.slash_pending) {
            m.ui.composer.slash_pending = true;
            return cmd::render_slash_prompt(m.ui.composer.text);
        }
    }

    // 还没有解决模型问题——不要开始转弯。发送 `"model": ""` 向当地
    // 人 OpenAI-compatible 服务器( E) llama.cpp
    // /vLLM)被拒绝,拒绝则会为转弯提供素材. 重试 机器,该机器重新
    // -fires 永远: "dead loop when prompted" 刚
    // 开始 -selected 本地预设显示在 / 模式获取已登陆和自动之前
    // -selected 拒绝并告诉用户,在作曲中保留文字,所以不会丢失任何东西。
    // (Hosted 提供者总是有模型的代号,所以这只会咬伤本地的
    // -preset-before-models-loaded 窗口,它应该覆盖。 )
    if (m.d.model_id.value.empty()) {
        // 当模型列表失败时,这种失败是没有模型的原因(一个被拒绝的密钥,
        // 最常出现);说它,不是"pick a model".
        if (!m.s.models_loading && !m.s.models_error.empty()) {
            m.s.status = provider::provider_display_name(m.d.selection) + ": "
                       + m.s.models_error + " \xc2\xb7 ^P to fix";
            m.s.status_until = m.now + std::chrono::seconds{8};
            return Cmd::none();
        }
        m.s.status = m.s.models_loading
            ? "loading models\xe2\x80\xa6 pick one (^/) before sending"
            : "no model selected \xe2\x80\x94 open the model picker (^/) first";
        m.s.status_until = m.now
                         + std::chrono::seconds{4};
        return Cmd::none();
    }
    // STALE-model 守护 LOCAL 供养者. settings.json
    // 单位 -provider 模式映射回溯到最后 -picked 编号 PER
    // SPEC STRING ——所以在同一服务器的拼写之间切换(localhost:
    // 8080 vs 127.0.0.1: 8080/v1 ) 可以恢复运行中的
    // 服务器不服务的 ID。 本地 目录 已经 AUTHORITATIVE (马拉
    // 马) -server 服务于 / 具体 v1/models 清单;
    // Ollama 404s 一个未被推动的标记,所以一个被召回的ID没有出现在一个非
    // -empty 已加载 列表将失败每个请求 – 拒绝干净的。 TLS 供应商享有
    // 豁免: 目录 咨询(别名,日期: 变体 并网关合法重写 数据流 细细未列出).
    {
        const auto& sel = m.d.selection;
        const bool local_openai = sel.kind == provider::Kind::OpenAI
                               && !sel.openai_endpoint.use_tls;
        if (local_openai && !m.d.available_models.empty()) {
            bool listed = false;
            for (const auto& mi : m.d.available_models)
                if (mi.id == m.d.model_id) { listed = true; break; }
            if (!listed) {
                m.s.status = "model '" + m.d.model_id.value
                           + "' isn't served by this host \xe2\x80\x94 pick one (^/)";
                m.s.status_until = m.now
                                 + std::chrono::seconds{5};
                return Cmd::none();
            }
        }
    }

    // 图像粘贴为文本, 尚未完全到达( 或永远不会) ,
    // 即为模型的 base64 兆字节。 请按住发送并说出要做什么。
    for (const auto& a : m.ui.composer.attachments) {
        if (a.kind != Attachment::Kind::Paste || !a.body.bytes().starts_with("richpaste:1:")) continue;
        return set_status_toast(m, armored_partial(a.body.bytes()) && !a.stalled
            ? "image still arriving \xe2\x80\x94 wait for it, or delete the chip to send without it"
            : "that image didn't arrive whole \xe2\x80\x94 delete the chip and copy it again",
            std::chrono::seconds{6});
    }

    // 电话 待处理-changes 复检覆盖 一个 窗口: 代理
    // 编辑完成后与用户的下一个消息之间。 发送一个新的转折结束窗口
    // — 编辑已经放在磁盘上, 所以提交方式
    // "I've seen them, carry on" (隐性
    // 接受) 清除队列, 以免条条横跨转弯。 明确审查( ) Ctrl
    // +R − 拒绝) 仍在运行 之前 你会发出新的信息;
    // 只有当你继续前进而不拒绝的时候, 才会发出新的信息。
    // ANNOUNCE ——沉默地默示接受N文件,恰恰是用户应该看到的
    // 决定(并学习审查的承受能力),即使他们从未就此采取行动。
    if (!m.d.pending_changes.empty()) {
        const int files = static_cast<int>(m.d.pending_changes.size());
        m.s.status = "kept " + std::to_string(files)
                   + (files == 1 ? " edited file" : " edited files")
                   + " from last turn";
        m.s.status_until = m.now
                         + std::chrono::seconds{3};
        m.d.pending_changes.clear();
        // 改变是不管怎样, -press 改变带
        // 的守卫不能在下一个转弯时存活 并且向
        // 一些无关的后按键发射
        m.d.reject_all_armed = false;
    }

    // ── 斜线-command扩展 ──────────────────────────────────
    // `/name args` ~ 命令模板 正文 与
    // $ARGUMENTS / 1美元9美元替换, 之前 任何
    // queue/checkpoint/wire 路径查看
    // 文本——每个 下游 消费者(排队重传、记录、提供者请求)
    // 统一获得扩展 提示词. A `/` 匹配未发现命令的
    // etc/hosts 以信息为主,必须保持工作状态。
    // 标签 幸存: 仅重写. text, 其内置占位符未更改(
    // 命令) 正文 不携带 标签 ,所以扩张不能孤儿之一。
    if (auto expanded = tools::commands::try_expand(m.ui.composer.text,
                                                    m.ui.library.commands)) {
        m.ui.composer.text   = std::move(*expanded);
        m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
    }

    // 排水沟 composer.text + composer.attachments
    // 将一个完全扩大的单一 载荷 字符串, 重置编曲字段。 用于队列
    // -on-busy 队列 -on-compact 路径和下面实际
    // 提交路径——所有三个都需要相同的路径
    // 此时将附件标签展开为文本，并清空附件向量。
    // 语义学所以队列项目中的重回( Up 箭头) 永远不会使
    // 指向已丢弃索引的占位符复活。
    // 排水沟 composer.text + composer.attachments
    // 改为 标签-form 载荷 占位符 —— 占位器 STAY
    // 文本和附件中 正文 由下列人员使用:
    //   • 排队 -on-busy 队列 -on-compact 路径( 排队项目)
    //     留着他们 标签 故调用 + 重播 渲染 作为
    //     标签 也不要线性地分;
    //   · 下面的实际提交路径(新信件获得
    //     相同 标签-form 文本和附件移动到
    //     `Message.attachments`).
    //
    // 电话 传输层 调用 `attachment::expand(...)`
    // 应请求 -build 时间来点燃 正文 回来的时候,
    // 模特儿还是看到贴纸 字节 /文件内容——
    // 唯一改变的是用户在提供的记录稿中看到的.
    auto drain_composer = [](Model& mm) {
        ComposerState::QueuedMessage out;
        out.text        = std::exchange(mm.ui.composer.text, {});
        out.attachments = std::exchange(mm.ui.composer.attachments, {});
        mm.ui.composer.text.clear();
        mm.ui.composer.attachments.clear();
        mm.ui.composer.cursor = 0;
        // 提交边界清除每 -draft 瞬态 撤消 / 重做和
        // 历史 -walk 索引属于用户刚发送的草稿;
        // 将索引带入下一个草稿将产生令人惊讶的效果。
        // Ctrl+Z 恢复了上一轮的一半内容。
        mm.ui.composer.undo_stack.clear();
        mm.ui.composer.redo_stack.clear();
        mm.ui.composer.browsing = ComposerState::Live{};
        mm.ui.composer.draft_save.reset();
        return out;
    };

    // 皮毛 皮毛 -item 提交: 用户按下 Alt 改
    // 为: 加载 列队的项目,可能编辑后提交。
    // 我们删除 ORIGINAL 队列中的槽;
    // 排出 -into-queued 下方路径( 当代理仍然忙碌
    // 时) 将按下编辑器 字节 返回到 末尾 如果代理闲置(
    // 很少—— 用户在代理忙碌时必须偷看, 然后在他们被击中之前
    // 完成) Enter ),编辑 字节 直接到那边去 传输 排队
    // 的排队速度会缩小一个
    if (const auto peek = m.ui.composer.queue_peek_index();
        peek && *peek < static_cast<int>(m.ui.composer.queued.size())) {
        m.ui.composer.queued.erase(
            m.ui.composer.queued.begin() + *peek);
        // draft_save (如果有的话)是用户按下前
        // 打过的活版草稿 Alt 他们通过提交证据 明确承诺
        // 了偷看物品 已保存 草案现在无家可归——放弃。
        // ( drain_composer 清理田地,
        // 但只有在我们决定排水之后, 这样做可以保住保释金
        // -out 路径高于整洁。 )
        m.ui.composer.draft_save.reset();
        m.ui.composer.draft_save_attachments.clear();
        m.ui.composer.browsing = ComposerState::Live{};
    }

    // 带子 -and-suspenders : 队列( 如果有) -Idle 阶段正在飞行中。
    // 赤色支票( 3) 流式传输 || ExecutingTool )在
    // 实践中是正确的——关键图路线. Esc/y/n/a 编号 权限 弹出面板
    // 何时 `pending_permission.has_value()` ,
    // 所以一个 AwaitingPermission 相位无法达到
    // ComposerEnter 分派 - 但是,我说, `active()` /
    // `!is_idle()` 使保证结构化,而不是依赖两个独立的吉卜赛层保持同步。
    // 未来添加新的阶段(或使作曲者在其中继续运行的重构) AwaitingPermission
    // )不会默默地退缩到
    // 提交覆盖了当前活动上下文。
    //
    // 也排队,而背景 OAuth 刷新正在飞行中。 Deps 仍然坚持着
    // 预告 -refresh (过期)认证 头文件 直到
    // TokenRefreshed 处理函数 换个位置; 发射一个 数据流 现在401号
    // 电话 处理函数 一旦新证书生效 排出队列
    if (m.s.active() || m.s.oauth_refresh_in_flight) {
        m.ui.composer.queued.push_back(drain_composer(m));
        return Cmd::none();
    }

    // 没有自动 -compaction。
    // 较早的版本排队了用户的信息,并发射同步压缩回合后再发布
    // 消息——用户点击 Enter ,30-60秒时
    // 看不到任何东西,然后他们的信息终于发出。这是无法接受的
    // 工作流程中断。
    //
    // 新的形状 : `launch_stream` 软 -trims
    // 编号 传输 载荷 装配到95% context_max
    // 每一正常转弯,你都要顺从, 绝不 需要等待压缩才能安全。
    // 用户的邮件会立即发出。背景自动 -compact 可能还在
    // 下一个哨站开火 -turn 闲置边界( 见
    // `maybe_autocompact_after_turn`
    // 输入 finalize_turn )——这是正确的时刻,
    // 因为用户正在读取模型的输出,而不是打字,而紧凑发生时并没有阻挡他们
    // 试图做的任何事情,/compact slash 命令也保留用于手动控制.

    Message user;
    user.role = Role::User;
    // 排水作曲家 – 标签-form 文本 + 附件。
    // 图像附件必须到达 传输 以 Anthropic 图像内容
    // 块( E) 不 作为 "[image: ...]" 传教标志;
    // 我们抬起他们的 字节 打开 user.images
    // 这儿 DROP 由附件产生,所以 -Message
    // 附件向量仅包含 传输 扩展器以文字处理( Paste
    // /) FileRef / 符号。。 内容。
    // 标签 图像的占位符保留在 user.text - 电话 渲染
    // 器 将指向过去附件的占位符作为图像 标签 咨询
    // user.images [ ] 标题。
    auto drained = drain_composer(m);
    // 图像附件:其 字节 被抬到 `user.images` 所以,
    // 我们 传输层 能够编码为 Anthropic 图像内容块。
    // We KEEP 附加条目在 `drained.attachments`
    // - 刚刚和 `body` 已搬出,
    // 所以占位指数 `user.text` 仍然有效(
    // 图像后加一个 @ 文件的粘贴会带有占位符 0, 1,
    // 2; 擦除后重新编号会使文本与向量脱同步)。
    // 传输 扩展器为类型 \\ Image 发出文字标记;
    // 渲染器 表面为相同的 标签 任何其它类型的标题
    for (auto& att : drained.attachments) {
        if (att.kind == Attachment::Kind::Image) {
            ImageContent img;
            img.media_type = att.media_type;     // 复制件: path/type 留在附件
            img.set_bytes(att.body.bytes());
            // 追踪手 -off。一个正确但从未到达 传输
            // 看着 IDENTICAL 给一个从未被抓住的...
            // 传言标记会从哪条路出去,所以告诉模型
            // 此处存在一张图像。
            // 记录大小和原因 传输 门,
            // 使两者与日志相区别,而不是通过二节形码.
            const auto dims = ::agentty::util::image_dimensions(img.bytes());
            AGT_LOG(Ui, Info, "submit.image_lift",
                    "media_type={} bytes={} dims={}x{} within_limits={}",
                    img.media_type, img.bytes().size(), dims.w, dims.h,
                    ::agentty::util::image_within_wire_limits(img.bytes()) ? 1 : 0);
            user.images.push_back(std::move(img));
        }
    }
    user.text        = std::move(drained.text);
    user.attachments = std::move(drained.attachments);

    // ── 用户 -explicit 技能激活( 光谱: 斜线) -command 语法) ──
    // `/skill-name [rest of prompt]` ——绳子
    // 截住符牌,使技能的全能激活. 载荷 输入信件,并标记其活动(因此是
    // 后期的模型) -driven `skill` 调用 )),脱脱脱脱.
    // 模型接收指令时不需要自己采取激活行动。
    // 首次 只有在它与发现的技能完全吻合时,
    // 才考虑指示符。 `/compact` 朋友们会
    // 陷入他们现有的困境 处理函数 毫无动静。
    if (!user.text.empty() && user.text.front() == '/') {
        auto sp  = user.text.find_first_of(" \t\n");
        auto tok = user.text.substr(1, sp == std::string::npos
                                           ? std::string::npos : sp - 1);
        const auto* sk = m.ui.library.skill(tok);
        if (sk) {
            std::string rest = sp == std::string::npos
                ? std::string{}
                : std::string{user.text.substr(sp + 1)};
            std::string expanded;
            // "Already active" 是从模型所见而来,而不是从一个
            // 会话集中产生: 后/ 配合旧的 正文 被概括为不见得,技能必须是 已加载 ,
            // 存储的集出错( Q) tools/skills.hpp,
            // "derived from the transcript").
            if (!tools::skills::is_active_in(sk->name,
                                             ::agentty::visible_text(m.d.current))) {
                expanded  = tools::skills::activation_payload(*sk, m.ui.library.skill_approvals);
                expanded += "\n\nFollow the skill instructions above";
                expanded += rest.empty() ? "." : " for this task: " + rest;
            } else {
                // 已经活跃了这个会话——不要重新 -inject 编号 正文.
                expanded = "Apply the already-loaded '" + sk->name
                         + "' skill" + (rest.empty() ? "." : ": " + rest);
            }
            user.text = std::move(expanded);
        }
    }

    if (m.d.current.title.empty()) {
        // 标题生成应该看到人类 -readable 文本,
        // 不为原始 标签 占位符。构建平原 -text 查看用户消息:
        // 每一个 `\x01ATT:N\x01` 变成 `[<chip-label>]` ,
        // 匹配用户在所渲染的转弯中看到的。
        std::string title_src;
        title_src.reserve(user.text.size());
        std::size_t i = 0;
        while (i < user.text.size()) {
            if (static_cast<unsigned char>(user.text[i]) == attachment::kSentinel) {
                auto len = attachment::placeholder_len_at(user.text, i);
                if (len > 0) {
                    auto idx = attachment::placeholder_index(user.text, i);
                    if (idx < user.attachments.size()) {
                        title_src.push_back('[');
                        title_src.append(attachment::chip_label(user.attachments[idx]));
                        title_src.push_back(']');
                    }
                    i += len;
                    continue;
                }
            }
            title_src.push_back(user.text[i++]);
        }
        m.d.current.title = persistence::title_from_first_message(title_src);
    }

    // ── Git 检查站(Zed) -agent 行为) ─────────────────────────
    // 在用户信件上贴上检查点编号,并 快照 整个工作树(跟踪+未跟踪,
    // .gitignore 尊重) 被钉住的无父母行为 之前 代理
    // 服务器开始变换文件。 快照 本身运行在孤立的 工作线程 (
    // git 添加( 输入) -A 需要一点时间 仓库 ),
    // 只有廉价的 仓库-ness 探测器 + id 印记 发生在这里。
    // 视图 渲染 转弯上方的检查站分隔符( 0.
    // cfg.checkpoint_above ),
    // 以及 "Rewind to checkpoint" 在调色板中恢复文件 + 将记录稿切换到此点。
    // 外面一个基特 仓库 说不 -op - 没有身份证明,没有分裂者,没有 工作线程.
    //
    // 电话 仓库 答案在 Model 一次启动的探测器着陆(
    // RepoProbed。在那之前,跳过检查站,让这个转弯,
    // 而不是向检查组询问 状态更新函数.
    std::optional<std::string> checkpoint_to_create;
    if (m.ui.git_repo.value_or(false)) {
        user.checkpoint_id  = CheckpointId{user.id.value};
        checkpoint_to_create = user.id.value;
    }

    // 选择主动的立足点,这是 OFF 默认情况下,
    // 因为自动记录誊本注入会占用用户上下文。启用时,
    // 运行一个完全孤立的检索,并推迟启动,直到解密;
    // 不要 竞态 用于对重复的同步套期 回退.
    std::string proactive_probe;
    {
        // 有效的主动门 = 每门 -thread 覆盖( 由叉设置) 选择
        // 面板 )如果存在,否则全球范围 RAG 模式。 FirstTurnOnly
        // 仅在 线程 尚未有前置助手转动( 此用户消息是第一个)。 关闭从不注入;
        // 总是在。 a shell AGENTTY_RAG_PROACTIVE
        // 超载( R) Model::env ) 击败全局模式.
        bool proactive_on = false;
        {
            const bool first_turn = std::none_of(
                m.d.current.messages.begin(), m.d.current.messages.end(),
                [](const Message& mm) {
                    return mm.role == Role::Assistant && !mm.text.empty();
                });
            // 缺席 = 继承全局模式; 订婚 = 此 线程 的自有模式.
            // 整个转动 枚举 ,因此添加一个 RagMode
            // 是一个 编译 此处出错而不是静默的坠入
            // "inherit" 这就是旧的内链比较 用它所没有
            // 识别的价值做的。
            if (const auto ov = m.d.current.rag_mode_override) {
                switch (*ov) {
                    case store::RagMode::Off:           proactive_on = false;      break;
                    case store::RagMode::On:            proactive_on = true;       break;
                    case store::RagMode::FirstTurnOnly: proactive_on = first_turn; break;
                }
            } else {
                // 从 Model :发射舱覆盖,否则 已保存
                // RAG 设置( 默认值至 面板 )),并被配置.
                const auto& rag = m.d.persisted.rag;
                proactive_on = m.env.rag_proactive_override.value_or(
                    rag.configured && rag.proactive && rag.mode != store::RagMode::Off);
                if (proactive_on && rag.configured
                    && rag.mode == store::RagMode::FirstTurnOnly)
                    proactive_on = first_turn;
            }
        }
        // 人类-readable 查看查询: 条 标签 占位符,
        // 所以检索器探测到真实的字, 不是 sentinel 字节.
        // 当转弯为 slash-command / 技能激活( 已经处理) 或
        // 平坦的 @ file 滴入没有旁白。
        std::string probe;
        probe.reserve(user.text.size());
        for (std::size_t i = 0; i < user.text.size();) {
            if (static_cast<unsigned char>(user.text[i]) == attachment::kSentinel) {
                auto len = attachment::placeholder_len_at(user.text, i);
                if (len > 0) { i += len; continue; }
            }
            probe.push_back(user.text[i++]);
        }
        // 知识 -shaped 门:足够的话成为真正的问题,而不是必须的文件
        // -mutation 命令( 想要的) grep/edit ,而不是医生 RAG
        // 廉价的heuristics——内部的信心棒 proactive_retrieve
        // 是一个真正的过滤器; 这只避免浪费一个 BM25 传下去 "hi" /
        // 例如，编辑 foo.cpp 或运行测试。
        auto word_count = [](const std::string& s) {
            int n = 0; bool in = false;
            for (char c : s) {
                bool w = std::isalnum(static_cast<unsigned char>(c));
                if (w && !in) ++n;
                in = w;
            }
            return n;
        };
        auto looks_imperative = [](const std::string& s) {
            // 下打包的第一个单词匹配一个mutation/command动词.
            std::string w;
            for (char c : s) {
                if (std::isalpha(static_cast<unsigned char>(c)))
                    w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                else if (!w.empty()) break;
            }
            static const char* verbs[] = {
                "edit","write","fix","run","add","remove","delete","create",
                "make","build","commit","refactor","rename","move","install",
                "update","change","implement","test","format","rebase","merge"};
            for (const char* v : verbs) if (w == v) return true;
            return false;
        };
        const bool slash = !user.text.empty() && user.text.front() == '/';
        if (proactive_on && !slash && !probe.empty()
            && word_count(probe) >= 3 && !looks_imperative(probe)) {
            proactive_probe = std::move(probe);
        }
    }
    // 而不是重新来过 -built 每隔一段时间 帧 整个运行,
    // 和解决 -time 冻结少了一个 接口边界 手拿开。
    // CORRECTION SIGNAL (会话级):如果这个新用户转身
    // 看起来像前一个的更正( "no", "that's wrong",
    // "actually…", "undo", "revert" ——
    // 推动 SESSION 工作偏移, 因此会话会更难。
    // 分类器是一个纯度的单位 -tested 函数( 智能 :):),
    // 所以此热减少路径只是问它。
    //
    // 这还用来写一个 -workspace 以转弯签名键入遗憾
    // ( Q) RoutingMemory )该商店已经
    // 消失:同一信号在第二秒就持续的时间尺度从未按照固定政策来衡量,
    // 而且它静静地将成本在会话中倾斜。 -memory 级联
    // 保留有用的一半——它每转弯都会衰变,被夹住,
    // 随着过程而死.
    if (m.d.smart.orchestration() && smart::is_routing_correction(user.text)) {
        const int clamp = m.d.smart.bias_clamp;
        if (m.s.smart_effort_bias < clamp) ++m.s.smart_effort_bias;
    }

    m.d.current.messages.push_back(std::move(user));

    // 强迫前转弯 逐步显示 以和解 之前 冻结 快照.
    // 通常,推迟结算 -freeze (meta.cpp 等待着 逐步显示
    // 以自己排水,但用户可以在滑翔中滑翔时提交(
    // pending_settle_freeze 冻死一个...
    // `live_` 部件会 快照 a 源码树 他们的散石离天而散,
    // -screen 现场直播 帧 ——员额 -stream 重复,
    // 请重复。 settle_message_md 运行( 现在无害)
    // finish() 从而部件处于固定形状;下面的冻结会捕捉到下一个生命的
    // 准确信息 帧 等提交文件的时候 msg.text 最终和 streaming_text 空的,
    // 所以这是形状 -neutral 已经是了 -drained 转身,
    // 一阵清空的倒塌 -animating 一条(用户继续移动;
    // 剪切最后的~100毫秒打字机是右侧) 调用 当他们撞到时 Enter).
    for (std::size_t i = m.ui.frozen_through;
         i + 1 < m.d.current.messages.size(); ++i) {
        auto& mm = m.d.current.messages[i];
        if (mm.role != Role::Assistant || mm.text.empty()) continue;
        settle_message_md(m, mm);
    }
    freeze_through(m, m.d.current.messages.size());
    // 延期结算 -freeze 可能还 待处理 上一个回合(
    // 用户在下一个闲置的 Tick 启动前提交)。 上面的冻结
    // 刚刚覆盖了它, 所以放下 标志 以避免冗余的 否 -op 下
    // 一张滴答时冻结。
    m.ui.pending_settle_freeze = false;

    // 智能模式: 表面为每张 -turn 路径 DECISION 首次参加
    // -class 线程 牌 (QQ),在助手回复前的正前,所以用户详细看到管
    // 弦乐的动作. 传输-inert; std::nullopt 关机时
    if (auto card = cmd::build_smart_routing_card(m))
        m.d.current.messages.push_back(std::move(*card));

    Message placeholder;
    placeholder.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(placeholder));

    m.d.current.updated_at = m.wall_now();

    // 闲 —— 流式传输。新鲜的 phase::Active 替换上一个转角的
    // 上下文批发( IDle 没有任何): 0 重试 新的计数器
    // started/last_event_at 默认邮票 RetryState。
    // 镜像 StreamStarted 处理函数 重置后, 才会有这个帖子 -submit
    // 渲染 正在布局 -identical 调任 -StreamStarted
    // 渲染 数毫秒后着陆的——没有这个,前轮遗留下来的状态土司( 重试 倒计时
    // / "Stream complete" / 错误横幅) 将会更改
    // status_bar 高度为一行 StreamStarted 产生
    // 可见的火种 "new turn appears at viewport bottom and then realigns"
    // 双 -frame 闪烁。
    auto now = m.now;
    phase::Active ctx;
    ctx.started       = now;
    ctx.last_event_at = now;
    m.s.phase         = phase::Streaming{std::move(ctx)};
    m.s.status.clear();
    m.s.status_until  = {};

    auto trim = trim_frozen_if_oversized(m);

    // ── 一样 -turn 以现场反馈为底线 ──────────────────────────
    // 上方同步套期错失(a) large/slow 密探的体
    // -embed 圆形 -trip 而不是(a) 冻结提交文件
    // 线程 等待,或 (b) 注射 STALE 可能晚点再打
    // -unrelated 转身,我们 DEFER 转弯了 数据
    // 流 检索后发射: 运行未 -hedged 在一个孤立的
    // 漏斗上 工作线程 并坚持请求直到降落,然后注入块 相同
    // -TURN 阶段已经进入 流式传输 {ctx} (上方设置),
    // 所以状态 -bar 加载动画 + 活动指标全天候实时——
    // 用户看到 "retrieving context…"
    // 永远不要挂断 UI。一对几百毫秒(偶尔是一两秒)读作模型思维,
    // 用户告诉我们只要有反馈,这些想法就很好。
    //
    // 当没有探测器(快速套期撞击,或者非-knowledge查询)时,
    // 我们立即像以前一样发射——在共同路径上零加延时.
    const bool defer_for_retrieval = !proactive_probe.empty();
    Cmd launch;
    if (defer_for_retrieval) {
        m.s.status       = "retrieving context\xE2\x80\xA6";   // …
        m.s.status_until = {};   // 粘到街区落地
        const std::uint64_t rid = ++m.s.retrieval_seq;
        if (auto* c = active_ctx(m.s.phase)) c->awaiting_retrieval = rid;
        // 发射由英国海军陆战队发射 ProactiveContextReady
        // 处理函数 一旦降落在记录里 我们只开始检索
        launch = Cmd::task_isolated(
            [](maya::Sink<Msg> out, std::stop_token, std::string probe,
               std::uint64_t id) {
                auto hit = tools::proactive_retrieve_blocking(probe, /*k=*/3);
                out.send(Msg{ProactiveContextReady{
                    hit ? std::move(hit->block) : std::string{},
                    hit ? hit->confidence : -1.0, id}});
            }, std::move(proactive_probe), rid);
    } else {
        perf_stamp("pre_launch_stream");
        launch = cmd::launch_stream(m);
        perf_stamp("post_launch_stream");
    }
    // 没有 commit_scrollback_overflow 提交
    // 不是批发模型交换,而是附在现有的记录中,所以 maya 普通行
    // diff 处理作曲 -shrink + 新建 -turn-rows
    // 转换正确。 agent_session.cpp 火灾
    // commit_scrollback 只在时间 FROZEN_MAX 修剪;
    // 我们在这里反射。 以往版本已发射 commit_scrollback_overflow
    // 每次提交时冲刷疑作曲家 -shrink 接口边界 ,但 调用 预
    // 提 prev_cells 过什么 渲染器 认为已经溢出——当
    // 没有东西真正溢出时,簿记就变得有效了 回滚缓冲区 镜像行进
    // "forget these" 下一个 diff re -emits ,
    // 在其中以重复卡片的形式显示 回滚缓冲区.
    std::vector<Cmd> parts;
    if (!trim.is_none()) parts.push_back(std::move(trim));
    // Worktree 快照搭乘与数据流发射相同
    // 的批次:它与请求的TTFB窗口同时运行,
    // 因此当模型要求其第一个文件编辑时,检查点被固定.
    // 失败是设计上沉默的——反弹的倒车道是re-verifies,
    // 相反,它在那里会举杯。
    if (checkpoint_to_create) {
        parts.push_back(Cmd::task_isolated(
            [](maya::Sink<Msg>, std::stop_token, std::string id) {
                (void)workspace::create_checkpoint(id);
            }, std::move(*checkpoint_to_create)));
    }
    parts.push_back(std::move(launch));
    auto cmd = parts.size() == 1
        ? std::move(parts.front())
        : Cmd::batch(std::move(parts));
    return cmd;
}

std::string active_provider_id(const Model& m) {
    return m.d.selection.catalog_id();
}

// ── 应享权利获得者(见 internal.hpp / domain/entitlement.hpp) ────
//
// (提供者、帐户) 此处 也没有其他地方,所以...
// 调用 站点无法按键账户 -scoped 仅由提供者进行的事实——遗产
// 帐户的确切缺陷 -blind 擦擦这个层替换。
namespace {
std::pair<std::string, std::string> entitlement_scope(
    const auth::AuthView& accounts, std::string_view provider) {
    std::string pid{provider};
    // 电话 注册表 用于此提供者的当前标签。 空是合法和手段
    // "the only account" - 单人 -account 用户密钥在
    // "" 这也是为什么这种情况不需要移徙来审理普通案件的原因。
    std::string acct = accounts.active_account(pid);
    return {std::move(pid), std::move(acct)};
}
} // 命名空间结束。

bool entitlement_blocked(const store::Settings& s,
                         const auth::AuthView& accounts,
                         domain::entitlement::Fact f,
                         std::string_view provider,
                         std::string_view model_id) {
    const auto [pid, acct] = entitlement_scope(accounts, provider);
    return domain::entitlement::blocked(s.entitlements, f, pid, acct, model_id);
}

bool entitlement_record_blocked(store::Settings& s,
                                const auth::AuthView& accounts,
                                domain::entitlement::Fact f,
                                std::string_view provider,
                                std::string_view model_id) {
    const auto [pid, acct] = entitlement_scope(accounts, provider);
    const bool is_new = domain::entitlement::record_blocked(
        s.entitlements, f, pid, acct, model_id);
    if (is_new)
        AGT_LOG(Model, Info, "entitlement.blocked",
                "fact={} provider={} account={} model={}",
                domain::entitlement::tag(f), pid,
                acct.empty() ? std::string{"(only)"} : acct,
                model_id.empty() ? std::string_view{"(account-wide)"} : model_id);
    return is_new;
}

std::string model_for_provider(const store::Settings& s,
                               std::string_view raw_spec) {
    // 回顾 已保存 犬科的标识下( catalog_id ),所以按同样的方式检查他们:
    // "codex" 保存 以 "openai", "codex-cli" 以 "chatgpt".
    const std::string canon =
        raw_spec.empty() ? std::string{}
                         : provider::parse_selection(raw_spec).catalog_id();
    const std::string_view spec = canon.empty() ? raw_spec : std::string_view{canon};
    const bool is_chatgpt =
        spec == "codex" || spec == "chatgpt" || spec == "codex-cli";
    const bool is_copilot = spec == "copilot";

    // 1) 记得用户最后使用在这个提供者上的模型。 旧的构建
    //    以原始的光谱键入; 也试试。
    auto it = s.provider_models.find(std::string{spec});
    if (it == s.provider_models.end() || it->second.empty())
        it = s.provider_models.find(std::string{raw_spec});
    if (it != s.provider_models.end() && !it->second.empty()) {
        // ChatGPT '线条' -up 是服务器 -driven 和随时间
        // 而变化:账户不再提供的一个闪存( e.g . 退休人员
        // `gpt-5.1-codex` )在第一个回合被拒绝。 ChatGPT 弹片
        // if the LIVE catalog still lists it; otherwise fall through to the
        // 账户是当前默认的,所以我们永远不恢复一个死模型。
        if (!is_chatgpt && !is_copilot) return it->second;
        // 副驾驶:信任被召回的弹壳 无需 网络回合 -trip -
        // 这东西在车上运行 UI 线程 打开服务机,然后按下直播
        // 目录 这里就是选择副驾驶时差的原因。
        // 同步 fetch_models 反切校正 a 过期 一会儿再打
        if (is_copilot) return it->second;
        // ChatGPT : 验证召回日期与 CACHED 目录 仅此而已
        // ——相同 UI-thread 规则为副驾驶。
        // list_models () 将阻断 目录 HTTP 获取时 缓存
        // 冷( 离线开关 = 满) 超时 冷冻 缓存 相信召回; 同步反射修正
        // a 过期 立即击掌 ModelsLoaded 土地。
        {
            auto cached = provider::chatgpt::list_models_cached();
            if (cached.empty()) return it->second;
            for (const auto& mi : cached)
                if (mi.id.value == it->second) return it->second;
        }
        // 记得的弹壳是 过期 ——通过. default_model (a) 与《京都议定书》第三条第1款有关的其他事项。
    }

    // (2) 不再记账——回到每个供应商的正常状态-in默认值。
    //
    // DERIVED默认先:ChatGPT的-up是服务器-driven,
    // 并随时间变化(gpt-5.4,不是我们曾经硬编码过的过期 gpt-5.1
    // -codex),所以从CACHEDXX目录——从网络中解决了这个问题,
    // 这个问题在UIXX线程开关时运行. 冷缓存返回空,
    // ModelsLoaded refetch -selects帐户的实际默认.
    if (is_chatgpt) {
        auto cached = provider::chatgpt::list_models_cached();
        return cached.empty() ? std::string{} : cached.front().id.value;
    }
    // STATIC 默认值从 注册表 空( 本地后端、 聚合器) 表示
    // 让重新获取逻辑自动选中第一个可用项。
    if (const auto* row = provider::preset_for(spec.empty() ? "anthropic" : spec))
        return std::string{row->default_model};
    return {};
}

void reset_composer_draft(ComposerState& c) {
    c.text.clear();
    c.cursor = 0;
    c.attachments.clear();
    c.undo_stack.clear();
    c.redo_stack.clear();
    c.browsing = ComposerState::Live{};
    c.draft_save.reset();
    c.draft_save_attachments.clear();
    c.queued.clear();
}

// 电话 一个 设置到达商店。
// 在 update/internal.hpp.
[[nodiscard]] Cmd save_record(Model& m) {
    return Cmd(SaveSettings{m.d.persisted});
}

[[nodiscard]] Cmd save_current(Model& m) {
    // 总是描述它; 作者跳过空 线程 ,就像它做,
    // 当它携带整个 线程.
    auto d = persistence::describe_save(m.d.current, m.d.saved_fps);
    m.d.saved_fps = d.fps;
    return Cmd(SaveThread{std::move(d)});
}

[[nodiscard]] Cmd save_current_whole(Model& m) {
    m.d.saved_fps.clear();
    return save_current(m);
}

[[nodiscard]] Cmd persist_settings(Model& m) {
    // 同步仍然作为单独域成员存在的设置 INTO 记录,
    // 然后 保存 分散的成员是那些没有迁移到
    // `m.d.persisted` 然而,在他们这样做之前,
    // 这是协调它们的职能。
    auto& s = m.d.persisted;
    s.model_id = m.d.model_id;
    s.profile  = m.d.profile;
    // 电话 活动 供应商,所以一个重启 回来的用户离开。
    // 曾经是写 仅 由 `--provider` CLI 标志 ,
    // 这意味着在 选择面板 持续数 -provider 模型,
    // 但不是提供者本身——每次重启都回到默认状态.
    if (const std::string pid = active_provider_id(m); !pid.empty())
        s.provider = pid;
    // favorite_models 真相之源是: ^F 直接编辑,
    // 每列恒星都从它读取。 这里没有任何内容从行中重写它。
    // 标志 ,这去 过期 并抹去下一个新爱 保存.
    // 将此模型记录为活动提供者的最后-used选择,
    // 因此稍后切换回它,就完全恢复了这个模型.
    if (!m.d.model_id.empty())
        s.provider_models[active_provider_id(m)] = m.d.model_id.value;
    s.effort = std::string{effort_to_wire_setting(m.d.effort)};
    // 智能模式: 整个配置, 一个任务。 While the second
    // 模式使用完整配置和一份说明。
    // AGENTTY_SMART_MODE 会话标点活动于 -memory `enabled` 标志 是那个 ENV '值,
    // 而不是用户的选择——所以坚持的偏好保持不变,而针头从不泄露到配置中.
    const bool keep_enabled = m.env.settings.smart_enabled.has_value();
    const bool was_enabled  = s.smart.enabled;
    s.smart = m.d.smart;
    if (keep_enabled) s.smart.enabled = was_enabled;
    auto save = save_record(m);
    // 副代理角色 -router (Layer 3b) 和提供者的针被标定范围从 Model ;
    // 缩写 分派 接口边界 之后再出版这些作品 归并 (update.cpp,
    // publish_derived).
    return save;
}

Cmd
commit_provider_switch(Model& m, std::string_view spec,
                       std::string_view label,
                       std::string_view desired_model,
                       bool open_panel) {
    const std::string spec_s{spec};

    // (1) 存档 OUTGOING 以其犬科提供商 id 的模型
    //     之前 下方的选取更改,所以后切换回的返回可以完全恢复
    //     这个模型.
    const std::string outgoing_id = active_provider_id(m);

    // (2)改变活跃的提供者——在拥有它的MODEL中.
    //
    //     以前是 provider::select (),
    //     一个过程 -global 从内部更新写入。全局性数据依然存在,
    //     用于解码循环(即: 数据流 工作线程, ACP ,但现在已出版:
    //     分派 接口边界 写在这之后 归并 (update.cpp,
    //     publish_derived。下面所有东西都放在 此 归并
    //     ——预热目标,模型采集, 凭据 敬酒仪式——读到
    //     m.d.selection ,所以它看到新的提供者,而不是被留下的。
    m.d.selection = provider::parse_selection(spec_s);

    // (2b) 预温 TLS/DNS 编号 NEWLY-active 供应商的宿主,
    //      所以在直播开关之后的第一个转弯速度和冷开关一样快。 副作用 (下
    //      文辩论);全文如下: m.d.selection ,就在上面设置。
    Cmd prewarm = cmd::prewarm_provider(m);

    Cmd recall_save = Cmd::none();
    {
        if (!m.d.model_id.empty())
            m.d.persisted.provider_models[outgoing_id] = m.d.model_id.value;
        m.d.persisted.provider = spec_s;
        recall_save = save_record(m);
    }

    // (3)为NEW后端制作有效有效模式。
    //     a 数据为: EXPLICIT desired_model (保险丝十字架)
    //     -provider 选择面板 预览 -chose 目标) − -provider —— 已建成
    //     -in 默认 ~ 空( Q) ModelsLoaded 自动 -selects ) ,第一部可选.
    //     明确路径是使一个已安装了`Enter`的保险箱成为ATOMIC供应商+模型开关
    //     而不是
    //     切换提供方，然后选择记忆值或默认值。
    std::string next{desired_model};
    if (next.empty()) next = model_for_provider(m.d.persisted, spec_s);
    if (!next.empty()) {
        m.d.model_id    = ModelId{next};
        m.s.context_max = resolved_context_max(m, detail::active_provider_id(m));
    } else {
        // 尚未能为新的后端解决模型(ChatGPT 目录尚未到达,Ollama,...).
        // CLEAR 型号id ——在此携带OUTGOING 供应商的模型将使得
        // persist_settings (step 5) 在新提供者的id(写
        // `codex → claude-opus-*`的bug)下归档一个横跨-provider
        // 过期模型. 一个空的id 被persist_settings 和
        // ModelsLoaded autXX -selects 跳过,成为第一个可用的.
        m.d.model_id = ModelId{""};
    }

    // LIVE路由章属于刚刚运行在OUTGOING
    // 供应商上的转弯. 状态徽章更喜欢它而不是选择,
    // 所以把它放在一个开关上,在新供应商名称旁边画上旧的
    // 提供者的模型——"Ollama / Opus 4.5",
    // 这种配对是不存在的,通常当转弯平息时会清除它,
    // 但开关可以在之后的任何时候发生(而结算路径保留它
    // 而工具仍然是待处理),所以开关必须自己掉掉.
    m.s.smart_turn_model = ModelId{};
    m.s.smart_turn_role.reset();

    // (4) 努力是: 不 重写在这里。 m.d.effort
    //     是用户选择的; 选择面板 徽章和图案 传输 每一个
    //     都把它夹在目前的模型上。在每个开关上重写它会永远
    //     失去选择(在总是思考的模型上,它变得很低,
    //     在切换后保持低水平)。
    // (5) 坚持 完整 设置形状( 提供者 + 每件)
    //     -provider 模型+努力+最爱)通过一个所有者,
    //     所以从不放弃努力跳,然后交换 Deps 验证和检测模型。
    auto settings_save = persist_settings(m);

    // 现场直播 头文件 用于新后端的, 由主机在此之后解决 归并 (
    // InstallAuth ),此处不读.
    Cmd install = Cmd(InstallAuth{.provider = spec_s});
    m.d.available_models.clear();
    m.s.models_loading = true;
    // 国家从左侧提供者那里学到的,它的获取错误(
    // "HTTP 401")否则会在新提供者的行和
    // 发送拒绝中显示,直到新列表登陆,它的符号校准
    // 描述的是另一个指示器.
    m.s.models_error.clear();
    m.s.est_calibration = 1.0;

    // 过期-token 换掉的守护 -TO 提供者。 账户 -picker
    // switch path (account_select) has always done this; the CROSS-provider
    // 路径没有,所以在人类标志失效后, 转换开口的人类
    // 第一次转弯时, 过期 无记名者(保证401 )
    // – 反应性刷新 → 重试, i.e。 初转明显缓慢,
    // 或者在更新路径有其他问题时出现硬错误。
    // 注册表-driven 闸门:只有选择主动刷新的行。
    Cmd refresh_cmd = Cmd::none();
    if (const auto* prow = provider::preset_for(spec_s);
        prow && prow->oauth_proactive_refresh && !m.s.oauth_refresh_in_flight) {
        m.s.oauth_refresh_in_flight = true;
        refresh_cmd = cmd::refresh_oauth_if_due();
    }

    // 打开模型 选择面板 立即让用户看到 "Loading models…"
    // 换成空的 线程 直到他们想打/模。 此处 ,在一个共享开关帮助器,
    // 所以 每个 入口( 提供者) 选择面板 ,自定义主机,
    // api-key/chatgpt/copilot 登录)
    // 统一获得它——不仅仅是两个登录 -modal 路径。
    // 外部 ACP 特工开自己的车 发现没有 目录 (
    // fetch_models 返回空的),所以打开 选择面板
    // 就会出现一个永久的 "no models" 框中——跳过。
    //
    // BUT 当呼叫者已经提前 -stashed a SPECIFIC
    // 模型(引信) 选择面板 :用户在一针中选择了提供者+模型,
    // 弹出经典 "Loading models…" 选择面板 上面是第二号罐头
    // 选择面板 已选择。跳过它。 需要的模型安装在 ModelsLoaded - 给
    // 一个 "Switching to X…" 来杯吐司
    const bool have_desired = !desired_model.empty();
    if (open_panel && m.d.selection.kind != provider::Kind::ExternalAcp
        && !have_desired)
        m.ui.panel.descend(pn::Models{{0, ""}});

    // 名称 派生 传输 结束点 OpenAI-dialect
    // 主机所以/v1默认值可见,而非魔法——自定义 -host
    // 已死亡 -loop 报告来自无法查看其光谱实际拨号的用户
    // ( "host:8080/" ^ 404 永远无线索),
    // 主机预设保留了短标签(其端点没有疑问).
    std::string toast_text = have_desired
        ? "switching to " + ui::pretty_model_label(std::string{desired_model})
              + "  \xc2\xb7  " + std::string{label} + "\xe2\x80\xa6"
        : "provider \xe2\x86\x92 " + std::string{label};
    {
        const auto& sel = m.d.selection;
        if (!have_desired && sel.kind == provider::Kind::OpenAI
            && !sel.openai_endpoint.use_tls)
            toast_text += "  (" + sel.openai_endpoint.host + ":"
                        + std::to_string(sel.openai_endpoint.port)
                        + sel.openai_endpoint.path + ")";
    }
    auto toast = set_status_toast(m, std::move(toast_text),
                                  std::chrono::seconds{4});
    return Cmd::batch(std::move(recall_save), std::move(settings_save),
                      std::move(prewarm), std::move(toast), cmd::fetch_models(m),
                      std::move(refresh_cmd), std::move(install));
}

bool no_provider_configured(const Model& m) {
    // 当前是否存在用户能够真正发送一轮请求的后端？
    //
    // 不 "is provider_keys empty"。一个没有密钥的自定义
    // 主机被存储为空 -string 价值( 5) login.cpp 写入
    // provider_keys [标 = "" ),所以地图 -emptiness
    // 测试报告 "configured" 对于一个无法使用地图的地图, 反向的大小写
    // 比它更难: 添加第二个本地主机看起来像一个 首次 RUN ,所以它收养了你 并
    // 移动了提供者 你的工作。
    //
    // 注册表 供应商: a 已保存 或信封 凭据 数。 Local/no-auth
    // 后端报告本地并完成 不 -- Ollama 正在安装在框中不是
    // 用户设置的证据 agentty 起来
    //
    // EXCEPT 是活动用户。 如果用户正在针对本地
    // 预设( Ollama) 工作, 则即使没有 凭据 ,
    // 也会保护会议。 跳过它使 Ollama 上
    // 的用户看起来没有密钥, 所以添加任何提供者。
    const std::string active = m.d.selection.catalog_id();
    for (const auto& p : provider::providers()) {
        const auto src = provider::auth_source(p, m.d.persisted, m.d.auth);
        if (src == provider::AuthSource::Saved
            || src == provider::AuthSource::Env)
            return false;
        if (src == provider::AuthSource::Local && !active.empty()
            && std::string{p.id} == active)
            return false;
    }
    // 已保存 自定义主机: 一个带有非 -empty 键是配置的后端。
    // 无密钥的只是配置的后端, 如果它是 活动
    // 选择 - i.e。 用户已经选择了与之相对应的工作,
    // 而这正是下一个 -in 的会话。
    for (const auto& [spec, key] : m.d.persisted.provider_keys) {
        if (!key.empty()) return false;
        if (!active.empty() && spec == active) return false;
    }
    return true;
}

Cmd provider_ready_without_switch(Model& m, std::string_view label,
                                  std::string_view detail) {
    // 电话 凭据 正在磁盘上, 认证视图会被调用者重新装入
    // 副作用 故意缺席:选择、模型、努力夹、 目录 擦擦 -
    // 一切 - 擦擦 commit_provider_switch
    // 做一个提供者 活动。用户要求设置一个。
    std::string text = std::string{label} + " is ready";
    if (!detail.empty()) text += "  \xc2\xb7  " + std::string{detail};
    // 说出切换方式,因为不切换是任何习惯于旧
    // 行为的人所惊讶的半个.
    text += "  \xc2\xb7  ^P to switch";
    return set_status_toast(m, std::move(text), std::chrono::seconds{6});
}

Cmd set_status_toast(Model& m, std::string text,
                                std::chrono::seconds ttl) {
    m.s.status = std::move(text);
    auto now = m.now;
    m.s.status_until = now + ttl;
    auto stamp = m.s.status_until;
    return Cmd::after(
        std::chrono::duration_cast<std::chrono::milliseconds>(ttl)
            + std::chrono::milliseconds{50},
        Msg{ClearStatus{stamp}});
}

} // namespace agentty::app::detail
