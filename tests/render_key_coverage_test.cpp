// render_key_coverage_test.cpp ——针头 Message::compute_render_key (a) 覆盖面。
//
// 原因 此 TEST EXISTS
// ====================
// compute_render_key ( ) 是 : 或 : CONTENT-identity
// 散开 maya 渲染 缓存 键上(通过) assistant_run_hash_id
// /每笔 -turn hash_id 输入 turn.cpp。。 内容。 缓存 合同:
// a hash_id 唯一决定已提供 存储单元。如果信件字段为 AFFECTS 已换卡,
// 但 不 混合为 compute_render_key ( ) 键不变, maya
// 快速路径为 STALE 缓存 存储单元 ,用户会看到腐败(旧内容上冻结的卡)。
// 这是准确的 "hash doesn't cover rendered content"
// 隐藏的课 终端-resize 错误进入 maya 高度( 高度 ) 缓存 (分别定.
//
// compute_render_key 文档 不变量 注释中( 以
// "keep in sync with the reads in turn.cpp" ,但评论不是执行。
// 这个测试使它可以执行:一个声明表,其中包含: "fields that must move the key."
// 添加已渲染的信件字段 ~ 在此添加一行 ~ 测试到此为止。 REQUIRES 匹配( T) mix() 线条。
// 它反射 visual_hash_coverage_test 费用 Program::visual_hash.
//
// 关于大小与字节内容的区别，见 compute_render_key。
// 混合字符串 SIZES ,没有 字节 ,按设计——助手文本为附件 -only
// 期间 流式传输 ,因此大小独占地识别内容。这些轴因此改变 LENGTH ,
// 匹配真实的变异形状。 (相同) -length 输入 -place 字节 互换
// 是合同外的,没有断言;对于结算的卡片,则从未发生 缓存 (服务。 )
#undef NDEBUG
#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using agentty::Message;
using agentty::Role;

namespace {

struct Axis {
    const char*                    name;
    std::function<void(Message&)>  mutate;
};

// 一个代表助理信息, 携带着各种卡片, 所以每个轴突变
// 了一个字段, 实际上是被覆盖的。
Message base_message() {
    Message m;
    m.role           = Role::Assistant;
    m.text           = "settled body text";
    m.streaming_text = "live tail";
    m.pending_stream = "buffered";
    m.error          = std::nullopt;
    // 智能路由卡.
    m.smart_routing          = true;
    m.smart_route_model      = "claude-opus-4-5";
    m.smart_route_effort     = "high";
    m.smart_route_complexity = "standard";
    m.smart_route_note       = "bumped for ambiguity";
    m.smart_route_orchestrate = false;
    m.smart_route_subagents   = false;
    // 叉卡(原变种).
    m.fork_note       = true;
    m.fork_transcript = "/tmp/parent.md";
    return m;
}

const std::vector<Axis>& render_axes() {
    static const std::vector<Axis> axes = {
        {"role", [](Message& m) { m.role = Role::User; }},
        {"text grows", [](Message& m) { m.text += " more"; }},
        {"streaming_text grows", [](Message& m) { m.streaming_text += " x"; }},
        {"pending_stream grows", [](Message& m) { m.pending_stream += " y"; }},
        {"error appears", [](Message& m) { m.error = "boom"; }},
        {"tool_calls appended", [](Message& m) {
            agentty::ToolUse tc; tc.name = agentty::ToolName{"shell"};
            m.tool_calls.push_back(tc);
        }},
        {"attachment appended", [](Message& m) {
            agentty::Attachment a; a.kind = agentty::Attachment::Kind::FileRef;
            // byte_count 是什么 说吧 compute_render_key
            // 混合( 不 编号 正文 ,它可能是一个非物质化的blob ref),
            // 所以必须设定这个轴来实际移动散列.
            a.name = "f.txt"; a.body.set_bytes("data"); a.byte_count = 4;
            m.attachments.push_back(a);
        }},
        {"is_compact_summary toggles", [](Message& m) {
            m.is_compact_summary = !m.is_compact_summary;
        }},
        {"proactive identity toggles", [](Message& m) {
            if (m.proactive) m.proactive.reset();
            else             m.proactive = Message::ProactiveContext{};
        }},
        {"proactive expanded toggles", [](Message& m) {
            if (!m.proactive) m.proactive = Message::ProactiveContext{};
            m.proactive->expanded = !m.proactive->expanded;
        }},
        {"reasoning text (thinking) changes length", [](Message& m) {
            // 驱动器 Message::reasoning_display_text (),
            // 推理障碍 渲染。必须在里面 渲染 键或缓存元素服务 a 过期 (
            // 或失踪) "Thought for N tokens" 总结。
            m.thinking += "chain of thought step";
        }},
        {"smart_routing toggles", [](Message& m) {
            m.smart_routing = !m.smart_routing;
        }},
        {"smart_route_model changes length", [](Message& m) {
            m.smart_route_model += "-mini";
        }},
        {"smart_route_effort changes length", [](Message& m) {
            m.smart_route_effort = "xhigh";
        }},
        {"smart_route_complexity changes length", [](Message& m) {
            m.smart_route_complexity = "complex";
        }},
        {"smart_route_note changes length", [](Message& m) {
            m.smart_route_note += " (learned prior)";
        }},
        {"smart_route_orchestrate toggles", [](Message& m) {
            m.smart_route_orchestrate = !m.smart_route_orchestrate;
        }},
        {"smart_route_subagents toggles", [](Message& m) {
            m.smart_route_subagents = !m.smart_route_subagents;
        }},
        {"fork_note toggles", [](Message& m) { m.fork_note = !m.fork_note; }},
        {"fork_transcript changes length", [](Message& m) {
            m.fork_transcript += "/deeper.md";
        }},
        {"proactive confidence bucket", [](Message& m) {
            if (!m.proactive) m.proactive = Message::ProactiveContext{};
            const double cur = m.proactive->confidence.value_or(0.0);
            m.proactive->confidence = cur < 0.5 ? 0.9 : 0.1;
        }},
    };
    return axes;
}

} // 命名空间结束。

TEST_CASE("render_key covers every rendered Message field") {
    // 无 -vacuity 警卫: 2 IDENTICAL 信息必须等距,
    // 和否 -op 不得 移动关键字——证明以下断言比较了真实的,
    // 不同的值(不是一散列,它改变于每一秒) 调用 ,
    // 它将使每个轴 "pass" 虚空无故.
    {
        Message a = base_message();
        Message b = base_message();
        CHECK(a.compute_render_key() == b.compute_render_key());
        const std::uint64_t k = a.compute_render_key();
        (void)a.text.size();                 // a 真正的第-op号改为
        CHECK(a.compute_render_key() == k);  // 稳定 调用
    }

    int failures = 0;
    for (const auto& ax : render_axes()) {
        Message before = base_message();
        const std::uint64_t k0 = before.compute_render_key();

        Message after = base_message();
        ax.mutate(after);
        const std::uint64_t k1 = after.compute_render_key();

        if (k0 == k1) {
            ++failures;
            std::fprintf(stderr,
                "render_key COVERAGE GAP: mutating '%s' did NOT change "
                "compute_render_key() \u2014 the render cache would serve stale "
                "cells for this field. Add a mix() for it in "
                "Message::compute_render_key (conversation.hpp).\n",
                ax.name);
        }
    }
    CHECK(failures == 0);
}
