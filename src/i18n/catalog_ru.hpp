// agentty::i18n——俄语。
//
// 三个-FORM PLURAL案件，以及其在该批次中的原因。迄今为止发布
// 的每种语言都有一种形式（zh、ja）或两种形式（en、de、es、fr）；
// 俄语首先是`one`/`few`/`many`，
// 这就是`"%d file(s)"`无法表达的情况，也是本项目不使用gettext
// 的运行时-evaluated规则的原因。
//
//     1 файл 1 以 1 结尾，但 不 11
//     2 файла 少数以 2-4 结尾，
//     但 不 12-14 5 файлов
//     许多其他一切 21 файл 又一——最后 DIGIT 决定
//
// 目录尚未携带复数字符串（34个id中没有一个被算作名词），
// 因此目前尚未针对真实的俄语文本进行测试。
// SELECTOR 经过彻底测试 - 每个整数 0..
// 1000 都针对 i18n_test 中独立编写的规则 - 这
// 是错误的一半。当第一个计数的字符串到达​​时，
// `_meta.plurals` 告诉译者他们欠三种形式。
//
// 也是第一CYRILLIC 目录。西里尔字母是 single-width （已验证：
// string_width("при")==3)，因此它的布局行为类似于
// 拉丁语——但它运行英语的约1.3倍，接近德语，因此宽度预算很重要。
//
// TRANSLATION NOTES:
//
//   * «песочница» 是安全领域中 沙箱 的既定俄语术语
//     书写，非借用。用于帮助文本中； 面板 标题保留为英文，
//     因为它是那里的专有名词。
//
//   * ты，不是 вы。 Dev-tool 寄存器，与西班牙语和法语相同 调用。
//
//   * 拉丁语技术术语（https、git-ssh、dns、stat()、fork）保留拉丁语 --
//     这就是它们在俄语技术散文中的写法。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kRussian = R"JSON({
  "_meta": { "lang": "ru", "plurals": ["one", "few", "many"] },

  "lang.picker.title":   { "text": "Язык" },
  "lang.picker.help":    { "text": "собственные подписи agentty · модель отвечает на том языке, на котором ты пишешь" },
  "lang.auto":           { "text": "Автоматически (система)" },
  "lang.incomplete":     { "text": "переведено на {pct}%" },

  "appearance.theme":        { "text": "Оформление" },
  "appearance.scheme":       { "text": "Схема" },
  "appearance.color":        { "text": "Цвет" },
  "appearance.layout":       { "text": "Макет" },
  "appearance.prose_width":  { "text": "Ширина текста" },
  "appearance.compact":      { "text": "Компактно" },
  "appearance.thread_preview": { "text": "Предпросмотр диалога" },
  "appearance.foreign_sessions": { "text": "Сессии Codex и Claude" },
  "appearance.motion":       { "text": "Анимация" },
  "appearance.content":      { "text": "Содержимое" },
  "appearance.syntax":       { "text": "Подсветка синтаксиса" },

  "appearance.help.scheme":      { "text": "native сохраняет цвета твоего терминала · Enter для просмотра" },
  "appearance.help.prose_width": { "text": "переносить текст на N колонках · 0 = вся ширина" },
  "appearance.help.compact":     { "text": "убрать пустую строку между ходами" },
  "appearance.help.thread_preview": { "text": "показывать выбранный диалог в ^J" },
  "appearance.help.foreign_sessions": { "text": "показывать в ^J, чтобы продолжить здесь" },
  "appearance.help.syntax":      { "text": "раскрашивать блоки кода по языку" },

  "sandbox.read_paths":  { "text": "Также читаемо" },
  "sandbox.write_paths": { "text": "Также записываемо" },
  "sandbox.deny_paths":  { "text": "Скрыто" },
  "sandbox.ports":       { "text": "Открытые порты" },
  "sandbox.hosts":       { "text": "Разрешённые хосты" },
  "sandbox.memory":      { "text": "Память (МБ)" },
  "sandbox.procs":       { "text": "Процессы" },
  "sandbox.scan_depth":  { "text": "Глубина поиска секретов" },

  "sandbox.help.read_paths":  { "text": "дополнительные пути, которые команда может читать, сверх области выше · для зависимостей вне рабочей папки" },
  "sandbox.help.write_paths": { "text": "намеренно отделено от чтения: доступ на запись — отдельное решение" },
  "sandbox.help.deny_paths":  { "text": "исключено даже внутри области выше · напр. ~/.cargo/credentials. учётные данные скрываются в любом случае." },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "пусто = любой публичный хост · github.com точно · *.github.com только поддомены" },
  "sandbox.help.memory":      { "text": "0 = без ограничения" },
  "sandbox.help.procs":       { "text": "0 = без ограничения · останавливает fork-бомбы" },
  "sandbox.help.scan_depth":  { "text": "0 = только корень рабочей папки · 3 находит services/*/.env · каждый уровень стоит вызовов stat() на каждой команде" },

  "common.on":     { "text": "вкл" },
  "common.off":    { "text": "выкл" },
  "common.custom": { "text": "Своё" }
})JSON";

}  // namespace agentty::i18n::catalogs
