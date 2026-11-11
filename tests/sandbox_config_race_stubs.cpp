// 狭义的沙箱目标共享的积分.
//
// sandbox.cpp 调用 workspace_root (a)
// 建立姿态或布道的参数,而实际定义存在于 fs_helpers.cpp ,
// 包括 mcp 的 fs_helpers 并拉整个工具层 后面。
// 链接 满足一个功能意味着 几百个仪器 编译单元 TSan下
// 测试指针互换,或拖动 auth+mcp+teardown 进入
// 只想要产卵壳的检查.
//
// 所以,我们... 接口边界 买根根根,是的 SETTABLE 而不是硬编码,
// 因为两个用户想要不同的东西:
//
//   sandbox_config_race_test从不产卵;该值无关紧要.
//   sandbox_live_check 绑定 工作区 并读
//                             到里面的植入文件,所以它必须是创建
//                             的检查的真实目录——
//                             一个硬码/tmp会让遮掩的个案对错误路径坚持,
//                             并因错误的理由通过.
//
// 由手宣布,而不是包括 fs_helpers.hpp ,
// 出于同样的原因 -- -- 头文件 即是到达Mcp的。
#include <filesystem>

#include "agentty/util/io.hpp"
#include <string_view>

namespace agentty::tools::util {

namespace {
std::filesystem::path& stub_root() {
    static std::filesystem::path root{"/tmp"};
    return root;
}
}  // 命名空间结束。

const std::filesystem::path& workspace_root() { return stub_root(); }

// 测试 -only 设置器, 匹配真实 set_workspace_root
// 名字这么读 sandbox.cpp 不需要怀疑它得到了哪个。
void set_workspace_root(Io, std::filesystem::path p) { stub_root() = std::move(p); }

}  // namespace agentty::tools::util

namespace agentty::tools::progress {

// subprocess.cpp 报告通过此程序实现的实载输出。
// 事件 ,所以这只须解决。
void emit(std::string_view) {}

}  // namespace agentty::tools::progress
