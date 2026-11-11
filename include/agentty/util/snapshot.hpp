#pragma once
// agentty::util::Snapshot<T> —后台 线程 重新发布的值的 不可变、
// 共享、never-dangling 视图。
//
// ── 设计缺陷就结束了 ──────────────────────────────────────────
//
// `@`和`#` 选择面板都读取后台行走填充的进程
// -wide 缓存。两个同级文件实现了相同的缓存并且在重要
// 的ONE事情上存在分歧：
//
//   files.cpp std::vector<std::string> list_workspace_files(...) // 按值
//   symbols.cpp const std::vector<SymbolEntry>& list_workspace_symbols(...)
//
// 第二个是等待发生的use-after-free，正文显示了原因：
//
//     { std::lock_guard lk(sym_mu());
//     if (自动 c = sym_cache()) 返回 *c;
//     } // ^ 使 缓冲区 保持存活的 shared_ptr
//     COPY 在这里 // 在右大括号处消亡
//     — 并且 // 引用无论如何都会逃逸。
//
// 它是良性的只是因为缓存今天恰好是write-once。
// 添加一个失效（刷新、工作区 开关、上限更改），它就是实时 UAF。
// 同时，by-value 同级是安全的，但会在每个 调用
// 上复制整个向量。
//
// 调用站点在本地都没有做任何错误。
// 缺陷在于 "hand back a value a background thread may replace"
// 没有 TYPE，因此每个站点都发明了自己的答案，其中一个站点发明了一个悬空答案。
//
// 快照<T>是该类型。它拥有 shared_ptr<const T>，因此：
//
//   ·读者的副本 KEEPS 该 BUFFER ALIVE 只要持有 —
//     重新发布交换了缓存的指针，它并没有释放读者正在
//     查看的内容；
//   ·分发一个是O(1)(引用计数碰撞)，而不是深拷贝；
//   • `operator*` / `operator->` 只能通过拥有来访问
//     句柄，所以没办法拼成
//     引用存活时间超过了允许它存在的生命周期。
//   · `empty()` 折叠了 null 情况，因此冷的 缓存 读取为空
//     范围而不是要求每个调用者都为 null-check。
//
// 结果：正确性（没有UAF）和速度（没有副本）不再
// 处于紧张状态，这就是使两兄弟选择相反立场的原因。
//
// ── THREADING CONTRACT  ⁇ 在共享前读此语 ──────────────────
//
// 快照与std::shared_ptr具有相同的线程-safety，
// 并且出于同样的原因：POINTEE是不可变并且自由共享，
// 但是HANDLE是普通对象。
//
//   SAFE任意数量的线程读取SAME 快照对象，
//           或者每个都持有它们自己的副本。这就是 选择面板
//           的情况——读者复制一次句柄，无论生产者重新发布多少次，
//           它的 缓冲区 都无法在其下面释放。
//
//   RACE一个线程复制快照，而另一个ASSIGNS复制到同一个快照对象。
//           这是对一个 `ptr_` 的读取和写入，并且没有任何指针
//           不变性可以修复它。 TSan 标志 它是一个堆
//           -use-after-free，这正是它的本质。
//
// 如果槽由生产者写入并由消费者读取，则它不是裸露的快照——它是
// AtomicSnapshot(如下)。区别在于类型，
// 因此 "which one do I need" 在声明中得到回答，
// 而不是在消毒剂下发现。

#include <cstddef>
#include <memory>
#include <utility>

#include <maya/runtime.hpp>   // maya::published：AtomicSnapshot后面的插槽

namespace agentty::util {

template <class T>
class Snapshot {
public:
    // 默认的快照是EMPTY，而不是空-and-dangerous。下面的每个访问器上都有
    // -defined，因此 "the background walk has not published yet"
    // 在任何 调用 站点都不需要特殊的 -casing。
    Snapshot() = default;

    explicit Snapshot(std::shared_ptr<const T> p) noexcept : ptr_(std::move(p)) {}

    // 值已经发布了吗？与 `empty()` 不同：已发布但真正空的 工作区 是
    // `has_value() && empty()`，这正是 选择面板 的
    // "indexing…" 与 "workspace empty" 提示所需要的
    // 区别 - 旧的 bo​​ol-returning `files_ready()`
    // 在没有第二个函数的情况下无法表达。
    [[nodiscard]] bool has_value() const noexcept { return static_cast<bool>(ptr_); }
    explicit operator bool() const noexcept { return has_value(); }

    // 借用观点。在未发布的 快照 上安全：产生对共享空实例的引用，
    // 而不是取消引用 null。
    [[nodiscard]] const T& operator*() const noexcept { return get(); }
    [[nodiscard]] const T* operator->() const noexcept { return &get(); }

    [[nodiscard]] const T& get() const noexcept {
        if (ptr_) return *ptr_;
        static const T kEmpty{};
        return kEmpty;
    }

    // 范围访问，因此快照<std::vector<X>>直接在
    // 范围-for中工作并且在选择面板已经使用的std算法中。
    [[nodiscard]] auto begin() const noexcept { return get().begin(); }
    [[nodiscard]] auto end()   const noexcept { return get().end(); }
    [[nodiscard]] std::size_t size()  const noexcept { return get().size(); }
    [[nodiscard]] bool        empty() const noexcept { return get().empty(); }

    [[nodiscard]] decltype(auto) operator[](std::size_t i) const noexcept {
        return get()[i];
    }

    // 所属指针，为AtomicSnapshot的store()。
    // 不是阅读表面的一部分——调用者通过上面的访问器进行工作。
    [[nodiscard]] std::shared_ptr<const T> share() const noexcept { return ptr_; }

private:
    std::shared_ptr<const T> ptr_;
};

// 将 `value` 发布为 不可变 快照。 shared_ptr
// 中的 `const` 是 加载-bearing 部分：一旦发布，
// 快照 就无法在读者背后进行变异，因此读者根本不需要 锁——只需要 缓存
// 内部的指针交换即可。
template <class T>
[[nodiscard]] Snapshot<T> make_snapshot(T value) {
    return Snapshot<T>{std::make_shared<const T>(std::move(value))};
}

// ── AtomicSnapshot<T> ── a 快照 制作人重印的插槽 ──────
//
// "one thread publishes, others read"的
// THE类型，是后台作业填充的该代码库中的每个缓存：工作区文件列表、
// 符号索引、git-status映射。
//
// 将其写为裸`Snapshot`字段加上互斥量是这些站点
// 中的每一个都手动完成的，并且hand
// -rolled版本对于LOAD是否也需要锁存在分歧（确实
// 如此——复制句柄读取`ptr_`）。错误是无声的：受指点确实不是可变，
// 所以代码看起来显然很好，竞态 仅在手柄上。
//
// 这是 maya::published<const T> 上的薄
// 面：published 拥有atomic<shared_ptr>（或者，
// 在没有该专门化的标准库上，小互斥量），因此可移植性问题在
// jaal中一次性解决。这会在顶部添加 快照 读取表面。
template <class T>
class AtomicSnapshot {
public:
    AtomicSnapshot() = default;

    // 采取私人句柄。对 调用 与 store() 同时安全：
    // 只要调用者持有它，返回的 快照 就拥有它的 缓冲区。
    [[nodiscard]] Snapshot<T> load() const noexcept {
        return Snapshot<T>{cell_.current()};
    }

    // 重新发布。已经掌握了把手的读者继续看到他们自己的一代；
    // 新 加载 看到这个。
    void store(Snapshot<T> s) noexcept { cell_.publish(s.share()); }

    void store(T value) { store(make_snapshot(std::move(value))); }

    // 仅当尚未发布任何内容时才发布。如果这个赢了，那就对了。
    bool store_if_empty(T value) {
        return cell_.publish_if(nullptr, make_snapshot(std::move(value)).share());
    }

    // 有什么已经发布了吗？与空的 载荷 不同 - "indexing…" 与
    // "workspace empty" 的区别是 选择面板 渲染。
    [[nodiscard]] bool has_value() const noexcept { return static_cast<bool>(cell_.current()); }

    void reset() noexcept { (void)cell_.take(); }

private:
    maya::published<const T> cell_;
};

} // namespace agentty::util
