#!/usr/bin/env sh
# resync.sh-将每个in-tree 子模块拉到其跟踪分支，然后重建。
#
# 正常构建故意使 DO 不 接触 子模块 (因此未更改的 third_party/maya
# / mcp-cpp / rag-cpp / acp-cpp 永远不会
# 触发包含其 头文件 的 ~64 编译单元 的不必要的重新编译)。
# 当您确实需要最新的 子模块 代码时，运行 此：
#
#   ./resync.sh # 同步所有 子模块 + 增量重建 ./resync.sh
#   -B build-rel # 使用特定的构建目录（默认：构建）
#
# 它是两个 CMake 目标的薄包装：
#   submodules_sync — fast-forwards 每 子模
#   块 (跳过任何本地工作) all — 正常增量构建；仅重新编译其
#                       头文件 实际更改的 编译单元。
set -eu

BUILD_DIR=build
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}

# 使用仓库-tuned ccache配置(内容-hashed，路径-relative)，
# 以便在子模块同步命中缓存之后进行重建，而不是重新编译每个头文件-dependent TU。
# 如果未安装 ccache，则为 No-op。
REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v ccache >/dev/null 2>&1; then
  export CCACHE_CONFIGPATH="$REPO_ROOT/.ccache.conf"
  export CCACHE_BASEDIR="$REPO_ROOT"
fi

while [ $# -gt 0 ]; do
  case "$1" in
    -B) BUILD_DIR=$2; shift 2 ;;
    -j) JOBS=$2;      shift 2 ;;
    -h|--help)
      sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "resync.sh: unknown arg '$1'" >&2; exit 2 ;;
  esac
done

if [ ! -d "$BUILD_DIR" ]; then
  echo "resync.sh: build dir '$BUILD_DIR' not found — configure it first, e.g.:" >&2
  echo "  cmake -S . -B $BUILD_DIR -GNinja -DCMAKE_BUILD_TYPE=Release" >&2
  exit 1
fi

echo ">> syncing submodules (third_party/{maya,mcp-cpp,acp-cpp,rag-cpp,claybin,richpaste}) …"
cmake --build "$BUILD_DIR" --target submodules_sync

echo ">> incremental rebuild (-j$JOBS) …"
cmake --build "$BUILD_DIR" -j"$JOBS"

if command -v ccache >/dev/null 2>&1; then
  echo ">> ccache:"
  ccache -s 2>/dev/null | grep -iE 'hits|misses|hit rate' || true
fi

echo ">> done."
