// tool_gallery – 每张工具卡,通过 真实 视图,
// 从 真实 记录工具 调用.
//
// 内容 此 IS  ⁇
// ============
// 固定的,不是测试,它重现了一套实际工具 调用 已取消记录 线程 ,构建一个真实
// 的 Model 个案,和 渲染 每一个通过 conversation_config
// -> maya::Conversation。它打印的是用户所看到的,
// 以颜色表示,以真正的宽度表示。
//
// 使用测试语料而不逐个手写用例的原因。
// =======================================
// 该文件的第一个版本发明了它的固定装置,发明的固定装置是: POLITE:
// a 6-line 读, a 2 -hit grep, 干净的 diff。
// 真正的工具输出没有。 在60 个记录中 线程 实际形状为:
//
//     bash      20077 次调用   191 次失败   输出最多 30 KB
//     edit      10045 次调用   408 次失败   输出最多 23 KB
//     read       7775 次调用   144 次失败   输出最多 80 KB
//     grep       3035 次调用    40 次失败   输出最多 29 KB
//
// 6号的牌设计不错 -line 读到80块就散了 KB 一种不是设计,
// 所以每个工具, 都保留着 TYPICAL 调用 (按输出大小的中位数 -
// - -- 用户整天盯着看),a LARGE 百分比(第93百分位)和百分比
// (第91%) FAILURE 工具有东西的地方
//
// 程序书被洗刷:家用路径、电子邮件和无关的项目名称被改写,
// 因为它来自真实的会话。
//
// 运行 : ./build/tool_gallery 每一个案例
//       ./build/tool_gallery read grep       # 工具
//       ./build/tool_gallery --kind failed   # only the failures
//       ./build/tool_gallery --running bash  # as in-flight cards
//       ./build/tool_gallery --ask bash      # with the permission prompt
//       ./build/tool_gallery --chrome        # 状态栏 +
//       ./build/tool_gallery --list          # what is in the corpus
//       COLS=120 ./build/tool_gallery        # at another width

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/thread/conversation.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/thread/welcome_screen.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/runtime/app/update/internal.hpp"   // collect_viewer_entries
#include "agentty/runtime/panel/tool_output.hpp"
#include "agentty/runtime/view/diff_review.hpp"
#include "agentty/diff/diff.hpp"
#include "agentty/runtime/fused_models.hpp"
#include "agentty/domain/ui_theme.hpp"

#include <format>
#include <maya/runtime.hpp>
#include <maya/core/anim_clock.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace agentty;
using json = nlohmann::json;

// --running: 渲染 每张牌都是一张 -flight 工具(闪电, 加载动画).
static bool g_running = false;
// --ask: 渲染 每个案件都在等待一个 权限 提示词.
static bool g_ask = false;

namespace {

struct Case {
    std::string tool;     // "read", "bash", …
    std::string kind;     // "typical" | "large" | "failed"
    json        args;
    std::string output;
    bool        failed = false;
};

std::vector<Case> load_corpus(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr,
            "tool_gallery: cannot open %s\n"
            "  The corpus ships at tests/fixtures/tool_corpus.json; pass a\n"
            "  different one as $TOOL_CORPUS.\n", path.c_str());
        return {};
    }
    json j;
    try { f >> j; } catch (const std::exception& e) {
        std::fprintf(stderr, "tool_gallery: %s is not valid JSON: %s\n",
                     path.c_str(), e.what());
        return {};
    }
    std::vector<Case> out;
    for (const auto& r : j) {
        Case c;
        c.tool   = r.value("tool", "?");
        c.kind   = r.value("kind", "typical");
        c.args   = r.contains("args") ? r["args"] : json::object();
        c.output = r.value("output", "");
        c.failed = r.value("status", "") == "failed";
        out.push_back(std::move(c));
    }
    return out;
}

std::chrono::steady_clock::time_point now_tp() {
    return std::chrono::steady_clock::now();
}

ToolUse make_tool(const Case& c) {
    ToolUse t;
    t.id   = ToolCallId{"call_" + c.tool + "_" + c.kind};
    t.name = ToolName{c.tool};
    t.args = c.args;
    const auto started  = now_tp();
    const auto finished = started + std::chrono::milliseconds{
        // 可用时间:读取速度快,任务不长。只影响页脚的时间 标签 ,
        // 但2米长 `task` 会被看成是假的。
        c.tool == "task" ? 42000 : c.tool == "test" ? 9100 :
        c.tool == "bash" ? 1400  : 180};
    if (g_running) {
        ToolUse::Running r;
        r.started_at = started;
        // 记录的输出代表了现场进展,所以运行卡有一个 正文 显示(
        // 即 shash 和任务 中点) -run).
        r.progress_text = c.output;
        t.status = std::move(r);
    }
    else if (c.failed) t.status = ToolUse::Failed{started, finished, c.output};
    else               t.status = ToolUse::Done{started, finished, c.output};
    return t;
}

// 序列化 将彩绘变为真实 ANSI.
//
// 工具卡的信息存在于其中 COLOUR - 状态glyph的呼声,
// dim 铬, 一个失败的红色, 语法跨度。 早期版本的固定
// 值折叠了每个 存储单元 以一面赤色的圆柱,并用一面壁画出
// 一面单色的墙,而它却认为墙上有颜色,而墙上藏着被判决的
// 真伪。
//
// 每一个 存储单元 将样式 ID 带入 池; Style::to_sgr
// ()解决它与现场主题。 SGR 只在 id 时才释放 CHANGES ,
// 因此输出形状相同 maya 自己的作家制作的
void emit_canvas(const maya::Canvas& canvas, const maya::StylePool& pool,
                 int cols) {
    std::string out;
    for (int y = 0; y <= canvas.max_content_row(); ++y) {
        int last = -1;
        for (int x = 0; x < cols; ++x) {
            const auto c = canvas.get(x, y);
            if (c.character != 0 && c.character != U' ') last = x;
        }
        std::uint16_t cur = 0xFFFF;
        for (int x = 0; x <= last; ++x) {
            const auto cell = canvas.get(x, y);
            if (cell.width == 2) continue;          // 宽的Char的下半身
            if (cell.style_id != cur) {
                cur = cell.style_id;
                out += "\x1b[0m";
                const auto sgr = pool.get(cur).to_sgr();
                if (!sgr.empty()) out += sgr;
            }
            const char32_t ch = cell.character ? cell.character : U' ';
            if (ch < 0x80) out += static_cast<char>(ch);
            else if (ch < 0x800) {
                out += static_cast<char>(0xC0 | (ch >> 6));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                out += static_cast<char>(0xE0 | (ch >> 12));
                out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (ch >> 18));
                out += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        out += "\x1b[0m\n";
    }
    std::fputs(out.c_str(), stdout);
}

// 一条短线命名代理商正在做的事情,所以卡片的上下文会给出真实的转折。
// 它是从参数中衍生出来的而不是发明出来的。
std::string prompt_for(const Case& c) {
    const auto s = [&](const char* k) -> std::string {
        if (c.args.is_object()) {
            auto it = c.args.find(k);
            if (it != c.args.end() && it->is_string()) return it->get<std::string>();
        }
        return {};
    };
    if (auto d = s("display_description"); !d.empty()) return d;
    if (auto p = s("command"); !p.empty())             return "run: " + p;
    if (auto p = s("pattern"); !p.empty())             return "find " + p;
    if (auto p = s("path"); !p.empty())                return "look at " + p;
    if (auto p = s("file_path"); !p.empty())           return "look at " + p;
    return c.tool + " (" + c.kind + ")";
}

void render_case(const Case& c, int cols) {
    Model m;
    Message user;
    user.role = Role::User;
    user.text = prompt_for(c);
    m.d.current.messages.push_back(std::move(user));

    Message asst;
    asst.role = Role::Assistant;
    ToolUse tool = make_tool(c);
    if (g_ask) {
        tool.status = ToolUse::Pending{std::chrono::steady_clock::now()};
        m.d.pending_permission = PendingPermission{tool.id, tool.name,
                                                   "needs permission", 0};
    }
    asst.tool_calls.push_back(std::move(tool));
    m.d.current.messages.push_back(std::move(asst));

    auto root = maya::Conversation{ui::conversation_config(m)}.build();
    maya::StylePool pool;
    // 足够高的,那么大 正文 永远不会被固定的本身剪断;
    // 卡片本身的预算是应该绑起来的,如果它不绑住,
    // 那就是发现.
    maya::Canvas canvas(cols, 400, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::live(), true);

    const std::string tag = c.tool + " \xc2\xb7 " + c.kind
                          + " \xc2\xb7 " + std::to_string(c.output.size()) + "b";
    std::printf("\n\x1b[2m\xe2\x94\x80\xe2\x94\x80 %s ", tag.c_str());
    for (int i = 0; i < cols - 6 - static_cast<int>(tag.size()); ++i)
        std::printf("\xe2\x94\x80");
    std::printf("\x1b[0m\n");

    emit_canvas(canvas, pool, cols);
}

void emit_element(const std::string& tag, const maya::Element& root, int cols) {
    maya::StylePool pool;
    maya::Canvas canvas(cols, 120, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::live(), true);
    std::printf("\n\x1b[2m\xe2\x94\x80\xe2\x94\x80 %s ", tag.c_str());
    for (int i = 0; i < cols - 6 - static_cast<int>(tag.size()); ++i)
        std::printf("\xe2\x94\x80");
    std::printf("\x1b[0m\n");
    emit_canvas(canvas, pool, cols);
}

// 对话周围的屏幕铬:状态栏和作曲,
// 在用户实际看到的状态.
// 一个真正的推理障碍(取自记录的线程),定居和生活,
// 随之而来的答案.
void render_reasoning(int cols) {
    static const char* kThought =
        "Since `truncate_middle` already exists, I'm planning extension-preserving "
        "truncation for FileTree filenames \xe2\x80\x94 keeping the stem shortened while "
        "preserving the extension by default, falling back to middle truncation "
        "when there's no extension or it's too long, and letting the active row "
        "reveal the full filename.\n\n"
        "I'm sketching out the config: a `FileTreeTrunc` enum (KeepExtension, "
        "Middle, End), plus fluent setters `truncate(mode)` and `reveal_active(bool)` "
        "on the FileTree class. Writing truncation dispatch logic...\n\n"
        "For the KeepExtension case, I need to preserve the extension while "
        "shortening the stem with an ellipsis, falling back to middle-truncation if "
        "the extension itself is too wide.";
    for (int live = 0; live < 2; ++live) {
        Model m;
        Message user;
        user.role = Role::User;
        user.text = "make long filenames in the file tree readable";
        m.d.current.messages.push_back(std::move(user));
        Message asst;
        asst.role = Role::Assistant;
        asst.thinking = kThought;
        asst.reasoning_ms = 6400;
        if (live) {
            // 6点4分开始思考 和那个时钟一样 风景是读的
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                maya::anim_now().time_since_epoch()).count();
            asst.reasoning_ms = 0;
            asst.reasoning_started_ms = now_ms - 6400;
            m.s.phase = phase::Streaming{};
        } else {
            asst.text = "Added `FileTreeTrunc` with three modes. KeepExtension is the "
                        "default, so `really_long_component_name.cpp` shows as "
                        "`really_lo\xe2\x80\xa6name.cpp`, and the selected row shows the whole name.";
        }
        m.d.current.messages.push_back(std::move(asst));
        auto root = maya::Conversation{ui::conversation_config(m)}.build();
        emit_element(live ? "reasoning \xc2\xb7 live" : "reasoning \xc2\xb7 settled",
                     root, cols);
    }
}

void render_chrome(int cols) {
    struct State { const char* tag; int tokens; bool streaming; const char* text; };
    const State states[] = {
        {"idle, empty composer",           12000,  false, ""},
        {"streaming, 42% context",         84000,  true,  ""},
        {"typing, mentions and a command", 150000, false,
         "/review @src/runtime/view/composer.cpp check the footer"},
        {"near the limit",                 188000, false, "summarise and compact"},
    };
    for (const auto& s : states) {
        Model m;
        m.s.tokens_in   = s.tokens;
        m.s.context_max = 200000;
        if (s.streaming) m.s.phase = phase::Streaming{};
        m.ui.composer.text   = s.text;
        m.ui.composer.cursor = static_cast<int>(std::string_view{s.text}.size());
        auto bar  = maya::StatusBar{ui::status_bar_config(m)}.build();
        auto comp = maya::Composer{ui::composer_config(m)}.build();
        emit_element(std::string{s.tag} + " \xc2\xb7 status", bar, cols);
        emit_element(std::string{s.tag} + " \xc2\xb7 composer", comp, cols);
    }
}

// 由每一份文稿第渲染器号手柄组成的一个记分式-heavy助理答复
// (从记录的第线程号手稿中提出)。
void render_prose(int cols) {
    static const char* kAnswer =
        "## Emphasis mix\n\n"
        "You can combine `code` with **bold**, or put a [link with **bold** text](https://example.com) inline.\n\n"
        "### Ordered + nested\n\n"
        "1. Setup\n   1. Install deps\n   2. Configure env\n2. Build\n3. Ship\n\n"
        "| Command | Description                        | Shorthand |\n"
        "|---------|------------------------------------|:---------:|\n"
        "| `status`| Show working tree state            | `st`      |\n"
        "| `commit`| Record changes to the repository   | `ci`      |\n\n"
        "```bash\nset -euo pipefail\nfor f in *.txt; do\n  echo \"processing $f\"\ndone\n```\n\n"
        "> Level 1\n> > Level 2\n\n"
        "- [x] Write parser\n- [ ] Ship release\n\n"
        "---\n\nWant me to focus on any one element?";
    Model m;
    Message user;
    user.role = Role::User;
    user.text = "show me every markdown element";
    m.d.current.messages.push_back(std::move(user));
    Message asst;
    asst.role = Role::Assistant;
    asst.text = kAnswer;
    m.d.current.messages.push_back(std::move(asst));
    auto root = maya::Conversation{ui::conversation_config(m)}.build();
    emit_element("prose", root, cols);
}

// 第一屏:新鲜的Model,没有对话.
void render_welcome(int cols) {
    Model m;
    auto root = maya::WelcomeScreen{ui::welcome_screen_config(m)}.build();
    emit_element("welcome", root, cols);
}

// 电话 覆盖面板 用户打开最多 : ^K 调色板,模型 选择面板, ^J 线程.
void render_panels(int cols) {
    namespace pn = agentty::ui::panel;
    {
        Model m;
        m.ui.panel.descend(pn::Palette{{}});
        emit_element("palette", ui::palette_panel(m), cols);
    }
    {
        auto mk = [](std::string id, std::string name, std::string prov) {
            ModelInfo mi;
            mi.id = ModelId{std::move(id)};
            mi.display_name = std::move(name);
            mi.provider = std::move(prov);
            mi.context_window = 200000;
            return mi;
        };
        auto cat = [](std::string id, std::string label, std::vector<ModelInfo> ms) {
            ProviderCatalog c;
            c.provider_id = std::move(id);
            c.label = std::move(label);
            c.state = ProviderCatalog::State::Ready;
            c.models = std::move(ms);
            return c;
        };
        const std::vector<ProviderCatalog> cats{
            cat("anthropic", "Anthropic",
                {mk("claude-opus-4-5", "Claude Opus 4.5", "anthropic"),
                 mk("claude-sonnet-4-5", "Claude Sonnet 4.5", "anthropic"),
                 mk("claude-haiku-4-5", "Claude Haiku 4.5", "anthropic")}),
            cat("openai", "OpenAI",
                {mk("gpt-5", "GPT-5", "openai"), mk("gpt-5-mini", "GPT-5 mini", "openai")}),
        };
        const std::vector<ModelRef> recents{{"anthropic", "claude-opus-4-5"},
                                            {"openai", "gpt-5"}};
        Model m;
        ui::FusedInputs in;
        in.catalogs = &cats;
        in.recents  = &recents;
        in.active   = ModelRef{"anthropic", "claude-opus-4-5"};
        m.d.fused_rows = ui::build_fused_rows(in);
        m.ui.panel.descend(pn::Models{});
        emit_element("model picker", ui::models_panel(m), cols);
    }
    {
        Model m;
        const char* titles[] = {"tool cards: chips and syntax colour",
                                "fix MSYS2 launch under mintty",
                                "provider switching ux overhaul",
                                "reasoning: neutral ink and beats"};
        int i = 0;
        for (const char* t : titles) {
            Thread th;
            th.id = ThreadId{"t-" + std::to_string(i)};
            th.title = t;
            th.message_count = 12 + 7 * i++;
            m.d.threads.push_back(std::move(th));
        }
        m.ui.panel.descend(pn::ThreadList{});
        emit_element("thread list", ui::thread_list_panel(m), cols);
    }
    {
        Model m;
        // 在一个文件中有两个单独的更改, 第一个已保存
        std::string before, after;
        for (int i = 1; i <= 30; ++i) {
            before += std::format("int f{}() {{ return {}; }}\n", i, i);
            after  += i == 4  ? std::string{"int f4() { return -1; }\nvoid log();\n"}
                    : i == 22 ? std::string{""}
                              : std::format("int f{}() {{ return {}; }}\n", i, i);
        }
        auto a = diff::compute("src/auth/login.cpp", before, after);
        if (!a.hunks.empty()) a.hunks[0].status = Hunk::Status::Accepted;
        m.d.pending_changes.push_back(a);
        m.d.pending_changes.push_back(diff::compute("README.md", "old\n", "new\nmore\n"));
        m.d.pending_changes.push_back(diff::compute("src/net/http_client.cpp", "a\n", "b\n"));
        m.ui.panel.descend(pn::DiffReview{{0, 1}});
        emit_element("diff review", ui::diff_review(m), cols);
    }
    {
        Model m;
        m.ui.panel.descend(pn::Providers{});
        emit_element("providers", ui::providers_panel(m), cols);
    }
    {
        Model m;
        m.ui.panel.descend(pn::SettingsList{});
        emit_element("settings", ui::settings_list_panel(m), cols);
    }
}

}  // 命名空间结束。

// 电话 ^O 工具 -output 面板 : 列表,然后各列表 正文 真实的体格
// VIEWER_ROWS 设置 终端 高度( E) 面板 设定( 默认 40 )。
static void render_viewer(int cols, const std::vector<std::string>& want,
                          const std::string& kind_filter) {
    namespace pn = agentty::ui::panel;
    const auto cases = load_corpus("tests/fixtures/tool_corpus.json");
    Model m;
    Message user;
    user.role = Role::User;
    user.text = "look around";
    m.d.current.messages.push_back(std::move(user));
    Message asst;
    asst.role = Role::Assistant;
    for (const auto& c : cases) {
        if (!kind_filter.empty() && c.kind != kind_filter) continue;
        bool hit = want.empty();
        for (const auto& w : want) if (c.tool.find(w) != std::string::npos) hit = true;
        if (hit) asst.tool_calls.push_back(make_tool(c));
    }
    m.d.current.messages.push_back(std::move(asst));
    const int rows = [] {
        const char* r = std::getenv("VIEWER_ROWS");
        return r ? std::max(10, std::atoi(r)) : 40;
    }();
    setenv("LINES", std::to_string(rows).c_str(), 1);
    setenv("COLUMNS", std::to_string(cols).c_str(), 1);
    auto entries = app::detail::collect_viewer_entries(m);
    if (entries.empty()) { std::puts("no entries"); return; }
    const int n = static_cast<int>(entries.size());
    m.ui.panel.descend(pn::ToolOutput{{std::move(entries), 0, false}});
    emit_element("tool output \xc2\xb7 list", ui::tool_output_panel(m), cols);
    auto* o = m.ui.panel.get<pn::ToolOutput>();
    o->viewing = true;
    if (const char* q = std::getenv("VIEWER_FIND")) { o->query = q; ++o->hit_seq; }
    if (std::getenv("VIEWER_FULL")) {
        // 真实屏幕: 基数 + 覆盖面板 ,为一列小马铺设,以图X行 终端.
        // 血清转弯,所以基部只是铬。
        m.d.current.messages.clear();
        m.ui.term_cols = cols;
        m.ui.term_rows = rows;
        {
            Model bare;
            bare.ui.term_cols = cols;
            bare.ui.term_rows = rows;
            std::printf("== base alone %dx%d: %d rows\n", cols, rows,
                        maya::measure_element(ui::view(bare), cols).height.value);
            bare.ui.panel.descend(pn::Palette{});
            std::printf("== palette open %dx%d: %d rows\n", cols, rows,
                        maya::measure_element(ui::view(bare), cols).height.value);
        }
        for (int i = 0; i < std::min(n, 3); ++i) {
            o->index = i;
            m.ui.tool_viewer_scroll.y = 0;
            auto root = ui::view(m);
            const auto sz = maya::measure_element(root, cols);
            std::printf("\n== full screen %dx%d, entry %d: frame is %d rows%s\n", cols, rows, i + 1,
                        sz.height.value, sz.height.value > rows ? "  OVERFLOW" : "");
            emit_element("screen", root, cols);
        }
        return;
    }
    const int bodies = std::getenv("VIEWER_ALL") ? n : std::min(n, 4);
    for (int i = 0; i < bodies; ++i) {
        o->index = i;
        m.ui.tool_viewer_scroll.y = 0;
        emit_element("tool output \xc2\xb7 body " + std::to_string(i + 1),
                     ui::tool_output_panel(m), cols);
    }
}

int main(int argc, char** argv) {
    // 观点是: loop_bound<T> 缓存 (工具 -body
    // 并解决 -panel 备忘在 agent_timeline.cpp ),
    // 这仅仅是内核的循环 线程 固定没有内核,所以借一下身份
    // jaal 暴露了它的存在: 它携带着 此 线程 范围。
    const auto loop = maya::arm_loop_for_tests();
    // FREEZE_CLOCK =1: 将动画钟按此 加载动画 闪烁 渲染
    // 同样的 帧 每一跑步( 以 UI 快照 测试设置它。
    if (const char* f = std::getenv("FREEZE_CLOCK"); f && *f == '1')
        maya::testing::freeze_anim_clock(1000);

    // 将主题安装为 LIVE 一张,不只是传给 render_tree.
    // Style::to_sgr ()解决一个槽通过 theme::live (;
    // 无设置 存储单元 空着出来 SGR 正确的行为,
    // 和单色的墙在这里。
    // THEME=<scheme name> (或 减 减 ) "native" );默认为Srcery, Ayush 运行的.
    const char* theme_name = std::getenv("THEME");
    if (!theme_name) theme_name = "Srcery";
    // TERM_BG / TERM_FG=RRGGBB 站在什么真实的
    // 终端 解答启动颜色探测器, 这样可以不带一个就检查原生。
    auto hex = [](const char* s, maya::LitColor& out) {
        if (!s || std::strlen(s) != 6) return false;
        const unsigned v = static_cast<unsigned>(std::strtoul(s, nullptr, 16));
        out = maya::LitColor::rgb(static_cast<uint8_t>(v >> 16),
                                  static_cast<uint8_t>(v >> 8),
                                  static_cast<uint8_t>(v));
        return true;
    };
    if (maya::LitColor fg = maya::LitColor::rgb(0, 0, 0), bg = fg;
        hex(std::getenv("TERM_FG"), fg) && hex(std::getenv("TERM_BG"), bg))
        maya::theme::set_terminal_colors(fg, bg);
    // TERM_PALETTE="RRGGBB,RRGGBB,..." (16个条目): OSC 4个答案。
    if (const char* pal = std::getenv("TERM_PALETTE")) {
        std::string s{pal};
        int k = 0;
        for (std::size_t p = 0; p <= s.size() && k < 16;) {
            const auto e = s.find(',', p);
            const std::string one = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
            if (maya::LitColor c = maya::LitColor::rgb(0, 0, 0); hex(one.c_str(), c))
                maya::theme::set_terminal_palette(k, c);
            ++k;
            if (e == std::string::npos) break;
            p = e + 1;
        }
    }
    const maya::Theme* th = ui_prefs::find_scheme(theme_name);
    ui_prefs::publish_theme(th ? *th : maya::theme::native);

    int cols = 92;
    if (const char* c = std::getenv("COLS")) {
        const int v = std::atoi(c);
        if (v >= 40 && v <= 400) cols = v;
    }
    std::string corpus = "tests/fixtures/tool_corpus.json";
    if (const char* p = std::getenv("TOOL_CORPUS")) corpus = p;

    std::vector<std::string> want;
    std::string kind_filter;
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list_only = true;
        else if (a == "--running") g_running = true;
        else if (a == "--ask") g_ask = true;
        else if (a == "--chrome") { render_chrome(cols); return 0; }
        else if (a == "--reasoning") { render_reasoning(cols); return 0; }
        else if (a == "--prose") { render_prose(cols); return 0; }
        else if (a == "--welcome") { render_welcome(cols); return 0; }
        else if (a == "--panels") { render_panels(cols); return 0; }
        else if (a == "--viewer") {
            std::vector<std::string> w; std::string k;
            for (int j = i + 1; j < argc; ++j) {
                const std::string b = argv[j];
                if (b == "--kind" && j + 1 < argc) k = argv[++j]; else w.push_back(b);
            }
            render_viewer(cols, w, k);
            return 0;
        }
        else if (a == "--kind" && i + 1 < argc) kind_filter = argv[++i];
        else want.push_back(a);
    }

    const auto cases = load_corpus(corpus);
    if (cases.empty()) return 1;

    if (list_only) {
        std::printf("%-18s %-8s %9s\n", "tool", "kind", "bytes");
        for (const auto& c : cases)
            std::printf("%-18s %-8s %9zu\n", c.tool.c_str(), c.kind.c_str(),
                        c.output.size());
        std::printf("\n%zu cases\n", cases.size());
        return 0;
    }

    int shown = 0;
    for (const auto& c : cases) {
        if (!kind_filter.empty() && c.kind != kind_filter) continue;
        if (!want.empty()) {
            bool hit = false;
            for (const auto& w : want)
                if (c.tool.find(w) != std::string::npos) hit = true;
            if (!hit) continue;
        }
        render_case(c, cols);
        ++shown;
    }
    std::printf("\n\x1b[2m%d case%s\x1b[0m\n", shown, shown == 1 ? "" : "s");
    return 0;
}
