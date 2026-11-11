#pragma once
// agentty/provider/openai/lens.hpp-观察没有规范的方言。
//
// ── 原因 此 IS 不 acp::Codec<T> ────────────────────────────────────────
//
// acp-cpp和mcp-cpp用`Codec<T>`（部分ISOMORPHISM）对他们的传输进行建模
//
//        T⇄JSON解码∘编码=id_T
//      编码解码
//
// 并且该定律是有意义的，因为 ACP 和 MCP 是 PROTOCOLS：
// 已发布的规范说明了 字节 是什么，因此未知字段是 对端 错误，
// 而 `throw CodecError` 是正确的响应。
//
// "OpenAI-compatible"不是协议。它是一种 API 形状，
// 因采用而获胜，然后被克隆。没有 RFC，没有 注册表，没有版本协商，
// 也没有拥有它的 正文。 de-facto 的两个事实来源 - OpenAI
// 自己的 openapi 文档和 vLLM 的服务器 - DISAGREE，
// 以及生态系统遵循更接近的那个。具体来说：
//
//   ·OpenAI的响应API 数据流认为是`reasoning`。
//   ·DeepSeek在聊天完成上引入了`reasoning_content`。
//   · vLLM 遵循 DeepSeek。某些网关发出 两者，其中之一是空的。
//   ·/v1/models中的`context_length`是vLLM扩展，其不在
//     OpenAI的形状根本不存在。
//
// 这些都不是违规，因为没有什么可违规的。
// 所以代数必须在两个方面有所不同：
//
//   1. THERE IS NO ENCODE SIDE。 agentty 消耗提供者响应；它从来没有
//      生产一个。编解码器需要逆元和两边都有规格；这里两者都不存在，
//      所以 round-trip 定律甚至无法被陈述。
//
//   2. FAILURE IS ORDINARY，不 EXCEPTIONAL。缺少密钥意味着“此服务器
//      拼写不同", not "此服务器已损坏”。解码必须DEGRADE，
//      绝不抛出。
//
// ── 内容 此 IS INSTEAD ─────────────────────────────────────────────────
//
// `Lens<T>`见证了JSON值的部分OBSERVATION
//
//        观察：JSON⇀T
//
// 并且法律是关于选择而不是round-tripping。 `Lens` 是
// Maybe 的替代方案； `|` 是其选择运算符：
//
//        (a | b)成功 ⟺ a 成功 ∨ b
//        成功 (a | b) | c = a | (b | c)
//        (结合) a | 失败 = a = 失败 | a (失败为恒等式)
//
// 这正是
// "this field has three spellings in the wild, try them in order of specificity"的结构。
// 偏差不再是隐藏在 3000-line 解码器中的 `if` 分支，
// 而是变成 DATA——这是一个您可以读取、针对捕获的装置进行测试并用一行进行扩展的声明。
//
// ── SSOT ─────────────────────────────────────────────────────────────────
//
// dialect.hpp保存根据该代数构建的观测表。
// 它们是 传输 上的字段拼写方式的唯一事实来源； 传输层 观察
// THROUGH 他们，而不是其他地方。

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentty::provider::openai {

using Json = nlohmann::json;

// ── 镜头 <T> ──────────────────────────────────────────────────────────────
// 删除的部分观察结果。总的意思是它永远不会抛出：
// 它无法识别的形状会产生 nullopt，调用者将其视为
// "not present in this dialect"，而不是错误。
template <class T>
struct Lens {
    using Observe = std::function<std::optional<T>(const Json&)>;
    Observe observe;

    [[nodiscard]] std::optional<T> operator()(const Json& j) const {
        return observe ? observe(j) : std::nullopt;
    }
};

// ── 失败: 镜头 <T> ───────────────────────────────────────────────────────
// `|`的身份。从来没有观察到任何事情。
template <class T>
[[nodiscard]] inline Lens<T> fail() {
    return Lens<T>{[](const Json&) { return std::optional<T>{}; }};
}

// ── a | b:选择 ───────────────────────────────────────────────────────
// 尝试`a`；如果没有观察到任何结果，请尝试 `b`。
// 关联，以 `fail` 作为标识 — 使偏差表成为单个可读
// 表达式的替代实例。
template <class T>
[[nodiscard]] inline Lens<T> operator|(Lens<T> a, Lens<T> b) {
    return Lens<T>{[a = std::move(a), b = std::move(b)](const Json& j)
                       -> std::optional<T> {
        if (auto v = a(j)) return v;
        return b(j);
    }};
}

// ── 键 : 镜头 <T> ────────────────────────────────────────────────────────
// 观察一个对象成员，当它存在右侧JSON类型的AND时。
// 错误的-typed成员产生nullopt而不是抛出：与填充的
// `reasoning_content`一起发送`reasoning: null`
// 的网关必须落入下一个替代方案，而不是中止数据流。
template <class T>
[[nodiscard]] inline Lens<T> key(std::string name) {
    return Lens<T>{[name = std::move(name)](const Json& j) -> std::optional<T> {
        if (!j.is_object()) return std::nullopt;
        const auto it = j.find(name);
        if (it == j.end()) return std::nullopt;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!it->is_string()) return std::nullopt;
            return it->template get<std::string>();
        } else if constexpr (std::is_same_v<T, bool>) {
            // BEFORE积分臂：std::is_integral_v<bool>
            // 是TRUE，因此布尔值将用is_number_integer
            // ()进行测试并被拒绝——`"reasoning": true`
            // 读作"absent"，这与从未声明它的提供者没有区别。
            // 严格：只有真正的JSON布尔值，而不是0/1或"true"。
            if (!it->is_boolean()) return std::nullopt;
            return it->template get<bool>();
        } else if constexpr (std::is_integral_v<T>) {
            if (!it->is_number_integer()) return std::nullopt;
            return it->template get<T>();
        } else {
            if (it->is_null()) return std::nullopt;
            return it->template get<T>();
        }
    }};
}

// ── 非空: 镜头 <string> + 镜头 <string> ───────────────────────────────
// 拒绝空观察，因此它落入下一个选择。
//
// 此 一个 IS LOAD-BEARING，不太好。某些 OpenRouter
// 直通会发送 空 `reasoning_content` 以及已填充的
// `reasoning`。获取仅 EXISTS 的第一个密钥，
// 默默地丢弃这些端点上的所有推理标记。该错误是看不见的：没有错误，没有警告，
// 只是一个似乎在沉默中思考的模型。
[[nodiscard]] inline Lens<std::string> nonempty(Lens<std::string> inner) {
    return Lens<std::string>{
        [inner = std::move(inner)](const Json& j) -> std::optional<std::string> {
            auto v = inner(j);
            if (v && !v->empty()) return v;
            return std::nullopt;
        }};
}

// ── truthy_object : 镜头 <bool> ───────────────────────────────────────────
// 将 "this capability object is PRESENT" 观察为布尔值 true。
//
// 一些服务器将能力声明为裸布尔值，其他服务器将能力声明
// 为描述如何使用它的对象，并且对象的存在本身就是声明：
//
//     "reasoning":真正的米斯特拉尔"reasoning":{
//     "allowed_options":["off","on"]，
//                   LM工作室"default":"on"}
//
// 无法推理的模型完全省略了密钥，因此没有什么可以读取INSIDE对象来回答yes/no
// ——存在就是答案。这与 `|` 下的 `key<bool>`
// 组合在一起，因此表格将两种拼写表述为一个表达式，
// 而不是在 调用 站点的 is_boolean() 上分支。
//
// 空物体=存在但什么也没说；被视为声明，
// 因为表示 "no" 的服务器将省略该密钥。
[[nodiscard]] inline Lens<bool> truthy_object(std::string name) {
    return Lens<bool>{[name = std::move(name)](const Json& j) -> std::optional<bool> {
        if (!j.is_object()) return std::nullopt;
        const auto it = j.find(name);
        if (it == j.end() || !it->is_object()) return std::nullopt;
        return true;
    }};
}

// ── 下方: (键, 连) <T> ) + 连 <T> ─────────────────────────────────────
// 下降到嵌套对象中并观察那里。让表格显示
// `under("delta", reasoning)`，而呼叫者无需打开信封。
template <class T>
[[nodiscard]] inline Lens<T> under(std::string name, Lens<T> inner) {
    return Lens<T>{[name = std::move(name), inner = std::move(inner)](
                       const Json& j) -> std::optional<T> {
        if (!j.is_object()) return std::nullopt;
        const auto it = j.find(name);
        if (it == j.end() || !it->is_object()) return std::nullopt;
        return inner(*it);
    }};
}

// ── 地图 : (Lens) <A> ) 镜头 <B> ─────────────────────────────────────
// 函子实例。观察 A，然后对其进行变换。
template <class A, class F>
[[nodiscard]] inline auto map(Lens<A> a, F f)
    -> Lens<std::invoke_result_t<F, A>> {
    using B = std::invoke_result_t<F, A>;
    return Lens<B>{[a = std::move(a), f = std::move(f)](
                       const Json& j) -> std::optional<B> {
        if (auto v = a(j)) return f(*v);
        return std::nullopt;
    }};
}

// ── 收集: 镜头 <vector<T>> ────────────────────────────────────────────
// 通过用元件镜头行走每个元件并保留观察的元件来观察ARRAY成员。
// 用于结构化内容 -parts 形式 (Mistral)，
// 其中 `content` 是 带类型的 部分的列表，而不是字符串。
template <class T>
[[nodiscard]] inline Lens<std::vector<T>> collect(std::string name,
                                                  Lens<T> element) {
    return Lens<std::vector<T>>{
        [name = std::move(name), element = std::move(element)](
            const Json& j) -> std::optional<std::vector<T>> {
            if (!j.is_object()) return std::nullopt;
            const auto it = j.find(name);
            if (it == j.end() || !it->is_array()) return std::nullopt;
            std::vector<T> out;
            out.reserve(it->size());
            for (const auto& el : *it)
                if (auto v = element(el)) out.push_back(std::move(*v));
            if (out.empty()) return std::nullopt;
            return out;
        }};
}

// ── 当 : (预告, 连数) <T> ) + 连 <T> ────────────────────────────────
// 对鉴别器e.g进行观察。仅从`type`为"thinking"
// 的内容部分中读取`text`。
template <class T>
[[nodiscard]] inline Lens<T> when(std::string tag_key, std::string tag_value,
                                  Lens<T> inner) {
    return Lens<T>{[tag_key = std::move(tag_key), tag_value = std::move(tag_value),
                    inner = std::move(inner)](const Json& j) -> std::optional<T> {
        if (!j.is_object()) return std::nullopt;
        const auto it = j.find(tag_key);
        if (it == j.end() || !it->is_string()) return std::nullopt;
        if (it->template get_ref<const std::string&>() != tag_value)
            return std::nullopt;
        return inner(j);
    }};
}

// ── 证明/观察 <T> ─────────────────────────────────────────────
//
// 关于端点的事实是三件事之一，并且"unknown" 必须 NOT
// 折叠成"no"。
//
// 为了防止而存在的错误具有名称和编号。 PR #53 为供应商
// DECLARING 提出了 注册表 行，它为型号 `yolo`
// 和 `yolo-small` 提供服务。用真正的密钥进行探测，
// `/v1/models` 都没有列出，并且调用它们为每个免费
// 帐户返回 403。声明不是观察。在没有真相来源的生态系统中，
// 只有探测器才能说话——并且类型系统应该拒绝让暗示冒充事实。
enum class Provenance : std::uint8_t {
    Probed,    // 我们拨打端点并看到了这一点。权威性。
    Declared,  // 注册表行或文档页面断言它。 HINT。
    Absent,    // 我们探查过，它不存在。
};

template <class T>
struct Observed {
    Provenance       how = Provenance::Absent;
    std::optional<T> value{};

    [[nodiscard]] static Observed probed(T v) {
        return {Provenance::Probed, std::move(v)};
    }
    [[nodiscard]] static Observed declared(T v) {
        return {Provenance::Declared, std::move(v)};
    }
    [[nodiscard]] static Observed absent() { return {Provenance::Absent, {}}; }

    // DELIBERATELY NOT操作符bool，并且故意不命名为`has_value`。
    // `if (caps.tools)`将声明的提示读取为真，
    // 这正是该类型存在的造成不可表示的混乱。
    // 来电者必须说明其需要哪种证据标准。
    [[nodiscard]] bool confirmed() const noexcept {
        return how == Provenance::Probed && value.has_value();
    }
    // 当错误时足够好很便宜（选择面板副本，默认），
    // 而不是当它决定请求是否好-formed时。
    [[nodiscard]] bool believed() const noexcept {
        return how != Provenance::Absent && value.has_value();
    }
    [[nodiscard]] const T& or_else(const T& fallback) const noexcept {
        return value ? *value : fallback;
    }
};

} // namespace agentty::provider::openai
