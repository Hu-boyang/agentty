// tool_result_image_test ——一个显示图像的工具(在 PNG
// )必须 序列化 图片输入其 tool_result 打开 每个 传输 方言,
// 不只是一种,这把针头钉在了 SSOT : 图像 POLICY (图像,
// 空) -skip 媒体 -type 默认) 一次存在 provider::wire ,
// 每个方言 渲染 以它本身 JSON 形状。 -dialect 循环是守护者
// Anthropic-only 漂流(一次运)不能回来.

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/ollama/transport.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/provider/responses/responses.hpp"

#include <nlohmann/json.hpp>

using agentty::ImageContent;
using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ThreadId;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;

namespace {

// 微小有效-looking PNG(魔法字节+略显正文).
ImageContent png() {
    ImageContent img;
    img.media_type = "image/png";
    img.set_bytes(std::string("\x89PNG\x0D\x0A\x1A\x0A", 8) + std::string(32, '\x00'));
    return img;
}

// 用户回合+助手 tool_use 其完成的结果随 `img`.
std::vector<Message> tool_image_messages(const ImageContent& img,
                                         const std::string& note) {
    Message user; user.role = Role::User; user.text = "look at pic.png";
    Message asst; asst.role = Role::Assistant; asst.text = "";
    ToolUse tc;
    tc.id   = ToolCallId{"toolu_img"};
    tc.name = ToolName{"read"};
    tc.args = nlohmann::json{{"path", "pic.png"}};
    tc.status = ToolUse::Done{std::chrono::steady_clock::now(),
                              std::chrono::steady_clock::now(), note, {img}};
    asst.tool_calls.push_back(std::move(tc));
    std::vector<Message> msgs;
    msgs.push_back(std::move(user));
    msgs.push_back(std::move(asst));
    return msgs;
}

// 如果有对象在其中,则递归为真实 `j` 已经 "type" ⁇   ⁇  中  ⁇  的 图像标记 之一 ,
// 每个方言使用( Anthropic "image", OpenAI "input_image") OR
// 携带非 -empty 乌拉玛 -style 顶端 -level "images" 数组。
bool json_has_image(const nlohmann::json& j) {
    if (j.is_object()) {
        const std::string ty = j.value("type", "");
        if (ty == "image" || ty == "input_image") return true;
        if (auto it = j.find("images");
            it != j.end() && it->is_array() && !it->empty())
            return true;
        for (auto& [k, v] : j.items()) if (json_has_image(v)) return true;
    } else if (j.is_array()) {
        for (const auto& e : j) if (json_has_image(e)) return true;
    }
    return false;
}

} // 命名空间结束。

TEST_CASE("tool_result image on every wire dialect") {
    namespace ap  = agentty::provider::anthropic;
    namespace oll = agentty::provider::ollama;
    namespace rsp = agentty::provider::responses;

    const ImageContent img = png();

    // ── Anthropic 信件 ──────────────────────────────────────────────
    {
        Thread t{ThreadId{"t"}, "", tool_image_messages(img, "[image pic.png]"), {}, {}};
        auto j = nlohmann::json::parse(ap::messages_json_string(t));
        // 找到我们的 tool_result 并申明其内容是一个阵列 w/ 图像。
        nlohmann::json tr;
        for (const auto& msg : j)
            for (const auto& b : msg.value("content", nlohmann::json::array()))
                if (b.value("type", "") == "tool_result"
                    && b.value("tool_use_id", "") == "toolu_img")
                    tr = b;
        check(!tr.is_null(), "anthropic: tool_result emitted");
        check(tr["content"].is_array(), "anthropic: image tool_result is an array");
        check(json_has_image(tr["content"]), "anthropic: image block present");
    }

    // ── OpenAI 答复 ChatGPT/Codex + 主办 OpenAI) ────────────────
    {
        agentty::provider::Request req;
        req.messages = tool_image_messages(img, "[image pic.png]");
        auto input = rsp::build_input(req);
        // 电话 function_call_output 为我们 调用 图像必须携带。
        bool found = false;
        for (const auto& item : input) {
            if (item.value("type", "") == "function_call_output"
                && item.value("call_id", "") == "toolu_img") {
                found = true;
                check(item["output"].is_array(),
                      "responses: image tool output is a content array");
                check(json_has_image(item["output"]),
                      "responses: input_image part present");
            }
        }
        check(found, "responses: function_call_output for the call emitted");
    }

    // ── 乌拉玛/ OpenAI-chat (内在角色:\) "tool\") ─────────────────────
    {
        auto arr = oll::build_messages(tool_image_messages(img, "[image pic.png]"),
                                       /*json_protocol=*/false);
        bool found = false;
        for (const auto& m : arr) {
            if (m.value("role", "") == "tool") {
                found = true;
                check(m.contains("images") && m["images"].is_array()
                          && !m["images"].empty(),
                      "ollama: tool result carries an images array");
            }
        }
        check(found, "ollama: role:tool result emitted");
    }

    // ── OpenAI 聊天完成 ─────────────────────────────────────────
    // 角色 : "tool" 信件为文本 -only 图片在工具结果之后,
    // 随即显示用户信息。它被运走丢失: 模型得到了 "image attached"
    // 和没有图像, 并保持 -reading 档案。
    {
        namespace oai = agentty::provider::openai;
        auto arr = oai::build_messages(
            Thread{ThreadId{"t"}, "", tool_image_messages(img, "[image pic.png]"), {}, {}});
        std::size_t tool_at = arr.size(), img_at = arr.size();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].value("role", "") == "tool") tool_at = i;
            if (arr[i].value("role", "") == "user" && arr[i]["content"].is_array())
                for (const auto& part : arr[i]["content"])
                    if (part.value("type", "") == "image_url") img_at = i;
        }
        check(tool_at < arr.size(), "openai: role:tool result emitted");
        check(arr[tool_at]["content"].is_string(), "openai: tool content stays text");
        check(img_at < arr.size() && img_at == tool_at + 1,
              "openai: the image follows the tool results as a user message");
    }
}

TEST_CASE("text-only tool_result keeps the plain shape") {
    namespace ap = agentty::provider::anthropic;

    Message user; user.role = Role::User; user.text = "run it";
    Message asst; asst.role = Role::Assistant; asst.text = "";
    ToolUse tc;
    tc.id = ToolCallId{"toolu_txt"};
    tc.name = ToolName{"shell"};
    tc.args = nlohmann::json{{"command", "echo hi"}};
    tc.status = ToolUse::Done{std::chrono::steady_clock::now(),
                              std::chrono::steady_clock::now(), "hi", {}};
    asst.tool_calls.push_back(std::move(tc));
    std::vector<Message> msgs;
    msgs.push_back(std::move(user));
    msgs.push_back(std::move(asst));

    auto j = nlohmann::json::parse(
        ap::messages_json_string(Thread{ThreadId{"t2"}, "", std::move(msgs), {}, {}}));
    nlohmann::json tr;
    for (const auto& msg : j)
        for (const auto& b : msg.value("content", nlohmann::json::array()))
            if (b.value("type", "") == "tool_result"
                && b.value("tool_use_id", "") == "toolu_txt")
                tr = b;
    check(!tr.is_null(), "text tool_result emitted");
    check(tr["content"].is_string(),
          "text-only tool_result keeps plain-string content");
}
