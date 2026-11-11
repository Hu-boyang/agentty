#pragma once
// agentty::provider::responses-OpenAI**响应API
// **方言，作为由per-host描述符参数化的共享编解码器。
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么存在这个模块
// ─────────────────────────────────────────────────────────────────────────
// 两个截然不同的主机说同样的传输方言：
//
//   ChatGPT（Codex）使用 chatgpt.com/backend-api/codex/responses 和 ChatGPT OAuth。
//   ·GitHub副驾驶API.*.githubcopilot.com/responses，代理令牌+
//                      Auto-session令牌
//
// 两者的昂贵部分是IDENTICAL：将agentty对话转变为
// `input[]`（工具-call配对、图像部分、age-tiered 传输预算），
// 构建`tools[]`，并运行将`response.output_text.delta`、
// `response.reasoning_summary_text.delta`、
// `response.output_item.*`和朋友翻译
// 回agentty的SSE状态机消息。该机器大约有 500
// 条线路，与拨打的 WHO 无关。
//
// 主机之间的实际区别仅在于HTTP信封。测量的，
// 不是假设的（针对两个后端的实时探测）：
//
//   ┌───────────────┬──────────────────────────┬───────────────────────────┐
//   │               │ ChatGPT                  │ Copilot                   │
//   ├───────────────┼──────────────────────────┼───────────────────────────┤
//   │ 主机与路径    │ chatgpt.com/backend-api/ │ api.individual.githubcopi │
//   │               │ codex/responses          │ lot.com/responses         │
//   │ 认证          │ OAuth 访问令牌           │ 来自代理的令牌           │
//   │               │ 另带 chatgpt-account-id  │ 通过令牌交换获得          │
//   │ 额外请求头    │ session_id、originator   │ copilot-session-token    │
//   │               │                          │ x-github-api-version,     │
//   │               │                          │ editor identification     │
//   │ 请求体附加项  │ include[] 中的加密内容   │ reasoning.summary        │
//   │               │ reasoning, store:false   │                           │
//   │ 错误说明      │ 提示运行 agentty login   │ 订阅权益相关说明         │
//   └───────────────┴──────────────────────────┴───────────────────────────┘
//
// 所以分割是：ONE编解码器，以及每个主机的小`Site`描述符。
// 主机提供 POST 的位置以及如何授权；第一个 字节 的
// 所有 下游 都是共享的。添加第三个响应后端是一个新的站点行，
// 而不是状态机的第二个副本。
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么是函数指针的描述符，而不是虚基
// ─────────────────────────────────────────────────────────────────────────
// agentty的提供者是一个“概念”(`stream(Request, EventSink) -> StreamResult`)，
// 由没有公共基础的六个不相关的类来满足。
// 这里引入层次结构将是提供者层中唯一的vtable。
// 指定的 -init 行普通函数指针与 `auth::vault::of()`
// 已使用的习惯用法相匹配 — 数据，可简单测试，
// 并且每个主机的行为在一个位置都可读。

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"   // provider::StreamResult
#include "agentty/io/http.hpp"

#include <expected>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace agentty::provider::responses {

// 在发出请求之前主机必须解决的所有问题：WHERE到POST和
// WITH 内容 凭据。由 Site::authorize 返回，
// 因此（阻塞，可能刷新）身份验证工作每回合发生一次，远离编解码器的路径。
struct Target {
    std::string host;                 // "chatgpt.com"
    std::uint16_t port = 443;
    // 通过普通http(vLLM，llama.cpp)的self-run服务器：
    // 没有TLS，以及本地超时梯子，其没有总上限(#108)。
    bool plaintext = false;
    std::string path;                 // "/backend-api/codex/responses"
    http::Headers headers;            // auth+身份验证，完全形成
    // 模型头戴在传输上。主机可能会 REWRITE 呼叫者的型号（
    // Copilot 的自动会话选择服务器-blessed slug），
    // 因此编解码器始终发送此信息而不是 req.model。
    std::string model;
};

// 讲响应方言的主持人。
//
// `authorize`执行阻塞凭据工作并返回目标或user
// -facing错误字符串（已经针对该主机表述——编解码器从不发明auth散文）。
// 它还可能会突变 `req`（e.g。副驾驶固定 Auto
// -session 的型号选择）。
struct Site {
    std::string_view id;              // "chatgpt" | "copilot" —路由密钥。

    // 解析凭据+目的地。失败时出现错误字符串。在 工作线程
    // 回合运行，不持有 锁（它可能会在令牌刷新时阻塞）。
    std::expected<Target, std::string> (*authorize)(const maya::unlocked& cap,
                                                     provider::Request& req);

    // Host-specific请求-body字段，在共享构建器之后应用。
    // ChatGPT增加include[]/store；副驾驶添加了 reasoning.summary。
    // 两者都收到相同的中性 正文。
    void (*decorate_body)(nlohmann::json& body, const provider::Request& req);

    // 将非2xx响应转变为可操作的诊断。主机表述自己的补救措施（
    // "run `agentty login`…" 与权利措辞）。
    std::string (*explain_http_error)(int status, std::string_view body);

    // 服务器向 `rejected_token` 应答 401。
    // 刷新 凭据 一次（无论其到期时间如何）并返回要重新发送到的新目标，
    // 或者要显示的错误。对于没有什么可刷新的主机来说为空。
    std::expected<Target, std::string> (*reauthorize)(const maya::unlocked& cap,
                                                       provider::Request& req,
                                                       const Target& rejected) = nullptr;
};

// 数据流对阵`site`一回合。拥有整个生命周期：授权、构建 正文、
// POST、翻译 SSE → 消息，并完成 SHARED 数据流 尾声，
// 因此此路径与 Anthropic/OpenAI. 相同地终止
[[nodiscard]] provider::StreamResult stream(const maya::unlocked& cap,
                                            const Site& site,
                                            provider::Request req,
                                            provider::EventSink sink);

// 同样，对于已经解析了本轮目标的主机：编解码器跳过 site.authorize
// 并使用给定的 `target`。这就是主机在没有侧通道的情况下
// 传递 per-turn 上下文的方式。
[[nodiscard]] provider::StreamResult stream(const Site& site,
                                            std::expected<Target, std::string> target,
                                            provider::Request req,
                                            provider::EventSink sink);

// ── 解码器(供测试和需要的宿主使用) ────────

// 对话 → 响应 `input[]`。
//
// `site`是该正文正在构建的主机FOR(`Site::id`
// -"chatgpt"，"copilot")的id。它控制加密的
// -reasoning 重播：一个 blob 是 account
// -scoped 密文，因此它只能被发送回创建它的站点。
// 空意味着 "no site context" 且不重播任何内容，
// 这是测试和任何未通过线程的调用者的安全默认值。
[[nodiscard]] nlohmann::json build_input(const provider::Request& req,
                                         std::string_view site = {});
// agentty ToolSpecs → 响应 `tools[]`。
[[nodiscard]] nlohmann::json build_tools(const provider::Request& req);
// 中立请求正文(model/input/tools/stream+推理梯)。
// 主机通过Site::decorate_body将他们的额外内容分层在顶部。
[[nodiscard]] nlohmann::json build_body(const provider::Request& req,
                                        std::string_view site = {});

// 脚本化的SSE `data:` 载荷→状态更新函数将看到的Msg序列。
// 每个Responses-dialect测试驱动的单一入口点。
// `show_reasoning`镜像Request::show_reasoning，
// 因此测试可以固定隐藏的-reasoning路径；默认为 true，这是每个现有
// 测试所期望的捕获语义。
[[nodiscard]] std::vector<Msg> parse_sse_for_test(
    const std::vector<std::string>& sse_data_lines,
    bool show_reasoning = true);

} // namespace agentty::provider::responses
