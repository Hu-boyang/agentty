// src/agentty_pch.hpp — optional precompiled-header prefix for the shared
// object libraries (AGENTTY_PCH=ON). Keep this implementation-only header
// alongside the sources; CMake compiles it once and reuses it across the
// object libraries. PCH is disabled by default because its benefit depends
// on the compiler, machine and workload.
//
// RULES:
//   * ONLY stable, ubiquitous STL headers. Nothing project-specific
//     that changes often — a PCH edit
//     invalidates every dependent TU, so churny headers here would DESTROY
//     the incremental-build win.
//   * No <mimalloc.h>, no <mcp/*>, no <rag/*>, no <nlohmann/*> — those are
//     conditional (compile-gated) and not universal; keep them out so the PCH
//     is identical regardless of feature flags.
//   * Applied via target_precompile_headers on the first objlib, REUSE_FROM'd
//     by the rest, so it is compiled exactly once.
#pragma once

// ── Only the EXPENSIVE-to-parse, ubiquitous headers ─────────────────────────
// A big PCH (21 MB with the full STL prefix) LOSES on a fast many-core box:
// the per-TU load cost + lost parallelism exceed the parse savings (measured).
// Keep the prefix lean — just the templates that are both widely included AND
// genuinely slow to parse — so the PCH stays small and can actually win on CI
// / cold / low-core builds.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
