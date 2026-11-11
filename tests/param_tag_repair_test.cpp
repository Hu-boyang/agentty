// 返回测试 XML-in-JSON 工具 -call 恢复(
// agentty/runtime/app/update/param_tag_repair.hpp).
//
// 复制在真实会话中观察到的失败模式: 模型混合 Anthropic 内部
// `<parameter name="…">` 工具 -call 语法进入 JSON
// 工具输入,在战术上生成合成 -valid-but-wrong 将所需字段埋入一个
// 偏移字符串值的参数。 单独( 无) maya /无 运行时) — 编译 反
// 对 头文件 + nlohmann + spec. + 双倍分数 + 单倍分数

#include "agtest.hpp"

#include "agentty/runtime/app/update/param_tag_repair.hpp"

#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using agentty::app::detail::repair_param_tag_leak;

namespace {
void expect(const char* name, bool cond, const std::string& detail = {}) {
    std::string msg = detail.empty() ? std::string{name}
                                     : std::string{name} + " — " + detail;
    CHECK_MESSAGE(cond, msg);
}
} // 命名空间结束。

TEST_CASE("param tag repair") {
    // ── 案例1:在地图上看到的准确形状 传输 ──────────────────────
    // {"path","display_description","edit":"\n<parameter name=\"old_text\">OLD",
    //  "new_text":"NEW" · 记 `edit` (单声道)和 old_text 藏在体内
    {
        json args = {
            {"display_description", "probe edit"},
            {"edit", "\n<parameter name=\"old_text\">    constexpr int k = 1;\n"},
            {"new_text", "    constexpr int k = 2;\n"},
            {"path", "src/foo.cpp"},
        };
        bool changed = repair_param_tag_leak("edit", args);
        expect("case1: repaired", changed, args.dump());
        expect("case1: edits array present",
              args.contains("edits") && args["edits"].is_array()
                  && args["edits"].size() == 1, args.dump());
        expect("case1: stray `edit` key removed", !args.contains("edit"));
        if (args.contains("edits") && !args["edits"].empty()) {
            auto& e = args["edits"][0];
            expect("case1: old_text recovered byte-exact",
                  e.value("old_text", "") == "    constexpr int k = 1;\n",
                  e.value("old_text", ""));
            expect("case1: new_text from clean key",
                  e.value("new_text", "") == "    constexpr int k = 2;\n",
                  e.value("new_text", ""));
        }
        expect("case1: path preserved", args.value("path", "") == "src/foo.cpp");
        expect("case1: display_description preserved",
              args.value("display_description", "") == "probe edit");
    }

    // ── 案例2:旧的和新的都走私到一个字符串中,并有更近的 ──
    {
        json args = {
            {"path", "a.cpp"},
            {"edits", "<parameter name=\"old_text\">AAA</parameter>"
                      "<parameter name=\"new_text\">BBB</parameter>"},
        };
        bool changed = repair_param_tag_leak("edit", args);
        expect("case2: repaired", changed, args.dump());
        if (args.contains("edits") && args["edits"].is_array()
            && !args["edits"].empty()) {
            auto& e = args["edits"][0];
            expect("case2: old_text", e.value("old_text", "") == "AAA");
            expect("case2: new_text", e.value("new_text", "") == "BBB");
        } else {
            expect("case2: edits is array", false, args.dump());
        }
    }

    // ── 案例3:没有标记 + 未标记,返回假 ──────────────────
    {
        json args = {
            {"path", "a.cpp"},
            {"edits", json::array({ json{{"old_text","X"},{"new_text","Y"}} })},
        };
        json before = args;
        bool changed = repair_param_tag_leak("edit", args);
        expect("case3: not repaired", !changed);
        expect("case3: unchanged", args == before, args.dump());
    }

    // ── 案例4:写漏——内容被走私到一条失落的密钥中 ─────────
    {
        json args = {
            {"path", "new.txt"},
            {"write", "<parameter name=\"content\">hello\nworld\n"},
        };
        bool changed = repair_param_tag_leak("write", args);
        expect("case4: repaired", changed, args.dump());
        expect("case4: content recovered",
              args.value("content", "") == "hello\nworld\n",
              args.value("content", ""));
        expect("case4: path preserved", args.value("path", "") == "new.txt");
    }

    // 用例 5：格式正确的 edit，其 new_text 合法包含
    //    文字标记文本。 extract_param_tags 只
    //    扫描 TOP-LEVEL 字符串值,所以标记嵌入在
    //    `edits` 数组( 非) -string ——修理
    //    是否 -op 和真实的, old_text 这个,
    //    加上已经叫人的人 -failing 路径,这就是为什么一个
    //    包含标记(类似此标记)的文件永远不会被损坏。
    {
        json args = {
            {"path", "a.cpp"},
            {"edits", json::array({ json{
                {"old_text","real old"},
                {"new_text","// see <parameter name=\"old_text\"> docs"}}})},
        };
        // 直接进行修复:它仍应产生一个可使用的编辑,
        // 并带有真顶部 -level old_text 更喜欢。
        repair_param_tag_leak("edit", args);
        bool usable = args.contains("edits") && args["edits"].is_array()
                      && !args["edits"].empty()
                      && args["edits"][0].value("old_text","") == "real old";
        expect("case5: genuine old_text preferred over in-content marker",
              usable, args.dump());
    }

    // 用例 6：多处编辑调用以 XML 形式泄漏，模型将其埋在
    //    双 old_text/new_text 组合。
    //    所有它们必须按顺序恢复,而不仅仅是第一个(留下大复数
    //    的bug) -hunk 编辑默默的截断).
    {
        json args = {
            {"edit",
             "<parameter name=\"path\">grid.el"
             "<parameter name=\"old_text\">AAA"
             "<parameter name=\"new_text\">aaa"
             "<parameter name=\"old_text\">BBB"
             "<parameter name=\"new_text\">bbb"},
        };
        bool changed = repair_param_tag_leak("edit", args);
        expect("case6: multi-edit leak repaired", changed);
        const auto& e = args["edits"];
        expect("case6: both edits recovered",
              e.is_array() && e.size() == 2, args.dump());
        expect("case6: order + pairing preserved",
              e.size() == 2
              && e[0].value("old_text","") == "AAA"
              && e[0].value("new_text","") == "aaa"
              && e[1].value("old_text","") == "BBB"
              && e[1].value("new_text","") == "bbb",
              args.dump());
        expect("case6: path recovered", args.value("path","") == "grid.el");
    }

    // 用例 7：泄漏的 display_description 不得原样保留。
    //    其标记完好无损——被丢弃,没有被带入重建的舰
    //    ⁇ (在那里会混淆工具卡/re-leak).
    {
        json args = {
            {"display_description",
             "do it\n<parameter name=\"path\">x.txt"
             "<parameter name=\"content\">hi\n"},
        };
        bool changed = repair_param_tag_leak("write", args);
        expect("case7: write leak repaired", changed);
        expect("case7: leaked description dropped",
              !args.contains("display_description"), args.dump());
        expect("case7: content recovered", args.value("content","") == "hi\n");
        expect("case7: path recovered", args.value("path","") == "x.txt");
    }
}
