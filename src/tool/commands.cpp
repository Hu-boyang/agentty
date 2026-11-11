// commands.cpp 用户 -authored 斜线命令。
// commands.hpp 结构镜像 skills.cpp
// 故意( 相同的宽大前题子集, 相同的 mtime-
// signition) 缓存 所以两个装货员在概念上 保持一件事。

#include "agentty/util/user_root.hpp"
#include "agentty/tool/commands.hpp"
#include "agentty/util/home_dir.hpp"

#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // KCommands Layout——单一宣言
#include "agentty/util/capped_read.hpp"   // 原始的封顶-read

#include <maya/runtime.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace agentty::tools::commands {

namespace {

constexpr std::size_t kMaxCommands  = 128;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr int         kMaxDepth     = 3;   // namespace nesting (a:b:c)

[[nodiscard]] fs::path home_dir() noexcept {
    return agentty::util::home_dir_or_empty();
}

[[nodiscard]] std::string trim(std::string_view v) {
    std::size_t b = 0, e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return std::string{v.substr(b, e - b)};
}

// NOTE : 当地 read_capped () 已消失——见
// util/capped_read.hpp 为什么三份都运走了同样的错误

[[nodiscard]] bool parse_kv(const std::string& line,
                            std::string& key, std::string& val) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return false;
    key = trim(line.substr(0, colon));
    val = trim(line.substr(colon + 1));
    if (val.size() >= 2 &&
        ((val.front() == '"' && val.back() == '"') ||
         (val.front() == '\'' && val.back() == '\''))) {
        val = val.substr(1, val.size() - 2);
    }
    return !key.empty();
}

// 解析 单曲 <name> . md 输入命令。 前置事项是可选的;
// 只有scalar 键 `description` /
// `argument-hint` 被识别( 命令比技能简单 —
// — 不需要块级的缩写 ; 块) -scalar 野外的描述
// 解析 保持了这个 `|` 标记并取出第一个通过 正文 回退).
[[nodiscard]] Command parse_command(const std::string& raw,
                                    std::string name,
                                    const std::string& source) {
    Command c;
    c.name   = std::move(name);
    c.source = source;

    std::istringstream in(raw);
    std::string line;
    std::streampos body_start = 0;
    bool fm_done = false;
    std::string first;
    if (std::getline(in, first) && trim(first) == "---") {
        while (std::getline(in, line)) {
            if (trim(line) == "---") {
                fm_done = true;
                body_start = in.tellg();
                break;
            }
            std::string k, v;
            if (!parse_kv(line, k, v)) continue;
            if      (k == "description")   c.description   = v;
            else if (k == "argument-hint") c.argument_hint = v;
            // 未知密钥被容忍( N) Claude 已允许
            // -tools ,模式——此处不执行;
            // 扩大范围 提示词 与正常的工具门一起穿过正常的转弯管道。
        }
    }
    if (fm_done && body_start != std::streampos(-1)) {
        c.body = trim(raw.substr(static_cast<std::size_t>(body_start)));
    } else {
        c.body = trim(raw);
    }
    if (c.description.empty()) {
        std::istringstream b(c.body);
        std::string l;
        while (std::getline(b, l)) {
            auto t = trim(l);
            if (!t.empty()) { c.description = t; break; }
        }
    }
    return c;
}

// 递归扫描 `dir` *.md命令文件,将子目录名称合并到命令名称中,
// 并带有 ':' (git/fixup.md ~  ⁇ :补.
void scan_dir(const fs::path& dir, const std::string& prefix,
              const std::string& source, int depth,
              std::vector<Command>& out, std::string& sig) {
    if (depth > kMaxDepth) return;
    std::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec))
        entries.push_back(it->path());
    std::sort(entries.begin(), entries.end());

    for (const auto& p : entries) {
        if (out.size() >= kMaxCommands) return;
        std::error_code sec;
        if (fs::is_directory(p, sec)) {
            const std::string sub = p.filename().string();
            if (!sub.empty() && sub[0] != '.')
                scan_dir(p, prefix.empty() ? sub : prefix + ":" + sub,
                         source, depth + 1, out, sig);
            continue;
        }
        if (p.extension() != ".md") continue;
        const std::string stem = p.stem().string();
        if (stem.empty() || stem[0] == '.') continue;

        auto fmt = fs::last_write_time(p, sec);
        if (!sec)
            sig += std::to_string(static_cast<long long>(
                       fmt.time_since_epoch().count())) + ";";
        // 命令没有 `agentty commands`
        // CLI 以报告,所以一个超大小的文件被跳过 --
        // 但是 TooBig 大小写现在被命名而不是隐藏在空字符串中,
        // 所以如果 CLI 船已经到这里了
        const auto got = ::agentty::util::capped_read(p, kMaxBodyBytes);
        if (!::agentty::util::has_content(got)) continue;
        const std::string& raw = ::agentty::util::bytes_or_empty(got);
        if (raw.empty()) continue;

        std::string name = prefix.empty() ? stem : prefix + ":" + stem;
        // 阴影:早先的根在名称碰撞上获胜.
        if (std::ranges::any_of(out,
                [&](const Command& e) { return e.name == name; }))
            continue;
        Command c = parse_command(raw, std::move(name), source);
        if (c.body.empty()) continue;
        std::error_code cec;
        auto abs = fs::weakly_canonical(p, cec);
        c.file = (cec ? fs::absolute(p, cec) : abs).string();
        out.push_back(std::move(c));
    }
}

void scan_root(const fs::path& root, const std::string& source,
               std::vector<Command>& out, std::string& sig) {
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec) return;
    auto mt = fs::last_write_time(root, ec);
    if (!ec)
        sig += source + ":" +
               std::to_string(static_cast<long long>(
                   mt.time_since_epoch().count())) + ";";
    scan_dir(root, /*前缀=( 缩写)*/{}, source, /*深度=( 以千兆克计)*/0, out, sig);
}

// 扫描命令和扫描时的Mtime签名。
struct Cache {
    std::vector<Command> commands;
    std::string          sig = "\x01uninit";
};
// 批发部分改为批发部分, 由每次查询读取: a 快照 ,
// 所以没有 锁 ,在读取路径上的级别或能力。
maya::snapshot<Cache>& cache() {
    static maya::snapshot<Cache> c;
    return c;
}

} // 命名空间结束。

std::vector<Command> all(Io) {
    // 在锁外扫描(指令行+文件读取),
    // 然后只有在签名移动时保留新列表.
    std::string sig;
    std::vector<Command> fresh;
    // 根梯子从 scope::plan (乐库) -major 对话框 -minor 项目。
    // agentty ..代理 .. . . .. .. 说出同样的三个在~. scan_root
    // 执行第一个 -name-wins 阴影 + 时间 -sig 各个根。项目保持 cwd 相对(
    // = = 3) env.project_root = "." )——命令始终解决.
    // ".agentty/commands" 对抗Cwd。
    scope::Env env;
    env.home             = home_dir();
    env.user_native_base = ::agentty::util::user_root();
    env.project_root     = fs::path{"."};
    env.project_writable = true;
    const scope::Layout& layout = ::agentty::config::kCommandsLayout;
    for (const scope::Source& src : scope::plan(layout, env)) {
        scan_root(src.base / layout.leaf,
                  std::string{scope::to_string(src.locus)}, fresh, sig);
    }
    auto now = cache().update(
        [](const Cache& c, const std::string& s, const std::vector<Command>& f) {
            if (s == c.sig) return c;
            Cache next;
            next.commands = f;
            next.sig      = s;
            return next;
        },
        std::move(sig), std::move(fresh));
    return now->commands;
}

std::optional<Command> find(Io io, std::string_view name) {
    for (auto& c : all(io))
        if (c.name == name) return std::move(c);
    return std::nullopt;
}

void invalidate_cache() {
    cache().update([](const Cache& c) {
        Cache next = c;
        next.sig   = "\x01invalidated";
        return next;
    });
}

std::string expand(std::string_view body, std::string_view args) {
    // 空白空间 -split 一次位置。
    std::vector<std::string> pos;
    {
        std::size_t i = 0;
        while (i < args.size()) {
            while (i < args.size() &&
                   std::isspace(static_cast<unsigned char>(args[i]))) ++i;
            std::size_t b = i;
            while (i < args.size() &&
                   !std::isspace(static_cast<unsigned char>(args[i]))) ++i;
            if (i > b) pos.emplace_back(args.substr(b, i - b));
        }
    }
    const std::string all_args = trim(args);

    std::string out;
    out.reserve(body.size() + all_args.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '$') { out += body[i]; continue; }
        // `$` 结束 正文 :字义.
        if (i + 1 >= body.size()) { out += '$'; continue; }
        const char c = body[i + 1];
        if (c == '$') { out += '$'; ++i; continue; }          // $$ → $
        if (c >= '1' && c <= '9') {                           // $1..$9
            const std::size_t idx = static_cast<std::size_t>(c - '1');
            if (idx < pos.size()) out += pos[idx];
            ++i;
            continue;
        }
        constexpr std::string_view kArgs = "ARGUMENTS";
        if (body.compare(i + 1, kArgs.size(), kArgs) == 0) {  // $ARGUMENTS
            out += all_args;
            i += kArgs.size();
            continue;
        }
        out += '$';                                           // unknown: literal
    }
    return out;
}

std::optional<std::string> try_expand(Io io, std::string_view text) {
    if (text.empty() || text[0] != '/') return std::nullopt;
    return try_expand(text, all(io));
}

std::optional<std::string> try_expand(std::string_view text, const std::vector<Command>& known) {
    // 形状: 起始 '/' ,然后是命令名称(没有白空),
    // 然后是可选的白空 + 参数。在前导引白空 '/'
    // 表示是传教,不是命令.
    if (text.empty() || text[0] != '/') return std::nullopt;
    std::size_t name_end = 1;
    while (name_end < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[name_end])))
        ++name_end;
    if (name_end == 1) return std::nullopt;   // 赤色( 浅色) "/"
    const std::string_view name = text.substr(1, name_end - 1);

    const auto cmd = std::ranges::find(known, name, &Command::name);
    if (cmd == known.end()) return std::nullopt;   // /etc/hosts,/未知  ⁇   ⁇  纯文本

    std::string_view args =
        name_end < text.size() ? text.substr(name_end + 1) : std::string_view{};
    return expand(cmd->body, args);
}

} // namespace agentty::tools::commands
