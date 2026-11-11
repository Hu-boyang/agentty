// scope_test - 配置图 -resolution 代数( 4) include/agentty/scope/scope.hpp).
//
// 以伪造的Env值(纯度合同的全部要点:没有文件系统,没有geenv)
// 来锻炼纯表面。 plan() ,两个 归并 单体( 单体)
// resolve_first 覆盖, resolve_union 阴影
// +证明),和内容 -bound 信任(MCPoison固定)。

#include "agtest.hpp"

#include "agentty/scope/scope.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace agentty::scope;

namespace {

// 既带有项目根又具有家的Env——所以plan()发布完整的Project×
// Dialect QQ用户×Dialict 梯级.
Env full_env() {
    Env e;
    e.home             = "/home/u";
    e.project_root     = "/work/repo";
    e.project_writable = true;
    return e;
}

// 内存 是一个 NATIVE-ONLY 关切: memory.jsonl 谓 agentty
// 其自成格式,所以其梯形为每蝗1个方言.
const Layout kMem{.leaf = "memory.jsonl"};

// 技能是: PORTABLE : 不变 SKILL.md 使用另一个工具,
// 因此这个梯子选择了 Interop 方言。 下面的大多数断言都使用这个工具,
// 因为它可以操控 Locus × Dialect 的完整产品。
const Layout kSkills{.leaf = "skills", .dialects = kPortable};

}  // 命名空间结束。

// ── plan():优先顺序+出处 ─────────────────────────────
static void plan_orders_by_precedence() {
    auto srcs = plan(kSkills, full_env());

    // 项目(3个方言)然后是用户(3个语方言)=6,当地没有排放.
    assert(srcs.size() == 6 && "project×3 ▷ user×3, no local");

    // Locus是主要的关键:每个项目源都先于每个用户源.
    bool seen_user = false;
    for (const auto& s : srcs) {
        if (s.locus == Locus::User) seen_user = true;
        assert(!(seen_user && s.locus == Locus::Project)
               && "project must never come after user");
    }

    // 对话是次要的关键:在项目内,土生土长(.agentty)是第一。
    assert(srcs[0].locus == Locus::Project && srcs[0].dialect == Dialect::Agentty);
    assert(srcs[0].base == std::filesystem::path{"/work/repo/.agentty"});
    assert(srcs[1].dialect == Dialect::Agents);
    assert(srcs[2].dialect == Dialect::Claude);
    assert(srcs[3].locus == Locus::User && srcs[3].dialect == Dialect::Agentty);
    assert(srcs[3].base == std::filesystem::path{"/home/u/.agentty"});

    // 只有土生土长的项目可以写(内存的守护,概括).
    assert(srcs[0].writable && "project .agentty writable");
    assert(!srcs[1].writable && "interop project dirs never a write target");
}

// 根词 "/" 属于非限制性 -access 哨兵,从没有地方散开。
// agentty 完全没有项目来源。
static void plan_rejects_filesystem_root() {
    Env e = full_env();
    e.project_root = "/";
    auto srcs = plan(kSkills, e);
    for (const auto& s : srcs)
        assert(s.locus != Locus::Project && "\"/\" yields no project locus");
    assert(srcs.size() == 3 && "only user×3 remain");
}

// 明确( $ENV-pointed ) 配置比一切,方言都高 -agnostic.
static void plan_explicit_wins() {
    Env e = full_env();
    e.explicit_config = std::filesystem::path{"/etc/agentty/mcp.json"};
    auto srcs = plan(kMem, e);
    assert(srcs.front().locus == Locus::Explicit);
    assert(srcs.front().base == std::filesystem::path{"/etc/agentty"});
}

// 没有 HOME (cron/systemd − 没有用户来源; 归并 千万别崩溃
static void plan_no_home() {
    Env e = full_env();
    e.home.clear();
    auto srcs = plan(kSkills, e);
    for (const auto& s : srcs)
        assert(s.locus != Locus::User);
    assert(srcs.size() == 3 && "only project×3 remain");
}

// ── 视宽度为每 -FEATURE ,在布局上声明 ───────────────
//
// 这不是变量 使梯子诚实: plan() 完全照
// 呼叫者所读的来电,所以什么也没有 下游 它会把每
// 个功能和语言的三种方言都释放出来 MCP 之后扔了两个,
// 这意味着 plan() 报告有6个来源,
// 其中3个曾读过。
static void plan_honours_layout_dialects() {
    // 原生的。 -only (默认):每个蝗虫一个方言,它就是. agentty.
    auto mem = plan(kMem, full_env());
    assert(mem.size() == 2 && "native-only: project ▷ user, one each");
    for (const auto& s : mem)
        assert(s.dialect == Dialect::Agentty && "no interop dirs for a native format");
    assert(mem[0].locus == Locus::Project);
    assert(mem[0].base == std::filesystem::path{"/work/repo/.agentty"});
    assert(mem[1].locus == Locus::User);
    assert(mem[1].base == std::filesystem::path{"/home/u/.agentty"});

    // 便携式:全品,同型嵌套.
    assert(plan(kSkills, full_env()).size() == 6 && "portable: project×3 ▷ user×3");

    // 一种没有方言名称的布局是调用错误,而不是一无所获的指令——
    // 空阶梯会读作"this feature has no config",
    // 并默默地禁用它。 回到原生默认状态。
    const Layout empty{.leaf = "x", .explicit_env = {}, .dialects = {}};
    auto e = plan(empty, full_env());
    assert(e.size() == 2 && "empty dialect span falls back to native");
    for (const auto& s : e) assert(s.dialect == Dialect::Agentty);

    // 明确是方言-agnostic:它无论走哪条路都高于产品,
    // 所以缩小宽度不能花费一个特征的逃生舱。
    Env env = full_env();
    env.explicit_config = std::filesystem::path{"/etc/agentty/memory.jsonl"};
    auto with_explicit = plan(kMem, env);
    assert(with_explicit.size() == 3 && "explicit + native project + native user");
    assert(with_explicit.front().locus == Locus::Explicit);
}

// ── resolve_first: 覆盖单体 ────────────────────────────────────────
static void resolve_first_picks_first_present() {
    // 假装只有 User/native 来源 "has a value"。
    // 该覆盖 归并 仍然必须返回 首次 以优先排序显示 —— 这里
    // 没有项目源( nullopt) , 因此用户胜出, 标记正确。
    auto loader = [](const Source& s) -> Result<std::optional<std::string>> {
        if (s.locus == Locus::User && s.dialect == Dialect::Agentty)
            return std::optional<std::string>{"from-user"};
        return std::optional<std::string>{std::nullopt};  // 缺席
    };
    auto r = resolve_first<std::string>(kMem, full_env(), loader);
    assert(r.has_value());
    assert(r->value == "from-user");
    assert(r->source.locus == Locus::User && "provenance preserved");
}

static void resolve_first_prefers_higher_locus() {
    // 项目和用户"present" × 项目(更早)均获奖.
    auto loader = [](const Source& s) -> Result<std::optional<std::string>> {
        if (s.dialect != Dialect::Agentty)
            return std::optional<std::string>{std::nullopt};
        return std::optional<std::string>{
            std::string{to_string(s.locus)} + "-hit"};
    };
    auto r = resolve_first<std::string>(kMem, full_env(), loader);
    assert(r.has_value());
    assert(r->value == "project-hit" && "override picks the strongest locus");
}

static void resolve_first_none_is_error() {
    auto loader = [](const Source&) -> Result<std::optional<std::string>> {
        return std::optional<std::string>{std::nullopt};
    };
    auto r = resolve_first<std::string>(kMem, full_env(), loader);
    assert(!r.has_value());
    assert(r.error().kind == ErrorKind::NoSource);
}

static void resolve_first_propagates_parse_error() {
    // 礼物 -but-broken 源短 -circuits (失声).
    auto loader = [](const Source& s) -> Result<std::optional<std::string>> {
        if (s.locus == Locus::Project && s.dialect == Dialect::Agentty)
            return fail(ErrorKind::ParseFailed, "bad json");
        return std::optional<std::string>{std::nullopt};
    };
    auto r = resolve_first<std::string>(kMem, full_env(), loader);
    assert(!r.has_value());
    assert(r.error().kind == ErrorKind::ParseFailed);
}

// ── resolve_union:阴影+来源 ────────────────────────────────────
static void resolve_union_shadows_by_key() {
    // 每个本地来源都产生两个项目: "shared" 键和蝗虫
    // -unique 键。 -key-wins → "shared" 与
    // 最强的蝗虫(项目)决裂,每个独特的钥匙存活一次,并贴上原产地标签。
    auto keyfn  = [](const std::string& v) { return v.substr(0, v.find(':')); };
    auto loader = [](const Source& s) -> Result<std::vector<std::string>> {
        if (s.dialect != Dialect::Agentty) return std::vector<std::string>{};
        std::string tag{to_string(s.locus)};
        return std::vector<std::string>{"shared:" + tag, tag + ":only"};
    };
    auto r = resolve_union<std::string>(kMem, full_env(), keyfn, loader);
    assert(r.has_value());

    // "shared" 一次,从胜(项目)的蝗虫。
    int shared = 0;
    for (const auto& t : *r)
        if (keyfn(t.value) == "shared") {
            ++shared;
            assert(t.value == "shared:project" && "project shadows user");
            assert(t.source.locus == Locus::Project);
        }
    assert(shared == 1 && "shadowed to a single winner");

    // 两个独特的密钥都存活下来: 共享(1)+项目:只 + 用户:只 = 3.
    assert(r->size() == 3);
}

// ── trust_of : 内容 -bound 而不是蝗虫 而不是蝗灾 -inferred (麦克布森) ────────────────
static void trust_user_is_implicit() {
    Source user{.locus = Locus::User};
    assert(std::holds_alternative<Trusted>(trust_of(user, "deadbeef", {})));
}

static void trust_project_pending_until_approved() {
    Source proj{.locus = Locus::Project};
    Approvals appr;

    // 未经核准的项目内容 × 待处理, 携带散列待批准.
    auto t0 = trust_of(proj, "hash-A", appr);
    assert(std::holds_alternative<Pending>(t0));
    assert(std::get<Pending>(t0).content_sha == "hash-A");

    // 批准THAT high → 信任。
    appr.approve("hash-A");
    assert(std::holds_alternative<Trusted>(trust_of(proj, "hash-A", appr)));

    // MCPoison:将内容换成批准名称 + 批准号为VOID.
    auto t2 = trust_of(proj, "hash-B", appr);
    assert(std::holds_alternative<Pending>(t2)
           && "changed content must re-gate trust");
}

TEST_CASE("scope") {
    plan_orders_by_precedence();
    plan_rejects_filesystem_root();
    plan_explicit_wins();
    plan_no_home();
    plan_honours_layout_dialects();

    resolve_first_picks_first_present();
    resolve_first_prefers_higher_locus();
    resolve_first_none_is_error();
    resolve_first_propagates_parse_error();

    resolve_union_shadows_by_key();

    trust_user_is_implicit();
    trust_project_pending_until_approved();
}
