// lazy_image_test 装入a 线程 不得解码其图像,
// 而且必须仍然是 字节-for-byte 纠正任何要求他们的时刻。
//
// 图像是大块头 线程 磁盘上(一个实数 29) MB 线程 这儿是17号
// MB 图像 载荷 并解码他们 加载 时间是纯粹的浪费: 渲染 路径
// 从不读取图像 字节 仅限大小 所以 ImageContent 推迟
// 按需加载 改为第一个 bytes() 调用.
//
// 优化只有在隐形的情况下才值钱。
// 这些测试将使其隐形的属性定点:
//
//   1. a 已保存 → 已加载 图像生成 ORIGINAL 字节 (blob和遗产)
//      内含-base64形式相同;
//   2. 单装货 不 按需加载 (整个问题),
//   3. 一份非物质化图像的副本仍然正确无误地解决,
//   4. 关于 -saving 一个非物质化的图像保存 载荷 - 电话
//      危险的,因为一个天真作家 会串连空的 字节 并
//      悄悄地摧毁了 每一个图像 线程 下一个自动保存,
//   5. 缺失的块会退化为空而非扔,匹配什么
//      旧的 提前加载 装填机的

#include "agtest.hpp"

#include <agentty/domain/conversation.hpp>
#include <agentty/io/persistence.hpp>
#include <agentty/util/base64.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

// 定型假-binaryXX载荷——包括无效的字节XXXX/UTF-8,
// 因此这条路径是弦化而不是把它当作字节断裂.
std::string payload(std::size_t n, unsigned seed = 1) {
    std::string s;
    s.reserve(n);
    unsigned x = seed * 2654435761u + 1;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        s.push_back(static_cast<char>(x >> 24));
    }
    return s;
}

Thread thread_with_image(const std::string& bytes) {
    Thread t;
    t.id = ThreadId{"lazyimg" + std::to_string(bytes.size())};
    Message m;
    m.role = Role::User;
    m.text = "look at this";
    m.images.push_back(ImageContent{"image/png", bytes});
    t.messages.push_back(std::move(m));
    return t;
}

} // 命名空间结束。

TEST_CASE("lazy image: save → load round-trips the exact bytes") {
    const std::string original = payload(64u * 1024u);
    Thread t = thread_with_image(original);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 1);
    REQUIRE(loaded->messages[0].images.size() == 1);

    const auto& img = loaded->messages[0].images[0];
    CHECK(img.media_type == "image/png");
    CHECK(img.bytes() == original);
}

TEST_CASE("lazy image: loading does NOT materialise the bytes") {
    const std::string original = payload(32u * 1024u, 7);
    Thread t = thread_with_image(original);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());
    const auto& img = loaded->messages[0].images[0];

    // 该 更改的点: 还没有解码。
    CHECK(!img.materialised());
    CHECK(!img.source().blob.empty());

    // 问是何以付之——当我们这样做时,是正确的.
    CHECK(img.bytes() == original);
    CHECK(img.materialised());
    // 幂等 : 秒数 调用 返回相同的内容 字节 ,不是一个 -read.
    CHECK(img.bytes() == original);
}

TEST_CASE("lazy image: a copy of an unmaterialised image still resolves") {
    const std::string original = payload(16u * 1024u, 3);
    Thread t = thread_with_image(original);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());

    // 线程 自由复制周围(检查点、叉、视图) 缓存).
    // 一份拷贝 之前 按需加载 一定要带着源头
    ImageContent copy = loaded->messages[0].images[0];
    CHECK(!copy.materialised());
    CHECK(copy.bytes() == original);
}

TEST_CASE("lazy image: re-saving an unmaterialised image keeps the payload") {
    // 危险的案子,每转弯都会自动保存 线程 ;
    // 如果作者通过读取其(空)来序列化一个非物质化的图像 字节 ,
    // 第一个 保存 切换后,会擦拭每张图像 线程.
    const std::string original = payload(48u * 1024u, 11);
    Thread t = thread_with_image(original);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());
    CHECK(!loaded->messages[0].images[0].materialised());

    // 保存 直冲回来 无需 曾经触摸 bytes().
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), *loaded);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto again = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(again.has_value());
    REQUIRE(again->messages[0].images.size() == 1);
    CHECK_MESSAGE(again->messages[0].images[0].bytes() == original,
                  "re-saving a never-materialised image must preserve it");
}

TEST_CASE("lazy image: legacy inline base64 still loads") {
    // 线程 在blob 商店载货前写 `data` (base64 ) 内线。
    // 他们必须继续工作,懒惰,没有移民。
    const std::string original = payload(4096, 5);
    Thread t;
    t.id = ThreadId{"lazyimg_legacy"};
    Message m;
    m.role = Role::User;
    m.text = "legacy";
    t.messages.push_back(std::move(m));
    // 种子 a LEGACY 整体 -document 直接存档。
    // save_thread ()现在迁移到日志格式,并退出。
    // JSON ,因此它不能再用来制造这个测试所涉及的旧形状.
    const auto seed_path = persistence::threads_dir() / (t.id.value + ".json");
    {
        auto j = persistence::thread_meta_to_json(t);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& mm : t.messages)
            arr.push_back(persistence::message_to_json(mm));
        j["messages"] = std::move(arr);
        persistence::write_json_atomic(::agentty::IoAccess::grant(), seed_path, j.dump(2));
    }

    // 手 -write 将遗迹形状变成 已保存 文档。
    const auto path = persistence::threads_dir() / (t.id.value + ".json");
    std::string doc;
    {
        std::ifstream in(path, std::ios::binary);
        doc.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
    }
    const std::string inject =
        "\"images\":[{\"data\":\"" + util::base64_encode(original)
        + "\",\"media_type\":\"image/png\"}],";
    const auto at = doc.find("\"role\"");
    REQUIRE(at != std::string::npos);
    doc.insert(at, inject);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << doc;
    }

    auto loaded = persistence::load_thread_file(::agentty::IoAccess::grant(), path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages[0].images.size() == 1);
    const auto& img = loaded->messages[0].images[0];
    CHECK(!img.materialised());          // 仍然 惰性 ,仅从 base64
    CHECK(img.bytes() == original);
}

TEST_CASE("lazy image: a legacy inline image migrates to a blob on save") {
    // 惰性 单装不会缩小遗产 线程 17个 MB 备注
    // base64 仍然以 JSON 解析器 每一张 加载。
    // 保存将其迁移到blob商店 一次 ,然后 线程 文件是小的,
    // 永远。 字节 必须在这种移徙中幸存下来。
    const std::string original = payload(8192, 13);
    Thread t;
    t.id = ThreadId{"lazyimg_migrate"};
    Message m;
    m.role = Role::User;
    m.text = "legacy";
    t.messages.push_back(std::move(m));
    // 种子 a LEGACY 整体 -document 直接存档(见上文注释)。
    const auto path = persistence::threads_dir() / (t.id.value + ".json");
    {
        auto j = persistence::thread_meta_to_json(t);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& mm : t.messages)
            arr.push_back(persistence::message_to_json(mm));
        j["messages"] = std::move(arr);
        persistence::write_json_atomic(::agentty::IoAccess::grant(), path, j.dump(2));
    }

    std::string doc;
    {
        std::ifstream in(path, std::ios::binary);
        doc.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
    }
    const std::string b64 = util::base64_encode(original);
    const std::string inject =
        "\"images\":[{\"data\":\"" + b64 + "\",\"media_type\":\"image/png\"}],";
    const auto at = doc.find("\"role\"");
    REQUIRE(at != std::string::npos);
    doc.insert(at, inject);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << doc;
    }
    const auto legacy_size = fs::file_size(path);

    // 加载(惰性)然后是保存直回——每转弯自动保存.
    auto legacy = persistence::load_thread_file(::agentty::IoAccess::grant(), path);
    REQUIRE(legacy.has_value());
    CHECK(!legacy->messages[0].images[0].materialised());
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), *legacy);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    // 电话 保存 重订 线程 输入 LOG 格式,
    // 因此遗留文档已删除, 载荷 现在住在小店里
    const auto log_path = persistence::threads_dir() / (t.id.value + ".jsonl");
    REQUIRE(fs::exists(log_path));
    CHECK_FALSE(fs::exists(path));

    std::string after;
    {
        std::ifstream in(log_path, std::ios::binary);
        after.assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
    }
    CHECK_MESSAGE(after.find(b64) == std::string::npos,
                  "the inline base64 must be replaced by a blob reference");
    CHECK_MESSAGE(fs::file_size(log_path) < legacy_size,
                  "and the thread file must shrink by the payload size");

    // 图像仍然读回 字节-for-byte.
    auto migrated = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(migrated.has_value());
    REQUIRE(migrated->messages[0].images.size() == 1);
    CHECK(migrated->messages[0].images[0].bytes() == original);
}

TEST_CASE("lazy image: a missing blob degrades to empty, never throws") {
    // 匹配 提前加载 装填器的旧行为 corrupt/absent 载荷:
    // 空 字节 ,每个 传输 路径已跳过。
    auto img = ImageContent::lazy(
        "image/png", ImageContent::Source{.blob = "definitely-not-a-real-blob",
                                          .b64 = {}});
    CHECK(img.bytes().empty());
}

TEST_CASE("lazy image: many threads reading one unmaterialised image") {
    // bytes() 正在连接并运行 工作线程 线程 : 提供者
    // 传输层 构建 传输 正文 关闭 UI 线程 ,并显示 持久化
    // 编剧 保存 曾经写过两首 `mutable` 字段没有保护,
    // 所以两个读取器没有解决 载荷 比赛在a std::string.
    // 一次就解决了 std::call_once。在 tsan
    // 预设下,这是回归检查;在普通建筑中,它检查每个读者都
    // 看到准确的 字节.
    const std::string original = payload(256u * 1024u, 11);
    Thread t = thread_with_image(original);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());

    // 参考文献中共享的天体, 和 复制件: 两者都是真实形状(
    // 凸轮)。 线程 交接( G), a 线程 复制每转弯)。
    const ImageContent& shared = loaded->messages[0].images[0];
    REQUIRE(!shared.materialised());
    std::vector<ImageContent> copies(4, shared);

    std::atomic<int> wrong{0};
    {
        std::vector<std::jthread> ts;
        for (int i = 0; i < 8; ++i)
            ts.emplace_back([&, i] {
                const auto& b = (i % 2) ? shared.bytes() : copies[i / 2].bytes();
                if (b != original) ++wrong;
            });
    }
    CHECK(wrong.load() == 0);
    CHECK(shared.materialised());
}

TEST_CASE("lazy image: set_bytes on one copy leaves the others alone") {
    // 复制共享一个 存储单元 用于廉价复制;
    // 替换 载荷 不得改变其他副本所见。
    const std::string original = payload(4u * 1024u, 5);
    ImageContent a{"image/png", original};
    ImageContent b = a;
    a.set_bytes("replaced");
    CHECK(a.bytes() == "replaced");
    CHECK(b.bytes() == original);
}
