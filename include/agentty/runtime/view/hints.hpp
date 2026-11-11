// SPDX-License-Identifier: Apache-2.0
#pragma once
// agentty::ui —响应式 key-hint 页脚条，由 选择面板 和 diff-review
// 面板 (任何带有 "key label   key label …" 页脚的 弹出面板) 共享。
// 渲染在SINGLE线上；当宽度不能容纳所有提示时，优先级最低的
// 提示将被删除（最右边的提示获胜），而幸存者则保持 left
// -to- 的正确顺序。永远不会包裹。提取后，每个 弹出面板 页脚
// 都会以相同的方式降解，并且破坏性操作可以一致地着色。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <maya/dsl.hpp>
#include "agentty/runtime/view/palette.hpp"   // 颜色标记（fg、静音……）+ fg_of/fg_dim
#include "agentty/runtime/view/helpers.hpp"   // query_caret (filter_header)

namespace agentty::ui {

// 一个关键-binding提示：关键字形+短标签、优先级（较高
// =在宽度压力下保持更长）、以及KEY字形的可选强调色（e.g。
// "accept"成功，"reject"危险——因此
// 破坏性行为永远不会与良性行为相同）。
struct Hint {
    std::string  key;
    std::string  label;
    int          priority = 0;
    maya::Color  key_color = fg;   // 默认 = 正常前景
};

[[nodiscard]] inline maya::Element key_hints(std::vector<Hint> hints) {
    using namespace maya;
    using namespace maya::dsl;
    return component([hints = std::move(hints)](int w, int) -> Element {
        if (w <= 0 || hints.empty()) return nothing();
        constexpr int gap = 3;   // 相邻提示之间的列
        auto pair_w = [](const Hint& hn) {
            return string_width(hn.key) + 1 + string_width(hn.label);
        };
        std::vector<bool> keep(hints.size(), true);
        // 空标签意味着"this action does not apply to the current row"
        // ——放弃提示而不是绘制一个悬空的键，后面什么都没有。
        // 让调用者写入 `cond ? "" : "label"`
        // 而不是构建两个不同的向量。
        for (std::size_t i = 0; i < hints.size(); ++i)
            if (hints[i].label.empty()) keep[i] = false;
        auto total = [&] {
            int sum = 0, shown = 0;
            for (std::size_t i = 0; i < hints.size(); ++i)
                if (keep[i]) { sum += pair_w(hints[i]); ++shown; }
            if (shown > 1) sum += gap * (shown - 1);
            return sum;
        };
        // 贪婪地逐出最低的-priority并保持提示，直到条带适合为止。
        while (total() > w) {
            int victim = -1;
            for (std::size_t i = 0; i < hints.size(); ++i) {
                if (!keep[i]) continue;
                if (victim < 0 || hints[i].priority <=
                        hints[static_cast<std::size_t>(victim)].priority)
                    victim = static_cast<int>(i);
            }
            if (victim < 0) break;
            keep[static_cast<std::size_t>(victim)] = false;
        }
        std::vector<Element> parts;
        bool first = true;
        for (std::size_t i = 0; i < hints.size(); ++i) {
            if (!keep[i]) continue;
            if (!first) parts.push_back(text(std::string(gap, ' ')));
            first = false;
            parts.push_back(text(hints[i].key + " ", fg_of(hints[i].key_color)));
            parts.push_back(text(hints[i].label, fg_dim(muted)));
        }
        if (parts.empty()) return nothing();
        return h(std::move(parts)).build();
    });
}

// 过滤线顶部有一个选择面板:一个放大镜，然后要么是查询，
// 要么是灰色的提示词。
//
// ONE定义，因为每个选择面板都有自己的hand-rolled，
// 并且它们以与hand-rolled chrome完全相同的
// 方式漂移：主题浏览器以调色板表情符号而不是放大镜引导，
// 并通过额外的空格缩进其文本，因此在它和提供者选择面板之间
// 切换将查询行在光标下横向移动。相同的小部件，三种拼写。
//
// `noun`命名正在过滤的内容（"providers"，"symbols")；
// 空给出了裸露的"type to filter…"，当面板标题已经说明了列表
// 是什么时，这是正确的。
//
// `caret`在查询之后添加闪烁的插入点，
// 对于读作提示词的选择面板，您正在键入INTO（命令选项板，
// 型号选择面板）而不是您正在缩小的列表。通过颜色选择；
// 默认没有光标。
[[nodiscard]] inline maya::Element filter_header(std::string_view query,
                                                std::string_view noun = {},
                                                std::optional<maya::Color> caret = {},
                                                std::string_view glyph = "\xf0\x9f\x94\x8d ") {
    using namespace maya;
    using namespace maya::dsl;
    std::string prompt = "type to filter";
    if (!noun.empty()) { prompt += ' '; prompt += noun; }
    prompt += "\xe2\x80\xa6";                      // …
    const bool empty_q = query.empty();

    std::vector<Element> parts;
    parts.push_back(text(std::string{glyph},
                         caret ? fg_bold(*caret) : fg_of(muted)));
    if (empty_q) {
        // 光标、之前、提示词：它标记第一次击键的位置，
        // 提示词是位于其右侧的占位符文本。
        if (caret) parts.push_back(query_caret(*caret));
        parts.push_back(text(std::move(prompt), fg_italic(muted)));
    } else {
        parts.push_back(text(std::string{query}, fg_of(fg)));
        if (caret) parts.push_back(query_caret(*caret));
    }
    return h(std::move(parts)).build();
}

} // namespace agentty::ui
