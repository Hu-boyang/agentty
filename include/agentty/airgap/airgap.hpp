#pragma once
// `agentty airgap` — 通过 SSH 反向隧道在 air
// -gapped 主机上运行 agentty。  请参阅 README 和
// 完整 UX 的实现；这里的合同是：解析子命令参数，可以选择复制凭据，
// 然后执行到ssh。  成功后，该进程将被 ssh 取代，并且永远不会返回。

namespace agentty::airgap {

// `argc`/`argv` 从 `airgap` 子命令标记*之后的 args 开始，
// 因此 argv[0] 是第一个用户-supplied flag/positional.
int cmd_airgap(int argc, char** argv);

} // namespace agentty::airgap
