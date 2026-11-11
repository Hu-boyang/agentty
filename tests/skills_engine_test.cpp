// skills_engine_test ——代理技能引擎
// 的正确性( 3) . agentskills.io 执行:
// 发现的跨越原生的+跨根,项目 -shadows-user 优先,
// 宽大前题 解析 (未引用的结肠, name/dir 匹配,
// 缺少描述),可选字段( 兼容性 / 允许) -tools /
// 禁用 -model-invocation ),第三级资源查点,
// 激活 载荷 形状,激活来源于可见的文字记录(包括 post-/compact
// 如果旧存储的集出错的话, 目录 过滤, 并读 -allowlist
// 打开门 `read` 获取捆绑的资源到外部 工作
// 区 写字门要严谨
//
// 战略:建立一个 沙箱 HOME + cwd 在一个临时目录下,
// 将过程指向它们( setenv) HOME + chdir),然后驱动真实的引擎.
// 引擎的车 缓存 已键入 root/file mtimes,
// 每一个测试阶段都写新文件, 所以交叉 -stage 只要阶段
// 使用不同的技能名称,污染就不可能发生。

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"   // Thread, visible_text
#include "agentty/tool/skills.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace agentty::tools;
using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using agentty::visible_text;



static void write_file_at(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

// 触摸根部,使引擎的mtime签名即使文件系统mtime颗粒性
// 粗糙时也会改变:写作一个blrand-newXXSKILL.md
// 在签名上添加了自己的mtime,这已经足够了.

TEST_CASE("skills engine") {
    agtest::ScopedEnvSandbox _env_guard;
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec) / "agentty_skills_test";
    fs::remove_all(base, ec);
    fs::path home = base / "home";
    fs::path work = base / "work";
    fs::create_directories(home);
    fs::create_directories(work);

#if defined(_WIN32)
    _putenv_s("HOME", home.string().c_str());
    // 用户-scope的基数为util::user_root(
    // $AGENTTY_HOME,回落至$HOME/agentty).
    // 指向THIS测试家,因此测试在home/.agentty下写到的技能是发现发现的
    // ——否则它解决了共享的-binary 沙箱.
    _putenv_s("AGENTTY_HOME", "");   // 倒车 $HOME/.agentty
#else
    setenv("HOME", home.string().c_str(), 1);
    unsetenv("AGENTTY_HOME");   // 倒车 $HOME/.agentty (这个测试的家)
#endif
    fs::current_path(work);
    util::set_workspace_root(::agentty::IoAccess::grant(), work);

    // ── 阶段1:发现横跨根部+优先 ─────────────────
    // 用户本地
    write_file_at(home / ".agentty/skills/alpha/SKILL.md",
        "---\nname: alpha\ndescription: user native alpha\n---\nUSER BODY\n");
    // 用户间(.agents)——同名,必须被本地人阴影.
    write_file_at(home / ".agents/skills/alpha/SKILL.md",
        "---\nname: alpha\ndescription: interop alpha\n---\nINTEROP BODY\n");
    // 用户 . claude compat —— 唯一名称, 必须发现
    write_file_at(home / ".claude/skills/claude-only/SKILL.md",
        "---\nname: claude-only\ndescription: from claude dir\n---\nCC BODY\n");
    // 工程本地化——与用户alpha同名,必须WIN
    write_file_at(work / ".agentty/skills/alpha/SKILL.md",
        "---\nname: alpha\ndescription: project alpha\n---\nPROJECT BODY\n");
    // 项目间距
    write_file_at(work / ".agents/skills/beta/SKILL.md",
        "---\nname: beta\ndescription: project interop beta\n---\nBETA BODY\n");

    {
        const auto a = skills::find(::agentty::IoAccess::grant(), "alpha");
        CHECK(a.has_value());
        if (a) {
            CHECK(a->source == "project");          // 工程阴影用户
            CHECK(a->body == "PROJECT BODY");
            CHECK(!a->dir.empty());
        }
        CHECK(skills::find(::agentty::IoAccess::grant(), "claude-only").has_value());   // . . .. . 来个比喻
        CHECK(skills::find(::agentty::IoAccess::grant(), "beta").has_value());          // . 代理接口
    }

    // ── 第二阶段:宽大处理 解析 ─────────────────────────────────────
    // 描述中未引用的冒号( 无效) YAML 其他项目 解析器 掐住它。。
    write_file_at(work / ".agentty/skills/colons/SKILL.md",
        "---\nname: colons\ndescription: Use when: things have colons\n---\nB\n");
    // name/dir 错配 - 加载 无论如何,前题名胜出。
    write_file_at(work / ".agentty/skills/dirname-x/SKILL.md",
        "---\nname: othername\ndescription: mismatch ok\n---\nB\n");
    // 根本没有前题 正文-first-line 成为描述。
    write_file_at(work / ".agentty/skills/bare/SKILL.md",
        "Just a bare instruction doc.\nMore text.\n");
    {
        const auto c = skills::find(::agentty::IoAccess::grant(), "colons");
        CHECK(c && c->description == "Use when: things have colons");
        CHECK(skills::find(::agentty::IoAccess::grant(), "othername").has_value());
        CHECK(!skills::find(::agentty::IoAccess::grant(), "dirname-x").has_value());  // 前题名已赢得
        const auto b = skills::find(::agentty::IoAccess::grant(), "bare");
        CHECK(b && b->description == "Just a bare instruction doc.");
    }

    // ── 阶段3:可选字段+目录过滤 ─────────────────
    write_file_at(work / ".agentty/skills/full-meta/SKILL.md",
        "---\n"
        "name: full-meta\n"
        "description: has every optional field\n"
        "compatibility: Requires python3\n"
        "allowed-tools: bash read\n"
        "license: Apache-2.0\n"
        "metadata:\n"
        "  author: example-org\n"
        "  version: \"1.0\"\n"
        "---\nMETA BODY\n");
    write_file_at(work / ".agentty/skills/hidden/SKILL.md",
        "---\nname: hidden\ndescription: user-explicit only\n"
        "disable-model-invocation: true\n---\nHIDDEN BODY\n");
    // 块 -scalar 说明( 已复制) `>-` ),该 Claude 代码 -ism.
    write_file_at(work / ".agentty/skills/folded/SKILL.md",
        "---\n"
        "name: folded\n"
        "description: >-\n"
        "  First folded line\n"
        "  second folded line\n"
        "---\nFOLD BODY\n");
    {
        const auto f = skills::find(::agentty::IoAccess::grant(), "full-meta");
        CHECK(f && f->compatibility == "Requires python3");
        CHECK(f && f->allowed_tools == "bash read");
        CHECK(f && f->license == "Apache-2.0");
        CHECK(f && f->metadata.size() == 2);
        if (f && f->metadata.size() == 2) {
            CHECK(f->metadata[0].first == "author"
                  && f->metadata[0].second == "example-org");
            CHECK(f->metadata[1].first == "version"
                  && f->metadata[1].second == "1.0");
        }
        const auto fo = skills::find(::agentty::IoAccess::grant(), "folded");
        CHECK(fo && fo->description ==
              "First folded line second folded line");
        CHECK(fo && fo->body == "FOLD BODY");
        // 隐藏:可明确查找,不见于目录.
        const auto h = skills::find(::agentty::IoAccess::grant(), "hidden");
        CHECK(h && h->user_only);
        auto cat = skills::catalog_block(::agentty::IoAccess::grant());
        CHECK(cat.find("full-meta") != std::string::npos);
        CHECK(cat.find("hidden") == std::string::npos);
        // 目录提到第三级合同。
        CHECK(cat.find("skill") != std::string::npos);
    }

    // ── 阶段4:三级资源+启动载荷 ───────────────
    write_file_at(work / ".agentty/skills/with-res/SKILL.md",
        "---\nname: with-res\ndescription: bundles resources\n---\n"
        "Run scripts/go.sh then read references/REF.md\n");
    write_file_at(work / ".agentty/skills/with-res/scripts/go.sh",
        "#!/bin/sh\necho hi\n");
    write_file_at(work / ".agentty/skills/with-res/references/REF.md",
        "deep reference\n");
    {
        const auto w = skills::find(::agentty::IoAccess::grant(), "with-res");
        CHECK(w.has_value());
        if (w) {
            CHECK(w->resources.size() == 2);
            // 排序、相对、前斜线; SKILL.md 不包括,但从上到下,
            CHECK(w->resources[0] == "references/REF.md");
            CHECK(w->resources[1] == "scripts/go.sh");
            auto pay = skills::activation_payload(::agentty::IoAccess::grant(), *w);
            CHECK(pay.find("<skill_content name=\"with-res\">") == 0);
            CHECK(pay.find("Skill directory: ") != std::string::npos);
            CHECK(pay.find("<skill_resources>") != std::string::npos);
            CHECK(pay.find("scripts/go.sh") != std::string::npos);
            CHECK(pay.find("</skill_content>") != std::string::npos);
        }
    }

    // ── 第5阶段:激活从可见的文字记录中取出DERIVED ───
    //
    // "Already active" 曾经是一个过程 -global 设定( e)
    // note_activated / reset_activations。它现在是一个函数 模型可以看到。
    // 下面的每个检查都是存储的集合出错或需要手-placedXXreset
    // ()的情况.
    {
        const auto body = [](const char* n) {
            return std::string{"<skill_content name=\""} + n + "\">\n...\n</skill_content>";
        };
        auto tool_result = [](std::string out) {
            ToolUse tc;
            tc.id     = ToolCallId{"call"};
            tc.name   = ToolName{"skill"};
            tc.status = ToolUse::Done{.output = std::move(out)};
            Message a; a.role = Role::Assistant;
            a.tool_calls.push_back(std::move(tc));
            return a;
        };

        // 没什么 已加载 − 无活动。 (没有) reset() 需要先重置,
        // 因为没有状态可以重置。 )
        Thread t;
        CHECK(!skills::is_active_in("alpha", visible_text(t)));

        // TOOL RESULT中的正文是活跃的——这是技能工具所赋予的,
        // 因此工具输出必须成为"visible"的一部分.
        t.messages.push_back(tool_result(body("alpha")));
        CHECK(skills::is_active_in("alpha", visible_text(t)));
        CHECK(!skills::is_active_in("beta", visible_text(t)));   // 独立

        // 另一种技能名称的前缀不是 匹配: 标记包括关闭的引号,
        // 所以 "review" 不点亮 "review-pr".
        t.messages.push_back(tool_result(body("review-pr")));
        CHECK(skills::is_active_in("review-pr", visible_text(t)));
        CHECK(!skills::is_active_in("review", visible_text(t)));

        // active_in从工具结果和文本中列出每个活动名称一次.
        {
            Message u; u.role = Role::User; u.text = body("gamma");   // /伽玛扩展
            t.messages.push_back(std::move(u));
            auto names = skills::active_in(visible_text(t));
            CHECK(names.size() == 3);
        }

        // 该 BUG 该 STORED SET HAD。在/约定 tool_result
        // 持有 "alpha" 被概括为已删除。 旧的套装仍然说 "active" ,
        // 所以模型被告诉 正文 曾经是 "in an earlier tool_result"
        // 已经不复存在,而且再也没有回来。 不 活动。
        {
            Thread c = t;
            Thread::CompactionRecord rec;
            rec.up_to_index = 1;                // 放弃α tool_result
            rec.summary     = "the user loaded a skill earlier";
            c.compactions.push_back(rec);
            CHECK(!skills::is_active_in("alpha", visible_text(c)));
            CHECK(skills::is_active_in("review-pr", visible_text(c)));   // 仍然可见
        }

        // 另一半:a 线程 其 人 名 正文 仍然需要考虑
        // NO 重置为恢复时正确, 且不获取 正文 两次
        // (旧集在恢复时开始空,Re-injected).
        CHECK(skills::is_active_in("review-pr", visible_text(t)));

        // 工具( E) -side 视图:什么是 分派 将它放进 调用 上下文 上下文的上下文 下下文的上下下文
        {
            CallContext ctx;
            ctx.active_skills = skills::active_in(visible_text(t));
            CHECK(ctx.skill_active("alpha"));
            CHECK(!ctx.skill_active("delta"));
        }
    }

    // ── 第5b阶段:光谱图 ───────────────────────────────────
    {
        // 干净的技巧 —— 没有诊断。
        const auto f = skills::find(::agentty::IoAccess::grant(), "full-meta");
        CHECK(f && skills::lint(*f).empty());
        // 违反规定 – 诊断火灾(装载行为较为宽松)。
        skills::Skill bad;
        bad.name = "Bad--Name-";
        bad.description = "";
        auto diags = skills::lint(bad);
        CHECK(diags.size() >= 3);   // 字符集 + 双连字符 + 边 + 脱色
        // name/dir 发现不匹配
        const auto mm = skills::find(::agentty::IoAccess::grant(), "othername");
        bool has_mismatch = false;
        if (mm) for (const auto& d : skills::lint(*mm))
            if (d.find("does not match parent directory") != std::string::npos)
                has_mismatch = true;
        CHECK(has_mismatch);
    }

    // ── 6.5阶段:巢(组)技能 ──────────────────────
    // 技能可以生活在 任意 深低于发现根;
    // 名称是根下方有段连接的路径 '-'.
    write_file_at(work / ".agentty/skills/embedded/startup/SKILL.md",
        "---\nname: embedded-startup\ndescription: nested two levels\n---\n"
        "NESTED BODY\n");
    write_file_at(work / ".agentty/skills/perf/alloc/SKILL.md",
        "---\ndescription: name falls back to the joined path slug\n---\n"
        "IMPLICIT NAME BODY\n");
    write_file_at(work / ".agentty/skills/deep/a/b/c/SKILL.md",
        "---\nname: deep-a-b-c\ndescription: four levels down\n---\nBODY\n");
    // 隐藏目录是存储的,从不熟练的领地.
    write_file_at(work / ".agentty/skills/.cache/junk/SKILL.md",
        "---\nname: cache-junk\ndescription: must not be discovered\n---\nBODY\n");
    // 加入-name碰撞:较浅技能获胜.
    write_file_at(work / ".agentty/skills/pair/SKILL.md",
        "---\nname: pair\ndescription: shallow\n---\nSHALLOW\n");
    write_file_at(work / ".agentty/skills/x/pair/SKILL.md",
        "---\nname: pair\ndescription: deeper\n---\nDEEPER\n");
    {
        const auto n = skills::find(::agentty::IoAccess::grant(), "embedded-startup");
        CHECK(n && n->body == "NESTED BODY");
        CHECK(n && n->source == "project");
        // Lint比较了LEAF目录,而不是联合名称.
        if (n) {
            bool mismatch = false;
            for (const auto& d : skills::lint(*n))
                if (d.find("does not match parent directory") != std::string::npos)
                    mismatch = true;
            CHECK(!mismatch);
        }
        // 明确 `name:` 缺少 + 回退 是已连接的路径弹。
        const auto impl = skills::find(::agentty::IoAccess::grant(), "perf-alloc");
        CHECK(impl && impl->body == "IMPLICIT NAME BODY");
        const auto deep = skills::find(::agentty::IoAccess::grant(), "deep-a-b-c");
        CHECK(deep && deep->body == "BODY");
        // 隐藏的迪尔从不贡献技能。
        CHECK(!skills::find(::agentty::IoAccess::grant(), "cache-junk").has_value());
        // 浅者在加盟上打得更深 -name 碰撞 撞车 撞车
        const auto p = skills::find(::agentty::IoAccess::grant(), "pair");
        CHECK(p && p->description == "shallow");
        CHECK(p && p->body == "SHALLOW");
    }

    // ── 深度优先为 DEPTH ,不通过 字节 顺序 ──────────────
    //
    // `pair` 数字 `pair/x` 上面碰巧是浅水 -first ,
    // 所以它不会执行规则。这对规则是: '-' (0x2D) 类型 之前 '/'
    // (0x2F),如此简单的词典顺序访问 `ambig/dup/SKILL.md`
    // 首先是,还有 DEEPER 双弹到双弹 `ambig-dup` ,
    // 所以一个现有的平面技能,当一个不相关的组文件夹出现旁边时,
    // 就会悄悄地被取代——确切的回归.
    // "flat layouts keep their names unchanged" 排除了,我们没有。
    write_file_at(work / ".agentty/skills/ambig-dup/SKILL.md",
        "---\ndescription: flat and shallow\n---\nFLAT\n");
    write_file_at(work / ".agentty/skills/ambig/dup/SKILL.md",
        "---\ndescription: nested and deeper\n---\nNESTED\n");
    {
        const auto a = skills::find(::agentty::IoAccess::grant(), "ambig-dup");
        CHECK(a.has_value());
        CHECK(a && a->body == "FLAT");
        CHECK(a && a->description == "flat and shallow");
    }

    // ── 衍生名称留在字符组内 ───────────────────
    //
    // 名称是用户的类型 `/name` 和刀锋 -command
    // 解析器 将其符牌在白空格上分割开来 —— 因此包含一个空间的
    // 名称可以发现, 列出, 永久不包含 -invokable.
    // 仅取代 '/' 允许空间、 下划线和大写。
    write_file_at(work / ".agentty/skills/My Group/Sub_Dir/SKILL.md",
        "---\ndescription: charset\n---\nCHARSET\n");
    {
        const auto c = skills::find(::agentty::IoAccess::grant(), "my-group-sub-dir");
        CHECK(c.has_value());
        CHECK(c && c->body == "CHARSET");
        // 原始的,未卫生的形态在任何拼写下都不得存在.
        CHECK(!skills::find(::agentty::IoAccess::grant(), "My Group-Sub_Dir").has_value());
        for (const auto& sk : skills::all(::agentty::IoAccess::grant())) {
            bool clean = true;
            for (unsigned char ch : sk.name) {
                const bool ok = (ch >= 'a' && ch <= 'z')
                             || (ch >= '0' && ch <= '9') || ch == '-';
                if (!ok) { clean = false; break; }
            }
            // 头事项 `name:` 有可能合法地推翻弹壳,所以 派生 名字
            // ( slug == name) 是字符集 -bound.
            if (sk.slug == sk.name)
                CHECK_MESSAGE(clean, "derived name outside [a-z0-9-]: " << sk.name);
        }
    }

    // 运行崩溃和边缘修剪 : `a  b` / `-lead-` 永远不
    // 产生加倍的产量 或边缘连字符。
    write_file_at(work / ".agentty/skills/-Odd  Name-/SKILL.md",
        "---\ndescription: collapse\n---\nCOLLAPSE\n");
    {
        const auto c = skills::find(::agentty::IoAccess::grant(), "odd-name");
        CHECK(c && c->body == "COLLAPSE");
    }

    // ── 一种连结技能命名为 PUT ──────────────────
    //
    // 将技能目录链接到 库 是一个支持的布局(平面
    // 扫描发现了它,而行走探测器直接因为行走者
    // 不会跟随它们而产生共鸣)。它的名字必须来自 链
    // 接 名称在 库, 不 不论从何处 字节 现场:
    // fs::relative 以卡尼化,所以测量 弹壳,
    // 从那里逃出根 `../../elsewhere/real` ,
    // 并用绝对文件系统路径命名了技能。
    {
        const fs::path target = base / "external" / "real-skill-dir";
        write_file_at(target / "SKILL.md",
            "---\ndescription: lives outside the library\n---\nLINKED\n");
        std::error_code lec;
        fs::create_directory_symlink(
            target, work / ".agentty/skills/aliased", lec);
        if (!lec) {   // 跳过无法连接的链接( 大约 CI 主机)
            const auto l = skills::find(::agentty::IoAccess::grant(), "aliased");
            CHECK(l.has_value());
            CHECK(l && l->body == "LINKED");
            // 决断的路径绝不能漏入名中.
            CHECK(!skills::find(::agentty::IoAccess::grant(), "real-skill-dir").has_value());
            for (const auto& sk : skills::all(::agentty::IoAccess::grant()))
                CHECK(sk.name.find("..") == std::string::npos);
        }
    }

    // ── 步行是受约束的 ──────────────────────────────────────────
    //
    // 深度停止于 kMaxSkill 深度: 组文件夹
    // 组织一个 库 自由的走动,都穿过了一条大路 源码树
    // 满载在每一 缓存 丢失和生成名称太长,无法输入(a
    // 40) -deep 巢产生了238个字符).
    {
        fs::path deep = work / ".agentty/skills/toodeep";
        for (int i = 0; i < 8; ++i) deep /= ("lvl" + std::to_string(i));
        write_file_at(deep / "SKILL.md",
            "---\ndescription: past the cap\n---\nTOODEEP\n");
        int past_cap = 0;
        for (const auto& sk : skills::all(::agentty::IoAccess::grant()))
            if (sk.description == "past the cap") ++past_cap;
        CHECK(past_cap == 0);
        // ...以及每个名字 IS 发现的可打字。
        for (const auto& sk : skills::all(::agentty::IoAccess::grant()))
            CHECK(sk.name.size() <= skills::kMaxSlugLen);
    }

    // 筑巢技巧就是... 不 里面有它所坐的技巧的源泉
    write_file_at(work / ".agentty/skills/host/SKILL.md",
        "---\nname: host\ndescription: holds a nested skill\n---\nHOST BODY\n");
    write_file_at(work / ".agentty/skills/host/nested/inner/SKILL.md",
        "---\nname: host-nested-inner\ndescription: inside host\n---\nINNER\n");
    write_file_at(work / ".agentty/skills/host/refs/NOTE.md", "plain ref\n");
    {
        const auto h = skills::find(::agentty::IoAccess::grant(), "host");
        CHECK(h && h->resources.size() == 1);
        if (h && h->resources.size() == 1)
            CHECK(h->resources[0] == "refs/NOTE.md");
        CHECK(skills::find(::agentty::IoAccess::grant(), "host-nested-inner").has_value());
    }

    // 具有相同巢名的项目阴影用户。
    write_file_at(home / ".agentty/skills/embedded/startup/SKILL.md",
        "---\nname: embedded-startup\ndescription: user variant\n---\nUSER BODY\n");
    {
        const auto n = skills::find(::agentty::IoAccess::grant(), "embedded-startup");
        CHECK(n && n->source == "project" && n->body == "NESTED BODY");
    }

    // ── 阶段6:读作-allowlist门 ─────────────────────────────────
    // A USER-scope 技能资源在 工作区 ;
    // 读门必须经过,写门仍必须拒绝.
    write_file_at(home / ".agentty/skills/usr-res/SKILL.md",
        "---\nname: usr-res\ndescription: user skill with resource\n---\nB\n");
    write_file_at(home / ".agentty/skills/usr-res/references/SECRET-FREE.md",
        "outside-workspace resource\n");
    {
        const auto u = skills::find(::agentty::IoAccess::grant(), "usr-res");   // 发现注册表 允许清单
        CHECK(u.has_value());
        auto res = (home / ".agentty/skills/usr-res/references/SECRET-FREE.md").string();
        CHECK(util::is_read_allowlisted(res));
        auto ok = util::make_readable_path_checked(res, "read");
        CHECK(ok.has_value());
        // Write 门没有被 允许清单.
        auto wr = util::make_workspace_path_checked(res, "write");
        CHECK(!wr.has_value());
        // 和一个没有 -skill 退出( O) -of-workspace 路径仍然无法读取门。
        auto bad = util::make_readable_path_checked(
            (home / "unrelated.txt").string(), "read");
        CHECK(!bad.has_value());
    }

    fs::current_path(base, ec);   // 离职 `work` 清理就可以把它删除了
    fs::remove_all(base, ec);

}

TEST_CASE("skills catalog cap: AGENTTY_MAX_SKILLS override") {
    agtest::ScopedEnvSandbox _env_guard;
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec) / "agentty_skills_cap_test";
    fs::remove_all(base, ec);
    fs::path home = base / "home";
    fs::path work = base / "work";
    fs::create_directories(home);
    fs::create_directories(work);

#if defined(_WIN32)
    _putenv_s("HOME", home.string().c_str());
    _putenv_s("AGENTTY_HOME", "");
#else
    setenv("HOME", home.string().c_str(), 1);
    unsetenv("AGENTTY_HOME");
#endif
    fs::current_path(work);
    util::set_workspace_root(::agentty::IoAccess::grant(), work);

    // 手柄每一次发现传球都读出来, all() 只有当其 mtime 签名发生变化
    // 时才重新扫描——所以每个子 -case 下方通过改变多少位来移动西格
    // SKILL.md 时间( 顶部) -bounded )行走收集(行走收集).
    // 相邻子区 -cases 永远不要在等待不同结果时共享签名,
    // 所以没有强制 缓存-buster 需要写作。

    // RAII 键本身:即使一个 CHECK 掷出.
    const char* old_raw = std::getenv("AGENTTY_MAX_SKILLS");
    const bool  had_old = old_raw != nullptr;
    const std::string old_val = had_old ? old_raw : "";
    struct Restore {
        const bool had; const std::string val;
        ~Restore() {
#if defined(_WIN32)
            if (had) _putenv_s("AGENTTY_MAX_SKILLS", val.c_str());
            else     _putenv_s("AGENTTY_MAX_SKILLS", "");
#else
            if (had) setenv("AGENTTY_MAX_SKILLS", val.c_str(), 1);
            else     unsetenv("AGENTTY_MAX_SKILLS");
#endif
        }
    } _restore{had_old, old_val};

    auto set_env = [](const char* v) {
#if defined(_WIN32)
        _putenv_s("AGENTTY_MAX_SKILLS", v);   // 空值删除( CRT)
#else
        setenv("AGENTTY_MAX_SKILLS", v, 1);
#endif
    };
    auto unset_env = []() {
#if defined(_WIN32)
        _putenv_s("AGENTTY_MAX_SKILLS", "");
#else
        unsetenv("AGENTTY_MAX_SKILLS");
#endif
    };

    // 80个技能——在下面的每一种情况下都超过了默认上限.
    constexpr std::size_t kSkills = 80;
    for (std::size_t i = 0; i < kSkills; ++i) {
        const std::string slug = "cap-skill-" + std::to_string(i);
        write_file_at(home / ".agentty/skills" / slug / "SKILL.md",
            "---\nname: " + slug + "\ndescription: cap test\n---\nB\n");
    }

    // 默认未设置时：最多 kMaxSkills 个条目同时保留在遍历结果和
    // 目录 切片——盖子适用于 WORK 以及《公约》缔约国 RESULT.
    unset_env();
    CHECK(skills::all(::agentty::IoAccess::grant()).size() == skills::kMaxSkills);

    // 提高上限时：所有发现的技能都加入目录，包括
    // 默认的-capped 步行甚至从未访问。
    set_env("200");
    CHECK(skills::all(::agentty::IoAccess::grant()).size() == kSkills);
    CHECK(skills::find(::agentty::IoAccess::grant(), "cap-skill-79").has_value());

    // 降低上限时：目录按覆盖值截断。
    set_env("10");
    CHECK(skills::all(::agentty::IoAccess::grant()).size() == 10);

    // 最小值限制：低于下限时固定为下限。
    set_env("2");
    CHECK(skills::all(::agentty::IoAccess::grant()).size() == 8);

    // 格式错误时：无效值保留默认上限。
    set_env("not-a-number");
    CHECK(skills::all(::agentty::IoAccess::grant()).size() == skills::kMaxSkills);

    // ── 一个 发现的每个通道 ───────────────────────────
    // 盖的界限 scan_root's WALK ,因此,
    // 它曾经被咨询到每个目录条目——以及每个条目释放
    // dbglog面包屑的不正确路径:测量了120 ERROR
    // -level 单次传球40次 all() 运行每个转弯和
    // dbglog 输入坠机记录器, 所以一个tv'd env
    // var 取代诊断 存在一个坠机倾储保存。
    //
    // 将属性设置在源头, 而不是计算日志行( logx
    // 文件的槽 lats on first use,
    // 所以 slow) -based 共享测试中计数不可靠 二
    // 进制程序 : 发现证必须解决盖子 恰好 一次,不管它走多少
    // 条条目 或它访问多少根。
    {
        // 强制重置( recan)——缓存在mtimes上键入,所以触摸一个文件.
        write_file_at(home / ".agentty/skills/cap-skill-0/SKILL.md",
            "---\nname: cap-skill-0\ndescription: touched\n---\nB\n");
        set_env("not-a-number");          // 用于垃圾邮件的路径
        const auto before = skills::debug_cap_resolutions();
        (void)skills::all(::agentty::IoAccess::grant());
        const auto after = skills::debug_cap_resolutions();
        CHECK_MESSAGE(after - before == 1,
            "a discovery pass resolved the cap " << (after - before)
            << " times; it must resolve exactly once (a malformed value "
            "logs an ERROR-level breadcrumb per resolution, into the "
            "crash flight recorder)");
    }

    fs::current_path(base, ec);
    fs::remove_all(base, ec);
}

// A SKILL.md 太大到无法 加载 必须 SAY 所以说 So.
//
// read_capped 返回空的,用于 "over the cap"
// 完全如它所为 "unreadable" ,所以超大小的技巧 用来
// 消失 从发现 `0 warning(s)` - 作者的文件坐在磁盘上,
// 没有任何东西能把它和路径类型区分开来。 形状与阴影日志相同:
// 文件 目录 忽视比它大声拒绝的更糟糕
TEST_CASE("skills: an oversized SKILL.md is reported, not dropped") {
    agtest::ScopedEnvSandbox _env_guard;
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec) / "agentty_skills_big_test";
    fs::remove_all(base, ec);
    fs::path home = base / "home";
    fs::path work = base / "work";
    fs::create_directories(home / ".agentty/skills", ec);
    fs::create_directories(work / ".agentty/skills", ec);
#if defined(_WIN32)
    _putenv_s("HOME", home.string().c_str());
    _putenv_s("AGENTTY_HOME", "");
#else
    setenv("HOME", home.string().c_str(), 1);
    unsetenv("AGENTTY_HOME");
#endif
    fs::current_path(work, ec);

    // 一件好技能,除了一个 超越盖子。
    write_file_at(work / ".agentty/skills/fine/SKILL.md",
                  "---\nname: fine\ndescription: loads normally\n---\nbody\n");
    {
        std::string big = "---\nname: toobig\ndescription: x\n---\n";
        big.append(skills::kMaxBodyBytes + 1024, 'x');
        write_file_at(work / ".agentty/skills/toobig/SKILL.md", big);
    }

    const auto& all = skills::all(::agentty::IoAccess::grant());
    const skills::Skill* fine = nullptr;
    const skills::Skill* big  = nullptr;
    for (const auto& s : all) {
        if (s.name == "fine")   fine = &s;
        if (s.name == "toobig") big  = &s;
    }

    REQUIRE(fine != nullptr);
    CHECK(fine->oversized_bytes == 0, "a normal skill is not flagged");

    REQUIRE_MESSAGE(big != nullptr,
        "an oversized SKILL.md must still appear in discovery -- silently "
        "dropping it is how an author loses a skill with no way to tell");
    CHECK(big->oversized_bytes > skills::kMaxBodyBytes,
          "the measured size is carried for the warning");

    // 一定要这样 LINT 说一个有用的东西 而不是一堆
    // "description is missing" 由从未解析过的字段组成。
    const auto warns = skills::lint(*big);
    REQUIRE(!warns.empty(), "it produces a warning");
    CHECK(warns.size() == 1, "exactly one warning -- the relevant one");
    CHECK(warns.front().find("NOT loaded") != std::string::npos,
          "the warning says the skill is not loaded");

    // 一定要这样 不 到达模型:没有 正文 启动,
    // 所以提供它会买一个浪费的转弯。
    const std::string catalog = skills::catalog_block(::agentty::IoAccess::grant());
    CHECK(catalog.find("toobig") == std::string::npos,
          "an unloadable skill is never offered to the model");
    CHECK(catalog.find("fine") != std::string::npos,
          "the good skill beside it is unaffected");

    fs::current_path(base, ec);
    fs::remove_all(base, ec);
}
