// stats_test - 电话 归并 ,时代的守护者,和标签表。
//
// 面板的全部值是它的数值是TRUE. 能够从实际
// 发生的情况中漂移的数据视图比没有视图更糟糕,
// 因为用户无法分辨哪些数字可以信任。这些测试将使数字
// 具有可信度的属性标定在了:
//
//   • 股份总额1比其申报分母多(无四舍五入漏漏出)
//   • 分母说明并一致
//   • 根本没有运行的转弯完全不计算在内
//   • Smart-Mode-off转弯是他们自己的桶,从未折叠成
//     战略性——使delegated_share可计算
//   • 未计量的转折不包括在平均数中,不计为零
//   • 递增 归并 同意全面重建, 始终 - 包括:
//     跨越三段历史 -mutating 业务
//
// 纯洁是为什么这一切是 检验没有 Model, a 传输 或时钟 :
// 归并是记录的功能, 而不是其他的。

#include "agtest.hpp"

#include "agentty/domain/stats/facts.hpp"
#include "agentty/domain/stats/tabs.hpp"
#include "agentty/domain/stats/unit.hpp"

#include <chrono>
#include <string>
#include <vector>

using agentty::Message;
using agentty::ModelId;
using agentty::Role;
using agentty::Thread;
using agentty::smart::ModelRole;
namespace st = agentty::stats;

namespace {

// 有个助手转过来 RAN : 印有出处, 如 launch_stream 已经。
Message served(const char* model, ModelRole role) {
    Message m;
    m.role = Role::Assistant;
    m.served_model = ModelId{model};
    m.served_role = role;
    return m;
}

// 与Smart Mode OFF运行的助理转弯:一个模式但无角色.
Message unrouted(const char* model) {
    Message m;
    m.role = Role::Assistant;
    m.served_model = ModelId{model};
    return m;
}

Message user_turn() {
    Message m;
    m.role = Role::User;
    m.text = "do the thing";
    return m;
}

// 进 -flight 占位符:从未发送过的助理消息.
Message placeholder() {
    Message m;
    m.role = Role::Assistant;
    return m;
}

Message with_usage(std::uint32_t in, std::uint32_t out,
                   std::uint32_t cache_r = 0, std::uint32_t cache_w = 0) {
    Message m = unrouted("m1");
    Message::Telemetry t;
    t.input_tokens  = in;
    t.output_tokens = out;
    t.cache_read     = cache_r;
    t.cache_creation = cache_w;
    t.ttft_ms   = 100;
    t.stream_ms = 900;
    m.telemetry = t;
    return m;
}

Thread thread_of(std::vector<Message> msgs) {
    Thread t;
    t.id = agentty::ThreadId{"t1"};
    t.messages = std::move(msgs);
    return t;
}

// 完全重建,为以下差分测试.
st::Facts rebuild(const Thread& t) {
    st::Projection p;
    return p.refresh(t);
}

[[nodiscard]] std::uint64_t count_of(const st::Tally& tally,
                                     std::string_view label) {
    for (const auto& [k, v] : tally.rows())
        if (k == label) return v;
    return 0;
}

}  // 命名空间结束。

// ── 电话 归并 ─────────────────────────────────────────────────────────────

TEST_CASE("stats: an empty transcript has no turns and does not divide") {
    const auto f = rebuild(thread_of({}));
    CHECK(f.session.assistant_turns == 0);
    CHECK(f.smart.routed == 0);
    CHECK(f.smart.unrouted == 0);
    CHECK(f.models.by_model.empty());
}

TEST_CASE("stats: a turn that never ran is not counted") {
    // 进 -flight 占位符从用户
    // 提交时算起, 就会使用户整个观察
    // 时间的每股都出现错误。
    const auto f = rebuild(thread_of({user_turn(), placeholder()}));
    CHECK(f.models.by_model.empty());
    CHECK(f.smart.routed == 0);
    // 来,来,来 IS 助理转弯,会话标签上写着
    CHECK(f.session.assistant_turns == 1);
    CHECK(f.session.user_turns == 1);
}

// ── Tool 时间: EXECUTION 窗口,不是卡片的命 ─────────────

// 一张卡片诞生于 T0 和发送日期: T_exec.
// 镜像真实生命周期 : StreamToolUseStart ——(可选为长) 权限 等待)
// ~ 运行{ executing_since – 说定了 – Done.
[[nodiscard]] Message tool_exec_ms(std::uint64_t exec_ms) {
    Message m = unrouted("m1");
    const auto base = std::chrono::steady_clock::time_point{
        std::chrono::milliseconds{1}};
    agentty::ToolUse tc;
    tc.id   = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    // 卡诞生的"long before"执行;无论该间隔是什么,
    // 只有执行窗口可以到达数据。
    const auto born = base - std::chrono::milliseconds{300'000};
    agentty::ToolUse::Done done{born,
                       base + std::chrono::milliseconds{
                           static_cast<std::int64_t>(exec_ms)},
                       "+ok"};
    done.executing_since = base;   // 分派 邮票, 之后 output/images
    tc.status = std::move(done);
    m.tool_calls.push_back(std::move(tc));
    return m;
}

TEST_CASE("stats: tools are billed for execution, not the approval wait") {
    // 这张卡在发送前就已经存在300秒(一个停在 权限 提示词 )
    // 折叠 started_at→finished_at 报告等待
    // 工具时间——会话标签上的2h20m类谎言
    // "where the time went" 铃声。只有执行窗口才算数。
    const auto f = rebuild(thread_of({tool_exec_ms(1'500)}));
    CHECK(f.tools.latency.count() == 1);
    CHECK(f.tools.latency.sum()   == 1'500);
    CHECK(f.tools.latency.max()   == 1'500);
}

TEST_CASE("stats: a tool that never dispatched contributes no tool time") {
    // 被拒绝 -without-run 中断( 崩溃中点) -turn )工具不带
    // 执行窗口。0是一个声明。 "no executions happened"
    // - 样本不能平均输入。
    Message m = unrouted("m1");
    agentty::ToolUse tc;
    tc.id = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    tc.status = agentty::ToolUse::Rejected{
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{5}}};
    m.tool_calls.push_back(std::move(tc));

    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK(f.tools.latency.count() == 0);
    CHECK(f.tools.total == 1);  // 仍计为CALL
}

TEST_CASE("stats: pre-exec-window threads fall back to no tool time, not garbage") {
    // A 线程 执行窗口前的文件以 DoneQQ,QQ,
    // output} 的形式重新水合。。。 归并 必须跳过它,
    // 而不是计算一个假的跨度: 工具 TIME 缺少行时 调用 COUNT
    // 保持完整,保持冷静。
    Message m = unrouted("m1");
    agentty::ToolUse tc;
    tc.id = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    tc.status = agentty::ToolUse::Done{{}, {}, std::string{"+ok"}};
    m.tool_calls.push_back(std::move(tc));

    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK(f.tools.latency.count() == 0);
    CHECK(f.tools.total == 1);
}

// ── 设置工具不能 LAUNDER 纸牌 -birth 输入执行章 ───
//
// 两个接入者有对立的语义,头文件说:
//   executing_since () 活性 -- -- 回归 started_at 故
//                      卡片的过期定时器总是有价值
//   exec_window_at (a) 会计 -- -- 仅a RECORDED 分派 计数
//
// 每个结算站点都会复制盖章,盖章覆盖状态。如果复制时带有
// executing_since (),然后是工具 绝不 已发出 --
// -- 仍 待处理 或 解决时获得批准 -- -- 取回 回退 纸牌出生,
// 并写到 Failed::executing_since 好像这是
// 真的一样 分派. exec_window_at 然后,见一个非人,
// -zero 整个卡片寿命: 权限 等待,队列,全部。 执行窗口是用来防止,
// 重新 -entering 穿过转弯时的路-- 拒绝, 取消,
// 看门狗, 数据流 死亡, 提供方 重试.
// 只有轮次间什么都不出错时才能工作。
//
// 这驱动了对真实类型的和解规则,而不是通过状态更新函数,
// 因为错误是读取器的,而这一点在这里是完全可
// 观察到的.
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] Clock::time_point at_ms(std::int64_t ms) {
    return Clock::time_point{std::chrono::milliseconds{ms}};
}

// 现在每个定居地都做什么: ToolUse::settle_failed。
// 驱动通过 真实 帮助者而不是再 -implementation
// 如果帮助者回到读取活性访问器时 就会失败
Message settle_failed(agentty::ToolUse::Status before, Clock::time_point now) {
    Message m = unrouted("m1");
    agentty::ToolUse tc;
    tc.id = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    tc.status = std::move(before);
    tc.settle_failed(now, "rejected");
    m.tool_calls.push_back(std::move(tc));
    return m;
}

// 预告 -fix 结算, 保存 仅 做为负控:用
// 活性存取器捕捉,覆盖,回写——~17站点用手做的.
Message settle_failed_the_old_way(agentty::ToolUse::Status before,
                                  Clock::time_point now) {
    Message m = unrouted("m1");
    agentty::ToolUse tc;
    tc.id = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    tc.status = std::move(before);
    const auto stamp = tc.executing_since();
    tc.status = agentty::ToolUse::Failed{tc.started_at(), now, "rejected"};
    std::get<agentty::ToolUse::Failed>(tc.status).executing_since = stamp;
    m.tool_calls.push_back(std::move(tc));
    return m;
}

}  // 命名空间结束。

TEST_CASE("stats: a tool rejected at the prompt bills no tool time") {
    // 纸牌生于 t=1s, 用户盯着 权限 提示词 300秒,然后拒绝,
    // 它从未运行。 Tool 时间必须 ZERO.
    auto m = settle_failed(agentty::ToolUse::Pending{at_ms(1'000)}, at_ms(301'000));
    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK_MESSAGE(f.tools.latency.count() == 0,
                  "a never-dispatched tool has no execution window");
    CHECK(f.tools.total == 1);   // 还是 CALL
}

TEST_CASE("stats: an approved-but-queued tool that is cancelled bills nothing") {
    // 批准,然后是分派之前的已取消——这批货仍然被踢,同样的规则,
    // 是不同的-dispatch前状态.
    auto m = settle_failed(agentty::ToolUse::Approved{at_ms(1'000)}, at_ms(90'000));
    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK(f.tools.latency.count() == 0);
}

TEST_CASE("stats: the old settle laundered the approval wait") {
    // 负控,以及上述测试存在的原因。 executing_since
    // () -- -- 现场访问器 -- -- 将卡片出生
    // 变成假 分派 和300美元在德国 提示词 以工具时间返回。
    // 如果报告中断了300秒, 访问者的语义会改变,
    // 上面的测试需要重新进行。 -reading.
    auto m = settle_failed_the_old_way(agentty::ToolUse::Pending{at_ms(1'000)},
                                       at_ms(301'000));
    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK(f.tools.latency.count() == 1);
    CHECK(f.tools.latency.sum() == 300'000);
}

TEST_CASE("stats: a tool that DID run keeps its window through a failed settle") {
    // 另一半:固定不能失去真正的窗口。 等待300秒后, 发送时间为301秒,
    // 被杀死 看门狗 at t=303.5s. 数据输入系统 Tool
    // 时间是它实际运行的2.5秒。
    agentty::ToolUse::Running run{};
    run.started_at = at_ms(1'000);
    run.executing_since = at_ms(301'000);

    auto m = settle_failed(std::move(run), at_ms(303'500));
    const auto f = rebuild(thread_of({std::move(m)}));
    CHECK(f.tools.latency.count() == 1);
    CHECK(f.tools.latency.sum() == 2'500);
}

TEST_CASE("stats: settling twice is idempotent on the window") {
    // 工具可以安顿下来,然后被后期的扫荡击中( 取消 之后的 看门狗。
    // 失败的窗口,以及 exec_window_at ()读取失败,
    // 所以第二个和解必须保留 -- -- 而不是掉落或重置 -- --。
    agentty::ToolUse tc;
    tc.id = agentty::ToolCallId{"call1"};
    tc.name = agentty::ToolName{"shell"};
    agentty::ToolUse::Running run{};
    run.started_at = at_ms(1'000);
    run.executing_since = at_ms(301'000);
    tc.status = std::move(run);
    tc.settle_failed(at_ms(303'500), "wedged");
    tc.settle_failed(at_ms(400'000), "cancelled");
    REQUIRE(tc.exec_window_at().has_value());
    CHECK(*tc.exec_window_at() == at_ms(301'000));
}

TEST_CASE("stats: routed and unrouted are separate buckets") {
    // 该 属性。智能 -Mode-off 转而没有作用;
    // 把它算作战略会夸大旗舰的股份,
    // 而放下它会低估分母。 它会得到自己的桶。
    const auto f = rebuild(thread_of({
        served("sonnet", ModelRole::Strategic),
        served("haiku",  ModelRole::Utility),
        unrouted("gpt-5"),
    }));
    CHECK(f.smart.routed == 2);
    CHECK(f.smart.unrouted == 1);
    CHECK(f.smart.routed + f.smart.unrouted == f.session.assistant_turns);
    // 模型标签计数 全部 三个 - 这是一种描述性,而不是一种判决。
    CHECK(f.models.by_model.total() == 3);
}

TEST_CASE("stats: delegated counts routed turns below Strategic") {
    const auto f = rebuild(thread_of({
        served("sonnet", ModelRole::Strategic),
        served("sonnet", ModelRole::Strategic),
        served("glm",    ModelRole::Implementation),
        served("haiku",  ModelRole::Utility),
        unrouted("gpt-5"),          // 必须在NOT 移动数字或分母
    }));
    CHECK(f.smart.routed == 4);
    CHECK(f.smart.delegated == 2);
}

TEST_CASE("stats: role shares sum to one over routed turns") {
    // 不准漏掉:零件是计数的,而且准确计算总和。
    const auto f = rebuild(thread_of({
        served("a", ModelRole::Strategic),
        served("b", ModelRole::Implementation),
        served("c", ModelRole::Utility),
        unrouted("d"),
    }));
    CHECK(f.smart.by_role.total() == f.smart.routed);
}

TEST_CASE("stats: an all-Strategic session delegates nothing") {
    const auto f = rebuild(thread_of({
        served("sonnet", ModelRole::Strategic),
        served("sonnet", ModelRole::Strategic),
    }));
    CHECK(f.smart.routed == 2);
    CHECK(f.smart.delegated == 0);
}

TEST_CASE("stats: smart routing cards are bookkeeping, not turns") {
    // 路由卡是合成的助理信息,计算它会充电
    // 面板上的每一个分母.
    Message card;
    card.role = Role::Assistant;
    card.smart_routing = true;
    const auto f = rebuild(thread_of({user_turn(), card,
                                      served("a", ModelRole::Strategic)}));
    CHECK(f.session.assistant_turns == 1);
    CHECK(f.smart.routed == 1);
}

TEST_CASE("stats: unmeasured turns stay out of the averages") {
    // 没有遥测的转弯 "not measured" ,
    // 它与 "measured as zero"。在虚构的零将低估每个
    // -turn 数字在a 线程 带有预 -telemetry 历史
    const auto f = rebuild(thread_of({
        with_usage(1000, 500),
        unrouted("old"),            // 无遥测: 字段前
    }));
    CHECK(f.session.assistant_turns == 2);
    CHECK(f.session.measured_turns == 1);
    CHECK(f.tokens.output == 500);
}

TEST_CASE("stats: the cache split accounts for every prefix token") {
    // input_tokens EXCLUDES 编号 缓存 字
    // 段上显示 传输 ,所以三桶在前缀中分割,没有算术,也没有发明.
    const auto f = rebuild(thread_of({with_usage(300, 100, 600, 100)}));
    CHECK(f.cache.hits == 600);
    CHECK(f.cache.writes == 100);
    CHECK(f.cache.misses == 300);
    CHECK(f.context.peak_input == 1000);
    CHECK(f.cache.turns_with_cache == 1);
}

// ── 递增的归并 ─────────────────────────────────────────────────

TEST_CASE("stats: incremental refresh equals a full rebuild") {
    // 核心索赔。如果这些有分歧,小组会显示取决于WHEN的数字,
    // 您打开它。
    st::Projection p;
    Thread t = thread_of({});
    for (int i = 0; i < 12; ++i) {
        t.messages.push_back(user_turn());
        t.messages.push_back(i % 3 == 0 ? served("sonnet", ModelRole::Strategic)
                                        : with_usage(100, 50, 20));
        const auto& inc = p.refresh(t);
        const auto  full = rebuild(t);
        CHECK(inc.session.assistant_turns == full.session.assistant_turns);
        CHECK(inc.session.user_turns == full.session.user_turns);
        CHECK(inc.tokens.output == full.tokens.output);
        CHECK(inc.smart.routed == full.smart.routed);
        CHECK(inc.cache.hits == full.cache.hits);
    }
}

TEST_CASE("stats: the live tail is re-folded, never sealed") {
    // 投影中的错误将替换: a { message_count,
    // thread_id 邮票看不到 最后 消息变异,
    // 所以在它下面的计数器在用户观察它移动时会完全冻结.
    st::Projection p;
    Thread t = thread_of({user_turn(), with_usage(100, 10)});
    CHECK(p.refresh(t).tokens.output == 10);

    // 同条信息计数——数据流号刚刚在它上落下更多的标志.
    t.messages.back().telemetry->output_tokens = 90;
    CHECK(p.refresh(t).tokens.output == 90);
}

TEST_CASE("stats: rewind rebuilds instead of keeping a stale prefix") {
    st::Projection p;
    Thread t = thread_of({with_usage(100, 10), with_usage(100, 20),
                          with_usage(100, 30)});
    CHECK(p.refresh(t).tokens.output == 60);

    t.messages.pop_back();          // 倒向检查站
    t.messages.pop_back();
    CHECK(p.refresh(t).tokens.output == 10);
}

TEST_CASE("stats: switching threads rebuilds") {
    st::Projection p;
    Thread a = thread_of({with_usage(100, 10)});
    CHECK(p.refresh(a).tokens.output == 10);

    Thread b = thread_of({with_usage(100, 77)});
    b.id = agentty::ThreadId{"t2"};
    CHECK(p.refresh(b).tokens.output == 77);
}

TEST_CASE("stats: compaction rebuilds") {
    // 压缩 REWRITES 添加此加法是怎样
    // 的 归并 开始生成错误和相信的数字。
    st::Projection p;
    Thread t = thread_of({with_usage(100, 10), with_usage(100, 20)});
    CHECK(p.refresh(t).tokens.output == 30);

    t.compactions.push_back({});
    t.messages = {with_usage(100, 5)};
    CHECK(p.refresh(t).tokens.output == 5);
    CHECK(p.refresh(t).context.compactions == 1);
}

// ── 塔利和赫斯特语Name ───────────────────────────────────────────────────────

TEST_CASE("stats: a tally ranks by count and keeps ties stable") {
    st::Tally t;
    t.add("first", 5);
    t.add("second", 5);
    t.add("third", 9);
    const auto r = t.ranked();
    REQUIRE(r.size() == 3u);
    CHECK(r[0].first == "third");
    // 等值计数保留 INSERTION 顺序,
    // 所以行不互换位置 帧 时 数据流 运行。
    CHECK(r[1].first == "first");
    CHECK(r[2].first == "second");
}

TEST_CASE("stats: a histogram reports quantiles without storing samples") {
    st::Hist h;
    for (int i = 0; i < 100; ++i) h.add(i < 95 ? 10u : 5000u);
    CHECK(h.count() == 100);
    CHECK(h.max() == 5000);
    // p50坐在快桶里,p95坐在慢桶里——这是
    // 整个点:259ms的平均值将描述两个人口.
    CHECK(h.quantile(0.5) <= 16.0);
    CHECK(h.quantile(0.95) >= 1024.0);
}

TEST_CASE("stats: quantiles separate within one octave") {
    // 平面的对数2桶每一步翻两番 所以一个50和95 在同一八
    // 进制的任何地方 都报告 相同 数字——按实数计算 线程 以
    // "Median 4.1s, p95 4.1s" ,
    // 它读作错误,但不告诉用户。 -buckets 就是他们分开的原因
    st::Hist h;
    for (int i = 0; i < 90; ++i) h.add(4200);    // 刚刚超过4096年
    for (int i = 0; i < 10; ++i) h.add(7900);    // 仍低于8192岁
    CHECK(h.quantile(0.5) < h.quantile(0.95));
}

TEST_CASE("stats: a quantile never exceeds the observed maximum") {
    // 桶的上边 EDGE 直方图不知道
    // 一个样本在水桶里坐在哪里 越过最大
    // 打印量的边缘 调用 数字不可能是
    // 真的 并且会让每个数字都失去信誉
    st::Hist h;
    h.add(10);
    h.add(5200);
    for (double q : {0.5, 0.9, 0.95, 0.99, 1.0})
        CHECK(h.quantile(q) <= static_cast<double>(h.max()));
}

// ── 标签表 ────────────────────────────────────────────────────────

TEST_CASE("stats: tab metadata is derived from one table") {
    // 键是 SSOT 如果标签上没有标题或字幕,
    // 漂移就会出现在这里,而不是作为空白条 标签.
    CHECK(st::kTabCount == static_cast<int>(st::kTabs.size()));
    for (const auto& d : st::kTabs) {
        CHECK(!d.title.empty());
        CHECK(!d.subtitle.empty());
        CHECK(d.available != nullptr);
        CHECK(!d.sections.empty());
        CHECK(st::tab_title(d.id) == d.title);
        CHECK(st::tab_subtitle(d.id) == d.subtitle);
    }
}

TEST_CASE("stats: a tab with nothing to say is not shown") {
    // 空状态是承认它不应该被绘制的标签。
    const auto empty = rebuild(thread_of({}));
    const auto vis = st::visible_tabs(empty);
    // 会话总是有的;Tools/Cache/Smart则没有.
    CHECK(std::find(vis.begin(), vis.end(), st::Tab::Session) != vis.end());
    CHECK(std::find(vis.begin(), vis.end(), st::Tab::Tools) == vis.end());
    CHECK(std::find(vis.begin(), vis.end(), st::Tab::Cache) == vis.end());
    CHECK(std::find(vis.begin(), vis.end(), st::Tab::Smart) == vis.end());
}

TEST_CASE("stats: Smart appears only once something was routed") {
    const auto off = rebuild(thread_of({unrouted("gpt-5")}));
    CHECK(!st::tab_available(st::Tab::Smart, off));

    const auto on = rebuild(thread_of({served("a", ModelRole::Utility)}));
    CHECK(st::tab_available(st::Tab::Smart, on));
}

TEST_CASE("stats: stepping never lands on a hidden tab") {
    // Tab/Shift-Tab 走路,走着 可见 设置,
    // 因此按键压不能选择条形图不绘制的视图。
    const auto f = rebuild(thread_of({served("a", ModelRole::Utility)}));
    const auto vis = st::visible_tabs(f);
    st::Tab cur = vis.front();
    for (int i = 0; i < 3 * st::kTabCount; ++i) {
        cur = st::tab_step(cur, +1, f);
        CHECK(st::tab_available(cur, f));
    }
    for (int i = 0; i < 3 * st::kTabCount; ++i) {
        cur = st::tab_step(cur, -1, f);
        CHECK(st::tab_available(cur, f));
    }
}

TEST_CASE("stats: every extractor is total on an empty Facts") {
    // 以零分母除以或索引空计的提取
    // 器会崩溃 一个 保证会打开:
    // 品牌 : -new 线程.
    const st::Facts empty;
    std::vector<st::Metric> out;
    for (const auto& d : st::kTabs)
        for (const auto& sec : d.sections) {
            out.clear();
            sec.extract(empty, out);
            for (const auto& mt : out) {
                // 任何一节选择的发射都必须是有限的和格式化的—
                // — NaN 到达屏幕时为 "nan".
                CHECK(mt.value == mt.value);
                CHECK(!st::format(mt.unit, mt.value).empty());
            }
        }
}

// ── format() ─────────────────────────────────────────────────────────────

TEST_CASE("stats: format spells each unit one way") {
    using st::Unit;
    CHECK(st::format(Unit::Count, 47) == "47");
    CHECK(st::format(Unit::Tokens, 1200) == "1.2k");
    CHECK(st::format(Unit::Tokens, 12000) == "12k");
    CHECK(st::format(Unit::Tokens, 1'300'000) == "1.3M");
    CHECK(st::format(Unit::Millis, 340) == "340ms");
    CHECK(st::format(Unit::Millis, 3200) == "3.2s");
    CHECK(st::format(Unit::Millis, 214'000) == "3m34s");
    CHECK(st::format(Unit::Ratio, 0.62) == "62%");
    CHECK(st::format(Unit::Rate, 1200) == "1.2k/s");
}

TEST_CASE("stats: a non-zero share never prints as zero") {
    // 与不允许圆形到空的栏规则相同: "almost none"
    // 财务报告和已审计财务报表 "none" 不同的读数,
    // 和一个打印第一个作为第二个的面板 躺着的方向是重要的。
    CHECK(st::format(st::Unit::Ratio, 0.004) == "<1%");
    CHECK(st::format(st::Unit::Ratio, 0.0) == "0%");
}
