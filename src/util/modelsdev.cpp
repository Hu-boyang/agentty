// agentty::modelsdev ——背景 快照 备注 models.dev (见A/C.5/61/L.48,第16段) 头文件).

#include "agentty/util/modelsdev.hpp"
#include "agentty/version.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <string_view>

#include <maya/runtime.hpp>

#include <simdjson.h>

#include "agentty/util/user_root.hpp"    // user_cache_dir( ) (中文( ) ).
#include <limits>
#include "agentty/domain/catalog.hpp"   // set_catalog_reasoning / set_catalog_effort_set 数据
#include "agentty/io/http.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/teardown.hpp"

namespace agentty::modelsdev {

namespace {

namespace fs = std::filesystem;

fs::path cache_path() { return util::user_cache_dir() / "modelsdev.json"; }

constexpr auto kMaxAge = std::chrono::hours{24};
constexpr std::size_t kMaxBody = 8ull * 1024 * 1024;   // api.json ~2 时 MB

// 地图a models.dev 工作值为我们的工作位; 0 = 不在我们梯级( ) e.g.
// "ultra" )——被忽略,无 nearest_effort 夹住缺口。
std::uint8_t bit_of(std::string_view v) {
    using agentty::Effort;
    using agentty::effort_bit;
    if (v == "minimal") return effort_bit(Effort::Minimal);
    if (v == "low")    return effort_bit(Effort::Low);
    if (v == "medium") return effort_bit(Effort::Medium);
    if (v == "high")   return effort_bit(Effort::High);
    if (v == "xhigh")  return effort_bit(Effort::Xhigh);
    if (v == "max")    return effort_bit(Effort::Max);
    return 0;
}

// 登记一个模型的申报。
//
// KEYING IS 该 整个 GAME 给 models.dev 模式键只是 SOMETIMES
// 提供者 -prefixed ("mistral-medium-3-5" 请求时,
// "mistral/mistral- medium-3.5" 纳米时 -gpt,
// "mistralai/mistral-medium-3-5" 和 开路器上。 agentty 能力注册表的使用
// "provider/model" 范围化密钥。 已经处理原始密钥时有两个错误 -scoped:
//   • 未加固定的密钥 AGGREGATOR (要求的全低 最大努力)
//     用于模型的阶梯 BINARY 直接设置和
//     匹配 agentty 赤裸裸的 ! -id 回
//     退 真正的供应商走错梯子;
//   • a "mistral/..."-prefixed 纳米键 -gpt 碰撞 字节-for-byte
//     与 agentty's 自身 "mistral/<model>" 范围
//     命名空间——第三方的假冒为米斯特拉尔的声明 -platform 事实。
// 现在:权威记录是: 始终 列入 models.dev PROVIDER 编号(
// "mistral/mistral-medium-2604",
// "requesty/mistral-medium- 3-5" )——对于真实的平台(
// mistral,openai,groq,cerebras...)来说,完全吻合. agentty
// ' 终点 ' -label 范围,所以活动提供者的范围搜索从 ITS 自身 models.dev 进入,
// 永远不要集合, 赤尾去 仅 通过合并 -or-poison 路径 :
// 横 -provider 协议保留事实,任何分歧都会毒害 赤裸裸的钥匙
// "no info" 故解脱归於范围之事实或id推论。
// -vs-openrouter 对错误的分歧 -medium- 3 -5现在
// 毒死赤色的钥匙, 和id推断的 effort_high_only
// 正确输出 {off, high} 梯子。 )
void register_model(CatalogBatch& out,
                    const std::string& dev_provider,
                    const std::string& dev_env0,
                    const std::string& dev_api,
                    const std::string& mid, simdjson::dom::element m) {
    bool reasons = false;
    if (m["reasoning"].get_bool().get(reasons)) return;   // 缺席或缺席

    // 努力 枚举: reasoning_options
    // 类型条目 "effort" 带有精确值列表。
    // 一个推理模型 无需 努力选项本身没有控制——被宣布推理属实,
    // 但努力设置 0将隐藏(无害,显示) -only) 切换 ,
    // 所以只记录一个值被命名时的集合。 带类型的 条目被跳
    // 过:一个差的字段不能花费文档的其余部分。
    int set = -1;
    bool has_toggle = false, has_none = false, has_effort = false;
    if (simdjson::dom::array ro; !m["reasoning_options"].get_array().get(ro)) {
        for (const simdjson::dom::element opt : ro) {
            std::string_view type;
            if (opt["type"].get_string().get(type)) continue;
            if (type == "effort") {
                has_effort = true;
                std::uint8_t bits = 0;
                if (simdjson::dom::array vals; !opt["values"].get_array().get(vals))
                    for (const simdjson::dom::element v : vals)
                        if (std::string_view s; !v.get_string().get(s)) {
                            bits |= bit_of(s);
                            if (s == "none") has_none = true;
                        }
                if (bits != 0) set = bits;
            } else if (type == "toggle") {
                has_toggle = true;
                if (set >= 0) continue;
                // 切换-only 推理( Kimi k2.5, 某些昆文):
                // on/off 且无阶梯. Model 成为了 二进制程序
                // 面具——梯子 渲染 高级( 诚实) on/off
                // 和任何要求的关卡 "on"。在同一列表中,
                // 一个努力选项获胜(k3 宣告) 两者 切换 和努力,以图谋划。
                set = static_cast<int>(
                    agentty::effort_bit(agentty::Effort::High));
            }
        }
    }
    // 怎样 "off" 的操作列表中,没有 "none" 且无 切换
    // 表示模型总是原因(o3, gpt) : -oss : 其最低
    // 层是下层。一个根本没有选项的推理模型是未知的,
    // 而不是 "impossible".
    using agentty::ReasoningOff;
    const ReasoningOff off =
        !reasons    ? ReasoningOff::Omit
        : has_none  ? ReasoningOff::EffortNone
        : has_toggle ? ReasoningOff::Toggle
        : has_effort ? ReasoningOff::Impossible
                     : ReasoningOff::Unknown;

    // 这些记录的范围:agentty的OWN提供方id,
    // 当models.dev条目的身份解决为注册表行时(确切的id ~ 主
    // env var → API主机,见capkey::resolve_dev_provider),
    // 否则,惰性的"dev:<id>"命名空间agentty的
    // 搜索永远无法与——未知集合器的事实得到保存但无害.
    const std::string scope = capkey::dev_scope(dev_provider, dev_env0,
                                                dev_api);

    // 范围记录:全中 和 ——供供应商使用 -prefixed 键( E)
    // "mistralai/mistral-medium-3-5" () ——在 末尾 ,
    // 自此 agentty 范围化的搜索使用 传输 模式 ID , 它可能忽略了供应商路径。
    // 设置中的卡顶正常化 归并 拼写 变体.
    // 上下文窗口,从 `limit.context`。
    // 与上述事实相同的范围界定:范围记录具有权威性,共享的赤色密钥是
    // 合并 -or-poison 以正整数——a null/absent/0
    // 限制为 "not declared" 并且记录0 将无法
    // 区分 与一个无所事事的宣布。
    int dev_ctx = 0;
    // 已签名或未签名的整数、 正和英寸范围。浮点、 字符串或 出值 -of-range
    // 值为 "not declared"。
    if (std::int64_t v = 0; !m["limit"]["context"].get_int64().get(v)) {
        if (v > 0 && v <= std::numeric_limits<int>::max())
            dev_ctx = static_cast<int>(v);
    }
    int dev_out = 0;
    if (std::int64_t v = 0; !m["limit"]["output"].get_int64().get(v)) {
        if (v > 0 && v <= std::numeric_limits<int>::max())
            dev_out = static_cast<int>(v);
    }

    out.set_reasoning(scope + "/" + mid, reasons);
    out.set_reasoning_off(scope + "/" + mid, off);
    if (dev_ctx > 0) out.set_context_window(scope + "/" + mid, dev_ctx);
    if (dev_out > 0) out.set_max_output(scope + "/" + mid, dev_out);
    if (set >= 0)
        out.set_effort_set(scope + "/" + mid, static_cast<std::uint8_t>(set));
    // CUSTOM-HOST 接口边界 : 将条目下的声明索引
    // API ORIGIN 本地主播/风俗习惯 -host 目录
    // 其 人 名 URL 匹配者可以要求图( 提供者)。
    // -scoped 键上永远无法到达自定义主机,其提供者 ID IS。
    // 提供方- 范围写入会保留预设的主通道。
    if (dev_ctx > 0 && !dev_api.empty())
        out.merge_endpoint_context_window(dev_api, mid, dev_ctx);
    const std::string tail = capkey::norm_tail(mid);
    if (tail != capkey::norm_model(mid)) {
        out.set_reasoning(scope + "/" + tail, reasons);
        out.set_reasoning_off(scope + "/" + tail, off);
        if (dev_ctx > 0) out.set_context_window(scope + "/" + tail, dev_ctx);
        if (dev_out > 0) out.set_max_output(scope + "/" + tail, dev_out);
        if (set >= 0)
            out.set_effort_set(scope + "/" + tail, static_cast<std::uint8_t>(set));
    }

    // 赤色密钥 : SHARED 在每个提供者之间合并 -or-poison 仅(同意)
    // 保留 不同意 ) 否 -info; RULE 3 进 capkey.hpp).
    if (!tail.empty()) {
        out.merge_reasoning(tail, reasons);
        out.merge_reasoning_off(tail, off);
        if (dev_ctx > 0) out.merge_context_window(tail, dev_ctx);
        if (dev_out > 0) out.merge_max_output(tail, dev_out);
        if (set >= 0)
            out.merge_effort_set(tail, static_cast<std::uint8_t>(set));
    }
}

// 解析 满额 api.json 文档进入 `batch` ; 已注册的返回模式。
// 打电话者公布过一次,所以读者从不看到半个文件,
// 而且 加载 是线性,不是四极。
//
// simdjson ,而不是nlohmann: 建造5+ MB 文档为
// std::map 源码树 剩下的启动项目大部分都完成了 CPU。
// 每个字段按类型和奇数读取 带类型的 跳过。 nlohmann 向这些上
// 扔(一个数字选项) "type" 或提供者 "api" 和捕获 丢弃 整个 文档,
// 所以一个错误 上游 进入时默默地花费了每个模型。
int parse_document(std::string_view body, CatalogBatch& batch) {
    simdjson::dom::parser parser;
    const simdjson::padded_string padded{body};
    simdjson::dom::object root;
    if (const auto err = parser.parse(padded).get_object().get(root)) {
        util::dbglog("modelsdev.parse", simdjson::error_message(err));
        return 0;
    }
    int n = 0;
    for (const auto provider : root) {
        simdjson::dom::object models;
        if (provider.value["models"].get_object().get(models)) continue;
        // 身份锚 capkey::dev_scope (见A/C.5/61/L.48,第16段)
        // capkey.hpp : 条目 首次 环境 var + API URL ,在文本缺失或未文本时为空。
        std::string env0;
        if (std::string_view s; !provider.value["env"].at(0).get_string().get(s)) env0 = s;
        std::string api;
        if (std::string_view s; !provider.value["api"].get_string().get(s)) api = s;
        const std::string pid{provider.key};
        for (const auto model : models) {
            register_model(batch, pid, env0, api, std::string{model.key}, model.value);
            ++n;
        }
    }
    return n;
}

int load_document(std::string_view body) {
    CatalogBatch batch;
    const int n = parse_document(body, batch);
    if (n > 0) batch.commit();
    return n;
}

bool cache_fresh() {
    std::error_code ec;
    const auto mtime = fs::last_write_time(cache_path(), ec);
    if (ec) return false;
    const auto age = fs::file_time_type::clock::now() - mtime;
    return age < kMaxAge;
}

} // 命名空间结束。

int parse_into(std::string_view body, CatalogBatch& out) {
    return parse_document(body, out);
}

void load_cached() {
    std::ifstream ifs(cache_path(), std::ios::binary);
    if (!ifs) return;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    load_document(ss.str());
}

int refresh() {
    // 新鲜的 缓存 已经 已加载 (start_background_refresh 加
    // 载 首先是? 解析 5 与第5条相同 MB 两次都是纯粹的浪费。
    if (cache_fresh()) return 0;
    http::Request req;
    req.method = http::HttpMethod::Get;
    req.host   = "models.dev";
    req.port   = 443;
    req.path   = "/api.json";
    req.headers = {
        {"accept", "application/json"},
        {"user-agent", ::agentty::user_agent()},
    };
    req.max_body_bytes = kMaxBody;
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(8'000);
    tos.total   = std::chrono::milliseconds(60'000);
    auto resp = http::default_client(::agentty::IoAccess::grant()).send(req, tos);
    if (!resp || resp->status != 200 || resp->body.empty())
        return 0;   // 网络下移/ 移动: 缓存的 加载 已经站立
    const int n = load_document(resp->body);
    if (n > 0) {
        // A 缓存 原子 所以读者从不看半个,没有Fsync.
        (void)persistence::write_atomic(::agentty::IoAccess::grant(), cache_path(), resp->body,
                                        {.durability = persistence::Durability::Atomic});
    }
    return n;
}

// ── 背景刷新( 模式与 blob_gc.cpp 相同) ────────────────
namespace {
// 这个子系统 自身 池: join_background_refresh
// () 允 诺 ITS 获取完成, 共享 池 无法一次加入一个工作。
// 见 blob_gc.cpp 两者故意保持相同。
//
// 替换 a 互斥量 +条件变量 + 生 工作线程 + 停止
// 键 标志 关闭的电源 工作线程 每当接获还在飞行中,
// 这恰恰是一个网络 调用 左转与静态相撞 CRT 正在毁灭。
// pool::shutdown 要求它停止,等待在 无限的恩典,
// 然后安全地放弃。
maya::pool& refresh_pool() {
    static maya::pool p{/*max_workers=*/1};   // 一次刷新,永远
    return p;
}
constexpr auto kStartDelay = std::chrono::seconds(5);
}  // 命名空间结束。

void start_background_refresh(bool no_net) {
    // 注册加入时的背景工作即将存在,而不是依赖 main() 来知道
    // 这个子系统是线性的。 main() 手的手,手的手的手 手的
    // 双手 -written 拆掉的列表是漏掉的: settings_cache
    // 和无人叫的合唱团一起被运走,还有它的 工作线程 正在
    // 连接到函数中 -local 静态,直到销毁器退出时终止
    // 进程。
    //
    // refresh_pool ()是函数 -local 静态,
    // 它活在处理退出,所以 回调 永远不能超过目标 需要没有 cancel()。
    // 整个开始每个过程一次。
    static std::once_flag once;
    maya::once(once, [no_net] {
        util::teardown::on_shutdown("modelsdev.refresh",
                                    [](const maya::unlocked& cap) { join_background_refresh(cap); });

    // 隔离 : refresh() 是一个网络获取,所以它可能永远不返回,
    // 并且不能占用共享 工作线程.
    refresh_pool().post_isolated([](const maya::unlocked& cap, std::stop_token st, bool no_net) {
        // 一个程序,一个从不拨号的 取消 频道——
        // 代号——而不是第二站 标志 通知。 通知。
        if (maya::delay_for(cap, st, kStartDelay)) return;
        try {
            load_cached();
            // 调 -check : 数据 缓存 加载 是一个便宜的一半,
            // 并且可能已经在运行时要求关闭。 没有开始拨号的点,
            // 我们即将放弃。
            if (!no_net && !st.stop_requested()) {
                try { (void)refresh(); } catch (...) {}
            }
        } catch (const std::exception& e) {
            util::dbglog("modelsdev.refresh", e.what());
        } catch (...) {}
    }, no_net);
    });
}

void join_background_refresh(const maya::unlocked& cap) noexcept {
    (void)refresh_pool().shutdown(cap);   // 请求停止，在宽限内等待
}

} // namespace agentty::modelsdev
