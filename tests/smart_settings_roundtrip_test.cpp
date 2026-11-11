// 智能模式 注册表 横行必须 WRITE BACK 不论他们的控制类型
//
// 由帆船公司报告( PR 42: 内 ^S ~ 智能模式, ~ 聪明模式,
// $ "Cheapest main-turn role" 弹出打开自己的选择,选择一个则不会改变任何东西。
//
// 状态更新函数 读取编辑字段为
//
//     如果(自动* num = std::get_if<form::field::Number>( & f- > value)),
//         则第registry::set(cfg, *d, std::to_string(num- > Value));
//
// 如此一行谁的控制什么的 OTHER 数字从1个数字中
// 跌落 `if` 并被放弃——默默地,因为周围的代码
// 仍然坚持(不变)的配置,重建形式,然后返回 done().
// 该行移动在屏幕上,并在下一次重建时返回.
//
// 5个智能*行中有两个受到影响,而不是一个:枚举("Cheapest main-turn role")
// 和Bool("Route the main turn"),两者都是Tier::Basic,
// i.e.可见,没有--advanced.
//
// 钉住它 CONTRACT 而不是控件类型: 注册表 老板
// 的曝光必须圆 -trippable 结束
// registry::set ,因此添加第六行并带有第六个控制类型,
// 不能重新引入静音滴。

#include "agtest.hpp"

#include "agentty/runtime/settings_registry.hpp"
#include "agentty/domain/smart_mode.hpp"

#include <string>

TEST_CASE("smart settings: every registry row round-trips through set()") {
    namespace reg = agentty::settings::registry;

    // 走智能模式行并证明每个都接受一个书面值并读回它。
    // 默默忽略一个字的行就是被报告的错误,
    // 一次一个控制类型。
    int checked = 0;
    for (const auto& d : reg::kSettings) {
        if (d.owner() != reg::Owner::Smart) continue;
        ++checked;

        agentty::smart::RoleConfig cfg;   // 默认

        switch (d.type) {
            case reg::Type::Bool: {
                // 把它从默认位置翻开,然后退后;两个方向必须坚持。
                // "false" 有趣的是,我们 set()
                // 只能尊重真理的弦 看上去是可行的
                CHECK(reg::set(cfg, d, "true"));
                CHECK(reg::get(cfg, d) == "true");
                CHECK(reg::set(cfg, d, "false"));
                CHECK(reg::get(cfg, d) == "false");
                break;
            }
            case reg::Type::Enum: {
                // 每个已宣布的选项必须可以选择。
                // smart.main_turn_floor ) 宣布实用性
                // | 执行 | 战略; 选择其中的任何一种, 之前没有改变。
                const std::string opts{d.options};
                REQUIRE(!opts.empty());
                std::size_t start = 0;
                while (start <= opts.size()) {
                    const auto bar = opts.find('|', start);
                    const std::string opt =
                        opts.substr(start, bar == std::string::npos
                                               ? std::string::npos : bar - start);
                    if (!opt.empty()) {
                        CHECK(reg::set(cfg, d, opt));
                        CHECK(reg::get(cfg, d) == opt);
                    }
                    if (bar == std::string::npos) break;
                    start = bar + 1;
                }
                break;
            }
            case reg::Type::Int:
            case reg::Type::Real: {
                // 申报范围内的值必须活下来。
                const double mid = (d.min + d.max) / 2.0;
                const std::string v = d.type == reg::Type::Int
                    ? std::to_string(static_cast<long long>(mid))
                    : std::to_string(mid);
                CHECK(reg::set(cfg, d, v));
                break;
            }
        }
    }
    // 步行必须确实覆盖了某种东西——在车主过滤
    // 器中打字会让这个通道空洞无处.
    CHECK(checked >= 5);
}

TEST_CASE("smart settings: the reported row is an Enum, and its siblings vary") {
    // 具体的回归,命名。 处理函数 用于读 仅
    // form::field::Number ,
    // 所以这两行永远无法执行。在这里确定其类型意味着未来的改变,
    // 使所报告的行再次成为数字(因此不慎) "fixes"
    // 上面的合约仍然是真正的警卫
    namespace reg = agentty::settings::registry;

    const auto* floor_row = reg::find("smart.main_turn_floor");
    REQUIRE(floor_row != nullptr);
    CHECK(floor_row->type == reg::Type::Enum);

    const auto* route_row = reg::find("smart.route_main_turn");
    REQUIRE(route_row != nullptr);
    CHECK(route_row->type == reg::Type::Bool);

    // 两者都是基本等级——默认可见 面板 后边
    // --advanced ,因此普通用户可以达到。
    CHECK(floor_row->tier == reg::Tier::Basic);
    CHECK(route_row->tier == reg::Tier::Basic);

    // 至少一个智能的IS a Number,这就是错误隐藏的
    // 原因:作者测试的行工作.
    bool any_number = false;
    for (const auto& d : reg::kSettings)
        if (d.owner() == reg::Owner::Smart
            && (d.type == reg::Type::Int || d.type == reg::Type::Real))
            any_number = true;
    CHECK(any_number);
}
