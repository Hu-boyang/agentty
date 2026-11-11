#pragma once
// agentty::store_fx-持久化作为副作用，而不是通过接口边界作为调用。
//
// 状态更新函数用于通过`Deps`到达磁盘：
//
//     deps().save_thread(m.d.current);      // 发生 DURING 和 状态更新函数
//
// 这使得状态更新函数变得不纯：为了测试回合是已保存，你必须安装假存储并随后检查它，
// 并且状态更新函数不能再被读取为
// 输入状态，输出新状态和副作用描述。
//
// 与 副作用 相同的是 值，状态更新函数 返回：
//
//     return save_thread_fx(m.d.current);   // DESCRIBES a save
//
// 主机执行它（参见runtime/app/host.hpp)，并且测试断言返回的Cmd
// -`expect_effect<save_thread>(1)`-根本没有存储。
// 这就是jaal形状的全部要点；参见 docs/design/jaal-rewrite.md.
//
// 原因 THESE FOUR 和 不 该 整个 OF Deps。
// 副作用 就是世界上有 副作用 的东西：写入 线程、删除一个、写入文件、
// 写入设置。 `Deps` 还带有 `new_thread_id` 和
// `title_from`，它们是永远不需要擦除的纯函数 - 现在直接
// 调用它们。而`load_*`是READS，副作用无法表达（jaal
// 副作用不会将值返回到状态更新函数中）；那些真正的
// "read state the Model already holds" 现在变成了
// `m.d.persisted`，剩下的两个真正的读数被记录在了它们所在的位置。

#include <string>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/domain/catalog.hpp"        // ModelInfo
#include "agentty/domain/conversation.hpp"   // 线程
#include "agentty/domain/thread_delta.hpp"   // ThreadDelta
#include "agentty/domain/id.hpp"             // ThreadId
#include "agentty/domain/smart_mode.hpp"     // smart::RoleConfig
#include "agentty/provider/selection.hpp"    // provider::Selection
#include "agentty/store/store.hpp"           // store::Settings
#include "agentty/auth/auth.hpp"             // auth::Credentials
#include "agentty/provider/chatgpt/codex_oauth.hpp"   // CodexCredentials

namespace agentty {

/// 保留线程:自该程序描述的最后保存(persistence::describe_save)
/// 以来可能已经改变的部分。拥有，因此主机可以在 Model
/// 继续运行后运行它。被拒绝的增量返回为 SaveRefused，
/// 下一个 保存 被完整发送。
struct SaveThread { ThreadDelta delta; };
using save_thread = maya::pure_fx<SaveThread, "save_thread">;

/// 从存储中移除线程。
struct DeleteThread { ThreadId id; };
using delete_thread = maya::pure_fx<DeleteThread, "delete_thread">;

/// Store为已保存 线程的新标题；它的消息和时间戳被保留。
/// `title` 已清洁 (persistence::clean_title)。
/// `renamed_at` 是 状态更新函数 的时钟，unix 秒。
/// `name_is_auto` 标记已清除的重命名（恢复为自动名称）。
struct RenameThread { ThreadId id; std::string title; std::int64_t renamed_at = 0;
                      bool name_is_auto = false; };
using rename_thread = maya::pure_fx<RenameThread, "rename_thread">;

/// Write一个文件到磁盘。 diff-review 面板 的 accept/reject 路径。
struct WriteFile { std::string path; std::string contents; };
using write_file = maya::pure_fx<WriteFile, "write_file">;

/// 保留设置记录。
///
/// 商店中的 Write-behind：这立即返回并且记录到达背景 工作线程
/// 上的磁盘，这使得每次击键的节省变得经济实惠。 `m.d.persisted`
/// 是一条记录 — 请参阅 runtime/app/update/internal.hpp
/// 中的 save_record 了解为什么只有一个写入路径。
struct SaveSettings { store::Settings settings; };
using save_settings = maya::pure_fx<SaveSettings, "save_settings">;

/// 将Model的子代理路由器的视图发布到注册表、工作线程、
/// 线程读取(tool/subagent.cpp)。
///
/// 注册表是四个Model字段的镜像。 状态更新函数 曾经直接用
/// tools::subagent::set_*() 来编写它，
/// 这使得它们不纯粹，并且使保持同步成为每个 状态更新函数 必须记住的事情。
/// 现在，分派 接口边界 返回此 副作用，并且仅在视图更改时才返回，
/// 因此主机每次更改都会运行一次。由值携带的原因与 SaveThread
/// 相同：它在 状态更新函数 返回后运行。
struct PublishSubagent {
    std::string             model;
    std::string             provider;
    smart::RoleConfig       smart;
    std::vector<ModelInfo>  candidates;
};
using publish_subagent = maya::pure_fx<PublishSubagent, "publish_subagent">;

/// 将Model的活动提供者发布到进程-global副本，该副本的循环外代码读取
/// （provider::active()：数据流 工作线程，ACP，主）。
///
/// Model拥有该选择。 状态更新函数 直接使用 调用
/// provider::select()，它从内部更新写入全局，
/// 并使 Model 不知道自己的提供者。现在只有 分派 接口边界
/// 返回此值，并且仅当选择更改时。
struct PublishSelection { provider::Selection selection; };
using publish_selection = maya::pure_fx<PublishSelection, "publish_selection">;

/// 将Model的web_search策略（设置→网页搜索）
/// 发布到进程-wide复制工具层读取循环：决定web_search
/// 是否继续在传输上的请求构建器，以及夹紧计数和附加排除（
/// tools::web_search_policy)的分派闭包。
///
/// 与 PublishSelection 相同的规则：Model 拥有该值，
/// 只有 分派 接口边界 返回该值，并且仅当它改变时 — 包括在
/// 第一个 归并 上，这使得工具层从启动时就与 Model 一致，
/// 而不是与它自己的 惰性 磁盘读取碰巧看到的任何内容一致。
struct PublishWebSearchPolicy { web_search_cfg::Config policy; };
using publish_web_search_policy =
    maya::pure_fx<PublishWebSearchPolicy, "publish_web_search_policy">;

/// 在剪贴板读取器(richpaste)中安装Model的粘贴设置。
/// 与PublishWebSearchPolicy相同的规则：仅分派 接口边界在第一个归
/// 并上以及每当m.d.persisted.paste改变时返回它。
struct PublishPasteConfig { paste_cfg::Config config; };
using publish_paste_config =
    maya::pure_fx<PublishPasteConfig, "publish_paste_config">;

/// 安装MCP工具-loading设置转动工作线程读取(#98)。
/// 相同的规则：只有 分派 接口边界 返回它。
struct PublishToolLoading { std::string mode; };
using publish_tool_loading =
    maya::pure_fx<PublishToolLoading, "publish_tool_loading">;

/// Re-install是实时认证头文件、数据流和子代理使用的。
///
/// 状态更新函数用于解析凭据本身（读取凭据文件或设置）
/// 并将结果推送到Deps。现在他们会说出哪个提供商，
/// 然后主机解析并安装它。 `clear` 安装一个空的 头
/// 文件（sign-out，已删除的帐户）。
struct InstallAuth {
    std::string provider;
    bool        clear = false;
};
using install_auth = maya::pure_fx<InstallAuth, "install_auth">;

/// 保留登录中的 Anthropic 凭据 并将它们归档为帐户。
/// `as_new_account`选择一个新的标签而不是重复使用当前的标签。
/// 随后是 "anthropic" 的 InstallAuth，因此新的 头文件 上线。
struct SaveCredentials {
    auth::Credentials creds;
    bool              as_new_account = false;
};
using save_credentials = maya::pure_fx<SaveCredentials, "save_credentials">;

/// 完成登录后保留 ChatGPT (Codex) OAuth 凭据。
/// 状态更新函数 仍然决定（仅在匹配活动尝试 id 后，因此废弃的
/// 工作线程 无法覆盖存储）；主机使用 `unlocked` 执行写入，
/// 因为它是 Codex 信用证 锁 下的加密文件 IO，
/// 并且 状态更新函数 既不能阻止也不能获取 锁。写入失败回答
/// CodexCredentialsSaveFailed。
struct SaveCodexCredentials {
    provider::chatgpt::CodexCredentials creds;
};
using save_codex_credentials = maya::pure_fx<SaveCodexCredentials, "save_codex_credentials">;

/// 将 凭据 商店、环境和账户 注册表 读入 AuthView 并回答
/// AuthViewLoaded。在任何更改它们的 副作用 之后进行批处理。
struct LoadAuthView {};
using load_auth_view = maya::pure_fx<LoadAuthView, "load_auth_view">;

/// 账户注册表，为副作用。每个都在主机上运行，​​然后 状态更新函数 批处理
/// LoadAuthView，以便 Model 看到结果。
///   激活——将命名槽复制到提供商的实时商店中。
///   移除——删除槽（注册表提升下一个槽）。
///   注册—快照为`label`下的直播凭据。
///   AddKey-保存粘贴密钥作为活动帐户（密钥提供者）。
///   SignOut-清除提供商的实时商店(vault::sign_out)。
///   ClearActive-清除实时存储而不接触注册表。
struct AccountOp {
    enum class Kind : unsigned char {
        Activate, Remove, Register, AddKey, SignOut, ClearActive,
    };
    Kind        kind = Kind::Activate;
    std::string provider;
    std::string label;   // 激活/删除/注册
    std::string key;     // AddKey
};
using account_op = maya::pure_fx<AccountOp, "account_op">;

}  // namespace agentty
