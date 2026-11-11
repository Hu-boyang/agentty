// agentty::i18n — 葡萄牙语（巴西）。
//
// 具体来说，pt-BR，而不是pt。两者在对开发工具很重要的词汇上有所不同
// ——"tela" vs "ecrã"，"arquivo" vs
// "ficheiro"——而目录自始至终都使用巴西形式。 parse_tag
// 在这里也映射了 pt-PT，这是一项经过深思熟虑的交易：阅读巴西表格的
// 欧洲葡萄牙语使用者比阅读英语的使用者得到的服务要好得多。 （对比 zh-TW，
// 不 会回退到 zh-CN——请参阅 parse_tag 了解两种情况
// 不同的原因。）
//
// TRANSLATION NOTES:
//
//   * "arquivo"不是"ficheiro"，"tela"不是"ecrã"，"usuário"不是"utilizador"。
//     pt-PT读者将会注意到；他们也会理解每一个字。
//
//   * você，动词变位third-person。那就是中性
//     巴西软件注册，
//     并且它完全回避了tu/você区域分裂。
//
//   * "Sandbox" 保留。 "Caixa de areia" 是直译，内容为
//     作为儿童沙坑的方式与英语完全不同。

#include <string_view>

namespace agentty::i18n::catalogs {

inline constexpr std::string_view kPortugueseBR = R"JSON({
  "_meta": { "lang": "pt-BR", "plurals": ["one", "other"] },

  "lang.picker.title":   { "text": "Idioma" },
  "lang.picker.help":    { "text": "os rótulos do próprio agentty · o modelo responde no idioma em que você escrever" },
  "lang.auto":           { "text": "Automático (do sistema)" },
  "lang.incomplete":     { "text": "{pct}% traduzido" },

  "appearance.theme":        { "text": "Tema" },
  "appearance.scheme":       { "text": "Esquema" },
  "appearance.color":        { "text": "Cor" },
  "appearance.layout":       { "text": "Layout" },
  "appearance.prose_width":  { "text": "Largura do texto" },
  "appearance.compact":      { "text": "Turnos compactos" },
  "appearance.thread_preview": { "text": "Prévia da conversa" },
  "appearance.foreign_sessions": { "text": "Sessões do Codex e Claude" },
  "appearance.motion":       { "text": "Animação" },
  "appearance.content":      { "text": "Conteúdo" },
  "appearance.syntax":       { "text": "Realce de sintaxe" },

  "appearance.help.scheme":      { "text": "native mantém as cores do seu terminal · Enter para explorar" },
  "appearance.help.prose_width": { "text": "quebrar o texto em N colunas · 0 = largura total" },
  "appearance.help.compact":     { "text": "remover a linha em branco entre os turnos" },
  "appearance.help.thread_preview": { "text": "mostrar a conversa selecionada no ^J" },
  "appearance.help.foreign_sessions": { "text": "listar no ^J para continuar aqui" },
  "appearance.help.syntax":      { "text": "colorir os blocos de código por linguagem" },

  "sandbox.read_paths":  { "text": "Também legível" },
  "sandbox.write_paths": { "text": "Também gravável" },
  "sandbox.deny_paths":  { "text": "Ocultos" },
  "sandbox.ports":       { "text": "Portas permitidas" },
  "sandbox.hosts":       { "text": "Hosts permitidos" },
  "sandbox.memory":      { "text": "Memória (MB)" },
  "sandbox.procs":       { "text": "Processos" },
  "sandbox.scan_depth":  { "text": "Profundidade da busca" },

  "sandbox.help.read_paths":  { "text": "caminhos adicionais que um comando pode ler, além do escopo acima · para dependências fora do espaço de trabalho" },
  "sandbox.help.write_paths": { "text": "separado da leitura de propósito: conceder escrita é outra decisão" },
  "sandbox.help.deny_paths":  { "text": "excluídos mesmo dentro do escopo acima · ex. ~/.cargo/credentials. as credenciais são ocultadas de qualquer forma." },
  "sandbox.help.ports":       { "text": "443 https · 80 http · 22 git-ssh · TCP only" },
  "sandbox.help.hosts":       { "text": "vazio = qualquer host público · github.com exato · *.github.com só subdomínios" },
  "sandbox.help.memory":      { "text": "0 = sem limite" },
  "sandbox.help.procs":       { "text": "0 = sem limite · barra fork bombs" },
  "sandbox.help.scan_depth":  { "text": "0 = só a raiz do espaço de trabalho · 3 encontra services/*/.env · cada nível custa chamadas stat() em cada comando" },

  "common.on":     { "text": "sim" },
  "common.off":    { "text": "não" },
  "common.custom": { "text": "Personalizado" }
})JSON";

}  // namespace agentty::i18n::catalogs
