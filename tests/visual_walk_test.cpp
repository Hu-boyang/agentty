// visual_walk_test ——结构化. 帧-hash 步行合同。
//
// 三个属性,每个属性对运出的错误类否定:
//   1. COMPLETENESS : 变异 任意 可见的面值 任意 面板状态变化
//      散列 ——无记,无记.
//   2. SECRETS: 凭据 字节 永远不影响散列(同样) -length
//      覆盖密件 / api_key 散列( Hash) -invisible。
//      长度变化可见。豁免是故意的和固定的。
//   3. ARITY PROOF:在有visual_parts名单的类别中增加一名成员
//      断开构造( O) 编译-time ; 以 static_asserts 输入
//      visual_parts.hpp - 这个测试将钉在 RUNTIME 报告内容为:

#include "agtest.hpp"

#include "agentty/runtime/panel/visual_parts.hpp"
#include "agentty/runtime/model.hpp"   // TodoState

#include <string>

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

std::uint64_t walk(const auto& v) {
    std::uint64_t H = 0;
    auto h = [&](std::uint64_t x) {
        H ^= x + 0x9e3779b97f4a7c15ull + (H << 6) + (H >> 2);
    };
    visual::mix_any(h, v);
    return H;
}

} // 命名空间结束。

TEST_CASE("visual walk: every panel facet changes the hash") {
    // 模式:基础成员( index/query/confirm_remove/provider_scope ),
    // 该 assign_slot 可选的——每个变异必须移动散列.
    pn::Models a;
    const auto h0 = walk(a);
    { auto b = a; b.index = 3;                CHECK(walk(b) != h0); }
    { auto b = a; b.query = "x";              CHECK(walk(b) != h0); }
    { auto b = a; b.confirm_remove = "y";     CHECK(walk(b) != h0); }
    { auto b = a; b.provider_scope = "z";     CHECK(walk(b) != h0); }
    { auto b = a; b.assign_slot = smart::ModelRole::Utility;
                                              CHECK(walk(b) != h0); }

    // 插槽 变体 : 切换替代物即使两个替代物默认都会移动
    // 散列 -constructed.
    ui::panel::State s1; s1.descend(pn::Models{});
    ui::panel::State s2; s2.descend(pn::Providers{});
    CHECK(walk(s1.raw()) != walk(s2.raw()));
}

TEST_CASE("visual walk: form facets all covered (the mix_form bug class)") {
    form::Form f;
    f.fields.push_back({.id = "host", .label = "Host",
                        .value = form::field::Text{"localhost", 4}});
    const auto h0 = walk(f);
    { auto g = f; g.cursor = 0; std::get<form::field::Text>(
          g.fields[0].value).value += "x";    CHECK(walk(g) != h0); }
    { auto g = f; std::get<form::field::Text>(
          g.fields[0].value).cursor = 9;      CHECK(walk(g) != h0); }
    { auto g = f; g.dirty = true;             CHECK(walk(g) != h0); }
    { auto g = f; g.note = "unsaved";         CHECK(walk(g) != h0); }
    { auto g = f; g.focus = form::focus::Editing{};
                                              CHECK(walk(g) != h0); }
    { auto g = f; g.fields[0].error = "bad";  CHECK(walk(g) != h0); }
    { auto g = f; g.fields[0].locked = true;  CHECK(walk(g) != h0); }
}

TEST_CASE("visual walk: secret bytes never reach the hash") {
    // field::Secret : 不变 -length 覆盖无形; 长度变化可见;
    // 光标运动可见。
    form::Form f;
    f.fields.push_back({.id = "key", .label = "API key",
                        .value = form::field::Secret{"hunter22", 3}});
    const auto h0 = walk(f);
    { auto g = f; std::get<form::field::Secret>(
          g.fields[0].value).value = "letmein9";   // 相同长度
      CHECK(walk(g) == h0); }
    { auto g = f; std::get<form::field::Secret>(
          g.fields[0].value).value = "longer-secret";
      CHECK(walk(g) != h0); }
    { auto g = f; std::get<form::field::Secret>(
          g.fields[0].value).cursor = 7;
      CHECK(walk(g) != h0); }
    // 占位符是绘制的文本,而不是 凭据 :必须 重绘.
    { auto g = f; std::get<form::field::Secret>(
          g.fields[0].value).placeholder = "\xe2\x9c\x93 key saved";
      CHECK(walk(g) != h0); }

    // EmbedConfig::api_key:配置层的相同规则.
    rag::embed::EmbedConfig c;
    c.api_key = "sk-aaaaaaaa";
    const auto e0 = walk(c);
    { auto d = c; d.api_key = "sk-bbbbbbbb";       // 相同长度
      CHECK(walk(d) == e0); }
    { auto d = c; d.api_key = "sk-longer-than-before";
      CHECK(walk(d) != e0); }
    { auto d = c; d.host = "other-host";           // 非-secret:可见
      CHECK(walk(d) != e0); }
}

TEST_CASE("visual walk: rag pane probe + form ride in structurally") {
    pn::Rag r;
    const auto h0 = walk(r);
    { auto q = r; q.embed.probe = rag_settings::EmbedForm::Testing{};
      CHECK(walk(q) != h0); }
    { auto q = r; q.embed.probe = rag_settings::EmbedForm::Ok{768, 42};
      CHECK(walk(q) != h0); }
    { auto q = r; q.advanced = true;          CHECK(walk(q) != h0); }
    { auto q = r; q.embed.form.dirty = true;  CHECK(walk(q) != h0); }
}

TEST_CASE("visual walk: exempt facets are exempt (From does not churn)") {
    // 父母一方在孩子上开张 父母在 `from` .
    // 父母不在孩子开放时被送走——步行不得 不同
    // 藏匿父母的不同(否则每一次键盘键会重新
    // -walk 祖先, 和恢复会 重绘 虚妄之说).
    pn::SettingsList a, b;
    ui::panel::State sa, sb;
    sa.descend(pn::Palette{});   // 父目录 1
    sb.descend(pn::Models{});    // 父目录 2
    a.from = pn::From::of(pn::Snapshot{sa.raw()});
    b.from = pn::From::of(pn::Snapshot{sb.raw()});
    CHECK(walk(a) == walk(b));
}

TEST_CASE("visual walk: panel-adjacent state is covered too") {
    // 待办事宜: 代理重写中途 -stream 时 弹出面板
    // 打开——以前只打开 open/closed 被散列( 本次
    // 迁移浮出水面的活虫: 状态翻转没有) 重绘).
    TodoState t;
    t.items.push_back({"write tests", TodoStatus::Pending});
    const auto t0 = walk(t);
    { auto u = t; u.items[0].status = TodoStatus::Completed;
      CHECK(walk(u) != t0); }
    { auto u = t; u.items[0].content = "write MORE tests";
      CHECK(walk(u) != t0); }
    { auto u = t; u.items.push_back({"another", TodoStatus::Pending});
      CHECK(walk(u) != t0); }

    // 插件 快照: connection/disable/error 翻转
    // 重绘 设置列表。
    mcp::PluginModel p;
    mcp::ServerState s; s.name = "srv"; s.connected = false;
    p.servers.push_back(s);
    const auto p0 = walk(p);
    { auto q = p; q.servers[0].connected = true;  CHECK(walk(q) != p0); }
    { auto q = p; q.servers[0].disabled = true;   CHECK(walk(q) != p0); }
    { auto q = p; q.servers[0].error = "boom";    CHECK(walk(q) != p0); }
    { auto q = p; q.servers[0].tools.push_back({"t", "d", true, false});
      CHECK(walk(q) != p0); }

    // 目录: models/state/account 移动散列;
    // 衍生 缓存 编辑 TTL 钟刻意地做 不.
    ProviderCatalog c;
    c.provider_id = "anthropic";
    const auto c0 = walk(c);
    { auto d = c; d.state = ProviderCatalog::State::Ready;
      CHECK(walk(d) != c0); }
    { auto d = c; d.models.push_back({}); CHECK(walk(d) != c0); }
    { auto d = c; d.account_label = "work"; CHECK(walk(d) != c0); }
    { auto d = c; d.loaded_at_ms = 12345;   CHECK(walk(d) == c0); }
    { auto d = c; d.search_keys.push_back("cache");
      CHECK(walk(d) == c0); }
}
