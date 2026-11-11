// agentty::tools::skills - 电话 `agentty skill` CLI 还有批准书店
//
// 原因 此 EXISTS, 和 原因 IT ISN'T A SETUP SCREEN
//
// 技能是指示,而不是沙盒编码。大多数是传言( "house style for docs"
// 并且除了钱之外,没有付出什么代价 目录。有人告诉代理运行程序, 访问网络,
// 并写入 凭据 输入用户的 仓库 ——和 该 和项目一样的威胁等级
// -local MCP 服务器, 哪个 agentty 已解决 :
// scope::Trust , 核准额以 CONTENT 永远不要给
// 名字(MCPoison的课, CVE-2025-54136).
//
// 所以一个能力进入agentty 完全一种方式:用户命名它.
//
//     agentty skill add github.com/user/repo
//
// 同样的指令是销售商的技能,本地目录和公司的内部技能。
// -run 屏幕,没有菜单条目,没有捆绑的默认——因为
// 一旦一个供应商得到安置,下一个供应商就没有原则性答案,
// 设置流程就成了一个好的广告牌 PR 一次是,
// 我和他在一起的时候
//
// 该 LOAD-BEARING PART: agentty 写公开,而不是技能。
//
// 同意书 提示词 谓 RENDERED 技术的申报 `effects:`
// 财务报告和已审计财务报表 `source:` 技能作者不能写出
// 他们自己的令人放心的总结——他们只能宣布能力,以及 agentty
// 将声明变成简单的句子。 -declares 获得一个
// 提示词 下边 -sells 但它也无法到达 LIE 输入 提示词 ,
// 这是值得设计出来的失败模式。
//
// 功劳:前题 -declared-capability 想法来自
// PR 47号(阿拉格·阿格拉瓦尔,Cohessivity).
// PR 提出捆绑有效技能,并附有第一个 -run 屏幕; 位置是什么
// agentty 却没有词汇来形容
// 此技能将执行程序并访问网络。
// 没有安装路径来对待每个源。

#include "agentty/tool/skills.hpp"

#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // kSkills 核准——一个名称
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

#include <nlohmann/json.hpp>

// 库存和技能 头文件 两者都存在,
// 因为 skills.hpp 被不得将库存运入的
// 呼叫者消耗;这使他们无法漂移。
static_assert(agentty::tools::skills::kApprovalsLeaf
                  == agentty::config::kSkillsApprovals,
              "skills approvals leaf must match the inventory");

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace agentty::tools::skills {
namespace {

// ── 核准储存 ────────────────────────────────────────────────────────

// 有点不妥 终端? agentty 船舶 Windows 也这样,所以这不能只是 isatty()
// 发件人 <unistd.h>.
[[nodiscard]] bool stdin_is_tty() noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

// 使用一个尺寸限制 两者 预选 -install 检查和复制 有
// 两个不同的限制是 add/list 最后,对于是否存在技能,
// 人们意见不一。
constexpr std::uintmax_t kMaxSkillFile = 2u * 1024 * 1024;   // 2 MB
constexpr std::uintmax_t kMaxSkillTotal = 8u * 1024 * 1024;  // 8 MB
constexpr int            kMaxSkillFiles = 128;

// 技术 `name:` 作者为作者 -controlled ,
// 和 `skill add` 构建一个 INSTALL PATH 退出它。
// 组合是一个文件系统 -write 原始的,除非名字被限制,它是一个技能宣告
//
//   name: ../../../../tmp/PWNED
//
// 安装到 ~/.agentty/skills/../../../../tmp/PWNED
// ——在技能根之外,从一种从未有过的传言技能. 提示词。绝对名称( =
// `name: /etc/cron.d/x` )完全忽略了根.
//
// 所以: 一个路径组件, 没有分隔符, 没有点 -only
// 保守的字符集,其他的都拒绝 BY NAME 而不是沉浸在别的
// 东西中——默默地安装 "foo" 当文件说
// "../../foo" 这将是它自己的惊喜。
[[nodiscard]] bool safe_component_impl(std::string_view n) {
    if (n.empty() || n.size() > 64) return false;
    if (n == "." || n == "..") return false;
    if (n.front() == '.' || n.front() == '-') return false;  // 没有点文件,没有 标志-lookalikes
    for (const char c : n) {
        const auto u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
                     || (u >= '0' && u <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;   // 拒绝 '/', '\\', NUL 空间, UTF-8
    }
    return true;
}

// 带和带:即使有安全组件,在写前,
// 也要在技能根内真实地验证已解决的目的地。
// 捕捉字符集检查遗漏的任何东西(连结根,
// 奇数平台语义) — 相同的封存检查工作区边界使用。
[[nodiscard]] bool inside_impl(const fs::path& root, const fs::path& child) {
    std::error_code ec;
    const auto r = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const auto c = fs::weakly_canonical(child, ec);
    if (ec) return false;
    auto ri = r.begin(), rend = r.end();
    auto ci = c.begin(), cend = c.end();
    for (; ri != rend; ++ri, ++ci) {
        if (ci == cend || *ci != *ri) return false;
    }
    return true;
}

// 说明a SKILL.md 在安装前, 使用 该 解析
// 器 而不是第二次,这有它自己的迷你 -parser
// 并立即分化: `description: |` 被当做
// 赤裸裸的 "|" 同意时 提示词 之后装入精细,
// 所以用户批准了他们从未实际看到过的描述.
std::optional<Skill> peek_skill(const fs::path& md) {
    // Size-check BEFORE读. A 10 MB
    // SKILL.md曾经被解析和筛选(~2s 用户等待的工作没有输出),
    // 然后安装——而LOADER默默地放下了超大小的文件,
    // 因此`skill list`于是说
    // "no skills installed"和add/list相互矛盾. 拒绝早,
    // 同样限制副本执行,所以有一个答案.
    std::error_code ec;
    const auto sz = fs::file_size(md, ec);
    if (!ec && sz > kMaxSkillFile) return std::nullopt;

    std::ifstream in(md, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    auto s = parse_skill_text(ss.str(),
                             md.parent_path().filename().string(),
                             "user");
    if (s.name.empty()) return std::nullopt;
    return s;
}

// ── 副作用 − 普通句子 ──────────────────────────────────────────────
// 每个声明位点一行,按位顺序排列,这样 提示词 运行之间
// 永远不要重排顺序。 AGENT 将受劝戒,而不教他如何做;
// "can" 做——技能不能做任何事情;它说服人.
struct EffectLine {
    std::string_view label;
    std::string_view gloss;
};

std::vector<EffectLine> effect_lines(EffectSet e) {
    std::vector<EffectLine> out;
    if (e.has(Effect::ReadFs))
        out.push_back({"read files",        "open and scan paths on this machine"});
    if (e.has(Effect::WriteFs))
        out.push_back({"write files",       "create or modify files that outlive the run"});
    if (e.has(Effect::Net))
        out.push_back({"reach the network", "send and receive data over the internet"});
    if (e.has(Effect::Exec))
        out.push_back({"execute programs",  "run commands, including fetched packages"});
    return out;
}

// 同意屏幕。 这里所有的东西都是由它衍生出来的: 技能贡献
// 它的名称、描述、来源和声明 副作用 - 从来没有一句话,
// 它的描述是: SANITISED 在显示之前( 已确认的
// 恶意技能的91% 携带注射; 能够打开新线的描述可以冒充)
// agentty 自己的框架。
//
// 屏幕是故意的NOT统一. Anthropic在
// Claude代码权限提示词上测得93%的认可率;
// 每次有人学会解雇时屏幕看起来都是一样的。 因此,
// 清真传教技能得到一个安静的2-line音符,
// 干净的功效技能得到常规的提示词, 和发现的技能得到
// 一个视觉不同的屏幕, 默认的答案被翻转。 区别是习惯文献
// 唯一认为持久的干预。
void print_consent(const Skill& s, const std::vector<Finding>& findings) {
    const bool critical = std::ranges::any_of(findings, [](const Finding& f) {
        return f.severity == Finding::Severity::Critical;
    });

    std::printf("\n");
    if (critical) {
        // 不同的形状,不同的词,不同的默认。
        // 一个已经批准20个技能的用户应该FEEL认为这个不是那些。
        std::printf("  ━━━ REVIEW THIS ONE ━━━\n\n");
    }

    std::printf("  install skill \"%s\"",
                sanitize_author_text(s.name, 64).c_str());
    if (!s.origin.empty())
        std::printf(" from %s", sanitize_author_text(s.origin, 96).c_str());
    std::printf("?\n\n");

    if (!s.description.empty())
        std::printf("  %s\n\n", sanitize_author_text(s.description).c_str());

    const auto lines = effect_lines(s.effects);
    if (lines.empty()) {
        std::printf("  declares no effects — prose only.\n");
    } else {
        std::printf("  the agent will be told it may:\n");
        for (const auto& l : lines)
            std::printf("    %-18s %s\n",
                        std::string{l.label}.c_str(),
                        std::string{l.gloss}.c_str());
    }
    std::printf("\n");

    // 调查结果,如果有的话。 agentty 说到它所看到
    // 的 正文 ——本节中技艺无发言权.
    if (!findings.empty()) {
        std::printf("  reading the file, agentty noticed:\n");
        for (const auto& f : findings) {
            const char* mark = f.severity == Finding::Severity::Critical ? "!!"
                             : f.severity == Finding::Severity::Warn     ? " !"
                                                                         : "  ";
            if (f.line > 0)
                std::printf("   %s line %-4d %s\n", mark, f.line, f.detail.c_str());
            else
                std::printf("   %s           %s\n", mark, f.detail.c_str());
        }
        std::printf("\n");

        // 诚实的警告 Snyk自己的扫描仪漏掉了东西 说
        // "scanned, looks fine"是有害的句子
        std::printf("  this is pattern matching over text, not proof of intent —\n");
        std::printf("  and a skill with no findings has not been proven safe.\n\n");
    }

    // 判决最需要,最不期望
    std::printf("  skills are instructions, not sandboxed code. agentty cannot\n");
    std::printf("  enforce what is written above — it is what the skill declared.\n");
    if (!s.origin.empty())
        std::printf("  you did not write these instructions.\n");
    std::printf("\n");
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// `skill add`安装到的地方:用户根,
// 所以每工作区中都可以使用,一个克隆的仓库不能覆盖它.
fs::path install_dir_for(std::string_view name) {
    return util::user_root() / "skills" / std::string{name};
}

// 技能目录是数据,不是 源码树 复制到 `recursive`
// 后面的符号: a `dir-escape -> /tmp`
// 共鸣让复制品走进/tmp,
// 和 a `leak.txt -> /etc/passwd` 共鸣会复制目标
// CONTENT 输入用户的技能目录。技能安装也不应该完成。
//
// 只复制纯文件,一个级的真子目录,完全跳过共鸣,
// 并封顶总——一个技能是文本加上几个资源.
struct CopyResult {
    bool ok = false;
    std::string error;
    int skipped_links = 0;
};

CopyResult copy_skill_tree(const fs::path& from, const fs::path& to) {
    CopyResult out;
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) { out.error = ec.message(); return out; }

    std::uintmax_t total = 0;
    int files = 0;

    // follow_directory_symlink 谓 OFF ,
    // 每个条目都是 -checked 与 is_symlink ()因为递归延绳仍然可以
    // 交回 链接 输入本身。
    fs::recursive_directory_iterator it(
        from, fs::directory_options::skip_permission_denied, ec);
    if (ec) { out.error = ec.message(); return out; }

    for (const auto& e : it) {
        const auto rel = fs::relative(e.path(), from, ec);
        if (ec) continue;

        if (fs::is_symlink(e.symlink_status())) { ++out.skipped_links; continue; }

        if (e.is_directory()) {
            fs::create_directories(to / rel, ec);
            continue;
        }
        if (!e.is_regular_file()) continue;     // fifo, 套接字, 设备: 不是数据

        const auto sz = e.file_size(ec);
        if (ec) continue;
        if (sz > kMaxSkillFile) {
            out.error = rel.string() + " is " + std::to_string(sz / 1024)
                      + " KB — over the 2 MB per-file limit";
            return out;
        }
        total += sz;
        if (++files > kMaxSkillFiles || total > kMaxSkillTotal) {
            out.error = "skill is too large (limit: 128 files, 8 MB total)";
            return out;
        }

        fs::create_directories((to / rel).parent_path(), ec);
        fs::copy_file(e.path(), to / rel,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            out.error = "copying " + rel.string() + ": " + ec.message();
            return out;
        }
    }

    out.ok = true;
    return out;
}

// ── 添加( E) ────────────────────────────────────────────────────────────────────

int verb_add(Io io, const maya::unlocked& cap, const std::vector<std::string>& argv) {
    if (argv.empty()) {
        std::fprintf(stderr, "usage: agentty skill add <github.com/user/repo | ./dir>\n");
        return 2;
    }
    const std::string src = argv[0];
    const bool force = std::ranges::find(argv, std::string{"--force"}) != argv.end();
    const bool yes   = std::ranges::find(argv, std::string{"--yes"})   != argv.end();

    // 本地目录: 复制它。 远程: 未在此获取...
    // agentty 网络在内部获取 CLI 动词
    // 要自己的设计(检查出针, 缓存 ,离线).
    // 在这里宣布了形状是正确的;远程臂是下一个承诺
    // 的工作。
    fs::path from = fs::path{src};
    if (!fs::exists(from / "SKILL.md")) {
        if (src.starts_with("github.com/") || src.starts_with("https://")) {
            std::fprintf(stderr,
                "remote fetch isn't wired yet. clone it and add the path:\n"
                "  git clone https://%s /tmp/skill && agentty skill add /tmp/skill\n",
                src.starts_with("github.com/") ? src.c_str() : src.c_str());
            return 1;
        }
        std::fprintf(stderr, "no SKILL.md under %s\n", from.string().c_str());
        return 1;
    }

    // 解析 BEFORE安装,所以同意的提示词描述的是
    // 真实文件而不是对此的承诺.
    const auto parsed = peek_skill(from / "SKILL.md");
    if (!parsed) {
        std::error_code sz_ec;
        const auto sz = fs::file_size(from / "SKILL.md", sz_ec);
        if (!sz_ec && sz > kMaxSkillFile) {
            std::fprintf(stderr,
                "refusing to install: SKILL.md is %ju KB, over the 2 MB limit.\n"
                "a skill is instructions — if it needs more than that, it "
                "probably wants to be a plugin.\n",
                static_cast<std::uintmax_t>(sz / 1024));
            return 1;
        }
        std::fprintf(stderr, "could not parse %s\n",
                     (from / "SKILL.md").string().c_str());
        return 1;
    }
    Skill s = *parsed;

    // 空或无体 SKILL.md 安装干净,
    // 然后什么都不做——它占据着 目录 每回合都出现一个不
    // 说话的插槽和符号。几乎总是错误(错误路径、短路下载)。
    if (s.body.empty() && s.description.empty()) {
        std::fprintf(stderr,
            "refusing to install: %s has no body and no description.\n"
            "an empty skill costs catalog space and tokens for nothing — "
            "check the path?\n",
            (from / "SKILL.md").string().c_str());
        return 1;
    }

    if (s.origin.empty() && (src.starts_with("github.com/")
                          || src.starts_with("https://")))
        s.origin = src;

    // 名字变成一个 PATH 拒绝任何不是普通部件的东西,
    // 然后将其用于一个部件——见 safe_component().
    if (!safe_skill_name(s.name)) {
        std::fprintf(stderr,
            "refusing to install: the skill's name is not a plain directory\n"
            "name (letters, digits, - and _). it said:\n  %s\n",
            sanitize_author_text(s.name, 120).c_str());
        return 1;
    }

    const auto dest = install_dir_for(s.name);

    // 第二,独立检查:解析目的地必须实际在技能根下.
    // 便宜,它不依赖上面的字符集
    // 规则是详尽的.
    const auto skills_root = util::user_root() / "skills";
    if (!path_inside(skills_root, dest)) {
        std::fprintf(stderr,
            "refusing to install: destination escapes %s\n",
            skills_root.string().c_str());
        return 1;
    }

    // 安装另一个目录已经声称的名称将创造出一种技能,
    // 它从不默默地 加载 —— 用户会看到一个成功的消息,
    // 然后再也不会看到技能。 拒绝使用相互冲突的路径,
    // 而不是写一个无所事事的文件。
    if (const auto clash = find(IoAccess::grant(), s.name);
        clash && !clash->dir.empty()
              && fs::weakly_canonical(clash->dir) != fs::weakly_canonical(dest)) {
        std::fprintf(stderr,
            "refusing to install: \"%s\" is already the name of a skill in\n"
            "  %s\n"
            "installing this one would be shadowed and never load. rename it,\n"
            "or remove the other first.\n",
            sanitize_author_text(s.name, 64).c_str(),
            clash->dir.string().c_str());
        return 1;
    }

    if (fs::exists(dest / "SKILL.md") && !force) {
        // 绝不要对用户编辑过的技能进行划痕。 (此规则来自 PR #47,
        // 因为它得到了正确的并测试了它。)
        std::fprintf(stderr,
            "skill \"%s\" already installed at %s\n"
            "re-run with --force to overwrite\n",
            s.name.c_str(), dest.string().c_str());
        return 1;
    }

    // 屏幕显示 BODY 不是头条,是 arXiv 100%的
    // 高级攻击中发现影子特征(密码中存在能力,文件中没有这种
    // 能力)——因此,技能本身的叙述是现有最不可靠的输入,
    // a declares-nothing/does-everything
    // 错配本身就是信号.
    const auto findings = screen_body(s.body);
    const bool critical = std::ranges::any_of(findings, [](const Finding& f) {
        return f.severity == Finding::Severity::Critical;
    });

    print_consent(s, findings);

    // --yes 用于脚本,它停留在关键的结论。
    // 标志 静静地安装一些带指令的东西 -override
    // 或 单 持久化 载荷 是那个 标志 代表用户
    // 进行伤害; 使自动化明确表明这一点。
    if (critical && yes) {
        std::fprintf(stderr,
            "refusing --yes: this skill has critical findings. install it\n"
            "interactively, or re-run with --i-have-read-this if you have.\n");
        const bool read_it = std::ranges::find(
            argv, std::string{"--i-have-read-this"}) != argv.end();
        if (!read_it) return 1;
    }

    if ((needs_trust_gate(s.effects) || critical) && !yes) {
        // 没有 TTY 意思是没人能回答 打印一个 提示词 进入
        // 管道和读取 EOF 安装后用于退出 0 无内容 -
        // 沉默不语 -op 报告成功,其中 CI 读作
        // "the skill is there" 直到它没有。
        if (!stdin_is_tty()) {
            std::fprintf(stderr,
                "this skill needs approval and there is no terminal to ask.\n"
                "run it interactively, or pass --yes if you have already "
                "reviewed it.\n");
            return 1;
        }

        // 发现会翻转默认值。 常规安装铅 与 [1] 安装;
        // 标记的一 读取文件的导线, 安全答案是您
        // 眼球着陆的第一件事。
        if (critical)
            std::printf("  [2] print it first   [3] cancel   [1] install anyway\n\n> ");
        else
            std::printf("  [1] install   [2] print it first   [3] cancel\n\n> ");
        std::fflush(stdout);
        std::string answer;
        if (!std::getline(std::cin, answer)) return 1;
        if (answer == "2") {
            std::printf("\n%s\n", read_file(from / "SKILL.md").c_str());
            std::printf("  [1] install   [3] cancel\n\n> ");
            std::fflush(stdout);
            if (!std::getline(std::cin, answer)) return 1;
        }
        if (answer != "1") {
            std::printf("cancelled — nothing written\n");
            return 1;
        }
    }

    // 只复制纯文件——无符号链接,无设备节点,大小-capped.
    // 这曾经是 fs::copy 后继的(复验)
    // `dir-escape -> /tmp` 连接到/tmp,当它出错时,
    // 留下了一半 -populated 目录中仍然打印成功并退出 0。
    const auto copied = copy_skill_tree(from, dest);
    if (!copied.ok) {
        std::fprintf(stderr, "install failed: %s\n", copied.error.c_str());
        std::error_code rm;
        fs::remove_all(dest, rm);   // 别留下半个 -skill 后面
        return 1;
    }

    // 一种技术,他 SKILL.md 检查一个文件,
    // 它会变成一个沉默的半个文件 -failure
    // 进入用户可以操作的错误。
    if (!fs::exists(dest / "SKILL.md")) {
        std::fprintf(stderr, "install failed: SKILL.md did not copy\n");
        std::error_code rm;
        fs::remove_all(dest, rm);
        return 1;
    }

    if (copied.skipped_links > 0)
        std::printf("skipped %d symlink%s — skills are copied as plain files\n",
                    copied.skipped_links,
                    copied.skipped_links == 1 ? "" : "s");

    // 批准我们刚才展示的 关键是批准 技术作为 LOADER
    // 解析 关上它,不要关上它 peek_skill 更
    // 便宜的读物,两个修剪 正文 不同的, 和不匹配 散列
    // 计算 加载 时间比没有更糟糕: 用户回答 a 提示词
    // 和技能仍然显示 PENDING 永远
    if (needs_trust_gate(s.effects)) {
        // all() 内容 -scans 当根的 mtime 签名发生变化时,
        // 上面的复制刚刚做了这个—— 所以 find() 查看新安装的文件。
        const auto installed = find(IoAccess::grant(), s.name);
        if (!installed) {
            std::fprintf(stderr,
                "installed, but %s did not load — run `agentty skills` to see why\n",
                s.name.c_str());
            return 1;
        }
        auto store = load_approvals(io);
        store.approve(content_sha_of(*installed));
        save_approvals(io, cap, store);
    }

    std::printf("installed %s → %s\n", s.name.c_str(), dest.string().c_str());
    if (needs_trust_gate(s.effects))
        std::printf("approved this version; editing it will ask again\n");
    return 0;
}

// ── 列表 ───────────────────────────────────────────────────────────────────

int verb_list() {
    const auto all_skills = all(IoAccess::grant());
    if (all_skills.empty()) {
        std::printf("no skills installed\n");
        return 0;
    }
    const auto store = load_approvals(IoAccess::grant());
    for (const auto& s : all_skills) {
        const char* trust = "-";
        if (needs_trust_gate(s.effects)) {
            const auto t = trust_of(s, store);
            trust = std::holds_alternative<scope::Trusted>(t) ? "approved"
                  : std::holds_alternative<scope::Pending>(t) ? "PENDING"
                                                              : "blocked";
        }
        std::printf("%-24s %-8s %-10s %s\n",
                    s.name.c_str(), s.source.c_str(), trust,
                    effects_to_frontmatter(s.effects).c_str());
        if (!s.description.empty())
            std::printf("    %s\n", s.description.c_str());
        if (!s.origin.empty())
            std::printf("    from %s\n", s.origin.c_str());
    }

    // 名称碰撞。失去一个技能的技巧在其它任何地方都看不见
    // ——不是在本列表中,也不是在面板中,
    // 也不是到`approve`——所以,如果从未提及过,
    // 用户就无法发现磁盘上有些东西在戴着另一个技能答案。
    const auto shadows = shadowed(IoAccess::grant());
    if (!shadows.empty()) {
        std::printf("\n");
        for (const auto& sh : shadows) {
            const bool same_scope = shadowed_within_scope(IoAccess::grant(), sh.name);
            // 十字记 -scope 是一个记录的超音速并获得一个静音符;
            // 相同 -scope 是个真正的问题,所以说
            std::printf("%s \"%s\" is also declared by %s%s\n",
                        same_scope ? "!!" : "  ",
                        sanitize_author_text(sh.name, 64).c_str(),
                        sh.dir.c_str(),
                        same_scope ? "" : " (shadowed by a narrower scope)");
            std::printf("   that copy is NOT loaded");
            if (!sh.effects.empty())
                std::printf(" and it declares %s",
                            effects_to_frontmatter(sh.effects).c_str());
            std::printf("\n");
        }
    }
    return 0;
}

// ── 删除 ─────────────────────────────────────────────────────────────────

int verb_remove(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        std::fprintf(stderr, "usage: agentty skill remove <name>\n");
        return 2;
    }
    if (!safe_skill_name(argv[0])) {
        std::fprintf(stderr, "not a skill name: %s\n",
                     sanitize_author_text(argv[0], 120).c_str());
        return 1;
    }

    // `list` 显示 FRONTMATTER 名称;
    // 目录可能被称为其他东西。只有目录删除,用户才能看到
    // "dangerous" 列表中包含且 `remove dangerous`
    // 失败 —— 无法解开它们面前的设置。 先尝试目录,
    // 然后用已声明的名称解决。
    auto dest = install_dir_for(argv[0]);
    if (!fs::exists(dest)) {
        if (const auto s = find(IoAccess::grant(), argv[0]);
            s && !s->dir.empty() && s->source == "user") {
            dest = s->dir;
        }
    }

    const auto skills_root = util::user_root() / "skills";
    if (!path_inside(skills_root, dest)) {
        std::fprintf(stderr, "refusing: path escapes %s\n",
                     skills_root.string().c_str());
        return 1;
    }
    if (!fs::exists(dest)) {
        std::fprintf(stderr, "not installed under %s: %s\n",
                     util::user_root().string().c_str(), argv[0].c_str());
        return 1;
    }
    std::error_code ec;
    fs::remove_all(dest, ec);
    if (ec) {
        std::fprintf(stderr, "remove failed: %s\n", ec.message().c_str());
        return 1;
    }
    std::printf("removed %s\n", argv[0].c_str());
    std::printf("  %s\n", dest.string().c_str());
    return 0;
}

// ── 核准 ────────────────────────────────────────────────────────────────

int verb_approve(Io io, const maya::unlocked& cap, const std::vector<std::string>& argv) {
    if (argv.empty()) {
        std::fprintf(stderr, "usage: agentty skill approve <name>\n");
        return 2;
    }
    const auto s = find(IoAccess::grant(), argv[0]);
    if (!s) {
        std::fprintf(stderr, "no such skill: %s\n", argv[0].c_str());
        return 1;
    }
    if (!needs_trust_gate(s->effects)) {
        std::printf("%s declares no effects — nothing to approve\n", argv[0].c_str());
        return 0;
    }
    print_consent(*s, screen_body(s->body));
    auto store = load_approvals(io);
    store.approve(content_sha_of(*s));
    save_approvals(io, cap, store);
    std::printf("approved %s\n", argv[0].c_str());
    return 0;
}

void usage() {
    std::printf(
        "usage: agentty skill <verb>\n\n"
        "  add <path> [--force] [--yes]   install a skill (shows what it declares)\n"
        "  list                           installed skills, effects, trust\n"
        "  remove <name>                  uninstall\n"
        "  approve <name>                 approve a pending skill after reading it\n\n"
        "a skill with no declared effects is prose and is never gated.\n");
}

} // 命名空间结束。

// ── 公开 ─────────────────────────────────────────────────────────────────

bool safe_skill_name(std::string_view name) noexcept {
    return safe_component_impl(name);
}

bool path_inside(const std::filesystem::path& root,
                 const std::filesystem::path& child) noexcept {
    return inside_impl(root, child);
}


scope::Approvals load_approvals(Io) {
    return scope::load_approvals(kApprovalsLeaf);
}

void save_approvals(Io io, const maya::unlocked& cap, const scope::Approvals& a) {
    (void)scope::save_approvals(io, cap, kApprovalsLeaf, a);
}

std::string content_sha_of(const Skill& s) {
    return scope::content_hash(s.body + "\n#effects:" + effects_to_frontmatter(s.effects));
}

scope::Trust trust_of(Io io, const Skill& s) noexcept {
    return trust_of(s, load_approvals(io));
}

int cli(Io io, const maya::unlocked& cap, const std::vector<std::string>& argv) {
    if (argv.empty()) { usage(); return 2; }
    const std::string verb = argv[0];
    const std::vector<std::string> tail(argv.begin() + 1, argv.end());

    if (verb == "add")     return verb_add(io, cap, tail);
    if (verb == "list")    return verb_list();
    if (verb == "remove")  return verb_remove(tail);
    if (verb == "approve") return verb_approve(io, cap, tail);

    usage();
    return 2;
}

} // namespace agentty::tools::skills
