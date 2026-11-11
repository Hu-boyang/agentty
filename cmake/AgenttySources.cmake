# AgenttySources.cmake-per-domain源组列表。
# 由对象 库 和主 exe 使用的纯变量声明 (set(AGENTTY_*_SOURCES
# ...))。没有目标，就没有 标志——可以安全地包含在 objlib/exe
# 定义之前的任何地方。

# ── 按域分列的来源组 ──────────────────────────────────────────────
# include/agentty/下的头文件 源码树镜像该分组一个-for-one：
#   domain/-纯值类型(没有I/O，没有UI)。仅限 头文件。
#   io/ — 套接字、TLS、HTTP/2、OAuth、
#                on-disk 持久化、OS 剪贴板。
#                任何与内核或 传输 对话且不依赖于特定提供商的内容。
#   provider/ —用于 上游 LLM API 的 传输-format 适配器。
#   diff/ —由编辑工具和审阅弹出面板使用的统一-diff 解析器。
#   tool/-代理的能力表面：注册表 + per-tool impls。
#   workspace/ — pure-I/O 扫描器扫描活动的 工作区
#                根（@mention 的文件枚举，# 的 符号 枚举）。
#                由 运行时 的 选择面板 消耗，但与它们供给的
#                UI 状态分开。
#   airgap/ — `agentty airgap` CLI 子命令（执行到 ssh，
#                从不返回）。  不是 maya 运行时 的一部分。
#   runtime/—Elm-style应用程序：模型、更新、事件订阅、视图源码树。

set(AGENTTY_IO_SOURCES
    src/version.cpp
    src/io/http.cpp
    src/io/inflate.cpp
    src/io/tls.cpp
    src/io/auth.cpp
    src/io/accounts.cpp
    src/io/account_switch.cpp
    src/io/keys.cpp
    src/io/vault.cpp
    src/io/cred_crypt.cpp
    src/io/keystore.cpp
    src/io/persistence.cpp
    src/io/shared_file.cpp
    src/io/blob_store.cpp
    src/io/thread_log.cpp
    src/io/blob_gc.cpp
    src/io/clipboard.cpp
    src/util/base64.cpp
    src/util/dbglog.cpp
    src/util/logx.cpp
    src/util/teardown.cpp
    src/util/home_dir.cpp
    src/util/user_root.cpp
    src/util/toml_lite.cpp
    src/util/storage_lock.cpp
    src/util/self_exe.cpp
    src/util/update.cpp
    src/util/modelsdev.cpp
    src/domain/complexity.cpp
    src/domain/sandbox_provenance.cpp
    src/domain/web_search_config.cpp
    src/i18n/i18n.cpp
    src/i18n/startup.cpp
    src/domain/model_name.cpp
    src/domain/stats/fold.cpp
    src/domain/stats/extract.cpp
)

set(AGENTTY_WORKSPACE_SOURCES
    src/workspace/files.cpp
    src/workspace/symbols.cpp
    src/workspace/symbol_match.cpp
    src/workspace/checkpoint.cpp
)

set(AGENTTY_AIRGAP_SOURCES
    src/airgap/airgap.cpp
)

set(AGENTTY_PROVIDER_SOURCES
    src/provider/anthropic/transport.cpp
    src/provider/anthropic/sse.cpp
    src/provider/anthropic/wire_body.cpp
    src/provider/prompt.cpp
    src/provider/chatgpt/provider.cpp
    src/provider/chatgpt/codex_oauth.cpp
    src/provider/chatgpt/responses.cpp
    # OpenAI响应-API方言，由讲该方言的每个主机共享（今天是ChatGPT/Codex，
    # 接下来是GitHub副驾驶）。摘自 chatgpt/responses.cpp
    # ——合同见 responses/responses.hpp。
    src/provider/responses/codec.cpp
    src/provider/copilot/provider.cpp
    src/provider/copilot/copilot_oauth.cpp
    src/provider/kimi/provider.cpp
    src/provider/kimi/kimi_oauth.cpp
    src/provider/openai/transport.cpp
    src/provider/openai/responses_site.cpp
    src/provider/ollama/transport.cpp
    src/provider/selection.cpp
    # 注册表谓词需要提供者自己的知识（模型
    # -aware推理-text答案查阅Copilot的方言表）。
    src/provider/registry.cpp
    src/provider/dialect.cpp
    src/provider/credentials.cpp
    src/provider/auth_state.cpp
    src/provider/prompt_policy.cpp
    # ONE提供者-routing 接口边界 —dispatch_stream()。
    # Provider-agnostic（类型 -erased 路由，包括 ACP 臂），
    # 因此它没有 acp-TU 依赖性，并且每次测试 链接 都存在于提供程序 objlib 中。
    # main.cpp绑定long-lived+外部路由； 分派 仅在选择上进行路由。
    src/provider/dispatch.cpp
    # ACP代理启动注册表(配置加载器)。纯 nlohmann/json，没有 acp-cpp 依赖，
    # 并由 selection.cpp (is_acp_agent_id) 引用 - 因此它存在于
    # agentty_acp_obj 中每次测试 链接、不 的提供程序 objlib 中。
    src/provider/acp_agents.cpp
)

# ACP (代理客户端协议）-让 agentty 作为 Zed (或任何 ACP
# 客户端）在 stdio 上的 JSON-RPC 上驱动的无头代理子进程运行。
# 传输 protocol/engine/transport存在于acp-cpp 子模块中(链接为
# acp::acp)； server.cpp 是 agentty-specific 胶水（转环 + 工具）。
set(AGENTTY_ACP_SOURCES
    src/acp/server.cpp
    src/provider/external_acp_backend.cpp
    src/provider/acp_provider_adapter.cpp
)

# MCP (Model上下文协议)客户端粘合——产生外部MCP服务器并将其工具
# 公开为agentty ToolDefs。重型 mcp-cpp 模板仅限于
# 这些 编译单元（链接为 mcp::mcp）。仅在AGENTTY_MCP时编译。
set(AGENTTY_MCP_SOURCES
    # JSON-RPC、对端两者都在MCP和ACP上运行。这里，在对象中每个
    # 二进制程序 链接，因为 MCP 连接使用它。
    src/rpc/peer.cpp
    src/mcp/bridge.cpp
    # 采用另一工具的服务器，而不是为每个工具增加第六个读根。有关参数，
    # 请参阅 include/agentty/mcp/import.hpp。
    src/mcp/import.cpp
    src/foreign/sessions.cpp
    src/mcp/connection.cpp
    src/mcp/http_server.cpp
    src/mcp/serve.cpp
    src/mcp/oauth.cpp
    src/tool/mcp_tools_bridge.cpp
    src/tool/mcp_tools_backends.cpp
)

set(AGENTTY_DIFF_SOURCES
    src/diff/diff.cpp
)

# RAG引擎现在是外部rag-cpp 库 (子模块 rag-cpp/，
# 目标ragcpp::ragcpp)。只有将应用程序的检索边界映射到 rag::Engine
# 的瘦 agentty 适配器保留在 -tree 中。
set(AGENTTY_RAG_SOURCES
    src/rag/adapter.cpp
    src/rag/embed_backend.cpp
    src/rag/embed_secret.cpp
    src/rag/rag_host.cpp
    src/rag/vector_cache.cpp
)

set(AGENTTY_TOOL_SOURCES
    src/tool/registry.cpp
    src/tool/tool_plan.cpp
    src/tool/tool_loading.cpp
    src/tool/schema.cpp
    src/tool/write_lanes.cpp
    src/tool/web_search_policy.cpp
    src/tool/web_search_plan.cpp
    src/tool/web_search_secret.cpp
    src/scope/scope.cpp
    src/scope/trust.cpp
    # app-level清单：每个Layout/Spec的ONE声明，
    # 由特征读取并由`agentty config`打印。
    # 位于两个基元之上并依赖于两者；两人都不知道它的存在。
    src/config/inventory.cpp
    # write-side范围为对端。出于与作用域相同的原因生活在这个组中：它需要来自
    # tool/util/fs_helpers. 的 util::project_root()
    src/dirs/dirs.cpp
    src/tool/progress.cpp
    src/tool/util/utf8.cpp
    src/tool/util/fs_helpers.cpp
    src/tool/util/disk_files.cpp
    # ONE地方agentty运行另一个程序：mcp-cpp的Exec能力，
    # 通过jaal的进程+反应器实现。替换两个偏离的轮询循环（源码树 的
    # subprocess.cpp 和 mcp-cpp 的）。
    src/tool/util/exec.cpp
    src/tool/util/subprocess.cpp
    src/tool/util/sandbox.cpp
    src/tool/util/sandbox_claybin.cpp
    src/tool/util/sandbox_netproxy.cpp
    src/tool/util/sandbox_broker.cpp
    src/tool/util/handoff_gate.cpp
    src/tool/util/arg_reader.cpp
    src/tool/util/partial_json.cpp
    src/tool/subagent.cpp
    src/tool/skills.cpp
    src/tool/skills_screen.cpp
    src/tool/skills_cli.cpp
    src/tool/commands.cpp
    src/tool/hooks.cpp
    src/tool/plugin.cpp
    src/tool/memory_store.cpp
)

# 除了入口点之外的一切——因此测试可以使链接与运行时相同，
# 而无需与main()战斗。
set(AGENTTY_RUNTIME_NOMAIN_SOURCES
    src/runtime/composer_attachment.cpp
    src/runtime/library.cpp
    src/runtime/slash.cpp
    src/runtime/slash_io.cpp
    src/runtime/app/deps.cpp
    src/runtime/app/settings_cache.cpp
    src/runtime/app/init.cpp
    src/runtime/app/loop_timing.cpp
    src/runtime/app/env.cpp
    src/runtime/app/cmd_factory.cpp
    src/runtime/app/cmd_code_block_run.cpp
    src/runtime/app/cmd_clipboard_diag.cpp
    src/runtime/app/cmd_image_paste.cpp
    src/runtime/app/wire_audit.cpp
    src/runtime/app/update.cpp
    src/runtime/app/update/composer.cpp
    src/runtime/app/update/stream.cpp
    src/runtime/app/update/stream_preview.cpp
    src/runtime/app/update/smart_mode.cpp
    src/runtime/app/update/web_search.cpp
    src/runtime/app/update/paste_settings.cpp
    src/runtime/app/update/appearance.cpp
    src/runtime/app/update/sandbox.cpp
    src/runtime/app/update/plugin_edit.cpp
    src/runtime/app/update/submit.cpp
    src/runtime/app/update/frozen.cpp
    src/runtime/app/update/tool.cpp
    src/runtime/app/update/tool_output.cpp
    src/runtime/app/update/login.cpp
    src/runtime/app/update/models.cpp
    src/runtime/app/update/providers.cpp
    src/runtime/app/update/thread_list.cpp
    src/runtime/app/update/palette.cpp
    src/runtime/app/update/mention.cpp
    src/runtime/app/update/symbol.cpp
    src/runtime/app/update/code_blocks.cpp
    src/runtime/app/update/checkpoints.cpp
    src/runtime/app/update/rag.cpp
    src/runtime/app/update/stats.cpp
    src/runtime/app/update/settings_list.cpp
    src/runtime/panel/settings_items.cpp
    src/runtime/panel/form.cpp
    src/runtime/panel/skills.cpp
    src/runtime/panel/form_keys.cpp
    src/runtime/view/panels/form_common.cpp
    src/runtime/panel/appearance_form.cpp
    src/runtime/panel/sandbox.cpp
    src/runtime/panel/rag_form.cpp
    src/runtime/settings_registry.cpp
    src/runtime/paste_cli.cpp
    src/runtime/panel/smart_form.cpp
    src/runtime/panel/web_search_form.cpp
    src/runtime/panel/plugin_form.cpp
    src/runtime/app/update/fork.cpp
    src/runtime/app/update/diff_review.cpp
    src/runtime/app/update/meta.cpp
    src/runtime/app/subscribe.cpp

    src/runtime/view/cache.cpp
    src/runtime/view/helpers.cpp
    src/runtime/view/host_escape.cpp
    src/runtime/view/thread/turn/agent_timeline/tool_args.cpp
    src/runtime/view/thread/turn/agent_timeline/tool_helpers.cpp
    src/runtime/view/thread/turn/agent_timeline/tool_body_common.cpp
    src/runtime/view/thread/turn/agent_timeline/edit_body.cpp
    src/runtime/view/thread/turn/agent_timeline/bash_body.cpp
    src/runtime/view/thread/turn/agent_timeline/write_body.cpp
    src/runtime/view/thread/turn/agent_timeline/git_diff_body.cpp
    src/runtime/view/thread/turn/agent_timeline/diff_preview_body.cpp
    src/runtime/view/thread/turn/agent_timeline/read_body.cpp
    src/runtime/view/thread/turn/agent_timeline/web_fetch_body.cpp
    src/runtime/view/thread/turn/agent_timeline/list_body.cpp
    src/runtime/view/thread/turn/agent_timeline/task_body.cpp
    src/runtime/view/thread/turn/agent_timeline/todo_body.cpp
    src/runtime/view/thread/turn/agent_timeline/tool_body_preview.cpp
    src/runtime/view/thread/turn/agent_timeline/agent_timeline.cpp
    src/runtime/view/thread/turn/permission.cpp
    src/runtime/view/thread/turn/turn.cpp
    src/runtime/view/thread/welcome_screen.cpp
    src/runtime/view/thread/activity_indicator.cpp
    src/runtime/view/thread/conversation.cpp
    src/runtime/view/thread/thread.cpp
    src/runtime/view/composer.cpp
    src/runtime/view/status_bar/title_chip.cpp
    src/runtime/view/status_bar/phase_chip.cpp
    src/runtime/view/status_bar/token_stream_sparkline.cpp
    src/runtime/view/status_bar/context_gauge.cpp
    src/runtime/view/status_bar/status_banner.cpp
    src/runtime/view/status_bar/model_badge.cpp
    src/runtime/view/status_bar/status_bar.cpp
    src/runtime/view/changes_strip.cpp
    src/runtime/view/slash_menu.cpp
    # 一个面板=一个视图文件(view/panels/<name>.cpp)； panels_prologue.hpp
    # / panels_common.hpp / form_common.cpp 共用脚手架。
    src/runtime/view/panels/models.cpp
    src/runtime/view/panels/providers.cpp
    src/runtime/view/panels/thread_list.cpp
    src/runtime/view/panels/palette.cpp
    src/runtime/view/panels/mention.cpp
    src/runtime/view/panels/symbol.cpp
    src/runtime/view/panels/smart_mode.cpp
    src/runtime/view/panels/web_search.cpp
    src/runtime/view/panels/paste_settings.cpp
    src/runtime/view/panels/appearance.cpp
    src/runtime/view/panels/sandbox.cpp
    src/runtime/view/panels/plugin_edit.cpp
    src/runtime/view/panels/code_blocks.cpp
    src/runtime/view/panels/tool_output.cpp
    src/runtime/view/panels/checkpoints.cpp
    src/runtime/view/panels/todo.cpp
    src/runtime/view/panels/rag.cpp
    src/runtime/view/panels/stats.cpp
    src/runtime/view/panels/skills.cpp
    src/runtime/view/panels/settings_list.cpp
    src/runtime/view/panels/fork.cpp
    src/runtime/view/diff_review.cpp
    src/runtime/view/login.cpp
    src/runtime/view/view.cpp
)

set(AGENTTY_RUNTIME_SOURCES
    src/runtime/main.cpp
    ${AGENTTY_RUNTIME_NOMAIN_SOURCES}
)

