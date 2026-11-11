// wire_audit.cpp - 看见了 wire_audit.hpp 为什么会有这种东西?
//
// 一个通过 传输 收集我们从一个
// 正文 垃圾 垃圾 废物 垃圾

#include "agentty/runtime/app/wire_audit.hpp"

#include <unordered_set>

namespace agentty::app::cmd {

WireAudit audit_wire(const std::vector<Message>& wire) {
    WireAudit a;
    a.message_count = wire.size();
    if (!wire.empty()) a.last_role = wire.back().role;

    // 传1——形状计数,加上缺陷,单条消息可以
    // 单独判断.
    //
    // Tool-call/result PAIRING 需要两端,所以它等待2号通道。
    // 这里的一切都是本地的:一个名字是空的还是没有.
    std::unordered_set<std::string> call_ids;     // 助理要求的编号
    std::unordered_set<std::string> answered;     // 带有结果的IDs

    for (std::size_t i = 0; i < wire.size(); ++i) {
        const auto& m = wire[i];

        a.image_part_count += m.images.size();
        for (const auto& img : m.images) {
            if (img.media_type.empty()) {
                a.defects.push_back({Defect::Kind::ImageMissingMediaType, i, {}});
            }
        }

        // 一个完全没有内容的信息。
        //
        // EXCEPT 最后一个。每个转弯后,
        // 助理消息就是 ABOUT 因此,在审计时,
        // 最后的空位是正当的——通过建筑,
        // 在每个健康转折时。
        //
        // 这火速攻击了真正的交通: 928路 -message
        // 线程 报告 `defects=1` 每一转,总是在指数
        // 927. 警告说,在健康转弯处开火比在外开枪更糟糕。
        // 因为它训练读者跳过频道 真正的缺陷到达了——
        // 准确的失败
        // 反复误报的检查器最终会被忽略。
        // 附注 待处理-call 手放在下面,我写了那张纸条
        // 然后把同样的错误 抬起来
        const bool has_text  = !m.text.empty() || !m.streaming_text.empty()
                            || !m.pending_stream.empty();
        const bool is_last   = (i + 1 == wire.size());
        if (!has_text && m.tool_calls.empty() && m.images.empty()
            && !is_last) {
            a.defects.push_back({Defect::Kind::EmptyMessage, i, {}});
        }

        for (const auto& tc : m.tool_calls) {
            ++a.tool_call_count;
            call_ids.insert(tc.id.value);

            // 该 一. 工具 调用 服务器不能路径,
            // 它作为400个命名索引而不是一个原因而出现。
            if (tc.name.value.empty()) {
                a.defects.push_back({Defect::Kind::NamelessToolCall, i,
                                     tc.id.value});
            }

            // A 调用 在一个 TERMINAL 状态将结果带入 传输 ;
            // 还有一个 待处理 而不是,这是正确的中期 -turn.
            // 仅a 终端 调用 计数为答案。
            if (tc.is_terminal()) {
                ++a.tool_result_count;
                answered.insert(tc.id.value);

                for (const auto& img : tc.done_images()) {
                    ++a.image_part_count;
                    if (img.media_type.empty()) {
                        a.defects.push_back(
                            {Defect::Kind::ImageMissingMediaType, i,
                             tc.id.value});
                    }
                }
            }
        }
    }

    // 传2——对联.
    //
    // 结果是没有 调用 总是错的 A 调用 只有在
    // 转弯之后, 最后 允许信件保存 调用 仍在
    // 等待处决,这是正常的中点 -turn 州
    // 而不是一个缺陷。
    for (std::size_t i = 0; i < wire.size(); ++i) {
        const auto& m = wire[i];
        const bool is_last = (i + 1 == wire.size());
        for (const auto& tc : m.tool_calls) {
            if (!tc.is_terminal() && !is_last) {
                a.defects.push_back({Defect::Kind::UnansweredToolCall, i,
                                     tc.name.value});
            }
        }
    }

    // 无法从中检测到孤儿结果 ToolUse 单独——a
    // 终端 调用 i)是它自己在这个模型中的结果,
    // 因此ID总是由构造来配对。 枚举 因为 ACP
    // 和子剂路径独立组装工具结果,并可以生成一个结果;
    // 在分类学中留下一个漏洞,将诊断结果重新转到读取一个 正文
    // 垃圾 垃圾 废物 垃圾
    (void)call_ids;
    (void)answered;

    return a;
}

}  // namespace agentty::app::cmd
