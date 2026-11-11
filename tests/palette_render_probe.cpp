// palette_render_probe — 渲染 命令调色板 和 diff (英语)
// -review 面板 并声明其布局 + 样式 不变量。生活在一个 归并 套房就这样 链接
// 完整 运行时 对象 源码树 财务报告和已审计财务报表 渲染 穿过真实的 maya 路径。
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/view/diff_review.hpp"
#include "agentty/runtime/panel/palette.hpp"
#include "agentty/diff/diff.hpp"
#include "agentty/runtime/panel/common.hpp"
#include <maya/print.hpp>
#include <cstdio>
#include <string>

namespace pn = agentty::ui::panel;

using namespace agentty;

static int g_fail = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_fail; }
}
static bool has(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}
static bool has_mojibake(const std::string& s) { return s.find("\xef\xbf\xbd") != std::string::npos; }
static int count_of(const std::string& h, const std::string& n) {
    int c = 0; for (std::size_t p = 0; (p = h.find(n, p)) != std::string::npos; p += n.size()) ++c;
    return c;
}

static void palette_checks() {
    Model m;
    m.ui.panel.descend(pn::Palette{{}});
    m.d.pending_changes.push_back(FileChange{});
    auto rend = [&]{ return maya::render_to_string(ui::palette_panel(m), 82); };
    std::string out = rend();
    // `--dump` 打印 帧 人类可以同样看待它
    // embed_render_probe 检查仍然运行。
    if (const char* d = std::getenv("PALETTE_DUMP"); d && d[0])
        std::printf("%s\n", out.c_str());
    // 区块很平整 头文件 现在: 小组讨论 渲染 以
    // + 规则为首, 以 右 边。 旧 的 "┌─ THREAD" 括号,
    // 它的脊椎下 每行,它的近是 第二 上面画
    // 的组合系统 脊椎花掉每行的徽章柱 头
    // 文件 上面已经说了。
    check(has(out, "THREAD"), "palette: section header present");
    check(has(out, "\xe2\x94\x80\xe2\x94\x80"), "palette: header rule");
    check(!has(out, "\xe2\x94\x8c\xe2\x94\x80 THREAD"), "palette: no bracket header");
    check(!has_mojibake(out), "palette: no mojibake");
    auto* o = m.ui.panel.get<agentty::ui::panel::Palette>();
    o->query = "rev";
    std::string ansi = maya::render_to_string_ansi(ui::palette_panel(m), 82);
    // 定位审查行,确认其带有鲜明的突出样式。
    auto strip = [](const std::string& s){ std::string r; for (std::size_t i=0;i<s.size();){ if(s[i]=='\x1b'){ i=s.find('m',i); if(i==std::string::npos)break; ++i;} else r+=s[i++]; } return r; };
    std::string hot; std::size_t st=0;
    while (st<=ansi.size()){ std::size_t nl=ansi.find('\n',st); std::string ln=ansi.substr(st,nl-st); if(strip(ln).find("Review changes")!=std::string::npos){hot=ln;break;} if(nl==std::string::npos)break; st=nl+1; }
    check(!hot.empty(), "palette: Review row present (ANSI)");
    check(count_of(hot, "\x1b[") >= 4, "palette: matched chars carry a highlight style");
}

static void diff_review_checks() {
    // 两个文件,第一个是两个 差异块 (所有项目) 待处理).
    Model m;
    auto a = diff::compute("src/login.cpp",
        "int f() {\n  return 0;\n}\n", "int f() {\n  return -1;\n  log();\n}\n");
    a.added = 2; a.removed = 1;
    m.d.pending_changes.push_back(a);
    auto b = diff::compute("README.md", "old\n", "new\nmore\n");
    m.d.pending_changes.push_back(b);
    m.ui.panel.descend(ui::panel::DiffReview{{0, 0}});

    std::string out = maya::render_to_string(ui::diff_review(m), 84);
    check(has(out, "login.cpp") && has(out, "README.md"),
          "diff: file rail shows every file");
    check(has(out, "reviewed"), "diff: overall hunk progress");
    check(has(out, "hunk 1/"), "diff: labelled hunk dividers (not raw @@)");
    check(!has(out, "@@ -"), "diff: raw git @@ headers are gone");
    check(has(out, "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"), "diff: has a rule");
    check(!has_mojibake(out), "diff: no mojibake");

    // 每一个 差异块 已经决定了,有故意的 NO "all reviewed" 横幅 :
    // 编号 面板 关闭当最后一个 待处理 差异块 已经决定了,所以 Esc 永远不要意味
    // 着两个对立的东西(应用上) -at-end 保留 -the-rest 中点 -review。
    // 参见注释 view/diff_review.cpp.
    //
    // 这个探测器用来竖起标志,自动-close号的操作是故意去掉的
    // ——支票在付得起的时空中过期,并坐落于红色.
    // 反之,把ABSENCE压住,这样矛盾就回不来了.
    for (auto& fc : m.d.pending_changes)
        for (auto& hk : fc.hunks) hk.status = Hunk::Status::Accepted;
    std::string done = maya::render_to_string(ui::diff_review(m), 84);
    check(!has(done, "all reviewed"),
          "diff: no all-reviewed banner — the pane auto-closes instead");
    check(has(done, "reviewed"), "diff: progress counter still reads N/N");
    check(has(done, "\xe2\x96\x88"), "diff: progress bar fills (\xe2\x96\x88) when done");
}

int main() {
    palette_checks();
    diff_review_checks();
    if (g_fail == 0) std::puts("palette_render_probe: OK");
    return g_fail ? 1 : 0;
}
