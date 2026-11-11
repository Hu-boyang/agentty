// 这么贵 catalog_block (现在它解决了信任?)
//
// 继续运行 每个 转弯( E) provider::system_prompt_for →
// default_system_prompt ),信任门则添加了文件读作+a SHA - 256 超越一切技能 -
// 255 over every skill 正文 这条路是用来衡量的,而不是假设的。

#include "agentty/tool/skills.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace agentty::tools::skills;

int main() {
    const auto base = fs::temp_directory_path() / "agentty-catalog-bench";
    fs::remove_all(base);
    const auto home = base / "home";
    const auto root = home / ".agentty" / "skills";

    // 40个有现实主义技能的 正文 (~4 KB 每一个——一个真实的 SKILL.md).
    const std::string body(4096, 'x');
    for (int i = 0; i < 40; ++i) {
        const auto d = root / ("skill" + std::to_string(i));
        fs::create_directories(d);
        std::ofstream(d / "SKILL.md")
            << "---\nname: skill" << i
            << "\ndescription: does thing number " << i << "\n---\n"
            << body << "\n";
    }

    ::setenv("HOME", home.c_str(), 1);
    ::setenv("AGENTTY_HOME", (home / ".agentty").c_str(), 1);

    // 暖和技能 缓存 故我们测量 目录 而不是发现。
    (void)catalog_block(::agentty::IoAccess::grant());

    constexpr int kRuns = 200;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t sink = 0;
    for (int i = 0; i < kRuns; ++i) sink += catalog_block(::agentty::IoAccess::grant()).size();
    const auto t1 = std::chrono::steady_clock::now();

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto per = static_cast<long long>(us / kRuns);
    std::printf("catalog_block: %lld us/call over %d calls (40 skills, 4KB bodies)\n",
                per, kRuns);
    (void)sink;

    // A BUDGET 不只是一个数字。 catalog_block
    // ()运行在每一个转弯,运行在用户按下输入和请求外出之间的路径.
    // 是3400块 us/call 何时 all() 内容 -parsed 每隔一段时间
    // SKILL.md 百分比 调用 扔了那个 解析 远离; 统计 -only 签名证上写着:
    //
    // 1500是故意松开的,这是在共享的基础上建立的调试器 CI ,
    // 然后删除而非修改。 REGRESSION SHAPE
    // (净) -parsing 编号 库 转弯时,不要紧张。
    //
    // Windows : 数据 -only 传球为~80路面(40个技能
    // 迪尔+他们的) SKILL.md ),并设置一个 NTFS 元
    // 数据查询是 ~30 - 40 我们对~3 我们上 Linux
    // 健康通过量等于3毫秒 -parse 将警卫的伤亡率再降低几倍(每一
    // 次) SKILL.md 打开、阅读和散列,所以形状仍然显示。
#if defined(_WIN32)
    constexpr long long kBudgetUs = 6000;
#else
    constexpr long long kBudgetUs = 1500;
#endif
    const bool ok = per < kBudgetUs;
    if (!ok) {
        std::printf("FAIL: %lld us/call exceeds the %lld us budget.\n", per, kBudgetUs);
        std::printf("  something on the per-turn path is doing work again —\n");
        std::printf("  check all() still skips the parse on an unchanged signature.\n");
    }

    fs::remove_all(base);
    std::printf("%s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
