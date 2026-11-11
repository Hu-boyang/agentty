// 嵌入线 面板 排行模式 : build_form / config_from_form.
//
// 电话 面板 现在它只持有一个 form::Form ——导航,
// 编辑,滴滴和密钥映射都来自共享组件层,每个格莱弗属于 maya::Form。
// 在此测试的只是唯一真正嵌入的 -specific 知识:
// 对应的 后端需要行,然后该行会绕 -trip 返回到一个
// EmbedConfig 而不扰动矢量 -space 身份 身份 姓名
// 身份 身份证

#include <doctest/doctest.h>

#include "agentty/runtime/panel/rag.hpp"

namespace rs = agentty::rag_settings;
namespace eb = agentty::rag::embed;
namespace ff = agentty::form::field;

TEST_CASE("embed form: rows are derived from the backend") {
    eb::EmbedConfig c;
    c.backend = eb::Backend::Auto;
    auto f = rs::build_form(c, agentty::store::RagMode::On);
    // 自动不需要终点,但 面板 持有 一个 排读为断.
    // 它说明Auto会做什么, 与行锁定而不是隐藏,
    // 仍然提供探测器。
    CHECK(f.fields.size() > 1);
    // 排行0是主动的 -retrieval MODE - 电话 面板 是一个问题(
    // "how does retrieval behave here?" ),作为模式 + 嵌入器询问。
    CHECK(f.fields[0].id == rs::kFieldMode);
    CHECK(f.find(rs::kFieldBackend) != nullptr);
    CHECK(f.find(rs::kFieldTest) != nullptr);
    // 端点行在 Auto 下显示-only.
    REQUIRE(f.find(rs::kFieldHost) != nullptr);
    CHECK(f.find(rs::kFieldHost)->locked);

    c.backend = eb::Backend::OpenAI;
    f = rs::build_form(c, agentty::store::RagMode::On);
    CHECK(f.find(rs::kFieldHost)      != nullptr);
    CHECK(f.find(rs::kFieldPort)      != nullptr);
    CHECK(f.find(rs::kFieldModel)     != nullptr);
    CHECK(f.find(rs::kFieldApiKey)    != nullptr);
    CHECK(f.find(rs::kFieldTest)      != nullptr);
    CHECK(f.find(rs::kFieldModelPath) == nullptr);   // 以 -process 计
    // 在真实的后端下,端点IS可编辑.
    CHECK_FALSE(f.find(rs::kFieldHost)->locked);

    // 内 -process : 一个模型 FILE ,则无终点或
    // 凭据 排行——展示后端的用途会是一个谎言.
    c.backend = eb::Backend::Gguf;
    f = rs::build_form(c, agentty::store::RagMode::On);
    CHECK(f.find(rs::kFieldModelPath) != nullptr);
    CHECK(f.find(rs::kFieldHost)      == nullptr);
    CHECK(f.find(rs::kFieldPort)      == nullptr);
    CHECK(f.find(rs::kFieldApiKey)    == nullptr);
}

TEST_CASE("embed form: display-only rows never write back into the config") {
    // 在 Automa 下,端点行显示一个已变换的 "host:port" SUMMARY.
    // 如果 config_from_form 读取已锁的行,则该摘要串会
    // 降落在 `host` 中,并损坏下一个键盘上的配置。
    eb::EmbedConfig c;
    c.backend = eb::Backend::Auto;
    c.host    = "127.0.0.1";
    c.port    = 11434;

    const auto back = rs::config_from_form(c, rs::build_form(c, agentty::store::RagMode::On));
    CHECK(back.host == "127.0.0.1");
    CHECK(back.port == 11434);
    CHECK(eb::identity(back) == eb::identity(c));
}

TEST_CASE("embed form: the backend row is a dropdown with per-option hints") {
    // 付得起的答案是 "which of these can I actually run here?"
    // 而不离开 面板.
    eb::EmbedConfig c;
    const auto f = rs::build_form(c, agentty::store::RagMode::On);
    const auto* row = f.find(rs::kFieldBackend);
    REQUIRE(row != nullptr);
    REQUIRE(row->is_choice());
    const auto& ch = std::get<ff::Choice>(row->value);
    CHECK(ch.count() == eb::kBackendCount);
    CHECK(ch.hints.size() == static_cast<std::size_t>(eb::kBackendCount));
    CHECK(ch.id() == "auto");
}

TEST_CASE("embed form: the API key row is a Secret") {
    // 如果将这个键退到文本中, 则按下 渲染 的清晰度 —— 类型拆
    // 分的准确漏出值是存在的, 以防止。
    eb::EmbedConfig c;
    c.backend = eb::Backend::OpenAI;
    c.api_key = "sk-live-key";
    const auto f = rs::build_form(c, agentty::store::RagMode::On);
    const auto* row = f.find(rs::kFieldApiKey);
    REQUIRE(row != nullptr);
    CHECK(std::holds_alternative<ff::Secret>(row->value));
}

TEST_CASE("embed form: a model file is a Path, so a typo is visible") {
    eb::EmbedConfig c;
    c.backend    = eb::Backend::Gguf;
    c.model_path = "/models/nomic.gguf";
    const auto f = rs::build_form(c, agentty::store::RagMode::On);
    const auto* row = f.find(rs::kFieldModelPath);
    REQUIRE(row != nullptr);
    CHECK(std::holds_alternative<ff::Path>(row->value));
}

TEST_CASE("embed form: build_form / config_from_form round-trips") {
    eb::EmbedConfig c;
    c.backend = eb::Backend::OpenAI;
    c.model   = "bge-m3";
    c.host    = "gateway.internal";
    c.port    = 8443;
    c.tls     = true;
    c.path    = "/v1/embeddings";
    c.api_key = "sk-round-trip";

    const auto back = rs::config_from_form(c, rs::build_form(c, agentty::store::RagMode::On));
    CHECK(back.backend == c.backend);
    CHECK(back.model   == c.model);
    CHECK(back.host    == c.host);
    CHECK(back.port    == c.port);
    CHECK(back.tls     == c.tls);
    CHECK(back.path    == c.path);
    CHECK(back.api_key == c.api_key);
    // 往返不得扰乱矢量-space身份.
    CHECK(eb::identity(back) == eb::identity(c));
}

TEST_CASE("embed form: editing a row is reflected in the config") {
    eb::EmbedConfig c;
    c.backend = eb::Backend::OpenAI;
    c.host    = "old.internal";
    auto f = rs::build_form(c, agentty::store::RagMode::On);

    auto* row = f.find(rs::kFieldHost);
    REQUIRE(row != nullptr);
    agentty::form::clear(row->value);
    agentty::form::paste(row->value, "new.internal");

    const auto back = rs::config_from_form(c, f);
    CHECK(back.host == "new.internal");
    // 改变端点MUST改变身份:它可能在同一个模型
    // 名称下服务不同的权重.
    CHECK(eb::identity(back) != eb::identity(c));
}

TEST_CASE("embed form: selecting a backend by dropdown rebuilds the rows") {
    // 使 面板 感觉连贯: 选择 "Custom endpoint" ,
    // 并显示终点行。
    eb::EmbedConfig c;
    auto f = rs::build_form(c, agentty::store::RagMode::On);
    auto* row = f.find(rs::kFieldBackend);
    REQUIRE(row != nullptr);
    std::get<ff::Choice>(row->value).select_id("openai");

    const auto next = rs::config_from_form(c, f);
    CHECK(next.backend == eb::Backend::OpenAI);
    const auto rebuilt = rs::build_form(next, agentty::store::RagMode::On);
    CHECK(rebuilt.find(rs::kFieldHost)   != nullptr);
    CHECK(rebuilt.find(rs::kFieldApiKey) != nullptr);
}

TEST_CASE("embed form: the in-process backends need no daemon") {
    // 回答的属性 "I can't run ollama at work".
    for (auto b : {eb::Backend::Gguf, eb::Backend::Onnx}) {
        eb::EmbedConfig c;
        c.backend    = b;
        c.model_path = "/models/m.bin";
        CHECK(eb::is_in_process(c.backend));
        CHECK(eb::is_valid(c));
        const auto f = rs::build_form(c, agentty::store::RagMode::On);
        CHECK(f.find(rs::kFieldHost) == nullptr);
    }
}

TEST_CASE("embed form: the probe row is an Action, not a value") {
    // 它运行一些东西和报告; 它没有配置。 作为真正的替代,
    // 它让状态更新函数 分派 在一个类型上,而不是字符串
    // -matching ID。
    eb::EmbedConfig c;
    c.backend = eb::Backend::Ollama;
    const auto f = rs::build_form(c, agentty::store::RagMode::On);
    const auto* row = f.find(rs::kFieldTest);
    REQUIRE(row != nullptr);
    CHECK(row->is_action());
    CHECK_FALSE(row->editable());   // Enter 点火它; 它从未进入编辑模式
}
