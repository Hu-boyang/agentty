<a id="agentty--architecture"></a>

# agentty 架构

本文面向准备阅读和修改代码的人，说明可执行程序由哪些部分组成。实现细节以代码为准，文档与代码不一致时应以代码为依据。

---

<a id="1-the-shape-of-the-program"></a>

## 1. 程序的整体结构

agentty 是 Elm 风格的应用。运行时反复应用以下纯函数。

```
(Model, Msg) -> (Model, Cmd<Msg>)
```

- **Model** 是汇集整个应用状态的聚合结构体。
- **Msg** 是包含所有可能事件的封闭和类型。
- **Cmd** 描述需要执行的网络、磁盘和定时器等副作用。
  运行时执行副作用，再将结果作为新的 `Msg` 送回应用。

渲染由纯函数 `view : Model -> Element` 表达，并委托给通过 Git 子模块引入的终端界面引擎 **maya**。宿主从 Model 构造组件的 Config，由 maya 负责界面符号、布局、边框和动画。

事件循环、副作用、任务、订阅，以及进程中的线程和锁均由 **jaal** 提供。agentty 严格通过 maya 使用这些能力，依赖方向为 agentty → maya → jaal。agentty 负责应用编排，其他子模块由宿主驱动。分层约束见 [LAYERING.md](LAYERING.md)，mcp-cpp、acp-cpp 和 rag-cpp 的集成约束见 [PROTOCOL_LIBRARIES.md](PROTOCOL_LIBRARIES.md)。

maya 的 Program 钩子在 `include/agentty/runtime/app/program.hpp` 中绑定。

| 钩子 | 职责 |
|----------------|-------------------------------------------------|
| `init` | 通过 Store 接口加载设置和最近会话。 |
| `update` | 通过领域重载转发到 `src/runtime/app/update/`。 |
| `view` | 将 Model 转换为 Element。 |
| `subscribe` | 声明定时器和实时数据流订阅。 |
| `visual_hash` | 判断是否需要重绘，哈希相同则跳过当前帧。 |
| `needs_warmup` | 恢复会话时一次性预热回滚缓冲区。 |

[`src/runtime/main.cpp`](../src/runtime/main.cpp) 解析参数，加载配置和凭据，构造提供方与 FsStore，通过 Deps 接口安装数据流路由和存储能力，再把 AgenttyApp 及其 Host 交给 maya::run。每次请求通过 [`provider/dispatch.cpp`](../src/provider/dispatch.cpp) 确定提供方路由。

---

<a id="2-directory-layout"></a>

## 2. 目录结构

include/agentty/ 与 src/ 按领域对应。头文件定义类型和内联逻辑，src/ 保存较重的实现。

- **`domain/`** 保存 session、conversation、catalog 等纯数据类型，不执行 I/O。
  还包括 todo、profile，以及 id.hpp 中的强类型 ID（ToolCallId、ThreadId、OAuthCode、PkceVerifier）。不同 ID 类型之间的误用会在编译时被发现。
- **`runtime/`** 是应用主体。
  - model.hpp 组合 Model，并保存输入框等仅用于界面的子状态。
    独占面板槽位和弹出面板也在其中，它们不属于单一领域。
  - panel/ 是面板子系统，包含独占槽位、优先级路由、
    各面板状态和表单，参见[面板头文件](../include/agentty/runtime/panel/)及[面板视图](../src/runtime/view/panels/)。
  - msg.hpp 定义按领域划分的 Msg 和类型，见第 4 节。
  - app/update/<domain>.cpp 保存各领域的状态更新函数。
  - view/ 负责 Model 到 Element 的转换，每类组件一个文件。
    各面板视图位于 view/panels/。
- **`provider/`** 定义 Provider 概念及其实现。
  anthropic/transport.cpp 使用 HTTP/2 和 SSE，是 OAuth / Pro / Max 的默认实现。openai/transport.cpp 支持 OpenAI 兼容接口，包括 openai、groq、openrouter、together、cerebras、ollama 和自定义主机。selection.cpp 根据 --provider 参数或持久化设置选择提供方。
- **`tool/`** 定义工具接口、注册表、分派边界、权限策略
  和宿主适配器。[mcp_tools_bridge.cpp](../src/tool/mcp_tools_bridge.cpp) 将 mcp-cpp 工具集包装为 ToolDef，[mcp_tools_backends.cpp](../src/tool/mcp_tools_backends.cpp) 提供依赖宿主的服务。通用工具位于 [third_party/mcp-cpp/src/tools/](../third_party/mcp-cpp/src/tools/)，memory_store.cpp 为 remember / forget 提供存储。
- **`scope/`** 提供配置解析代数，包括 Locus × Dialect 优先级、
  携带来源的 Source、resolve_first / resolve_union 归并，以及绑定内容的 Trust。记忆、技能、Agent 和斜杠命令发现共同使用这一纯函数基础能力，参见[头文件](../include/agentty/scope/)和[实现](../src/scope/)。
- **`io/`** 包含 http，以及无 zlib 依赖的 gzip / deflate 解压实现 inflate，
  还包括 tls 证书固定、auth 的 OAuth / PKCE、persistence 原子写入和 clipboard 剪贴板能力。
- **`airgap/`** 通过 SSH 提供 SOCKS5 隧道，使 Agent 可以运行在无法直接访问互联网的主机上，
  由笔记本转发网络数据。

---

<a id="3-seams-how-concrete-types-stay-hidden"></a>

## 3. 依赖接口如何隐藏具体类型

AgenttyApp 不应以 Provider 和 Store 的具体类型作为模板参数，否则每个编译单元都必须了解这些类型，并在它们变化后重新编译。include/agentty/runtime/app/deps.hpp 定义了由 std::function 组成的小型 Deps 结构体。

- **提供方接口**为 stream(Request, EventSink)。
- **存储接口**包括 `save_thread`、`load_threads`、`load_thread`、
  `load_settings`、`save_settings`、`new_thread_id` 和 `title_from`。
- **认证上下文**保存当前会话带类型的 `AuthHeader`。

src/runtime/main.cpp 在启动时组装数据流路由函数和存储对象。状态更新函数通过 app::deps() 访问这些接口。应用内登录后，update_auth(...) 可以实时替换凭据，无需重启。执行中的数据流已在构造请求时缓存认证头，因此不受影响。

Provider 概念刻意保持简小。

```cpp
template <class P>
concept Provider = requires(P& p, Request req, EventSink sink) {
    { p.stream(std::move(req), std::move(sink)) } -> std::same_as<void>;
};
```

任何能够流式生成聊天响应的类型都满足此概念，生产中可以使用 Anthropic 或 OpenAI 兼容传输实现，测试中可以替换为行为确定的内存脚本。

多个具体传输实现通过共享设计保持一致。路由作为注册表条目的字段，并由 static_assert 验证。模型特性保存为目录数据，共用的传输逻辑集中维护。参见[提供方注册表](../include/agentty/provider/registry.hpp)、[分派实现](../src/provider/dispatch.cpp)和[共享传输辅助函数](../include/agentty/provider/msg_shared.hpp)。

模型以 (provider, model) 为键，订阅权益以 (provider, account, model) 为键。新增属性应放在哪一层，可以参考[模型目录](../include/agentty/domain/catalog.hpp)与[权益类型](../include/agentty/domain/entitlement.hpp)的划分。

Anthropic、OpenAI 兼容接口、Ollama 原生接口和 ChatGPT / Codex Responses 各自维护特有的流式解析与恢复逻辑。共用的输入处理集中在唯一实现中，包括 provider::finish_stream 与 wire::SseFramer / LineFramer 的字节分帧和结束事件处理，provider::parse_retry_after 与 parse_retry_after_body 的服务器退避时间解析（优先 Retry-After，缺失时读取错误正文，Gemini 使用后者，见 StreamScaffold::retry_after()），wire::scrub_utf8 的严格 UTF-8 校验，wire::could_be_tool_json 对弱本地模型泄漏工具调用前缀的探测，provider::usage::{from_openai,from_responses,from_ollama} 对三种用量数据的转换，以及 auth::bearer_token 对 OpenAI 系列认证头的构造。Anthropic 保留
自身按凭据分支类型访问的逻辑，确保 API 密钥进入 x-api-key 头。

---

<a id="4-msg-a-closed-sum-split-for-compile-speed"></a>

## 4. Msg 按领域拆分的封闭和类型

将所有叶事件直接放进一个巨大变体，会使 sizeof(Msg) 由最大的叶事件决定，实例化宽度为 N 的 std::visit 分派表，并在任意叶事件变化后重新编译整个状态更新编译单元。

agentty 在 [msg.hpp](../include/agentty/runtime/msg.hpp) 中按领域拆分子变体，包括 ComposerMsg、StreamMsg、ToolMsg、ModelsMsg、ProvidersMsg、ThreadListMsg、PaletteMsg 和 MetaMsg。运行时遍历消息树，并调用 [program.hpp](../include/agentty/runtime/app/program.hpp) 中 AgenttyApp 对应领域的重载。

```cpp
static Cmd update(Model& m, msg::ComposerMsg d, Clock::time_point now) {
    m.now = now;
    auto cmd = app::update(m, std::move(d));
    return app::publish_derived(m, std::move(cmd));
}
```

示例省略了计时统计。[update.hpp](../include/agentty/runtime/app/update.hpp) 声明、[update.cpp](../src/runtime/app/update.cpp) 定义的自由函数重载转发到 update/<domain>.cpp。update.cpp 中返回二元组的 update(Model, Msg) 是完整消息测试入口，访问相同的领域状态更新函数并执行同样的 publish_derived 步骤。测试可显式传入归并时间，否则使用固定的测试时间起点。

每个领域实现位于独立编译单元。调用方仍通过 std::variant 的转换构造函数直接创建 Msg。每个叶事件只属于一个领域，因此包装关系没有歧义。

---

<a id="5-tools-typed-bundles-behind-a-json-edge"></a>

## 5. JSON 分派边界内的带类型工具

Tool 概念（include/agentty/tool/tool.hpp）要求以静态接口提供身份、模式定义、副作用和执行行为。

```cpp
template <class T>
concept Tool = requires {
    typename T::Args;
    typename T::Result;
    { T::name() }         -> std::convertible_to<std::string_view>;
    { T::description() }  -> std::convertible_to<std::string_view>;
    { T::input_schema() } -> std::convertible_to<nlohmann::json>;
    { T::effects() }      -> std::convertible_to<EffectSet>;
} && requires(const nlohmann::json& args) {
    { T::execute(args) }  -> std::convertible_to<ExecResult>;
};
```

工具内部使用完整类型，只有分派边界接收 JSON。DynamicDispatch 从注册表查找工具，在 try/catch 内执行，将异常转换成 ToolError，并限制单个工具的输出预算，防止 read / bash / grep 产生过多内容而耗尽上下文窗口。

**工具只能通过分派器执行，此约束由编译器保证。** ToolDef 的主体是 tools::ToolBody，可以赋值和测试，但调用必须提供只有 DynamicDispatch 能构造的 DispatchKey。工作线程的 unlocked 能力、异常隔离、输出预算和写入通道（tool/write_lanes.hpp）都集中在 DynamicDispatch::execute_with，所有调用必须经过这里。该机制覆盖主 Agent 的批次、task 子 Agent、ACP、mcp-serve 和已放弃的任务。带 WriteFs 的调用在执行期间为每个目标文件占用写入通道，没有指明文件时则占用整个工作区。

- 同一文件的两个写入者顺序执行，避免丢失更新。
- 通道要么全部获得，要么全部不获得，避免交叉申请造成死锁。
- 排队中的调用被取消时会完整释放申请。

tool_boundary 静态检查（tests/lint/protocol_libraries.py）防止绕过类型边界。其他位置构造 DispatchKey 或通道 Hold 会使检查失败。输出裁剪保证 UTF-8 安全，并提供以下三种策略。

- **Head** 保留开头，适合 read、edit、write 等有序内容。
- **Tail** 保留末尾，适合 bash 和 diagnostics 等日志流。
- **HeadTail** 保留首尾，并在中间插入省略标记，适合
  首尾都包含有效信息的工具，例如 grep、web_* 和 git diff / log / status。

现有工具包括 read、write、edit、move、remove、bash、process_start / process_poll / process_stop、grep、glob、list_dir、repo_map、find_definition、search_structural、web_fetch、web_search、todo、diagnostics、test、git_status、git_diff、git_log、git_show、git_blame、git_commit、remember、forget、wipe_memory、search_docs、search_code，以及用于分派子 Agent 的 task 和按需加载技能正文的 skill。

遍历目录的工具（grep、glob、list_dir、repo_map、find_definition、@ 文件选择和符号索引）共用 should_skip_dir 目录排除表，跳过 .git、node_modules、build*、cmake-build*、_deps、target、vendor、.venv 等目录。grep / find_definition 优先使用 ripgrep，并将相同排除表作为 -g 排除参数传入，使内置后端与 ripgrep 的裁剪结果一致。这减少了冷缓存下对构建文件的 stat 和 gitignore 检查，也防止缺少 .gitignore 的仓库中构建产物污染搜索结果。

---

<a id="6-permission-policy-a-constexpr-matrix"></a>

## 6. constexpr 矩阵表达的权限策略

每个工具通过 EffectSet 声明 ReadFs、WriteFs、Net、Exec 四种能力的组合。tool/policy.hpp 中的纯 constexpr 函数 policy::permission(effects, profile) 根据当前 Profile 与能力集合返回 Allow 或 Prompt。规则如下。

| 权限档位 | Pure | ReadFs | WriteFs | Net | Exec |
|-------------|-------|--------|---------|--------|--------|
| **Write** | 允许 | 允许 | 允许 | 允许 | 允许 |
| **Ask** | 允许 | 允许 | 需确认 | 需确认 | 需确认 |
| **Minimal** | 允许 | 需确认 | 需确认 | 需确认 | 需确认 |

Write 自动允许所有工具。Ask 信任只读检查，使 read / grep / glob 不必逐次确认，但修改状态、执行代码或访问网络需要确认。Minimal 对一切接触外部世界的工具要求确认，只自动允许纯工具。Exec 是最强的能力，只要包含它就需要确认，因为 bash 允许模型编写具体副作用，影响范围超过单个文件操作。

整张表在编译期验证。EffectSet 的四个位产生 16 种集合，乘以 3 个档位，恰好为 48 个单元。expected_decision 独立重述策略，constexpr 循环对每个单元 static_assert 检查 permission(e,p) == expected_decision(e,p)。单方面改动任一实现会直接导致构建失败。另一个断言固定集合位数，新增第五种 Effect 时必须同时扩展两侧实现。

运行时通过唯一的 DynamicDispatch::needs_permission 判断是否需要用户批准。未知工具默认要求权限确认。policy::reason 提供权限卡片上的简短说明，例如请求执行任意子进程或修改磁盘文件。

---

<a id="7-tool-scheduling-parallel-safety-from-the-effect-set"></a>

## 7. 根据副作用集合调度工具并发

同一个 EffectSet 也决定两个工具能否并行。effects::is_parallel_safe(active, want) 判断执行 active 副作用期间，是否允许启动具有 want 副作用的工具。

- **WriteFs 和 Exec 要求独占访问。** 写入可能改变其他工具正在读取、写入
  或通过 Shell 访问的状态。看似不同的文件路径也可能重叠。Exec 的命令由模型选择，因此运行时必须串行执行。
- **Pure、ReadFs 和 Net 可以自由组合。** 两个读取不会形成写入竞态，Net
  不操作文件系统或进程状态，todo 等内存纯工具也不操作模型可并发观察的外部数据。

effects.hpp 中的 static_assert 固定独占与组合规则，工具定义还包含 parallel_rule_is_well_founded 检查。副作用按工具对外部世界的实际影响分类。例如 git_status 虽然启动 git 子进程，但其行为已知，因此属于 ReadFs。bash 的具体命令由模型选择，因此属于 Exec。

---

<a id="8-the-streaming-turn-a-phase-fsm-with-a-retry-watchdog"></a>

## 8. 流式轮次的阶段状态机与重试看门狗

一个轮次可以包含多次请求，循环经过 Streaming → AwaitingPermission → ExecutingTool → Streaming → … → Idle。domain/session.hpp 通过阶段变体建模。取消令牌、开始时间和重试计数等 Active 上下文存在于每个非 Idle 分支中，从 Idle 读取这些字段会成为类型错误。合法转移以右值引用获取原状态，并将上下文重新包装到目标状态，使状态机自身负责跨阶段携带轮次数据。

可靠性由两个独立机制保证。

- **重试状态机**采用 retry::Fresh / StallFired / Scheduled，替代
  需要手工同步的两个布尔值。120 秒无进展看门狗触发取消（Fresh → StallFired），合成的 StreamError 通过 Cmd::after 安排重试（→ Scheduled）。等待期间的第二个错误不会重复安排任务，RetryStream 触发后返回 Fresh。
- **两个独立的重试预算。** truncation_retries 处理工具参数尚未接收完整时
  遇到 EOF 的情况，transient_retries 处理 5xx、网络故障、过载和
  429. transient_retries 在单个轮次内可以重置。
  首次内容增量、SSE ping 或思考增量证明传输正常时，计数就重置为 0。因此连接后收到 ping 再卡住的过程，每次尝试都能获得新预算，避免会话过早进入不可恢复状态。

---

<a id="85-context-management-smart-compaction-not-a-fixed-cliff"></a>

## 8.5. 上下文管理与智能压缩

长会话最终需要生成摘要才能放入模型上下文窗口。以下机制减少压缩次数与开销。

- **上下文接近窗口的 95% 时才触发压缩，
  尽量减少昂贵的压缩操作。** domain/session.hpp 的 StreamState::compaction_threshold() 使用 min(kSoftFillPct% × context_max, context_max - kMinOutputHeadroom)，即 min(95%, 窗口 - 20K)。200K 窗口在 180K 触发，1M 窗口可用到 950K。重复会话前缀通常命中提示词缓存（wire_body.cpp 中 Anthropic 的一小时锚点以及固定的 OpenAI prompt_cache_key），每轮成本约为新输入的 10%。压缩本身需要摘要请求，还会重置缓存，使下一轮全量未命中。因此推迟到 95% 可在大窗口上将昂贵操作减少到约四分之一，降低总 token 开销。策略自动执行，旧的 Compaction depth 用户命令已
  移除，以尽量保留上下文并减少消耗。
- **摘要输入切片有上限**，即使窗口很大也能控制摘要开销。
  cmd_factory.cpp 的 wire_messages_for_compaction 从最近会话中选取窗口约 65% 与 kCompactionSliceCap（约 150K）两者的较小值，并保留首个用户轮次以维持原始任务目标。廉价模型一次压缩 150K 的近期历史，效果和成本都优于直接处理 650K。
- **空闲接近缓存过期时提前压缩，避免冷缓存成本骤增。**
  深度使用窗口依赖缓存仍保存会话前缀。如果已用到 950K，离开超过约一小时的缓存 TTL，下次输入会让全部 950K 按完整输入价格计费。update/meta.cpp 的 Tick 处理函数通过 domain/session.hpp 的 should_compact_on_idle() 提前应对。当空闲约 48 分钟（kIdleCompactAfter，即 TTL 的 80%）且前缀至少 200K（kIdleCompactMinTokens）时，在缓存仍热的情况下生成摘要。普通短暂停顿不会触发，这也是唯一主动提前压缩的路径。
- **摘要请求使用当前提供方中满足能力要求的最便宜模型**。
  它与第 8.5.1 节的子 Agent 共用 cheapest_capable_model 路由。过去每次触发都会把完整对话前缀和冗长摘要提示词发送给 Opus / Sonnet，按完整输入价格计算，是长会话中的主要隐藏成本。现在采用 Haiku 级别的摘要模型。
- **1M 上下文窗口是受账户权益控制的显式模型变体**。
  实现与 Claude Code 的模型目录保持一致，并对其发布的可执行文件做过验证。在 OAuth（Pro / Max）路径上，Anthropic /v1/models 中每个支持 1M 的模型都增加一个 <model>[1m] 条目。[1m] 仅用于选择面板，通过 ModelCapabilities::extended_context_1m 扩展 context_window() 并请求 context-1m-2025-08-07 beta。构造传输请求前，wire_model_id() 会去除该标记。

<a id="851-the-subagent-cost-router"></a>

### 8.5.1 子 Agent 的成本路由

task 的只读角色 explorer / reviewer 使用当前提供方中通过能力下限的最便宜模型，要求支持工具且不是嵌入、图像、音频资源或 Weak 档位。tester / coder / general 会修改工作区，因而沿用父模型。ModelCapabilities::tier() 根据模型标识推导同一提供方内部的相对能力，依据模型系列和弱工具调用信号进行排序，不跨提供方比较。路由不会升级到更贵的模型，没有合适的廉价模型时保留父模型，因此单模型账户或只有 Opus 的账户行为不变。

---

<a id="9-safety-boundaries"></a>

## 9. 安全边界

- **访问边界与当前项目是两个根目录概念**，需要分别处理。
  访问边界 util::workspace_root() 控制安全检查，文件工具拒绝越界路径。它默认是启动目录，可由 --workspace DIR 扩大，--workspace / 表示允许整个磁盘。当前项目 util::project_root() 是进程工作目录，agentty 不会离开启动目录，并将项目限制在边界内。相对工具路径和项目级默认值以当前项目为基准。因此即使设置 --workspace /，read src/foo.cpp 仍访问启动项目内的文件。默认两者相同，只有扩大访问边界后才产生区别。normalize_path 的相对路径解析，以及 grep / glob / list_dir 的
  默认目录、repo_map、find_definition、diagnostics / test 的构建目录与清单、Git 工具的 default_git_start、检查点、@ 文件选择器、符号索引和项目级 remember 都使用 project_root()。包含关系检查、沙箱绑定挂载和禁止删除根目录等安全操作使用 workspace_root()。
- **沙箱。** bash 和 diagnostics 默认在 claybin 中运行，平台实现包括
  Linux 的命名空间、Landlock、seccomp 和 cgroup2，macOS 的 seatbelt 与 rlimits（以 sandbox-exec 回退），Windows 的 AppContainer、受限令牌、作业对象和缓解策略。Windows 无回退引擎，无法建立隔离时启动会明确说明。Linux 已移除 bwrap，仅保留一个引擎。工作区、系统库和网络可按策略访问，~/.ssh、/etc 和其他项目为只读。即使批准 bash，也不能读取 ~/.ssh/id_rsa。配置参数为 --sandbox auto|on|off 和 --sandbox-backend claybin。
- **TLS 证书固定。** 对真实上游执行端到端证书固定，
  经过 airgap SOCKS 隧道时也保持这一保证。
- **原子写入。** 每个持久化文件都经过 write、fsync 和 rename，或
  Windows 上等价的 MoveFileExW，因此写入中途崩溃不会破坏会话或凭据存储。

---

<a id="10-rendering-performance"></a>

## 10. 渲染性能

空闲时 fps=0，maya 只在 Msg、输入或定时器触发时渲染。以下两个宿主侧优化降低活动期间的开销。

- **visual_hash** 只组合会改变屏幕内容的状态维度。若与上一帧
  相同，就完全跳过 view 和渲染。它只处理活动消息末尾，已经冻结的历史前缀保持不变。长字符串采用采样哈希，基于时间的动画使用时间桶，每个可见步骤只推进一次哈希。时间桶与实际屏幕动画保持相位同步，使渲染条件和动画不会相互错拍。
- **needs_warmup** 在恢复会话时触发一次不输出到终端的预热渲染，
  将工具较多会话的首个可见帧开销从 O(content) 降为 O(blit)。

---

<a id="11-build-notes"></a>

## 11. 构建说明

- 要求 GCC 14+、Clang 18+ 或 MSVC 14.40+，以及 CMake 3.28+，使用 C++26。
- 启用 -DAGENTTY_STANDALONE=ON 后，存在对应 .a 静态库时，OpenSSL、nghttp2、libstdc++ 和
  libgcc 采用静态链接，libc 仍动态链接。使用 musl 工具链并启用 -DAGENTTY_FULLY_STATIC=ON 可以得到完全静态的程序。
- 默认启用的 -DAGENTTY_USE_MIMALLOC=ON 将 malloc / free 和全局
  new / delete 路由到 mimalloc。CMake FetchContent 跟踪上游 main，并在每次配置时检查更新。仓库不保存 mimalloc 源码或子模块，它以开启 C / C++ 分配覆盖的静态 C 库构建。release_to_kernel() 在较粗粒度的内存释放边界调用 mi_collect(true)。
- **子模块默认不自动更新。** AGENTTY_AUTO_PULL_SUBMODULES 默认关闭，
  因为常规构建中的获取操作会改变头文件修改时间，引起约 64 个编译单元的不必要重编译。需要时运行 cmake --build build --target submodules_sync 或 ./resync.sh。更新采用 merge --ff-only，并拒绝处理存在未暂存、已暂存但未提交修改，或 origin 上不存在的本地提交的子模块，保护本地工作。

---

<a id="12-one-paragraph-mental-model"></a>

## 12. 整体运行流程

src/runtime/main.cpp 解析凭据，通过 Deps 安装提供方路由和存储，再把控制权交给在 jaal 上运行程序的 maya。maya 调用 view(model) 绘制界面，并为每个事件调用对应领域的 AgenttyApp::update(Model&, DomainMsg, now)。用户输入和流式数据块变成 Msg，领域处理函数更新模型并返回描述副作用的 Cmd。工具经过 JSON 分派边界、constexpr 权限策略和操作系统沙箱执行，结果作为新的 Msg 回到循环。循环通过运行时执行的显式 Cmd 完成外部操作，不直接修改全局状态。
