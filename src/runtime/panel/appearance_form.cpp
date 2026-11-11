// appearance_form.cpp ——建设外观. 面板 预科医生的表格
//
// 纯投影:预射在, form::Form 出局,每行都一样 DATA ,
// 所以共享形式 状态更新函数 拥有导航、下拉和编辑,
// 以及 maya::Form 拥有每个格言。这个文件的唯一观点是,
// 哪些行存在,它们被称作什么,以及每个行对自己所说的话。

#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/i18n/i18n.hpp"   // t(), endonym(), completeness()

#include <span>

#include "agentty/domain/ui_theme.hpp"

#include <maya/style/schemes.hpp>

#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace agentty::ui::panel {

namespace {

namespace up = agentty::ui_prefs;

// 由 枚举 标签构建的选择, 由当前值选择。
//
// 这些矿坑每个都很小,而且封闭着——三到五个价值,符合一个标准。
// 头文件 评论——正是 form.hpp 选择规则,
// 而不是选择规则
template <typename E, std::size_t N>
void enum_choice(form::Builder& b, std::string_view id, std::string label,
                 const std::array<E, N>& values, E current,
                 std::string help,
                 const std::array<const char*, N>& blurbs) {
    std::vector<std::string> labels, ids, hints;
    labels.reserve(N); ids.reserve(N); hints.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        labels.emplace_back(up::label(values[i]));
        ids.emplace_back(up::label(values[i]));
        hints.emplace_back(blurbs[i]);
    }
    b.choice(std::string{id}, std::move(label), std::move(labels),
             std::move(ids), up::label(current), std::move(help),
             std::move(hints));
}

}  // 命名空间结束。

form::Form build_appearance_form(const up::Prefs& p, const up::Detected& d) {
    const up::Resolved r = up::resolve(p, d);

    form::Builder b{" Appearance "};

    // 字幕中包含着一个事实,这些行不能: 已保存
    // 外观是 USER 关注——一个轻度 终端
    // 是你的眼财产,不是你的眼财产 仓库 说到这里
    // 不再问每个排的问题了
    b.subtitle("how agentty looks \xc2\xb7 saved for your user, not this project");

    // ── 语言 ────────────────────────────────────────────────────
    //
    // 首次 ,上方的主题,因为它会更改屏幕上
    // 的所有其它标签,包括主题的标签。
    // 面板 无法导航到修正它行,所以它必须是他们
    // 到达的不读的。
    b.header(std::string{i18n::t("lang.picker.title")});
    {
        // 地名,而不是我们的英文名:有人在寻找自己的语言,
        // 正在寻找THEY的词。一个日语
        // speaker scans for 日本語 and slides straight past "Japanese".
        const auto cur = i18n::active();
        // 滴滴滴滴下滴滴滴 滴滴 倒下滴滴 落下滴滴 CHOICE ,没有满 -screen 选择面板.
        //
        // 主题行是选择面板,因为浏览主题就是点—
        // —你箭头穿过它们来比较,每个主题的活预
        // 览都是特征。语言没有等效:在打开行前,
        // 你知道想要哪个,然后选择一次。
        //
        // 二十岁 -one 条目不只应包含一个 QQ/ 周期,
        // 而这正是 form.hpp 滴水是用来。
        //
        // 编号是 TAGS ,标签是内语地名。用 id 来选择,
        // 表示存储值是 "de" 而不是索引,所以以后在表格中
        // 插入语言不能默默改变什么是现有的 settings.json
        // 说吧
        std::vector<std::string> ids{""};
        std::vector<std::string> labels{std::string{i18n::t("lang.auto")}
                                        + " \xc2\xb7 "
                                        + std::string{i18n::endonym(cur)}};
        for (int i = 0; i < i18n::kLangCount; ++i) {
            const auto l = static_cast<i18n::Lang>(i);
            ids.emplace_back(i18n::tag_of(l));

            // 内语名加,当翻译为部分,多为部分.
            //
            // 显示是因为有一半 -translated
            // UI 否则读作断开: 英文的列外的德国行
            // 看起来像一个错误, 直到您知道翻译为 60%。
            // 显示它 此处 ——在列表中,在犯罪之前,
            // 它是一个选择而不是发现。被压在100%,
            // 而这个数量从未改变过就是噪音。
            std::string label{i18n::endonym(l)};
            if (const double done = i18n::completeness(l); done < 0.999) {
                const i18n::Arg pct{
                    "pct", std::to_string(static_cast<int>(done * 100.0 + 0.5))};
                label += " \xc2\xb7 " + i18n::format("lang.incomplete",
                                                     std::span{&pct, 1});
            }
            labels.push_back(std::move(label));
        }
        b.choice(std::string{kApLang},
                 std::string{i18n::t("lang.picker.title")},
                 std::move(labels), std::move(ids), p.lang,
                 std::string{i18n::t("lang.picker.help")});

        // 证明,主题行用于"why is it this"的同一档次.
        //
        // 空`lang`表示自动,而自动是用户最可能
        // 感到惊讶的地方——他们没有选择,环境就是如此.
        // 命名变量使得"why is agentty in German"在不进行
        // 网络搜索的情况下可以回答.
        if (p.lang.empty()) b.origin("from your system locale");
    }

    // ── 主题 ───────────────────────────────────────────────────────
    b.header(std::string{i18n::t("appearance.theme")});
    {
        // 选择,而不是选择:57个计划,并且正在增长,这正是事实。
        // form.hpp 表示属于可搜索的 选择面板 而不是
        // 一个滴滴。
        //
        // 电话 值 仅仅是方案名称。它也用于带超常原因( Name
        // "Dracula — needs 256 colors — using native" ),（Dracula 需要 256 色；能力不足时使用原生主题。）
        // 它同时打破了三条路: 后方的 —— 标记
        // "this opens a picker" 被推
        // 下右边缘,所以单此行看起来不像设置中的其他每
        // 一个 Pick; 值列不再与上下行排成一行;
        // 和狭义的 终端 整个东西椭圆化,隐藏了方案名称本身—
        // —这是行存在要显示的事实之一.
        //
        // 原因属于 `origin` ,
        // 这是用于 "where this value came from / why it is what it is" ,
        // 其他各行都已经用到这个了。
        b.pick(std::string{kApTheme}, std::string{i18n::t("appearance.scheme")},
               p.theme.empty() ? "native" : p.theme,
               std::string{i18n::t("appearance.help.scheme")});
        const std::string_view why = up::theme_override_reason(p, r);
        if (!why.empty())          b.origin(std::string{why});
        else if (p.theme.empty())  b.origin("your terminal");
    }

    // ── 颜色 ───────────────────────────────────────────────────────
    b.header(std::string{i18n::t("appearance.color")});
    {
        static constexpr std::array kTiers = {
            up::ColorTier::Auto, up::ColorTier::TrueColor,
            up::ColorTier::Ansi256, up::ColorTier::Ansi16, up::ColorTier::Mono,
        };
        static constexpr std::array<const char*, 5> kTierBlurbs = {
            "detect from the terminal",
            "24-bit \xc2\xb7 38;2;r;g;b",
            "the xterm-256 palette",
            "the sixteen every terminal has",
            "no color at all",
        };
        enum_choice(b, kApTier, "Colors", kTiers, p.tier,
                    "what agentty may emit", kTierBlurbs);
        // 电话 RESOLVED 值作为出处。
        // 行读取 "auto" 单凭一个检测结果无法分辨
        // 工作默认值 终端 ——这是第37期的全部内容.
        if (p.tier == up::ColorTier::Auto) {
            switch (r.tier) {
                case maya::theme::ColorTier::TrueColor: b.origin("detected: truecolor"); break;
                case maya::theme::ColorTier::Ansi256:   b.origin("detected: 256 colors"); break;
                case maya::theme::ColorTier::Ansi16:    b.origin("detected: 16 colors"); break;
                case maya::theme::ColorTier::Mono:      b.origin("detected: monochrome"); break;
            }
        }
    }
    {
        static constexpr std::array kPol = {
            up::Polarity::Auto, up::Polarity::Dark, up::Polarity::Light,
        };
        static constexpr std::array<const char*, 3> kPolBlurbs = {
            "read COLORFGBG, else leave it unknown",
            "assume a dark background",
            "assume a light background",
        };
        enum_choice(b, kApPolarity, "Background", kPol, p.polarity,
                    "flags a scheme that fights your terminal", kPolBlurbs);
        if (p.polarity == up::Polarity::Auto) {
            switch (r.polarity) {
                case maya::theme::Polarity::Dark:    b.origin("detected: dark"); break;
                case maya::theme::Polarity::Light:   b.origin("detected: light"); break;
                // 不算失败,大多数 终端 从未设置 COLORFGBG
                // 猜想是这整个虫子 面板 存在以修正。
                case maya::theme::Polarity::Unknown: b.origin("not reported"); break;
            }
        }
    }

    // ── 版式 ──────────────────────────────────────────────────────
    b.header(std::string{i18n::t("appearance.layout")});
    {
        static constexpr std::array kDens = {
            up::Density::Compact, up::Density::Normal, up::Density::Roomy,
        };
        static constexpr std::array<const char*, 3> kDensBlurbs = {
            "10 rows \xc2\xb7 for a short terminal",
            "14 rows \xc2\xb7 the shared default",
            "22 rows \xc2\xb7 fewer panels scroll",
        };
        enum_choice(b, kApDensity, "Panel height", kDens, p.density,
                    "how tall a panel's body may get", kDensBlurbs);
    }
    // 读数。 0 是一个真正的值。 "no cap" ——所以它是一个
    // 数字而不是一个选择:有用的设置是一个连续体(72,80,100),
    // 而不是一个 枚举 任何人都可以说出名字。
    b.number(std::string{kApProseWidth}, std::string{i18n::t("appearance.prose_width")}, p.prose_width, 0, 200,
             std::string{i18n::t("appearance.help.prose_width")});
    b.toggle(std::string{kApCompact}, std::string{i18n::t("appearance.compact")}, p.compact_turns,
             std::string{i18n::t("appearance.help.compact")});
    b.toggle(std::string{kApThreadPeek}, std::string{i18n::t("appearance.thread_preview")},
             p.thread_preview, std::string{i18n::t("appearance.help.thread_preview")});
    b.toggle(std::string{kApForeign}, std::string{i18n::t("appearance.foreign_sessions")},
             p.foreign_sessions, std::string{i18n::t("appearance.help.foreign_sessions")});

    // ── 动议 ──────────────────────────────────────────────────────
    b.header(std::string{i18n::t("appearance.motion")});
    {
        static constexpr std::array kMot = {
            up::Motion::Full, up::Motion::Reduced, up::Motion::Off,
        };
        static constexpr std::array<const char*, 3> kMotBlurbs = {
            "streaming reveal and spinners",
            "reveal kept \xc2\xb7 \xc2\xbc the repaints \xc2\xb7 no glyph churn",
            "nothing animates",
        };
        // 减少运动是无障碍环境,
        // 排字机 逐步显示 真正令人不快的是 手部紊乱症,
        // 和 加载动画 慢慢来 链接 声音在 传输.
        //
        // 模糊的字迹 "no reveal \xc2\xb7 spinners kept" ,
        // 在两个半数上都倒了过来——缩小了 KEEPS
        // 编号 逐步显示 并降低装饰性粉饰。它也低估了慢度很重要的部分。
        // 链接 : 减少现在的瘦化 重绘 利率到四分之一,
        // 所以它是到达 超过摩什(问题36)而不是
        // 温和的 -sounding 满.
        enum_choice(b, kApMotion, "Animation", kMot, p.motion,
                    "reduce or stop movement", kMotBlurbs);
    }
    {
        static constexpr std::array kScroll = {
            up::Scrollback::Auto, up::Scrollback::Tiny, up::Scrollback::Small,
            up::Scrollback::Big, up::Scrollback::Full,
        };
        static constexpr std::array<const char*, 5> kScrollBlurbs = {
            "match the terminal's scrollback",
            "last ~1k rows \xc2\xb7 lightest",
            "last ~5k rows",
            "last ~20k rows",
            "the whole thread \xc2\xb7 most memory",
        };
        enum_choice(b, kApScrollback, "Scrollback", kScroll, p.scrollback,
                    "how much history is redrawn on open and resize", kScrollBlurbs);
    }

    // ── 内容 ─────────────────────────────────────────────────────
    b.header(std::string{i18n::t("appearance.content")});
    b.toggle(std::string{kApSyntax}, std::string{i18n::t("appearance.syntax")}, p.syntax,
             std::string{i18n::t("appearance.help.syntax")});
    {
        static constexpr std::array kTool = {
            up::ToolOutput::Collapsed, up::ToolOutput::Preview, up::ToolOutput::Full,
        };
        static constexpr std::array<const char*, 3> kToolBlurbs = {
            "just the header line",
            "the first few lines",
            "everything, always",
        };
        enum_choice(b, kApToolOutput, "Tool output", kTool, p.tool_output,
                    "how much a tool shows before you ask", kToolBlurbs);
    }
    {
        static constexpr std::array kThink = {
            up::Thinking::Shown, up::Thinking::Collapsed, up::Thinking::Hidden,
        };
        static constexpr std::array<const char*, 3> kThinkBlurbs = {
            "reasoning inline",
            "a line you can open",
            "never shown · not requested",
        };
        enum_choice(b, kApThinking, "Thinking", kThink, p.thinking,
                    "whether the model's reasoning is shown — and asked for",
                    kThinkBlurbs);
    }
    {
        static constexpr std::array kStamp = {
            up::Timestamps::Off, up::Timestamps::Relative, up::Timestamps::Absolute,
        };
        static constexpr std::array<const char*, 3> kStampBlurbs = {
            "no times",
            "\"2m ago\"",
            "\"14:32\"",
        };
        enum_choice(b, kApTimestamps, "Timestamps", kStamp, p.timestamps,
                    "whether turns carry a time", kStampBlurbs);
    }

    // 没有 "unsaved" 标记和无 ^S :这里的
    // 每行都写到它变化的瞬间,因为一个主题是通过看它来判断的,
    // 在选择和看之间有一个应用步骤使得这个过程变得不可能.
    b.note("changes apply immediately");
    return b.build();
}

// ── 主题搜索 ────────────────────────────────────────────────────────
//
// 子序列匹配, 而非子字符串 : "gvd" 发现 "Gruvbox Dark"
// 模糊的路 模糊的风云 选择面板 ,当清单长度为57个,
// 名字为2个字和3个字时,这很重要。 -insensitive ,
// 然后空查询会列出所有信息,这样 选择面板 打开可浏览目录而不是
// 空白 提示词.
//
// "native" 总是领导清单。它不是产生的
// 计划之一,而是没有计划,而是回到自己的计划。
// 终端 颜色绝对不是你要知道的名字
// ── 方案列表, 作为 选择面板 源 + 过滤器 ────────────────────
//
// `matching_themes`
// 整个故事: 状态更新函数 3个独立
// 生成列表几何的呼叫者正是光标和光
// 标的精确方式。 可视区域 漂散无边.
// 现在有一个拥有者(FilteredPicker),
// 这两个功能只是它所养的.

// 每个方案名, 本地先作为空字符串。 process -lifetime 财务报告和已审计
// 财务报表 不可变 ,所以,请 选择面板's 快照 已经建成一次, 并且永远不再重新填充。
const util::Snapshot<std::vector<std::string>>& all_theme_names() {
    static const util::Snapshot<std::vector<std::string>> names = [] {
        std::vector<std::string> out;
        out.reserve(std::size(maya::theme::schemes) + 1);
        out.emplace_back();   // 原生的——终端的自有颜色
        for (const auto& s : maya::theme::schemes)
            out.emplace_back(s.name);
        return util::make_snapshot(std::move(out));
    }();
    return names;
}

ui::SnapshotSource<std::string> theme_source() {
    return ui::SnapshotSource<std::string>{
        .ready = [] { return true; },
        .fetch = [] { return all_theme_names(); },
    };
}

ui::FilterFn<std::string> theme_filter() {
    return [](const std::vector<std::string>& entries, std::string_view query) {
        auto fold = [](std::string_view s, std::string& into) {
            into.clear();
            into.reserve(s.size());
            for (char c : s)
                into.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c))));
        };
        std::string q;
        fold(query, q);

        // 子序列, 不是子字符串 : "gvd" 发现 "Gruvbox Dark" 旧
        // 规则 matching_themes 已经用过了,所以肌肉 内存 不变。
        auto subseq = [](std::string_view hay, std::string_view needle) {
            std::size_t k = 0;
            for (char c : hay)
                if (k < needle.size() && c == needle[k]) ++k;
            return k == needle.size();
        };

        std::vector<std::size_t> out;
        out.reserve(entries.size());
        // 一个可重复使用 缓冲区 而不是每个方案的新字符
        // 串: 在615个条目中, 大部分是 分配 全部丢弃
        std::string folded;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            // 空名称 IS 本地。它匹配空查询,
            // 与字面 "native" 否则——所以一个查询,
            // 显然意味着一个方案不会拖着原生物沿着顶部。
            const std::string_view name =
                entries[i].empty() ? std::string_view{"native"}
                                   : std::string_view{entries[i]};
            if (!q.empty()) {
                fold(name, folded);
                if (!subseq(folded, q)) continue;
            }
            out.push_back(i);
        }
        return out;
    };
}

const std::vector<std::string>& matching_themes(std::string_view query) {
    // 查询时已提醒, 返回 BY REFERENCE.
    //
    // 615个计划 并建立一个 std::string 每场
    // 比赛——~330 分配 11 -34 我们a 调用 - 叫做两
    // 三次 一个 箭头键 : move_highlight ()需要
    // 伯爵来包罗索引, highlighted_theme ()
    // 需要列表来命名登陆行,视图需要再次绘制. 3个相同的答案,
    // 对于一个输入没有改变它们之间的问题.
    //
    // 电话 记忆缓存 杀了三个人 SCANS 它没有杀死这三人
    // COPIES : 按值返回, 重新交回 615 -allocated
    // 字符串在每张 调用 ,所以一个持有 -down 箭头键仍付
    // ~1000 分配 每一个按键可以回答它已经回答的问题。
    // 缓存 是进程 -lifetime 财务报告和已审计财务报表
    // 不可变 所以一个参考信息既安全 也完全属于缓存
    static std::string   cached_query;
    static bool          cached_valid = false;
    static std::vector<std::string> cached;

    if (cached_valid && cached_query == query) return cached;

    auto lower = [](std::string_view s) {
        std::string o; o.reserve(s.size());
        for (char c : s) o.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(c))));
        return o;
    };
    const std::string q = lower(query);

    auto subseq = [](std::string_view hay, std::string_view needle) {
        std::size_t k = 0;
        for (char c : hay) if (k < needle.size() && c == needle[k]) ++k;
        return k == needle.size();
    };

    std::vector<std::string> out;
    // 本地第一,只有在匹配的时候——所以一个显然意味着
    // 一个方案在顶端不会随身携带的查询.
    if (q.empty() || subseq("native", q)) out.emplace_back();

    // 一个可重复使用 缓冲区 ,而不是一个新名称
    // std::string 615个条目,
    // 其中多数为 分配 立即丢弃了所有的东西
    std::string folded;
    for (const auto& s : maya::theme::schemes) {
        const std::string_view name{s.name};
        if (!q.empty()) {
            folded.clear();
            folded.reserve(name.size());
            for (char c : name)
                folded.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c))));
            if (!subseq(folded, q)) continue;
        }
        out.emplace_back(name);
    }

    cached_query = std::string{query};
    cached       = std::move(out);
    cached_valid = true;
    return cached;
}

}  // namespace agentty::ui::panel
