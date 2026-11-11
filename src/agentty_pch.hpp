// src/agentty_pch.hpp —共享对象库的可选预编译
// -header前缀(AGENTTY_PCH=ON)。
// 将此实现-only 头文件 与源代码一起保存； CMake 编译 一次
// 并在对象 库 中重复使用它。默认情况下，PCH 处于禁用状态，
// 因为它的优势取决于 编译器、计算机和工作负载。
//
// RULES:
//   * ONLY稳定、普遍存在的STL 头文件。没有项目-specific
//     经常发生变化 - PCH 编辑会
//     使每个依赖的 TU 无效，因此这里的 头文件 将使 DESTROY
//     增量 -build 获胜。
//   * 没有 <mimalloc.h>、没有 <mcp/*>、没有 <rag/*>、没有 <nlohmann/*> — 这些是
//     有条件的(编译-gated)且不通用；将它们排除在外，
//     以便无论 标志 功能如何，PCH 都是相同的。
//   * 通过 target_precompile_headers 应用到第一个 objlib，REUSE_FROM'd
//     其余的，因此它被编译一次。
#pragma once

// ── 只有 EXPENSIVE-to-parse ,无所不在 头文件 ─────────────────────────
// 快速的 Many-core 盒子上的大 PCH (21 MB，具有完整的 STL 前缀) LOSES：
// 每-TU加载成本+失去的并行性超过解析节省（测量的）。
// 保持前缀精简——只有广泛包含的模板AND真正慢到解析—
// —
// 所以PCH保持较小并且实际上可以在CI/冷/低-core构建上获胜。
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
