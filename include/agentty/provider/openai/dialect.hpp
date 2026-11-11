#pragma once
// agentty/provider/openai/dialect.hpp-观察表。 SSOT。
//
// "OpenAI-compatible" 传输在野外生活中以各种方式拼写
// 为HERE，作为声明，而不是在其他地方。 传输层 通过这些镜头进行观察；
// 它不会在 调用 站点上 re-derive 字段名称。
//
// 原因 A TABLE 和 不 `if` BRANCHES：偏差
// 不是边缘情况，它们是没有规范的生态系统的正常状态（参见
// lens.hpp了解为什么没有要符合的规范）。它们分布在 3000
// -line 解码器上，是不可见的，无法单独测试，每个新的提供者
// 都会添加另一个必须有人找到的分支。作为数据，它们是一个列表，
// 您可以在一个屏幕中读取、重播捕获的灯具以及用一行进行扩展。
//
// 每个 ENTRY 下方 IS A CLAIM ABOUT AN OBSERVED PROVIDER，
// 不是猜测。当您添加一个时，请说出您在哪个端点上看到它。

#include "agentty/provider/openai/lens.hpp"

namespace agentty::provider::openai::dialect {

// ── 推理/链式-of-thought文本 ────────────────────────────────────
//
// Reasoning-capable模型数据流在领域PARALLEL至`content`中的思考。
// 其名称尚未达成一致：
//
//   reasoning_content DeepSeek 在聊天完成中介绍了它； vLLM紧随其后，
//                      因此大多数 self-hosted 和大多数 "compat"
//                      网关都使用它。 （已验证：DeepSeek，vLLM。）
//   推理 OpenAI 的响应 API、一些 OpenRouter 直通和 Yolo-Auto（
//                      计费代理后面的 vllm-0.29.x — 已于 2026 年
//                      9 月 23 日实时验证：数据流 裸露 `reasoning`）。
//
// ORDER MATTERS、和 SO 执行 `nonempty`。
// 某些 OpenRouter 直通会发送 空 `reasoning_content`
// 以及 POPULATED `reasoning`。获取仅存在的第一个
// 密钥会默默地丢弃这些端点上的所有推理标记 - 没有错误，只是一个似乎默默
// 思考的模型。 `nonempty` 是导致失败发生的原因。
inline const Lens<std::string>& reasoning_delta() {
    static const Lens<std::string> l =
          nonempty(key<std::string>("reasoning_content"))
        | nonempty(key<std::string>("reasoning"));
    return l;
}

// 读取字段reasoning_delta()的名称，因此回合可以以相同的名称发送回其推理。
// GitHub 自己的客户端正是这样做的（
// runtime.node:model_protocol/conversation_reasoning.rs
// 记录 "reasoning_content" 或 "reasoning" 的每条消息
// `reasoning_wire_field`，并且 chat-completions 编码器将文本写回其下方）。
// 与 reasoning_delta() 的顺序和 `nonempty` 规则相同，因此两者不能不一致。
[[nodiscard]] inline std::string_view reasoning_field(const nlohmann::json& delta) {
    for (const char* k : {"reasoning_content", "reasoning"})
        if (auto it = delta.find(k); it != delta.end() && it->is_string()
            && !it->get_ref<const std::string&>().empty())
            return k;
    return {};
}

// ── 传言内容 ────────────────────────────────────────────────────────
//
// 普通情况是普通字符串。 Mistral 的推理模型而是发送 ARRAY 的 带类型的
// 部分（在 Mistra-small-latest 上进行实时探测，
// reasoning_effort=高）：
//
//   {"content":[{"type":"thinking","thinking":[{"type":"text","text":"…"}]}，
//               {"type":"text","text":"…"}]}
//
// 在处理这个数组形式之前，它是默默地DROPPED：在长
// 时间的推理过程中没有推理，没有散文，也没有活跃心跳——
// 这使得看门狗陷入饥饿，看起来像是挂起。
inline const Lens<std::string>& content_delta() {
    static const Lens<std::string> l = key<std::string>("content");
    return l;
}

// 结构化内容-parts数组的一个元素，当它承载散文时。
inline const Lens<std::string>& content_part_text() {
    static const Lens<std::string> l =
        when("type", "text", key<std::string>("text"));
    return l;
}

// 结构化内容-parts数组的一个元素，当其承载思维时。
// 看到的两种形状：嵌套（`thinking`本身就是文本部分的
// 数组）和扁平（`text`直接在思考部分上）。
inline const Lens<std::vector<std::string>>& content_part_thinking_nested() {
    static const Lens<std::vector<std::string>> l =
        when("type", "thinking",
             collect("thinking", when("type", "text", key<std::string>("text"))));
    return l;
}
inline const Lens<std::string>& content_part_thinking_flat() {
    static const Lens<std::string> l =
        when("type", "thinking", key<std::string>("text"));
    return l;
}

// ── 工具-call 索引 ──────────────────────────────────────────────────────
//
// OpenAI在每个tool_calls增量上发送`index`，
// 因此可以跨块重新组装片段。许多克隆会忽略它并按顺序为每个元素
// 发送一个 调用。如果没有 `index`，ARRAY POSITION
// 是唯一可用的标识——这就是为什么它返回一个可选值而不是默认为
// 0：默默地将每个无索引的 调用 折叠到槽 0 上，
// 将两个不同的 调用' 参数连接成一个畸形的 blob。
inline const Lens<int>& tool_call_index() {
    static const Lens<int> l = key<int>("index");
    return l;
}

// ── 错误信封 ──────────────────────────────────────────────────────
//
// 对于同一事物看到的三种形状：
//
//   {"error":{"message":"…"}} OpenAI，以及复制它的所有内容
//                               （已验证：Yolo-Auto 为 401/403 返回
//                               完全相同的内容）。
//   {"error":"…"}一些网关将其扁平化为裸字符串{"message":"…",
//   "code":500} llama.cpp的裸形式，没有`error`包装器
//
// 数据流失败，且无法识别的包络面为"the model stopped for no reason"，
// 这是最糟糕的可能诊断。所有这三个都
// 属于一处。
inline const Lens<std::string>& error_message() {
    static const Lens<std::string> l =
          under("error", key<std::string>("message"))
        | key<std::string>("error")
        | key<std::string>("message");
    return l;
}

// ── /v1/models的模型元数据 ───────────────────────────────────────
//
// 不 IN OPENAI 的 SHAPE AT 全部。 OpenAI 自己的 /
// v1/models 仅返回 {id, object,created, owned_by} — 无窗口。
// 因此，每个想要发布网关的网关都发明了自己的放置位置，现在嵌套块（`top_provider`、
// `model_info`、`meta`）和 GGUF arch-prefixed 键（
// "qwen2.context_length"）之间有大约 15 种拼写。
//
// 该 完整 LADDER LIVES IN
// detail::advertised_window_tokens() (
// src/provider/openai/transport.cpp)并且
// 该函数是SSOT——它是按优先级顺序知道所有这些的一个地方，每个条目都归
// 因于其所观察到的供应商。不要在这里重复它：第二个部分列表比没有更糟糕，
// 因为这个列表错过的拼写会默默地回退到默认窗口，仪表误读，
// 并且 auto-compaction 在错误的点触发（即问题＃49）。
//
// 这个透镜特意是NARROW情况：探针在普通vLLM行上看到的两
// 个扁平键，在仅需要快速top-level读取的情况下使用。
// 任何需要真实答案调用 advertised_window_tokens()的事情。
//
// 在 Yolo-Auto (vllm-0.29.1，2026-09-23)上验证：两个密钥都存在，
// 131072，并且受到认可——125,040-token 提示词 在空闲密钥上返回 200。
inline const Lens<int>& model_context_window_flat() {
    static const Lens<int> l =
          key<int>("context_length")
        | key<int>("max_model_len");
    return l;
}

// ── 已声明的推理支持, 从/ 型号行 ───────────────────────
//
// 模型是否是 CAN 原因，正如服务器本身声明的那样——与上面的
// reasoning_delta() 不同，后者从 数据流 读取思考 TEXT。
// 这是决定agentty是否提供努力阶梯以及它是否发送
// `reasoning_effort`的事实。
//
// 这对于 LOCAL 服务器来说最为重要。 resolved_caps() 将能力解析为
//
//     per-model 覆盖 > env > 实时 目录 > id 推断
//
// 并且id-inference梯级是对托管命名约定（`:7b`标签、
// 已知系列前缀）的启发式。 llama.cpp 服务器报告 GGUF
// 文件名，LM Studio 服务器报告 `publisher/model` 密钥，
// 因此该梯级几乎没有用处 - 这正是为什么必须根据服务器
// 所说的内容填充 目录 梯级的原因。
//
// 观察到两种拼写：
//
//   "reasoning"：真正的 Mistral 的 /v1/models. 一个普通布尔值。
//   "reasoning":{"allowed_options":LM Studio的/api/v1/models
//                   ["off"，"on"]，(记录在其REST参考文献中)。
//                 "default":
//   "on"}"supports_reasoning":真LiteLLM。平放在行上，
//   and/or "model_info"：{"supports_reasoning"嵌
//                    套在model_info下 - 这是：true}代理配置
//                                        的`model_info:`
//                                        块的形状直接传递到/v1/models和/model/info.
//
//
// 对象形式是 LM Studio 默默地没有推理的原因：读者测试了
// is_boolean() 并跳过了其他任何内容，因此每个 LM
// Studio 模型都通过了 id 推断并按其文件名进行分类。
//
// LiteLLM的拼写是为什么一个LiteLLM用户做了所有正确的事情——
// model_info中的`supports_reasoning: true`，
// 代理转发的`reasoning_effort`——仍然没有思考，
// 而将agentty直接指向其背后的SAME llama.cpp却有效。
// 他们的配置没有问题，代理也没有问题：llama.cpp 是通过完全不同的门
// 检测到的（/props → chat_template_caps，
// 请参见下面的 template_reasoning_effort），并且这个镜头（网
// 关行穿过的唯一门）从未被告知 `supports_reasoning` 这个词。
// 这里添加网关是指添加其拼写；
// 没有回退可以猜出。
//
// 对象IS的存在声明——`allowed_options`从不需要解析来回答yes/no问题。
// （梯子应该是什么是一个单独的问题；请参阅下面的
// declared_reasoning_default。）
//
// ABSENCE IS 不 A NO。丢失的键返回 nullopt，
// 并且调用者必须保留 目录 梯级 空，而不是记录 false。 "Missing"
// 涵盖了 "cannot reason" 和 "this entry does not say"，
// 这些不是同一个声明 - 写入 false 会覆盖其下方的 id-inference 横档，
// 固定 e.g。 DeepSeek-R1 在仅省略该字段的清单上
// 蒸馏为 no-reasoning。沉默不是证据。
inline const Lens<bool>& declared_reasoning() {
    static const Lens<bool> l =
          key<bool>("reasoning")                       // 米斯特拉尔：普通布尔值
        | truthy_object("reasoning")                   // LM工作室：对象
        | key<bool>("supports_reasoning")              // LiteLLM: flat
        | under("model_info",                          // LiteLLM: nested
                key<bool>("supports_reasoning"));
    return l;
}

// CAN原因的机型是否默认开启。
//
// LM Studio的物体带有`"default": "on" | "off"`。
// 默认为 "off" 的模型仍然支持推理 - 它只需要询问 -
// 因此它故意与 declared_reasoning() 分开，
// 而不是折叠到其中。
//
// 不 catalog::reasons_by_default，尽管名字读起来多么接近。
// 该问题询问模型是否开始发出 THOUGHTS 且没有打开 <think>
// 标签（Magistral，DeepSeek-R1）。它决定如何 PARSE
// 和 数据流，并且是关于模型如何训练的事实，在每个主机上都是相同的。
// THIS询问LM Studio以哪种方式翻转开关——
// 关于一台服务器的配置的事实。将其输入到默认值开启的
// 推理模型中，每个 数据流 都会出现错误 -parse：
// 领先的散文将被视为隐式推理并从答案中消失。
//
// 无线，如declared_tool_use。诚实消费者是 per
// -model 初始努力层，agentty 的努力是一个全局用户选择，
// 而不是 per-model 设置。此处声明是因为它可以免费
// 读取我们已经获取的响应。
inline const Lens<std::string>& declared_reasoning_default() {
    static const Lens<std::string> l =
        under("reasoning", key<std::string>("default"));
    return l;
}

// ── 已声明的工具支持, 从/ models 列 ────────────────────────────
//
// 再次是LM Studio的能力对象(`trained_for_tool_use`)。
// 命名如下：该模型是用于工具使用的 TRAINED，
// 这比 "the server will accept a tools array" 更强——
// llama.cpp 会接受任何模型的模型，并让它在生成时失败。
//
// 不 WIRED至ModelInfo::supports_tools，
// 并且与declared_reasoning的不对称是故意的。
// 此处保留错误的工具将是根据错误的声明行事：LM Studio
// 正在描述模型的 TRAINING，而服务器以任何一种方式接受工具数组。
// 默默地从不接收工具的模型与收到工具但没有帮助的模型没有什么区别——
// 确切的失败 ModelInfo::supports_tools 文档，
// 以及 tri-state SENDS 未知的原因。
//
// 它也不供给阶梯帽，这是另一个看似合理的消费者：
// 该上限已经对本地行上的每个模型触发（is_local ⇒ 上限，参见 cmd_factory
// 的 weak_step_cap），因此 per-model "badly trained for tools"
// 信号不能使其触发得更猛烈。已检查；它什么也改变不了。
//
// 因此它被读取并被故意丢弃。它保留在表中是因为它是
// 该表所拥有的形状中的真实字段 - 该表的要点是在
// 一个位置说明每个拼写 - 并且因为下一个找到
// 它的人应该获得该段落，而不是将其连接到保留中。
inline const Lens<bool>& declared_tool_use() {
    static const Lens<bool> l = key<bool>("trained_for_tool_use");
    return l;
}

// ── 宣布的能力,自llama.cpp/道具 ───────────────────────────
//
// llama.cpp在/v1/models行上没有能力对象，因此上面的两
// 个镜头从不在那里闪光。但它确实回答了/props——agentty已经
// 为运行时上下文窗口获取了——并且该响应携带
// `chat_template_caps`。
//
// 这些上限不是某人带类型的中的元数据。llama.cpp RUNS
// 模型自己的jinja聊天模板针对加载时间的探测输入，并比较渲染的
// 输出以查看模板实际反应的内容（参见common/jinja/caps.cpp、
// caps_get)。所以这是一个精确的测量
// 该服务器实际应用于请求的模板，比其他信息更能说明
// 比任何文件名或third-party 目录，以及当用户交换GGUF时它自身
// -corrects。
//
// 键名称由同一文件中的 caps::to_map() 固定：
//
//   supports_tools模板渲染工具阵列supports_tool_calls ...
//   和助手tool_calls回到supports_parallel_tool_calls
//   supports_system_role
//   supports_string_content
//   / supports_typed_content
//   supports_object_arguments
//   supports_preserve_reasoning不断地轮流思考，而不仅仅是最后的
//   supports_reasoning_effort模板荣誉reasoning_effort kwarg
//
// 只有最后一位回答"should agentty show an effort ladder"。
// 模板忽略 reasoning_effort 的服务器会很高兴地接受
// 该字段并将其扔在地板上，这是最糟糕的结果：选择面板 提供了一个没有
// 连接任何东西的拨号盘。
//
// supports_preserve_reasoning是故意读取的NOT。
// 它说模板可以保留历史中较早回合的思维，而不仅仅是最后一个 - 如果
// agentty 重播此 传输 上的思维，这将很重要，现在确实如此：每个
// 助理消息的推理都会返回到它到达的字段 (
// Message::reasoning_wire_field) 下。 标志 在这里仍然
// 没有决定任何事情。
inline const Lens<bool>& template_reasoning_effort() {
    static const Lens<bool> l =
        under("chat_template_caps", key<bool>("supports_reasoning_effort"));
    return l;
}

// 模板是否可以渲染工具调用。
//
// 读取supports_tool_calls，而不是supports_tools：
// 渲染是工具阵列但不能渲染助手的tool_calls回到历史中的模板在第二
// 回合中断，这看起来像模型"forgetting"它所调用的任何东西。
// 在 llama.cpp 的 caps 结构中，两者都默认为 true，
// 因此此处的 false 是明显使探测失败的模板。
inline const Lens<bool>& template_tool_calls() {
    static const Lens<bool> l =
        under("chat_template_caps", key<bool>("supports_tool_calls"));
    return l;
}

} // namespace agentty::provider::openai::dialect
