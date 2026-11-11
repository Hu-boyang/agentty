# agentty 的核心设计

agentty 用类型表达 Agent 的行为。值具有明确类型，状态机使用和类型，可能失败的操作返回
`std::expected<T, E>`，副作用、身份和能力等跨领域约束也体现在类型中。
JSON、网络和终端 I/O 位于一层薄薄的动态边界内，越过边界后使用带类型的数据。

本文规定贡献代码时需要遵守的设计约束。新增代码应与现有代码保持一致，使阅读者能够沿用同一套理解方式。

---

## 七项规则

### 1. 状态机使用 std::variant，每个分支拥有自身数据

具有多种形态的状态应表达为封闭和类型。每个分支保存只在该状态下有效的数据，
避免把枚举、布尔值和可能无效的字段拼在一起。

```cpp
// ✗ output 的含义依赖 error，状态关系需要调用者记住。
struct ToolExecOutput { ToolCallId id; std::string output; bool error; };

// ✓ 各分支拥有自身数据。
struct ToolExecOutput { ToolCallId id; std::expected<std::string, ToolError> result; };
```

可参考 `ToolUse::Status` 的 Pending / Approved / Running / Done / Failed / Rejected
状态、`Phase`、`Msg` 和 `provider::ErrorClass`。

“此字段仅在 X 为真时有效”这样的注释，通常意味着应将字段移入和类型的对应分支。

### 2. 可能失败的操作返回 std::expected，错误类型包含 Kind 枚举

错误通过明确类型传递，并在层边界捕获异常、转换成对应错误。枚举描述错误种类，
自由文本补充供人阅读的细节。

```cpp
// ✗ 下游必须匹配字符串才能判断错误。
std::expected<Response, std::string> send(...);

// ✓ 调用者根据 kind 分派。
enum class HttpErrorKind { Tls, Timeout, Refused, Status, Cancel };
struct HttpError { HttpErrorKind kind; int http_status = 0; std::string detail; };
std::expected<Response, HttpError> send(...);
```

可参考 `ToolError`、`ExecResult` 和 `provider::ErrorClass`。
下游需要匹配错误消息中的子串时，应检查上游是否遗漏了带类型的错误信息。

### 3. ID、路径、URL 和名称使用强类型

```cpp
// ✗ 调用者可以交换参数，编译器无法发现。
void run(std::string id, std::string path, std::string command);

// ✓ 不同概念由不同类型表达。
void run(ToolCallId id, FilePath path, BashCommand command);
```

可参考 `ToolCallId`、`ThreadId`、`ModelId`、`CheckpointId` 和 `ToolName`。
新增概念时，遵循 `domain/id.hpp` 的 `Id<Tag>` 模式定义带标签的类型。

### 4. 每个概念有唯一的权威定义

策略决策和字段不变量各自在一处定义。工具不能覆盖权限策略，调用者也不能自行复制
`provider::classify` 的分类逻辑。

```cpp
// ✗ 每个工具分别维护权限判断，容易遗漏或产生差异。
t.needs_permission = [](Profile p){ return p != Profile::Write; };

// ✓ 工具声明能力，由唯一的策略函数决策。
t.effects = {Effect::WriteFs};
// permission(effects, profile) 定义在 tools/policy.hpp。
```

可参考 `provider::classify`、`policy::permission` 和基于 `std::visit` 的 `update`。

### 5. 分层隔离，领域类型只保存值，I/O 通过概念接口提供

```text
include/agentty/domain/            纯值类型，不做 I/O、不含界面和线程
include/agentty/{io,provider,tool}/ 通过概念接口提供能力，由 deps 注入
src/runtime/                      Elm 循环，update 返回包含 I/O 的 Cmd
third_party/maya/                  展示与运行时 API，运行时委托给 jaal
```

领域函数不能打开文件，状态更新函数应返回 `cmd::io_task`，由副作用执行 `http::send`。
视图函数也不能修改模型。

所有执行 I/O 或修改进程全局状态的 agentty 函数都需要 `agentty::Io` 令牌
（见 `util/io.hpp`）。状态更新函数和视图拿不到令牌，因此无法调用这些函数。
副作用主体通过 `cmd::io_task`、`io_task_isolated` 或 `io_stream` 获得令牌，
宿主持有自己的令牌。只有 `tests/lint/io_roots.txt` 列出的文件可以创建令牌，
由 `io_roots` 检查约束。

原始标准库和操作系统调用（时钟、getenv、fstream 等）无法附加令牌，因此
`elm_purity` 仍按名称禁止这些调用。原始 JSON、HTTP 和终端转义序列集中在动态边界，
边界内侧使用带类型的数据。

### 6. 用类型声明副作用

函数会修改文件系统时，这一事实应通过 `Effect::WriteFs` 表达。
权限策略只读取这些能力声明。

```cpp
// ✗ 每个调用点自行记住哪些工具需要确认。
if (tool_name == "bash" || tool_name == "write" || ...) prompt();

// ✓ 统一依据工具能力决策。
if (policy::permission(td->effects, profile) == Decision::Prompt) prompt();
```

可参考 `tools::Effect`、`tools::EffectSet` 和 `tools::policy::permission`。
增加 `Clipboard`、`Browser` 等副作用时，应扩展枚举和策略表。

### 7. 用显式状态表达字段的有效范围

```cpp
// ✗ active 与 result 隐含两套需要同步的状态关系。
bool active;
std::optional<Result> result;

// ✓ 明确列出所有状态，遗漏分支时编译器可以报错。
using Status = std::variant<Idle, InFlight, Done, Failed>;
Status status;
```

`expanded`、`collapsed`、`dim` 等真正独立的二元属性可以使用 `bool`。
决定其他字段是否有效的状态应使用明确的和类型。

---

## 各层采用的模式

### 领域层 include/agentty/domain/

- 只包含值类型。用 `std::variant` 表达状态，用 `std::expected` 表达失败。
- 为枚举或变体提供 `to_string(X)`，供序列化和渲染使用。
- 通过 `Id<Tag>` 提供强类型 ID。
- 不执行 I/O、不创建线程、不持有静态可变状态。

### I/O 与提供方层 include/agentty/io/、include/agentty/provider/

- 通过概念接口提供能力，经 `deps()` 注入，使测试可以替换为伪实现。
- 公共方法返回带有明确错误 `Kind` 的 `std::expected<T, FooError>`。
- 在边界捕获异常并转换为带类型的错误。
- 原始 JSON 和字符串留在实现内部，对外返回带类型的值。

### 工具层 include/agentty/tool/、src/tool/、third_party/mcp-cpp/src/tools/

- [注册表](../src/tool/registry.cpp)通过[桥接层](../src/tool/mcp_tools_bridge.cpp)
  从 mcp-cpp 工具集获得工具目录。桥接层将模式定义、副作用、结果和文件变化转换成 agentty 类型。
- 通用工具位于 [mcp-cpp](../third_party/mcp-cpp/src/tools/)。agentty 拥有
  [宿主适配器](../src/tool/mcp_tools_backends.cpp)、状态、执行、权限和沙箱边界。
- 每个工具声明自身 `effects`，权限判断由统一策略提供。
- 模式定义在分派边界仍使用 JSON。

### 运行时 src/runtime/

- 实际运行时调用 [program.hpp](../include/agentty/runtime/app/program.hpp) 中的
  `AgenttyApp::update(Model&, DomainMsg, now)`，记录传入时间，调用对应领域的自由函数重载，
  再发布派生状态。[update/](../src/runtime/app/update/) 中的处理函数修改模型并返回 `Cmd`，
  I/O 由返回的副作用执行。
- [update.cpp](../src/runtime/app/update.cpp) 的 `update(Model, Msg) → (Model, Cmd)`
  是处理完整消息的测试入口。它用确定的时间调用相同的领域处理函数，并执行相同的发布步骤。
- `view(Model) → Element` 是不修改状态的纯函数。
- `subscribe(Model) → Sub<Msg>` 是纯函数，返回当前状态所需的订阅。
- 副作用通过 `Cmd::task` / `Cmd::after` 执行。

### 视图层 src/runtime/view/*

- 读取 `Model` 并返回 `Element`，不修改模型。
- 需要感知宽度的组件通过带 `measure` 回调的 `ComponentElement` 向父布局报告尺寸。
- 各发言方和状态的颜色由 `palette.hpp` 提供。

---

## 新贡献者的阅读路线

沿[源码阅读入口](../README.md)从启动读到渲染。

1. 读 [`src/runtime/main.cpp`](../src/runtime/main.cpp)，再读
   [`program.hpp`](../include/agentty/runtime/app/program.hpp)，理解启动、依赖与运行时钩子。
2. 读 [`model.hpp`](../include/agentty/runtime/model.hpp)、
   [`domain/`](../include/agentty/domain/) 和 [`msg.hpp`](../include/agentty/runtime/msg.hpp)，
   理解状态与事件。
3. 读 [`update.hpp`](../include/agentty/runtime/app/update.hpp) 和
   [`update/`](../src/runtime/app/update/) 的领域处理函数，再看
   [`update.cpp`](../src/runtime/app/update.cpp) 提供的完整消息测试入口。
4. 读 [`cmd_factory.cpp`](../src/runtime/app/cmd_factory.cpp) 与
   [`provider/dispatch.cpp`](../src/provider/dispatch.cpp)，理解副作用与模型请求路由。
   然后读[工具接口](../include/agentty/tool/)、[宿主桥接层](../src/tool/mcp_tools_bridge.cpp)
   和 [mcp-cpp 工具实现](../third_party/mcp-cpp/src/tools/)。
5. 读 [`view.cpp`](../src/runtime/view/view.cpp)，再读[视图组件与面板](../src/runtime/view/)。

文档与实现不一致时，以代码和测试为准，并相应更新文档。

---

## 具体示例

### 动态边界上的带类型错误

`http::Client::send` 返回 `HttpResult = std::expected<Response, HttpError>`。
`HttpErrorKind` 包含 Cancelled、Resolve、Connect、Tls、Protocol、SocketHangup、Timeout、
PeerClosed、Status、Body、Unknown。poll()、SSL_connect、nghttp2 帧或空闲超时等故障，
都通过静态工厂构造正确的错误类型。分类器根据 `kind` 分派。

```cpp
constexpr ErrorClass classify(const http::HttpError& e) noexcept {
    using K = http::HttpErrorKind;
    switch (e.kind) {
        case K::Cancelled:                  return ErrorClass::Cancelled;
        case K::Resolve: case K::Connect: case K::Tls:
        case K::Protocol: case K::SocketHangup:
        case K::Timeout: case K::PeerClosed: return ErrorClass::Transient;
        case K::Status:
            if (e.http_status == 401 || e.http_status == 403) return ErrorClass::Auth;
            if (e.http_status == 429)                          return ErrorClass::RateLimit;
            if (e.http_status == 408 || e.http_status == 502
             || e.http_status == 503 || e.http_status == 504
             || e.http_status == 529)                          return ErrorClass::Transient;
            return ErrorClass::Terminal;
        case K::Body: case K::Unknown:      return ErrorClass::Terminal;
    }
}
```

新增 `HttpErrorKind` 后必须补齐处理分支才能通过编译，策略因此保持穷尽。

### 和类型凭据

`auth::Credentials = std::variant<cred::None, cred::ApiKey, cred::OAuth>`。
None 不含数据，ApiKey 只保存 key，OAuth 保存 access_token / refresh_token / expires_at_ms。
操作通过自由函数中的 `std::visit` 访问变体。增加 `cred::DeviceFlow` 等分支时，
所有访问函数都必须补齐分支才能编译。

### 带类型的反序列化

`persistence::load_thread_file(path)` 返回 `std::expected<Thread, DeserializeError>`。
`DeserializeError::Kind` 包含 JsonParse、MissingField、InvalidValue、InvalidVariantTag 和 Io。
`parse_message`、`parse_thread`、`parse_tool_status` 用单子式组合传递错误。
遍历目录的加载器将错误种类和字段路径记录到 stderr，让用户知道哪个文件损坏以及原因，
避免通过 `catch (...) { continue; }` 静默忽略故障。

### 派生状态

`StreamState::active()` 从 `!is_idle()` 推导结果。过去单独保存 `bool active`，
调用者必须让它与 phase 同步，记住 active 当且仅当 phase != Idle 的不变量。
现在变体状态是权威来源，布尔值只是对它的一种观察。

### 和类型选择面板

模型选择和会话列表使用 `pick::OneAxis = variant<Closed, OpenAt{index}>`。
差异审查使用 `pick::TwoAxis = variant<Closed, OpenAtCell{file, hunk}>`。
待办覆盖面板使用 `pick::Modal`。
检查点回退面板的 `CheckpointPickerState = variant<Closed, Open{index}>`
（见 `runtime/checkpoint_picker.hpp`）同样采用单轴结构，每个用户轮次一行，Select 将固定的快照交给
已有的 RestoreCheckpoint 回退逻辑。

这避免了 `bool open + int index` 中关闭时 index 无意义的问题。
打开时赋值 `m.ui.x = pick::OpenAt{i}`，关闭时赋值 `m.ui.x = pick::Closed{}`，
修改时使用 `if (auto* p = pick::opened(m.ui.x)) p->index = …`。

### 副作用类型与权限

每个工具声明 `tools::EffectSet`，取值为 ReadFs / WriteFs / Net / Exec 的子集。
唯一的 `tools::policy::permission(EffectSet, Profile) → Decision` constexpr 函数
决定是否需要确认。增加副作用时扩展枚举和策略分支，工具不提供单独覆盖权限的逻辑，
`ToolDef::needs_permission` 已从类型中移除。

### 使用 expected 表达工具结果

`Msg::ToolExecOutput` 保存 id 和 `std::expected<std::string, ToolError> result`。
过去的 `{id, output, bool error}` 让 output 的含义依赖布尔值。
现在状态更新函数使用 `if (e.result) … else …`，并将 `ToolError::kind` 传到视图。

这些示例展示了本项目用类型表达约束的方式，新增代码应遵循相同模式。

---

## 编译期证明

C++26 的 `consteval` 与 `static_assert` 可以把不变量转化为构建时检查。
能够在编译期证明的属性应在编译期验证，使破坏属性的修改直接导致构建失败。

### 信任矩阵 include/agentty/tool/policy.hpp

权限策略 `(EffectSet × Profile) → Decision` 是 constexpr 函数，
信任矩阵的每个单元都可以通过 static_assert 验证。
头文件末尾的 proofs 命名空间描述了这个矩阵。

```cpp
static_assert(permission(kPure,  Profile::Write) == Decision::Allow);
static_assert(permission(kExec,  Profile::Write) == Decision::Allow);
static_assert(permission(kRead,  Profile::Ask)   == Decision::Allow);
static_assert(permission(kWrite, Profile::Ask)   == Decision::Prompt);
static_assert(permission(kRead,  Profile::Minimal) == Decision::Prompt);
// 共 17 个单元和 5 项结构属性。
```

读者可以直接检查这些断言。修改策略时，出错单元会产生构建错误，
证明与被验证的函数放在一起，无需单独运行测试或加载测试数据。

### HTTP 错误到 ErrorClass 的映射 include/agentty/provider/error_class.hpp

重试策略也采用同一方式。

```cpp
static_assert(classify(HttpError{HttpErrorKind::Timeout, 0, ""}) == ErrorClass::Transient);
static_assert(classify(HttpError{HttpErrorKind::Status, 429, ""}) == ErrorClass::RateLimit);
static_assert(classify(HttpError{HttpErrorKind::Status, 401, ""}) == ErrorClass::Auth);
// 覆盖每个 HttpErrorKind，以及具有明确重试语义的状态码。
```

调整 classify 的分支顺序，或增加错误种类而遗漏处理时，会在 HTTP 请求走错分支前使构建失败。

### 工具目录 include/agentty/tool/spec.hpp

工具名称、副作用和 FGTS 开关存放在 constexpr std::array 中，不变量由 consteval 谓词表达。

```cpp
static_assert(all_names_unique());
static_assert(only_known_exec_tools());
static_assert(no_writefs_and_exec_combo());
static_assert(readonly_invariants());
static_assert(only_web_is_net());
```

查找函数是 consteval 函数，各工具工厂使用 FixedName 非类型模板参数。

```cpp
constexpr const auto& kSpec = spec::require<"bash">();   // ✓ 可以编译
constexpr const auto& kSpec = spec::require<"bsh">();    // ✗ static_assert 失败
```

工具工厂中的名称拼写错误会在对应编译单元报错。目录统一定义能力，
工厂不能覆盖已声明的副作用，也不能注册目录之外的名称。

### 精化类型 include/agentty/domain/refined.hpp

`Refined<T, Predicate>` 在类型层面保证内部值满足谓词。
构造函数为私有，只能通过执行验证并返回 expected 的 try_make 创建对象。

```cpp
auto cmd = NonEmpty<std::string>::try_make(args.command);
if (!cmd) return std::unexpected(ToolError::invalid_args("..."));
// 从这里开始，可以证明 cmd->value() 非空。

using Port = Bounded<std::uint16_t, 1, 65535>;   // 无法表示 0 或 65536 以上的值
```

这些基础类型在头文件的 tests 命名空间中包含 static_assert 自检。
新增精化类型时，也应在附近添加对应断言。

### 编译期证明的意义

阅读 policy.hpp、error_class.hpp 和 spec.hpp 时，可以先读证明，再读实现。
证明由编译器实际检查。类似的属性在 Rust 中可以用 `#[test]` 放到 CI 运行，
这里则通过 static_assert 在每次构建时验证。

新增代码应同时提供可写成 constexpr 的谓词，以及固定其行为的 static_assert。
运行时表格的各行也应具有编译期正确性检查。
