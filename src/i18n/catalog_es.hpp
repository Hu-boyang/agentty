// agentty::i18n——西班牙语。
//
// "ordinary"拉丁语目录的第一个：两个复数形式，
// 单-width脚本，～1.2x英语。结构上没有什么新的东西，
// 这就是重点——德国人和中国人建立了极端，而这五个人则
// 证实了中间的情况是平静的。
//
// TRANSLATION NOTES:
//
//   * "Sandbox" 保留。西班牙语技术写作借用了它； "caja de arena"
//     是没有人为此使用的直译。
//
//   * tú，未使用。 agentty是开发者工具，注册在
//     Spanish-language开发文档是非正式的——"usted"将
//     读作银行。
//
//   * 切换值是sí/no而不是activado/desactivado：该行是
//     四列宽且长的表格不适合。 startup.cpp 的预算是这样说的（
//     common.on，width_hint 2）。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kSpanish = R"JSON({
  "_meta": { "lang": "es", "plurals": ["one", "other"] },

  "lang.picker.title":   { "text": "Idioma" },
  "lang.picker.help":    { "text": "las etiquetas propias de agentty · el modelo responde en el idioma en que le escribas" },
  "lang.auto":           { "text": "Automático (del sistema)" },
  "lang.incomplete":     { "text": "{pct}% traducido" },

  "appearance.theme":        { "text": "Tema" },
  "appearance.scheme":       { "text": "Esquema" },
  "appearance.color":        { "text": "Color" },
  "appearance.layout":       { "text": "Diseño" },
  "appearance.prose_width":  { "text": "Ancho del texto" },
  "appearance.compact":      { "text": "Turnos compactos" },
  "appearance.thread_preview": { "text": "Vista previa del hilo" },
  "appearance.foreign_sessions": { "text": "Sesiones de Codex y Claude" },
  "appearance.motion":       { "text": "Movimiento" },
  "appearance.content":      { "text": "Contenido" },
  "appearance.syntax":       { "text": "Resaltado de sintaxis" },

  "appearance.help.scheme":      { "text": "native conserva los colores de tu terminal · Enter para explorar" },
  "appearance.help.prose_width": { "text": "ajustar el texto a N columnas · 0 = todo el ancho" },
  "appearance.help.compact":     { "text": "quitar la línea en blanco entre turnos" },
  "appearance.help.thread_preview": { "text": "mostrar el hilo resaltado en ^J" },
  "appearance.help.foreign_sessions": { "text": "listarlas en ^J para seguir aquí" },
  "appearance.help.syntax":      { "text": "colorear los bloques de código por lenguaje" },

  "sandbox.read_paths":  { "text": "También legible" },
  "sandbox.write_paths": { "text": "También escribible" },
  "sandbox.deny_paths":  { "text": "Ocultos" },
  "sandbox.ports":       { "text": "Puertos permitidos" },
  "sandbox.hosts":       { "text": "Hosts permitidos" },
  "sandbox.memory":      { "text": "Memoria (MB)" },
  "sandbox.procs":       { "text": "Procesos" },
  "sandbox.scan_depth":  { "text": "Profundidad de búsqueda" },

  "sandbox.help.read_paths":  { "text": "rutas adicionales que un comando puede leer, más allá del alcance de arriba · para dependencias fuera del espacio de trabajo" },
  "sandbox.help.write_paths": { "text": "separado de la lectura a propósito: conceder escritura es otra decisión" },
  "sandbox.help.deny_paths":  { "text": "excluidos incluso dentro del alcance de arriba · p. ej. ~/.cargo/credentials. las credenciales se ocultan igualmente." },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "vacío = cualquier host público · github.com exacto · *.github.com solo subdominios" },
  "sandbox.help.memory":      { "text": "0 = sin límite" },
  "sandbox.help.procs":       { "text": "0 = sin límite · detiene las fork bombs" },
  "sandbox.help.scan_depth":  { "text": "0 = solo la raíz del espacio de trabajo · 3 encuentra services/*/.env · cada nivel cuesta llamadas a stat() en cada comando" },

  "common.on":     { "text": "sí" },
  "common.off":    { "text": "no" },
  "common.custom": { "text": "Personalizado" }
})JSON";

}  // namespace agentty::i18n::catalogs
