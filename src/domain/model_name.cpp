// agentty::model_name——显示-name SSOT的实现。
//
// 该文件的结构：
//
//   1. `normalize_id_words` — id → human-typography 通行证。这是
//      这里是 仅 字符串分词器，它是曾经存在于
//      ui::pretty_model_label 中的字符串分词器（从
//      视图层向下移动，行为不变 - 其测试套件仍然固定它）。它处理 快照
//      日期、:最新别名、Ollama 标签、首字母缩略词大小写和版本连接。
//
//   2.`decode`-将TAXONOMY委托给ModelCapabilities::from_id并使用
//      (1)仅适用于残留"we don't know this family"的
//      情况并用于协调服务器-supplied的营销名称。
//
//   3.预测。
//
// 分工是整个设计：from_id回答"which lane is this?"，
// 规范化回答"how do humans spell this token?"，并且没有任何
// 东西同时回答两者。

#include "agentty/domain/model_name.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace agentty::model_name {

namespace {

// ── 字符上游( 3) ASCII ; 模型标识为 ASCII 建筑) ────
constexpr bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }

constexpr char to_lower(char c) {
    return is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}
constexpr char to_upper(char c) {
    return is_lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
}

constexpr bool lower_eq(std::string_view w, std::string_view lit) {
    if (w.size() != lit.size()) return false;
    for (std::size_t i = 0; i < w.size(); ++i)
        if (to_lower(w[i]) != lit[i]) return false;
    return true;
}

constexpr bool all_digits(std::string_view w) {
    if (w.empty()) return false;
    for (char c : w)
        if (!is_digit(c)) return false;
    return true;
}

// 8-digit "20YYMMDD"运行——版本快照，不是身份。
constexpr bool is_snapshot_word(std::string_view w) {
    return w.size() == 8 && all_digits(w) && w.substr(0, 2) == "20";
}

// Alias/status 标记提供者在其显示名称中的括号拼写：
// "GPT-5.3 Chat (latest)"。出处，而非身份。
constexpr bool is_paren_alias(std::string_view w) {
    if (w.size() < 2 || w.front() != '(' || w.back() != ')') return false;
    const std::string_view in = w.substr(1, w.size() - 2);
    return lower_eq(in, "latest") || lower_eq(in, "preview")
        || lower_eq(in, "deprecated") || lower_eq(in, "beta")
        || lower_eq(in, "exp") || lower_eq(in, "experimental");
}

// Ollama标签中的纯量化噪声：改变保真度，而不是身份，并且读起来像选择
// 面板行中的线路噪声。 `q4_K_M`、`Q8_0`、`fp16`。
constexpr bool is_quant(std::string_view p) {
    if (p.size() < 2) return false;
    if ((p.front() == 'q' || p.front() == 'Q') && is_digit(p[1])) return true;
    return p == "fp16" || p == "fp32" || p == "bf16";
}

// ── 人类的排字法 ────────────────────────────────────────
//
// 规则，按优先顺序：
//   ·现有的all-caps首字母缩略词(≤4个字符)被逐字保留——GPT、GLM、SQL
//   ·精心设计的小写首字母缩略词是 upper-cased — 这些读起来是错误的 title-cased
//   ·混合-case品牌名称获得其品牌拼写——ChatGPT、DeepSeek
//   · DIGIT-LED 字是 version/size 串并保持小写 — 4o、8x7b、70b
//   ·o-series约定：数字之前的单独的'o'是小写的——o4-mini
//   ·否则标题-case为前导字母
void append_word(std::string& out, std::string_view w) {
    if (w.empty()) return;

    bool all_caps = true, has_alpha = false;
    for (char c : w)
        if (is_alpha(c)) {
            has_alpha = true;
            if (!is_upper(c)) all_caps = false;
        }
    if (has_alpha && all_caps && w.size() <= 4) {
        out.append(w);
        return;
    }

    for (std::string_view acr : {"gpt", "glm", "sql", "tts", "vl"})
        if (lower_eq(w, acr)) {
            for (char c : w) out.push_back(to_upper(c));
            return;
        }

    struct Brand { std::string_view key, spelling; };
    for (const Brand& b : {Brand{"chatgpt", "ChatGPT"},
                           Brand{"deepseek", "DeepSeek"},
                           Brand{"openai", "OpenAI"}})
        if (lower_eq(w, b.key)) {
            out.append(b.spelling);
            return;
        }

    if (is_digit(w.front())) {
        for (char c : w) out.push_back(to_lower(c));
        return;
    }

    if (w.size() >= 2 && (w[0] == 'o' || w[0] == 'O') && is_digit(w[1])) {
        out.push_back('o');
        out.append(w.substr(1));
        return;
    }

    bool cased = false;
    for (char c : w) {
        if (!cased && is_lower(c)) {
            out.push_back(to_upper(c));
            cased = true;
        } else {
            if (is_alpha(c)) cased = true;
            out.push_back(c);
        }
    }
}

// ── id – 人类排印 ────────────────────────────────────────────────
//
// 处理自然出现的形状：
//   claude-3-5-haiku-20241022 快照字→"Claude 3.5 Haiku"
//   gpt-4o-2024-08-06 4-2-2日期三元→"GPT 4o"
//   chatgpt-4o-latest别名指针→"ChatGPT 4o" qwen2.5
//   -coder:7b Ollama尺寸标签→"Qwen2.5 Coder 7b" llama3:
//   70b-instruct-q4_K_M 标签链 + Quant → "Llama3 70b Instruct"
//   openrouter/anthropic/… 提供者命名空间被剥离
//
// 注意，它确实NOT剥离[1m]/[2m]：decode()首先剥离那些，
// 因此标记变成结构化的`annotation`而不是被默默地丢弃
// （付费-for 1M窗口在徽章中不可见的错误）。
std::string normalize_id(std::string_view id) {
    // Provider命名空间：保留最后一个'/'之后的段。
    if (const auto slash = id.find_last_of('/');
        slash != std::string_view::npos)
        id.remove_prefix(slash + 1);

    // Ollama `:tag` (尺寸/变体/定量选择器)。
    std::string_view tag;
    if (const auto colon = id.find(':'); colon != std::string_view::npos) {
        tag = id.substr(colon + 1);
        id  = id.substr(0, colon);
    }
    // `:latest`不携带信息——每次拉取都默认它。
    if (lower_eq(tag, "latest")) tag = {};

    // 收集单词FIRST，以便邻居-aware规则（版本连接、
    // 日期三元组）可以看到整个ID而不是盲目的流式传输。
    std::vector<std::string_view> words;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        const bool boundary =
            (i == id.size() || id[i] == '-' || id[i] == '_' || id[i] == ' ');
        if (!boundary) continue;
        if (i > start) words.push_back(id.substr(start, i - start));
        start = i + 1;
    }

    // 删除跟踪版本快照——出处，而不是身份。
    if (!words.empty() && is_snapshot_word(words.back())) {
        words.pop_back();
    } else if (words.size() >= 4) {   // ≥4，因此我们永远不会将 id 缩减为空
        const auto y = words[words.size() - 3];
        const auto m = words[words.size() - 2];
        const auto d = words[words.size() - 1];
        if (y.size() == 4 && all_digits(y) && y.substr(0, 2) == "20"
            && m.size() == 2 && all_digits(m)
            && d.size() == 2 && all_digits(d))
            words.resize(words.size() - 3);
    }
    // 尾随的"latest"/"(preview)"是别名指针，而不是名称。
    if (words.size() > 1 && lower_eq(words.back(), "latest"))
        words.pop_back();
    if (words.size() > 1 && is_paren_alias(words.back()))
        words.pop_back();

    std::string out;
    out.reserve(id.size() + tag.size() + 1);
    bool prev_short_number = false;
    for (const std::string_view w : words) {
        // 版本连接：相邻的短pure-digit字是ONE点版本，不是两个数字——
        // `claude-sonnet-4-5`是Sonnet 4.5，
        // `claude-3-5-haiku`是3.5。 Size/quant 单词 (9b,
        // 8x7b) 包含字母，因此它们永远不会连接； 快照 被丢弃在上面。
        const bool short_number = all_digits(w) && w.size() <= 2;
        if (short_number && prev_short_number) {
            out.push_back('.');
            out.append(w);
        } else {
            if (!out.empty()) out.push_back(' ');
            append_word(out, w);
        }
        prev_short_number = short_number;
    }
    if (out.empty()) out.assign(id);   // 病理all-delimiter id

    // Ollama标签链：保留人类区分型号BY(尺寸`70b`、变体、
    // `instruct`/`coder`)，丢弃量化。
    if (!tag.empty()) {
        std::string cleaned;
        std::size_t p0 = 0;
        for (std::size_t i = 0; i <= tag.size(); ++i) {
            if (i != tag.size() && tag[i] != '-') continue;
            const std::string_view part = tag.substr(p0, i - p0);
            p0 = i + 1;
            if (part.empty() || is_quant(part)) continue;
            if (!cleaned.empty()) cleaned.push_back(' ');
            append_word(cleaned, part);
        }
        if (!cleaned.empty()) {
            out.push_back(' ');
            out.append(cleaned);
        }
    }
    return out;
}

// 字母数字-only，外壳-folded骨架。具有相同骨架的两根
// 琴弦是同一型号，但拼写不同（"GPT-4o" vs "gpt-4o"），
// 不带有任何营销信号。
std::string skeleton(std::string_view s) {
    std::string k;
    k.reserve(s.size());
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)))
            k.push_back(to_lower(c));
    return k;
}

// ── 输入加硬化 ──────────────────────────────────────────────────────
//
// Model id从提供商API和用户配置到达。 NEITHER 值得信赖：
// 如果端点是（自定义主机、受损聚合器、typo-squatted代理），
// 则`/v1/models`响应是attacker-controlled，
// 并且它直接流向解释它所传递的内容的终端。
//
// 这里在边界处强制执行两个属性，
// 因此下游投影或小部件不必记住它们：
//
//   1.NO CONTROL CHARACTERS到达屏幕。一个 id 包含
//      "\x1b[31m"否则会发出LIVE转义序列——重新
//      着色UI，移动光标，或者破坏用户从不使用带类型的的
//      字符串的显示。这是一个 终端-injection 错误，
//      而不是装饰性错误，这就是为什么清理工作属于解码器而不是
//      每个调用者。
//
//   2. LENGTH IS BOUNDED。 4 KB id 不是名称；这是对的否认
//      针对每个宽度-shed梯子下游的服务（作曲家页脚、
//      状态栏、选择面板行都测量它们所给出的内容）。
//      截断是可见的（省略号，而不是无声剪切），因此令人惊讶的标签读
//      取为 "too long"，而不是 "wrong"。
//
// 两者都是故意哑的：下降字节，帽长度。更糟糕的是-is-better
// ——在一个瓶颈点应用的简单规则胜过在多个地方应用的聪明规则，
// 因为在新的调用站点上没有什么可以犯微妙的错误，
// 也没有什么可以忘记的。

// 我们将发出的最长的标签。轻松超过了最长的真实型号名称（
// "Qwen2.5 Coder 32b Instruct" 是 26），
// 并且远低于棚梯必须应对的任何宽度。
constexpr std::size_t kMaxNameLen = 48;

// 我们甚至会查看最长的ID。除此之外，通过任何合理的读取，
// 输入都不是模型 ID，并且 解析 它浪费的工作量与攻击者
// 的输入成正比。
constexpr std::size_t kMaxIdLen = 256;

// 对于决不能到达 终端 的 字节 来说也是如此：C0 控件（包括 ESC、
// CR、LF、TAB、BEL）和 DEL。 UTF-8延续字节都是
// >= 0x80 and so pass through untouched — "Modèle" and "模型" survive.
constexpr bool is_control_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

// 放下控制字节并折叠它们留下的空白。
std::string strip_controls(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (!is_control_byte(c)) out.push_back(c);
    return out;
}

// 给成品标签盖上盖子，标记切口，以便截断是清晰的而不是神秘的。
// 在 UTF-8 边界上截断：切割 -sequence
// 中部会发出替换字形，或者更糟糕的是，使 渲染器 的宽度
// 计算不同步。
std::string clamp_len(std::string s) {
    if (s.size() <= kMaxNameLen) return s;
    std::size_t cut = kMaxNameLen - 1;
    // 退出任何 UTF-8 延续 字节 (10xxxxxx)。
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    s.resize(cut);
    s.append("\xe2\x80\xa6");   // …
    return s;
}

// ── 供应商前缀 ──────────────────────────────────────────────────────
//
// ID (`claude-`、`anthropic.`、
// `openai/`)中的主要供应商令牌是模型名称的NOT部分——
// 它是provider/vendor出处，并且提供者已经由其自己的标签从注册表行呈现。
//
// 这对于NOT表还知道的家庭来说是最重要的。 `claude-opus-4-8`
// 通过族表和 渲染 "Opus 4.8"、vendor-free 进行解码。
// 如果没有这条条带，当 Anthropic 发布
// `claude-quasar-6` 的时候，它就会变成 渲染
// "Claude Quasar 6"——不一致的排版恰好出现在用户最有可能看到它的那一刻。
// 这里的剥离意味着未知族降级为已知族的 相同 形状。
//
// 仅去除前导标记，并且仅精确匹配：`claude-opus`是前缀，
// 但真正命名为`gemma`的模型保留其名称。
constexpr std::string_view kVendorPrefixes[] = {
    "claude", "anthropic", "openai", "google", "meta", "mistralai",
    "deepseek-ai", "alibaba", "qwen-team", "x-ai", "xai",
};

bool is_vendor_token(std::string_view t) {
    for (const auto v : kVendorPrefixes)
        if (lower_eq(t, v)) return true;
    return false;
}

// 解码的generation/revision.暗示的点版本重新使用from_id的已经-validated数字而不是重新-scanning字符串
// ——这正是旧的第三版本
// -extractor与关于日期的其他两个版本不一致的原因。
//
std::string version_from(const ModelCapabilities& caps) {
    if (caps.generation <= 0) return {};
    std::string v = std::to_string(caps.generation);
    if (caps.revision > 0) {
        v.push_back('.');
        v.append(std::to_string(caps.revision));
    }
    return v;
}

// 将id分割成'-'/'.'-delimited令牌，其中提供者命名
// 空间和任何Ollama `:tag`已被移除。用于 from_id
// 确实 不 公开的两个剩余事实，这两个事实都是 POSITION
// -dependent，因此无法单独从功能结构中恢复。
std::vector<std::string_view> id_tokens(std::string_view id) {
    if (const auto slash = id.find_last_of('/');
        slash != std::string_view::npos)
        id.remove_prefix(slash + 1);
    if (const auto colon = id.find(':'); colon != std::string_view::npos)
        id = id.substr(0, colon);

    std::vector<std::string_view> t;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= id.size(); ++i) {
        bool boundary = (i == id.size() || id[i] == '-');
        // '.'分割了VERSION (`gpt-5.1` → 5 | 1)，
        // 但不是它所粘着的名称(`llama3.3` → Llama3.3，
        // `qwen2.5` → Qwen2.5)。区分测试是到目前为止的令牌是否是纯数字：
        // 在 `5.1` 中是，在 `llama3.3` 中不是。无条件分裂将
        // "Llama3.3"变成了"Llama3 3"。
        if (!boundary && id[i] == '.') {
            const std::string_view sofar = id.substr(start, i - start);
            boundary = all_digits(sofar);
        }
        if (!boundary) continue;
        if (i > start) t.push_back(id.substr(start, i - start));
        start = i + 1;
    }
    return t;
}

// 命名家族或npos的令牌的索引。
std::size_t family_token_index(const std::vector<std::string_view>& toks) {
    for (std::size_t i = 0; i < toks.size(); ++i)
        if (lower_eq(toks[i], "haiku") || lower_eq(toks[i], "sonnet")
         || lower_eq(toks[i], "opus")  || lower_eq(toks[i], "fable")
         || lower_eq(toks[i], "mythos") || lower_eq(toks[i], "gpt"))
            return i;
    return std::string_view::npos;
}

// LEGACY SCHEMA RECOVERY。
//
// from_id从代币AFTER中读取代家族字，即现代的模式定义 (`claude-sonnet-4-5`)。
// 旧版 模式定义 将其设置为 之前 (`claude-3-5-haiku-20241022`)，
// 因此 caps.generation 在那里为 0 — catalog.hpp
// 的 max_output_tokens_for() 准确记录了这一点，
// 并使用 `find("claude-3")` 前缀嗅探来解决它。
//
// 没有这个，`claude-3-5-haiku` 渲染作为裸"Haiku"，
// 失去3.5并使其与选择面板行中的4.5俳句无法区分。
// 阅读该系列单词之前的简短的 pure-digit。
std::string version_before_family(const std::vector<std::string_view>& toks,
                                  std::size_t fam) {
    if (fam == std::string_view::npos || fam == 0) return {};
    std::size_t first = fam;
    while (first > 0 && all_digits(toks[first - 1])
           && toks[first - 1].size() <= 2)
        --first;
    if (first == fam) return {};

    std::string v;
    for (std::size_t i = first; i < fam; ++i) {
        if (!v.empty()) v.push_back('.');
        v.append(toks[i]);
    }
    return v;
}

// QUALIFIER RECOVERY。
//
// 系列 + 版本并不总是完整的标识：`gpt-5.1-codex-max`
// 和 `gpt-5.1` 是不同的产品，并且 渲染 都作为
// "GPT 5.1" 会使 选择面板 对于您实际要支付的型号不明确。
// 收集跟随该系列及其版本运行的尾随字母标记 - "Codex Max"、
// "Chat"、"Sol" - 完全像 normalize_id
// 一样删除出处（快照，别名指针）。
std::string qualifier_after_family(const std::vector<std::string_view>& toks,
                                   std::size_t fam) {
    if (fam == std::string_view::npos) return {};

    // 跳过家族词，然后跳过任何短数字版本标记。
    std::size_t i = fam + 1;
    while (i < toks.size() && all_digits(toks[i]) && toks[i].size() <= 2)
        ++i;

    std::string out;
    for (; i < toks.size(); ++i) {
        const std::string_view t = toks[i];
        if (is_snapshot_word(t)) continue;              // 20250514
        if (all_digits(t) && t.size() >= 3) continue;   // 日期片段
        if (lower_eq(t, "latest")) continue;            // 别名指针
        if (!out.empty()) out.push_back(' ');
        append_word(out, t);
    }
    return out;
}

// UNKNOWN-FAMILY STRUCTURAL DECODE。
//
// 已知家族从表中获得其名称，并从from_id获得其版本。
// 未知的两者都没有——但它是NOT，因此是非结构化的。 Model
// id 绝大多数遵循 `[vendor-]name-version[-qualifier…]`，
// 因此我们可以恢复相同的三个字段，并为未知模型提供与已知
// 模型相同的棚梯和相同的版式。
//
// 这就是使解码器优雅地降级而不是从悬崖上掉下来的原因。在此之前，
// `gemini-4-pro` 为所有三个梯级生产了一根扁绳 -
// 因此棚梯是 no-op，其名称为 LONGEST，棚梯最重要
// - 未来的 `claude-quasar-6` 会使用当前
// 型号未使用的供应商前缀进行渲染。
//
// 填充`out.name`/`.version`/`.qualifier`。对于非 -empty id，
// 切勿将名称留空。
void decode_unknown(std::string_view id, ModelName& out) {
    // Ollama `:tag`携带人类区分本地模型BY的内容——尺寸(
    // `70b`)和变体(`instruct`)——并且id_tokens()丢弃它，
    // 因此单独恢复它并将其附加到限定符。丢失它会将 `llama3:8b`
    // 和 `llama3:70b` 折叠到同一标签。
    std::string tag_words;
    if (const auto colon = id.find(':'); colon != std::string_view::npos) {
        std::string_view tag = id.substr(colon + 1);
        if (!lower_eq(tag, "latest")) {
            std::size_t p0 = 0;
            for (std::size_t i = 0; i <= tag.size(); ++i) {
                if (i != tag.size() && tag[i] != '-') continue;
                const std::string_view part = tag.substr(p0, i - p0);
                p0 = i + 1;
                if (part.empty() || is_quant(part)) continue;
                if (!tag_words.empty()) tag_words.push_back(' ');
                append_word(tag_words, part);
            }
        }
    }
    const auto attach_tag = [&](std::string& q) {
        if (tag_words.empty()) return;
        if (!q.empty()) q.push_back(' ');
        q.append(tag_words);
    };

    auto toks = id_tokens(id);

    // 领先供应商令牌是出处，而不是名称。
    std::size_t b = 0;
    while (b < toks.size() && is_vendor_token(toks[b])) ++b;
    // ...除非消耗了所有东西（仅`claude`）：保留最后一个，
    // 这样名称就不会为空。
    if (b == toks.size() && b > 0) --b;

    // 尾随出处：快照日期、日期片段、别名指针。
    std::size_t e = toks.size();
    while (e > b + 1) {
        const std::string_view t = toks[e - 1];
        if (is_snapshot_word(t) || lower_eq(t, "latest")
            || (all_digits(t) && t.size() >= 3)) { --e; continue; }
        break;
    }
    // `YYYY-MM-DD` 三元组拼写为三个标记（gpt-4o-2024-08-06）。
    if (e >= b + 4) {
        const auto y = toks[e - 3], mo = toks[e - 2], d = toks[e - 1];
        if (y.size() == 4 && all_digits(y) && y.substr(0, 2) == "20"
            && mo.size() == 2 && all_digits(mo)
            && d.size() == 2 && all_digits(d))
            e -= 3;
    }

    if (b >= e) {                       // 什么都没有幸存：整体倒退
        out.name = normalize_id(id);
        return;
    }
    // NAME运行直到第一个独立版本令牌——短的纯数字运行，
    // 或者本身不是名称的一部分的数字-led令牌（`gpt-4o`
    // IS中的`4o`是名称的版本；`qwen2.5`中的`2.5`
    // 被标记器粘在其上并且从不显得独立）。
    std::size_t v = b;
    while (v < e && !(all_digits(toks[v]) && toks[v].size() <= 2)) ++v;

    std::string name;
    for (std::size_t i = b; i < v; ++i) {
        if (!name.empty()) name.push_back(' ');
        append_word(name, toks[i]);
    }

    // VERSION：短数字标记的运行，用点表示。
    std::size_t q = v;
    std::string version;
    while (q < e && all_digits(toks[q]) && toks[q].size() <= 2) {
        if (!version.empty()) version.push_back('.');
        version.append(toks[q]);
        ++q;
    }

    // QUALIFIER：版本后面的内容。
    std::string qualifier;
    for (std::size_t i = q; i < e; ++i) {
        if (!qualifier.empty()) qualifier.push_back(' ');
        append_word(qualifier, toks[i]);
    }

    // id为ONLY版本(`4-5`)的id没有名称；
    // 保持标准化的整体而不是发出裸露的数字。
    if (name.empty()) {
        out.name = normalize_id(id);
        return;
    }

    attach_tag(qualifier);
    out.name      = std::move(name);
    out.version   = std::move(version);
    out.qualifier = std::move(qualifier);
}

} // 命名空间结束。

// ── 解码 ───────────────────────────────────────────────────────────────

ModelName decode(std::string_view id, std::string_view server_name) {
    ModelName out;
    if (id.empty()) return out;

    // ── 0. 投入的硬化, 一次 ,在事发前 解析 他们 ──────
    //
    // 低于此点的所有内容都可以假定其输入是control
    // -free并且有界。这个假设使得文件的其余部分保持简单：
    // 没有解析器、投影或小部件必须重新-check，
    // 并且没有可以忘记的调用站点。
    const std::string safe_id   = strip_controls(
        id.substr(0, std::min(id.size(), kMaxIdLen)));
    const std::string safe_name = strip_controls(
        server_name.substr(0, std::min(server_name.size(), kMaxIdLen)));
    id          = safe_id;
    server_name = safe_name;
    if (id.empty()) return out;   // id为ENTIRELY的控件字节

    // 夹住输出时的每个字段，无论哪个分支产生它。
    // 在这里而不是在每个分支中执行此操作意味着未来的
    // 分支无法重新引入无界标签。
    const auto finish = [](ModelName n) {
        n.name      = clamp_len(std::move(n.name));
        n.qualifier = clamp_len(std::move(n.qualifier));
        n.version   = clamp_len(std::move(n.version));
        return n;
    };

    // ── 1. 将 " 破坏 " 行为 agentty 延长 -context 标记 ───────────────────
    // `[1m]`/`[2m]`是选择-time标记(
    // wire_model_id在id到达传输之前剥离它们)。
    // 它们不能作为文字泄漏，但也不能默默地 DROPPED：变体
    // 是其 选择面板 行的全部要点，启用 1M 窗口的用户需要
    // 看到他们得到了它。所以它变成了结构化数据。
    struct Marker { std::string_view tag, shown; };
    std::string bare;
    for (const Marker& mk : {Marker{"[1m]", "1M"}, Marker{"[2m]", "2M"}}) {
        const auto pos = id.find(mk.tag);
        if (pos == std::string_view::npos) continue;
        bare.assign(id.substr(0, pos));
        bare.append(id.substr(pos + mk.tag.size()));
        out.annotation = std::string{mk.shown};
        id = bare;
        break;
    }

    // 在 normalize_id() 删除路由命名空间之前记住它。
    //
    // 只有基名：`openrouter/anthropic/x`之前的
    // LAST段是通过openrouter到达的，但由anthropic产生，
    // 并且较近的前缀是区分另外两个-identical行的前缀。
    // 对于单个 -segment 前缀 (`igpu/x`)，两者一致。
    if (const auto slash = id.find_last_of('/');
        slash != std::string_view::npos && slash > 0) {
        auto prefix = id.substr(0, slash);
        if (const auto prev = prefix.find_last_of('/');
            prev != std::string_view::npos)
            prefix = prefix.substr(prev + 1);
        out.ns = std::string{prefix};
    }

    // ── 2. 分类学:授权,永远不再 -derived ────────────────────────
    const auto caps = ModelCapabilities::from_id(id);
    out.color        = color_of(caps.family);
    out.family_known = caps.is_known_family();

    if (out.family_known) {
        out.name    = std::string{family_label(caps.family)};
        out.version = version_from(caps);

        // 两个剩余事实from_id无法表达，均为POSITIONAL:
        //
        //   ·遗留模式定义将版本BEFORE置于家族词中
        //     (claude-3-5-haiku)，其中from_id的post
        //     -family读取什么也没找到——为3.5模型留下裸露的"Haiku"；
        //   ·产品限定符可以遵循版本(gpt-5.1-codex-max)，
        //     丢弃它使得两种不同的产品渲染相似。
        //
        // 此处标记一次，并 仅 咨询了该功能
        // 结构体确实没有携带的事实；分类本身仍来自
        // 独家来自from_id。
        const auto toks = id_tokens(id);
        const auto fam  = family_token_index(toks);
        if (out.version.empty())
            out.version = version_before_family(toks, fam);
        out.qualifier = qualifier_after_family(toks, fam);
        return finish(std::move(out));
    }

    // ── 3. 未知家族:从身份识别本身中恢复结构 ───────
    //
    // NOT是一条死胡同。 Model id 绝大
    // 多数遵循 `[vendor-]name-version[-qualifier]`，
    // 因此未知模型与已知模型具有相同的三个字段、相同的棚梯和相同的
    // 供应商-free 排版 - 它只是从字符串而不是表中获取它们。
    // 这就是为什么 UI 的新系列发货为 NON-EVENT：
    // `claude-quasar-6`读取"Quasar 6"，就像`claude-opus-4-8`
    // 读取"Opus 4.8"一样，比任何人将Quasar添加到枚举早几个月。
    decode_unknown(id, out);

    if (server_name.empty() || server_name == id) return finish(std::move(out));

    // 服务器名称仅仅是id的re-cased/re-punctuated拼写吗？
    // 然后更喜欢 id-derived 形式，以便同一模型在不同
    // 的提供商之间读取相同的内容。只有 id 无法重建的真正的营销别名（
    // gemini-3 对应的显示名称为 Nano Banana Pro。
    // -pro-image）才会获胜 - 即使如此，它也会被标准化，
    // 因此它自己的缺陷（（最新），外壳）被清理以匹配列表的其余排版。
    //
    // 营销别名是NAME，而不是名称+版本+限定符三元
    // 组（"Nano Banana Pro"没有要拆分的版本），
    // 因此它替换整个解码而不是合并到其中——合并将
    // 留下从悬挂在不相关别名上的id恢复的版本。
    const std::string from_id   = out.full();
    const std::string from_name = normalize_id(server_name);
    const bool same_model = skeleton(from_id) == skeleton(from_name)
                         || skeleton(server_name) == skeleton(id);
    if (!same_model) {
        out.name      = from_name;
        out.version.clear();
        out.qualifier.clear();
    }
    return finish(std::move(out));
}

// ── 预测 ──────────────────────────────────────────────────────────
//
// 通过构造严格嵌套，每个梯级恰好添加一个字段：
//
//   tiny() = 名称 + 版本
//   medium() = tiny() + 限定符
//   full() = medium() + 注释
//
// 以字面组合形式书写，因此嵌套在页面上是明显的，
// 而不是要记住的 不变量——窄表面永远不能陈述与
// 宽表面相矛盾的东西，因为它是宽表面的前缀。

std::string ModelName::tiny() const {
    std::string s = name;
    if (!version.empty()) {
        s.push_back(' ');
        s.append(version);
    }
    return s;
}

std::string ModelName::medium() const {
    std::string s = tiny();
    if (!qualifier.empty()) {
        s.push_back(' ');
        s.append(qualifier);
    }
    return s;
}

std::string ModelName::full() const {
    std::string s = medium();
    if (!annotation.empty()) {
        s.append(" \xc2\xb7 ");   // " · "
        s.append(annotation);
    }
    return s;
}

std::string ModelName::qualified() const {
    if (ns.empty()) return full();
    std::string s = ns;
    s.push_back('/');
    s.append(full());
    return s;
}

} // namespace agentty::model_name
