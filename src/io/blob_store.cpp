// agentty::blobs —内容-addressed 载荷存储。参见 blob_store.hpp。

#include "agentty/io/blob_store.hpp"

#include "agentty/io/persistence.hpp"   // threads_dir，write_json_atomic

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace agentty::blobs {

namespace fs = std::filesystem;

fs::path dir() {
    auto p = persistence::threads_dir() / "blobs";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

std::string name_for(std::string_view bytes) {
    // FNV-1a超过字节。非加密 — 这命名为本地
    // 缓存 条目，它不验证任何内容。 64 位超过
    // 几千个 载荷 是远远低于磁盘自身错误率的碰撞概率，
    // 并且长度被混合到名称中作为廉价的第二
    // 维度。
    //
    // 确切的格式("%016llx-%zx")是加载-bearing：磁盘
    // 上已有的每个blob都由它命名，因此改变它会孤立每个现有线程中的
    // 每个图像。不要"clean this up"。
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llx-%zx",
                  static_cast<unsigned long long>(h), bytes.size());
    return buf;
}

std::string put(const std::string& bytes) {
    if (bytes.empty()) return {};
    const std::string n = name_for(bytes);
    const fs::path out = dir() / n;
    std::error_code ec;
    // Content-addressed：如果它已经存在，则通过构造它是字
    // 节-identical，因此重写它将是纯粹的I/O，没有任何改变。
    if (fs::exists(out, ec)) {
        // 刷新时间：GC仅回收早于其宽限窗口的斑点，
        // 因此飞行中的保存重新-referenced的斑点必须看起来新鲜，
        // 即使其字节是旧的。
        fs::last_write_time(out, fs::file_time_type::clock::now(), ec);
        return n;
    }
    // 重用原子临时+fsync+重命名写入器：blob
    // 仅被完整发布，
    // 因此mid-write崩溃不能留下指向被截断的载荷的引用。
    if (!persistence::write_json_atomic(::agentty::IoAccess::grant(), out, bytes)) return {};
    return n;
}

std::string get(const std::string& name) {
    if (name.empty()) return {};
    // 保护连接：线程文件是user-writable，
    // 并且精心设计的"../../"名称不得在blob目录之外读取。
    if (name.find('/') != std::string::npos
        || name.find('\\') != std::string::npos
        || name.find("..") != std::string::npos) return {};
    std::ifstream in(dir() / name, std::ios::binary);
    if (!in) return {};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

} // namespace agentty::blobs
