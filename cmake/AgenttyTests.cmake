# AgenttyTests.cmake——声明性测试表。
#
# 每次测试一个agentty_test()。 MODE 将 归并 合并为 agentty_tests doctest
# 二进制程序；独立构建自己的exe（forkers/PTY/fuzzers/benches）； raw =
# caller-defined（narrow-source 消毒剂测试）。聚合（`tests`、
# `tests_gating`、`sanitizer_tests`）在最终确定时为 派生 — 没有 hand-listing。
#
# 需要（由根在包含之前设置）：AGENTTY_SHARED_OBJECTS、AGENTTY_HAS_RAGCPP、
# AGENTTY_HAS_MIMALLOC、AGENTTY_MCP以及导入的目标（maya::maya、
# mcp:​​:*、acp::acp、doctest::doctest、OpenSSL、……）。

include(AgenttyTestRegistry)

# ── 合并数 单元测试 (doctest TEST_CASEs 输入 agentty_tests) ───────────
# Pure/logic测试链接共享对象设置一次。每个都是从 per-exe 版本迁移而来；
# 请参阅 git 历史记录以获取 per-test 基本原理评论。
set(_AGENTTY_CONSOLIDATED
    error_class_test accounts_registry_test acp_agents_test acp_integration_test
    custom_host_key_prompt_test dispatch_route_test provider_keys_seal_test
    model_label_test cache_anchor_test composer_edit_test paste_settings_test hooks_gate_test
    tool_result_image_test
    image_dims_test
    quit_cancels_stream_test
    midrun_freeze_test smart_mode_test stream_liveness_test wire_golden_test
    smart_tuning_settings_test smart_routing_card_test settings_nav_test settings_rows_input_test
    slash_menu_test
    effort_auto_test settings_multiproc_test
    wire_shared_test complexity_test copilot_token_test kimi_token_test
    chatgpt_bundled_models_test settings_default_test
    turn_provenance_test subagent_pin_test
    program_hooks_test login_cancel_test subs_key_test reducer_effects_test
    sim_turn_test view_purity_test
    update_check_test update_ux_test mcp_result_type_test
    workspace_index_test modelsdev_parse_test form_fuzz_test context_gauge_wire_test partial_json_fuzz_test tool_card_truth_test
    teardown_test
    snapshot_picker_test
    dup_tool_call_id_test salvage_dedup_test compaction_wire_test shell_detour_streak_test wire_tool_order_test
    web_search_policy_test web_search_pane_test web_search_services_test
    speculative_dispatch_args_test
    tool_definition_pin_test
    shell_env_floor_test
    edit_idempotent_guard_test
    edit_lane_race_test
    exec_policy_test
    subprocess_group_kill_test
    plugins_in_model_test tool_stream_snapshot_test tool_timeline_adapter_test
    anthropic_sse_golden_test codex_login_flow_test mcp_reload_race_test
    persistence_proactive_test proactive_deferred_test rag_adapter_test rag_vector_cache_test
    scheduler_path_test tool_result_budget_test tool_wedge_liveness_test
    transcript_bound_test turn_settle_test stream_clock_test midrun_seam_test midrun_wire_test
    codex_responses_test doom_loop_test visual_hash_coverage_test
    wire_fragmentation_test wire_supersede_test provider_identity_test provider_conformance_test
    provider_matrix_test tool_call_identity_test attribution_discipline_test
    empty_tool_args_test responses_log_test reasoning_ssot_test
    copilot_item_id_test wire_audit_test
    login_routing_test tool_advertisement_test gateway_context_window_test
    smart_settings_roundtrip_test sandbox_parity_test context_ladder_test
    sandbox_win_posture_test netproxy_allowlist_test sandbox_preset_roundtrip_test update_release_window_test
    capability_conformance_test
    ollama_transport_test openai_transport_test session_affinity_test local_stream_cap_test tool_plan_test tool_search_eval_test tool_loading_wire_test schema_safety_test code_block_extract_test
    openai_dialect_test host_probe_taxonomy_test openai_conformance_policy_test
    openai_request_body_test context_window_robustness_test
    command_palette_test compaction_threshold_test fsm_test model_caps_test
    config_inventory_test mcp_import_test capped_read_test foreign_sessions_test
    foreign_open_test stream_epilogue_test stream_recovery_test codex_auth_test openrouter_test
    dialect_test
    embed_backend_test form_test embed_form_test escape_guarantee_test
    theme_discipline_test
    key_routing_test
    native_visibility_test
    palette_nav_test panel_test panel_nav_test status_bar_cache_test
    appearance_rows_test
    issue37_terminal_respect_test
    param_tag_repair_test shell_cmd_alias_test sandbox_escape_test sandbox_pane_test sandbox_broker_test handoff_gate_test handoff_shell_test scope_test table_render_test
    empty_turn_test i18n_test
    ssrf_guard_test render_key_coverage_test reasoning_render_test
    plugin_config_test skills_engine_test skill_effects_trust_test
    skill_screen_test
    slash_commands_test fuzzy_match_smoke
    provider_model_switch_test
    oauth_proactive_refresh_test maya_host_sequence_test
    smart_slot_panel_stack_test account_switch_refresh_test fused_models_test
    panel_sections_render_test provider_panel_sections_test settings_roundtrip_test
    queued_preview_test
    credentials_test entitlement_test inflate_test
    settings_list_scroll_test visual_walk_test md_robustness_test
    stats_test
    ui_prefs_test
    stats_scroll_test
    stats_visual_hash_test
    panel_overflow_probe
    panel_draw_budget_test
    panel_width_probe
    reveal_bucket_test
    ui_motion_frame_test
    reasoning_ticker_height_test
    turn_height_monotonic_test
    stats_golden_test
    stats_render_probe
    context_window_test
    thread_blob_test lazy_image_test thread_log_test
    thread_migration_test thread_save_incremental_test blob_gc_test)
foreach(_t ${_AGENTTY_CONSOLIDATED})
    agentty_test(${_t} MODE consolidated)
endforeach()

# ── 独立的全-stack测试 ─────────────────────────────────────────────
# 无法共享doctest进程的Forkers/PTY/fuzzers/e2e/benches。
# ── 独立测试 ────────────────────────────────────────────
# 这些不可能是共享进程中的 doctest 情况（fork/exec、PTY、模糊种子循环、
# 子进程 e2e）——但是它们 DON'T 每个都需要自己的 100 MB+链接。
# agentty_fold_test() 将它们全部放入 一个 二进制程序 (agentty_standalone_tests) 中，
# 并给出每个它自己的 ctest 条目，将其作为单独的 PROCESS 运行：
#   add_test(NAME x COMMAND
# agentty_standalone_tests x)因此进程隔离与之前相同，为1 链接而不是~16。
# 每个TU的main()通过per-source -Dmain=重命名为<name>_main； 分派器 (
# tests/agentty_standalone_tests_main.cpp + .def) 调用 它。
agentty_fold_test(long_session_bench       TIMEOUT 600 LABELS perf)
# 线程工作台采用线程文件； symbol_read_corpus 采用 <path>
# \t<symbol> 对中的 TSV。 FIXTURE 为每个人提供一个小的
# check-in 输入，因此 ctest 实际上对它们进行 EXERCISES，
# 而不是裸露地运行它们，观察它们打印使用情况，然后称之为通过。
# 当您想要反映您自己历史的数字时，可以手动对照真实的 线程 (~/.agentty/threads/
# <id>.jsonl) — 该夹具用于捕获破损，而不是用于基准测试。
agentty_fold_test(tool_latency_bench ARGS  TIMEOUT 600 LABELS perf)
agentty_fold_test(turn_prep_bench          TIMEOUT 600 LABELS perf
                  FIXTURE tests/fixtures/bench_thread.jsonl)
agentty_fold_test(cache_churn_bench        TIMEOUT 600 LABELS perf
                  FIXTURE tests/fixtures/bench_thread.jsonl)
agentty_fold_test(save_bench               TIMEOUT 600 LABELS perf
                  FIXTURE tests/fixtures/bench_thread.jsonl)
agentty_fold_test(wire_encode_bench        TIMEOUT 600 LABELS perf
                  FIXTURE tests/fixtures/bench_thread.jsonl)
agentty_fold_test(symbol_read_corpus       TIMEOUT 600 LABELS perf
                  FIXTURE tests/fixtures/symbol_read_corpus.tsv)
agentty_fold_test(cross_process_lock_test  TIMEOUT 30)
# persistence::SharedFile确实达到了边车上jaal的锁，
# 并且排除了第二PROCESS。 Fork，所以它需要自己的进程。
agentty_fold_test(shared_file_test        TIMEOUT 30)
# 使用 COLD all() 缓存 和它自己的 HOME 驱动 skills::catalog_block
# ()/activation_payload() — 在共享的 二进制程序 中，
# 另一个案例已经加热了 mtime 缓存，因此不能在那里诚实地设置有趣的案例（隐藏
# 未经批准的技能）。
agentty_fold_test(skill_catalog_trust_test TIMEOUT 30)
# 自己HOME+冷技缓存，道理同skill_catalog_trust_test。
agentty_fold_test(skills_panel_test        TIMEOUT 30)
agentty_fold_test(catalog_cost_probe       TIMEOUT 60)
# 编译 AtomicSnapshot 超过了工具链上已发布的 <T> 的
# 互斥量 回退 (libc++/Termux 路径)，HAS 是原子专门化的，
# 因此它不会被忽视。 链接 NO agentty 对象的 BARE 二进制程序：
# MAYA_FORCE_PUBLISHED_MUTEX改变了槽的布局，
# 因此将其与没有它构建的对象混合是ODR违规（程序挂在静态初始化中）。
# 头文件-only 使用 jaal，因此它的包含目录没有任何链接。
add_executable(snapshot_mutex_fallback_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/snapshot_mutex_fallback_test.cpp)
target_include_directories(snapshot_mutex_fallback_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(snapshot_mutex_fallback_test PRIVATE
    MAYA_FORCE_PUBLISHED_MUTEX=1)
find_package(Threads REQUIRED)
target_link_libraries(snapshot_mutex_fallback_test PRIVATE Threads::Threads)
add_test(NAME snapshot_mutex_fallback_test
         COMMAND snapshot_mutex_fallback_test)
set_tests_properties(snapshot_mutex_fallback_test PROPERTIES TIMEOUT 60)
# EXCLUDE_FROM_ALL使其远离普通的`make`，这是正确的—
# —它不能链接到任何东西。但 add_test() 无论如何都会向
# ctest 注册它，因此如果没有构建它，套件会报告 "Not Run"
# 并导致作业失败。这就是它所做的：用于测试的红色 CI 作业，
# 其 二进制程序 从未生产过。
#
# 它无法通过 agentty_test()（该测试必须避免的帮助器 链
# 接 和 agentty 对象），因此它在这里手动加入 派生 聚合
# — hand-rolled 目标必须选择加入的地方，位于原因旁边。
set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_STANDALONE
             snapshot_mutex_fallback_test)
set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_SANITIZER
             snapshot_mutex_fallback_test)
# sandbox_live_check:此时，在该主机上，
# 沙箱实际上是ENFORCE吗？不是 ctest，而且是故意的：
# 它需要有效的用户+挂载命名空间，它会建立一个真正的出站连接来
# 检查宽松的设置是否仍然允许，并且这两者都是机器的属性而不是代码的属性。
# 在 CI 中，它要么会跳过自身（什么也不证明，绿色），
# 要么会因与更改无关的原因而失败。
#
# 因此，它是您触摸沙箱时用手运行的目标：
#     cmake --build build --target sandbox_live_check && ./build/sandbox_live_check
#
# 自动化套件涵盖的内容：sandbox_escape_test断言我们构建的ARGV/plan
# （不需要命名空间），并且claybin自己的20-test套件涵盖了引擎。
# 这缩小了 "the plan says X" 和 "the child experiences X"
# 之间的最后一个差距——这正是 mremap bug 所在的地方：
# 每个表-level测试都通过，而realloc()在来宾中失败。
# 编译 两个后端源直接进入，而不是 链接
# agentty_tool_obj：objlib 将整个工具层（mcp 配置、
# auth、teardown）拖到其后面，并且此检查仅需要生成路径。
# sandbox.cpp伴随着build_bwrap_argv——bwrap
# 掩蔽情况驱动REAL argv而不是hand-built，
# 因此它不会偏离生产传递给bwrap的内容。
add_executable(sandbox_live_check EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/sandbox_live_check.cpp
    ${CMAKE_SOURCE_DIR}/tests/sandbox_config_race_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_claybin.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_netproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_broker.cpp
    # 门拥有主机-trusted路径的ONE表，
    # 并且sandbox.cpp现在读取它以构建read-only绑定（预防一半）。
    # 链接到此处，以便实时检查驱动真实列表，而不是可能漂移的副本。
    ${CMAKE_SOURCE_DIR}/src/tool/util/handoff_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/domain/sandbox_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/exec.cpp   # run_child:subprocess.cpp通过它产生
    ${CMAKE_SOURCE_DIR}/src/tool/util/utf8.cpp
    ${CMAKE_SOURCE_DIR}/src/util/logx.cpp
    ${CMAKE_SOURCE_DIR}/src/util/dbglog.cpp
    ${CMAKE_SOURCE_DIR}/src/util/home_dir.cpp
    ${CMAKE_SOURCE_DIR}/src/util/user_root.cpp
    ${CMAKE_SOURCE_DIR}/src/util/teardown.cpp)
target_include_directories(sandbox_live_check PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sandbox_live_check PRIVATE
    claybin nlohmann_json::nlohmann_json Threads::Threads maya::app)
if(TARGET mcp::tools)
    target_link_libraries(sandbox_live_check PRIVATE mcp::tools)   # exec.cpp实现mcp-cpp的Exec
endif()
if(TARGET maya::app)
    target_include_directories(sandbox_live_check SYSTEM PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_definitions(sandbox_live_check PRIVATE
    AGENTTY_MCP=0)

# tool_gallery:每个工具卡，通过REAL视图呈现，按需。
#
# 从屏幕截图判断卡牌设计意味着等待正确的会话发生。这会为每张卡构建一个
# Model——读取、grep、编辑、bash（正常和失败）、
# 仍在运行的工具、todo、任务、web、git——以及 渲染 到
# conversation_config，所以它打印的就是用户看到的内容。
#
# 故意包含不讨人喜欢的情况（在 -comment 中进行的阅读，
# 点击的 grep 是评论片段），因为这些是值得修复的，
# 并且它们在 happy-path 演示中是不可见的。
#
# 不是ctest：它没有断言什么，它是LOOK处的东西。
#
#     cmake --build build --target tool_gallery
#     ./build/tool_gallery              # every card
#     ./build/tool_gallery read grep    # 仅运行指定工具
#     COLS=120 ./build/tool_gallery     # 使用另一种宽度
add_executable(tool_gallery EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/tool_gallery.cpp
    ${AGENTTY_SHARED_OBJECTS})
_agentty_test_link_full(tool_gallery)
set_target_properties(tool_gallery PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# ui_snapshots：画廊的每个表面的输出，包括颜色，
# 与tests/fixtures/ui_snapshots.不同 捕获改变某物外观的变化。
# 使用 tests/ui_snapshots.sh
#     build/tool_gallery --update 接受预期更改
if(UNIX)
    add_test(NAME ui_snapshots
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/ui_snapshots.sh
                     $<TARGET_FILE:tool_gallery>)
    set_tests_properties(ui_snapshots PROPERTIES TIMEOUT 120 LABELS ui)
    # 由`tests`/`tests_gating`构建，因此CI的ctest运行具有二进制程序。
    set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_STANDALONE tool_gallery)
endif()

# ws_bind_probe:手-run对
# 工作区是否真正绑定，其余主目录是否确实只读？
# 的回答——通过run_shell_command（运送路径）驱动而不是手-built姿势，
# 因为错误报告声称两者不同意。与上面的 sandbox_live_check
# 相同的构建形状和基本原理（需要真正的命名空间；而不是 ctest）。
#
#     cmake --build build --target ws_bind_probe && ./build/ws_bind_probe <dir>
add_executable(ws_bind_probe EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/ws_bind_probe.cpp
    ${CMAKE_SOURCE_DIR}/tests/sandbox_config_race_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_claybin.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_netproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_broker.cpp
    # 门拥有主机-trusted路径的ONE表，
    # 并且sandbox.cpp现在读取它以构建read-only绑定（预防一半）。
    # 链接到此处，以便实时检查驱动真实列表，而不是可能漂移的副本。
    ${CMAKE_SOURCE_DIR}/src/tool/util/handoff_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/domain/sandbox_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/exec.cpp   # run_child:subprocess.cpp通过它产生
    ${CMAKE_SOURCE_DIR}/src/tool/util/utf8.cpp
    ${CMAKE_SOURCE_DIR}/src/util/logx.cpp
    ${CMAKE_SOURCE_DIR}/src/util/dbglog.cpp
    ${CMAKE_SOURCE_DIR}/src/util/home_dir.cpp
    ${CMAKE_SOURCE_DIR}/src/util/user_root.cpp
    ${CMAKE_SOURCE_DIR}/src/util/teardown.cpp)
target_include_directories(ws_bind_probe PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ws_bind_probe PRIVATE
    claybin nlohmann_json::nlohmann_json Threads::Threads maya::app)
if(TARGET mcp::tools)
    target_link_libraries(ws_bind_probe PRIVATE mcp::tools)   # exec.cpp实现mcp-cpp的Exec
endif()
if(TARGET maya::app)
    target_include_directories(ws_bind_probe SYSTEM PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_definitions(ws_bind_probe PRIVATE
    AGENTTY_MCP=0)

# exec_exit_probe:（沙盒）命令在退出时返回，还是仅在其 超时
# 处返回？来源和基本原理与 ws_bind_probe 相同；手动运行：
#     cmake --build build --target exec_exit_probe && ./build/exec_exit_probe <dir> [on|off] [syscall_mode]
add_executable(exec_exit_probe EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/exec_exit_probe.cpp
    ${CMAKE_SOURCE_DIR}/tests/sandbox_config_race_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_claybin.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_netproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_broker.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/handoff_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/domain/sandbox_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/exec.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/utf8.cpp
    ${CMAKE_SOURCE_DIR}/src/util/logx.cpp
    ${CMAKE_SOURCE_DIR}/src/util/dbglog.cpp
    ${CMAKE_SOURCE_DIR}/src/util/home_dir.cpp
    ${CMAKE_SOURCE_DIR}/src/util/user_root.cpp
    ${CMAKE_SOURCE_DIR}/src/util/teardown.cpp)
target_include_directories(exec_exit_probe PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(exec_exit_probe PRIVATE
    claybin nlohmann_json::nlohmann_json Threads::Threads maya::app)
if(TARGET mcp::tools)
    target_link_libraries(exec_exit_probe PRIVATE mcp::tools)
endif()
if(TARGET maya::app)
    target_include_directories(exec_exit_probe SYSTEM PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_definitions(exec_exit_probe PRIVATE
    AGENTTY_MCP=0)

# sandbox_audit:agentty ACTUALLY在THIS主机上强制执行什么？
#
# 不是测试——它没有断言任何内容。它打印出真实的姿势，逐项能力，
# 以及每一项能力背后的机制。关键是 "is our sandbox any good"
# 不再是一个观点问题：其输出中的每一个 `none`
# 要么是故意交易，要么是缺口，而工具会告诉你是哪一个。
#
# 与sandbox_live_check分开，因为它回答了不同的问题。
# 现场检查询问"does the wall we claim actually hold in the child"；
# 这要求 "which walls do we claim at all, and how strongly"。
# 能力可以通过实时检查，但这里仍然是 `partial`。
#
#     cmake --build build --target sandbox_audit && ./build/sandbox_audit
add_executable(sandbox_audit EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/sandbox_audit.cpp
    ${CMAKE_SOURCE_DIR}/tests/sandbox_config_race_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_claybin.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_netproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/sandbox_broker.cpp
    # 门拥有主机-trusted路径的ONE表，
    # 并且sandbox.cpp现在读取它以构建read-only绑定（预防一半）。
    # 链接到此处，以便实时检查驱动真实列表，而不是可能漂移的副本。
    ${CMAKE_SOURCE_DIR}/src/tool/util/handoff_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/domain/sandbox_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/tool/util/exec.cpp   # run_child:subprocess.cpp通过它产生
    ${CMAKE_SOURCE_DIR}/src/tool/util/utf8.cpp
    # logx:sandbox_claybin.cpp记录代理的决定，
    # 因此审计需要真实的记录器而不是存根——存根的记录器会编译，
    # 然后不能证明审计跟踪存在。
    ${CMAKE_SOURCE_DIR}/src/util/logx.cpp
    ${CMAKE_SOURCE_DIR}/src/util/dbglog.cpp
    ${CMAKE_SOURCE_DIR}/src/util/home_dir.cpp
    ${CMAKE_SOURCE_DIR}/src/util/user_root.cpp
    ${CMAKE_SOURCE_DIR}/src/util/teardown.cpp)
target_include_directories(sandbox_audit PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sandbox_audit PRIVATE claybin nlohmann_json::nlohmann_json
    Threads::Threads maya::app)
if(TARGET mcp::tools)
    target_link_libraries(sandbox_audit PRIVATE mcp::tools)   # exec.cpp实现mcp-cpp的Exec
endif()
if(TARGET maya::app)
    target_include_directories(sandbox_audit SYSTEM PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_definitions(sandbox_audit PRIVATE
    AGENTTY_MCP=0)

agentty_fold_test(fork_test                TIMEOUT 30)
agentty_fold_test(palette_render_probe     TIMEOUT 30)
agentty_fold_test(embed_render_probe       TIMEOUT 30)
agentty_fold_test(form_edit_nav_test       TIMEOUT 30)
agentty_fold_test(login_render_probe       TIMEOUT 30 ARGS)
agentty_fold_test(settings_add_render_probe TIMEOUT 30 ARGS)
agentty_fold_test(thread_delete_test       TIMEOUT 30)
# 指出每个线程子系统注册其OWN连接，而不是main()保留它们
# 的hand-written列表。该列表已经丢失了一个（
# settings_cache 的 工作线程 处于静态状态，
# 直到 ~std::thread 终止该进程）；这反而失败了。
agentty_fold_test(teardown_registration_test TIMEOUT 60)
# seam_stress_test:由多个线程同时驱动的过程-wide
# 接口边界 (提供商选择、工具目录、能力注册表)。 不 标记为 `race`
# — 它是 归并，归并 内的标签将所有 375 个 编译单元 拖入 TSan
# 通道（请参见下面的 persistence_race_test）。
# TSan 车道反而让球穿过狭窄的目标。
agentty_fold_test(seam_stress_test TIMEOUT 120)
# 指出恢复的线程在原生回滚缓冲区中留下了真实的历史：
# 再水合种子比活动画布更宽，
# 并且后-paint修剪COMMITS差异而不是丢弃它。
# 在这次分裂之前，线程 开关显示一圈，上面没有任何可滚动的内容。
agentty_fold_test(rehydrate_scrollback_test TIMEOUT 60)
agentty_fold_test(diff_review_test         TIMEOUT 30)
agentty_fold_test(reveal_freeze_gate_probe TIMEOUT 30)
# maya 54ad00d 的回归：已确定的降价 源码树 比它的小
# 部件寿命更长（冻结的 回滚缓冲区 隐藏它），因此它的布局 lambda
# 不能通过捕获的 `this` 进行写入。独立，因为它故意破坏小部件
# 和 渲染 孤儿 源码树。
agentty_fold_test(streaming_markdown_lifetime_test TIMEOUT 60)
# 崩溃探针：渲染在命令行上传递了REAL 线程文件。
# ARGS，因此ctest运行它而不需要任何东西（它退出2并且在那里简单地通过）；
# 重点是当用户报告 渲染 崩溃时，合成台不会重现，
# 针对 ~/.agentty/threads/<id>.JSON 运行 BY HAND。
agentty_fold_test(real_thread_render_probe TIMEOUT 60 ARGS)
agentty_fold_test(thread_mem_probe TIMEOUT 60 ARGS)
# 手动运行的 真实 线程 上的 Per-frame view()+油漆成本：合成
# 的 long_session_bench 不能再现真正的 700+ 消息的成本。
agentty_fold_test(real_thread_frame_probe TIMEOUT 60 ARGS)
# ~/.agentty/threads到ThreadLog中的每REAL 线程轮-trips。
# ARGS+no-op当语料库不存在时，所以CI不重要地通过；
# 该值是在信任具有历史记录的迁移之前运行它 BY HAND。
#
# LABELS性能：在HAS一个真正的语料库的机器上，这可以完成整个事情（测量：
# 609 线程 / 594 MB的〜75秒）。 RUN_SERIAL 源自 perf，
# 它可以防止慢速探针在 -j8 上缺乏更快的测试 - 如果没有它，
# 探针就会超出 60 秒的 ctest 运行预算，并将 15 个后续测试
# 级联为“未运行”。
agentty_fold_test(thread_log_corpus_probe TIMEOUT 300 ARGS LABELS perf)
# 证明STORE SEAM更喜欢日志：转换真实的线程，
# 通过load_thread_by_id读回，并进行比较。需要 AGENTTY_HOME + id。
agentty_fold_test(thread_log_seam_probe   TIMEOUT 120 ARGS)
# 在 真实 线程 上运行 真实 保存 路径，并证明仅在每条消息、
# 工具输出和图像 字节 读回之后遗留文件才被停用。
agentty_fold_test(thread_migration_probe  TIMEOUT 300 ARGS)
# 迁移整个threads/目录(一个COPY)，
# 之后证明每个线程的内容是相同的。接触真实数据之前的演练。
agentty_fold_test(thread_migration_bulk_probe TIMEOUT 900 ARGS)
# 将线程切换分为工作线程-thread与UI-thread成本，
# 因此下一个优化针对用户实际等待的内容。
agentty_fold_test(thread_switch_prof_probe TIMEOUT 120 ARGS)
# 标记-and-sweep真实线程目录的blob存储。
# DRY RUN 除非 --apply，因为相关文件保存图像和工具输出。
agentty_fold_test(blob_gc_probe TIMEOUT 300 ARGS)
# 终端 RESIZE 的成本是多少：宽度改变使每个缓存的布局无效，
# 因此整个冻结的画布以新的宽度重新-lays-out。
agentty_fold_test(resize_prof_probe TIMEOUT 120 ARGS)
# 是否浏览主题DEGRADE？ "Laggy after a while"
# 是无限增长的标志，而不是缓慢的代码，因此它会在浏览器中运行数百次并报告
# 桶中的 per-switch 成本 + RSS：平坦 = 无泄漏。
agentty_fold_test(theme_switch_leak_probe TIMEOUT 300 ARGS)
# 另一半：如果密钥到达速度快于切换成本，则 flat-and-expensive
# 仍然感觉滞后。报告一支箭头是否适合 key-repeat 插槽。
agentty_fold_test(theme_input_lag_probe TIMEOUT 120 ARGS LABELS perf)
agentty_fold_test(theme_lag_repro TIMEOUT 180 ARGS NO_TEST)
agentty_fold_test(theme_memo_stale_probe TIMEOUT 120 NO_TEST)
# 该 不变量 每个列表覆盖面板都欠其用户：突出显示的行在屏幕上。
# 针对 FilteredPicker 编写，因此它适用于构建在其上
# 的每个 选择面板，而不仅仅是损坏的主题浏览器。
agentty_fold_test(picker_cursor_visible_test TIMEOUT 120)
# 报告的手势针对PERSISTENT 渲染器。每个其他主题探针 渲染
# 到 render_to_string（每个 调用 都有新的 池 + 缓存），
# 在结构上无法捕获 过期 交叉 -frame 缓存 条目。
agentty_fold_test(theme_alternating_key_test TIMEOUT 180)
# "live theme switch is flaky"的
# OTHER一半，以及PERMANENT而不是迟到的一半：提交的
# Markdown块被存储为已经-RENDERED元素，
# 因此它保持调色板在其文本被提交时处于活动状态。一切仍然正确地动画 重绘，
# 这就是让它看起来断断续续的原因。
agentty_fold_test(theme_settled_recolour_test TIMEOUT 180)
# 每个面板必须响应一个 key-repeat 槽内的按键，一次读取中传送
# 的 和 突发必须落在 single-stepping 所在的位置。
agentty_fold_test(panel_input_snappiness_test TIMEOUT 180 LABELS perf)
# 每个箭头都得到FRAME吗？批量输入会减少 N 事件
# 并绘制一次，因此用户经过的行会被计算但不会显示。
agentty_fold_test(frame_per_key_probe TIMEOUT 120 ARGS)
# 主题改变每存储单元重新着色，因此每次按键整个可视
# 区域都在传输上。将其与普通光标移动进行比较。
agentty_fold_test(theme_wire_cost_probe TIMEOUT 120 ARGS)
if(UNIX)
    # PTY-driven(开放式)； full-runtime Ghost-caret 重现 — 请参阅
    # tests/test_ghost_caret_runtime.cpp 的 头文件（来源：davidwed）。
    agentty_fold_test(test_ghost_caret_runtime TIMEOUT 60 SKIP_CODE 77 UNIX_LIBS util)
endif()
agentty_fold_test(toolset_e2e_test         TIMEOUT 120)
agentty_fold_test(subagent_report_test     TIMEOUT 60)
agentty_fold_test(plugin_disabled_tools_test TIMEOUT 60)
agentty_fold_test(tool_budget_env_test     TIMEOUT 60)
agentty_fold_test(frozen_invariant_fuzz)
agentty_fold_test(scrollback_wire_fuzz     TIMEOUT 120)
if(UNIX)
    # 两者都通过 openpty 驱动 运行时 — 仅 POSIX (参见 .def)。
    agentty_fold_test(reveal_scrollback_test   TIMEOUT 180 UNIX_LIBS util)
    agentty_fold_test(scrollback_oracle_test   TIMEOUT 600 UNIX_LIBS util)
endif()
agentty_fold_test(external_acp_backend_test TIMEOUT 60)
agentty_fold_test(md_shape_sweep           TIMEOUT 120)
agentty_fold_test(reasoning_stall_sweep    TIMEOUT 120
                  FIXTURE tests/fixtures/reasoning_burst_shape.jsonl)
agentty_fold_test(reveal_headroom_test     TIMEOUT 60)
agentty_fold_test(md_cache_probe           TIMEOUT 120 LABELS perf)
if(AGENTTY_MCP)
    agentty_fold_test(mcp_bridge_test      TIMEOUT 60)
    set_tests_properties(mcp_bridge_test PROPERTIES ENVIRONMENT
        "AGENTTY_MCP_E2E_SERVER=${CMAKE_BINARY_DIR}/third_party/mcp-cpp/examples/mcp_server_example")
    agentty_fold_test(mcp_http_test        TIMEOUT 60)
endif()
# anthropic_md_stream是capture/replay HARNESS，而不是它自己的ctest条目：
# 下面的 reveal_stream_gate* 臂通过折叠的 二进制程序 调用它（带有参数）。
# 注册在.def；这里没有 add_test。
set_source_files_properties(${CMAKE_SOURCE_DIR}/tests/anthropic_md_stream.cpp
    PROPERTIES COMPILE_DEFINITIONS "main=anthropic_md_stream_main")
set_property(DIRECTORY APPEND PROPERTY AGENTTY_FOLD_NAMES anthropic_md_stream)

# 构建一个二进制程序:每个折叠测试的额外objs/libs.的并集
# persistence_race：TSan下的REAL异步保存队列，不是其模型。
# 折叠的(它拥有主+AGENTTY_HOME，并且需要完整的I/O对象集)，
# 因此它与它的兄弟姐妹一起注册在agentty_standalone_tests.def中。
#
# NOT标记为`race`：该标签是TSan CI通道选择的，
# 并且这是归并内唯一的`race`测试，迫使该通道在
# -fsanitize=thread下构建agentty_standalone_tests的所有375个编译单元来运行一个测试。
# persistence_race_test_narrow（下图）与
# 它实际需要的 ~11 编译单元 构建的源相同，并带有 `race` 标签。
# 该条目仍然在普通套件中未经检测地运行，无论如何，这都是其非
# -race 断言（冲洗排水管、延迟写入土地）所属的地方。
agentty_fold_test(persistence_race_test TIMEOUT 180)
# thread_index:mtime 缓存实际上是缓存。折叠的原因与 persistence_race
# 相同 - 它拥有 AGENTTY_HOME 并需要 I/O 对象集。
agentty_fold_test(thread_index_test TIMEOUT 60)
agentty_fold_test(theme_preview_cost_probe TIMEOUT 120 NO_TEST)

# agents_md_test —锁 wire::agents_md_block (AAIF AGENTS.md 标准)。
# checkpoint_test — git-backed 工作树 快照。
#
# 两者都是FOLDED。它们是自己的 二进制程序，因为它们 chdir() 进入临时 工作区，
# 但 归并 已经将每个条目作为单独的 PROCESS (agentty_standalone_tests
# <name>) 运行，因此 chdir 不再比以前更共享 - 并且十个已经
# -folded 测试执行完全相同的操作（skills_engine、
# toolset_e2e、workspace_index、 ...）。
# 两个 531 MB 链接 是 归并 存在的最后一个 -test 链接 成本，需要移除。
agentty_fold_test(agents_md_test  TIMEOUT 30)
agentty_fold_test(checkpoint_test TIMEOUT 60)
agentty_fold_test(settings_add_effect_test TIMEOUT 30)

# 构建一个二进制程序:每个折叠测试的额外objs/libs.的并集
agentty_finalize_fold(
    OBJS $<TARGET_OBJECTS:agentty_acp_obj>
    LIBS acp::acp)

# stats_visual —转储真实 ANSI 中的每个统计选项卡，供人类查看 LOOK。
# NO_TEST是故意的：其输出是颜色和布局，
# 并且断言"this looks nice"的测试什么也不断言。明确地构建它：
#   cmake --build build --target stats_visual && ./build/stats_visual
agentty_test(stats_visual            MODE standalone NO_TEST)
# NO_TEST也是：它打印人类的计时。对微秒的断言将
# 是对其运行的机器的不稳定测试，而不是代码的测试。
agentty_test(stats_refresh_bench     MODE standalone NO_TEST)
# 对于人类来说，每个设置面板的击键延迟（打印计时）。
agentty_test(settings_input_bench    MODE standalone NO_TEST)


# ── 窄 -source 消毒器测试( raw: must) 不 链接 全部共享集) ──
# 他们在asan/ubsan下干净地运用了agentty自己的逻辑和链接，
# 而没有拉动maya的un-instrumented 渲染器。注册原料+标记消毒剂。
agentty_test(concurrency_primitives_test MODE raw LABELS sanitizer)
add_executable(concurrency_primitives_test EXCLUDE_FROM_ALL
    tests/concurrency_primitives_test.cpp src/util/dbglog.cpp src/util/logx.cpp
    src/util/teardown.cpp)
target_include_directories(concurrency_primitives_test PRIVATE include)
# util/background.hpp发布到jaal::kernel::pool而不是产生它自己的线程，
# 因此这个原始目标像任何其他消费者一样在包含路径上需要
# jaal。
target_link_libraries(concurrency_primitives_test PRIVATE maya::app)
add_test(NAME concurrency_primitives_test COMMAND concurrency_primitives_test)
set_tests_properties(concurrency_primitives_test PROPERTIES TIMEOUT 30 LABELS sanitizer)

# 并发写入下的ChildProcess、close_stdin、alive()和terminate()。
# 指的是 TSan 源码树；普通构建会检查它是否不会挂起或崩溃。
agentty_test(child_process_race_test MODE raw LABELS sanitizer)
add_executable(child_process_race_test EXCLUDE_FROM_ALL tests/child_process_race_test.cpp)
target_include_directories(child_process_race_test PRIVATE include)
target_link_libraries(child_process_race_test PRIVATE maya::app)
add_test(NAME child_process_race_test COMMAND child_process_race_test)
set_tests_properties(child_process_race_test PROPERTIES TIMEOUT 60 LABELS sanitizer)

# subagent_lifetime:提供者接口边界必须在ABANDONED 工作线程中幸存。
#
# 子代理在Cmd::task_isolated 线程上运行，jaal分离
# 并且从不等待； shutdown 的等待是有意限制的，
# 因此放弃是 DESIGNED 结果。因此，提供程序不能是通过引用捕获的
# 堆栈对象（它们是）——在主程序展开时，废弃的 工作线程 mid-stream
# 被调用到已释放的堆栈中，这是间歇性的 "subagents crash" 报告。
# 所有权 是修复；这固定它。
#
# ASan Lane，因为破碎的形状是那里的硬堆叠-use-after-scope并且仅可能在其他地方。
# Narrow-source 与其邻居一样：在仪表化构建中没有
# maya 渲染器 到 ODR-clash。
agentty_test(subagent_lifetime_test MODE raw LABELS sanitizer)
add_executable(subagent_lifetime_test EXCLUDE_FROM_ALL
    tests/subagent_lifetime_test.cpp)
target_include_directories(subagent_lifetime_test PRIVATE include)
add_test(NAME subagent_lifetime_test COMMAND subagent_lifetime_test)
set_tests_properties(subagent_lifetime_test PROPERTIES TIMEOUT 30 LABELS sanitizer)

# race_harness:THREAD-sanitizer车道。注册 raw+narrow
# -source 的原因与其邻居相同（没有 maya 渲染器 到 ODR-clash），
# 并标记为 两者 `sanitizer` 和 `race`，以便 CI 可以在 TSan 下单独运行
# 它 - ASan 和 TSan 不能合并在一个 二进制程序 中。
#
# 为什么存在：这个代码库所带来的每个并发错误对于asan+ubsan
# 来说是不可见的（shared_ptr循环使工作线程加入自身，
# 工作线程在静态中留下可加入，N 线程竞相发布一张地图）。 竞态 需要
# 竞态 探测器。
agentty_test(race_harness_test MODE raw LABELS sanitizer)
add_executable(race_harness_test EXCLUDE_FROM_ALL
    tests/race_harness_test.cpp src/util/teardown.cpp
    src/util/logx.cpp src/util/dbglog.cpp)
target_include_directories(race_harness_test PRIVATE include)
# logx.cpp仅将maya::guarded用于其旋转锁:头文件。
target_include_directories(race_harness_test PRIVATE
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
# 目录案例包括真实的(头文件-only)catalog.hpp。
target_link_libraries(race_harness_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME race_harness_test COMMAND race_harness_test)
set_tests_properties(race_harness_test PROPERTIES TIMEOUT 120 LABELS "sanitizer;race")

# real_subsystem_race_test:同TSan车道，
# 但驾驶REAL共享接口边界而不是其中的手-written模型。
# race_harness_test 练习了 快照 存储单元/teardown
# 注册表/write-behind 队列的模型，它捕获了 PATTERN 中的错误；
# 这个 链接 是实际的 编译单元，它捕获了 CODE 中的错误。
# 否则两者就会分道扬镳，而交付的是代码。
agentty_test(real_subsystem_race_test MODE raw LABELS sanitizer)
add_executable(real_subsystem_race_test EXCLUDE_FROM_ALL
    tests/real_subsystem_race_test.cpp src/util/teardown.cpp
    src/util/logx.cpp src/util/dbglog.cpp)
target_include_directories(real_subsystem_race_test PRIVATE include)
# logx.cpp仅将maya::guarded用于其旋转锁:头文件。
target_include_directories(real_subsystem_race_test PRIVATE
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME real_subsystem_race_test COMMAND real_subsystem_race_test)
set_tests_properties(real_subsystem_race_test PROPERTIES TIMEOUT 120 LABELS "sanitizer;race")

# persistence_race_test_narrow — 相同 测试
# 作为折叠的 `persistence_race_test` 条目，由它实际需要的 ~11
# 编译单元 构建而成，而不是整个 375-TU 独立 归并。
#
# 为什么：这是 agentty_standalone_tests 中唯一的 `race`
# -labelled 测试，因此 TSan CI 通道正在编译整个 二进制程序 —
# 每个提供商、ACP 服务器、RAG 适配器、所有 maya — 在
# -fsanitize=thread 下运行 一个 测试。 TSan 大约使 编译
# 时间增加了一倍，因此这是 CI 中最昂贵的东西，并且没有一个是测试读取的仪表代码。
#
# 仅在 `race` 下注册。折叠条目保留了 asan 通道的
# `sanitizer` 标签（出于自身原因无论如何都会构建 二进制程序），
# 因此覆盖范围是相同的，并且两个通道都不会丢失支票。
agentty_test(persistence_race_test_narrow MODE raw LABELS race)
add_executable(persistence_race_test_narrow EXCLUDE_FROM_ALL
    tests/persistence_race_test.cpp tests/persistence_race_stubs.cpp
    src/io/persistence.cpp src/io/blob_store.cpp src/io/thread_log.cpp
    src/runtime/settings_registry.cpp src/tool/util/utf8.cpp
    src/util/logx.cpp src/util/dbglog.cpp src/util/home_dir.cpp
    src/util/user_root.cpp src/util/base64.cpp src/util/teardown.cpp
    src/io/shared_file.cpp
    src/domain/web_search_config.cpp)
target_include_directories(persistence_race_test_narrow PRIVATE include)
# 运行时的头文件:user_root.cpp使用maya::guarded作为其warn
# -once集(并发性禁用清单禁止原始std::mutex)。这里是 头文件-only，
# 所以 maya::app 的使用要求就足够了，没有任何链接：这个目标的存在是为了将
# TSan 通道保持在 ~11 编译单元 而不是 375。
target_include_directories(persistence_race_test_narrow PRIVATE
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
# maya::runtime用于SharedFile的交叉-process 锁 (平台file_lock)。
target_link_libraries(persistence_race_test_narrow PRIVATE
    nlohmann_json::nlohmann_json simdjson::simdjson Threads::Threads maya::runtime)
target_compile_definitions(persistence_race_test_narrow PRIVATE
    AGENTTY_MCP=0)
add_test(NAME persistence_race_test_narrow COMMAND persistence_race_test_narrow)
set_tests_properties(persistence_race_test_narrow PROPERTIES TIMEOUT 120 LABELS "race")

# 设置 write-behind 缓存，从许多 线程 与 in-memory
# 磁盘中锤击。三个编译单元，所以在TSan巷里还是很便宜的。
agentty_test(settings_cache_race_test MODE raw LABELS race)
add_executable(settings_cache_race_test EXCLUDE_FROM_ALL
    tests/settings_cache_race_test.cpp src/runtime/app/settings_cache.cpp
    src/util/teardown.cpp src/util/logx.cpp src/util/dbglog.cpp)
target_include_directories(settings_cache_race_test PRIVATE include tests
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(settings_cache_race_test PRIVATE
    nlohmann_json::nlohmann_json Threads::Threads maya::runtime)
target_compile_definitions(settings_cache_race_test PRIVATE AGENTTY_MCP=0)
add_test(NAME settings_cache_race_test COMMAND settings_cache_race_test)
set_tests_properties(settings_cache_race_test PROPERTIES TIMEOUT 120 LABELS "race")

# util::GenCache (Copilot/Kimi型号-list 缓存):仅头文件。
agentty_test(gen_cache_race_test MODE raw LABELS race)
add_executable(gen_cache_race_test EXCLUDE_FROM_ALL tests/gen_cache_race_test.cpp)
target_include_directories(gen_cache_race_test PRIVATE include
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(gen_cache_race_test PRIVATE Threads::Threads maya::runtime)
add_test(NAME gen_cache_race_test COMMAND gen_cache_race_test)
set_tests_properties(gen_cache_race_test PROPERTIES TIMEOUT 120 LABELS "race")

# mcp::Connection在其读取器位于服务器回调内部时掉落。
# connection.cpp+peer.cpp+记录，在in-memory通道上。
if(AGENTTY_MCP)
    agentty_test(mcp_connection_race_test MODE raw LABELS race)
    add_executable(mcp_connection_race_test EXCLUDE_FROM_ALL
        tests/mcp_connection_race_test.cpp src/mcp/connection.cpp src/rpc/peer.cpp
        src/util/teardown.cpp src/util/logx.cpp src/util/dbglog.cpp)
    target_include_directories(mcp_connection_race_test PRIVATE include tests
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(mcp_connection_race_test PRIVATE
        mcp::mcp jsonrpc::jsonrpc nlohmann_json::nlohmann_json Threads::Threads maya::runtime)
    add_test(NAME mcp_connection_race_test COMMAND mcp_connection_race_test)
    set_tests_properties(mcp_connection_race_test PROPERTIES TIMEOUT 120 LABELS "race")
endif()

# sandbox_config_race_test — 在工具读取时发布 沙箱 策略。
#
# 故意缩小范围，与上面的 persistence_race_test_narrow
# 的推理相同：它需要 sandbox.cpp 和它提取的少数实用程序，
# 而不是 375-TU 归并，因此 TSan 通道仪器 ~6 编译单元 来检查实际上是活跃的
# 东西。
#
# 它保护什么：设置 面板 使 沙箱 配置成为已发布的
# -at-runtime 值，并且它是 "set once before maya starts"
# 前提下的普通全局值。从 工作线程 线程 下分配一个由三个
# 向量组成的结构是一个 use-after-free，
# 窗口非常窄——就是那种在别人的机器上发布然后崩溃的类型。
agentty_test(sandbox_config_race_test MODE raw LABELS race)
add_executable(sandbox_config_race_test EXCLUDE_FROM_ALL
    tests/sandbox_config_race_test.cpp tests/test_main.cpp
    tests/sandbox_config_race_stubs.cpp
    src/tool/util/sandbox.cpp src/tool/util/subprocess.cpp
    src/tool/util/exec.cpp   # run_child:subprocess.cpp通过它产生
    src/tool/util/sandbox_claybin.cpp
    src/tool/util/sandbox_netproxy.cpp
    src/tool/util/sandbox_broker.cpp src/tool/util/handoff_gate.cpp
    src/domain/sandbox_provenance.cpp
    src/tool/util/utf8.cpp
    src/util/logx.cpp src/util/dbglog.cpp src/util/home_dir.cpp
    src/util/user_root.cpp src/util/teardown.cpp)
target_include_directories(sandbox_config_race_test PRIVATE include)
target_link_libraries(sandbox_config_race_test PRIVATE
    doctest::doctest nlohmann_json::nlohmann_json Threads::Threads claybin maya::app)
if(TARGET mcp::tools)
    target_link_libraries(sandbox_config_race_test PRIVATE mcp::tools)   # exec.cpp
endif()
# maya仅限头文件(sandbox.cpp的及物包括到达scroll_state/anim_clock)；
# 没有 maya 链接，因此 TSan 构建仍然很小。
if(TARGET maya::app)
    target_include_directories(sandbox_config_race_test SYSTEM PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
target_compile_definitions(sandbox_config_race_test PRIVATE
    AGENTTY_MCP=0)
add_test(NAME sandbox_config_race_test COMMAND sandbox_config_race_test)
set_tests_properties(sandbox_config_race_test PROPERTIES TIMEOUT 120 LABELS "race")

# exec_session_race_test:从几个线程驱动的真实Exec(
# src/tool/util/exec.cpp):除了前台运行之外，
# 后台会话在其他线程上启动、轮询、停止和丢弃。 2026 年 10 月发生的两起
# lock_order_error 事故都发生在这条道路上。故意小（exec.cpp
# + 日志记录），所以 TSan 车道可以承受； 沙箱 在测试中被切断。
if(TARGET mcp::tools)
    agentty_test(exec_session_race_test MODE raw LABELS race)
    add_executable(exec_session_race_test EXCLUDE_FROM_ALL
        tests/exec_session_race_test.cpp src/tool/util/exec.cpp
        src/util/teardown.cpp src/util/dbglog.cpp src/util/logx.cpp)
    target_include_directories(exec_session_race_test PRIVATE include)
    # sandbox.hpp命名claybin类型，因此即使该构建将
    # 沙箱存根并且链接不存根claybin的代码，也需要其头文件。
    target_include_directories(exec_session_race_test PRIVATE
        $<TARGET_PROPERTY:claybin,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(exec_session_race_test PRIVATE mcp::tools maya::runtime Threads::Threads)
    add_test(NAME exec_session_race_test COMMAND exec_session_race_test)
    set_tests_properties(exec_session_race_test PROPERTIES TIMEOUT 180 LABELS race)

    # 真实沙箱下的相同会话：泵应答seccomp代理并通过cgroup.kill杀死。
    # 仅限 Linux（seccomp 用户-notify）。
    # 不是 TSan 目标：claybin 的设置子项是 TSan 运行时 的
    # 原始克隆3，其在其中报告并停止。在未检测的情况下运行，它是捕获挂在 glibc
    # 的中止 锁 (claybin spawn.cpp) 上的生成的原因。
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT AGENTTY_SANITIZE_ALL)
        agentty_test(exec_session_race_sandboxed MODE raw LABELS sandbox)
        add_executable(exec_session_race_sandboxed EXCLUDE_FROM_ALL
            tests/exec_session_race_test.cpp tests/sandbox_config_race_stubs.cpp
            src/tool/util/exec.cpp src/tool/util/sandbox.cpp
            src/tool/util/subprocess.cpp src/tool/util/sandbox_claybin.cpp
            src/tool/util/sandbox_netproxy.cpp
            src/tool/util/sandbox_broker.cpp src/tool/util/handoff_gate.cpp
            src/domain/sandbox_provenance.cpp src/tool/util/utf8.cpp
            src/util/logx.cpp src/util/dbglog.cpp src/util/home_dir.cpp
            src/util/user_root.cpp src/util/teardown.cpp)
        target_compile_definitions(exec_session_race_sandboxed PRIVATE
            EXEC_RACE_REAL_SANDBOX=1 AGENTTY_MCP=0)
        target_include_directories(exec_session_race_sandboxed PRIVATE include)
        target_link_libraries(exec_session_race_sandboxed PRIVATE
            mcp::tools claybin nlohmann_json::nlohmann_json maya::app Threads::Threads)
        add_test(NAME exec_session_race_sandboxed COMMAND exec_session_race_sandboxed)
        set_tests_properties(exec_session_race_sandboxed PROPERTIES TIMEOUT 180 LABELS sandbox)
    endif()
endif()

# NOTE:seam_stress_test故意没有获得狭窄的TSan目标。
#
# 它驱动的 接口边界 — provider::select/
# active、工具 目录、功能 注册表 — 是整个应用程序汇
# 聚的部分，因此它们需要身份验证、帐户、密钥库、cred_crypt、
# 保管库和 HTTP 客户端。它的 "narrow" 版本是
# 归并 的大部分，但有额外的步骤，并且伪造该链所需的存根文件足够大，
# 足以构成其自身的维护风险（并且会准确伪造正在测试的
# 加锁 的代码）。
#
# 因此，它在正常套件中运行UNINSTRUMENTED，
# 其中其断言（没有撕裂的选择、没有畸形的快照)仍然成立，并且这些接口
# 边界的TSan覆盖来自real_subsystem_race_test，
# 其通过狭窄的编译单元驱动相同的publish/consume形状。
# 如果感觉不够，则采取的行动是为 TSan 通道提供 归并 并接受成本，
# 而不是停止正在测试的子系统。


agentty_test(cred_crypt_test MODE raw LABELS sanitizer)
add_executable(cred_crypt_test EXCLUDE_FROM_ALL
    tests/cred_crypt_test.cpp src/io/cred_crypt.cpp src/util/base64.cpp)
target_include_directories(cred_crypt_test PRIVATE include)
target_link_libraries(cred_crypt_test PRIVATE
    maya::maya maya::runtime nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME cred_crypt_test COMMAND cred_crypt_test)
set_tests_properties(cred_crypt_test PROPERTIES TIMEOUT 60 LABELS sanitizer)

# logx：独立 二进制程序 ON PURPOSE — 日志系统在首次使用时
# 锁定其环境配置（神奇静态），因此测试必须拥有其进程才能在记录任何内容之前
# 设置 AGENTTY_LOG/_FILE。
agentty_test(logx_test MODE raw)
add_executable(logx_test EXCLUDE_FROM_ALL
    tests/logx_test.cpp src/util/logx.cpp src/util/dbglog.cpp)
target_include_directories(logx_test PRIVATE include)
# logx.cpp仅将maya::guarded用于其旋转锁:头文件。
target_include_directories(logx_test PRIVATE
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME logx_test COMMAND logx_test)
set_tests_properties(logx_test PROPERTIES TIMEOUT 30)

# logx redaction/format：由于 相同 原因而独立，
# 如上面的 logx_test — 接收器在首次使用时锁存，因此需要记录 ON
# 的测试必须拥有其进程。这些被简单地合并到合并的 二进制程序 中，
# 其中守卫 `if (!logging_on()) return;` 使所有
# 8 个案件通过了 CI 中的 ZERO 断言：一套绿色套件什么也证明不了。
# ENV 使 ctest 在 二进制程序 启动之前配置日志，因此断言实际运行。
foreach(_logx_t logx_redaction_test logx_format_test logx_lifecycle_test)
    agentty_test(${_logx_t} MODE raw)
    add_executable(${_logx_t} EXCLUDE_FROM_ALL
        tests/${_logx_t}.cpp tests/test_main.cpp
        src/util/logx.cpp src/util/dbglog.cpp)
    target_include_directories(${_logx_t} PRIVATE include tests)
    # nlohmann针对HEADERS，而不是针对符号：这些测试包括agtest.hpp，
    # 其达到tool/util/fs_helpers.hpp->tool/registry.hpp-
    # ><nlohmann/json.hpp>。原始目标无法从对象 库 获取
    # 包含路径，因此如果没有此选项，构建将在 SCAN 步骤（C++20 模
    # 块依赖项扫描）失败，这就是为什么它在 CI 中作为致命 "no such file"
    # 在测试中出现，即 链接 没有 JSON-related。
    target_link_libraries(${_logx_t} PRIVATE
        doctest::doctest maya::maya nlohmann_json::nlohmann_json)
    # logx.cpp仅将maya::guarded用于其旋转锁:头文件。
    target_include_directories(${_logx_t} PRIVATE
        $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
    # AGENTTY_HOME，不是退役的AGENTTY_LOG_FILE：目的地
    # 现在是`--log-file`，并且这些二进制程序是从未见过agentty的
    # argv 解析器的doctest干线。将用户 root 指向构建目录会使接收器
    # 解析为 <root>/logs/agentty.log，这是测试通过
    # logx::log_file() 读回的内容。
    add_test(NAME ${_logx_t} COMMAND ${_logx_t})
    set_tests_properties(${_logx_t} PROPERTIES TIMEOUT 30
        ENVIRONMENT "AGENTTY_LOG=trace;AGENTTY_HOME=${CMAKE_CURRENT_BINARY_DIR}/${_logx_t}.home")
endforeach()

# logx旋转：与上面的块相同的standalone-process原因。
# 它还需要一个足够小的旋转阈值，实际上是 CROSS — 在发
# 货 32 MB 时，mid-run 旋转 接口边界 需要几分钟才能达到，
# 这正是其中的 use-after-close 幸存下来的原因：
# 写入者在没有旋转 锁 的情况下读取接收器 fd，因此旧的 swap
# -then-close 发布了内核可以在其下回收的描述符。
# 该测试通过内部 符号 本身降低阈值，因此此处无需配置环境旋钮。
agentty_test(logx_rotation_test MODE raw)
add_executable(logx_rotation_test EXCLUDE_FROM_ALL
    tests/logx_rotation_test.cpp tests/test_main.cpp
    src/util/logx.cpp src/util/dbglog.cpp)
target_include_directories(logx_rotation_test PRIVATE include tests)
# 与上面的logx块（agtest.hpp)相同的传递性nlohmann包括。
target_link_libraries(logx_rotation_test PRIVATE
    doctest::doctest maya::maya nlohmann_json::nlohmann_json)
# logx.cpp仅将maya::guarded用于其旋转锁:头文件。
target_include_directories(logx_rotation_test PRIVATE
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME logx_rotation_test COMMAND logx_rotation_test)
set_tests_properties(logx_rotation_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "AGENTTY_LOG=trace;AGENTTY_HOME=${CMAKE_CURRENT_BINARY_DIR}/logx_rotation_test.home")

# keystore.cpp通过产生它的助手run_child，
# 它位于沙箱和工具层上，所以这需要完整的链接。不在消毒剂通道
# 中：它会跳过没有AGENTTY_USE_KEYSTORE和一个钥匙圈，
# CI两者都没有，其完整链接使该通道编译了所有agentty在
# ASan 下（~390 个文件），直到跑步者被杀死。
agentty_test(keystore_test MODE standalone
    OBJS $<TARGET_OBJECTS:agentty_acp_obj> LIBS acp::acp)
if(WIN32 AND TARGET keystore_test)
    target_link_libraries(keystore_test PRIVATE advapi32)   # CredReadW & co
endif()

agentty_test(host_escape_test MODE raw)
add_executable(host_escape_test EXCLUDE_FROM_ALL
    tests/host_escape_test.cpp src/runtime/view/host_escape.cpp)
target_include_directories(host_escape_test PRIVATE include)
add_test(NAME host_escape_test COMMAND host_escape_test)
set_tests_properties(host_escape_test PROPERTIES TIMEOUT 30)

# ── 循环亲和度 : agentty '环' -only 状态真的就是循环 -only ───────────
#
# jaal自己的测试证明了loop_bound<T>的行为。
# 他们没有提及 此 源码树 是否使用它，而这个差距就是过去的保证消失
# 的地方：jaal 运送了 loop_bound，而 jaal +
# maya + agentty 中的数量加起来为零，因为获得代币的唯一
# 方法是伪造一个。这个测试锁定了消费者的一半——熔炉关闭，
# agentty的渲染 缓存在循环中是可到达的，而工作线程可以到达一个ABORTS
# 而不是从它自己的空副本中进行绘制。
#
# raw/standalone，未合并：最后一种情况在分叉子项中断言 SIGABRT，
# 而 doctest 无法托管该子项。 头文件-only 链接（jaal + 测试），
# 因此它保持约 3 秒 编译。
agentty_test(loop_affinity_test MODE raw LABELS sanitizer)
add_executable(loop_affinity_test EXCLUDE_FROM_ALL tests/loop_affinity_test.cpp)
target_include_directories(loop_affinity_test PRIVATE include)
# 直接jaal、不 maya:这个测试是关于运行时的loop-affinity的保证，
# 所以应该不需要终端框架来链接。无论如何，maya 不会重新包含
# -export jaal 的包含目录。
target_link_libraries(loop_affinity_test PRIVATE maya::app)
add_test(NAME loop_affinity_test COMMAND loop_affinity_test)
set_tests_properties(loop_affinity_test PROPERTIES TIMEOUT 60 LABELS sanitizer)

# Single-root布局+遗留~/.config/agentty迁移。
# 缩小 链接 — 仅 user_root.cpp + home_dir.cpp —
# 因此沙盒 $HOME 操作无法与任何其他子系统的静态数据交互。
agentty_test(user_root_test MODE raw)
add_executable(user_root_test EXCLUDE_FROM_ALL
    tests/user_root_test.cpp src/util/user_root.cpp src/util/home_dir.cpp)
target_include_directories(user_root_test PRIVATE include)
target_link_libraries(user_root_test PRIVATE maya::app)   # user_root.cpp使用maya::guarded
add_test(NAME user_root_test COMMAND user_root_test)
set_tests_properties(user_root_test PROPERTIES TIMEOUT 30)

# WRITE-side存储原语(agentty::dirs)，范围对端。
# 与 user_root_test 相同的狭窄-link推理：此测试
# chdir() 并突变 $HOME，因此它 链接 仅执行它所执行的 编译单元，
# 而不是拖入其静态可以观察到的子系统。
#
# logx被包括在内，因为dirs.cpp记录了其决议； fs_helpers 代表
# project_root()/set_workspace_root()，锚点行走从该处开始。
agentty_test(dirs_test MODE raw)
add_executable(dirs_test EXCLUDE_FROM_ALL
    tests/dirs_test.cpp
    src/dirs/dirs.cpp
    src/io/shared_file.cpp
    src/util/user_root.cpp
    src/util/home_dir.cpp
    src/util/logx.cpp
    src/tool/util/fs_helpers.cpp
    src/tool/util/disk_files.cpp)
# 运行时的头文件:dirs.cpp和user_root.cpp都使用maya::guarded
# 作为它们的warn-once集。如果没有这个，独立目标根本就不是 编译（它是
# EXCLUDE_FROM_ALL，所以普通的 `ninja` 从来没有注意到）。
target_include_directories(dirs_test PRIVATE include
    $<TARGET_PROPERTY:maya::app,INTERFACE_INCLUDE_DIRECTORIES>)
# 出于同样的原因，keystore_test 需要相同的传递依赖：logx 拉取 nlohmann
# + maya，并且 fs_helpers.cpp 包括 mcp-cpp 的 util
# 头文件 以将 工作区 根镜像到其中。
target_link_libraries(dirs_test PRIVATE maya::maya maya::runtime nlohmann_json::nlohmann_json)
if(TARGET mcp::tools)
    target_link_libraries(dirs_test PRIVATE mcp::tools)
endif()
add_test(NAME dirs_test COMMAND dirs_test)
set_tests_properties(dirs_test PROPERTIES TIMEOUT 30)

# ── CLI 参数顺序 ──────────────────────────────────────────────────────
# shell测试，因为parse_args存在于main.cpp内部并且值得固定的
# 合约是BINARY的：`agentty run`必须以任何顺序接受--agent、
# -w/-m和提示词。 解析器 过去常常停在它不拥有的第一个 标志 处，因此 -w
# 之后的 提示词 会作为 "unknown arg: <prompt>" 死亡—
# —这是一条看不见的规则，因为错误命名的是 提示词 而不是位置。
add_test(NAME cli_arg_order_test
         COMMAND ${CMAKE_COMMAND} -E env sh
                 ${CMAKE_SOURCE_DIR}/tests/cli_arg_order_test.sh
                 $<TARGET_FILE:agentty>)
# 二进制程序 的 15 次发射。 Windows 上的调试版本约为
# 860 MB 未剥离的图像，每次启动需要约 3-6 秒来进行映射（在
# 空闲盒子上总共需要 39 秒）；在并行 ctest 下，
# 映射与所有其他测试的 I/O 竞争，并超过了 3 倍。 RUN_SERIAL
# 保持诚实； 解析 本身是微秒。
set_tests_properties(cli_arg_order_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# `agentty config move`/医生对抗真正的二进制程序在刮伤家里。
if(NOT WIN32)
    add_test(NAME config_move_test
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/config_move_test.sh $<TARGET_FILE:agentty>)
    set_tests_properties(config_move_test PROPERTIES TIMEOUT 60)
    # `agentty acp`驱动就像Zed/JetBrains驱动一样，
    # 每条消息都对照官方ACP v1 模式定义 (tests/acp/schema/v1)进行检查。
    add_test(NAME acp_client_test
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/acp/run_acp_client.sh $<TARGET_FILE:agentty>)
    set_tests_properties(acp_client_test PROPERTIES TIMEOUT 180)
    # `agentty run`脚本使用它的方式：退出代码、--json、-o、
    # --max-turns、--timeout和Ctrl-C/SIGTERM杀死工具源码树。
    add_test(NAME run_cli_test
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_cli_test.sh $<TARGET_FILE:agentty>)
    set_tests_properties(run_cli_test PROPERTIES TIMEOUT 120)
    # search_code/search_docs质量地板经评判评估，
    # 关键字-only因此不需要嵌入器(tests/rag_eval)。
    add_test(NAME rag_eval_guard
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/rag_eval/rag_eval_guard.sh
                     $<TARGET_FILE:agentty> ${CMAKE_SOURCE_DIR})
    set_tests_properties(rag_eval_guard PROPERTIES TIMEOUT 300 LABELS rag)
endif()

# ── 最终:构建agentty_tests+衍生总量 ──────────────────────
agentty_finalize_tests()

# ── reveal_stream_gate 武器——测试正在运行的条目 anthropic_md_stream ──────
# 实况逐步显示上的回归门滑过记录的Anthropic 数据流。
set(_RSG_FIXTURE ${CMAKE_SOURCE_DIR}/tests/fixtures/anthropic_md_smoke.jsonl)
agentty_add_ctest(reveal_stream_gate COMMAND
    agentty_standalone_tests anthropic_md_stream det ${_RSG_FIXTURE}
    --assert-max-delta 40 --assert-finalize-max 40 --assert-finalize-ms 3600)
agentty_add_ctest(reveal_stream_gate_prod COMMAND
    agentty_standalone_tests anthropic_md_stream det ${_RSG_FIXTURE}
    --cps 45 --drain 0.40 --adaptive
    --assert-max-delta 40 --assert-finalize-max 40 --assert-finalize-ms 3600)
agentty_add_ctest(reveal_stream_gate_snap COMMAND
    agentty_standalone_tests anthropic_md_stream det ${_RSG_FIXTURE}
    --cps 45 --drain 0.40 --adaptive --snap-at 40 --snap-glide 150
    --assert-max-delta 40 --assert-finalize-max 40 --assert-finalize-ms 3600)

# REASONING简介，推理-SHAPED 数据流。
#
# 上面的固定装置是答案散文：频繁的小增量。推理是相反的形状 - 提供者默默
# 思考 1-3 秒，然后发出 ~300-character 摘要块 -
# 并且两者的调整不同。 Reasoning 的超前窗口为 0.28 秒，
# 该值由 "never hoard a backlog to dump at the reasoning->answer settle"
# 证明合理；答案路径已经尝试了紧密的滞后（0.15），分析了它，
# 并退出了它，因为短的 缓冲区 使光标空闲 AT 突发之间
# 的边缘 - 冲刺，然后停顿，这就是 "slow and bursty"
# 的含义。
#
# 在此装置上测量，用产品自身的自适应范围扫描滞后窗口：0.28使光标空闲
# -at-edge在实时帧的7.0%上（最大每-frame跳跃14)，
# 在2.9%上0.40 (11)，在1.6%上0.55 (9)，在0.9%上
# 0.70 (8)——而最终-of-reasoning排水管拉伸1.9s /
# 2.5s / 3.0s。车道运行 0.55，膝盖。该门固定了三角洲上限，
# 因此未来的重新调整无法悄悄地重新引入分块。
#
# 自适应WINDOW被显式传递，因为它是配置文件的一部分：
# 没有它，线束使用maya的默认值(20..220)并且该
# "reasoning profile"正在测量应答车道的窗口。
set(_RSG_REASONING ${CMAKE_SOURCE_DIR}/tests/fixtures/reasoning_burst_shape.jsonl)
agentty_add_ctest(reveal_stream_gate_reasoning COMMAND
    agentty_standalone_tests anthropic_md_stream det ${_RSG_REASONING}
    --cps 60 --drain 0.55 --adaptive --adaptive-min 45 --adaptive-max 280
    --assert-max-delta 16 --assert-finalize-max 24 --assert-finalize-ms 3600)

# ── 货币ban-list ───────────────────────────────────────────
# 原始线程、锁、原子和thread_locals仅在人类签署时
# 表明它们ARE是安全类型的实现。其他一切都经过jaal（任务而不是线程，
# guarded<T>而不是互斥量，loop_bound<T>
# 而不是thread_local）。
#
# 这是jaal自己的指向agentty的支票(third_party/maya/third_party/jaal/tests/lint/banlist.cmake)。
# jaal 在 ITS 源码树 上运行它，这对消费者没有任何影响：
# agentty 可能会在循环旁边生成一个裸露的 线程，
# 或者将 状态更新函数 状态置于 thread_local 中，而 jaal
# 永远不会注意到。两个根，因为 允许清单 在相对于它扫描的根的路径上键。
#
# 成本：读取一个文件，没有编译。添加条目是故意行为
# ——声称有人手工检查了使用情况。
set(_BANLIST ${CMAKE_SOURCE_DIR}/third_party/maya/third_party/jaal/tests/lint/banlist.cmake)
if(EXISTS ${_BANLIST})
    add_test(NAME concurrency_banlist_src
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_SOURCE_DIR}/src
                     -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/allowlist.txt
                     -P ${_BANLIST})
    add_test(NAME concurrency_banlist_include
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_SOURCE_DIR}/include
                     -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/allow_include.txt
                     -P ${_BANLIST})
    set_tests_properties(concurrency_banlist_src concurrency_banlist_include
                         PROPERTIES LABELS "static")

    # 对下面的层进行相同的检查，每个层都有其自己的允许清单。
    # 它们也在 maya 和 jaal 自己的构建中运行，但 agentty
    # 不构建这些，因此如果没有这些，maya 或 jaal 中的新原始原语将在
    # 这里看不到。
    set(_MAYA ${CMAKE_SOURCE_DIR}/third_party/maya)
    set(_JAAL ${_MAYA}/third_party/jaal)
    foreach(_bl
            "maya_src|${_MAYA}/src|${_MAYA}/tests/lint/allowlist.txt"
            "maya_include|${_MAYA}/include/maya|${_MAYA}/tests/lint/allow_include.txt"
            "jaal_src|${_JAAL}/src|${_JAAL}/tests/lint/allowlist_src.txt"
            "jaal_include|${_JAAL}/include/jaal|${_JAAL}/tests/lint/allowlist.txt")
        string(REPLACE "|" ";" _bl "${_bl}")
        list(GET _bl 0 _n)
        list(GET _bl 1 _root)
        list(GET _bl 2 _allow)
        add_test(NAME concurrency_banlist_${_n}
                 COMMAND ${CMAKE_COMMAND} -DROOT=${_root} -DALLOW=${_allow} -P ${_BANLIST})
        set_tests_properties(concurrency_banlist_${_n} PROPERTIES LABELS "static")
    endforeach()
endif()

# ── 榆树纯洁 ────────────────────────────────────────────────────
# update()是(Model，Msg)的纯函数：没有IO、没有时钟、
# 没有环境、没有进程-global从状态更新函数写入。
# tests/lint/elm_allowlist.txt 为剩余债务；
# 严格检查拒绝任何新事物，严格检查拒绝不再需要的豁免，因此列表只会缩小。
# ── 层层 : agentty → maya → jaal ────────────────────────────────
# agentty仅通过maya (<maya/runtime.hpp>)到达运行时，而不是直接通过jaal。
# docs/LAYERING.md.
add_test(NAME layering
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/layering.cmake)
set_tests_properties(layering PROPERTIES LABELS static)
# 链的其余部分，来自 agentty 自己的静态标签，
# 因此一个 `ctest -L static` 检查整个层次结构：maya 在其之上没有命名任何内容，
# 并且只有 maya/host/ 触及 jaal； jaal 上面没有任何名称。
add_test(NAME layering_maya
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}/third_party/maya
                 -P ${CMAKE_SOURCE_DIR}/third_party/maya/tests/lint/layering.cmake)
add_test(NAME layering_jaal
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}/third_party/maya/third_party/jaal
                 -P ${CMAKE_SOURCE_DIR}/third_party/maya/third_party/jaal/tests/lint/layering.cmake)
set_tests_properties(layering_maya layering_jaal PROPERTIES LABELS static TIMEOUT 120)
# maya的seam.sh:仅host/和re-export 头文件接触jaal，
# 没有分离的线程，一个SIGWINCH所有者。没有任何东西运行它，所以它运行 过期。
add_test(NAME maya_seam
         COMMAND sh ${CMAKE_SOURCE_DIR}/third_party/maya/tests/seam.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/maya)
set_tests_properties(maya_seam PROPERTIES LABELS static TIMEOUT 60)
# 库 agentty供应商不持有运行时，保留一份JSON-RPC核心的副本，并且jsonrpc
# -cpp保留叶子(docs/PROTOCOL_LIBRARIES.md§7)。
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
foreach(_pl purity duplication jsonrpc_leaf spawn_via_jaal no_sleep_poll setenv_in_main tool_boundary)
    if(_pl STREQUAL "purity")
        set(_pl_name submodule_purity)
    elseif(_pl STREQUAL "duplication")
        set(_pl_name no_duplication)
    else()
        set(_pl_name ${_pl})
    endif()
    add_test(NAME ${_pl_name}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/protocol_libraries.py
                     ${CMAKE_SOURCE_DIR} ${_pl})
    set_tests_properties(${_pl_name} PROPERTIES LABELS static TIMEOUT 60)
endforeach()
# disk_writes:共享文件以一种方式写入(io/shared_file.hpp)。
add_test(NAME disk_writes
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/disk_writes.py
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(disk_writes PROPERTIES LABELS static TIMEOUT 60)
# hidden_locks：手动检查原子等待/call_once/自旋。
add_test(NAME hidden_locks
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/hidden_locks.py
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(hidden_locks PROPERTIES LABELS static TIMEOUT 60)
endif()

add_test(NAME io_roots
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/io_roots.py ${CMAKE_SOURCE_DIR})
set_tests_properties(io_roots PROPERTIES LABELS static TIMEOUT 60)

# I/O不能被召唤：状态更新函数既不能调用和IO函数也不能
# 构建令牌。情况 0 编译，因此损坏的包含不能作为失败通过。
foreach(_io_case 0 1 2)
    add_test(NAME io_token_case_${_io_case}
             COMMAND ${CMAKE_CXX_COMPILER} -std=c++26 -fsyntax-only
                     -DIO_CASE=${_io_case} -I${CMAKE_SOURCE_DIR}/include
                     ${CMAKE_SOURCE_DIR}/tests/compile_fail/io_token.cpp)
    set_tests_properties(io_token_case_${_io_case} PROPERTIES LABELS static TIMEOUT 60)
    if(NOT _io_case EQUAL 0)
        set_tests_properties(io_token_case_${_io_case} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()

add_test(NAME elm_purity
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_SOURCE_DIR}/src/runtime/app
                 -DVIEW_ROOT=${CMAKE_SOURCE_DIR}/src
                 -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/elm_allowlist.txt
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/elm_purity.cmake)
add_test(NAME elm_purity_tight
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_SOURCE_DIR}/src/runtime/app
                 -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/elm_allowlist.txt
                 -DTIGHT=1
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/elm_purity.cmake)
set_tests_properties(elm_purity elm_purity_tight PROPERTIES LABELS "static")

# ── 乌蒂层 ─────────────────────────────────────────────────
# 存在两个`util`命名空间（tools::util=工具边界，
# util=app-wide管道）并且ONE-WAY依赖性使得冲突保持可容忍。
# sail3r 将这对标记为难以区分； 头文件 现在
# 解释了拆分，这使得该规则成为构建错误而不是约定。
add_test(NAME util_layering
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/util_layering.cmake)
set_tests_properties(util_layering PROPERTIES LABELS "static")

# ── 工程点目录 ─────────────────────────────────────────────
# 用户root将自身排序为cache/ credentials/
# logs/ threads/；项目根漂移到四个生命周期中的十三个平面条目。
# 每一次都合法地到达一个提交，这就是审阅者错过下一个提交的原因。
# 这使得顶层成为一个已声明的集合，因此该类别是一个决定而不是偶然。
# 参见 tests/lint/project_dotdir.cmake.
add_test(NAME project_dotdir
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/project_dotdir.cmake)
set_tests_properties(project_dotdir PROPERTIES LABELS "static")

# ── 密件文件模式 ───────────────────────────────────────────
# credentials/是0600审计应该没有发现异常的一个目录。
# accounts.json 是七个 0600 兄弟姐妹中的 0644，
# 因为 std::ofstream 在 0666 & ~umask 创建，
# 没有其他说明。包含在 0700 目录中，并且仍然值得进行结构防护：该包含
# 距离成为唯一剩下的东西只有一个 chmod 距离。
add_test(NAME secret_modes
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/secret_modes.cmake)
set_tests_properties(secret_modes PROPERTIES LABELS "static")

# ── i18n 目录 数据 ───────────────────────────────────────────────────────────
# 三个门：每个t() id都存在于英文目录中，每个目录 id都被使用，
# 并且src/provider或src/tool中没有任何内容翻译（model-facing字符串保持英文
# ——参见tests/lint/i18n_lint.cmake)。
#
# 第一个是使字符串扫描可审查的原因："did we drop one"
# 不是人类可以在数百个移动文字中回答的问题，
# 并且丢失的id 渲染作为屏幕上的id而不是大声失败。
#
# 与上面的禁用清单形状和成本相同：文本扫描，没有编译。
add_test(NAME i18n_catalog_lint
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/lint/i18n_lint.cmake)
set_tests_properties(i18n_catalog_lint PROPERTIES LABELS "static")

# ── ui::Prefs 实地表格 ────────────────────────────────────
# settings.json写入器、其加载器和保存
# -time合并都走ui_prefs::for_each_field，
# 因此它们都没有命名字段。剩下的 接口边界 是添加到结构而不是该表的字
# 段：编译器 看不到它，并且该设置将永远不会持久存在。
add_test(NAME prefs_field_sync
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/lint/prefs_sync.py
                 ${CMAKE_SOURCE_DIR}/include/agentty/domain/ui_prefs.hpp)
set_tests_properties(prefs_field_sync PROPERTIES LABELS "static")

# ── agentty 描述 UI, maya 画出来 ────────────────────────────
# agentty的视图层返回maya::Element值；每个像素的决定
# ——布局、截断、颜色分辨率、光标——都是 maya 的。
# 这就是让 maya 小部件在 agentty 不注意的情况下更改其宽度协商的原因。
#
# 习惯所保持的界限，没有任何强制力。这禁止编写 canvas/cell、
# 定义 paint/draw/render 方法并发出原始 ANSI 转义，
# 并带有两个命名的非 -TUI 异常（Ctrl+G 运行程序和 沙箱 的
# plain-stderr 路径，均位于 maya 的 终端 之外）。
add_test(NAME ui_boundary
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/lint/ui_boundary.py
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(ui_boundary PROPERTIES LABELS "static")

# ── 允许清单 烂掉 ──────────────────────────────────────────────────────────
# 禁用清单遍历磁盘上的文件并且仅遍历LOOKS、UP和允许清单，
# 因此其正当理由消失的授权被默默地忽略而不是被报告。
# 这会以一种方式腐烂：豁免比代码的寿命更长，并且下一个向该文件添加
# std::mutex 的人继承了无人授予他们的通行证。添加此内容时
# 进行测量：4 个名为不再存在的文件的条目和 28 个文件已停止使用
# 的授予原语 - 包括 provider/ollama/provider.cpp，
# 很久以前重命名为 transport.cpp，以至于后继者
# 都没有使用其条目仍然允许的两个原语。
#
# Cost是文件读取，没有编译。缩小 授权 的范围是保持 禁用
# 清单 诚实的廉价一半。
set(_PRUNE ${CMAKE_SOURCE_DIR}/tests/lint/prune_allowlist.cmake)
if(EXISTS ${_PRUNE})
    add_test(NAME allowlist_tight_src
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_SOURCE_DIR}/src
                     -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/allowlist.txt
                     -P ${_PRUNE})
    add_test(NAME allowlist_tight_include
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_SOURCE_DIR}/include
                     -DALLOW=${CMAKE_SOURCE_DIR}/tests/lint/allow_include.txt
                     -P ${_PRUNE})
    set_tests_properties(allowlist_tight_src allowlist_tight_include
                         PROPERTIES LABELS "static")
endif()

# local_stub_claims:tests/fake_local_server.py仍然服务于tests/verify_stub_against_real.py声称真实服务器发送的每个形状吗？
#
#
# 这两个文件是我们和 LM Studio bug 重复之间
# 的唯一障碍，其中读者在服务器作为对象发送的字段上需要
# is_boolean()，并且每个模型都默默地从其文件名中
# 进行猜测。存根是我们写下我们认为每个本地服务器返回的内容的地方；
# CLAIMS 是我们写下要与真品进行检查的内容的
# 地方。
#
# 他们朝着隐藏的方向漂移：存根的lmstudio模式根本
# 没有能力对象，因此提交添加的读取器从未在任何测试中执行，
# 并且CLAIMS也从未提及该字段。什么都没有失败。
# 该功能根本没有覆盖范围，而且看起来也有覆盖范围。
#
# 这提出了两个问题中较弱的一个——存根是否满足我们所声称的
# 需求——因为它是不需要硬件的一半。它不能告诉你这个说法与现实相符；
# 只需将脚本指向真正的 llama.cpp 或 LM
# Studio 即可。它的作用是：在不声明声明的情况下向
# 存根添加形状（反之亦然）现在在这里失败，而不是几年后。
find_package(Python3 COMPONENTS Interpreter QUIET)
set(_STUBCHK ${CMAKE_SOURCE_DIR}/tests/verify_stub_against_real.py)
if(Python3_Interpreter_FOUND AND EXISTS ${_STUBCHK})
    add_test(NAME local_stub_claims
             COMMAND ${Python3_EXECUTABLE} ${_STUBCHK} --self-check)
    # 绑定环回端口来运行存根，因此使其远离并行清理器通道，
    # 并为其提供五个连续服务器启动的空间。
    set_tests_properties(local_stub_claims PROPERTIES
                         LABELS "static" TIMEOUT 120 RUN_SERIAL TRUE)
endif()
