// SPDX-License-Identifier: Apache-2.0
//
// snapshot_picker_test.cpp — 锁 换了手的两只原始人 -copied 选择面板
// 管道: 水管, 水管: 水管道, 水管道:
//
//   util::Snapshot ——关闭使用. -after-free 说
//                         list_workspace_symbols 已经( 它复制
//                         了自有的) shared_ptr 输入块 -scoped 当地人,
//                         并返回一个 已经过期的参考文献) 不支付 files.cpp 深层复制品。
//
//   ui::FilteredPicker ——关上寒冷 -open 填充孔:
//                         用于居住的填充 一个 消息臂在 an 后面
//                         `entries.empty()` 警卫,所以 选择面板
//                         索引发布前打开,然后 ARROWED (无) 带类型
//                         的 ) 从未填充,一旦查询是非 -empty 警卫
//                         再也打不开枪了

#include "agentty/runtime/panel/filtered_picker.hpp"
#include "agentty/util/snapshot.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using agentty::ui::FilteredPicker;
using agentty::ui::SnapshotSource;
using agentty::util::Snapshot;
using agentty::util::make_snapshot;

namespace {

// 底弦滤波器,足以锻炼记忆缓存,而不会拉入真正的模糊
// 计分器.
std::vector<std::size_t> substr_filter(const std::vector<std::string>& v,
                                       std::string_view q) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (q.empty() || v[i].find(q) != std::string::npos) out.push_back(i);
    return out;
}

} // 命名空间结束。

TEST_CASE("Snapshot: a default snapshot is empty, not null-and-dangerous") {
    Snapshot<std::vector<std::string>> s;
    CHECK(!s.has_value());
    CHECK(s.empty());
    CHECK(s.size() == 0);
    // 每一个进入者都很好 -defined 有关未出版的 快照 —
    // "the walk hasn't landed yet" 不需要特别 -casing 任何活动 调用 网站。
    CHECK(s.get().empty());
    for ([[maybe_unused]] const auto& x : s) FAIL("empty snapshot must not iterate");
}

TEST_CASE("Snapshot: a reader keeps its buffer alive across a republish") {
    // THE直接使用-after-free. 读者持有快照;
    // 然后缓存发布全新的值,读者的观点必须保持有效且不变——
    // 根据旧的const-ref合同,这是一首令人费解的读物。
    auto published = make_snapshot(std::vector<std::string>{"a", "b", "c"});

    const auto reader = published;         // O(1) 重计折叠, 而不是复制
    published = make_snapshot(std::vector<std::string>{"z"});   // 重排( R)

    REQUIRE(reader.size() == 3);
    CHECK(reader[0] == "a");
    CHECK(reader[2] == "c");
    CHECK(published.size() == 1);
    CHECK(published[0] == "z");
}

TEST_CASE("Snapshot: has_value and empty are different questions") {
    // "published but genuinely empty workspace" 数字
    // "still indexing" - 两者 渲染 不同的暗示, 和它们合起来 是使寒冷
    // -open 路径不可见。
    const auto indexing = Snapshot<std::vector<std::string>>{};
    const auto empty_ws = make_snapshot(std::vector<std::string>{});

    CHECK(!indexing.has_value());
    CHECK(indexing.empty());

    CHECK(empty_ws.has_value());
    CHECK(empty_ws.empty());
}

TEST_CASE("FilteredPicker: refills when the index publishes LATE") {
    // 回归,打开冷却,然后永远 ARROW ——永远不要键入。旧代码只重新填入
    // "printable typed" 手臂,所以这个 选择面板 留下来
    // "indexing…" 永远
    bool ready = false;
    std::vector<std::string> backing{"alpha.cpp", "beta.cpp", "gamma.hpp"};

    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [&] { return ready; },
            .fetch = [&] { return make_snapshot(backing); },
        },
        substr_filter,
    };

    // 冷:没什么可显示的,还有 选择面板 说得没错
    CHECK(!p.source_ready());
    CHECK(p.entries().empty());
    CHECK(p.selected() == nullptr);

    // 脚底行走着陆,没有向选择面板发送信息。
    ready = true;

    // 纯光标移动——曾经完全跳过重填的手臂.
    p.move(1);

    CHECK(p.source_ready());
    REQUIRE(p.entries().size() == 3);
    REQUIRE(p.selected() != nullptr);
    CHECK(*p.selected() == "beta.cpp");
}

TEST_CASE("FilteredPicker: refills even when the query is already non-empty") {
    // 下半部的同虫: 旧的守军是
    // `if (entries.empty())` ,所以曾经有一个查询 带
    // 类型的 填充剂永远无法再开火 和 选择面板 一辈子都保留着一份部分名单
    bool ready = false;
    std::vector<std::string> backing{"alpha.cpp", "beta.cpp"};

    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [&] { return ready; },
            .fetch = [&] { return make_snapshot(backing); },
        },
        substr_filter,
    };

    p.type(U'a');                 // 带类型的 却依然寒冷
    CHECK(p.filtered().empty());

    ready = true;
    CHECK(p.entries().size() == 2);
    CHECK(p.filtered().size() == 2);   // 两者均包含'a'
}

TEST_CASE("FilteredPicker: the cursor cannot point outside the filtered list") {
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{
                              "alpha", "beta", "gamma"}); },
        },
        substr_filter,
    };

    p.move(100);                  // 走到尽头
    CHECK(p.index() == 2);
    REQUIRE(p.selected() != nullptr);
    CHECK(*p.selected() == "gamma");

    p.move(-100);
    CHECK(p.index() == 0);

    // 缩小列表必须拉回光标的射程,
    // 而不是让它指向已经不存在的行.
    p.move(2);
    CHECK(p.index() == 2);
    p.type(U'b');                 // 过滤器下调至 "beta"
    CHECK(p.filtered().size() == 1);
    CHECK(p.index() == 0);
    REQUIRE(p.selected() != nullptr);
    CHECK(*p.selected() == "beta");
}

TEST_CASE("FilteredPicker: typing resets the cursor to the top") {
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{
                              "aa", "ab", "ac"}); },
        },
        substr_filter,
    };
    p.move(2);
    CHECK(p.index() == 2);
    p.type(U'a');
    CHECK(p.index() == 0);        // 内容 -filtering 使旧行变得毫无意义
}

TEST_CASE("FilteredPicker: backspace reports whether anything was erased") {
    // 电话 `@` 财务报告和已审计财务报表 Ctrl+K 选择面板 使用此词来表示
    // 查询为空时按退格会关闭面板。
    // 返回它可以保持测试和变异原子,而不是两个可以分开的语句。
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{"x"}); },
        },
        substr_filter,
    };

    CHECK(!p.backspace());        // 空查询 ~ "nothing to erase" 关
    p.type(U'x');
    CHECK(p.query() == "x");
    CHECK(p.backspace());         // 已删除
    CHECK(p.query().empty());
    CHECK(!p.backspace());        // 又空了 ! !
}

TEST_CASE("FilteredPicker: backspace pops a whole UTF-8 code point") {
    // 从一个多-byte序列中弹出一个字节,留下一个无效的字符串,
    // 然后由过滤器扫描。
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{"x"}); },
        },
        substr_filter,
    };
    p.type(U'é');                 // 2 bytes
    CHECK(p.query().size() == 2);
    CHECK(p.backspace());
    CHECK(p.query().empty());     // 而不是一个零碎的续作 字节
}

TEST_CASE("FilteredPicker: the filter runs once per (query, snapshot)") {
    // 电话 记忆缓存. 状态更新函数 并查看每键键的读数;没有这个,
    // 扫描会运行两三次,只读一个键。
    int calls = 0;
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{
                              "alpha", "beta"}); },
        },
        [&](const std::vector<std::string>& v, std::string_view q) {
            ++calls;
            return substr_filter(v, q);
        },
    };

    (void)p.filtered();
    (void)p.filtered();
    (void)p.filtered();
    CHECK(calls == 1);            // 三读,一读

    p.type(U'a');
    (void)p.filtered();
    (void)p.filtered();
    CHECK(calls == 2);            // 查询更改一次 —— 一次完全通过
}

TEST_CASE("FilteredPicker: selected() folds the empty and out-of-range cases") {
    FilteredPicker<std::string> p{
        SnapshotSource<std::string>{
            .ready = [] { return true; },
            .fetch = [] { return make_snapshot(std::vector<std::string>{"alpha"}); },
        },
        substr_filter,
    };
    REQUIRE(p.selected() != nullptr);
    p.type(U'z');                 // 匹配无
    CHECK(p.filtered().empty());
    CHECK(p.selected() == nullptr);   // 唯一的失败模式,是 带类型的
}
