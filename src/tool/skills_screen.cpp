// agentty::tools::skills ——不可信) . -text 进行消毒和注射检查。
//
// 研究结论及其对本实现的影响。
//
// 2026年文献的三项发现 驱使了这个文件:
//
// 1. Snyk(2026年2月,3 984名来自 ClawHub/skills.sh : 13.4%携带a
//    CRITICAL 问题; -malicious 设定,
//    100%包含恶意代码,91%包含恶意代码 ALSO
//    使用过的 提示词 注射。组合是点——注射基数使剂接受
//    载荷 否则,它的安全训练会拒绝。
//
// 2. arXiv 2602.06547(98 380个技能,157个行为得到证实):攻击
//    平均4.03个脆弱性,中间值为3个杀-chain阶段。
//    "Shadow features" ——文献中
//    缺少的能力——在基本攻击中出现0%,在高级攻击中
//    出现100%。 LEAST 可靠的信号可用。
//
// 3. Anthropic (2026年3月): 陈 ⁇ . Claude 代码用户批准93% 权限
//    提示词 Akhawe & Felt 测量了在 Chrome 上的
//    70% 点击率 SSL 警示 制服 提示词 训练用户将其开除。
//
// (3) 为何此文件存在。 如果每次安装都显示相同的屏幕,
// 屏幕就是装饰。 筛选让常见的大小写保持安静,
// 并让罕见的 危险大小写外观。 不同 ——多形态警告是习惯文献
// 中发现的持久引起注意的一种干预(Anderson等人,
// 第18段)。 BYU , fMRI + 实地研究).
//
// (2) 为何在“E”栏目上进行筛选。 BODY 永远不要信任
// 前题: 一种宣布 `effects: []` 然后管道卷成sh
// 就是阴影 -feature ,而错配本身就是发现.
//
// LIMITS,
// STATED HONESTLY 这是审查援助,是Regex
// -and-heuristics 文字上,所以可以用别名,包装脚本,
// 编码来逃避 载荷 ,而还没有人公布任何图案。Datadog自己对等的greps的指引:
// 将匹配作为审查线索,并将结果视为干净的 一个 证据
// agentty 措辞随即,从来没有说过 "safe".

#include "agentty/tool/skills.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::tools::skills {
namespace {

bool contains_ci(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    const auto it = std::search(
        hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return it != hay.end();
}

// Unicode 走私: bodi 覆盖和 0 -width
// 字符让技能 渲染 人类和人类的一件事 解析器。
// 记录在 arXiv 学习为模式 P2 (隐示书).
struct Smuggle { std::string_view utf8; std::string_view what; };
constexpr std::array<Smuggle, 9> kSmuggle{{
    {"\xE2\x80\xAE", "right-to-left override (U+202E)"},
    {"\xE2\x80\xAD", "left-to-right override (U+202D)"},
    {"\xE2\x80\xAA", "left-to-right embedding (U+202A)"},
    {"\xE2\x80\xAB", "right-to-left embedding (U+202B)"},
    {"\xE2\x81\xA6", "first-strong isolate (U+2066)"},
    {"\xE2\x80\x8B", "zero-width space (U+200B)"},
    {"\xE2\x80\x8C", "zero-width non-joiner (U+200C)"},
    {"\xE2\x80\x8D", "zero-width joiner (U+200D)"},
    {"\xEF\xBB\xBF", "zero-width no-break space (U+FEFF)"},
}};

void add(std::vector<Finding>& out, Finding::Severity sev,
         std::string code, std::string detail, int line) {
    // 每个代码一个发现——一个十二卷曲的技能 |
    // sh行是问题之一,不是十二行。 提示词 可读性,
    // 即存在全部理由筛选.
    for (const auto& f : out)
        if (f.code == code) return;
    out.push_back(Finding{sev, std::move(code), std::move(detail), line});
}

} // 命名空间结束。

std::string sanitize_author_text(std::string_view raw, std::size_t max_len) {
    std::string out;
    out.reserve(std::min(raw.size(), max_len + 1));

    std::size_t i = 0;
    bool last_was_space = false;
    while (i < raw.size() && out.size() < max_len) {
        // 直接放弃走私代码,而不是 渲染 他们。
        bool skipped = false;
        for (const auto& s : kSmuggle) {
            if (raw.compare(i, s.utf8.size(), s.utf8) == 0) {
                i += s.utf8.size();
                skipped = true;
                break;
            }
        }
        if (skipped) continue;

        const unsigned char c = static_cast<unsigned char>(raw[i]);

        // 新建行和控制字符崩溃到一个空格: 描述永远不能
        // 打开新字段 "line" 输入 提示词 并
        // 冒充 agentty 自己的框架。
        if (c < 0x20 || c == 0x7F) {
            if (!last_was_space && !out.empty()) { out += ' '; last_was_space = true; }
            ++i;
            continue;
        }

        // 复制 a 整个 UTF -8个序列或者没有 字节
        // -at-a-time 版本在盖子上分割多字节字符——描述
        // emoji或 CJK 切换为无效 字节 ,然后进入模型的
        // 系统 提示词 编辑 TUI . 验证:以10为上限的U+1F680运行,
        // 生成了一条正在铺设的铅 字节.
        std::size_t len = 1;
        if      ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else if (c >= 0x80)          { ++i; continue; }  // 偏差续作

        if (i + len > raw.size()) break;                 // 短调输入
        // 续 字节 输入必须实际是连续;如果不是,
        // 输入格式不正确,则输入 字节 改为删除而不是复制。
        bool well_formed = true;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(raw[i + k]) & 0xC0) != 0x80)
                well_formed = false;
        if (!well_formed) { ++i; continue; }

        // 停下来 之前 盖子而不是横跨它。
        if (out.size() + len > max_len) break;

        last_was_space = (len == 1 && c == ' ');
        out.append(raw, i, len);
        i += len;
    }

    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (i < raw.size()) out += "\xE2\x80\xA6";     // 被截断了,然后说
    return out;
}

std::vector<Finding> screen_body(std::string_view body) {
    std::vector<Finding> out;
    using S = Finding::Severity;

    // 全文 -file 检查第一(第0行)。
    for (const auto& s : kSmuggle) {
        if (body.find(s.utf8) != std::string_view::npos) {
            add(out, S::Critical, "unicode-smuggling",
                std::string{"contains "} + std::string{s.what} +
                " — text that renders differently than it parses", 0);
            break;
        }
    }

    // 行线 -wise 检查 便宜 给用户一个线号可以跳转到
    int line = 0;
    std::size_t pos = 0;
    while (pos <= body.size()) {
        const auto nl = body.find('\n', pos);
        const auto l = body.substr(pos, (nl == std::string_view::npos ? body.size() : nl) - pos);
        ++line;

        // ── 远程代码执行( Snyk SC2, Datadog grep # 1) Name ──
        if ((contains_ci(l, "curl") || contains_ci(l, "wget")) &&
            (contains_ci(l, "| sh") || contains_ci(l, "|sh") ||
             contains_ci(l, "| bash") || contains_ci(l, "|bash") ||
             contains_ci(l, "| source") || contains_ci(l, "iex"))) {
            add(out, S::Critical, "curl-pipe-shell",
                "downloads a script and pipes it straight into a shell — "
                "the code that runs is whatever the server sends today", line);
        }

        // ── 死刑(Snyk SC3) ──
        if ((contains_ci(l, "base64 -d") || contains_ci(l, "base64 --decode") ||
             contains_ci(l, "atob(") || contains_ci(l, "from_base64")) &&
            (contains_ci(l, "eval") || contains_ci(l, "exec") ||
             contains_ci(l, "| sh") || contains_ci(l, "| bash"))) {
            add(out, S::Critical, "obfuscated-exec",
                "decodes base64 and executes the result — the real command "
                "is hidden from anyone reading this file", line);
        }

        // ── 凭据 访问( Snyk) E2, arXiv 凭据 进入阶段) ──
        if (contains_ci(l, "gh auth token") || contains_ci(l, "~/.aws/credentials") ||
            contains_ci(l, ".ssh/id_") || contains_ci(l, "~/.netrc") ||
            contains_ci(l, "GITHUB_TOKEN") || contains_ci(l, "AWS_SECRET") ||
            contains_ci(l, "printenv") || contains_ci(l, "process.env") ||
            contains_ci(l, "os.environ")) {
            add(out, S::Warn, "reads-credentials",
                "reads credentials or environment secrets", line);
        }

        // ── 过滤:凭据 访问+以一种技能外出 ──
        if ((contains_ci(l, "curl") || contains_ci(l, "wget") ||
             contains_ci(l, "fetch(") || contains_ci(l, "requests.post")) &&
            (contains_ci(l, "-d ") || contains_ci(l, "--data") ||
             contains_ci(l, "POST"))) {
            add(out, S::Warn, "posts-data-outbound",
                "sends data to a remote endpoint", line);
        }

        // ── 指令覆盖( E) arXiv P1 斯奈克语 提示词-injection) ──
        if (contains_ci(l, "ignore previous instruction") ||
            contains_ci(l, "ignore all previous") ||
            contains_ci(l, "disregard the above") ||
            contains_ci(l, "developer mode") ||
            contains_ci(l, "you are now") ||
            contains_ci(l, "system:") ||
            contains_ci(l, "security warnings are")) {
            add(out, S::Critical, "instruction-override",
                "tries to override the agent's own instructions — a skill "
                "should describe a task, not reprogram the agent", line);
        }

        // ── 隐藏指令 HTML 评论( arXiv P2) ──
        if (contains_ci(l, "<!--") &&
            (contains_ci(l, "instruction") || contains_ci(l, "ignore") ||
             contains_ci(l, "secret") || contains_ci(l, "token") ||
             contains_ci(l, "do not tell") || contains_ci(l, "don't tell"))) {
            add(out, S::Critical, "hidden-directive",
                "puts instructions inside an HTML comment — invisible when "
                "the file is rendered, visible to the agent", line);
        }

        // ── 用户隐瞒(arXiv Defence Evasion) ──
        if (contains_ci(l, "do not tell the user") ||
            contains_ci(l, "don't tell the user") ||
            contains_ci(l, "without informing") ||
            contains_ci(l, "silently") ||
            contains_ci(l, "without mentioning")) {
            add(out, S::Critical, "conceal-from-user",
                "instructs the agent to hide what it is doing from you", line);
        }

        // ── 持久化 (arXiv 影响阶段) ──
        if (contains_ci(l, "crontab") || contains_ci(l, "systemctl enable") ||
            contains_ci(l, "launchctl load") ||
            contains_ci(l, "authorized_keys") ||
            contains_ci(l, ">> ~/.bashrc") || contains_ci(l, ">> ~/.zshrc") ||
            contains_ci(l, ">> ~/.profile")) {
            add(out, S::Critical, "persistence",
                "installs something that keeps running after this session — "
                "cron, a service, an SSH key, or a shell profile edit", line);
        }

        // ── 破坏性 ──
        if (contains_ci(l, "rm -rf /") || contains_ci(l, "rm -rf ~") ||
            contains_ci(l, "git push --force") || contains_ci(l, "git push -f") ||
            contains_ci(l, "DROP TABLE") || contains_ci(l, "mkfs")) {
            add(out, S::Warn, "destructive",
                "contains an irreversible or destructive command", line);
        }

        // ── 密码 -protected 档案( Snyk : AV/scanner 逃避) ──
        if ((contains_ci(l, "unzip") || contains_ci(l, "7z")) &&
            (contains_ci(l, "-P ") || contains_ci(l, "-p"))) {
            add(out, S::Warn, "encrypted-archive",
                "extracts a password-protected archive — a known way to get "
                "a payload past scanners", line);
        }

        // ── 宽前-approval( Datadog grep # 2) Name ──
        if (contains_ci(l, "allowed-tools") &&
            (contains_ci(l, "Bash(*)") || contains_ci(l, "bash(*)"))) {
            add(out, S::Critical, "blanket-shell",
                "asks to pre-approve unrestricted shell access", line);
        }

        // agentty 不实现 ! 动态上下文。如果技能已经
        // 书面期待(从 Claude 用户应该
        // 知道这些行是惰性的,而且技能是用来制作工具的,
        // 可以运行 之前 任何审查。
        if (l.find("!`") != std::string_view::npos) {
            add(out, S::Note, "dynamic-context",
                "uses Claude Code's !`cmd` dynamic context, which agentty "
                "does not execute — these lines are inert here", line);
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    std::ranges::stable_sort(out, [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return a.severity > b.severity;
        return a.line < b.line;
    });
    return out;
}

} // namespace agentty::tools::skills
