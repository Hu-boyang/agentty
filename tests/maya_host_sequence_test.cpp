// maya_host_sequence_test - 电话 emit_host_sequence 东道主 -escape 原始的,我从不这样
//
// 验证 maya-level 组件 agentty 用于与合作主机
// 交谈 终端 (Emacs/vterm's OSC 钩子等:
//   • emit_osc (代码, 载荷 ) 建造水井 -formed, ST-terminated OSC,
//   • emit_host_sequence(raw)带有任意的逐字顺序,
//   • 两者都幸存 Cmd::map () (用于嵌入儿童 Msg),
//   • 另一种选择是不同的 变体 手,手,手 手,手臂,手,手臂 手,臂,手,臂 手,胳膊,手,胳膊
//
// 员额 -jaal 形状: 副作用 谓 maya::EmitHostSequence (a 载荷 结构化
// maya/host/effects.hpp ),以及一个程序 Cmd 谓 maya::Cmd<Msg, effects...>
// 命名为 副作用 使用——而不是 maya::Cmd<Msg> ,则已不复存在。
// `inner` 仍为平原 std::variant 因此,这些说法没有改变。
#include <doctest/doctest.h>

#include <maya/host/effects.hpp>
#include <maya/runtime.hpp>

#include <string>
#include <variant>

namespace {

// 两个无关 Msg 类型,这样我们就可以练习映射 <Parent>(child_cmd).
struct ChildMsg { int v = 0; };
struct ParentMsg { int v = 0; };

// 排:仅此两台副作用本试验驱动器.
template <class Msg>
using Cmd = maya::Cmd<Msg, maya::emit_host_sequence, maya::set_title>;

using CCmd = Cmd<ChildMsg>;
using PCmd = Cmd<ParentMsg>;

// 将原始序列从一个 EmitHostSequence Cmd ,或 ,或 "" 否则。
template <class Msg>
std::string sequence_of(const Cmd<Msg>& c) {
    if (auto* e = std::get_if<maya::EmitHostSequence>(&c.inner))
        return e->sequence;
    return {};
}

// 电话 OSC 构建器是一个简单的函数,位于 载荷 现在
CCmd osc(int code, std::string payload) {
    return CCmd(maya::EmitHostSequence{
        "\x1b]" + std::to_string(code) + ";" + std::move(payload) + "\x1b\\"});
}

} // 命名空间结束。

TEST_CASE("maya emit_osc builds a well-formed OSC") {
    // ESC [7];你好 ESC \   (ST-terminated ,该光谱 -preferred 窗体)
    CHECK(sequence_of(osc(7, "hello")) == std::string("\x1b]7;hello\x1b\\"));
}

TEST_CASE("maya emit_osc carries an arbitrary numeric code + payload") {
    CHECK(sequence_of(osc(1337, "path=/tmp/x.cpp;line=42")) ==
          std::string("\x1b]1337;path=/tmp/x.cpp;line=42\x1b\\"));
}

TEST_CASE("maya emit_host_sequence is verbatim") {
    const std::string raw = "\x1b]52;c;YWJj\x1b\\";   // a 数据为: OSC 52号电话前 -built
    CCmd c = CCmd(maya::EmitHostSequence{raw});
    CHECK(sequence_of(c) == raw);
}

TEST_CASE("maya EmitHostSequence survives Cmd::map") {
    auto child = osc(9, "ping");
    PCmd parent = std::move(child).map([](ChildMsg m) { return ParentMsg{m.v}; });
    // map() 保留 副作用 (没有) Msg ),而不是放弃.
    CHECK(sequence_of(parent) == std::string("\x1b]9;ping\x1b\\"));
}

TEST_CASE("maya EmitHostSequence is a distinct variant arm") {
    CCmd host  = CCmd(maya::EmitHostSequence{"x"});
    CCmd title = CCmd(maya::SetTitle{"t"});
    CHECK(std::holds_alternative<maya::EmitHostSequence>(host.inner));
    CHECK(!std::holds_alternative<maya::EmitHostSequence>(title.inner));
    // 空原始序列仍然可以代表( Name 运行时 无 -ops ) , , —— —— , ) .
    CHECK(sequence_of(CCmd(maya::EmitHostSequence{""})) == "");
}
