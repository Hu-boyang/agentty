// agentty::tools::memory ——执行。
// memory_store.hpp 用于数据形状和存储理由。
//
// 打开 -disk 格式 (单位:千美元) JSONL ,每行记录一册:
//
//   {"id":"a1b2c3d4","ts":1731860000,"scope":"project","text":"prefer fish"}
//
// 读取容忍斜线:无法读取的行 解析 跳过并计算未来内部
// "memory health" 表面; 代理商从未看到它们。
// 写入是附件 -only 快乐的道路上; 稀有的帽子 -rollover
// 路径在原子上重写文件 util::write_file (它
// 本身在基础的impl支持时使用write+flush+重命名).

#include "agentty/util/user_root.hpp"
#include "agentty/tool/memory_store.hpp"

#include "agentty/io/shared_file.hpp"     // FileLocks, write_atomic, append_atomic
#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // KMemary Layout—— 唯一的宣言
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/util/lock_levels.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#  include <pwd.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include <maya/runtime.hpp>
#include <nlohmann/json.hpp>

namespace agentty::tools::memory {

namespace {

using json = nlohmann::json;

// 每一个读数 -modify-write 显示在任意一个范围文件内运行 store() .. .. 和... . 一起,我和你们一起
// 内容最多为"the agent fired remember + forget back-to-back".
// 需要锁的帮助者会拿走文件符号,它只存在于内部.
//
// store() 覆盖 此 进程。用户 -scope 文件由每个文件
// 共享 agentty 和工程文件,按每个 agentty 输入 仓库 ,
// 所以重写时,文件的十字架也保存着 -process 锁 (持久化::
// FileLocks , 绑定, 排序, 所以两个进程不能 死锁 ),内带.
// 正文.
struct Files {};
using StoreLock = maya::guarded<Files, locks::memory_store>;
StoreLock& store() { static StoreLock f; return f; }

[[nodiscard]] fs::path home_dir() noexcept {
    if (auto* h = std::getenv("HOME"); h && *h) return fs::path{h};
#if defined(_WIN32)
    if (auto* h = std::getenv("USERPROFILE"); h && *h) return fs::path{h};
#else
    // HOME 合法可不设( 已设置的 ProtectHome
    // = 未经批准发射的容器 --env HOME。
    // 返回当前 uid 的密码数据库中。
    // POSIX 系统有真正的家 每个登录uid。
    if (::geteuid() != 0 || std::getenv("SUDO_USER") == nullptr) {
        // getpwuid_r 是那个 线程-safe 变体 ; 绑定 缓冲区 16
        // 岁时 KiB ,比任何理智的 pw_dir +登录名称+ shell.
        std::vector<char> buf(4096);
        struct passwd  pw{};
        struct passwd* result = nullptr;
        for (;;) {
            int e = ::getpwuid_r(::geteuid(), &pw, buf.data(), buf.size(), &result);
            if (e == 0) break;
            if (e == ERANGE && buf.size() < (1u << 16)) {
                buf.resize(buf.size() * 2);
                continue;
            }
            result = nullptr;
            break;
        }
        if (result && result->pw_dir && *result->pw_dir)
            return fs::path{result->pw_dir};
    }
#endif
    return {};
}

// 真实的如果 `dir` 已经存在,而且可以被我们写下来, OR 还没有存在,
// 但是它的第一个现存祖先是可写(所以) create_directories
// 将成功。 root 上的假 = '/' 任何非缔约方 -root 用户,
// 在读数中的路径 -only 挂载等。 检查最好 -effort —
// a TOCTOU 竞态 写作与实际写作之间无害:如果写作与写作之间有误,
// 则写作者自己会浮出水面。 竞态 降落方向不对
[[nodiscard]] bool dir_path_writable(const fs::path& dir) noexcept {
#ifdef _WIN32
    // * Windows上的存取(W_OK)对于目录ACL的检查来说是
    // 著名的不可靠的。 将写入本身的操作程序拖到 XX002xx 上。
    // 返回真实的, 让失败路径生成真正的 Windows 错误消息。
    // 下面的倒转逻辑只用于装载/ 读出 -only 或者作为无人
    // 运行的 POSIX 主机。
    (void)dir;
    return true;
#else
    std::error_code ec;
    fs::path probe = dir;
    while (!probe.empty() && !fs::exists(probe, ec)) {
        auto parent = probe.parent_path();
        if (parent == probe) break;            // 无法超越根
        probe = parent;
    }
    if (probe.empty()) return false;
    return ::access(probe.c_str(), W_OK) == 0;
#endif
}

// 生成一个 8-char x id. `random_device`
// 是一个过程的种子; 跨一个 内存 文件的 生命周期 碰撞
// 是微不足道的( ^32 空间, ~ 数百 记录 )。
// 两种记录在彼此的毫秒内在同一 id 上着陆, 只会产生两条带有
// 相同 id 的线 — — `forget {id}` 将清除两者,
// 毕竟是 -intuitive 的结果。
[[nodiscard]] std::string make_id() {
    // 每台1个发动机 线程 : 无 锁 ,所以这个可以调用 从内部
    // memory_store 锁 正文 (remember() 薄荷的标识
    thread_local std::mt19937_64 engine{std::random_device{}()};
    const auto v = static_cast<std::uint32_t>(engine());
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return std::string{buf, 8};
}

[[nodiscard]] std::int64_t now_unix() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// 双端的Trim ASCII 白空。我们故意不使
// 内部的白空或NFC正常化 — 用户带类型的是模型
// 后来读到的。
[[nodiscard]] std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string{s.substr(b, e - b)};
}

[[nodiscard]] std::optional<Record> parse_record_line(std::string_view line) {
    std::string s = trim(line);
    if (s.empty()) return std::nullopt;
    try {
        auto j = json::parse(s);
        if (!j.is_object()) return std::nullopt;
        Record r;
        r.id   = j.value("id", std::string{});
        r.ts   = j.value("ts", std::int64_t{0});
        auto sc = parse_scope(j.value("scope", std::string{}));
        if (!sc) return std::nullopt;
        r.scope = *sc;
        r.text = j.value("text", std::string{});
        if (r.id.empty() || r.text.empty()) return std::nullopt;
        // 选字段——在遗留记录中缺失,默认读取.
        // 防御性 解析 : 一只手 : -edited
        // 字段错误类型的文件会回到默认状态,
        // 而不是丢弃整个记录。
        if (j.contains("pinned") && j["pinned"].is_boolean())
            r.pinned = j["pinned"].get<bool>();
        if (j.contains("hits") && j["hits"].is_number_integer())
            r.hits = j["hits"].get<std::int32_t>();
        if (j.contains("tags") && j["tags"].is_array()) {
            for (const auto& t : j["tags"]) {
                if (!t.is_string()) continue;
                auto s2 = t.get<std::string>();
                if (!s2.empty()) r.tags.push_back(std::move(s2));
            }
        }
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string serialise_record(const Record& r) {
    json j;
    j["id"]    = r.id;
    j["ts"]    = r.ts;
    j["scope"] = to_string(r.scope);
    j["text"]  = r.text;
    // 设定时只发出可选字段——为普通大小写保持短线(没有标记,
    // 没有固定,从未解开)和上 -disk 格式 字节
    // -identical 以遗赠的形状。
    if (r.pinned)        j["pinned"] = true;
    if (r.hits > 0)      j["hits"]   = r.hits;
    if (!r.tags.empty()) j["tags"]   = r.tags;
    // dump() 不得缩进:每行施工记录1个。
    return j.dump();
}

// 下调 -case ASCII + 崩溃 内部白空间运行到单空间 + 条尾。
// 被 解开 散列 使用 "Build moha with cmake "
// 匹配 "build moha with cmake"。是否将 不 触摸(
// T) UTF -8个多 -byte 运行中——模型不太可能在两种情况之间发生
// 差异: 调用 并妥善折叠它们会拉进来 ICU.
[[nodiscard]] std::string normalise_for_dedup(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool in_ws = true;   // 跳过正向空白
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            if (!in_ws) { out.push_back(' '); in_ws = true; }
            continue;
        }
        in_ws = false;
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + 32);
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// 便宜相似性: 前缀 -bounded 短弦的Jaro。返回[0] 1 的分数;
// 我们把 0.92 当作 "same fact, different wording".
// 我们不拉在贾罗 -Winkler 因为前缀偏差会使 "build with cmake"
// 财务报告和已审计财务报表 "build with cargo" 分数 > 0.
// 95 — 它们有一个共同的前缀, 但意思不同。 普通的Jaro 足够用于
// 典型的模型的分辨率。 -restatement 案件记录。
[[nodiscard]] double jaro_similarity(std::string_view a, std::string_view b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    const std::size_t la = a.size();
    const std::size_t lb = b.size();
    const std::size_t match_window = la > lb ? la / 2 : lb / 2;
    std::vector<char> a_matched(la, 0);
    std::vector<char> b_matched(lb, 0);
    std::size_t matches = 0;
    for (std::size_t i = 0; i < la; ++i) {
        const std::size_t lo = i > match_window ? i - match_window : 0;
        const std::size_t hi = std::min(i + match_window + 1, lb);
        for (std::size_t j = lo; j < hi; ++j) {
            if (b_matched[j]) continue;
            if (a[i] != b[j])  continue;
            a_matched[i] = 1;
            b_matched[j] = 1;
            ++matches;
            break;
        }
    }
    if (matches == 0) return 0.0;
    // 转位:数对对,但顺序不同。
    std::size_t k = 0, t = 0;
    for (std::size_t i = 0; i < la; ++i) {
        if (!a_matched[i]) continue;
        while (!b_matched[k]) ++k;
        if (a[i] != b[k]) ++t;
        ++k;
    }
    const double m = static_cast<double>(matches);
    return (m / la + m / lb + (m - t / 2.0) / m) / 3.0;
}

// 标记正常化: 下-case ASCII, 放下空格, 解脱, 排序。
[[nodiscard]] std::vector<std::string> normalise_tags(
        const std::vector<std::string>& in) {
    std::vector<std::string> out;
    out.reserve(in.size());
    for (auto t : in) {
        // 小写 ASCII 已就位
        for (auto& c : t)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        // 修剪
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
            t.erase(t.begin());
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
            t.pop_back();
        if (!t.empty()) out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

[[nodiscard]] std::vector<Record> read_records(const fs::path& p) {
    std::vector<Record> out;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) return out;
    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (auto r = parse_record_line(line)) out.push_back(std::move(*r));
    }
    return out;
}

// 重写文件的原子化( temp + fsync + 重命名)。 成功时空。
[[nodiscard]] std::string write_records(const fs::path& p,
                                        const std::vector<Record>& recs) {
    std::string out;
    for (const auto& r : recs) { out += serialise_record(r); out += '\n'; }
    if (persistence::detail::write_in_section(p.string(), out,
            {.durability = persistence::Durability::Durable, .mode = 0600}))
        return {};
    return "could not write " + p.string();
}

// 附加 一个 写 (2) 中的一个记录, 加上 O_APPEND ,
// 因此两个 agentty 进程 总是不同时附加到彼此的行中。 空在成功时。
[[nodiscard]] std::string append_line(const fs::path& p, std::string_view line) {
    std::string rec{line};
    rec += '\n';
    if (persistence::detail::append_in_section(p.string(), rec,
                                               persistence::Durability::Durable, 0600))
        return {};
    return "could not append to " + p.string();
}

// ── 时间 -keyed 缓存 财务报告和审定财务报表 提示词-loader 高频路径 ────────────────────
// `default_system_prompt` 调用 load_recent_ *
// ()每转时,都照亮 read_memory_cached 模式在
// transport.cpp 这么说吧 内存 当磁盘上没有任何变化时, 仍可免费加载。
// Name 缓存 键是范围 -file 路径; 值是 末尾-N 记录列表,
// 当 mtime 移动时重生成。
struct CacheEntry {
    long long           mtime = 0;   // file_time_type 勾选
    std::vector<Record> tail;
};
using Cache = std::unordered_map<std::string, CacheEntry>;
// A 快照 : 最多两个条目(用户+项目),在错误时替换,并在每次转弯时读取。
// 没有 锁 ,所以装填器和 bump_cache 不需要能力,
// 而且可以从内部调用 memory_store 锁 正文.
maya::snapshot<Cache>& cache() { static maya::snapshot<Cache> c; return c; }

[[nodiscard]] std::vector<Record> tail_of(const std::vector<Record>& all,
                                          std::size_t n) {
    if (all.size() <= n) return all;
    return std::vector<Record>(all.end() - static_cast<std::ptrdiff_t>(n),
                               all.end());
}

[[nodiscard]] std::vector<Record> load_recent_for(Scope s) {
    const auto p = path_for(s);
    if (p.empty()) return {};
    const auto key = p.string();
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) {
        // 缺少文件 - 滴 缓存 输入这样的未来 -creation 兹予观察。
        cache().update([](const Cache& c, const std::string& k) {
            Cache next = c; next.erase(k); return next;
        }, key);
        return {};
    }
    const long long now_mt = static_cast<long long>(t.time_since_epoch().count());
    {
        const auto cur = cache().load();
        auto it = cur->find(key);
        if (it != cur->end() && it->second.mtime == now_mt) return it->second.tail;
    }
    auto tail = tail_of(read_records(p), kTailLoadCount);
    cache().update([](const Cache& c, const std::string& k, const CacheEntry& e) {
        Cache next = c; next[k] = e; return next;
    }, key, CacheEntry{now_mt, tail});
    return tail;
}

// 无效 缓存 变异后的一个范围 调用.
// `last_write_time` 由 OS 当我们写,
// 所以 缓存 将自己 -heal 下个数据表上 —— 但急切地取消
// 保留下一个加载器 调用 由具有文件系统时间颗粒性的赛车( ) HFS
// +和一些 NFS 出口量第二 -resolution 时间。
void bump_cache(Scope s) {
    const auto p = path_for(s);
    if (p.empty()) return;
    cache().update([](const Cache& c, const std::string& k) {
        Cache next = c; next.erase(k); return next;
    }, p.string());
}

} // 命名空间结束。

std::optional<Scope> parse_scope(std::string_view s) noexcept {
    if (s == "user") return Scope::User;
    if (s == "project") return Scope::Project;
    return std::nullopt;
}

// ── suggest_scope:一个在事实文本上打分的信号模型 ───────────────
// 不是关键字抓取 -bag :每个提示是加权的 EVIDENCE
// 向用户(关于人类的事实)或项目(关于人类的真相)拉动 代码库).
// 我们把证据汇总起来,把比值, 并压扁它 [0,1] 信心。
// 平局或无-evidence文本 以0 = 调用者保持默认值。
ScopeSuggestion suggest_scope(std::string_view text) noexcept {
    // 较小的大小写, 空格 -padded 复制到这个字 -boundary 探测器简单,
    // 在较长的标志内不匹配( " i " 数字 "api").
    std::string t;
    t.reserve(text.size() + 2);
    t.push_back(' ');
    for (char c : text) t.push_back(static_cast<char>(std::tolower((unsigned char)c)));
    t.push_back(' ');
    auto has = [&](std::string_view needle) {
        return t.find(needle) != std::string_view::npos;
    };

    struct Cue { std::string_view probe; float w; std::string_view why; bool boundary; };

    // USER 证据——事实是 ABOUT 该 HUMAN 首页 -person
    // 自我参考和 identity/preference 动词是强分词.
    static constexpr Cue kUser[] = {
        {" i ",         0.45f, "first-person 'I'",           false},
        {" i'm ",       0.45f, "first-person 'I'm'",         false},
        {" my ",        0.40f, "possessive 'my'",            false},
        {" me ",        0.25f, "first-person 'me'",          false},
        {"call me",     0.60f, "identity ('call me')",       false},
        {"my name",     0.70f, "identity ('my name')",       false},
        {"i prefer",    0.65f, "stated preference",          false},
        {"i like",      0.50f, "stated preference",          false},
        {"i use",       0.55f, "personal tooling",           false},
        {"i always",    0.45f, "personal habit",             false},
        {"i work",      0.35f, "personal context",           false},
        {"personal",    0.40f, "'personal'",                 false},
        {"pronoun",     0.35f, "identity (pronouns)",        false},
    };
    // PROJECT 证据——事实是 ABOUT 此 CODEBASE. 仓库 团队,德克西 'we' ,
    // 和混凝土 build/path/command 标志。
    static constexpr Cue kProject[] = {
        {"this project",  0.65f, "repo deixis ('this project')", false},
        {"this repo",     0.65f, "repo deixis ('this repo')",    false},
        {"this codebase", 0.65f, "repo deixis ('this codebase')",false},
        {"the project",   0.35f, "project reference",            false},
        {" we ",          0.40f, "team 'we'",                    false},
        {" our ",         0.35f, "team 'our'",                   false},
        {"build command", 0.60f, "build workflow",               false},
        {"the build",     0.45f, "build workflow",               false},
        {"to build",      0.40f, "build workflow",               false},
        {"cmake",         0.45f, "build tool token",             false},
        {"the test",      0.35f, "test workflow",                false},
        {"convention",    0.40f, "project convention",           false},
        {" src/",         0.45f, "source path",                  false},
        {".cpp",          0.35f, "source file",                  false},
        {".hpp",          0.35f, "source file",                  false},
        {"the api",       0.30f, "code surface",                 false},
        {"module",        0.25f, "code structure",               false},
    };

    float user = 0.0f, proj = 0.0f;
    std::string_view user_why, proj_why;
    for (const auto& c : kUser)
        if (has(c.probe) && c.w > user) { user = c.w; user_why = c.why; }
    for (const auto& c : kProject)
        if (has(c.probe) && c.w > proj) { proj = c.w; proj_why = c.why; }
    // 积聚一点 MULTIPLE 独立提示在一边(秒,弱击加分),
    // 所以 "I prefer, and my name is"
    // 读起来比单独读更响亮——不放一班人跑了。
    auto bump = [&]<std::size_t N>(const Cue (&cues)[N], float& top) {
        float extra = 0.0f;
        for (const auto& c : cues) if (has(c.probe)) extra += c.w * 0.25f;
        top = std::min(1.0f, top + std::max(0.0f, extra - top * 0.25f));
    };
    bump(kUser, user);
    bump(kProject, proj);

    ScopeSuggestion s;
    const float margin = user - proj;
    if (margin > 0.0f) {
        s.scope = Scope::User;
        s.confidence = std::min(1.0f, margin);
        s.reason = std::string{user_why};
    } else if (margin < 0.0f) {
        s.scope = Scope::Project;
        s.confidence = std::min(1.0f, -margin);
        s.reason = std::string{proj_why};
    } else {
        // 没有任何证据,或真正的领带 + 模糊不清。项目是中性前置(
        // 匹配 提示词 '' 记录的默认) , 以零置信度,
        // 所以 confident() 虚假的,而呼叫者自己守着默认。
        s.scope = Scope::Project;
        s.confidence = 0.0f;
        s.reason = user == 0.0f ? "no signal" : "conflicting signals";
    }
    return s;
}

fs::path path_for(Scope s) {
    // 内存 消费者的第一 agentty::scope。它拥有边缘分辨率(它拥有
    // home_dir (a) 携带 getpwuid_r 回退 瞄准镜不)和手瞄准镜 虚构的天线;
    // scope::plan ()然后集中Locus × Dialect 排版 + o.
    // project-clamp/writability 守护器, 用来在此处内线。
    // 行为相同 : 用户。 ~/.agentty/memory.jsonl ,
    // 项目 + <project-root>/.agentty/memory.jsonl
    // (空着,在 "/" 或当不可写时)。 内存 只能使用本地人。
    // agentty 方言,所以我们把代理源 要求的蝗虫。
    const scope::Layout& layout = ::agentty::config::kMemoryLayout;
    scope::Env env;
    env.home             = home_dir();       // 比范围本身更丰富
    env.user_native_base = ::agentty::util::user_root();
    env.project_root     = util::project_root();
    env.project_writable = dir_path_writable(env.project_root / ".agentty");

    const scope::Locus want =
        (s == Scope::User) ? scope::Locus::User : scope::Locus::Project;

    for (const scope::Source& src : scope::plan(layout, env)) {
        // 洛克斯还是个过滤器... path_for 答案( T)
        // "the path for THIS scope" ,没有 "the winning path"。
        // 过去坐在旁边的方言检查已经不存在:布局只要求本地语言,所以每个来源
        // plan() 产量已经。 agentty.
        if (src.locus != want) continue;
        // 项目范围必须能够写成有效的存储目标(默默
        // 地推广到用户范围的项目事实将流血于工作区).
        // 用户范围只需要一个解决的基础.
        if (want == scope::Locus::Project && !src.writable) return {};
        return src.base / layout.leaf;
    }
    return {};
}

namespace {
AppendResult append_locked(Files&, Scope actual_scope, std::string body,
                           std::vector<std::string> tags_norm, AppendOptions opts,
                           const fs::path& p, AppendResult res);
}

AppendResult append(const locks::below_memory_store& cap, Scope s,
                    std::string_view text, AppendOptions opts) {
    AppendResult res;
    std::string body = trim(text);
    if (body.empty()) {
        res.error = "remember: text is empty after trim";
        return res;
    }
    if (body.size() > kMaxTextBytes) {
        // 断线在(可能) UTF-8-safe 我们不拉
        // utf8 的帮助在这里 —— 回到最后的领先 字节 4
        // 号机 -byte 倒影覆盖最长的法律 UTF -8个序列,
        // 最糟糕的情况是,我们失去了一个跟踪的代码点。
        std::size_t cut = kMaxTextBytes;
        for (int back = 0; back < 4 && cut > 0; ++back, --cut) {
            unsigned char c = static_cast<unsigned char>(body[cut]);
            if ((c & 0xC0) != 0x80) break;
        }
        res.note = "text truncated to " + std::to_string(cut) + " bytes (was "
                 + std::to_string(body.size()) + ")";
        body.resize(cut);
    }

    auto tags_norm = normalise_tags(opts.tags);

    auto p = path_for(s);
    Scope actual_scope = s;
    if (p.empty() && s == Scope::Project) {
        // 项目范围不详(unresolvable/unwritable项目 cwd)。
        // 不执行 默默地回到用户范围:一个推广到
        // 用户范围的项目事实 加载 输入 每个 其他项目
        // 工作区 系统 提示词 - 十字架 - 十字记
        // -workspace "memory bleed"
        // 告诉打电话者如何明确选择一个全球事实
        res.error =
            "remember: project scope is unavailable here — workspace root '"
            + util::workspace_root().string()
            + "' is not writable, so there is nowhere to store a project-"
              "scoped fact. Re-run with scope=\"user\" if you intended a "
              "global fact (it will load into every workspace), or start "
              "agentty in a writable project directory.";
        return res;
    }
    if (p.empty()) {
        // s ⁇  Scope::User甚至用户路径也无法解决.
        res.error = "remember: can't resolve a writable user memory path "
                    "(HOME is unset and getpwuid_r returned no home directory).";
        return res;
    }

    return store().with(cap, [](Files& f, Scope actual_scope, std::string body,
                           std::vector<std::string> tags_norm, AppendOptions opts,
                           std::string path, AppendResult res) {
        return append_locked(f, actual_scope, std::move(body), std::move(tags_norm),
                             std::move(opts), fs::path{path}, std::move(res));
    }, actual_scope, std::move(body), std::move(tags_norm), std::move(opts),
       p.string(), std::move(res));
}

namespace {
AppendResult append_locked(Files&, Scope actual_scope, std::string body,
                           std::vector<std::string> tags_norm, AppendOptions opts,
                           const fs::path& p, AppendResult res) {
    // 其他人员 agentty 进程:见关于 store()。
    // 替换时也可能重写 OTHER 范围文件,所以当它被要求的时候,
    // 两个文件都被锁在前面一个固定的顺序(用户,然后是工程)—
    // —一个固定的次序是使两个进程不同时存在,而每个进程都等待
    // 另一个进程.
    const persistence::FileLocks xlocks = [&] {
        const fs::path pu = actual_scope == Scope::User ? p : path_for(Scope::User);
        const fs::path pp = actual_scope == Scope::Project ? p : path_for(Scope::Project);
        const bool both = !trim(opts.supersedes_id).empty();
        std::vector<fs::path> files;
        if (both || actual_scope == Scope::User)    files.push_back(pu);
        if (both || actual_scope == Scope::Project) files.push_back(pp);
        return persistence::FileLocks{std::move(files)};
    }();
    auto existing = read_records(p);

    // ── 跳过 ───────────────────────────────────────────────────
    // 将已有的记录走过, 寻找一个正版文本接近的 -match 预选表 正文。
    // 在命中时:刷新 t, 撞击, 合并 pin( Q)
    // "once pinned, stays pinned" 工会的标签。
    // 没有分配新的 id; 返回现有记录的 id ,
    // `deduped` 标志 给打电话者的解约
    if (!opts.no_dedup) {
        const auto needle = normalise_for_dedup(body);
        for (auto& rec : existing) {
            if (rec.scope != actual_scope) continue;
            const auto hay = normalise_for_dedup(rec.text);
            if (hay == needle
                || jaro_similarity(hay, needle) >= 0.92) {
                rec.ts = now_unix();
                if (rec.hits < std::numeric_limits<std::int32_t>::max())
                    ++rec.hits;
                if (opts.pinned) rec.pinned = true;
                if (!tags_norm.empty()) {
                    auto merged = rec.tags;
                    merged.insert(merged.end(),
                                  tags_norm.begin(), tags_norm.end());
                    rec.tags = normalise_tags(merged);
                }
                // 解脱时的替换:罕见但有可能(一个
                // 模型既重复又试图放弃之前的版本).
                // 将替换应用到下面相同的重写中。
                std::string sup = trim(opts.supersedes_id);
                std::size_t sup_dropped = 0;
                if (!sup.empty()) {
                    auto before = existing.size();
                    existing.erase(
                        std::remove_if(existing.begin(), existing.end(),
                            [&](const Record& r){ return r.id == sup; }),
                        existing.end());
                    sup_dropped = before - existing.size();
                }
                std::string err = write_records(p, existing);
                if (!err.empty()) {
                    res.error = "remember: " + err;
                    return res;
                }
                res.id = rec.id;
                res.deduped = true;
                std::string add = "deduped: refreshed existing record (hits="
                                + std::to_string(rec.hits) + ")";
                if (!sup.empty()) {
                    add += sup_dropped
                        ? "; superseded " + sup
                        : "; supersede id " + sup + " not found";
                }
                if (res.note.empty()) res.note = std::move(add);
                else { res.note += "; "; res.note += add; }
                bump_cache(actual_scope);
                return res;
            }
        }
    }

    // ── 补过 ───────────────────────────────────────────────
    // 将命名的前身( 无论是范围还是范围) 丢入添加新记录的重写中。
    // 缺少的 id 是非 -fatal 我们只是注意到了 这
    // 是原子编辑 -an-existing-fact 路径。
    std::string sup = trim(opts.supersedes_id);
    bool sup_hit = false;
    if (!sup.empty()) {
        auto before = existing.size();
        existing.erase(
            std::remove_if(existing.begin(), existing.end(),
                [&](const Record& r){ return r.id == sup; }),
            existing.end());
        sup_hit = (existing.size() != before);
        if (!sup_hit) {
            // id可能生活在OTHER范围中。
            // 把它也放在那里—— 无论模型认为它是在编辑
            // 哪个范围, 替换语义都保持一致。
            Scope other = (actual_scope == Scope::User) ? Scope::Project : Scope::User;
            auto op = path_for(other);
            if (!op.empty()) {
                auto other_recs = read_records(op);
                auto ob = other_recs.size();
                other_recs.erase(
                    std::remove_if(other_recs.begin(), other_recs.end(),
                        [&](const Record& r){ return r.id == sup; }),
                    other_recs.end());
                if (other_recs.size() != ob) {
                    (void)write_records(op, other_recs);
                    bump_cache(other);
                    sup_hit = true;
                }
            }
        }
        std::string add = sup_hit
            ? "superseded " + sup
            : "supersede id " + sup + " not found (new record still written)";
        if (res.note.empty()) res.note = std::move(add);
        else { res.note += "; "; res.note += add; }
    }

    // ── 构建新记录 ─────────────────────────────────────────
    Record r;
    r.id     = make_id();
    r.ts     = now_unix();
    r.scope  = actual_scope;
    r.text   = std::move(body);
    r.pinned = opts.pinned;
    r.tags   = std::move(tags_norm);
    r.hits   = 0;

    // ── 翻转车头 ─────────────────────────────────────────────────
    // 封存记录是封顶的 -exempt : 走最老路
    // -first 放下第一个 UNPINNED 只有在
    // 文件里的所有东西都被封住,我们仍会溢出的情况下,
    // 才恢复到被封住的状态——如果这种堕落的情况,
    // 用户拥有比盖子更坚固的事实,而最古老的被封住者必须离开。
    auto drop_oldest_unpinned = [](std::vector<Record>& v) -> bool {
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (!it->pinned) { v.erase(it); return true; }
        }
        return false;
    };
    bool rollover = false;
    while (existing.size() + 1 > kMaxRecordsPerScope) {
        bool dropped = drop_oldest_unpinned(existing);
        if (!dropped) {
            // 全部( A) -pinned 溢出——牺牲最古老的钉子.
            existing.erase(existing.begin());
        }
        ++res.rolled;
        rollover = true;
    }
    existing.push_back(r);
    auto total_bytes = [](const std::vector<Record>& rs) {
        std::size_t b = 0;
        for (const auto& x : rs) b += serialise_record(x).size() + 1;
        return b;
    };
    while (existing.size() > 1 && total_bytes(existing) > kMaxFileBytes) {
        if (!drop_oldest_unpinned(existing)) {
            // 全部( A) -pinned 溢出时间 字节 盖子 放下
            // 最古老的钉子 而不是我们刚刚推的纪录
            if (existing.size() > 1) existing.erase(existing.begin());
            else break;
        }
        ++res.rolled;
        rollover = true;
    }

    std::string err;
    if (rollover || sup_hit) {
        // 任何重写-shaped突变都经过原子路径.
        err = write_records(p, existing);
    } else {
        err = append_line(p, serialise_record(r));
    }
    if (!err.empty()) {
        res.error = "remember: " + err;
        return res;
    }
    res.id = r.id;
    bump_cache(actual_scope);
    return res;
}
} // 命名空间结束。

std::vector<Record> load_all(const locks::below_memory_store& cap, Scope s) {
    const auto p = path_for(s);
    if (p.empty()) return {};
    return store().with(cap, [](Files&, std::string path) { return read_records(path); }, p.string());
}

std::vector<Record> load_recent_user()    { return load_recent_for(Scope::User); }
std::vector<Record> load_recent_project() { return load_recent_for(Scope::Project); }

// UTF-8-safe剪辑最多为`cap`XX字节,以一个完整的代码点结束.
static std::string clip_text(const std::string& s, std::size_t cap) {
    if (s.size() <= cap) return s;
    std::size_t cut = cap;
    for (int back = 0; back < 4 && cut > 0; ++back, --cut) {
        unsigned char c = static_cast<unsigned char>(s[cut]);
        if ((c & 0xC0) != 0x80) break;   // 领导 字节 □ 安全边界
    }
    std::string out = s.substr(0, cut);
    out += "\xe2\x80\xa6";   // ——信号提示词副本被剪辑
    return out;
}

PromptSelection select_for_prompt(std::vector<Record> recent,
                                  std::size_t byte_budget) {
    PromptSelection sel;
    if (recent.empty()) return sel;

    // 大约每名 -record 提示词 成本 :
    // render_for_prompt 添加一个 `[id] ` 前缀
    // ( ~11) 字节 ,可选的 QQ, 标记后缀, 以及一条后缀的新线。
    // 输入剪辑的文本长度+固定的铬估计值, 这样预算将反映实际在 提示词.
    auto cost_of = [](const Record& r) -> std::size_t {
        std::size_t c = std::min(r.text.size(), kPromptRecordCap);
        c += 16;                                   // [id] + +  ⁇ + 新线
        for (const auto& tg : r.tags) c += tg.size() + 2;
        return c;
    };

    // 预算排名 -fill : 被锁定,然后是更多点击,然后是最近。
    // 我们保留一个索引列表,这样就可以 FINAL 排放量可恢复到最古老的稳定
    // -first 顺序( 最近[ ] 已经是最古老的 -first).
    std::vector<std::size_t> order(recent.size());
    for (std::size_t i = 0; i < recent.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) {
            const Record& ra = recent[a];
            const Record& rb = recent[b];
            if (ra.pinned != rb.pinned) return ra.pinned;       // 被钉住的胜者
            if (ra.hits   != rb.hits)   return ra.hits > rb.hits;// 更多点击数获胜
            return ra.ts > rb.ts;                                // 较近期获胜
        });

    // 顺着顺序排列, 接受记录直到 字节 预算已经用完。
    // 即使超过预算,也承认有固定记录(它们已经使用)。
    // 加载-bearing 定义上,其他一切都尊重盖子。
    std::vector<char> keep(recent.size(), false);
    std::size_t spent = 0;
    for (std::size_t idx : order) {
        const std::size_t c = cost_of(recent[idx]);
        if (recent[idx].pinned || spent + c <= byte_budget) {
            keep[idx] = true;
            spent += c;
        }
    }

    // 爱慕保存在最古老的档案 -first 命令,剪辑任何结束
    // -long 文本为每张 -record 盖子,数数滴子
    for (std::size_t i = 0; i < recent.size(); ++i) {
        if (!keep[i]) { ++sel.dropped; continue; }
        Record r = std::move(recent[i]);
        r.text = clip_text(r.text, kPromptRecordCap);
        sel.records.push_back(std::move(r));
    }
    return sel;
}

namespace {
// 从两个范围文件中丢弃匹配`pred`的每个记录.
template <class Pred>
std::size_t remove_where(Files&, Pred pred) {
    std::size_t removed = 0;
    for (auto s : {Scope::User, Scope::Project}) {
        const auto p = path_for(s);
        if (p.empty()) continue;
        const persistence::FileLocks xlock({p});   // 见关于store()的说明
        auto recs = read_records(p);
        const std::size_t before = recs.size();
        std::erase_if(recs, pred);
        if (recs.size() != before) {
            (void)write_records(p, recs);
            removed += before - recs.size();
            bump_cache(s);
        }
    }
    return removed;
}
} // 命名空间结束。

std::size_t forget_by_id(const locks::below_memory_store& cap, std::string_view id) {
    std::string want{id};
    if (trim(want).empty()) return 0;
    return store().with(cap, [](Files& f, std::string w) {
        return remove_where(f, [&w](const Record& r) { return r.id == w; });
    }, want);
}

std::size_t forget_by_substring(const locks::below_memory_store& cap,
                                std::string_view needle) {
    std::string want = trim(needle);
    if (want.empty()) return 0;
    return store().with(cap, [](Files& f, std::string w) {
        return remove_where(f, [&w](const Record& r) {
            return r.text.find(w) != std::string::npos;
        });
    }, want);
}

std::string render_for_prompt(const Record& r) {
    std::string out;
    out.reserve(r.text.size() + 32);
    out += '[';
    out += r.id;
    out += "] ";
    if (r.pinned) out += "\xe2\x98\x85 ";   // +++- 固定的视觉提示
    // 记录可以循环 -trip 任意 字节 从粘贴工具输出或剪贴板抓取
    // 中( ) e.g。一个短长的复数 -byte UTF - 给它修补一下,
    // 系统就修好了 提示词 ——由提供者的系列 JSON 垃圾场,
    // 总是好的 -formed 便宜无 -op。当已经有效时。
    out += tools::util::to_valid_utf8(r.text);
    if (!r.tags.empty()) {
        out += "  {";
        for (std::size_t i = 0; i < r.tags.size(); ++i) {
            if (i) out += ", ";
            out += r.tags[i];
        }
        out += '}';
    }
    return out;
}

std::optional<std::pair<Record, Scope>> find_by_id(const locks::below_memory_store& cap,
                                                   std::string_view id) {
    std::string want{id};
    if (trim(want).empty()) return std::nullopt;
    return store().with(cap, [](Files&, std::string w) -> std::optional<std::pair<Record, Scope>> {
        for (auto s : {Scope::User, Scope::Project}) {
            const auto p = path_for(s);
            if (p.empty()) continue;
            for (auto& r : read_records(p))
                if (r.id == w) return std::make_pair(std::move(r), s);
        }
        return std::nullopt;
    }, want);
}

std::vector<Record> preview_forget_by_substring(const locks::below_memory_store& cap,
                                                std::string_view needle) {
    std::vector<Record> out;
    std::string want = trim(needle);
    if (want.empty()) return out;
    return store().with(cap, [](Files&, std::string w) {
        std::vector<Record> hits;
        for (auto s : {Scope::User, Scope::Project}) {
            const auto p = path_for(s);
            if (p.empty()) continue;
            for (auto& r : read_records(p))
                if (r.text.find(w) != std::string::npos) hits.push_back(std::move(r));
        }
        return hits;
    }, want);
}

std::optional<std::size_t> wipe(const locks::below_memory_store& cap, Scope s) {
    const auto p = path_for(s);
    if (p.empty()) return std::nullopt;
    // 特朗卡特(写作空)而不是fs::remove——保留路径对后续的附录调用有效,
    // 而不用re-running create_directories.
    // write_records在空矢量上写作0-byte文件,
    // 其中read_records / load_all都处理.
    const std::size_t n = store().with(cap, [](Files&, std::string path) {
        const persistence::FileLocks xlock({fs::path{path}});
        const std::size_t count = read_records(path).size();
        (void)write_records(path, {});
        return count;
    }, p.string());
    bump_cache(s);
    return n;
}

} // namespace agentty::tools::memory
