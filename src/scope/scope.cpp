// scope.cpp ——不纯洁的边缘. agentty::scope.
//
// 任何触摸文件系统或环境的东西 此处 ;
// 缩写 头文件 保持一个纯的、可测试的代数(两个)
// 归并 3个需要一个 编译单元:
//   • plan()——将(Layout,Env)变成命令的来源清单。
//   • current_env () 改为: HOME / project_root / 写作能力一次,在边缘。
//   • trust_of ()+批准——内容. -hash 信托商店(MCPoison固定)。

#include "agentty/scope/scope.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

#include "agentty/tool/util/fs_helpers.hpp"   // util::project_root( ) (中文( ) ).

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

#if !defined(_WIN32)
#  include <unistd.h>   // access(), W_OK
#endif

namespace agentty::scope {

namespace {

// NOTE : 方言列表在这里不再是一个全局。 它已移动到
// Layout::dialects (scope.hpp ),因此每个功能都声明自己的
// 宽度和 plan() 准确的表示将读到的内容——见上文注解。 dir_name().

// 已经 `dir` 一个真正的目录,我们可以 create/write 将
// 探测器通用化 内存 提供项目范围之前已执行。 -throwing.
[[nodiscard]] bool dir_writable(const fs::path& dir) noexcept {
    if (dir.empty()) return false;
    std::error_code ec;
    // 步行到最近的现存祖先——写作 "<root>/.agentty/x"
    // 仅需 <root> (或它的第一个存在的父母)可以写作.
    fs::path probe = dir;
    while (!probe.empty() && !fs::exists(probe, ec)) {
        auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (probe.empty() || !fs::is_directory(probe, ec)) return false;
#if defined(_WIN32)
    return true;  // 便宜的,不便宜的,你买的 W_OK ; 写入本身会通过 I/O
#else
    return ::access(probe.c_str(), W_OK) == 0;
#endif
}

[[nodiscard]] fs::path home_dir() noexcept {
    return util::home_dir_or_empty();
}

// A Project/Local 仅当根 project_root () 真实的,非 -root 目录。
// "/"是agentty的"unrestricted access boundary"的哨兵,
// 从来没有分散的. agentty状态——与内存的卫兵完全匹配.
[[nodiscard]] bool usable_project_root(const fs::path& p) noexcept {
    if (p.empty()) return false;
    return p != p.root_path();   // 拒绝 "/" (和 "C:\" 作为项目根
}

}  // 命名空间结束。

std::vector<Source> plan(const Layout& layout, const Env& env) {
    // 特写是 自身 方言列表, 不是全局列表。 空的跨度会默默地表示否
    // Project/User 源的原文为
    // "this feature has no config" 而不是说
    // "somebody passed {}" - 所以把它当作默认,而不是尊重任何人不能达到的价值观。
    const std::span<const Dialect> dialects =
        layout.dialects.empty() ? std::span<const Dialect>{kNativeOnly}
                                : layout.dialects;

    std::vector<Source> out;
    out.reserve(1 + 2 * dialects.size());

    // 明确——一个洞 -pointed 文档用户针对我们。最高优先级、方言 -agnostic
    // (用户命名了精确路径). 它的底部是文件的 PARENT 这么说吧 Source::base
    // 留下来 "the dir this config lives in".
    if (env.explicit_config && !env.explicit_config->empty()) {
        Source s;
        s.locus    = Locus::Explicit;
        s.dialect  = Dialect::Agentty;         // 常规;叶子是明显的
        s.base     = env.explicit_config->parent_path();
        s.writable = dir_writable(s.base);
        out.push_back(std::move(s));
    }

    // 项目——承诺、共享,每个方言目录一个来源,土语第一。
    // (Locus::Local 故意的 故意的,故意的,是故意的,这是故意的 不
    // 已排放的——值存在于窗体中,但没有消费者选择加入。 头文件 其迁注. )
    if (usable_project_root(env.project_root)) {
        for (Dialect d : dialects) {
            Source s;
            s.locus    = Locus::Project;
            s.dialect  = d;
            s.base     = env.project_root / dir_name(d);
            s.writable = env.project_writable && d == Dialect::Agentty;
            out.push_back(std::move(s));
        }
    }

    // 用户——全球,跨越每个区域 工作区。每个方言目录一个来源。
    // 当地方言锚点在 user_native_base (util::user_root():
    // ~/.agentty 或 或 时 间 $AGENTTY_HOME ); 互交方言停留在~.
    if (!env.home.empty() || !env.user_native_base.empty()) {
        for (Dialect d : dialects) {
            Source s;
            s.locus    = Locus::User;
            s.dialect  = d;
            if (d == Dialect::Agentty) {
                s.base = !env.user_native_base.empty()
                             ? env.user_native_base
                             : env.home / dir_name(d);
                s.writable = dir_writable(s.base.parent_path());
            } else {
                if (env.home.empty()) continue;   // 需要一个真正的~
                s.base     = env.home / dir_name(d);
                s.writable = false;               // 中间的二角星为 -only
            }
            out.push_back(std::move(s));
        }
    }

    // 计划已经制定 ROOTS 叶子与叶子结合 caller/loader.
    // 电话 DIALECT 上面的宽度是布局计划中的某一部分——见注:
    // Layout::dialects 为何它从全球移动。
    return out;
}

Env current_env(const Layout& layout) {
    Env e;
    e.home             = home_dir();
    e.user_native_base = util::user_root();
    e.project_root = tools::util::project_root();
    e.project_writable =
        usable_project_root(e.project_root) &&
        dir_writable(e.project_root / dir_name(Dialect::Agentty));

    if (!layout.explicit_env.empty()) {
        if (const char* v = std::getenv(std::string{layout.explicit_env}.c_str());
            v && *v) {
            std::error_code ec;
            fs::path p{v};
            if (fs::is_regular_file(p, ec)) e.explicit_config = std::move(p);
        }
    }
    return e;
}

// ── 信任( L) ─────────────────────────────────────────────────────────────────

bool Approvals::approved(std::string_view content_sha) const noexcept {
    return std::find(shas.begin(), shas.end(), content_sha) != shas.end();
}

void Approvals::approve(std::string_view content_sha) {
    if (!approved(content_sha)) shas.emplace_back(content_sha);
}

// content_hash / load_approvals / save_approvals 住在这里
// src/scope/trust.cpp ——它们依赖认证(Auth) SHA 和(或)
// nlohmann/json ,这是精细的瞄准镜核心(和独立的 scope/skills/commands
// 测试 链接 只有这个 TU ) 不得拉动. trust_of ()以下留于此:其纯.


Trust trust_of(const Source& src,
               std::string_view content_sha,
               const Approvals& approvals) noexcept {
    // 明确 / 用户: 人类自己将这个文件放置在了暗中信任的处子
    // ( 两者都在克隆的仓库 的接触范围之外 ).
    if (src.locus == Locus::Explicit || src.locus == Locus::User)
        return Trusted{};

    // 项目/当地:参加 工作区。 信任的( ) 仅 何时 此 准确内容散
    // 列已获批准——互换 字节 批准无效(MCPoison / CVE
    // -2025-54136 固定,称为行为。
    // "nothing executable to vouch for" 并按核准数处理。
    if (content_sha.empty() || approvals.approved(content_sha))
        return Trusted{};
    return Pending{std::string{content_sha}};
}

}  // namespace agentty::scope
