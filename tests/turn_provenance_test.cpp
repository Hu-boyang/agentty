// turn_provenance_test——第ACTUALLY
// 号服务模式贴上助手转弯的标签,而不是现在第选择面板号的车型。
//
// 错误:在Smart Mode下,转弯是按已解决的榜样(launch_stream
// 中的`strategic_profile.model`)发出的,
// 通常在选择面板中是NOT。 状态-bar徽章和转弯头文件改为
// `Model::d.model_id`-SELECTION,
// 因此agentty显示"Mistral Large",而答复中每字节来自GLM。
// 调试日志(它报告传输)和UI(它报告选择)都不一致,
// UI是错误的。
//
// 也追溯性地撒谎:因为 头文件 是从现时状态计算出来的 渲染 时间,
// 转换模型重新标记了已经坐落在记录的每个转弯。转弯的出处是一个属性。
// OF 该 TURN ,所以它现在生活 转弯( Message::served_model
// / served_role ),在一次印章上加盖 StreamStarted ,
// 存活下来 持久化 ,并混入 渲染 键。

#include <string>

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"   // launch_stream
#include "agentty/provider/selection.hpp"
#include "agentty/store/store.hpp"

using namespace agentty;
namespace store = agentty::store;

namespace {

store::Settings g_settings;

void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const Thread&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& s) { g_settings = s; },
        .new_thread_id  = [] { return ThreadId{"t-provenance"}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = {},
    });
}

// 中场模特儿 -turn : 一个用户消息, 一个助理占位符, 即 传输 即将打开。
// `routed` 是什么 说吧 launch_stream 已解决此
// 转折( 空 = 智能模式关闭, 因此选择服务它 )。
// `routed`空 / `role`缺席 = Smart Mode 关闭,
// 所以选择服务于转弯,两者都是现在的TYPED,这就是点:一个
// 呼叫者不能传递模式去的地方的角色,也不能发明角色拼写.
Model mid_turn(const std::string& selected, const std::string& routed,
               std::optional<smart::ModelRole> role) {
    Model m;
    m.d.model_id = ModelId{selected};
    Message user;
    user.role = Role::User;
    user.text = "hello";
    m.d.current.messages.push_back(std::move(user));
    Message asst;
    asst.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(asst));
    m.s.smart_turn_model = ModelId{routed};
    m.s.smart_turn_role  = role;
    return m;
}

} // 命名空间结束。

TEST_CASE("turn provenance: the role codec round-trips") {
    // served_role 被坚持为字符串,并读回为 枚举 ,所以两个方向必须一致。
    // 它们过去没有: 作者只排放 "strategic" ,
    // 视图匹配 "strategic"/
    // "implementation"/"utility" ,和 role_label (
    // 单位:千美元) "impl" ——一个概念的三种拼写,无 编译器-checked.
    // (docs/STRONG_TYPES_AUDIT.md § 查找 2.)
    using smart::ModelRole;
    for (auto r : {ModelRole::Strategic, ModelRole::Implementation,
                   ModelRole::Utility}) {
        const auto wire = smart::role_wire_name(r);
        const auto back = smart::role_from_wire_name(wire);
        REQUIRE_MESSAGE(back.has_value(),
                        "every role must survive a write/read cycle");
        CHECK(*back == r);
    }

    // role_label (a) 属于 UI 缩略语( "impl"), 不 编号 传输 拼写。
    // 在磁盘上重用它就是能打碎读者的 整洁的-up,
    // 所以读者会以防御性的方式接受它—— 被固定在这里,这样才是真的.
    const auto legacy = smart::role_from_wire_name("impl");
    CHECK(legacy.has_value());
    CHECK(*legacy == ModelRole::Implementation);

    // 都失踪了 也不知道 "no role tag" 而不是
    // 猜测 A 线程 由一个知道角色的建筑所写 这个角色不能
    // 继续打开, 只是没有口音。
    CHECK_FALSE(smart::role_from_wire_name("").has_value());
    CHECK_FALSE(smart::role_from_wire_name("a-role-from-the-future").has_value());
}

TEST_CASE("turn provenance: the header names the model that served the turn") {
    install_stub_deps();
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    // ── StreamStarted 将路由模型印到助手转弯上 ──
    {
        Model m = mid_turn("mistral-large-3", "glm-5.3", smart::ModelRole::Strategic);
        auto [m1, _] = app::update(std::move(m), Msg{StreamStarted{}});
        const auto& asst = m1.d.current.messages.back();
        CHECK(asst.served_model == ModelId{"glm-5.3"},
              "the turn records the model that actually served it");
        CHECK(asst.served_role == smart::ModelRole::Strategic,
              "the turn records the role it was routed as");
        CHECK(m1.d.model_id.value == "mistral-large-3",
              "stamping provenance must not disturb the user's selection");
    }

    // ── 智能模式关闭:转弯为 仍然 , 没有角色 ──────
    // 服务于转弯的模型是路由器是否选择转弯的事实。
    // 留空在这里是 # 120: 一个平整的会话
    // 记录到什么也没有, 所以每个头文件 都回到了
    // 现场选择, 切换模型重新标记了整个记录片。
    {
        Model m = mid_turn("mistral-large-3", "mistral-large-3", std::nullopt);
        auto [m1, _] = app::update(std::move(m), Msg{StreamStarted{}});
        const auto& asst = m1.d.current.messages.back();
        CHECK(asst.served_model == ModelId{"mistral-large-3"},
              "an unrouted turn still records the model that served it");
        CHECK(!asst.served_role.has_value());
        INFO("no role — nothing routed it, and claiming one would "
             "overstate Smart Mode in the stats fold");
    }

    // ── 转换模型会重新标记 不 已重刻在记录簿中 ──
    // 这是错误的追溯性半个: 头文件 曾经是来自活邦 所以
    // 定居 GLM 转身成为了 "Mistral"
    // 转动你转动的瞬间。
    {
        Model m = mid_turn("mistral-large-3", "glm-5.3", smart::ModelRole::Strategic);
        auto [m1, _] = app::update(std::move(m), Msg{StreamStarted{}});
        m1.d.model_id = ModelId{"claude-opus-4-5"};   // 用户切换器在之后
        CHECK(m1.d.current.messages.back().served_model == "glm-5.3",
              "a settled turn keeps its author across a model switch");
    }

    // ── 证明更改 渲染 键( 以缓存元素棒) ──
    {
        Message a;
        a.role = Role::Assistant;
        a.text = "same text";
        Message b = a;
        b.served_model = ModelId{"glm-5.3"};
        b.served_role  = smart::ModelRole::Strategic;
        CHECK(a.compute_render_key() != b.compute_render_key(),
              "served_model/role must invalidate the per-message render cache");
    }
}

TEST_CASE("turn provenance: a plain session stamps every turn (#120)") {
    // 被报道的虫子,在它实际生活的地层上. launch_stream
    // 用于写入 smart_turn_model 仅 当Smart
    // Mode 安排了转弯时,所以一个普通会话会让每个助理
    // 消息都未加标记。 头文件 然后回到现场选择,
    // 切换模型 将整个记录稿重新贴上追溯标签—— 一个Granite转弯,
    // 声称是用户刚刚选择的 Spark模型。
    install_stub_deps();
    {
        provider::Selection sel;
        sel.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), sel);
    }

    Model m;
    m.d.model_id = ModelId{"granite-4.2-3b"};
    // Smart Mode OFF——无记录的条件.
    CHECK_FALSE(m.d.smart.orchestration());

    Message user;
    user.role = Role::User;
    user.text = "first";
    m.d.current.messages.push_back(std::move(user));
    Message asst;
    asst.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(asst));

    (void)agentty::app::cmd::launch_stream(m);
    CHECK(m.s.smart_turn_model == ModelId{"granite-4.2-3b"},
          "an unrouted turn records the model serving it");
    INFO("an unrouted turn gets no role");
    CHECK_FALSE(m.s.smart_turn_role.has_value());

    // 邮票落在了信息上,在后来的模型开关中幸存下来.
    auto [m1, _] = app::update(std::move(m), Msg{StreamStarted{}});
    CHECK(m1.d.current.messages.back().served_model == ModelId{"granite-4.2-3b"});
    m1.d.model_id = ModelId{"spark-1"};
    CHECK(m1.d.current.messages.back().served_model == ModelId{"granite-4.2-3b"},
          "switching model must not rewrite who served an earlier turn");
}
