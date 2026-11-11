// modelsdev_parse_test - 电话 models.dev 装入器必须将
// 文档转换成完全相同的 目录 事实无论如何 JSON 解析器 用来
//
// 检查 -in 固定覆盖装填器读取的每个字段; 第二个大小
// 写贯穿真实 ~/.agentty 缓存 显示并打印
// a 指纹 每一个事实,所以 解析器 互换可以与完整实时
// 文档进行核对.

#include "agtest.hpp"

#include "agentty/domain/catalog.hpp"
#include "agentty/util/modelsdev.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace agentty;

namespace {

// 以按顺序排列的文本行排列的每个事实。 顺序 -independent ,
// 所以两个 解析器 以不同的方式对文档进行对比。
std::vector<std::string> facts_of(const CatalogBatch& b) {
    std::vector<std::string> out;
    for (const auto& [k, v] : b.reasoning.sets)   out.push_back("rs " + k + "=" + (v ? "1" : "0"));
    for (const auto& [k, v] : b.reasoning.merges) out.push_back("rm " + k + "=" + (v ? "1" : "0"));
    for (const auto& [k, v] : b.effort.sets)      out.push_back("es " + k + "=" + std::to_string(v));
    for (const auto& [k, v] : b.effort.merges)    out.push_back("em " + k + "=" + std::to_string(v));
    for (const auto& [k, v] : b.context.sets)     out.push_back("cs " + k + "=" + std::to_string(v));
    for (const auto& [k, v] : b.context.merges)   out.push_back("cm " + k + "=" + std::to_string(v));
    for (const auto& [h, m, e] : b.endpoints)
        out.push_back("ep " + h + " " + m + "=" + std::to_string(e.tokens) + ":" + std::to_string(e.port));
    std::sort(out.begin(), out.end());
    return out;
}

std::uint64_t fnv(const std::vector<std::string>& lines) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& l : lines) {
        for (unsigned char c : l) { h ^= c; h *= 1099511628211ull; }
        h ^= '\n'; h *= 1099511628211ull;
    }
    return h;
}

constexpr const char* kFixture = R"({
  "acme": {
    "env": ["ACME_KEY", "ACME_ALT"],
    "api": "https://api.acme.test:8443/v1",
    "models": {
      "acme/think-3.5": {
        "reasoning": true,
        "limit": {"context": 200000, "output": 8192},
        "reasoning_options": [
          {"type": "effort", "values": ["low", "medium", "high", "ultra"]}
        ]
      },
      "acme-toggle": {
        "reasoning": true,
        "reasoning_options": [{"type": "toggle"}]
      },
      "acme-both": {
        "reasoning": true,
        "reasoning_options": [{"type": "toggle"}, {"type": "effort", "values": ["high"]}]
      },
      "acme-plain": {"reasoning": false, "limit": {"context": 32000}},
      "acme-no-reasoning-field": {"limit": {"context": 1000}},
      "acme-bad-context": {"reasoning": true, "limit": {"context": -5}},
      "acme-huge-context": {"reasoning": true, "limit": {"context": 99999999999}},
      "acme-float-context": {"reasoning": true, "limit": {"context": 1.5}},
      "acme-string-reasoning": {"reasoning": "yes"},
      "acme-empty-effort": {"reasoning": true,
        "reasoning_options": [{"type": "effort", "values": []}]},
      "acme-weird-opts": {"reasoning": true,
        "reasoning_options": ["x", {"type": 3}, {"values": ["low"]}]}
    }
  },
  "noenv": {
    "models": { "solo": {"reasoning": true, "limit": {"context": 4096}} }
  },
  "badenv": {
    "env": [42], "api": 7,
    "models": { "solo2": {"reasoning": false} }
  },
  "notobject": 5,
  "nomodels": {"env": ["X"]},
  "modelsnotobject": {"models": []},
  "localhost": {
    "api": "http://localhost:1234/v1",
    "models": { "local-m": {"reasoning": true, "limit": {"context": 8192}} }
  }
})";

}  // 命名空间结束。

TEST_CASE("modelsdev parse: fixture yields the expected facts") {
    CatalogBatch b;
    const int n = modelsdev::parse_into(kFixture, b);
    const auto facts = facts_of(b);
    // 每个模型条目都是计算,奇怪的是 带类型的 包括那些内容。
    CHECK(n == 14);
    const std::vector<std::string> want = {
        // 从nlohmann装货机录到投出的条目(Acme)
        // -weird-opts ' 数字 "type").
        "cm acme-plain=32000",
        "cs dev:acme/acme-plain=32000",
        "em acme-both=4",
        "em acme-toggle=4",
        "ep api.acme.test acme-plain=32000:8443",
        "es dev:acme/acme-both=4",
        "es dev:acme/acme-toggle=4",
        "rm acme-bad-context=1",
        "rm acme-both=1",
        "rm acme-empty-effort=1",
        "rm acme-float-context=1",
        "rm acme-huge-context=1",
        "rm acme-plain=0",
        "rm acme-toggle=1",
        "rs dev:acme/acme-bad-context=1",
        "rs dev:acme/acme-both=1",
        "rs dev:acme/acme-empty-effort=1",
        "rs dev:acme/acme-float-context=1",
        "rs dev:acme/acme-huge-context=1",
        "rs dev:acme/acme-plain=0",
        "rs dev:acme/acme-toggle=1",
    };
    // 旧装机生产的一切,必须仍然生产,不变.
    for (const auto& w : want)
        CHECK_MESSAGE(std::find(facts.begin(), facts.end(), w) != facts.end(), "missing: " << w);
}

TEST_CASE("modelsdev parse: one malformed entry doesn't cost the document") {
    // nlohmann 投进了一个数字选项 "type" (和
    // 数字提供者) "api" 发现的炸弹已经丢弃了所有模型,
    // 现在,坏的田地被跳过,其他文件仍然在降落。
    CatalogBatch b;
    (void)modelsdev::parse_into(kFixture, b);
    const auto facts = facts_of(b);
    const auto has = [&](std::string_view s) {
        return std::any_of(facts.begin(), facts.end(),
                           [&](const std::string& f) { return f.find(s) != std::string::npos; });
    };
    CHECK(has("acme-weird-opts"));          // 带有错误选项的条目
    CHECK(has("think-3-5"));                // 之后的模型( 键已正常)
    CHECK(has("solo"));                     // 后期提供者
    CHECK(has("solo2"));                    // 提供方为数字API
    CHECK(has("local-m"));                  // 最后一个提供者
}

TEST_CASE("modelsdev parse: how each model turns reasoning off") {
    constexpr const char* doc = R"({
      "p": { "models": {
        "none-ok":   {"reasoning": true,  "reasoning_options": [{"type": "effort", "values": ["none", "low", "high"]}]},
        "toggle":    {"reasoning": true,  "reasoning_options": [{"type": "toggle"}]},
        "always":    {"reasoning": true,  "reasoning_options": [{"type": "effort", "values": ["low", "medium", "high"]}]},
        "both":      {"reasoning": true,  "reasoning_options": [{"type": "toggle"}, {"type": "effort", "values": ["low", "high"]}]},
        "bare":      {"reasoning": true},
        "plain":     {"reasoning": false}
      }}
    })";
    CatalogBatch b;
    (void)modelsdev::parse_into(doc, b);
    auto kind = [&](std::string_view id) -> int {
        for (const auto& [k, v] : b.reasoning_off.sets)
            if (k.ends_with("/" + std::string{id})) return v;
        return -1;
    };
    using R = ReasoningOff;
    CHECK(kind("none-ok") == int(R::EffortNone));
    CHECK(kind("toggle")  == int(R::Toggle));
    CHECK(kind("always")  == int(R::Impossible));
    CHECK(kind("both")    == int(R::Toggle));
    CHECK(kind("bare")    == -1);   // 未知:未记录
    CHECK(kind("plain")   == int(R::Omit));
}

TEST_CASE("reasoning off: picker never offers off on a model that can't stop") {
    ModelCapabilities caps;
    caps.effort_set_known = true;
    caps.effort_set = effort_bit(Effort::Low) | effort_bit(Effort::Medium) | effort_bit(Effort::High);
    caps.reasoning_off = static_cast<std::uint8_t>(ReasoningOff::Impossible);
    const auto ladder = available_efforts(caps);
    CHECK(std::find(ladder.begin(), ladder.end(), Effort::None) == ladder.end());
    // 夹子放在地板上 屏幕上 传输.
    CHECK(clamp_effort(Effort::None, caps) == Effort::Low);
    CHECK(effort_wire_for(Effort::None, caps) == "low");

    // 一个模型 CAN 别吵了
    caps.reasoning_off = static_cast<std::uint8_t>(ReasoningOff::Toggle);
    const auto l2 = available_efforts(caps);
    CHECK(std::find(l2.begin(), l2.end(), Effort::None) != l2.end());
    CHECK(effort_wire_for(Effort::None, caps).empty());
}

TEST_CASE("modelsdev parse: the real cache gives the same facts") {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::ifstream in(std::string{home} + "/.agentty/cache/modelsdev.json", std::ios::binary);
    if (!in) return;   // 此机上没有缓存:无可比较
    std::ostringstream ss;
    ss << in.rdbuf();
    CatalogBatch b;
    const int n = modelsdev::parse_into(ss.str(), b);
    const auto facts = facts_of(b);
    MESSAGE("models=" << n << " facts=" << facts.size() << " fnv=" << fnv(facts));
    CHECK(n > 0);
    // 指纹 的输出, 缓存 文档被录入到此选项。
    // 缓存 更改它, 所以只有文件是准确的(
    // 按大小) 时才会检查。
    if (ss.str().size() == 5623041) {
        CHECK(n == 8814);
        CHECK(facts.size() == 65355);
        CHECK(fnv(facts) == 11372656136564762570ull);
    }
}
