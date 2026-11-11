// agentty::i18n-在启动时选择语言，以及嵌入式目录。
//
// 与i18n.cpp分开，因为该文件是纯粹的存储和查找：它
// 不知道目录来自哪里或如何选择语言。这个拥有两者，
// 这使得可测试核心不受 getenv 和文件 IO 的影响。

#include "agentty/i18n/startup.hpp"

#include "catalog_de.hpp"
#include "catalog_es.hpp"
#include "catalog_fr.hpp"
#include "catalog_ja.hpp"
#include "catalog_pt_BR.hpp"
#include "catalog_ru.hpp"
#include "catalog_zh_CN.hpp"

#include <cstdlib>

namespace agentty::i18n {

namespace {

// 英文目录，嵌入式。
//
// 嵌入而不是从磁盘读取，这不仅仅是传送一个文件。
// 在 运行时 上读取的 目录 是一个注射表面：它决定
// 每个标签的内容，包括 权限 提示词 上的标签。
// 如果用户被告知将 JSON 文件放入正确的目录中，
// 则会出现一个对话框，其中显示的内容与实际操作不同。
//
// 调试从 $AGENTTY_HOME/i18n/ (参见 load_overrides)构建
// hot-reload，以便翻译器可以迭代，并且正是由于这个原因，该路径是 debug
// -only。
//
// STARTING SMALL 和 HONEST。这些是 沙箱 面板 的行
// ——应用程序中最密集的用户-facing 表面和最好的-tested 表面。
// 扫描从 面板 继续到 面板；棉绒（tests/lint/i18n_lint.cmake）
// 证明途中没有掉落任何东西。
constexpr std::string_view kEnglish = R"JSON({
  "_meta": { "lang": "en", "plurals": ["one", "other"] },

  "lang.picker.title":   { "text": "Language",      "width_hint": 8 },
  "lang.picker.help":    { "text": "agentty's own labels · the model answers in whatever language you write in", "width_hint": 72 },
  "lang.auto":           { "text": "Auto (follow system)", "width_hint": 20 },
  "lang.incomplete":     { "text": "{pct}% translated",    "width_hint": 16 },

  "appearance.theme":        { "text": "Theme",               "width_hint": 5 },
  "appearance.scheme":       { "text": "Scheme",              "width_hint": 6 },
  "appearance.color":        { "text": "Color",               "width_hint": 5 },
  "appearance.layout":       { "text": "Layout",              "width_hint": 6 },
  "appearance.prose_width":  { "text": "Prose width",         "width_hint": 11 },
  "appearance.compact":      { "text": "Compact turns",       "width_hint": 13 },
  "appearance.thread_preview": { "text": "Thread preview", "width_hint": 14 },
  "appearance.foreign_sessions": { "text": "Codex & Claude sessions", "width_hint": 23 },
  "appearance.motion":       { "text": "Motion",              "width_hint": 6 },
  "appearance.content":      { "text": "Content",             "width_hint": 7 },
  "appearance.syntax":       { "text": "Syntax highlighting", "width_hint": 19 },

  "appearance.help.scheme":      { "text": "native keeps your terminal's own colors · Enter to browse", "width_hint": 56 },
  "appearance.help.prose_width": { "text": "wrap assistant text at N columns · 0 = the full width",    "width_hint": 53 },
  "appearance.help.compact":     { "text": "drop the blank line between turns",   "width_hint": 33 },
  "appearance.help.thread_preview": { "text": "show the highlighted thread in ^J", "width_hint": 33 },
  "appearance.help.foreign_sessions": { "text": "list them in ^J to continue here", "width_hint": 32 },
  "appearance.help.syntax":      { "text": "colour code fences by language",      "width_hint": 30 },

  "sandbox.read_paths":  { "text": "Also readable", "width_hint": 13 },
  "sandbox.write_paths": { "text": "Also writable", "width_hint": 13 },
  "sandbox.deny_paths":  { "text": "Masked",        "width_hint": 6 },
  "sandbox.ports":       { "text": "Allowed ports", "width_hint": 13 },
  "sandbox.hosts":       { "text": "Allowed hosts", "width_hint": 13 },
  "sandbox.memory":      { "text": "Memory (MB)",   "width_hint": 11 },
  "sandbox.procs":       { "text": "Processes",     "width_hint": 9 },
  "sandbox.scan_depth":  { "text": "Secret scan depth", "width_hint": 17 },

  "sandbox.help.read_paths":  { "text": "extra paths a command may read, beyond the scope above · for a dependency outside the workspace", "width_hint": 94 },
  "sandbox.help.write_paths": { "text": "separate from readable on purpose: granting write is a different decision", "width_hint": 72 },
  "sandbox.help.deny_paths":  { "text": "carved out even inside the scope above · e.g. ~/.cargo/credentials. credential files are masked anyway.", "width_hint": 102 },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only", "width_hint": 74 },
  "sandbox.help.hosts":       { "text": "empty = any public host · github.com exact · *.github.com subdomains only", "width_hint": 74 },
  "sandbox.help.memory":      { "text": "0 = no cap", "width_hint": 10 },
  "sandbox.help.procs":       { "text": "0 = no cap · stops fork bombs", "width_hint": 29 },
  "sandbox.help.scan_depth":  { "text": "0 = workspace root only · 3 finds services/*/.env · each level costs stat() calls on every command", "width_hint": 97 },

  "common.on":     { "text": "on",     "width_hint": 2 },
  "common.off":    { "text": "off",    "width_hint": 3 },
  "common.custom": { "text": "Custom", "width_hint": 17 }
})JSON";
// common.custom的width_hint是17，
// 而不是英文串的6，这是一般规则而不是例外：width_hint
// 是LAYOUT必须吸收的列数，而不是英文的长度。德语需要 17
// 来表示 "Benutzerdefiniert"，该词没有更短的形式。
// lint 发现它超出了预算 2.83 倍，而英语提示就是
// 错误所在，因为 one-word 英语标签并不能证明每种
// 语言都有一个。

// 以 POSIX 优先顺序从环境中读取语言环境。
//
// LC_ALL覆盖一切，LC_MESSAGES是实际管理程序消息的消息，LANG是回退。
// LANGUAGE（GNU 扩展名）是一个冒号-separated
// PREFERENCE LIST——我们取它的第一个条目，
// 它是最接近"what this user would rather read" 的。
[[nodiscard]] bool from_environment(Lang& out) {
    const char* const vars[] = {"LC_ALL", "LC_MESSAGES", "LANGUAGE", "LANG"};
    for (const char* v : vars) {
        const char* raw = std::getenv(v);
        if (!raw || !*raw) continue;

        std::string_view s{raw};
        // "C"和"POSIX"表示"no locale"，
        // 而不是"a language named C"。它们不能落入找到捷克语的前缀匹配。
        if (s == "C" || s == "POSIX" || s == "C.UTF-8") return false;

        // LANGUAGE是列表："de:en:fr"。拿第一个。
        if (const auto colon = s.find(':'); colon != std::string_view::npos)
            s = s.substr(0, colon);

        if (parse_tag(s, out)) return true;
        // 我们识别为语言但不识别SHIP (ar、he、th)的标签必须在此
        // 停止而不是落入下一个变量——LANG=ar_EG且LC_MESSAGES
        // 未设置意味着阿拉伯语，并且回答"English"是正确的而
        // 回答"whatever LANG says next"则不是。
        // parse_tag 已经返回 false；继续循环是此注释要防止的错误，
        // 因此：不要添加跳过 VALID-but-unshipped
        // 标记的 `continue`。
        return false;
    }
    return false;
}

}  // 命名空间结束。

Lang resolve_language(std::string_view cli_lang, std::string_view saved) {
    Lang l{};

    // 1.标志。命令总是获胜——这就是用户 带类型的 此
    //    运行，并且通过设置或区域设置它不能是第二个-guessed。
    if (!cli_lang.empty() && parse_tag(cli_lang, l)) return l;

    // 2.当用户做出选择时，已保存设置。空的意思是
    //    "auto"，这就是它被存储为空而不是存储为"en"的原因（参见ui_prefs::
    //    Prefs::lang)。
    if (!saved.empty() && parse_tag(saved, l)) return l;

    // 3.环境。
    if (from_environment(l)) return l;

    // 4.英语。
    return Lang::en;
}

bool init(std::string_view cli_lang, std::string_view saved) {
    // 英文ALWAYS 加载，无论选择哪个。 目录 的 回退
    // 只翻译了 70%，而 completeness() 则
    // 针对它采取措施——因此缺少 en 目录 将使所有其他语言
    // 报告 0% 并回退到原始 ID。
    if (!install_catalog(Lang::en, kEnglish)) return false;

    // 每OTHER 目录，无条件。
    //
    // 加载所有这些语言而不仅仅是活动语言在启动时要
    // 花费 解析（微秒——这是几十个短字符串），
    // 并且购买了两个更有价值的东西：选择面板 可以
    // 显示您不使用的语言的真实完整性百分比，
    // 并且 运行时 处的切换语言是指针存储而不是 解析。
    //
    // IGNORED 故障是故意的。错误的翻译不得阻止 agentty
    // 的启动； install_catalog 将插槽留空，
    // set_active 拒绝它，选择面板 将其显示为 0%。
    // 这是可见且可恢复的，而启动中止则不然。
    (void)install_catalog(Lang::de, catalogs::kGerman);
    (void)install_catalog(Lang::zh_CN, catalogs::kChineseSimplified);
    (void)install_catalog(Lang::es, catalogs::kSpanish);
    (void)install_catalog(Lang::fr, catalogs::kFrench);
    (void)install_catalog(Lang::ja, catalogs::kJapanese);
    (void)install_catalog(Lang::pt_BR, catalogs::kPortugueseBR);
    (void)install_catalog(Lang::ru, catalogs::kRussian);

    const Lang want = resolve_language(cli_lang, saved);
    if (want == Lang::en) return set_active(Lang::en);

    // 没有编译目录的语言不是错误：它是尚未有人翻译的语言。
    // set_active 拒绝并保留英语，
    // 这是正确的结果，不需要任何消息 - 选择面板
    // 显示完整性，因此在重要的地方可以看到差距。
    if (!set_active(want)) return set_active(Lang::en);
    return true;
}

}  // namespace agentty::i18n
