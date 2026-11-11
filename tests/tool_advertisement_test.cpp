// Tool 广告绝不应默不作声地放弃。
//
// 已报告: 与“ 辅助驾驶” 模型有关的工具失败。 决定工具是否启动的门
// 传输 谓 一个 线条在 cmd_factory:
//
//     if (mi.id.value == model_id) model_supports_tools = mi.supports_tools;
//     ...
//     tools_disabled_by_capability = has_value() && !value()
//
// 两种方式都是错误的,两者都沉默不语——转弯,
// 模型根本看不到工具:
//
//   1. supports_tools 模特儿假的,你还敢说? 执行 辅助工具。
//      目录 读取盖 [ "supports"]["tool_calls"
//      并且一行漏掉钥匙 或者用别的名字报告 却在这里被当成假的
//
//   2. ID不匹配,查找是精确的字符串比较
//      m.d.available_models ,所以一个模型 目录 不列出(
//      代理驾驶员自动重写 sattle 服务器) -side ;提供者可能归还
//      一个大小写或后缀不同的弹片) 找不到任何东西——这是无名的, i.e.
//      "unknown", i.e 工具 ARE 这是安全的方向,
//      这个测试针 它保持安全的方向。
//
// 电话 不变量 : 工具被扣 仅 明确、积极宣布不参加 -support。
// 未知的必须意味着 "send them".

#include "agtest.hpp"

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/provider/registry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace {

// 电话 一个 上游数 调用 现时使用( 地址) ( agentty::tools_allowed).
[[nodiscard]] bool withholds_tools(std::optional<bool> supports) {
    return !agentty::tools_allowed(supports);
}

// 精确的检索cmd_factory用来寻找活动模型的能力.
[[nodiscard]] std::optional<bool>
capability_for(const std::vector<agentty::ModelInfo>& models,
               const std::string& model_id) {
    for (const auto& mi : models)
        if (mi.id.value == model_id) return mi.supports_tools;
    return std::nullopt;
}

}  // 命名空间结束。

TEST_CASE("tools: the member and free predicates agree") {
    // 两种拼写,一种规则 如果这些有分歧的话,一个 调用
    // 拥有整个网站的 ModelInfo 并且只有可选性的
    // 人对同一个模型有不同意见——这就是共享的上游存在来防止
    // 的漂移.
    for (std::optional<bool> v : {std::optional<bool>{},
                                  std::optional<bool>{true},
                                  std::optional<bool>{false}}) {
        agentty::ModelInfo mi;
        mi.supports_tools = v;
        CHECK(mi.tools_allowed() == agentty::tools_allowed(v));
    }
}

TEST_CASE("tools: unknown capability means SEND, never withhold") {
    // 失败 -safe。在“定义”中缺少一个模型。
    // 目录 服务器重置的枪声 完全没有能力
    // 报告 每一个都决心放弃 绝对不能放弃
    CHECK(!withholds_tools(std::nullopt));
    CHECK(!withholds_tools(true));
    CHECK(withholds_tools(false));   // 编号 仅 预扣案件

    // 模型的ID,没有在 目录 未知,不是不支持。
    std::vector<agentty::ModelInfo> catalog = agentty::catalog::bundled("copilot");
    REQUIRE(!catalog.empty());
    CHECK(!withholds_tools(capability_for(catalog, "some-model-we-never-listed")));
    // 副驾驶自动重写 sult 服务器 -side ,
    // 所以身份识别码 传输 和任何与所有 目录 列表中。不能禁用工具。
    CHECK(!withholds_tools(capability_for(catalog, "copilot-auto")));
}

TEST_CASE("tools: no bundled model claims to lack tool support") {
    // 每个供应商 agentty 船装一捆 目录 因为是一个
    // 工具 -calling 提供方——这就是为什么它出现在
    // 选择面板 组合行 supports_tools 假的会默默地
    // 剥离工具 给所有用户 在任何生命之前 目录 到达,
    // 这正是这个文件 所要防止的失败类别。
    for (const auto& p : agentty::provider::providers()) {
        const auto models = agentty::catalog::bundled(p.id);
        for (const auto& mi : models) {
            const bool bad = withholds_tools(mi.supports_tools);
            if (bad) {
                std::printf("bundled %s/%s declares supports_tools=false\n",
                            std::string{p.id}.c_str(), mi.id.value.c_str());
            }
            CHECK(!bad);
        }
    }
}

TEST_CASE("tools: every OAuth provider's default model can call tools") {
    // 签了字后第一个转弯 -in 运行 default_model ()
    // 如果该模型扣留工具,代理人在抵达时是惰性的——它可以说话,
    // 但不能读取文件——该文件读作: "tools are broken with this provider"
    // 而不是能力不匹配
    for (const auto& p : agentty::provider::providers()) {
        if (!p.token_in_transport) continue;      // OAuth 供应商
        const auto models = agentty::catalog::bundled(p.id);
        if (models.empty()) continue;             // 目录 现场直播 -only
        const std::string def{p.default_model};
        if (def.empty()) continue;
        CHECK(!withholds_tools(capability_for(models, def)));
    }
}
