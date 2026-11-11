# third_party/

第三方依赖。本目录中的依赖均为 Git 子模块，由 agentty 使用，但不由 agentty 维护。仓库顶层的 `src/`、`include/`、`tests/`、`cmake/`、`docs/` 属于 agentty 自身，其他依赖集中放在这里。

| 目录 | 目标 | 职责 |
|----------------|----------------------------|-----------------------------------------------|
| `maya/` | `maya::maya`, `maya::app` | TUI 框架：渲染、布局和组件 |
| `mcp-cpp/` | `mcp::mcp`, `mcp::tools` | MCP 协议与完整工具集 |
| `acp-cpp/` | `acp::acp` | Agent Client Protocol，即代理客户端协议（`agentty acp`） |
| `jsonrpc-cpp/` | `jsonrpc::jsonrpc` | mcp 和 acp 共享的仅头文件 JSON-RPC 2.0 实现 |
| `rag-cpp/` | `ragcpp::ragcpp` | 为 `search_docs` / `search_code` 提供检索 |
| `claybin/` | `claybin` | 为代理执行的每条命令提供沙箱 |
| `richpaste/` | `rp` | 为 Ctrl+V 读取剪贴板，包括图像、文件和 SSH 场景 |

`maya` 自己还包含子模块 `maya/third_party/jaal`。jaal 是通用的 Elm 架构运行时内核，因此依赖链为 **agentty → maya → jaal**。

这些依赖都是必需的；子模块缺失或为空会在配置阶段触发 `FATAL_ERROR`。尤其是 mcp-cpp 不能关闭，`AGENTTY_MCP=OFF` 会明确报错，因为没有原生工具回退实现；rag-cpp 也不能被悄悄替换为不执行检索的空实现。claybin 同样不再可选，也没有 `AGENTTY_CLAYBIN` 标志：若设置了沙箱却没有实际引擎执行限制，会比没有沙箱更糟。

claybin 的设计、威胁模型和计划中的 microvm 后端见 `claybin/docs/` 中的 `design.md`、`platforms.md`、`microvm.md`。

<a id="getting-them"></a>

## 获取依赖

```sh
git submodule update --init --recursive
```

<a id="updating-them"></a>

## 更新依赖

构建过程不会更新子模块，因为拉取代码会改变头文件时间戳，导致包含它们的约 64 个编译单元无谓重编译。需要时手动同步：

```sh
cmake --build build --target submodules_sync   # 也可运行 ./resync.sh
```

拉取脚本会拒绝修改存在本地工作的子模块，包括未暂存修改、已暂存但未提交的修改，以及尚未推送到 origin 的本地提交。脚本采用 `merge --ff-only`，不会丢弃已有工作。jsonrpc-cpp 是仅头文件库，不在该同步流程中；请手动执行 `git -C third_party/jsonrpc-cpp pull --ff-only` 更新。

<a id="notes"></a>

## 说明

- 目录位置不会改变包含路径；头文件通过目标的
  包含目录传递，因此仍写作 `#include <maya/widget/composer.hpp>`，无需使用相对此目录的路径。
- 每个子模块都有独立测试套件。集成到 agentty 时，其测试
  选项（`MAYA_BUILD_TESTS`、`CLAYBIN_TESTS`、`RICHPASTE_TESTS` 等）默认关闭。因此，子模块改动可能在 agentty 中通过，却无法通过自身 CI。要正确验证，应在该子模块目录中独立构建，例如在 `third_party/maya/` 中运行 `cmake -B build -DMAYA_BUILD_TESTS=ON`，或在 `third_party/claybin/` 中运行 `cmake -B build`，与其 CI 方式一致。agentty 自身的沙箱端到端检查为 `cmake --build build --target sandbox_live_check && ./build/sandbox_live_check`。
- `add_subdirectory` 会在构建树中保留对应目录结构，因此产物
  位于 `build/third_party/<name>/`，例如 `build/third_party/mcp-cpp/examples/date-mcp/date_server`。
