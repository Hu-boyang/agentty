// agentty::i18n—简体中文目录。
//
// 与德语相反的极端，这就是为什么它是第三。中文是
// the DOUBLE-WIDTH case: 系统调用过滤 is six codepoints and twelve columns, so
// 每个计算字符而不是列的宽度计算在这里都是错误的两倍
// ——并且在危险的方向上是错误的，因为它UNDER
// -measures并且让溢出的行通过。
//
// 这就是为什么 lint 的宽度预算（门 4) 免除
// zh/ja/ko:CMake 无法测量显示宽度，并且代码点预算将在每
// 个正确的翻译上触发，同时错过真正的溢出。它们的宽度改为通过 渲染 测试，
// 使用 maya::string_width 进行测量。
//
// 布局上的网络副作用：中文在列中比英文SHORTER——
// 在标签上大约0.6倍——因此它减轻了面板而不是强调它。
// 它施加的压力是在测量代码上，而不是在 帧 上。
//
// TRANSLATION NOTES:
//
//   * "Sandbox" is 沙盒, which is the established term in Chinese technical
//     文字（不是音译）。
//
//   * 中文保留拉丁文的技术名词保留拉丁文：ports are
//     written as 端口 but the protocol names (https, http, git-ssh, dns) and
//     路径不被翻译。 `stat()` 和 `services/* /.env`
//     的英文帮助热线遵循相同的规则。
//
//   * 汉字之间无空格，"·"分隔符保留
//     其周围有空格——这是 zh-Hans UI 文本中的约定，
//     它使帮助行保持可扫描性。
//
//   * 中文有NO复数形式（每n有CLDR类别`other`），所以没有
//     这里的条目携带one/few/many.。
//     lint拒绝zh 目录上的`few`键正是出于这个
//     原因：这将是翻译者误解模式定义，并且运行时永远不会读取它。
//
// 截至本次提交，已针对 en.json 完成，这使得它成为
// 有用的 double-width 测试——部分 目录 回落到
// 英语并隐藏了我们正在测量的东西。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kChineseSimplified = R"JSON({
  "_meta": { "lang": "zh-CN", "plurals": ["other"] },

  "lang.picker.title":   { "text": "语言" },
  "lang.picker.help":    { "text": "agentty 自身的界面文字 · 模型会用您提问时所用的语言回复" },
  "lang.auto":           { "text": "自动（跟随系统）" },
  "lang.incomplete":     { "text": "已翻译 {pct}%" },

  "appearance.theme":        { "text": "主题" },
  "appearance.scheme":       { "text": "配色方案" },
  "appearance.color":        { "text": "颜色" },
  "appearance.layout":       { "text": "布局" },
  "appearance.prose_width":  { "text": "正文宽度" },
  "appearance.compact":      { "text": "紧凑对话" },
  "appearance.thread_preview": { "text": "对话预览" },
  "appearance.foreign_sessions": { "text": "Codex 与 Claude 会话" },
  "appearance.motion":       { "text": "动画" },
  "appearance.content":      { "text": "内容" },
  "appearance.syntax":       { "text": "语法高亮" },

  "appearance.help.scheme":      { "text": "native 保留终端自身的颜色 · 按 Enter 浏览" },
  "appearance.help.prose_width": { "text": "正文在 N 列处换行 · 0 = 使用全部宽度" },
  "appearance.help.compact":     { "text": "去掉对话之间的空行" },
  "appearance.help.thread_preview": { "text": "在 ^J 中显示选中的对话" },
  "appearance.help.foreign_sessions": { "text": "在 ^J 中列出，以便在此继续" },
  "appearance.help.syntax":      { "text": "按语言为代码块着色" },

  "sandbox.read_paths":  { "text": "额外可读" },
  "sandbox.write_paths": { "text": "额外可写" },
  "sandbox.deny_paths":  { "text": "已屏蔽" },
  "sandbox.ports":       { "text": "允许的端口" },
  "sandbox.hosts":       { "text": "允许的主机" },
  "sandbox.memory":      { "text": "内存 (MB)" },
  "sandbox.procs":       { "text": "进程数" },
  "sandbox.scan_depth":  { "text": "密钥扫描深度" },

  "sandbox.help.read_paths":  { "text": "命令可读取的额外路径，超出上面的范围设置 · 用于工作区之外的依赖" },
  "sandbox.help.write_paths": { "text": "与可读分开是有意为之：授予写入是另一个决定" },
  "sandbox.help.deny_paths":  { "text": "即使在上面的范围内也会排除 · 例如 ~/.cargo/credentials。凭据文件本来就会被屏蔽。" },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "留空 = 任意公网主机 · github.com 精确匹配 · *.github.com 仅子域名" },
  "sandbox.help.memory":      { "text": "0 = 不限制" },
  "sandbox.help.procs":       { "text": "0 = 不限制 · 可阻止 fork 炸弹" },
  "sandbox.help.scan_depth":  { "text": "0 = 仅工作区根目录 · 3 可找到 services/*/.env · 每增加一层都会让每条命令多出 stat() 调用" },

  "common.on":     { "text": "开" },
  "common.off":    { "text": "关" },
  "common.custom": { "text": "自定义" }
})JSON";

}  // namespace agentty::i18n::catalogs
