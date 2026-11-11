// long_session_bench ——压力+工具的时间安排 -heavy 长会
//
// 构建合成 线程 被压合(转动×) write_bytes ×
// edit_hunks × bash_output 形状,然后是每一次
// 高频路径 ,该 resume/render 用于捕捉与所列七针相对
// 应的反射 docs/INLINE_SCROLLBACK.md.
//
// 每种情况都报告K周期的中位数/p50/p99/平均值。
// 每种情景的阶段时间:
//
//   建筑工程——建造 线程 价值 render_key/tail
//   合计 compute_render_key (在现场) 末尾 (单位:千美元)
//                       -frame 工作 工作 时间 visual_hash
//   冻结——清 + freeze_through 满地都是 线程 (建造每个要素的
//                       费用) 快照 (二) 水合物 -- --
//   rehydrate_frozen (注: -tail 恢复路径) view_build
//   — conversation_config + AppLayout::build
//                       (每笔) -frame 构件)
//   cold_render ——第一个 render_tree 进入新画布+ 池
//                       (用户) -visible 首个 帧 续会)
//   warm_render ——第二个 render_tree 在同一画布上
//                       + 池 (缓存-hit 平稳
//   状态) 修剪—— trim_frozen_if_oversized 当超大小
//
// 构建: 链接自 CMake 经由 AGENTTY_RUNTIME_NOMAIN_SOURCES
// 运行时 捆绑起来再用生产 状态更新函数 + 查看码,
// 无 main() 碰撞。 碰撞。
//
// 运行 : ./build/long_session_bench [scenario_glob
//      没有 arg – 运行所有情况。
//      arg – 只运行其名称包含子字符串的情景。
//
// 信封节点 :
//   BENCH_ASSERT =1 = 失败(非) -zero ưμ㼯A 高频路径 超过
//                     上限的 CI 平均 -regression 大门
//   BENCH_PHASES =1 = 打印 a/
//                     -scenario 凉 -render
//                     分解( 构建/ 版式 / 油漆纳秒 + 内组
//                     件) render() 数)到积分。这就是寒冷的
//                     滋味 -render 成本是按以下特点划分的:
//                     正文 彩绘布局为~70% /~30%,
//                     渲染 计数比额表 LINEARLY 内含内容(每个
//                     减值块一个内含部分,每个内含一次)——测量和
//                     油漆传出共享内部 -frame 结果 缓存 ,
//                     所以没有双 -render )在测试时保留此选项。
//                     渲染 路径,这样下一个人就不会重新 -derive
//                     寒冷的风 帧 已经是单身 -pass.
//   BENCH_ITERS =N = = 超过每架 -phase 迭代计数( 默认 5)。
//   BENCH_JSON =1 = 发射a JSON 以每例情景代替
//                     表格(用于自动化/ CI 回归跟踪)。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/app_layout.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/changes_strip.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"
#include "agentty/runtime/view/thread/conversation.hpp"
#include "agentty/runtime/view/thread/thread.hpp"

using namespace std::chrono;
using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;

// ─────────────────────────────────────────────────────────────────────────
// 时间的原始人
// ─────────────────────────────────────────────────────────────────────────

using Clock = steady_clock;

[[nodiscard]] double ms(Clock::duration d) noexcept {
    return duration_cast<duration<double, std::milli>>(d).count();
}

struct Stats {
    double median = 0;
    double p99    = 0;
    double mean   = 0;
    double min    = 0;
    double max    = 0;
    int    n      = 0;
};

[[nodiscard]] Stats summarise(std::vector<double>& samples) {
    Stats s;
    s.n = static_cast<int>(samples.size());
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.min  = samples.front();
    s.max  = samples.back();
    s.mean = 0;
    for (double v : samples) s.mean += v;
    s.mean /= static_cast<double>(samples.size());
    s.median = samples[samples.size() / 2];
    // p99为小 N 的线性套接字;底部为 N < 100 时.
    if (samples.size() == 1) {
        s.p99 = samples[0];
    } else {
        const double pos = 0.99 * static_cast<double>(samples.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(pos);
        const std::size_t hi = std::min(lo + 1, samples.size() - 1);
        const double frac = pos - static_cast<double>(lo);
        s.p99 = samples[lo] + (samples[hi] - samples[lo]) * frac;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────
// 合成内容生成器
// ─────────────────────────────────────────────────────────────────────────
//
// 目标: 字节 看上去像真正的工具输出
// render_tree 练习次数相同 -line
// 我们从小选一个 池 以下列方式输出:
//
//   • 转弯之间的compute_render_key变化(长度不同)。
//   • 转向内加亮标记/代码 渲染
//     击打现实的线形( 并非全部) -spaces ,并非全部 -identical).
//   • UTF -8并不主导(多倍的喷洒) -byte 仅字符).
//
// 定型:单一种子 RNG 整个运行如此两条
// 引言 二进制程序 生产相同 线程 投入。

namespace gen {

std::mt19937_64& rng() {
    static std::mt19937_64 r{0xC0FFEEULL};
    return r;
}

// 极易被破坏的源线——种类广泛,足以断断散式短-circuits.
constexpr std::string_view kCodeLines[] = {
    "    auto it = std::find_if(begin(v), end(v), [&](const auto& x) { return x.id == needle; });",
    "    if (status == Status::Running && !is_terminal()) { schedule_retry(ctx); }",
    "    return std::format(\"[{}] {} -> {}ms\", tag, name, elapsed.count());",
    "#include <chrono>",
    "",
    "    // Bail out if we already drained this turn — see comment in update/stream.cpp.",
    "    const auto t0 = std::chrono::steady_clock::now();",
    "    for (std::size_t i = 0; i < messages.size(); ++i) {",
    "        if (messages[i].role == Role::Assistant && !messages[i].is_terminal()) {",
    "            return std::nullopt;",
    "        }",
    "    }",
    "    static_assert(sizeof(Header) == 16, \"layout drift\");",
    "    Result<Element> el = builder.with_color(Color::rgb(0x4a, 0x9e, 0xff)).build();",
    "    std::shared_ptr<const AgentTimelineEvent> ev = std::make_shared<...>(...);",
    "}",
    "namespace agentty::detail {",
    "",
    "void freeze_range(Model& m, std::size_t from, std::size_t to) {",
    "    if (from >= to) return;",
};
constexpr std::size_t kCodeLinesN = sizeof(kCodeLines) / sizeof(kCodeLines[0]);

[[nodiscard]] std::string code_block(int n_lines) {
    std::string out;
    out.reserve(static_cast<std::size_t>(n_lines) * 64);
    auto& r = rng();
    for (int i = 0; i < n_lines; ++i) {
        out += kCodeLines[r() % kCodeLinesN];
        out += '\n';
    }
    return out;
}

[[nodiscard]] std::string bash_output(int n_lines) {
    static constexpr std::string_view sh[] = {
        "[  0.012s] linking target maya::maya",
        "[  0.084s] linking target agentty",
        "warning: unused parameter 'ctx' [-Wunused-parameter]",
        "/usr/bin/ld: /tmp/foo.o: in function `bar': undefined reference to `baz'",
        "PASS test_render_scaling.cpp:118  cold_paint_under_budget",
        "PASS test_render_scaling.cpp:140  warm_paint_under_budget",
        "FAIL test_render_scaling.cpp:683  per_event_hash_id_bounds_cost",
        "21 tests passed, 1 failed",
    };
    std::string out;
    auto& r = rng();
    for (int i = 0; i < n_lines; ++i) {
        out += sh[r() % (sizeof(sh) / sizeof(sh[0]))];
        out += '\n';
    }
    return out;
}

[[nodiscard]] std::string assistant_prose(int n_paragraphs) {
    static constexpr std::string_view paras[] = {
        "I'll start by exploring the auth flow so I can see what's actually wired together. "
        "The login handler in `src/auth/login.cpp` looks like the right entry point.",
        "The provider factory currently constructs a `LegacyAuth` on every call. "
        "I'll swap that for the new `NewAuth::create` builder, which returns a `Result<Session>` "
        "so the caller can surface init errors instead of crashing.",
        "Three callers depend on the old signature. I'll touch each one in turn: "
        "`src/api/login.cpp`, `src/cli/auth_cmd.cpp`, and `tests/test_auth_flow.cpp`.",
    };
    std::string out;
    auto& r = rng();
    for (int i = 0; i < n_paragraphs; ++i) {
        out += paras[r() % (sizeof(paras) / sizeof(paras[0]))];
        out += "\n\n";
    }
    return out;
}

} // namespace gen

// ─────────────────────────────────────────────────────────────────────────
// Tool 建构器
// ─────────────────────────────────────────────────────────────────────────
//
// 每个建造者都生产TERMINAL ToolUse(状态=完成)
// ——冻结门拒绝冻结n-terminal运行,
// 因此n-terminal工具会默默地跳过每一个冻结路径,扭曲时间.

namespace tool {

int s_id_counter = 0;

[[nodiscard]] ToolUse done(std::string name, nlohmann::json args, std::string output) {
    ToolUse t;
    t.id   = ToolCallId{"call_" + std::to_string(++s_id_counter)};
    t.name = ToolName{std::move(name)};
    t.args = std::move(args);
    auto now = Clock::now();
    t.status = ToolUse::Done{
        .started_at  = now - milliseconds{42},
        .finished_at = now,
        .output      = std::move(output),
    };
    return t;
}

[[nodiscard]] ToolUse write_tool(std::string_view path, int n_lines) {
    nlohmann::json args = {
        {"file_path", std::string{path}},
        {"content",   gen::code_block(n_lines)},
    };
    // Write 工具的输出是 Canonial "wrote N lines · M bytes" 线条。
    const std::string body = args["content"].get<std::string>();
    std::string out = "wrote " + std::to_string(n_lines) + " lines · "
                    + std::to_string(body.size()) + " bytes";
    return done("write", std::move(args), std::move(out));
}

[[nodiscard]] ToolUse edit_tool(std::string_view path, int n_hunks) {
    nlohmann::json edits = nlohmann::json::array();
    auto& r = gen::rng();
    for (int i = 0; i < n_hunks; ++i) {
        edits.push_back({
            {"old_text", gen::code_block(3 + static_cast<int>(r() % 6))},
            {"new_text", gen::code_block(3 + static_cast<int>(r() % 6))},
        });
    }
    nlohmann::json args = {
        {"file_path", std::string{path}},
        {"edits",     std::move(edits)},
    };
    std::string out = "applied " + std::to_string(n_hunks) + " edits to "
                    + std::string{path};
    return done("edit", std::move(args), std::move(out));
}

[[nodiscard]] ToolUse bash_tool(std::string_view cmd, int n_lines) {
    nlohmann::json args = {{"command", std::string{cmd}}};
    return done("shell", std::move(args), gen::bash_output(n_lines));
}

[[nodiscard]] ToolUse read_tool(std::string_view path, int n_lines) {
    nlohmann::json args = {{"path", std::string{path}}};
    return done("read", std::move(args), gen::code_block(n_lines));
}

} // namespace tool

// ─────────────────────────────────────────────────────────────────────────
// 情景形状——宣示性美化
// ─────────────────────────────────────────────────────────────────────────

// 匿名命名空间: 捆绑到 agentty_standalone_tests ,
// 在其中 md_shape_sweep 宣布自己的决定 `Shape` ——内部联系保持两者.
// TU-local 碰撞产生的类型( ODR).
namespace {
struct Shape {
    std::string name;
    int  n_turns           = 0;     // 助手转动; 用户转动与每个对齐
    int  write_lines       = 0;     // 单行数 Write 工具 正文
    int  penult_write_lines = 0;    // 覆盖 Write 用于第2行的线条 -to-last
                                    // 转( 复制一个关 -screen GIANT
                                    // 正文 由微小结果后的再水合物保存)
    int  edit_hunks        = 0;     // 每个编辑工具, 0 跳过
    int  bash_lines        = 0;     // 每个巴什工具, 0 跳过
    int  read_lines        = 0;     // 每个读工具, 0 跳过
    int  assistant_prose_p = 1;     // 助理文本的段落,每回合
    int  user_text_chars   = 80;    // 每个回合的用户消息长度
    int  iters             = 5;     // 数据表的外式-loop
};
}  // 命名空间( N) 归并 : 形状为 TU-local)

[[nodiscard]] std::string user_prompt(int chars) {
    std::string s;
    s.reserve(static_cast<std::size_t>(chars));
    static constexpr std::string_view tpl =
        "Please refactor the auth flow to use the new provider, "
        "and run the test suite afterwards. Note any flakes. ";
    while (static_cast<int>(s.size()) < chars) s += tpl;
    s.resize(static_cast<std::size_t>(chars));
    return s;
}

[[nodiscard]] Thread build_thread(const Shape& sh) {
    Thread t;
    t.id    = agentty::ThreadId{"bench_thread"};
    t.title = "Long-session bench: " + sh.name;
    t.messages.reserve(static_cast<std::size_t>(sh.n_turns) * 2);

    for (int turn = 0; turn < sh.n_turns; ++turn) {
        // 用户转身。
        Message u;
        u.role = Role::User;
        u.text = user_prompt(sh.user_text_chars);
        t.messages.push_back(std::move(u));

        // 助理转会:传言+工具.
        Message a;
        a.role = Role::Assistant;
        a.text = gen::assistant_prose(sh.assistant_prose_p);
        int wl = sh.write_lines;
        if (sh.penult_write_lines > 0 && turn == sh.n_turns - 2)
            wl = sh.penult_write_lines;   // 关 -screen 巨型在小块后面 末尾
        if (wl > 0)
            a.tool_calls.push_back(tool::write_tool(
                "src/auth/login.cpp", wl));
        if (sh.edit_hunks > 0)
            a.tool_calls.push_back(tool::edit_tool(
                "src/api/login.cpp", sh.edit_hunks));
        if (sh.bash_lines > 0)
            a.tool_calls.push_back(tool::bash_tool(
                "cmake --build build -j10", sh.bash_lines));
        if (sh.read_lines > 0)
            a.tool_calls.push_back(tool::read_tool(
                "tests/test_auth.cpp", sh.read_lines));
        t.messages.push_back(std::move(a));
    }
    return t;
}

[[nodiscard]] Model build_model(const Shape& sh) {
    Model m;
    m.d.current = build_thread(sh);
    return m;
}

// ─────────────────────────────────────────────────────────────────────────
// 时间阶段——每个阶段在`sh.iters`运行中返回Stats struct.
// ─────────────────────────────────────────────────────────────────────────

namespace phase {

[[nodiscard]] Stats construct(const Shape& sh) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto t0 = Clock::now();
        auto m  = build_model(sh);
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
        (void)m;
    }
    return summarise(samples);
}

[[nodiscard]] Stats render_key_tail(const Shape& sh) {
    auto m = build_model(sh);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (auto& msg : m.d.current.messages) acc ^= msg.compute_render_key();
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
        volatile std::uint64_t anti_dce = acc; (void)anti_dce;   // 反-DCE(便携式)
    }
    return summarise(samples);
}

[[nodiscard]] Stats freeze(const Shape& sh) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto m = build_model(sh);
        auto t0 = Clock::now();
        agentty::app::detail::clear_frozen(m);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
    }
    return summarise(samples);
}

[[nodiscard]] Stats rehydrate(const Shape& sh) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto m = build_model(sh);   // Model 正在移动 -only ——每期重建
        auto t0 = Clock::now();
        agentty::app::detail::rehydrate_frozen(m);
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
    }
    return summarise(samples);
}

[[nodiscard]] Stats view_build(const Shape& sh) {
    auto m = build_model(sh);
    agentty::app::detail::rehydrate_frozen(m);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto t0 = Clock::now();
        auto root = maya::AppLayout{{
            .thread        = agentty::ui::thread_config(m),
            .changes_strip = agentty::ui::changes_strip_config(m),
            .composer      = agentty::ui::composer_config(m),
            .status_bar    = agentty::ui::status_bar_config(m),
            .overlay       = std::nullopt,
        }}.build();
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
        volatile const void* anti_dce = &root; (void)anti_dce;   // 反-DCE(便携式)
    }
    return summarise(samples);
}

// 渲染 阶段:寒冷对温暖 相同 画布 + 池 ,因此我们测量 缓存
// -hit 赢 warmup_render 设计为交付。
struct RenderStats { Stats cold; Stats warm; };

[[nodiscard]] RenderStats render(const Shape& sh) {
    constexpr int kCanvasW = 120;
    constexpr int kCanvasH = 800;

    std::vector<double> cold_samples;
    std::vector<double> warm_samples;
    cold_samples.reserve(static_cast<std::size_t>(sh.iters));
    warm_samples.reserve(static_cast<std::size_t>(sh.iters));

    for (int i = 0; i < sh.iters; ++i) {
        auto m = build_model(sh);
        agentty::app::detail::rehydrate_frozen(m);

        auto root = maya::AppLayout{{
            .thread        = agentty::ui::thread_config(m),
            .changes_strip = agentty::ui::changes_strip_config(m),
            .composer      = agentty::ui::composer_config(m),
            .status_bar    = agentty::ui::status_bar_config(m),
            .overlay       = std::nullopt,
        }}.build();

        // 新鲜 池 +帆布 + 缓存 空 ~ 冷 路径。
        maya::StylePool pool;
        maya::Canvas canvas(kCanvasW, kCanvasH, &pool);
        canvas.clear();

        const std::uint64_t b0 = maya::render_detail::rt_build_ns();
        const std::uint64_t l0 = maya::render_detail::rt_layout_ns();
        const std::uint64_t p0 = maya::render_detail::rt_paint_ns();
        const std::uint64_t rc0 = maya::render_detail::component_render_calls();
        auto t0 = Clock::now();
        maya::render_tree(root, canvas, pool, maya::theme::native,
                          /*auto_height=*/true);
        auto t1 = Clock::now();
        cold_samples.push_back(ms(t1 - t0));
        if (std::getenv("BENCH_PHASES")) {
            std::fprintf(stderr,
                "    [phases] %-30s total=%.2f build=%.2f layout=%.2f paint=%.2f renders=%llu (ms)\n",
                sh.name.c_str(), ms(t1 - t0),
                double(maya::render_detail::rt_build_ns()  - b0) / 1e6,
                double(maya::render_detail::rt_layout_ns() - l0) / 1e6,
                double(maya::render_detail::rt_paint_ns()  - p0) / 1e6,
                (unsigned long long)(maya::render_detail::component_render_calls() - rc0));
        }

        // 温暖: 相同的画布 + 池 ,同样根 □ 缓存 应该是血淋淋的
        canvas.clear();
        auto t2 = Clock::now();
        maya::render_tree(root, canvas, pool, maya::theme::native,
                          /*auto_height=*/true);
        auto t3 = Clock::now();
        warm_samples.push_back(ms(t3 - t2));
    }
    return { summarise(cold_samples), summarise(warm_samples) };
}

[[nodiscard]] Stats trim(const Shape& sh) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    for (int i = 0; i < sh.iters; ++i) {
        auto m = build_model(sh);   // Model 正在移动 -only ——每期重建
        agentty::app::detail::rehydrate_frozen(m);
        // 强制 -pad m.ui.frozen 过了软盖,
        // 修剪的就真的起火了。 element/meta 内部锁锁
        // 的存储(旧的三) -parallel-vector
        // UB 结构上已经不存在了 我们只要封印一个现有区块的
        // 复制品 并盖上他们的油漆 -recorded 高度高,
        // 所以修剪的可推广性门 让他们下降。
        if (!m.ui.frozen.empty()) {
            const auto exemplar =
                m.ui.frozen.elements()[m.ui.frozen.size() - 1];
            const std::size_t exemplar_rows =
                std::max<std::size_t>(1,
                    m.ui.frozen.block_rows(m.ui.frozen.size() - 1));
            while (m.ui.frozen.size() < 200)
                m.ui.frozen.seal(exemplar, exemplar_rows);
            for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
                m.ui.frozen.record_paint(
                    k, static_cast<int>(m.ui.frozen.block_rows(k)));
        }
        auto t0 = Clock::now();
        (void)agentty::app::detail::trim_frozen_if_oversized(m);
        auto t1 = Clock::now();
        samples.push_back(ms(t1 - t0));
    }
    return summarise(samples);
}

// 中间 -run 百分比 -frame 稳定状态——用户实际感受到
// 的代价 DURING 长的自动 -pilot 转向。生产将画布与
// trim_frozen_if_oversized 转弯边界(
// 冻结时保留到 frozen_row_budget 并且只有活的 和
// 我们一起生活 末尾 每一个重建 帧 ; 冷冻的前缀 blits。
// 此阶段复制: 水合物, 运行修饰直到它不是 -op (按其固定大小冻结),
// 建立元素 源码树 一次 时 a WARM 渲染 (同样的) canvas/pool ),
// 这是真实的每 -tick 如果这在全局保持平稳 D/E/G/H
// 形状(200吨,3000) -line 写) 单 -frame
// 路径是真实的,如果它以 线程 修剪的尺寸不是这个形状的
struct MidrunStats { Stats frame; std::size_t frozen_rows_after = 0;
                     std::size_t frozen_entries_after = 0; };

[[nodiscard]] MidrunStats midrun_frame(const Shape& sh) {
    auto m = build_model(sh);
    agentty::app::detail::rehydrate_frozen(m);
    // 将修剪器开到固定点 冷却器就处于
    // 固定的产量水平
    for (int guard = 0; guard < 64; ++guard) {
        auto c = agentty::app::detail::trim_frozen_if_oversized(m);
        if (c.is_none()) break;
    }

    auto root = maya::AppLayout{{
        .thread        = agentty::ui::thread_config(m),
        .changes_strip = agentty::ui::changes_strip_config(m),
        .composer      = agentty::ui::composer_config(m),
        .status_bar    = agentty::ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    constexpr int kCanvasW = 120;
    constexpr int kCanvasH = 4000;
    maya::StylePool pool;
    maya::Canvas canvas(kCanvasW, kCanvasH, &pool);
    canvas.clear();
    // 一次(冷),所以下面的温暖时间是纯的 缓存-hit.
    maya::render_tree(root, canvas, pool, maya::theme::native,
                      /*auto_height=*/true);

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sh.iters));
    double clear_sum = 0, render_sum = 0;
    // 镜像生产内线路径: 第一次( 初选) 之后 帧 ,
    // 保留 不可变 上方的冻字前缀 可视区域 且只清净其窗口,
    // render_tree 缓存的 blit 可以跳过未修改的前缀。
    // 假设为 80 -row 终端 可视区域 (无) 回退 几何),
    // 同为 8 -row 边距 app.cpp 使用时。
    constexpr int kTermH = 80;
    constexpr int kPreserveMargin = 8;
    const bool no_preserve = std::getenv("BENCH_NOPRESERVE") != nullptr;
    for (int i = 0; i < sh.iters; ++i) {
        auto tc0 = Clock::now();
        const int prev_ch = maya::content_height(canvas);
        int keep_top = 0;
        if (!no_preserve && prev_ch > kTermH) {
            keep_top = prev_ch - kTermH - kPreserveMargin;
            if (keep_top < 0) keep_top = 0;
        }
        if (keep_top > 0) canvas.clear_below(keep_top);
        else              canvas.clear();
        auto tc1 = Clock::now();
        maya::render_tree(root, canvas, pool, maya::theme::native,
                          /*auto_height=*/true);
        auto t1 = Clock::now();
        clear_sum  += ms(tc1 - tc0);
        render_sum += ms(t1 - tc1);
        samples.push_back(ms(t1 - tc0));
    }
    if (std::getenv("BENCH_SPLIT") && sh.iters > 0) {
        std::fprintf(stderr, "    [split] %-30s clear=%.3f render=%.3f ms/frame\n",
                     sh.name.c_str(), clear_sum / sh.iters, render_sum / sh.iters);
    }
    if (std::getenv("BENCH_DUMP")) {
        std::fprintf(stderr, "    [dump] %s frames:", sh.name.c_str());
        for (double s : samples) std::fprintf(stderr, " %.2f", s);
        std::fprintf(stderr, "\n");
    }
    MidrunStats out;
    out.frame                = summarise(samples);
    out.frozen_rows_after    = m.ui.frozen.row_total();
    out.frozen_entries_after = m.ui.frozen.size();
    return out;
}

// TRUE 流式传输 百分比 -frame 成本——用户感觉的
// "30% CPU on long turns"。与 midrun_frame
// (它构建元素) 源码树 一次 和关于 -renders 静态根),
// 此词忠实地复制生产: 最后 助理运行为 IN-FLIGHT (一个运行中的写作工具,
// 其 args_streaming 正文 不断增长,所以是故意的 不
// hash_id 'd——整个树下树被重建(D) view_build)
// 和 重新油漆每张 RAF 帧 上方的冰冻前缀。 可视区域 保存( 3)
// clear_below ),但现场直播 末尾。如果此缩放与 插入 -flight 正文 排数,
// 现场 末尾 犯人是犯人 如果是平的,那就 末尾 已设定了界限。
struct StreamingStats { Stats frame; Stats build; Stats render;
                        std::size_t live_rows = 0; };

[[nodiscard]] StreamingStats streaming_frame(const Shape& sh, int live_lines) {
    // 调和 PRIOR 转弯;最后的助手转弯是左侧直播.
    auto m = build_model(sh);
    // 使最后的助理信息成为-flight 流式传输写:一个运行工具,
    // 其中有一个大args_streaming 正文 + 增长的传言.
    auto& msgs = m.d.current.messages;
    if (!msgs.empty()) {
        // 添加一个新直播助手转弯( 用户+ 助手) ,
        // 所以前面 线程 冻死,只有这个留在现场 末尾.
        Message u; u.role = Role::User; u.text = user_prompt(sh.user_text_chars);
        msgs.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        // 由live_lines标志控制的两个活体形状:
        //   live_lines > 0: 以内 -flight WRITE 工具为 live_lines 备注 正文.
        //   live_lines < 0: 以内 -flight PROSE 数据流 备注 |live_lines
        //                     | 线条( 无工具) —— 测试下标 -body 路径。
        if (live_lines < 0) {
            // 建个大楼 流式传输 标记下 正文: |live_lines| 混合
            // 线条 prose/code 故外元素( 外元素) 源码树 有很多块
            const int n = -live_lines;
            std::string body;
            body.reserve(static_cast<std::size_t>(n) * 48);
            for (int i = 0; i < n; ++i) {
                body += (i % 5 == 0)
                    ? "Here's the next step in the refactor, paragraph line."
                    : "    auto x = compute(step, ctx); // inline detail";
                body += '\n';
            }
            a.streaming_text = std::move(body);
            a.role = Role::Assistant;
        } else {
            a.text = gen::assistant_prose(sh.assistant_prose_p);
            ToolUse live;
            live.id     = ToolCallId{"call_live"};
            live.name   = ToolName{"write"};
            live.status = ToolUse::Running{};
            // args_streaming 带部分 JSON 写卡 渲染 编号
            // 正文 从——种其为. `live_lines` 代码。
            live.args_streaming =
                "{\"file_path\":\"src/auth/login.cpp\",\"content\":\""
                + gen::code_block(live_lines);
            // ...和 ⁇ 带 PARSED 快照 ,
            // 与 状态更新函数 大约120米 try_parse_partial
            // 保持其生产( ) stream.cpp。没有这个,
            // 写卡 正文 分支读取空 "content" 财务报告和已
            // 审计财务报表 渲染 无内容 ——扫射然后测量每张大小的空卡,
            // 并用挥发的平面,这恰恰是一个O(O)的原理. 正文 返回
            // 到 正文 渲染器 将滑过这个探测器 未经测量。
            live.args = {{"file_path", "src/auth/login.cpp"},
                         {"content", gen::code_block(live_lines)}};
            a.tool_calls.push_back(std::move(live));
        }
        msgs.push_back(std::move(a));
    }
    agentty::app::detail::rehydrate_frozen(m);

    constexpr int kCanvasW = 120;
    constexpr int kCanvasH = 4000;
    maya::StylePool pool;
    maya::Canvas canvas(kCanvasW, kCanvasH, &pool);
    canvas.clear();
    constexpr int kTermH = 80;
    constexpr int kPreserveMargin = 8;

    std::vector<double> frame_s, build_s, render_s;
    const int iters = std::max(sh.iters, 8);
    frame_s.reserve(iters); build_s.reserve(iters); render_s.reserve(iters);

    // 一次大势所趋.
    {
        auto root = maya::AppLayout{{
            .thread        = agentty::ui::thread_config(m),
            .changes_strip = agentty::ui::changes_strip_config(m),
            .composer      = agentty::ui::composer_config(m),
            .status_bar    = agentty::ui::status_bar_config(m),
            .overlay       = std::nullopt,
        }}.build();
        maya::render_tree(root, canvas, pool, maya::theme::native, true);
    }

    for (int i = 0; i < iters; ++i) {
        auto tb0 = Clock::now();
        // 生产重建整个元素 源码树 每隔一段时间 帧 ;
        // 输入 -flight 运行为 不 缓存了 所以这是真正的视图成本
        auto root = maya::AppLayout{{
            .thread        = agentty::ui::thread_config(m),
            .changes_strip = agentty::ui::changes_strip_config(m),
            .composer      = agentty::ui::composer_config(m),
            .status_bar    = agentty::ui::status_bar_config(m),
            .overlay       = std::nullopt,
        }}.build();
        auto tb1 = Clock::now();

        const int prev_ch = maya::content_height(canvas);
        int keep_top = 0;
        if (prev_ch > kTermH) {
            keep_top = prev_ch - kTermH - kPreserveMargin;
            if (keep_top < 0) keep_top = 0;
        }
        if (keep_top > 0) canvas.clear_below(keep_top);
        else              canvas.clear();

        auto tr0 = Clock::now();
        maya::render_tree(root, canvas, pool, maya::theme::native, true);
        auto tr1 = Clock::now();

        build_s.push_back(ms(tb1 - tb0));
        render_s.push_back(ms(tr1 - tr0));
        frame_s.push_back(ms(tb1 - tb0) + ms(tr1 - tr0));
    }

    StreamingStats out;
    out.frame     = summarise(frame_s);
    out.build     = summarise(build_s);
    out.render    = summarise(render_s);
    out.live_rows = static_cast<std::size_t>(maya::content_height(canvas));
    return out;
}

} // namespace phase

// ─────────────────────────────────────────────────────────────────────────
// 报告
// ─────────────────────────────────────────────────────────────────────────

struct ScenarioResult {
    Shape shape;
    Stats construct;
    Stats render_key;
    Stats freeze;
    Stats rehydrate;
    Stats view_build;
    Stats cold_render;
    Stats warm_render;
    Stats trim;
    Stats midrun_frame;
    std::size_t midrun_rows = 0;
    std::size_t midrun_entries = 0;
    std::size_t total_msgs = 0;
    std::size_t total_bytes = 0;
    std::size_t frozen_entries = 0;
};

void print_header() {
    std::printf("\n%-30s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s\n",
        "scenario",
        "construct",
        "render_key/tail",
        "freeze (full)",
        "rehydrate",
        "view_build",
        "cold render",
        "warm render",
        "trim");
    std::printf("%-30s + %-19s + %-19s + %-19s + %-19s + %-19s + %-19s + %-19s + %-19s\n",
        "------------------------------",
        "-------------------",
        "-------------------",
        "-------------------",
        "-------------------",
        "-------------------",
        "-------------------",
        "-------------------",
        "-------------------");
}

static void fmt_cell(char* dst, std::size_t cap, const Stats& s) {
    std::snprintf(dst, cap, "%7.2f / %7.2fp99", s.median, s.p99);
}

void print_row(const ScenarioResult& r) {
    char c0[32], c1[32], c2[32], c3[32], c4[32], c5[32], c6[32], c7[32];
    fmt_cell(c0, sizeof(c0), r.construct);
    fmt_cell(c1, sizeof(c1), r.render_key);
    fmt_cell(c2, sizeof(c2), r.freeze);
    fmt_cell(c3, sizeof(c3), r.rehydrate);
    fmt_cell(c4, sizeof(c4), r.view_build);
    fmt_cell(c5, sizeof(c5), r.cold_render);
    fmt_cell(c6, sizeof(c6), r.warm_render);
    fmt_cell(c7, sizeof(c7), r.trim);
    std::printf("%-30s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s | %-19s\n",
        r.shape.name.c_str(),
        c0, c1, c2, c3, c4, c5, c6, c7);
}

void print_footnote(const ScenarioResult& r) {
    const double warm_speedup = r.warm_render.median > 0.0
        ? r.cold_render.median / r.warm_render.median
        : 0.0;
    std::printf("    %-26s   msgs=%zu  bytes=%zu  frozen=%zu  warm/cold=%.1fx\n",
        "",
        r.total_msgs,
        r.total_bytes,
        r.frozen_entries,
        warm_speedup);
    std::printf("    %-26s   MIDRUN per-frame=%.2f / %.2fp99 ms  "
                "(trimmed frozen: %zu rows, %zu entries)\n",
        "",
        r.midrun_frame.median, r.midrun_frame.p99,
        r.midrun_rows, r.midrun_entries);
}

void emit_json(const ScenarioResult& r) {
    nlohmann::json j;
    j["scenario"]       = r.shape.name;
    j["n_turns"]        = r.shape.n_turns;
    j["write_lines"]    = r.shape.write_lines;
    j["edit_hunks"]     = r.shape.edit_hunks;
    j["bash_lines"]     = r.shape.bash_lines;
    j["read_lines"]     = r.shape.read_lines;
    j["iters"]          = r.shape.iters;
    j["total_msgs"]     = r.total_msgs;
    j["total_bytes"]    = r.total_bytes;
    j["frozen_entries"] = r.frozen_entries;
    auto pack = [](const Stats& s) {
        return nlohmann::json{
            {"median_ms", s.median},
            {"p99_ms",    s.p99},
            {"mean_ms",   s.mean},
            {"min_ms",    s.min},
            {"max_ms",    s.max},
            {"n",         s.n},
        };
    };
    j["construct"]       = pack(r.construct);
    j["render_key_tail"] = pack(r.render_key);
    j["freeze"]          = pack(r.freeze);
    j["rehydrate"]       = pack(r.rehydrate);
    j["view_build"]      = pack(r.view_build);
    j["cold_render"]     = pack(r.cold_render);
    j["warm_render"]     = pack(r.warm_render);
    j["trim"]            = pack(r.trim);
    j["midrun_frame"]    = pack(r.midrun_frame);
    j["midrun_rows"]     = r.midrun_rows;
    j["midrun_entries"]  = r.midrun_entries;
    std::printf("%s\n", j.dump().c_str());
    std::fflush(stdout);
}

// ─────────────────────────────────────────────────────────────────────────
// 假设
// ─────────────────────────────────────────────────────────────────────────

std::vector<Shape> all_scenarios(int iters_override) {
    auto apply_iters = [&](Shape s) {
        if (iters_override > 0) s.iters = iters_override;
        return s;
    };
    return {
        // -perf 恢复时的基线形状会传递信息。
        apply_iters({.name = "A: 6t × 300-line write",
                     .n_turns = 6,  .write_lines = 300}),
        apply_iters({.name = "B: 6t × 800-line write",
                     .n_turns = 6,  .write_lines = 800}),
        apply_iters({.name = "C: 20t × 500-line write",
                     .n_turns = 20, .write_lines = 500}),
        apply_iters({.name = "D: 80t × 500-line write",
                     .n_turns = 80, .write_lines = 500}),
        // 压力形状—— 远超过kfrozenMax(80) 所以修饰是有意义的,
        // 并且每 -frame 排版控制着寒冷 渲染.
        apply_iters({.name = "E: 200t × 500-line write",
                     .n_turns = 200, .write_lines = 500, .iters = 3}),
        // 编辑 -heavy 数量 : 差异块 百分比 调用 ,没有 Write -
        // 强调编辑 -diff 正文 预览路径 agent_timeline.
        apply_iters({.name = "F: 20t × 10-hunk edit",
                     .n_turns = 20, .edit_hunks = 10}),
        // 混合现实: Write + 编辑 + Bash +
        // 读取每回合长会话。 这是最接近真实重会话的会话。
        apply_iters({.name = "G: 80t × Write+Edit+Bash+Read",
                     .n_turns = 80,
                     .write_lines = 200,
                     .edit_hunks  = 3,
                     .bash_lines  = 30,
                     .read_lines  = 80,
                     .assistant_prose_p = 2,
                     .iters = 3}),
        // 病理学——大单体 Write 模拟 "dump entire file"
        // 转动那艘船 ~3000线。
        apply_iters({.name = "H: 6t × 3000-line write",
                     .n_turns = 6, .write_lines = 3000, .iters = 3}),
        // OFF-SCREEN 巨型: 3000 个 -line 写作第二
        // -to-last 转身,转身小点,水合物使巨人进入 -window
        // (在小的流结果后面),所以画布是~3000行. -time
        // 崩溃( 现在) OFF 默认情况下——它隐藏了磁盘 -loaded
        // 正文 将关闭。 -screen 巨型为 1 -row 树枝 A/B
        // 设置 AGENTTY_FROZEN_COLLAPSE=1.
        apply_iters({.name = "I: off-screen 3000-line write + small tail",
                     .n_turns = 6, .write_lines = 8,
                     .penult_write_lines = 3000, .iters = 3}),
    };
}

// ─────────────────────────────────────────────────────────────────────────
// 驱动程序
// ─────────────────────────────────────────────────────────────────────────

[[nodiscard]] std::size_t total_bytes(const Thread& t) {
    std::size_t b = 0;
    for (const auto& m : t.messages) {
        b += m.text.size();
        for (const auto& tc : m.tool_calls) {
            b += tc.args_streaming.size();
            b += tc.output().size();
        }
    }
    return b;
}

ScenarioResult run_one(const Shape& sh) {
    ScenarioResult r;
    r.shape = sh;

    // 阶段顺序——每个阶段都是独立的(每个阶段都建立自己
    // 的) Model 除非明确表明 否则我们以后会进行更重
    // 的阶段 以便早期的数字不会因 分配器 热身;
    // 每一阶段的 喷头循环 也排除了一个 -shot 工艺品。
    r.construct   = phase::construct(sh);
    r.render_key  = phase::render_key_tail(sh);
    r.freeze      = phase::freeze(sh);
    r.rehydrate   = phase::rehydrate(sh);
    r.view_build  = phase::view_build(sh);
    auto rs       = phase::render(sh);
    r.cold_render = rs.cold;
    r.warm_render = rs.warm;
    r.trim        = phase::trim(sh);
    auto mid      = phase::midrun_frame(sh);
    r.midrun_frame   = mid.frame;
    r.midrun_rows    = mid.frozen_rows_after;
    r.midrun_entries = mid.frozen_entries_after;

    // 用于脚注的模型形状的Stats 快照.
    auto m = build_model(sh);
    agentty::app::detail::rehydrate_frozen(m);
    r.total_msgs     = m.d.current.messages.size();
    r.total_bytes    = total_bytes(m.d.current);
    r.frozen_entries = m.ui.frozen.size();
    return r;
}

int main(int argc, char** argv) {
    const char* filter   = (argc > 1) ? argv[1] : "";
    const char* iters_e  = std::getenv("BENCH_ITERS");
    const int iters_override = iters_e ? std::atoi(iters_e) : 0;
    const bool emit_json_lines = std::getenv("BENCH_JSON") != nullptr;

    const auto shapes = all_scenarios(iters_override);

    // BENCH_STREAM =1 = 运行 仅 真实的 流式传输-frame 探测
    // 器: 扫射 -flight 写入 正文 大小和打印 build/render/frame
    // 那样我们就能看清现场是否 末尾 费用比额表,与 流式传输 正文 长度。
    if (std::getenv("BENCH_STREAM")) {
        std::printf("streaming_frame probe — in-flight (un-cached) live tail\n");
        std::printf("%-8s | %-10s | %-10s | %-10s | %-8s\n",
                    "live", "build ms", "render ms", "frame ms", "rows");
        std::printf("---------+------------+------------+------------+---------\n");
        // 稍有固定的背景(6个转x300) -line )
        // 所以冷冻前缀是现实的;活的 正文 是我们扫荡的
        Shape base{.name = "stream", .n_turns = 6, .write_lines = 300,
                   .assistant_prose_p = 2, .iters = 20};
        std::printf("-- WRITE tool body sweep --\n");
        for (int live_lines : {8, 100, 400, 800, 1500, 3000}) {
            auto s = phase::streaming_frame(base, live_lines);
            std::printf("%-8d | %8.3f   | %8.3f   | %8.3f   | %-8zu\n",
                        live_lines, s.build.median, s.render.median,
                        s.frame.median, s.live_rows);
        }
        std::printf("-- PROSE stream (markdown body) sweep --\n");
        for (int prose_lines : {8, 100, 400, 800, 1500, 3000}) {
            auto s = phase::streaming_frame(base, -prose_lines);
            std::printf("%-8d | %8.3f   | %8.3f   | %8.3f   | %-8zu\n",
                        prose_lines, s.build.median, s.render.median,
                        s.frame.median, s.live_rows);
        }
        return 0;
    }

    if (!emit_json_lines) {
        std::printf("long_session_bench — agentty resume/render hot paths\n");
        std::printf("  scenarios: %zu, filter: %s, iters override: %s\n",
                    shapes.size(),
                    *filter ? filter : "(none)",
                    iters_e ? iters_e : "(none)");
        std::printf("  cells: median / p99 milliseconds\n");
        print_header();
    }

    int failures = 0;
    // 佩尔夫 -regression 打开门,打开门 BENCH_ASSERT =1 (单位:千美元) CI 工作是无穷无尽的。
    // 上限是快速的dev框上的观测值~5-8×,宽大到不能在
    // black上击落 loaded/slow CI 跑步,紧紧的,
    // 足以抓住真正的订单 -of-magnitude 回归(意外 O
    // (n) 重新加入到每 -frame 路径, 丢失 缓存 我们
    // 锁定两个真正约束交互性的参数:
    //   • MIDRUN每-frame p99——REAL稳定-state 流式传输使用户付出了代价
    //     感觉;观察到的0.9m,必须远远低于16ms 帧预算.
    //   • render_key p99 —— 人均 -frame visual_hash 步行; 观测到的亚微分。
    const bool assert_perf = std::getenv("BENCH_ASSERT") != nullptr;
    constexpr double kMidrunFrameCeilMs = 5.0;   // 预算为1 600万美元,~6x 松懈(MEDIAN)
    // p99 在一个共享的跑者身上 本来就很紧张(
    // 一个停滞) 帧 将p99作为一条松散的灾难绊线,
    // 仍然在16米以下。 帧 预算。
    constexpr double kMidrunFrameP99CeilMs = 12.0;
    constexpr double kRenderKeyCeilMs   = 1.0;   // 观测到0.00;大松懈
    int perf_violations = 0;

    // ── 现场直播 -tail FLATNESS 门 ──────────────────────────────────
    // 人类最重要的财产 流式传输 路径: 单 -frame 费用 IN
    // -FLIGHT 转动时不能按大小大小大小 正文 被流着。
    // maya's agent_session 结构上(封闭的块被卷入已建元素;
    // 只有活的边缘重建); agentty 通过下列途径实现相同的目标:
    // 末尾 窗口( 窗口) tool_body_common
    // 千丝盘切片 + 末尾-anchored 制造器。
    // ~0.18ms/frame 8至3000平方英尺 正文 线条。
    //
    // 等同点是 加载-bearing 并且以前已经执行 NOWHERE
    // — BENCH_STREAM 任何能为未发现的改变提供素材的 正文
    // 切换到部件层( 丢入) tail_window 调用,
    // a 渲染器 不再这样 末尾-anchored ,意外 O( 正文)
    // 解析 百分比 帧 ) 转长 数据流 改为每 -frame 税种的税
    // 种没有约束,而且没有发现任何现存的门。 -flight 正文
    // 直接失败 RATIO - 一台机器 - 一台电脑 - A
    // magical -speed-independent 与绝对上限不同。
    if (assert_perf) {
        Shape base{.name = "flatness", .n_turns = 6, .write_lines = 300,
                   .assistant_prose_p = 2, .iters = 20};
        struct Probe { const char* what; int small_body; int big_body;
                       double ratio_ceil; };
        // 100对12000线:120x 正文-size 散开。
        // 未见窗外 正文 以 O(n) 显示 BUILD 成本,
        // 而宽幅分布与健康断裂的断裂——仅30×散(~2.8×)的
        // 断裂的构造比率就坐落在任何松散到无法抛裂的天花板下.
        //
        // 人数 -probe 上限,因为两条路径不均匀:
        //   • 写作工具—— tail_window 切开 正文 东道主 -side ,所以建设
        //     谓 FLAT (~1x健康;用9.1x的测量,并切除窗户).
        //   • 传教者—— 正文 永远不通过 tail_window; StreamingMarkdown
        //     拥有全源,并搭载一个小结构 O( Q) 正文 ) 任期(~
        //     6年) ns/line : 0.014 ms,100 台线
        //     − 0.084 ms,12k, 比率~4.2x.
        //     与16 ms相比,可忽略不计. 帧 预算,但 真实 ——
        //     所以道词上限被设定在今天的系数上方,以赶上它不断增长,
        //     而不是假装路径平坦.
        for (const Probe p : {Probe{"write tool", 100, 12000, 3.0},
                              Probe{"prose", -100, -12000, 6.0}}) {
            const auto s = phase::streaming_frame(base, p.small_body);
            const auto b = phase::streaming_frame(base, p.big_body);
            // 注意噪音层的分母比例
            // 门上 BUILD 比率,而不是 帧 比率。
            // O( (百分比) 正文 元素建筑中的工作生活(
            // view_build); 渲染 以 ~ 固定的绘行数为主,
            // 并稀释信号 – 测量: 断开写入 末尾-window
            // 增加8.4x 的构造,而 帧 完全移动只移动2.5x.
            // 地板小侧远高于计时器噪音,所以比例是有意义的.
            const double small_ms = std::max(s.build.median, 0.02);
            const double ratio    = b.build.median / small_ms;
            // 单是比率就很脆弱,因为 两者 侧面坐落于次毫秒楼:
            // 健康的散文建筑为~0.02 ms小/~0.09 ms大
            // (~4.2x),所以是一个. 已加载 CI 跑步器在
            // 0.02微分母上单独划过6x,没有真正的回归(在林勒上
            // 观察到) -gcc 门。 GENUINE O(正文 (
            // b) 爆炸—— 末尾-window 移除此门以抓取——
            // 推 12 k -line 进入多面 -millisecond
            // 范围(测量的写探针为8.4x) 和 毫秒( X)
            // -scale。所以要求大方在高比率计数前也超过有意义
            // 的绝对成本:噪音仍为亚 -ms ,真实回归则不然。
            constexpr double kBuildRatioAbsFloorMs = 1.0;
            if (ratio > p.ratio_ceil && b.build.median > kBuildRatioAbsFloorMs) {
                ++perf_violations;
                std::fprintf(stderr,
                    "PERF REGRESSION [streaming %s]: live-tail BUILD cost is "
                    "not flat — %.3f ms at %d lines vs %.3f ms at %d lines "
                    "(%.1fx > %.1fx ceiling). Element construction now "
                    "scales with the in-flight body; see tail_window / "
                    "kStreamTailLines in tool_body_common.\n",
                    p.what, b.build.median, std::abs(p.big_body),
                    s.build.median, std::abs(p.small_body),
                    ratio, p.ratio_ceil);
            }
            if (b.frame.median > kMidrunFrameCeilMs) {
                ++perf_violations;
                std::fprintf(stderr,
                    "PERF REGRESSION [streaming %s]: %.3f ms/frame at %d "
                    "lines > %.2f ms ceiling\n",
                    p.what, b.frame.median, std::abs(p.big_body),
                    kMidrunFrameCeilMs);
            }
        }
    }

    for (const auto& sh : shapes) {
        if (*filter && sh.name.find(filter) == std::string::npos) continue;

        ScenarioResult r{};
        try {
            r = run_one(sh);
        } catch (const std::exception& e) {
            ++failures;
            std::fprintf(stderr, "scenario %s threw: %s\n",
                         sh.name.c_str(), e.what());
            continue;
        }

        if (emit_json_lines) {
            emit_json(r);
        } else {
            print_row(r);
            print_footnote(r);
        }

        if (assert_perf) {
            // 门 MIDRUN 费用 MEDIAN 中位数是稳态
            // -state 百分比 -frame 用户实际感受
            // 的成本(观察~0.9毫秒,16毫秒的预算);
            // 帧 共享 CI 径向螺旋钉 p99( 中位数为 ~
            // 1 ms) , 并用没有真实回归的键击此门。
            // 真正的回归会改变整个分布, 所以中位数也仍然被检查。
            // p99 仍然在检查中, 但它比松得多。
            // 末尾 最高额,每名灾难 -frame 摊位旅行。
            if (r.midrun_frame.median > kMidrunFrameCeilMs) {
                ++perf_violations;
                std::fprintf(stderr,
                    "PERF REGRESSION [%s]: MIDRUN per-frame median %.2f ms > %.2f ms ceiling\n",
                    r.shape.name.c_str(), r.midrun_frame.median, kMidrunFrameCeilMs);
            }
            if (r.midrun_frame.p99 > kMidrunFrameP99CeilMs) {
                ++perf_violations;
                std::fprintf(stderr,
                    "PERF REGRESSION [%s]: MIDRUN per-frame p99 %.2f ms > %.2f ms ceiling\n",
                    r.shape.name.c_str(), r.midrun_frame.p99, kMidrunFrameP99CeilMs);
            }
            if (r.render_key.p99 > kRenderKeyCeilMs) {
                ++perf_violations;
                std::fprintf(stderr,
                    "PERF REGRESSION [%s]: render_key p99 %.4f ms > %.2f ms ceiling\n",
                    r.shape.name.c_str(), r.render_key.p99, kRenderKeyCeilMs);
            }
        }
    }

    if (assert_perf) {
        if (perf_violations == 0)
            std::printf("\nBENCH_ASSERT: all %zu scenarios within perf ceilings.\n",
                        shapes.size());
        else
            std::fprintf(stderr,
                "\nBENCH_ASSERT: %d perf ceiling violation(s) — FAIL.\n",
                perf_violations);
        failures += perf_violations;
    }

    if (!emit_json_lines) {
        std::printf("\n");
        std::printf("Notes:\n");
        std::printf("  • cold render = first render_tree on fresh canvas/pool (resume frame).\n");
        std::printf("  • warm render = second render_tree, same canvas/pool (cache-hit frame).\n");
        std::printf("  • freeze = clear_frozen + freeze_through over the FULL thread (worst case).\n");
        std::printf("  • rehydrate = bounded-tail rebuild (the actual ThreadLoaded path).\n");
        std::printf("  • view_build excludes render — it's the cost of constructing the Element tree only.\n");
        std::printf("  • render_key/tail = ⊕ compute_render_key() across every message;\n");
        std::printf("    this is the per-frame work visual_hash pays. Should be sub-microsecond.\n");
    }
    return failures == 0 ? 0 : 1;
}
