// fused_models_test ——统一十字的纯排位核心. -provider 模型
// 选择面板。无 Model / 解析/ 网络: 我们手 build_fused_rows
// () 平整 目录 并坚持命令, 区, de -dup , 活动 pinning, 认证分拆。

#include "agentty/runtime/fused_models.hpp"
#include "agentty/domain/capkey.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <filesystem>

#include <doctest/doctest.h>

using namespace agentty;
using namespace agentty::ui;

namespace {

ModelInfo mk(std::string id, std::string name, std::string provider,
             int ctx = 200000, bool fav = false) {
    ModelInfo mi;
    mi.id = ModelId{std::move(id)};
    mi.display_name = std::move(name);
    mi.provider = std::move(provider);
    mi.context_window = ctx;
    mi.favorite = fav;
    return mi;
}

ProviderCatalog cat(std::string id, std::string label,
                    std::vector<ModelInfo> models) {
    ProviderCatalog c;
    c.provider_id = std::move(id);
    c.label = std::move(label);
    c.state = ProviderCatalog::State::Ready;
    c.models = std::move(models);
    return c;
}

} // 命名空间结束。

TEST_CASE("fused: active pinned first and marked, no query") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-sonnet-4-6", "Claude Sonnet 4.6", "anthropic"),
             mk("claude-opus-4", "Claude Opus 4", "anthropic")}),
        cat("openai", "OpenAI",
            {mk("gpt-5-codex", "gpt-5-codex", "openai", 400000),
             mk("gpt-4o", "gpt-4o", "openai", 128000)}),
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.active = ModelRef{"openai", "gpt-5-codex"};

    auto rows = build_fused_rows(in);
    REQUIRE(!rows.empty());
    // 活动封钉于行0,在RECENT区段,标记活动.
    CHECK(rows[0].provider_id == "openai");
    CHECK(rows[0].model.id.value == "gpt-5-codex");
    CHECK(rows[0].active);
    CHECK(rows[0].recent);
    // 活跃模式不得也出现在"all providers"部分.
    int count_codex = 0;
    for (auto& r : rows)
        if (r.provider_id == "openai" && r.model.id.value == "gpt-5-codex")
            ++count_codex;
    CHECK(count_codex == 1);
    // 彼此之间 目录 模型就存在一次
    CHECK(rows.size() == 4);
}

TEST_CASE("fused: query spans providers, ranks by match") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-sonnet-4-6", "Claude Sonnet 4.6", "anthropic")}),
        cat("openai", "OpenAI",
            {mk("gpt-5-codex", "gpt-5-codex", "openai"),
             mk("gpt-4o", "gpt-4o", "openai")}),
        cat("kimi", "Kimi", {mk("kimi-k2", "kimi-k2-0905", "kimi", 256000)}),
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.query = "gpt";

    auto rows = build_fused_rows(in);
    // 只有两个 GPT 行匹配 "gpt".
    CHECK(rows.size() == 2);
    for (auto& r : rows) {
        CHECK(r.provider_id == "openai");
        CHECK(r.model.id.value.find("gpt") != std::string::npos);
    }
}

TEST_CASE("fused: RECENT holds MRU order while browsing, not alphabetical") {
    // 部分说"recent",所以它必须按顺序排列。
    // 浏览用字母顺序排列它,这把10秒前你选择的模型 放在
    // 你一周前最后一次碰过的模型下面。
    //
    // 标签是选的,所以两个命令 DISAGREE :字母法 < 中
    // < 泽塔,但 MRU 是新来的,是阿尔法,是中区的。
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("m-active", "Active Model", "anthropic"),
             mk("m-zeta",   "Zeta",  "anthropic"),
             mk("m-alpha",  "Alpha", "anthropic"),
             mk("m-mid",    "Mid",   "anthropic")}),
    };
    std::vector<ModelRef> recents = {
        {"anthropic", "m-zeta"},     // 最近使用过的
        {"anthropic", "m-alpha"},
        {"anthropic", "m-mid"},      // 最小的最近时间
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.recents  = &recents;
    in.active   = ModelRef{"anthropic", "m-active"};

    const auto rows = build_fused_rows(in);   // 无查询=浏览
    REQUIRE(rows.size() >= 4);

    // 积极仍以建设为先导.
    CHECK(rows[0].model.id.value == "m-active");
    CHECK(rows[0].active);
    // 礛 MRU 秩序, 不 字母( 字母是α, 中, zeta)。
    CHECK(rows[1].model.id.value == "m-zeta");
    CHECK(rows[2].model.id.value == "m-alpha");
    CHECK(rows[3].model.id.value == "m-mid");
    for (int i = 0; i < 4; ++i)
        CHECK(rows[static_cast<std::size_t>(i)].recent);
}

TEST_CASE("fused: MRU section then all-providers, deduped") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-sonnet-4-6", "Claude Sonnet 4.6", "anthropic"),
             mk("claude-opus-4", "Claude Opus 4", "anthropic")}),
        cat("openai", "OpenAI", {mk("gpt-4o", "gpt-4o", "openai")}),
    };
    std::vector<ModelRef> recents = {
        {"openai", "gpt-4o"},
        {"anthropic", "claude-opus-4"},
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.recents = &recents;
    in.active = ModelRef{"anthropic", "claude-sonnet-4-6"};

    auto rows = build_fused_rows(in);
    // RECENT = 活动 + 2 MRU =3行,均为标注近期.
    int recent_rows = 0;
    for (auto& r : rows) if (r.recent) ++recent_rows;
    CHECK(recent_rows == 3);
    // ● LEADS, 始终 . 活性模型通过建筑被钉在0行.
    // (这一说法被短暂地颠倒,期望Opus先行,
    // 将回归编码为合同: 选择面板 预览 -selects 第0行,
    // 所以一个被替换的 \ 表示打开菜单时不再突出
    // 您实际使用的模型。 )
    CHECK(rows[0].model.id.value == "claude-sonnet-4-6");
    CHECK(rows[0].active);                     // 活动先被锁定
    // 针后面,最近的情况 MRU 命令 - 命令 `recents`
    // 上面列出的是gpt-4o,所以它引领了这里的奥普斯。
    // (它以前读作opus-first,因为浏览字母表将这个部分,
    // 而这个部分正是命令删除的重任错误. )
    CHECK(rows[1].model.id.value == "gpt-4o");
    CHECK(rows[2].model.id.value == "claude-opus-4");
    // 不同型号总数=3(RECENT和所有-providers之间没有二联体)。
    CHECK(rows.size() == 3);
}

TEST_CASE("fused: sign-in offers are QUERY-GATED — hidden while browsing") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-sonnet-4-6", "Claude Sonnet 4.6", "anthropic")}),
    };
    std::vector<SigninOffer> offers = {
        {"groq", "Groq"}, {"cerebras", "Cerebras"},
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.offers = &offers;

    // 空查询 = 浏览视图 : NO 提供行(列表保持干净;
    // 签名可到达 TYPING 提供者的名称,或通过 ^P).
    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].authed);

    // 一个匹配 un-authed 提供者的查询会显示它的提议—
    // —寻找一个没有添加的提供者永远不会是死胡同.
    in.query = "groq";
    rows = build_fused_rows(in);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].is_signin_offer());
    CHECK(rows[0].provider_id == "groq");
    // -in号报价没有型号标识。
    CHECK(rows[0].model.id.value.empty());
}

TEST_CASE("fused: query filters sign-in offers by provider name") {
    std::vector<ProviderCatalog> cats;
    std::vector<SigninOffer> offers = {{"groq", "Groq"}, {"cerebras", "Cerebras"}};
    FusedInputs in;
    in.catalogs = &cats;
    in.offers = &offers;
    in.query = "cere";

    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].provider_id == "cerebras");
    CHECK(rows[0].is_signin_offer());
}

TEST_CASE("fused: favorites float above equal-scoring peers") {
    std::vector<ProviderCatalog> cats = {
        cat("openai", "OpenAI",
            {mk("gpt-4o", "gpt-4o", "openai", 128000, /*发音=*/false),
             mk("gpt-5", "gpt-5", "openai", 400000, /*发音=*/true)}),
    };
    FusedInputs in;
    in.catalogs = &cats;
    // 没有查询QQ 全部与分数 0 匹配; 喜欢的必须先排序。
    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].model.favorite);
    CHECK(rows[0].favorite_section);
    CHECK(rows[0].model.id.value == "gpt-5");
}

TEST_CASE("fused: every favorite leads the list, across providers and ahead of recents (#99)") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-opus", "Opus", "anthropic"),
             mk("claude-haiku", "Haiku", "anthropic")}),
        cat("openai", "OpenAI",
            {mk("gpt-4o", "gpt-4o", "openai"),
             mk("gpt-5", "gpt-5", "openai")}),
    };
    // 星星生活在环境里,而不是在 目录 行: 无 -active
    // 供应商 目录 从来没有携带过 标志.
    std::vector<ModelId> favs = {ModelId{"gpt-5"}, ModelId{"claude-haiku"}};
    std::vector<ModelRef> recents = {ModelRef{"anthropic", "claude-opus"},
                                     ModelRef{"openai", "gpt-5"}};
    FusedInputs in;
    in.catalogs  = &cats;
    in.favorites = &favs;
    in.recents   = &recents;
    in.active    = ModelRef{"anthropic", "claude-opus"};
    auto rows = build_fused_rows(in);

    REQUIRE(rows.size() == 4);
    CHECK(rows[0].favorite_section);
    CHECK(rows[1].favorite_section);
    CHECK(rows[0].model.favorite);
    CHECK(rows[1].model.favorite);
    // 无论是哪家供应商都喜欢,先来。
    std::set<std::string> top = {rows[0].model.id.value, rows[1].model.id.value};
    CHECK(top == std::set<std::string>{"gpt-5", "claude-haiku"});
    // 近期的出现, 和一个明星近期 没有列出两次。
    CHECK(rows[2].recent);
    CHECK(rows[2].model.id.value == "claude-opus");
    CHECK(rows[2].active);
    CHECK_FALSE(rows[2].model.favorite);
    CHECK(rows[3].model.id.value == "gpt-4o");
    std::size_t gpt5 = 0;
    for (const auto& r : rows) gpt5 += r.model.id.value == "gpt-5";
    CHECK(gpt5 == 1u);
}

TEST_CASE("fused: a search keeps matching favorites on top (#99)") {
    std::vector<ProviderCatalog> cats = {
        cat("openai", "OpenAI",
            {mk("gpt-4o", "gpt-4o", "openai"),
             mk("gpt-4o-mini", "gpt-4o-mini", "openai")}),
    };
    std::vector<ModelId> favs = {ModelId{"gpt-4o-mini"}};
    FusedInputs in;
    in.catalogs  = &cats;
    in.favorites = &favs;
    in.query     = "gpt-4o";
    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].model.id.value == "gpt-4o-mini");
    CHECK(rows[0].favorite_section);
}

TEST_CASE("fused: a recent whose model left the catalog is dropped") {
    std::vector<ProviderCatalog> cats = {
        cat("openai", "OpenAI", {mk("gpt-4o", "gpt-4o", "openai")}),
    };
    std::vector<ModelRef> recents = {
        {"openai", "gpt-3.5-gone"},        // 不再提供
        {"openai", "gpt-4o"},
    };
    FusedInputs in;
    in.catalogs = &cats;
    in.recents = &recents;

    auto rows = build_fused_rows(in);
    // 只有静点 -present 近期生存; 过期 一只被悄悄地跳过
    CHECK(rows.size() == 1);
    CHECK(rows[0].model.id.value == "gpt-4o");
    CHECK(rows[0].recent);
}

TEST_CASE("fused: [1m] context variants are distinct rows, not alias dupes") {
    // 1M型机车 -context 变体 是一个 SEPARATE 从
    // 基本模式中选择,两者都必须可以列出。 归并 拼写化名但不得 归并
    // 编号 `[1m]` 标记( 3) capkey::norm_row_id
    // 数字 norm_model :后者将其剥离以进行能力检查,
    // 这使得每1M行看起来就像其基数的别名,并从列表中消失.
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-opus-4-8",     "Claude Opus 4.8", "anthropic"),
             mk("claude-opus-4-8[1m]", "Claude Opus 4.8 (1M Context)",
                "anthropic", 1'000'000),
             mk("claude-sonnet-4-6",   "Claude Sonnet 4.6", "anthropic")}),
    };
    ui::FusedInputs in;
    in.catalogs = &cats;
    auto rows = ui::build_fused_rows(in);

    bool base = false, one_m = false;
    for (const auto& r : rows) {
        if (r.model.id.value == "claude-opus-4-8")     base  = true;
        if (r.model.id.value == "claude-opus-4-8[1m]") one_m = true;
    }
    CHECK(base);
    CHECK(one_m);   // 回归: 默默地去除

    // 一个真正的别名 SPELLING 仍然脱落( 行为)
    // norm_model -3 -5和 -3.5是一排模特儿
    std::vector<ProviderCatalog> alias = {
        cat("mistral", "Mistral",
            {mk("mistral-medium-3-5", "Mistral Medium 3.5", "mistral"),
             mk("mistral-medium-3.5", "Mistral Medium 3.5", "mistral")}),
    };
    ui::FusedInputs in2;
    in2.catalogs = &alias;
    auto rows2 = ui::build_fused_rows(in2);
    int mistral_rows = 0;
    for (const auto& r : rows2)
        if (r.provider_id == "mistral") ++mistral_rows;
    CHECK(mistral_rows == 1);
}

// 浏览 -view 排名: 与 NO 询问列表的首端必须是您
// 可以合理选择的模型,而不是任意的片段, 任何提供者都会服务。
// Name
//
// 动机是集合器: OpenRouter-class 目录 贡献 ~
// 14 个行数 -row 可视区域 ,由提供者订购 -registry
// 仅是位置就意味着第一个屏幕是由用户所关心的事物决定的。
// 以能力级别(旗舰)排名(旗舰 ) 中线(便宜) 弱线(Capital
// chicks) 将可信的选择放在屏幕1上,因此滚动成为选择。
//
// 反面同样重要: QUERY 正在运行,模糊的分数是意图信号,
// 必须分级 不 内容 -rank 后面——否则用户
// 的行将瞄准在他们打字时的动作.
TEST_CASE("fused: browse ranks by tier, search stays relevance-ordered") {
    // 一家供应商,故意列出最弱者 -first 这么说吧 注册
    // 表 单凭命令就能把弱型放在顶端
    std::vector<ProviderCatalog> cats = {
        cat("openrouter", "OpenRouter",
            {mk("tinyllama:1b",        "TinyLlama 1B",   "openrouter"),
             mk("qwen2.5-coder:7b",    "Qwen2.5 Coder",  "openrouter"),
             mk("claude-haiku-4-5",    "Claude Haiku",   "openrouter"),
             mk("claude-sonnet-4-6",   "Claude Sonnet",  "openrouter"),
             mk("claude-opus-4-5",     "Claude Opus",    "openrouter")}),
    };

    // ── 浏览:本节内按型号标签字母顺序 ──────
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(rows.size() >= 5);
        // 收集浏览过的模型标签,确认它们按正常
        // 的字母顺序排列(类号使用的顶点为归并)。
        std::vector<std::string> labels;
        for (const auto& r : rows) {
            if (r.is_signin_offer()) continue;
            labels.push_back(r.model_label);
        }
        std::vector<std::string> sorted = labels;
        std::sort(sorted.begin(), sorted.end(),
                  [](const std::string& a, const std::string& b) {
                      return capkey::norm_row_id(a) < capkey::norm_row_id(b);
                  });
        INFO("browse list is ordered alphabetically, not by tier");
        CHECK(labels == sorted);
        // 具体地说:按标签字母顺序排列—— "Claude Haiku" 前置
        // 为 "Qwen2.5 Coder" 不论能力如何
        int haiku = -1, qwen = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (rows[static_cast<std::size_t>(i)].model.id.value == "claude-haiku-4-5")  haiku = i;
            if (rows[static_cast<std::size_t>(i)].model.id.value == "qwen2.5-coder:7b") qwen  = i;
        }
        REQUIRE(haiku >= 0);
        REQUIRE(qwen  >= 0);
        INFO("labels sort alphabetically: \"Claude Haiku\" before \"Qwen2.5 Coder\"");
        CHECK(haiku < qwen);
    }

    // ── 搜索: 关联性获胜, 后面没有字母顺序的 re-rank ────
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        in.query = "coder";           // 仅匹配弱的本地模式
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        INFO("a query's best match leads, regardless of its tier");
        CHECK(rows.front().model.id.value == "qwen2.5-coder:7b");
    }

    // ── 喜欢的成绩仍然高于一切,在两种模式中 ────────────
    {
        std::vector<ProviderCatalog> favc = {
            cat("openrouter", "OpenRouter",
                {mk("tinyllama:1b",    "TinyLlama 1B", "openrouter", 8000, true),
                 mk("claude-opus-4-5", "Claude Opus",  "openrouter")}),
        };
        ui::FusedInputs in;
        in.catalogs = &favc;
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        INFO("an explicit favorite outranks tier — the user already chose");
        CHECK(rows.front().model.id.value == "tinyllama:1b");
    }

    // ── 电话 活动 模型仍然可以在 RECENT ────────────
    // 顺序更改仅适用于浏览区块;
    // 最近区块的含义(和活动的标志)不得被阶层所扰动.
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        in.active = ModelRef{"openrouter", "tinyllama:1b"};   // 弱但活跃
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        INFO("the active model still leads the list");
        CHECK(rows.front().active);
        INFO("tier ranking does not evict the active model from the top");
        CHECK(rows.front().model.id.value == "tinyllama:1b");
    }

    // 即使存在竞争的最近使用项，● 标记仍排在前面。
    // 以上案件(和 "active pinned first"
    // 每个模型都有活性模型作为 仅 最近,所以浏览类型没有
    // 重新排序,而且无论是否针真的持有,都通过。
    // 盲点可以让字母顺序 RECENT 将活动行落在任何更早的
    // -sorting 最近从同一个提供者。
    //
    // 因此: 给活跃的提供者 两个 最近的版本, 并按字母顺序将
    // 活跃的提供者 作为 最后 的。 任何将 `active`
    // 当作只是另一种键的顺序失败的命令, 只有一个不能到达的密针。
    {
        std::vector<ProviderCatalog> two = {
            cat("anthropic", "Anthropic",
                {mk("claude-opus-4-5",  "Claude Opus 4.5",  "anthropic"),
                 mk("claude-haiku-4-5", "Claude Haiku 4.5", "anthropic")}),
        };
        // "Claude Haiku" < "Claude Opus" 字母顺序,
        // 海库是新人 MRU 输入——两条订单都放在首位.
        std::vector<ModelRef> recents{
            ModelRef{"anthropic", "claude-haiku-4-5"},
            ModelRef{"anthropic", "claude-opus-4-5"},
        };
        ui::FusedInputs in;
        in.catalogs = &two;
        in.recents  = &recents;
        in.active   = ModelRef{"anthropic", "claude-opus-4-5"};
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        INFO("● leads RECENT even when another recent sorts before it");
        CHECK(rows.front().active);
        CHECK(rows.front().model.id.value == "claude-opus-4-5");
        // ...还有其他的 RECENT 仍然在针后面按字母顺序排列。
        std::vector<std::string> rest;
        for (const auto& r : rows)
            if (r.recent && !r.active) rest.push_back(r.model_label);
        std::vector<std::string> sorted_rest = rest;
        std::sort(sorted_rest.begin(), sorted_rest.end(),
                  [](const std::string& a, const std::string& b) {
                      return capkey::norm_row_id(a) < capkey::norm_row_id(b);
                  });
        INFO("the pin does not disturb alphabetical order below it");
        CHECK(rest == sorted_rest);
    }

    // ── FILTERING 保留 MRU 包括了针头 ──────────────────────
    // 字母顺序是 BROWSE 付得起。 有了查询, 列表是相关的
    // -ordered ,和 RECENT 留下来 MRU ——断言
    // 针逻辑没有渗入过滤路径.
    {
        std::vector<ProviderCatalog> two = {
            cat("anthropic", "Anthropic",
                {mk("claude-opus-4-5",  "Claude Opus 4.5",  "anthropic"),
                 mk("claude-haiku-4-5", "Claude Haiku 4.5", "anthropic")}),
        };
        std::vector<ModelRef> recents{
            ModelRef{"anthropic", "claude-haiku-4-5"},
            ModelRef{"anthropic", "claude-opus-4-5"},
        };
        ui::FusedInputs in;
        in.catalogs = &two;
        in.recents  = &recents;
        in.active   = ModelRef{"anthropic", "claude-opus-4-5"};
        in.query    = "claude";
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        INFO("the active model still leads RECENT while filtering");
        CHECK(rows.front().active);
    }
}

// 浏览分为两个标题部分——"recent"和"all providers"
// ——第二字母顺序,由第EVERY供应商的模型标签排列(活动供应商
// 不再细分为自己的第"from this provider"块)。
// 电话 活动 提供者名称 最后 以字母顺序证明它 不 被身份
// 所左右 被其他的都压在了头顶上
TEST_CASE("fused: browse is recent, then a flat all-providers list") {
    std::vector<ProviderCatalog> cats = {
        cat("zeta", "ZetaCo",
            {mk("zeta-2",  "Zeta Two",  "zeta"),
             mk("zeta-1",  "Zeta One",  "zeta")}),
        cat("alpha", "AlphaCo",
            {mk("alpha-2", "Alpha Two", "alpha"),
             mk("alpha-1", "Alpha One", "alpha")}),
        cat("mid",   "MidCo",
            {mk("mid-1",   "Mid One",   "mid")}),
    };
    std::vector<ModelRef> recents = {ModelRef{"alpha", "alpha-1"}};
    ui::FusedInputs in;
    in.catalogs = &cats;
    in.recents  = &recents;
    in.active   = ModelRef{"zeta", "zeta-1"};   // 活动提供者 = "zeta"(最后!)
    in.recent_cap = 5;

    const auto rows = ui::build_fused_rows(in);

    // 第1节——最近: 活动 模型首先被固定(并标注),
    // MRU 列随行。 活动模式为 de -duped 由自己的
    // 供应商提供 目录 下面,所以不再重复。
    REQUIRE(rows.size() >= 2);
    CHECK(rows[0].recent);
    CHECK(rows[0].active);
    CHECK(rows[0].provider_id == "zeta");
    CHECK(rows[0].model.id.value == "zeta-1");
    CHECK(rows[1].recent);
    CHECK(rows[1].provider_id == "alpha");

    // 第2节—— "all providers" : 其余所有非 -recent 列,
    // 按型号标签字母顺序 ACROSS zeta-1(活性)和α-1(最近)在以上消耗,
    // 所以: "Alpha Two" < "Mid One" < "Zeta Two".
    std::vector<const FusedRow*> browse;
    for (const auto& r : rows)
        if (!r.recent && !r.is_signin_offer()) browse.push_back(&r);
    REQUIRE(browse.size() == 3);
    CHECK(browse[0]->model.id.value == "alpha-2");
    CHECK(browse[1]->model.id.value == "mid-1");
    CHECK(browse[2]->model.id.value == "zeta-2");   // 活动提供者 不 已锁定

    // 所有非-recent列是近代列后的一个毗连区块.
    int last_recent = -1, first_browse = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        if (r.recent) last_recent = i;
        if (!r.recent && !r.is_signin_offer() && first_browse < 0)
            first_browse = i;
    }
    REQUIRE(first_browse >= 0);
    CHECK(last_recent < first_browse);
}

// 第-layout号算术:第选择面板号算法对三列——
//
//   行布局：提供方留白、● 标记、模型名、右对齐上下文、收藏标记、附加标记。
//
// maya的选择面板文件,
// 召客必须"pad badges to a common width for column alignment",
// 而这个选择面板没有,因此,提供商的标签运行3.14字符,
// 每个型号NAME开始于不同的栏目. 在FLAT的交叉-provider列表中,
// 提供商徽章是分组信号,所以一个挂号的徽章柱会挫败整个布局——你不能垂直扫描.
//
// 上下文窗口为 MEASUREMENT 说得没错 -aligned
// 改为固定的 5 -col 字段;在它之后,两个标记占据了
// 固定的槽,而后一个行缺少一个最爱的孔,而不是向左滑动它的QQ(
// 这就是在滚动时使列表变得焦躁).
//
// 测试会将视图的算术固定在它上。 它会刻意地进行 不
// 渲染 它复制了来自 ui::fused_picker
// 在那里没有变化的改变 被抓住了
TEST_CASE("fused: row columns align") {
    // ── 徽章贴垫 ────────────────────────────────────────────────
    auto pad_badge = [](const std::string& label, std::size_t w) {
        return label.size() < w ? label + std::string(w - label.size(), ' ')
                                : label;
    };
    {
        // 宽度是目前最宽的标签,被夹在12块上.
        const std::vector<std::string> labels = {
            "Groq", "Anthropic", "GitHub Copilot", "OpenRouter"};
        std::size_t w = 0;
        for (const auto& l : labels) w = std::max(w, l.size());
        w = std::min<std::size_t>(w, 12);
        CHECK(w == 12);   // "GitHub Copilot" 14 ~ 被夹住

        // 每个加法徽章的宽度相同,所以下一列开始为
        // 每行一个固定的偏移.
        std::size_t first = pad_badge(labels.front(), w).size();
        for (const auto& l : labels) {
            const auto b = pad_badge(l, w);
            CHECK(b.size() == std::max(first, l.size()));
        }
        // 一个短标签真的是加的( 这是正在修复的错误 )。
        CHECK(pad_badge("Groq", w) == "Groq        ");
        // 结束了 -long 标签单独存在; maya 截断溢出.
        CHECK(pad_badge("GitHub Copilot", w) == "GitHub Copilot");
    }

    // ── 上下文窗口对 -aligned 5 中,将 -column 字段 ─────────
    auto ctx_field = [](int win) {
        std::string ctx;
        if (win > 0) {
            if (win >= 1'000'000) {
                ctx = std::to_string(win / 1'000'000) + "M";
                if (win % 1'000'000 != 0) ctx += "+";
            } else if (win >= 1000) {
                ctx = std::to_string(win / 1'000) + "k";
            } else {
                ctx = std::to_string(win);
            }
        }
        return ctx.size() < 5 ? std::string(5 - ctx.size(), ' ') + ctx : ctx;
    };
    {
        CHECK(ctx_field(200000) == " 200k");
        CHECK(ctx_field(128000) == " 128k");
        CHECK(ctx_field(8000)   == "   8k");
        CHECK(ctx_field(1000000)== "   1M");
        CHECK(ctx_field(1500000)== "  1M+");
        CHECK(ctx_field(0)      == "     ");   // 未知: 空白, 仍对齐
        // 每个字段都正好是5宽 数字共用一栏
        for (int w : {200000, 128000, 8000, 1000000, 1500000, 0})
            CHECK(ctx_field(w).size() == 5);
    }

    // ── 占用固定位置的标记 ────────────────────────────────────
    auto marks = [](bool fav, bool reasons) {
        std::string t;
        t += fav ? "  \xe2\x98\x85" : "   ";
        t += reasons ? " \xe2\x9c\xa6" : "  ";
        return t;
    };
    {
        // DISPLAY COLUMNS ,没有 字节 ++++ 和
        // +++为 3 -byte UTF -8个序列 一个 列,各栏,
        // 所以 字节 与首选的行和未选的行之间有合理的差异——比较
        // 早先版本的测试 find() 经常预算: "failed"
        // 必须在正确的布局上进行标记。 COLUMN.
        auto cols = [](std::string_view t) {
            int n = 0;
            for (unsigned char c : t)
                if ((c & 0xC0) != 0x80) ++n;   // 计数无 -continuation 字节
            return n;
        };
        // 每个组合总共占5个栏: "  ★" / "   " 3, 将它变成一个小的
        // " ✦" / "  " is 2.
        CHECK(cols(marks(true,  true))  == 5);
        CHECK(cols(marks(true,  false)) == 5);
        CHECK(cols(marks(false, true))  == 5);
        CHECK(cols(marks(false, false)) == 5);
        // 因此,QQ从第3栏开始,不管QQ是否在场—
        // —它永远不能滑入最喜爱的槽.
        auto star_slot = [&](bool fav) { return cols(std::string{marks(fav, false)}); };
        CHECK(star_slot(true) == star_slot(false));
    }
}

// 徽章栏的缩放 终端。固定夹子在两个方向上都是错误的:在狭义
// 的分割中太贪婪(14列重复每行的供货商)使模型饿死 NAME ),
// 这是用户读到的,在宽屏幕上过于紧凑(运行中)
// "GitHub Copilot" 改为 "GitHub Copil"
// 等有充足的空间的时候。 选择面板 通常用于拆分 面板 ,
// 所以这不是假设。
TEST_CASE("fused: badge column scales with terminal width") {
    // 镜像 panel_badge_max_cols ()规则:
    // 在非常狭窄的地带上 终端 徽章是缩写硬的; 任何地方,
    // 它都可能成长为符合最长的实际提供者标签, 但从未消耗超过一半
    // 的 选择面板 水平空间,所以模型 NAME 栏仍可使用。
    auto badge_max = [](int cols) {
        if (cols <= 40) return 8;
        const int cap = cols / 2;
        return cap < 8 ? 8 : cap;
    };

    CHECK(badge_max(60)  == 30);    // 60-col 拆分 − 上限 = 30(为8)
    CHECK(badge_max(80)  == 40);   // 经典的终端 = 40(为 12)
    CHECK(badge_max(120) == 60);   // 宽 = 60(为 16)
    CHECK(badge_max(40)  == 8);    // 微小的, 微小, 微细的 -terminal 楼层

    // 单调递增的 体制:扩大 终端 永远不产生 NARROWER
    // 徽章栏。
    int prev = 0;
    for (int c : {40, 60, 80, 100, 120, 200}) {
        const int w = badge_max(c);
        CHECK(w >= prev);
        prev = w;
    }

    // 倒退:共享 -hostname 自定义提供者( E)
    // ollama.com 关键 ollama.com 曾经超过固定的16
    // 个 -col 盖顶并打破模型列的对齐。动态盖必须在任意一个上接受
    // 终端 宽到足够 选择面板 用于(++++48 cols).
    CHECK(badge_max(80) >= 24);    // "ollama.com [seaventures]"为24
    CHECK(badge_max(80) >= 21);    // "ollama.com [liferaft]"为 21
    // 并且缩写为,不允许吃这排,在 phone/SSH 面板.
    CHECK(badge_max(40) <= 8);
    // 最大范围的真正预设标签 仍然适合一旦有空间。
    CHECK(badge_max(120) >= 14);   // "GitHub Copilot"
}

// 共享-hostname固定(承诺
// 通过 #tag 区分自定义主机的条目。
// refresh_fused_sources()在其选择面板标签中显示已保存宿主的
// "#tag",因此在一个宿主名上记着不同的字号,这个镜像完全是生的已保存谱,
// 标签是它产生的provider_display_name(),并给决定何时附加
// " [tag]"的护卫钉上针.
TEST_CASE("fused: custom-host #tag disambiguates the picker label") {
    // 与非同寻常的规则 -preset 分支在 refresh_fused_sources().
    auto derive = [](std::string_view id, std::string label) {
        if (auto h = id.rfind('#'); h != std::string_view::npos
                                    && h + 1 < id.size()) {
            const std::string tag{id.substr(h + 1)};
            if (label.find('#' + tag) == std::string::npos)
                label += " [" + tag + "]";
        }
        return label;
    };

    // Host-form 规格 : provider_display_name ()返回裸宿主,
    // 抛下碎片——所以标记必须是重新 -exposed ,一个主机上的两个
    // 账户最终 DISTINCT 标签。
    CHECK(derive("ollama.com#main",        "ollama.com") == "ollama.com [main]");
    CHECK(derive("ollama.com#seaventures", "ollama.com") == "ollama.com [seaventures]");
    CHECK(derive("ollama.com#main", "ollama.com")
          != derive("ollama.com#work", "ollama.com"));

    // URL-form 光谱保存 "#tag" 标签中已打逐字记录;
    // 不要加倍 -tag 他们。
    CHECK(derive("https://ollama.com/v1#main", "ollama.com#main") == "ollama.com#main");

    // 警卫的钥匙 EXACT "#tag" 块,而不是标记的赤色下弦。
    // 其文字也出现在 HOST 还必须置于括号内,
    // 否则 "main.ollama.com#main" 和兄弟的账户相撞
    CHECK(derive("main.ollama.com#main", "main.ollama.com") == "main.ollama.com [main]");
    CHECK(derive("main.ollama.com#main", "main.ollama.com")
          != derive("main.ollama.com#work", "main.ollama.com"));

    // 脱源光谱:没有碎片,或线索 '#' 带有空标签的 ——
    // 标签未被移动( 无偏差) " []").
    CHECK(derive("ollama.com",  "ollama.com") == "ollama.com");
    CHECK(derive("ollama.com#", "ollama.com") == "ollama.com");
}

// 排行带有能力级,预计算。有两个消费者需要它 — 浏览器
// -mode 排序和视图(它使提供商的徽章如此强烈)
// -first 命令可以辨别,而不是解释)——而且两者都运行热:
// 参照方为O(n log n),视图每个可见行都触碰 帧.
// tier_for () 表示id,并运行若干次子字符串扫描,
// 从而解决 一次 每行,随行 `reasons` ,出于同样的原因。
TEST_CASE("fused: rows carry a precomputed capability tier") {
    std::vector<ProviderCatalog> cats = {
        cat("openrouter", "OpenRouter",
            {mk("claude-opus-4-5",  "Claude Opus",   "openrouter"),
             mk("claude-sonnet-4-6","Claude Sonnet", "openrouter"),
             mk("claude-haiku-4-5", "Claude Haiku",  "openrouter"),
             mk("tinyllama:1b",     "TinyLlama 1B",  "openrouter")}),
    };

    auto tier_of = [&](const std::vector<FusedRow>& rows, std::string_view id) {
        for (const auto& r : rows)
            if (r.model.id.value == id) return static_cast<int>(r.tier);
        return -1;
    };

    // ── 喷洒在 全部-PROVIDERS 行数 ─────────────────────────
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        const auto rows = ui::build_fused_rows(in);
        CHECK(tier_of(rows, "claude-opus-4-5")   == 3);   // 旗舰
        CHECK(tier_of(rows, "claude-sonnet-4-6") == 2);   // 中间
        CHECK(tier_of(rows, "claude-haiku-4-5")  == 1);   // 便宜
        CHECK(tier_of(rows, "tinyllama:1b")      == 0);   // 弱点
    }

    // ── 正在使用时 FILTERING 也一样 ───────────────────────────────
    // 浏览时只需要分级,所以它曾经是计算在`no_query`.
    // 视图总是需要它 — — 一个过滤过的列表仍然会遮盖
    // 它的徽章 — — 因此查询不能在0时离开字段(它
    // 会把每个匹配都画成弱).
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        in.query = "opus";
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        CHECK(tier_of(rows, "claude-opus-4-5") == 3);
    }

    // ── 载荷 RECENT 行( 单独的构建路径) ────────────
    {
        std::vector<ModelRef> recents = {ModelRef{"openrouter", "claude-opus-4-5"}};
        ui::FusedInputs in;
        in.catalogs = &cats;
        in.recents  = &recents;
        const auto rows = ui::build_fused_rows(in);
        REQUIRE(!rows.empty());
        CHECK(rows.front().recent);
        CHECK(static_cast<int>(rows.front().tier) == 3);
    }

    // ── 水平在字母顺序的 re-order 存活( badge hue stay true) ──
    // 浏览不再按等级排列——按字母顺序排列——但徽章的
    // 色调仍然从预算的等级计算,因此旗舰和3B本地型号无论
    // 现在坐在哪里都必须保持它们的DISTINCT色调.
    // 确定预计算字段仍然与实时计算匹配。
    {
        ui::FusedInputs in;
        in.catalogs = &cats;
        const auto rows = ui::build_fused_rows(in);
        for (const auto& r : rows) {
            if (r.is_signin_offer()) continue;
            CHECK(static_cast<int>(r.tier) ==
                  static_cast<int>(ModelCapabilities::tier_for(r.model.id.value)));
            CHECK(!r.model_label.empty());             // 名字就在这里
        }
    }
}

// ── 选择面板 线条 -style 公约》缔约国(来源: -level 警卫) ────────────────────
//
// 三个规则 横跨每个 选择面板 ,这三项在本届会议之前
// 都遭到了侵犯:
//
//   1. 决 议 PRIMARY 标签 渲染 完全前方的无 -selected 排行数.
//      缩小每行但倒转一个等级:列表读作统一不可用,
//      而后缀引用 标签 退出( O) -shouts 名字,
//      你实际上选择。
//   2. TRAILING 存储单元是`trailing_secondary`,因此它产生空间FIRST
//      宽度压力下 maya 默认值相反( 对于像文件这样
//      的行) 选择面板 尾声, 后面的 存储单元 并继承了
//      短号模型名称以保存 "200k ★ ✦".
//   3. 徽标栏加插在 DISPLAY COLUMNS ,永远 字节 - 习惯
//      主机为用户 -named 并可能持有 CJK 或爱莫吉。
//
// 这些是观赏 -layer 样式常数,无 运行时 接口
// 边界 以反驳,所以这个读取源。 -shaped 检验标准是
// 不寻常的,但另一种办法是一种默默腐烂的公约——这正是规则
// 1和规则2在四之间发生的。 选择面板.
TEST_CASE("pickers: primary labels are not dimmed, trailing yields first") {
    // 电话 选择面板 查看活动包含在多个 . cpp 文件的目录下,
    // 阅读和整理它们全部, 这样会议守卫覆盖每个 选择面板.
    namespace fs = std::filesystem;
    std::string src;
    const fs::path dir{AGENTTY_PICKERS_SRC_DIR};
    REQUIRE_MESSAGE(fs::is_directory(dir), "no picker src dir: " << dir);
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (ent.path().extension() != ".cpp") continue;
        std::ifstream in(ent.path());
        REQUIRE_MESSAGE(in.good(), "cannot open " << ent.path());
        std::stringstream ss;
        ss << in.rdbuf();
        src += ss.str();
        src += '\n';
    }
    REQUIRE(!src.empty());

    auto count = [&](std::string_view needle) {
        std::size_t n = 0, pos = 0;
        while ((pos = src.find(needle, pos)) != std::string::npos) { ++n; ++pos; }
        return n;
    };

    // 第1条:不 选择面板 淡化其主标签于非 -active 树枝。
    // (`fg_dim(muted)` 时间: TRAILING 存储单元 正确,不匹配此处。 )
    CHECK(count("leading_style  = active ? fg_bold(fg) : fg_of(muted)") == 0);
    CHECK(count("leading_style = active ? fg_bold(fg) : fg_of(muted)") == 0);
    CHECK(count("leading_style  = is_current ? fg_bold(info) : fg_of(muted)") == 0);

    // 第2条:每 选择面板 显示线索 标签 标记它第二。
    // Model ,提供者, 线程 列表,命令调色板。
    CHECK(count("trailing_secondary = true") >= 4);

    // 第3条:徽章贴设措施栏,而非字节.
    CHECK(src.find("maya::string_width(r.label)") != std::string::npos);

    // NOTE : 浏览 "all providers" 节次 头文件 谓 不 此处为证明。
    // 盗取这个文件只会证明分支是 TYPED - 不是说
    // 它 渲染 或者它标题的行在屏幕上。
    // panel_sections_render_test 渲染 真实
    // 选择面板 读到 头文件 背靠帆布, 这绝对是更强壮。
}


// ── 衍生 -cache 一致性: 缓存 是一个优化,而不是叉子 ──
//
// 人均数 -keystroke 构建读取三个预计算大小 -guard-invalidated
// 缓存 打开 ProviderCatalog: search_keys (小的干草堆),
// , (小的草堆) , row_keys (capkey::norm_row_id 归并 ,
// 用于别名dadup)和 display_labels . 它们纯粹是为了速度而存在
// 的——450 -model 汇总列表为3.8ms → 每键键键为0.6ms,
// 所以 绑定 不变量 结果是 IDENTICAL 与无其人. 缓存 改变行为是错误。
namespace {
// 输入 a 目录 ' 来源 缓存 完全一样 rebuild_fused_rows (
// src/runtime/app/update/picker.cpp 已经。
void build_caches(ProviderCatalog& c) {
    c.row_keys.clear();
    c.display_labels.clear();
    for (const auto& mi : c.models) {
        c.row_keys.push_back(agentty::capkey::norm_row_id(mi.id.value));
        c.display_labels.push_back(
            mi.display_name.empty() ? mi.id.value : mi.display_name);
    }
}
std::vector<std::pair<std::string, std::string>> ids_of(
        const std::vector<FusedRow>& rows) {
    std::vector<std::pair<std::string, std::string>> v;
    v.reserve(rows.size());
    for (const auto& r : rows) v.emplace_back(r.provider_id, r.model.id.value);
    return v;
}
} // 命名空间结束。

TEST_CASE("fused: derived caches never change the result set") {
    std::vector<ProviderCatalog> cats = {
        cat("anthropic", "Anthropic",
            {mk("claude-sonnet-4-6", "Claude Sonnet 4.6", "anthropic"),
             mk("claude-opus-4", "Claude Opus 4", "anthropic")}),
        cat("openai", "OpenAI",
            {mk("gpt-5-codex", "gpt-5-codex", "openai", 400000),
             mk("gpt-4o", "gpt-4o", "openai", 128000)}),
    };
    std::vector<ModelRef> recents{{"anthropic", "claude-opus-4"}};
    FusedInputs in;
    in.catalogs = &cats;
    in.recents  = &recents;
    in.active   = ModelRef{"openai", "gpt-5-codex"};

    for (const char* q : {"", "gpt", "claude", "o", "zzz"}) {
        INFO("query = '" << q << "'");
        in.query = q;
        for (auto& c : cats) { c.row_keys.clear(); c.display_labels.clear(); }
        const auto bare = ids_of(build_fused_rows(in));
        for (auto& c : cats) build_caches(c);
        CHECK(ids_of(build_fused_rows(in)) == bare);
    }
}

TEST_CASE("fused: alias twins dedupe identically through the row_keys cache") {
    // A 目录 上面列出了两个拼写 一个 模型(真正的错
    // -medium-3-5 / -3.5 个例),完全必须浮出一行,
    // 无论 归并 来源于 缓存 或者被计算为活体。
    std::vector<ProviderCatalog> cats = {
        cat("aggr", "Aggr",
            {mk("mistral-medium-3-5", "Mistral Medium 3.5", "aggr"),
             mk("mistral-medium-3.5", "Mistral Medium 3.5", "aggr")}),
    };
    FusedInputs in;
    in.catalogs = &cats;

    CHECK(build_fused_rows(in).size() == 1);   // 现场直播 归并
    for (auto& c : cats) build_caches(c);
    CHECK(build_fused_rows(in).size() == 1);   // 缓存的归并
}

// ── Tool 能力是一个 RANKING 信号,不只是标签 ───────────────
//
// 一个提供商正面报告没有任何工具支持的模型根本无法驱动代理。
// 标签是必要但还不够的: 虽然BROWSING 也
// 必须沉到每个能够完成此任务的模型下面,
// 所以第一个屏幕是您可以实际选择的模型。 明确的搜索
// 仍然会立即浮出水面( 您要求它的名称 )。
TEST_CASE("fused: no-tools models sink below capable ones while browsing") {
    auto no_tools = [](ModelInfo mi) { mi.supports_tools = false; return mi; };
    std::vector<ProviderCatalog> cats = {
        cat("ollama", "Ollama",
            {no_tools(mk("llama3-chat", "llama3-chat", "ollama")),
             mk("qwen3-coder", "qwen3-coder", "ollama")}),
    };
    FusedInputs in;
    in.catalogs = &cats;

    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].model.id.value == "qwen3-coder");
    CHECK(rows[0].tool_capable);
    CHECK(rows[1].model.id.value == "llama3-chat");
    CHECK(!rows[1].tool_capable);

    // . 但命名它立刻发现它: 得分门比水槽高.
    // (查询选择通过提供者标签无法匹配——).
    // "Ollama" 本身包含 "llama" 将它作为次序列,
    // 两者 排在大海斯塔克的提供者部分上匹配. )
    in.query = "chat";
    auto hit = build_fused_rows(in);
    REQUIRE(!hit.empty());
    CHECK(hit[0].model.id.value == "llama3-chat");
}

TEST_CASE("fused: unknown tool support is treated as capable") {
    // 主办供应商不做广告;缺席绝不能被理解为 "cannot" ,
    // 或每个 OpenAI/Anthropic 列会被标记为聊天 -only.
    std::vector<ProviderCatalog> cats = {
        cat("openai", "OpenAI", {mk("gpt-4o", "gpt-4o", "openai")}),
    };
    FusedInputs in;
    in.catalogs = &cats;
    auto rows = build_fused_rows(in);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].tool_capable);
}

// 两个供应商上相同的型号代号是两个型号,主演是Groq的gpt -oss
// 千万别上星 OpenRouter's.
TEST_CASE("fused: a favorite belongs to one provider") {
    std::vector<ProviderCatalog> cats = {
        cat("groq", "Groq", {mk("gpt-oss-120b", "gpt-oss-120b", "groq")}),
        cat("openrouter", "OpenRouter",
            {mk("gpt-oss-120b", "gpt-oss-120b", "openrouter")}),
    };
    std::vector<ModelId> favs = {ModelId{favorite_key("groq", "gpt-oss-120b")}};
    FusedInputs in;
    in.catalogs  = &cats;
    in.favorites = &favs;
    int starred = 0;
    for (const auto& r : build_fused_rows(in))
        if (r.model.favorite) {
            ++starred;
            CHECK(r.provider_id == "groq");
        }
    CHECK(starred == 1);

    // 赤裸裸的进入 已保存 以古老的建筑为星,
    std::vector<ModelId> legacy = {ModelId{"gpt-oss-120b"}};
    in.favorites = &legacy;
    int legacy_starred = 0;
    for (const auto& r : build_fused_rows(in)) legacy_starred += r.model.favorite;
    CHECK(legacy_starred == 2);
}
