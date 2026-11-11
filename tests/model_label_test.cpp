// model_label_test - 模型 - 模型 -name 显示 SSOT.
//
// 有两个层被钉住:
//
//   • 观点 -layer 帮助者( E) pretty_model_label / model_display_label),
//     代表人数很少,所以这些案件将给 END-TO-END 结果
//     是用户实际读取;
//   • 解码器本身( domain/model_name.hpp ),其名称为: 不变量 (单位:千美元)
//     筑巢,整体,交叉 -surface 协议,
//     职位恢复) -parser 设计甚至无法表达。
//
// 原始提供者 IDs (% 1) `codellama:latest`,
// `qwen2.5-coder:7b`, `openai/gpt-4o-mini`,
// `claude-sonnet-4-5[1m]` 长而丑陋,这把清理钉在每一个真正的 -world
// 以我们为对象的船体 以一个回归 -leaks 原始身份标识输入 头文件 被困在了这里 CI.

#include "agentty/domain/model_name.hpp"
#include "agentty/runtime/view/helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "agtest.hpp"

namespace {

void expect_label(std::string_view id, std::string_view want) {
    std::string got = agentty::ui::pretty_model_label(id);
    CHECK_MESSAGE(got == want, "pretty_model_label(\"" << std::string(id)
                                  << "\") got \"" << got << "\" want \""
                                  << std::string(want) << "\"");
}

// 以 选择面板 空无 -query 比较者,
// 加入一个单一的可读断言。
std::string ordered(std::vector<std::string> in) {
    std::stable_sort(in.begin(), in.end(),
        [](const std::string& a, const std::string& b){
            return agentty::ui::model_order_less(a, b);
        });
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (i) out += " | ";
        out += in[i];
    }
    return out;
}

} // 命名空间结束。

TEST_CASE("pretty_model_label normalizes real-world ids") {
    // ── Ollama : 降: latest, 保持有意义的 size/quant 标记 ──────────
    expect_label("codellama:latest",        "Codellama");
    expect_label("llama3.2:latest",         "Llama3.2");
    expect_label("qwen2.5-coder:7b",        "Qwen2.5 Coder 7b");
    expect_label("llama3.1:70b",            "Llama3.1 70b");
    expect_label("mixtral:8x7b",            "Mixtral 8x7b");
    expect_label("phi3:3.8b",               "Phi3 3.8b");
    expect_label("deepseek-coder:6.7b",     "DeepSeek Coder 6.7b");
    expect_label("gemma2:9b",               "Gemma2 9b");

    // OpenAI / OpenAI 兼容模型使用标题式大小写，保留 GPT 缩写和版本。
    expect_label("gpt-4o",                  "GPT 4o");
    expect_label("gpt-4o-mini",             "GPT 4o Mini");
    expect_label("gpt-5",                   "GPT 5");
    expect_label("o4-mini",                 "o4 Mini");
    expect_label("chatgpt-4o-latest",       "ChatGPT 4o");     // 品牌大小写 + 异名
    expect_label("gpt-4o-2024-08-06",       "GPT 4o");         // 快照 3 个滴
    expect_label("gpt-4.1-nano",            "GPT 4.1 Nano");
    expect_label("gpt-5.1-codex-max",       "GPT 5.1 Codex Max");
    expect_label("codex-mini-latest",       "Codex Mini");

    // ── Provider-namespaced 编号( 4) OpenRouter / 汇总数) ────────────
    expect_label("openai/gpt-4o-mini",      "GPT 4o Mini");
    expect_label("anthropic/claude-3-haiku","Haiku 3");
    expect_label("meta-llama/Llama-3.1-8B", "Llama 3.1 8B");
    expect_label("google/gemini-2.0-flash", "Gemini 2.0 Flash");

    // ── Gemini /xAI /DeepSeek 主办 ────────────────────────────
    expect_label("gemini-1.5-pro",          "Gemini 1.5 Pro");
    expect_label("grok-2",                  "Grok 2");
    expect_label("grok-beta",               "Grok Beta");
    expect_label("deepseek-r1",             "DeepSeek R1");    // 品牌大小写
    expect_label("deepseek-chat",           "DeepSeek Chat");

    // ── Claude: adjacent version digits join with a dot; snapshots drop ─
    expect_label("claude-sonnet-4-5",       "Sonnet 4.5");
    expect_label("claude-opus-4-1",         "Opus 4.1");
    expect_label("claude-3-5-haiku-20241022", "Haiku 3.5");
    expect_label("claude-sonnet-4-20250514",  "Sonnet 4");

    // ── agentty `[1m]`/`[2m]` 扩展的-context 标记被剥除 ───
    expect_label("claude-sonnet-4-5[1m]",   "Sonnet 4.5 \xc2\xb7 1M");
    expect_label("claude-opus-4-5[2m]",     "Opus 4.5 \xc2\xb7 2M");
    expect_label("gpt-4o[1m]",              "GPT 4o \xc2\xb7 1M");

    // ── Ollama 量子标记: 保持大小 + 变体 ,减少四分音量 ──────
    expect_label("llama3.3:70b-instruct-q4_K_M", "Llama3.3 70b Instruct");
    expect_label("phi4:Q8_0",               "Phi4");
    expect_label("llama3:8b-fp16",          "Llama3 8b");

    // ── 缩略语保存+ 已经 -cased 输入 ────────────────────
    expect_label("glm-4-9b",                "GLM 4 9b");
    expect_label("Llama-3.1-8B-Instruct",   "Llama 3.1 8B Instruct");

    // ── 减位输入从不崩溃 / 永不空 ───────────────────
    expect_label("",                        "");
    expect_label(":latest",                 "");      // 家族空, 标签已丢弃
    expect_label("model",                   "Model");
    expect_label("a",                       "A");
}

TEST_CASE("model_order_less: family group + natural version-desc") {
    // 在一个家庭里, NEWEST 首个——和版本数与整数比较,
    // 所以4.10种 上方 4.8 (解释性词法将颠倒,
    // 经典 file2/file10 错误).
    CHECK(ordered({"Claude Sonnet 4.5", "Claude Sonnet 4.6",
                   "Claude Sonnet 4", "Claude Sonnet 4.10"})
          == "Claude Sonnet 4.10 | Claude Sonnet 4.6 | "
             "Claude Sonnet 4.5 | Claude Sonnet 4");

    // gpt-10必须排在gpt-2(整数,而非词典)之后.
    CHECK(ordered({"GPT 2", "GPT 10", "GPT 5"})
          == "GPT 10 | GPT 5 | GPT 2");

    // 家庭组合在一起,按字母顺序排列,每个家庭内部,
    // 最先更新(Sonnet前的Opus: 'o' < 's'.)
    CHECK(ordered({"Claude Sonnet 4.5", "Claude Opus 4.1",
                   "Claude Sonnet 4.6", "Claude Opus 4.5"})
          == "Claude Opus 4.5 | Claude Opus 4.1 | "
             "Claude Sonnet 4.6 | Claude Sonnet 4.5");

    // 纯洁 -number 在同一位置字母之前的块排序,
    // 所以 "GPT 4o" (第4组) "GPT Image".
    CHECK(ordered({"GPT Image 1", "GPT 4o", "GPT 5"})
          == "GPT 5 | GPT 4o | GPT Image 1");

    // 小数小版本自然比较:5.6 > 5.10为FALSE
    // (10 > 6作为整数)——点部分是单独的数块.
    CHECK(ordered({"GPT 5.6", "GPT 5.10", "GPT 5.2"})
          == "GPT 5.10 | GPT 5.6 | GPT 5.2");

    // 稳定/ 总计: 等于 -family 相同 -version
    // 化名不会坠落和降落
    CHECK(ordered({"Gemini 2.5 Flash", "Gemini 2.5 Flash"})
          == "Gemini 2.5 Flash | Gemini 2.5 Flash");
}

TEST_CASE("model_display_label: one canonical label across providers") {
    using agentty::ui::model_display_label;
    auto L = [](std::string_view id, std::string_view name) {
        return model_display_label(id, name);
    };

    // 无服务器名称( N) OpenAI-compat / Ollama 重复编号) ^ id -normalized.
    CHECK(L("gpt-4o-mini", "")            == "GPT 4o Mini");
    CHECK(L("gpt-image-1.5", "gpt-image-1.5") == "GPT Image 1.5"); // 名字的缩写

    // 服务器名称,只是一个 -cased 编号( "GPT-4o",
    // "Hy-MT2-30B-A3B" 生成 相同 单独标识的清晰标签——十字
    // -provider 一致,而不是提供者的广告 -hoc 弹壳
    CHECK(L("gpt-4o", "GPT-4o")           == L("gpt-4o", ""));
    CHECK(L("hy-mt2-30b-a3b", "Hy-MT2-30B-A3B") == L("hy-mt2-30b-a3b", ""));

    // 服务器名称( latest, 外壳) 中的 cruft 已正常化,
    // 因此它也匹配 id-derived 形式。
    CHECK(L("gpt-5.3-chat-latest", "GPT-5.3 Chat (latest)")
          == L("gpt-5.3-chat-latest", ""));

    // 真实的 MARKETING 无法重建的代号是 KEPT (但仍恢复正常)
    // - "Nano Banana Pro" 重排时不包含
    // "gemini-3-pro-image".
    CHECK(L("gemini-3-pro-image", "Nano Banana Pro") == "Nano Banana Pro");

    // A KNOWN 家族解码完全从 id 中, 所以服务器名称没有
    // 添加任何内容, 被忽略 —— 每个服务商都服务于此模式。 渲染 同样的,
    // 不管什么广告 -hoc 拼写,他们各自发送。
    CHECK(L("claude-sonnet-4-5-20250929", "Claude Sonnet 4.5")
          == "Sonnet 4.5");

    // 脱源:空 id + 空名永不崩溃.
    CHECK(L("", "") == "");
}

// 扩展 -context 变体 一定是,我必须 DISTINGUISHABLE 列表中。
//
// `[1m]` 是一个 选择面板-only 标记 : agentty 附加它,以提供 1M
// 上下文窗口作为单独的、可选择的行,以及 wire_model_id 将它剥离,在ID到达 传输.
// pretty_model_label 也脱光了,没错,因为 传输 标记绝不应作为字面内容
// 漏出 "[1m]" - 但后果是, `claude-opus-4-8` 财务报告和已审计
// 财务报表 `claude-opus-4-8[1m]` 以《公约》第2条第1款 相同 弦乐。
// 两行,名称相同,唯一区别(上下文列)坐落在
// 行的远端的暗淡参考文本中.
//
// fused_models_test 已经说两者是不同的 ROWS ;这一说法是,
// 它们与《公约》第19条不同。 READER ,这是一半的 关系。
TEST_CASE("model label: extended-context variants stay distinguishable") {
    auto L = [](std::string_view id, std::string_view name = "") {
        return agentty::ui::model_display_label(id, name);
    };

    // 基模未动.
    CHECK(L("claude-opus-4-8") == "Opus 4.8");

    // 电话 变体 将窗口放在其上 NAME 不只是它的ctx专栏。
    CHECK(L("claude-opus-4-8[1m]") == "Opus 4.8 \xc2\xb7 1M");
    CHECK(L("claude-sonnet-4-6[2m]") == "Sonnet 4.6 \xc2\xb7 2M");

    // ...他们不同,这是整个问题。
    CHECK(L("claude-opus-4-8") != L("claude-opus-4-8[1m]"));

    // 当服务器同时命名变体时没有DOUBLE标签:一个已知的家族
    // 忽略了服务器名称,所以我们得到OUR 犬语"· 1M",
    // 而不是提供者的ad-hoc "(1M Context)" 拼写——两
    // 个服务于同一变体的提供者同意.
    CHECK(L("claude-opus-4-8[1m]", "Claude Opus 4.8 (1M Context)")
          == "Opus 4.8 \xc2\xb7 1M");

    // 标记从字面上,无论以哪种形式,都永远不会漏出.
    for (std::string_view id : {"claude-opus-4-8[1m]", "claude-sonnet-4-6[2m]"}) {
        const auto s = L(id);
        CHECK(s.find("[1m]") == std::string::npos);
        CHECK(s.find("[2m]") == std::string::npos);
    }

    // 营销化名还是比ID更受欢迎 -derived ,
    // 而不为 变体 处理。
    CHECK(L("gemini-3-pro-image", "Nano Banana Pro") == "Nano Banana Pro");
}

// ── 电话 SSOT 属于自己的 不变量 ────────────────────────────────────────────
//
// 这些是属性, 旧 设计甚至无法表达,
// 因为没有单一的解码值来说明它们——六个独立
// 解析器 ,每个部分覆盖。 SSOT 已存在。

TEST_CASE("model_name: the shed ladder is strictly nested") {
    using agentty::model_name::decode;

    // tiny() ⊑ medium() ⊑ full()。狭小的表面永远
    // 不能说出一个宽的表面相矛盾的东西,因为它从字面上说是它的前缀。
    // 这就是旧的紧凑徽章所违反的:它从一个小的上掉下来了版本。 不同 分行,
    // 所以,我说 "Opus" 财务报告和已审计财务报表 "Opus 4.8"
    // 由无关的代码产生,并可能不同意。
    for (std::string_view id : {"claude-opus-4-8",
                                "claude-opus-4-8[1m]",
                                "claude-3-5-haiku-20241022",
                                "gpt-5.1-codex-max",
                                "gpt-4o-mini",
                                "qwen2.5-coder:7b",
                                "mystery-model"}) {
        const auto n = decode(id);
        CHECK_MESSAGE(n.medium().starts_with(n.tiny()),
                      "medium() must extend tiny() for \"" << std::string(id)
                      << "\": tiny=\"" << n.tiny() << "\" medium=\""
                      << n.medium() << "\"");
        CHECK_MESSAGE(n.full().starts_with(n.medium()),
                      "full() must extend medium() for \"" << std::string(id)
                      << "\": medium=\"" << n.medium() << "\" full=\""
                      << n.full() << "\"");
    }
}

TEST_CASE("model_name: totality — never blank, never a raw id leak") {
    using agentty::model_name::decode;

    // 总函数: 任何 id 值产生非 -empty 名称,没有 agentty
    // -internal 标记一直到达屏幕。
    for (std::string_view id : {"claude-opus-4-8[1m]", "gpt-5", "o4-mini",
                                "openrouter/anthropic/claude-opus-4",
                                "llama3:70b-instruct-q4_K_M",
                                "codex-mini-latest", "x", "---"}) {
        const auto n = decode(id);
        CHECK_MESSAGE(!n.name.empty(),
                      "name must never be empty for \"" << std::string(id) << "\"");
        const auto s = n.full();
        CHECK(s.find("[1m]") == std::string::npos);
        CHECK(s.find("[2m]") == std::string::npos);
    }

    // 处理掉的输入,而不是崩溃。
    CHECK(decode("").name.empty());
    CHECK(decode("").full().empty());
}

TEST_CASE("model_name: one model has ONE identity across surfaces") {
    using agentty::model_name::decode;

    // 错误类 此文件存在以预防: 作曲者 标签 说 "Opus"
    // 转弯 头文件 说 "Opus 4.8" ,则 选择面板 说
    // "Claude Opus 4.8" 同一个模型,三弦,
    // 三弦 解析器 现在,每个表面都预测 相同 解码值,
    // 因此家族字和颜色通过构造是完全相同的,只有棚层级别不同.
    const auto a = decode("claude-opus-4-8");
    const auto b = decode("claude-opus-4-8");
    CHECK(a == b);                       // 解码是一个纯函数

    // 该版本生存到中转——这是旧的紧凑徽章造成的
    // 准确信息损失(4.5对4.8不可分).
    CHECK(decode("claude-opus-4-5").medium() != decode("claude-opus-4-8").medium());

    // 一个已知的家庭从不依赖提供者的拼写,
    // 因此两个不同的提供者所服务的同一模型读起来相同.
    CHECK(decode("claude-sonnet-4-5", "Claude Sonnet 4.5 (Bedrock)").full()
          == decode("claude-sonnet-4-5", "anthropic.claude-sonnet-4-5-v1").full());

    // 颜色来自家庭桌子,因此一个车道的两名成员共用一个胡同,
    // 两个车道则不使用.
    CHECK(decode("claude-opus-4-8").color == decode("claude-fable-5").color);
    CHECK(decode("claude-opus-4-8").color != decode("claude-sonnet-4-5").color);
    CHECK(decode("claude-haiku-4-5").color != decode("claude-sonnet-4-5").color);
}

TEST_CASE("model_name: positional facts from_id cannot express") {
    using agentty::model_name::decode;

    // LEGACY SCHEMA : 版本( X) 之前 家庭的词。 from_id
    // 读取符号 之后 家人,所以,我们 caps.generation 此处为
    // 0( N) catalog.hpp 文档并用查找来操作( "claude-3"
    // 没有恢复证,这只只够光着身子 "Haiku" ——3.5
    // 型型号与4.5型型号不可区分. 选择面板 划线
    CHECK(decode("claude-3-5-haiku-20241022").medium() == "Haiku 3.5");
    CHECK(decode("claude-3-haiku").medium()            == "Haiku 3");

    // ...而MODERN 模式定义 仍然从from_id读取其版本.
    CHECK(decode("claude-sonnet-4-5").medium() == "Sonnet 4.5");

    // 快照 日期是来源,从来不是版本。 "Sonnet 4.20250514" 本来是
    // 真实的 渲染 虫子,你这混蛋
    CHECK(decode("claude-sonnet-4-20250514").medium() == "Sonnet 4");
    CHECK(decode("claude-sonnet-4-20250514").full().find("2025")
          == std::string::npos);

    // QUALIFIER :产品后缀是身份,而不是装饰——gpt-5.1和gpt
    // -5.1. -codex-max 不同产品,不能同时读作
    // "GPT 5.1".
    CHECK(decode("gpt-5.1-codex-max").medium() == "GPT 5.1 Codex Max");
    CHECK(decode("gpt-5.1-codex-max").medium() != decode("gpt-5.1").medium());
    // 它在最窄的径处流出 宽度是 绑定 限制。
    CHECK(decode("gpt-5.1-codex-max").tiny() == "GPT 5.1");
}

TEST_CASE("model_name: no family borrows a status hue") {
    // 执行时间: COMPILE 时间 proofs::no_family_uses_a_status_hue ;
    // 此选项重现于 运行时 所以从测试中可以发现意图。
    // palette.hpp 规则是 "one hue = one axis" :
    // 绿色表示状态 -ok ,所以海库 -as-green (旧部件的映射) 使模型
    // 看起来像一个完成检查标记。
    using agentty::model_name::decode;
    for (std::string_view id : {"claude-haiku-4-5", "claude-sonnet-4-5",
                                "claude-opus-4-8", "claude-fable-5", "gpt-5"}) {
        const auto c = decode(id).color;
        CHECK(c != maya::Color::green());
        CHECK(c != maya::Color::bright_green());
        CHECK(c != maya::Color::red());
        CHECK(c != maya::Color::yellow());
    }
}

// ── SSOT 执行 ─────────────────────────────────────────────────────
//
// 电话 不变量 上面证明 DECODER 这件案子是保护了解码器不能
// 做到的东西: 每个表面都确实 ROUTES 差距不是假设的——巩固六大因素。
// 解析器 最初错过了第七( activity_indicator.cpp
// 携带自己的 is_opus/is_sonnet/is_haiku
// 颜色链条),它已经以预测的方式漂移: Opus plain
// -magenta 而不是光明,还有海库 GREEN 状态 -ok 发现它时,
// 一个grep发现了它。这个测试意味着下一个被找到的是一个
// CI 换成是的
TEST_CASE("model_name: every surface agrees on a model's colour") {
    using agentty::model_name::decode;

    // 模型的颜色是模型的功能 ALONE —— 不需要
    // 这些部件。 任何表面重置 -deriving
    // a 当地人对此持异议,因为家庭表是地图的唯一
    // 存在地点。
    //
    // 这些是 SLOTS ,而不是字面上:一个家庭的呼声是
    // 一个角色,主题决定一个角色的长相。在这里用字面上
    // 写字会把徽章钉在一个调色板上,这正是它过去所做的。
    using S = maya::ThemeSlot;
    struct Case { std::string_view id; maya::Color want; };
    const Case cases[] = {
        {"claude-opus-4-8",   maya::Color::slot(S::Accent)},
        {"claude-fable-5",    maya::Color::slot(S::Accent)},
        {"claude-mythos-5",   maya::Color::slot(S::Accent)},
        {"claude-sonnet-4-5", maya::Color::slot(S::Primary)},
        {"claude-haiku-4-5",  maya::Color::slot(S::Link)},
    };
    for (const auto& c : cases)
        CHECK_MESSAGE(decode(c.id).color == c.want,
                      "family colour drifted for \"" << std::string(c.id) << "\"");

    // 巷会成员意指颜色平等——寓言和神话分享奥普斯的旗舰色,因为他们分享它的道(英语:
    // Fail and Mythos share Opus). catalog.hpp 将它们
    // 分组 is_flagship()).
    CHECK(decode("claude-fable-5").color  == decode("claude-opus-4-8").color);
    CHECK(decode("claude-mythos-5").color == decode("claude-opus-4-8").color);

    // 未知家庭仍然有颜色——永远不缺省 -constructed 一种
    // 会 渲染 视之为无形或错误 -themed 甘露(变种).
    CHECK(decode("some-local-model:7b").color == maya::Color::slot(S::Info));

    // 每一个家庭都跟着这个主题。
    // 这就是用来打破的字面文字: 选择一个方案,
    // 徽章随它移动,而不是保留它编译的颜色。
    maya::Theme t = maya::theme::native;
    t.accent = maya::Color::rgb(0xBD, 0x93, 0xF9);
    CHECK(t.resolve(decode("claude-opus-4-8").color) == t.accent);
}

// ── 伟大的退化:我们从未见过的模型 ───────────────────
//
// 解码器最严格的要求是我们今天所发的识别码上不正确——上面
// 的识别码是那些识别码。 不 被看见的退化到同样的 SHAPE ,
// 因此,在这个建筑之后推出的模型看起来是本地的,
// 而不是看起来是破损的。用户会遇到一个品牌 -new
// 旗舰是最糟糕的时刻 UI 开始 渲染 原始的,
// 将使用 传输 标识或不一致的打字法。
TEST_CASE("model_name: an unknown family renders like a known one") {
    using agentty::model_name::decode;

    // A FUTURE Anthropic 家庭,在任何人加入之前几个月 枚举.
    // 它的形体一定与我们所认识的家庭是分不开的:
    // 供应商 -free ,版本分拆, 快照 掉下来了
    CHECK(decode("claude-quasar-6").medium()            == "Quasar 6");
    CHECK(decode("claude-quasar-6-2-20260114").medium() == "Quasar 6.2");
    CHECK(decode("claude-nova-7-1[1m]").full()          == "Nova 7.1 \xc2\xb7 1M");

    // 形状匹配 a KNOWN 家庭的完全——同一个
    // 字段分割,同样的没有卖家前缀。这是使家庭增加财产的属性。
    // 枚举 优化而不是错误修复。
    const auto known   = decode("claude-opus-4-8");
    const auto unknown = decode("claude-quasar-6-8");
    CHECK(known.version   == "4.8");
    CHECK(unknown.version == "6.8");
    CHECK(known.name      == "Opus");
    CHECK(unknown.name    == "Quasar");
    CHECK(known.family_known);
    CHECK(!unknown.family_known);   // ...它仍然在结构上解码

    // 其他供应商也得到同样的待遇。
    CHECK(decode("gemini-4-pro").medium() == "Gemini 4 Pro");
    CHECK(decode("grok-5-fast").medium()  == "Grok 5 Fast");

    // 棚梯子 WORKS 在结构解码之前,
    // 它们为三个组都生产了一个平弦,即: -op
    // 名字最长的地方 和剪辑关系最密切
    CHECK(decode("gemini-4-pro").tiny() == "Gemini 4");
    CHECK(decode("gemini-4-pro").tiny() != decode("gemini-4-pro").medium());
}

TEST_CASE("model_name: no vendor prefix ever reaches the screen") {
    using agentty::model_name::decode;

    // 供应商和 PROVIDER 独立轴( 代理驾驶员服务) Claude,
    // GPT 财务报告和已审计财务报表 Gemini 供应商已经从
    // 注册表 逐行逐行 标签 模型名称中的供应商前缀充其量是多余的,
    // 最糟糕的是,是一种谎言—— "Claude Opus 4.8" 副
    // 驾驶 标签 请您在对您说话时所读的错读 Anthropic.
    for (std::string_view id : {"claude-opus-4-8", "claude-quasar-6",
                                "anthropic/claude-sonnet-4-5",
                                "openrouter/anthropic/claude-opus-4",
                                "openai/gpt-4o-mini"}) {
        const auto s = decode(id).full();
        CHECK_MESSAGE(s.find("Claude") == std::string::npos,
                      "vendor prefix leaked for \"" << std::string(id)
                      << "\": \"" << s << "\"");
        CHECK_MESSAGE(s.find("Anthropic") == std::string::npos,
                      "vendor leaked for \"" << std::string(id) << "\"");
        CHECK(s.find('/') == std::string::npos);   // namespace never shown
    }

    // .但一个真正的模特儿. NAMED 喜欢卖家保留它的名字。
    // LEADING 卖家的代币被剥光, 并且只有精确的匹配。
    CHECK(decode("gemma2:9b").name.starts_with("Gemma"));
}

TEST_CASE("model_name: adversarial ids never crash or blank") {
    using agentty::model_name::decode;

    // ID 由提供者 API 和用户配置 —— 既不可靠也不可靠 -formed。
    // 其中每一项必须产生 SOMETHING 已经可以了
    for (std::string_view id : {"-", "---", ":", ":::", "[1m]", "claude",
                                "claude-", "-claude-", "4-5", "1.2.3.4.5",
                                "::latest", "a", "/", "//", "x/y/",
                                "20250101", "openai/", "model:tag:extra"}) {
        const auto n = decode(id);
        const auto s = n.full();
        // 总量:预测从不投球,也从不不同意筑巢,
        // 无论输入多少.
        CHECK_MESSAGE(n.medium().starts_with(n.tiny()),
                      "nesting broke on \"" << std::string(id) << "\"");
        CHECK_MESSAGE(s.starts_with(n.medium()),
                      "nesting broke on \"" << std::string(id) << "\"");
        // 没有 agentty-internal 标记逃到屏幕上。
        CHECK(s.find("[1m]") == std::string::npos);
        CHECK(s.find("[2m]") == std::string::npos);
    }

    // 病理身份标识仍为名称 SOMETHING 而不是说 渲染 空白 — 一个空
    // 模型 标签 读作错误,而不是 "unknown model".
    CHECK(!decode("---").name.empty());
    CHECK(!decode("claude").name.empty());
    CHECK(!decode("4-5").name.empty());
}

// ── 敌对输入: id不可信 ─────────────────────────────────
//
// Model ID从提供者API和用户配置中运抵, 并且两者都不可信。
// A `/v1/models` 反应是攻击者 -controlled 如果终点是(
// 自定义主机、 已损坏的聚合器、 typo) -squatted 代理),
// 并直接流到 a 终端 说 INTERPRETS 手握的是什么? 手握着的是什么?
TEST_CASE("model_name: no terminal escape ever reaches the screen") {
    using agentty::model_name::decode;

    // 来,来,来 ESC 字节 标签中 终端-injection 虫子,
    // 不是化妆品: "\x1b[31m" 将重新颜色 UI,
    // "\x1b[2J" 将它清除,光标 -movement 序列会腐蚀 帧
    // ——全部从字符串开始,用户从不 带类型的 中,每个 C0 控制及
    // 控制 DEL 被丢在解码器边界上,所以没有 下游 部件必须记住。
    for (std::string_view id : {"claude-opus\x1b[31m-4-8",
                                "gpt-\x07-4o",
                                "model\r\n-1",
                                "a\x1b]0;title\x07-2",
                                "x\x7f-3"}) {
        const auto s = decode(id).full();
        for (unsigned char c : s) {
            const bool printable = (c >= 0x20) && (c != 0x7F);
            CHECK_MESSAGE(printable,
                          "control byte " << int(c) << " survived decode");
        }
    }

    // 一张身份证,就是 ENTIRELY 控制 字节 解码
    // 为空而非垃圾——里面真的没有名字.
    CHECK(decode("\x1b\x07\r\n").name.empty());
}

TEST_CASE("model_name: output length is bounded") {
    using agentty::model_name::decode;

    // 不加限制的标签是拒绝向每个宽度-shed梯级下游(连锁脚印,
    // 状态栏,选择面板行)提供各种计量的服务.
    // ) 转角是VISIBLE——椭圆形,因此一个令人惊讶的标签
    // 读作"too long",而不是"wrong".
    const std::string huge(4000, 'x');
    for (const std::string& id : {huge,
                                  huge + "-4-5",
                                  std::string(500, 'a') + ":" + std::string(500, 'b')}) {
        const auto n = decode(id);
        CHECK(n.name.size()      <= 64);
        CHECK(n.qualifier.size() <= 64);
        CHECK(n.full().size()    <= 200);
        CHECK(!n.name.empty());          // 被束缚,但从未被空白
    }

    // 许多短的标志也都被捆绑着——封顶在OUTPUT上,
    // 所以它仍然保持着长度的到来.
    std::string many;
    for (int i = 0; i < 200; ++i) many += "tok-";
    CHECK(decode(many + "1").name.size() <= 64);

    // 通常的名字永远不能被截断——盖子决不能被合法模型所达到。
    // 我们所对付的最长的真名是~26个字符。
    CHECK(decode("qwen2.5-coder:32b-instruct").full().find("\xe2\x80\xa6")
          == std::string::npos);
}

TEST_CASE("model_name: UTF-8 survives intact") {
    using agentty::model_name::decode;

    // Sanitising在BYTE之前将C0控制下降,每UTF-8
    // 的续写字节是QQ 0x80,所以多-byte文本通过未受限制.
    // 服务于非-ASCII模型名称的提供者必须不能将其擦擦.
    CHECK(decode("mod\xc3\xa8le-4-5").full()  == "Mod\xc3\xa8le 4.5");
    CHECK(decode("\xe6\xa8\xa1\xe5\x9e\x8b-4-5").full()
          == "\xe6\xa8\xa1\xe5\x9e\x8b 4.5");

    // 截流面积在 UTF -8个边界——切中 -sequence
    // 将释放替换的格莱弗或解同步 渲染器 宽度核算。
    // 构建一个长的纯度 -multi-byte 姓名和检查结果还不错
    // -formed UTF-8.
    std::string wide;
    for (int i = 0; i < 100; ++i) wide += "\xe6\xa8\xa1";   // 模
    const auto s = decode(wide).full();
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        if      (c < 0x80)        len = 1;
        else if ((c >> 5) == 0x06) len = 2;
        else if ((c >> 4) == 0x0E) len = 3;
        else if ((c >> 3) == 0x1E) len = 4;
        const bool valid_lead = (len != 0);
        CHECK_MESSAGE(valid_lead, "truncation produced an invalid lead byte");
        if (!valid_lead) break;
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) {
            const bool cont =
                (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
            CHECK(cont);
        }
        i += len;
    }
    CHECK(i == s.size());   // 完全消耗——无部分粘合剂
}

// ── 自己 -hosted 网关: 路由命名空间是 IDENTITY ──────────────
//
// 报告这一情况的实际设置( gitlab.com/r3xxar/fw16-ai-inference
// : 8090前两端的Bifrost网关 llama.cpp 服务器和 a CPU 车道。
// Model IDs 名称空格由 LANE — `dgpu/…` 打开一个 RX 7700S, `igpu/…`
// 780M的雷达上 `utils/…` 打开 CPU.
//
// 解码器会丢掉一个主名空间,因为对主机提供者来说,
// 它就是噪音( `openrouter/anthropic/claude-…`
// 但它能为它服务 相同 重量,然后两行 渲染 同样的,
// 同样的标签, 疯狂的机器。 README
// 文档在网关上使用一个别名表,客户端显示的
// 怪异不应计算任何人的费用。
TEST_CASE("model label: a routing namespace is kept as a fact") {
    using agentty::model_name::decode;

    // 平面标签不变——普通案件保持清白.
    CHECK(decode("igpu/laguna-xs-2.1", "").full() == "Laguna Xs 2.1");
    CHECK(decode("dgpu/granite-4.2-3b", "").full() == "Granite 4.2 3b");

    // ...但车道被保留了 所以需要拆散的呼叫者可以
    CHECK(decode("igpu/laguna-xs-2.1", "").ns == "igpu");
    CHECK(decode("dgpu/granite-4.2-3b", "").ns == "dgpu");
    CHECK(decode("utils/granite-4.0-h-tiny", "").ns == "utils");

    // 没有任何命名空间的主机代号保留着空的代号—— qualified()
    // 降解为 full() ,所以99%的案件没有变化。
    CHECK(decode("gpt-4o", "").ns.empty());
    CHECK(decode("gpt-4o", "").qualified() == decode("gpt-4o", "").full());

    // 多个 -segment : 数据 NEAREST 前缀区分,而不是整个路径。
    // `openrouter/anthropic/x`由人类生产,
    // 通过开放路ter到达;制作者是两行相隔的分量.
    CHECK(decode("openrouter/anthropic/claude-3-5-haiku", "").ns == "anthropic");
}

TEST_CASE("model label: same weights on two lanes are distinguishable") {
    using agentty::model_name::decode;

    // 该 BUG. 同一型号,两车道:一条8GB离散的GPU和CPU.
    // 在此之前,两车都使"Granite 4.0 H Tiny"和
    // 选择面板两车道两车道显示两列相同的两台机型.
    const auto dgpu = decode("dgpu/granite-4.0-h-tiny", "");
    const auto util = decode("utils/granite-4.0-h-tiny", "");

    CHECK(dgpu.full() == util.full());            // 仍然明显地碰撞...
    CHECK(dgpu.qualified() != util.qualified());  // ...但现在可以分开了
    CHECK(dgpu.qualified() == "dgpu/Granite 4.0 H Tiny");
    CHECK(util.qualified() == "utils/Granite 4.0 H Tiny");
}
