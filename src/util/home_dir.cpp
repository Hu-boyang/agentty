// agentty::util::home_dir - 看见了 头文件 财务报告和审定财务报表 MSYS2 拆分( E) -brain 理由说明。
#include "agentty/util/home_dir.hpp"

#include <cstdlib>

namespace agentty::util {

namespace fs = std::filesystem;

fs::path home_dir() {
    if (auto p = home_dir_or_empty(); !p.empty()) return p;
    return fs::current_path();
}

fs::path home_dir_or_empty() {
    // $HOME先(POSIX/MSYS2/mintty/Cygwin),
    // 后为$USERPROFILE(原第Windows),两者均不设时空——由呼叫者决定这意味着什么.
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home);
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
        return fs::path(up);
    return {};
}

} // namespace agentty::util
