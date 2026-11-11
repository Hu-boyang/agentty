// SPDX-License-Identifier: Apache-2.0
//
// persistence_race_stubs.cpp — 链接-time 支点 settings/vault 半数
// persistence.cpp ,所以,我说 persistence_race_test 能够建立狭长的建筑。
//
// 原因. persistence_race_test 驱动自动同步 THREAD 保存 队列 :
// save_thread, load_all_threads, flush_pending_saves 永远
// 调用 load_settings 或 或 时 间 save_settings 永远不要碰提供商的钥匙。
//
// 不过 persistence.cpp 是一个 编译单元 和它的设置 :
// 调用 auth::keys::{加载,保存 } - > 保险库 -
// > 密钥托盘 - > 凭据 - > 提供者 注册表. 链接 链拉~
// 74 编译单元 用于代码的此测试不执行。在 -fsanitize=thread ,
// 其中, CI 竞态 车道生活, 每一个这些 编译器械。
//
// 换个方法就是 CI 建造了整个375座 -TU
// agentty_standalone_tests 由TSan负责这次测试
//
// SAFETY。每个根茎都代表一个函数,测试可能永远
// 无法到达,每个中断而不是返回一个假值 -- -- 因此,
// 如果未来的编辑结果到 persistence.cpp
// 执行 开始呼叫一个从 线程 路径,此测试会大声地在
// 调用 而不是悄悄地撒谎。这就是使这里诚实地抽泣的
// 财产:抽筋不能悄悄地改变测试措施。

#include "agentty/auth/keys.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void unreachable(const char* who) {
    std::fprintf(stderr,
        "persistence_race_test: %s was called, but this narrow build stubs "
        "it out because the THREAD save path is not supposed to reach the "
        "settings/vault half of persistence.cpp.\n"
        "Either the test grew a settings dependency (then link the real "
        "vault sources in cmake/AgenttyTests.cmake), or persistence.cpp's "
        "thread path grew one (then that is the bug).\n", who);
    std::abort();
}
}  // 命名空间结束。

namespace agentty::auth::keys {

KeyMap load() { unreachable("auth::keys::load"); }
bool save(const KeyMap&) { unreachable("auth::keys::save"); }

}  // namespace agentty::auth::keys

// persistence_race_test.cpp 谓 ALSO 独立
// 成员 归并 ,并显示 归并 重命名每个成员的 main() 改为
// <name> 通过源属性主(_M),该属性设定为 DIRECTORY 范围,
// 所以它适用于这个狭义的目标 复制同一个源 - - TU 编译 ,
// 但无 `main` 符号 出来吧
//
// 而不是对抗重命名( 源的第二份副本,
// 或目标) -scoped 将漂移于 归并 刚刚,
// 只是 调用 重命名产生的东西 一行,它保持 一个
// 复制给折叠的条目和狭小的条目的测试源,这就是点——
// 它们永远不能测试不同的东西.
extern int persistence_race_test_main();
int main() { return persistence_race_test_main(); }
