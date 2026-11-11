// codex_login_flow_test - 进去 -app ChatGPT (Codex 登录( I)
// 状态更新函数 不需要网络的臂膀 I/O.
//
// 锁 第一次的纯过渡 -class 输入 -app ChatGPT 签名 -in:
//
//   A. LoginPickMethod{'3' 将军的选手 弹出面板 → ChatGptWaiting
//      (和无) -none Cmd : 启动Aync回路登录).
//   B. CodexLoginDone 时 间 时 时 , ChatGptWaiting ~ 失败{消息}。
//   C. CodexLoginDone 到达时 弹出面板 谓 不 ChatGptWaiting
//      (用户) Esc 放弃了 没有州级的改变
//   D. Anthropic-scoped 添加( E) -account 选择忽略 ChatGPT 键 '3'.
//   E. ChatGPT帐号列表中添加的行发射 OAuth直接.
//   F. 设备-code 进度充充等待状态.
//   G. 过时装置 -code 后的进展 取消 被忽略。
//   H. 过期尝试不能覆盖较新的尝试代码。
//   一、结束会议 弹出面板 合作旅行 工作线程 取消.
//
// 成功之路( 3) CodexLoginDone {creds} 运行 commit_provider_switch ,
// 需要完全 -installed Deps + 过程 -global 提供者
// 注册表 由现场提供者负责 -switch 测试,不是在这里。

#include <string>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

using agentty::Model;
namespace login = agentty::ui::login;
namespace msg = agentty::msg;

static int g_checks = 0;

TEST_CASE("codex login flow") {
    std::printf("codex_login_flow_test\n");

    // A. 选取+键 '3' → ChatGptWaiting ,并附有发射装置 Cmd.
    {
        Model m;
        m.ui.login = login::Picking{};
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginPickMethod{U'3'}});
        check(std::holds_alternative<login::ChatGptWaiting>(m2.ui.login),
              "A: '3' from Picking enters ChatGptWaiting");
        // 没有 Cmd 更多: 进入状态是开始 工作线程.
        // subscribe() 返回 a 数据流 尝试时按键,
        // 所以州 IS 发射,离开它就是 取消.
        check(cmd.is_none(),
              "A: the worker is a subscription, not a launch Cmd");
    }

    // B. A 等待时失败 CodexLoginDone / 与信件失败。
    {
        Model m;
        m.ui.login = login::ChatGptWaiting{.attempt_id = 1};
        agentty::auth::OAuthError err{
            agentty::auth::OAuthErrorKind::Network, "callback timed out"};
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m),
            msg::LoginMsg{agentty::CodexLoginDone{
                .attempt_id = 1,
                .result = std::unexpected(err),
            }});
        auto* failed = std::get_if<login::Failed>(&m2.ui.login);
        check(failed != nullptr, "B: failure enters Failed state");
        check(failed && failed->message.find("timed out") != std::string::npos,
              "B: Failed carries the rendered error message");
        (void)cmd;
    }

    // C. 迟到 CodexLoginDone 当 弹出面板 岂不是 ChatGptWaiting 被删除。
    {
        Model m;
        m.ui.login = login::Closed{};
        agentty::auth::OAuthError err{
            agentty::auth::OAuthErrorKind::Network, "late arrival"};
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m),
            msg::LoginMsg{agentty::CodexLoginDone{
                .attempt_id = 1,
                .result = std::unexpected(err),
            }});
        check(std::holds_alternative<login::Closed>(m2.ui.login),
              "C: a late result after Esc leaves the modal Closed");
        (void)cmd;
    }

    // D. 问 题 Anthropic-scoped 添加流量不能逃入 ChatGPT 欧奥特
    {
        Model m;
        m.ui.login = login::Picking{.provider = "anthropic"};
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::LoginPickMethod{U'3'}});
        auto* picking = std::get_if<login::Picking>(&m2.ui.login);
        check(picking && picking->provider == "anthropic",
              "D: Anthropic add flow ignores ChatGPT choice");
        check(cmd.is_none(), "D: no ChatGPT OAuth command is launched");
    }

    // E.ChatGPT只有一个加法,因此其加法行直接启动OAuth.
    {
        Model m;
        login::AccountList accounts;
        accounts.provider = "chatgpt";
        accounts.provider_label = "ChatGPT";
        accounts.cursor = 0; // 空列表: 行0为 "+ Add another account…"
        m.ui.login = std::move(accounts);
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::AccountSelect{}});
        check(std::holds_alternative<login::ChatGptWaiting>(m2.ui.login),
              "E: ChatGPT add row enters ChatGptWaiting directly");
        // 进入状态 IS 发射 subscribe() 返回“%s” 工作线程
        // 's 数据流 已按键尝试。见案例A。
        check(cmd.is_none(),
              "E: ChatGPT add row starts the worker via the state, not a Cmd");
    }

    // F. Decast -code 进度更新直播等待面板.
    {
        Model m;
        m.ui.login = login::ChatGptWaiting{
            .attempt_id = 42,
            .device_auth = true,
        };
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::CodexDeviceCodeReady{
                .attempt_id = 42,
                .verification_url = "https://auth.openai.com/codex/device",
                .user_code = "ABCD-EFGH",
            }});
        auto* waiting = std::get_if<login::ChatGptWaiting>(&m2.ui.login);
        check(waiting && waiting->device_auth,
              "F: device-code progress remains in device mode");
        check(waiting && waiting->authorize_url.find("/codex/device") != std::string::npos,
              "F: device verification URL reaches waiting state");
        check(waiting && waiting->user_code == "ABCD-EFGH",
              "F: one-time code reaches waiting state");
        check(cmd.is_none(), "F: progress update launches no second command");
    }

    // G. 废弃的密码 工作线程 不得重开 已取消 弹出面板.
    {
        Model m;
        m.ui.login = login::Closed{};
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::CodexDeviceCodeReady{
                .attempt_id = 7,
                .verification_url = "https://auth.openai.com/codex/device",
                .user_code = "LATE-CODE",
            }});
        check(std::holds_alternative<login::Closed>(m2.ui.login),
              "G: late device code after Esc leaves modal closed");
        check(cmd.is_none(), "G: late device code launches no command");
    }

    // H. 尝试A的进展不能改变新的尝试B。
    {
        Model m;
        m.ui.login = login::ChatGptWaiting{
            .attempt_id = 200,
            .device_auth = true,
            .user_code = "CURRENT",
        };
        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::CodexDeviceCodeReady{
                .attempt_id = 199,
                .verification_url = "https://auth.openai.com/codex/device",
                .user_code = "STALE",
            }});
        auto* waiting = std::get_if<login::ChatGptWaiting>(&m2.ui.login);
        check(waiting && waiting->user_code == "CURRENT",
              "H: stale attempt cannot overwrite newer device code");
        check(cmd.is_none(), "H: stale progress launches no command");
    }

    // 一. 结束等待 弹出面板 取消其名称 工作线程.
    //
    // 电话 工作线程 已键入 Sub::stream 现在,所以 取消
    // 不是一个 标志 编号 状态更新函数 旅行——这是 事件订阅 离开,
    // 这就是火的点燃 正文's stop_token 保持实际停止它的东西。
    {
        Model m;
        m.ui.login = login::ChatGptWaiting{
            .attempt_id = 300,
            .device_auth = true,
        };
        const auto count = [](const Model& mm) {
            int n = 0;
            agentty::app::subscribe(mm).for_each([&](const auto&) { ++n; });
            return n;
        };
        const int waiting_subs = count(m);

        auto [m2, cmd] = ::agentty::app::detail::step(agentty::app::detail::login_update, std::move(m), msg::LoginMsg{agentty::CloseLogin{}});
        check(std::holds_alternative<login::Closed>(m2.ui.login),
              "I: Esc closes ChatGPT login modal");
        check(count(m2) == waiting_subs - 1,
              "I: Esc drops the ChatGPT login worker's subscription");
        check(cmd.is_none(), "I: close launches no command");
    }
}
