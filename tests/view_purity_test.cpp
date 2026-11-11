// view() 是一个纯函数 Model 这条线说,
// 是一条被打破的路。
//
// 面板读作 `app::deps().load_settings()`
// 来构建其行。症状不是崩溃: 相同的 Model 画两个不同的 帧
// 取决于商店的状态 调用 时间,这也使得 帧 缓存 不当的 -
// visual_hash 总结 MODEL ,所以,我说 maya
// 有权跳过 重绘 隐藏的输入方式改变。
//
// 格氏体是测试的正确形状: 属性是 "no view TU reaches the seam" ,
// 没有拨打次数 view() 能够证明。
#include "agtest.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// 脱/和/*** /所以在散文中提及不会不合格测试.
[[nodiscard]] std::string without_comments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool line_c = false, block_c = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (line_c) { if (src[i] == '\n') { line_c = false; out += '\n'; } continue; }
        if (block_c) { if (src.compare(i, 2, "*/") == 0) { block_c = false; ++i; } continue; }
        if (src.compare(i, 2, "//") == 0) { line_c = true; continue; }
        if (src.compare(i, 2, "/*") == 0) { block_c = true; ++i; continue; }
        out += src[i];
    }
    return out;
}

} // 命名空间结束。

TEST_CASE("view: no view TU reads through the Deps seam") {
    const fs::path root = fs::path{AGENTTY_SRC_ROOT} / "src" / "runtime" / "view";
    REQUIRE(fs::exists(root));

    std::vector<std::string> offenders;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
        std::ifstream f(e.path());
        const std::string src{std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>()};
        const std::string code = without_comments(src);
        if (code.find("deps()") != std::string::npos)
            offenders.push_back(e.path().filename().string());
    }

    std::string msg = "view TUs must read the Model, not deps(): ";
    for (const auto& o : offenders) { msg += o; msg += ' '; }
    CHECK_MESSAGE(offenders.empty(), msg);
}
