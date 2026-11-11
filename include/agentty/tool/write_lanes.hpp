#pragma once
// agentty/tool/write_lanes.hpp-谁现在可以写入哪个文件。
//
// 对于每一个其副作用包括WriteFs的调用，"files a tool is writing"
// 的一个进程-wide表由ONE工具调用者tool::DynamicDispatch::execute_with获取。
// 调用 是一个工具 正文（tools::ToolBody 只能
// 使用 分派器 制作的密钥运行），因此没有路径可以围绕它编写：
// 父调度批次、每个`task`子代理、ACP、废弃作业以及
// `agentty mcp-serve`上的MCP客户端都经过这里。
//
// 调用声称：
//   * 它命名的文件(路径/file_path/...，以及移动的两端)，
//     规范化，因此`./a.c`、`a.c`和`/ws/a.c`是一条泳道；
//   * 整个 工作区，当它写入但没有命名文件时（git_commit，
//     git_stash，MCP工具，插件）：它可以触及任何东西，
//     所以它等待每个作家，每个作家都等待它。
// 通道按前缀嵌套：`src/`的写入器和`src/a.c`的写入器重叠。
//
// 声明是全部-or-nothing(没有部分保留），
// 因此两个调用各自想要对方的文件——在移动B->A旁边移动A->B
// ——不能死锁：在等待时两者都不持有任何东西。等待是可以取消的，
// 并且永远不会保留 锁：排队时 已取消 的 调用 会放弃并彻底失败。
//
// 读取不占用通道。写入以 write_atomic（临时 + 重命名）着陆，
// 因此并发读取器会看到旧文件或新文件，而不会看到撕裂的文件。

#include <string>
#include <vector>

#include <maya/runtime.hpp>
#include <nlohmann/json.hpp>

namespace agentty::tools {
struct CallContext;
}

namespace agentty::tools::write_lanes {

// WriteFs 调用需要的通道：其规范路径，或者当它没有命名时为{
// kWholeWorkspace}。相对路径针对 调用 的项目进行解析，
// 然后是 工作区，然后是进程 工作区。
inline constexpr const char* kWholeWorkspace = "\x01*whole-workspace*";
[[nodiscard]] std::vector<std::string> lanes_for(std::string_view tool,
                                                 const nlohmann::json& args,
                                                 const CallContext& ctx);

// 两条车道是否碰撞（相同、一条在另一条之下、或者其中一条
// 是整个工作区）。
[[nodiscard]] bool overlap(std::string_view a, std::string_view b) noexcept;

// 为一个调用持有一组车道；无论结束如何释放它们。
class Hold {
public:
    // 等待直到没有保留的车道与`lanes`重叠，然后立即声明它们。
    // 如果等待时 `ctx` 是 已取消，则放弃 (held() == false)。
    Hold(const maya::unlocked& cap, std::vector<std::string> lanes, const CallContext& ctx);
    ~Hold();
    Hold(const Hold&)            = delete;
    Hold& operator=(const Hold&) = delete;

    [[nodiscard]] bool held() const noexcept { return lanes_.empty() || held_; }

private:
    const maya::unlocked&    cap_;
    std::vector<std::string> lanes_;
    bool                     held_ = false;
};

// 测试挂钩：现在保留了多少条车道。
[[nodiscard]] std::size_t held_count(const maya::unlocked& cap);

}  // namespace agentty::tools::write_lanes
