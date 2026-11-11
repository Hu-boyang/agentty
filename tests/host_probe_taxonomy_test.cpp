// tests/host_probe_taxonomy_test.cpp — 为什么一个自定义主机失败, 以文字表示.
//
// 探测器的工作不是 "did it work" 不过
// "what do I tell the user to do next"。过去作为同一字符串出现的三个截然不同的错误:
//
//   200 + HTML 你粘贴了仪表板 URL ,而不是 API
//   基础 401/403 地址是: 地址是 RIGHT ,
//   它想要一个密钥连接失败 什么都没有在那里听
//
// 每一个都有一个不同的固定,所以每个都需要一个不同的句子——401案
// 不能让你回去编辑一个 URL 来,你 带类型的 正确,我记得
//
// 电话 HTML 案件不是假设: https://yolo-auto.com/models 返回
// 时 HTTP 200个满额 SPA (验证直播2026-09-23),所以一个用户如果放弃/v1点击200
// -with-HTML 并且过去被告诉 "HTTP 200 — no model list at any known path" ,
// 它既不是问题的名称,也不是问题所在。

#include <doctest/doctest.h>

#include <string>

#include "agentty/provider/openai/transport.hpp"

using agentty::provider::openai::HostProbe;

namespace {
HostProbe failed(HostProbe::Failure f, int status = 0) {
    HostProbe p;
    p.failure     = f;
    p.http_status = status;
    return p;
}
}  // 命名空间结束。

TEST_CASE("probe: every failure explains itself, and no two read alike") {
    const auto unreachable = failed(HostProbe::Failure::Unreachable);
    const auto needs_key   = failed(HostProbe::Failure::NeedsKey, 401);
    const auto not_an_api  = failed(HostProbe::Failure::NotAnApi, 200);
    const auto no_list     = failed(HostProbe::Failure::NoModelList, 200);
    const auto http_err    = failed(HostProbe::Failure::HttpError, 502);

    // 没有一个是ok().
    CHECK(!unreachable.ok());
    CHECK(!needs_key.ok());
    CHECK(!not_an_api.ok());

    // 说着要说点什么 不同 其臂部产生相同句子的
    // 分类学不是分类学.
    const std::string a = unreachable.explain(), b = needs_key.explain(),
                      c = not_an_api.explain(),  d = no_list.explain(),
                      e = http_err.explain();
    CHECK(a != b); CHECK(a != c); CHECK(a != d);
    CHECK(b != c); CHECK(b != d); CHECK(c != d); CHECK(d != e);
    for (const auto& s : {a, b, c, d, e}) CHECK(!s.empty());

    // 每一个名字 实际 固定。 固定。
    CHECK(a.find("nothing listening") != std::string::npos);
    CHECK(b.find("needs an API key") != std::string::npos);
    CHECK(b.find("endpoint is right") != std::string::npos);  // 千万不要编辑URL
    CHECK(c.find("web page") != std::string::npos);
    CHECK(c.find("/v1") != std::string::npos);                // 命名固定点
    CHECK(d.find("no model list") != std::string::npos);

    // 成功无从解释,因为没有什么可以弥补的。
    HostProbe good;
    good.dialect     = HostProbe::Dialect::OpenAiCompat;
    good.model_count = 2;
    CHECK(good.ok());
    CHECK(good.explain().empty());
}

TEST_CASE("probe: a 401 must not read as 'nothing there'") {
    // 该 REGRESSION 此 PINS。回答401的终点是 CORRECT
    // ——它回答。它像连接失败一样报告它,让用户重新 -type 地址已经正确了,
    // 对于最普通的付费地址来说, 完全倒着 -provider 正在登录路径
    // ( 类型主机 )。 401 = 粘贴键。
    // https://yolo-auto.com/v1/models 返回 401,
    // 请输入 "Missing API key" 没有 凭据.
    const auto needs_key = failed(HostProbe::Failure::NeedsKey, 401);
    const auto dead      = failed(HostProbe::Failure::Unreachable);

    CHECK(needs_key.explain().find("nothing listening") == std::string::npos);
    CHECK(dead.explain().find("API key") == std::string::npos);

    // 403是同一类——Yolo-Auto返回它作为计划没有包括的模型,
    // 在本来完全好的终点上.
    const auto forbidden = failed(HostProbe::Failure::NeedsKey, 403);
    CHECK(forbidden.explain().find("403") != std::string::npos);
    CHECK(forbidden.explain().find("endpoint is right") != std::string::npos);
}

TEST_CASE("probe: 200-with-HTML is diagnosed as a web page, not a JSON error") {
    // 仪表板 -URL 旧代码看见200,试图 解析 ,失败,
    // 并报告200 —告诉用户服务器没事,没有改变。
    // 修复必须 NAMED ,因为 "use the API base URL"
    // 并不是用户猜测的。
    const auto html = failed(HostProbe::Failure::NotAnApi, 200);
    CHECK(html.explain().find("web page") != std::string::npos);
    CHECK(html.explain().find("/v1") != std::string::npos);

    // 和它不同,它与 "reachable but no model list" ,
    // 这是真正不同的情况(a) JSON API )),在错误的前缀上.
    const auto json_but_wrong = failed(HostProbe::Failure::NoModelList, 200);
    CHECK(html.explain() != json_but_wrong.explain());
}

TEST_CASE("probe: success carries what the UI shows") {
    // "✓ 12 models · openai · 45ms" - 就是这样
    // -just-works 线条的每一块都来自探测器 所以它必须生存下来
    HostProbe p;
    p.dialect     = HostProbe::Dialect::OpenAiCompat;
    p.models_path = "/v1/models";
    p.model_count = 12;
    p.latency_ms  = 45;
    p.http_status = 200;

    CHECK(p.ok());
    CHECK(p.failure == HostProbe::Failure::None);
    CHECK(p.model_count == 12);
    CHECK(p.latency_ms == 45);
    CHECK(p.models_path == "/v1/models");

    // 赤裸裸的 Ollama 守护进程回答不同的路径,
    // 并且仍然是成功的 —— 探测器 ADOPTS 而不是要求用户声明
    HostProbe ol;
    ol.dialect     = HostProbe::Dialect::OllamaNative;
    ol.models_path = "/api/tags";
    ol.model_count = 3;
    CHECK(ol.ok());
    CHECK(ol.explain().empty());
}
