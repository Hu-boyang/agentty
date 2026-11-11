<a id="protocol-libraries-no-runtime-one-copy-agentty-drives"></a>

# 由 agentty 驱动的协议库与共享核心

本文规定 jsonrpc-cpp、mcp-cpp、acp-cpp、rag-cpp 和 claybin 的集成约束。实现与约束冲突时需要修正实现。修改必须符合这些规则，或先说明原因并更新规则，不能将问题留待以后处理。

本文将 [LAYERING.md](LAYERING.md) 第 5 项规则明确为子模块完全不持有运行时。各库都是被动库，agentty 拥有整个程序并负责驱动它们。

<a id="contents"></a>

## 目录

1. [基本模型](#1-the-model)
2. [约束规则](#2-the-rules)
3. [库允许包含的内容](#3-what-a-library-may-contain)
4. [如何满足各类需求](#4-how-each-need-is-met-without-a-runtime)
5. [共享 JSON-RPC 核心](#5-one-copy-the-shared-json-rpc-core)
6. [agentty 提供的能力](#6-what-agentty-provides)
7. [约束检查方式](#7-how-it-is-enforced)
8. [迁移计划与顺序](#8-migration-plan-and-order)
9. [修改前的决策检查清单](#9-decision-checklist-for-any-change)
10. [术语表](#10-glossary)

---

<a id="1-the-model"></a>

## 1. 基本模型

```
agentty     应用程序，管理全部线程、计时器、锁和进程，
   │        决定何时执行什么任务，并驱动各个库。
   ▼
 maya       agentty 访问运行时的唯一接口（maya::guarded、pool、
   │        scope、delay_for、stop_group、published、platform::process 等）
   ▼
 jaal       运行时本体。

 mcp-cpp ─┐
 acp-cpp ─┼─ 被动库，只在被调用时执行，不自行使用线程、
 rag-cpp ─┘  时钟、锁或进程。
 claybin     同样被动：构建沙箱方案，由 agentty 启动。
```

库由**值和函数**组成。调用时，它在调用方线程上完成工作并返回。它绝不会：

- 在后台运行任何任务；
- 等待任何事件，例如计时器、响应或管道数据；
- 在两条线程可同时访问的调用之间共享可变状态；
- 决定任何工作在哪条线程上执行。

需要上述能力的逻辑全部放在 agentty 中，基于 maya → jaal 实现。库暴露**状态机**，agentty 负责**驱动状态机**。

可以用这个问题检验设计：*如果库中的每个函数都按某种顺序在同一条线程上调用，库能否正常工作？* 答案必须是能。并发由 agentty 在库外部提供，库本身既不假设并发存在，也不提供并发机制。

<a id="2-the-rules"></a>

## 2. 约束规则

**R1. 库中禁止使用运行时原语。** 禁止 `std::thread`、`std::jthread`、`std::async`、`.detach()`、任何种类的 `std::mutex`、`std::condition_variable`、`std::atomic`、`thread_local`、`std::this_thread::sleep_*`、`std::stop_source`、`std::future`/`promise`、`std::latch`/`barrier`/`semaphore`。“独立运行的回退实现”也不例外。库若需要线程配合，应说明宿主必须调用哪些函数。

**R2. 库中也禁止定义运行时接口。** 禁止由库向外调用的 `Runtime`、`Executor`、`set_runtime`、`spawn`、`sleep_for` 或 `parallel_for` 钩子。即使注入运行时，仍然是库在决定何时开展并发工作，只是借用了外部线程。宿主应主动调用库；库不反过来安排调度。

唯一例外是纯粹的**数据并行内核**，且工作已由调用方划分（见 R6 和 §4.6）；即便如此，也必须通过参数传入划分方式，不能使用全局对象。

**R3. 禁止共享可变状态。** `static`、全局变量和函数局部静态变量在初始化后都不得变化。一个库对象在同一时刻只由一个调用方拥有。如果两个线程需要访问同一个对象，由 agentty 将它放进 `maya::guarded`、`maya::published`，或交给单个工作线程管理，库无需感知这些安排。

不可变的静态对象可以使用，例如查找表、`constexpr` 对象或只构建一次的 `const` 集合。

**R4. 禁止读取时钟。** 库不读取 `steady_clock` 或 `system_clock`。当前时间通过参数 `now` 传入，截止时间也作为值传入；是否超时由二者的纯函数判断。时钟由调用方掌控。

**R5. 禁止创建进程或执行阻塞 I/O。** 库不启动子进程、创建管道或阻塞读取管道。它解析传入的字节，并生成待发送的字节。进程启动和读写由 agentty 负责，使用 maya 的 `platform::process` 和轮询反应器。协议库 jsonrpc-cpp、mcp-cpp、acp-cpp 完全不进行文件 I/O：mcp-cpp 的工具只能通过宿主传入的 `FileSystem`（`HostServices::files`）访问磁盘，库本身不提供磁盘实现；agentty 的实现是 `tools::util::DiskFiles`。存储库 rag-cpp 是例外：在调用方线程上，它可以同步读写调用方指定的文档、索引和 WAL，因为这正是它的职责。claybin 是进程启动方面的例外：构建并启动受限子进程就是它的用途，agentty 在自己的进程管理代码中调用它。

**R6. 确定性与可重入。** 相同输入应得到相同输出。禁止会改变行为的隐藏缓存；允许按输入建立纯记忆缓存，但缓存必须属于调用方持有的对象，不能是静态对象。库触发的回调必须在触发该回调的调用内部、调用方线程上同步执行，执行回调时不得持锁。

**R7. 只保留一份实现。** 多个库中存在重复代码就是缺陷。JSON-RPC 分帧、消息封装、标识与等待项表、编解码代数、`Sum`/`Maybe`/`Newtype` 类型体系和协程基础设施，都只在共享核心（§5）中实现一次。mcp-cpp 和 acp-cpp 依赖该核心，各自不保留副本。

**R8. 可以破坏公共 API，不能破坏规则。** 这些库服务于 agentty。现有 API 与规则冲突时，应修改 API；不能为了兼容性保留已经废弃的线程实现。

**R9. agentty 是唯一集成方。** 库的工作与线程、计时器、锁或进程发生交互的位置，都必须位于 agentty 中，每个库对应一个集成模块，并只使用 maya 提供的接口。其他 agentty 模块不能直接调用库中呈现异步形态的 API。

<a id="3-what-a-library-may-contain"></a>

## 3. 库允许包含的内容

| 允许 | 禁止 |
|---|---|
| 普通类型、值语义、`std::variant`、`std::optional`、`std::expected` | `std::thread`、`jthread`、`async`、`detach` |
| 纯函数、`constexpr`、模板 | 任何互斥量、`condition_variable`、`atomic` |
| 通过方法修改 `*this` 的对象，同一时刻由一个调用方拥有 | `thread_local`、可变静态或全局对象 |
| 同步执行的 `std::function` 回调 | 由库向外调用的 `Runtime`、`Executor`、`set_*` 钩子 |
| 同步读取调用方指定的文件 | 创建进程、管道、套接字或执行轮询 |
| 通过参数接收 `now` 或截止时间 | 直接读取时钟 |
| 用动作列表或截止时间等数据返回“下一步做什么” | 休眠、等待或阻塞在 future 上 |
| 通过参数接收划分器的 `parallel_for` 形式计算内核（§4.6） | 自行决定并行方式 |

`const_cast` 和裸指针不属于本文讨论范围；它们涉及内存安全，由常规审查和禁用规则处理。

<a id="4-how-each-need-is-met-without-a-runtime"></a>

## 4. 不持有运行时时如何满足各类需求

各小节分别说明需求、原有或待移除的实现，以及应采用的替代结构。

<a id="41-reading-frames-off-a-transport"></a>

### 4.1 从传输层读取帧

- **原实现：** 每个库的 `StdioTransport` 启动一个读取任务，循环调用
  `getline`，并在该线程上把数据送入引擎。*（已移除）*
- **规则要求：** 库不包含主动读取的传输层。引擎接收
  `step(engine, Received{line})`；未分帧的字节流由值类型 `jsonrpc::LineSplitter` 处理。持有字节的一方负责调用。
- **agentty：** `rpc::Peer` 的读取任务读取通道，例如子进程的
  标准输入输出、自身文件描述符或以管道形式呈现的 HTTP 传输，然后通过引擎所有者推进状态（§4.4）。

<a id="42-writing-frames"></a>

### 4.2 写入帧

- **原实现：** 引擎在写互斥量保护下调用 `Transport` 回调。
  *（已移除）*
- **规则要求：** 引擎不直接写入。所有产生输出的操作都要
  **返回输出**，例如 `Outcome { std::vector<std::string> out; ... }`，或同步追加到调用方提供的输出容器。输出顺序就是调用顺序，无需加锁。
- **agentty：** `rpc::Peer` 将帧放入有序发件队列，由写入
  任务排空队列，因此执行写操作时不持锁。

<a id="43-requestresponse-matching-and-timeouts"></a>

### 4.3 请求响应匹配与超时

- **原实现：** `request_raw` 返回 `std::future`，等待项表由
  互斥量保护，专用截止时间线程使超时的等待项失败。*（已移除）*
- **规则要求：** 引擎是一个 Elm 模型（§5）：
  - `request<M>(engine, params, deadline) -> {RequestId, Effects}` 记录一个
    待处理项，`Effects.send` 保存待写入的帧。
  - `step(engine, Received{line})` 将响应匹配到 `Effects.completed` 中，形式为
    `{RequestId, method, expected<Json, RpcError>}`；`result<M>(c)` 再将其解码为 `M::result`。
  - `step(engine, Tick{now})` 使所有超过截止时间的条目失败。
    `next_deadline()` 告知下次值得发送 Tick 的时刻。
  - 无需 future 或 promise。完成结果就是交还给调用方的值。
- **agentty：** `rpc::Peer` 按标识暂存完成结果，调用方
  使用 `guarded::wait_with` 等待自己的结果。计时任务通过 `maya::delay_for` 休眠到 `next_deadline()`，然后推进 `Tick{now}`。

<a id="44-thread-safety-of-an-engineconnection-object"></a>

### 4.4 引擎和连接对象的线程安全

- **原实现：** `RpcEngine` 和 `ClientProvider` 内部加锁，允许任意线程
  调用。*（已移除）*
- **规则要求：** 引擎由**单一所有者**管理。方法不保证
  线程安全，并明确说明这一点。成员既不使用原子变量，也不自行加锁。
- **agentty：** 每个连接的引擎放在 `rpc::Peer` 内的一个 `maya::guarded` 中；
  所有状态推进都在其保护下执行，副作用则在保护范围外执行。

<a id="45-handlers-async-replies-cancellation"></a>

### 4.5 处理函数、异步响应与取消

- **原实现：** 引擎注册的处理函数在读取线程上执行；
  异步处理函数捕获 `Responder`；取消监视器启动轮询任务。*（已移除）*
- **规则要求：** 对端请求以**值**的形式出现在
  `Effects.calls` 中（`Call{Id, method, params}`）；引擎不执行处理函数。回复使用 `reply<M>(id, result) -> frame`，所有者获得答案后再写出。放弃己方请求通过 `step(engine, Cancel{RequestId})` 表达，通知对端则使用协议自身的通知。对端发来的取消出现在 `Effects.notifications`。带类型的**分派表** `Router<Ctx>` 是纯辅助工具：`router.handle(ctx, call) -> Maybe<frame>` 在调用方线程上同步执行；若方法需要延后回复，则返回 Nothing。
- **agentty：** 决定哪些请求立即处理，哪些交给工作线程
  （`Peer::Deferred<M>` 稍后回复一次），并拥有全部取消令牌。`CallOptions::cancel` 是真实的 `std::stop_token`，连接到 `stop_callback`，不使用轮询。

<a id="46-data-parallel-work-rag-cpp-index-build-batch-embedding-search"></a>

### 4.6 数据并行任务（rag-cpp 索引构建、批量嵌入与搜索）

- **原有实现：** `rag::util::parallel_for` 通过已安装的 `Executor` 执行
  （回退到 `std::jthread`）；部分索引内部持有互斥量。
- **规则要求：** 可拆分的计算函数通过
  参数接收划分器：

  ```cpp
  // Splitter 对 [0, n) 中的每个 i 执行 fn(i)，由调用方提供。
  using Splitter = std::function<void(std::size_t n,
                                      const std::function<void(std::size_t)>& fn)>;
  Result<Index> build_index(const Corpus&, const Options&,
                            const Splitter& split = serial);
  ```

  `serial` 是普通循环，也是默认实现。每次 `fn(i)` 只写入预先分配的独立槽位，不共享可变状态，因此无需加锁。索引结构构建完成后冻结；对冻结索引的查询为 `const` 只读操作，因此无需锁也能安全并发查询。
- **agentty：** 传入基于 `maya::scope` 的划分器。若其他线程正在
  查询索引，由 agentty 构建新索引，再通过 `maya::published` 替换。

<a id="47-caches-and-registries-inside-a-library"></a>

### 4.7 库内部的缓存与注册表

- **原有实现：** `rag::cache`、`rag::plugin::registry`、mcp-cpp 的
  `cap::registry` 和 `fs_helpers` 快照，都是由互斥量保护的静态对象。
- **规则要求：** 缓存必须是**调用方拥有的对象**，例如 `Cache c;
  c.get(key)`。注册表是构建一次后传入的值，例如 `Registry r = Registry::builtin(); engine.use(r);`，也可以是不可变表。
- **agentty：** 拥有实例；需要跨线程共享时，使用
  `maya::guarded` 或 `maya::published`。

<a id="48-processes-mcp-cpp-toolsprocesscpp-stdio-servers"></a>

### 4.8 进程（mcp-cpp 的 `tools/process.cpp` 与标准输入输出服务器）

- **原实现：** mcp-cpp 自行启动服务器进程并执行 shell 工具，
  使用读取线程和输出互斥量。服务器进程现由 agentty 的 `mcp::stdio_link` 管理；当时 `ChildProcess` 仍保留在 mcp-cpp 中，作为 agentty 调用的可移植进程启动原语。
- **规则要求：** 库描述进程，例如 `ProcessSpec {argv, env,
  cwd, stdin}`，并解析输出，例如 `parse_tool_output(bytes) -> Result`，但不启动进程。对于职责就是运行命令的内置工具，其 `execute` 接收 `HostServices`，由 agentty 实现其中的 `exec(ProcessSpec, limits) -> ExecResult`。shell 工具已使用 `HostServices::exec`，它应成为唯一执行路径。
- **agentty：** 基于 `maya::platform::process` 实现 `exec`，并管理空闲
  超时和总运行时间限制（`src/tool/util/exec.cpp`）。

<a id="49-coroutines"></a>

### 4.9 协程

- **原实现：** `mcp::co` 和 `acp::co` 的 `Task` 类型使用 `await_jobs` 注册表，
  每等待一个 future 就启动一个任务。*（两处均已删除）*
- **规则要求：** 协程**类型**可以放在共享核心中，例如 `Task<T>` 的 promise 类型，
  以及等待库定义事件的可等待对象；但库不能自行选择线程恢复协程。可等待对象保存句柄，完成结果从 `Outcome.completed` 到达时，由**调用方**恢复。不能每次 await 都创建一个任务。
- **agentty：** 完成结果到达后，由引擎所有者恢复相应句柄。

<a id="5-one-copy-the-shared-json-rpc-core"></a>

## 5. 唯一共享的 JSON-RPC 核心

mcp-cpp 和 acp-cpp 是同一传输模型上的两种协议方言。它们曾各自携带一份近乎相同的头文件：runtime.hpp 完全相同，coro.hpp 只差两行，core、codec、rpc、stdio 则是逐渐分化的副本。按照 R7，它们调整为：

```
jsonrpc-cpp   独立仓库，位于 third_party/jsonrpc-cpp；仅头文件，使用 C++23，
               唯一外部依赖是 nlohmann_json。
  core.hpp      类型代数：Unit、Maybe、List、Sum、Newtype、StaticString、match
  codec.hpp     Codec<T>：通过归并序列化，使用 record、sum_tagged、enum_codec
  error.hpp     Id（对端标识）、RequestId（己方标识）、RpcError、errc
  message.hpp   Message = Call | Notification | Reply，只在边界解析一次
  method.hpp    Method<"name", Params, Result>, Note<"name", Params>
  engine.hpp    Engine + step(Engine&, Event) -> Effects，构成 Elm 模型
  router.hpp    Router<Ctx>：按方法类型分派对端调用
  framing.hpp   LineSplitter：将字节流拆分为行

mcp-cpp  链接 jsonrpc::jsonrpc，拥有 MCP 类型、方法、能力，
         客户端与服务器协议状态机，以及内置工具逻辑。
acp-cpp  链接 jsonrpc::jsonrpc，拥有 ACP 类型、方法、会话更新，
         以及代理与客户端协议状态机。
```

引擎是一个 Elm 模型：状态是值，唯一的状态转换为

```
Effects step(Engine&, Event)
Event   = Received{line} | Tick{now} | Cancel{RequestId} | Closed{reason}
Effects = send（按顺序发送的帧）+ completed（己方请求的完成结果）
        + calls（对端请求）     + notifications（对端通知）
```

而启动请求的 `request<M>(engine, params, deadline)` 会返回新生成的 `RequestId` 及其副作用。宿主构成外围的 Elm 运行时，负责输入事件、执行副作用，并为 `next_deadline()` 安排计时器。

类型承载协议定义：

- **每个方法只声明一次**，形式为 `Method<"name", Params, Result>`。
  带类型的 `request<M>` 只接收 `M::params`，`result<M>` 只解码为 `M::result`，`Router::on<M>` 只接受 `f(Ctx&, const M::params&) -> M::result` 形式的处理函数。调用方、处理函数和编解码器之间的类型偏差会在编译时被发现。
- **两种标识使用两种类型。** `Id` 表示对端发送的标识，可以是数字或
  字符串；`RequestId` 表示己方生成的标识。`Cancel` 只接受 `RequestId`，二者不能互相转换。
- **`Newtype` 不会自动退化为底层类型。** 它不提供隐式转换，必须
  用 `.get()` 解包。此前的隐式转换曾允许把 `RequestId` 错当作传输层的 `Id`。
- **只解析一次。** 帧在边界处转换为 `Message`，之后通过
  穷尽式 `match` 处理。格式错误的输入以值表达，并携带对应错误回复，不使用异常。

共享核心的规则：

- 它是实现 JSON-RPC 2.0 的**唯一位置**。某种协议方言若需要
  不同行为，应在核心中增加选项，不应创建分支副本。
- 协议方言库在共享类型体系的地方，通过 `using` 将核心名称重新导出到自身命名空间
  （`mcp::`、`acp::`），调用方无需关心类型实际定义在哪里。
- 它遵守 §2 的全部规则，且不依赖任何上层库，包括 agentty、
  maya、jaal、mcp-cpp 和 acp-cpp。
- **不依赖 jaal。** jaal 是运行时且需要 C++26；共享核心是纯状态机，
  不需要它们。Elm 结构直接体现在类型中。
- 核心位于独立仓库 `1ay1/jsonrpc-cpp`，作为 agentty 的子模块。
  agentty 在 mcp-cpp 和 acp-cpp 之前添加该模块；独立构建时，核心会获取 nlohmann_json 并运行自身测试，无需 `-pthread`。
- jsonrpc-cpp、mcp-cpp、acp-cpp 和 rag-cpp **统一使用 C++23**
  （需要 `std::expected`）。acp-cpp 接入核心时从 C++20 升级。

<a id="6-what-agentty-provides"></a>

## 6. agentty 提供的能力

所有能力均使用 maya 的接口，每个库对应一个集成模块。以下职责均属于 agentty。

| 需求 | agentty 模块 | 基础机制 |
|---|---|---|
| 启动 MCP/ACP 对端并管理其标准输入输出 | [MCP 连接](../src/mcp/connection.cpp)、[ACP 后端](../src/provider/external_acp_backend.cpp)、[共享对端](../src/rpc/peer.cpp) | `maya::platform::process`、轮询反应器 |
| 读取帧并输入引擎 | 同上 | 每个对端一个读取任务（`maya::pool` 隔离任务），把行投递给所有者 |
| 拥有引擎并串行执行调用 | 同上 | `maya::guarded<Engine>` 或单一所有者工作线程 |
| 请求续接与超时 | 同上 | 通过 `maya::delay_for` 等待到 `next_deadline()`，再执行 `expire(now)` |
| HTTP/SSE MCP 传输 | `src/mcp/http_server.cpp` | agentty 的 HTTP 客户端、`util::WorkerGroup` |
| ACP 代理服务，例如接入 Zed | `src/acp/server.cpp` | 标准输入输出读取任务与 `maya::guarded<Sessions>` |
| 运行命令的内置工具 | `src/tool/util/exec.cpp`（`HostServices::exec`） | `maya::platform::process` |
| RAG 索引构建与批量嵌入 | `src/rag/adapter.cpp` | 基于 `maya::scope` 的 `Splitter`；通过 `maya::published` 替换索引 |
| 原先由库管理的缓存和注册表 | 对应的 agentty 所有者模块 | `maya::guarded` / `maya::published` |

工具实现与宿主集成各有归属：[mcp-cpp 工具集](../third_party/mcp-cpp/src/tools/)实现通用工具，agentty 的[注册表](../src/tool/registry.cpp)、[桥接层](../src/tool/mcp_tools_bridge.cpp)和[后端](../src/tool/mcp_tools_backends.cpp)将它们接入应用状态、进程执行、HTTP 与检索服务。agentty 中不存在 `src/tool/tools/` 实现目录。

无需安装运行时：`protocol_runtime.cpp` 已删除。

<a id="7-how-it-is-enforced"></a>

## 7. 约束检查方式

检查加入 agentty 的 `static` ctest 标签，执行 `ctest -L static` 即可验证。三项检查都位于 `tests/lint/protocol_libraries.py`，先去掉注释和字符串字面量，再扫描代码，且**没有允许清单**：

- **`submodule_purity`**：扫描每个库的 `src/` 和 `include/`：
  检查 R1 的原语及其头文件，包括线程、锁、原子变量、`thread_local`、future、`stop_source`、latch 和信号量；R2 的钩子，包括 `set_runtime`、`set_executor`、`Runtime`、`Executor`；R3 的可变 `static`；R4 的时钟读取；R5 的休眠、进程启动、套接字和环境读取；以及 jsonrpc-cpp、mcp-cpp、acp-cpp 中的文件 I/O，包括文件流、`std::filesystem` 查询和修改、目录遍历、`open`、`fsync`。claybin 仅豁免 R5 的进程与环境规则，因为启动受限子进程就是它的职责。三个协议库的文件 I/O 规则没有例外；存储引擎 rag-cpp 不受该项限制。
- **`no_duplication`**：如果 mcp-cpp 或 acp-cpp 重新定义核心导出的名称，
  例如 `Engine`、`Codec`、`RpcError`、`RequestId`、`Newtype`，或三者中任意两个库共享连续 12 行代码，检查就会失败。
- **`jsonrpc_leaf`**：jsonrpc-cpp 只能包含清单中的标准库头文件、
  nlohmann 和自身头文件。agentty 中只有 §6 的 rpc/mcp/acp 模块可以包含 `<jsonrpc/...>`。
- **`layering` / `layering_maya` / `layering_jaal`**：规则不变，agentty
  不直接引用 jaal；maya 不引用 agentty；jaal 不引用前两者。
- jsonrpc-cpp 和 acp-cpp 的单元测试以**单线程方式**构建，不使用
  `-pthread` 或 `Threads::Threads`，因此依赖线程的库无法通过链接。mcp-cpp 和 rag-cpp 的测试会链接线程库，但只用于扮演宿主，例如并行划分器或管道读取器；被测库代码仍由 `submodule_purity` 检查。

新增检查必须与使该检查通过的改动一起提交。

<a id="8-migration-plan-and-order"></a>

## 8. 迁移计划与顺序

每一步都必须保证源码能够编译、agentty 程序能够端到端运行，并且 `static` 检查通过。各库分别提交改动，agentty 在每一步完成后更新子模块指针。

1. **jsonrpc-cpp 核心。** *（已完成）* 从
   mcp-cpp/acp-cpp 提取共享头文件到 `1ay1/jsonrpc-cpp` 仓库，按 §4.3/§4.5 重写：引擎是状态机，无 future、无传输层、无运行时。单线程测试覆盖请求与响应、各种 JSON 类型标识、批处理、通过 `expire(now)` 检查截止时间、畸形帧、通知以及未知标识的响应。
2. **acp-cpp 接入核心。** *（已完成）* 删除其运行时、RPC 引擎、标准输入输出
   传输层、协程和连接类；每个方法都在 `protocol.hpp` 中定义为类型。agentty 通过 `rpc::Peer`（`src/rpc/peer.cpp`）驱动双方，在 maya 上运行读取、写入和截止时间任务，引擎由一个 `maya::guarded` 保护。验证包括 `external_acp_backend_test` 的内存内、真实子进程和代理卡死场景，以及 `agentty acp` 标准输入输出驱动测试。
3. **mcp-cpp 接入核心。**
   - *（已完成）* 协议代数和编解码器来自 jsonrpc-cpp；每个方法
     都在 `protocol.hpp` 中定义为类型；`Server` 和 `ClientHandlers` 成为应答表；MRTR 使用 `mrtr_retry()`；调度器接收 `Splitter`。删除 RPC 引擎、传输层、协程、Runtime 钩子、`Client`、`ClientProvider`、`StdioServerProvider` 和 `Guarded`。agentty 的 `mcp::Connection`（`src/mcp/connection.cpp`）充当客户端：在 `Link`（标准输入输出子进程或 HTTP）上使用 `rpc::Peer`，维护缓存列表，并提供带 MRTR 和事件驱动取消的工具调用。`mcp-serve` 在对端上运行 `Server`。已通过 mcp-cpp 的 62 个用例，以及 `mcp_bridge_test`、`mcp_http_test`、`mcp_reload_race_test`、`plugin_disabled_tools_test`、`toolset_e2e_test`、`mcp-serve` 和挂起工具的取消与重连探测。
   - *（已完成）* `cap::Registry` 是单一所有者管理的值，路由在
     查找时计算，不使用锁；textproc/grep/structural 将互不重叠的扫描区间划分到各自槽位，不共享游标、计数器或互斥量。
   - *（已完成）* 工具不自行保留状态。`ToolState` 包含工作区、读取根目录、
     读取缓存、文件快照、进程会话和仓库图，由 agentty 拥有，并通过 `StateAccess` 共享（`mcp_tools_bridge.cpp` 中的 `GuardedToolState`，使用一个 `maya::guarded`）。每次调用的读取器和取消状态由 `cap::Request` 传递，扫描由 `Call::split` 划分。不读取时钟：`process_poll` 等待宿主的 Session，运行时间和耗时由宿主提供。mcp-cpp 的沙箱模块、进度与取消的线程局部变量以及 `ChildProcess` 均已移除，后者迁移到 `agentty/util/child_process.hpp`。验证包括 mcp-cpp 的 62 个用例、process_tools/shell_bound、`toolset_e2e_test`、`mcp_bridge_test`、`mcp_http_test`、`plugin_disabled_tools_test`、`subagent_report_test`、包含文件描述符泄漏用例的 `exec_policy_test` 和 `mcp-serve`。
4. **rag-cpp。** *（已完成）* 并行计算区域接收 `Splitter`；语料库、hnsw
   和 bm25 构建后冻结；缓存、`Extractors` 和 `Registries` 都是调用方拥有的值。网络与进程后端使用宿主的 `HostIo`，rag-cpp 内没有 HTTP 客户端或进程启动器，因此 `builtin()` 只按名称查找它们并报告不可用。agentty 的接入点为 `agentty/rag/rag_host.hpp`。已通过 rag-cpp 的 192 个用例和 C API 冒烟测试，以及 `rag adapter`、`rag shutdown interrupts warm promptly` 和经由 `mcp-serve` 调用的 `search_docs`/`search_code`。
5. **agentty 清理。** *（已完成）* 删除 `protocol_runtime.cpp` 和
   各库的允许清单，并启用 §7 的三项检查。为通过检查，rag-cpp 的 GPU 改为调用方拥有的 `gpu::Device`，不再使用静态上下文和 `RAGCPP_GPU_BACKEND`；RAG 重试不使用等待钩子；mcp-cpp 扫描改用 `Splitter` 值而非 `Executor`；`~` 和 jina 从 `ToolState::home` / `ToolsetConfig::jina` 获取，不读取环境变量；mcp 的 `RequestId` 替换为 `jsonrpc::Id`；claybin 的应用阶段文件描述符放入 `ApplyFds` 值，不再使用文件级全局变量。已通过 rag-cpp 的 192 个用例，包括 RTX 4060 上启用 OpenCL 的测试，以及 mcp-cpp 的 62 个用例、claybin 的 escape/broker/embed/plan/mount/cgroup、`rag adapter`、`toolset_e2e_test`、`shell_cmd_alias_test` 和 `mcp-serve`。
6. **完整验证。** *（已完成）* 普通构建执行 `ctest -j12 -LE perf`，
   1462 项全部通过。完整运行发现并修复了此前定向测试遗漏的五个问题：jaal 的 POSIX 进程启动会把未设置 O_CLOEXEC 的文件描述符泄漏到子进程，现已关闭 stderr 以上的全部描述符；另有四项测试过时，分别是仍使用旧客户端 API 的 acp_integration、未发送 TerminalResized 的 ghost caret、缺少头文件的 maya_host_sequence 和 fork_test。TSan 下，RPC/ACP/MCP/rag/exec/race 子集的 54 项测试通过且没有报告；完整 TSan 套件发现面板测试自身的 setenv 竞态，也已修复。

<a id="9-decision-checklist-for-any-change"></a>

## 9. 修改前的决策检查清单

编写库代码前，请回答以下问题。任何一项回答“是”，都表示代码应放在 agentty 中：

- 是否需要在调用方做其他事情时继续运行？
- 是否需要等待时间、响应、管道或锁？
- 是否需要保证两个线程同时调用时的安全性？
- 是否读取时钟、读取环境或启动其他进程？
- 是否在调用之间保留状态，且状态不在调用方拥有的
  对象中？
- 相同代码是否已经存在于另一个库？

对于驱动库的 agentty 代码，还应检查：

- 是否位于该库对应的唯一集成模块中（§6）？
- 所有线程、等待、锁和进程操作是否只使用 maya 的接口？
- 引擎是否恰好由一个所有者访问？

<a id="10-glossary"></a>

## 10. 术语表

- **被动库（Passive library）**：只在调用方线程上、当前调用内部完成
  工作并返回的代码；没有后台任务、等待或共享可变状态。
- **引擎（Engine）**：JSON-RPC 连接的状态机，维护待处理请求、
  标识和截止时间；输入字节，返回帧与完成结果。
- **所有者（Owner）**：唯一允许调用引擎的位置，可以是受保护的值或一个
  工作线程，由 agentty 决定。
- **结果（Outcome）**：库调用以返回值代替 I/O，包含待写入的帧、
  完成结果、入站请求和取消信息。
- **划分器（Splitter）**：调用方提供的函数，在 `[0, n)` 上执行 `fn(i)`，
  可以并行执行。这是库唯一允许接受的并行形式。
- **核心（Core）**：jsonrpc-cpp，即 JSON-RPC 2.0 和编解码类型体系的
  唯一共享实现。
