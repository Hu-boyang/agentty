#pragma once
// agentty::modelsdev-models.dev社区能力数据库的背景快照
// (https://models.dev/api.json).
//
// models.dev是开放的社区-maintained型号-metadata DB(由OpenCode
// 在生产中使用)。它的 模式定义 准确地命名了 agentty 对异构提供商的需求：per
// -model `reasoning` 加上
// `reasoning_options: [{type:"effort", values:[...]}]` — EXACT 的努力
// 模型接受的枚举值，差异可能很大，例如完整六级阶梯或二元
// {高，最大}，切换-only，无）。
//
// refresh()获取api.json(config_dir()下的24h磁盘缓存)、
// 解析，并将每个-model事实推送到目录注册表中：
//   set_catalog_reasoning(id，bool) — 推理 on/off
//   声明 set_catalog_effort_set(id，位掩码) — 声明的
// 工作 枚举 Id 被记录 BARE (models.dev 键是提供者 -scoped；
// agentty 模型 id 是裸 传输 id) — "provider/model" 键
// 及其在最后一个之后的 末尾 '/' 已注册，因此任一拼写均可解析。
//
// resolved_caps()中的优先级管道将这些声明置于用户覆盖之下、
// 学习的-from-rejection事实之下、from_id
// 推论之上。因此：品牌 -new 型号发货 → 社区更新
// models.dev → agentty 在一天之内即可获取，无需发布。
// 失败是无声的（元数据刷新决不能作为错误出现）。

#include <string>
#include <string_view>

#include <maya/runtime.hpp>

namespace agentty::modelsdev {

// 加载将缓存的快照从磁盘(如果有的话)缓存到目录注册表中。
// 便宜； 调用 在启动时先于第一个 渲染。
void load_cached();

// 如果 缓存 早于约 24 小时，则获取新的 快照，然后（重新）加载。
// 阻塞——在后台工作线程 (Cmd::task)上运行。
// 返回已注册模型的数量（任何失败均返回 0）。
int refresh();

// 在短暂延迟后在拥有的 线程 上运行 load_cached() + refresh()，
// 因此立即退出的进程 (pipe-EOF 冒烟测试、--version)永远不会启动网络 调用。
// 线程 由 join_background_refresh() 连接，
// 拆解前为 main() 调用；分离的 线程 仍在 refresh() 的 HTTP 调用 内，
// 而 CRT 释放静态是 use-after-free（CI 管道烟雾测试
// 中的 Windows 0xC0000005 — 与 blob-gc 修复相同）。
void start_background_refresh(bool no_net = false);
// 取消并加入。 幂等，如果启动从未运行过，则没有-op。加入：`unlocked`。
void join_background_refresh(const maya::unlocked& cap) noexcept;

} // namespace agentty::modelsdev

namespace agentty { struct CatalogBatch; }
namespace agentty::modelsdev {
// 解析将api.json文档放入`out`中而不发布它。对于测试：
// 允许在不触及全局注册表的情况下比较解析的事实。
// 返回已注册的模型。
int parse_into(std::string_view body, CatalogBatch& out);
} // namespace agentty::modelsdev
