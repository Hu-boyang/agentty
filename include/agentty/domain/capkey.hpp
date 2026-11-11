#pragma once
// agentty::capkey —THE规范能力-key纪律。
//
// 每个能力注册表 (声明推理、declared/learned工作集）
// 都是由字符串键入的映射。三个独立的命名机构
// 产生这些字符串，但他们都不同意：
//
//   1.agentty的提供者注册表("mistral"，"gemini"，"together")
//   2.提供商的传输型号ID("mistral-medium-3.5"，"Kimi-K2.5")
//   3、models.dev的社区数据（提供者"togetherai"、"google"、
//      "fireworks-ai";型号密钥有时是vendor-prefixed
//      "mistralai/mistral-medium-3-5"，有时是裸露的、
//      混合大小写的、dot/dash/underscore 相同 型号的拼写）
//
// Mistra-medium-3-5 与 -3.5 Two-ladders
// 错误正是这样的：一个模型的两种拼写通过不同的键解析为不同的事实。
// 修复 per-call-site 的键控是 whack-a-mole；
// 这个 头文件 使其结构：
//
//   RULE 1——一种拼写。在任何 注册表 写入 OR 读取：小写，
//     [._ :] → '-'，运行折叠之前，每个关键组件都会通过
//     norm_model()。 "Kimi-K2.5"、"kimi-k2-5"、
//     "kimi k2.5"为同一型号；现在它们是相同的KEY。
//     (针对完整的models.dev语料库进行验证：269个
//     multi-spelling组统一，没有错误合并—
//     —折叠仅涉及分隔符，而不是字母数字。)
//
//   RULE 2——一个提供者命名空间。作用域密钥始终使用 AGENTTY 的
//     提供商 ID，而不是第三方的。 models.dev 提供者 ID 通过
//     resolve_dev_provider() 进行转换，
//     它锚定在 IDENTITY EVIDENCE 而不是名称表上：
//       a.精确的 ID 匹配（mistral ==
//       milstra） b．主要身份验证环境变量（GEMINI_API_KEY
//                                     名称谷歌的条目 - 环境变量是
//                                     全球唯一的提供者身份的构造） API
//       主机子字符串 (api.githubcopilot.com)
//     不匹配的开发提供者保留 "dev:<id>/" — 命名
//     空间 agentty 的范围查找永远不会发生冲突，
//     因此未知聚合器的事实是惰性的而不是损坏的。
//
//   RULE 3——裸密钥是共识-only。没有提供者范围的密
//     钥只能通过 merge-or-poison 路径写入
//     （同意 → 保留，不同意 → no-info）。无法通过此
//     API 表达对裸键的直接写入。
//
// 这里的一切都是纯粹的 string/model-registry 逻辑——没有 IO，
// 没有它自己的 锁（它提供的注册表带有它们自己的）。

#include <array>
#include <string>
#include <string_view>

#include "agentty/provider/registry.hpp"

namespace agentty::capkey {

// ── RULE1: 犬科模型拼写 ───────────────────────────────────
// 小写； '.'、'_'、' '、':' 归并 至 '-'； '-' 运行崩溃；
// leading/ 尾部 '-' 被修剪； agentty 的
// 内部扩展-context 标记（[1m]/[2m] — 选择面板
// -only，从未在 传输 上）被删除，因为能力事实（推理、
// 努力枚举）属于 MODEL，而不属于上下文 -window 变体 — 在
// 传输 id 下查找的事实必须在查找时命中[1m]-marked 选择面板 编号。
// 只有分隔符和内部标记 归并 — 字母数字永远不会改变，
// 因此不同的模型无法合并。
[[nodiscard]] inline std::string norm_model(std::string_view id) {
    std::string out;
    out.reserve(id.size());
    for (std::size_t i = 0; i < id.size();) {
        // 跳过"[1m]"/"[2m]"('m'上的情况-insensitive)。
        if (id[i] == '[' && i + 3 < id.size()
            && (id[i + 1] == '1' || id[i + 1] == '2')
            && (id[i + 2] == 'm' || id[i + 2] == 'M')
            && id[i + 3] == ']') {
            i += 4;
            continue;
        }
        char c = id[i++];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '.' || c == '_' || c == ' ' || c == ':') c = '-';
        if (c == '-' && (out.empty() || out.back() == '-')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// ── ROW IDENTITY : 喜欢 norm_model ,不过 KEEPS 上下文标记 ────────
// norm_model 归并 `[1m]`/`[2m]` 故意
// 离开——能力事实属于模型，而不属于窗口 变体。对于任何必须区分两个
// 选择面板 ROWS 的事情来说，这是完全错误的：
// `claude-opus-4-8`和`claude-opus-4-8[1m]`
// 是用户必须能够在两个不同的选择之间进行选择，并且折叠它们使得融合的选择
// 面板将1M行视为基行的"alias spelling"并且默默地将其DROP
// (报告为"1M models are not visible")。
//
// 所以：相同的separator/case折叠（因此真正的别名拼写如
// mistral-medium-3-5 vs -3.5仍然重复数据删除），
// 但标记作为区分后缀保留下来。将其用于行重复数据删除/选择标识；
// 使用 norm_model 进行功能查找。
[[nodiscard]] inline std::string norm_row_id(std::string_view id) {
    std::string out;
    out.reserve(id.size());
    for (std::size_t i = 0; i < id.size();) {
        // 逐字保留 "[1m]" / "[2m]"（小写）
        // 而不是跳过它——这个 变体 的全部要点。
        if (id[i] == '[' && i + 3 < id.size()
            && (id[i + 1] == '1' || id[i + 1] == '2')
            && (id[i + 2] == 'm' || id[i + 2] == 'M')
            && id[i + 3] == ']') {
            out += '[';
            out += id[i + 1];
            out += 'm';
            out += ']';
            i += 4;
            continue;
        }
        char c = id[i++];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '.' || c == '_' || c == ' ' || c == ':') c = '-';
        if (c == '-' && (out.empty() || out.back() == '-')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// 可能的供应商-prefixed型号ID的裸末尾，已标准化。
// "mistralai/Mistral-Medium-3.5"→"mistral-medium-3-5"。
[[nodiscard]] inline std::string norm_tail(std::string_view id) {
    if (auto slash = id.rfind('/'); slash != std::string_view::npos)
        id = id.substr(slash + 1);
    return norm_model(id);
}

// ── RULE 2: 供应商 -identity 决议 ────────────────────────────────
// 通过身份证据将models.dev提供者条目映射到agentty的提供者ID。
// `dev_id`是models.dev的钥匙； `dev_env0` 它的第一个环境变量
// （可能为空）； `dev_api` 其 API URL（可能为空）。
//
// 锚，最强的第一：
//   1. dev_id == agentty id（米斯特拉尔、groq、大脑……）
//   2. dev_env0==agentty行的主环境（GEMINI_API_KEY→gemini；
//      env名称是de-facto全局提供者身份：每个SDK，
//      包括models.dev本身，其上的密钥凭据）
//   3.agentty行的端点主机⊆dev_api(api.githubcopilot.com)
// 不匹配 → ""（调用者将记录命名为外部；参见 dev_scope）。
[[nodiscard]] inline std::string resolve_dev_provider(
        std::string_view dev_id,
        std::string_view dev_env0,
        std::string_view dev_api) {
    using provider::providers;
    // 1.准确的id。
    for (const auto& p : providers())
        if (dev_id == p.id) return std::string{p.id};
    // 2.主环境变量。两边只有 首次 环境条目：agentty
    //    行列出后面的槽中的回退 (OPENAI_API_KEY)，
    //    其将跨越-match的一半注册表。
    if (!dev_env0.empty())
        for (const auto& p : providers())
            if (!p.auth_env[0].empty() && dev_env0 == p.auth_env[0])
                return std::string{p.id};
    // 3.Host遏制(固定主机的agentty行)。
    if (!dev_api.empty())
        for (const auto& p : providers())
            if (!p.prewarm_host.empty()
                && dev_api.find(p.prewarm_host) != std::string_view::npos)
                return std::string{p.id};
    return {};
}

// models.dev提供者记录的范围前缀：当身份解析时
// agentty的id，否则惰性外部命名空间"dev:<id>"。
[[nodiscard]] inline std::string dev_scope(std::string_view dev_id,
                                           std::string_view dev_env0,
                                           std::string_view dev_api) {
    auto own = resolve_dev_provider(dev_id, dev_env0, dev_api);
    if (!own.empty()) return own;
    std::string s{"dev:"};
    s += dev_id;
    return s;
}

// ── 关键组装(作家和读者都使用这些组装) ───────────────────
[[nodiscard]] inline std::string scoped(std::string_view provider,
                                        std::string_view model) {
    std::string k;
    const std::string nm = norm_model(model);
    k.reserve(provider.size() + 1 + nm.size());
    k += provider;
    k += '/';
    k += nm;
    return k;
}
[[nodiscard]] inline std::string bare(std::string_view model) {
    return norm_tail(model);
}

} // namespace agentty::capkey
