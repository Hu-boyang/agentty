// OAuth登录路由——每个供应商必须打开ITSXXOWN标志-in面板.
//
// 报告: "all oauth flow open openai panel" 选择
// GitHub 代理驾驶员(或Kimi),从供应商 选择面板 发射 ChatGPT/Codex
// 浏览器流,所以用户被要求签名到 OpenAI 认证 GitHub.
//
// 根因: 2个 标志 读起来像同义词,而不是。
//
//   oauth_native：通过登录认证，并使用自身长期保持的传输连接。
//                   TRUE 用于聊天,副驾驶 和 kimi;这恰恰是怎样的
//                   credentials.cpp 读它。
//   device_login  — "uses the shared device-code launcher" 副驾驶,奇米
//
// login.cpp和providers.cpp都测试了`oauth_native`
// XXFIRST,并将一个命中点当作"this is the Codex flow"
// ——这只对一个不是device_login的供应商来说是真实的. 注册表的评论
// 甚至声称"today only ChatGPT sets this",
// 在加入Copilot和Kimi时,它就不再是真的了,调用两个站点都信任该评论在行上.
//
// 问题在于 ProviderDescriptor::codex_login (),因此,
// 标志 活在排行中而不是重新开始 -derived (和错) -ordered ),
// 由每个呼叫者进行.

#include "agtest.hpp"

#include "agentty/provider/registry.hpp"

#include <string>
#include <string_view>

TEST_CASE("login routing: only ChatGPT uses the Codex flow") {
    namespace pr = agentty::provider;

    // 三个OAuth供应商, 以及每个供应商必须做什么。
    const auto* chatgpt = pr::preset_for("chatgpt");
    const auto* copilot = pr::preset_for("copilot");
    const auto* kimi    = pr::preset_for("kimi");
    REQUIRE(chatgpt != nullptr);
    REQUIRE(copilot != nullptr);
    REQUIRE(kimi    != nullptr);

    // 这三项都是oauth_native——这是使
    // 天真测试错误的事实,因此明确断言而不是暗示。
    CHECK(chatgpt->oauth_native);
    CHECK(copilot->oauth_native);
    CHECK(kimi->oauth_native);

    // ...但只说 ChatGPT 使用铃声 Codex 流动。
    CHECK(chatgpt->codex_login());
    CHECK(!copilot->codex_login());
    CHECK(!kimi->codex_login());

    // 设备-flow供应商取用共享发射装置。
    CHECK(!chatgpt->device_login);
    CHECK(copilot->device_login);
    CHECK(kimi->device_login);
}

TEST_CASE("login routing: the two flags cannot both claim a provider") {
    // 电话 不变量 使错误无法代表, 请检查 整个
    // 注册表 而不是上面的三行——后来添加的第四个
    // OAuth提供者免费获得这个.
    //
    // codex_login (a) 与 device_login 建筑
    // 是相互排斥的, 而这些针头是每个奥奥特人 -native 行解决
    // 了完全一个发射装置,所以没有一个可以掉进错误的面板(或者没有).
    namespace pr = agentty::provider;

    int native = 0, codex = 0;
    for (const auto& p : pr::providers()) {
        if (!p.oauth_native) continue;
        ++native;
        const bool takes_codex  = p.codex_login();
        const bool takes_device = p.device_login;
        if (takes_codex) ++codex;
        // 完全是一个发射器, 从来没有两者,也没有。
        CHECK(takes_codex != takes_device);
    }
    CHECK(native >= 3);   // 聊天,副驾驶,至少基米
    // 只有一个供应商拥有这个 Codex 如果有第二秒,
    // 流动就不再是 "the ChatGPT one" 这条路线需要
    // 重新思考 而不是另一排
    CHECK(codex == 1);
}
