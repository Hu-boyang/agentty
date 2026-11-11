# 分层关系 agentty → maya → jaal

本文规定项目的分层约束。实现与这些约束不一致时，需要修正实现。

## 分层规则

```text
agentty          负责应用编排，拥有程序，决定执行什么、何时执行
   │
   ▼
 maya            向 agentty 提供视图、终端和运行时的统一接口
   │             以 maya 命名空间重新导出 jaal 的运行时能力
   ▼
 jaal            提供事件循环、副作用、任务、订阅、线程、
                 锁、取消、进程和轮询
```

1. **agentty 只通过 maya 使用 jaal。** agentty 的 `src/`、`include/` 和 `tests/`
   通过 `<maya/...>` 与 `maya::...` 使用运行时能力。这些目录中禁止出现
   `#include <jaal/...>`、`jaal::`，也禁止在构建中直接引用
   `third_party/maya/third_party/jaal`。
2. **maya 依赖 jaal，jaal 不依赖上层。** jaal 不包含 maya 的头文件。maya 的核心视图和
   终端代码也不包含 jaal，只有 maya 的运行时接口层可以依赖它，具体为 `maya::app`、
   `<maya/runtime*.hpp>` 和 `<maya/host/...>`。
3. **所有运行时与并发操作都通过 maya → jaal。** 这包括线程、工作池、后台任务、定时器、
   锁、用于协调的原子变量、条件变量、线程局部变量、取消、子进程和 I/O 轮询。
   agentty 通过 maya 的运行时 API 使用这些能力，其他子模块不自行创建线程或获取锁。
4. **agentty 负责调度。** 它决定任务在哪个工作线程上执行、采用什么取消机制以及允许执行多久，
   并向其他库提供所需能力。
5. **其他子模块不持有运行时。** mcp-cpp、acp-cpp、rag-cpp 和 claybin 都是被动库，
   不包含线程、池、定时器、锁、原子变量、`std::async`、future、`detach()`、
   线程局部变量、时钟或进程。独立运行时回退实现以及向外请求执行的 `Runtime` / `Executor`
   钩子也在禁止之列。它们作为状态机，由 agentty 基于 maya → jaal 驱动。
   mcp-cpp 和 acp-cpp 共享一个 JSON-RPC 核心。完整约束、各类需求的替代方案和迁移计划见
   [协议库文档](PROTOCOL_LIBRARIES.md)。

## 库如何获得并发能力

库接收字节、数据值和 `now`，返回帧、完成结果以及截止时间。agentty 拥有围绕它们的读取器、
定时器、锁和进程，每条连接由一个明确的所有者驱动。
[协议库文档](PROTOCOL_LIBRARIES.md)第 4 节介绍读取帧、请求匹配与超时、处理函数、
数据并行、缓存、进程和协程的实现方式，第 6 节说明对应的 agentty 模块。

库也不会向外调用 `Runtime` / `Executor` 接口。数据并行通过调用者传入的 splitter 值实现，
splitter 执行调用者已经拆分好的工作。

## maya 提供的接口

`<maya/runtime.hpp>` 以类型别名重新导出应用需要的 jaal 能力，不会增加运行成本，也不会复制实现。

- **程序与副作用**包括 `maya::Cmd`、`Sub`、`Sink`、`pure_fx`、`Program`、`run`、
  `host_context`、`payload_t` 和 `require_host_for`。
- **并发能力**包括 `maya::guarded`、`scope` / `nursery`、`pool`、`delay_for`、`stop_group`
  和 `loop_bound`。
- **线程安全标记**包括 `Sendable`、`sendable_opt_in`、`frozen_opt_in` 和 `shared`。
- **平台能力**包括进程、轮询反应器、文件锁和时钟。
- **测试宿主**包括 `sim`、`headless` 和 `explore`。

agentty 需要某项尚未重新导出的 jaal 能力时，先在 maya 中增加对应接口。
这份显式清单定义了 maya 的运行时 API，也使依赖边界可以审查。

### 接口边界的作用

这些别名具有 `using maya::X = ::jaal::X` 的含义。
`maya::guarded<T>` 与 `jaal::guarded<T>` 是同一个类型，具有相同的名称修饰结果和错误信息。
因此，agentty 仍然与 jaal 的 API 形状耦合，jaal 函数签名的变化会直接影响 agentty。

这层接口统一固定了运行时版本。agentty 固定 maya 的版本，maya 再固定 jaal 的版本。
审查一个头文件即可知道 agentty 能使用哪些运行时能力。并发禁用清单以及 `unlocked` / `held`
能力类型负责约束原始线程和锁的使用。

## 如何执行这些约束

以下检查都是带 `static` 标签的 ctest 项目，运行前无需编译。

- **layering** 检查 agentty 的 `src/`、`include/` 和 `tests/`。先逐文件移除注释和字符串，
  再拒绝 `<jaal/`、代码中的 `jaal` 引用（包括 `jaal::x`、`jaal :: x`、
  `using namespace jaal;`、`namespace j = jaal;`）以及 `JAAL_` 宏。
  实现位于 `tests/lint/layering.cmake`。
- **spawn_via_jaal** 要求每个子进程都通过 `maya::platform::native_process` 启动。
  它拒绝 fork、exec、popen、system，也扫描 Windows 专用代码中的 CreateProcess、_popen
  和 _spawn。最后两处绕过统一入口的进程启动曾出现在那里。
- **submodule_purity / no_duplication / jsonrpc_leaf** 检查库不持有运行时、共享唯一的
  JSON-RPC 核心，并保持 jsonrpc-cpp 为依赖叶节点。这组检查没有允许清单。
  实现位于 `tests/lint/protocol_libraries.py`，规则见 `PROTOCOL_LIBRARIES.md` 第 7 节。
- **concurrency_banlist_src / _include** 对 agentty 执行相同的并发禁用检查。
  允许清单列出实现安全类型所必需的少量文件，并说明原因。
- **elm_purity** 检查状态更新函数只读取 Model 并返回副作用。
- **hidden_locks** 检查 jaal 无法观察到的锁，包括 `atomic::wait`、`std::call_once`
  和通过 yield 忙等实现的锁。每个例外都在允许清单中解释为何不会持有 jaal 锁等待。
  实现位于 `tests/lint/hidden_locks.py`。
- **maya 自身的 `tests/seam.sh`** 要求只有 `maya/host/` 和 `maya/runtime.hpp`
  可以依赖 jaal，并检查 maya 不自行启动线程。

## agentty 如何驱动各个库

这些库在每次调用时接收所需能力。rag-cpp 从 `agentty/rag/rag_host.hpp` 获得 Splitter、
HostIo 和转换器运行器。mcp-cpp 的[工具实现](../third_party/mcp-cpp/src/tools/)通过 HostServices
获得 splitter、进程执行、HTTP 和状态能力，由 agentty 的
[工具桥接层](../src/tool/mcp_tools_bridge.cpp)和[宿主后端](../src/tool/mcp_tools_backends.cpp)提供。
JSON-RPC 连接运行在 [rpc::Peer](../src/rpc/peer.cpp) 上。

## 退出流程

jaal 的 pool 和 stop_group 采用有界关闭策略，会放弃等待忽略停止令牌的线程，
从而避免卡住的系统调用阻塞程序退出。`maya::strays()` 对这些线程持续计数，直到它们返回。
main() 在清理后检查这个计数，必要时调用 `_Exit`，以免线程仍在读取对象时执行静态析构。

## 当前实现状态

- agentty → maya → jaal 的依赖方向已经严格执行，agentty 不包含 jaal 头文件，也不直接使用其名称。
- maya 不自行创建线程。
- claybin、jsonrpc-cpp、acp-cpp、mcp-cpp 和 rag-cpp 都是被动库，不持有线程、锁、原子变量、
  时钟、进程或全局状态。agentty 基于 rpc::Peer 驱动它们的连接，拥有 mcp-cpp 的工具状态，
  并向 rag-cpp 提供 splitter 和 I/O。
