// chatgpt_bundled_models_test - 守卫着 ChatGPT
// (Codex ) 供应商的模型发现,以对抗打破了 选择面板:
//
//   1. 退役的`gpt-5.1-codex`弹丸,服务器撤回,现在拒绝
//      初转时( 时) "model is not supported when using Codex with a ChatGPT account"
//      )它必须 绝不 显示在 list_models () 或由
//      default_model ()——不是现场直播. 目录 ,
//      而不是从离线捆绑 回退.
//   2. Hidden/internal 模型( 4) e.g. `codex-auto-review` ,可见度= "hide")
//      绝对不能在用户中浮出水面 -facing 目录.
//
// 测试适应环境:
//   • 未签字的 ——演习装订的OFFLINE 回退(必须是`gpt-5`)。
//   • 参加第LIVE 目录号演习(必须是非第-empty号,真正的子弹)。
// 不论哪种方式,退休的弹壳必须缺失,而且必须选择一个模型。
//
// 运行: 构建`chatgpt_bundled_models_test`目标, 执行。 退出 0 = 通过。

#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/provider/chatgpt/responses.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;

TEST_CASE("chatgpt bundled model catalog") {
    const bool signed_in = cc::responses_available();

    auto models = cc::list_models(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(!models.empty());   // 模型必须可选择 始终

    // 退休的服务器 -rejected 涕丸绝不能露出表面——这是
    // 用户所击中的准确的失败( 选择面板 默认为 `gpt-5.1-codex`
    // 也主张内部自动 -review 模型被过滤出。
    for (const auto& m : models) {
        CHECK(m.id.value != "gpt-5.1-codex");
        CHECK(m.id.value != "codex-auto-review");
    }

    // 默认值(目录指数0)必须是活的,可选择的弹簧.
    const std::string def = cc::default_model(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(!def.empty());
    CHECK(def != "gpt-5.1-codex");

    if (signed_in) {
        // Live 目录路径:获取的列表必须包含真实的sults,
        // 默认必须是其中之一(无论捆绑的占位符都是盲目的).
        bool default_in_list = false;
        for (const auto& m : models)
            if (m.id.value == def) { default_in_list = true; break; }
        CHECK(default_in_list);
    } else {
        // 离线捆绑 回退 :必须是马厩,永远 -accepted `gpt-5`.
        CHECK(def == "gpt-5");
    }

    (void)signed_in;
}
