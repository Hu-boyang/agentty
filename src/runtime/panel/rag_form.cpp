// rag_settings_form.cpp ——嵌入式 面板 排队模特儿
//
// build_form / config_from_form 是一个回合 -trip 双:
// build_form 项目 a EmbedConfig 进入窗体行,
// config_from_form 把它们读回来 保持这里(和纯洁)意味着 状态更新函数 永远
// 不要手 -maps 配置字段的行索引,对称是属性 -testable 无 终端.
//
// 行为 派生 从后端,所以窗体只显示可以影响所选后端的字段
// ——a API-key 排在一排内 -process
// 模型会是一个谎言 关于什么的配置。
//
// 其他一切——导航,编辑,滴滴,密钥映射,
// 每个格言——都来自共享的表层和maya::Form.
// 此文件只持有真正关于嵌入的知识.

#include "agentty/runtime/panel/rag.hpp"

#include <algorithm>

namespace agentty::rag_settings {

namespace eb = agentty::rag::embed;

namespace {

// text_of 来源于表层 SSOT 存取器( 4) form.hpp). bool_of
// 因为它携带一个 FALLBACK — "row absent ⇒ keep the config's current value"
// ——中立人 -default form::toggle_of 无法
// 表达( 数位于此处) appear/disappear )),后端选择.
using form::text_of;

[[nodiscard]] bool bool_of(const form::Form& f, const char* id, bool fallback) {
    const auto* row = f.find(id);
    if (!row) return fallback;
    const auto* t = std::get_if<form::field::Toggle>(&row->value);
    return t ? t->on : fallback;
}

// 两者按表格顺序排列,与组别 头文件
// 不断穿越边界。
//
// 设置 -owned 排行是:  ⁇ SMART MODE 路线政策,
// 以及 渲染 智能模式中 面板 (^S → ^A ) 在它们所
// 管理的开关和插槽旁边——不是这里。在错误的处显示一个旋钮。
// 面板 只能偶然找到,这与把它留在一个阴道中一样。 面板 是检索的,
// 所以它走检索的行。
void add_all_registry_rows(form::Builder& b, const store::Settings& settings,
                           const agentty::settings::registry::EnvSnapshot& env,
                           bool advanced) {
    namespace reg = agentty::settings::registry;
    auto last_group = reg::Group::Sources;
    bool first = true;
    reg::add_rows(b, settings.rag, env, reg::Owner::Rag, advanced, first, last_group);
}

} // 命名空间结束。

form::Form build_form(const eb::EmbedConfig& c, store::RagMode mode,
                      const store::Settings& settings,
                      const agentty::settings::registry::EnvSnapshot& env,
                      bool advanced) {
    form::Builder b{" Retrieval "};
    b.subtitle(eb::describe(c));

    // 第 0 行 : WHEN 检索运行。一个真 枚举 (
    // 三个名为政策),所以降级是正确的控制。它过去是一个
    // 完整的单独的 覆盖面板 其唯一内容是这三行——这给
    // 嵌入器设置没有明显的生活地点,使得它们无法发现.
    {
        std::vector<std::string> labels, ids, hints;
        for (store::RagMode mm : kModes) {
            labels.emplace_back(store::to_string(mm));
            ids.emplace_back(store::to_string(mm));
            hints.emplace_back(store::describe(mm));
        }
        b.choice(kFieldMode, "Proactive", std::move(labels), std::move(ids),
                 store::to_string(mode),
                 "when to retrieve context before a turn", std::move(hints));
    }

    // 第1行: 将所有文件都写在“A”字后面。 内容 计算矢量。以下移方式显示,
    // 以每 -option 模糊,所以, "which of these can I actually run here?"
    // 可在不离开 面板.
    {
        std::vector<std::string> labels, ids, hints;
        for (eb::Backend bk : eb::kBackends) {
            labels.emplace_back(eb::label_of(bk));
            ids.emplace_back(eb::id_of(bk));
            hints.emplace_back(eb::blurb_of(bk));
        }
        b.choice(kFieldBackend, "Embeddings", std::move(labels), std::move(ids),
                 eb::id_of(c.backend),
                 "what computes the embedding vectors", std::move(hints));
    }

    // 自动和关闭不需要终点和模型文件——但需要 面板 单行读作破损(
    // 为何此处只剩 Backend 这一项？
    // 自动不是 "nothing configured" ,
    // 这是一个 POLICY ,所以说它会做什么,并显示仍然适用的一个旋钮。
    // 行是锁着的而不是隐藏的:一个可见的,解释过的行比空的 面板.
    if (c.backend == eb::Backend::Auto) {
        b.text(kFieldModel, "Model", c.model,
               "used when Auto finds a local Ollama");
        b.text(kFieldHost, "Endpoint", c.host + ":" + std::to_string(c.port));
        b.lock("auto-detected");
        b.action(kFieldTest, "Test connection",
                 "check whether Auto can reach a local Ollama right now");
        add_all_registry_rows(b, settings, env, advanced);
        return b.build();
    }
    if (c.backend == eb::Backend::Disabled) {
        b.text(kFieldModel, "Retrieval", "keyword only (BM25)");
        b.lock("embeddings off");
        add_all_registry_rows(b, settings, env, advanced);
        return b.build();
    }

    if (eb::needs_model_name(c.backend))
        b.text(kFieldModel, "Model", c.model,
               "embedding model name as the endpoint knows it");

    if (eb::needs_endpoint(c.backend)) {
        b.text(kFieldHost, "Host", c.host, "hostname or IP (no scheme)");
        b.number(kFieldPort, "Port", static_cast<std::int64_t>(c.port), 1, 65535);
    }

    if (c.backend == eb::Backend::OpenAI) {
        b.toggle(kFieldTls, "HTTPS", c.tls, "use TLS for the endpoint");
        b.text(kFieldPath, "Path", c.path, "request path; blank = /v1/embeddings");
        b.secret(kFieldApiKey, "API key", c.api_key,
                 "stored in the OS keystore, never in settings.json");
    }

    if (c.backend == eb::Backend::LlamaCpp)
        b.text(kFieldPath, "Path", c.path, "request path; blank = /embedding");

    if (eb::needs_model_path(c.backend)) {
        // 一个路径字段,而不是文本:您输入时的部件徽章为exists/missing,
        // 所以一个打字的d模型路径可以立即看到,而不是在后来作为
        // "backend could not be constructed"不透明的表面。
        b.path(kFieldModelPath, "Model file", c.model_path,
               c.backend == eb::Backend::Gguf
                   ? "absolute path to a .gguf embedding model"
                   : "absolute path to a .onnx model");
        if (c.backend == eb::Backend::Onnx)
            b.path(kFieldTokenizer, "Tokenizer", c.tokenizer_path,
                   "tokenizer.json path (blank = alongside the model)");
    }

    // 探测器行。动作,而不是值:它也是 仅 方式( 方式 ) `dim`
    // 被设定,因为用户 -supplied 维度默默下降 rag-cpp's
    // HNSW 错配
    b.action(kFieldTest, "Test connection",
             "embed a probe string and measure the vector");

    add_all_registry_rows(b, settings, env, advanced);
    return b.build();
}

eb::EmbedConfig config_from_form(const eb::EmbedConfig& base, const form::Form& f) {
    eb::EmbedConfig c = base;

    if (const auto* row = f.find(kFieldBackend))
        if (const auto* ch = std::get_if<form::field::Choice>(&row->value))
            c.backend = eb::backend_from_id(ch->id());

    // 锁定的行是 DISPLAY-ONLY : 在“自动”下,
    // 端点行显示已渲染 "host:port" 概要和模型行显示“自动”将使用什么。
    // 读回这些将将摘要字符串写入 `host` 并堕落的神情。
    // `editable()` 同一上游的词组 状态更新函数 和关键路由器的使用,
    // 所以 "can the user change this" 有个主人
    const auto readable = [&](const char* id) -> const form::Field* {
        const auto* row = f.find(id);
        return (row && row->editable()) ? row : nullptr;
    };

    if (readable(kFieldModel))     c.model          = text_of(f, kFieldModel);
    if (readable(kFieldHost))      c.host           = text_of(f, kFieldHost);
    if (readable(kFieldPath))      c.path           = text_of(f, kFieldPath);
    if (readable(kFieldModelPath)) c.model_path     = text_of(f, kFieldModelPath);
    if (readable(kFieldTokenizer)) c.tokenizer_path = text_of(f, kFieldTokenizer);
    if (readable(kFieldApiKey))    c.api_key        = text_of(f, kFieldApiKey);
    if (readable(kFieldTls))       c.tls            = bool_of(f, kFieldTls, c.tls);

    if (const auto* row = readable(kFieldPort))
        if (const auto* n = std::get_if<form::field::Number>(&row->value))
            c.port = static_cast<std::uint16_t>(std::clamp<std::int64_t>(form::number_value(*n), 1, 65535));

    return c;
}

store::RagMode mode_from_form(const form::Form& f, store::RagMode fallback) {
    const auto* row = f.find(kFieldMode);
    if (!row) return fallback;
    const auto* ch = std::get_if<form::field::Choice>(&row->value);
    if (!ch) return fallback;
    const auto id = ch->id();
    for (store::RagMode mm : kModes)
        if (store::to_string(mm) == id) return mm;
    return fallback;
}

void apply_form_to_settings(const form::Form& f, store::Settings& settings) {
    // 只有拉格号 -owned 行; 智能模式行属于智能 面板.
    settings::registry::read_rows(settings.rag, f, settings::registry::Owner::Rag);
}

} // namespace agentty::rag_settings
