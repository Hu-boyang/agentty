// anthropic_md_stream - 抓住一个真实的 Anthropic
// SSE 标记下 数据流 一次,然后通过 maya's StreamingMarkdown
// 逐步显示-fx 路径,这样我们就可以验证打字机动画 相对于真实 字
// 节 (德尔塔大小, -delta 时间),不需要网络访问 CI.
//
// 模式 :
//   --capture <out.jsonl> [--prompt "..."] [--model
//       表演 表演 演出 表演 一个 现场直播 调用 改为 api.anthropic.com 使用与主版本
//       相同的认证分辨率 二进制程序 (~/.config/agentty/credentials.json
//       或 或 时 间 ANTHROPIC_API_KEY。各附录 StreamTextDelta 作为
//       JSON 线条 : { "t_ms": <int>, "delta": "<text>"}.
//       t_ms 是那个 单调递增的 由第一三角洲供资。
//
//   --replay <in.jsonl> [--realtime] [
//       --width 100] [--no-fx 读了记录的三角洲 把它们推向一个
//       StreamingMarkdown 与 reveal_fx ON,
//       渲染 每一个 帧 来点小酒 --realtime 记录的cadence 时在三角洲
//       之间睡眠;默认值为 "as fast as possible" 但仍在投票中 渲染
//       16毫秒时, 逐步显示 光标明显走动。
//
// 故意最小化:这是开发者工具/长凳,而不是测试项. EXCLUDE_FROM_ALL
// CMakeLists中; 以CMake构建 --build 构
//   cmake --build build --target anthropic_md_stream

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"  // 完成( I) provider::StreamResult
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/runtime/msg.hpp"

#include <maya/maya.hpp>
#include <maya/print.hpp>
#include <maya/widget/markdown.hpp>

using namespace agentty;
using namespace std::chrono;
using json = nlohmann::json;

namespace {

// ── 共享帮助 ────────────────────────────────────────────────────────

[[nodiscard]] std::string escape_json_text(std::string_view s) {
    // 自动操作控制器 UTF -8+ 控制器 字节 正确; 圆 -trip 途经 json::value
    // ()是一款 -liner 而不是手, 手, 而不是手 -rolling - 逃出来了
    return json(s).dump();
}

void die(std::string_view msg, int code = 2) {
    std::println(stderr, "anthropic_md_stream: {}", msg);
    std::exit(code);
}

// ── 抓取 ───────────────────────────────────────────────────────────────

constexpr const char* kDefaultPrompt =
    "Write a richly-formatted markdown response (about 800-1200 words) "
    "that exercises every shape: an H1 title, two H2 subsections, a "
    "bulleted list, a numbered list, an inline code span, a fenced code "
    "block (~15 lines of C++), a blockquote, a table with 3 columns and "
    "5 rows, and a closing paragraph with **bold** and *italic*. Topic: "
    "'a brief tour of CommonMark for terminal renderers'. Plain prose, "
    "no preamble.";

int do_capture(const std::string& out_path,
               const std::string& prompt,
               const std::string& model) {
    // 解脱之心如 main.cpp 已经。
    auth::Credentials creds = auth::resolve(::agentty::IoAccess::grant(), /*cli_api_key=*/"");
    auth::AuthHeader  hdr   = auth::make_auth_header(creds);
    if (auth::is_empty(hdr)) {
        die("no credentials — set ANTHROPIC_API_KEY or run `agentty login`", 3);
    }

    Thread t;
    Message m;
    m.role = Role::User;
    m.text = prompt;
    t.messages.push_back(std::move(m));

    provider::anthropic::Request req;
    req.model         = model.empty() ? std::string{"claude-sonnet-4-5"} : model;
    req.system_prompt = provider::default_system_prompt(::agentty::IoAccess::grant());
    req.messages      = t.messages;
    req.max_tokens    = provider::kSafeMaxTokens;
    req.auth          = hdr;

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) die("cannot open output: " + out_path, 4);

    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
    std::string       err_msg;
    std::size_t       delta_count = 0;
    std::size_t       total_bytes = 0;

    auto t0 = steady_clock::time_point::min();
    auto sink = [&](Msg m) {
        auto* sm = std::get_if<msg::StreamMsg>(&m);
        if (!sm) return;
        if (auto* d = std::get_if<StreamTextDelta>(sm)) {
            auto now = steady_clock::now();
            if (t0 == steady_clock::time_point::min()) t0 = now;
            const auto t_ms =
                duration_cast<milliseconds>(now - t0).count();
            out << R"({"t_ms":)" << t_ms
                << R"(,"delta":)" << escape_json_text(d->text)
                << "}\n";
            out.flush();
            ++delta_count;
            total_bytes += d->text.size();
            std::print(stderr, ".");
            std::fflush(stderr);
        } else if (std::get_if<StreamFinished>(sm)) {
            finished = true;
        } else if (auto* e = std::get_if<StreamError>(sm)) {
            failed   = true;
            err_msg  = e->message;
            finished = true;
        }
    };

    std::println(stderr, "→ capturing from api.anthropic.com (model={}) ...", req.model);
    const provider::StreamResult sr =
        provider::anthropic::run_stream_sync(std::move(req), sink, /*cancel=*/{});
    if (!sr.ok() && !failed) {
        failed = true;
        if (sr.error) err_msg = *sr.error;
    }
    std::println(stderr, "");

    if (failed) {
        std::println(stderr, "stream error: {}", err_msg);
        return 5;
    }
    std::println(stderr, "captured {} deltas, {} bytes total → {}",
                 delta_count, total_bytes, out_path);
    return 0;
}

// ── 重播( E) ────────────────────────────────────────────────────────────────

struct Delta { long long t_ms; std::string text; };

std::vector<Delta> load_fixture(const std::string& path) {
    std::ifstream in(path);
    if (!in) die("cannot open fixture: " + path, 4);
    std::vector<Delta> out;
    std::string        line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            out.push_back({j.at("t_ms").get<long long>(),
                           j.at("delta").get<std::string>()});
        } catch (const std::exception& e) {
            die(std::string{"bad fixture line: "} + e.what(), 4);
        }
    }
    if (out.empty()) die("fixture has no deltas", 4);
    return out;
}

int do_replay(const std::string& in_path,
              bool realtime,
              int width,
              bool fx_on,
              double floor_cps,
              double drain_secs,
              double feed_cps,
              bool trace) {
    auto deltas = load_fixture(in_path);
    std::println(stderr,
        "→ replay {} deltas (realtime={}, width={}, fx={}, cps={}, drain={}s, feed_cps={}, trace={})",
        deltas.size(), realtime, width, fx_on, floor_cps, drain_secs,
        feed_cps > 0.0 ? std::to_string(feed_cps) : std::string{"∞"}, trace);

    // 共享 md 由生产商提供 线程 读作: maya::live's 渲染 线程.
    // StreamingMarkdown::append() + build() 未记录为
    // 线程-safe ; 对于 dev 工具箱(单一的生产者,单一的消费者,附件总是在构建前看到)
    // 小工具箱(Single Production, single close,
    // application always before building sees it) 竞态
    // 窗口为可接受。 互斥量 环绕 append/build 并且不是视觉验证所必需的
    auto md = std::make_shared<maya::StreamingMarkdown>();
    md->set_live(true);
    md->set_reveal_fx(fx_on);
    md->set_reveal_pacing(floor_cps, drain_secs);

    std::atomic<bool> done{false};
    auto wall_start = steady_clock::now();

    // 将所有三角洲平整成一个 字节 数据流 何时 --feed-cps 内容在 :
    // 我们滴代码点 -by-codepoint。
    // 这就是打字机的感觉。 真实 ——减记 解析器's
    // commit_range 无法比用户看到的时间更快地解析段落,
    // 因此承诺的前缀不会在前端闪烁 -styled.
    // 没有油门,150 -char 三角洲的爆发 全部着陆 字节 一次,
    // 一个 解析器 执行本款( e.g 打开 `\n\n` ),
    // 以及制作的带有全风格的前缀画—— maya's 逐步显示 光标只显示
    // 静态动画 -live TAIL 之后,用户会感知
    // 所有内容先出现，随后才播放动画。
    std::string all_bytes;
    if (feed_cps > 0.0) {
        std::size_t total = 0;
        for (const auto& d : deltas) total += d.text.size();
        all_bytes.reserve(total);
        for (const auto& d : deltas) all_bytes.append(d.text);
    }

    if (trace) {
        // 跟踪模式绕行 maya::live 运行一个手动 渲染 循环,
        // 记录每个 帧's source/reveal/committed
        // 状态到 stderr。让我们 SEE 不论来自制片人,
        // 逐步显示 光标、 承诺或 渲染器.
        auto start = steady_clock::now();
        std::thread tprod([&, md] {
            if (feed_cps > 0.0) {
                const auto per_cp = duration_cast<nanoseconds>(
                    duration<double>(1.0 / feed_cps));
                auto next_wake = steady_clock::now();
                std::size_t i = 0;
                while (i < all_bytes.size()) {
                    std::size_t j = i + 1;
                    while (j < all_bytes.size() &&
                           (static_cast<unsigned char>(all_bytes[j]) & 0xC0) == 0x80)
                        ++j;
                    md->append(std::string_view{all_bytes.data() + i, j - i});
                    i = j;
                    next_wake += per_cp;
                    std::this_thread::sleep_until(next_wake);
                }
            } else {
                for (std::size_t i = 0; i < deltas.size(); ++i) {
                    if (realtime && i > 0) {
                        auto target = start + milliseconds(deltas[i].t_ms);
                        std::this_thread::sleep_until(target);
                    }
                    md->append(deltas[i].text);
                }
            }
            std::this_thread::sleep_for(1500ms);
            md->finish();
            done = true;
        });

        // 帧 循环。 渲染 每一个 帧 改为普通字符串( 无)
        // ANSI ),测量可见字符的显示数量,并记录每个字符的显示数。
        // -frame 三角洲,如果打字机很滑, 就会增加~
        // floor_cps * 0.033个字符/33毫秒
        // 帧 如果它爆炸,我们会看到 长的平展 之后的大跳跃。
        std::size_t last_visible = 0;
        int frame = 0;
        while (!done) {
            const maya::Element& el = md->build();
            std::string rendered = maya::render_to_string(el, width);
            // 计数可见( 非) -whitespace 编号、非 -newline
            // (字符作为代词) "what the user can see"
            // 中添加了白空+新线。 渲染器 用于布局, 并会将每个
            // 泥土 -frame 三角洲信号
            std::size_t visible = 0;
            for (char c : rendered) {
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                    ++visible;
            }
            // 成行的渲染高度 = 新线数 + 1.
            // 这是驱动铬位置的信号; 这里的振荡 IS
            // 闪烁器。
            std::size_t rows = 1;
            for (char c : rendered) if (c == '\n') ++rows;
            static std::size_t last_rows = 0;
            const long long row_delta = static_cast<long long>(rows)
                                      - static_cast<long long>(last_rows);
            const auto t_ms = duration_cast<milliseconds>(
                steady_clock::now() - start).count();
            const long long delta = static_cast<long long>(visible)
                                  - static_cast<long long>(last_visible);
            std::println(stderr,
                "[{:>6} ms] frame={:>4} rows={:>4} Δrows={:+3} visible={:>5} Δ={:+4}",
                t_ms, frame, rows, row_delta, visible, delta);
            last_visible = visible;
            last_rows = rows;
            ++frame;
            std::this_thread::sleep_for(33ms);
        }
        tprod.join();
        std::println(stderr, "→ trace done ({} deltas, {} frames)", deltas.size(), frame);
        return 0;
    }

    std::thread producer([&, md] {
        if (feed_cps > 0.0) {
            // 代码point-paced滴滴. UTF-8
            // 代码点边界=字节,其前2位数不10.
            // 步行字节XX数据流,按要求的速率一次释放1 cp.
            const auto per_cp = duration_cast<nanoseconds>(
                duration<double>(1.0 / feed_cps));
            auto next_wake = steady_clock::now();
            std::size_t i = 0;
            while (i < all_bytes.size()) {
                std::size_t j = i + 1;
                while (j < all_bytes.size() &&
                       (static_cast<unsigned char>(all_bytes[j]) & 0xC0) == 0x80)
                    ++j;
                md->append(std::string_view{all_bytes.data() + i, j - i});
                i = j;
                next_wake += per_cp;
                std::this_thread::sleep_until(next_wake);
            }
        } else {
            for (std::size_t i = 0; i < deltas.size(); ++i) {
                if (realtime && i > 0) {
                    auto target = wall_start + milliseconds(deltas[i].t_ms);
                    std::this_thread::sleep_until(target);
                }
                md->append(deltas[i].text);
            }
        }
        // 让逐步显示光标在停止前追上+铬结算.
        std::this_thread::sleep_for(2s);
        md->finish();
        std::this_thread::sleep_for(500ms);
        done = true;
    });

    // 将主机分割删除 maya::live 和全球化问题 maya::quit
    // 模式是 INTERACTIVE 重放——试臂全部运行 `det`
    // 30岁 FPS 渲染 同一建筑商的圆圈是诚实的 -in : 它
    // 行使相同的 StreamingMarkdown 路径和结束当
    // 制作人设定 `done` ,不退出 标志 需要的,请说明。
    while (!done) {
        (void)maya::render_to_string(md->build(), width);
        std::this_thread::sleep_for(33ms);
    }

    producer.join();
    std::println(stderr, "→ replay done ({} deltas)", deltas.size());
    return 0;
}

// ── 确定性的重玩(可信赖的衡量) ─────────────────────
// 单身 -threaded 时速。 帧 步骤; 附加每个已
// 记录的三角洲 t_ms 已经通过; 渲染 每隔一段时间 帧
// 计量单位 -frame 可见-character 三角洲,
// 因为一切都在一线运行 线程 冻结时钟时,源/夹/光标/可见均一致
// ——无制片人 -thread 竞态 ,没有墙 -clock 紧张。
// 这就是我们所知道的,在 真实 记录 字节 ,是否为 逐步显示 滑翔机。 滑翔器。
int do_det(const std::string& in_path, int width, double floor_cps,
           double drain_secs, bool fx_on, int snap_at_frame, int snap_glide_ms,
           long long assert_max_delta, bool adaptive,
           long long assert_finalize_max, long long assert_finalize_ms,
           bool deco_on = true, int frame_div = 1,
           double adapt_min = 20.0, double adapt_max = 220.0) {
    std::vector<Delta> deltas = load_fixture(in_path);
    if (deltas.empty()) { std::println(stderr, "no deltas"); return 2; }

    maya::testing::freeze_anim_clock(0);
    // 测试中的运动级别。 "Reduced" 慢着
    // 就意味着什么 链接 ;没有它,长凳只能测量两
    // 个极端.
    maya::anim::set_frame_divisor(frame_div);
    maya::StreamingMarkdown md;
    md.set_live(true);
    md.set_reveal_fx(fx_on);
    // 三个运动水平的外观 面板 提供这两个交换机的映射,
    // 所以只能表示 fx 的长凳 on/off 无法测量
    // MIDDLE ——高低的级别, -latency 链
    // 接 (问题36):保留进步剪辑(进步信息),
    // 并放下装饰性的甘油churn(不是信息).
    //   满=fx 开, deco 开,
    //   decod 开, deco
    //   OFF 关闭=fx 关闭, 解码
    md.set_reveal_decorate(fx_on && deco_on);
    md.set_reveal_pacing(floor_cps, drain_secs);
    // 适应性WINDOW是测试中剖面图的一部分,而不是细节:
    // 车道的天花板是决定光标是否冲过一个破裂,
    // 然后坐落在边缘。默认它在这里测量了答录道的
    // 窗口( 20. 220) , 同时自称是推理剖面,
    // 也就是再次躺着的仪器。
    if (adaptive) md.set_reveal_adaptive(true, adapt_min, adapt_max);

    std::size_t wire_bytes_total = 0, wire_bytes_changed = 0, wire_frames_changed = 0;
    std::string prev_render;

    auto visible_of = [&](const maya::Element& el) -> std::size_t {
        std::string s = maya::render_to_string(el, width);
        // 数到真 CONTENT 只跳过代码点:新行、
        // 空格和(关键)框 -drawing 块 -element
        // 铬(U+2500.U+259F-标题下划线规则,表框),
        // 这些是装饰 渲染器 当块存在时添加整块; 计为 "visible"
        // 报告一个标题的满 -width 下划线为内容破碎。
        std::size_t v = 0;
        for (std::size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if ((c & 0x80) == 0) {            // ASCII
                if (c != '\n' && c != ' ') ++v;
                ++i;
            } else if ((c & 0xE0) == 0xC0) {  // 2-byte 东
                ++v; i += 2;
            } else if ((c & 0xF0) == 0xE0) {  // 3-byte 东
                // 解码以测试box-drawing/block范围.
                char32_t cp = (c & 0x0F) << 12;
                if (i + 1 < s.size()) cp |= (static_cast<unsigned char>(s[i+1]) & 0x3F) << 6;
                if (i + 2 < s.size()) cp |= (static_cast<unsigned char>(s[i+2]) & 0x3F);
                if (!(cp >= 0x2500 && cp <= 0x259F)) ++v;   // 跳过铬
                i += 3;
            } else {                          // 4-byte(emoji等) 互联网档案馆的存檔,存档日期2014-09-02.
                ++v; i += 4;
            }
        }
        return v;
    };

    constexpr std::int64_t kFrameMs = 16;
    const std::int64_t last_t = deltas.back().t_ms;
    const std::int64_t end_t  = last_t + 4000;   // 排水量 末尾

    std::size_t di = 0, prev_vis = 0;
    long long max_delta = 0, worst_t = 0, max_delta_all = 0;
    int frame = 0, over24 = 0;
    // 定稿 -glide 仪器(# 5 正核查):
    // 流式传输 上方的门刻意 EXCLUDES 定稿 帧 ,
    // 所以结束 -of-turn 合同——一种有界限
    // 的可见滑翔机,从未贴过,只是通过眼球来验证。
    //   • fin_max_delta : 最糟糕的单曲定稿 -frame 逐步显示 滑翔机
    //     画了几幅cells/frame;一个糊涂了整个积压。
    //   • fin_arm_t / fin_land_t : 墙 -clock 第一次定稿以来
    //     帧 以斜坡截止时间+和解窗口(部件通过拼接
    //     分辨率持有 live_)为界:
    //     settle_window_ms ×2.5××940 ms).
    long long fin_max_delta = 0, fin_worst_t = 0;
    std::int64_t fin_arm_t = -1, fin_land_t = -1;
    std::println(stderr,
        "→ det replay {} deltas (w={}, cps={}, drain={}s, fx={})",
        deltas.size(), width, floor_cps, drain_secs, fx_on);

    for (std::int64_t t = 0; t <= end_t; t += kFrameMs) {
        while (di < deltas.size() && deltas[di].t_ms <= t) {
            md.append(deltas[di].text);
            ++di;
        }
        if (di >= deltas.size()) md.request_finalize(200);
        // 模拟 a TOOL BOUNDARY: agentty 调用 snap_reveal_to_edge
        // () 当工具卡出现中点时 -stream (turn.cpp - 它跳过
        // 逐步显示 光标直接粘贴到边缘 带类型的-but-unrevealed 积压案件 帧。
        // 这是用户在真实转弯时看到的破裂(
        // 首字符停住，其余内容直到下一次工具调用才出现。
        // 纯净的 -text 重放永远不会触发。
        if (snap_at_frame >= 0 && frame == snap_at_frame) {
            md.snap_reveal_to_edge(snap_glide_ms);
            std::println(stderr, "  >>> snap_reveal_to_edge({}) at frame {} "
                                 "(simulated tool boundary)", snap_glide_ms, frame);
        }
        const std::size_t vis = visible_of(md.build());
        // 传输 COST 内容 存储单元 正确的衡量标准
        // "does the reveal glide" 但他们不是
        // 高人 -latency 链接 支付(问题36): agentty
        // 穿过摩什的 传输 是那个 RENDERED FRAME 越狱的
        // 顺序和所有,所以测量这个, 并测量它有多少 CHANGED ,
        // 因为没有变化 帧 是一个 渲染 门应该完全跳过。
        {
            std::string s = maya::render_to_string(md.build(), width);
            wire_bytes_total += s.size();
            if (s != prev_render) {
                ++wire_frames_changed;
                // 最长的常见 prefix/suffix : 一个粗糙的立
                // 面 -in 以什么为笑柄, 终端 作家会派来
                std::size_t p = 0;
                const std::size_t lim = std::min(s.size(), prev_render.size());
                while (p < lim && s[p] == prev_render[p]) ++p;
                std::size_t q = 0;
                while (q < lim - p && s[s.size()-1-q] == prev_render[prev_render.size()-1-q]) ++q;
                wire_bytes_changed += s.size() - p - q;
                prev_render = std::move(s);
            }
        }
        if (const char* df = std::getenv("DET_DUMP_FRAME");
            df && frame == std::atoi(df)) {
            std::println(stderr, "===== DUMP frame {} (src={} clip={} tailclip={} cur={:.0f} cmt={}) =====",
                frame, md.debug_source_size(), md.debug_reveal_byte_clip(),
                md.debug_tail_clip(), md.debug_reveal_cp(), md.debug_committed());
            std::string s = maya::render_to_string(md.build(), width);
            std::println(stderr, "---- rendered ({} bytes) ----\n{}\n---- end ----", s.size(), s);
        }
        const long long d = static_cast<long long>(vis)
                          - static_cast<long long>(prev_vis);
        // 追踪最大爆炸量 STREAMING 正文 仅此而已。
        // 帧 故意的结束 -of-turn / 工具 -boundary
        // 滑翔机可以更快地降落 末尾 数着会 标志 打算的和解作为爆发。
        // ( max_delta_all 仍报告为能见度。
        // )
        if (d > max_delta_all) { max_delta_all = d; }
        if (!md.is_finalizing() && d > max_delta) { max_delta = d; worst_t = t; }
        if (!md.is_finalizing() && d > 24) ++over24;
        if (md.is_finalizing()) {
            if (fin_arm_t < 0) fin_arm_t = t;
            if (d > fin_max_delta) { fin_max_delta = d; fin_worst_t = t; }
        }
        if (fin_arm_t >= 0 && fin_land_t < 0 && !md.is_live())
            fin_land_t = t;
        if (d != 0 || (frame % 30 == 0))
            std::println(stderr,
                "[{:>6} ms] f={:>4} vis={:>5} d={:+5} src={} clip={} cur={:.0f} cmt={} live={}",
                t, frame, vis, d,
                md.debug_source_size(), md.debug_reveal_byte_clip(),
                md.debug_reveal_cp(), md.debug_committed(), (int)md.is_live());
        prev_vis = vis;
        ++frame;
        maya::testing::advance_anim_clock_ms(kFrameMs);
        if (di >= deltas.size() && !md.is_live() && !md.is_finalizing()) break;
    }
    maya::testing::unfreeze_anim_clock();
    std::println(stderr,
        "\u2192 wire cost: frames={} changed={} bytes_rendered={} bytes_changed={} "
        "(avg {} changed B/frame)",
        frame, wire_frames_changed, wire_bytes_total, wire_bytes_changed,
        frame ? wire_bytes_changed / frame : 0);
    std::println(stderr,
        "→ det done: frames={} max_frame_delta={} (@ {} ms) frames_over_24={} "
        "(incl-settle max={})",
        frame, max_delta, worst_t, over24, max_delta_all);
    std::println(stderr,
        "→ finalize glide: max_delta={} (@ {} ms) armed@{} ms landed@{} ms "
        "({} ms arm→land)",
        fin_max_delta, fin_worst_t, fin_arm_t, fin_land_t,
        (fin_arm_t >= 0 && fin_land_t >= 0) ? fin_land_t - fin_arm_t : -1);
    // 退路门:爆破是单人 帧 显示的比 assert_max_delta
    // 内容 存储单元 中忽略的 settle/finalize 末尾 (
    // 最后敲定) 帧 究竟是那片被蓄意破坏的土地吗? -the-tail 滑翔,
    // 允许移动更快) 0 = 无断言(交互/测量使用).
    if (assert_max_delta > 0 && max_delta > assert_max_delta) {
        std::println(stderr,
            "FAIL: max_frame_delta {} exceeds cap {} — the reveal burst "
            "instead of gliding", max_delta, assert_max_delta);
        return 1;
    }
    // 5号正门 --assert-finalize-max :甚至
    // 故意占地的地盘 -the-tail 滑翔必须 PACED - 最终
    // 决定 帧 发现所有积压的 立即结束 -of-turn 粘贴滑翔板
    // 是用来清除的。 --assert-finalize-ms :滑翔机
    // 必须实际 LAND 约束范围内的(live_off)——捕获一个楔形
    // 斜坡(#2级)和一个无约束的适应性拉伸(#1级).
    if (assert_finalize_max > 0 && fin_max_delta > assert_finalize_max) {
        std::println(stderr,
            "FAIL: finalize-frame delta {} exceeds cap {} — the end-of-turn "
            "glide pasted instead of gliding",
            fin_max_delta, assert_finalize_max);
        return 1;
    }
    if (assert_finalize_ms > 0) {
        if (fin_arm_t < 0 || fin_land_t < 0) {
            std::println(stderr,
                "FAIL: finalize glide never {} (armed@{} landed@{}) — the "
                "ramp wedged instead of landing",
                fin_arm_t < 0 ? "armed" : "landed", fin_arm_t, fin_land_t);
            return 1;
        }
        if (fin_land_t - fin_arm_t > assert_finalize_ms) {
            std::println(stderr,
                "FAIL: finalize glide took {} ms (cap {}) — deadline "
                "stretched past its contract",
                fin_land_t - fin_arm_t, assert_finalize_ms);
            return 1;
        }
    }
    return 0;
}

void usage() {
    std::println(stderr,
        "anthropic_md_stream — capture/replay real Anthropic SSE for maya md tests\n"
        "\n"
        "  capture <out.jsonl> [--prompt \"...\"] [--model claude-...]\n"
        "  replay  <in.jsonl>  [--realtime | --feed-cps N]\n"
        "                      [--width N] [--no-fx]\n"
        "                      [--cps N] [--drain SECS]\n"
        "\n"
        "  --realtime     feed each delta at the recorded inter-delta gap.\n"
        "                 Each delta still lands as ONE append() — the\n"
        "                 markdown parser commits whole blocks instantly,\n"
        "                 so settled paragraphs appear in one go.\n"
        "  --feed-cps N   drip the stream byte-by-byte at N codepoints/sec\n"
        "                 (e.g. 20 = 50 ms per char). This is the REAL\n"
        "                 typewriter — commit_range can't settle a paragraph\n"
        "                 faster than chars appear, so the user sees every\n"
        "                 byte show up. Try `--feed-cps 25` first.\n"
        "  --cps N        floor cps of maya's own reveal cursor (default 120).\n"
        "  --drain SECS   target seconds to clear a burst backlog (default 0.8).\n"
        "  --adaptive-min/--adaptive-max N   the lane's adaptive cps window\n"
        "                 (production: answer 25..180, reasoning 45..280).\n");
}

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string mode = argv[1];
    std::string path = argv[2];

    if (mode == "capture") {
        std::string prompt = kDefaultPrompt;
        std::string model;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--prompt" && i + 1 < argc) prompt = argv[++i];
            else if (a == "--model" && i + 1 < argc) model = argv[++i];
            else { usage(); return 1; }
        }
        return do_capture(path, prompt, model);
    }
    if (mode == "det") {
        int    width      = 100;
        double floor_cps  = 90.0;   // 生产默认
        double drain_secs = 0.15;   // 生产默认
        bool   fx_on      = true;
        int    snap_at    = -1;     // 帧 发射模拟工具 -boundary 响亮
        int    snap_glide = 0;      // 0 = 即时断裂; >0 = 带界滑翔仪 ms
        long long assert_max = 0;   // >0 = 流式传输 帧 冲过时失败
        bool   adaptive   = false;  // 自动 -tune 下至层 传输 费率
        bool   deco_on    = true;   // 减速运动=fx 启动, 脱机
        int    frame_div  = 1;      // 运动减少也稀疏 重绘
        long long assert_fin_max = 0;  // >0 = 帧的逐步显示的定稿上限
        long long assert_fin_ms  = 0;  // >0=臂墙上的盖子 -clock
        double adapt_min = 20.0;       // maya's set_reveal_adaptive 默认
        double adapt_max = 220.0;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if      (a == "--no-fx")  fx_on = false;
            else if (a == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
            else if (a == "--cps"   && i + 1 < argc) floor_cps  = std::atof(argv[++i]);
            else if (a == "--drain" && i + 1 < argc) drain_secs = std::atof(argv[++i]);
            else if (a == "--snap-at" && i + 1 < argc) snap_at = std::atoi(argv[++i]);
            else if (a == "--snap-glide" && i + 1 < argc) snap_glide = std::atoi(argv[++i]);
            else if (a == "--assert-max-delta" && i + 1 < argc) assert_max = std::atoll(argv[++i]);
            else if (a == "--assert-finalize-max" && i + 1 < argc) assert_fin_max = std::atoll(argv[++i]);
            else if (a == "--assert-finalize-ms" && i + 1 < argc) assert_fin_ms = std::atoll(argv[++i]);
            else if (a == "--adaptive") adaptive = true;
            else if (a == "--adaptive-min" && i + 1 < argc) adapt_min = std::atof(argv[++i]);
            else if (a == "--adaptive-max" && i + 1 < argc) adapt_max = std::atof(argv[++i]);
            else if (a == "--no-deco") deco_on = false;
            else if (a == "--frame-div" && i + 1 < argc) frame_div = std::atoi(argv[++i]);
            else { usage(); return 1; }
        }
        return do_det(path, width, floor_cps, drain_secs, fx_on, snap_at,
                      snap_glide, assert_max, adaptive,
                      assert_fin_max, assert_fin_ms, deco_on, frame_div,
                      adapt_min, adapt_max);
    }
    if (mode == "replay") {
        bool   realtime   = false, fx_on = true;
        int    width     = 100;
        double floor_cps = 120.0;
        double drain_secs = 0.8;
        double feed_cps   = 0.0;   // 0=未喉咙(使用三角洲卡登斯)
        bool   trace      = false;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if      (a == "--realtime") realtime = true;
            else if (a == "--no-fx")    fx_on    = false;
            else if (a == "--trace")    trace    = true;
            else if (a == "--width"   && i + 1 < argc) width = std::atoi(argv[++i]);
            else if (a == "--cps"     && i + 1 < argc) floor_cps  = std::atof(argv[++i]);
            else if (a == "--drain"   && i + 1 < argc) drain_secs = std::atof(argv[++i]);
            else if (a == "--feed-cps" && i + 1 < argc) feed_cps  = std::atof(argv[++i]);
            else { usage(); return 1; }
        }
        return do_replay(path, realtime, width, fx_on,
                         floor_cps, drain_secs, feed_cps, trace);
    }
    usage();
    return 1;
}
