#include <maya/runtime.hpp>
#include "agentty/workspace/symbols.hpp"
#include "agentty/workspace/files.hpp"   // deprioritize_prewarm_thread

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "agentty/tool/util/fs_helpers.hpp"

namespace agentty {

namespace {
namespace fs = std::filesystem;

std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

// 我们扫描的文件扩展名。 镜像 find_definition.cpp
// 的设定; 如果添加语言扩展名, 请在此镜像。
bool is_source_ext(std::string_view ext) noexcept {
    static const std::string_view kExts[] = {
        ".cpp", ".hpp", ".c", ".h", ".cc", ".hh", ".cxx", ".hxx",
        ".py", ".js", ".ts", ".jsx", ".tsx",
        ".go", ".rs", ".java", ".kt", ".rb", ".swift", ".zig", ".lua",
    };
    for (auto e : kExts) if (ext == e) return true;
    return false;
}

// 符号 每行提取 : symbol_on_line (symbol_match.cpp ),
// 一只手(手),手(手,手),(手) (手),一只双手(手) -written 匹配器,
// 相当于旧的 std::regex 设定时没有使扫描成本为秒的后跟踪引擎 CPU 百分比 工作区.

bool scan_file(const fs::path& root, const fs::path& file,
               std::vector<SymbolEntry>& out, std::size_t cap) {
    std::error_code ec;
    auto sz = fs::file_size(file, ec);
    if (ec) return true;
    // 跳过病理大文件( 最小捆绑, 生成) 头文件).
    // 512 KiB 匹配 find_definition 盖子
    if (sz > 512 * 1024) return true;
    std::ifstream in(file);
    if (!in) return true;

    std::error_code rec;
    auto rel = fs::relative(file, root, rec);
    std::string path_str = rec ? file.string() : rel.string();

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const std::string_view name = symbol_on_line(line);
        if (name.empty()) continue;
        SymbolEntry e;
        e.name = std::string{name};
        e.path = path_str;
        e.line_number = lineno;
        out.push_back(std::move(e));
        if (out.size() >= cap) return false;
    }
    return true;
}

} // 命名空间结束。

// ── 缓存 + 同步预热( 镜像) files.cpp) ─────────────────────
// 电话 符号 扫描可读取最多5000个源文件的每行。
// 它使用了 std::regex 每行,每行成本为 CPU 百分比 工作区
// 并冻结 UI 当运行在 `#` 键盘中,所以它移动到一个背景 池 启动时,
// 匹配者是手 -written (symbol_match.cpp )和扫描
// 大多是 I/O.
//
// 不过 "background" 仅是 THREAD ,而不是从 COST。
// 该词 池 已计时( T) hardware_concurrency, 12) 工作线程
// 把他们赶走,然后发射 agentty 真实的 仓库 ~6s的每个核钉:测量到的1129
// % CPU 2864年的这段 -file 源码树 ,而其中98% -file 目录。
// 电脑上是扇形的 手机在用户出现前被烧了 带类型的
// 任何(问题38;36)都是来自另一端的同一种投诉——
// 高 -latency 发射爆发最容易发生的故障)。
//
// 工作本身值得做;而错误在于它 PRIORITY。这是猜测 缓
// 存 代表 选择面板 用户可能永远不会打开,所以它必须失去每
// 一个 竞态 反对实际工作:
//
//   1. 更少的工作线程. 半个核,封顶为4——扫描速度快~4x.
//      而不是序列,最后的8线程在使机器无法使用
//      的同时购买了一小段秒.
//   2. 排程器优先度最低。 SCHED_IDLE (Linux / 不错的+19 其它处
//      表示扫描只运行在核心上,别无他愿,
//      因此爆裂会变得对前缘看不见,而不是与之竞争.
//   3. 将文件之间的比例调值,因此即使具有同等优先性,工作线程是
//      无法中断 而不是独占一个核心 来量子。
namespace {
// 发表 符号 索引。 AtomicSnapshot 与文件列表相同的原因
// files.cpp: a `mutex + shared_ptr` 双人
// 需要 锁 自动调试 LOAD 也 (复制控点) 竞态 店里的人一半都掉下来
util::AtomicSnapshot<std::vector<SymbolEntry>> g_symbols;
// 单身 -flight 锁锁 符号 扫描旁边的生活
// prewarm_workspace_symbols 下面,旁边是唯一读它代码。

// `fan` 打电话者的证据是没有 锁 ,需要打开平行
// maya::scope。没有一位的呼叫者(冷同步) 选择面板
// 取) 通行证 nullptr 扫描显示为单股 线程.
std::vector<SymbolEntry> build_symbol_list(const maya::unlocked* fan, std::size_t cap,
                                           std::stop_token stop = {}) {
    std::vector<SymbolEntry> out;
    const auto root = tools::util::project_root();
    if (root.empty()) return out;

    // 1. 收集候选源文件(廉价目录行走).
    std::vector<fs::path> src;
    {
        std::error_code ec;
        constexpr std::size_t kFileCap = 5000;
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && src.size() < kFileCap;
             ) {
            if (stop.stop_requested()) return {};   // 提前保释 关闭
            if (ec) {
                ec.clear();
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            const auto& entry = *it;
            auto fn = entry.path().filename().string();
            std::error_code qec;
            const bool is_dir = entry.is_directory(qec);
            if (is_dir && tools::util::should_skip_dir(fn)) {
                it.disable_recursion_pending();
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            if (is_dir && it.depth() > 0 && fn.starts_with(".")) {
                it.disable_recursion_pending();
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            if (is_dir) {
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            if (!entry.is_regular_file(qec)) {
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            if (!is_source_ext(entry.path().extension().string())) {
                std::error_code step_ec;
                it.increment(step_ec);
                if (step_ec) break;
                continue;
            }
            src.push_back(entry.path());
            std::error_code step_ec;
            it.increment(step_ec);
            if (step_ec) break;
        }
    }
    if (src.empty()) return out;

    // 2. 平行扫描文件——每工作线程建立局部矢量,然后
    //    我们合并。
    //
    //    HALF 芯片,封顶4,这以前是分钟(核,12),在任何现代笔记本
    //    电脑上都意味着 "all of them" : 测量值为1129%
    //    CPU 用于 ~6 s 的发射。扫描为 unit 线性 线程 反正(
    //    这是) I/O 加号 分配器-bound 和我们一样 CPU ),
    //    所以最后的8个 工作线程 买下一分之一秒 并让用户的机器损失了
    //    半个核心自由 也意味着 UI 线程 ,提供方拨号,
    //    RAG 温暖 -up 永远不要排队。
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned nthreads = std::max(1u, hw / 2);
    nthreads = std::min<unsigned>(nthreads, 4);
    nthreads = std::min<unsigned>(nthreads, (unsigned)src.size());

    std::atomic<std::size_t> next{0};
    std::vector<std::vector<SymbolEntry>> partials(nthreads);
    auto share = [&](unsigned t) {
                // 最低优先级:这是投机 缓存 代表 选择面板
                // 可能永远无法打开 所以它只能使用核心 不需要
                // 其他东西
                if (fan) deprioritize_prewarm_thread();
                auto& local = partials[t];
                for (;;) {
                    if (stop.stop_requested()) return;   // 关闭时的排水量
                    std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= src.size()) return;
                    if (local.size() >= cap) return;
                    try { (void)scan_file(root, src[i], local, cap); }
                    catch (...) { /* 跳过一个会移动匹配者的文件 */ }
                    // 给排程员一个 接口边界 将文件之间连接起来。
                    // SCHED_IDLE 被拒绝(一个受限制的集
                    // 装箱):没有这种集装箱, 工作线程 能够保持一个
                    // 核心 整个量子中 -burst.
                    std::this_thread::yield();
                }
    };
    if (fan) {
        // parallel_for 返回前加入每股,
        // 所以 `share` 和它所达到的,均超过各种用途。
        // 每一份都只写自己的部分。
        maya::parallel_for(*fan, nthreads, nthreads, [&share](std::size_t t) {
            share(static_cast<unsigned>(t));
        });
    } else {
        for (unsigned t = 0; t < nthreads; ++t) share(t);
    }
    for (auto& p : partials) {
        for (auto& e : p) {
            out.push_back(std::move(e));
            if (out.size() >= cap) break;
        }
        if (out.size() >= cap) break;
    }
    std::sort(out.begin(), out.end(),
              [](const SymbolEntry& a, const SymbolEntry& b){ return a.name < b.name; });
    return out;
}
} // 命名空间结束。

namespace {
// 单身 -flight 锁锁. 不 a 线程 ——本单元不拥有一个单元.
//
// WHO 扫描是打电话者的事: init.cpp 把它交给 maya's
// Cmd::task_isolated。必须生存的是什么 此处 仅是
// "don't scan twice at once" ,因为这是财产 缓存 正在填补。
std::atomic<bool>& sym_building() {
    static std::atomic<bool> b{false};
    return b;
}
}  // 命名空间结束。

void prewarm_workspace_symbols(const maya::unlocked& lk, std::stop_token stop, std::size_t cap) {
    // SYNCHRONOUS - 看见了 files.cpp 简言之: agentty
    // 有两个机制可以促进 "run this in the background"
    // (maya's Cmd 接口边界 手和手 -rolled std::threads
    // 重复的,让船用光滑的 线程 且无单 -flight 一点都没有
    // 现在有一个机制,它 maya's.
    //
    // 荣誉 `stop` 事项: jaal 关机要等
    // 我们回来
    if (g_symbols.has_value()) return;
    bool expected = false;
    if (!sym_building().compare_exchange_strong(expected, true)) return;
    // 在每个出口上释放,包括扫描后扔出一个(任务执行者吞下它);
    // 卡住的锁链会离开 `#` 打开 "indexing…"
    // 其余时间。
    struct Release { ~Release() { sym_building().store(false); } } release;

    // 目录行走于 池 单人 -threaded
    // 但仍有数千人 stat() 调用
    // ——走和扫描—— 远离前方。
    deprioritize_prewarm_thread();
    auto syms = build_symbol_list(std::addressof(lk), cap, stop);
    if (!stop.stop_requested()) g_symbols.store(std::move(syms));
}

bool symbols_ready() {
    return g_symbols.has_value();
}

// 回返a 快照 而不是一个孔斯特 -ref.
//
// 该 BUG 此 CLOSES : 旧的 正文 拿着 锁 ,
// 复制 shared_ptr 输入块 -scoped 当地,
// 然后返回 `*c` - 所以,唯一保持这一点 缓冲区 活生生的人死在了关上
// 证明书逃出来了 是因为这个 缓存 正在写入 -once 今天;
// 一个无效( Q) 工作区 切换,刷新,更换盖子) 将其变成直播 UAF.
//
// 弟弟进来了 files.cpp 躲过了回来 BY
// 值 安全,但每个条目的深层副本 调用 两个人从两
// 边挑到的 safety/speed 权衡是说
// 权衡 不应该是他们自己做的: 不可变 快照 都安全了,
// 我没事 和 O(1),现在都用它。
util::Snapshot<std::vector<SymbolEntry>> list_workspace_symbols(std::size_t cap) {
    if (auto warm = g_symbols.load(); warm.has_value()) return warm;
    // 冷同步呼叫器: 构建内置+发布。 两个冷同步呼叫机可能同时扫描,
    // 而不是一个等待另一个多功能 -second 走路 - 看,
    // 我看见了 list_workspace_files 同样的交易。
    auto built = util::make_snapshot(build_symbol_list(nullptr, cap));
    g_symbols.store(built);
    return built;
}

std::vector<std::size_t>
filter_symbols(const std::vector<SymbolEntry>& entries, std::string_view query) {
    std::vector<std::size_t> matches;
    matches.reserve(entries.size());
    if (query.empty()) {
        for (std::size_t i = 0; i < entries.size(); ++i) matches.push_back(i);
        return matches;
    }
    auto needle = to_lower(query);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto hay = to_lower(entries[i].name);
        if (hay.find(needle) != std::string::npos) matches.push_back(i);
    }
    return matches;
}

} // namespace agentty
