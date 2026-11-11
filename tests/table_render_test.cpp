// table_render_test ——核查. maya's
// 流式传输 标记下划井 -formed GFM 表格为表格,
// 且表格格式不正确(在表格之前提交) 头文件 管道,模型的形状)作为段落。
// 这是用户看到的准确失败: 渲染器 属于 spec -correct ,
// 所以修复 生活系统 提示词 这儿不行,这个测试针 渲染
// 器 行为让我们知道哪个形状 渲染 成为网格。
#include <string>

#include "agtest.hpp"

#include <maya/widget/markdown.hpp>

using K = maya::StreamingMarkdown::BlockKind;

static const char* kind_name(K k) {
    switch (k) {
        case K::Paragraph: return "Paragraph";
        case K::Heading:   return "Heading";
        case K::CodeBlock: return "CodeBlock";
        case K::Blockquote:return "Blockquote";
        case K::List:      return "List";
        case K::HRule:     return "HRule";
        case K::Table:     return "Table";
        default:           return "Other";
    }
}

static bool has_table(const std::string& src) {
    maya::StreamingMarkdown md;
    md.set_content(src);
    md.finish();
    bool found = false;
    std::printf("  source:\n");
    // 回声源缩进
    {
        std::string line;
        for (char c : src) {
            if (c == '\n') { std::printf("    | %s\n", line.c_str()); line.clear(); }
            else line += c;
        }
        if (!line.empty()) std::printf("    | %s\n", line.c_str());
    }
    std::printf("  blocks: ");
    for (std::size_t i = 0; i < md.block_count(); ++i) {
        K k = md.block_meta(i).kind;
        std::printf("%s ", kind_name(k));
        if (k == K::Table) found = true;
    }
    std::printf("\n");
    return found;
}

TEST_CASE("table render") {
    
    // 1. 格式正确的表格：表头行以 | 开始，并有空行
    //    之前,必须分类为表。
    std::printf("[well-formed table]\n");
    const std::string good =
        "Source layout (src/):\n"
        "\n"
        "| Dir | Likely role |\n"
        "|-----|-------------|\n"
        "| airgap/ | SSH airgap / SOCKS5 relay |\n"
        "| diff/ | Diff rendering for edits |\n";
    CHECK_MESSAGE(has_table(good), "expected a Table block, got none");

    // 2. 模型产生的畸形表格：说明文字与表头位于同一行，
    //    作为 头文件. GFM 拒绝 ~ 段落( 管道的墙)。
    //    这是用户看到的。 渲染器 说对了
    std::printf("\n[malformed: prose before header pipe]\n");
    const std::string bad =
        "Source layout (src/): | Dir | Likely role | airgap/ | SSH airgap |\n"
        "|---|---|\n"
        "| diff/ | Diff rendering |\n";
    CHECK_MESSAGE(!has_table(bad),
                  "parser accepted a non-conformant table (bug is elsewhere)");
}
