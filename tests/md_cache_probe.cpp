// md_cache_probe ——按 -frame 费用 StreamingMarkdown::build
// 长 流式传输 形状,核查 记忆缓存 层积(摊销的O(1)/ 帧).
// 数据流 每个形状符号 -ish 逐块与 逐步显示 直播和汇报
// -frame 建构+ render_tree 费用超过 数据流 生活,
// 分为四重体 数据流 进展(统一概况=) 缓存 悬浮;
// 正在增长的剖面 = 某些 O( N) 路径 逃出备忘录。
#include <chrono>
#include <ctime>     // clock_gettime, CLOCK_THREAD_CPUTIME_ID
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <maya/core/anim_clock.hpp>
#include <maya/core/render_context.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/markdown.hpp>

using namespace maya;
using clk = std::chrono::steady_clock;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)

// 人数 -frame 成本按 THREAD CPU TIME 时间。
// 线程 已排定时间,但日期 已加载 框会随其他许多测试的运行而
// 增长 -- -- 测试转而为每次测试 -frame 成本与机器
// 的比值 加载. CPU 时间仅在此时提前 线程 开始 -core .
// (这本身并非完全固定: 内存-bandwidth 争议仍然
// 充盈真实 CPU 工作,这就是为什么测试条目也是
// RUN_SERIAL 下方的预算则包括会议室。 )
//
// CLOCK_THREAD_CPUTIME_ID 谓 POSIX ;
// 其他地方则回到墙壁时间,因为 回退 仍然工作,比一个不工作 编译.
[[nodiscard]] double cpu_now_us() noexcept {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return static_cast<double>(ts.tv_sec) * 1e6
             + static_cast<double>(ts.tv_nsec) / 1e3;
#endif
    return std::chrono::duration<double, std::micro>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

static constexpr int kWidth = 100;
static constexpr int kTermH = 40;

// `budget` 最大值为 q4/q1 生长这个形状在它算作逃跑之前
// 记忆缓存。这是 PER SHAPE 因为形状并非都有一个平坦的
// 诚实的描述: 越来越多的引文 -nested 栅栏真的要花更多钱 帧
// 随着它的生长,在闲置的盒子上测量~2.9x,而列表和段落形状坐落在
// ~1.1-1.2x. 探测器对每件事都使用一个全球3.0x,
// 这一次双向错误—— quote_fence_300 拥有0.1x 头室,
// 且当盒子忙碌时失败( 读作 flashy) ,
// 而 paras_200 被允许接近 TRIPLE 在别人发现之前
//
// 将合成O(N)成本注入定时区校准:
// 平面3.6x的中度漏泄,5-6x的坏漏,因此,
// 这些预算既能捕捉到,又能清除测量的闲置基线(下方
// 每个形状),并有缓冲或已加载机的空间。
struct ShapeGen { const char* name; std::string body; double budget; };

static std::vector<ShapeGen> shapes() {
    std::vector<ShapeGen> v;
    { // 长 LOOSE 列表——每个项目之间的空白(新的凝聚路径)
        std::string b;
        for (int i = 0; i < 400; ++i) {
            b += "- loose item number " + std::to_string(i)
               + " with a bit of text\n\n";
        }
        b += "after paragraph\n";
        v.push_back({"loose_list_400", std::move(b), 2.5});  // 闲~1.2x
    }
    { // 长TIGHT列表——块头人家的地盘(退路检查)
        std::string b;
        for (int i = 0; i < 400; ++i)
            b += "- tight item number " + std::to_string(i)
               + " with a bit of text\n";
        b += "\nafter paragraph\n";
        v.push_back({"tight_list_400", std::move(b), 2.5});  // 闲~1.2x
    }
    { // 长引号-nested 代码栅栏( 新的 dequote 等值扫描运行在
      // 标记-only 活线)
        std::string b = "> intro\n> ```\n";
        for (int i = 0; i < 300; ++i)
            b += "> quoted code line " + std::to_string(i) + "\n";
        b += "> ```\n\nafter\n";
        // 真正成长的形状: 脱引物等效扫描 -runs 标记
        // -only 活线,所以 -frame 成本随着围栏而上升。
        v.push_back({"quote_fence_300", std::move(b), 4.0});  // 闲~2.9x
    }
    { // 许多段落 – 执行路径/ 冻结前缀
        std::string b;
        for (int i = 0; i < 200; ++i)
            b += "paragraph number " + std::to_string(i)
               + " has some words in it to wrap around maybe.\n\n";
        v.push_back({"paras_200", std::move(b), 2.5});  // 闲置~1.1x
    }
    { // 长表格
        std::string b = "| Col A | Col B | Col C |\n|---|---|---|\n";
        for (int i = 0; i < 300; ++i)
            b += "| row " + std::to_string(i) + " | data | more |\n";
        b += "\nafter\n";
        v.push_back({"table_300", std::move(b), 2.5});  // 闲置~1.1x
    }
    { // 数量 链接-ref 定义(新零) -row 路径)
        std::string b = "See [a][1] and [b][2].\n\n";
        for (int i = 0; i < 200; ++i)
            b += "[" + std::to_string(i) + "]: https://example.com/"
               + std::to_string(i) + "\n";
        b += "\nafter\n";
        v.push_back({"link_refs_200", std::move(b), 3.0});  // 闲~1.9-2.2x
    }
    return v;
}

}  // 名称空间( 归并)

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    bool bad = false;
    for (auto& sh : shapes()) {
        if (only && std::strcmp(only, sh.name)) continue;
        StreamingMarkdown md;
        md.set_reveal_fx(true);
        md.set_reveal_pacing(2000.0, 0.3);  // 快光标:不要在逐步显示上瓶颈
        md.set_live(true);

        StylePool pool;
        std::vector<layout::LayoutNode> nodes;

        std::vector<double> frame_us;
        std::size_t fed = 0;
        const std::size_t chunk = 24;   // ~ 键 -sized
        while (fed < sh.body.size()) {
            std::size_t n = std::min(chunk, sh.body.size() - fed);
            md.append(std::string_view{sh.body}.substr(fed, n));
            fed += n;
            maya::testing::advance_anim_clock_ms(16);
            const double t0 = cpu_now_us();
            {
                RenderContext ctx{kWidth, kTermH, render_generation(), true};
                RenderContextGuard guard(ctx);
                Canvas c(kWidth, 6000, &pool);
                c.clear();
                render_tree(md.build(), c, pool, theme::native, nodes, true);
            }
            const double t1 = cpu_now_us();
            frame_us.push_back(t1 - t0);
        }
        // 人数 -quartile MEDIAN 而不是刻意的
        // 偶尔会有人拖着一个小小的 -preemption
        // 插值( 参见多 -millisecond `worst`
        // 值)无关 缓存 行为——a 已加载 `ctest -j`
        // 外面的人可以推小一点 -q1 ⁇ -q4 中位
        // 数跟踪到 TREND (O(N)漏掉会改变整个分布)
        // 同时拒绝孤立的摊位,因此它会一直诚实地争论.
        auto qmed = [&](std::size_t a, std::size_t b) {
            std::vector<double> w;
            for (std::size_t i = a; i < b && i < frame_us.size(); ++i)
                w.push_back(frame_us[i]);
            if (w.empty()) return 0.0;
            std::sort(w.begin(), w.end());
            return w[w.size() / 2];
        };
        std::size_t N = frame_us.size();
        double q1 = qmed(0, N/4), q2 = qmed(N/4, N/2),
               q3 = qmed(N/2, 3*N/4), q4 = qmed(3*N/4, N);
        double worst = 0; for (double f : frame_us) if (f > worst) worst = f;
        // 增长比率:最后四分位数对第一。
        // 中位数拒绝电源, 1ms 层保持子
        // -millisecond 触发良性比。 上限是这个形状自己的预算。
        // 见。 ShapeGen.
        double ratio = q1 > 0 ? q4 / q1 : 0.0;
        bool flag = q4 > 1000.0 && ratio > sh.budget;
        if (flag) bad = true;
        std::printf("%-18s frames=%4zu  q1=%7.0fus q2=%7.0fus q3=%7.0fus q4=%7.0fus  worst=%7.0fus  growth=%.1fx/%.1fx%s\n",
                    sh.name, N, q1, q2, q3, q4, worst, ratio, sh.budget,
                    flag ? "  <-- O(N) ESCAPE" : "");
    }
    if (!bad) std::puts("CACHES HOLD (flat per-frame profile)");
    return bad ? 1 : 0;
}
