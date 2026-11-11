// hooks.cpp 用户 -authored 生命周期钩子,
// 看 hooks.hpp 设计及同意 -gate 理由说明。
//
//   • 国家 载荷 旅行经过 AGENTTY_HOOK_PAYLOAD_FILE (模式0600临时)
//     文件)而不是 stdin : run_shell_command
//     没有标准 -pipe 接口边界 ,文件将保持多 -MB 工具结果出自
//     环境块(env vars有硬平台盖).
//   • 钩式命令贯穿 run_shell_command - 电话 相同 环形山 /
//     沙箱-exec 包装用到的击打工具——所以一个经批准的钩子是 工作区
//     -confined 完全像模型一样 -driven 壳壳。
//   • 核准储存:内容 -hash 通过 SHARED scope::Approvals
//     原始( 同一插件使用) ,
//     坚持到 ~/.agentty/hooks_approved.json 以
//     列表列出已核准的文件散列。 字节 更改为钩状文件生成新散列 = re
//     -gated 预览 -scope {路径:散列}商店在第一读时透明迁移。

#include "agentty/tool/hooks.hpp"
#include "agentty/util/user_root.hpp"
#include "agentty/config/inventory.hpp"   // kHooks 核准——一个名字
#include "agentty/util/capped_read.hpp"   // 原始的封顶-read

#include "agentty/auth/auth.hpp"            // auth::sha256_hex (档案 -content (hash) 语句
#include "agentty/scope/scope.hpp"          // scope::Approvals(共享信托商店)
#include "agentty/io/persistence.hpp"       // random_hex_id
#include "agentty/io/shared_file.hpp"       // create_exclusive
#include "agentty/tool/util/sandbox.hpp"    // run_shell_command
#include "agentty/tool/util/subprocess.hpp"
#include "agentty/tool/util/utf8.hpp"        // strip_terminal_controls, to_valid_utf8

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>   // chmod
#include <unistd.h>     // 发作
#else
#include <process.h>    // _getpid
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace agentty::tools::hooks {

namespace {

constexpr std::size_t kMaxHooksFileBytes = 64 * 1024;
constexpr std::size_t kMaxPayloadBytes   = 4 * 1024 * 1024;
constexpr auto        kHookTimeout       = std::chrono::seconds{30};

[[nodiscard]] inline std::uint64_t current_pid() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

struct HookEntry {
    std::string match;   // 工具名上的ERE
    std::string run;     // sh -c 命令
};

struct HooksFile {
    std::string            path;      // 文件的绝对路径已加载
    std::string            hash;      // 原生的沙256块 字节
    std::vector<HookEntry> pre_tool;
    std::vector<HookEntry> post_tool;
    bool                   ok = false;
    // 设定钩子文件时 EXISTS 但是已经过了这个尺寸了,
    // 所以什么也做不了 已加载 与它不同,与它不同 "no file"
    // :一个使用屏蔽钩子的用户默默无闻,理应被告知它是什么.
    std::string            too_big;   // 超大小路径, 否则为空
};

[[nodiscard]] bool hooks_disabled() {
    const char* off = std::getenv("AGENTTY_NO_HOOKS");
    return off && off[0] && off[0] != '0';
}

// NOTE : 当地 read_all () 已消失——见 util/capped_read.hpp.
// 里面还有另外两本 skills.cpp 财务报告和已审计财务报表 commands.cpp ,所有数据都运出相同的错误 :
// "" 意味着失踪 和 太大了,所以,我说 `agentty hooks`
// 打印 "no hooks file" 和一个无声的阻断钩子

// 地点+ 解析 活动钩子文件。 不 缓存到交叉处 调用 目的:每
// 个工具一次钩火 调用 (秒相隔),一个 stat+ 解析 那边有噪音,
// 然后跳过 缓存 删除整个类的 Staleness 错误( 编辑)
// hooks.json ——下一个工具 调用 看见了,批准后就登门).
[[nodiscard]] HooksFile load_hooks_file() {
    HooksFile out;
    const fs::path candidates[] = {
        fs::path{".agentty"} / "hooks.json",
        ::agentty::util::user_root().empty() ? fs::path{}
                                  : ::agentty::util::user_root() / "hooks.json",
    };
    for (const auto& c : candidates) {
        if (c.empty()) continue;
        const auto got = ::agentty::util::capped_read(c, kMaxHooksFileBytes);
        if (::agentty::util::oversize_bytes(got) > 0 && out.too_big.empty())
            out.too_big = c.string();
        const std::string& raw = ::agentty::util::bytes_or_empty(got);
        if (raw.empty()) continue;
        std::error_code ec;
        auto abs = fs::weakly_canonical(c, ec);
        out.path = (ec ? fs::absolute(c, ec) : abs).string();
        out.hash = auth::sha256_hex(raw);
        json j = json::parse(raw, nullptr, /*投掷=*/false);
        if (!j.is_object()) return out;   // OK=假: 无法解析
        auto read_list = [&](const char* key, std::vector<HookEntry>& dst) {
            if (!j.contains(key) || !j[key].is_array()) return;
            for (const auto& e : j[key]) {
                if (!e.is_object()) continue;
                HookEntry h;
                h.match = e.value("match", std::string{});
                h.run   = e.value("run", std::string{});
                if (!h.run.empty()) dst.push_back(std::move(h));
            }
        };
        read_list("pre_tool",  out.pre_tool);
        read_list("post_tool", out.post_tool);
        out.ok = true;
        return out;
    }
    return out;   // 无文件
}

// -scope { path: hash} 存储, 如果仍然在周围。
// 范围拥有当前位置( 用户 root / state/); 这只需要找到一个旧文件即可迁移,
// 所以它看起来都是两个文件。
[[nodiscard]] fs::path legacy_approvals_path() {
    auto root = ::agentty::util::user_root();
    if (root.empty()) return {};
    const fs::path leaf{::agentty::config::kHooksApprovals};
    std::error_code ec;
    const fs::path in_state = ::agentty::util::user_state_dir() / leaf;
    if (fs::is_regular_file(in_state, ec)) return in_state;
    return root / leaf;
}

// 虎克信任现在骑在路上 SHARED 内容 -hash 原始(
// 4月 ) scope::Approvals ——每个可执行文件都有一个信任机制。
// -from-untrusted-origin 表面(插头、钩子)。
// 散列无论哪种方式都相同( scope::content_hash ==
// auth::sha256_hex ,和 hf.hash 已经 sha256_hex ),
// 所以没有关于安全财产的更改:批准文件的精确性 字节 ; 任意 字节 更改产生
// 新的散列,不在批准的一组,所以它是 -gates.
//
// 店里留有叶子 "hooks_approved.json" 用于连续性。 scope::Approvals 是
// 一个平坦的散列 LIST 预 告 -scope 格式为 { abs_path 说吧,我... OBJECT.
// load_hook_approvals () 读作其中之一——
// 物体通过散列迁移而发生。 VALUES 所以升级时没有人会失去批准
constexpr std::string_view kHooksApprovalsLeaf =
    ::agentty::config::kHooksApprovals;

[[nodiscard]] scope::Approvals load_hook_approvals() {
    // 快路:瞄准镜加载器处理新的数组形式(并在遗留
    // 对象形式上返回空,它不能将它作为解析作为数组).
    scope::Approvals a = scope::load_approvals(kHooksApprovalsLeaf);
    if (!a.shas.empty()) return a;
    // 遗产{路径:散列}对象 – 将其值移入列表中。
    const auto p = legacy_approvals_path();
    std::string raw = std::string{::agentty::util::bytes_or_empty(
        ::agentty::util::capped_read(p, kMaxHooksFileBytes))};
    if (raw.empty()) return a;
    json j = json::parse(raw, nullptr, /*投掷=*/false);
    if (j.is_object())
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it->is_string()) a.approve(it->get<std::string>());
    return a;
}

[[nodiscard]] bool is_approved(const HooksFile& hf) {
    if (hf.path.empty() || hf.hash.empty()) return false;
    return load_hook_approvals().approved(hf.hash);
}

void store_approval(Io io, const maya::unlocked& cap, const HooksFile& hf) {
    if (hf.hash.empty()) return;
    scope::Approvals a = load_hook_approvals();   // 包括任何迁移的遗产散装物
    a.approve(hf.hash);
    (void)scope::save_approvals(io, cap, kHooksApprovalsLeaf, a);   // 重写为数组形式
}

[[nodiscard]] bool name_matches(const std::string& ere, std::string_view tool) {
    if (ere.empty()) return true;   // 没有匹配的密钥 QQ 每个工具
    try {
        std::regex re(ere, std::regex::extended | std::regex::nosubs);
        return std::regex_search(std::string{tool}, re);
    } catch (const std::regex_error&) {
        return false;   // 坏模式从不匹配( 无法关闭预端)
    }
}

// Write 编号 载荷 以返回私有临时文件;返回路径 ('在错误中)。
// 临时目录与其他用户共享,因此文件在0600时创建完整,
// 永远不能超越一个已有的名称:没有人能提早读取,
// 或将写作的符号设置在其中.
[[nodiscard]] std::string write_payload(const std::string& payload) {
    std::error_code ec;
    auto dir = fs::temp_directory_path(ec);
    if (ec) return {};
    const auto base = "agentty_hook_" + std::to_string(current_pid()) + "_"
                    + persistence::random_hex_id();
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto p = dir / (base + "_" + std::to_string(attempt));
        if (persistence::detail::create_in_section(p.string(), payload, 0600)) return p.string();
    }
    return {};
}

// 前缀 `cmd` 包含可变任务, shell 仅适用于该命令。
// POSIX: `export A='v' B='v'; <cmd>` (出口后,
// 钩子是脚本) 看见他们,单身 -quoted 故任何价值都不能被解释。
// Windows cmd.exe: `set "A=v" && set "B=v" && call <cmd>` ;
// 这里的值是 事件 名称、工具名称和临时路径,没有一条 `"` 或 或 时 间 `%`。
// 该词 `call` 谓 加载-bearing : cmd 扩展% VAR 当它 PARSES 线条,
// 在任何线条之前 `set` 上面有一条钩子,
// 写着 `findstr x "%AGENTTY_HOOK_PAYLOAD_FILE%"` 见文义文.
// `call` 内容 -parses 其论点在套装之后,也就是在Vars存在的时候。
[[nodiscard]] std::string with_hook_env(
    const std::string& cmd,
    std::initializer_list<std::pair<const char*, std::string>> vars) {
    std::string out;
#ifndef _WIN32
    out = "export";
    for (const auto& [k, v] : vars) {
        out += ' ';
        out += k;
        out += "='";
        for (char c : v) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += '\'';
    }
    out += "; ";
#else
    for (const auto& [k, v] : vars) {
        out += "set \"";
        out += k;
        out += '=';
        out += v;
        out += "\" && ";
    }
    out += "call ";
#endif
    out += cmd;
    return out;
}

// 运行一个钩状命令 载荷 通过 env + 文件 调制。
// 返回{ exit_code , 捕获输出].
struct HookRun { int exit_code = 0; std::string output; };
[[nodiscard]] HookRun run_hook(const HookEntry& h, std::string_view event,
                               std::string_view tool,
                               const std::string& payload) {
    const std::string pf = write_payload(payload);
    // 钩子的上下文在ITS命令行中,而不是进程内涵:
    // setenv/unsetenv 变异状态 线程 因此,
    // 同时使用的工具需要全球的共享。 锁。每个运行前缀 -process.
    const std::string cmd = with_hook_env(h.run, {
        {"AGENTTY_HOOK_EVENT",        std::string{event}},
        {"AGENTTY_HOOK_TOOL",         std::string{tool}},
        {"AGENTTY_HOOK_PAYLOAD_FILE", pf},
    });
    auto res = util::sandbox::run_shell_command(cmd, kMaxPayloadBytes,
                                                kHookTimeout);
    if (!pf.empty()) {
        std::error_code ec;
        fs::remove(pf, ec);
    }
    return {res.exit_code, std::move(res.output)};
}

} // 命名空间结束。

PreToolDecision run_pre_tool(std::string_view tool,
                             const std::string& args_json) {
    PreToolDecision d;
    if (hooks_disabled()) return d;
    HooksFile hf = load_hooks_file();
    if (!hf.ok || hf.pre_tool.empty()) return d;
    if (!is_approved(hf)) return d;   // 未经批准的文件NEVER运行

    json payload = {{"event", "pre_tool"},
                    {"tool", std::string{tool}},
                    {"args", args_json}};
    // error_handler_t::replace: args_json 可能埋入损失 字节;
    // dump() THROWS 无效 UTF -8 默认——工具路径内部发生崩溃.
    const std::string body =
        payload.dump(-1, ' ', false, json::error_handler_t::replace);
    for (const auto& h : hf.pre_tool) {
        if (!name_matches(h.match, tool)) continue;
        HookRun r = run_hook(h, "pre_tool", tool, body);
        if (r.exit_code != 0) {
            d.blocked = true;
            if (r.output.empty()) {
                d.reason = "blocked by pre_tool hook (`" + h.run + "` exited "
                         + std::to_string(r.exit_code) + ")";
            } else {
                // 钩子的断层变成错误 MODEL 读写 - 擦除 终端 控件,
                // 强制有效 UTF -8,盖上它(a 4) MB
                // 警卫 -script 垃圾场会毁了转弯的象征性预算。
                std::string reason = util::to_valid_utf8(
                    util::strip_terminal_controls(r.output));
                if (reason.size() > 4096) {
                    reason.resize(4096);
                    reason = util::to_valid_utf8(std::move(reason));
                    reason += "\n[hook output truncated at 4 KiB]";
                }
                d.reason = std::move(reason);
            }
            return d;
        }
    }
    return d;
}

void run_post_tool(std::string_view tool, const std::string& args_json,
                   const std::string& result_text) {
    if (hooks_disabled()) return;
    HooksFile hf = load_hooks_file();
    if (!hf.ok || hf.post_tool.empty()) return;
    if (!is_approved(hf)) return;

    json payload = {{"event", "post_tool"},
                    {"tool", std::string{tool}},
                    {"args", args_json},
                    {"result",
                     result_text.size() > kMaxPayloadBytes
                         ? result_text.substr(0, kMaxPayloadBytes)
                         : result_text}};
    // 替换 -handler: result_text 是任意工具输出( bash 能够
    // 发出原始 字节 ; 默认 dump() 丢弃无效 UTF-8.
    const std::string body =
        payload.dump(-1, ' ', false, json::error_handler_t::replace);
    for (const auto& h : hf.post_tool) {
        if (!name_matches(h.match, tool)) continue;
        (void)run_hook(h, "post_tool", tool, body);   // 开火 -and-forget
    }
}

bool pending_approval(Io) {
    if (hooks_disabled()) return false;
    HooksFile hf = load_hooks_file();
    return hf.ok && !is_approved(hf);
}

std::string active_file(Io) {
    return load_hooks_file().path;
}

int cli(Io io, const maya::unlocked& cap, const std::string& verb) {
    HooksFile hf = load_hooks_file();
    if (verb == "list" || verb.empty()) {
        if (hf.path.empty()) {
            // "No file"和"a file too big to load"
            // 是不同的答案,而打印第一个答案的第二个是:阻塞钩
            // 如何在用户认为它有武器的同时保持无声的惰性.
            if (!hf.too_big.empty()) {
                std::printf("%s is over the %zu KB limit — NOT loaded,"
                            " so no hooks are running\n",
                            hf.too_big.c_str(), kMaxHooksFileBytes / 1024);
                return 1;
            }
            std::printf("no hooks file (.agentty/hooks.json or "
                        "~/.agentty/hooks.json)\n");
            return 0;
        }
        std::printf("hooks file: %s\n", hf.path.c_str());
        if (!hf.ok) {
            std::printf("  PARSE ERROR — file is not valid JSON\n");
            return 1;
        }
        std::printf("  approval:  %s\n",
                    is_approved(hf) ? "APPROVED"
                                    : "NOT APPROVED (hooks will not run — "
                                      "`agentty hooks approve`)");
        auto show = [](const char* ev, const std::vector<HookEntry>& v) {
            for (const auto& h : v)
                std::printf("  %-9s match=%-20s run=%s\n", ev,
                            h.match.empty() ? "(all)" : h.match.c_str(),
                            h.run.c_str());
        };
        show("pre_tool",  hf.pre_tool);
        show("post_tool", hf.post_tool);
        return 0;
    }
    if (verb == "approve") {
        if (hf.path.empty()) {
            if (!hf.too_big.empty()) {
                std::fprintf(stderr, "agentty hooks approve: %s is over the "
                                     "%zu KB limit — nothing to approve\n",
                             hf.too_big.c_str(), kMaxHooksFileBytes / 1024);
                return 1;
            }
            std::fprintf(stderr, "agentty hooks approve: no hooks file found\n");
            return 1;
        }
        if (!hf.ok) {
            std::fprintf(stderr, "agentty hooks approve: %s is not valid "
                                 "JSON — fix it first\n", hf.path.c_str());
            return 1;
        }
        if (is_approved(hf)) {
            std::printf("already approved: %s\n", hf.path.c_str());
            return 0;
        }
        // 显示正批准的字节,然后需要明确y.
        std::printf("── %s ──\n%s\n────────\n"
                    "These hooks run ARBITRARY SHELL automatically on "
                    "matching tool calls\n(sandboxed like the bash tool, "
                    "but still able to act inside your workspace).\n"
                    "Approve this exact file content? [y/N] ",
                    hf.path.c_str(),
                    std::string{::agentty::util::bytes_or_empty(
                        ::agentty::util::capped_read(
                            hf.path, kMaxHooksFileBytes))}.c_str());
        std::fflush(stdout);
        std::string line;
        std::getline(std::cin, line);
        if (line != "y" && line != "Y" && line != "yes") {
            std::printf("not approved\n");
            return 1;
        }
        store_approval(io, cap, hf);
        std::printf("approved (any byte change re-gates)\n");
        return 0;
    }
    std::fprintf(stderr, "usage: agentty hooks [list|approve]\n");
    return 2;
}

} // namespace agentty::tools::hooks
