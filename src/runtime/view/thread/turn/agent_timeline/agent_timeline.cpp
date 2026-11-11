#include "agentty/runtime/view/thread/turn/agent_timeline/agent_timeline.hpp"

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include <maya/core/render_context.hpp>   // available_height (末尾-spinner 门)
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_body_preview.hpp"
#include "agentty/domain/ui_live.hpp"   // tool_output : 多少钱 正文 显示
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_helpers.hpp"

namespace agentty::ui {
namespace {

// ── 已解决 -tool 正文 缓存 ───────────────────────────────────────────
//
// tool_body_preview_config ()是 O( )。 正文 曰:其为. 解析 工具的吗?
// JSON 参数( 参数) safe_arg 满座的, `content` ),实现 output() ,用于
// Edit 扫描差异正文中的 ```diff 围栏，再提取对应差异块。
// 文本。对于一个 write/edit, 千行为~10 ms。
//
// 在一个 edit/write-heavy 打开 -flight 助理运行载
// 具 MANY 已经 -terminal 牌( 每个子) -turn
// 下一期续作到达前结算). build_live_tail 给整个英寸
// -flight 运行 NO hash_id (最新工具仍然是 待处理 因此,
// 主机重建了每张已结算的卡片 正文 每个 帧 — O(Σ 正文 大小)每 帧
// 10-60时 FPS。这就是攀登 CPU 转弯的转弯会增加
//
// A 终端 工具的 正文 字节 已经 不可变 : 状态固定, output().
// size() 已经固定了。所以我们记住了在“设计”下构建的“配置”。 EXACT 内容
// -address 数字 -event hash_id 已经信任——(d,
// status.index(), output().size(), highlight_lines 签名).
// 点击时,我们复制缓存的配置(一个平面) string/vector 而不是重新印发。
// -parsing JSON + 调值 -scanning 迪夫, 发泄 字节 已经
// 字节-identical 以新的建筑为装饰,而加以冻结。 freeze_range 邮票,
// 同一张 assistant_run_hash_id 保持纯洁 maya 缓存 击中——没有
// 回滚缓冲区-corruption 表面, 显示
//
// 单身 -threaded 建筑工程( 运行时 序列化更新+视图
// 线程 ),所以一个 thread_local 存储不需要 加锁
// 已设界 LRU 无法随会话长度而增长; 卷出后会冷冻的已定
// 卡片 m.ui.frozen 再也不会问这个了 所以他们的
// 条目都掉到下面了 LRU 压力自然。
class BodyConfigCache {
public:
    static constexpr std::size_t kCap = 512;

    // 返回缓存配置 `key` ,或 ,或 nullptr 有关小姐。
    std::shared_ptr<const maya::ToolBodyPreview::Config> get(const std::string& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second.lru_it);
        return it->second.cfg;
    }

    void put(const std::string& key,
             std::shared_ptr<const maya::ToolBodyPreview::Config> cfg) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.cfg = std::move(cfg);
            lru_.splice(lru_.begin(), lru_, it->second.lru_it);
            return;
        }
        lru_.push_front(key);
        map_.emplace(key, Entry{std::move(cfg), lru_.begin()});
        while (map_.size() > kCap) {
            map_.erase(lru_.back());
            lru_.pop_back();
        }
    }

private:
    struct Entry {
        std::shared_ptr<const maya::ToolBodyPreview::Config> cfg;
        std::list<std::string>::iterator                     lru_it;
    };
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string>                 lru_;
};

// 三者,我们先走 缓存 下图 LOOP-ONLY ,现在用他们的类型来形容。
//
// 他们以前是 `thread_local` ,用于记录意图,
// 并强制执行任何内容:a 工作线程 被调入这个文件 会悄悄地
// 被自己空空 缓存 财务报告和已审计财务报表 渲染 这回答是错误的,
// 比坠机更糟糕, 缺少面板或 过期 正文 没有任何原因
//
// loop_bound<T> 实现内核循环值的证明 线程 ,
// 而获得该证明的唯一方法就是实际存在;从循环中,
// 访问中断,并带有一个命名规则的信息,而不是悄悄地交回第二个副本.
// view() 运行在环(内核将它的身份标注在它的
// 全部) 生命周期 ,它涵盖 -steps 窗口,
// 显示主机油漆,所以 -loop 路径不变,
// 并且不变 锁-free.
//
// 见jaal/kernel/loop.hpp和jaal/docs/concurrency.md第1.2和P1节。
maya::loop_bound<BodyConfigCache> g_body_cache;

// ── 已解决 -panel ELEMENT 缓存 ───────────────────────────────────────
//
// 上一级 正文 缓存。该词 正文 缓存 记住每个 终端 工具的 O () 正文)
// ToolBodyPreview::Config; 此 缓存 纪念 整个 全部建
// 面板元素 -terminal 批量,长篇 -flight 自动驾驶转弯
// 会累积许多已安顿的子 -turn 实时面板 末尾 ; 运行没有 hash_id
// 时 末尾 数据流 ,所以没有这个主机 -ran agent_timeline_config
// (grep扫描,每 -event 细节 + CacheIdBuilder,
// footer/title 组装) 和 maya 内容 -laid-out
// 一张卡片的面板 帧 - 零(批)次(X) 帧 ,线性滞后 o1_probe
// "in-flight run, N accumulated edits"（正在执行的任务，以及已经累计的 N 处编辑。）
// 表格显示。点击时复制预建元素(a) 变体 复制其承诺的子公司
// -trees 仍然携带着他们的一 -event hash_ids ,所以,
// 我说 maya 继续打扫他们 存储单元 而不是重建任何东西。
//
// 一样 LRU + thread_local 单人 -threaded 纪律为 g_body_cache.
class PanelElementCache {
public:
    // 以覆盖一个很长的单曲在 -flight 自动驾驶转动
    // 而不将屏幕上的面板驱逐。 正文 存储单元 住过一次
    // maya 组件 缓存 键入 per-event/panel
    // hash_ids ),所以高帽是便宜的.
    // 下方是300块 -edit 转过来打 缓存 (证明面板仍在
    // 直播中) 末尾 和每笔 -frame 成本再次攀升——160~
    // 320膝盖在 o1_probe 缩放表格。
    static constexpr std::size_t kCap = 512;

    // 返回缓存共享元素控件, 或丢失时的 nullptr。
    // 存储为 shared_ptr<const Element> 如此的,这样 缓存
    // 击中是重计的撞伤 maya's shared_ptr 元素显示器(a)
    // ComponentElement 带有控件 -block-keyed hash_id
    // 而不是一个 DEEP COPY。整个面板子树。面板是一个 BoxElement 其
    // 子孙为矢量者, <Element> (每笔) -event 每一行都有自己的 正文 ),
    // 所以通过数值复制它可以复制整个 源码树 每隔一段时间 帧 用于每个已结算子
    // -turn 在一个很长的 -flight 运行 - O( ++ 面板节点)/ 帧.
    // 共享崩溃, 以 O(1) 为单位, 每块已结算的面板 : maya 被
    // 包裹的树皮被打碎 存储单元 从它的十字架上 -frame 组件 缓存.
    const std::shared_ptr<const maya::Element>* get(const std::string& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second.lru_it);
        return &it->second.el;
    }

    void put(const std::string& key, std::shared_ptr<const maya::Element> el) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.el = std::move(el);
            lru_.splice(lru_.begin(), lru_, it->second.lru_it);
            return;
        }
        lru_.push_front(key);
        map_.emplace(key, Entry{std::move(el), lru_.begin()});
        while (map_.size() > kCap) {
            map_.erase(lru_.back());
            lru_.pop_back();
        }
    }

private:
    struct Entry {
        std::shared_ptr<const maya::Element> el;
        std::list<std::string>::iterator     lru_it;
    };
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string>                 lru_;
};

// 循环 -only ,与 g_body_cache 见上文注解。
maya::loop_bound<PanelElementCache> g_panel_cache;

// ── 人数 -message 面板 渲染 记忆缓存 (快速主键) ──
//
// g_panel_cache 以上为内容 -addressed : 击打它仍然
// 需要 O( 工具) 键 -string 建( 建( O) std::to_string 中,
// 在长的 -flight 自动驾驶运行 SETTLED 子目录 -turn
// 重新为 -emitted 每隔一段时间 帧 (整个运行 留在现场) 末尾 ),
// 所以这个密钥 -build 运行 O( 运行) -length 乘数(
// 每次) 帧 —— 残值 -frame 代价是,它仍然 与转弯深度。
//
// 这个 记忆缓存 移除它。 它在子上键入 -turn 稳定 MessageId
// 库存( render_key , 手柄):单张 -uint64 比较一下
// 击球的判定,没有构建字符串. render_key 谓
// Message::compute_render_key ()如果面板输出时
// 会改变(它) 归并 每个工具 status/output/render 键。
// A still -running 工具将密钥推进到 帧 重建(自然错
// 失) 加载动画 动画; 一旦子 -turn 解冻, 密钥冻结,
// 以及以后 帧 纯金币64 -compare 击回共享的手柄。
//
// 为什么这个生命 此处 而不是在 ViewCache : 条目为轻量级元素
// HANDLES (无) 存储单元 住过一次 maya 组件 缓存 ,
// 按面板的每张键 -event hash_ids), 不 重压下 源码树
// ViewCache。所以这个地图可以慷慨地覆盖一个非常深的运行,而不包含 > 1
// GiB 将风险定在 ViewCache 上限为32——脱钩
// "how deep a run stays flat" 发件人
// 我们保留了多少 Markdown 内存？
class PanelRenderMemo {
public:
    // 已缩小以覆盖一个非常深的单自动驾驶转弯( 每个条目为~a控制块
    // + 小) ComponentElement ) 舒适地高于
    // 任何现实的子 -turn 计数; 较长的运行会优雅地降低到
    // g_panel_cache 路径(它本身仍然是 O(1) 命中,
    // 只是键 -build 税收返还)。
    static constexpr std::size_t kCap = 4096;

    // 返回存储的wandif a 记忆缓存条目存在`mid` AND的
    // render_key匹配——否则为nullptr(呼叫器重建).
    // 异质的仰望( 超度) string_view 键对 a std::string
    // 故热 HIT 路径分配无益——无临时 std::string 每分机
    // -turn 百分比 帧.
    const std::shared_ptr<const maya::Element>*
    get(std::string_view mid, std::uint64_t render_key) {
        auto it = map_.find(mid);
        if (it == map_.end() || it->second.render_key != render_key)
            return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second.lru_it);
        return &it->second.el;
    }

    void put(std::string_view mid, std::uint64_t render_key,
             std::shared_ptr<const maya::Element> el) {
        if (auto it = map_.find(mid); it != map_.end()) {
            it->second.render_key = render_key;
            it->second.el         = std::move(el);
            lru_.splice(lru_.begin(), lru_, it->second.lru_it);
            return;
        }
        // 少见小姐(每名子 -turn ),
        // 所以这里的一串实现是关闭的 高频路径.
        lru_.emplace_front(mid);
        map_.emplace(std::string{mid},
                     Entry{render_key, std::move(el), lru_.begin()});
        while (map_.size() > kCap) {
            map_.erase(lru_.back());
            lru_.pop_back();
        }
    }

private:
    struct Entry {
        std::uint64_t                        render_key;
        std::shared_ptr<const maya::Element> el;
        std::list<std::string>::iterator     lru_it;
    };
    // 透明 hash/eq 因此找到( E) string_view )不建立
    // 临时 std::string 键。
    struct SvHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct SvEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a == b;
        }
    };
    std::unordered_map<std::string, Entry, SvHash, SvEq> map_;
    std::list<std::string>                               lru_;
};

// 循环 -only ,与 g_body_cache 见上文注解。
maya::loop_bound<PanelRenderMemo> g_panel_render_memo;

} // 命名空间结束。

maya::AgentTimeline::Config agent_timeline_config(std::span<const ToolUse> tool_calls,
                                                  int spinner_frame,
                                                  maya::Color rail_color) {
    int total = static_cast<int>(tool_calls.size());
    int done  = 0;
    float total_elapsed = 0.0f;
    int running_idx = -1;

    std::vector<std::pair<std::string, int>> cat_counts;
    auto bump_cat = [&](const std::string& cat) {
        for (auto& [k, n] : cat_counts) if (k == cat) { ++n; return; }
        cat_counts.emplace_back(cat, 1);
    };

    for (std::size_t i = 0; i < tool_calls.size(); ++i) {
        const auto& tc = tool_calls[i];
        if (tc.is_terminal()) {
            ++done;
            total_elapsed += tool_elapsed(tc);
        }
        if (running_idx < 0 && (tc.is_running() || tc.is_approved()))
            running_idx = static_cast<int>(i);
        bump_cat(std::string{tool_category_label(tc.name.value)});
    }

    // 十字记 -tool 语义: 扫描完成一次 Greps Name
    // -front 并建立一个 `path → {line numbers}` 索引。
    // 随后 Read/find_definition 打开其中任何
    // 路径的工具继承 grep 点击为 `highlight_lines` ,
    // 将用户的眼睛固定在前面显示的线上, 而不是强迫重新 -scan.
    // 镜像 agent_session.cpp's grep_hits → FileRead 线条输入 maya.
    const GrepHits grep_hits = collect_grep_hits(tool_calls);

    // grep的签名 -hits 索引,折叠为每 终端 工具的 正文-cache 键。
    // grep_hits 种子 a Read/find_definition
    // 正文's `highlight_lines` ,从而改变所提供的
    // HEIGHT 不变 output().size() ——所以一个在同
    // 一个时代之前就已经解决的读物. -path 格瑞普必须降落 不 服务a
    // 过期 (无) -highlight ( 缓存) 正文 一旦Grep索引了路径。
    // 整个索引上的键入是保守的(任何grep变化都重新 -mints ),
    // 但grep命中数在Greps定点后稳定,因此稳态保持永久命中数.
    // 便宜的建筑: 仅线号,没有 正文 字节.
    std::string grep_sig;
    {
        // 决定顺序 : unordered_map 迭代
        // 顺序不稳定,但我们只需要签名到 CHANGE
        // 当内容发生时, 并且要完全相同 帧 内容
        // 相同——由于地图每张都一样重建,故维持不变 帧.
        for (const auto& [path, lines] : grep_hits) {
            grep_sig += path;
            grep_sig.push_back(':');
            for (int ln : lines) {
                grep_sig += std::to_string(ln);
                grep_sig.push_back(',');
            }
            grep_sig.push_back(';');
        }
    }

    maya::AgentTimeline::Config cfg;
    cfg.frame = spinner_frame;

    // 统计：每类选择代表颜色，使徽标
    //    匹配每个 -event 源码树 格言颜色 下游.
    for (const auto& [cat, n] : cat_counts) {
        maya::Color cc = (cat == "mutate")  ? accent
                       : (cat == "execute") ? success
                       : (cat == "plan")    ? warn
                       : (cat == "vcs")     ? highlight
                                            : info;
        cfg.stats.push_back({cat, n, cc});
    }

    // ── Events.
    cfg.events.reserve(tool_calls.size());
    for (const auto& tc : tool_calls) {
        std::string detail = tool_timeline_detail(tc);
        if (detail.empty()) {
            detail = tc.is_running()  ? std::string{"running\xe2\x80\xa6"}
                   : tc.is_pending()  ? std::string{"queued\xe2\x80\xa6"}
                   : tc.is_approved() ? std::string{"approved\xe2\x80\xa6"}
                                      : std::string{"\xe2\x80\xa6"};
        }
        // 人数 -event hash_id 时值 TERMINAL
        // 工具 事件 - 现场直播 末尾 和 冻结 快照。一旦一个工具是
        // 终端 编号 字节 已经 不可变 (状况得到确定, output().
        // size() 固定),所以 缓存 键是内容 -addressed
        // 并保持稳定, 帧: maya 擦伤 事件's (头文件 +
        // 正文 ) 次级 -tree 从其组成部分 缓存 而不是重新
        // -laying-out ToolBodyPreview 单位
        // -line 元素 用于解决 write/edit 数百行将数百条弹性
        // 行折叠成一个缓存的闪光块—— 占优势的每条 -frame 高额的
        // 定居卡在一英寸 -flight 助理运行。
        //
        // 为什么这在现场是安全的 末尾 (历史问题):
        // 编号 maya prev_cells 最初导致实卡倒塌到最后一行
        // 并失去左侧边框的解码器被固定在 maya HEAD (卡片)
        // (卡片 -border 测试 5/5 )冻结的交接也是纯粹的 缓
        // 存 点击 : freeze_range 构建相同的元素子 -tree
        // 下级 hash_id ,所以,我说 maya's 缓存 输入在零
        // 重置的 活式/ 冻结过渡中存活 -paint.
        //
        // 为何需要这样做: -flight 助手运行可以包含很多已
        // 包含的 -terminal write/edit/read
        // 牌( 每个子) -turn 在下次续作到达之前就已结算。
        // 没有密钥,则每张 帧 重建与重建 -lays 每一高个 正文
        // 整个运行——~21 ms/frame 3000美元
        // 3000个 -line 读取,乘以每张在运行中的固定牌。
        // 在转弯增加时,它会变成可见的滞后。
        //
        // 正文 配置 首次 ——它的树脂 -derived
        // `highlight_lines` 更改已交付的 HEIGHT (
        // FileRead 预留 a `▸ matches: …` 非时摘要
        // 行 -empty) 无需 变化 output().
        // size() . 之前解决的读物 -path Grep 登陆时缓存
        // 了没有突出显示;一旦 Grep的点击索引路径,则 正文 长一行,
        // 但 id+status+大小密钥不变。 maya 就会打碎 过期
        // (缩短) 存储单元 进入高点的预留槽, 移动下方的每一行,
        // 流血 过期 存储单元 (截图腐败). 归并 将突出符号插入到密钥中,
        // 这样高度就会改变薄荷,从而成为新的条目。
        // 正文 构建 缓存 (终端 仅限工具)。 终端 工具的 正文 字节
        // 已经 不可变 ,所以我们记忆O( 正文 (a) 内容下的配置
        // -address 捕获每个输入的构建取决于: id + 状态
        // + 输出大小 + grep -hits 签名( 输入)
        // Read/find_definition highlight_lines。
        // 点击时,我们复制了缓存的配置而不是重新 -parsing JSON
        // + 调值 -scanning diff - 转动 O( ) 正文
        // 大小)/ 缩放 帧 在一个漫长的 -flight 运行到每张结算卡
        // 的廉价副本。 字节 和新造一样 所以冻结的交接是纯的 maya 击
        maya::ToolBodyPreview::Config body;
        // 用于 终端 我们保留工具 正文 后面的a shared_ptr
        // (中文(简体) ). 不可变 对象 正文-config 缓存 并
        // 交给 maya 途经 body_shared ——无 -frame
        // 深层副本 正文 文本此处 OR 内 append_event's
        // 终端 关闭。 -terminal 工具 正文 仍然被新建 并搬
        // 进 `.body` 按数值(它变异于每 帧 ,所以没有什么可以
        // 分享的).
        std::shared_ptr<const maya::ToolBodyPreview::Config> body_sp;
        std::string body_key;
        const bool cacheable = tc.is_terminal();
        // 工具输出要显示多少。 预览是 缓存 KEY 的一部分,
        // 在查询后没有应用。 在 "preview" 下创建的缓
        // 存的 正文 , 一旦用户要求 "full" , 则不能交还,
        // 这正是 -the-fact 后 的 tweak 将
        // 引入的bug( 仅在 渲染 上, 情况更糟糕 )。
        const auto tool_out = agentty::ui_prefs::current().tool_output;
        const auto apply_tool_output =
            [tool_out](maya::ToolBodyPreview::Config c) {
                switch (tool_out) {
                    case agentty::ui_prefs::ToolOutput::Collapsed:
                        // 头文件 只有牌上写着 内容 运行,
                        // 以及它是否失败; 正文 事情就是这样
                        c.code_head = c.code_tail = 0;
                        c.bash_tail = c.read_head = 0;
                        c.show_all  = false;
                        break;
                    case agentty::ui_prefs::ToolOutput::Preview:
                        break;   // 调制的默认值为 ARE 预览
                    case agentty::ui_prefs::ToolOutput::Full:
                        c.show_all = true;
                        break;
                }
                return c;
            };
        if (cacheable) {
            body_key.reserve(tc.id.value.size() + grep_sig.size() + 48);
            body_key += "t:";
            body_key += tc.id.value;
            body_key.push_back('|');
            body_key += std::to_string(tc.status.index());
            body_key.push_back('|');
            body_key += std::to_string(tc.output().size());
            body_key.push_back('|');
            body_key += std::to_string(static_cast<int>(tool_out));
            body_key.push_back('|');
            body_key += grep_sig;
            if (auto hit = g_body_cache.with(
                    [&](BodyConfigCache& c) { return c.get(body_key); })) {
                body_sp = hit;  // SHARE 缓存( E) 不可变 ) 建造
            } else {
                body_sp = std::make_shared<const maya::ToolBodyPreview::Config>(
                    apply_tool_output(tool_body_preview_config(tc, &grep_hits)));
                g_body_cache.with(
                    [&](BodyConfigCache& c) { c.put(body_key, body_sp); });
            }
        } else {
            body = apply_tool_output(tool_body_preview_config(tc, &grep_hits));
        }

        // 事件-hash / 高度输入从任意一个读取 正文 我们做到了。
        const maya::ToolBodyPreview::Config& body_ref =
            body_sp ? *body_sp : body;

        // 密钥: 工具 -call id + 状态 + 输出大小 + 正文-height 输入,
        // 但并非由 output().size() (highlight_lines).
        // 常设 缓存 击打一次 终端 和 其亮点组稳定; running/pending
        // 事件 保持原样 -keyed (他们) 正文 仍然变异 帧 并会别名
        // a 过期 ) (英文) (英文(英文). blit).
        maya::CacheId event_hash_id;
        if (tc.is_terminal()) {
            maya::CacheIdBuilder kb;
            kb.add(std::string_view{"agentty.tool_event"})
              .add(std::string_view{tc.id.value})
              .add(static_cast<std::uint64_t>(tc.status.index()))
              .add(static_cast<std::uint64_t>(tc.output().size()))
              .add(static_cast<std::uint64_t>(body_ref.highlight_lines.size()));
            for (int hl : body_ref.highlight_lines)
                kb.add(static_cast<std::uint64_t>(hl));
            event_hash_id = kb.build();
        }

        cfg.events.push_back({
            .name            = tool_display_name(tc.name.value),
            .detail          = std::move(detail),
            // 被击落在 事件's HEADER 排队,为一 TERMINAL 工具,
            // 这是最后的, 不可变 (finished_at ——安全地排在一条承诺的行上。
            // NON-TERMINAL 工具 tool_elapsed
            // () 读作: steady_clock::now (,
            // 所以这个) TICKS 每堵墙 -second - 和运行的工具的
            // 头文件 排队坐着 上方 其不断进步 正文 一旦划线穿过 变成本土
            // 回滚缓冲区 计数秒重写承诺的行 {} maya 门
            // HardResets (scrollback_oracle t*-r*
            // -run * 追回。 相同 接口边界-immutability
            // 规则为 title/border/spinner 现场小组 头文件
            // 必须是行 字节-stable。发送 0(无持续时间) 存储
            // 单元 )而非则 -terminal 活期在 FOOTER's
            // total_elapsed ,这是下方的下边附件 接口边界。
            // 线条不会水平地在 终端 翻转, 因为脚印已经载有运行的总数。
            .elapsed_seconds = tc.is_terminal() ? tool_elapsed(tc) : 0.0f,
            .category_color  = tool_category_color(tc.name.value),
            .status          = tool_event_status(tc),
            // 终端 工具( ) : 正文 住在这里 body_shared (maya's
            // 终端 关闭通过参考来捕捉 -bump ,没有副本。 -terminal:
            // 将新建筑移动到 `.body` (对) -renders 再说了
            .body            = std::move(body),
            .body_shared     = std::move(body_sp),
            .hash_id         = event_hash_id,
        });
    }

    // 页脚：在面板的整个生命周期内显示，包括实时和
    //    。所以,当最后一个工具转换到完成时,
    //    面板的行数不会增加1。 跨过这个转换的高度
    //    稳定性就是使面板横跨 回滚缓冲区 接口边界
    //    铁路/边界碎片搁浅: maya diff 的
    //    行控件在 -place 排成一列的突变,
    //    但是高三角洲的面板上 已经部分是原生的
    //    回滚缓冲区 无法重写滚动的行。
    if (total > 0) {
        int failed = 0, rejected = 0;
        for (const auto& tc : tool_calls) {
            if (tc.is_failed())   ++failed;
            if (tc.is_rejected()) ++rejected;
        }
        const bool all_done = (done == total);
        maya::AgentTimelineFooter f;
        if (all_done) {
            f.glyph = "\xe2\x9c\x93";   // ✓
            f.text  = "done";
            f.color = success;
            if (failed > 0) {
                f.glyph = "\xe2\x9c\x97";           // ✗
                f.text  = std::to_string(failed) + " failed";
                f.color = danger;
            } else if (rejected > 0) {
                f.glyph = "\xe2\x8a\x98";           // ⊘
                f.text  = std::to_string(rejected) + " rejected";
                f.color = warn;
            }
        } else {
            // 电话 仅 动画 字节 在一个活面板。
            // 接口边界-safe 因为脚印是面板的 最后 横行,
            // 而任何工具都是非 -terminal ,
            // 面板是 最后 记录中的内容( 仅继续传言)
            // 数据流 每一个工具结果返回后。进步输出会增长
            // 正文 上方 脚步,所以脚步永远不能被推过 可视区域 上方,
            // 仍为动画, 按内容 数据流 面板下面,
            // 所有工具都是 终端 和这个分支 渲染 静态的...
            //
            // 格莱弗斯从 maya's 加载动画<Dots> 帧 表
            // (每张动画表层图的真相单一来源),而非手 -copied 数组。
            // agent_timeline 是一个 STATELESS
            // 纯 渲染 - 它不能坚持 maya::Spinner
            // 实例( 拥有 -frame 状态,必须推进到整个 帧 ,
            // 这个配置是从零开始重建的 帧 ,所以嵌入式 加载
            // 动画 将重置为 帧 每人0次 渲染 永远不要动。
            // 电话 持久化 时钟是 m.s.spinner 每页中,每页勾选一次 帧 输入
            // meta.cpp ; 东道主 线程 编号 frame_index (
            // a) 以下列方式计算: spinner_frame 我们把它映射到这里的共享格言集上
            constexpr auto sp = maya::detail::get_spinner_frames<
                maya::SpinnerStyle::Dots>();
            f.glyph = sp.frames[((spinner_frame % sp.count) + sp.count) % sp.count];
            f.text  = "running";
            f.color = muted;
        }
        f.summary = std::to_string(done) + "/" + std::to_string(total)
                  + (total == 1 ? " action   " : " actions   ")
                  + format_duration_compact(total_elapsed);
        cfg.footer = std::move(f);
    }

    // 标题和边框：左侧为 ACTIONS · done/total，右侧为
    //    (边界) -text-end : 现时 -running
    //    工具名称,或 已解决的已全部通过 —— 将已通过
    //    的两针分割到右边缘, 而不是离开它 -glued
    //    以动作计数。
    //
    // ── SCROLLBACK-SEAM IMMUTABILITY (现场直播) ─────────────────
    //
    // 该 SCROLLBACK INVARIANT (maya app.cpp
    // : 卷成本地的一行 终端 回滚缓冲区 谓 不可变.
    // agentty 保持一个 TALL 现场直播 末尾 时 流式传输 ,
    // 所以一个面板的 TOP 列——边框标题行和第一行 event/stats
    // 横行——可以横行 可视区域 足够多的流言 数据流 标题行承诺 回滚缓冲区,
    // 任意 以后改为 字节 是一个承诺 -row 重写 : maya
    // 门检测前缀不匹配,在增长时 帧 造成它,只能通过破坏恢复
    // HardReset (\x1b[2J\x1 b][3J擦擦+]).
    // 重绘 ) 改为: -strands 面板的铬 上一层屏幕——
    // 报道的
    // "stacked ACTIONS · 1/1 MUTATE fragments in scrollback" 腐败。
    //
    // 动画的头文件输入正中三:
    //   • 国家 `done/total` 计数器( ticks) 1/2 → 2/2 工具完成时,
    //   • `title_end`(运行中的工具名称,或活过),
    //   • 加载动画 `frame`(活体成员Pending/Running 状态格言)
    //     第一次 第一次 事件 行,直坐于标题下).
    // 用于 LIVE (任何项目) -tool-non-terminal
    // ) 每块板可以变异一个已经执行的行。所以冻结所有三至
    // 批次 -stable 面板启用时的窗体: 放下计数,
    // 勾选 title_end 动画 加载动画。小组仍在更新其
    // BOTTOM (新设) 事件 排行,脚步运行~done翻
    // 转)——所有附件下方 接口边界 ,该 不变量 许可证。
    // 标题：在面板整个生命周期内保持字节稳定的唯一字符串。
    //    生命周期 生活,定居,和冻。 加载-bearing 不变量
    //    : 冻结的交接转换 LIVE 面板进入全部 -terminal
    //    (已确定)小组到位,如果其名称有差异,甚至一个 字节
    //    冻结 REWRITES 标题行——一旦该行进入原生 回滚缓冲区 ,
    //    是执着的 -row 重写旅行 maya 进入一个破坏性的
    //    大门 HardReset (无)
    //    "card duplicated one screen up" 腐败问题)。
    //    故生而安居乐业 必须 渲染 标题 字节 我们携带 NO
    //    计数和 NO 标题中已过期(动画/活字与已解决的活字不同);
    //    done/total + 持续时间在 FOOTER ,
    //    它总是下方的下边附件 接口边界。该词 加载动画
    //    `frame` 任何非为零 -yet-all-terminal
    //    面板这样顶端 事件 排的状态格莱弗不能动画一个承诺的排.
    std::string title = " A C T I O N S ";
    std::string title_end;

    bool all_terminal_title = total > 0;
    for (const auto& tc : tool_calls)
        if (!tc.is_terminal()) { all_terminal_title = false; break; }

    cfg.title        = std::move(title);
    cfg.title_end    = std::move(title_end);
    // 边框颜色是其中的一部分 每个 面板行的 字节 (
    // left/right 边框 存储单元 + 标题行),
    // 因此它属于与标题相同的生命周期- 稳定性规则: 用于互换
    // 的活页- 固定翻转 rail_color ~ 沉默,
    // 使每排被害的棋盘 越过 接口边界 - 一种风格 -only
    // 承诺 -row 重写闸门对格子重写( 元件覆盖样式
    // ID) 的处理方式相同。 固定的音效为整齐 生命周期
    // 活的由脚步手持 `● running` 和每笔 -event
    // 类别颜色,两者均为 接口边界-safe (脚印 = 下附着,
    // 类别颜色从第一个开始固定) 渲染).
    cfg.border_color = muted;
    // 帧 = -1 ⇒ maya 渲染 STATIC 状态格言(
    // * 运行, 0) 待处理 继续 事件 头文件 排行而不是大字报
    // 加载动画 这些排可以坐到 接口边界 (工具进步增加) 正文
    // 他们必定是 字节-stable 冻死大号 帧 (`⠋`
    // 永远) "stuck"。一个固体。。 不可变 并读作故意的。
    // 动静 加载动画 生活在一个 FOOTER 格莱弗( Q) 接口边界-safe ,见上文)。
    //
    // EXCEPTION - 电话 TAIL-RUNNING
    // 大小写(绝对常见的顺序流): 仅 无 -terminal
    // 事件 是那个 最后 其一,其一,该 头文件 排在记录
    // 底部的几行内( 其自己的直播) 正文 正在消除 -capped ,
    // 只选择页脚+边框 渲染 因为它不能越过 可视区域 顶端—
    // —动画其格言完全等于 接口边界-safe 以重写已到达该行的。
    // 递回真实的 帧 所以,在 -flight 动作明显旋转;
    // 终端 事件 ⁇ / ⁇ 是 帧-independent 动画,
    // 没有其他动画。 OTHER 形状( 非)
    // -terminal 事件 与 事件 之后的-平行批次)
    // 保持静态 : 头文件 能够真正坐到 接口边界.
    // 睡在房间里 可视区域 : 时间很短 终端 (
    // 甲骨文测试运行于60x18) 边界距离仍然可以超过
    // 可视区域 保持保守的静态胶片
    if (!all_terminal_title) {
        const bool tail_running =
            !tool_calls.empty()
            && !tool_calls.back().is_terminal()
            && [&] {
                   for (std::size_t i = 0; i + 1 < tool_calls.size(); ++i)
                       if (!tool_calls[i].is_terminal()) return false;
                   return true;
               }();
        const int H = maya::available_height();
        bool animate = tail_running && H >= 20;
        // 相同的约束,计量。 末尾-running 头文件
        // 最多坐落在卡底上方的 H - 12 行( 盖上)
        // 正文 + footer + 边框),这就是旋转安全的原因。
        // 当旋转时, 整个 活牌没有比这高的,每个 头文件
        // 里面离我们只有一公里 可视区域 上面,所以排队和平
        // 行工具也可以旋转,而不是坐落在静态点上。
        // 在任何工具都运行时,卡片是记录片中的最后一件事,
        // 因此它的高度是重要的距离。
        if (!animate && H >= 20) {
            maya::AgentTimeline::Config probe = cfg;
            probe.frame = -1;   // 相同的格子宽度,所以同样的高度
            const auto sz = maya::measure_element(
                maya::AgentTimeline{std::move(probe)}.build(),
                std::max(20, maya::available_width() - 4));
            animate = sz.height.value <= H - 12;
        }
        if (!animate) cfg.frame = -1;
    }

    // 小组讨论会 -level 缓存 键 全部-TERMINAL 批次。
    // 已解决的工具 字节 已经 不可变 也没有铬动画(每经一次都是最后的,
    // 没有) 加载动画 ),所以,(a) ,(a, ) maya 能够
    // 打碎整个面板 存储单元 横跨 帧 而不是重新 -laying-out
    // 内容 -address 面板形状取决于:工具计数+铁路颜色+每个工具的ID,
    // 状态,输出大小,以及 渲染 键。当任何工具仍在使用时,
    // 留空 -flight 所以,我们 spinner/elapsed
    // 继续动画 冻结的交接保持纯度 缓存 击出是因为 freeze_range
    // 在同一键下建立相同的批次。
    {
        bool all_terminal = total > 0;
        for (const auto& tc : tool_calls)
            if (!tc.is_terminal()) { all_terminal = false; break; }
        if (all_terminal) {
            maya::CacheIdBuilder kb;
            kb.add(std::string_view{"agentty.tool_panel"})
              .add(static_cast<std::uint64_t>(tool_calls.size()))
              .add(static_cast<std::uint64_t>(rail_color.kind()))
              .add(static_cast<std::uint64_t>(rail_color.raw_r()))
              .add(static_cast<std::uint64_t>(rail_color.raw_g()))
              .add(static_cast<std::uint64_t>(rail_color.raw_b()))
              // 工具( E) -output 预览更改 正文's
              // HEIGHT 因此它必须按键每层 记住一个已建的卡片,
              // 否则翻转它 重绘 直到下一个状态改变,一切都不会改变。
              .add(static_cast<std::uint64_t>(
                  agentty::ui_prefs::current().tool_output));
            for (const auto& tc : tool_calls) {
                kb.add(std::string_view{tc.id.value})
                  .add(static_cast<std::uint64_t>(tc.status.index()))
                  .add(static_cast<std::uint64_t>(tc.output().size()))
                  .add(tc.compute_render_key());
            }
            cfg.hash_id = kb.build();
        }
    }
    return cfg;
}

maya::Element agent_timeline_element(std::span<const ToolUse> tool_calls,
                                     int spinner_frame,
                                     maya::Color rail_color) {
    // 全部都给我 -terminal 批次为 字节-stable :每个工具的状态,
    // 输出,和参数都是固定的,没有 spinner/elapsed 反动画
    // (所有持续时间均为最终时间)。 整个 已建元素为内容 -addressed
    // 之后每一次都一样 帧。非 -terminal 批次载a
    // running/pending 工具( 使用后计数器, 动画 加载动画 途经
    // `frame` 和。 和 简 况 必须 重建每个 帧 - 倒倒直过。
    bool all_terminal = !tool_calls.empty();
    for (const auto& tc : tool_calls) {
        if (!tc.is_terminal()) { all_terminal = false; break; }
    }
    if (!all_terminal) {
        return maya::AgentTimeline{
            agent_timeline_config(tool_calls, spinner_frame, rail_color)}.build();
    }

    // 内容 -address :工具计数+铁路颜色+每个工具的ID,状态,
    // 输出大小,以及 渲染 键( 键) 归并 args_streaming /
    // 进度/ 输出大小—— 每一份输入面板 正文 取决于).
    // spinner_frame 故意的 故意的,故意的,是故意的,这是故意的
    // EXCLUDED :一个已解决的面板没有动画格,所以 帧 指数不影响它的 字节
    // 包括它会打败 缓存 (索引每秒勾选) 帧).
    std::string key;
    key.reserve(tool_calls.size() * 40 + 16);
    key += "p|";
    key += std::to_string(static_cast<unsigned>(rail_color.kind()));
    key.push_back('.');
    key += std::to_string(rail_color.raw_r());
    key.push_back('.');
    key += std::to_string(rail_color.raw_g());
    key.push_back('.');
    key += std::to_string(rail_color.raw_b());
    key.push_back('|');
    key += std::to_string(tool_calls.size());
    for (const auto& tc : tool_calls) {
        key.push_back('|');
        key += tc.id.value;
        key.push_back(':');
        key += std::to_string(tc.status.index());
        key.push_back(':');
        key += std::to_string(tc.output().size());
        key.push_back(':');
        key += std::to_string(tc.compute_render_key());
    }
    key.push_back('#');
    key += std::to_string(
        static_cast<int>(agentty::ui_prefs::current().tool_output));

    // 电话 缓存 返回一个 OWNING 手柄,而不是指向自己: 驱逐
    // LRU 可以在下一个时放弃条目 put() ,并设置一个
    // `const shared_ptr<...>*` 复制到地图上就会在那里缠绕。
    // shared_ptr 出局是一次重计,还是没有 源码树 ⁇ .
    // loop_bound<T> * 拒绝指针 -returning 形状直立。
    if (auto hit = g_panel_cache.with([&](PanelElementCache& c) {
            const std::shared_ptr<const maya::Element>* p = c.get(key);
            return p ? *p : std::shared_ptr<const maya::Element>{};
        }))
        return maya::Element{hit};   // 共享手柄:重计凸起,无源码树副本

    auto el = std::make_shared<const maya::Element>(
        maya::AgentTimeline{
            agent_timeline_config(tool_calls, spinner_frame, rail_color)}.build());
    g_panel_cache.with([&](PanelElementCache& c) { c.put(key, el); });
    return maya::Element{el};
}

maya::Element agent_timeline_element_memoized(std::string_view msg_id,
                                              std::uint64_t render_key,
                                              std::span<const ToolUse> tool_calls,
                                              int spinner_frame,
                                              maya::Color rail_color) {
    // 仅 SETTLED (所有项目) -terminal )
    // 批次可回忆: 运行 / 待处理 工具动画 a 加载动画 + 活过,
    // 所以它 render_key (因此其产出) 帧 - 电话
    // 记忆缓存 只会错过 -and-store 探测到前面,
    // 直落到每架 -frame 构建, 与基础函数完全相同。
    bool all_terminal = !tool_calls.empty();
    for (const auto& tc : tool_calls) {
        if (!tc.is_terminal()) { all_terminal = false; break; }
    }
    if (!all_terminal) {
        return maya::AgentTimeline{
            agent_timeline_config(tool_calls, spinner_frame, rail_color)}.build();
    }

    // 快主键 : MessageId + render_key。一个已结算的
    // 子公司 -turn's render_key 冰冻,所以这是第一次的永久
    // 打击 帧 - 比较一个宾特64, NO 内容 -key 字符串已构建和(
    // 通过多样的查找) NO 临时字符串。这是整个点:它删除 O( 工具)
    // std::to_string 键 -build 连一个
    // g_panel_cache 命中支付, 也就是仍然随运行深度而缩放的( O(
    // 运行) ) -length 键 -builds 百分比 帧).
    // 自己解决, 原因与 g_panel_cache 上节。
    if (auto hit = g_panel_render_memo.with([&](PanelRenderMemo& c) {
            const std::shared_ptr<const maya::Element>* p = c.get(msg_id, render_key);
            return p ? *p : std::shared_ptr<const maya::Element>{};
        }))
        return maya::Element{hit};   // 共享手柄: 重计折叠

    // 密钥: 通过内容构建 -addressed 路径( 本身点击)
    // g_panel_cache 横跨子 -turns 批次相同 字节, e.g.
    // ),然后将生成的手柄记录在快速键下,因此以后每一次 帧 跳
    // 过密钥 -build。将 相同 共享控点 g_panel_cache
    // 返回时保持一个稳定的控制区块—— maya 擦亮
    // 它的颜色 存储单元 横 -frame.
    auto el = std::make_shared<const maya::Element>(
        agent_timeline_element(tool_calls, spinner_frame, rail_color));
    g_panel_render_memo.with(
        [&](PanelRenderMemo& c) { c.put(msg_id, render_key, el); });
    return maya::Element{el};
}

} // namespace agentty::ui
