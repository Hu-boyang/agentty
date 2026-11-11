// panel_width_probe - 宽度 终端 买更宽的内容?
//
// 报告于75:很长一段时间 URL 窗体字段中 渲染
// 断开时 面板 右侧有明显的空间,一行下的一个长长的错误
// 也一样。
//
// 两者都是两个面孔的错:宽度预算来自Panel::
// Config::min_width而不是终端。 min_width
// 是一个FLOOR——这个面板经过它,以填满它的容器——所以从它
// 计算出来的每一个预算都按底价冻结,并且对60-column 面板和
// 200-column 1的相同数字作出答复。 draw_budget
// ()已经了解这一点(它的评论如此说);edit_budget
// ()没有。
//
// 测量结束 -to-end 真实画布上: 渲染
// 编号 面板 以若干宽度计算值中有多少字符幸存。
// 预算追踪 终端 增加这个数;
// 冻结的数不会。

#include "agtest.hpp"

#include <maya/print.hpp>
#include <maya/widget/panel.hpp>

#include <cstdlib>
#include <string>

namespace {

// 面板读取 终端 宽度通过 COLUMNS 当没有Tty的时候,
// 每个试验都是这样。 渲染 并放回去,一个漏掉的
// COLUMNS 改变后来的每一次面板试验措施 也就是这个
// 探测器如何首先打破它的两个邻居
struct AtWidth {
    std::string saved;
    bool had = false;
    explicit AtWidth(int w) {
        if (const char* c = std::getenv("COLUMNS")) { saved = c; had = true; }
        setenv("COLUMNS", std::to_string(w).c_str(), /*覆盖=*/1);
    }
    ~AtWidth() {
        if (had) setenv("COLUMNS", saved.c_str(), /*覆盖=*/1);
        else     unsetenv("COLUMNS");
    }
};

// A URL 足够长的宽度可以超过任何宽度, 并且没有重复运行——所以
// "how much of it is on screen" 可以从最后
// 一位幸存的数中读出,而不是推断出来。
const std::string kUrl =
    "https://gateway.example.com/v1/openai/compat/deployments/"
    "gpt-4o-mini-2024-07-18/chat/completions?api-version=2024-08-01";

// 任意行中最长的 URL 显示,以字符表示。
std::size_t url_chars_visible(int w, bool editing) {
    const AtWidth at{w};

    maya::Panel::Config cfg;
    cfg.title     = " Plugin ";
    cfg.min_width = 60;          // 普通楼层的小组标准

    maya::Panel::Item row;
    row.leading = "URL";
    row.control = maya::panel::Text{
        kUrl, editing ? kUrl.size() : std::string::npos, {}};
    cfg.items.push_back(std::move(row));

    // render_to_string 铺设 源码树 出局时间
    // `w` ,和 COLUMNS 告诉显示的面板 终端 ——两人
    // 必须同意,否则预算的宽度与被画的那条大相径庭.
    const std::string out =
        maya::render_to_string(maya::Panel{std::move(cfg)}.build(), w);

    // 最长的长度 URL-ish 任意行中的字符。
    // 光标 窗口会绕着它的切片 来寻找整个 URL 永远不匹配,
    // 重要的是如何 WIDE 幸存的片段是。
    std::size_t best = 0;
    std::size_t run  = 0;
    for (const char c : out) {
        const bool urlish = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9')
                         || c == '/' || c == '.' || c == ':'
                         || c == '-' || c == '?' || c == '=';
        if (urlish) { if (++run > best) best = run; }
        else run = 0;
    }
    return best;
}

}  // 命名空间结束。

TEST_CASE("panel: a wider terminal shows more of a long value") {
    // 电话 面板 地板是60,80栏已经松懈;
    // 200栏是地板的三倍。每一步都应该显示更多。
    const std::size_t at80  = url_chars_visible(80,  /*编辑=*/true);
    const std::size_t at120 = url_chars_visible(120, /*编辑=*/true);
    const std::size_t at200 = url_chars_visible(200, /*编辑=*/true);

    INFO("visible url chars: 80=", at80, " 120=", at120, " 200=", at200);

    // 不是确切的计数——窗口的卷轴标记和
    // 标签道不尽相同。要求是单通增长:宽度
    // 为面板必须达到值。
    CHECK(at120 > at80);
    CHECK(at200 > at120);

    // 增长必须是真实的,不是一柱或两柱的四舍五入。
    // -character URL 屏幕上应该完全显示。
    CHECK(at200 >= 110);
}

TEST_CASE("panel: an idle long value is not windowed to the floor either") {
    const std::size_t at80  = url_chars_visible(80,  /*编辑=*/false);
    const std::size_t at200 = url_chars_visible(200, /*编辑=*/false);
    INFO("idle url chars: 80=", at80, " 200=", at200);
    CHECK(at200 > at80);
}

TEST_CASE("panel: a long row error wraps instead of losing its tail") {
    // 第二次截图在75号: 行下的错误被切中
    // -word 带有椭圆,而下面的行为空白。
    // 错误是其中的一个字符串。 面板 ——这是唯一告诉
    // 用户该如何修复的东西。
    const std::string err =
        "connect failed: no route to host 10.42.0.17:11434 "
        "(check the endpoint, or whether the server is listening)";

    const AtWidth at{100};

    maya::Panel::Config cfg;
    cfg.title     = " Plugin ";
    cfg.min_width = 60;
    maya::Panel::Item row;
    row.leading = "Endpoint";
    row.control = maya::panel::Text{"10.42.0.17:11434", std::string::npos, {}};
    row.error   = err;
    cfg.items.push_back(std::move(row));

    const std::string out =
        maya::render_to_string(maya::Panel{std::move(cfg)}.build(), 100);

    // 电话 末尾 告诉你该怎么做的
    CHECK(out.find("server is listening)") != std::string::npos);
}

TEST_CASE("panel: a pane-wide error wraps where a subtitle would truncate") {
    // Config::error 存在, 因为主机正在设置
    // 失败 `subtitle` ,它会切换——所以命名固定点的留言
    // 中的一半从未到达.
    const AtWidth at{70};

    maya::Panel::Config cfg;
    cfg.title     = " Plugin ";
    cfg.min_width = 60;
    cfg.subtitle  = "not connected";
    cfg.error     = "spawn failed: no such file or directory "
                    "(is mcp-server-git on PATH?)";
    cfg.items.push_back(maya::Panel::Item{});

    const std::string out =
        maya::render_to_string(maya::Panel{std::move(cfg)}.build(), 70);

    CHECK(out.find("not connected") != std::string::npos);
    CHECK(out.find("on PATH?)") != std::string::npos);
}
