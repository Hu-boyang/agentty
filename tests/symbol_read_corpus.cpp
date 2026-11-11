// symbol_read_corpus ——重播真实
// `read symbol=` 无法在其中搜索 已保存 届会的会期 当前 工具。
// 每个实体文件的行是 `<abs path>\t<symbol>` ;
// 缩写 符号 显示多少解析度,并打印出误差。还通过 --verbose
// 以显示每个已解析的跨度的第一行(安全性:应该是定义)。
//   agentty_standalone_tests symbol_read_corpus <corpus.tsv> [--verbose] (中文(简体) ).

#include "agtest_cap.hpp"
#include <cstdio>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

using nlohmann::json;
using namespace agentty;

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: symbol_read_corpus <corpus.tsv> [--verbose]\n"); return 2; }
    const bool verbose = argc > 2 && std::string{argv[2]} == "--verbose";
    tools::util::set_workspace_root(::agentty::IoAccess::grant(), "/");
    const auto read = tools::find("read");
    if (!read) { std::printf("read tool not registered\n"); return 1; }
    std::ifstream in(argv[1]);
    std::string line;
    int total = 0, ok = 0;
    while (std::getline(in, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string path = line.substr(0, tab), sym = line.substr(tab + 1);
        ++total;
        auto r = agentty::tool::DynamicDispatch::execute_with(agtest::cap(), read, read->name.value, json{{"path", path}, {"symbol", sym}}, agentty::tools::CallContext{});
        if (r && r->text.find("no definition of") == std::string::npos) {
            ++ok;
            if (verbose) {
                auto body = r->text;
                auto nl = body.find('\n');
                auto nl2 = nl == std::string::npos ? nl : body.find('\n', nl + 1);
                auto nl3 = nl2 == std::string::npos ? nl2 : body.find('\n', nl2 + 1);
                std::printf("OK   %-28s %s\n", sym.c_str(),
                            body.substr(nl2 == std::string::npos ? 0 : nl2 + 1,
                                        nl3 == std::string::npos ? 100 : std::min<std::size_t>(100, nl3 - nl2 - 1)).c_str());
            }
        } else {
            std::printf("MISS %-28s %s\n", sym.c_str(), path.c_str());
        }
    }
    std::printf("resolved %d / %d\n", ok, total);
    if (total == 0) {
        std::printf("corpus had no usable `<path>\\t<symbol>` lines\n");
        return 1;
    }
    // 每行都是一条 符号 KNOWN 写进文件里,
    // 所以误读就是回归 `read symbol=` 而不是被
    // 打印出来 MISS 并且退出 0 —— 无法失败的探测器
    // 不是测试.
    return ok == total ? 0 : 1;
}
