#pragma once
// plugin_form — 插件编辑器的行模型（详细信息+添加）。
//
// 纯：读取值快照 (PluginFormInputs)，
// 发出form::Form。 状态更新函数从mcp.json+现场
// PluginModel解析出快照并上交；该文件仅决定 ARE 行。
// 与 smart_form.hpp 的分工相同——配置表面是一个投影，
// 而不是带有意见的 面板。
//
// KIND-FIRST ADD:在添加模式下第一行是
// `kind`选择(stdio/http/sse/passthrough+python/uvx/npx简写)。
// 将其 REBUILDS 更改为具有所需字段集的表单，
// 保留用户已经 带类型的 的任何 name/url/command
// 文本 — 一个没有向导镶边的向导。在详细模式下，
// 类型被锁定（更改服务器的 传输层 是删除+添加，而不是编辑 - 生成
// 身份和信任哈希随之更改）。

#include <string>
#include <vector>

#include "agentty/runtime/panel/form.hpp"

namespace agentty::plugin_form {

// 字段 ids (稳定；状态更新函数 打开这些)。
inline constexpr const char* kKind        = "kind";
inline constexpr const char* kName        = "name";
inline constexpr const char* kCommand     = "command";
inline constexpr const char* kArgs        = "args";
inline constexpr const char* kUrl         = "url";
inline constexpr const char* kTools       = "tools";      // 直通名称
inline constexpr const char* kAdvertise   = "advertise";  // 直通
inline constexpr const char* kEnabled     = "enabled";
inline constexpr const char* kScopeProject= "scope_project";  // 仅添加模式
inline constexpr const char* kSave        = "save";
inline constexpr const char* kApprove     = "approve";
inline constexpr const char* kRemove      = "remove";
// Per-tool 切换行使用id "tool:<bare-name>"。
inline constexpr const char* kToolPrefix  = "tool:";

// 页脚注释——共享密钥语法由form_config为每个表格
// 面板呈现；这些带有 仅 面板-specific 内容。
// 武装删除警告取代了语法行（note_replaces_grammar），
// 因此破坏性确认是页脚唯一显示的内容。
inline constexpr const char* kNoteRemoveArmed =
    "Enter again to REMOVE — any other key keeps it";

// 选择行的种类ID。
inline constexpr const char* kKindStdio       = "stdio";
inline constexpr const char* kKindHttp        = "http";
inline constexpr const char* kKindSse         = "sse";
inline constexpr const char* kKindPassthrough = "passthrough";

struct ToolRow {
    std::string name;
    std::string description;
    bool enabled = true;
};

struct PluginFormInputs {
    bool add_mode = false;

    // 身份+传输层（详细信息：来自配置；添加：到目前为止的user-typed）。
    std::string kind = kKindStdio;   // kKind* id之一
    std::string name;
    std::string command;
    std::string args;                // space-joined 为文本行
    std::string url;
    std::string tools;               // 直通：逗号-joined名称
    bool        advertise = false;   // 透传：也在传输上做广告

    // 详细信息-mode状态。
    bool        enabled   = true;    // !disabled in config
    bool        connected = false;
    std::string error;               // 连接错误/门原因
    std::string scope_label;         // "user"/"project"/"explicit"
    std::string config_file;         // 该条目所在的mcp.json
    bool        untrusted = false;   // 项目立项待批
    std::vector<ToolRow> tool_rows;  // live/declared工具(详细模式)

    bool        project = false;     // 添加模式：写入项目配置
};

[[nodiscard]] form::Form build_form(const PluginFormInputs& in);

} // namespace agentty::plugin_form
