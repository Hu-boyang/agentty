// agentty::i18n——日语。
//
// SECOND双-width语言，这就是它的工作。
// 中文证明测量路径有效；日本人证明这不是偶然的
// zh-specific. Three scripts mixed in one string (kanji 設定, hiragana かな,
// 片假名 sandobokkusu) 加上拉丁语技术术语，
// 除了拉丁语之外的所有 double-width，这正是天真的宽度实现出错的情况。
//
// 像中文一样：NO复数形式（每个n对应CLDR `other`），
// 所以这里没有条目携带one/few/many.。lint拒绝
// ja 目录上的`few`键，因为运行时永远不会读取它——这
// 是翻译器误读了模式定义，沉默会比构建失败更糟糕。
//
// TRANSLATION NOTES:
//
//   * sandobokkusu (片假名) 为 "sandbox" -- 既定贷款，不是
//     本土造币。
//
//   * 日语字符之间没有空格。 "·" 分隔符保持其
//     围绕空格，因为它位于短语之间，
//     这是日语UI文本中混合拉丁标点符号的惯例。
//
//   *帮助行结束时没有句点。日本 UI 常规微缩副本
//     省略了。关于片段，这些都是片段。
//
//   *～(波浪破折号)是刻意回避的：它与渲染不一致
//     跨终端字体并且具有臭名昭著的Unicode规范化陷阱(U+301C
//     vs U+FF5E)。这里没有什么需要它。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kJapanese = R"JSON({
  "_meta": { "lang": "ja", "plurals": ["other"] },

  "lang.picker.title":   { "text": "言語" },
  "lang.picker.help":    { "text": "agentty 自身のラベル · モデルはあなたが書いた言語で返答します" },
  "lang.auto":           { "text": "自動（システムに従う）" },
  "lang.incomplete":     { "text": "{pct}% 翻訳済み" },

  "appearance.theme":        { "text": "テーマ" },
  "appearance.scheme":       { "text": "配色" },
  "appearance.color":        { "text": "色" },
  "appearance.layout":       { "text": "レイアウト" },
  "appearance.prose_width":  { "text": "本文の幅" },
  "appearance.compact":      { "text": "コンパクト表示" },
  "appearance.thread_preview": { "text": "スレッドのプレビュー" },
  "appearance.foreign_sessions": { "text": "Codex と Claude のセッション" },
  "appearance.motion":       { "text": "アニメーション" },
  "appearance.content":      { "text": "内容" },
  "appearance.syntax":       { "text": "構文強調" },

  "appearance.help.scheme":      { "text": "native は端末自身の色を保ちます · Enter で一覧" },
  "appearance.help.prose_width": { "text": "本文を N 桁で折り返す · 0 = 全幅" },
  "appearance.help.compact":     { "text": "ターンの間の空行を省く" },
  "appearance.help.thread_preview": { "text": "^J で選択中のスレッドを表示" },
  "appearance.help.foreign_sessions": { "text": "^J に表示してここで続ける" },
  "appearance.help.syntax":      { "text": "コードブロックを言語ごとに色分けする" },

  "sandbox.read_paths":  { "text": "追加の読み取り" },
  "sandbox.write_paths": { "text": "追加の書き込み" },
  "sandbox.deny_paths":  { "text": "マスク済み" },
  "sandbox.ports":       { "text": "許可ポート" },
  "sandbox.hosts":       { "text": "許可ホスト" },
  "sandbox.memory":      { "text": "メモリ (MB)" },
  "sandbox.procs":       { "text": "プロセス数" },
  "sandbox.scan_depth":  { "text": "機密の探索深度" },

  "sandbox.help.read_paths":  { "text": "上の範囲設定に加えてコマンドが読み取れるパス · ワークスペース外の依存関係のため" },
  "sandbox.help.write_paths": { "text": "読み取りとは意図的に分けています。書き込みの許可は別の判断です" },
  "sandbox.help.deny_paths":  { "text": "上の範囲内でも除外されます · 例 ~/.cargo/credentials。認証情報はいずれにせよマスクされます" },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "空 = すべての公開ホスト · github.com は完全一致 · *.github.com はサブドメインのみ" },
  "sandbox.help.memory":      { "text": "0 = 制限なし" },
  "sandbox.help.procs":       { "text": "0 = 制限なし · fork 爆弾を止めます" },
  "sandbox.help.scan_depth":  { "text": "0 = ワークスペース直下のみ · 3 で services/*/.env を検出 · 階層ごとに各コマンドで stat() が増えます" },

  "common.on":     { "text": "オン" },
  "common.off":    { "text": "オフ" },
  "common.custom": { "text": "カスタム" }
})JSON";

}  // namespace agentty::i18n::catalogs
