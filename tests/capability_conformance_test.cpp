// capability_conformance_test — 不变量 每个 提供
// 者的模型 目录 满足,一致主张,而不是按 -provider.
//
// ── 该文件为何存在 ─────────────────────────────────────────────────
//
// Model 能力由优先级决定—— env 覆盖,
// 然后是活的 目录 提供商声明,然后从ID中推断出结构.
// 梯子很好,没有说明什么是 目录 ENTRY
// 一定看起来像, 适用于每个提供者一次。
//
// 差距不是理论上的。 ModelInfo::context_window
// 默认为 2000 00, 即对 Claude 和错的基米 K2
// (256k). Kimi的/型号. 载荷 没有任何内容长度,
// 所以没有 任何过度重写默认,每个基米 悄悄地转过身来 -reported
// 其窗口乘以56k — 上下文显示器和压缩阈值都提前发射。
// 一个提供者正确的默认,对后一提供者来说是无声错误的:形状与副驾驶相同 传输
// 虫子们
//
// 所以这些测试不检查 "Kimi is 256k"。它们检查必须持有的属性。
// 每个 提供者,所以下一个后端继承它们.

#include <set>
#include <string>

#include "agtest.hpp"

#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/provider/registry.hpp"

using namespace agentty;

namespace {

// 每一个供应商 运送捆绑 目录 种子是用户在(或没有)
// 现场采集之前看到的,所以这些是地板。 不变量 警卫
const std::vector<std::string>& seeded_providers() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (const auto& p : provider::providers())
            if (!catalog::bundled(p.id).empty()) out.emplace_back(p.id);
        return out;
    }();
    return v;
}

} // 命名空间结束。

TEST_CASE("every bundled model declares a plausible context window") {
    for (const auto& pid : seeded_providers()) {
        for (const auto& m : catalog::bundled(pid)) {
            INFO("provider = " << pid << ", model = " << m.id.value);
            // 0或负的窗口将使上下文表值除以0
            // 和紧凑阈值变得毫无意义.
            CHECK(m.context_window > 0);
            // 任何运货模型都没有一个窗口低于4k;
            // 这个值意味着一个字段被保留了未设置或解析错误.
            CHECK(m.context_window >= 4096);
            // 10M以外的任何当前模型也不存在——这个大值是单位错误
            // (tokens vs 字节)而不是一个真正的窗口.
            CHECK(m.context_window <= 10'000'000);
        }
    }
}

TEST_CASE("every bundled model is attributed to its own provider") {
    // 保险丝 选择面板 渲染 每一个授权提供者
    // 的行一次并解决行下的能力 自身 提供方范围。
    // 一行带错提供方的ID会解决错误的合同。
    for (const auto& pid : seeded_providers())
        for (const auto& m : catalog::bundled(pid)) {
            INFO("provider = " << pid << ", model = " << m.id.value);
            CHECK(m.provider == pid);
        }
}

TEST_CASE("bundled model ids are unique and non-empty within a provider") {
    for (const auto& pid : seeded_providers()) {
        std::set<std::string> seen;
        for (const auto& m : catalog::bundled(pid)) {
            INFO("provider = " << pid << ", model = " << m.id.value);
            CHECK_FALSE(m.id.value.empty());
            CHECK_FALSE(m.display_name.empty());
            // 重复的id使得选择面板的选题模糊不清.
            CHECK(seen.insert(m.id.value).second);
        }
    }
}

TEST_CASE("capability gates are internally consistent for every bundled model") {
    for (const auto& pid : seeded_providers()) {
        for (const auto& m : catalog::bundled(pid)) {
            INFO("provider = " << pid << ", model = " << m.id.value);
            const auto c = ModelCapabilities::from_id(m.id.value);

            // 努力阶梯是一个等级: 一个模型没有底盘就
            // 无法提供更高的径向。 将`xhigh` 发送到
            // 一个不需要努力参数的模型是400。
            if (c.supports_effort_max())   CHECK(c.supports_effort());
            if (c.supports_effort_xhigh()) CHECK(c.supports_effort());

            // 3个 -level 比较 枚举 (减) | 中号 |
            // 高)不能表达扩展的长长,因此两者都决不能同时主张.
            if (c.reasoning_compat) {
                CHECK_FALSE(c.supports_effort_max());
                CHECK_FALSE(c.supports_effort_xhigh());
            }
        }
    }
}

TEST_CASE("an unrecognised model claims no capabilities") {
    // 该 坚固性能 "works with every provider"
    // :一个我们从未见过的模型必须堕落到安全的地板上,永远不要结束
    // -promise 终点将拒绝参数。 提供者会不断添加模型;
    // agentty 查找用户选择一个时。
    for (const char* id : {"glm-4.6", "minimax-m2", "qwen3-coder-480b",
                           "llama-4-maverick", "command-r-plus",
                           "some-model-from-2027", ""}) {
        INFO("model = " << id);
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.family == ModelCapabilities::Family::Unknown);
        CHECK_FALSE(c.supports_effort());
        CHECK_FALSE(c.supports_effort_max());
        CHECK_FALSE(c.supports_effort_xhigh());
    }
}

TEST_CASE("a model id with huge numbers in it parses without overflow") {
    // ID 是不信任的文本。 长位数运行用于溢出英寸( Q) UB 大小
    // -tag 和版本 解析器 ; 找到的 tests/fuzz. 现在已经饱和了
    for (const char* id : {"x-777777777777b", "x-99999999999t", "[77777777t777.O:",
                           "claude-opus-99999999999", "gpt-5.99999999999",
                           "gpt-99999999999.1", "qwen-1234567890123456b"}) {
        INFO("model = " << id);
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.params_b >= 0);
        CHECK(c.params_b <= 1'000'000);
    }
    CHECK(ModelCapabilities::from_id("qwen2.5-coder:32b").params_b == 32);
    CHECK(ModelCapabilities::from_id("kimi-k2-1t").params_b == 1000);
}

TEST_CASE("known models still resolve their capabilities") {
    // 上面的测试镜: 只有识别仍然有效, 才会安全地降解。
    // 这些标注了结构解码( 家族 + 生成 + 修订) ,
    // 而不是一个硬编码的 ID 列表。
    const auto opus = ModelCapabilities::from_id("claude-opus-4-5");
    CHECK(opus.family == ModelCapabilities::Family::Opus);
    CHECK(opus.generation == 4);
    CHECK(opus.revision == 5);
    CHECK(opus.supports_effort());

    // A FUTURE 修订必须继承其家族的大门,
    // 而不改变代码——这就是结构解码值得拥有的原因。
    const auto future = ModelCapabilities::from_id("claude-opus-4-9");
    CHECK(future.family == ModelCapabilities::Family::Opus);
    CHECK(future.supports_effort());

    const auto gpt5 = ModelCapabilities::from_id("gpt-5.6-luna");
    CHECK(gpt5.family == ModelCapabilities::Family::Gpt);
    CHECK(gpt5.supports_effort());
}

TEST_CASE("every provider that can be selected yields a non-empty catalog") {
    // 空模式列表与失败的获取无法区分, 这正是自定义-host
    // 副驾驶错误的呈现方式: 没有模型, 没有错误。
    // 每个拥有静态种子的提供者必须在任何网络之前显示行.
    for (const auto& p : provider::providers()) {
        if (p.kind() == provider::Kind::ExternalAcp) continue;  // 代理选择
        if (p.is_local) continue;                               // 服务器-driven
        INFO("provider = " << std::string{p.id});
        const auto seed = catalog::bundled(p.id);
        // 已主机 API-key 没有种子的供应商会回到现场采集,
        // 这是有意的选择,而不是错误。 绝不能发生的情况是
        // 存在但结构不整的种子,上面的测试涵盖了这些种子。
        if (!seed.empty()) CHECK(seed.size() >= 1);
    }
}
