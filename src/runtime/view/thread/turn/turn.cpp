#include "agentty/runtime/view/thread/turn/turn.hpp"

#include "agentty/util/logx.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <maya/widget/agent_timeline.hpp>
#include <maya/widget/markdown.hpp>
#include <maya/widget/thinking.hpp>  // reasoning/thinking 块
#include <maya/widget/reasoning.hpp>  // ReasoningStream (流式传输 推理块).
#include "agentty/domain/ui_live.hpp"  // 运动/思考/ 表达式-width预览
#include <maya/core/render_context.hpp> // available_height (调整规模) -shrink 探测)
#include <maya/core/anim_clock.hpp>     // maya::anim_now_ms( 测试钟下的决定)
#include <maya/render/cache_id.hpp>
#include <maya/render/renderer.hpp> // build_layout_tree / layout::compute 数据
#include <maya/layout/yoga.hpp>     // maya::layout::compute / LayoutNode 数据
#include <maya/platform/io.hpp>
#include <maya/style/theme.hpp>
#include <maya/core/motion.hpp>   // anim::keep_animating — 帧 请求

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/model_name.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/agent_timeline.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include "agent_timeline/tool_body_common.hpp"   // stream_card_rows_bound
#include "agentty/runtime/view/cache.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/permission.hpp"
#include "agentty/runtime/view/thread/seam.hpp"

namespace agentty::ui {

namespace {

// 缓存的 Markdown 渲染。此处唯一保留的 Element 返回辅助函数。
//    agentty - 严格地说,因为十字架 -frame 缓存 国家
//    生活在 StreamingMarkdown 部件实例,
//    我们一直保持到整个 帧 所以它块 缓存 活下来了
//
//    单身 渲染 路径: StreamingMarkdown 部件用于 live
//    和 已解析信件。 部件的预置 -finish 输出( 前缀)
//    ComponentElement + 末尾 包在其中的元素 vstack.gap
//    (1) 总高度与总高度略有不同 -shot maya::markdown()
//    解析器 输出( 同一 vstack 包装下有平面块,
//    但无) ComponentElement 接口边界。。 之间撕裂,
//    在 StreamFinished 以~3行移动画布,通过每行画布传播 -row 迪夫,
//    把作曲家留在别的地方 终端 行——可见于
//    第一次按键时输入框被向下推，上方出现重复输入框。
//
//
//    继续守着 流式传输 部件保持高度稳定 流式传输 ——闲置过渡.
//    set_content 与 字节-identical 字节 内部是否 -op;
//    build() 返回时 cached_build_ 当无物污秽的时候,
//    -frame 成本与最终路径相同。 末尾 内容 -parses
//    在每个 帧 也给最后的留言,但这是单内线 解析 上几个 字节 -
//    便宜的 - 便宜点 - 便宜一点 - 便宜些 - lowe.
// 哪个 正文 发文日期 a StreamingMarkdown 插槽 渲染。
// 该词 ANSWER 路径( 默认) 数据流 msg.text +
// streaming_text + pending_stream
// 透过信息本身 缓存 插槽。 REASONING 路径 数据流
// msg.reasoning_display_text (a) 通过 SEPARATE ,
// 兄弟们 缓存 插槽( N) MessageId 后缀为 "#r" )所以推理得到
// 相同的平滑 逐步显示 + 以普通文本形式结算机械—— 它 数据流 就像普通的
// 文字一样, 缓存 象普通文字一样,在和解后完全保留。 归并。两个槽是独立的部件,
// 所以答案 正文 推理界不会因为一个而争吵 逐步显示 光标。
enum class MdView : std::uint8_t { Answer, Reasoning };

// MessageId 会 议 日 程 和 议 程 缓存 时间档 `view` 信件
// 中 `mid`。回答使用电子邮件 id 逐字记录; 理由附件 "#r" 因此它
// 生活在自己的槽中(冻结时与解答槽一起掉落——见 frozen.cpp).
inline MessageId md_slot_id(const MessageId& mid, MdView view) {
    return view == MdView::Reasoning ? MessageId{mid.value + "#r"} : mid;
}

// 应用流言 -width 盖到已建 正文.
//
// 一个读量:长行读取难度可观,因为眼睛在回程扫荡
// 中失去位置,一个最大值的终端是200+列.
// 0表示没有封顶,这就是为什么这是数字而不是选择——
// 有用的值是一个连续体.
//
// 用作 MAX ,从不设固定宽度:狭宽 终端 保持
// 每根柱子,只有到 BODY ——工具板,
// diffs和表格是结构化输出,其列意味着什么,因此,
// 挤压它们以利其传播,会非常糟糕.
[[nodiscard]] maya::Element prose_capped(maya::Element body) {
    using namespace maya::dsl;
    const int cap = agentty::ui_prefs::current().prose_width;
    if (cap <= 0) return body;
    // 限制柱子 标记的设置, 让一条后路灵活 存储
    // 单元 左转 -aligned 而不是以中心为中心:
    // 铁路, 格律和上面的每一转角 都从左边开始,
    // 和流传在他们的脚本读作不同的栏目 在页
    // 面。
    //
    // `width` 是一个 CEILING 练习中,
    // 因为行在自动内 -width 链(结) ~ 转 ~
    // 正文): a 终端 宽度小于顶部的弹性行 `cap` 和固定的,
    // 存储单元 以它为缩水,而不以它为溢水。
    return h(v(std::move(body)) | width(cap), v() | grow_<1>).build();
}

maya::Element cached_markdown_for(const Message& msg, const Model& m,
                                  MdView view = MdView::Answer) {
    const MessageId slot_id = md_slot_id(msg.id, view);
    const bool reasoning_view = view == MdView::Reasoning;
    // 生命周期 -aware 缓存 访问(见 cache.hpp 分区理由).
    // 消息的磁盘槽 LOAD-BEARING 动画状态—
    // — StreamingMarkdown 逐步显示 部件
    // + 推迟记账—— 确切地说, 仍在移动: 已启动 传输
    // 字节 到达 OR 部件本身仍然在动画中( 使用/
    // 最终确定斜坡/ 光标滑翔/ 自动同步) 解析。
    // 打开窗口的插槽会破坏 逐步显示 中点 -glide 拖住打字机,
    // 所以一定是 PINNED 完全排空后,
    // 槽就纯净了 渲染 记忆缓存 阶段都结束了,直到冻结下来。
    //
    // 我们看不出来部件的帖子 -update 动画
    // 状态在我们进入槽前,所以在前面的信号上标注:
    // 直播 传输 字节 ,或一个自上一个图像中报告
    // 本身的已有部件 帧。既未确定又未显示(在冻结时滴
    // 入)的信息。上游的形状与 turn.cpp's
    // `subturn_stably_keyable`
    // 否定——活泼有 一个 定义。
    // 理由是"live",而数据流运行AND,此消息尚未产生输出(没有回答的旁白,
    // 没有工具调用,文本块打开)——一旦它采取行动(tool_calls)或回答,
    // 字节就不再出现推理,所以逐步显示必须完成,头文件翻转为"Reasoned".
    // Mirrers reasoning_slot的
    // `active`.
    const bool reasoning_produced_output =
        !msg.text.empty() || !msg.streaming_text.empty()
        || !msg.tool_calls.empty() || msg.text_block_closed;
    const bool reasoning_active =
        m.s.is_streaming() && !reasoning_produced_output;
    const bool has_live_bytes = reasoning_view
        ? (!msg.streaming_text.empty() || !msg.pending_stream.empty()
           || reasoning_active)
        : (!msg.streaming_text.empty() || !msg.pending_stream.empty());
    const auto& probe = m.ui.view_cache; // 偷看,没有touch/reorder
    const bool widget_animating = [&] {
        // is_pinned 是一个廉价的凸轮检索;
        // 如果插槽已经从上一个套位被固定 帧 一直坚持到
        // 水流干涸为止 -update 如果它解决了或者
        // 缺席了 我们只有在信息携带时才被扣 字节.
        return probe.is_pinned(m.d.current.id, slot_id);
    }();
    const bool want_pin = has_live_bytes || widget_animating;

    auto& cache = want_pin
        ? m.ui.view_cache.message_md_live(m.d.current.id, slot_id)
        : m.ui.view_cache.message_md    (m.d.current.id, slot_id);

    bool fresh_widget = false;
    if (!cache.streaming) {
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
        fresh_widget = true;
        // 逐步显示 距离 -smoothed 绑定 -lag 光标(maya
        // RateCursor) 光标显示在 / drain_secs 积压时,
        // 所以它 TRACKS 的自有速度有固定的时间滞后, 而 低 -passes
        // 的 速度是 宽度 传输 的滑动, 而不是 传送。 参数 :
        //   • floor_cps = = 数据 MINIMUM 逐步显示 速度,还有小滴在排出
        //     硂 slow/local 模型一次不在一个字符中英寸。
        //     不 上限——快速模型显示 FASTER ,
        //     以自己的交付率(光标跟踪 传输 ),所以,
        //     逐步显示 永远不要落下 和垃圾堆在定居。
        //     cp/s 是一个可读最小值。
        //   • drain_secs = 目标 LAG : 活的边缘后面有多远
        //     光标的乘数, 以秒计. rate = 积压 / drain_secs 拿着
        //     逐步显示 ~drain_secs 后面的 传输 现场 传输 自己的速度。
        //
        // 重排为45 cps / 040 s(为90 / 0.15).
        // real models. 数据流 输入 BURSTS 空闲
        // 空隙( 已显示: ~ 57%) 帧 光标被冻结 AT 边缘在
        // 90/0.15 水分排尽了每个肥大三角洲 然后闲坐到下一个站
        // 可见的停站 -and-go ) 低层+较大滞后保持连续 缓冲区 因此,
        // 光标 GLIDES 透過空闲的空隙而不是冻结( 相同的配置 :
        // < 1% 闲置) 帧 时间 45/0.40 . 一种做法的
        // 旧理由 TINY 时差是工具的硬边界 -snapped ,
        // 以 线条表示的。 snap_reveal_to_edge(
        // glide_ms =150)\u2014 a 界限 可见 滑翔,
        // 不是贴面,2014年,这样比较稳定 -state 积压的也顺利地排尽了,
        // 而且 回滚缓冲区 滑翔机在150米以内着陆,以保持安全(
        // scrollback_oracle_test 绿色).
        cache.streaming->set_reveal_pacing(/*floor_cps=*/45.0,
                                           /*lead_secs=*/0.40);
        // 适应性地板:自动 -tune 编号 逐步显示 速度到 传输 观测
        // 到的速率(clammped 25.180 cps) 所以滑翔机平滑
        // 地跨过 不同模式的吞吐量,没有手 -picked 恒定 — 慢的
        // 本地模型不会冻结( 运行时的曲线超过 传输 快速的主持人不会落后
        // 45/0.40 上面是寒冷的,我说, -start 种子,
        // 直到估计值变暖; drain_secs (滞后) 缓冲区 )仍然适用。
        cache.streaming->set_reveal_adaptive(true, /*分钟*/25.0, /*最大数*/180.0);

        // REASONING 已按进度 SPEED ,同时保留动画幻灯片。
        // 提供商将推理作为摘要,常常在以下地点登陆: one/two
        // 思维窗口末端的大三角洲,所以光标会同时看到大量积压。
        // 逐步显示 引擎有 NO 巡航 -speed 顶点 :
        // 移动在 backlog/drain_secs 紧紧地按[min,
        // max]和动画质量(快速) scramble/ghost SLIDE 永远不要贴上 由
        // 来自 rate_tau 低级 -pass - 也就是说, INDEPENDENT 速度带。
        // 所以我们将CEILING提升到答案路径之上(解释
        // 是批量的,用户读得比流言快的可淡化的内容),
        // 并迅速保持地板的brisk如此短的推理类型.
        //
        // 电话 LAG 匹配答案路径(0.40),这是固定的:0.28,
        // 理由是
        // 保持较小显示延迟，让光标追上传输进度，避免在推理转回答时一次性倾倒积压文字。
        // 这是上面的答案路径在0.15时尝试并退休的同样论点,
        // 其特征是: 紧滞的滞后导致没有 缓冲区 ,所以光标排水 每
        // 个肥三角洲 然后坐 IDLE 57%的边缘 帧 冻结时间:
        // 90/0.15 , < 1% 时 45/0.40
        // ) 原因出现 BIGGER 流言比流言蜚语少,
        // 所以短 缓冲区 咬得更紧,而不是更少——这是所报告的
        // "slow and bursty" 推理:冲刺,然后是摊位.
        //
        // 它所保护的垃圾场 已经是其他的界限了:工具边界从这里穿过
        // snap_reveal_to_edge (150) 和推理-
        // > 答案 接口边界 通过 request_finalize (
        // 220) 武装在以下 -- -- 两者都受约束 可见 滑翔而不是贴面。
        // (那个) 接口边界 斜坡为新 : 此注释先前声称 接口边界 已经
        // 排尽 request_finalize (160) 原为
        // ANSWER 路径的机制。 没有任何东西可以限制推理。 )
        //
        // 0.55,不是0.40,还有一半 -step 关键所在。在推理定律上
        // ( reveal_stream_gate_reasoning ),
        // 在滞后窗口上测量:0.40 使光标闲置 AT 2.9%的活体边缘 帧
        // 0.55对1.6%,0.70对0.9% -- -- 和结束
        // -of-reasoning 排水管的拉伸为1.9s / 2.5s /
        // 3.0s。 Stalls 是用户看到的中点。 -stream 排水沟
        // 就是他们在 接口边界 ,因此这个位置坐落在膝盖上,而不是两端。
        // 较大的后退也意味着滑翔滑翔(最大11 - > 9) 字节 显示( )
        // 帧 ),因为利率是 backlog/lag.
        if (reasoning_view) {
            cache.streaming->set_reveal_pacing(/*floor_cps=*/60.0,
                                               /*lead_secs=*/0.55);
            cache.streaming->set_reveal_adaptive(true, /*分钟*/45.0,
                                                 /*最大数*/280.0);
        }
    }

    // 逐步显示 on/off 谓 POLICY ——来源于 终端
    // 和圆圈——不同于上面的间隔种子,它们从未改变。
    // -applied 只有在 ANSWER 改变,
    // 所以一个呼叫者 故意推翻 标志 已存在的部件( 测试前)
    // -seed reveal_fx = 假以测量已定居高度)保持其超度,
    // 而真正的政策变化(出口) AGENTTY_REVEAL 仍然
    // 需要我们 副作用 用于下一个消息的部件。
    //
    // DEFAULT: ON EVERYWHERE 中,
    // tmux 包含。 逐步显示 重写现场 末尾 每一动画的
    // 行 帧 并且现在 可视区域-blind (部件无法看到滚动位置,
    // 所以只能安全触摸下行)。在 tmux 下,这些重写用于从 mux
    // 网格中解析的文字和 GHOST ——文字重叠上行,字句在中段分拆.
    // -token ——这就是为什么默认是 终端-derived
    // (0ebcf5f9), 缩写为: 末尾-rewrite 路径
    // 已经实现 可视区域-safe ,所以多轴器不再获得退化的默认
    // 值: 各地的行为都比分 -brain 一种沉默的区别 终端.
    // 如果一个给定 tmux/terminal 组合还是鬼魂,
    // `AGENTTY_REVEAL=0` 关掉它,不用重建
    //
    // 信封总是赢,双向:
    //   • AGENTTY_REVEAL ——主人(off QQ) 否 ——主主 ——主 ——主人 ——主 主 ——主——主 : 逐步显示 完全没有)
    //   • AGENTTY_REVEAL_TYPEWRITER——渐进式剪辑
    //   • AGENTTY_REVEAL_DECORATE - 电话 scramble/gradient/caret 覆盖面板
    // 每一个接受0/false/off/no=关闭;其他任何=开启;
    // 未设置=默认值.
    {
        // 默认不再因 终端 : 没有 tmux 探测器,
        // 没有 AGENTTY_UNDER_TEST 特例都一样
        // 套房只读到这个 来给一个 终端-derived 默认,
        // 没有了,测试要 逐步显示 OFF 仍在预 -seeds
        // 编号 标志 自行部件( N) midrun_wire_test
        // 从来没有被打过
        // 发射时读到一次 m.env (env.cpp).
        const bool master     = m.env.reveal;
        // 电话 MOTION 双层门,在洞下 标志 .
        // 减少保留进步的剪辑——文字走进是关于进步的信息
        // ——并放下装饰性的格莱弗圆珠,而这个不是.
        // 关闭两个:一个打字机逐步显示 与前部障碍
        // 是真正不愉快的,所以这是在它成为首选之前
        // 的无障碍开关,它必须实际到达部件.
        const bool want_fx    = master && agentty::ui_prefs::animations_on();
        const bool want_deco  = master && agentty::ui_prefs::reveal_decoration_on();
        // 为每个都写 NEWLY CONSTRUCTED 部件——不按 帧
        // 故意推翻 标志 有关现有部件的( ) midrun_wire_test
        // 预览 -seeds reveal_fx =false to
        // imprision settle high)保持它的覆盖;
        // 而不是每个进程一次,所以每个消息的部件都得到策略而不是仅仅第一个.
        //
        // ... EXCEPT 当预览本身改变时, 即一个实例,
        // 一个现场设置必须击败规则: 关闭中途运动的用户
        // -stream 就是要我们停下来 当前 ,而不是在下一个消息。
        //
        // 比较什么 此 部件配置为 (M)
        // cache.applied_motion ),
        // 而不是每个呼叫者共享的批量。此函数运行于每个调用者若干次 帧
        // - 推理 "#r" 车道,答题车道,每个车道1 -turn
        // - 来个比较 -and-reset 锁链由它先到达的部件和
        // 后来到达的部件所消耗 帧 读取 "unchanged"
        // 中间移动 -stream 然后应用到推理块,并留下了答案动画:
        // 以上段落的同样"only the first widget gets the policy"
        // 未能防止。
        //
        // 人数 -widget 也不需要,请不要 帧 语义,
        // 一个全局变化的语义 -this-frame 标志 是否为: 标志
        // 粘度是任何不发布任何 帧 ,所以它会通过测试和面板泄漏 -only 帧.
        // 当前拥有的状态与实际生效的状态不同。
        // 真实状况,没有这种危险。
        const int motion_now = static_cast<int>(agentty::ui_prefs::current().motion);
        // -0 警卫: -1 表示"this widget's policy was never set by us".
        // `fresh_widget`已经覆盖了刚刚创建的WE,
        // 所以唯一能够到达这里的另一种方法是从外部(一个测试楼,
        // 一个槽直接用来测量一个固定高度)种子的部件。
        // 将未标记的过期 处理为将覆盖一个有意设置,所以一个变化
        // 意味着一个KNOWN 级别是不同的 — — 不是
        // 一个缺席的。
        const bool motion_changed =
            cache.applied_motion >= 0 && cache.applied_motion != motion_now;

        if (fresh_widget || motion_changed) {
            cache.streaming->set_reveal_fx(
                want_fx && m.env.reveal_typewriter);
            cache.streaming->set_reveal_decorate(
                want_deco && m.env.reveal_decorate);
            // 印上我们刚刚配置的图章,所以下一个 帧 内容
            // -applies 仅用于真正的动作。在分支内无条件: 新的
            // 部件有 applied_motion = -1 并且必须记录其水平,
            // 否则将重新 -apply 时值 帧 转弯的一生。
            cache.applied_motion = motion_now;
        }
    }

    // 选择源头 字节 费用 此 帧。该词 逐步显示 光标必须看到 每个 字节 已经
    // 从 传输 以便它依自己的墙钟而顺利地节奏。 `streaming_text` ,
    // 可见文本只能当 meta.cpp 选中值 处理函数 滴滴声
    // pending_stream → streaming_text (33 ms
    // 同步 / 100 ms 非同步 / 更多 ) SSH。在勾选之间,
    // 每个文本三角洲仍然强制要求a 渲染 (FPS =0),但将" ⁇ "( ⁇ ),
    // " ⁇ "," ⁇ ( ⁇ )",
    // "" ⁇
    // ". streaming_text 就不会长大了 所以... 逐步显示 没有任何新内容可以显示——文本坐落 STUCK 直到下一个滴滴一个块,然后 JUMPED。这就是 "
    // md gets stuck then bursts" 结巴,它的结构:三角洲饲料
    // pending_stream ,滴入的种子, 滴入的信号 streaming_text
    // 仅限 Tick , 视图只读 streaming_text。阅读文本 +
    // streaming_text + pending_stream 造墙
    // -clock 逐步显示 光标 唯一 显示所有到达的缓冲器 字节 ,因此可见输出在
    // kRevealCharsPerSec 独立于勾选卡登斯(credence).
    // (中文(简体) ). meta.cpp 滴滴还在流 流着 字节 输入
    // streaming_text 解决路径会让他们承担责任的,但不再控制用户
    // SEES.)
    //
    //   • 解决: msg.text 举行最后的 正文, streaming_text +
    //     pending_stream 空无所着.
    //   • 中点 -sub-turn-2: msg.text 拿着 PRIOR 子目录 -turn 已经解决
    //     正文; streaming_text + pending_stream 坚持
    //     住 -flight 随其后 -up 字节 供养一切,让活人 末尾 不断增长.
    //   • -turn-1 流式传输:msg.text空,streaming_text +
    //     pending_stream 增长。
    // 加入的队伍 缓冲区 缓存于 MessageMdCache 所以,
    // 我们 string_view 我们交给 set_content_async 在整个 调用.
    //
    // 大小 -based 无 -op 快速( T) -path. Reveal_fx
    // 60岁动画 FPS; 字节 达到 10-30 / s,所以 > 90% 帧
    // 没有源代码 -size 换衣服,没有这名警卫,我们就 -concat msg.text
    // + streaming_text + pending_stream (O(N) 偶
    // 联+微量) 和 set_content_async 将结果( O( N) 中)
    // 乘以每 帧 用于 -flight 转身,50元 KB 子目录 -turn-2
    // 正文 ~100 ~ ~ = = $100 = ~ $ KB 备注 内存 每带宽
    // 帧 来比较三个大小: 如果没有增长, 来源 字节 已经 字节-identical
    // 直接跳到这里 build().
    // 内 REASONING 模式 正文 是一个单一的生长字符串( Q)
    // reasoning_display_text : 没有 text/streaming/pending 3 个 -buffer.
    // 把它塞进 持久化 combined_source 缓冲区 (不 当地),
    // 因此, set_content_async's string_view 横跨有效 帧 ,
    // 并按其大小显示,如答案路径。
    if (reasoning_view) {
        // 理由说明 正文 是原始推理文本( 简写为 ReasoningStream
        // 铬供应 + 暗的铁路。 持久化 缓冲区 这么说吧
        // set_content_async's string_view 横跨有效 帧.
        std::string_view rsrc = msg.reasoning_display_text();
        if (rsrc != cache.combined_source)
            cache.combined_source.assign(rsrc);
    }

    const bool sizes_unchanged = reasoning_view
      ? (cache.last_text_size == cache.combined_source.size())
      : (cache.last_text_size      == msg.text.size()
     && cache.last_streaming_size == msg.streaming_text.size()
     && cache.last_pending_size   == msg.pending_stream.size());

    const std::string* source_ptr =
        reasoning_view ? &cache.combined_source : &msg.text;
    const bool has_live = reasoning_view
      ? false // 推理有一个 缓冲区; source_ptr 已经指向它
      : (!msg.streaming_text.empty() || !msg.pending_stream.empty());
    if (has_live) {
        if (msg.text.empty() && msg.pending_stream.empty()) {
            // 流式传输-text 仅此而已,没有缓冲——直接引用,
            // 没有副本.
            source_ptr = &msg.streaming_text;
        } else if (sizes_unchanged && !cache.combined_source.empty()) {
            // 大小与我们上次的配置一致 帧 ;
            // 支持 缓冲区。 重新使用; 没有复制。
            source_ptr = &cache.combined_source;
        } else {
            cache.combined_source.clear();
            cache.combined_source.reserve(
                msg.text.size() + msg.streaming_text.size()
                + msg.pending_stream.size());
            cache.combined_source.append(msg.text);
            cache.combined_source.append(msg.streaming_text);
            cache.combined_source.append(msg.pending_stream);
            source_ptr = &cache.combined_source;
        }
    }
    const std::string& source = *source_ptr;

    // 记住下一个组件大小 帧 才不是,我说 -op 检查。
    cache.last_text_size      = reasoning_view ? source.size() : msg.text.size();
    cache.last_streaming_size = msg.streaming_text.size();
    cache.last_pending_size   = msg.pending_stream.size();

    // 推理槽在 数据流 停止( E) OR 其
    // 答案 正文 已经开始( 4) 字节
    // 答案槽会解决经典的方法。
    const bool settled = reasoning_view
      ? (!reasoning_active && !source.empty())
      : (!msg.text.empty()
         && msg.streaming_text.empty()
         && msg.pending_stream.empty());
    if (!reasoning_view && settled && !cache.combined_source.empty()) {
        // 重拾刮伤 缓冲区 此时此刻 streaming_text
        // 下个冰冻会让 快照 关 msg.text.
        // (重音保持其 快照 此处 ,所以它可以免责. )
        std::string{}.swap(cache.combined_source);
    }

    // 快点 -path : 完全解答的信息 逐步显示 已经完成。
    // 跳过全部每 -frame 工作(打字机预发,
    // set_content ,结束,直播 -mode
    // 检查)并返回缓存元素 源码树 直接。
    // 这是长会话中的主要案例——N可见转折×× 帧 否则,
    // 将支付每一笔固定间接费用。
    if (settled
        && cache.last_settled_size == source.size()
        && cache.revealed_size    == source.size()) {
        auto built = cache.streaming->build();
        // 生命周期下降 -migration : 此信息已完全排
        // 空( 无直播)。 字节, 逐步显示 如果它的槽仍然在
        // PINNED 从其 流式传输 几天,把它还给 LRU 现在,
        // 它是一个纯洁的 渲染 记忆缓存 从这里开始,
        // 留下它会泄露一个永久的(未封顶)进入 每一个固定的转弯。
        // `build` 是值(不是输入槽的引用),所以下面的迁移
        // 可以安全移动条目。 幂等 : 无 -op 曾经安顿下来的
        if (want_pin)
            (void)m.ui.view_cache.message_md(m.d.current.id, slot_id);
        // 理性有其自己的铁路和自己的衡量标准;
        // 上限是回答的READING度量.
        return reasoning_view ? built : prose_capped(std::move(built));
    }

    // ── 逐步显示 : 种子 全部 抵达 字节 ,每个 帧 ──
    //
    // Τ NO 东道主 -side 键入光标。
    // 显示缓冲器是 maya's reveal_fx (斜拉-
    // 折叠+梯度小径+) 光标 以我们所喂的文字的尾声为动画。
    // 主持人的唯一工作是继续将传输运来的字
    // 节的全套部件交给部件,并在数据流直播
    // 时保留动画帧的武装.
    //
    // 为什么没有第二个光标:一个拒绝的显示器间距时钟 字节 然后,
    // 他们就被放逐到一个固定的、 char/sec 是一个 第二
    // 时钟对(a) 传输-arrival 时钟和(b) 渲染
    // -wake 节拍。它们之间的任何拍子都明显为结巴,
    // 如果主机钟停止前进(a) 渲染 跳过,醒悟的卡登斯 掉到100
    // 毫秒滴滴滴滴。。 STUCK 中点 -reveal 直至无关
    // 者 事件 翻转 帧。喂养生灵 字节 完全删除该时钟:可见文本
    // === 已到达文本。 Chunky deltas(一个
    // 多 -KB 代码块在一个三角洲)一次出现,
    // 但 reveal_fx 动画他们的后边 所以 接口边界 仍然读作活的
    // 数据流 ,同样的方法Zed / Claude 密码使用.
    //
    // revealed_size被保留 = source.size(),
    // 所以已结算的快-path和冻结的快照(读作revealed_size)看到一个完全
    // -revealed的信息,从不冻结一个部分的正文.
    cache.revealed_size = source.size();
    const std::string_view feed_source = source;

    // 已解决 -message 快速路径。一旦消息得到解析
    // ( Q) msg.text 最终, streaming_text
    // 空的)来源 字节 已经 不可变 其余时间。
    // set_content 平等内容检查仍然花费 O( O)
    // source.size () 中度计数 帧 长的 线程 带有许多
    // 可见的转折,加起来。 调用 完全一旦我们吃饱了最后的 字节 穿过一次
    //
    // 门为(大小+已确定) -once 标志 仅此而已。 状态更新函数
    // 重写助理 msg.text 之后的设置 StreamFinished
    // 移动 streaming_text + 文本;
    // 后编辑替换信件批发( 新) MessageId 新建 缓存 打开插槽。
    // 字节 每隔一段时间 帧 来防范同样的攻击 -length
    // 输入 -place 重写不存在的 O( 文本) 每可见的已
    // 结算转折 帧 ——占支配地位者 -frame 长会费用。
    const bool already_settled_into_cache =
        settled
        && cache.last_settled_size == source.size()
        && cache.revealed_size == source.size();

    // 可选人数 -frame 时间 流式传输-markdown 部件:日志 set_content
    // +完成键+ build() 每一非缔约方的费用 -fast-path 调用.
    // 隔离在 -flight 部件成本从( 单独) 中 -profiled
    // () 时间表 渲染。每行1行 调用 ; 跳过已定居的快速 -path.
    // 帧-build 费用+ 逐步显示 节奏 AGENTTY_LOG 追踪表面:
    // 追踪,而不是调试,因为这火点在每一个 流式传输 帧.
    const auto prof_t0 = ::agentty::logx::enabled(
                             ::agentty::logx::Channel::Perf,
                             ::agentty::logx::Level::Trace)
                       ? std::chrono::steady_clock::now()   // stopwatch: 仅限perf日志
                       : std::chrono::steady_clock::time_point{};

    if (!already_settled_into_cache) {
        // 跳过 set_content_async 完全按尺寸 -unchanged 帧.
        // 部件上一个帧已经看到这些精确的字节;
        // 其内部来源_按长度和前缀与feed_source匹配.
        // set_content_async 自己没有 -op 快速( T)
        // -path 将中度O( N) 字节 以发现此值; 这里的大小检查是 O(1)。
        // 仍然发泄 调用 当大小匹配但部件为当前 解析
        // async(需要通过民意测验才能通过结果) OR 当
        // 这是第一个种子时( Q) combined_source
        // 上面空的意味着我们走的是直接的回路。
        const bool skip_set_content =
            sizes_unchanged
            && cache.streaming->source().size() == feed_source.size()
            && !cache.streaming->is_parsing();
        if (!skip_set_content) {
            // 路径以活性分隔 :
            //
            //   LIVE 传输 (! 定下来了): 总是喂同步
            //   set_content。现场综合饲料是附加 -in-
            //   精神——文本+ streaming_text +
            //   pending_stream 单调生长在子体内 -turn。
            //   但是,在每个子 -turn 边界上一个子 -turn's
            //   streaming_text 归并 输入 msg.text 财务
            //   报告和已审计财务报表 streaming_text 重新来过,
            //   所以 下一个 种子是不同的 -prefix 更改; 一旦累积
            //   正文 十字路口 set_content_async's 16
            //   KB 交换的门槛将产生脱落 解析 工作线程。当 工作线程 运行
            //   is_parsing ()是真实的,部件返回其 PREVIOUS
            //   元素 源码树 - 现场直播 逐步显示 FREEZES ——结果只通过(.
            //   maybe_apply_async_ )下一次(见注:
            //   build() 已经调查过了 如果 传输 安静地呆着,
            //   冰冻的 -scrollback 视觉 -hash 门压制重画,
            //   build() 停止民意调查 完成 解析 未应用的座位: 末尾
            //   拖拉机,直到下一个三角洲/100米。 流式传输 停止( E)
            //   之后的一段时间, 下一回合再做一次精细的修改。
            //   报告( 下转 = 新鲜 < 16 KB 占位符 – 同步
            //   路径 )。 同步路径没有is_parsing( ) 窗口,
            //   因此, 运行的逐步显示 永远不能冻结 解析。
            //
            //   SETTLED / RELOAD (已解决):保持同步 变体.
            //   A 线程 重新装入或 回滚缓冲区 整个16年的回收
            //   交换 KB 正文 一次作为不同的前缀; 卸载该前缀
            //   -shot 解析 改为: 工作线程 保留 渲染 线程
            //   反应迅速,以及 finish() 低于部队 -adopts
            //   在同一条通道里,所以没有无限制的冷冻窗.
            if (!settled)
                cache.streaming->set_content(feed_source);
            else
                cache.streaming->set_content_async(feed_source);
        }

        // 处理好留言 – 执行任何跟踪 末尾 转到前缀的块列表。
        // finish() 冲着那些还留在里面的东西 流式传输
        // 末尾 进入克尼科承诺的区块路径(
        // md_block_to_element 故结业者, 渲染 谓 字节
        // -identical 给活的
        //
        // 历史上,这是 加载-bearing 用于跟踪关闭的代码栅栏:
        // find_block_boundary 只犯过一次围栅
        // 其结束 ``` 后紧跟换行，因此消息若结束于
        // 关闭后键( 常见的以代码结尾的回覆例) 时, 最后一个块卡在 末尾 ,
        // 通过 render_tail '是内线路径. render_tail
        // 财务报告和已审计财务报表 md_block_to_element
        // 给同样的 border/padding 构建器 代码字符串略有不同,
        // 所以 存储单元 刚才不是 字节-identical - 在
        // 最后一块块的解决上 -emitted 编号 终端 (无) "repaint" ,
        // 最糟糕的过去 SSH。现在这种差异已经固定 上游 输入 maya
        // (boundary.cpp 提前加载-commits 端端的
        // 关闭栅栏 -of-buffer 故他们已活了,已安定下来,
        // 存储单元 已在此之前匹配 调用. finish() 是因为它 幂
        // 等 (无) -op 曾经犯过罪... source_.size
        // 并且仍然是正确的冲洗地点 OTHER 跟踪 -block 有点
        //
        // 解决后我们再跳下最后的斜坡 首次 - 部件滑
        // 动了它 逐步显示 光标到活边的光标为~200 ms,
        // 当光标赶上时会自动翻转。 我们推迟。
        // finish() 直到坡道完工,所以...
        // 逐步显示 动画没有缩短, 并把它的积压堆积成
        // 一 帧.
        //
        // 跳过大小 -unchanged 帧. set_live 穿过有轨电车
        // 的自动包装 -bumps build_dirty_ 打开 每个 转让
        // (即使相同) -value : 浪费的重建 帧 计60岁 FPS.
        // request_finalize 谓 幂等 (早间 -out ) ,
        // 但跳过费用低廉。使用此门,不 -grow 帧 离职 build_dirty_
        // 单独和 build() 返回时 cached_build_
        // 直接来
        if (!sizes_unchanged) {
            if (settled) {
                // 完成后,活体高度等于 settled/frozen 高度 -
                // 高度 - (单位:千米) agent_session's
                // MessageStop 纪律—— 除非 结束 -of-turn
                // 最终坡道仍在滑翔(5号,交互式) 终端 仅限: finalize_turn
                // 武器 request_finalize 而不是安顿下来,
                // 确切地说,稳定 -state 积压类型超过 ~200 ms
                // 而不是粘贴在其中 帧。在坡道运行时,部件正在产生密集的动画
                // 帧 并翻转 live_ 关闭 ON ITS 自身
                // 一次 末尾 视线沉淀; 强制 finish() 这里可以
                // 剪断滑翔板并倾倒积压的物—— 斜坡的准确贴贴是防止的。
                // 一旦部件自行解决( ) is_finalizing 假的,
                // 这个分支运行 finish() 与以前一样,
                // -op 形状 -wise 但它冲刷任何后方的块 并保持 FPS
                // =0/SSH 路径( 此处为 状态更新函数 已经通过
                // settle_message_md )在锁步.
                const bool end_glide_running =
                    cache.streaming->is_finalizing()
                    || (cache.streaming->is_live()
                        && cache.streaming->reveal_in_progress());
                if (!end_glide_running) cache.streaming->finish();
            } else {
                cache.streaming->set_live(true);
            }
        }

        // ── 推理 – 回答 接口边界 ─────────────────────────────
        //
        // 推理槽解决 INSTANT 答案产生第一个 字节 (见A/C.5/61/L.48,
        // 第16段) `settled` 以上:
        // reasoning_active 如果推理光标在当时仍有积压,
        // 则上面没有限制它:
        //
        //   · 定居点分行被关上门! sizes_unchanged ,和推理的
        //     来源 STOPS GROWING 等它解决了,
        //     就从下个开始 帧 门上是假的,树枝再也不会跑了
        //   • 即使它运行, end_glide_running 真实,而
        //     逐步显示 正在进行中,所以 finish() 被跳过。
        //
        // 因此,部件被保留在无约束滑翔机的状态下:推理
        // 块在回答时,会按自己的速度打字 竞态 里面有两
        // 个打字机跑到不同的时钟,那是个干手 -off —
        // —和备选方法(删除) finish() 火)
        // 是一门 -frame 余者用膏,更甚.
        //
        // 绑在同一个地方 可见 滑翔其使用的答案
        // 路径 接口边界 : 将光标冲到边缘的硬
        // 期限为~220 mss。 原因在于读者在
        // 回答开始前已经移动过, 所以迅速完成是
        // 眼睛想要的; 坡道保持滑动而不是流行。
        //
        // 武装ONCE每槽(seam_glide_armed),
        // 大小门外,因为触发器是状态TRANSITION,而不是字节到达.
        // 调 -arming 每隔一段时间 帧 将补上最后期限,
        // 光标永远不会真正着陆。
        if (reasoning_view && settled && !cache.seam_glide_armed) {
            cache.seam_glide_armed = true;
            if (cache.streaming->is_live()
                && cache.streaming->reveal_in_progress()
                && !cache.streaming->is_finalizing()) {
                // 径直缩放到实际的LEFT,而不是恒定.
                //
                // 固定220毫秒本身就是一个爆炸 当其余的都是大,
                // 此时 接口边界 通常情况下: 最终推理三角洲常常
                // 只在答案的第一个符号之前降落几百 ms, 所以几百
                // ms的字符仍然可以不读。 220 ms的350
                // chars是~1600 cps—— 一个穿坡道衣服的贴纸,
                // 确切的说就是 "no time to glide, it just bursts"
                // 报告。
                //
                // 底部可以保持微小的剩余部分,
                // 避免感到疲软;
                // 顶端会停得非常长。
                // 末尾 回答已经是前面的段落了,
                // 现在没人读它。
                //
                // 答题道 接口边界 因为它的排水量
                // 在文本流出时会燃烧 QUIET ,
                // 因此,由于建筑工程的积压量很小。
                // 事件 在一个不同的频道。
                constexpr double kGlideCps = 220.0;   // 可读, brisk
                constexpr int    kMinMs    = 260;
                constexpr int    kMaxMs    = 1400;
                const std::size_t clip = cache.streaming->debug_reveal_byte_clip();
                const std::size_t shown =
                    (clip == static_cast<std::size_t>(-1)) ? source.size() : clip;
                const std::size_t backlog =
                    source.size() > shown ? source.size() - shown : 0;
                const int ramp = std::clamp(
                    static_cast<int>(1000.0 * static_cast<double>(backlog)
                                     / kGlideCps),
                    kMinMs, kMaxMs);
                cache.streaming->request_finalize(ramp);
            }
        }

        // 自动 -fold 长代码块为 DISABLED : 代码块 总是 渲染
        // 完全没有崩溃到一个 "▸ N lines of code hidden — unfold"
        // 长栅栏在长话短说时仍会扩大。 归并 被恢复,
        // prose_rows 输入 frozen.cpp 必须是相同的
        // threshold/kinds 或中间 -run 保留 -loop 线条
        // a 回滚缓冲区 鬼魂(鬼魂) 鬼魂 (鬼魂)

        if (settled
            && cache.revealed_size == source.size()
            && !cache.streaming->is_finalizing())
        {
            cache.streaming->finish();
            cache.last_settled_size = source.size();
        }
    }

    // (Live/finalize 过渡在上文,在 finish() 门,
    // 那取决于他们。 )

    // 音轨时间 `source` 上次长大了,所以我们才能知道 "actively streaming"
    // (字节 流动) "streaming but stalled" (e.g
    // 60-120 s 延长 -thinking 暂停, 不使用三角形。 由。 RAF 门
    // 就在下面
    {
        const std::int64_t now2 = ::maya::anim_now_ms();
        if (source.size() > cache.last_grow_size) {
            cache.last_grow_size = source.size();
            cache.last_grow_tick_ms = now2;
        } else if (source.size() < cache.last_grow_size) {
            // 源滚动/ 缩小( set_content 滚回, 重置消息)。
            cache.last_grow_size = source.size();
            cache.last_grow_tick_ms = now2;
        }
    }

    // 调 -arm 16 ms 动画 帧 财务报告和审定财务报表 整个 活动 流式
    // 传输 窗口中选择一个窗口。 reveal_fx (现场直播) -edge
    // scramble/gradient/caret )是现在唯一的动画;
    // 它需要每个唤醒 帧 时 字节 流淌着,所以它的尾部会顺利地运转。
    //
    // PRIMARY 信号: 模型具有权威性 流式传输. m.s .
    // .is_streaming() is variant-backed (phase::Streaming) and exact —
    // phase::Streaming )和准确的——是状态栏/相的相同信号.
    // 标签 打开闪光门,与 ! 连接( 此信息仍然在直播) 字节 输入
    // streaming_text/pending_stream ),
    // 意思是说 "the wire is open and THIS turn is the one receiving it."
    // 保持这个状态的同时,保持 光标 武装 UNCONDITIONALLY
    // 不论三角洲之间的距离有多长 脉冲 光标 表示 "waiting for the model,"
    // 完全正确,在一次 中间 -delta 暂停,甚至10秒,
    // 这就是 使固定在横跨 models/networks : 它按键关闭
    // 真实的英寸 -flight 州,不猜测三角洲cadence。
    //
    // 较早期 字节-recency 窗口( 窗口) since_grow_ms
    // ) 赛跑模型的缺口:每慢跑一次,250毫秒内失效 -model
    // 间隙(中间~470毫秒),冷却 光标 中点 -sentence (
    // "stream looks dead" ),撞到3秒只移动了悬崖。
    // 超时 能够 始终 出来 -run 由慢的模型或拉吉 链接 相位门
    // 不能——它结束瞬间 传输 关闭(阶段) + Idle/ExecutingTool
    // 而不是一个 帧 成本计算 流式传输 已限制且已支付:
    // build() 运行 帧 视像散列推进; 武装 光标 添加 ~ 0.1
    // -0.4 毫秒 否 -content 重绘 只有在真正活着的时候
    //
    // 电话 since_grow_ms 窗口为 SECONDARY 回退 边上
    // is_streaming () 已经翻转( ) e.g. StreamFinished 着陆,
    // 阶段 ~ 空),但 逐步显示 光标仍有一个积压需要滑出...
    // reveal_in_progress (以下已涵盖这一点,但窗口抓住了 过期
    // -phase 拍不拍不拍就拍不动 -introducing 编号 竞态 (
    // 仅限 EXTENDS 武装,永远不要缩短时间 is_streaming()).
    const bool wire_streaming_here = !settled && m.s.is_streaming();
    constexpr std::int64_t kRevealActiveMs = 3000;
    const std::int64_t now3 = ::maya::anim_now_ms();
    const std::int64_t since_grow_ms =
        cache.last_grow_tick_ms == 0
            ? kRevealActiveMs + 1
            : now3 - cache.last_grow_tick_ms;
    const bool stream_in_motion =
        wire_streaming_here
        || (!settled && since_grow_ms <= kRevealActiveMs);

    // 回滚安全：逐字显示尚未完成的行不得越过可视区边界。
    //    顶部。顶部的行被承诺 不可变 本地语言 回滚缓冲区 如果有
    //    人在打字机后面 到达那里 传输 (鬼魂们) -blanked
    //    存储单元 , 摇摆的尖端, 热梯度), 稍后 帧
    //    完成该 逐步显示 画布行不再符合承诺的复制品 maya's
    //    回滚缓冲区-invariant 山门着火,生长着 -path
    //    恢复为a HardReset (\x1b[2J。
    //    x1 b][3J擦拭]——被用户视为 duplicated/garbled
    //    传言被困在工具卡上面
    //
    //    (a) TOOL CARDS 发件人: Anthropic
    //        传输 关闭文本块 之前 tool_use 数据流 ,
    //        所以这个信息 的散文是 传输-complete 瞬间 a
    //        ToolUse 已存在 —— 已没有可以打出的任何字。 同时,
    //        牌面板 渲染 下方 降级和生长( 待处理 ——正取得进展 ——完成,
    //        推传言 toward/past 编号 可视区域 顶部。
    //        三个 -part 全部硬化,所有 幂等:
    //        • snap_reveal_to_edge (单位:千美元) -ghosts 编号 末尾 (弹簧)
    //          build_dirty_ 故重建工程是立即进行的。
    //        • set_reveal_fx (假):停止每名 -frame 提示突变
    //          (斜滑格,梯度重样式,脉动 光标 所以,
    //          每行滚动掉 是在它的 FINAL 格言+风格;
    //        • finish() : 强 -commits 编号 TRAILING PARAGRAPH 输入
    //          块路径 当前。一个消息的最后一段从未收到结束的\n\n,
    //          所以没有它就进入 render_tail 整个
    //          工具阶段的内置路径, 并且只在转弯时转换成承诺的块
    //          —— 和两个路径的( 关闭) -by-one ) 宽度不同,
    //          因此转换 REWRITES 如果这些行越过 可视区域
    //          工具阶段的顶部(他们经常这样做——不断增长的卡片将
    //          他们推向上),和解 -time 重写点击 不可变
    //          回滚缓冲区 财务报告和已审计财务报表 maya '门
    //          只能 HardReset (\x1b[2J\x1
    //          b[3J],擦除和重置) -stranding 笔录
    //          ——甲骨文的t1 -settle 腐败和用户
    //          的腐败 "prose duplicated above the cards" 截图。
    //          在此提交会让段落仍然在 可视区域 下方(
    //          diff) -repaintable ),
    //          因此,它为: 字节 已经 FINAL
    //          以后任何时间之前 帧 可以把它们卷下来
    //        员额 -tool 传言传到 新 占位符信件(
    //        自有部件, fx 打开), 因此没有可见的丢失
    //        —— 动画完全不会超过它的传动画。
    {
        // NOTE。这个工具的块原因 CARDS 连接
        // 到 正文 被揭示,这是 ANSWER-body 关切:
        // 理由 "#r" 插槽内没有牌( 一个工具) 调用
        // 属于答案, 位于推理块下面。 因此, 大部分下面的
        // 内容对于推理来说是惰性的 -- 但是光标 SNAPS
        // 他们没有,他们跑到两条车道(#79).
        //
        // 警卫在下面的snap/finish地点而不是has_cards地点,
        // 故意:has_cards也出现NEGATED(见
        // text_gone_quiet),因此强迫它作假推理将使该
        // 车道上一条新的REACHABLE排水道,而不是无法到达.
        // 删除错误的副作用时,不得添加新的。
        const bool has_cards = !msg.tool_calls.empty();

        // ── 预览 -emptive 结束 -of-text 排水管(在断裂处杀死 ROOT) ──
        //
        // 电话 逐步显示 光标骑~ drain_secs 后面的 传输 以平滑的抖动,
        // 所以当模型关闭其文本块时,会出现积压(~ wire_cps ×
        // drain_secs 键入。 tool_use 到达下面的警卫
        // 必须 难 -snap 积压到边缘(如果卡片不断增长,就会拖住任何滞
        // 后的内线—— 回滚缓冲区 腐败,神谕 -proven 打开 任意
        // 滑翔机,那支闪光灯是可见的 BURST (
        // 首字符停住，其余内容直到下一次工具调用才出现。
        //
        //
        // 但是在那个 传输 文本块去 QUIET 拍到拍子前的一拍,
        // 拍到拍到拍拍子前 tool_use 数据流。在这种差距中, NO 牌还没,
        // 所以 逐步显示 安全无虞 GLIDE 向边缘移动,
        // 下方没有任何东西可以向后行滚动。 检测空隙( 有效, 无卡,
        // ) 字节 已经停止生长,以建立短窗口), request_finalize:
        // 光标冲刺到边缘的有界斜坡 WHILE fx 继续,
        // 所以它读作打字机追赶,而不是贴面。
        // tool_use 光标已经处于边缘,下面的强制坐标是 a
        // NO-OP - 零爆破 - 零破碎 - 零裂破碎 零爆裂
        //
        // 安全地对着中点 -text 暂停( 模式摊位中间)
        // -sentence ),但实际上没有完成):如果更多 字节
        // 过早排水后到达,部件只是从新边缘恢复,即早期捕获 -up
        // 看起来像 "caught up, waiting," 永远不要爆发,
        // 永远不要触摸 回滚缓冲区 (还是没有牌) 同一牌的
        // since_grow 窗口 RAF 逻辑使用所以它不会战斗
        // 流式传输.
        // 只有当 传输 HAS STOPPED 流式传输 (is_streaming
        // (假),不是在中间 -stream 间 -delta 差距,
        // 慢慢来 -streaming 模式常规暂停 > kTextQuiletMs
        // BETWEEN 中点时的三角洲 -message ;
        // 排水时 request_finalize (s),坡道完成并翻转部件 直播_,
        // 和 下一个 三角形( D) -lives 和它, PASTES 整个三角洲在
        // 一个 帧 (已显示: 直播=0闲置) 帧 紧接在每+100 个前 -cell。
        // 排水沟的真正工作是文本工具。 接口边界 ,其中, 传输 真正安静下来
        // tool_use 数据流 继续玩! wire_streaming_here 将它
        // 限制在那个案子里 text_block_closed (明确结束) -of-text
        // 传输 事件 —— —— 仍然无条件排水.
        constexpr std::int64_t kTextQuietMs = 120;
        const bool text_gone_quiet =
            !settled && !has_cards && !wire_streaming_here
            && cache.streaming->is_live()
            && cache.streaming->reveal_in_progress()
            && cache.last_grow_tick_ms != 0
            && since_grow_ms >= kTextQuietMs;
        // ANSWER LANE 仅 ,两个术语。
        //
        // 上文段落所说的排水沟的工作是文本工具 接口边界 :在牌画
        // 之前把散文拿出来。两个信号都有关 ANSWER 正文 --
        // text_block_closed 答案就是结束
        // -of-text 传输 事件 ,和 since_grow_ms
        // 跟踪答案 字节 - 所以,没有说 任何关于推理
        // 通道是否完成。
        //
        // 未加保护的推理工具 -call 转( 常见形状: 思考,
        // 然后行动) 关闭了答案的文本块, 而推理 逐步显示 那时还中场
        // -glide ,并武装160毫秒 HARD 斜坡上, 上面有斜坡。
        // 一个~300 -character 段落在 160 ms 中
        // 完成为 ~ 1900 cps: 第一个推理框即使是一个段落,
        // 也不会爆炸, 没有工具卡和计时器。 兄弟姐妹 # 79 -
        // 原因相同, 答案不同。 -body 到达另一条通道的机理。
        //
        // 推理道不需要等同:它没有卡片可以摆脱,它自己的终点
        // 也一样 -of-channel 已经处理到
        // `settled` 计算。
        if (!reasoning_view && (text_gone_quiet || msg.text_block_closed))
            cache.streaming->request_finalize(/*ramp_ms=*/160);

        // 打开门 is_live(): finish() 将 liveQQfalse
        // 分配到 TrackedQQ 的包装器中, 它将产生 build_dirty_
        // 打开 每个 转让(即使相同) -value ——运行此块每 帧 将
        // 强制重建完整的部件 帧 工具阶段的周期(较长) -turn
        // "md streaming becomes slow" 慢点
        // 第一次通过直播后 is_live () 虚假,且块被跳过;
        // snap/fx-off 无效 -ops 正在打开一个完成的部件。
        //
        // 为什么 HARD SNAP 在卡片可以画之前:工具卡
        // 渲染 下方 开始这段流言 GROWING 同样的
        // 帧 其出现( 4) 待处理 滑翔机,可以看见牌,
        // 可以看见 逐步显示 光标~ drain_secs
        // 光标和边缘之间的线条仍然在线条内(未承诺)
        // ——而不断增长的卡片恰恰将这些线条推向原生线条.
        // 回滚缓冲区 在光标到达它们之前( 甲骨文)
        // -proven : 滑动时124门恢复。
        // 弹出立即消除滞后, 这样就不会出现故障。 -swept
        // 用于卡片串行的内线窗口。
        //
        // ── Tool-panel DEFERRAL (杀死边界破裂) ──
        //
        // 预告 -emptive 排水量很少能跑,在 传输, content_block_stop
        // (文本)和 content_block_start(tool_use ) ,
        // 即为 CONSECUTIVE SSE 事件 - 通常是一样的 TCP 段
        // - 如此 text_block_closed 财务报告和已审计财务报表
        // has_cards 变成真实的 相同 减少批量,排水量增加 ZERO 帧。
        // 在卡片存在之前。强制抓取后粘贴整个卡片 wire_cps×drain_secs
        // 积压案件 帧 : 用户 -reported
        // Markdown 停住，工具调用时又突然成批出现。
        // (tool_boundary_burst_probe 4 -10× 以
        // 稳态复制 逐步显示 80毫秒,并无条件 传输层 永远不发
        // StreamTextBlockClosed).
        //
        // 分辨率: 当光标仍然在中点时 -glide, HOLD 该 TOOL
        // PANEL OFF-SCREEN (
        // cache.defer_tool_panel ——消耗
        // append_assistant_body_slots /
        // turn_config_for_assistant_run 同样的
        // 帧 并武装最后的斜坡。 渲染 流言蜚语下面没有 增长的元素推中
        // -reveal 横行过 可视区域 顶端——唯一可以跨越的行是:
        // OLDEST ,完全读取 -revealed 承诺,完全的 正常的
        // 中点 -stream 实例 甲骨文已经证明是安全的。打字机完成滑翔
        // (通常为300-700毫秒可见渔获量) -up ,
        // 则 RateCursor 自然积压的衰减,
        // reveal_in_progress (翻转假,和) THEN 三角跑步,
        // 快击是不行 -op ,fx滴,(Fx滴出), finish() 将后面
        // 的段落改为: FINAL 散文,零面条,还有 回滚缓冲区 不变量 (卡片
        // 从不画在un下) -swept 内线行)通过构造持有.
        //
        // 约束和bai-outs——推迟绝不能
        // 无限期隐藏卡片或拖延互动:
        //   • 最后-MESSAGE 门:滑翔机只是 回滚缓冲区-safe 时
        //     无内容 渲染 下方的传言。以下的信息即刻存在(该帖子)
        //     -tool 子目录 -turn 占位符——其
        //     markdown/panel 渲染 下方和成长的一排
        //     内线 上方的排行可以推过 可视区域 上中层 -reveal
        //     —— 严打所防止的腐败(奇迹) -proven :
        //     在一个不断增长的继任者领导下推迟, 增加了9个门的回收。
        //     因此, 推迟只停留在此信息为实时时。
        //     末尾 '在底部' ("底部") -most 元素。
        //   • 全部-PENDING + 高处 -budget 门: 隐蔽 帧 增长
        //     编号 末尾 以隐藏面板的全高处显示 一个 帧.
        //     横跨 可视区域 成长的顶端是承诺 AS PAINTED
        //     如果成长... 可视区域 高度,旧底 -rule
        //     行( 由未隐藏的移入面板内容) 横跨未 -repainted
        //     ——阴影错配 —— HardReset (
        //     甲骨文) -proven 60x18: 一张运行卡,
        //     累积2×H 隐藏的在未隐藏时回收的进展。
        //     待处理 牌的预览是 末尾-windowed (高音)
        //     (高音(hight)) -bounded ~ 12 行;
        //     运行进度 / 完成输出未绑定。 因此推迟只
        //     保留时 每个 牌还没完 待处理 和 估计的
        //     隐藏高度适合 可视区域 生产时间是常见的情况:
        //     工具翻转仅于 finalize_turn ,
        //     所以整个词组 -streaming 窗口(破碎的窗口)仍可延缓。
        //   • kMaxCardDeferMs硬盖:病理积压(
        //     适应性斜坡英寸 request_finalize 伸展到2.5
        //     秒对十 -of-KB 垃圾)在1.5 s之后又回到硬点,
        //     少量的剩余面条是较小的邪恶面条, 相对于看起来卡住了的卡片。
        //     时间从 cache.card_defer_since.
        //   • pending_permission:等待批准的工具浮动
        //     权限 纸牌作为自己的直播 -tail 划一样 帧
        //     ——用户必须看到 内容 他们正在批准,
        //     所以小组不能落后于 提示词 立刻抓好,马上抓好
        // Tool EXECUTION没有受到任何影响——kick_pending_tools
        // 运行于状态更新函数,无论面板能见度如何;
        // 一个快速工具在它的卡片第一次涂装时可能已经是Running/Done,
        // 其写法为"typewriter finished, then the result landed".
        if (has_cards && !settled && cache.streaming->is_live()) {
            constexpr std::int64_t kMaxCardDeferMs = 1500;
            const std::int64_t defer_now = ::maya::anim_now_ms();
            const std::int64_t deferred_ms =
                cache.card_defer_since_ms == 0
                    ? 0
                    : defer_now - cache.card_defer_since_ms;
            const bool is_tail_bottom =
                !m.d.current.messages.empty()
                && &msg == &m.d.current.messages.back();
            // 隐藏 -height 预算:每张卡必须 待处理 ,
            // 最糟糕的,最糟糕的是,你还是 -case 隐藏行必须
            // 非常适合 可视区域 那样无藏的生长 就不会推动突变 接口
            // 边界 逐行越过承诺边界 帧.
            //
            // 这以前是公寓 `12 rows per card`
            // 但它并不是一个束缚——这是猜测,它朝着腐败的方向是错误的。
            // STREAMING 编辑卡 渲染
            //
            //     1个统计 标签 + 2×edit_tail_per_side diff 行数
            //
            // 地点 per_side = 夹( () stream_body_budget
            // (-1)/2, 1,6) 扩大范围 终端 高度( 4)
            // edit_body.cpp 80x30时 终端 + 2×6 = 13
            // 的数值 正文 排行,卡片测量了20行带铬——而估计是12.
            // 8 -row 下级 -estimate 授权在飞行中行数超过
            // 可视区域 撑得住,所以半个 -typed 逐步显示
            // 承诺 不可变 回滚缓冲区 (scrollback_oracle_test ,
            // 80x30: 标记 uniq-2-1 搁浅).
            //
            // 从这里得到它 相同 预算 渲染器 使用,所以两者不能漂移:
            // stream_card_rows_bound () 结束
            // -estimating 这里安全——它只推迟卡片或解决几行额外问题;
            // -estimating 线条刻入 回滚缓冲区 没有 重
            // 绘 永远可以修复。
            bool all_pending = true;
            for (const auto& tc : msg.tool_calls)
                if (!tc.is_pending()) { all_pending = false; break; }
            const int est_hidden_rows =
                static_cast<int>(msg.tool_calls.size())
                * ui::detail::stream_card_rows_bound();
            const bool hidden_fits =
                est_hidden_rows < ::maya::available_height() - 4;

            // ── 电话 一个 其实,这一切 接口边界 键关: 逐步显示 积压 ──
            //
            // reveal_backlog = 来源 字节 击打机光标在到达
            // 直播边缘之前仍需要赶超( Q)
            // debug_reveal_byte_clip ()是光标的 字节 位置;
            // -1 = 已经 AT 这是唯一的真理来源
            // 这里是否存在需要平滑处理的突发增量？
            // 下面的每个分支都读它——没有咨询 reveal_in_progress (
            // 原始光标<指针擦亮,点火3) -byte 末尾 和3一样容易 KB (一)
            // 或提供者是否发送 StreamTextBlockClosed 事件.
            //
            // 为何这很重要: 推迟隐藏新工具卡直到 逐步显示 光标着陆,
            // 纯粹是为了阻止一个大不见识的 末尾 由粘贴在一块 帧 文本的
            // 文本 工具 接口边界。在门槛以下没有平滑之处,
            // 所以推迟纯粹是延缓的。卡片在显示前会隐藏在最后的斜坡上。
            // reveal_in_progress () 而不是积压
            // SIZE 曾经是 Anthropic-vs-OpenAI
            // "liveness" 非对称性 : Anthropic
            // 船在50-100的三角洲上 装有90-200米的空隙 所以
            // 在 接口边界 最后一个小的 -delta 末尾 几乎总是
            // 滑翔( reveal_in_progress (a) 真实 —
            // — 推迟,持卡; OpenAI-compat 通常光标已经位于
            // 边缘(假的 ~ 卡片显示一次, "more live" )
            // 处理积压案件 SIZE 让每个提供者都表现得一样: 立即为普通
            // 短片弹出卡片 -prose-then-tool 大小写,
            // 仍能平滑的 真正的大写 -backlog 爆裂 ~2包着的排.
            constexpr std::size_t kMinDeferBacklogBytes = 160;
            const std::size_t reveal_clip =
                cache.streaming->debug_reveal_byte_clip();
            const std::size_t reveal_backlog =
                (reveal_clip == static_cast<std::size_t>(-1)
                 || source.size() <= reveal_clip)
                    ? 0                                   // 边角的光标
                    : source.size() - reveal_clip;
            const bool backlog_worth_smoothing =
                reveal_backlog >= kMinDeferBacklogBytes;

            // ── 头室证明:为什么卡片在传言显示时可以弹出 ──
            //
            // 电话 回滚缓冲区 先知的神话 不变量 承诺行是附加
            // -only 中间 -reveal 行( 主办) 末尾
            // / 摇摆的格言) 仅是 DANGEROUS 如果它能
            // 达到 不可变 回滚缓冲区 在它决心之前,有一条线,
            // 回滚缓冲区 仅 被推过 可视区域 顶端,它要求总内容
            // 高度超过 可视区域.
            //
            // 我们控制的几何: 逐步显示 末尾 总是 BOTTOM
            // -MOST 信件的传言,下面唯一能表达的就是此
            // 信件的工具卡。所以,尚未解决的 末尾 如果横跨顶部,
            // 则
            //
            //     rows_below_tail + tail_rows  >  viewport_rows
            //
            // 电话 KEY 改进后a 二进制程序 "does it all fit"
            // 门: 不变量 不是,是"我们",不是"我们"
            // 尾部内容已完整显示。
            // 不过 "nothing UNRESOLVED crosses the top" 不同,
            // 行离开 可视区域 只有在他们仍然被鬼魂包围的时候,
            // 他们才危险,他们的动画也即将变得隐形。 末尾 已经 不
            // 适合我们不隐藏牌 我们不粘整张牌 末尾 - 我们决定
            // 恰好 溢出前缀( Q) advance_reveal_floor
            // )让每一个静静 -visible 排行保留打字机。所以,
            // 卡片立即出现在 全部 案件; 只有你无法观看的部分被跳过。
            const int term_rows_now = ::maya::available_height();
            const int cols_now      = std::max(1, ::maya::available_width());

            // 捆绑着未读的行 末尾。 ceil (帮助) ( bytes/cols)
            // ALONE 谓 不 上方绑定,用于 两个 独立理由——均按
            // tests/reveal_headroom_test.cpp:
            //
            //  1. 新线,它假定每条线 字节 消耗列,该列为
            //     '\n'违反:100字节持有40'\n'占据40+行,
            //     而不是2(测量为41对2,一个20x under
            //     -estimate). Newline-密集的标记下限(列表,
            //     表格,短栅栏线)正是倾向于在工具调用之前的形状.
            //  2. 内置、标记区块 渲染 内 padding/indent ,所以,请
            //     可用文本宽度为 NARROWER 超过 终端。 以全数分隔
            //     `cols` 因此结束 -states 每行文字的匹配度( 测量:
            //     500) -char 线条, 以 46  cols 键 渲染 12行,
            //     但缩写 -wide 11),第11节。
            //
            // 所以: 将每个未读的绑起来 LINE 与A类分开
            // CONSERVATIVE 包宽度。 长度 L
            // 成本( C) L/wrap_w )行,最小值1(空白行仍
            // 占一行)。 末尾 主导包词和线条 -count 术语。
            // 单词 -wrap 只休息时间 EARLIER 而不是硬
            // 字符包(它反向到一个空格),所以是一个字符 -wrap
            // 以宽度计算,不大于真实值是一个真正的上方界限。
            constexpr int kBlockInset = 2;   // padding/indent 安全保障
            const int wrap_w = std::max(1, cols_now - kBlockInset);
            const std::string_view tail_sv =
                std::string_view{source}.substr(source.size() - reveal_backlog);
            auto rows_of_line = [&](std::size_t len) {
                return std::max<int>(
                    1, static_cast<int>((len + static_cast<std::size_t>(wrap_w) - 1)
                                        / static_cast<std::size_t>(wrap_w)));
            };
            int est_tail_rows = 0;
            {
                std::size_t line_start = 0;
                while (line_start <= tail_sv.size()) {
                    const std::size_t nl = tail_sv.find('\n', line_start);
                    const std::size_t len =
                        (nl == std::string_view::npos ? tail_sv.size() : nl) - line_start;
                    est_tail_rows += rows_of_line(len);
                    if (nl == std::string_view::npos) break;
                    line_start = nl + 1;
                }
            }

            // Chrome the 末尾 与 可视区域 共用:作曲家+状态+转弯自己
            // 的 header/rail. 匹配 hidden_fits 使用的边距.
            constexpr int kChromeRows = 6;
            // 缺少 -level 标记在未读的 末尾 中,
            // 它会增加超出自己文本行的行(代码 -fence 边框,
            // 表分隔符,块引号垫). MEASURED,没有猜测:
            // reveal_headroom_test 渲染 a
            // fence/quote/table 5宽度的函体和主张
            // est_tail_rows + 这种松懈覆盖了每一个真实
            // 高度——最差的观测赤字是2行(表边框),所以4个持持,
            // 有2行差值. maya 标记下铬和这个血压 失败之前,
            // 证据可以授权一张卡片 拖着幽灵的行。 请保持两张相等。
            constexpr int kBlockChromeSlack = 4;
            // 牌一显示,有多少行必须离开可视区域.
            // >0表示数量 LEADING 末尾 将行滚动到
            // 不可变 回滚缓冲区 却依然被鬼所缠绕
            const int overflow_rows =
                (term_rows_now <= 0)
                    ? 0
                    : (est_hidden_rows + est_tail_rows + kChromeRows
                       + kBlockChromeSlack) - term_rows_now + 1;

            const bool instant_card_ok =
                backlog_worth_smoothing
                && is_tail_bottom
                && all_pending
                && !m.d.pending_permission
                && term_rows_now > 0;

            // 推迟 仅 当几何学无法使用时( 无)
            // 可视区域 高度本身就再也不会迫使人们
            // 保持高度了 — — 溢出问题由解决溢出
            // 问题来加以解决。
            const bool can_defer =
                backlog_worth_smoothing
                && !instant_card_ok
                && is_tail_bottom
                && all_pending
                && hidden_fits
                && !m.d.pending_permission
                && deferred_ms < kMaxCardDeferMs;
            if (can_defer) {
                if (cache.card_defer_since_ms == 0)
                    cache.card_defer_since_ms = defer_now;
                cache.defer_tool_panel = true;
                // 滑到边缘,现在——覆盖 传输层 永远不发
                // StreamTextBlockClosed (上面
                // 的排水管可能没有发射) -arms 其有时能知.
                cache.streaming->request_finalize(/*ramp_ms=*/160);
            } else if (instant_card_ok) {
                // ── INSTANT CARD + PARTIAL RESOLVE ──
                //
                // 牌已经显示 此 帧 无条件。 末尾 合适,
                // 不需要其他东西,整个 逐步显示 如果真的有,
                // 请继续动画 不 合适,解决只是领先
                // `overflow_rows` 排好队,不要鬼鬼祟祟 存储
                // 单元 可能困在 回滚缓冲区 ,让其余的继续输入屏幕。
                if (overflow_rows > 0) {
                    // 行 = 代码前缀。 全部行走
                    // LINES 一旦累积的行成本覆盖了溢出,
                    // 就停止。 UP (我们消耗了跨越
                    // 边界的全线): MORE 绝对
                    // 要求总是安全的, 解决的不是。
                    std::size_t resolve_bytes = 0;
                    int rows_acc = 0;
                    std::size_t line_start = 0;
                    while (line_start <= tail_sv.size() && rows_acc < overflow_rows) {
                        const std::size_t nl = tail_sv.find('\n', line_start);
                        const std::size_t len =
                            (nl == std::string_view::npos ? tail_sv.size() : nl) - line_start;
                        rows_acc += rows_of_line(len);
                        if (nl == std::string_view::npos) {
                            resolve_bytes = tail_sv.size();
                            break;
                        }
                        resolve_bytes = nl + 1;
                        line_start    = nl + 1;
                    }
                    // advance_reveal_floor
                    // 需要一个 ABSOLUTE 整个源代码点位置,
                    // 所以数一下被揭示的前缀加上我们刚决定解决的切片.
                    const std::size_t abs_bytes =
                        (source.size() - reveal_backlog) + resolve_bytes;
                    std::size_t cp = 0;
                    for (std::size_t i = 0; i < abs_bytes && i < source.size(); ++i)
                        if ((static_cast<unsigned char>(source[i]) & 0xC0) != 0x80) ++cp;
                    cache.streaming->advance_reveal_floor(cp);
                }

                // 不执行 来,来,来 不 finish() : 数据
                // 逐步显示 保持活泼,并保持滑翔 在可见的卡。
                // request_finalize 将适应性斜坡举起,
                // 这样 末尾 仍然迅速着陆(和 传输层 不含
                // StreamTextBlockClosed 而不是爬在可读的地板上
                // 现在没有 传输 左转转顺滑
                cache.defer_tool_panel    = false;
                cache.defer_exit_finished = false;
                cache.card_defer_since_ms = 0;
                cache.streaming->request_finalize(/*ramp_ms=*/160);
                ::maya::anim::keep_animating();
            } else {
                // 退出(滑翔完成/盖帽击中/保释) -out。而不是一个
                // INSTANT 抓完( 贴满了 ) 带类型的-but- 未
                // 报告积压的1件 帧 工具边界上——
                // 首字符停住，其余内容直到下一次工具调用才出现。
                // 断裂),手臂a BOUNDED GLIDE : 数据 逐步显示
                // 短跑到边缘的~150毫秒(快速但可见),
                // 以及 finish() 运行在 a LATER 帧 光标一旦降落,
                // 通过同样 phase-1/ 推迟的阶段2 -exit 150毫秒
                // 的短路可以停留 回滚缓冲区-safe (一张成长的牌不能在坡道
                // 着陆前 拖一排落后的线) "catching up to land"
                // 而不是一个糊涂。
                //
                // 仍有两起案件结案 IMMEDIATELY (无滑翔): 逐步
                // 显示 积压低于平滑阈值( 常见的已) -caught-up
                // 微小的 微小 微小 -tail 案件 - 没有什么可以滑翔的),
                // 或者我们从来没有进入一个现场 逐步显示。这些保持原始单曲
                // -frame 行为,这是不可察觉的。
                // backlog_worth_smoothing SSOT 延迟门使用——无
                // 秒 reveal_in_progress ()用不同的语义来阅读.
                const bool was_deferring = cache.defer_tool_panel;
                const bool has_backlog = backlog_worth_smoothing;
                if (reasoning_view) {
                    // 推理道没有卡片可以保持一致,
                    // 所以这里没有任何保护,断裂是纯粹的
                    // 损坏:它在每个工具边界上将推理光标跳到活边,
                    // 每工具一次爆破块 调用 最后一段
                    // 最糟糕的是,最后一段 调用 离开
                    // 土地(#79) 逐步显示 独自一人,
                    // 让它继续滑翔。
                    //
                    // 回滚缓冲区 安全,这就是为什么 闪电是
                    // ANSWER-body 关注:这是日益增长的
                    // 工具 CARD 推得过半 -revealed
                    // 排出顶部,推理板坐牌上方,不是生长的.
                } else if (has_backlog) {
                    // 被挡住的滑翔机,然后把完成延迟到出口路径.
                    cache.streaming->snap_reveal_to_edge(/*glide_ms=*/150);
                    cache.defer_tool_panel    = true;
                    cache.defer_exit_finished = false;
                    ::maya::anim::keep_animating();
                } else {
                    cache.streaming->snap_reveal_to_edge();   // 无-op/即时
                    cache.streaming->set_reveal_fx(false);
                    cache.streaming->finish();
                    cache.card_defer_since_ms = 0;
                    if (was_deferring) {
                        cache.defer_exit_finished = true;   // 下个 帧 解密
                        ::maya::anim::keep_animating();
                    } else {
                        cache.defer_tool_panel = false;
                    }
                }
            }
        } else if (has_cards && !settled && cache.defer_tool_panel) {
            if (!cache.defer_exit_finished) {
                // 推迟完成的斜坡, 部件翻转为直播_ 关闭 ON ITS
                // 自身 (advance_reveal_cursor_
                // 正在移动 -settle 门),前面的出口有一个 帧.
                // finish() 必须 仍然 运行精确一次: 它强制
                // -commits 后段从 render_tail '是内线路径,
                // 其关闭 -by-one 将承诺包起来 -block 路径是和解
                // -time 行重写( R) "t1-settle corruption"。
                // 小组将隐藏此内容 帧 (第1阶段); 下一轮(第2阶段)。
                cache.streaming->set_reveal_fx(false);
                cache.streaming->finish();
                cache.defer_exit_finished = true;
                ::maya::anim::keep_animating();
            } else {
                // 第二阶段:结束 -mutation 帧 已涂色;
                // 面板现在以纯底显示 -append 增长。
                cache.defer_tool_panel   = false;
                cache.defer_exit_finished = false;
                cache.card_defer_since_ms = 0;
            }
        } else {
            // 没有卡/ 已结算/ 部件已完成 ——
            // 确保 过期 迟到永远不能隐藏面板 帧.
            cache.defer_tool_panel    = false;
            cache.defer_exit_finished = false;
            cache.card_defer_since_ms = 0;
        }
    }

    //    (b) 可视区域 HEIGHT SHRINK : 数据 终端 自动
    //        推顶 可视区域 排成原生 回滚缓冲区 如果活下来 逐步显示 边缘在其中,
    //        它会冻结 过期 (高度) 高处 -resize 缩放是
    //        一个单独的用户 事件 ,所以,我说 渲染 编号 末尾 完全
    //        删除 -revealed 给那个 帧 无法察觉,
    //        没有留下 幽灵行行。 snap_reveal_to_edge 是
    //        一个不 -op 解决/未解决 reveal_fx 已经到了边缘
    {
        const int cur_h = ::maya::available_height();
        if (cache.last_render_height > 0 && cur_h < cache.last_render_height
            && !settled)
            cache.streaming->snap_reveal_to_edge();
        cache.last_render_height = cur_h;
    }

    auto built = cache.streaming->build();

    // 留着16毫 帧 武装时 EITHER 新建( N) 字节
    // 流动积极( stream_in_motion) OR 部件的
    // 逐步显示 光标在爆炸后仍然在向活边滑动。 第二个条件是,
    // 打字机是如何连续地穿过一个 传输 暂停: 模型船发生爆炸,
    // 就会保持100-200毫秒的安静, 但光标仍有积压需要打印出来,
    // 无需重排 -arming 转圈会掉到滴答卡登斯 光标会在下
    // 一个钟声上跳下( "bursts, not continuous"
    // 症状). build() 将光标移到上面,
    // 所以 reveal_in_progress () 反映这一点 帧 '其州.
    //
    // 第三个条件——背景 解析 飞行中( is_parsing()):
    // set_content_async 手持一个大相径庭的前缀
    // 工作线程 并不断返回 PREVIOUS 元素 源码树 直到结果落地。
    // maybe_apply_async_ (通过着陆结果)运行 仅
    // 发件人 build(), i.e。仅在 view() 运行 i.e。
    // 只有当视觉散列推进时。如果 字节 暂停中间 -parse (a
    // 传输 爆裂( 爆炸) -then-quiet ),散列停止前进,
    // build() 停止呼叫, 结束 解析 静坐不应用—— 末尾
    // 冻结直到下一个三角洲或100 ms 滴答恰好唤醒了循环(即
    // "md gets stuck then bursts" 口吃大贴/大回流。
    // 帧 武装时 解析 制造 build() 继续投票
    // 投票结果立即通过 工作线程 结束。
    //
    // 第四个条件——部件是 LIVE 与 reveal_fx (is_live()).
    // render_live_panel_ 动画后方 -edge
    // 摇摆/ 渐变/ 脉冲 光标 每个 帧 部件为 live_,
    // 即使当 逐步显示 光标已抓到边缘( 背面log 0) ,
    // 并且正在等待下一个符号。 前三个术语可以 全部 被钉在那个
    // 钉子里是虚假的 -but-live 状态在中点 -stream
    // 间隙 : 字节 流不动( A) stream_in_motion
    // 如果相位短暂离开, 则会失效。 流式传输 工具回合期间
    // -trip 和 3 s 窗口过期时,光标位于边缘( 3 s)。
    // reveal_in_progress 假的,没有斜坡,
    // 没有 解析。没有这一术语, 光标 转弯会冷冻,尽管模型还在
    // 工作 is_live () (确切条件)
    // render_live_panel_ 动画下方)保证 光标
    // 保持整个直播窗口的呼吸, 独立于相位或任何 超时 - 同样的强性原则
    // 作为 is_streaming() 光标 门,应用到部件自身的状态。
    // 成本为 ~0.1-0.4 ms no -content 重绘 ,
    // 只在转弯真正活着的时候才支付.
    const bool live_caret =
        !settled && cache.streaming->is_live();
    if (stream_in_motion
        || live_caret
        || cache.streaming->is_animating()) {
        ::maya::anim::keep_animating();
    }

    if (prof_t0.time_since_epoch().count() != 0) {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - prof_t0).count();   // stopwatch: 仅限perf日志
        // 真实 逐步显示 光标( 光标) 字节 和每台打字机
        // -call 跳跃——显示一个 BURST 滑翔机可以移动
        // 剪辑 字节 百分比 帧 ; 粘贴( 工具) -boundary 开始,
        // 开始 -progress-line 逐步显示
        // 跳跃了几百个在一个 调用 暗号是真实的爆破信号
        const std::size_t clip = cache.streaming->debug_reveal_byte_clip();
        // 人数 -SLOT 上一个剪辑,不是每个呼叫者共享的静态.
        //
        // cached_markdown_for 每运行一次 流
        // 式传输 部件每件 帧 - 推理 "#r" 车道,
        // 答题车道,每个车道1 -turn - 和单人
        // `static prev_clip` 都不同,
        // 所以Dclip是推理 -clip 减去答题 -clip :噪音,
        // 往往是阴性,偶尔会有一个巨大的正态,看起来完全像它应该检测的面条。
        // 诊断暴发的一个仪器在最抱怨的车道上是无法使用的,
        // 它没有报告一条线路属于哪条车道。
        static std::unordered_map<std::string, std::size_t> prev_clips;
        const std::size_t now_clip =
            (clip == static_cast<std::size_t>(-1)) ? source.size() : clip;
        auto& prev = prev_clips[slot_id.value];
        const long long dclip =
            static_cast<long long>(now_clip) - static_cast<long long>(prev);
        prev = now_clip;
        AGT_LOG_HOT(Perf, Trace, "stream.frame",
                "lane={} slot={} src={} clip={} dclip={:+} live={} "
                "finalizing={} settled={} fastpath={} build_us={}",
                reasoning_view ? "reasoning" : "answer",
                slot_id.value,
                source.size(),
                now_clip,
                dclip,
                cache.streaming->is_live() ? 1 : 0,
                cache.streaming->is_finalizing() ? 1 : 0,
                settled ? 1 : 0, already_settled_into_cache ? 1 : 0,
                static_cast<long long>(us));
    }

    // 现场打压 正文 返回( E) UNPADDED 反作曲家
    // -bounce - 电话 数据流-start 指标 − 第
    // 一项 -content 接口边界 ,在区块边界上,打字机在结算
    // 时会崩溃,工具卡会自动处理。 maya (
    // Runtime::render 追踪现场记录 -max
    // 高度和垫子都覆盖在瞬间 等心理医生证明 如此闲置的空间没有枯萎后
    // 就会衰败 maya 拥有此机制结束 -to-end ;
    // 此选项 正文 元素纯 渲染 真相大白的暴露了一切
    return reasoning_view ? built : prose_capped(std::move(built));
}

// 各发言方的视觉标识：侧边线颜色、符号和显示名称。
//    集中了铁路的颜色, 头文件 格莱弗,
//    还有底部 流式传输 指标保持锁定。
struct SpeakerStyle {
    maya::Color color;
    std::string glyph;
    std::string label;
    // Smart Mode 角色口音,当 Smart Mode 不引导此转弯时空.
    // 变暗了 " · strategist" 模型名称旁边的后缀
    // 这样授权就可辨认了,没有第二秒 UI 表面, 显示
    std::string role_label;
    maya::Color role_color;
};

// 智能模式的Per-role口音转弯。
// 不同的花色,这样滚动的笔录就成了代表团的一面:谁在思考,
// 谁在工作,谁在做咕噜声。
struct RoleAccent { std::string label; maya::Color color; };
// 疲劳过度 ModelRole 而不是串链比较。
// 区别不是样式:旧的形态默默返回了任何它
// 没有识别到的字段的无名词,所以在字符串中
// 添加了一个角色。 枚举 完全开关 -Wswitch
// 说吧 编译 时间
[[nodiscard]] RoleAccent role_accent_for(smart::ModelRole role) {
    switch (role) {
        case smart::ModelRole::Strategic:
            return RoleAccent{"strategist",  role_brand_alt};
        case smart::ModelRole::Implementation:
            return RoleAccent{"implementer", role_info};
        case smart::ModelRole::Utility:
            return RoleAccent{"utility",     code_path};
    }
    return RoleAccent{"strategist", role_brand_alt};
}

// `msg`是标题中的信息:一个助手转弯被ACTUALLY
// 服务于它的模型(Message::served_model)
// 贴上标签,而不是由运行的选择面板选择。 在智能模式下,
// 大多数转弯时不同,选择也会在用户切换模型时追溯性地改变,
// 因此在这里读它会错误地标上整个记录。
SpeakerStyle speaker_style_for(Role role, const Model& m, const Message* msg) {
    if (role == Role::User) {
        // 用户栏杆是 `role_brand` (magenta)——与代码不同
        // -reference 青色和配对 作曲者的口音颜色何时有 -text ,
        // 所以用户的 带类型的 消息可视化地流动到它们转弯 头文件.
        return {role_brand, "\xe2\x9d\xaf", "You"};                  // ❯
    }
    // 更喜欢转弯自己的出处; 回到现场选择在球场前(
    // 或用Smart Mode 关闭)的转弯.
    const std::string& id = (msg && !msg->served_model.empty())
                                ? msg->served_model.value
                                : m.d.model_id.value;

    // 一个 从域解码 SSOT (domain/model_name.hpp。
    // 这曾经是如果 -chain 结束 is_opus/is_sonnet/is_haiku
    // 加上一只手,我帮你 -rolled 开始 -based 版本扫描
    // 仪——大约60行,既复制了家族分类器,又复制了版本提取器,
    // 与其他复制件不相符。它携带了两条混凝土错误:
    //
    //   • 链条没有is_fable/is_mythos/is_gpt臂,因此2026年
    //     旗舰航道掉到 "non-Anthropic local model"
    //     分支和已交付 WITH 销售商前缀,其他任何用法;
    //   • Haiku的呼声(bright_cyan)不同意徽章部件的呼声
    //     (绿色),所以一个模型有两个身份。
    //
    // 两者目前都不可能在结构上实现:家庭表格在家庭结构方面是详尽无遗的。
    // 枚举 并且证明颜色表不会与状态色调相撞。 `medium()` —
    // "Opus 4.8" - 轮到你们了 -header 投影;
    // 数据 `· 1M` 注释是 选择面板 生意上的事 说吧
    const auto name = model_name::decode(id);
    SpeakerStyle out{name.color, "\xe2\x9c\xa6", name.medium()};     // ✦
    if (msg)
        if (msg->served_role) {
            auto ra = role_accent_for(*msg->served_role);
            out.role_label = std::move(ra.label);
            out.role_color = ra.color;
        }
    return out;
}

// 轮次标题后的元信息条，格式为 `12:34 · 4.2s · turn N`。
// `checkpoint` 贴上一个微妙的 `· ↺ checkpoint`
// 标记为恢复 -point 用户转身读作普通转身,带有标记,
// 不 单独装满 -width 悬在铁轨上方的分割部件( 它看起来
// 像铬 / 一个破碎的空信件)。
// "just now" / "4m ago" / "2h ago" / "3d ago" 从墙上 -clock 邮票。
//
// 粗体的ONXX PURPOSE:相对邮票回答的问题为
// "how long ago, roughly",转弯读作"4m 12s ago",
// 请无人愿意做的算术. 一个单位,不小数.
[[nodiscard]] inline std::string relative_time(
        std::chrono::system_clock::time_point ts,
        std::chrono::system_clock::time_point now) {
    if (ts.time_since_epoch().count() <= 0) return {};
    auto s = std::chrono::duration_cast<std::chrono::seconds>(now - ts).count();
    // 未来的邮票是时钟摇摆,不是报告的时间;诚实的 渲染
    // 备注 "negative ago" 是现在。
    if (s < 0)  s = 0;
    if (s < 45) return "just now";
    if (s < 3600)  return std::to_string(s / 60)   + "m ago";
    if (s < 86400) return std::to_string(s / 3600) + "h ago";
    return std::to_string(s / 86400) + "d ago";
}

// `now` 是那个 帧 '日历时间( Model::wall_at(anim_now (()),
// 因此, "4m ago" 邮票是该表的函数 Model 编辑 帧 钟声。
std::string format_turn_meta(const Message& msg, int turn_num,
                             std::optional<float> elapsed_secs,
                             std::chrono::system_clock::time_point now,
                             bool checkpoint = false) {
    // 时间戳,按外观预告.
    //
    //   关闭,没有。默认,正确的一个:在现场
    //              "when" 总是 "just now"
    //              时钟是每个转弯的沟渠里的声音
    //   相对的 - "4m ago"。当您想要时, -reading
    //              长 线程 :转弯之间的距离,而不是时空.
    //   绝对-"14:32". 与日志或同事的关系。
    std::string meta;
    switch (agentty::ui_prefs::current().timestamps) {
        case agentty::ui_prefs::Timestamps::Off:
            break;
        case agentty::ui_prefs::Timestamps::Relative:
            meta = relative_time(msg.timestamp, now);
            break;
        case agentty::ui_prefs::Timestamps::Absolute:
            meta = timestamp_hh_mm(msg.timestamp);
            break;
    }
    // 接下来的每个部分 " \xc2\xb7 "-joined ,
    // 所以分离器必须以已经存在的东西为条件——否则Off留下一个
    // 领先的 "\xc2\xb7" 挂在钟原来的位置上
    const auto join = [&meta](std::string part) {
        if (!meta.empty()) meta += "  \xc2\xb7  ";
        meta += std::move(part);
    };
    // 只有表面经过才有意义,一个接近 -instant 当地 -model 回复值~0s
    // 壁 -clock ,哪个 `format_duration_compact` 渲染
    // 是个没用的 "0ms"/"12ms" - 任何低于100米的东西。
    if (elapsed_secs && *elapsed_secs >= 0.1f)
        join(format_duration_compact(*elapsed_secs));
    if (turn_num > 0)
        join("turn " + std::to_string(turn_num));
    if (checkpoint)
        join("\xe2\x86\xba checkpoint");   // 检查站
    return meta;
}

// 计算助手轮次的实际经过时间，从前一条
//    用户信件的时间戳到此。
std::optional<float> assistant_elapsed(const Message& msg, const Model& m) {
    if (msg.role != Role::Assistant) return std::nullopt;
    // 记忆:结果仅取决于 msg.timestamp 和用户信件
    // 的时间戳- 两者 不可变 一旦此消息存在, 已解析的值
    // 就永远不会改变。 没有此信息, 则将无法更改。 记忆缓存
    // 下面的反向扫描是 O( sub) -turns 自上次用户
    // 回合以来) 运行每 帧 以转弯深度生长的运行头部(整个运行头部),
    // -flight 转身坐在现场 末尾 以至你们安定下来。
    // 缓存 头条消息的每 -message 槽;
    // 返回随后每次的缓存值 帧.
    //
    // 访问是 PARTITION-SAFE 现场跑步的头头经常
    // 是 PINNED 流式传输 边缘; 路径 记忆缓存
    // 解决 message_md 将它从固定的集合中移出 帧
    // 击败驱逐 -immunity。通过非读取 -migrating
    // peek() 首; 只有在槽存在且已经持有时 记忆缓
    // 存 我们一定要写出失落的画面,所以我们摸一下它已经住
    // 过的哪个家的插槽。 message_md_live
    // 以钉住者发誓, message_md ()否则——
    // 所以写字从不移动条目横跨分区.
    if (const auto* mc = m.ui.view_cache.peek(m.d.current.id, msg.id);
        mc && mc->elapsed_valid)
        return mc->elapsed_cached;
    std::optional<float> result;
    for (std::size_t i = m.d.current.messages.size(); i-- > 0;) {
        if (&m.d.current.messages[i] == &msg) continue;
        if (m.d.current.messages[i].role == Role::User) {
            auto dt = std::chrono::duration<float>(
                msg.timestamp - m.d.current.messages[i].timestamp).count();
            if (dt > 0.0f && dt < 3600.0f) result = dt;
            break;
        }
    }
    auto& cache = m.ui.view_cache.is_pinned(m.d.current.id, msg.id)
        ? m.ui.view_cache.message_md_live(m.d.current.id, msg.id)
        : m.ui.view_cache.message_md     (m.d.current.id, msg.id);
    cache.elapsed_cached = result;
    cache.elapsed_valid  = true;
    return result;
}

// 添加一个助理信件 正文 插槽( 标记下+工具面板+内含) 权限 改
// 为 `cfg.body`。从 turn_config 因此,
// 可以将连续的助理信件运行为 一个 转弯( 见
// turn_config_for_assistant_run ,
// 匹配 agent_session 每一个内部的纪律 接口边界 捐款a 正文 位置到一个转弯
//
// Tool 面板排放从文本排放中分割,因此是连续的子
// 排放 -turn 信件( 每个都有一个工具, 没有文本)
// 渲染 以 一个 合并面板而不是 N 堆叠单 -tool
// 小组讨论。 缓存 键是 `anchor_msg_id`
// —— 重建间稳定, 因为信件是附件 -only ,
// 每个合并组的独特性,因为每个组都固定在 首次 提供信件。
// 新建动作面板 帧 (agent_session , 以图示形式)。
// 解密助理运行被快照到 m.ui.frozen 由
// freeze_range - 他们从不 -enter。
// 活面板以 IN 键为界。 -flight 转动工具数,
// 所以 -frame 成本为 O( active_tools。
// 一个 AgentTimeline 每一面板的载体,生于此地,
// 在 cfg.body 超出范围:否 shared_ptr 身份翻转 竞态 ,
// 没有 加载动画-bucket 缓存 ,没有快速冻结路径。旧的冻结路径
// 缓存 被直接描绘成 m.ui.frozen 同样的 帧 插槽是人烟
//
// 权限 纸牌是 NO LONGER 随附在此。 主机将它作为自己的浮点
// live_tail 正在运行的助手转弯( 镜像) 下方的条目
// agent_session's `Permission` 结巴在根茎下.
void append_assistant_tool_panel(maya::Turn::Config& cfg,
                                 const Message& msg,
                                 std::span<const ToolUse> tool_calls,
                                 const Model& m,
                                 const SpeakerStyle& style)
{
    if (tool_calls.empty()) return;

    // 路线已解决子 -turn 专用面板 -message 面板 记忆缓存 (
    // g_panel_render_memo, agent_timeline.cpp。
    // 它被键入此消息的稳定 id + compute_render_key (
    // 所以)一个已解决的潜水艇 -turn 是一个单人 -uint64-compare
    // 点击——甚至跳过 O(工具) 内容 -key 字符串构建为赤色
    // g_panel_cache 击球会付出代价的,这就是保持每架 -frame
    // 将成本平整视为 -flight 运行累积数百个已定居子 -turns (
    // 他们留在现场) 末尾 直到安顿下来,然后重新安顿 -emitted 每隔
    // 一段时间 帧。。 内容。 记忆缓存 住在旁边 g_panel_cache,
    // 不 输入 RAM-bounded ViewCache ,所以它的深度 与
    // 标记下降脱钩 -tree 保留。运行中的工具将推进 render_key
    // 每一个 帧 重建(自然错失) 加载动画 动画,在记忆器内处理.
    const int frame = m.s.spinner.frame_index();
    cfg.body.emplace_back(agent_timeline_element_memoized(
        msg.id.value, msg.compute_render_key(),
        tool_calls, frame, style.color));
}

// Reasoning/thinking 块,用于一个助理消息,或者当消息没有带推理文本时无效。
// 唯一 位置推理变成一个可转换的插槽; 两者都是 -message 路径( 选项)
// append_assistant_body_slots )和运行
// -merged live/frozen 路径( 选项) emit_subturn)
// 调用 现场直播和冷冻记录 渲染 与每个供应商通过一个执行的漏斗相同( DRY )统一.
// Message::reasoning_display_text () 指
// Anthropic 认为, Codex 推理摘要,以及 OpenAI-compat
// reasoning_content 全部( E) 渲染 同一个街区。
//
// CENTRAL STREAMING : 推理文本流经 相同 StreamingMarkdown
// 逐步显示 + 缓存 作为普通答文的机器( cached_markdown_for
// 与 MdView::Reasoning ,一个兄弟 "#r" 缓存 插槽,
// 所以这样 数据流 平稳和快速——递增 解析 , 费率 -smoothed 逐步显示 光标,
// 组件 缓存 完全如答案一样 正文 ,而不是重新 -parsing a 末尾 窗口中
// 帧.
//
// UX (没有交互,正确与 不可变 回滚缓冲区):
//   • LIVE:阴暗的,左方的-gutter-bordered认为数据流有呼吸能力
//     "Reasoning" 头文件,流式传输 FULL文本通过逐步显示光标.
//   • SETTLED : 数据 相同 块状保持完全渲染——它确实 不 归并 改为:
//     单曲 -line 简表。 头文件 失去它,失去它,没有它 加载动画 读
//     "✦ Reasoned (~N tokens)" ; 完整的推理仍然在下面,
//     暗淡, 答案之上。 烤焦在冻结中; 永不改变, 永不需要键盘。
std::optional<maya::Element> reasoning_slot(const Message& msg, const Model& m) {
    // 一个 切换决定是否设置推理块 渲染 : 外观 "Thinking"
    // 划线
    //
    // 昔有二人,而先人已死. `m.d.show_reasoning`
    // 此处记录为 "^R in the model picker" - 但是,
    // 我们... ^R 正在审核,没有发送 ModelsToggleShowReasoning
    // (其数量) 状态更新函数 手和手 Msg 变体 仍然在那里,
    // 无法到达。 标志 随便坐着 settings.json
    // 最后一次持有,并且 过期 价值默默地决定了是否任何人能看到
    // 这样的想法: `true` 外表隐藏是唯一能关掉它的方法
    // `false` 表示外观行完全没有做任何事情。
    // 既不符合用户的选择, 因为他们从未获得选择。
    //
    // docs/COPILOT_RESPONSES.md 已经抓到
    // 了一样的 标志 与工作条相矛盾 REQUEST 边并移除它。
    // 这是清理中的一半。规则本身就生活在其中。
    // ui::reasoning_shown 现在,因为 逐步显示
    // 事件订阅 活动指标也必须以同样的方式回答这个问题。
    if (!reasoning_shown(msg)) return std::nullopt;

    // 两个问题,不是一个,他们过去都分享一个布尔。
    //
    //   in_flight ——这一转机是否仍在这个消息上起作用?
    //   thinking_now 模型是现在在思考,
    //                  而不是运行一个工具还是回答? HEADER
    //                  上面写着 和已经过期的计数器的滴答
    //
    // m.s.active ()而不是(a) is_streaming
    // (因为工具执行是同一转弯飞行,而且) msg.thinking_open
    // (而不是偷看是否 逐步显示 中间 -glide 因为 传输 自己的
    // 思考阶段 生存了一段静静的空白——动画 标志 ,并按键 头文件
    // 掉下来的,翻过来的 "Reasoned" 每一个暂停。
    const auto& msgs = m.d.current.messages;
    const bool is_live_tail =
        !msgs.empty() && msgs.back().id == msg.id;
    const bool in_flight    = is_live_tail && m.s.active();
    const bool thinking_now = in_flight && msg.thinking_open();

    // 推理 正文 数据流 通过 CENTRAL 流式传输-markdown
    // 路径( 属于 ) "#r" 缓存 插槽, 交叉 -frame-persistent
    // )所以它像正常的文字一样平稳地揭示出来,在和解后保持完全的渲染——
    // 不 归并 ,没有高帽。 maya::ReasoningStream
    // 部件拥有抛光的铬(动画) 头文件 以活的标尺+左栏),我们把缓存的交给它.
    // 正文.
    maya::Element body = cached_markdown_for(msg, m, MdView::Reasoning);

    // 理由说明 渲染 转弯:一个真正的大胆的左侧铁路(同一个原子
    // 边框是转弯用于其铁路)——转弯不会腐化 回滚缓冲区
    // 因为生命边缘 数据流 通过共享 逐步显示 / 回滚缓冲区
    // -safety 推理通过 cached_markdown_for
    // )加一个暗淡的 正文 所以下面的答案总是赢得眼球。
    // 正文 平滑时 逐步显示 完全铁路,暗淡的,离谱的
    maya::ReasoningStream::Config rcfg;
    rcfg.boxed    = true;    // 实轨(如转弯),不单轨 -line 前缀
    rcfg.meter_below = true; // 将 "· 158 tokens · 6.2s" 行读作页脚
                             // 是因为你刚刚看到的,
                             // 而正是这行 不断改变, 让它在活的边缘
                             // 而不是在中途滚动 -tick
    rcfg.meter_chip  = false; // 不 a 标签 乐队的独家 -column 垫
                              // 将 # # # 推到 # # # # * 右侧
                              // 正文 文本,所以脚印从不与它总结的词句排成一行。
                              // Flush 文本 + movement 表示
                              // "label" 而不设内障.
    rcfg.meter_sweep = true;  // 右转时,
    rcfg.meter_sweep_fg = ui::fg;  // 峰顶是 BRIGHTNESS ,不是第二个呼声:
                                   // 哑巴上方的一片混乱 与
                                   // 红衣铁道搏斗 并被击倒
                                   // theme::native
                                   // 混合的脆饼,所以一个螺旋 将
                                   // 灰灰色的 magenta。
    rcfg.meter_rate  = true;  // "· 26 tok/s" ——它实际上在思考的有多快?
    rcfg.dim_body = true;    // 以颜色退换 正文 , 从而获得答案
    // 裁决书("Let me…"、"Actually"、
    // "The issue"、粗体标题)在连接的传言消退时,
    // 用全文颜色点亮,所以一个长块读作阶梯而不是一条灰色河。
    rcfg.structured  = true;
    rcfg.waypoint_fg = ui::text_primary;
    // 颜色MUST来自命名的-ANSI调色板,而非部件硬编码的真彩默认值(0x8a
    // gray 正文 / indigo rail). agentty的规则是
    // "the terminal theme wins":命名为ANSI适应用户的调色板 ,
    // 所以"recede"在光的OR暗色背景上表示相同的东西.
    // 硬编码的灰色在光的 终端上是低-contrast-to-invisible.
    //   正文 − 固定的 MID-gray (0x9a9a9). 真实的中点.
    //           -gray 是保持可见的颜色 任意 与黑白两者的对比
    //           终端 ,所以,我说 "dimmed" 永不崩溃为隐形。
    //           命名 ANSI 无法保证( 主题可能映射)
    //           bright_black 接近 -black ,或白色到接近
    //           -bg ); 固定中间 -gray 仍然明显地退到 光明
    //           的白色的回答旁白下
    //   头文件 主题的次要墨水, 总是一样 -visible 推理作为
    //           正文.
    //
    //   这些是 FIXED 灰色( 0x9a9a9), 选择原因 ui::muted (
    //   bright_black (倒塌至附近) -invisible 真实的 -black 主题和
    //   隐藏活 "Thinking" 单词和 "Reasoned · ~N tokens" 计数器。
    //   将一个隐形物换成另一个:一个中点 -grey 被钉在地上 RGB 无法读
    //   取到的 LIGHT 终端 ,
    //   正是这个 "almost unusable on a light background" 报告(第37期)。
    //
    //   主题本身 secondary/muted 墨水是固定的,
    //   因为墨水是相对的 -checked 以这个主题的画布为
    //   对象——本地人将它们解决给用户自己. ANSI 调色板,
    //   以及一个从自己背景中挑选的花样的命名方案。字面文字总是
    //   在一半上,这也不能错。 终端 世间.
    //           NOTE : 部件已解决 -rail dim( 摄氏度) =
    //           accent.darken () 是否定的( ) -op 名称上 ANSI 颜色,
    //           因此紫红色的铁路保持恒定(fine——稳定的铁路读作一个连续的边).
    rcfg.accent      = ui::role_brand;          // 铁路+西吉尔
    rcfg.header_word = ui::text_secondary;      // 可见的header/meter
    rcfg.body_fg     = ui::text_secondary;      // 永远 -visible 暗处
    // `secondary` 是一个 HUE 在大多数方案中(在
    // Srcery中为绿色),所以推理读起来像成功输出。
    // 当主题表达出真实的颜色时,使用自己的文字墨水向画布拉回,
    // 并带有铁路的触觉:中立,明显比答案安静,仍然QQ4.5:1.
    {
        const maya::Theme& th = maya::theme::live();
        const auto tx = th.resolve(maya::Color::slot(maya::ThemeSlot::Text));
        const auto bg = th.resolve(maya::Color::slot(maya::ThemeSlot::Background));
        const auto ac = th.resolve(ui::role_brand);
        if (tx.has_channels() && bg.has_channels() && ac.has_channels()) {
            auto lum = [](int r, int g, int b) {
                auto f = [](int v) {
                    const double s = v / 255.0;
                    return s <= 0.03928 ? s / 12.92
                                        : std::pow((s + 0.055) / 1.055, 2.4);
                };
                return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b);
            };
            const double lb = lum(bg.r(), bg.g(), bg.b());
            auto mix = [&](double t) {
                auto ch = [&](int a, int b, int c) {
                    const double v = a + (b - a) * t;          // 向bg方向移动
                    return static_cast<uint8_t>(v + (c - v) * 0.12 + 0.5);  // 锡
                };
                return maya::Color::rgb(ch(tx.r(), bg.r(), ac.r()),
                                        ch(tx.g(), bg.g(), ac.g()),
                                        ch(tx.b(), bg.b(), ac.b()));
            };
            // 退到40%,但从不低于帆布上的4.5:1.
            for (double t = 0.40; t >= 0.0; t -= 0.05) {
                const auto c = maya::theme::live().resolve(mix(t));
                const double lc = lum(c.r(), c.g(), c.b());
                const double cr = lc > lb ? (lc + 0.05) / (lb + 0.05)
                                          : (lb + 0.05) / (lc + 0.05);
                if (cr >= 4.5 || t <= 0.0) {
                    rcfg.body_fg = rcfg.header_word = mix(t);
                    break;
                }
            }
        }
    }
    // Thinking::Collapsed - ——一览,
    // 这就是整个前言:最后8段 ROWS 无论是现在还是很久之前 都有可能找到答案
    // Thinking::Shown 是那个 渲染 全部都一样
    //
    // 上限是NOT 取决于转弯状态,这是故意的。
    // 是从哪来的? "is this live"
    // 表示块在 事件 却不说读者想要多少推理:
    // 工具启动时释放的窗口( 左侧阶段) 流式传输 ),
    // 当打字机在暂停时闲置,当后来的子机时 -turn 不再使用此
    // 信件 末尾 ——走出一条整条链子. -of-thought 中
    // 点 -turn (计量:+32行,一行) 帧 中,
    // 在一个 QUIET 帧 一次爆炸降落时会回击 一顶帽子 一只形状
    //
    // ROWS,所以块会增长到8,然后坚守——它不能缩水.
    if (agentty::ui_prefs::current().thinking
            == agentty::ui_prefs::Thinking::Collapsed)
        rcfg.live_tail_rows = 8;
    maya::ReasoningStream rs{rcfg};
    rs.set_live(thinking_now);
    rs.set_char_hint(msg.reasoning_display_text().size());
    // 有效期 "· 3.2s" 头文件 度量:
    // 关闭阶段加开阶段,所以转动时会思考,动作,
    // 再思考一次,在第一阶段显示总比开始或冻结.
    const std::int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            maya::anim_now().time_since_epoch()).count();
    rs.set_elapsed_ms(msg.thinking_elapsed_ms(now_ms));
    return rs.build_with_body(std::move(body));
}

// 单身 -message 正文 插槽附件:文本( 如果有) 然后此信件的
// 工具面板( 如果有的话)。 使用 `turn_config` 非
// 国家 -run-merged 渲染 (用户将代表转向不同的分支;
// 这是被单项击中的) -Message 预告的助理路径 -dates )),运行合并.
void append_assistant_body_slots(maya::Turn::Config& cfg,
                                 const Message& msg,
                                 std::span<const ToolUse> tool_calls,
                                 const Model& m,
                                 const SpeakerStyle& style)
{
    // 原因块首先——它 渲染 上方 答案(链) -of-thought
    // 将您输入到一个共享的帮助器中。 reasoning_slot.
    if (auto rs = reasoning_slot(msg, m))
        cfg.body.emplace_back(std::move(*rs));

    const bool has_body = !msg.text.empty() || !msg.streaming_text.empty();
    if (has_body) {
        cfg.body.emplace_back(cached_markdown_for(msg, m));
        // Tool-panel 推迟 : cached_markdown_for
        // 刚刚决定(同样) 帧 是否为 逐步显示 光标仍然在中端 -glide
        // 使用新工具卡。通过非工具卡读取 -migrating peek()
        // - 槽可能是 - 可能就是 时间档 PINNED (活),
        // 和 message_md () 这里将它从固定的套装中移出。
        //
        // BUT 工具一旦生效,就永远不要推迟 EXECUTING。推迟只用于平滑
        // 简短的表达语 -glide 与新* 关系 待处理*-card 窗口;
        // a Running/Done 工具的卡片必须立即显示。没有此选项,
        // 如果推迟 -exit 机器缺少一个跟随 -up 帧 (无) 数据流 安静,
        // 没有动画 帧 火灾) defer_tool_panel 保持真实,
        // 运行中的卡片不会被看到—— 即 "tool shows running but I don't see its card"
        // 报告。
        const bool any_executing = std::any_of(
            tool_calls.begin(), tool_calls.end(),
            [](const ToolUse& tc){ return !tc.is_pending(); });
        if (!any_executing) {
            if (const auto* mc = m.ui.view_cache.peek(m.d.current.id, msg.id);
                mc && mc->defer_tool_panel)
                return;
        }
    }
    append_assistant_tool_panel(cfg, msg, tool_calls, m, style);
}

} // 命名空间结束。

maya::Turn::Config turn_config(const Message& msg, std::size_t msg_idx,
                               int turn_num, const Model& m,
                               bool continuation,
                               std::string_view meta_override,
                               std::span<const ToolUse> tool_calls_override) {
    // agent_session 模式: 构建一个新配置 调用。
    // 和解的转折 让他们的元素被拍成 m.ui.frozen
    // 冻结时间和从那里产生; 现场 末尾 重建每个 帧
    // 被绑在其中 -flight 转弯。无配置/元素
    // 记忆化缓存 给
    (void)msg_idx;

    // Tool-batch 合并管道:当呼叫者通过一个覆盖跨度时,
    // 视之为有效 tool_calls 轮到这个了 保存 深 O( N) 深
    // O ( N) -copy 备注 `msg` 每隔一段时间 帧 现场直播
    // 末尾 合并路径——原作保留在 `m.d.current.messages[*]`
    // 并且我们借他们整个周期。
    std::span<const ToolUse> tool_calls = tool_calls_override.empty()
        ? std::span<const ToolUse>{msg.tool_calls}
        : tool_calls_override;

    auto style = speaker_style_for(msg.role, m, &msg);

    maya::Turn::Config cfg;
    cfg.glyph        = style.glyph;
    cfg.label        = style.label;
    cfg.rail_color   = style.color;
    cfg.continuation = continuation;
    cfg.meta         = format_turn_meta(msg, turn_num,
                          msg.role == Role::Assistant
                              ? assistant_elapsed(msg, m)
                              : std::nullopt,
                          m.wall_at(maya::anim_now()),
                          /*检查站=*/msg.role == Role::User
                              && msg.checkpoint_id.has_value());
    if (!meta_override.empty()) cfg.meta = std::string{meta_override};
    // 智能模式角色标记,领导元条,因此代表团追踪读出右-hand沟:
    // "strategist · 12:34 · 4.2s · turn 3".
    if (!style.role_label.empty())
        cfg.meta = style.role_label + "  \xc2\xb7  " + cfg.meta;

    // 压缩 -boundary 转弯:作为真实、
    // 最小的 SYSTEM 转身而不是赤裸裸的满地 -width
    // 分词(读作铬/在记录中漂浮的一条偏离规则),它获得自己的
    // 静音演说者身份——a `≡` 格力,一条哑巴,
    // 一个 "Compacted" 标签 + 时间戳元—— 和
    // 一个 -line 正文 谈话中这个插槽是可辨识的 事件 ,
    // 形状与 User/Assistant 转过来,所以它看起来不再
    // 坏了。
    //
    // 该模型仍然收到关于 传输 (msg.text
    // 图片中, 包含许多内容。 KB ,
    // 并会推动保存 末尾 关 -screen
    // 即时紧凑的土地。 CC ——其国家
    // `compact_boundary` 记录行
    // 渲染 作为标记,而不是摘要 正文.
    if (msg.is_compact_summary) {
        cfg.glyph      = "\xe2\x89\xa1";              // ≡
        cfg.label      = "Compacted";
        cfg.rail_color = muted;
        cfg.meta       = timestamp_hh_mm(msg.timestamp);
        cfg.body.emplace_back(maya::Turn::PlainText{
            .content = "Earlier conversation summarized to reclaim context.",
            .color   = muted});
        return cfg;
    }

    // 叉出处卡:一种合成的Role::User消息种子在
    // 新式-forkedXX线程. WITHOUT这个
    // 叉子看起来像空白屏幕——messages.empty()
    // 是假的(本注存在),所以欢迎屏幕被压制,
    // 然而原始传输指示的纯用户泡会读作"USER带类型的"的指针.
    // 相反,它得到了自己的安静身份——一个 "\u2443" 叉格力
    // 信息铁路 一条 "Forked" 标签——和标签 -line
    // 正文 告诉用户, 父记录可以按要求读取。 模型仍然收到
    // msg.text 满载在 传输 (这是真实的用户信息,不是滑动的).
    if (msg.fork_note) {
        using namespace maya::dsl;
        cfg.glyph      = "\xe2\x91\x83";              // ⑃
        cfg.label      = "Forked";
        cfg.rail_color = status_info;
        cfg.meta       = timestamp_hh_mm(msg.timestamp);
        cfg.body.emplace_back(maya::Turn::PlainText{
            .content = "Branched into a fresh thread with near-zero context.",
            .color   = fg});
        // 第二行将父文本命名为用户(和 回滚缓冲区
        // 可见 WHERE 之前的对话生活——模型按要求读取。
        // 代码路径 -reference 青色,
        // 剪辑的长的绝对路径 永远不能包卡。
        if (!msg.fork_transcript.empty()) {
            std::string content;
            std::vector<maya::StyledRun> runs;
            auto push = [&](std::string_view part, maya::Style st) {
                if (part.empty()) return;
                runs.push_back(maya::StyledRun{content.size(), part.size(), st});
                content.append(part);
            };
            push("prior transcript \xc2\xb7 ", maya::Style{}.with_fg(muted));
            push(msg.fork_transcript, maya::Style{}.with_fg(code_path));
            cfg.body.emplace_back(maya::Turn::BodySlot{maya::Element{maya::TextElement{
                .content = std::move(content),
                .style   = {},
                .wrap    = maya::TextWrap::TruncateEnd,
                .runs    = std::move(runs),
            }}});
        }
        return cfg;
    }

    // 智能模式 ROUTING 牌:合成的, 传输-inert 事件
    // 表面显示( E) -turn 路径 DECISION (哪个
    // 模型 + 努力, 运行在, 分类复杂度 缩放, 哪些层是活动).
    // 其特性——脑胶水,紫色铁栏杆, "Smart Mode"
    // 标记为管弦乐 事件 真正的潜质代表团 渲染
    // 普通的,我们来 `task` 工具卡。
    if (msg.smart_routing) {
        using namespace maya::dsl;
        cfg.glyph      = "\xf0\x9f\xa7\xa0";          // 🧠
        cfg.label      = "Smart Mode";
        cfg.rail_color = accent;                       // purple/accent 轴
        cfg.meta       = timestamp_hh_mm(msg.timestamp);

        // 复杂度 – 状态色调: 复杂度=警告, 标准值=info, simple/trivial 被谋杀了
        auto cx_color = [&](const std::string& c) -> maya::Color {
            if (c == "complex")  return status_warn;
            if (c == "standard") return status_info;
            return muted;
        };

        // 第1行:"routed → <model>  · effort <e>  · <complexity>"
        // 作为一个调试节点,所以它永远不能包.
        {
            std::string content;
            std::vector<maya::StyledRun> runs;
            auto push = [&](std::string_view part, maya::Style st) {
                if (part.empty()) return;
                runs.push_back(maya::StyledRun{content.size(), part.size(), st});
                content.append(part);
            };
            push("\xe2\x86\x92 ", maya::Style{}.with_fg(muted));   // →
            push(msg.smart_route_model, maya::Style{}.with_fg(code_path).with_bold());
            push("  \xc2\xb7 effort ", maya::Style{}.with_fg(muted));
            push(msg.smart_route_effort.empty() ? "off" : msg.smart_route_effort,
                 maya::Style{}.with_fg(fg));
            push("  \xc2\xb7 ", maya::Style{}.with_fg(muted));
            push(msg.smart_route_complexity,
                 maya::Style{}.with_fg(cx_color(msg.smart_route_complexity)));
            if (!msg.smart_route_note.empty()) {
                push("   ", maya::Style{});
                push(msg.smart_route_note, maya::Style{}.with_fg(muted).with_italic());
            }
            cfg.body.emplace_back(maya::Turn::BodySlot{maya::Element{maya::TextElement{
                .content = std::move(content),
                .style   = {},
                .wrap    = maya::TextWrap::TruncateEnd,
                .runs    = std::move(runs),
            }}});
        }

        // 第2行:活动层作为紧凑的标签.
        {
            std::string content;
            std::vector<maya::StyledRun> runs;
            auto chip = [&](std::string_view label, bool on) {
                const std::size_t s = content.size();
                content.append(on ? "\xe2\x97\x8f " : "\xe2\x97\x8b ");  // ● / ○
                content.append(label);
                content.append("   ");
                runs.push_back(maya::StyledRun{s, content.size() - s,
                    maya::Style{}.with_fg(on ? status_ok : muted)});
            };
            chip("orchestrate", msg.smart_route_orchestrate);
            chip("subagents",   msg.smart_route_subagents);
            cfg.body.emplace_back(maya::Turn::BodySlot{maya::Element{maya::TextElement{
                .content = std::move(content),
                .style   = {},
                .wrap    = maya::TextWrap::TruncateEnd,
                .runs    = std::move(runs),
            }}});
        }
        return cfg;
    }

    // 主动-retrieval上下文转:与紧凑边界一样,
    // 这是MODEL完全看到的(msg.text携带<retrieved-context>
    // 块)合成用户信息,但记录稿渲染是静默的1-liner,
    // 所以原始通道不会支配用户的视角,它本身的虚弱身份(一本书glyph,
    // "Retrieved context"标签)标记为
    // 自动-injected参考,而不是用户的词.
    if (const auto& pc = msg.proactive) {
        // 解析 [来源:路径:线] 头文件 出块后,卡片可以显示用户
        // 内容 答案是建立在基础上的,而不仅仅是多少段。 头文件 窗体
        // 的行 [docs: path/to/file.md
        //   :12]/[技能:git:0]/[1]]  ⁇ .
        // 内存 :...] 我们收集 DISTINCT "source path"
        // 双( 丢弃行号以及同一文件的任何重复块) 因此,
        // 贡献了三个段落的文件会显示一次。 便宜: 对一个有界限
        // 块进行一次线性扫描。
        int n = 0;
        // 人数 -distinct-source 来源,
        // 在第一 -seen 我们捕捉到所有可以显示的,
        // 所以卡片读起来像真实的引用,而不仅仅是文件名: 那种,
        // 路径,路径, 首次 它引用的行号,同一来源贡献的
        // 段落(chunks),以及第一个段落文本的预览。
        struct Src {
            std::string kind;      // 文档/技能/内存/mcp
            std::string path;      // 文件路径或记录 id (无: line)
            std::string line;      // 首引用行号( 3) "" (无)
            std::string snippet;   // 第正文段预览
            std::string full;      // 完全通过 正文 (捆绑)用于扩充
            int         chunks{0}; // 同一来源的段落
        };
        std::vector<Src> sources;
        // 人数 -kind 记录( docs/skill/memory/mcp/ .) 契约
        // "2 memory · 1 doc" 元线的细分情况——以首字母 -seen 秩序。 秩序。
        std::vector<std::pair<std::string, int>> kinds;
        auto bump_kind = [&](const std::string& k) {
            for (auto& kv : kinds) if (kv.first == k) { ++kv.second; return; }
            kinds.emplace_back(k, 1);
        };
        for (std::size_t p = msg.text.find("\n["); p != std::string::npos;
             p = msg.text.find("\n[", p + 1)) {
            ++n;
            std::size_t open = p + 2;                       // 过去 "\n["
            std::size_t close = msg.text.find(']', open);
            if (close == std::string::npos) continue;
            std::string tag = msg.text.substr(open, close - open);
            // 拆分( E) "source:path:line" → "source" + "path"
            // (按一条线索) ":line" 如果最后一个结肠 -field 数字。
            std::size_t colon = tag.find(':');
            std::string src  = colon == std::string::npos
                                 ? std::string{"docs"} : tag.substr(0, colon);
            std::string path = colon == std::string::npos
                                 ? tag : tag.substr(colon + 1);
            bump_kind(src);
            std::string lineno;
            if (std::size_t lc = path.rfind(':'); lc != std::string::npos) {
                std::string_view tail{path.data() + lc + 1,
                                      path.size() - lc - 1};
                if (!tail.empty()
                    && std::all_of(tail.begin(), tail.end(),
                                   [](unsigned char c){ return std::isdigit(c); })) {
                    lineno = std::string{tail};
                    path.resize(lc);
                }
            }

            // 第一次不拉就拉出来 -empty 线条 此 过路的
            // 正文 以预览形式,并 完整 正文 (下个) "\n["
            // 头文件 或结尾),用于扩展视图。 正文 刚开始 头
            // 文件 关闭 "]\n".
            std::string snippet, full;
            std::size_t body = msg.text.find('\n', close);
            if (body != std::string::npos) {
                ++body;                                     // 过新线
                // 整个通道一直到下一个 头文件 ("\n[" )或块端.
                std::size_t next = msg.text.find("\n[", body);
                std::size_t stop = next == std::string::npos
                                     ? msg.text.size() : next;
                full = msg.text.substr(body, stop - body);
                // 调整后行/ 空白空间。
                while (!full.empty()
                       && (full.back() == '\n' || full.back() == ' '
                        || full.back() == '\t' || full.back() == '\r'))
                    full.pop_back();
                // 封锁了扩建,以至于一个巨大的通道不能吹动卡片.
                constexpr std::size_t kFullCap = 800;
                if (full.size() > kFullCap) {
                    full.resize(kFullCap);
                    full += "\xe2\x80\xa6";                 // …
                }

                std::size_t eol = msg.text.find('\n', body);
                std::string line = eol == std::string::npos
                    ? msg.text.substr(body)
                    : msg.text.substr(body, eol - body);
                std::size_t a = line.find_first_not_of(" \t");
                if (a != std::string::npos) {
                    line.erase(0, a);
                    // 折叠内部白空间运行到一个空格,所以引用从一个空格中
                    // 解开 ALIGNED 缩放表格行( Q)
                    // "col a          | col b")
                    // 渲染 以一条紧凑的线,而不是由死沟槽的垫片空格分割。
                    // Tabs/CR 也已经恢复正常了
                    std::string compact;
                    compact.reserve(line.size());
                    bool in_ws = false;
                    for (char ch : line) {
                        const bool is_ws = ch == ' ' || ch == '\t'
                                        || ch == '\r' || ch == '\n';
                        if (is_ws) {
                            if (!in_ws) compact.push_back(' ');
                            in_ws = true;
                        } else {
                            compact.push_back(ch);
                            in_ws = false;
                        }
                    }
                    // 丢弃任何倒塌留下的后方空间。
                    if (!compact.empty() && compact.back() == ' ')
                        compact.pop_back();
                    line = std::move(compact);
                    // 留着 整个 第一行(仅限 渲染 键
                    // / 缓存 ——视图将其剪切到 实际
                    // 彩绘时的卡宽度 `| clip`
                    // 宽度 终端 显示远不止一个窄的,
                    // 而不是固定的 60 -char 死空的树枝
                    constexpr std::size_t kSnipCap = 512;
                    if (line.size() > kSnipCap) {
                        line.resize(kSnipCap);
                        line += "\xe2\x80\xa6";             // …
                    }
                    snippet = std::move(line);
                }
            }

            // 合并到 difficient-source 列表中:一个曾经贡献过
            // 三个块的显示的文件, dicus  ⁇ 3 和第一个引用的行 /
            // spipet / full 正文 保留.
            auto it = std::find_if(sources.begin(), sources.end(),
                [&](const Src& s){ return s.kind == src && s.path == path; });
            if (it != sources.end()) {
                ++it->chunks;
            } else {
                sources.push_back(Src{std::move(src), std::move(path),
                                      std::move(lineno), std::move(snippet),
                                      std::move(full), 1});
            }
        }

        cfg.glyph      = "\xf0\x9f\x93\x9a";          // 📚
        cfg.label      = "Retrieved context";
        // 蓝色铁路:这是 CONTEXT / 参考转弯( 转弯)
        // status_info ),它也把整张卡片从平面的哑灰色中抬出来.
        cfg.rail_color = status_info;

        // 元线( 右) -aligned ,就像在助手转弯时经过的时间)
        // 背负整个 "how good / how much" 提要 简
        // 表 正文 纯种的,是自由的。 COLOR-SEGMENTED
        // 脱光( rax) cfg.meta_element )——单调弦
        // 不能通过自信来触动表,这个图读起来一看:
        //   - 4个中度医生;
        //   └─ filled cells + %+word tinted green/yellow/red by strength;
        //      空 存储单元 已变音; 代码中的种类计数 -cyan ,标签暗淡。
        // 紧凑( 8) -cell 测量,缩写为类型标签,
        // 无 " total" 并剪辑了它,这样它就可以
        // 绝不 将“完全响应”包装到第二行:窄 终端 略微
        // 计算 末尾 宽度显示整个, 测量率+% 总是存活。
        {
            using namespace maya::dsl;
            // 元条条是用每-segment的颜色运行的SINGLE TextElement建造的,
            // 使TextWrap::TruncateEnd变为了双元条条条条.
            // 一个单节节线的文本节点是GUARANTEED,可以占用任何宽度的一行: .
            // 独立的彩色节点的支架(和在~30上) -col 移动 SSH
            // 终端 已经),溢出 头文件 排行和 WRAP 放在第二行(
            // 键入) ( "67%·str" / "ong" )将每个
            // 部分叠成一个节点+ TruncateEnd 使结构上的
            // 包装无法进行——狭义 终端 略微计算 末尾 测量度+%
            // (被允许的左侧)总是能存活下来。
            std::string meta;
            std::vector<maya::StyledRun> mruns;
            auto push = [&](std::string_view s, maya::Style st) {
                if (s.empty()) return;
                mruns.push_back(maya::StyledRun{meta.size(), s.size(), st});
                meta.append(s);
            };

            // 在`pc`守护内部,因此信心只能通过一个
            // 实际上有一条信息——没有哨兵比较——来达到.
            if (pc->confidence) {
                const double c = std::clamp(*pc->confidence, 0.0, 1.0);
                constexpr int kCells = 8;
                const int filled = static_cast<int>(c * kCells + 0.5);
                // 1 hue = 1 含义: 强度图到状态轴( 绿色 OK / 黄色
                // 警告 / 红色 弱) 所以显示器颜色本身就答了。
                // "trust this grounding?" 预览 -attentively.
                const char* word = c >= 0.60 ? "strong"
                                 : c >= 0.35 ? "moderate"
                                             : "weak";
                const maya::Color tint = c >= 0.60 ? status_ok
                                       : c >= 0.35 ? status_warn
                                                   : status_error;
                std::string filled_cells, empty_cells;
                for (int i = 0; i < filled; ++i)      filled_cells += "\xe2\x96\xb0";
                for (int i = filled; i < kCells; ++i) empty_cells  += "\xe2\x96\xb1";
                const int pct = static_cast<int>(c * 100.0 + 0.5);
                // 填充部分在强度色调中, 空在哑音中,
                // 显示表读作真正的填充条, 然后粗体+暗字
                // 两者都锡着, 所以整个集群 汇成一个信号。
                push(filled_cells, maya::Style{}.with_fg(tint));
                push(empty_cells,  maya::Style{}.with_fg(muted));
                push(" " + std::to_string(pct) + "%", maya::Style{}.with_fg(tint).with_bold());
                push(std::string("\xc2\xb7") + word, maya::Style{}.with_fg(tint).with_dim());
            }

            // 由 -kind 细目( "2 doc · 1 mem" ) 答案( 多少 )。
            // 代码中的数 -reference 青色(您扫描的数量),
            // 标签+点数 dim。 缩略以保持条形线短;
            // 一条后缀的暗色总链接多 -kind 划开
            auto short_kind = [](std::string k) -> std::string {
                // singularize/abbrev: "memory"->"mem", "docs"->"doc" 等 类.
                if (k == "memory")  return "mem";
                if (k == "docs" || k == "doc") return "doc";
                if (k == "skill" || k == "skills") return "skill";
                if (k.size() > 5) k.resize(5);
                return k;
            };
            if (!meta.empty())
                push(" \xc2\xb7 ", maya::Style{}.with_fg(muted));
            if (!kinds.empty()) {
                for (std::size_t i = 0; i < kinds.size(); ++i) {
                    if (i) push(" \xc2\xb7 ", maya::Style{}.with_fg(muted));
                    push(std::to_string(kinds[i].second) + " ", maya::Style{}.with_fg(code_path));
                    push(short_kind(kinds[i].first), maya::Style{}.with_fg(muted));
                }
                if (kinds.size() > 1 && n > 0)
                    push("  (" + std::to_string(n) + ")", maya::Style{}.with_fg(muted));
            } else {
                const int shown_n = n > 0 ? n : 1;
                push(std::to_string(shown_n) + " ", maya::Style{}.with_fg(code_path));
                push(shown_n == 1 ? "passage" : "passages", maya::Style{}.with_fg(muted));
            }

            cfg.meta_element = maya::Element{maya::TextElement{
                .content = std::move(meta),
                .style   = {},
                .wrap    = maya::TextWrap::TruncateEnd,
                .runs    = std::move(mruns),
            }};
        }

        // 正文 = 纯来源,每个不同来源的一条密集线:
        //   └ 记忆 · 0357a6d8            “agentty 现在具有原生……”
        //   └ 文档 · RUST-CRITIQUE.md:42 ×3  “Rust 支持者会在哪里……”
        // 类型是哑音, 代码路径 -reference
        // 青(可操作锚), ":line" 引文中附有一个
        // 和一个 "×N" 一条线条的路段 被一个线条所覆盖
        // 片段的线索在引文中,所以你可以看到 CONTENT 答案,
        // 所有基础 一个 排行。 "… N more".
        {
            using namespace maya::dsl;
            constexpr std::size_t kMaxSources = 5;
            const std::size_t total = sources.size();
            const bool expanded = pc->expanded;
            // 将每个出处行装入ONE 漏洞-0 vstack,
            // 作为单个BodySlot. maya::Turn在相邻的非
            // -blank 正文空档间插入一个空白空档,这样,
            // 每个出处都作为自己的空档在行间搁浅。 将它们装入一个空档间,
            // 使得maya看到一个单一块 – 排在彼此之间,
            // 一个跨-slot空档只将整个出处块与周围的meta/affordance分开。
            //
            std::vector<maya::Element> rows;
            for (std::size_t i = 0; i < sources.size() && i < kMaxSources; ++i) {
                const Src& s = sources[i];
                std::string kindpart = s.kind + " \xc2\xb7 ";     // "docs · "
                std::string pathpart = s.path.empty() ? s.kind : s.path;
                if (!s.line.empty()) pathpart += ":" + s.line;
                // "×N"徽章只有在此来源贡献 >1通过时才使用.
                std::string badge = s.chunks > 1
                    ? "  \xc3\x97" + std::to_string(s.chunks) : std::string{};

                // 每个出处条目必须是ONE的文本节点。
                // 横向堆放单个样式的碎片仍要求瑜伽将每个
                // 碎片都抛出;一旦固定的前缀消耗了行,
                // 以后的碎片即使最后片段已经包起来也能够包起来.
                // `| clip`。结合运行使宽度
                // 制约结构而非最佳 -effort.
                auto source_row = [&](std::string_view preview) {
                    std::string content;
                    std::vector<maya::StyledRun> runs;
                    auto push = [&](std::string_view part, maya::Style style) {
                        if (part.empty()) return;
                        runs.push_back(maya::StyledRun{content.size(), part.size(), style});
                        content.append(part);
                    };
                    push("  \xe2\x94\x94 ", maya::Style{}.with_fg(muted));
                    push(kindpart, maya::Style{}.with_fg(muted));
                    push(pathpart, maya::Style{}.with_fg(code_path));
                    push(badge, maya::Style{}.with_fg(status_warn));
                    if (!preview.empty()) {
                        push("  ", maya::Style{}.with_fg(muted));
                        push(preview, maya::Style{}.with_fg(muted));
                    }
                    return maya::Element{maya::TextElement{
                        .content = std::move(content),
                        .style = {},
                        .wrap = maya::TextWrap::TruncateEnd,
                        .runs = std::move(runs),
                    }};
                };

                if (expanded) {
                    // 扩展的内容也仍然是一行。
                    // 渲染器 ,但被剪切到当前
                    // 终端 宽度而不是将检索卡(
                    // 以及之后的转折)推到额外的行上。
                    std::string quoted = s.full.empty() ? std::string{}
                        : " \xe2\x80\x9c" + s.full + "\xe2\x80\x9d";
                    rows.push_back(source_row(quoted));
                } else if (!s.snippet.empty()) {
                    // 在同一节点保留引号:不贴近引号,
                    // 而且,关键是,不贴第二行。
                    rows.push_back(source_row(
                        "\xe2\x80\x9c" + s.snippet + "\xe2\x80\x9d"));
                } else {
                    rows.push_back(source_row({}));
                }
            }
            if (total > kMaxSources) {
                rows.push_back(maya::Element{maya::TextElement{
                    .content = "  \xe2\x80\xa6 " + std::to_string(total - kMaxSources)
                             + " more source"
                             + (total - kMaxSources == 1 ? "" : "s"),
                    .style = maya::Style{}.with_fg(muted),
                    .wrap = maya::TextWrap::TruncateEnd,
                }});
            }
            if (!rows.empty()) {
                cfg.body.emplace_back(maya::Turn::BodySlot{
                    maya::detail::vstack().gap(0)(std::move(rows)).build()});
            }
            // 付得起的脚步手——只有满了 正文 值得扩展到。
            // 告诉用户 切换 和它所做的一切,
            // 暗淡的,所以它永远不与引用的内容竞争。
            // 将单人间作为自己的插槽 -slot
            // 隔板把它和上面的冲浪源块分开
            const bool any_full = std::any_of(sources.begin(), sources.end(),
                [](const Src& s){ return !s.full.empty(); });
            if (any_full) {
                const std::string affordance = expanded
                    ? "  ^U collapse"
                    : "  ^U expand full passages";
                cfg.body.emplace_back(maya::Turn::BodySlot{
                    maya::Element{maya::TextElement{
                        .content = affordance,
                        .style = maya::Style{}.with_fg(status_info),
                        .wrap = maya::TextWrap::TruncateEnd,
                    }}});
            }
        }
        return cfg;
    }

    if (msg.role == Role::User) {
        // 替换 标签 占位符(\x01ATT:N\x01) 与他们的人类
        // -readable 标题如此 400 -line 粘贴 渲染
        // 以 "[Pasted text · 412 lines · 14 KB]"
        // 而不是把整个都排入记录片 正文。该词 传输 仍然看到满的 字
        // 节 - 电话 传输层 调用 attachment::expand
        // (a) 应请求 -build 时间。图像占位符查询
        // msg.attachments (每个图像仍保留一个条目,
        // 其中 path/media_type/byte_count 即使在之后,
        // 人口也仍然众多 字节 被抬到 msg.images ),
        // 所以一样 标签 作曲中使用的标签公式在此逐字应用。
        std::string display;
        if (msg.attachments.empty()) {
            display = msg.text;
        } else {
            display.reserve(msg.text.size());
            std::size_t i = 0;
            while (i < msg.text.size()) {
                if (static_cast<unsigned char>(msg.text[i]) == attachment::kSentinel) {
                    auto len = attachment::placeholder_len_at(msg.text, i);
                    if (len > 0) {
                        auto idx = attachment::placeholder_index(msg.text, i);
                        if (idx < msg.attachments.size()) {
                            display.push_back('[');
                            display.append(attachment::chip_label(msg.attachments[idx]));
                            display.push_back(']');
                        }
                        i += len;
                        continue;
                    }
                }
                display.push_back(msg.text[i++]);
            }
        }
        cfg.body.emplace_back(maya::Turn::PlainText{.content = std::move(display), .color = fg});
    } else if (msg.role == Role::Assistant) {
        append_assistant_body_slots(cfg, msg, tool_calls, m, style);
        if (msg.error) cfg.error = *msg.error;
    }

    return cfg;
}

maya::Turn::Config turn_config_for_assistant_run(
    std::size_t run_first, std::size_t run_end,
    int turn_num, const Model& m)
{
    const auto& msgs = m.d.current.messages;
    // 预览 -conditions 仅两人为辩护 调用 地点( build_live_tail
    // / freeze_range ),但无论如何要注意,这样这个功能
    // 就可以在不细微行的情况下被重用 -shape 腐败,如果范围 变成空的。
    if (run_first >= run_end || run_first >= msgs.size())
        return {};
    const std::size_t end = std::min(run_end, msgs.size());

    const Message& head = msgs[run_first];
    auto style = speaker_style_for(head.role, m, &head);

    maya::Turn::Config cfg;
    cfg.glyph        = style.glyph;
    cfg.label        = style.label;
    cfg.rail_color   = style.color;
    cfg.meta         = format_turn_meta(head, turn_num,
                          head.role == Role::Assistant
                              ? assistant_elapsed(head, m)
                              : std::nullopt,
                          m.wall_at(maya::anim_now()));
    if (!style.role_label.empty())
        cfg.meta = style.role_label + "  \xc2\xb7  " + cfg.meta;

    if (head.role != Role::Assistant) {
        // 防御:只有助理运行使用多-message路径.
        // 对于用户头部来说, 崩溃到 -message 的单块。
        return turn_config(head, run_first, turn_num, m,
                           /*继续=*/false,
                           /*meta_override=*/{},
                           /*tool_calls_override=*/{});
    }

    // 运行,每个信件释放一个工具面板, 并携带工具。
    // agent_session '纪律: 一个工具批次=一个面板,
    // 当下一个文本块启动时冲掉 OR 运行结束。
    // agentty 术语a "batch" 是一个单项工具
    // Anthropic 信件; 邮件 -tool 占位符
    // 模型给出每个子信件一个 -turn 已经,所以每个消息的
    // tool_calls 概念上是一批(模型的回复文本用于该
    // 子) -turn 或者在他们之前传出同样的教诲,
    // 或者在以下的教诲中传到他们面前。
    //
    // 所交付的命令 正文 镜像 传输 顺序:文本(一) → 面板
    // (一) — 文本(一+1) → 表板(一+ 1)...
    // 子 -turns 唯一的贡献是 工具批(无文本) 仅发出一个面板。
    // 无十字架 -Message 合并: 每个子 -turn 获得自己的面板,
    // 匹配 agent_session 每一地点 ev::ToolEnd 批次变成自己的
    // actions_panel(...).
    std::string error_accum;

    // 把一个子轮次的正文槽位输出到 into，从
    //    运行循环,所以已解决 -prefix 下方的升能重用 EXACT
    //    同一档次的构造( 4) : 字节-identical 排)
    //    在巢中转弯.
    auto emit_subturn = [&](std::size_t idx, maya::Turn::Config& into) {
        const Message& m_i = msgs[idx];
        // 理由块第一——在本子之上 -turn 的答案文本。同样 DRY
        // 帮助者作为单身 -message 路径如此的活和冻的匹配。
        if (auto rs = reasoning_slot(m_i, m))
            into.body.emplace_back(std::move(*rs));
        const bool has_text = !m_i.text.empty() || !m_i.streaming_text.empty();
        bool defer_panel = false;
        if (has_text) {
            into.body.emplace_back(cached_markdown_for(m_i, m));
            // 一样 -frame 推迟握手为 append_assistant_body_slots:
            // cached_markdown_for 刚刚决定(这个) 帧
            // 是否为 逐步显示 光标仍然在中端 -glide 并带有新的工具卡。
            // 读读 标志 以非方式 -migrating peek() —
            // cached_markdown_for 可能已经
            // PINNED 和关于 -accessing 透过定居的
            // message_md (这里会把它从被钉住的套装上移
            // 回) -pinning 我们刚保护的边缘 peek()
            // 读着它住的哪个家 而不扰动分界线
            if (const auto* mc = m.ui.view_cache.peek(m.d.current.id, m_i.id))
                defer_panel = mc->defer_tool_panel;
            // 工具一旦真正存在,就永远不要推迟 EXECUTING - 和我们一样的警卫
            // append_assistant_body_slots。推迟只使简短的散文平滑
            // -glide 与新* 关系 待处理*-card 窗口; a Running/Done
            // 牌必须立即显示, 即使延迟 -exit 机器缺少一个跟随 -up 帧
            // 工具显示正在运行，却没有卡片。
            if (defer_panel
                && std::any_of(m_i.tool_calls.begin(), m_i.tool_calls.end(),
                               [](const ToolUse& tc){ return !tc.is_pending(); }))
                defer_panel = false;
        }
        if (!m_i.tool_calls.empty() && !defer_panel) {
            append_assistant_tool_panel(
                into,
                m_i,
                std::span<const ToolUse>{m_i.tool_calls},
                m, style);
        }
        if (m_i.error && error_accum.empty()) error_accum = *m_i.error;
    };

    // 每个子轮次具有稳定标识的槽位，使每帧开销保持固定，并提供
    //    腐败 -proof 现场直播 末尾 转弯深度。
    //
    //    进一进 -flight 助理运行可以累积数百个已安顿
    //    的子程序 -turns 都坐在现场 末尾 直到整个
    //    运行平息和冻结(冻结只发生于转弯) -settle ;
    //    没有声音中 -run 冻结——见 frozen.cpp
    //    故曰活人. 末尾 在深层自动驾驶运行中 及其领先的
    //    子程序中 生长无限制 -turns 溢出进入
    //    终端's 不可变 本地语言 回滚缓冲区 运行还在进行中,
    //    有两个要求,一个机制满足了两个条件:
    //
    //      (1) FLAT 百分比 -frame 成本: 已结算子 -turn 绝对
    //          不是 rebuilt/re-walked 从零开始 帧 ,或按 -frame
    //          CPU 增长 O( 转弯深度 )。
    //      (2) APPEND-ONLY 回滚缓冲区 : 一次子 -turn
    //          排队对本地人有承诺 回滚缓冲区 他们可以 绝不 在一个改变位置
    //          或改变元素身份下重新发射—— maya 内置 diff 处理任何已
    //          执行 -row 变异为无法更正和 HardResets (\x1b
    //          [2J\x1 b[3J],擦除+重排笔录. 此 是用户的报表 :
    //          运行中的工具卡片未显示，随后回复和旧卡片又渲染错乱。
    //
    //
    //
    //    机制: 包 EACH 已结算子 -turn 中文本无 自身 光线 打开按键在 a
    //    STABLE 百分比 -sub-turn 散列( 其信件 id + 其本身)
    //    compute_render_key 后果:
    //
    //      • 终生稳定身份。 -turn 时段占用
    //        固定 正文 指数(其 序号 )和,一旦潜艇 -turn 和解,
    //        一个固定 hash_id ——每笔 帧 直到它冻起来。
    //        maya 由它的组成部分擦出 缓存 (费用上涨),
    //        不再重 -emits 附录 -only 安全地),一个安顿的子.
    //        -turn 刚刚从 "hash changes each frame"
    //        (活)到 "hash frozen" ——确切的生命周期 maya
    //        已经处理整个直播 末尾.
    //      • NO 重新组合,永远, 早期增长 -prefix 财务报告和已审计财务报表
    //        fixed/row 块设计两个 MOVED 子类 -turn 从 a。
    //        提前加载 顶端 -level 插槽 INTO 不一样
    //        -keyed 组合随边界的推进而转动。在 a 可视区域 转弯已经够短了,
    //        移动重排了承诺的一行。 HardReset 数字
    //        -sub-turn 插槽从不在容器之间移动,因此不能在
    //        任意 深度或 显示 终端 大小( 4) scrollback_oracle
    //        deep_run_turn : 绿色在每一个
    //        形状上).
    //
    //    坚固的雕刻 -outs (必须保留每个 -frame REBUILT ,
    //    没有被刺键,而其中的任何一个持有——否则一张卡片消失
    //    了或一个动画摊:
    //      • 非 -terminal 工具( 仍在运行): 其 render_key 已经
    //        预发 帧 (progress/elapsed ),
    //        所以按键是自然的错失+重建;我们仍然在键上盖章,
    //        这样,血滴就立即解决。
    //      • A live/finalizing/revealing/parsing 逐步显示 部件:散列
    //        谓 不变量 穿过了整个摇篮 干净的过渡 如此
    //        稳定的键中 -reveal 将冻结拼音。
    //        -turn UNKEYED (重建时间) 帧 )直到排水。
    //      • 一台活动工具-panel DEFER机(defer_tool_panel /
    //        defer_exit_finished / card_defer_since
    //        : 推迟 标志 单位 -frame 可变 状态 不 折叠到
    //        渲染 键,以及其两个 -phase 只退出预付,
    //        而 cached_markdown_for 运行 帧 稳定键将(
    //        a) 烤面板 -hidden 正文 键下推迟 -clear
    //        永远不要碰 ~ 牌永远隐形,和 (b) 拖住退出机器。
    //        保留它 UNKEYED 直到延迟机器闲置为止
    //
    //    字节-identity 与平面建筑( freeze_range 整个运行
    //    -hash 现场直播 -tail 构造所控路径:每一次光转再利用
    //    maya::Turn 属于自己的 is_blank-gated 正文 组装起来,
    //    像顶部一样坐着 -level 正文 插槽,所以,( 插槽) build_inner
    //    插入相邻非相邻的精确空格 -blank 插槽——与排放子的同列 -turn's
    //    markdown/panel 内置。
    auto subturn_stably_keyable = [&](std::size_t j) -> bool {
        const auto& mj = msgs[j];
        if (mj.role != Role::Assistant) return false;
        // 现场直播 传输 字节 仍然到达 —— 这是 流式传输 边( 或子)
        // -turn 中点 -stream。。 其内容 reveal_fx 动画
        // / 渐变 / 光标 BETWEEN 字节 抵达者 NO 大小变化,
        // 所以内容 -keyed 散列会在三角洲之间冻结: maya 闪烁了缓存的光转,
        // cached_markdown_for 永远不要 -runs ,
        // 其编号为 request_animation_frame () 永远不再
        // -armed ,打字机则 FREEZES 直至不相干的散列轴翻转(低点)
        // -CPU "md gets stuck mid-turn"。必须内置,
        // 这样它就可以在下方 -frame 构建器持续运行。 已检查 首次 ——不
        // 考虑是否 msg.text 还没有确定前缀。
        if (!mj.streaming_text.empty() || !mj.pending_stream.empty())
            return false;
        for (const auto& tc : mj.tool_calls)
            if (!tc.is_terminal()) return false;
        // 无 -migrating peek() : 这是 READ-ONLY 州检,
        // 通过 message_md () 将一个被钉住的活人迁移到定居
        // 的地图上——即未定居的地图。 -pin 我们必须避免。
        // 一个尚未存在的插槽是微不足道的不动画(没有部件),所以 nullptr
        // − 可键 -so-far.
        const auto* mc = m.ui.view_cache.peek(m.d.current.id, mj.id);
        if (mc && (mc->defer_tool_panel || mc->defer_exit_finished
                || mc->card_defer_since_ms != 0))
            return false;
        // 逐步显示 部件仍在动画中( 启用/
        // 最终确定斜坡/ 光标滑翔积压/ 自动启动)
        // 解析 □ 与活体相同的冻结危险 字节
        // : 散列是( ) 不变量 使用文本是否已经执行。
        // 消息可以在最后的斜坡上有一个已解决的文本前缀,
        // 并且部件仍然在运行中。
        // DON 打开这个在文本是非 -empty.
        if (mc && mc->streaming && mc->streaming->is_animating())
            return false;
        // 相同 兄弟姐妹的危害 REASONING 插槽( N) mj.id + "#r"
        // : 推理 数据流 透过自己的 StreamingMarkdown 其 人 名
        // 逐步显示 动画 BETWEEN 思考三角洲,没有答案 -channel 大小变化。
        // 如果我们按下关闭内容键,同时进行推理 逐步显示 中间 -glide ,
        // 推理部件的构建器停止了 -running 和推理打字机 FREEZES (
        // 推理内容一次性出现，没有流式显示。
        // 在推理时必须保持内在 逐步显示 谓 live/animating 也一样。
        const auto* rc =
            m.ui.view_cache.peek(m.d.current.id, MessageId{mj.id.value + "#r"});
        if (rc && rc->streaming && rc->streaming->is_animating())
            return false;
        return true;
    };

    // ── 现场直播 -edge 保护是 STRUCTURAL 而不是手动触摸 ──
    //
    //    下面的环 调用 cached_markdown_for
    //    (a) 每分公司一次 -turn 案文——O(深度)
    //    不同( 线程 .msg) 访问次数 帧 之前,我们分享了
    //    一个 LRU 有了活的边缘,一旦深度超过盖子,
    //    步行就驱逐了最古老的条目,因为它插入了新的条目——和 流式传输
    //    边( 上一封信件, 触摸上一个) 坐在 LRU 回到散步开始的时候
    //    它摧毁了它的StreamingMarkdown部件+逐步显示簿记,
    //    从头开始重新启动逐步显示并拖住打字机(
    //    "md animation not smooth in a long turn"报告)。
    //    旧的固定装置是一条带-and-braces手动触碰的边缘BEFORE,
    //    走动加一个凸起的盖子,这样现实的深度就不会被打
    //    乱了——这两种装置都减轻了共享的-LRU危险。
    //
    //    危险已经不复存在了,没有 LRU 现在,所有。
    //    cached_markdown_for 路径一条实时消息( live)
    //    传输 字节 OR 动画部件)通过 缓存's PINNED 映射,
    //    从未被删除; 已定居子 -turns 在一个单独的中图中,
    //    它永远不会被逐出——这是 DROPPED 百分比 -message
    //    冻结时(见 cache.hpp / freeze_range。
    //    没有行走深度可以触摸活的边缘,也没有行走深度会长出来 内存
    //    : 两张地图都以活动转弯为界。 摊位类不能够代表, 不仅仅是不可能。
    //    没有预览 -touch 给

    for (std::size_t i = run_first; i < end; ++i) {
        if (msgs[i].role != Role::Assistant) break;   // 运行边界
        if (subturn_stably_keyable(i)) {
            // 已解决子 -turn : 发射作为自己的光线 转动,
            // 每转稳定 -sub-turn hash_id. maya 缓存 +
            // 擦伤每个 帧 永远不要 -emits 一旦它一行承诺 回滚缓冲区.
            maya::Turn::Config sub;
            sub.bare       = true;
            sub.rail_color = style.color;
            emit_subturn(i, sub);
            if (sub.body.empty()) continue;   // 无所显示
            sub.hash_id = maya::CacheIdBuilder{}
                .add(std::string_view{"agentty.turn.subturn"})
                .add(std::string_view{msgs[i].id.value})
                .add(msgs[i].compute_render_key())
                .build();
            cfg.body.emplace_back(maya::Turn{std::move(sub)}.build());
        } else {
            // 直播/ 动画/ 推迟子 -turn : 将内线建在外侧
            // 转身 -effecting 百分比 -frame
            // 建筑师( 逐步显示 光标,推迟退出机器, 加载动画 继续跑
            emit_subturn(i, cfg);
        }
    }

    if (!error_accum.empty()) cfg.error = std::move(error_accum);

    return cfg;
}

std::size_t turn_run_end(const std::vector<Message>& messages,
                         std::size_t from)
{
    if (from >= messages.size()) return from;
    if (messages[from].role != Role::Assistant) return from + 1;
    std::size_t end = from + 1;
    while (end < messages.size()
           && messages[end].role == Role::Assistant) {
        ++end;
    }
    return end;
}

maya::CacheId assistant_run_hash_id(
    const Model& m, std::size_t run_start, std::size_t run_end)
{
    const auto& msgs = m.d.current.messages;
    maya::CacheIdBuilder kb;
    kb.add(std::string_view{"agentty.turn.assistant_run"})
      .add(static_cast<std::uint64_t>(run_end - run_start));
    for (std::size_t j = run_start; j < run_end && j < msgs.size(); ++j) {
        kb.add(std::string_view{msgs[j].id.value});
        kb.add(msgs[j].compute_render_key());
    }
    return kb.build();
}


} // namespace agentty::ui
