// 定義重複,第3層:AGENTTY的视图+冷冻分类账.
//
// 两者 maya-layer 重写两张 PASS : 检测到主题交换,
// 即 存储单元 更改、 样式标识不碰撞、 并释放 字节 带着新墨水
// 因此,在正确交付maya 渲染时,正确无误地交付-rebuilt 源码树.
//
// 这会把怀疑推回 agentty : 数据 FROZEN
// LEDGER 如果主题更改不重建这些元素, maya 被
// 递出一个 源码树 仍带有旧调色板的 maya 将忠诚 渲染
// 完全一样,每个都给过 maya-layer 测试还是会通过
//
// 这能推动真正的 状态更新函数 路径( 选项) move_highlight ->
// restyle_sealed_turns -> rehydrate_frozen 并声称,重要的是结束的财产:
//
//   在一个模拟箭头键后, FROZEN 分类账已经开出
//   字节 使用新选择的图案墨水。
//
// 该公司还声称,用户实际报告的这一条: 两个 连续箭头
// 键必须生成 两个 屏幕上不同的调色板——每两张压板
// 都一个不变.

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/domain/ui_theme.hpp"
#include "agentty/io/persistence.hpp"

#include <maya/maya.hpp>
#include <maya/print.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace agentty;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

constexpr int kWidth = 100;

// 或 FROZEN 分类账会放在 传输 ,包含颜色。
std::string frozen_bytes(const Model& m) {
    std::string out;
    for (const auto& blk : m.ui.frozen.elements())
        out += maya::render_to_string_ansi(blk, kWidth);
    return out;
}

// 或 整个 将打开 传输.
std::string view_bytes(const Model& m) {
    return maya::render_to_string_ansi(ui::view(m), kWidth);
}

} // 命名空间结束。

int main(int argc, char** argv) {
    // 这些比较画 字节 跨调色板; a NO_COLOR 电话铃声的内脏
    // CI ,代理贝壳,一个控制台,没有 VT )将使每个方案都相同。
    // MAYA_COLOR 谓 maya 已经记录的定型 -test 覆盖( 见
    // detect_color_level).
    ::unsetenv("NO_COLOR");
    ::setenv("MAYA_COLOR", "truecolor", 1);
    const int msgs = argc > 1 ? std::atoi(argv[1]) : 200;
    // 可选:REALXX线程文件. 合成消息不复制被
    // 冻结的分类账的真实块形状(工具卡,代码块,归并),
    // 整个bug生活在那些块重绘的状态.
    const char* thread_path = argc > 2 ? argv[2] : nullptr;

    app::install_deps(app::Deps{
        .stream = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread = [](const Thread&) {},
        .load_threads = [] { return std::vector<Thread>{}; },
        .load_thread = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"probe"}; },
        .title_from = [](std::string_view) { return std::string{"t"}; },
    });

    std::printf("=== repro 3: agentty view + frozen ledger ===\n\n");

    Model m;
    if (thread_path) {
        // A 线程 id,不是路径: 商店会说 .jsonl 日志格式( )
        // load_thread_file 是那个 LEGACY 整体 -document .
        // JSON 读取器,它默默地不返回日志文件。
        auto t = persistence::load_thread_by_id(::agentty::IoAccess::grant(), ThreadId{thread_path});
        if (t) {
            m.d.current = std::move(*t);
            std::printf("loaded thread %s: %zu messages\n\n",
                        thread_path, m.d.current.messages.size());
        } else {
            std::printf("could not load thread %s — falling back to synthetic\n\n",
                        thread_path);
        }
    }
    if (m.d.current.messages.empty()) {
        for (int i = 0; i < msgs; ++i) {
            Message msg;
            msg.role = (i % 2) ? Role::Assistant : Role::User;
            msg.text = "## Heading " + std::to_string(i) +
                "\n\nProse with `code` and *emphasis* that wraps across lines.\n\n"
                "```cpp\nint f" + std::to_string(i) + "() { return 42; }\n```\n";
            m.d.current.messages.push_back(std::move(msg));
        }
    }

    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;

    // 将颜色 TIER 绑定。
    //
    // resolve() 倒车 `native` 当等级太低,
    // 无法画出一种时,每个命名方案都使用。 detect_tier
    // ()无头报告最低层,因为Stdout是管子。没有这个探测器,
    // 探测器就会解决 每个 主题改为同一调色板和
    // "the ledger did not change" 是一种系系系系,而不是测试中的编码。
    // TrueColor 简直是真实的 终端 已经是了
    m.d.ui().tier = ui_prefs::ColorTier::TrueColor;

    app::detail::rehydrate_frozen(m);

    std::printf("transcript=%d msgs, frozen_through=%zu, blocks=%zu\n\n",
                msgs, m.ui.frozen_through, m.ui.frozen.elements().size());

    // 一个 "frame" :一个真正的循环会做什么—— view() 发布+构建,
    // 我们抓住将进入 传输.
    auto frame = [&](std::string& frozen, std::string& whole) {
        whole  = view_bytes(m);     // 将主题发布为 帧 的真值。
        frozen = frozen_bytes(m);
    };

    // ── 一个箭头键必须改变屏幕 ─────────────────────────────
    {
        std::printf("a single arrow key changes what is on screen\n");
        std::string f0, v0, f1, v1;
        frame(f0, v0);
        const std::string t0 = m.d.ui().theme;

        m = app::update(std::move(m), Msg{AppearanceThemeMove{1}}).first;
        frame(f1, v1);
        const std::string t1 = m.d.ui().theme;

        std::printf("    theme: '%s' -> '%s'\n", t0.c_str(), t1.c_str());
        // 区分 "the reducer never picked a real scheme"
        // 发件人 "it did but the ledger did not rebuild".
        // resolve() 颜色何时回原生 TIER 过于低沉,
        // 无法描绘一个命名的图案,因此无头运行可以默默地将一切解于同
        // 一个调色板上——它看起来完全像错误,而不会是它.
        {
            const auto r0 = ui_prefs::resolve(m.d.ui(), m.env.terminal);
            std::printf("    resolved tier=%d  scheme=%s  ink=%02x%02x%02x\n",
                        static_cast<int>(r0.tier),
                        (r0.theme == &maya::theme::native) ? "native(FALLBACK)"
                                                           : "named",
                        r0.theme->text.r(), r0.theme->text.g(), r0.theme->text.b());
        }
        check(t0 != t1,  "the model's theme actually changed");
        check(v0 != v1,  "the rendered VIEW differs after one press");
        check(f0 != f1,  "the FROZEN ledger differs after one press");
        std::printf("\n");
    }

    // ── 该 REPORT:两台压机,两台显式不同的屏幕 ───────────
    // 需要移动两次方向键，才能看到主题变化。
    // 表示按 N 和 按 N+1 键 渲染 完全一样,他们肯定不会
    {
        std::printf("consecutive presses each change the screen\n");
        int unchanged = 0;
        std::string prev_f, prev_v;
        frame(prev_f, prev_v);

        for (int k = 0; k < 8; ++k) {
            m = app::update(std::move(m), Msg{AppearanceThemeMove{1}}).first;
            std::string f, v;
            frame(f, v);
            if (f == prev_f) {
                ++unchanged;
                std::printf("    press %d: frozen ledger IDENTICAL to previous\n",
                            k + 1);
            }
            prev_f = std::move(f);
            prev_v = std::move(v);
        }
        std::printf("    presses that changed nothing on screen: %d / 8\n",
                    unchanged);
        check(unchanged == 0, "every press repaints (no two-press lag)");
        std::printf("\n");
    }

    if (failures == 0) {
        std::printf("PASS — no lag reproduced in the reducer/view layer\n");
        return 0;
    }
    std::printf("FAILED: %d check(s) — bug reproduced here\n", failures);
    return 1;
}
