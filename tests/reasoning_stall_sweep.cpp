// reasoning_stall_sweep ——推理块的高度跨越a.
// 真实 包括摊位 包括车厢
//
// 每一个其他推理测试每帧上都输入部件新字节,
// 所以用户实际抱怨的状态是他们从未发过渲染:
// 传输在逐步显示赶上后再坐到边缘时会安静一秒或多点.
// Trailing-gap行,最终坡道和窗口
// 自己的收成在所持的帧上都表现得和移动的
// 一样.
//
// 因此:在记录的时间戳60
// FPS重播tests/fixtures/reasoning_burst_shape.jsonl,
// 并在EVERY 帧上测量块的漆高——包括静静的,高度绝不能下降,无论是在思维模式中
// 还是思维模式中.

#include <maya/core/anim_clock.hpp>
#include <maya/print.hpp>
#include <maya/widget/markdown.hpp>
#include <maya/widget/reasoning.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Delta { long long t_ms; std::string text; };

// 固定的就是一个 JSON 每行对象 :{ "t_ms": N, "delta": "..."}.
std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out.push_back(s[i]); continue; }
        switch (s[++i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '"': out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back(s[i]); break;
        }
    }
    return out;
}

std::vector<Delta> load(const std::string& path) {
    std::vector<Delta> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto tpos = line.find("\"t_ms\"");
        const auto dpos = line.find("\"delta\"");
        if (tpos == std::string::npos || dpos == std::string::npos) continue;
        const auto colon = line.find(':', tpos);
        const long long t = std::atoll(line.c_str() + colon + 1);
        const auto q1 = line.find('"', line.find(':', dpos) + 1);
        if (q1 == std::string::npos) continue;
        std::size_t q2 = q1 + 1;
        while (q2 < line.size() && !(line[q2] == '"' && line[q2 - 1] != '\\')) ++q2;
        out.push_back({t, unescape(std::string_view{line}.substr(q1 + 1, q2 - q1 - 1))});
    }
    return out;
}

int rows_of(const std::string& s) {
    int n = 0;
    for (const char c : s) if (c == '\n') ++n;
    // 后端的空白行不是用户看到的高度
    std::size_t end = s.size();
    while (end > 0 && (s[end - 1] == '\n')) { --end; --n; }
    return n + 1;
}

struct Result {
    int frames = 0;
    int drops = 0;
    int worst = 0;
    long long worst_at_ms = 0;
    int at_edge = 0;
    int max_rows = 0;
};

Result sweep(const std::vector<Delta>& deltas, int cap, int width,
             bool trace) {
    maya::ReasoningStream::Config cfg;
    cfg.live_tail_rows = cap;
    cfg.gradient_body  = true;
    cfg.pulse          = true;
    maya::ReasoningStream rs{cfg};
    rs.set_live(true);

    // 生产形状: agentty 拥有 StreamingMarkdown (无) "#r" 缓
    // 存 并手握手握手掌 build() 改为 build_with_body (,
    // 所以速度 住在主机的部件上—— 不在 ReasoningStream 属于自己的
    maya::StreamingMarkdown md;
    md.set_reveal_fx(true);
    md.set_live(true);
    md.set_reveal_pacing(60.0, 0.55);
    md.set_reveal_adaptive(true, 45.0, 280.0);

    const long long last = deltas.empty() ? 0 : deltas.back().t_ms;
    const long long end_ms = last + 4000;   // 保留 渲染 之后 传输
    std::string src;
    std::size_t next = 0;
    Result r;
    int prev = 0;

    maya::testing::freeze_anim_clock(0);
    for (long long t = 0; t <= end_ms; t += 16) {
        bool fed = false;
        while (next < deltas.size() && deltas[next].t_ms <= t) {
            src += deltas[next].text;
            ++next;
            fed = true;
        }
        if (!src.empty()) md.set_content(src);
        rs.set_char_hint(src.size());
        const std::string frame =
            maya::render_to_string(rs.build_with_body(md.build()), width);
        const int rows = src.empty() ? 0 : rows_of(frame);
        if (rows < prev) {
            ++r.drops;
            if (prev - rows > r.worst) { r.worst = prev - rows; r.worst_at_ms = t; }
            if (trace)
                std::printf("  [%6lld ms] %d -> %d rows (src=%zu bytes%s)\n",
                            t, prev, rows, src.size(), fed ? ", delta" : ", quiet");
        }
        if (!fed) ++r.at_edge;
        r.max_rows = rows > r.max_rows ? rows : r.max_rows;
        prev = rows;
        ++r.frames;
        maya::testing::advance_anim_clock_ms(16);
    }
    maya::testing::unfreeze_anim_clock();
    return r;
}

}  // 命名空间结束。

int main(int argc, char** argv) {
    // ctest 通过注册项中的 FIXTURE 传入夹具路径，并从源码根目录
    // 运行，因此回退路径相对于仓库根目录解析。
    const std::string path = argc > 1
        ? argv[1]
        : std::string{"tests/fixtures/reasoning_burst_shape.jsonl"};
    const auto deltas = load(path);
    if (deltas.empty()) {
        std::printf("FAIL: no deltas loaded from %s\n", path.c_str());
        return 2;
    }
    std::printf("reasoning_stall_sweep: %zu deltas, last at %lld ms\n",
                deltas.size(), deltas.back().t_ms);

    int failures = 0;
    struct Case { const char* name; int cap; int width; };
    const Case cases[] = {
        {"collapsed (8-row window), w=72", 8, 72},
        {"collapsed (8-row window), w=50", 8, 50},
        {"shown (no window), w=72",        0, 72},
        {"shown (no window), w=50",        0, 50},
    };
    for (const auto& c : cases) {
        const Result r = sweep(deltas, c.cap, c.width, /*微量=( 微量)*/true);
        std::printf("  %-34s frames=%4d quiet=%4d max_rows=%2d drops=%d",
                    c.name, r.frames, r.at_edge, r.max_rows, r.drops);
        if (r.drops) {
            std::printf("  WORST -%d rows @ %lld ms", r.worst, r.worst_at_ms);
            ++failures;
        }
        std::puts("");
    }
    if (failures) {
        std::printf("FAIL: the block shrank in %d of %zu cases\n",
                    failures, sizeof(cases) / sizeof(cases[0]));
        return 1;
    }
    std::puts("OK: height never dropped, quiet frames included");
    return 0;
}
