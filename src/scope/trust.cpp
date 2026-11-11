// trust.cpp - 电话 持久化 + 散开一半的瞄准镜信托店.
//
// 从 scope.cpp 纯代数从没有被引出 SHA 或 256)或
// nlohmann/json ——瘦身独立. scope/skills/commands
// 测试 链接 仅限 scope.cpp。这个 TU 被整个拉动 二进制程序
// 和专注的信任测试。
//
// 店里住着一个 USER 根 (正数) util::user_root(): ~/.agentty ,
// 或 ,或 $AGENTTY_HOME 当他们被制服的时候, approvals_path
// 由内容散列键入,所以是克隆 仓库 既不能为自己写一个批准书,也不能在命令后保留
// 一个有效文件 字节 更改(MCPoison / ) CVE -2025-
// 54136 重击 -gate).

#include "agentty/scope/scope.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

#include "agentty/auth/auth.hpp"        // auth::sha256_hex
#include "agentty/io/shared_file.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include <nlohmann/json.hpp>

namespace agentty::scope {

std::string content_hash(std::string_view bytes) noexcept {
    // 重用认证 SHA -256(原始钩子的审批店使用),
    // 所以信任身份在两种内容之间一致 -hash 大门
    return agentty::auth::sha256_hex(std::string{bytes});
}

namespace {
// 批准书在 USER 根 - 切换 util::user_root()
// (~/.agentty ,或 ,或 $AGENTTY_HOME ——从不为项目,
// 所以是一个克隆人. 仓库 无法为自己写一个批准书。 user_root
// () 而不是加入 $HOME + dir_name(Dialect::Agentty
// ) 使信任成为了 FOLLOW 迁移的商店: AGENTTY_HOME 装
// 好钩子 MCP 技能审批权在以下地区 凭据 他们的大门,而不是留在
// 真正的 ~/.agentty ,在每一个迁移运行中丢失它们的地方(每个
// 抓痕运行 -prompted 和关于 -approved 输入/tmp)。
// 默认解决方式相同 : user_root ( ) 系 , ( ) $HOME/.agentty
// 空( 失败) -closed 既无真正的家,也无真正的家 $AGENTTY_HOME.
namespace {
std::vector<std::string> parse_list(const std::optional<std::string>& bytes) {
    std::vector<std::string> out;
    if (!bytes) return out;
    try {
        auto doc = nlohmann::json::parse(*bytes, nullptr, /*投掷=*/false);
        if (doc.is_array())
            for (const auto& v : doc)
                if (v.is_string()) out.emplace_back(v.get<std::string>());
    } catch (...) { out.clear(); }
    return out;
}

void merge_into(std::vector<std::string>& into, std::vector<std::string> add) {
    for (auto& s : add)
        if (std::ranges::find(into, s) == into.end()) into.push_back(std::move(s));
}
}  // 命名空间结束。

// 商店的住址, 以及它曾经的( 平面用户根)。
// 当没有安全家时空出路径。
struct ApprovalPaths { fs::path dest, legacy; };

[[nodiscard]] ApprovalPaths approvals_paths(std::string_view leaf) {
    const char* override = std::getenv("AGENTTY_HOME");
    // 无法关闭, 没有家, 没有覆盖 : util::user_root
    // () 否则将回到 $PWD/.agentty 并将批准放入
    // 任意目录 agentty 可能是一个项目,确切的说 仓库
    // -vouches-for-itself 危险, 商店要停止。
    if (util::home_dir_or_empty().empty() && !(override && *override))
        return {};
    const fs::path root = util::user_root();
    if (root.empty()) return {};

    // state/ (或 减 减 ) $AGENTTY_STATE_DIR : 机器 -written ,永远不要手 -edited.
    const fs::path dir = util::user_state_dir();
    if (dir.empty()) return {};
    return {dir / leaf, root / leaf};
}
}  // 命名空间结束。

Approvals load_approvals(std::string_view leaf) noexcept {
    // 锁-free : 呼叫者包括: 锁 正文 (单位:千美元) MCP 连接圆形。
    // 旧的平面文件也被读取,所以,在下一个文件之前,批准仍然数到 保存
    // 归并 让他们进来吧
    Approvals a;
    const auto [dest, legacy] = approvals_paths(leaf);
    if (dest.empty()) return a;
    // 失败 CLOSED : 任意 解析 问题 – 空批准 – 项目配置 保留
    // 待处理 永远不要放弃这个 noexcept 信任路径。
    try {
        a.shas = parse_list(persistence::peek_file(dest));
        merge_into(a.shas, parse_list(persistence::peek_file(legacy)));
    } catch (...) { a.shas.clear(); }
    return a;
}

bool save_approvals(Io io, const lock_levels::below_file_lane& cap, std::string_view leaf,
                    const Approvals& a) noexcept {
    const auto [dest, legacy] = approvals_paths(leaf);
    if (dest.empty()) return false;
    // 调 -read + 合并 + 写入一节。批准是增长 -only
    // 内容散列( 无撤销 ) , 因此结合是正确的合并 :
    // 两种程序同时批准不同的事物 两者都保留着它们
    // 旧平面文件在此折叠并移除,置于同值的锁下.
    try {
        return persistence::Update::Failed != persistence::update_shared(io, cap, dest,
            {.durability = persistence::Durability::Durable},
            [](std::optional<std::string> cur, std::vector<std::string> add, std::string old)
                -> std::optional<std::string> {
                auto merged = parse_list(cur);
                merge_into(merged, std::move(add));
                merge_into(merged, parse_list(persistence::peek_file(old)));
                std::error_code rm;
                fs::remove(old, rm);
                return nlohmann::json(merged).dump(2);
            }, a.shas, legacy.string());
    } catch (...) { return false; }
}

}  // namespace agentty::scope
