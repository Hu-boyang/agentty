// plugin_form.cpp ——插件编辑器的行模式。
// 见 头文件 用于此类 -first 纯投影:输入 = 行,无 IO.

#include "agentty/runtime/panel/plugin_form.hpp"

namespace agentty::plugin_form {

namespace {

// 类型词汇的一个位置: ids, lables, 和 one-line 帮助选择的下拉显示.
// order = 下拉顺序(stdio first- the common case;
// 最后传入，保留专家配置这一场景。
struct KindInfo {
    const char* id;
    const char* label;
    const char* hint;
};
constexpr KindInfo kKinds[] = {
    {kKindStdio,       "stdio (spawn a command)",
     "launch a local MCP server process"},
    {kKindHttp,        "http (remote MCP)",
     "connect to a Streamable-HTTP MCP server"},
    {kKindSse,         "sse (remote MCP, legacy)",
     "connect to an SSE MCP server"},
    {kKindPassthrough, "passthrough (proxy tools)",
     "execute tools a proxy/gateway advertises, via POST to a URL"},
};

[[nodiscard]] const KindInfo& kind_info(const std::string& id) {
    for (const auto& k : kKinds)
        if (id == k.id) return k;
    return kKinds[0];
}

} // 命名空间结束。

form::Form build_form(const PluginFormInputs& in) {
    const bool add = in.add_mode;
    form::Builder b{add ? " Add plugin " : (" " + in.name + " ")};

    // ── 副标题: 面板 是一个 -line 情况报告 ─────────────
    //
    // 连接失败不是 情况报告——它是一个信息,它是一个信息:
    // 末尾 说要修什么,所以它继续 `error` (它
    // 包装)和字幕 继续描述 面板 把它放在这里,把它切掉。
    if (!in.error.empty()) b.error(in.error);
    if (add) {
        b.subtitle("pick a kind — the fields below follow");
    } else if (in.untrusted) {
        b.subtitle("untrusted project config — approve below to enable");
    } else if (!in.enabled) {
        b.subtitle("disabled — nothing from this server is on the wire");
    } else if (in.kind == kKindPassthrough) {
        b.subtitle("forwards proxy-advertised tool calls to your URL");
    } else if (!in.error.empty()) {
        b.subtitle("not connected");
    } else {
        b.subtitle(in.connected ? "connected" : "connecting…");
    }

    // ── 温柔 ───────────────────────────────────────────────────────
    {
        std::vector<std::string> labels, ids, hints;
        for (const auto& k : kKinds) {
            labels.emplace_back(k.label);
            ids.emplace_back(k.id);
            hints.emplace_back(k.hint);
        }
        b.choice(kKind, "Kind", std::move(labels), std::move(ids),
                 kind_info(in.kind).id, {}, std::move(hints));
        if (!add)
            b.lock("transport is identity — remove and re-add to change it");
    }

    // ── 身份 ───────────────────────────────────────────────────
    if (add) {
        b.text(kName, "Name", in.name,
               "letters, digits, _ - : (this is the mcp.json key)");
    }

    // ── 温柔 -specific 传输层 行数 ───────────────────────────────
    if (in.kind == kKindStdio) {
        b.text(kCommand, "Command", in.command,
               "executable to spawn (searched on PATH)");
        b.text(kArgs, "Arguments", in.args,
               "space-separated; quoting is not shell — one token per arg");
    } else if (in.kind == kKindPassthrough) {
        b.text(kUrl, "URL", in.url,
               "calls POST their args JSON here; the body is the result");
        if (add) {
            b.text(kTools, "Tools", in.tools,
                   "comma-separated tool names the proxy advertises");
        }
        b.toggle(kAdvertise, "Advertise on the wire", in.advertise,
                 "off = proxy owns the schema (usual); on = agentty "
                 "advertises it too");
    } else {   // http/SSE
        b.text(kUrl, "URL", in.url,
               "http(s)://host[:port]/path of the MCP endpoint");
    }

    // ── 范围+来源 ─────────────────────────────────────────
    if (add) {
        b.toggle(kScopeProject, "Project-scoped", in.project,
                 "write to ./.agentty/mcp.json instead of your user config");
    } else {
        b.header("Where it lives");
        b.text("scope_ro", "Scope", in.scope_label, {});
        b.lock("provenance");
        b.path("file_ro", "Config file", in.config_file, {});
        b.lock("edit via Save below — this row is the address");
    }

    // ── 详细-mode: 启用的+工具 ───────────────────────────────
    if (!add) {
        b.toggle(kEnabled, "Enabled", in.enabled,
                 in.kind == kKindPassthrough
                     ? "off = its declared tools stop dispatching"
                     : "off = disconnect; nothing from it is advertised");

        if (!in.tool_rows.empty()) {
            b.header(in.kind == kKindPassthrough ? "Declared tools"
                                                 : "Advertised tools");
            for (const auto& t : in.tool_rows) {
                b.toggle(kToolPrefix + t.name, t.name, t.enabled,
                         t.description);
            }
        }
    }

    // ── 行动 ────────────────────────────────────────────────────
    b.header("");
    if (!add && in.untrusted) {
        b.action(kApprove, "Approve this server",
                 "records a hash of this exact spec; edits re-ask");
    }
    b.action(kSave, add ? "Add plugin" : "Save changes",
             add ? "validates, writes mcp.json, connects on next reload"
                 : "writes this entry back to its mcp.json");
    if (!add) {
        b.action(kRemove, "Remove…",
                 "deletes the entry from its mcp.json (asks once)");
    }

    // 没有 b.note (:共享密钥语法来自 form_config (SSOT).
    return b.build();
}

} // namespace agentty::plugin_form
