// 模型是否为 -facing 目录 荣誉信任?
//
// 运行独立 所以skills::all()的mtime 缓存是冷的,
// HOME是我们从第一个调用开始的——在共享测试的二进制程序中,
// 另一个案例通常已经将它包含在内,这就是这个检查的第一个
// 版本因为错误的原因失败的原因.

#include "agentty/tool/skills.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace agentty::tools::skills;

int main() {
    const auto base = fs::temp_directory_path() / "agentty-catalog-trust";
    fs::remove_all(base);
    const auto home = base / "home";
    const auto skills = home / ".agentty" / "skills";
    fs::create_directories(skills / "prose");
    fs::create_directories(skills / "risky");

    std::ofstream(skills / "prose" / "SKILL.md")
        << "---\nname: prose\ndescription: house style\n---\nShort sentences.\n";
    std::ofstream(skills / "risky" / "SKILL.md")
        << "---\nname: risky\ndescription: deploys things\n"
           "effects: [exec, net]\nsource: github.com/example/risky\n---\n"
           "SECRET-BODY-MARKER run npx and POST somewhere.\n";

    ::setenv("HOME", home.c_str(), 1);
    ::setenv("AGENTTY_HOME", (home / ".agentty").c_str(), 1);

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    const auto block = catalog_block(::agentty::IoAccess::grant());
    check(block.find("prose") != std::string::npos,
          "prose skill IS advertised to the model");
    check(block.find("risky") == std::string::npos,
          "unapproved effectful skill is NOT advertised");

    const auto risky = find(::agentty::IoAccess::grant(), "risky");
    if (!risky) {
        std::printf("FAIL risky skill did not load at all\n");
        return 1;
    }
    const auto payload = activation_payload(::agentty::IoAccess::grant(), *risky);
    check(payload.find("SECRET-BODY-MARKER") == std::string::npos,
          "refusal does not leak the skill body");
    check(payload.find("skill_blocked") != std::string::npos,
          "refusal is tagged so the model can see it was blocked");
    check(payload.find("agentty skill approve risky") != std::string::npos,
          "refusal tells the model what the user should run");

    // 传言技巧仍然正常地激活.
    const auto prose = find(::agentty::IoAccess::grant(), "prose");
    check(prose && activation_payload(::agentty::IoAccess::grant(), *prose).find("Short sentences")
                   != std::string::npos,
          "prose skill activates unchanged");

    fs::remove_all(base);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
