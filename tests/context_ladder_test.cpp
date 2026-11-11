// 上下文-window分辨率:梯子,和它所隐藏的1M模型.
//
// 报告: "still 1M models don't show up generally"
// 原因不是网关探测器,而是突袭 下方 就是这样
// ModelCapabilities::from_id ()回答
// 一个已知家庭的200k,其他一切的0,这是正确的。 Claude 几乎
// 对其它一切都是错误的,所以每个非 -Claude 模型以0到达梯子底部,
// 以200k默认值离开.
//
// 与活的models.devXX快照:7824型中的2698型实际窗口为1M或以上.
// gemini-2.5-pro(1M),gpt-4.1(1M) ,
// qwen3-coder-plus(1M"),ellama-4-scout
// (10M) ——都少报了5x或更糟糕,所有压缩时间都远早于需要,
// 所有报告的数字都无人申报.
//
// 梯子,最有力的证据首先:
//
//   1. 用户覆盖,他们配置了网关
//   2. 现场广告 API 谓 TRUTH 只有400块能溢出
//   3. models.dev 7824型号深处
//   4.id推断 Claude/GPT 家庭,[1m]后缀
//   5. 200k 默认无实可知
//
// 钉住它 ORDER ,不仅仅是值:每个运行
// 必须击败下面的,输给上面的,因为订单是设计.

#include "agtest.hpp"

#include "agentty/domain/catalog.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/store/store.hpp"

#include <string>
#include <cstdlib>

namespace {

// A models.dev-shaped 声明,不需要 快照 磁盘上。
void declare(const std::string& scope, const std::string& model, int tokens) {
    agentty::set_catalog_context_window(scope + "/" + model, tokens);
}

}  // 命名空间结束。

TEST_CASE("context ladder: models.dev rescues the non-Claude models") {
    agentty::store::Settings s;

    // 在任何声明之前: 一个未知的家庭没有什么可继续的,
    // 所以保守的默认状态是存在的。 这是被报道的bug的起始状态。
    CHECK(agentty::ui::resolve_context_window("google", "ctxtest-unknown-1", 0, s)
          == agentty::ui::kDefaultContextWindow);

    // 带有声明,则使用真实窗口.
    declare("google", "ctxtest-wide-1", 1'048'576);
    CHECK(agentty::ui::resolve_context_window("google", "ctxtest-wide-1", 0, s)
          == 1'048'576);

    // Claude 保持其编号 -inferred 窗口中,
    // 新运行不能干扰已经奏效的东西。
    CHECK(agentty::ui::resolve_context_window("anthropic", "claude-sonnet-4-5", 0, s)
          == 200'000);
    // ...包括明确[1m]变体.
    CHECK(agentty::ui::resolve_context_window("anthropic", "claude-sonnet-4-5[1m]", 0, s)
          == 1'000'000);
}

TEST_CASE("context ladder: the API outranks every static source") {
    agentty::store::Settings s;
    declare("acme", "ctxtest-wide-2", 1'000'000);

    // models.dev 门是32k,
    // 门是胜利的,是唯一知道如何 IT 也是
    // 唯一能拒绝溢出请求的 -party
    // 目录 描述模型一般不能推翻描述此部署
    // 的主机。
    CHECK(agentty::ui::resolve_context_window("acme", "ctxtest-wide-2",
                                              /*广告=*/32'768, s) == 32'768);

    // 且无广告,则宣用之.
    CHECK(agentty::ui::resolve_context_window("acme", "ctxtest-wide-2", 0, s)
          == 1'000'000);
}

TEST_CASE("context ladder: the user override outranks even the API") {
    agentty::store::Settings s;
    declare("acme", "ctxtest-wide-3", 1'000'000);
    s.context_overrides[agentty::ui::context_override_key("acme", "ctxtest-wide-3")]
        = 512'000;

    // 用户配置了网关; 没有任何东西比它更胜过它。
    // 不是声明, 甚至连广告也没有。
    CHECK(agentty::ui::resolve_context_window("acme", "ctxtest-wide-3", 0, s)
          == 512'000);
    CHECK(agentty::ui::resolve_context_window("acme", "ctxtest-wide-3",
                                              /*广告=*/32'768, s) == 512'000);
}

TEST_CASE("context ladder: hosts disagreeing about a bare id poisons it") {
    // 同一赤色模型id被不同主机以不同大小真正提供——
    // 测量: qwen3 -coder-plus 将第一个
    // 作者的号码保存在一块的共享密钥上, 将会把一个主机
    // 的号码 流到另一个主机的身上, 因此分歧必须读作
    // "no declaration" 并进行推论。
    agentty::merge_catalog_context_window("ctxtest-shared-1", 256'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-shared-1") == 256'000);

    agentty::merge_catalog_context_window("ctxtest-shared-1", 1'000'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-shared-1") == 0);

    // 有毒是会话的永久内容: 后期协议的写法
    // 不会使两个关键来源重新产生相互矛盾。
    agentty::merge_catalog_context_window("ctxtest-shared-1", 256'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-shared-1") == 0);

    // A SCOPED 记录不受影响——这是范围界定的整个要点。
    declare("acme", "ctxtest-shared-1", 777'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-shared-1", "acme") == 777'000);
}

TEST_CASE("context ladder: a vendor-prefixed id meets its bare form") {
    // 空名空间由 TAIL 这么说吧 "elsewhere/model"
    // 财务报告和已审计财务报表 "model" 写着前
    // 缀的密钥意味着他们永远不会见面: 分歧永远无法察觉,
    // 读者们也无法发现 末尾 ——会错过每本前缀的写作。
    // 这种不对称存在于兄弟会中。 注册表 ;
    // 这个钉子,这个没有复制它。
    agentty::merge_catalog_context_window("vendorx/ctxtest-tail-1", 128'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-tail-1") == 128'000);

    agentty::merge_catalog_context_window("ctxtest-tail-1", 999'000);
    CHECK(agentty::catalog_context_window_for("ctxtest-tail-1") == 0);
}

TEST_CASE("context ladder: a non-declaration is never recorded as one") {
    // 0和负数是 "nothing was declared"
    // 而不是小窗口,把它们与真正的零声明是分不开的,而且会缩短
    // -circuit 下方的龙头。
    agentty::set_catalog_context_window("acme/ctxtest-zero-1", 0);
    CHECK(agentty::catalog_context_window_for("ctxtest-zero-1", "acme") == 0);
    agentty::merge_catalog_context_window("ctxtest-zero-2", -5);
    CHECK(agentty::catalog_context_window_for("ctxtest-zero-2") == 0);

    agentty::store::Settings s;
    CHECK(agentty::ui::resolve_context_window("acme", "ctxtest-zero-1", 0, s)
          == agentty::ui::kDefaultContextWindow);
}

TEST_CASE("context ladder: the picker column and the status bar agree") {
    // 两面面靠窗子解决 不同 路径,这是bug 帆
    // 3r的 PR 确定39人:
    //
    //   状态栏 - > ui::resolve_context_window(), i.e。 梯子( 梯子)
    //   选择面板 ctx - > (中文(简体) ). ModelInfo::context_window ,
    //                  烤在当 目录 已加载
    //
    // 如果烤肉不运行梯子,一个 models.dev 声明在用户要
    // 检查的地方是隐形的: 列上写着 "auto" 状态栏上写着1M
    // 他们描述的是同一个模型 他的设定
    // 选择器上下文列与状态栏由结构保证一致。
    // ——这是正确的要求,因此,此处是断言,
    // 而不是留待谁决定。 调用 网站记忆.
    //
    // bake_context_window () 建造:一种功能,
    // 供 目录 装载机,这给一排 相同的数量梯子会。
    agentty::store::Settings s;
    agentty::set_catalog_context_window("ctxprov/ctxtest-baked-1", 1'000'000);

    // 一个没有广告的网关。
    agentty::ModelInfo row;
    row.id = agentty::ModelId{"ctxtest-baked-1"};
    row.provider = "ctxprov";
    row.context_window = 0;

    agentty::ui::bake_context_window(row, "ctxprov", s);
    const int ladder = agentty::ui::resolve_context_window(
        "ctxprov", "ctxtest-baked-1", 0, s);

    CHECK(row.context_window == ladder);
    CHECK(row.context_window == 1'000'000);   // 否 "auto", 不为 200k

    // 来,来,来 ADVERTISED 窗口仍然获胜,两面都同意。
    agentty::ModelInfo adv;
    adv.id = agentty::ModelId{"ctxtest-baked-1"};
    adv.provider = "ctxprov";
    adv.context_window = 32'768;              // 网关已打开
    agentty::ui::bake_context_window(adv, "ctxprov", s);
    CHECK(adv.context_window == 32'768);
    CHECK(adv.context_window == agentty::ui::resolve_context_window(
              "ctxprov", "ctxtest-baked-1", 32'768, s));

    // 和每笔 -model 都比两面都高
    s.context_overrides[agentty::ui::context_override_key(
        "ctxprov", "ctxtest-baked-1")] = 512'000;
    agentty::ModelInfo ovr;
    ovr.id = agentty::ModelId{"ctxtest-baked-1"};
    ovr.provider = "ctxprov";
    ovr.context_window = 32'768;
    agentty::ui::bake_context_window(ovr, "ctxprov", s);
    CHECK(ovr.context_window == 512'000);
}

TEST_CASE("context ladder: the env floor catches what no catalog can know") {
    // 无法知晓的案件:一个私人网关服务于无人公布的身份证.
    // 发现覆盖了95%的运输供应商,但一个10.0.0.5:4000的
    // 模型被调用 `internal-v2` 无法通过构建实现,所以必须存在一个手动运行。
    // Zed 需要 max_tokens 输入 settings.json
    // for OpenAI-compatible endpoints; Claude Code has
    // CLAUDE_CODE_MAX_CONTEXT_TOKENS。这反映了后者的拼写。
    struct ScopedEnv {
        const char* k;
        ScopedEnv(const char* key, const char* v) : k(key) {
            if (v) ::setenv(k, v, 1); else ::unsetenv(k);
        }
        ~ScopedEnv() { ::unsetenv(k); }
    };
    agentty::store::Settings s;
    const char* kEnv = "AGENTTY_MAX_CONTEXT_TOKENS";
    namespace ui = agentty::ui;

    // 梯形取解(Model::Env::max_context_tokens)值.
    // 取消( 0): 保守的默认立方.
    CHECK(ui::resolve_context_window("priv", "ctxtest-env-1", 0, s, 0)
          == ui::kDefaultContextWindow);
    // 设定: 使用它, 因为没有比它更好的说话。
    CHECK(ui::resolve_context_window("priv", "ctxtest-env-1", 0, s, 1'000'000)
          == 1'000'000);
    // 最后 伦格,不是第一个,是 GLOBAL ,
    // 所以一个会议达到几个模型 否则会戳上一个数字 在所有模型上,
    // 任何真正知道这个模型的,都必须赢。
    CHECK(ui::resolve_context_window("priv", "ctxtest-env-1",
                                     /*广告=*/32'768, s, 1'000'000)
          == 32'768);
    agentty::set_catalog_context_window("priv/ctxtest-env-2", 262'144);
    CHECK(ui::resolve_context_window("priv", "ctxtest-env-2", 0, s, 1'000'000)
          == 262'144);
    CHECK(ui::resolve_context_window("anthropic", "claude-sonnet-4-5", 0, s,
                                     1'000'000)
          == 200'000);

    // 电话 解析器 垃圾不能变成一个小窗口: "1M"
    // 解析 和1个 -token 环境将逐个变化。
    {
        ScopedEnv e{kEnv, "1000000"};
        CHECK(ui::max_context_tokens_from_env() == 1'000'000);
    }
    {
        ScopedEnv e{kEnv, nullptr};
        CHECK(ui::max_context_tokens_from_env() == 0);
    }
    for (const char* bad : {"1M", "abc", "", "0", "-5", "12x", "1e6"}) {
        ScopedEnv e{kEnv, bad};
        CHECK(ui::max_context_tokens_from_env() == 0);
    }
}
