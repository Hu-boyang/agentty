// smart_form.cpp — Smart Mode的行型.
//
// 纯度: 不检查目录, 不 IO ,没有铬。 面板 解决
// 每个角色(需要模型目录和主动提供者),并将由此产生的
// 标签交接进去;此文件只决定行中的内容 ARE.

#include "agentty/runtime/panel/smart_form.hpp"

#include "agentty/runtime/settings_registry.hpp"   // 行范围

namespace agentty::smart_form {

namespace {

void add_slot(form::Builder& b, const char* id, std::string label,
              const SlotView& v, std::string help) {
    b.pick(id, std::move(label), v.label, std::move(help));
    // 证明, 嵌入式 面板 用于 "settings.json"
    // / "env: …" 的同一栏。 固定槽是有意选择的用户;
    // 自动槽是目录的选取。 说这是 "why is it using that model?"
    // 和 答案的区别。
    b.origin(v.pinned ? "pinned" : "auto");
}

} // 命名空间结束。

form::Form build_form(const Inputs& in) {
    form::Builder b{" Smart Mode "};
    // 下方的注册表行的Group-header簿记.
    // 槽行是手-written(它们是型号为选择面板,不是桌行),
    // 所以行走开始新鲜,并发出自己的头文件.
    bool first = true;
    auto last_group = settings::registry::Group::Sources;
    b.subtitle(in.enabled
        ? "routing each turn to the cheapest model that can do it"
        : "every turn goes to the main model");

    b.toggle(kFieldEnabled, "Smart Mode", in.enabled,
             "route by role instead of sending everything to one model");
    if (!in.enabled_lock.empty()) b.lock(in.enabled_lock);

    // 插槽只在路由开启时表示某种意思。
    // 锁定而非隐藏: a 面板 内容出现和消失 令人
    // 不适 整行正是解释 上面开关会是什么 DO.
    const bool live = in.enabled;

    add_slot(b, kFieldStrategic, "Strategic", in.strategic,
             "planning, architecture, hard debugging");
    if (!live) b.lock("Smart Mode off");

    add_slot(b, kFieldImpl, "Implementation", in.implementation,
             "writing and editing code");
    if (!live) b.lock("Smart Mode off");

    add_slot(b, kFieldUtility, "Utility", in.utility,
             "summaries, titles, cheap mechanical work");
    if (!live) b.lock("Smart Mode off");

    // ── 高级:数字政策 ────────────────────────────────
    // WALKED 从设置中 注册表 ,完全作为检索 面板 标记、帮助、
    // 控制类型、范围、组 头文件 环境 锁 和来源都来自排行——这个
    // 面板 仅贡献
    // 由 Settings 管理的行属于此面板。
    //
    // 这些都是手 -rolled 在此首先: 三个标签、
    // 三个帮助字符串和三个区域从已经声明的表格中重新打印出来,
    // 加上第二个执行内存 -lock 规则,一切 注册表 已经存在,
    // 以预防。
    settings::registry::add_rows(b, in.smart, in.env,
                                 settings::registry::Owner::Smart,
                                 in.advanced, first, last_group);

    // 付得起的必须 ON SCREEN。高级行默认被隐藏,
    // 没有人能看见的密钥就是这些旋钮被移出env
    // var以修复的可发现性故障。
    // 面板-specific 注释中只有 – 共享密钥语法来自 form_config
    // (SSOT). ^E 切换 高级行( 面板 ) ,"国家".
    b.note(in.advanced ? "^E  hide advanced" : "^E  advanced");

    return b.build();
}

std::optional<smart::ModelRole> role_of_field(std::string_view id) noexcept {
    if (id == kFieldStrategic) return smart::ModelRole::Strategic;
    if (id == kFieldImpl)      return smart::ModelRole::Implementation;
    if (id == kFieldUtility)   return smart::ModelRole::Utility;
    return std::nullopt;
}

std::string_view field_of_role(smart::ModelRole r) noexcept {
    switch (r) {
        case smart::ModelRole::Strategic:      return kFieldStrategic;
        case smart::ModelRole::Implementation: return kFieldImpl;
        case smart::ModelRole::Utility:        return kFieldUtility;
    }
    return kFieldStrategic;
}

void focus_role(form::Form& f, smart::ModelRole r) noexcept {
    const auto id = field_of_role(r);
    for (std::size_t i = 0; i < f.fields.size(); ++i)
        if (f.fields[i].id == id) { f.cursor = static_cast<int>(i); return; }
}

bool enabled_from_form(const form::Form& f, bool fallback) noexcept {
    if (const auto* row = f.find(kFieldEnabled))
        if (const auto* t = std::get_if<form::field::Toggle>(&row->value))
            return t->on;
    return fallback;
}

} // namespace agentty::smart_form
