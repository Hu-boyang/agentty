// code_block_extract_test ——已关闭 -vs-open 用于拔除围栏 Ctrl+G.
//
// 流式过程中运行代码块的功能只提供结束 ``` 围栏已经
// 已经 CLOSED (运行中一个半) -typed 命令不安全。
// 此针 extract_closed_code_blocks :它必须掉下一个有痕迹的街区,它的栅栏仍然开着,
// 保持每一个封闭的街区,并报告 `had_open` 所以,我们 状态更新函数 说得没错
// "still streaming" 数字 "no blocks".
#include "agtest.hpp"

#include "agentty/runtime/panel/code_blocks.hpp"

#include <string_view>

namespace cbp = agentty::code_blocks;


TEST_CASE("code block extract") {
    std::printf("[code_block_extract]\n");

    // 1. 封闭区块,然后是 OPEN 一条( 仍然) 流式传输 : 仅限
    //    提供封闭区块,并设置had_open.
    {
        std::string_view s =
            "run this:\n```sh\necho one\n```\nnow watch:\n```sh\necho two";
        bool open = false;
        auto b = cbp::extract_closed_code_blocks(s, &open);
        CHECK(b.size() == 1, "one closed block offered (trailing open dropped)");
        CHECK(open, "had_open flags the still-streaming block");
        if (!b.empty())
            CHECK(b[0].body.find("echo one") != std::string::npos,
                  "the closed block is the first one");
    }

    // 2. 所有围栏关闭:每个街区都开放, had_open 假的,我说,我...
    {
        std::string_view s = "```sh\necho a\n```\n```sh\necho b\n```\n";
        bool open = true;
        auto b = cbp::extract_closed_code_blocks(s, &open);
        CHECK(b.size() == 2, "both closed blocks offered");
        CHECK(!open, "had_open false when nothing is mid-fence");
    }

    // 3. 仍为-opening区块(尚未关闭):没有提议,但
    //    had_open 是真的,所以打电话的人说 "still streaming".
    {
        std::string_view s = "here you go:\n```bash\nsudo systemctl restart x";
        bool open = false;
        auto b = cbp::extract_closed_code_blocks(s, &open);
        CHECK(b.empty(), "no block offered while its fence is open");
        CHECK(open, "had_open true for the lone mid-fence block");
    }

    // 4. 完全没有围栏的保质保质:空的, had_open 假的,我说,我...
    {
        std::string_view s = "just some prose, no code here at all.\n";
        bool open = true;
        auto b = cbp::extract_closed_code_blocks(s, &open);
        CHECK(b.empty(), "no blocks in plain prose");
        CHECK(!open, "had_open false with no fences");
    }

    // 5. 蒂尔德围栏关闭正确。
    {
        std::string_view s = "~~~sh\necho tilde\n~~~\n";
        bool open = true;
        auto b = cbp::extract_closed_code_blocks(s, &open);
        CHECK(b.size() == 1, "tilde-fenced closed block offered");
        CHECK(!open, "tilde close detected");
    }

    // 6. extract_closed 永远不减产 MORE 高于 extract_code_blocks.
    {
        std::string_view s = "```sh\na\n```\n```sh\nb";   // 一个闭口,一个开口
        auto all    = cbp::extract_code_blocks(s);
        auto closed = cbp::extract_closed_code_blocks(s);
        CHECK(all.size() == 2, "raw extract keeps the open trailing block");
        CHECK(closed.size() == 1, "closed extract drops the open one");
        CHECK(closed.size() < all.size(), "closed <= raw, strictly here");
    }
}
