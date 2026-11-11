#pragma once
// agentty::app::settings_cache-Deps::save_settings后面的写入-behind 缓存。
//
// 该 PROBLEM。设置从 ~8 状态更新函数 写入：配置文件周期、
// 智能模式 切换、插槽清除、提供商切换、模型选择、退出、RAG 提交、
// 登录。每次写入都是加载-modify-save：读取+解析 settings.json，
// mutate，fsync，rename。这是一个圆-trip的磁盘，
// 状态更新函数在两个帧之间运行ON 该 UI THREAD——所以这些
// 交互中的每一个都会发生，最明显的是当你翻转切换时动画停顿。
//
// per-call-site 修复（将其包含在 Cmd 中）有八次忘记的机会，
// 并且每次有人添加设置写入时就有第九次机会忘记。因此，修复程序位于 SEAM：
// `save_settings` 发布到 内存 并返回；单个 工作线程
// 即可完成 IO。即使 状态更新函数 尝试阻止，也无法阻止设置 IO。
//
// ── 两者 不变量 使这个安全 ───────────────────────────────
//
// 1.READ-YOUR-WRITES。 `load_settings` 返回缓存的值，所以非常
//    常见的加载-modify-save里面的一个状态更新函数看到它刚刚写的是什么。
//    如果在写入正在进行时读取到磁盘，
//    则该模式将默默地丢失较早的字段。
//
// 2.ORDERED，COALESCEDWRITES。一个 工作线程 应用在提交中写入
//    顺序，因此较晚的 保存 不能在较早的之前着陆。
//    连续 保存 崩溃到最新的 待处理 值 - 持有的 -down
//    密钥的中间状态并不重要，只有它停止的地方才有意义。
//
// ── 什麽不是 解开这里 ──────────────────────────────────────────────
// 在退出时仍在进行中的写入将会丢失，因此`flush()`会阻塞，
// 直到队列耗尽并在退出路径上被调用。这是同步等待
// 正确的地方：它之后的 帧 永远不会被绘制。
//
// 缓存还假设它是ONLY写入者。在其背后写入 settings.json 的
// 代码（直接从 provider::credentials::add_key
// 和其他 凭据 帮助程序 调用 persistence::save_settings 中，
// 因为它们在没有 Deps 的路径上运行）会留下 缓存 提供一个早于写入的值，
// 而下一个 保存 到 接口边界 则将 过期 值发布回其上。
// 绕过写入器必须 调用 `invalidate()`；请参阅那里的注释。

#include <functional>

#include <maya/runtime.hpp>

#include "agentty/store/store.hpp"

namespace agentty::app::settings_cache {

// 将同步(加载，保存)对包装成写入-behind(加载，保存)对。
// `install_deps`使用这个，因此在任何地方构建的每个Deps都会获得该行为；
// 没有 opt-in 可以忘记。
//
// 锁顺序：缓存是LEAF 锁之一。 加载 和 保存 仅使用它，因此它们需要
// `below<leaf>`（任何不持有叶子的调用者）。磁盘端在排水管工作线程上运行，
// 并有自己的`unlocked`。 flush/invalidate/shutdown等待工作线程，
// 所以他们需要`unlocked`。
struct Seam {
    std::function<store::Settings(const maya::below<maya::leaf>&)>              load;
    std::function<void(const maya::below<maya::leaf>&, const store::Settings&)> save;
};

[[nodiscard]] Seam wrap(
    const maya::unlocked& cap, std::function<store::Settings()> load_from_disk,
    std::function<void(const maya::unlocked&, const store::Settings&)> save_to_disk);

// 删除缓存的值，以便下一个`load`再次从磁盘故障。
//
// 对于通过该接口边界编写settings.json WITHOUT的代码。
// 典型的情况是 凭据 帮助程序（provider::credentials::add_key，
// clear_active，...）：它们从 CLI 路径运行，没有安装 Deps，
// 所以它们直接 调用 persistence::save_settings。
// 如果没有失效，缓存 会保留其前 -write 副本，而下一个 状态更新函数 保存
// 会将该副本写回 — 默默地撤消刚刚添加的密钥。
//
// 首先耗尽任何排队的写入，因此in-flight 保存无法将绕过写入降落在
// 磁盘AFTER上并重新-clobber它。
void invalidate(const maya::unlocked&) noexcept;

// 阻塞直到每个排队的写入都已命中磁盘。从退出路径调用。
// 从不抛出；当没有任何东西排队或没有任何东西被包装时，对于 调用 是安全的。
void flush(const maya::unlocked&) noexcept;

// 停止工作线程并排水。 幂等；拆解期间调用。
void shutdown(const maya::unlocked&) noexcept;

} // namespace agentty::app::settings_cache
