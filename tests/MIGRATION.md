<a id="test-migration-hand-rolled-harness--doctest-single-binary"></a>

# 测试迁移：自定义测试框架 → doctest 单一测试程序

目标：将约 70 个独立测试程序合并为一个 `agentty_tests`。此前每个程序都会静态链接整套共享目标文件，这是 CI 的主要开销；合并后只需链接一次。测试转换为自动注册的 doctest `TEST_CASE`，`doctest_discover_tests()` 仍为每个用例生成独立 ctest 条目，保留 `ctest -j` 并行执行和逐用例报告。

<a id="the-recipe-per-test-file"></a>

## 迁移步骤（逐个测试文件处理）

现有测试使用两种旧框架。`tests/agtest.hpp` 同时兼容二者，因此迁移几乎不改测试主体。使用该头文件替代 `<doctest/doctest.h>`。

<a id="style-1--file-has-define-check"></a>

### 方式一：文件中定义了 `#define CHECK(...)`
1. 删除本地 `#define CHECK(...)`、`#define REQUIRE(...)`，以及
   `static int g_failures` 或 `g_fails` 计数器。
2. 添加 `#include "agtest.hpp"`，放在标准库和第三方头文件之后、
   `agentty/...` 头文件之前。**只有**测试主体不再使用 `printf`/`fprintf` 时，才删除 `#include <cstdio>`；已经删除的尾部计数报告不算在内。
3. 将 `main()` 调用的每个 `static void test_XXX()` 转换为
   `TEST_CASE("readable name") { ...body unchanged... }`，保留测试主体不变，并使用易读的用例名称。
4. 删除 `int main()`。若其中包含不在辅助函数中的内联断言，
   将相应代码块放入单独的 `TEST_CASE`。

<a id="style-2--file-has-a-void-checkbool--helper--counter"></a>

### 方式二：文件中有 `void check(bool, ...)` 辅助函数和计数器
1. 删除本地 `void check(...)` 和计数器，兼容层已经提供
   `check(bool)`、`check(bool, const char*)` 和 `check(bool, std::string)`。
2. 添加 `#include "agtest.hpp"`；如果 `<cstdio>` 不再使用，则删除它。
3. 把旧 `main()` 的整个主体（去掉末尾计数报告和返回语句）
   放入一个 `TEST_CASE("<filename without _test>")`，名称使用去掉 `_test` 的文件名；也可以按独立的 `// ── N. xxx ──` 小节拆分成多个 TEST_CASE。
4. 删除 `int main()`。

然后在 `CMakeLists.txt` 中将测试名称加入 `AGENTTY_MIGRATED_TESTS`。这一清单就是迁移开关：`agentty_add_full_test()` 遇到名称后，会通过全局属性 `AGENTTY_TEST_SOURCES` 将源码加入 `agentty_tests`，不再创建独立程序。普通测试无需修改其他 CMake 配置。

<a id="gotchas-each-cost-a-debug-cycle--check-these"></a>

## 常见问题（以下每项都曾导致额外调试）

- **`CHECK(a && b)`**：doctest 的表达式分解器拒绝逻辑
  运算符，报 `Expression Too Complex`。兼容层已为整个判断表达式增加括号，因此无需手动拆分。
- **本地 `check` 的签名不同**，例如 `check(id, want)` 或
  三参数形式：将所有编译单元合并后，它可能与兼容层的 `check` 冲突或产生歧义。如果它只是转发到 doctest 宏，就删除它；否则重命名为文件专用的名称，例如 `expect_label`、`check_edit`。
- **辅助函数后面的 `set_tests_properties(<name> PROPERTIES TIMEOUT n)`**
  不适用于已迁移的名称，因为独立 ctest 条目已经消失。改用 `agentty_test_timeout(<name> n)`，它会对已迁移的名称不执行操作。
- **聚合清单**：从 `tests` /
  `sanitizer_tests` 的 `add_custom_target(... DEPENDS ...)` 块以及 sanitizer 的 `LABELS` `set_tests_properties` 清单中删除已迁移名称，因为这些位置引用的是已经不存在的独立目标或 ctest 条目。
- **额外链接依赖**：如果原独立测试注册时传入了
  `LIBS acp::acp`，或链接了 `$<TARGET_OBJECTS:agentty_acp_obj>` / `ragcpp::ragcpp`，合并后的程序也需要这些依赖。acp/rag/acp_obj 已接通；其他依赖按需增加 `if(TARGET ...)` 块。
- **删除 `<cstdio>`**：测试主体仍输出诊断信息时应保留。
- **原先独立进程自然具备的测试初始化**，例如冻结
  动画时钟、切换到临时目录，必须在 TEST_CASE 内执行，不能留在原文件作用域，否则共享测试程序可能没有完成初始化。
- **匿名命名空间内的 TEST_CASE 不会注册。** 部分文件
  把辅助函数和旧测试函数都放在 `namespace { ... }` 中。doctest 自动注册依赖外部链接，因此应在辅助函数后、第一个 TEST_CASE 前关闭匿名命名空间（`} // namespace`）。辅助函数仍在同一编译单元中可见。
- **用脚本删除 main() 并不可靠**，尤其当 main() 中有嵌套大括号时。
  例如正则 `int main\(\)\s*\{[^}]*\}` 会在第一个内部 `}` 处停止，留下尾部碎片。应手动删除 `int main(){...}`，或检查最后一个 TEST_CASE 后是否残留 `return`/`printf`。

<a id="keep-standalone-do-not-consolidate"></a>

## 保留独立程序（不要合并）
以下测试依赖进程隔离或单一定义规则（ODR）隔离：
- **跨测试的全局状态污染**：测试若切换到临时目录、设置
  `HOME` 或其他环境变量，或者修改进程全局状态，可能单独通过却破坏共享程序中后续用例，因为后者继承了改变后的工作目录或环境。典型现象是 `--test-case=X` 单独运行通过，但完整程序失败。这类端到端或集成测试，如 toolset_e2e、subagent_report、plugin_disabled_tools，应保留独立程序。每批迁移后都要运行完整 `agentty_tests`，以发现这类问题。
- **fork/exec/posix_spawn** 测试，包括 cred_crypt、concurrency_primitives、
  cross_process_lock、external_acp_backend、fork_test。
- **PTY / openpty** 测试，包括 scrollback_oracle、reveal_scrollback。
- **需要 sanitizer 隔离的测试子集**，要求单一用途、无 ODR 冲突的编译单元。
- **通过参数驱动同一程序的 ctest 条目**，例如
  reveal_stream_gate*，以及为每个测试单独设置 `ENVIRONMENT` 的端到端测试，例如 mcp_bridge_test 的 AGENTTY_MCP_E2E_SERVER 和 mcp_http_test。
- **开发探测程序、基准测试和模糊测试**，例如 `*_probe`、`*_bench`、`*_fuzz`：
  它们主要执行计时循环而不是断言，通常已经标记为 `NO_TEST`。

<a id="verify-each-file-after-converting"></a>

## 每转换一个文件都要验证

检查文件是否包含 `#include "agtest.hpp"`、至少一个 `TEST_CASE`，且没有 `int main`。随后构建并运行 `agentty_tests`，确保用例通过并被 ctest 列出。按每批 3～5 个文件迁移，每批都构建和运行；合并可能暴露只有链接到同一程序后才出现的跨文件冲突。

<a id="submodules"></a>

## 子模块

已完成。每个子模块都采用相同方式：各自提供 tests/agtest.hpp 兼容层、tests/test_main.cpp、单个 `<repo>_tests` 程序和 doctest_discover_tests：
- **acp-cpp**：9 个基于 assert() 的测试合并为 acp_tests，bench_transport 保持独立。
- **mcp-cpp**：17 个测试合并为 mcp_tests；subprocess_tty 和 process_tools 保持独立，
  因为它们会 fork 或启动 sh -c 子进程。
- **maya**：31 个单元测试合并为 maya_tests，共 470 个用例。多 main 文件、
  与 agentty 测试套件共享的 reveal/lag 流式探测程序、基准测试以及仅 POSIX 的 PTY 测试保持独立。注意，test_render_scaling 中 printf 风格的 CHECK(cond, fmt, args...) 需要可变参数兼容层；多个编译单元共享一个程序后，重复的文件级 get_row() 辅助函数需要加上 `static`，避免 ODR 冲突。
- **rag-cpp**：已通过自身的 TEST() 注册表使用单个测试程序，保持现状。
