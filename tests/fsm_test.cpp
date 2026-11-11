// fsm_test ——通用型国产机械在境内单位覆盖.
// include/agentty/io/fsm.hpp.
//
// 电话 头文件 是那个 编译-time 核心资源 agentty's
// IO/network 类型状态图案( HTTP/2 连接寿命周期
// src/io/http.cpp 测试结果将保证三者 头文件 使:
//
//   1. 边缘合法性是纯粹的 编译-time 由每一实体产生的关系
//      状态在 -class `fsm_to` 后续列表。 已声明的边值读取为真;
//      其他所有( 包括跳过)。 -ahead 和反向边缘)读作虚伪。
//      A 终端 状态( 无) `fsm_to` )没有向外边缘。
//
//   2. `assert_legal_edge<From,To>()` 编译 非法的,因为非法的
//      边缘会很难的 编译 错误,所以不能在 运行时 我们
//      把基本特征 定下来,这就是什么 static_assert
//      键关上。 )
//
//   3. 国家正在移动 -only 能力符号: 非 -copyable 移动 -only ,以及
//      其破坏器释放了完全一次拥有的资源。
//      消耗状态的过渡和放弃状态的失败都具有决定性的
//      清理作用 — — 拨号管所依赖的RAII纪律。
//
// 没有 传输层 ,没有网络:这是孤立的机器。

#include "agtest.hpp"

#include "agentty/io/fsm.hpp"

#include <cassert>
#include <expected>
#include <string>
#include <type_traits>
#include <vector>

namespace fsm = agentty::io::fsm;

// ---------------------------------------------------------------------------
// 反射拨号生命周期形状的玩具 4-state 线性机器:
//   A ─▶ B ─▶ C ─▶ D(
// 终端 )加一个分支状态 R {B, C} 来进行多功能 -successor 列表中。
// ---------------------------------------------------------------------------
struct A;
struct B;
struct C;
struct D;
struct R;

struct A : fsm::State<struct ATag> { using fsm_to = fsm::to<B>; };
struct B : fsm::State<struct BTag> { using fsm_to = fsm::to<C>; };
struct C : fsm::State<struct CTag> { using fsm_to = fsm::to<D>; };
struct D : fsm::State<struct DTag> { /* 终端 : 无 fsm_to */ };
struct R : fsm::State<struct RTag> { using fsm_to = fsm::to<B, C>; };

// ─── 1. 边缘合法性是一种 编译-time 关系 ───────────────────────────
// 法(宣)边.
static_assert(fsm::is_legal_edge_v<A, B>);
static_assert(fsm::is_legal_edge_v<B, C>);
static_assert(fsm::is_legal_edge_v<C, D>);
// 分支:两个继承人都是合法的.
static_assert(fsm::is_legal_edge_v<R, B>);
static_assert(fsm::is_legal_edge_v<R, C>);

// 非法: 跳过-ahead.
static_assert(!fsm::is_legal_edge_v<A, C>);
static_assert(!fsm::is_legal_edge_v<A, D>);
static_assert(!fsm::is_legal_edge_v<B, D>);
// Illegal: reverse.
static_assert(!fsm::is_legal_edge_v<B, A>);
static_assert(!fsm::is_legal_edge_v<C, B>);
// 非法: 自行-loop(从未申报)。
static_assert(!fsm::is_legal_edge_v<A, A>);
// 非法:任何离开终端州的东西.
static_assert(!fsm::is_legal_edge_v<D, A>);
static_assert(!fsm::is_legal_edge_v<D, C>);
// 分局状态有 不 越来越紧张,它没有宣布。
static_assert(!fsm::is_legal_edge_v<R, D>);
static_assert(!fsm::is_legal_edge_v<R, A>);

// ─── 国家-trait 理智 ────────────────────────────────────────────────────
static_assert(fsm::is_state_v<A>);
static_assert(fsm::is_state_v<D>);
struct NotAState {};
static_assert(!fsm::is_state_v<NotAState>);

// 国家正在移动 -only.
static_assert(!std::is_copy_constructible_v<A>);
static_assert(!std::is_copy_assignable_v<A>);
static_assert(std::is_move_constructible_v<A>);

// ─── 2. assert_legal_edge 编译 法律优势 ─────────────────────────
// (叫它非法的边缘很难) 编译 设计错误,所以负数 被"! is_legal_edge_v
// static_asserts (b) 将第1款(b)项改为(b)款(b)(一)项(b)目(b)
void edge_guards_compile() {
    fsm::assert_legal_edge<A, B>();
    fsm::assert_legal_edge<B, C>();
    fsm::assert_legal_edge<C, D>();
    fsm::assert_legal_edge<R, B>();
    fsm::assert_legal_edge<R, C>();
}

// ---------------------------------------------------------------------------
// 3. 移动 -only RAII : 一个资源 -owning 状态运行 清理准确一次,
//    无论是成功过渡还是失败时 都消耗了它
// ---------------------------------------------------------------------------
static std::vector<std::string>* g_log = nullptr;

struct Err { std::string why; };

// 拥有一个命名为"handle";除非发布,否则记录销毁情况。
struct Owning : fsm::State<struct OwningTag> {
    using fsm_to = fsm::to<struct Owning2>;
    std::string name;
    bool        live = false;

    explicit Owning(std::string n) : name(std::move(n)), live(true) {}
    Owning(Owning&& o) noexcept
        : name(std::move(o.name)), live(o.live) { o.live = false; }
    Owning& operator=(Owning&&) = delete;
    ~Owning() { if (live && g_log) g_log->push_back("~" + name); }

    void release() noexcept { live = false; }
};
struct Owning2 : fsm::State<struct Owning2Tag> { std::string name; };

static_assert(fsm::is_legal_edge_v<Owning, Owning2>);

// 过渡 SUCCEEDS :消耗Owning, 手握手柄Owning2。
fsm::Result<Owning2, Err> advance_ok(Owning&& s) {
    fsm::assert_legal_edge<Owning, Owning2>();
    s.release();                       // 所有权 已转发
    return Owning2{ {}, std::move(s.name) };
}

// 过渡 FAILS : 返回出乎意料; 消耗的 Owning 的
// dtor 必须开火( 清除部分 -built 资源)。
fsm::Result<Owning2, Err> advance_fail(Owning&& s) {
    fsm::assert_legal_edge<Owning, Owning2>();
    (void)s;                           // 未发布 ~ ~ 正在运行日志
    return std::unexpected(Err{ "boom" });
}

TEST_CASE("fsm") {
    edge_guards_compile();

    // 成功过渡:没有清理日志(手移,源代码被发布;
    //     Owning2不日志).
    {
        std::vector<std::string> log;
        g_log = &log;
        auto r = advance_ok(Owning{ "conn-ok" });
        assert(r.has_value());
        assert(r->name == "conn-ok");
        assert(log.empty() && "released handle must not run cleanup");
        g_log = nullptr;
    }

    // (b) 过渡失败:消耗的代号的破坏器释放了整整
    //     一次半-built资源。
    {
        std::vector<std::string> log;
        g_log = &log;
        auto r = advance_fail(Owning{ "conn-fail" });
        assert(!r.has_value());
        assert(r.error().why == "boom");
        assert(log.size() == 1 && log[0] == "~conn-fail"
               && "dropped token must run cleanup exactly once");
        g_log = nullptr;
    }

    // (c) 管道线化:预期的寺院 and_then 通过法律链条取得进展,
    //     简略 -circuiting 第一次失败时,模拟 dial_new(...).
    //     and_then(...).and_then (.)形状。
    {
        std::vector<std::string> log;
        g_log = &log;
        // A-ok-(然后是失败的一步)不会漏水:每个消耗的符
        // 物要么向前移动,要么被摧毁。
        auto pipeline =
            advance_ok(Owning{ "p" })
                .and_then([](Owning2&& s) -> fsm::Result<Owning2, Err> {
                    // 否 否 : -op "transition" 刚刚向前
                    return Owning2{ {}, std::move(s.name) };
                });
        assert(pipeline.has_value());
        assert(pipeline->name == "p");
        assert(log.empty());
        g_log = nullptr;
    }
}
