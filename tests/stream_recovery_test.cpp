// stream_recovery_test——错误的转弯自自身恢复.
//
//   1. invalid_encrypted_content: 我们重播的加密推理是没有的
//      打开后端可读性。放下它,再发送,不要失败。
//   2. 上下文溢出,说明模型的真正窗口:
//      窗口和重传; 软-trim使请求适合。
//
// 都通过 app::update 一条活路 StreamError 带子

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

#include <string>

using namespace agentty;

namespace {

Model streaming_model() {
    Model m;
    m.d.current.id = ThreadId{"recovery"};
    Message u; u.role = Role::User; u.text = "go on";
    Message prev; prev.role = Role::Assistant; prev.text = "earlier reply";
    prev.reasoning_encrypted = "gAAAA-old-blob";
    prev.reasoning_site = "chatgpt";
    Message u2; u2.role = Role::User; u2.text = "and now?";
    Message live; live.role = Role::Assistant;   // 还没有流出
    m.d.current.messages = {u, prev, u2, live};
    m.s.phase = phase::Streaming{phase::Active{}};
    return m;
}

Model fail_with(Model m, std::string message, int status) {
    StreamError e{std::move(message), std::nullopt};
    e.http_status = status;
    auto [out, _] = app::update(std::move(m), Msg{msg::StreamMsg{std::move(e)}});
    return out;
}

bool retrying(const Model& m) {
    const auto* s = std::get_if<phase::Streaming>(&m.s.phase);
    return s != nullptr;
}

}  // 命名空间结束。

TEST_CASE("recovery: unreadable replayed reasoning is dropped and the turn resent") {
    Model m = fail_with(streaming_model(),
                        R"(HTTP 400: {"error":{"code":"invalid_encrypted_content",)"
                        R"("message":"The encrypted content could not be verified."}})",
                        400);
    CHECK(retrying(m), "the turn is resent, not failed");
    for (const auto& msg : m.d.current.messages)
        CHECK(msg.reasoning_encrypted.empty(), "no stale blob is sent again");
    CHECK(!m.d.current.messages.back().error.has_value());
}

TEST_CASE("recovery: an overflow that names the window shrinks to it and resends") {
    Model base = streaming_model();
    base.s.context_max = 200000;
    Model m = fail_with(std::move(base),
                        "This model's maximum context length is 128000 tokens. "
                        "However, your messages resulted in 150000 tokens.",
                        400);
    CHECK(m.s.context_max == 128000, "the real window is learned");
    CHECK(retrying(m), "and the turn is resent");
}

TEST_CASE("recovery: an overflow with no number is not guessed at") {
    Model base = streaming_model();
    base.s.context_max = 200000;
    Model m = fail_with(std::move(base),
                        "Your input exceeds the context window of this model.", 400);
    CHECK(m.s.context_max == 200000, "no number, no change");
}
