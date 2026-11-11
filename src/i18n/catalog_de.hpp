// agentty::i18n-德国目录。
//
// 为什么德语是SECOND语言而不是西班牙语：它是最长的。
// 在真实的agentty标签上测量，德运行1.35-1.61x英文，
// 压力点是设置面板，其是硬帧内的两列。如果布局在德语中幸存下来，
// 那么它在该集合中的所有其他拉丁语-script语言中都幸存下来。
// （中文是另一个极端——宽度是一半，每个字符的列
// 数是两倍——并且是下一个出于同样原因登陆
// 的。）
//
// TRANSLATION NOTES，写下来，因为下一个接触这些的
// 人将会"fix"：
//
//   * "Sandbox" 保持 "Sandbox"。德国科技德国人借用了它，并且
//     "Sandkasten"表示儿童游乐区。与 "Tokens" 相同。
//
//   * 行标签是NOUN PHRASES，不是句子，匹配英文。德语
//     每个名词都大写，因此 "Auch lesbar" 是正确的，
//     而英语读者可能期望 "Auch Lesbar"——lesbar 是形容词。
//
//   *帮助行保留英语的下-case开头及其"·"分隔符。
//     它们是片段，不是句子，布局中标点符号为加载
//     -bearing。
//
//   * 一个有意缩短的是appearance.help.prose_width：
//     直译运行71列，而英语运行53列，这在80列处溢出了帮助槽。
//     "assistant text" 的 "Fließtext"
//     是标准印刷术语，回购 12 个。
//
// UNTRANSLATED ON PURPOSE：这里什么都没有。
// 截至本次提交，这个 目录 相对于 en.json 是完整的，
// 这使得它作为布局测试很有用——部分 目录 将回退到英语并隐藏溢出。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kGerman = R"JSON({
  "_meta": { "lang": "de", "plurals": ["one", "other"] },

  "lang.picker.title":   { "text": "Sprache" },
  "lang.picker.help":    { "text": "agenttys eigene Beschriftungen · das Modell antwortet in der Sprache, in der Sie schreiben" },
  "lang.auto":           { "text": "Automatisch (System)" },
  "lang.incomplete":     { "text": "zu {pct}% übersetzt" },

  "appearance.theme":        { "text": "Design" },
  "appearance.scheme":       { "text": "Farbschema" },
  "appearance.color":        { "text": "Farbe" },
  "appearance.layout":       { "text": "Layout" },
  "appearance.prose_width":  { "text": "Textbreite" },
  "appearance.compact":      { "text": "Kompakte Züge" },
  "appearance.thread_preview": { "text": "Thread-Vorschau" },
  "appearance.foreign_sessions": { "text": "Codex- & Claude-Sitzungen" },
  "appearance.motion":       { "text": "Animation" },
  "appearance.content":      { "text": "Inhalt" },
  "appearance.syntax":       { "text": "Syntaxhervorhebung" },

  "appearance.help.scheme":      { "text": "native behält die Farben Ihres Terminals · Enter zum Durchsuchen" },
  "appearance.help.prose_width": { "text": "Fließtext bei N Spalten umbrechen · 0 = volle Breite" },
  "appearance.help.compact":     { "text": "Leerzeile zwischen den Zügen weglassen" },
  "appearance.help.thread_preview": { "text": "den markierten Thread in ^J zeigen" },
  "appearance.help.foreign_sessions": { "text": "in ^J zeigen, um hier weiterzumachen" },
  "appearance.help.syntax":      { "text": "Codeblöcke nach Sprache einfärben" },

  "sandbox.read_paths":  { "text": "Auch lesbar" },
  "sandbox.write_paths": { "text": "Auch beschreibbar" },
  "sandbox.deny_paths":  { "text": "Maskiert" },
  "sandbox.ports":       { "text": "Erlaubte Ports" },
  "sandbox.hosts":       { "text": "Erlaubte Hosts" },
  "sandbox.memory":      { "text": "Speicher (MB)" },
  "sandbox.procs":       { "text": "Prozesse" },
  "sandbox.scan_depth":  { "text": "Suchtiefe für Geheimnisse" },

  "sandbox.help.read_paths":  { "text": "zusätzliche Pfade, die ein Befehl lesen darf, über den obigen Bereich hinaus · für Abhängigkeiten außerhalb des Arbeitsbereichs" },
  "sandbox.help.write_paths": { "text": "bewusst getrennt vom Lesen: Schreibrechte sind eine andere Entscheidung" },
  "sandbox.help.deny_paths":  { "text": "auch innerhalb des obigen Bereichs ausgenommen · z. B. ~/.cargo/credentials. Zugangsdaten werden ohnehin maskiert." },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "leer = jeder öffentliche Host · github.com genau · *.github.com nur Subdomains" },
  "sandbox.help.memory":      { "text": "0 = keine Grenze" },
  "sandbox.help.procs":       { "text": "0 = keine Grenze · stoppt Fork-Bomben" },
  "sandbox.help.scan_depth":  { "text": "0 = nur das Wurzelverzeichnis · 3 findet services/*/.env · jede Ebene kostet stat()-Aufrufe bei jedem Befehl" },

  "common.on":     { "text": "ein" },
  "common.off":    { "text": "aus" },
  "common.custom": { "text": "Benutzerdefiniert" }
})JSON";

}  // namespace agentty::i18n::catalogs
