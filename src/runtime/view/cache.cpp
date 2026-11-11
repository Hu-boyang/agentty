#include "agentty/runtime/view/cache.hpp"

#include <cstddef>
#include <string>

namespace agentty::ui {

std::string ViewCache::make_key_(const ThreadId& tid, const MessageId& mid) {
    // ⁇ + ':' + 中点。 两者均为十六进制 -shaped 字符串;
    // 分隔符表示一个键, 如果我们需要的话( 目前我们不需要—— 键) ,
    // 可以解析。 缓存 将密钥视为不透明。 长度预览 -reserved
    // 所以附件不会重新分配。
    std::string k;
    k.reserve(tid.value.size() + mid.value.size() + 1);
    k.append(tid.value);
    k.push_back(':');
    k.append(mid.value);
    return k;
}

ViewCache::Entry& ViewCache::migrate_to_pinned_(const std::string& key) {
    // 先决条件: 密钥在条目_( 已确定)。 移动其 载荷 输入封入_。
    // 地图 -node 移动保留 md/cfg (逐步显示 部件,
    // 块 缓存 , 推迟状态),所以动画状态在生命周期过渡中
    // 完整地存活下来.
    auto it = entries_.find(key);
    it->second.pinned = true;
    auto [ins, _] = pinned_.emplace(key, std::move(it->second));
    entries_.erase(it);
    return ins->second;
}

ViewCache::Entry& ViewCache::migrate_to_settled_(const std::string& key) {
    // 先决条件:键在绑定_(活性)。移动其 载荷。
    // 没有上限/ 没有驱逐: 已定居的地图以主动转弯和自定义为
    // 界 -empties 冻结时( freeze_range
    // 放下它封的每根钥匙 所以这里永远没有东西可以赶走
    auto it = pinned_.find(key);
    it->second.pinned = false;
    auto [ins, _] = entries_.emplace(key, std::move(it->second));
    pinned_.erase(it);
    return ins->second;
}

ViewCache::Entry& ViewCache::touch_settled_(const std::string& key) {
    // 已经解决了 —— 把它还回去.
    if (auto it = entries_.find(key); it != entries_.end())
        return it->second;
    // 现被困 —— 打电话者现在认为已定居; 向下迁徙( 载荷 ——
    // —————— ————。
    if (pinned_.find(key) != pinned_.end())
        return migrate_to_settled_(key);
    // 新鲜密钥: 插入到已解决的映射中。
    auto [ins, _] = entries_.emplace(key, Entry{});
    ins->second.pinned = false;
    return ins->second;
}

ViewCache::Entry& ViewCache::touch_pinned_(const std::string& key) {
    // 已经被困住了 没有什么可做的了
    if (auto it = pinned_.find(key); it != pinned_.end())
        return it->second;
    // 目前定居的 —— 晋升到固定的套装( 载荷 ———————— ————。
    if (entries_.find(key) != entries_.end())
        return migrate_to_pinned_(key);
    // 新鲜密钥:直接插入固定套件。
    auto [ins, _] = pinned_.emplace(key, Entry{});
    ins->second.pinned = true;
    return ins->second;
}

MessageMdCache& ViewCache::message_md(const ThreadId& tid, const MessageId& mid) {
    return touch_settled_(make_key_(tid, mid)).md;
}

MessageMdCache& ViewCache::message_md_live(const ThreadId& tid, const MessageId& mid) {
    return touch_pinned_(make_key_(tid, mid)).md;
}

bool ViewCache::is_pinned(const ThreadId& tid, const MessageId& mid) const noexcept {
    return pinned_.find(make_key_(tid, mid)) != pinned_.end();
}

void ViewCache::drop(const ThreadId& tid, const MessageId& mid) {
    // 将进入任何一个家庭,从任何一个固定的进入的死亡时刻释放出来。
    // freeze_range 调用 封在每条信息上 因为被冻结的
    // 信息的行里 m.ui.frozen 财务报告和已审计财务报表
    // ViewCache 条目不再读取。 重新打开被绑定的边太关闭镜
    // 像危险( 停止动画但从未下线的部件) -migrated
    // 现场 渲染 接口边界). erase() 收回
    // shared_ptr 载荷 立刻开始
    const auto key = make_key_(tid, mid);
    entries_.erase(key);
    pinned_.erase(key);
}

const MessageMdCache* ViewCache::peek(const ThreadId& tid,
                                      const MessageId& mid) const noexcept {
    const auto key = make_key_(tid, mid);
    if (auto it = pinned_.find(key); it != pinned_.end())
        return &it->second.md;
    if (auto it = entries_.find(key); it != entries_.end())
        return &it->second.md;
    return nullptr;
}

} // namespace agentty::ui
