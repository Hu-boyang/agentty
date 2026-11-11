#pragma once
// agentty::Io — 权限 触动世界，作为一种价值。
//
// 执行IO或改变进程-wide状态（读取凭据存储、遍历技能目录、
// 拨号、交换活动提供者）的函数采用`Io`参数。 update() 和
// view() 从来没有一个，所以他们不能 调用 它，他们也不能 调用 任何帮助者，
// 除非该帮助者也被交给了 I/O。
// 这就是 "the reducer reached the disk through a helper"
// 成为 编译 错误而不是 lint miss 的原因。
//
// 只有少数地方制造一个，直到IoAccess：
//   * 副作用 正文:cmd::io_task(runtime/cmd.hpp)和后台作业
//     在util/background.hpp
//   * 运行副作用、main()和工具/提供者入口点的主机
//   *测试
// tests/lint/io_roots.txt列出它们； io_roots lint 在
// 任何其他名为 IoAccess 的文件上失败，并且在列出的不再失败的文件上失败。
//
// 其为空值：可自由传递，无需存储任何内容。它不是可发送的（jaal
// 无法看到其内部），因此它无法将 Msg 放入 Model 中，
// 并且工作是自己创造的，而不是被交给的。

namespace agentty {

class Io {
public:
    Io(const Io&)            = default;
    Io& operator=(const Io&) = default;

private:
    constexpr Io() noexcept = default;
    friend struct IoAccess;
};

struct IoAccess {
    [[nodiscard]] static constexpr Io grant() noexcept { return Io{}; }
};

} // namespace agentty
