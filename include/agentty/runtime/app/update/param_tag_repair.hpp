#pragma once
// ── XML-in-JSON 工具 -call 恢复 ────────────────────────────────────
// 一些模型推出间歇地将Anthropic的内部XML工具-call语法混合到JSON工具输入中。
// 传输 不是干净的形状 {"path":"f","edits":[{
//   "old_text":"A","new_text":"B"}]}，
// 而是在语法上携带 -VALID JSON，其字符串值将真实参数偷偷带入
// `<parameter name="…">…` 标记 e.g 内。
//   {"path":"f","edit":"\n<parameter name=\"old_text\">A",
// "new_text":"B"}（注意`edit`，而不是`edits`，
// 以及隐藏在其中的old_text）。因为 JSON 解析，抢救路径永远不会运行，
// 但所需字段（old_text /编辑）现在被埋在杂散键中，并且 调用 在 required
// -field 防护中消失，并带有误导性的 "looks incomplete" 消息。
// 一旦模型锁定到XML-emitting状态（在最坏情况下
// 约27%的编辑），观察到它对会话中的所有编辑失败负责。
//
// 恢复将字符串值中的每个`<parameter name="K">V`
// 段提升回正确的top-level键并重建规范的arg形状。
// SAFETY：唯一的调用者（guard_truncated_tool_args）
// 仅在已经-failing路径上调用它 - 一个well
// -formed edit/write，其内容合法地包含文字标记（e.g。
// 编辑此文件）具有其必需的字段，并且永远不会被重写。

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/tool/spec.hpp"

namespace agentty::app::detail {

// 将 `<parameter name="K">V` 段从 `args`、
// IN ORDER 中的每个字符串值中取出，保留 DUPLICATE 名称。
// 每个值从 `>` 之后到下一个 `<parameter name="`
// / `</parameter>` / end-of-string。
// 顺序+重复项是 加载-bearing：一个 multi-edit 调用 泄漏，
// 因为 XML 依次携带多个 `old_text`/`new_text` 标签，
// 并且折叠它们（first-wins）将默默地删除第一个之后的每个编辑。
// 字符串值按 JSON 对象的键顺序访问，因此重建的序列与 传输 匹配。
using ParamTag = std::pair<std::string, std::string>;

inline std::vector<ParamTag>
extract_param_tags(const nlohmann::json& args) {
    constexpr std::string_view kOpen  = "<parameter name=\"";
    constexpr std::string_view kClose = "</parameter>";
    std::vector<ParamTag> out;
    if (!args.is_object()) return out;
    for (const auto& [k, v] : args.items()) {
        if (!v.is_string()) continue;
        const std::string& s = v.template get_ref<const std::string&>();
        std::size_t pos = 0;
        while ((pos = s.find(kOpen, pos)) != std::string::npos) {
            std::size_t name_start = pos + kOpen.size();
            std::size_t name_end   = s.find('"', name_start);
            if (name_end == std::string::npos) break;
            std::size_t gt = s.find('>', name_end);
            if (gt == std::string::npos) break;
            std::size_t val_start = gt + 1;
            std::size_t next  = s.find(kOpen,  val_start);
            std::size_t close = s.find(kClose, val_start);
            std::size_t val_end = std::min(
                next  == std::string::npos ? s.size() : next,
                close == std::string::npos ? s.size() : close);
            out.emplace_back(s.substr(name_start, name_end - name_start),
                             s.substr(val_start, val_end - val_start));
            pos = val_end;
        }
    }
    return out;
}

// 用 `name` 或 nullopt 标记的第一个值。 First
// -wins 查找标量字段（路径/内容/行），其中重复项是噪音。
inline std::optional<std::string>
first_tag(const std::vector<ParamTag>& tags, std::string_view name) {
    for (const auto& [k, v] : tags)
        if (k == name && !v.empty()) return v;
    return std::nullopt;
}

// 当检测到并恢复XML参数-tag泄漏时，将`args`重写
// 为规范形状并返回真；否则返回 false（保留 `args`
// 不变）。仅当工具 调用 已无法通过所需的 -field
// 检查时，才会调用 必须 — 请参阅 头文件 注释。
inline bool repair_param_tag_leak(std::string_view tool_name,
                                  nlohmann::json& args) {
    using nlohmann::json;
    if (!args.is_object()) return false;
    auto kind = tools::spec::kind_of(tool_name);
    if (!kind) return false;

    constexpr std::string_view kOpen = "<parameter name=\"";
    bool marker = false;
    for (const auto& [k, v] : args.items())
        if (v.is_string()
            && v.template get_ref<const std::string&>().find(kOpen)
                   != std::string::npos) {
            marker = true; break;
        }
    if (!marker) return false;

    auto tags = extract_param_tags(args);
    if (tags.empty()) return false;

    // 更喜欢干净的 top-level 字符串字段（本身不是
    // 泄漏的字段）；回退到从参数标记恢复的 首次 值。
    auto pick = [&](std::initializer_list<const char*> names)
        -> std::optional<std::string> {
        for (const char* n : names)
            if (auto it = args.find(n); it != args.end() && it->is_string()) {
                const std::string& s = it->template get_ref<const std::string&>();
                if (!s.empty() && s.find(kOpen) == std::string::npos) return s;
            }
        for (const char* n : names)
            if (auto v = first_tag(tags, n)) return v;
        return std::nullopt;
    };

    // 逐字记录的display_description本身可能是泄漏(
    // `\n<parameter name="...">...`)。只有在干净的情况下才保留它。
    auto clean_description = [&](nlohmann::json& dst) {
        if (auto it = args.find("display_description");
            it != args.end() && it->is_string()) {
            const std::string& s = it->template get_ref<const std::string&>();
            if (s.find(kOpen) == std::string::npos)
                dst["display_description"] = s;
        }
    };

    using K = tools::spec::Kind;
    if (*kind == K::Edit) {
        // 重建EVERY泄露的编辑，而不仅仅是第一个。作为 XML 泄露的 multi
        // -edit 调用 按顺序携带 old_text/new_text
        // 标签：将每个 old_text 与其后面的 new_text 配对。
        // 回到顶部-level洁净场，以发现单个-edit泄漏，
        // 其中只有一侧走私了标记。
        json edits = json::array();
        std::optional<std::string> pending_old;
        for (const auto& [k, v] : tags) {
            if (k == "old_text" || k == "old_string") {
                if (pending_old) {   // 旧的，没有匹配的新的 — 齐平 as-is
                    json e = json::object();
                    e["old_text"] = std::move(*pending_old);
                    e["new_text"] = "";
                    edits.push_back(std::move(e));
                }
                pending_old = v;
            } else if (k == "new_text" || k == "new_string") {
                json e = json::object();
                e["old_text"] = pending_old ? std::move(*pending_old)
                                            : std::string{};
                e["new_text"] = v;
                pending_old.reset();
                if (!e["old_text"].get_ref<const std::string&>().empty())
                    edits.push_back(std::move(e));
            }
        }
        if (pending_old && !pending_old->empty()) {
            json e = json::object();
            e["old_text"] = std::move(*pending_old);
            // 悬空的 old_text 没有匹配的 new_text 标签：新的
            // 一面可能作为 CLEAN top-level 密钥到达（情况 1
            // — 仅 old_text 走私到泄露的字符串中）。更喜欢它。
            auto new_t = pick({"new_text", "new_string"});
            e["new_text"] = new_t.value_or("");
            edits.push_back(std::move(e));
        }

        // Single-edit泄漏，其中一侧是干净的top-level字段：回落到pick()，
        // 因此`{"edit":"<parameter...>OLD","new_text":"NEW"}`
        // 形状（仅泄漏的old_text）仍然恢复。
        if (edits.empty()) {
            auto old_t = pick({"old_text", "old_string"});
            if (!old_t) return false;   // 无可挽回
            auto new_t = pick({"new_text", "new_string"});
            json one = json::object();
            one["old_text"] = std::move(*old_t);
            one["new_text"] = new_t.value_or("");
            edits.push_back(std::move(one));
        }

        // 单个泄露的`line`提示适用于(唯一的)恢复的编辑。
        if (edits.size() == 1) {
            if (auto v = first_tag(tags, "line")) {
                try { edits[0]["line"] = std::stoi(*v); } catch (...) {}
            } else if (auto it = args.find("line");
                       it != args.end() && it->is_number_integer()) {
                edits[0]["line"] = *it;
            }
        }

        auto path = pick({"path", "file_path", "filepath", "filename"});
        json rebuilt = json::object();
        if (path) rebuilt["path"] = std::move(*path);
        clean_description(rebuilt);
        rebuilt["edits"] = std::move(edits);
        args = std::move(rebuilt);
        return true;
    }
    if (*kind == K::Write) {
        auto content = pick({"content", "file_text", "text",
                             "file_content", "contents", "body", "data"});
        if (!content) return false;
        auto path = pick({"path", "file_path", "filepath", "filename"});

        json rebuilt = json::object();
        if (path) rebuilt["path"] = std::move(*path);
        clean_description(rebuilt);
        rebuilt["content"] = std::move(*content);
        args = std::move(rebuilt);
        return true;
    }
    return false;
}

} // namespace agentty::app::detail
