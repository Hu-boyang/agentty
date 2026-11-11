#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/env.hpp"
#include "agentty/util/startup_trace.hpp"
#include <maya/terminal/scrollback.hpp>
#include "agentty/mcp/client.hpp"   // mcp_config_present()
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/workspace/files.hpp"
#include "agentty/workspace/checkpoint.hpp"
#include "agentty/tool/registry.hpp"   // tools::registry (启动时热)
#include "agentty/workspace/symbols.hpp"
#include "agentty/io/blob_gc.hpp"
#include "agentty/util/modelsdev.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/tool/mcp_tools_backends.hpp"   // rag_embed_status:温养犬关闭-thread
#include "agentty/tool/subagent.hpp"   // set_smart:将引脚推入任务路由器

#include <cstdlib>
#include <vector>

namespace agentty::app {

namespace {
std::vector<ModelInfo> seed_models(const provider::Selection& sel) {
    // 发射层是ACTIVE提供商的捆绑目录，所以选择面板立即满了；
    // 实时获取会取代它。自定义主机没有捆绑列表，
    // 并且在应答之前开始为空。在这里播种 Anthropic 的列表，
    // 无论将 Claude 型号放在任何活跃的提供商下，
    // 然后选择一个将 Claude id 发送到该主机。
    if (sel.kind == provider::Kind::Anthropic) return catalog::bundled("anthropic");
    if (sel.kind == provider::Kind::OpenAI) return catalog::bundled(sel.openai_endpoint.label);
    return {};
}
} // 命名空间结束。

std::pair<Model, Cmd> init() {
    startup::mark("init");
    Model m;
    // 发射环境，捕获一次。每个状态更新函数读取m.env；
    // 无 调用 getenv（由 elm_purity lint 强制执行）。
    // init()是主机的一次启动读取：env、settings、凭据。
    const Io io = IoAccess::grant();
    m.env = read_launch_env(io);
    startup::mark("  launch env");
    // 将日历锚定到稳定时钟一次。每个 归并 都从这对中导出其壁时间 (
    // Model::wall_now)，并且步时间 jaal 会交给它，
    // 因此 状态更新函数 不会读取 system_clock。
    // init() 在第一个 归并 之前运行，并且本身就是允许读取
    // 时钟的地方：它是程序来自外部世界的输入，与读取 env 相同。
    m.steady_epoch = std::chrono::steady_clock::now();
    m.wall_epoch   = std::chrono::system_clock::now();
    m.now          = m.steady_epoch;
    // 为 persist-on-success 停放的定制 --provider 规格 main()。
    // 在这里读一次；从现在起，Model 拥有它。
    m.ui.unproven_spec = provider::take_unproven_spec_at_launch(io);
    // 主动提供者。 main()解析启动规范并将其安装在进程全局中，
    // 然后将程序交给jaal；这是 Model 对全局的一次读取。
    // 从这里开始，Model 拥有它，接口边界 将其发布回来（
    // update.cpp，publish_derived）。
    m.d.selection = provider::active();
    // 在启动时为作曲家idle-blink时钟设定种子，因此15秒的
    // blink-stop倒计时现在开始，而不是在第一次击键时开始。
    // 刚打开且尚未操作过的 agentty。
    // 正是idle-CPU 的情况（forever-blinking 绘制的光标保持
    // GPU 终端 合成），因此即使用户从不键入，它也必须超时。
    m.ui.composer.last_edit_ms = maya::anim::default_clock().now_ms();
    // 线程历史是单个最大的启动成本：数百个multi-MB 线程
    // JSON的真实-world历史在第一个帧可以渲染之前序列化成几秒的同步解析工作。
    // 遵循后台任务； ThreadsLoaded 登陆
    // 后就填满了列表。在此之前，线程 选择面板 会读取
    // `m.s.threads_loading` 以显示
    // "loading…" 提示。
    m.s.threads_loading  = true;
    m.d.available_models = seed_models(m.d.selection);

    auto settings = deps(io).load_settings();
    startup::mark("  settings loaded");
    // 凭据状态外记录（token文件、env、账户），这里像launch
    // env一样读取一次；后来的更改以 AuthViewLoaded
    // 的形式出现。
    m.d.auth = provider::load_auth_view(io, settings);
    startup::mark("  auth view");

    // 通过刷新路径使用的SAME梯子烘焙种子行。
    //
    // seed_models()返回捆绑的目录的原始数据，
    // 并且它在设置存在之前运行——因此为模型固定上下文
    // 窗口的用户在每一行上都被忽略，直到第一个提供者刷新。
    // 直到那时，选择面板的ctx栏和状态栏对于同一模型
    // 的看法不一致，这正是bake_context_window
    // ()被引入结束的矛盾(PR #39)。刷新路径烘焙；
    // 种子路径没有，所以 不变量 保存在除了你实际
    // 开始的状态之外的任何地方。
    //
    // 便宜：在启动时一次性捆绑少量行。
    for (auto& mi : m.d.available_models)
        ui::bake_context_window(mi, detail::active_provider_id(m), settings,
                                m.env.max_context_tokens);
    // 一个-TIME MIGRATION:旧账户-blind
    // `context_1m_blocked`布尔值成为CURRENTLY
    // ACTIVE账户的关键事实。这是最好的可用归因——布尔值
    // 从未记录过它是谁的块，而活动帐户是最后一次观察到的帐户。
    // 归属它（而不是丢弃它）可以在整个升级过程中保留
    // 用户的学习状态；下一个 保存 会永远丢弃布尔值。
    bool migrated_1m = false;
    if (settings.context_1m_blocked) {
        settings.context_1m_blocked = false;
        migrated_1m = detail::entitlement_record_blocked(
                settings, m.d.auth, domain::entitlement::Fact::Context1M, "anthropic",
                wire_model_id(settings.model_id.value));
    }
    // DISCOVERED权利：该账户在之前的会话中在context-1m
    // beta上被400。去掉前面的种子 `[1m]` 行，
    // 这样 选择面板 就不会在第一次实时 ModelsLoaded 获取（也
    // 过滤）之前短暂提供它们 - 选择一个只会重新 -trigger 和 回退。
    if (!m.d.available_models.empty()) {
        std::erase_if(m.d.available_models, [&](const ModelInfo& mi) {
            return mi.id.value.find("[1m]") != std::string::npos
                && detail::entitlement_blocked(settings, m.d.auth,
                                       domain::entitlement::Fact::Context1M,
                                       detail::active_provider_id(m),
                                       wire_model_id(mi.id.value));
        });
    }
    // DISCOVERED权利：该帐户在之前的会话中在context-1m
    // beta上被400。持久的 `[1m]` 模型 ID 会在第一回合
    // 重新 -send 测试版，然后再次死亡 -end - 预先剥离标记。
    if (settings.model_id.value.find("[1m]") != std::string::npos
        && detail::entitlement_blocked(settings, m.d.auth,
                               domain::entitlement::Fact::Context1M,
                               detail::active_provider_id(m),
                               wire_model_id(settings.model_id.value)))
        settings.model_id = ModelId{wire_model_id(settings.model_id.value)};

    // 在上面的两个修正已经纠正它（1M-entitlement透明和
    // [1m]标记条）之后并且在下面的代码从中导出
    // 分散成员之前，安置WHOLE记录。
    //
    // `m.d.persisted`是状态更新函数手中的保存到副作用
    // 的真相来源。 init() 从 `settings` 中读取但
    // 从未复制到 Model 中的任何内容都会被下一个 保存 默默丢弃，
    // 因此记录被分配为 一次，整个，而不是逐个字段：没有人迁移
    // 但仍然围绕 -trips 的首选项。
    m.d.persisted = settings;
    // 开始的模型。 per-provider 召回获胜：全局
    // model_id 是在 任意 提供程序上最后运行的任何内容，
    // 因此在启动时切换提供程序时，它会命名错误的后端模型。
    if (const auto recall = detail::model_for_provider(settings,
                                                       detail::active_provider_id(m));
        !recall.empty())
        settings.model_id = ModelId{recall};
    if (!settings.model_id.empty()) {
        // 防止跨-provider模型ID冲突。持久模型 ID 属于保存时
        // 处于活动状态的任何提供者；在 不同 提供程序上重新启动（或没有
        // --provider，回退到 Anthropic）不得携带
        // 该 ID — e.g。剩余的 "qwen2.5-coder:7b"
        // 在第一次提示时发送到 Anthropic 404。
        // 对于 Anthropic，仅当保存的 id 是已知的 Claude
        // id 时才采用；否则将其丢弃并让默认种子（model_id 的
        // built-in）保持不变。 OpenAI 端 self
        // -corrects 通过下面急切的 fetch_models() 往返，
        // 因此它保留了保存的 id 并让 ModelsLoaded 验证它。
        const bool anthropic_active =
            m.d.selection.kind == provider::Kind::Anthropic;
        bool honour = true;
        if (anthropic_active) {
            // 如果是已知的种子模型OR或任何`claude-*` ID，
            // 则尊重已保存 ID。种子列表是一个小的硬编码快照 (
            // opus/sonnet/haiku 4.5)；用户选择的 NEWER
            // Claude (e.g.claude-opus-4-8) 是一个完全
            // 有效的 Anthropic id，但没有播种，在这里拒绝它正是每次启动
            // 时最后选择的模型被忘记的原因 - 我们回退到内置的 -in 默认值。
            // 任何 `claude-` 前缀都是 Anthropic id，可以安全发送；
            // 只有一个外部 ID（e.g。剩余的 Ollama
            // "qwen2.5-coder:7b"）必须被删除，这样就不会出现 404 错误。
            honour = settings.model_id.value.starts_with("claude-");
            if (!honour) {
                for (const auto& mi : m.d.available_models)
                    if (mi.id == settings.model_id) { honour = true; break; }
            }
        } else if (m.d.selection.kind == provider::Kind::OpenAI
                   && provider::preset_for(m.d.selection.openai_endpoint.label) == nullptr
                   && settings.model_id.value.starts_with("claude-")) {
            // 自定义主机上的裸 Claude ID 是 Anthropic
            // 默认值的剩余部分，而不是主机列出的型号（服务 Claude
            // 的网关通过自己的 ID 对其进行命名）。放下它；
            // 实时抓取会选择一个真实的。
            honour = false;
        }
        if (honour) {
            m.d.model_id = settings.model_id;
            // 确保荣誉 ID 位于 选择面板 列表中（较新的 Claude
            // 不会出现在种子 快照 中），因此模型 选择面板 显示所
            // 选的活动模型，而不是不显示任何内容。
            bool present = false;
            for (const auto& mi : m.d.available_models)
                if (mi.id == m.d.model_id) { present = true; break; }
            if (!present) {
                // 与每隔一行相同的梯子。这里的固定 200000 为 已
                // 保存 `[1m]` 模型提供了 200k 窗口，
                // 其输出上限（窗口的四分之一）变为 50k。
                ModelInfo row{m.d.model_id, m.d.model_id.value,
                              detail::active_provider_id(m), 0, false};
                ui::bake_context_window(row, detail::active_provider_id(m),
                                        settings, m.env.max_context_tokens);
                m.d.available_models.insert(m.d.available_models.begin(),
                                            std::move(row));
            }
        } else if (!anthropic_active) {
            m.d.model_id = m.d.available_models.empty()
                ? ModelId{} : m.d.available_models.front().id;
        }
    } else if (m.d.selection.kind == provider::Kind::OpenAI
               && !m.d.available_models.empty()) {
        // 对于该提供商来说，没有 已保存：从其第一个捆绑模型开始，
        // 而不是该主机无法提供服务的 Claude 默认模型。
        m.d.model_id = m.d.available_models.front().id;
    } else if (m.d.selection.kind != provider::Kind::Anthropic) {
        m.d.model_id = ModelId{};
    }
    // 现在（在任何 数据流 运行之前）设置 per-model 上下文窗口，
    // 以便 ctx% bar 使用来自 帧 1 的正确分母，
    // 而不是在用户的第一条消息到达之后。
    m.s.context_max = ui::context_max_for_model(m.d.model_id.value);
    m.d.profile = settings.profile;
    // 外观这里无需排队：`m.d.ui()` IS `m.d.persisted.ui`，
    // 坐在上面的整个记录​​上。该视图每隔 帧 就根据
    // m.env.terminal 解析它。
    m.d.effort  = effort_from_wire(settings.effort);
    // 将用户的per-model推理-effort覆盖发布到目录 注册表中，
    // 以便resolved_caps()(并且因此supports_effort/选择面板/传输)
    // 从帧 1.镜像set_custom_auth_header尊重它们。
    set_reasoning_overrides(settings.reasoning_effort_overrides);
    // 水合LEARNED工作集（精确地从过去会话中从提供商拒绝
    // 中发现的每个-model reasoning_effort合同），
    // 因此梯子/夹具/传输从帧 1开始是正确的——拒绝→学习→
    // 重试循环对于每个模型仅支付一次其one-time成本。
    set_learned_effort_sets(settings.learned_effort_sets);
    // 对于可选的正文字段，主机400'd（#93)相同：严格的主机
    // 支付拒绝→学习→重试轮-trip一次，永远，而不是每次启动一次。
    set_rejected_fields(settings.rejected_fields);
    // 对于需要在较早回合发回推理的模型（#117）：
    // 重启后的第一回合已经携带它。
    set_reasoning_echoes(settings.reasoning_echo);

    // 智能模式：持久化配置IS、运行时配置——一种类型，无映射。
    // 旧的再水化手动完成的所有操作（三个插槽×三个字段，
    // 然后是数字策略，然后推送到子代理路由器）要么已经在结构中，
    // 要么由 apply_smart 的 fan-out 完成。
    m.d.smart = settings.smart;
    // 会话引脚：AGENTTY_SMART_MODE覆盖THIS
    // 进程的持久主开关（脚本化运行、基准测试、平分）。
    // persist_settings在固定时跳过该字段，
    // 因此用户的已保存偏好在会话中保持不变。
    if (m.env.settings.smart_enabled)
        m.d.smart.enabled = *m.env.settings.smart_enabled;
    // `agentty --model X` 也以交互方式绑定每个
    // 角色：没有这个，压缩就会远离用户命名的模型（#70）。
    m.d.smart.pinned_model = smart::tuning::model_pinned();
    // 数字策略的环境覆盖，在此处解析一次，因此分类器读
    // 取普通 int，而不是每轮调用 getenv()。
    settings::registry::apply_env(m.d.smart, m.env.settings);

    // 将再水化的配置下推到子代理路由器NOW。
    //
    // `task`在工作线程 线程上运行，无法访问该Model，因此路由器保留其
    // 自己的进程-global 快照 (tools::subagent::Config::
    // 聪明）。 main() 在读取设置之前安装该配置，
    // 并带有 DEFAULT-CONSTRUCTED RoleConfig
    // — 启用 = false，无插槽。在某些事情将真正的委托推倒之前，
    // 每个委托都会像智能模式关闭一样解析，默默地忽略用户的 pin。
    //
    // 这里是无条件的：用于执行此操作的路径都是有条件的，
    // 因此具有来自先前会话的 pin 的用户，其 目录 获取失败，
    // 而是委托给 auto-router 的选择。
    tools::subagent::set_smart(io, m.d.smart);
    // 这些引脚的范围为提供者。如果没有它，从设置中重新水化的
    // pin 将在启动时恰好处于活动状态的任何提供程序下受到尊重，
    // 这正是范围界定要防止的 cross-provider
    // 分派。
    tools::subagent::set_provider(io, detail::active_provider_id(m));
    // 对于候选池，auto-router排名高于的推理相同：否则它仅从
    // ModelsLoaded臂设置，因此失败的获取使得工作线程排名
    // 高于EMPTY列表。用我们现在拥有的种子（bundled/seeded
    // 目录）； ModelsLoaded 成功后会刷新它。
    tools::subagent::set_candidates(io, m.d.available_models);
    // ...和 ​​PARENT 型号，出于同样的原因。 main()
    // 安装 settings.json 所持有的任何内容，但上面的 cross
    // -provider 防护可以 REJECT 该 id（在 Anthropic
    // 上重新启动的剩余 "qwen2.5-coder:7b"）并回退到种子默认值。
    // 如果没有这个，快照 会保留被拒绝的 id，因此 write-role
    // 工作线程（继承父模型）会分派 UI 刚刚拒绝使用的确切 id。
    // init() 是已解析模型的单一事实来源；让路由器同意它。
    tools::subagent::set_model(io, m.d.model_id.value);
    // 审查UI：持久化在编辑后是否改变条带渲染。
    m.d.show_changes_strip = settings.show_changes_strip;
    m.d.show_reasoning     = settings.show_reasoning;
    // 补充"always allow"工具授权(Zed的always_allow规则)。
    // PermissionApproveAlways 附加到此列表中；
    // 在这里加载意味着上周给出的 授权 仍然会抑制今天的 提示词。
    //
    // 条目是"name@<hex>"——用户批准的定义，
    // 因此授权不会默默地转移到此后以相同名称重新定义的工具。
    // 裸露的 "name"（由旧版本编写，
    // 或手 -edited）加载 作为哈希 0，这意味着未经
    // 检查：它已经具有的行为。
    for (const auto& g : settings.always_allow_tools) {
        const auto at = g.rfind('@');
        std::string name = (at == std::string::npos) ? g : g.substr(0, at);
        if (name.empty()) continue;
        std::uint64_t hash = 0;
        if (at != std::string::npos) {
            const std::string hex = g.substr(at + 1);
            // 只有完整的16-hex-digit后缀才是散列。
            // 其他任何内容都是恰好包含 '@' 的工具名称的一部分。
            if (hex.size() == 16
                && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
                hash = std::stoull(hex, nullptr, 16);
            } else {
                name = g;   // 毕竟不是哈希后缀
            }
        }
        m.d.session_grants.insert(name);
        if (hash != 0) m.d.grant_def_hash[name] = hash;
    }
    for (auto& mi : m.d.available_models)
        mi.favorite = is_favorite(settings.favorite_models,
                                  detail::active_provider_id(m), mi.id.value);

    m.d.current.id  = deps(io).new_thread_id();
    m.d.current.auto_title_ok = true;
    m.d.current.workspace = m.env.cwd;
    m.s.status = "ready";
    startup::mark("  model filled");

    // 延迟启动命令（提前声明：下面的第一个-run分支可以对OpenProviders
    // 分派进行排队）。
    std::vector<Cmd> cmds;
    // 上述1M-entitlement迁移改变了记录； 保存 它就像
    // 副作用 一样，就像所有其他设置一样。
    if (migrated_1m) cmds.push_back(Cmd(SaveSettings{m.d.persisted}));

    // 尚未安装 凭据 → main() 使用空的 头文件 调用了
    // install()。打开登录弹出面板，以便用户无需离开TUI即可
    // 进行身份验证； subscribe.cpp 将所有输入路由到那里，
    // 直到完成（或 Esc 出去，留下 agentty 未经验证 -
    // 他们会在第一次发送时遇到 数据流 错误，并且可以从调色板 / 登录）。
    //
    // 主动提供者上的门是 Anthropic:弹出面板 是
    // Anthropic-specific (OAuth/sk-ant 密钥)。
    // OpenAI-family 后端通过启动时解析的环境变量进行身份验证，
    // 因此空的 头文件 意味着 "no key in env" — 弹出
    // Anthropic OAuth 弹出面板 是无意义的。这些用户在第一次
    // 发送时会收到 数据流 错误，指出缺少的密钥。
    //
    // 首次-RUN CREDENTIAL DETECTION：在默认为 Anthropic
    // 弹出面板 之前，扫描 注册表 来查找 ALREADY 具有 凭据 的提供者（来自
    // 先前运行的粘贴密钥，或者像 shell 中导出的 GROQ_API_KEY
    // 这样的环境变量）。如果存在，请打开 PROVIDER PICKER —
    // 它的行徽章正是每个 凭据 的来源 ("● key from GROQ_API_KEY")，
    // 因此用户的字面上的 -first 交互是
    // "oh, it found my key → Enter"，而不是他们从来
    // 不想要的 Anthropic 标志 -in。简单的first-runs（任何
    // 地方都没有信用）保留经典的Anthropic 弹出面板——大多数路径保持不变。
    if (auth::is_empty(live_auth())
        && m.d.selection.kind == provider::Kind::Anthropic) {
        bool cred_elsewhere = false;
        for (const auto& p : provider::providers()) {
            // 跳过我们已知的未经授权的提供者（活动的提供者）。
            if (p.id == provider::default_provider_id()) continue;
            const auto src = provider::auth_source(p, settings, m.d.auth);
            if (src == provider::AuthSource::Saved
                || src == provider::AuthSource::Env) {
                cred_elsewhere = true;
                break;
            }
        }
        if (cred_elsewhere)
            cmds.push_back(Cmd::after(
                std::chrono::milliseconds{0}, Msg{OpenProviders{}}));
        else
            m.ui.login = ui::login::Picking{};
    }

    // Codex (ChatGPT)也是第一个-class提供者：如果
    // 它是活动后端并且没有ChatGPT 凭据是已保存，则将用户直接放在
    // 标志-in 弹出面板上——与Anthropic完全相同——而不是在第一次
    // 发送时失败。 弹出面板 的选项 3 运行本机环回 OAuth。
    {
        const auto& sel = m.d.selection;
        const bool codex_active = sel.is_chatgpt();
        if (codex_active && !provider::chatgpt::responses_available())
            m.ui.login = ui::login::Picking{};
        // 对 GitHub 副驾驶的同样礼貌：与 --provider 副驾驶一起启动，
        // 但还没有 GitHub 凭据 → 打开标志 -in 弹出面板
        // 而不是在第一次发送时失败。
        if (sel.is_copilot() && !provider::copilot::signed_in())
            m.ui.login = ui::login::Picking{.provider = "copilot"};
        if (sel.is_kimi() && !provider::kimi::signed_in())
            m.ui.login = ui::login::Picking{.provider = "kimi"};
    }

    cmds.push_back(cmd::load_threads_async());
    // 磁盘上的技能、命令和挂钩，供查找它们的状态更新函数使用。
    cmds.push_back(cmd::load_library());
    // 在 工作线程 上启动时连接 MCP 服务器（插件），
    // 与 线程 完全相同。这会在第一轮 和 将 快照 降落在
    // m.ui.plugins 时加热工具表面，因此插件面板在打开时
    // 立即填充 - 连接是循环 -driven，而不是 惰性 副作用。
    if (mcp::mcp_config_present())
        cmds.push_back(cmd::load_plugins_async(/*重新连接=*/true));
    else
        // 没有MCP服务器要连接，但是FIRST对tools::registry()
        // 的访问仍然构建整个native-tool 传输 快照冷——并且在第一回合，
        // 该构建落在launch_stream的工作线程上，在模型回复数据流
        // 之前的TTFB窗口中。在这里加热，这样第一个回合就会发现 快照 已经构建好了。
        // （当 MCP 配置 IS 存在时，上面的 load_plugins_async
        // 已经接触 registry() 并对其进行加热。）
        cmds.push_back(Cmd::task_isolated(
            [](maya::Sink<Msg>, std::stop_token) { tools::warm_registry(); }));

    // OpenAI-family 后端（Ollama、llama.cpp、
    // groq 等）没有固定的内置 -in 型号列表 — seed_models
    // () 只知道 Claude id。主动端点可能无法提供 已保存 或
    // --provider-restored 型号 ID（e.g.
    // "qwen2.5-coder:7b" 持续存在，但守护进程从未拉取它，
    // 或者该 ID 属于不同的后端），这会在第一个 提示词 上出现 404 错误。
    // 在启动时急切地获取 /v1/models，
    // 以便 ModelsLoaded 状态更新函数 可以交换到用户发送任何
    // 内容的真实的服务模型 ID 之前 - 与模型 选择面板 所做的相同验证，
    // 只是不限制用户打开它。 Anthropic 也可以获取：它的实时列表包含每
    // 个模型的真实输出上限和窗口，对于比此版本更新的模型，捆绑种子无法知道这些。
    if (m.d.selection.kind == provider::Kind::OpenAI
        || m.d.selection.kind == provider::Kind::Anthropic) {
        m.s.models_loading = true;
        cmds.push_back(cmd::fetch_models(m));
    }

    // 后台OAuth刷新切换。当 `auth::resolve()` 在
    // 磁盘上发现过期的 -but-refreshable 信用时，
    // 将刷新令牌停放在此处；拿起它，分派 在 工作线程 上进行网络往返，
    // 因此 TUI 立即可以交互。粘性吐司（无有效期）- TokenRefreshed
    // 完成后将其清除。 oauth_refresh_in_flight
    // 标志 门控 submit_message，因此用户无法在 过期
    // auth_header 仍处于 Deps 的情况下发射 数据流。
    if (auto refresh = auth::take_pending_refresh()) {
        m.s.oauth_refresh_in_flight = true;
        m.s.status = "refreshing OAuth token…";
        m.s.status_until = {};
        cmds.push_back(cmd::refresh_oauth(std::move(*refresh)));
    }

    // 在较早的会话中主持证明了 self-hosted。
    // 在任何模型获取之前推送到 传输层，因此刷新可以探测它们，
    // 而无需用户 re-adding 主机。
    if (!settings.probe_hosts.empty())
        provider::openai::install_probe_hosts(settings.probe_hosts);

    // 后台版本检查 — 24h-cached（快速路径是在 工作线程
    // 上读取的一个小文件），因此这在大多数启动中实际上是免费的；
    // 当存在较新版本时，状态栏会增加不显眼的 "⬆ vX.Y.Z" 标签，
    // 并且调色板会增加 "Update agentty"。
    // AGENTTY_NO_UPDATE_CHECK=1(或气隙模式)完全禁用它。
    if (!m.env.no_update_check)
        cmds.push_back(cmd::check_for_update());

    // models.dev能力快照:解析是BACKGROUND 线程上的缓存副本，
    // 然后通过网络刷新(24h-cached获取https://
    // models.dev/api.json).。缓存是JSON的~
    // 4.4 MB(~7.5k型号)；解析在这里内联花费了第一帧
    // 几十毫秒的 main-thread 时间 — 作为单个最大的
    // pre-paint 块进行测量。功能注册表为 互斥量-guarded
    // 和 epoch-versioned (caps_epoch)，
    // 每个消费者（梯子、徽章、传输 夹具）每个 frame/request
    // 都 re-resolves，因此在第一次绘制后，
    // 事实会着陆一些 帧。自动 - 下面的后台刷新始终依赖相同的合同。
    // 排序：load_cached 首次，然后刷新——在
    // 一个任务内排序，以便网络副本永远不会输给磁盘副本。
    // Community-maintained元数据吸收了提供者在版本之间的偏差；
    // 所学的-from-rejection 注册表仍然超过它。
    // 与发布检查相同的 opt-outs (气隙安装不得
    // 拨出)。
    const bool no_net = m.env.no_update_check;
    startup::mark("  cmds queued");
    agentty::modelsdev::start_background_refresh(no_net);

    // 预热作曲家的 `@` (文件)和 `#` (符号)索引，
    // 以便当用户键入时触发 选择面板 立即打开，而不是在 multi
    // -thousand-path 步行/ multi-second 正则表达
    // 式扫描上阻止 UI 线程。
    //
    // 通过Cmd 接口边界，就像此功能中的所有其他后台作业一样。
    // 它们直接用于 调用 prewarm_workspace_*()，
    // 这将 -rolled 分配给每个私有的 std::thread——在已经
    // 经历 Cmd::task_isolated 的 modelsdev 刷新
    // 下面两行。一项工作的两种机制是 git-refresh 同级如何最终得到
    // BARE 分离的 线程 并且根本没有单个 -flight：新代码复制它首先
    // 看到的邻居。
    //
    // task_isolated（不是任务），因为文件系统遍历
    // 正是其文档描述的hang-prone工作——
    // 死的NFS/FUSE安装将永远楔入共享-pool插槽并使每个后来的工具调用挨饿。
    // 相反，楔形隔离任务会泄漏 线程。
    cmds.push_back(Cmd::task_isolated(
        [](maya::Sink<Msg>, std::stop_token st) { prewarm_workspace_files(st); }));

    // 加热 RAG 回收器 OFF 状态更新函数螺纹。
    // 对 shared_retriever() 的第一次调用运行其函数
    // -local 静态构造函数，其中 apply_config() 和 re
    // -probes 嵌入器 — 具有 multi-second 超时的网络拨号。
    // 否则，第一个调用会在第一回合发生 内部 submit_message()
    // （主动门用于每次发送时调用 shared_retriever()，
    // 甚至是 2-char "hi"；它现在读取 Model），因此 slow/unreachable
    // 嵌入端点在 Enter 和流启动之间使状态更新函数停滞约 1.2 秒 —
    // 真正的第一个 -Enter 挂起（测量：submit.stage
    // pre_launch_stream ms=1202 旁边的
    // rag.embed：在 相同 线程上不可用）。在这里强制静态 init
    // 将该探针移动到后台线程；在第一轮中，检索器是热的（或者已经缓存了其不可用
    // 的判决），并且门是廉价的快照读取。
    cmds.push_back(cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg>, std::stop_token) { (void)tools::rag_embed_status(io); }));
    cmds.push_back(Cmd::task_isolated(
        [](const maya::unlocked& cap, maya::Sink<Msg>, std::stop_token st) { prewarm_workspace_symbols(cap, st); }));

    // 工作区 是 git 仓库 吗？产生了两个 `git rev-parse`，
    // 因此远离 状态更新函数；答案返回为 RepoProbed，并存在于 Model 中。
    cmds.push_back(cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token st) {
            workspace::prewarm_repo_info(st);
            if (auto r = workspace::in_git_repo_if_ready(io))
                out.send(Msg{RepoProbed{*r}});
        }));

    // 回滚缓冲区=自动时的终端的回滚缓冲区长度。读取其配置文件（并可能询问
    // gsettings / 默认值），因此它在这里运行，而不是在 UI
    // 线程 上运行；在回答之前重播使用 env-only 猜测。
    cmds.push_back(Cmd::task_isolated(
        [](maya::Sink<Msg> out, std::stop_token st) {
            if (st.stop_requested()) return;
            const auto s = maya::terminal::scrollback_full();
            out.send(Msg{TerminalScrollbackProbed{s.rows, s.unlimited, s.exact, s.source}});
        }));

    // 打开到 api.anthropic.com 当前 的 TCP +
    // TLS + HTTP/2 连接，当用户仍在阅读屏幕/键入他们的第一条消息时，
    // 因此第一个真实请求重用热池连接，而不是在回合的关键路径上支付
    // DNS + TCP + TLS 握手 + h2 设置。
    // 在 Windows 上，冷握手约为 1 秒——一旦上面的
    // git/skill/tool 预热将其成本从 状态更新函数 线程 上移走，
    // 这是第一个 -Enter 延迟的最大部分。
    // prewarm_anthropic()内部有single-flights，
    // 表盘是跟踪背景线程（关机时join_prewarm），因此可以无条件安全地触发；
    // 将其设置在 Anthropic 作为活动提供者上，这样我们就不会拨
    // 打会话永远不会与之通信的主机。
    if (m.d.selection.kind == provider::Kind::Anthropic)
        cmds.push_back(Cmd::task_isolated(
            [](maya::Sink<Msg>, std::stop_token) { auth::prewarm_anthropic(); }));

    // 副驾驶的第一回合是 "weirdly hangs for a second"
    // 情况：该回合的 数据流 工作线程 在第一个令牌之前阻止 fresh_token
    // () (ghu_ -> proxy-token 交换，冷 TLS
    // 回合 -trip 到 api.github.com)，然后是 auto_session
    // () (另一个冷回合 -trip 到 /models/session) 数据流。
    // （绝不是 状态更新函数：每个调用者都是 工作线程，并且都是 锁 下的单个 -flight，
    // 其网络 调用 的上限为 30 秒。） 缓存 他们的结果，
    // 因此在这里启动它们 - 关闭 状态更新函数 线程，而用户仍在输入 -
    // 意味着第一个真正的提交找到一个温暖的代理令牌 + 自动会话，而不是支付两
    // 个序列回合关键路径上的握手。守护副驾驶提供者； fresh_token()
    // /auto_session() 内部为 single-flight。
    if (m.d.selection.is_copilot())
        cmds.push_back(Cmd::task_isolated(
            [](const maya::unlocked& cap, maya::Sink<Msg>, std::stop_token) {
                const auto t0 = std::chrono::steady_clock::now();
                auto ms = [&]{ return std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now() - t0).count(); };
                AGT_LOG(Perf, Debug, "prewarm.copilot", "fire=1");
                if (!provider::copilot::signed_in()) {
                    AGT_LOG(Perf, Debug, "prewarm.copilot", "skip=not_signed_in");
                    return;
                }
                (void)provider::copilot::fresh_token(cap);
                AGT_LOG(Perf, Debug, "prewarm.copilot", "stage=token_done ms={:.1f}", ms());
                (void)provider::copilot::auto_session(cap);
                AGT_LOG(Perf, Debug, "prewarm.copilot", "stage=session_done ms={:.1f}", ms());
            }));

    // 回收不再引用线程的斑点（删除线程，替换输出）。
    // 每天最多一次，24 小时宽限期，因此在这个或另一个
    // 实例中飞行的 保存 永远不会丢失 载荷。它自己的可连接
    // 线程（延迟后启动），不是一个独立的任务：在出口处仍在运行
    // 的步行触及释放的静力学并崩溃了 Windows CI。
    blobs::start_background_gc();

    startup::mark("init done");
    return {std::move(m), Cmd::batch(std::move(cmds))};
}

} // namespace agentty::app
