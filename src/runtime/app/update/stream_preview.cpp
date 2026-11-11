// 活性工具 -args 预览解码器, 拆分出 stream.cpp.
//
// update_stream_preview 运行于每个 input_json_delta
// 勾选( 由呼叫者按键) 并逐步解码正在增长的工具 -args 传输 缓冲区
// 输入 tc.args 工具卡可以用来 渲染 路径/内容/编辑/待办事宜 数据流.
// sync_todo_state_from_args 镜像已完成的待办事宜 调用
// 输入 UI 计划状态。 internal.hpp (横扫) -TU ) 共享的纯
// 叶助产者,他们靠在其中; stream_args.hpp.
//
// 此文件带有 O(N)-per-tick 解码 高频路径 (write's
// crashd -offset 内容解码,编辑编辑 [] 镜像)。
// 保留它从stream.cpp 缩小了 状态更新函数 TU , 并且从它不依赖的
// finalize/error 状态机器中隔离预览逻辑。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update/stream_args.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "agentty/tool/util/partial_json.hpp"

namespace agentty::app::detail {

using json = nlohmann::json;

void update_stream_preview(ToolUse& tc) {
    // 早点便宜 -out : 如果 流式传输 缓冲区 自上次预览以来,
    // 还没有增长过, 重新 -parsing 输出相同。
    // 调用器限制速率中的120毫秒节流; 当模型暂停时, 此限制 * 工作
    // * -stream (空三角洲,心跳空隙)——0 -copy 零(零)
    // -alloc 跳过。
    if (tc.args_streaming.size() == tc.stream_sniff_size
        && tc.stream_sniff_size != 0) {
        return;
    }
    tc.stream_sniff_size = tc.args_streaming.size();

    auto set_arg = [&](std::string_view key, std::string v) {
        if (!tc.args.is_object()) tc.args = json::object();
        auto& cur = tc.args[std::string{key}];
        // 便宜 "did it change?" ——满 字节 比较多 -KB 内容字符串
        // 为 ~ 一半。 -tick 成本,同样 -size + 不变 -bookend
        // 是一个非常强烈的信号, "unchanged" 附加时 -only 流式传输.
        if (cur.is_string()) {
            const auto& s = cur.get_ref<const std::string&>();
            if (s.size() == v.size()
                && (s.empty()
                    || (s.front() == v.front() && s.back() == v.back())))
                return;
        }
        cur = std::move(v);
        tc.mark_args_dirty();
    };
    auto try_set = [&](std::string_view canon,
                       std::span<const std::string_view> keys = {}) {
        auto ks = keys.empty() ? std::span{&canon, 1} : keys;
        if (auto v = sniff_any(tc.args_streaming, ks, /*部分=(%)*/false)) {
            set_arg(canon, *v); return true;
        }
        return false;
    };
    auto try_set_partial = [&](std::string_view canon,
                               std::span<const std::string_view> keys = {}) {
        auto ks = keys.empty() ? std::span{&canon, 1} : keys;
        if (auto v = sniff_any(tc.args_streaming, ks, /*部分=(%)*/true)) {
            set_arg(canon, *v); return true;
        }
        return false;
    };
    // 惰性: try_parse_partial 是 O( 单数)
    // |args_streaming| ) 单位 调用 ——多层
    // -KB 写入 正文 运行时每120米是四倍径 `edit`
    // /`todo` 分支实际需要结构化视图( 走动) `edits[]`
    // / `todos[]`。其他分支都使用嗅觉标度+缓存
    // -offset 内容解码,并全部跳过此成本。可选内容在第一个
    // 访问时被覆盖。
    //
    // 增长 -window 摊销:甚至 edit/todo 我们不 -parse
    // 每一滴都一样,但窗户是 ADAPTIVE ,不是平房 512 号 字节 —
    // —一个平面窗口进行编辑 逐步显示 粗体 ~ 512 个 -byte BLOCKS
    // 感觉迟钝和粗糙 "block-wise, not streaming"。
    // 我们用 缓冲区 大小,所以成本保持不变(______________________
    // 数据流 内容 -parsed 常数),而 SMALL 编辑 – 普通案件 –
    // re -parse 每~64个 字节, i.e 大致线条 -by-line ,
    // 为平滑 Codex-style 逐步显示. StreamToolUseEnd
    // 内容 -parses 完整内容 缓冲区 权威上,所以最终的词源总是准确的.
    //   窗口 = 夹( ) buffer_size / 8,
    // 64, 4096) → a 256 B 差异块 显示于~64 B级(4个) 重绘);
    //   a 32 KB 最多写到 ~8x 共。 Newline -dense
    //   编辑 正文 因此, 数据流 一次一两行, 而不是一半 -a-KB 立刻来
    const std::size_t kStreamParseGrowth =
        std::clamp<std::size_t>(tc.args_streaming.size() / 8, 64, 4096);
    std::optional<json> parsed_cache;
    bool                parsed_attempted = false;
    auto get_parsed = [&]() -> const std::optional<json>& {
        if (!parsed_attempted) {
            const bool grew_enough =
                tc.stream_parse_through == 0
                || tc.args_streaming.size()
                       >= tc.stream_parse_through + kStreamParseGrowth;
            if (grew_enough) {
                parsed_cache = try_parse_partial(tc.args_streaming);
                tc.stream_parse_through = tc.args_streaming.size();
            }
            parsed_attempted = true;
        }
        return parsed_cache;
    };
    auto try_struct = [&](std::string_view canon,
                          std::span<const std::string_view> keys) -> bool {
        const auto& p = get_parsed();
        if (!p) return false;
        if (auto s = get_string_any(*p, keys)) {
            set_arg(canon, *s);
            return true;
        }
        return false;
    };
    auto try_struct_first_edit = [&](std::string_view canon,
                                     std::string_view field) -> bool {
        const auto& p = get_parsed();
        if (!p) return false;
        auto it = p->find("edits");
        if (it == p->end() || !it->is_array() || it->empty()) return false;
        const auto& first = (*it)[0];
        if (!first.is_object()) return false;
        auto f = first.find(std::string{field});
        if (f == first.end() || !f->is_string()) return false;
        set_arg(canon, f->get<std::string>());
        return true;
    };
    auto pull_desc = [&] {
        // display_description 是一个短的scalar字段—
        // —嗅觉严格来说比满的快 解析 跳过它 try_struct 所以我们
        // 不会强迫 -materialize `parsed` 仅仅40 -char
        // 说明热量 write/bash/grep 路径。
        (void)try_set("display_description", std::span{&kDisplayDescription, 1});
    };
    const auto& n = tc.name.value;
    if      (n == "read" || n == "list_dir") {
        // 路径是短的scalar——嗅觉比全解析快.
        try_set("path", kPathAliases);
        pull_desc();
    }
    else if (n == "write") {
        // Write 快速路径 将军 try_struct / try_parse_partial
        // 关闭和重新 -parses * 实体 * 生长的恒星 缓冲区 每分勾上,
        // 小工具都很好,四进制多 -KB 写入 正文 (a 50 KB 8 的
        // 内容字段 ticks/sec 单位为 400 KB 以百万字节计,
        // 每秒升至百万字节 末尾 会 议 日 程 和 议 程 数据流 )路径
        // + desc位于该路径的首端附近. 缓冲区 和廉价;内容使用 *
        // 标记 * 起始 -of-value 抵消, 因此每个勾选只解码新
        // -appended 字节 ,而不是累积 缓冲区.
        try_set("path", kPathAliases);
        pull_desc();

        // 找到开口地点 `"` 内容值的 一次,
        // 缓存 右转,然后在后面的勾勾上恢复从那里解码。
        // args_streaming 增长附件 -only ,因此该工具的偏移
        // 仍然有效 调用's 生命周期 我们探测每个别名直到一个命中,
        // 然后停止探测( stream_sniff_offset != 0).
        if (tc.stream_sniff_offset == 0) {
            for (auto k : kContentAliases) {
                if (auto p = agentty::tools::util::locate_string_value(
                        tc.args_streaming, k)) {
                    tc.stream_sniff_offset = *p;
                    tc.stream_decode_through = *p;
                    break;
                }
            }
        }
        if (tc.stream_sniff_offset != 0) {
            // 递增解码:每勾选只消耗自第stream_decode_through
            // 以来新的字节,然后将累积值调整到~2×的预览上限,
            // 这样内存就保留在巨大的写法上。累积成本是整个第
            // 数据流的O(args_streaming.size()),
            // 而不是O(N2).
            agentty::tools::util::decode_string_append(
                tc.args_streaming,
                &tc.stream_decode_through,
                tc.stream_decoded_value,
                &tc.stream_decode_closed);
            constexpr std::size_t kKeepCap = 2 * kStreamingPreviewCap;
            if (tc.stream_decoded_value.size() > kKeepCap) {
                // 将头部缩放, 保留最新鲜的 2xxcap 字节。
                // 预览只显示过后缀的盖,
                // 因此我们掉下来的头已经隐形。
                tc.stream_decoded_value.erase(
                    0, tc.stream_decoded_value.size() - kKeepCap);
            }
            const auto& v = tc.stream_decoded_value;
            if (!v.empty()) {
                if (v.size() > kStreamingPreviewCap) {
                    // 预览显示末尾——最新字节是
                    // 数据仍在流动的最有用的确认.
                    // 将封顶预览建在一个镜头中,
                    // 而不是 concat + substr (它分配
                    // 完整的N-byte中间体,只用来丢弃).
                    std::string tail;
                    tail.reserve(kStreamingPreviewCap + 32);
                    tail.append("\xe2\x80\xa6 (showing tail) \xe2\x80\xa6\n");
                    tail.append(v, v.size() - kStreamingPreviewCap,
                                kStreamingPreviewCap);
                    set_arg("content", std::move(tail));
                } else {
                    set_arg("content", v);
                }
            }
        }
    }
    else if (n == "edit") {
        // 编辑是唯一真正需要结构化的解析的分支——`edits[]`
        // 是一组对象,我们希望将其字段映射成tc.args,顺序是.
        // try_struct/try_struct_first_edit将在第调用上懒散的按需加载
        // `parsed`.
        if (!try_struct("path", kPathAliases)) try_set("path", kPathAliases);
        pull_desc();
        // 镜像 完整 编辑数组为 tc.args["edits" 随它增长,
        // 卡可以 渲染 编辑时的每次编辑 流式传输 ,而不仅仅是第一个。
        // 每个条目都是部分对象。 old_text 可能在此之前在场
        // new_text 开始,我们让他们 订购,所以 渲染器's
        // "edit N/M" 标签在编辑更多土地时保持稳定。
        bool wrote_edits_array = false;
        const auto& parsed = get_parsed();
        if (parsed) {
            if (auto it = parsed->find("edits");
                it != parsed->end() && it->is_array() && !it->empty())
            {
                json arr = json::array();
                for (const auto& e : *it) {
                    if (!e.is_object()) continue;
                    json out = json::object();
                    if (auto o = e.find("old_text"); o != e.end() && o->is_string())
                        out["old_text"] = o->get<std::string>();
                    else if (auto o2 = e.find("old_string"); o2 != e.end() && o2->is_string())
                        out["old_text"] = o2->get<std::string>();
                    if (auto nv = e.find("new_text"); nv != e.end() && nv->is_string())
                        out["new_text"] = nv->get<std::string>();
                    else if (auto nv2 = e.find("new_string"); nv2 != e.end() && nv2->is_string())
                        out["new_text"] = nv2->get<std::string>();
                    arr.push_back(std::move(out));
                }
                if (!arr.empty()) {
                    if (!tc.args.is_object()) tc.args = json::object();
                    auto& cur = tc.args["edits"];
                    bool changed = !cur.is_array() || cur.size() != arr.size();
                    if (!changed) {
                        for (std::size_t i = 0; i < arr.size(); ++i) {
                            const auto& a = arr[i];
                            const auto& b = cur[i];
                            auto a_old = a.value("old_text", std::string{});
                            auto b_old = b.value("old_text", std::string{});
                            auto a_new = a.value("new_text", std::string{});
                            auto b_new = b.value("new_text", std::string{});
                            if (a_old.size() != b_old.size() || a_new.size() != b_new.size()) {
                                changed = true; break;
                            }
                        }
                    }
                    if (changed) {
                        cur = std::move(arr);
                        tc.mark_args_dirty();
                    }
                    wrote_edits_array = true;
                }
            }
        }
        if (!wrote_edits_array) {
            if (!try_struct_first_edit("old_string", "old_text"))
                try_set_partial("old_string", kOldStrAliases);
            if (!try_struct_first_edit("new_string", "new_text"))
                try_set_partial("new_string", kNewStrAliases);
        }
    }
    else if (n == "shell") { try_set("command", kCommandAliases); pull_desc(); }
    else if (n == "grep")  { try_set("pattern"); try_set("path", kPathAliases); pull_desc(); }
    else if (n == "glob")  { try_set("pattern"); pull_desc(); }
    else if (n == "find_definition") { try_set("symbol"); pull_desc(); }
    else if (n == "web_fetch")       { try_set("url");    pull_desc(); }
    else if (n == "web_search")      { try_set("query");  pull_desc(); }
    else if (n == "diagnostics")     { try_set("command", kCommandAliases); pull_desc(); }
    else if (n == "git_status" || n == "git_diff"
          || n == "git_log"    || n == "git_commit") {
        if (n == "git_commit") try_set("message");
        pull_desc();
    }
    else if (n == "todo") {
        pull_desc();
        // 将部分的todos[]阵列映射为tc.args["todos"],
        // 如同它的数据流,所以内置计划卡填充为第-by-row行(
        // 和在-progress项下方的持久化计划状态),
        // 每件物品落地时——不只在整阵数到达StreamToolUseEnd时,
        // Mirs zed的活的update_plan元素.
        const auto& parsed = get_parsed();
        if (parsed) {
            if (auto it = parsed->find("todos");
                it != parsed->end() && it->is_array() && !it->empty())
            {
                json arr = json::array();
                for (const auto& td : *it) {
                    if (!td.is_object()) continue;
                    auto c = td.find("content");
                    if (c == td.end() || !c->is_string()) continue;
                    json item = json::object();
                    item["content"] = c->get<std::string>();
                    if (auto s = td.find("status");
                        s != td.end() && s->is_string())
                        item["status"] = s->get<std::string>();
                    else
                        item["status"] = "pending";
                    arr.push_back(std::move(item));
                }
                if (!arr.empty()) {
                    if (!tc.args.is_object()) tc.args = json::object();
                    auto& cur = tc.args["todos"];
                    bool changed = !cur.is_array() || cur.size() != arr.size();
                    if (!changed) {
                        for (std::size_t i = 0; i < arr.size(); ++i) {
                            if (arr[i].value("content", std::string{})
                                    != cur[i].value("content", std::string{})
                             || arr[i].value("status", std::string{})
                                    != cur[i].value("status", std::string{})) {
                                changed = true; break;
                            }
                        }
                    }
                    if (changed) { cur = std::move(arr); tc.mark_args_dirty(); }
                }
            }
        }
    }
}

void sync_todo_state_from_args(Model& m, const nlohmann::json& args) {
    if (!args.is_object()) return;
    auto it = args.find("todos");
    if (it == args.end() || !it->is_array() || it->empty()) return;

    std::vector<TodoItem> items;
    items.reserve(it->size());
    for (const auto& td : *it) {
        if (!td.is_object()) continue;
        auto c = td.find("content");
        if (c == td.end() || !c->is_string()) continue;
        TodoItem item;
        item.content = c->get<std::string>();
        auto st = td.value("status", std::string{"pending"});
        item.status = st == "completed"   ? TodoStatus::Completed
                    : st == "in_progress" ? TodoStatus::InProgress
                                           : TodoStatus::Pending;
        items.push_back(std::move(item));
    }
    if (items.empty()) return;
    // 便宜,没有改变 -guard : 预览会点火此片
    // `todo` 调用 谓 流式传输 ,但计划只有在项目内容或
    // 状态翻转时才实际改变。重建+移动 -assigning 每
    // 个勾选方向 分配。比较形状(计算数+每件) -item
    // content/status )和相同者保释。
    if (items.size() == m.ui.todo.items.size()) {
        bool same = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].status != m.ui.todo.items[i].status
                || items[i].content != m.ui.todo.items[i].content) {
                same = false;
                break;
            }
        }
        if (same) return;
    }
    m.ui.todo.items = std::move(items);
}

} // namespace agentty::app::detail
