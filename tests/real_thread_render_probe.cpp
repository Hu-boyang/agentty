// real_thread_render_probe — 加载 a 真实 线程 离盘和 渲染 就是这样
//
// 不是一个 单元测试 撞机探测器 瞄准一个 线程 文档,
// 它走准确的路径 TUI 选一个就走 线程 出处 选择面板 — 加载 →
// rehydrate_frozen → conversation_config
// → render_tree 宽度几度,所以堆叠溢出,或者在一旁的
// 一幅画 渲染 步行在此复制, 而不是只在用户的 终端.
//
//   ./build/real_thread_render_probe ~/.agentty/threads/<id>.json
//
// 0 = 已清理。 信号=错误, 并有可用的反跟踪。

#include <agentty/domain/session.hpp>
#include <agentty/io/persistence.hpp>
#include "agtest_cap.hpp"
#include <agentty/io/thread_log.hpp>
#include <agentty/runtime/model.hpp>
#include <agentty/runtime/view/thread/conversation.hpp>
#include <agentty/runtime/app/update/internal.hpp>

#include <maya/render/renderer.hpp>
#include <maya/widget/conversation.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

using namespace agentty;

// 元素中最深的巢穴水平 源码树. paint_element / 版式/
// 编辑 build_layout_tree 全部每关重复一次,所以这个数字是每关
// 重复的倍数 -frame 堆栈成本是必须适应的 线程's 8 MB 堆栈。
static int element_depth(const maya::Element& e, int d = 0) {
    int best = d;
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, maya::BoxElement>) {
            for (const auto& c : n.children)
                best = std::max(best, element_depth(c, d + 1));
        } else if constexpr (std::is_same_v<T, maya::ElementList>) {
            for (const auto& c : n.items)
                best = std::max(best, element_depth(c, d + 1));
        } else if constexpr (std::is_same_v<T, maya::ElementListRef>) {
            if (n.items_ref)
                for (const auto& c : *n.items_ref)
                    best = std::max(best, element_depth(c, d + 1));
        }
    }, e.inner);
    return best;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        // 没有 线程 给定: 没什么可探测的。这是Ctest如何引用它,
        // 所以它必须 SUCCEED 而不是报告失败——探测器是一只手
        // -run 工具(见框注) 头文件 ),而不是一个未加注意的回归测试。
        std::printf("real_thread_render_probe: no thread file given, nothing "
                    "to do.\nusage: %s <thread.json> [width]\n", argv[0]);
        return 0;
    }
    const std::filesystem::path p = argv[1];
    const int width = (argc > 2) ? std::atoi(argv[2]) : 120;

    auto t0 = std::chrono::steady_clock::now();
    std::printf("stage: load\n"); std::fflush(stdout);

    // 迁移 线程 谓 <id> .jsonl + .ofs +. ) 高温的电压.
    // meta.json ; 遗产是单 <id>.JSON。接受其中之一,
    // 所以同样的探测器测量 线程 迁移前后.
    std::optional<Thread> loaded;
    if (p.extension() == ".jsonl") {
        auto log = ThreadLog::open_path(p);
        if (log) loaded = log->load_thread();
    } else {
        // 子类 -time 编号 加载 所以探测器说 WHERE 编号 线程-switch
        // 耐久性为: 生 I/O ,则 JSON DOM 解析 ,或 DOM->线程 走着走着吧
        auto a = std::chrono::steady_clock::now();
        std::ifstream ifs(p, std::ios::binary);
        std::string buf;
        {
            std::error_code ec;
            auto sz = std::filesystem::file_size(p, ec);
            if (!ec && sz > 0) {
                buf.resize(static_cast<std::size_t>(sz));
                ifs.read(buf.data(), static_cast<std::streamsize>(sz));
                buf.resize(static_cast<std::size_t>(ifs.gcount()));
            }
        }
        auto b = std::chrono::steady_clock::now();
        auto io_ms = std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
        nlohmann::json j = nlohmann::json::parse(buf, nullptr, false);
        auto c = std::chrono::steady_clock::now();
        auto dom_ms = std::chrono::duration_cast<std::chrono::milliseconds>(c - b).count();
        std::printf("  io: %lld ms (%zu bytes)  json-dom: %lld ms\n",
                    (long long)io_ms, buf.size(), (long long)dom_ms);
        std::fflush(stdout);
        if (auto r = persistence::load_thread_file(::agentty::IoAccess::grant(), p)) loaded = std::move(*r);
    }
    std::printf("stage: load returned\n"); std::fflush(stdout);
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", p.c_str());
        return 2;
    }
    auto ms = [&](auto since) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - since).count();
    };
    std::printf("loaded %zu messages in %lld ms\n",
                loaded->messages.size(), (long long)ms(t0));
    std::fflush(stdout);

    Model m;
    m.d.current = std::move(*loaded);

    // MIGRATE=1: 加载 那样 保存 直接背( 自动保存在之后的
    // 线程 切换为)并报告文件 -size 更改。遗存内含 base64
    // 图像变成blob 引用,所以一个旧图像 -heavy 线程
    // 一次缩进 加载 之后就快了,永远的快
    if (std::getenv("MIGRATE")) {
        const auto before = std::filesystem::file_size(p);
        persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), m.d.current);
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
        const auto after_path =
            persistence::threads_dir() / (m.d.current.id.value + ".json");
        std::error_code ec;
        const auto after = std::filesystem::file_size(after_path, ec);
        std::printf("migrate: %.1f MB -> %.1f MB (%s)\n",
                    before / 1e6, ec ? 0.0 : after / 1e6,
                    after_path.string().c_str());
        std::fflush(stdout);
        return 0;
    }

    // BISECT=1: 二进制程序-search 第一次损坏的讯息前缀,
    // 所以是 1700 -message 线程 名称 一个 打破 渲染器 而不是
    // 离开 "somewhere in here".
    if (std::getenv("BISECT")) {
        const auto all = m.d.current.messages;
        std::printf("bisect over %zu messages\n", all.size());
        std::fflush(stdout);
        for (std::size_t n = 1; n <= all.size(); ++n) {
            Model probe;
            probe.d.current = m.d.current;
            probe.d.current.messages.assign(all.begin(), all.begin() + n);
            app::detail::rehydrate_frozen(probe);
            auto c = ui::conversation_config(probe);
            auto e = maya::Conversation{std::move(c)}.build();
            volatile std::size_t sz = maya::render_to_string(e, width).size();
            (void)sz;
            std::printf("ok through message %zu (role=%d, text=%zu bytes)\n",
                        n, (int)all[n - 1].role, all[n - 1].text.size());
            std::fflush(stdout);
        }
        std::printf("BISECT: no single prefix crashed\n");
        return 0;
    }

    auto t1 = std::chrono::steady_clock::now();
    std::printf("stage: rehydrate\n"); std::fflush(stdout);
    app::detail::rehydrate_frozen(m);
    std::printf("rehydrate_frozen: %lld ms, frozen=%zu through=%zu\n",
                (long long)ms(t1), m.ui.frozen.size(), m.ui.frozen_through);
    std::fflush(stdout);

    for (int w : {width, 80, 40}) {
        auto t2 = std::chrono::steady_clock::now();
        std::printf("stage: config w=%d\n", w); std::fflush(stdout);
        auto cfg = ui::conversation_config(m);
        std::printf("stage: build w=%d\n", w); std::fflush(stdout);
        auto el  = maya::Conversation{std::move(cfg)}.build();
        std::printf("stage: depth w=%d\n", w); std::fflush(stdout);
        std::printf("  element tree depth: %d\n", element_depth(el));
        std::fflush(stdout);
        // 深处 -copy 编号 源码树 之前 任何 渲染 摸摸它,
        // 如果这样 throws/corrupts ,损害发生在 BUILD
        // 时间和时间 渲染器 是个无辜的旁观者
        if (std::getenv("COPY_PROBE")) {
            std::printf("stage: copy-probe (pre-render)\n"); std::fflush(stdout);
            maya::Element copy = el;
            std::printf("  copy ok, depth %d\n", element_depth(copy));
            std::fflush(stdout);
        }
        std::printf("stage: render w=%d\n", w); std::fflush(stdout);
        const std::string out = maya::render_to_string(el, w);
        std::printf("width %3d: built+rendered in %lld ms, %zu bytes\n",
                    w, (long long)ms(t2), out.size());
        std::fflush(stdout);
    }

    std::printf("OK\n");
    return 0;
}
