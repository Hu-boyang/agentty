// agentty::i18n——法语。
//
// 该集合中的CLDR例外：法语的`one`类别涵盖i=0，1，因此ZERO
// TAKES 该 SINGULAR——"0 fichier"，
// 而不是"0 fichiers"。这里的所有其他两种-form 语言都将0 放入
// `other` 中。 plural.hpp 通过一次额外的比较而不是第六个
// 规则形状来处理它，并且 i18n_test 中的扫描根据独立编写的规则检查每个整数 0..
// 1000，因此这不能安静地漂移。
//
// 目录中尚不存在复数字符串，因此该规则当前尚未
// 针对真正的法语文本进行测试——但是选择器是，
// 这是可能错误的部分。
//
// TRANSLATION NOTES:
//
//   * 印刷间距。法语在 : 之前加一个空格；！？和里面
//     《》。我们在冒号前使用 NARROW NO-BREAK
//     SPACE (U+202F)，这是正确的字符，渲染 作为一列。
//     允许使用普通空格换行，留下以 ":" 开头的行。
//
//   * 编译单元，不是你。与西班牙语的推理相同：dev-tool 寄存器。
//
//   * "Sandbox" 保留——"bac à sable" 是直译，并且确实
//     出现在法语安全书写中，但是 CLI 标签不是长
//     形式的位置。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kFrench = R"JSON({
  "_meta": { "lang": "fr", "plurals": ["one", "other"] },

  "lang.picker.title":   { "text": "Langue" },
  "lang.picker.help":    { "text": "les libellés propres à agentty · le modèle répond dans la langue que tu utilises" },
  "lang.auto":           { "text": "Automatique (système)" },
  "lang.incomplete":     { "text": "traduit à {pct}%" },

  "appearance.theme":        { "text": "Thème" },
  "appearance.scheme":       { "text": "Palette" },
  "appearance.color":        { "text": "Couleur" },
  "appearance.layout":       { "text": "Mise en page" },
  "appearance.prose_width":  { "text": "Largeur du texte" },
  "appearance.compact":      { "text": "Tours compacts" },
  "appearance.thread_preview": { "text": "Aperçu du fil" },
  "appearance.foreign_sessions": { "text": "Sessions Codex et Claude" },
  "appearance.motion":       { "text": "Animation" },
  "appearance.content":      { "text": "Contenu" },
  "appearance.syntax":       { "text": "Coloration syntaxique" },

  "appearance.help.scheme":      { "text": "native garde les couleurs de ton terminal · Entrée pour parcourir" },
  "appearance.help.prose_width": { "text": "couper le texte à N colonnes · 0 = toute la largeur" },
  "appearance.help.compact":     { "text": "supprimer la ligne vide entre les tours" },
  "appearance.help.thread_preview": { "text": "afficher le fil sélectionné dans ^J" },
  "appearance.help.foreign_sessions": { "text": "les lister dans ^J pour continuer ici" },
  "appearance.help.syntax":      { "text": "colorer les blocs de code selon le langage" },

  "sandbox.read_paths":  { "text": "Aussi lisible" },
  "sandbox.write_paths": { "text": "Aussi modifiable" },
  "sandbox.deny_paths":  { "text": "Masqués" },
  "sandbox.ports":       { "text": "Ports autorisés" },
  "sandbox.hosts":       { "text": "Hôtes autorisés" },
  "sandbox.memory":      { "text": "Mémoire (Mo)" },
  "sandbox.procs":       { "text": "Processus" },
  "sandbox.scan_depth":  { "text": "Profondeur de recherche" },

  "sandbox.help.read_paths":  { "text": "chemins supplémentaires qu'une commande peut lire, au-delà de la portée ci-dessus · pour une dépendance hors de l'espace de travail" },
  "sandbox.help.write_paths": { "text": "séparé de la lecture à dessein : accorder l'écriture est une autre décision" },
  "sandbox.help.deny_paths":  { "text": "exclus même dans la portée ci-dessus · p. ex. ~/.cargo/credentials. les identifiants sont masqués de toute façon." },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "vide = tout hôte public · github.com exact · *.github.com sous-domaines seulement" },
  "sandbox.help.memory":      { "text": "0 = sans limite" },
  "sandbox.help.procs":       { "text": "0 = sans limite · arrête les fork bombs" },
  "sandbox.help.scan_depth":  { "text": "0 = uniquement la racine · 3 trouve services/*/.env · chaque niveau coûte des appels stat() à chaque commande" },

  "common.on":     { "text": "oui" },
  "common.off":    { "text": "non" },
  "common.custom": { "text": "Personnalisé" }
})JSON";

}  // namespace agentty::i18n::catalogs
