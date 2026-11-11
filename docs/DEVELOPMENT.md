# 本机开发环境

工具和开发库安装在 `~/.local/agentty-deps/usr`，使用 Clang 22 编译 C++26。
以下配置适用于当前 Ubuntu / WSL 工作区。Bash IDE 的工具路径对应 `/home/hby`。

- `Ctrl+Shift+B`：配置 Debug 预设并构建 `agentty`。
- 状态栏的 ▶ 会构建并运行选中的 CMake 目标。首次运行时选择 `agentty`。
- “终端 → 运行任务 → 运行 agentty”会先构建 `agentty`，然后从工作区根目录启动它。
- “终端 → 运行任务 → CMake: 运行 C++26 验证测试”：构建并运行工具链验证测试。
- CMake 状态栏保留配置预设、构建、运行和测试，其余入口在 CMake 侧栏中使用。
- C++ 补全和诊断由 clangd 读取 `build/compile_commands.json`，C/C++ 扩展仍可用于调试。
- Shell 高亮使用 Better Shell Syntax；Bash IDE 提供补全、ShellCheck 检查和 shfmt 格式化。

新建的集成终端会加载项目环境。在外部终端里，从项目根目录运行以下命令。

```bash
bash scripts/agentty-env.sh cmake --preset debug
bash scripts/agentty-env.sh cmake --build build --target agentty --parallel 4
bash scripts/agentty-env.sh ./build/agentty
```

首次切换编译器或工具链时，使用 `cmake --fresh --preset debug` 重新初始化缓存。
日常配置无需 `--fresh`，以免重复下载 FetchContent 依赖。

推荐扩展已经登记在 [`.vscode/extensions.json`](../.vscode/extensions.json)。新安装扩展后若高亮未刷新，执行
“Developer: Reload Window”；终端环境变化需要新建终端后生效。
