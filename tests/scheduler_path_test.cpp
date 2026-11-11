// scheduler_path_test 路径 -aware 并行工具列表( Q)
// cmd::schedule_parallel_batch 输入 cmd_factory.cpp).
//
//   agentty 已经打开并行工具执行 EffectSet (
//   read/net 作曲; write/exec 要求独家访问。
//   此测试将锁定 PATH- AWARE 改进:两个作者将文件解开,
//   在同一波中运行,一个读取与一个无关的写同时运行,以及重叠 /
//   exec / 盲 -writer 案件仍按序列排列。 maya ,
//   没有阶段机械。

#include "agtest.hpp"

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using agentty::ToolUse;
using agentty::ToolName;
using agentty::ToolCallId;
using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::Profile;
using agentty::phase::Streaming;
using agentty::phase::Active;
using agentty::app::cmd::kick_pending_tools;
using agentty::app::cmd::schedule_parallel_batch;

namespace {
void expect(const char* name, bool cond, const std::string& detail = {}) {
    std::string msg = detail.empty() ? std::string{name}
                                     : std::string{name} + " — " + detail;
    CHECK_MESSAGE(cond, msg);
}

// A PENDING 工具 调用 ,并带有给定名称+参数。
ToolUse pending(const char* name, nlohmann::json args) {
    ToolUse t;
    static int c = 0;
    t.id   = ToolCallId{"c" + std::to_string(++c)};
    t.name = ToolName{name};
    t.args = std::move(args);
    t.status = ToolUse::Pending{std::chrono::steady_clock::now()};
    return t;
}
ToolUse running(const char* name, nlohmann::json args) {
    ToolUse t = pending(name, std::move(args));
    t.status = ToolUse::Running{std::chrono::steady_clock::now(), {}};
    return t;
}

std::string promoted_str(const std::vector<std::size_t>& v) {
    std::string s = "{";
    for (auto i : v) { s += std::to_string(i); s += ','; }
    s += "}";
    return s;
}
bool has(const std::vector<std::size_t>& v, std::size_t i) {
    for (auto x : v) if (x == i) return true;
    return false;
}
} // 命名空间结束。

TEST_CASE("scheduler path") {
    std::printf("scheduler_path_test — path-aware parallel scheduling\n\n");

    namespace fs = std::filesystem;
    const fs::path workspace = fs::temp_directory_path() / "agentty_scheduler_workspace";
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), workspace);

    // (a) 两次写信给: DISJOINT 文档 – 两者 升职( 同时运行).
    {
        std::vector<ToolUse> b = {
            pending("write", {{"file_path", "a.cpp"}, {"content", "x"}}),
            pending("write", {{"file_path", "b.cpp"}, {"content", "y"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("disjoint writes both run", d.promote.size() == 2,
              promoted_str(d.promote));
    }

    // (b) 两名代表致函 相同 文档 = 首次 运行(序列化)。
    {
        std::vector<ToolUse> b = {
            pending("write", {{"file_path", "same.cpp"}, {"content", "x"}}),
            pending("edit",  {{"path", "same.cpp"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("same-file writes serialise", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }

    // (c) 读取 a.cpp + 写入 b.cpp → 两者 运行
    //     ( 脱节, 虽写着粗糙的 ) 副作用 规则会把它们串起来的。
    {
        std::vector<ToolUse> b = {
            pending("read",  {{"path", "a.cpp"}}),
            pending("write", {{"file_path", "b.cpp"}, {"content", "y"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("read + disjoint write run together", d.promote.size() == 2,
              promoted_str(d.promote));
    }

    // (d) 读取 a.cpp + 写入 a.cpp ——只读数运行(重叠).
    {
        std::vector<ToolUse> b = {
            pending("read",  {{"path", "a.cpp"}}),
            pending("write", {{"file_path", "a.cpp"}, {"content", "y"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("read + same-file write serialise", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }

    // (e) 目录 -prefix 重叠:编辑 "src/a.c" vs 写入 "src/" ~ 串行记.
    {
        std::vector<ToolUse> b = {
            pending("edit",  {{"path", "src/a.c"}}),
            pending("write", {{"file_path", "src"}, {"content", "x"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("dir-prefix overlap serialises", d.promote.size() == 1,
              promoted_str(d.promote));
    }

    // (f) "srcfoo" 谓 不 下级 "src" − 边界 -correct 都跑开
    {
        std::vector<ToolUse> b = {
            pending("write", {{"file_path", "src"}, {"content", "x"}}),
            pending("write", {{"file_path", "srcfoo"}, {"content", "y"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("srcfoo not under src", d.promote.size() == 2, promoted_str(d.promote));
    }

    // (g) 批次中任何地方的批次(Exec) ——它按序逐一地与一切进行对比。
    //     已发售的读数 之前 击打运行; 击打+ 后读停留 待处理.
    {
        std::vector<ToolUse> b = {
            pending("read", {{"path", "a.cpp"}}),
            pending("shell", {{"command", "rm -rf build"}}),
            pending("read", {{"path", "b.cpp"}}),
        };
        auto d = schedule_parallel_batch(b);
        // 0( 读) 运行; 1( bash) 与运行中读 − 冲突。
        // 待处理 ; 2 (已读) 读 -safe 但拍拍没有跑——
        // 读了 -compatible 正在读的,所以它 ALSO 跑步,单打独斗
        expect("bash defers, reads run", has(d.promote, 0) && has(d.promote, 2)
              && !has(d.promote, 1), promoted_str(d.promote));
    }

    // (h) 盲作家(无可提取路径) 独家的系列剧。
    {
        std::vector<ToolUse> b = {
            pending("read",  {{"path", "a.cpp"}}),
            pending("write", {{"content", "no path here"}}),   // 无file_path
        };
        auto d = schedule_parallel_batch(b);
        expect("blind writer serialises", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }

    // (i) 许多不相连的读物 – 全部运行(常见的fan-out案例)。
    {
        std::vector<ToolUse> b = {
            pending("read", {{"path", "a"}}),
            pending("read", {{"path", "b"}}),
            pending("read", {{"path", "c"}}),
            pending("grep", {{"pattern", "foo"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("read fan-out all parallel", d.promote.size() == 4, promoted_str(d.promote));
    }

    // (j) A RUNNING 写到 a.cpp 块a 待处理 读取 a.cpp 但不是,
    //     没有 待处理 读取 b.cpp.
    {
        std::vector<ToolUse> b = {
            running("write", {{"file_path", "a.cpp"}, {"content", "x"}}),
            pending("read",  {{"path", "a.cpp"}}),   // 已封锁
            pending("read",  {{"path", "b.cpp"}}),   // 免费
        };
        auto d = schedule_parallel_batch(b);
        expect("running write blocks overlapping read only",
              !has(d.promote, 1) && has(d.promote, 2), promoted_str(d.promote));
    }

    // (k) 任务 FAN-OUT : 在一个波中的若干副剂 全部 运行同时。
    //     这就是工具的全部要点—— sched_effects 将任务映射到 { ReadFs ,
    //     Net ) (可合并),而其 PERMISSION 大门继续开 Exec.
    {
        std::vector<ToolUse> b = {
            pending("task", {{"prompt", "explore module A"}}),
            pending("task", {{"prompt", "explore module B"}}),
            pending("task", {{"prompt", "review the diff"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("task fan-out all parallel", d.promote.size() == 3,
              promoted_str(d.promote));
    }

    // (l) 任务+读作组曲(副代理机与直接读作组一起运行)。
    {
        std::vector<ToolUse> b = {
            pending("task", {{"prompt", "map the codebase"}}),
            pending("read", {{"path", "a.cpp"}}),
            pending("grep", {{"pattern", "foo"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("task + reads run together", d.promote.size() == 3,
              promoted_str(d.promote));
    }

    // (m) 任务确实 不 专机是独家的,任务悬浮的视野仍然
    //     随身携带 ReadFs/Net 与Exec冲突。
    {
        std::vector<ToolUse> b = {
            pending("shell", {{"command", "make -j8"}}),
            pending("task", {{"prompt", "explore while building"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("bash excludes task", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }

    // (m2) WAITS 连续排列。 "sleep; check" 炮弹和
    //      process_polls 表示 "wait, look, wait again"
    //      : 一起运行, 它们都从头开始。 只有第一个等待才能运行; 没有跳过它。
    {
        std::vector<ToolUse> b = {
            pending("shell", {{"command", "sleep 270; tail -1 build.log"}}),
            pending("process_poll", {{"id", "proc-1"}, {"wait_ms", 250000}}),
            pending("shell", {{"command", "sleep 280; tail -1 build.log"}}),
            pending("process_poll", {{"id", "proc-1"}, {"wait_ms", 250000}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("only the first wait runs", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }
    {
        // 随着第一场等待的进行,之后的民意调查仍然在等待轮到它.
        std::vector<ToolUse> b = {
            running("shell", {{"command", "sleep 270; tail -1 build.log"}}),
            pending("process_poll", {{"id", "proc-1"}, {"wait_ms", 250000}}),
            pending("process_poll", {{"id", "proc-1"}, {"wait_ms", 250000}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("a poll never overtakes a running wait", d.promote.empty(),
              promoted_str(d.promote));
    }
    {
        // 同一批次的读物不等待,仍会扇出.
        std::vector<ToolUse> b = {
            pending("process_poll", {{"id", "proc-1"}}),
            pending("read", {{"path", "a.cpp"}}),
            pending("process_poll", {{"id", "proc-1"}}),
            pending("read", {{"path", "b.cpp"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("reads beside one wait", has(d.promote, 0) && has(d.promote, 1)
              && !has(d.promote, 2), promoted_str(d.promote));
    }

    // (m3) process_start 那样 process_poll 一
    //      组: 民意测验名称 开始时的会话, 所以它不能运行在它旁边。
    {
        std::vector<ToolUse> b = {
            pending("process_start", {{"command", "make"}}),
            pending("process_poll", {{"id", "proc-2"}, {"wait_ms", 290000}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("start runs, poll waits for it", d.promote.size() == 1 && has(d.promote, 0),
              promoted_str(d.promote));
    }
    {
        std::vector<ToolUse> b = {
            running("process_start", {{"command", "make"}}),
            pending("process_poll", {{"id", "proc-2"}}),
            pending("process_stop", {{"id", "proc-2"}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("nothing overtakes a running process_start", d.promote.empty(),
              promoted_str(d.promote));
    }

    // (n) 语法别名和绝对-vs-relative拼写法将同一资源命名,
    //     因此序列化。
    {
        std::vector<ToolUse> b = {
            pending("write", {{"file_path", "src/./nested/../a.cpp"}, {"content", "x"}}),
            pending("read",  {{"path", (workspace / "src/a.cpp").string()}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("lexical and absolute aliases serialise",
              d.promote.size() == 1 && has(d.promote, 0), promoted_str(d.promote));
    }

    // (o) 一旦作者活动,正常化的不确定性即告失效。
    {
        std::vector<ToolUse> b = {
            pending("write", {{"file_path", "a.cpp"}, {"content", "x"}}),
            pending("read",  {{"path", 42}}),
        };
        auto d = schedule_parallel_batch(b);
        expect("uncertain path serialises with writer",
              d.promote.size() == 1 && has(d.promote, 0), promoted_str(d.promote));
    }

    // (p) 安全二号 -phase 权限 行为:发现后期
    //     权限 提示词 必须在更早的可发送之前发生 调用 已变异
    //     为运行( its) Cmd 否则,将因提前返回而放弃。
    {
        Model m;
        m.d.profile = Profile::Ask;
        m.s.phase = Streaming{Active{}};
        Message asst;
        asst.role = Role::Assistant;
        asst.tool_calls.push_back(pending("read", {{"path", "a.cpp"}}));
        asst.tool_calls.push_back(pending("write", {{"file_path", "b.cpp"},
                                                     {"content", "x"}}));
        const auto permission_id = asst.tool_calls[1].id;
        m.d.current.messages.push_back(std::move(asst));
        auto ignored = kick_pending_tools(m);
        (void)ignored;
        const auto& calls = m.d.current.messages.back().tool_calls;
        expect("permission preflight leaves earlier tool Pending",
              calls[0].is_pending() && calls[1].is_pending());
        expect("permission preflight selects later guarded tool",
              m.d.pending_permission && m.d.pending_permission->id == permission_id);
    }
}
