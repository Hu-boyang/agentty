#include "agentty/workspace/files.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/subprocess.hpp"

#if defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#endif
#if defined(__unix__) || defined(__APPLE__)
#  include <sys/resource.h>
#endif

namespace agentty {

namespace {
namespace fs = std::filesystem;

[[nodiscard]] inline char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

// ── 模糊计分器 ────────────────────────────────────────────────────
//
// 子序列匹配: 每个字符 `needle` 必须出现在 `hay`
// 顺序( 大小写) -insensitive ),
// 但不一定是毗连的。分数奖励与“看”类似,“打字查询”指
// 的是什么—— 底名点击、前缀点击、骆驼跳/路径 -segment
// 返回 INT_MIN 如果 `needle` 不是一个子
// 序列 `hay`.
//
// 热力学( 以 fzf v2 和 VS 代码的 文件匹配为蓝本):
//
//   + 每匹配字符+16 + 连续
//   运行奖金+18 + 每位字符在第一个+ 匹配开始后
//   "word" +30(开始) -of-string 之后, '/',
//                                       '_', '-', '.',
//   ' ' ,或下-上/下/下/上/下)+ 匹配基名内+12(最后路径段,
//   最后 '/' ) + 准确的基名
//   等于查询+200+ 基名开始查询+
//   120+ 完整路径开始查询+60
//   - 干草1中的每个跳过字符(重罚:路径较短)
//                                       相同击出模式胜出)
//   - 深度惩罚 - 2 * (#'/'在干草中)
//
// 所有英寸;无 分配 自动调试 高频路径.
[[nodiscard]] inline std::string_view basename_view(std::string_view p) noexcept {
    auto slash = p.find_last_of("/\\");
    return slash == std::string_view::npos ? p : p.substr(slash + 1);
}

// 返回小写扩展名 无需 线索( Q) "cpp", "md", ""。
// (如果无的话)。 -dot 名称如 "foo.tar.gz" 只返回最后一个段
// ( Q) "gz" )——与人类通常的意思相匹配. "extension".
[[nodiscard]] inline std::string ext_of(std::string_view path) {
    auto base = basename_view(path);
    auto dot  = base.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == base.size())
        return {};
    std::string e;
    e.reserve(base.size() - dot - 1);
    for (std::size_t i = dot + 1; i < base.size(); ++i)
        e.push_back(ascii_lower(base[i]));
    return e;
}

// 人数 -file-class 偏差加到每个分数上 —— 当
// 模糊匹配在其他方面是可比时, 使源代码表面高于
// docs / configs / 资产。 值被刻意放在与匹配
// 奖金( 强烈的文件名击出仍然能击败类偏) 相同的数量顺序上,
// 所以这个动向不会占据主导地位。
//
//   +90 初级源 .ccc.cpp.cxx.hpp.rs.go.py.ts.jsx.jva.kt.swift.rb.php.lua.zig.scala.clj.exs.ml.hl.nim.v
//                            +60 建设/项目. CMakeLists.txt .
//                            c make file .mk .bazel . . 制作文件 .
//   mak .bazel . BUILD WORKSPACE .格勒·斯布特(英语:.
//                            gradle.sbt) Cargo.toml go.mod
//                            package.json pyproject.toml 等 语 文
//   +30 docs / 配置 .md.rst.txt。 JSON .yaml yml .
//                            toml ..
//   ini.conf.xml.html.cs.css.sql.proto
//   − 0 未知/ 通用 − 50
//                            资产/数据.png.jpg.jpeg.gif.webp.svg.pdf.mp3.mp4.mov.wav.ogg.flac.zip.tar.gz.7.z.7.iz.b.bin.iso.
//                            锁 -50 隐藏点文件基
//   名起始 '.' (.bashrc,.gitignore)——权力.
//                            -users 仍然通过打上名字来达到他们;
//                            他们只是停止挤压顶部.
[[nodiscard]] int class_bias(std::string_view path) noexcept {
    auto base = basename_view(path);
    int bias = 0;
    if (!base.empty() && base.front() == '.') bias -= 50;

    // 全文 -filename 特殊设备( 无扩展名或文件名) IS 信号。
    auto eq_ci = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
        return true;
    };
    if (eq_ci(base, "CMakeLists.txt") || eq_ci(base, "Makefile") ||
        eq_ci(base, "GNUmakefile")     || eq_ci(base, "BUILD")    ||
        eq_ci(base, "WORKSPACE")       || eq_ci(base, "Dockerfile") ||
        eq_ci(base, "Cargo.toml")      || eq_ci(base, "go.mod")   ||
        eq_ci(base, "package.json")    || eq_ci(base, "pyproject.toml") ||
        eq_ci(base, "pnpm-lock.yaml")  || eq_ci(base, "yarn.lock") ||
        eq_ci(base, "poetry.lock")     || eq_ci(base, "Gemfile")  ||
        eq_ci(base, "Rakefile"))
        return bias + 60;

    auto e = ext_of(path);
    if (e.empty()) return bias;

    // 大致按真实编码库中的频率排序——早期的返回使常见
    // 的大小写(源文件)深为1strcmp.
    static constexpr std::string_view src[] = {
        "c",  "cc",  "cpp", "cxx", "c++", "h",  "hh", "hpp", "hxx", "h++",
        "rs", "go",  "py",  "ts",  "tsx", "js", "jsx", "mjs", "cjs",
        "java", "kt", "kts", "swift", "m", "mm",
        "rb", "php", "lua", "zig", "scala", "clj", "cljs", "cljc",
        "ex", "exs", "erl", "hrl", "ml", "mli", "hs", "dart",
        "nim", "v",  "sv", "vhd", "sh", "bash", "zsh", "fish", "ps1",
        "r",  "jl", "sol", "tf", "hcl",
    };
    for (auto s : src) if (e == s) return bias + 90;

    static constexpr std::string_view build[] = {
        "cmake", "mk", "make", "bazel", "bzl", "gradle", "sbt",
        "ninja",
    };
    for (auto s : build) if (e == s) return bias + 60;

    static constexpr std::string_view docs_cfg[] = {
        "md", "mdx", "rst", "txt", "adoc", "org",
        "json", "json5", "jsonc", "yaml", "yml", "toml", "ini", "conf",
        "cfg", "properties", "env", "xml", "html", "htm", "css", "scss",
        "sass", "less", "sql", "proto", "graphql", "gql",
    };
    for (auto s : docs_cfg) if (e == s) return bias + 30;

    static constexpr std::string_view assets[] = {
        "png", "jpg", "jpeg", "gif", "webp", "svg", "ico", "bmp", "tiff",
        "pdf", "psd", "ai",
        "mp3", "mp4", "mov", "avi", "mkv", "webm", "wav", "ogg", "flac",
        "m4a", "aac", "opus",
        "zip", "tar", "gz", "tgz", "xz", "bz2", "7z", "rar",
        "bin", "iso", "img", "dmg", "exe", "dll", "so", "dylib", "a",
        "o", "obj", "class", "jar", "war", "pyc", "pyo", "wasm",
        "lock", "sum", "midi", "mid",
    };
    for (auto s : assets) if (e == s) return bias - 50;

    return bias;
}

[[nodiscard]] inline bool is_word_boundary(std::string_view hay,
                                           std::size_t i) noexcept {
    if (i == 0) return true;
    char prev = hay[i - 1];
    char cur  = hay[i];
    if (prev == '/' || prev == '\\' || prev == '_' || prev == '-' ||
        prev == '.' || prev == ' ')
        return true;
    // 驼峰: 下 → 上 过渡
    if (prev >= 'a' && prev <= 'z' && cur >= 'A' && cur <= 'Z') return true;
    return false;
}

struct Scored {
    int         score;
    std::size_t idx;
};

// 回返情况 INT_MIN 如果 `needle` 不是一个子序列 `hay`.
// `hay` 财务报告和已审计财务报表 `needle_lower` 均为预期的小写
// ASCII; `hay_orig` 原为 -case 用于驼峰检测的路径。
[[nodiscard]] int fuzzy_score(std::string_view hay_orig,
                              std::string_view needle_lower) noexcept {
    if (needle_lower.empty()) return 0;
    if (needle_lower.size() > hay_orig.size()) return INT32_MIN;

    const auto base = basename_view(hay_orig);
    const std::size_t base_off = static_cast<std::size_t>(
        base.data() - hay_orig.data());

    // Fast-path奖金(与小写基名/全路径相配).
    int bonus = 0;
    {
        // 比较碱基名大小写-insensitively与针.
        bool base_eq = base.size() == needle_lower.size();
        bool base_pref = base.size() >= needle_lower.size();
        bool path_pref = hay_orig.size() >= needle_lower.size();
        for (std::size_t k = 0; k < needle_lower.size(); ++k) {
            char nb = needle_lower[k];
            if (base_eq   && ascii_lower(base[k])     != nb) base_eq   = false;
            if (base_pref && ascii_lower(base[k])     != nb) base_pref = false;
            if (path_pref && ascii_lower(hay_orig[k]) != nb) path_pref = false;
            if (!base_eq && !base_pref && !path_pref) break;
        }
        if (base_eq)        bonus += 200;
        else if (base_pref) bonus += 120;
        if (path_pref)      bonus += 60;
    }

    // 贪婪左移-to-right子序列行走,
    // 贪婪一般情况下是次优化的(最佳对齐是O(n*
    // m)DP),但对于典型长度的路径串,区别是隐形的,
    // 贪婪版本是~50×更快的——很容易在每
    // 条键盘上运行,对准数千条路径.
    int score = 0;
    std::size_t hi = 0;  // 索引为干草
    std::size_t ni = 0;  // 指数化为针
    bool prev_matched = false;
    int  skipped = 0;

    while (ni < needle_lower.size() && hi < hay_orig.size()) {
        char nc = needle_lower[ni];
        char hc = ascii_lower(hay_orig[hi]);
        if (hc == nc) {
            score += 16;
            if (prev_matched)            score += 18;
            if (is_word_boundary(hay_orig, hi)) score += 30;
            if (hi >= base_off)          score += 12;
            prev_matched = true;
            ++ni;
            ++hi;
        } else {
            prev_matched = false;
            ++skipped;
            ++hi;
        }
    }
    if (ni < needle_lower.size()) return INT32_MIN;  // 不是子序列

    score -= skipped;  // 轻度处罚 "chars we walked past"

    // 深度惩罚——给予两个同样好的匹配,更倾向于更浅的路径(
    // `src/foo.cpp` 结束 `build-x/.../foo.cpp`).
    int slashes = 0;
    for (char c : hay_orig) if (c == '/' || c == '\\') ++slashes;
    score -= 2 * slashes;

    // 文件-class偏差 — 源代码比 Docs 胜资产。
    // 上表参见 class_bias( ) ; 数值被调制
    // 成绑定的粘合符, 而不会压倒一个明显更好的文件名击打。
    score += class_bias(hay_orig);

    return score + bonus;
}

} // 命名空间结束。

// ── 缓存 + 预热 ───────────────────────────────────
// 文件列表已构建 一次 每个进程。历史上,构建在第一个进程上同步运行。
// `@` 键盘键—— a recursive_directory_iterator
// 千余条道路阻挡着 UI 线程 整个散步。
//   • prewarm_workspace_files ()踢行走在脱落上 线程 时间
//     启动,所以,当用户到达`@`时,列表就绪;
//   • 其结果公布在根据互斥量互换的shared_ptr之后,
//     所以读者从不屏蔽 也从不看到半-built矢量;
//   • files_ready ( ) 是非 的。 -blocking 探测作曲家用来打开
//     选择面板 INSTANTLY (空 + ) "indexing…" 当暖气还没有降落的时候
namespace {
// 已公布的文件列表。
//
// AtomicSnapshot ,没有 `mutex + shared_ptr`
// 手执 -rolled 需要表格( E) 锁 自动调试 LOAD
// 以及商店——复制 shared_ptr 手动键与另一个手动
// 键 线程 指定为数据 竞态 指针本身,即使指针不是可变 那种微妙的,
// 就是读者的风格 "simplifies" 将它放入类型中
// 可以消除问题:没有暴露的手柄可以打开 竞态 5号线
// lock/unlock 这个地方过去需要的已经不见了。
util::AtomicSnapshot<std::vector<std::string>> g_files;
// 单身 -flight 档案的锁链就住在旁边 prewarm_workspace_files 下面,
// 旁边是唯一读它代码。

// 频率:用户参考的路径,大多数为-recent-first-weighted。
// 微小的复健列表(不是完全的节俭衰减)——最后的.
// -referenced 文档就是跟随 -up `@` 想要在顶端。
using Frecency = std::vector<std::string>;
// A 快照 : 在每个引用上替换, 在每个过滤器通过时读一次。
// no 锁 ,所以,请 选择面板 并且注明参考文献的工具不需要能力。
maya::snapshot<Frecency>& frecency() { static maya::snapshot<Frecency> v; return v; }

// ── Git信号 ──────────────────────────────────────────
// 唯一最强的 "which file matters" 信号是 git:
// 您修改/ 已设置/ 刚刚触碰的文件 ARE 工作场景 FFF 和Zed
// 都领导着他们 选择面板。我们计算过一次预温状态图( 状态)
// --porcelain + 近期承诺触摸历史)和 归并 将其排入排名+行标记。
// 路径( Q) 工作区-relative) -> GitTag.
using GitTags = std::unordered_map<std::string, GitTag>;
// 于预热时建成一次, 在每个过滤器上读取: a 快照 ,没有 锁.
maya::snapshot<GitTags>& git_status_map() { static maya::snapshot<GitTags> m; return m; }

void build_git_signals() {
    const auto root = tools::util::project_root();
    if (root.empty()) return;
    std::unordered_map<std::string, GitTag> tags;

    // 1. 工作 -tree 状态。 NEWLINE-separated (不 -z : 子进程
    //    洗涤器吃东西 NUL 字节), quotePath 离离离离的Unicode路径生存。
    //    瓷v1XY栏: ⁇ index,Y=worktree.
    auto st = tools::util::run_argv_s(
        {"git", "-C", root.string(), "-c", "core.quotePath=false",
         "status", "--porcelain", "--untracked-files=all"},
        512 * 1024, std::chrono::seconds{8});
    if (st.started && st.exit_code == 0) {
        std::istringstream in(st.output);
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 4) continue;
            char x = line[0], y = line[1];
            std::string path = line.substr(3);
            // 重命名格式"old -> new":保留新路径.
            if (auto arrow = path.find(" -> "); arrow != std::string::npos)
                path = path.substr(arrow + 4);
            GitTag tag = GitTag::None;
            if (x == '?' && y == '?')            tag = GitTag::Untracked;
            else if (x != ' ' && x != '?')       tag = GitTag::Staged;   // 指数变化
            else if (y != ' ')                   tag = GitTag::Modified; // 工作树变化
            if (tag != GitTag::None) tags[path] = tag;
        }
    }

    // 2. 最近-commit 触摸历史: 上~20 年的文件更改
    //    即使现在干净, 也成为当前工作的一部分。
    //    疲软比活的肮脏状态要低, 只有标记文件还没有标记。
    auto touched = tools::util::run_argv_s(
        {"git", "-C", root.string(), "-c", "core.quotePath=false",
         "log", "--name-only", "--pretty=format:", "-n", "20"},
        1024 * 1024, std::chrono::seconds{8});
    if (touched.started && touched.exit_code == 0) {
        std::istringstream in(touched.output);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            tags.emplace(line, GitTag::RecentlyCommitted);   // 嵌入 = 无覆盖
        }
    }

    git_status_map().store(std::move(tags));
}

std::vector<std::string> build_file_list(std::size_t cap, std::stop_token stop = {}) {
    std::vector<std::string> out;
    out.reserve(std::min<std::size_t>(cap, 1024));
    const auto root = tools::util::project_root();
    if (root.empty()) return out;
    std::error_code ec;
    std::size_t tick = 0;
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && out.size() < cap;
         it.increment(ec)) {
        // 关闭时立即保释 : jaal 等待着这个任务返回,
        // 所以一个无法取消的完整 -tree 扫描会拖住 ^C 在一个大的 仓库.
        if ((++tick & 63) == 0 && stop.stop_requested()) return out;
        if (ec) { ec.clear(); continue; }
        const auto& entry = *it;
        auto fn = entry.path().filename().string();
        const bool is_dir = entry.is_directory(ec);
        if (is_dir && tools::util::should_skip_dir(fn)) {
            it.disable_recursion_pending();
            continue;
        }
        if (is_dir && it.depth() > 0 && fn.starts_with(".")) {
            it.disable_recursion_pending();
            continue;
        }
        if (is_dir) continue;
        if (!entry.is_regular_file(ec)) continue;
        std::error_code rec;
        auto rel = fs::relative(entry.path(), root, rec);
        out.push_back(rec ? entry.path().string() : rel.string());
    }
    std::sort(out.begin(), out.end());
    return out;
}
} // 命名空间结束。

namespace {
// 单身 -flight 锁锁. 不 a 线程 ——本单元不再拥有一个.
//
// 谁是工作负责人 maya 生意(电话员把这个递过来) 正文 改为
// Cmd::task_isolated ) 唯一必须活下去的东西 此处 谓
// "don't do it twice at once" ,因为这是财产 缓存
// 被填充,而不是排在排满它。
std::atomic<bool>& files_building() {
    static std::atomic<bool> b{false};
    return b;
}
}  // 命名空间结束。

void prewarm_workspace_files(std::stop_token stop, std::size_t cap) {
    // SYNCHRONOUS 电话员决定这个过程 init.cpp 把它交给
    // maya's Cmd::task_isolated ,
    // 拥有 线程 并吞下例外,在关闭时 jaal 停止( E) `stop`
    // 等待我们回来 这很重要: 步行触摸过程 -wide 缓存.
    if (g_files.has_value()) return;   // 已经很温暖了
    bool expected = false;
    if (!files_building().compare_exchange_strong(expected, true)) return;
    // 在每个出口上释放,包括从步行中扔出一个投球(任务执行者
    // 吞下它);一个卡住的锁会离开 `@` 打开
    // "indexing…" 其余时间。
    struct Release { ~Release() { files_building().store(false); } } release;

    // 投机: `@` 选择面板 可能永远不打开,所以这必须失去每一个 竞态 实际工作(见《大会正式记录,
    // 第五十七届会议,补编第6号》(A/C.4/56/6)) 头文件).
    deprioritize_prewarm_thread();
    // Git 信号( 快速, ~ 2 git 引用) 所以
    // 文件列表已经是 git 了 -aware 即刻出版,
    // `@` 让你的脏文件从第一个打开。
    build_git_signals();
    auto files = build_file_list(cap, stop);
    if (!stop.stop_requested()) g_files.store(std::move(files));
}

void deprioritize_prewarm_thread() noexcept {
#if defined(__linux__)
    // SCHED_IDLE ,不好看(19):一个好人 线程
    // 仍然在争得时间的分寸 SCHED_IDLE 只有在正常情况下,
    // 运行时才会运行。 这正是一个投机性预温所想要的合同 — —
    // 使用闲置的机器, 而不是繁忙的机器。
    sched_param p{};
    p.sched_priority = 0;
    if (::pthread_setschedparam(::pthread_self(), SCHED_IDLE, &p) == 0) return;
    // 一些集装箱拒绝 sched_setscheduler ; 掉进 nice().
#endif
#if defined(__unix__) || defined(__APPLE__)
    (void)::setpriority(PRIO_PROCESS, 0, 19);
#endif
}

bool files_ready() {
    return g_files.has_value();
}

void note_file_referenced(std::string_view path) {
    if (path.empty()) return;
    (void)frecency().update([](const Frecency& old, const std::string& p) {
        Frecency v = old;
        std::erase(v, p);                     // 脱尘器——移到前面
        v.insert(v.begin(), p);
        if (v.size() > 64) v.resize(64);      // 限值复用窗口
        return v;
    }, std::string{path});
}

GitTag file_git_tag(std::string_view path) {
    const auto m = git_status_map().load();
    auto it = m->find(std::string{path});
    return it == m->end() ? GitTag::None : it->second;
}

std::string_view git_tag_label(GitTag tag) {
    switch (tag) {
        case GitTag::Modified:          return "modified";
        case GitTag::Staged:            return "staged";
        case GitTag::Untracked:         return "new";
        case GitTag::RecentlyCommitted: return "recent";
        default:                        return "";
    }
}

void refresh_git_signals() { build_git_signals(); }

// 回返a 快照 : O(1) 重计撞,返回的手柄保留
// 缓冲区 只要呼叫者持有它, 就会活着。 旧的签名还原了矢量。
// BY 值 安全,但有一份完整的深层副本 记录了每一
// 条路 调用 单键键键键的键盘。
util::Snapshot<std::vector<std::string>> list_workspace_files(std::size_t cap) {
    // 快路:温暖 缓存 分享它。 ~ 分享它, 锁-free ;
    // 返回的控件拥有其 缓冲区 ,所以同时重排不能从呼叫者
    // 下面拔出.
    if (auto warm = g_files.load(); warm.has_value()) return warm;

    // 冷而且有人需要它 当前(同步呼叫器,还没有预热):
    // 构建内线并发布,因此之后的调用即时发布.
    //
    // 冷酷的打来电可以 竞态 来,来,来 来, 来,去,来, 都来, 故意的:
    // 替代是持有 锁 跨多块 -thousand-path。。
    // )。 它可以让第二位被调用者在第一位等待。
    // 两者都产生一个等价列表, 最后一家商店获胜, 每个读者都
    // 看到一个完整的一代。
    auto built = util::make_snapshot(build_file_list(cap));
    g_files.store(built);
    return built;
}

std::vector<std::size_t>
filter_files(const std::vector<std::string>& files, std::string_view query) {
    std::vector<std::size_t> matches;
    matches.reserve(files.size());

    // 频率快照:路径 → 静态级别(0=最近). 小地图,
    // 每过滤器通过一次读取.
    std::unordered_map<std::string, int> frec;
    {
        const auto v = frecency().load();
        for (std::size_t i = 0; i < v->size(); ++i) frec.emplace((*v)[i], (int)i);
    }
    // Git-status 快照 : 数据 STRONGEST 信号。您正在编辑的
    // 文件几乎总是 dirty/staged - 领头吧
    const auto git_h = git_status_map().load();
    const GitTags& git = *git_h;
    auto git_bonus = [&](std::size_t i) -> int {
        auto it = git.find(files[i]);
        if (it == git.end()) return 0;
        switch (it->second) {
            case GitTag::Modified:          return 100000;   // 正在编辑
            case GitTag::Staged:            return 90000;
            case GitTag::Untracked:         return 80000;
            case GitTag::RecentlyCommitted: return 30000;
            default:                        return 0;
        }
    };
    auto frec_bonus = [&](std::size_t i) -> int {
        auto it = frec.find(files[i]);
        if (it == frec.end()) return 0;
        // 最近的文件会大到排行榜 -decaying 奖金是这样的
        // "the file I just touched" 浮在两个不的顶端 -query 列表和链接。
        return 10000 - it->second * 100;
    };
    // 综合背景得分——git主导,frecency断开关系,
    // 和两个都坐远于模糊的底部,所以工作 -set
    // 文件从不低于字母顺序 -earlier 陌生人的
    // 毛毛分略好一点
    auto ctx_bonus = [&](std::size_t i) { return git_bonus(i) + frec_bonus(i); };

    if (query.empty()) {
        for (std::size_t i = 0; i < files.size(); ++i) matches.push_back(i);
        // 工作 -set 先是(肮脏) 开始 新的 最近的) 冷冻
        // 然后再是预 -sorted 字母顺序,所有没有改变。
        std::stable_sort(matches.begin(), matches.end(),
            [&](std::size_t a, std::size_t b) {
                return ctx_bonus(a) > ctx_bonus(b);
            });
        return matches;
    }

    // 从查询中去除空白空间——用户 带类型的 过滤器,而不是字面图案。
    // 内部空格被保存为子序列空白(所以) "src cpp" 仍然
    // 匹配( T) "src/foo.cpp").
    std::string needle;
    needle.reserve(query.size());
    for (char c : query) {
        if (c == ' ' || c == '\t') continue;
        needle.push_back(ascii_lower(c));
    }
    if (needle.empty()) {
        for (std::size_t i = 0; i < files.size(); ++i) matches.push_back(i);
        std::stable_sort(matches.begin(), matches.end(),
            [&](std::size_t a, std::size_t b) {
                return ctx_bonus(a) > ctx_bonus(b);
            });
        return matches;
    }

    std::vector<Scored> scored;
    scored.reserve(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        int s = fuzzy_score(files[i], needle);
        if (s != INT32_MIN) scored.push_back({s + ctx_bonus(i), i});
    }

    // 平稳排序: 等分的分数回落到已经确定的list_workspace_files
    // () 的字母顺序。 分数优先。 NAME OF TRANSLATORS
    std::stable_sort(scored.begin(), scored.end(),
        [](const Scored& a, const Scored& b) { return a.score > b.score; });

    matches.reserve(scored.size());
    for (const auto& s : scored) matches.push_back(s.idx);
    return matches;
}

} // namespace agentty
