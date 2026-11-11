// `cmd`外壳-tool别名的复航试验(
// agentty/runtime/app/update/stream_args.hpp).
//
// 复制与 Laguna S 2.1 的观测差异: 模型释放 shell 工具的命令参数为
// `cmd` (OpenAI-style 光谱形状)而不是 模式定义's `command`.
// mcp-cpp's 分派器 ArgReader 已经接受 `cmd` 通过其
// 别名表——但东道主 -side 所需员额 -field 警卫(
// missing_required_field 和每个 UI 表面( 微米) 权限 纸牌、
// 输出面板、时间表)改为 `tc.args["command"]` 字面意思是,所以,
// 我说, 调用 与死亡的 "missing the required field `command`"
// 之前的 分派。修补的神通 `cmd` → `command` 在每
// 个参数中 -parse 并让警卫化名 -aware.
//
// 独立( 无) maya /无 运行时) — 编译 反 对 头文件 + nlohmann
// + 光谱,镜像 param_tag_repair_test.cpp.

#include "agtest.hpp"

#include "agentty/runtime/app/update/stream_args.hpp"

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using agentty::app::detail::canonify_alias_keys;
using agentty::app::detail::canonify_tool_args;
using agentty::app::detail::kCommandAliases;
using agentty::app::detail::missing_required_field;

namespace {
void expect(const std::string& name, bool cond, const std::string& detail = {}) {
    std::string msg = detail.empty() ? name : name + " — " + detail;
    CHECK_MESSAGE(cond, msg);
}
} // 命名空间结束。

TEST_CASE("shell cmd alias canonification") {
    // ── 案例1:在地图上看到的准确形状 传输 (拉古纳S 2.1) ─────────
    // {"cmd": "cat /etc/os-release …"} — 解析 不错,那老警卫就失败了
    // "missing the required field `command`".（缺少必需字段 command。）
    {
        json args = {{"cmd", "cat /etc/os-release 2>/dev/null | head -3"}};
        // 将“第一”加冕(作为 解析 现场现在有,然后守卫。
        (void)canonify_tool_args("shell", args);
        auto missing = missing_required_field("shell", args);
        expect("cmd-shaped call passes the required-field guard",
               missing.empty(),
               "missing_required_field returned `" + std::string{missing} + "`");
        expect("canonical key materialised", args.contains("command"));
        expect("canonical value preserved",
               args["command"].get<std::string>()
                   == "cat /etc/os-release 2>/dev/null | head -3");
        expect("alias entry kept so retry sees what was sent",
               args.contains("cmd"));
    }

    // ── 案例2: 每一个可接受的异名, 被封禁 ────────────────────────
    {
        for (auto alias : kCommandAliases) {
            if (alias == "command") continue;
            json args = json::object();
            args[std::string{alias}] = "echo alias-ok";
            (void)canonify_tool_args("shell", args);
            auto missing = missing_required_field("shell", args);
            std::string an{alias};
            expect(an + " accepted by the guard",
                   missing.empty(),
                   "missing_required_field returned `" + std::string{missing}
                       + "`");
            expect(an + " canonified",
                   args.contains("command")
                       && args["command"].get<std::string>() == "echo alias-ok");
        }
    }

    // ── 案例3: 已存在的峡谷密钥胜诉 ──────────────────────
    // 两种密钥都发送( 稀有, 但代理可以合并块):
    // 犬形值绝不能被别名覆盖。
    {
        json args = {{"command", "echo canonical"},
                     {"cmd",     "echo alias"}};
        bool changed = canonify_alias_keys(args, "command", kCommandAliases);
        expect("no rename when canonical key present", !changed);
        expect("canonical value untouched",
               args["command"].get<std::string>() == "echo canonical");
        expect("guard passes", missing_required_field("shell", args).empty());
    }

    // ── 案例4:真正失踪 ——仍然失败,没有改变 ──────────────
    // 警卫是别名扩展,而不是削弱所需的战区:一个
    // 拥有NO命令变体的hell 调用仍然报告错误.
    {
        json args = {{"display_description", "no command here"}};
        auto missing = missing_required_field("shell", args);
        expect("missing command still reported",
               missing == "command",
               "returned `" + std::string{missing} + "`");
        (void)canonify_tool_args("shell", args);
        expect("canonify is a no-op with no alias key",
               !args.contains("command"));
    }

    // ── 案例5:非-string别名价值没有被划线 ────────────────
    // 守卫的别名检查只接受非空字符串; 将合同与合同相匹配,
    // 从而成为数字 `cmd` 不能作为走私者通过 "empty" 命令(
    // 命令 分派器 将胁迫, -dump ) , , —— —— , ) .
    {
        json args = {{"cmd", 42}};
        bool changed = canonify_alias_keys(args, "command", kCommandAliases);
        expect("non-string alias not renamed", !changed);
        expect("missing still reported",
               missing_required_field("shell", args) == "command");
    }

    // ── 案例6:工具范围界定——其他工具未受影响 ────────────────
    {
        json read_args = {{"cmd", "should stay"}};
        expect("canonify is inert for non-command tools",
               !canonify_tool_args("read", read_args));
        expect("read args unchanged",
               !read_args.contains("command"));
        json empty = json::object();
        expect("no crash on empty object",
               !canonify_tool_args("shell", empty));
    }

    // ── 案例7:诊断/测试/process_start共享固定 ────────
    {
        for (std::string_view tn : {"diagnostics", "test", "process_start"}) {
            json args = {{"cmd", "echo shared"}};
            (void)canonify_tool_args(tn, args);
            auto missing = missing_required_field(tn, args);
            std::string tn_s{tn};
            expect(tn_s + " cmd-shaped call passes the guard",
                   missing.empty(),
                   "missing_required_field returned `" + std::string{missing}
                       + "`");
            expect(tn_s + " canonified",
                   args["command"].get<std::string>() == "echo shared");
        }
    }

    // ── 案例8:别名清单保留SHORT,并故意如此 ────────
    //
    // 每个条目都是一个键,其值被交给 shell,
    // 所以一个错误的猜测不会花费一个失败的调用——它执行错误的字符串.
    // `shell` 财务报告和已审计财务报表 `run` 名单的第一版中,并退出:
    // `shell` 作为工具上的密钥 NAMED 壳是模糊的,而不是拼写
    // "the command" ,和 `run` 是个真实的 agentty 子
    // 命令模型可以发送其它含义( 特别是要发送到 process_start).
    //
    // 被钉住作为测试,因为失败模式是 re -adding
    // 沉默和坏: 调用 成功,运行一个无人想要的字符串。
    {
        for (auto alias : kCommandAliases) {
            expect("no ambiguous alias in the command family",
                   alias != "shell" && alias != "run",
                   "`" + std::string{alias} + "` is a key whose value we hand "
                   "to a shell; it must be an unambiguous spelling of "
                   "\"the command to run\"");
        }

        // 而送ONLY的调用,一个未接受的密钥,
        // 仍必须大声地让守军失败,而不是默默地晋升到一个指挥部.
        json args = {{"shell", "rm -rf /"}};
        (void)canonify_tool_args("shell", args);
        expect("an unaccepted key is NOT promoted to command",
               !args.contains("command"));
        expect("and the guard still rejects the call",
               missing_required_field("shell", args) == "command",
               "a key we do not understand must fail loudly, not execute");
    }

    // ── 案例9: 被没收为IDEMPOTENT ───────────────────────────────
    //
    // 现在它运行在 数据流-parse 网站 和 再来一次 分派 边界(
    // 秒数 ) mcp_tools_bridge's def.execute ),
    // 所以在正常路径上两次被加冕。第二段通道必须是无 -op : 第一位
    // 离开 `command` 即时,情况正是如此
    // canonify_alias_keys 拒绝接触。
    {
        json args = {{"cmd", "echo once"}};
        const bool first = canonify_tool_args("shell", args);
        const bool second = canonify_tool_args("shell", args);
        expect("first pass canonifies", first);
        expect("second pass is a no-op", !second);
        expect("value survives both passes",
               args["command"].get<std::string>() == "echo once");
        expect("original key is still there for a model re-reading its call",
               args["cmd"].get<std::string>() == "echo once");
    }
}

TEST_CASE("edit top-level old_text/new_text guard acceptance") {
    // 第二次拉古纳S 2.1号发现的回转(do_edit.md追踪)。
    // 编辑工具的编辑[]条目键为old_text/new_text——模式
    // 定义的OWN词汇——模式漏出拼写到顶端的-levelold_string/new_string槽.
    // parse_edit_args接受两个级别上的两种拼写,
    // 因此调用是很好的-formed,本会执行;
    // 警卫用"missing the required field `old_string`"失败,
    // 因为kOldStrAliases/kNewStrAliases没有列出_文本表.
    // 与贝壳固定不同,NO canonifation涉及: .
    // 每个编辑表面( 守护、 预览嗅探器, edit_body ,
    // 则 ACP diff卡)已经读过两种拼写,而单是拓宽了
    // 警卫就使整个路径都奏效了.

    // ── 案例1:根据 传输 痕量重建的准确形状 ──────
    // 流化的碎片 : { "path": …, "old_text": …, "new_text": …,
    // "display_description" :...}(记录) 末尾 开始于 `, "new_text":`).
    {
        json args = {{"path", "~/.config/yazi/superuser.yazi/main.lua"},
                     {"old_text", "local sudo = \"sudo\""},
                     {"new_text", "local sudo = {\"sudo\", \"sudo-rs\", \"run0\", \"doas\"}"},
                     {"display_description",
                      "Add -S flag to sudo/sudo-rs and mark stdin-capable escalators"}};
        auto missing = missing_required_field("edit", args);
        expect("top-level old_text/new_text call passes the guard",
               missing.empty(),
               "missing_required_field returned `" + std::string{missing} + "`");
    }

    // ── 案例2: 警卫现在接受的每对拼写 ───────────────
    {
        struct Spelling { const char* oldk; const char* newk; };
        const Spelling spellings[] = {{"old_string", "new_string"},
                                      {"old_text",   "new_text"},
                                      {"old_str",    "new_str"},
                                      {"oldStr",     "newStr"}};
        for (const auto& sp : spellings) {
            json args = {{"path", "a.lua"},
                         {sp.oldk, "x"},
                         {sp.newk, "y"}};
            auto missing = missing_required_field("edit", args);
            expect(std::string{sp.oldk} + "/" + std::string{sp.newk}
                       + " accepted by the edit guard",
                   missing.empty(),
                   "returned `" + std::string{missing} + "`");
        }
    }

    // 用例 3：接受混合字段写法，每个字段都分别检查
    //    独立地使用自己的表格
    {
        json args = {{"path", "a.lua"},
                     {"old_text", "x"},
                     {"new_string", "y"}};
        expect("mixed old_text + new_string accepted",
               missing_required_field("edit", args).empty());
        json args2 = {{"path", "a.lua"},
                      {"old_string", "x"},
                      {"new_text", "y"}};
        expect("mixed old_string + new_text accepted",
               missing_required_field("edit", args2).empty());
    }

    // ── 案例4:保留了短-circuit行为 ────────────
    // 非 国 家 -empty 编辑[ 阵列在顶部之前满足守护 -level
    // old/new 检查过运行。
    {
        json args = {{"path", "a.lua"},
                     {"edits", json::array({
                         json{{"old_text", "x"}, {"new_text", "y"}}})}};
        expect("edits[] call passes", missing_required_field("edit", args).empty());
        json empty_edits = {{"path", "a.lua"},
                            {"edits", json::array()}};
        auto missing = missing_required_field("edit", empty_edits);
        expect("empty edits[] falls through to top-level check",
               missing == "old_string",
               "returned `" + std::string{missing} + "`");
    }

    // ── 案例5:非-evidenced同义词不包括在内 ─────────────────────────
    // 政策(见2004年6月30日的表格评论) stream_args.hpp
    // : 添加一个别名,只加上一个显示实际排放的模型的文字记录。
    // 这些模型没有一个,所以它们必须不断失败——一个错误。 `old`
    // 猜测编辑路径上不花费失败的成本 调用 ,它会变换错误的文本。
    {
        json args = {{"path", "a.lua"},
                     {"old", "x"},
                     {"new", "y"}};
        expect("bare old/new NOT accepted (unevidenced)",
               missing_required_field("edit", args) == "old_string");
    }

    // ── 案例6:真正失踪仍未成功,没有改变 ─────────────────
    {
        json args = {{"path", "a.lua"}, {"new_text", "y"}};
        auto missing = missing_required_field("edit", args);
        expect("missing old_string still reported",
               missing == "old_string",
               "returned `" + std::string{missing} + "`");
        json args2 = {{"path", "a.lua"}, {"old_text", "x"}};
        auto missing2 = missing_required_field("edit", args2);
        expect("missing new_string still reported",
               missing2 == "new_string",
               "returned `" + std::string{missing2} + "`");
    }
}