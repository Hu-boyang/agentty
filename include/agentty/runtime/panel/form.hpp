#pragma once
// agentty::form——表单组件层。
//
// 一个 带类型的形成模型+一个 状态更新函数+一个 渲染器，因此agentty
// 中的每个配置表面(嵌入、智能模式、检索阶段、未来面板)是DATA而不是另一只
// 手-written 选择面板。在此之前，每个 面板 re-implemented
// 行导航、单个-line 文本编辑以及自己的
// "am I typing or navigating?" 想法 - 相同 200 行的五个不同副本。
//
// ── 电话 加载-bearing 类型 : FormFocus ─────────────────────────────────────
// 一个表单正好有THREE种交互模式，且任意时刻正好有一个人拥有键盘。
// 拼写为 `bool editing` + `bool dropdown_open`
// 是编码三种状态的两个布尔值，而 不变量 ("never both")
// 仅存在于注释中 - 与stall-watchdog 布尔值和
// ollama_probed/ollama_ready 对的形状相同，
// 两者都带来了错误。所以它是 和类型，VIEW 读取与密钥路由器相同的值：
// 渲染 和接收密钥的内容不能不一致。
//
// ── 选择有两种互动,而不是两种 ───────────────────────────
// pick-one-of-N字段是一个值。无论您是使用 ←/→ 内联
// 循环它还是使用 Enter 打开列表，都是品味和选项计数的问题，
// 而不是不同类型的数据 - 因此 `Choice` 支持 两者 并且没有
// 显示 标志 可以设置错误。简短的枚举感觉就像 切换；长的会得到一个可
// 搜索的浮动列表。相同的字段，相同的状态，无需配置。
//
// ── 秘密为什么是自己的选择 ────────────────────────────────────
// `Text` + `masked` bool 使得泄漏 凭据 成为记住
// 在每个 渲染 站点检查 bool 的问题。作为一个独特的替代方案，
// 屏蔽是 TYPE-LEVEL 事实：display() 没有可以发出
// Secret 明文的分支，因此 渲染器 物理上无法泄漏它，并且密文键位于
// 类型上而不是字段 -name 启发式上。

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace agentty::form {

// ── 外地类型 ──────────────────────────────────────────────────────────

namespace field {

// 布尔值。 Enter/Space/←/→全部翻转即可。
struct Toggle {
    bool on = false;
};

// 选择 SMALL、CLOSED 组中的一个 — 枚举，仅此而已。
//
// ←/→循环到位； Enter 打开一个简短的浮动列表。
// 没有搜索框，也没有过滤，因为当列表需要时，它就不是一个
// 它不再是简单枚举，也不适合下拉框，而应使用完整的
// 选择面板 覆盖面板 (参见下面的 `Pick`)。
// 保持边界清晰可以阻止这个小部件慢慢成长为第二个更糟糕的 选择面板。
//
// 经验法则：如果您无法命名 头文件 注释中的每个选项，
// 请使用 `Pick`。
//
// `labels`是用户看到的； `ids` 是调用者存储的
// 内容 - 保持并行，因此表单永远不会保留显示字符串，
// 并且重新标记的选项不会默默地更改 已保存 值。
struct Choice {
    std::vector<std::string> labels;
    std::vector<std::string> ids;      // 可能为空 ⇒ ids == labels
    std::vector<std::string> hints;    // 可选的 per-option 暗淡简介
    int                      index = 0;

    [[nodiscard]] int count() const noexcept { return static_cast<int>(labels.size()); }
    [[nodiscard]] int normalized(int i) const noexcept {
        const int n = count();
        return n <= 0 ? 0 : ((i % n) + n) % n;
    }
    [[nodiscard]] std::string_view label() const noexcept {
        if (labels.empty()) return {};
        return labels[static_cast<std::size_t>(normalized(index))];
    }
    // 当前选择的STORED值。
    [[nodiscard]] std::string_view id() const noexcept {
        if (labels.empty()) return {};
        const auto i = static_cast<std::size_t>(normalized(index));
        return ids.empty() ? std::string_view{labels[i]} : std::string_view{ids[i]};
    }
    [[nodiscard]] std::string_view hint_at(int i) const noexcept {
        if (hints.empty()) return {};
        const auto k = static_cast<std::size_t>(normalized(i));
        return k < hints.size() ? std::string_view{hints[k]} : std::string_view{};
    }
    // 通过存储的id进行选择；当索引不存在时，单独保留索引，
    // 因此加载由较新的 agentty 编写的配置不会默默地重置该字段。
    void select_id(std::string_view want) noexcept {
        const auto& keys = ids.empty() ? labels : ids;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == want) { index = static_cast<int>(i); return; }
    }
};

// 有界整数，编辑为digit-by-digit。
//
// 当编辑打开时，该行仅保存 带类型的 (`draft`) 的数字，
// 并且当编辑结束时该值被钳位一次。每当 min > 0 时，夹紧每个键都会
// 导致该行无法输入：清除后，该值会捕捉到 min，因此在 1-65535
// 端口上，清除后的每个数字 带类型的 都会附加到该 1 (#97)。
// `value` 始终在范围内；只有`draft`才有意义，而`editing`
// 才有意义。
struct Number {
    std::int64_t value = 0;
    std::int64_t min   = 0;
    std::int64_t max   = 65535;
    std::string  draft;            // 到目前为止的数字 带类型的，可能为空
    bool         editing = false;  // 草稿正在直播
};

// 有界实数，以←/→步进并绘制为条形。与数字不同，因为一次输入一位数字
// "0.65" 是很痛苦的，而且因为需要它的旋钮（mmr_lambda、
// dedup_threshold、dense_weight、
// proactive_min_conf ...）是比率，其值最好通过视觉来判断。
struct Slider {
    double value = 0.0;
    double min   = 0.0;
    double max   = 1.0;
    double step  = 0.05;
    int    decimals = 2;
};

// 免费单-line文本。
struct Text {
    std::string value;
    std::size_t cursor = 0;
};

// 决不能以明文形式呈现、记录或保留的自由文本。
struct Secret {
    std::string value;
    std::size_t cursor = 0;
    // EMPTY字段在未被编辑时表示什么（"not set"为空白时）。
    // 不是秘密，因此可能会显示：面板 使用它来报告密钥已在其他地方
    // ("\xe2\x9c\x93 key saved") 保存，
    // 但值从未出现在表单中。
    std::string placeholder;
};

// 文件系统路径。文本加上存在探测，因此 "model file not found"
// 在键入时可见，而不是稍后显示为失败的探测。
struct Path {
    std::string value;
    std::size_t cursor = 0;
    bool        want_dir = false;
};

// 其值来自ANOTHER 覆盖面板的行。
//
// `Choice`的对应物：当候选集很大、动态或者需要搜索（模型、
// 文件、线程)时，Enter不关闭任何内容并移交给真正
// 的选择面板——这正是当您设置角色的模型时智能模式已经
// 做的事情。表单仅保存CURRENT显示标签；
// 面板 拥有手-off 并将结果写回。
//
// 这是保持 `Choice` 诚实的逃生舱口：永远
// 没有理由将下拉列表增长为可搜索列表，因为这种情况有其
// 自己的行类型。
struct Pick {
    std::string label;         // 当前选择，用于显示
    std::string placeholder = "\xe2\x80\x94";
};

// SECTION HEADER的行，不是设置。
// 分组是一个 渲染 问题，否则表单无法表达，并且用锁定的
// 文本行伪造它会将杂散的 "—" 占位符泄漏到值列中。
struct Header {};

// DOES某物而不是保存值的行（e.g.
// "Test connection")。一种真正的替代方案，
// 而不是永远不会呈现为一个的 切换：状态更新函数 在类型上分派，`status`
// 携带结果，因此结果 渲染 位于操作所在的位置。
struct Action {
    std::string status;        // 呈现为right-aligned;空⇒提示
    std::string hint = "press Enter";
    enum class Tone : std::uint8_t { Neutral, Busy, Good, Bad };
    Tone tone = Tone::Neutral;
};

} // namespace field

using FieldValue = std::variant<field::Toggle, field::Choice, field::Number,
                                field::Slider, field::Text, field::Secret,
                                field::Path, field::Pick, field::Header,
                                field::Action>;

// 下拉列表在滚动之前显示的行。 枚举 列表应该很适合这个；
// 该窗口的存在是为了让病态的短 终端 优雅地降级，
// 而不是作为浏览功能。
inline constexpr int kDropdownViewport = 8;

// ── 排行数 ────────────────────────────────────────────────────────────────

struct Field {
    std::string id;        // 稳定键状态更新函数+主叫方地址
    std::string label;
    std::string help;      // one-line描述，暗淡
    FieldValue  value;

    // 以暗色调呈现值的右侧——用于来源（
    // "env: AGENTTY_RAG_MMR_LAMBDA"，"default")，
    // 因此用户可以看到值来自WHERE，这是关于分层配置的最令人困惑的事情。
    std::string origin;

    // 锁定的行可见但不可编辑，并说明原因。
    // 覆盖 settings.json 的环境变量不得默默吞下编辑。
    bool        locked = false;
    std::string locked_reason;

    // Per-row有效性，在表格下内联呈现。空⇒有效。
    std::string error;

    [[nodiscard]] bool is_text_like() const noexcept {
        return std::holds_alternative<field::Text>(value)
            || std::holds_alternative<field::Secret>(value)
            || std::holds_alternative<field::Path>(value)
            || std::holds_alternative<field::Number>(value);
    }
    [[nodiscard]] bool is_choice() const noexcept {
        return std::holds_alternative<field::Choice>(value);
    }
    [[nodiscard]] bool is_pick() const noexcept {
        return std::holds_alternative<field::Pick>(value);
    }
    [[nodiscard]] bool is_action() const noexcept {
        return std::holds_alternative<field::Action>(value);
    }
    [[nodiscard]] bool is_header() const noexcept {
        return std::holds_alternative<field::Header>(value);
    }
    // Pick 移交给另一个 覆盖面板，Action 运行某些东西，
    // 而 头文件 根本不是设置；这三个都没有被就地编辑。
    [[nodiscard]] bool editable() const noexcept {
        return !locked && !is_action() && !is_pick() && !is_header();
    }
};

// ── 焦点:三种互动模式 ───────────────────────────────────

namespace focus {

// 光标在行之间移动； ←/→ 调整到位。
struct Browsing {};

// 文本-like行拥有键盘：可打印键EDIT而不是导航。
struct Editing {};

// 浮动选项列表在表单上打开。它拥有键盘和高亮；
// 在用户提交之前底层字段保持不变，因此 Esc 是
// 真正的 取消。
//
// 这里没有查询：下拉列表仅包含 枚举，而您需要搜索的
// 枚举 是属于真正的 选择面板 的 `Pick` 行。
struct Choosing {
    int highlighted = 0;
    int scroll      = 0;
};

} // namespace focus

using FormFocus = std::variant<focus::Browsing, focus::Editing, focus::Choosing>;

// ── 表格 ─────────────────────────────────────────────────────────────

struct Form {
    std::string        title;
    std::string        subtitle;      // e.g。有效配置摘要
    // 整个 面板 都处于失败状态。投影到 Panel::Config::error 上，
    // 将其包裹起来 — 与截断的字幕不同。将错误放入
    // `subtitle` 的 面板 丢失了表示要修复的内容的 末尾。
    std::string        error;
    std::vector<Field> fields;
    int                cursor = 0;
    FormFocus          focus{focus::Browsing{}};

    // 自上次提交以来值发生变化时设置 — 让视图显示 "unsaved"
    // 而不是用户猜测。 form_config APPENDS 是
    // 页脚的共享 "unsaved · ^S save" 标记，
    // 无论何时设置 — 面板 永远不会自己拼写。
    bool dirty = false;

    // 页脚注释。 SHARED 关键语法 (↑↓ · Enter · ^S · Esc)
    // 由 form_config 为 每个 呈现，形成 面板 — 没有 面板 重复。
    // 该字段仅携带PANE-SPECIFIC：Rag的"a advanced"、
    // 验证摘要、武装-remove警告。当设置为 TRANSIENT
    // WARNING（武装删除）时，面板 的 状态更新函数 拥有
    // 清除它 - 语法的投影 渲染 和 INSTEAD，
    // 因此它是不可错过的。
    std::string note;
    // 当 true 时，`note` REPLACES 是共享语法行，
    // 而不是在它前面 — 对于 arm/confirm 警告，
    // 这必须是页脚所说的唯一内容。
    bool note_replaces_grammar = false;

    // CURRENT编辑会话改变了字段的值。编辑时由关键层（
    // insert/backspace/clear）和paste_into设置；
    // 当编辑会话开始和结束时清除。 Powers
    // Applied::left_field — commit-on-exit
    // 仅在离开实际更改的字段时触发，因此通过字段进行纯粹的导航是免费的。
    bool edit_dirty = false;

    [[nodiscard]] const Field* focused() const noexcept {
        if (cursor < 0 || cursor >= static_cast<int>(fields.size())) return nullptr;
        return &fields[static_cast<std::size_t>(cursor)];
    }
    [[nodiscard]] Field* focused() noexcept {
        if (cursor < 0 || cursor >= static_cast<int>(fields.size())) return nullptr;
        return &fields[static_cast<std::size_t>(cursor)];
    }
    [[nodiscard]] const Field* find(std::string_view id) const noexcept;
    [[nodiscard]] Field*       find(std::string_view id) noexcept;

    [[nodiscard]] bool editing()  const noexcept {
        return std::holds_alternative<focus::Editing>(focus);
    }
    [[nodiscard]] bool choosing() const noexcept {
        return std::holds_alternative<focus::Choosing>(focus);
    }
    [[nodiscard]] const focus::Choosing* dropdown() const noexcept {
        return std::get_if<focus::Choosing>(&focus);
    }
    [[nodiscard]] focus::Choosing* dropdown() noexcept {
        return std::get_if<focus::Choosing>(&focus);
    }
};

// ── 构建器 ──────────────────────────────────────────────────────────────
// 符合人体工程学的表面。整个 面板 大约有 10 行：
//
//   auto f = form::Builder{"Embeddings"}
//       .choice("backend", "Backend", labels, ids, current_id)
//       .text  ("host",    "Host",    cfg.host,  "hostname or IP")
//       .number("port",    "Port",    cfg.port,  1, 65535)
//       .secret("api_key", "API key", cfg.key,   "stored in the OS keystore")
//       .slider("weight",  "Dense weight", 0.65)
//       .action("test",    "Test connection")
//       .build();
//
// 链接通过值-reference返回*this，因此调用者永远不必命名中间体；
// `build()` 将结果移出。
class Builder {
public:
    explicit Builder(std::string title) { form_.title = std::move(title); }

    Builder& subtitle(std::string s) { form_.subtitle = std::move(s); return *this; }
    // 面板-wide故障。渲染时会换行，因此消息中说明
    // 如何处理的部分会保留在狭窄的 面板 中。
    Builder& error(std::string s) { form_.error = std::move(s); return *this; }
    // 行下的页脚行：验证摘要、保存提示或关键可供性。
    // 面板 已直接设置的同一字段 - 暴露在构建器上，
    // 因此一次构建的表单可以说出其内容，
    // 而无需调用者触及结果。
    Builder& note(std::string s) { form_.note = std::move(s); return *this; }

    Builder& toggle(std::string id, std::string label, bool on, std::string help = {});

    // `ids`可能为空⇒标签ARE是id。 `selected` 按 ID 选择。
    Builder& choice(std::string id, std::string label,
                    std::vector<std::string> labels,
                    std::vector<std::string> ids = {},
                    std::string_view selected = {},
                    std::string help = {},
                    std::vector<std::string> hints = {});

    Builder& number(std::string id, std::string label, std::int64_t value,
                    std::int64_t min, std::int64_t max, std::string help = {});

    Builder& slider(std::string id, std::string label, double value,
                    double min = 0.0, double max = 1.0, double step = 0.05,
                    std::string help = {});

    Builder& text(std::string id, std::string label, std::string value,
                  std::string help = {});

    Builder& secret(std::string id, std::string label, std::string value,
                    std::string help = {});

    Builder& path(std::string id, std::string label, std::string value,
                  std::string help = {}, bool want_dir = false);

    Builder& action(std::string id, std::string label, std::string help = {},
                    std::string hint = "press Enter");

    // 第 头文件 节。通过光标移动跳过 - 落在不执行
    // 任何操作的标签上是死按键。
    Builder& header(std::string label);

    // 由另一个覆盖面板支持的行：Enter移交给真正
    // 的选择面板而不是打开下拉菜单。当候选集很大、
    // 动态或需要搜索时，请使用此选项。
    Builder& pick(std::string id, std::string label, std::string current,
                  std::string help = {});

    // 刚添加的秘密行在其为空时显示的文本（参见field::
    // Secret::placeholder)。任何其他类型均无 -op。
    Builder& placeholder(std::string text);

    // 将刚刚添加的行标记为锁定（e.g.由环境变量固定）。
    Builder& lock(std::string reason);
    // 刚刚添加的行的出处("default"、"settings.json"、"env: X")。
    Builder& origin(std::string where);

    // 构建表单的光标从其第一个实行开始，而不是 头文件：
    // 停放在不执行任何操作的行上的游标读取为冻结的面板。
    [[nodiscard]] Form build() {
        for (int i = 0; i < static_cast<int>(form_.fields.size()); ++i)
            if (!form_.fields[static_cast<std::size_t>(i)].is_header()) { form_.cursor = i; break; }
        return std::move(form_);
    }

private:
    Form form_;
};

// ── 渲染 住在这里 maya ,不在这里 ────────────────────────────────────
// 该头文件仅为STATE+REDUCER。它知道表单包含什么以及
// 键如何改变它；它没有字形、没有列、没有颜色、没有 光标 绘图。
// 视图层将表格投影到maya::Form::Config (机械场
// -by-field地图）上并且maya拥有每个像素——相同的合同选择面板和时间线遵循。
// `display()` 和朋友们之所以在这里做 不是因为这个原因：主机
// 中的 "just this one string" 助手是 chrome
// 跨边界泄漏的方式。

// ── 选项列表中显示一个下拉 ─────────────────────────────────────
// Choice 的选项逐字 — 没有过滤，因为下拉列表仅包含 枚举。
// 视图将其交给 maya::Form::Menu，状态更新函数 解析
// 针对它的提交，因此显示的内容和 Enter 选择的内容不会有
// 分歧。
struct Options {
    std::vector<std::string> labels;
    std::vector<std::string> hints;
};
[[nodiscard]] Options visible_options(const field::Choice& c);

// 秘密的字符计数，对于 maya::form::Secret{filled}
// — 小部件根本不会被给予明文。
[[nodiscard]] std::size_t secret_filled(const field::Secret& s) noexcept;

// 光标作为BYTE偏移到值中，这就是maya的with_caret
// 如何拼接它的。 npos 当该字段未被编辑时。
[[nodiscard]] std::size_t caret_offset(const FieldValue& v, bool editing) noexcept;

// 数字行的文本：编辑时迄今为止的数字带类型的（可能为空），
// 否则为值。
[[nodiscard]] std::string number_text(const field::Number& n);
// 数字行的实际值：mid-edit，带类型的数字被限制在范围
// 内（空保留已保存值）；否则该值。通过此读取 Number 行，
// 切勿使用 `.value`，它仅在编辑结束时更新。
[[nodiscard]] std::int64_t number_value(const field::Number& n);

// ── 编辑(纯突变) ─────────────────────────────────────────────
// 每个都是其不适用的类型的 no-op，因此 调用 站点
// 从不开启替代方案。全部夹住；没有人可以使字段无效。

// 将一个字符插入到文本-like字段中。
//
// `fresh`=这是编辑会话的第一次击键。文本字段忽略它（
// 光标 已经说明了角色着陆的位置）；数字字段将其用于
// REPLACE 而不是附加，因为编辑开始时屏幕上的值是 已保存，
// 并且没有人会扩展他们未输入的数字。
// 通过 `!f.edit_dirty`。
void insert(FieldValue& v, char32_t ch, bool fresh = false);
void backspace(FieldValue& v);
void delete_forward(FieldValue& v);
void move_cursor(FieldValue& v, int delta);
void cursor_home(FieldValue& v);
void cursor_end(FieldValue& v);
void paste(FieldValue& v, std::string_view text);

// ── 全文 -form 粘贴( E) SSOT) ─────────────────────────────────
// 将括号内的粘贴传送到表格 面板 中的一种正确方法。
// 通过 re-checking 此处的 TRUE 模式防范 过期-snapshot
// 竞态（输入路由器目标粘贴可能滞后的 FormFocus 快照；
// 请参阅 form_keys.cpp 的编辑-intent 防护以了解
// 相同的规则）：只有在实际编辑可编辑的未锁定行时，文本才会落地。
// 执行时返回 true（调用者设置 面板-specific 脏语义（如果有））。
// 每个形态面板的粘贴臂MUST正是`form::paste_into(o->form, e.text)`
// 或`form::paste_into_focused(o->form, e.text)`
// ——一只手-rolled守卫是这些存在的终结（在此之前三个面板有它的
// 三个副本）。
bool paste_into(Form& f, std::string_view text);

// 粘贴输入方式(type-to-edit)：编辑时，正好paste_into；
// 当 BROWSING 是可编辑的、未锁定的文本 -like 行时，
// 粘贴将打开编辑会话并作为其第一个输入。下拉列表、锁定的行或非 -text
// 行不会发生任何事情，或者对于粘贴，该行将不包含任何内容（数字没有数字，
// 只有 newlines/tabs）——编辑会话永远不会在没有任何内容的
// 情况下打开。当文本到达时返回 true。使用此功能的 面板 每当
// OPEN 时都会将粘贴路由到自身，而不仅仅是 mid-edit (
// subscribe.cpp)；这里的警卫保证了这里的安全。
bool paste_into_focused(Form& f, std::string_view text);

// ── 带类型的 字段阅读器( E) SSOT) ───────────────────────────────
// 通过id读取字段的值，而不需要在每个调用站点拼写变体访问。 text_of 涵
// 盖了 全部 文本-like 替代品 (Text/Secret/Path)，
// 因此 面板 无法从它认为是文本的 Secret 中静默读取 "" — 编写确切
// 的错误类 rag_form 的本地帮助程序是为了避免，现在已提升，
// 因此 smart_form/plugin_form/rag_form 共享一个定义。
// 缺少id或错误种类⇒中性值；必须区分"absent"和
// "empty"的面板直接使用Form::find。
[[nodiscard]] std::string text_of  (const Form& f, std::string_view id);
[[nodiscard]] bool        toggle_of(const Form& f, std::string_view id);
// 选择行的选定id(当absent/not为选择时为"")。
[[nodiscard]] std::string choice_of(const Form& f, std::string_view id);
void clear(FieldValue& v);

// ←/→连续：翻转切换，循环选择，步进Slider/Number.
void adjust(FieldValue& v, int dir);

// ── 表-level 业务 ────────────────────────────────────────────────
// 共享状态更新函数 正文。 per-pane 状态更新函数 将其消息
// 映射到这些消息上，然后读回值；它从不重新-implements导航。

// 移动行光标（换行）。当下拉菜单打开时，No-op - 该
// 模式拥有突出显示。
void move(Form& f, int delta);

// 在焦点行上开始/结束编辑会话。进入或离开编辑的 每个
// 路径会经过这些路径，因此具有会话状态的字段类型（数字
// 的数字草稿）不能被一个入口点跳过。 end_edit
// 将草稿提交到该值中。
void begin_edit(Form& f);
void end_edit(Form& f);

// 将表单的行替换为`next`，保留用户的位置：如果相同
// id的行仍然存在，则光标停留在该行上，否则停留在相同位置的
// 最近的真实行上；从来没有在 头文件 上。打开的编辑或下拉菜单
// 被关闭（其行可能不再存在），并保留 `dirty`。
// 每个重建其行的 面板 都必须使用它，而不是手动修补游标。
// `prefer` 落在该行上（当它存在时），
// 用于将光标移动到刚刚添加的行的重建。
void rebuild(Form& f, Form next, std::string_view prefer = {});
// 在适当地添加或删除行之后将光标放回到实际行上：如果有效
// 则相同的索引，否则是最近的非-header行。
void settle_cursor(Form& f);
// Home/End:跳转到first/last非-header字段WITHOUT换行。
void move_edge(Form& f, bool last);
// PgUp/PgDn:头文件-skipping在边缘跨过CLAMPS(无环绕)。
void move_page(Form& f, int delta);
// 将光标置于具有该id的行上。当表单没有这样的行时，
// 为 False（光标不变）。
bool focus_row(Form& f, std::string_view id);

// 焦点行上的 Enter：接合文本字段、打开选择的下拉菜单、
// 翻转 切换、将选择交给其 选择面板，或报告动作被
// 触发。
enum class Activated : std::uint8_t { Nothing, StartedEditing, OpenedDropdown,
                                      Changed, FiredAction, HandOff };
[[nodiscard]] Activated activate(Form& f);

// Esc:离开最内层模式。当表单本身应该关闭时返回 true
// （i.e。我们已经在浏览）。
[[nodiscard]] bool escape(Form& f);

// Dropdown-only操作。在任何模式下对 调用 都是安全的（其他地方对 -ops 是安全的）。
void dropdown_move(Form& f, int delta);
// 将突出显示的选项提交到字段中并返回到浏览。
[[nodiscard]] bool dropdown_commit(Form& f);

// 将焦点行重置为`dflt`并将表格标记为脏。
void reset_field(Form& f, const FieldValue& dflt);

// ── 版式 ───────────────────────────────────────────────────────────────
// 故意缺席。下拉菜单浮动的位置、它如何在底部边缘附近翻
// 转以及它的宽度是 LAYOUT 决定 - maya::Form
// 从已经给出的行集中解析它们。 Host-side
// 几何管道（在此处测量行，将坐标传回）正是让 回滚缓冲区
// 会计漂移这么长时间的形状：两个所有者进行一次测量。

} // namespace agentty::form
