// agentty::util::user_root 普普尔,见 头文件 单人 -root 理由(为什么)
// ~/.agentty 而不是 说 $XDG_CONFIG_HOME/agentty).
#include "agentty/util/user_root.hpp"
#include "agentty/util/home_dir.hpp"

#include <cstdlib>
#include <cstdio>
#include <maya/runtime.hpp>
#include <set>
#include <string>
#include <system_error>

#ifndef _WIN32
#  include <sys/stat.h>   // chmod
#  include <pwd.h>        // eupwuid——env-proof 家用于测试绊线
#  include <unistd.h>     // 获取( G)
#endif

namespace agentty::util {

namespace fs = std::filesystem;

namespace {

// 开发商的 TRUE ,来自密码数据库,而不是 $HOME ,
// 测试通常在临时目录中重新指向。仅用下面的测试绊线来回答
// "is this the real store?" 某种程度上
// 没有内存突变( 也没有任何静态) -init 命令)可以混淆。
// 空开 platforms/paths 无法确定,
// 哪些解除了警卫的枪械 而不是冒着假流产的风险。
fs::path passwd_home() {
#ifndef _WIN32
    if (const char* p = std::getenv("AGENTTY_TEST_FAKE_PASSWD_HOME"))
        return fs::path{p};                 // 逃出舱门,供警卫自己测试
    if (const struct passwd* pw = ::getpwuid(::getuid());
        pw && pw->pw_dir && *pw->pw_dir)
        return fs::path{pw->pw_dir};
#endif
    return {};
}

// 解析根 无需 副作用 (由 user_root
// 和迁移探针。
fs::path resolve_root() {
    if (const char* h = std::getenv("AGENTTY_HOME"); h && *h) return fs::path{h};
    fs::path home = home_dir();
    if (home.empty()) return {};
    return home / ".agentty";
}

// 遗产的分割 -root 位置 : $XDG_CONFIG_HOME/agentty
// 或 或 时 间 ~/.config/agentty. 预览 -consolidation
// 这份文件 凭据 ,账户,提供商 OAuth 文件 + 锁 ,则 models.dev
// 缓存 和更新的印章。 无需 创建任何东西。
fs::path legacy_config_dir() {
    fs::path base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) base = xdg;
    else {
        fs::path home = home_dir();
        if (home.empty()) return {};
        base = home / ".config";
    }
    return base / "agentty";
}

// 复制+验证一个文件进入 `to_dir` 最佳状态 -effort 移除源头。
// 绝不覆盖一个已有的目的地( 新布局下的新文件
// 总是胜过 过期 遗存副本)。 绝不丢弃。
void migrate_file(const fs::path& from, const fs::path& to_dir) {
    std::error_code ec;
    if (!fs::is_regular_file(from, ec)) return;
    const fs::path to = to_dir / from.filename();
    if (fs::exists(to, ec)) {                 // 已迁移 / re-created
        fs::remove(from, ec);                 // 丢弃 过期 遗稿
        return;
    }
    fs::copy_file(from, to, fs::copy_options::none, ec);
    if (ec) return;                           // 离开源; 下一轮运行 重试
    // 在删除原件前,对落地的复制件进行同样大小的校验——
    // ~/.config → ~/.agentty可以跨文件系统,
    // 因此没有平面的rename(),截断的复制件不得
    // 破坏唯一的优秀的字节.
    std::error_code e1, e2;
    if (fs::file_size(to, e1) != fs::file_size(from, e2) || e1 || e2) {
        fs::remove(to, ec);
        return;
    }
#ifndef _WIN32
    // 凭据 保持拥有者 -only 位; 对 缓存 文档。
    ::chmod(to.c_str(), S_IRUSR | S_IWUSR);
#endif
    fs::remove(from, ec);
}

// 一个 -time (每架; 幂等 遗产迁移
// ~/.config/agentty 密钥在 credentials/,
// 缓存 输入 cache/. 任何未识别的都留在原处——这
// 可以迁移文件 agentty 自己写,没有别的。
fs::path subdir_under(const fs::path& root, const char* env, const char* leaf,
                      bool owner_only);

void migrate_legacy_config(const fs::path& root) {
    std::error_code ec;
    const fs::path legacy = legacy_config_dir();
    if (legacy.empty() || !fs::is_directory(legacy, ec)) return;

    // 访问者所解决的、 包含的覆盖的相同 dirs。 按给定的根 : 调用
    // user_root () 这里将重新 -enter 编号 call_once.
    const fs::path creds = subdir_under(root, "AGENTTY_CREDENTIALS_DIR", "credentials", true);
    const fs::path cache = subdir_under(root, "AGENTTY_CACHE_DIR", "cache", false);
    if (creds.empty() || cache.empty()) return;

    // 机密+每笔 -provider 国家 □ credentials/. 那个...
    // 锁 伴侣是忠告的, -lock inodes; 迁移它们保持 锁 路径
    // 在他们守卫的文件旁边。
    for (const char* name : {"credentials.json", "credentials.json.lock",
                             "accounts.json",
                             "codex_credentials.json", "codex_credentials.json.lock",
                             "copilot_credentials.json", "copilot_credentials.json.lock",
                             "kimi_credentials.json", "kimi_credentials.json.lock",
                             "kimi_device_id"})
        migrate_file(legacy / name, creds);

    // 可拖曳性 − cache/.
    for (const char* name : {"modelsdev.json", "update_check.json"})
        migrate_file(legacy / name, cache);

    // 诊断 + logs/ (附录) -only ;合并将交互会话,
    // 所以移动 -if-absent 也是正确的语义。 agentty.log 预览
    // -consolidation 根号 -level 沉积; stderr.log
    // 住在遗产的配置目录中。
    const fs::path logs = subdir_under(root, "AGENTTY_LOGS_DIR", "logs", false);
    if (logs.empty()) return;
    migrate_file(legacy / "stderr.log", logs);
    migrate_file(root / "agentty.log", logs);

    // 如果迁移空了, 则删除遗留目录。 如果用户( 或其它
    // 工具) 将其它任何内容放在那里, 请放它一旁。
    if (fs::is_directory(legacy, ec) && fs::is_empty(legacy, ec))
        fs::remove(legacy, ec);
}

// 解决一个不可忽视的子目录。
//
// `$env` 设定时胜负, 非胜负 -empty ; 否则 `leaf` 根下结.
// A RELATIVE 覆盖是针对用户根解决的, 绝不针对进程
// CWD: `AGENTTY_LOGS_DIR=logs2`
// 必须是一个目录,而不是每个目录的不同 agentty 发射开始
//
// 无法创建覆盖不是 默认情况下无声打印。 用户在未挂
// 载的驱动器上对日志进行指点并把它们输入 ~/.agentty 反正,
// 两次得到错误的回答: 设置没有被接受,
// 也没有任何反应。 警告一次, 然后退后 ——
// 完全失去诊断结果比写到默认位置更糟糕,
// 但只有在用户被告知发生什么时才会这样。
fs::path resolve_subdir(const char* env, const char* leaf, bool owner_only) {
    return subdir_under(user_root(), env, leaf, owner_only);
}

fs::path subdir_under(const fs::path& root, const char* env, const char* leaf,
                      bool owner_only) {
    if (root.empty()) return root;

    fs::path p;
    bool overridden = false;
    if (const char* v = std::getenv(env); v && *v) {
        fs::path given{v};
        p = given.is_absolute() ? std::move(given) : root / given;
        overridden = true;
    } else {
        p = root / leaf;
    }

    std::error_code ec;
    fs::create_directories(p, ec);
    if (overridden && !fs::is_directory(p, ec)) {
        // maya::snapshot 而不是一个 锁 : 此解析器
        // 从任何地方( 包括排名以下) 得到。 锁 ) 没有能力
        // 线程 ,而片场只生长。 锁-free 读 -then-insert ;
        // 二 个 线程 在同一瞬间击中同样的破损的覆盘,
        // 可能同时发出警告,这无害。
        static maya::snapshot<std::set<std::string>> warned;
        const std::string key{env};
        if (warned.load()->contains(key)) return root / leaf;
        (void)warned.update([](const std::set<std::string>& old, const std::string& k) {
            auto next = old;
            next.insert(k);
            return next;
        }, key);
        // create_directories () 叶子 `ec`
        // CLEAR 当路径已存在为非 -directory - 它没有失败,
        // 它没有做 - 所以这里的报章 ec 打印 "Undefined error: 0"
        // 用于最可能发生的单一错误(将超度指向文件)。
        // 直接标注这个案件,其余的则保留错误文本。
        std::error_code exists_ec;
        const bool in_the_way = fs::exists(p, exists_ec);
        std::fprintf(stderr,
            "agentty: warning: $%s='%s' is not usable (%s) — falling back to %s\n",
            env, p.string().c_str(),
            in_the_way ? "exists but is not a directory"
                       : (ec ? ec.message().c_str() : "cannot create"),
            (root / leaf).string().c_str());
        p = root / leaf;
        fs::create_directories(p, ec);
    }
#ifndef _WIN32
    if (owner_only) ::chmod(p.c_str(), S_IRWXU);
#endif
    return p;
}

}  // 命名空间结束。

fs::path user_root() {
    static std::once_flag once;
    fs::path root = resolve_root();
    if (root.empty()) return root;

    // ── Test-isolation 绊线 ──────────────────────────────────
    // 一项涉及DEVELOPER'S 真实 ~/.agentty的测试二进制程序在他们的凭据商店,
    // 帐户注册表和线程的直播中写着.
    // 这正是当单一的-root合并重新将config_dir()号
    // 号号划为AGENTTY_HOME时发生的情况,而5次凭据试验仍然
    // 通过XDG_CONFIG_HOME号隔离——他们的沙箱号静默地
    // 变成了一个-op号和套套件的变异真实账户。
    //
    // 下级 AGENTTY_UNDER_TEST (由两个
    // 测试主机设置),如果解析的根是 真实 一个 "Real"
    // 以最初使用时记录的流程实际家庭情况来判断,
    // 这种测试将重新指向 $HOME 临时目录(
    // 技能/钩/设置覆盖的常见模式)是适当的隔离模式,必须经过,
    // 尽管它设置了不 $AGENTTY_HOME.
    // 选择 -IN (AGENTTY_STRICT_TEST_ROOT =
    // 1 : 如果测试在开发商的真货店上着陆, 则中止。 主要保护是两个
    // 测试主机都关闭。 沙箱 $AGENTTY_HOME 整个 二进制程序 !
    // ! 这个警卫是皮带 -and-braces 检查 CI / 人工审计,
    // 默认情况下不进行, 这样测试可以合法地重新指定 $HOME 不可
    // 误以为是无异性者.
    if (const char* strict = std::getenv("AGENTTY_STRICT_TEST_ROOT");
        strict && *strict && *strict != '0') {
        static const fs::path real_root = [] {
            fs::path h = passwd_home();
            return h.empty() ? fs::path{} : h / ".agentty";
        }();
        if (!real_root.empty() && root == real_root) {
            std::fprintf(stderr,
                "\n[agentty] FATAL: a test reached the real user root (%s).\n"
                "Isolate it — setenv AGENTTY_HOME (or HOME) to a temp dir — "
                "otherwise it mutates real credentials/threads.\n\n",
                root.string().c_str());
            std::abort();
        }
    }

    // 每解析根创建一次, 而不是每 调用. user_root (a)
    // 坐在每名 -turn 路径(技能) 目录 批准, 内存 ),
    // 并持续 Windows create_directories
    // 甚至是数个元数据查询 源码树 中位数。 -process (测试
    // 重点) AGENTTY_HOME ),所以记住 对应的 路径被保证,
    // 而不仅仅是路径。 活进程下面删除的根由下一个需要它( 每个
    // 作者创建自己的父 dirs) 的编剧重现。 已发布 不可变
    // 数值: 已读 锁-free 每隔一段时间 调用 ,在更改时替换;
    // 2 线程 竞速改变两个创建( R) 幂等 )和两者都发表.
    static maya::published<const fs::path> ensured;
    if (const auto e = ensured.current(); !e || *e != root) {
        std::error_code ec;
        fs::create_directories(root, ec);
#ifndef _WIN32
        // 拥有者 -only :根有对话的历史,有知识 内存 和(员
        // 额) -consolidation) 凭据 单曲的全程
        // 安全故事 -root 设计。
        ::chmod(root.c_str(), S_IRWXU);
#endif
        if (!ec) ensured.publish(std::make_shared<const fs::path>(root));
    }
    maya::once(once, [&] { migrate_legacy_config(root); });
    return root;
}

fs::path user_credentials_dir() {
    return resolve_subdir("AGENTTY_CREDENTIALS_DIR", "credentials", /*owner_only=*/true);
}

fs::path user_state_dir() {
    return resolve_subdir("AGENTTY_STATE_DIR", "state", /*owner_only=*/false);
}

fs::path user_cache_dir() {
    return resolve_subdir("AGENTTY_CACHE_DIR", "cache", /*owner_only=*/false);
}

fs::path user_logs_dir() {
    return resolve_subdir("AGENTTY_LOGS_DIR", "logs", /*owner_only=*/false);
}

fs::path user_threads_dir() {
    // 拥有者 -only : 对话历史与 凭据 默认
    // 继承了 7700 从根部继承,但一个覆盖可以指向
    // 任何地方 — 包括用户创建的 0755 路径
    // — — 因此,这是需要明确设定的位数而不是假设的
    // 所在。
    return resolve_subdir("AGENTTY_THREADS_DIR", "threads", /*owner_only=*/true);
}

}  // namespace agentty::util
