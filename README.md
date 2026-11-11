# agentty 源码阅读入口

agentty 是一个 C++ 终端编程助手。它把会话状态、模型流式响应、工具执行和终端界面组织成 Elm 风格的应用：事件更新状态并返回副作用描述，运行时执行副作用，再把结果作为新事件送回应用。

这个工作区用于阅读源码和本机开发，保留业务源码、头文件、功能测试、CMake 构建配置、第三方依赖和许可证，以及现有的本机构建与编辑器环境。官网、安装脚本、平台打包、发布脚本和 GitHub 自动化设施已移除。

## 目录与依赖

| 路径 | 职责 |
| --- | --- |
| [`src/`](src/) | 主项目实现，按领域划分；程序入口为 [`src/runtime/main.cpp`](src/runtime/main.cpp)。 |
| [`src/agentty_pch.hpp`](src/agentty_pch.hpp) | 可选预编译头的稳定 STL 前缀，由 `AGENTTY_PCH=ON` 启用，默认关闭。 |
| [`include/agentty/`](include/agentty/) | 领域类型、应用状态、消息、副作用、接口和内联逻辑，与 `src/` 大致对应。 |
| [`src/runtime/`](src/runtime/) | 启动、应用事件处理、命令构造、订阅、面板与视图。 |
| [`src/domain/`](src/domain/) / [`include/agentty/domain/`](include/agentty/domain/) | 会话、消息内容、模型目录、权限配置等领域数据。 |
| [`src/provider/`](src/provider/) | 模型选择、请求路由、认证、提示词和各提供方的流式传输。 |
| [`src/tool/`](src/tool/) | 工具注册、调度边界、权限、沙箱和宿主适配；通用工具实现在 mcp-cpp 中。 |
| [`src/io/`](src/io/) / [`src/dirs/`](src/dirs/) / [`src/config/`](src/config/) | HTTP、持久化、剪贴板、数据目录和配置加载。 |
| [`src/rpc/`](src/rpc/) / [`src/mcp/`](src/mcp/) / [`src/acp/`](src/acp/) | JSON-RPC 连接驱动与 MCP、ACP 的宿主集成。 |
| [`src/rag/`](src/rag/) / [`src/workspace/`](src/workspace/) | 检索适配、工作区索引及其生命周期。 |
| [`tests/`](tests/) | C++ 功能测试、静态约束、命令行与协议测试、测试数据和辅助服务。 |
| [`cmake/`](cmake/) / [`CMakeLists.txt`](CMakeLists.txt) | 源文件集合、工具链、依赖、测试注册及本地优化选项。 |
| [`scripts/`](scripts/) / [`resync.sh`](resync.sh) | ACP 测试、剪贴板诊断、模拟嵌入服务、PGO 训练及子模块同步入口。 |
| [`third_party/`](third_party/) / [`.gitmodules`](.gitmodules) | 固定版本的第三方子模块；各子模块内部的文档、示例、测试和平台实现完整保留。 |
| [`docs/`](docs/) | 五份核心设计与架构文档，见下文。 |
| `.vscode/` / `build/` / `.cache/` | 当前机器的开发配置、构建结果、编译数据库和 clangd 缓存；本地内容不属于可移植的依赖安装方案。 |

应用通过 maya 使用终端界面和运行时能力；maya 再通过其内部的 jaal 提供事件循环、副作用、任务、取消与进程管理，依赖方向是 **agentty → maya → jaal**。主项目通过 `maya::` API 使用这些能力。

其他依赖由宿主驱动：jsonrpc-cpp 提供共享的 JSON-RPC 核心，mcp-cpp 提供 MCP 协议与通用工具集，acp-cpp 提供 ACP 协议，rag-cpp 提供检索引擎，claybin 提供沙箱能力，richpaste 提供富文本粘贴支持。具体构建开关及 nlohmann_json、simdjson、doctest、mimalloc、OpenSSL、nghttp2 等依赖的处理见 [`cmake/AgenttySubmodules.cmake`](cmake/AgenttySubmodules.cmake)。

## 建议阅读顺序

按“程序入口 → 状态与消息 → 事件处理 → 模型请求与工具调用 → 渲染”串起一次交互，再按需要深入各领域。

1. **程序入口**：从 [`src/runtime/main.cpp`](src/runtime/main.cpp) 的 `main()` 进入，沿启动流程阅读参数、配置、认证、提供方与 `FsStore` 的构造，以及 `maya::run<app::AgenttyApp, app::Host>`。随后看 [`program.hpp`](include/agentty/runtime/app/program.hpp)、[`host.hpp`](include/agentty/runtime/app/host.hpp) 和 [`deps.hpp`](include/agentty/runtime/app/deps.hpp)，理解应用钩子、副作用宿主与依赖注入的关系。

2. **状态与消息**：阅读 [`model.hpp`](include/agentty/runtime/model.hpp)、[`domain/conversation.hpp`](include/agentty/domain/conversation.hpp)、[`domain/session.hpp`](include/agentty/domain/session.hpp)、[`msg.hpp`](include/agentty/runtime/msg.hpp) 和 [`cmd.hpp`](include/agentty/runtime/cmd.hpp)。`Model` 汇集状态，`Msg` 是分域的事件变体，`Cmd` 描述需要宿主执行的副作用。

3. **事件处理**：运行时沿消息树调用 `AgenttyApp::update(Model&, DomainMsg, now)`，再由 [`update.hpp`](include/agentty/runtime/app/update.hpp) 声明、[`update.cpp`](src/runtime/app/update.cpp) 实现的分域重载转发到 [`src/runtime/app/update/`](src/runtime/app/update/) 的处理函数；`program.hpp` 负责写入本次事件的时间，并调用 `publish_derived`。先看 `composer.cpp`、`stream.cpp` 和 `tool.cpp`，再看其他领域。`update.cpp` 还提供返回 `(Model, Cmd)` 的整条 `Msg` 入口，供测试驱动事件使用。

4. **模型请求与工具调用**：从 [`cmd_factory.cpp`](src/runtime/app/cmd_factory.cpp) 跟踪请求和工具任务的构造，再看 [`provider/dispatch.cpp`](src/provider/dispatch.cpp) 与各提供方传输实现。工具侧先读 [`registry.cpp`](src/tool/registry.cpp)、[`tool.hpp`](include/agentty/tool/tool.hpp) 中的 `DynamicDispatch`、[`effects.hpp`](include/agentty/tool/effects.hpp) 和 [`policy.hpp`](include/agentty/tool/policy.hpp)，再看 [`mcp_tools_bridge.cpp`](src/tool/mcp_tools_bridge.cpp) 与 [`mcp_tools_backends.cpp`](src/tool/mcp_tools_backends.cpp)。主项目在这里提供状态、执行、HTTP、检索等宿主适配；文件、搜索、进程、Git 等通用工具的实现位于 [`third_party/mcp-cpp/src/tools/`](third_party/mcp-cpp/src/tools/)。沙箱入口在 [`src/tool/util/`](src/tool/util/)。

5. **渲染**：从 [`src/runtime/view/view.cpp`](src/runtime/view/view.cpp) 阅读 `Model → maya::Element` 的组合，再看同目录的会话、输入框、状态栏与 [`panels/`](src/runtime/view/panels/)。`program.hpp` 中的 `visual_hash` 决定是否需要重绘，maya 负责布局和实际绘制。订阅及终端事件转换见 [`subscribe.cpp`](src/runtime/app/subscribe.cpp)。

用测试追踪行为时，可从 [`tests/reducer_effects_test.cpp`](tests/reducer_effects_test.cpp)、[`tests/program_hooks_test.cpp`](tests/program_hooks_test.cpp) 和 [`tests/toolset_e2e_test.cpp`](tests/toolset_e2e_test.cpp) 入手。测试注册表在 [`cmake/AgenttyTests.cmake`](cmake/AgenttyTests.cmake)，目标聚合规则在 [`cmake/AgenttyTestRegistry.cmake`](cmake/AgenttyTestRegistry.cmake)。

## 本机编译与测试

当前工作区使用 `~/.local/agentty-deps/usr` 下的 Clang 22 和开发库，配合 Ninja 构建 C++26。外部终端通过 `scripts/agentty-env.sh` 加载环境。VS Code 的运行任务和快捷键见[本机开发环境](docs/DEVELOPMENT.md)。这里的命令沿用现有 `build/`：

```bash
# 在现有构建目录重新配置 Debug。
bash scripts/agentty-env.sh cmake --preset debug

# 构建主程序和已有的工具链验证目标。
bash scripts/agentty-env.sh cmake --build build \
  --target agentty snapshot_mutex_fallback_test --parallel 4

# 运行静态约束和工具链验证测试。
bash scripts/agentty-env.sh ctest --test-dir build \
  -L static --output-on-failure --parallel 2
bash scripts/agentty-env.sh ctest --test-dir build \
  -R '^snapshot_mutex_fallback_test$' --output-on-failure

# 检查主程序版本。
bash scripts/agentty-env.sh ./build/agentty --version
```

`snapshot_mutex_fallback_test` 验证共享快照的互斥量回退路径，用于本机工具链检查；完整功能测试需要先构建对应目标。要运行更多测试：

```bash
# tests 聚合目标构建项目测试，可能耗时较长。
bash scripts/agentty-env.sh cmake --build build --target tests --parallel 4
bash scripts/agentty-env.sh ctest --test-dir build \
  --output-on-failure --parallel 2
```

`ctest` 只运行测试，不会自动构建测试可执行文件。查看已注册测试可使用 `ctest --test-dir build -N`。`tests_gating` 聚合目标排除性能探针，`sanitizer` 预设供本机内存与未定义行为检查。所有预设默认共用 `build/`，切换预设会重新配置同一个目录。

clangd 从 `build/compile_commands.json` 读取编译选项，使用现有头文件、第三方目录和生成文件。子模块同步入口为 `cmake --build build --target submodules_sync` 或 `./resync.sh`；它会访问上游并可能更新依赖，只在明确需要同步时运行。

在另一台机器上，应先准备满足 [`CMakeLists.txt`](CMakeLists.txt) 和 [`AgenttyToolchain.cmake`](cmake/AgenttyToolchain.cmake) 要求的编译器、CMake、Ninja 与开发库，并初始化 `.gitmodules` 中的子模块。本机 `.vscode/` 的绝对路径和用户目录配置需要相应调整。

## 核心文档与许可证

- [`DEVELOPMENT.md`](docs/DEVELOPMENT.md) 介绍本机开发环境、VS Code 配置和构建运行命令。
- [`ARCHITECTURE.md`](docs/ARCHITECTURE.md)：程序组成、状态、副作用、工具和会话生命周期。
- [`DESIGN.md`](docs/DESIGN.md)：类型、错误、状态机、权限等设计约束及阅读线索。
- [`LAYERING.md`](docs/LAYERING.md)：agentty、maya、jaal 的依赖与并发边界。
- [`PROTOCOL_LIBRARIES.md`](docs/PROTOCOL_LIBRARIES.md)：协议库、工具库及检索库与宿主之间的职责。
- [`SANDBOX.md`](docs/SANDBOX.md)：沙箱配置、平台能力、执行边界和验证方法。

文档用于导航，具体行为以当前源码与测试为准。主项目许可证见 [`LICENSE`](LICENSE)；第三方依赖保留各自许可证。
