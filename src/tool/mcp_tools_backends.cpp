// mcp_tools_backends.cpp - 电话 agentty-side HostServices
// 后端为 mcp-cpp 工具集的主机 -coupled SHELLS 分派 进入。
//
//   mcp-cpp 拥有协议表面 remember/forget/wipe ,
//   待办,技能, search_docs ,和任务——姓名, 模式定义
//   参数 解析 ,范围验证, dedup/dry-run 通讯、格式化。
//   但 DATA 编辑 WORK 用来保护这些工具 agentty :
//   数据 JSONL 内存 库,代理 -Skills 引擎,该引擎 RAG 管道,
//   子代理环路。 此文件是反转 -of-control 接口边界 :每个后端
//   是一个小类,从中衍生出一个 `mcp::tools::*` 接口和
//   授权到 agentty 现有的子系统,带有 EXACT arg_后端映射、
//   范围词汇和输出格式化本地工具 正文 曾经是。
//
//   由 + 建造 build_mcp_tool_defs() (mcp_tools_bridge.cpp).

#include "agentty/util/user_root.hpp"
#include "agentty/tool/mcp_tools_backends.hpp"
#include "agentty/runtime/app/wire_audit.hpp"
#include "agentty/util/home_dir.hpp"

#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // KAgents Layout—— 单一声明
#include "agentty/tool/memory_store.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/tool/registry.hpp"   // CallContext
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/tool_plan.hpp"
#include "agentty/tool/host_call.hpp"
#include "agentty/tool/tool.hpp"       // tool::DynamicDispatch, ToolUse ,消息...
#include "agentty/tool/util/partial_json.hpp"   // 截流工具的回收参数JSON
#include "agentty/tool/util/utf8.hpp"           // progress_tail
#include "agentty/runtime/app/update/stream_args.hpp"  // canonify_tool_args (`cmd`→`command`)

#include "agentty/provider/anthropic/provider.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/prompt_policy.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/wire.hpp"

#include "agentty/rag/rag_adapter.hpp"
#include "agentty/rag/embed_secret.hpp"
#include <thread>
#include "agentty/io/persistence.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/store/store.hpp"

#include "agentty/mcp/client.hpp"   // mcp_resources / mcp_read_resource 接口边界 / (中文(简体) ).
#include "agentty/util/background.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/logx.hpp"

#include <mcp/tools/host.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentty::tools {

namespace {

namespace mt  = ::mcp::tools;
namespace fs  = std::filesystem;
using json    = nlohmann::json;

// 机器 -readable 事件 数据流 无头运行,
// 启用于 `agentty run --events jsonl` (以及老年人)
// AGENTTY_TRACE_TOOLS =1,现在选择相同的 数据流).
//
// 原因 此 EXISTS 基准操纵驱动
// `agentty run` 需要知道哪些工具被调用,
// 它们是否有效。 -visible 信号在渲染的活动视图
// 中是Unicode齿轮格格,而且格格法只在 SUBAGENT 路径,
// 从不通过平原 `run`。至少一个外部的绳子被咬了,
// 什么都没有,并记录了它的工具 -call 列为
// "NA" 数据存在( tool.exec ,有时间),
// 但只在日志文件中,一个沙盒跑在撕裂时丢掉.
//
// 这么说: JSON 每行对象, 开 STDERR . stderr 因为
// run/acp/ 微缩 -serve 保持Studout为报告本身,
// 和一个把两者合并的绳子 数据流 两者都一样
//
//   {"ev":"tool","seq":3,"tool":"read","ms":12,"ok" 是真的,我对这个很感兴趣 {。
//    cHFFFFFF}{\3cH111111}{\4cH111111}
//   True, "args_sha":"a3f1c09d"} {"ev":"tool","seq":4,"tool":"edit",
//    "ms":4,"ok" : 假话, "err":"invalid_args: no such file","args_sha":"77b2e410"}
//
// args_sha ,而不是参数:重复 -detector 需要
// 知道两个 调用 曾经 IDENTICAL 而不是他们包含的
// Tool 参数包含路径、 env 值和命令行, 以及想要此运行的调
// 用 agentty 沙盒是用来保存数据。散列回答问题而不漏出,
// 不需要 AGENTTY_LOG_BODIES-style 大门
enum class EventFormat { Off, Jsonl };

[[nodiscard]] EventFormat event_format() {
    static const EventFormat fmt = [] {
        // --events jsonl胜负; AGENTTY_TRACE_TOOLS 是一个老的拼写。
        if (const char* e = std::getenv("AGENTTY_EVENTS");
            e && e[0] && std::string_view{e} == "jsonl")
            return EventFormat::Jsonl;
        const char* v = std::getenv("AGENTTY_TRACE_TOOLS");
        const bool on = v && v[0] && v[0] != '0' && v[0] != 'f' && v[0] != 'F'
                          && v[0] != 'n' && v[0] != 'N';
        return on ? EventFormat::Jsonl : EventFormat::Off;
    }();
    return fmt;
}

// FNV -1a 过争论 JSON。短、
// 稳定地跨越各条路线和进程,不能被反转到它所代表的论据中。
[[nodiscard]] std::string args_digest(std::string_view args) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : args) { h ^= c; h *= 16777619u; }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string{buf};
}

// 一个 事件 线条。永不丢弃:诊断 数据流 一定
// 无法控制它所描述的运行
void emit_event(nlohmann::json j) noexcept {
    if (event_format() == EventFormat::Off) return;
    try {
        static std::atomic<unsigned long long> seq{0};
        j["seq"] = seq.fetch_add(1, std::memory_order_relaxed);
        const std::string line = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::fprintf(stderr, "%s\n", line.c_str());
    } catch (...) {
        // 内存格式化诊断线中:放下线.
    }
}

void emit_tool_event(std::string_view tool, long long ms, bool ok,
                     std::string_view err, std::string_view args) noexcept {
    if (event_format() == EventFormat::Off) return;
    try {
        nlohmann::json j{
            {"ev",   "tool"},
            {"tool", tool},
            {"ms",   ms},
            {"ok",   ok},
            {"args_sha", args_digest(args)},
        };
        if (!ok && !err.empty()) j["err"] = err;
        emit_event(std::move(j));
    } catch (...) {}
}

// `agentty run --events jsonl`的Model输出:推理
// 和答题文本作为数据流,按行批量进行,这样一盏吊带就不会被1-char 事件淹没.
// 只有顶端的-level运行会释放这些,而不是巢状的子剂.
class TextEvents {
public:
    explicit TextEvents(bool on, int turn) : on_(on), turn_(turn) {}
    TextEvents(const TextEvents&) = delete;
    TextEvents& operator=(const TextEvents&) = delete;
    ~TextEvents() { flush(); }
    void reasoning(std::string_view s) { add(Kind::Reasoning, s); }
    void text(std::string_view s)      { add(Kind::Text, s); }
    void flush() noexcept {
        if (!on_ || buf_.empty()) return;
        try {
            emit_event({{"ev", kind_ == Kind::Reasoning ? "reasoning" : "text"},
                        {"turn", turn_}, {"text", buf_}});
        } catch (...) {}
        buf_.clear();
    }
private:
    enum class Kind { Reasoning, Text };
    void add(Kind k, std::string_view s) {
        if (!on_ || s.empty()) return;
        if (k != kind_) { flush(); kind_ = k; }
        buf_ += s;
        // 段落中的一行或~2 KiB,以先到者为准。
        if (buf_.size() >= 2048 || buf_.find('\n') != std::string::npos) flush();
    }
    bool on_;
    int turn_;
    Kind kind_ = Kind::Text;
    std::string buf_;
};

// ── MemoryStore ────────────────────────────────────────────────────────
//   背部记着/ 忘了/ 擦。 外壳拥有 模式定义 + dedup/pin/
//   tag/supersede 表面; 此图将其请求映射到 agentty::tools::
//   内存 自由函数。范围词汇是 [ "project","user"
//   )所以 shell 默认为投影(更安全的默认,匹配本地记忆工具)
//   并接受 "user" 用于交叉 -project 事实。
// 平行结果结构的漂流保护在下文的append()中架设。
// agentty::tools::memory::AppendResult和mcp::tools::MemoryAppendResult按相同的顺序搭载相同的字段;
// 桥面逐一复制。如果字段的大小是added/removed,
// 则在编译时间进行这一次旅行——提醒更新BOTH
// structs和绘图。 (下面的指定-init
// 绘图分别守护renames/removals.)
static_assert(sizeof(memory::AppendResult) == sizeof(mt::MemoryAppendResult),
              "AppendResult / MemoryAppendResult drifted — update both structs "
              "and the field mapping in AgenttyMemoryStore::append()");

// 锁 顺序 : mcp-cpp's MemoryStore 接口 手握这些方法 否 调用 ,
// 所以工具 工作线程 能力不能用上下文来显示它们。 -states 它在这个边界上
// ( `mem_root`, tests/lint/allowlist.txt):
// 只能由 mcp-cpp's 内存 工具从工具中 正文 工具上
// : 工作线程 ,则不 agentty 锁 他们只拿 内存 存储
// 行( 排在每片叶下方) 及其文件 IO.
template <class F>
auto mem_root(F&& f) -> decltype(maya::thread_root(std::forward<F>(f))) {
    return maya::thread_root(std::forward<F>(f));
}

class AgenttyMemoryStore final : public mt::MemoryStore {
public:
    // scopes() [0] 是 DEFAULT 范围 : 当模型省略时,
    // 贝壳使用 `scope` 我们只做广告 "project"
    // (并只让它成为默认) 当项目存储是实际 WRITABLE 给,
    // 在A 工作区 其根为 "/" 或无法写, path_for (
    // 项目)是空的,项目附件将失败——所以提供 "project"
    // 默认让模型成为了第一个 remember() 调用 每次都失败,
    // 强迫 重试 范围= "user"。从词汇中删除无法使用的范围,
    // 则默认值 "user" 总是成功 无需 默默地向用户范围
    // 宣传明确的项目事实( append() 仍然拒绝一个无法
    // 明确获取的 -project 写——无交叉—— 工作区
    // 内存 流血) (英语:
    std::vector<std::string> scopes() const override {
        if (!memory::path_for(memory::Scope::Project).empty())
            return {"project", "user"};   // 可写出的项目 − 正常命令
        return {"user"};                   // 无法执行的工程 – 用户默认
    }

    mt::MemoryAppendResult append(const mt::MemoryAppendRequest& req) override {
        auto scope = memory::parse_scope(req.scope);
        if (!scope)
            return mt::MemoryAppendResult{.error = "unknown scope '" + req.scope + "'"};

        // 智能范围:  shell 默认省略 `scope` 改为
        // scopes()[0] ("project" 写到可以写的时候,
        // 所以一个事实, 很明显, HUMAN — "my name is…",
        // "I use fish" ——在项目中默认土地,然后流血成每
        // 块土地 仓库. suggest_scope (a) 阅读事实案文;
        // CONFIDENT 用户对项目发出的信号,我们写着,我们纠正,
        // 并在注释中说。我们只用这种方式重新定向项目。 (绝不相反,
        // 而且项目真正是为了一个 代码库 风险是一 -directional
        // 个人事实,逃入共享项目 内存 是伤害; 用户中的项目事实
        // 内存 仅仅是狭义的。 UNAVAILABLE 用户范围
        // 被单独保留(附录自己的守护手柄可达性).
        std::string smart_note;
        if (*scope == memory::Scope::Project) {
            const auto hint = memory::suggest_scope(req.text);
            if (hint.confident() && hint.scope == memory::Scope::User
                && !memory::path_for(memory::Scope::User).empty()) {
                *scope = memory::Scope::User;
                smart_note = "scope→user (" + hint.reason + ")";
            }
        }

        memory::AppendOptions opts;
        opts.pinned        = req.pinned;
        opts.tags          = req.tags;
        opts.supersedes_id = req.supersedes_id;
        const auto r = mem_root([&](const maya::unlocked& cap) {
            return memory::append(cap, *scope, req.text, opts);
        });

        // 将 agentty 结果连接到 mcp 结果上。
        // 指定指定 的 renamed/removed 字段是 编译 错误,
        // 而不是默读; 高于 static_assert 的 渔获量漂移。
        std::string note = r.note;
        if (!smart_note.empty())
            note = note.empty() ? smart_note : (smart_note + "; " + note);
        return mt::MemoryAppendResult{
            .id      = r.id,
            .error   = r.error,
            .note    = note,
            .rolled  = r.rolled,
            .deduped = r.deduped,
        };
    }

    std::size_t forget_by_id(const std::string& id) override {
        return mem_root([&](const maya::unlocked& cap) { return memory::forget_by_id(cap, id); });
    }
    std::size_t forget_by_substring(const std::string& needle) override {
        return mem_root([&](const maya::unlocked& cap) { return memory::forget_by_substring(cap, needle); });
    }
    std::vector<mt::MemoryRecord> preview_forget(const std::string& needle) override {
        std::vector<mt::MemoryRecord> out;
        for (const auto& r : mem_root([&](const maya::unlocked& cap) { return memory::preview_forget_by_substring(cap, needle); })) {
            mt::MemoryRecord m;
            m.id     = r.id;
            m.text   = r.text;
            m.scope  = std::string{memory::to_string(r.scope)};
            m.pinned = r.pinned;
            m.tags   = r.tags;
            m.ts     = r.ts;
            m.hits   = r.hits;
            out.push_back(std::move(m));
        }
        return out;
    }

    std::optional<std::size_t> preview_wipe(const std::string& scope) override {
        auto s = memory::parse_scope(scope);
        if (!s || memory::path_for(*s).empty()) return std::nullopt;
        return mem_root([&](const maya::unlocked& cap) { return memory::load_all(cap, *s).size(); });
    }

    std::optional<std::size_t> wipe(const std::string& scope) override {
        auto s = memory::parse_scope(scope);
        if (!s) return std::nullopt;
        return mem_root([&](const maya::unlocked& cap) { return memory::wipe(cap, *s); });
    }
};

// 电话 agentty CallContext 后面的a 调用 (见A/C.5/61/L.48,第16段) HostCall).
// A 调用 没能通过 DynamicDispatch (单位:千美元) MCP 客户端通过 mcp -serve
// 无:空的。
const CallContext& context_of(const mt::Call& call) {
    static const CallContext kNone{};
    const auto& h = host_call(call.host);
    return h.ctx ? *h.ctx : kNone;
}

// ── SkillResolver ──────────────────────────────────────────────────────
//   返回技能工具。 贝壳返回任意字符串 load() 产量逐字,
//   所以 load() 返回“%s” 完整 激活 载荷 (正文 包起来
//   <skill_content> ,绝对技能目录,
//   <skill_resources> ——与原生工具完全相同——并适用
//   相同的re -activation 调值( 第5节: 不重写) -inject
//   a 正文 上下文中已存在)。在未知名称上,它留下了 正文 空且填充
//   `err` 与现有 -skills 恢复提示。
class AgenttySkillResolver final : public mt::SkillResolver {
public:
    std::optional<std::string> load(const mt::Call& call, const std::string& name,
                                    std::string& err) override {
        const Io io = IoAccess::grant();   // 工具 正文 工具上 工作线程
        const auto s = skills::find(io, name);
        if (!s) {
            std::ostringstream avail;
            bool first = true;
            for (const auto& sk : skills::all(io)) {
                avail << (first ? "" : ", ") << sk.name;
                first = false;
            }
            err = "no skill named '" + name + "'";
            if (!first) err += " — available: " + avail.str();
            else        err += " — no skills are installed in this workspace";
            return std::nullopt;
        }
        // "Already active" 是从集合中 分派 由可见
        // 记录产生( skills::active::Scope ,
        // 安装于 run_tool - 运行在... 工作线程 并且没有
        // 线程 衍生的,不记得了:a 正文 / compact 汇总的离去不活动,
        // 所以模型再次得到它而不是被指向一个 tool_result
        // 已经不在了
        if (context_of(call).skill_active(s->name)) {
            return "Skill '" + s->name + "' is already active in this "
                   "session — its instructions are in an earlier tool_result. "
                   "Refer to that instead of re-loading.";
        }
        return skills::activation_payload(io, *s);
    }
};

// ── DocRetriever ───────────────────────────────────────────────
//   背对面 search_docs 运行 agentty 满了 RAG 管道( rag-cpp
//   )返回平面通道。漏斗,如实际的线条。 src/rag/adapter.cpp:
//
//     来源:docs文件夹+技能+ 内存 ,与 MCP 选择资源 -in
//     获取 : BM25 加上探测 Ollama 加权密集检索 RRF 组合可
//     选 : PRF, GraphRAG, CRAG, HyDE 多个 -query
//     它们是明确模式的重新排序:确定特性的重新排序+ MMR + 相邻
//     -hit 缝合压缩:查询 -focused 产出总预算下的跨度
//     保持不变:经验证的表 + 增量 docs/code 刷新( T)
//
//   电话 `mode` 字符串带有丰富的来源( 根路、 模式、
//   重新排序、 + N) 变体 ,自信),所以当壳体时,
//   没有信号丢失 渲染 结果是。
// 地图a 持续 store::RagConfig 进入现场 rag::Config ,
// 保留以下的田间 选择面板 从未从给定的基数( env) 中曝光( docs root)
// -derived ) 调和.
static ::agentty::rag::Config rag_config_from_settings(
        const store::RagConfig& s, ::agentty::rag::Config base) {
    base.skills          = s.skills;
    base.memory          = s.memory;
    base.mcp_resources   = s.mcp_resources;
    base.contextual      = s.contextual;
    base.dedup           = s.dedup;
    base.mmr             = s.mmr;
    base.stitch          = s.stitch;
    base.autocut         = s.autocut;
    base.mmr_lambda          = s.mmr_lambda;
    base.dedup_threshold     = s.dedup_threshold;
    base.autocut_sensitivity = s.autocut_sensitivity;
    base.dense_weight        = s.dense_weight;
    base.bm25_weight         = s.bm25_weight;
    base.prf             = s.prf;
    base.corrective      = s.corrective;
    base.graph           = s.graph;
    base.expand          = s.expand;
    base.hyde            = s.hyde;
    base.fusion          = s.fusion;
    base.adaptive_fusion = s.adaptive_fusion;
    base.persist         = s.persist;
    base.learn           = s.learn;
    base.trace           = s.trace;
    base.proactive          = s.proactive;
    base.proactive_min_conf = s.proactive_min_conf;
    base.proactive_bytes    = s.proactive_bytes;

    // 嵌入式。空后端 ID 表示用户从未打开过 面板 ,
    // 所以这个洞 -derived 嵌入的配置立体未受破坏。
    if (!s.embed_backend.empty()) {
        namespace eb = ::agentty::rag::embed;
        auto& e = base.embed;
        e.backend = eb::backend_from_id(s.embed_backend);
        if (!s.embed_model.empty())      e.model          = s.embed_model;
        if (!s.embed_host.empty())       e.host           = s.embed_host;
        if (s.embed_port != 0)           e.port           = s.embed_port;
        e.tls            = s.embed_tls;
        e.path           = s.embed_path;
        e.model_path     = s.embed_model_path;
        e.tokenizer_path = s.embed_tokenizer_path;
        e.dim            = s.embed_dim;
        // 电话 凭据 永远不要圆 -trips 结束 settings.json
        // 是从大地取来的, keystore/sealed 存储按端点键。
        if (eb::needs_api_key(e.backend)) {
            if (auto key = eb::load_key(eb::endpoint_key(e)); !key.empty())
                e.api_key = std::move(key);
        }
    }
    return base;
}

// 单一进程 -wide 检索器返回两个 search_docs 工具与
// 主动预告 -turn 函数 -local 静态 QQ 建于首次使用(
// 在应用程序设置了 cwd / env 之后) , 并在退出时被销毁。
// 在第一次使用时, 我们 归并 任何坚持 RAG 设置( 设置 RAG 选择
// 面板 所以用户的 已保存 配置胜过包围 -derived 默认;
// 如果用户从未碰过 选择面板 (构图=假) 洞穴配置立面.
static ::agentty::rag::Retriever& shared_retriever() {
    static ::agentty::rag::Retriever r;
    // 归并 持续 RAG 选择面板 设置精确一次,在第一次使用时。
    static const bool configured_once = [] {
        try {
            // 第一次使用总是在工具或检索器工作线程上.
            auto s = persistence::load_settings(IoAccess::grant());
            if (s.rag.configured)
                r.apply_config(rag_config_from_settings(s.rag, r.snapshot_config()));
        } catch (...) { /* 最佳-effort; 封装配置符 */ }
        return true;
    }();
    (void)configured_once;
    return r;
}

} // 命名空间( 匿名)

// 现场直播 -apply a RagConfig 输入进程 -wide 寻者.
// SYNCHRONOUS :它需要回收者 互斥量 ,可重选 -probe 嵌入
// 器(网络拨号)和冷气 -rebuilds 引擎,永远不要 调用 是从一个 状态更新函数
// - 电话 TUI 只能通过它 cmd::apply_rag_settings ,
// 它运行作为一个 副作用 内核隔离 池 ,所以更新要保持纯度,内核拥有 线程.
void rag_apply_settings_now(const store::RagConfig& s) {
    auto& r = shared_retriever();
    r.apply_config(rag_config_from_settings(s, r.snapshot_config()));
}

RagEmbedStatus rag_embed_status(Io) {
    RagEmbedStatus out;
    try {
        const auto s = shared_retriever().embed_status();
        using S = ::agentty::rag::Retriever::EmbedStatus::State;
        out.state = s.state == S::Ready       ? RagEmbedStatus::State::Ready
                  : s.state == S::Unavailable ? RagEmbedStatus::State::Unavailable
                                              : RagEmbedStatus::State::Unprobed;
        out.dim        = s.dim;
        out.latency_ms = s.latency_ms;
        out.reason     = s.reason;
        out.describe   = s.describe;
    } catch (...) {
        out.state  = RagEmbedStatus::State::Unavailable;
        out.reason = "retriever unavailable";
    }
    return out;
}

RagProbeOutcome rag_probe_embedder(const store::RagConfig& s, const std::string& api_key) {
    namespace eb = ::agentty::rag::embed;
    RagProbeOutcome out;
    try {
        // 构建候选配置 无需 触碰活检索器:
        // 用户正在测试他们没有执行的配置,
        // 失败的探测器不得降低他们目前工作的检索.
        ::agentty::rag::Config base;
        eb::apply_env(IoAccess::grant(), base.embed);
        auto cfg = rag_config_from_settings(s, base);
        if (!api_key.empty()) cfg.embed.api_key = api_key;
        // 永远不要相信一个载体 -over 维度:测量它就是点。
        cfg.embed.dim = 0;

        auto r = eb::probe(cfg.embed);
        if (const auto* ok = std::get_if<eb::ProbeOk>(&r)) {
            out.ok         = true;
            out.dim        = ok->dim;
            out.latency_ms = ok->latency_ms;
        } else {
            out.error = std::get<eb::ProbeErr>(r).why;
        }
    } catch (const std::exception& e) {
        out.error = e.what();
    } catch (...) {
        out.error = "unknown error while probing";
    }
    return out;
}

// 提示词 撕下回收人的背景 温暖。 已预热的工作线程 仅在函数时停止
// -local `static Retriever` 被销毁于进程
// 退出(主返回后),该退出被封存 ^C 4 - 10 s 时, 1 英寸
// -flight 嵌入式通行证完成。 提前拨打 main() 撕裂的声浪。
void rag_shutdown() {
    try { shared_retriever().shutdown(); } catch (...) { /* 最佳-effort */ }
}


namespace {

class AgenttyDocRetriever final : public mt::DocRetriever {
public:
    std::vector<mt::DocPassage>
    retrieve(const mt::DocQuery& q, std::string& mode, std::string& err) override {
        return run_(q, mode, err);
    }

private:
    static std::vector<mt::DocPassage>
    run_(const mt::DocQuery& q, std::string& mode, std::string& err) {
        std::vector<mt::DocPassage> out;
        auto res = shared_retriever().retrieve(q.query, q.k);
        if (!res.error.empty()) { err = res.error; return out; }
        mode = res.mode;
        // 匹配的成功搜索 无内容 并不是失败,而是空洞的结果,
        // "corpus is irrelevant" 无法区分
        // "index empty / Ollama down / no docs configured".（索引为空、Ollama 不可用，或未配置文档。）
        // 归并 进入 MODE 标签( 其外壳) 渲染 而不是
        // 错误) model/user 错误是空的: 真正
        // 的失败已经通过 res.error 上节。
        if (res.passages.empty()) {
            mode = res.mode.empty()
                 ? "no matches (no docs indexed — set AGENTTY_DOCS_DIR, or none "
                   "of your skills/memory matched)"
                 : ("no matches (" + res.mode + ")");
            return out;
        }
        out.reserve(res.passages.size());
        for (auto& p : res.passages) {
            mt::DocPassage d;
            d.source     = std::move(p.source);
            d.path       = std::move(p.path);
            d.line_start = p.line_start;
            d.line_end   = p.line_end;
            d.score      = p.score;
            d.text       = std::move(p.text);
            out.push_back(std::move(d));
        }
        return out;
    }
};


// ── CodeRetriever ──────────────────────────────────────────
//   定义 -aware 源块; BM25 总是可以使用,
//   并且有界限 Ollama 探测器可以进行密集检索。 -file
//   显示检测漂移, 且小编辑集更新仅更改文件。
class AgenttyCodeRetriever final : public mt::DocRetriever {
public:
    // 机会性查询(结构零) -hit 领队,完毕 -budget 命令)
    // 绝不能触发冷码 -index 建楼——只明确 search_code
    // 调用 支付。温暖的指数生活在 内存 或坚持在磁盘上。
    bool warm() const override { return shared_retriever().code_warm(); }

    std::vector<mt::DocPassage>
    retrieve(const mt::DocQuery& q, std::string& mode, std::string& err) override {
        std::vector<mt::DocPassage> out;
        auto res = shared_retriever().retrieve_code(q.query, q.k);
        if (!res.error.empty()) { err = res.error; return out; }
        mode = res.mode;
        out.reserve(res.passages.size());
        for (auto& p : res.passages) {
            mt::DocPassage d;
            d.source     = std::move(p.source);
            d.path       = std::move(p.path);
            d.line_start = p.line_start;
            d.line_end   = p.line_end;
            d.score      = p.score;
            d.text       = std::move(p.text);
            out.push_back(std::move(d));
        }
        return out;
    }
};

// ── SubagentRunner ─────────────────────────────────────────────────────
//   返回任务工具。 整个 孤立代理循环——代理 -type
//   角色 提示词 ,每笔 -completion 数据流 重新组装,
//   本地工具 分派 ,通过泵入母卡的活动反馈 progress::emit
//   报告收成——住在这,从本地任务工具中逐字取消。
//   模式定义 + 可用门; run() 做工作,我们的工作。
//
//   本地任务工具的 `display_description` 参数为 UI-only
//   留在壳里 模式定义; run() 永远不需要它。

// 特务人物来自哪里 任务卡的出处如此浮现 仓库
// -injected 代理从不隐形(透明,不是大门).
} // 命名空间( 匿名)
enum class AgentOrigin : std::uint8_t { Builtin, User, Project };

struct AgentType {
    std::string_view              name;
    bool                          read_only;
    std::string_view              role;
    std::vector<std::string_view> allow;   // 全部空( 减去任务)
    smart::ModelRole              model_role = smart::ModelRole::Utility;
    // 这个人从哪里来,建起来的 -ins 和用户的 自身
    // ~/.agentty/agents 信任的选项 -by-authorship;
    // a PROJECT-scoped 探员骑着马冲进来了 工作区 (克隆人可以起
    // 其作用) 提示词 ),所以任务卡表面 "project agent" ——不是块,
    // 只是这样一个被注入的人a就永远看不见。镜像插件 选择面板 瞄准镜徽章
    AgentOrigin                   origin = AgentOrigin::Builtin;
};

// ── 用户 -defined 代理人( 4) .agentty/agents/*.md) ─────────────────────
// 用户代理是一个标记下的文件,其 BODY 成为角色
// 提示词 和它的前题, 沙箱:
//
//     ---
//     说明:审查 SQL 移民是为了安全。 # 目录 工具:读取 grep
//     glob list_dir                     #
//     允许清单 (备选案文)改为 -only : 真实的 # 副作用 门( opt)
//     ---
//     你的角色是: 爱丽丝·卡西尔·卡西姆·卡西 MIGRATION REVIEWER 检查每一个迁移...
//
// 根( 项目先, 然后是用户; 先名胜出—— 镜像) commands/ 技能:
// .agentty/agents, .agents/agents,
// .claude/agents ,然后在~下设置相同的三个. -in 类型总是会战胜
// 一个同名用户代理(用户文件无法默默替换) `general`).
//
// 存储便条 : AgentType 字段为
// string_views 输入 `owned`。
// 一代人就是,我们不是可变 已发布; 子代理运行持有
// ResolvedAgent 使一代人能活着 永远无法释放 阅读的一幕
// 一代人终究会死去
struct UserAgentStore {
    std::vector<std::unique_ptr<AgentType>> types;   // 稳定地址
    std::deque<std::string>                 owned;   // 元素 -stable 辅助
    std::string                             sig = "\x01uninit";
};

} // namespace agentty::tools
// AgentType's string_views 字符串字节的点( 已建)
// -ins 或进入 UserAgentStore's `owned` 时间
// 是出版后没有写字的,而且只要商店存续,店里就会有跑步(例如: ResolvedAgent ),
// 所以观点永远没有超过他们 字节。这让一个康斯特 AgentType 冻死;
// jaal 无法看出来 因为原则上拒绝观点
MAYA_FROZEN(agentty::tools::AgentType);
namespace agentty::tools {
namespace {

// 漏掉的,所以在关闭的恩典上弃置的跑步,
// 仍然可以在静态的走动时读到它;世代本身被重新计算.
maya::published<const UserAgentStore>& user_agent_gen() {
    static auto* g = new maya::published<const UserAgentStore>;
    return *g;
}

// 前题子集 解析器 (与《公约》相同的宽大规则) commands/skills).
void parse_user_agent(const std::string& raw, const std::string& slug,
                      AgentOrigin origin, UserAgentStore& store) {
    auto trim = [](std::string_view v) -> std::string {
        std::size_t b = 0, e = v.size();
        while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(v[e-1]))) --e;
        return std::string{v.substr(b, e - b)};
    };

    std::string body = raw, tools_line;
    bool read_only = false;
    std::istringstream in(raw);
    std::string line;
    if (std::getline(in, line) && trim(line) == "---") {
        std::streampos body_start{-1};
        while (std::getline(in, line)) {
            if (trim(line) == "---") { body_start = in.tellg(); break; }
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = trim(line.substr(0, colon));
            std::string v = trim(line.substr(colon + 1));
            if      (k == "tools")     tools_line = v;
            else if (k == "read-only") read_only = (v == "true" || v == "1" || v == "yes");
            // 说明 extra_agent_types ' 通过任务呼叫
            // 者 -tool 说明; 不需要在 AgentType 自己来
        }
        if (body_start != std::streampos(-1))
            body = trim(raw.substr(static_cast<std::size_t>(body_start)));
    }
    body = trim(body);
    if (body.empty()) return;

    auto at = std::make_unique<AgentType>();
    store.owned.push_back(slug);
    at->name = store.owned.back();
    store.owned.push_back(std::move(body));
    at->role = store.owned.back();
    at->read_only = read_only;
    at->model_role = read_only ? smart::ModelRole::Utility
                               : smart::ModelRole::Implementation;
    at->origin = origin;
    // 空白空间 -split 工具 允许清单 ——每个存储的代币都拥有.
    std::istringstream ts(tools_line);
    std::string tok;
    while (ts >> tok) {
        store.owned.push_back(tok);
        at->allow.push_back(store.owned.back());
    }
    store.types.push_back(std::move(at));
}

// 扫描 6 根; 当 mtime 签名移动时发布新一代。
// 返回当前生成。
std::shared_ptr<const UserAgentStore> user_agents() {
    namespace fs = std::filesystem;
    std::string sig;
    auto scan_root = [&](const fs::path& root) {
        std::error_code ec;
        if (!fs::is_directory(root, ec) || ec) return;
        auto mt = fs::last_write_time(root, ec);
        if (!ec)
            sig += std::to_string(static_cast<long long>(
                       mt.time_since_epoch().count())) + ";";
        std::vector<fs::path> files;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end;
             it.increment(ec))
            if (it->path().extension() == ".md") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            std::error_code fec;
            auto fmt = fs::last_write_time(p, fec);
            if (!fec)
                sig += std::to_string(static_cast<long long>(
                           fmt.time_since_epoch().count())) + ";";
        }
    };
    auto home = agentty::util::home_dir_or_empty();
    // 根梯子从 scope::plan (乐库) -major 对话框 -minor 项目。
    // agentty ..代理 . . .. . 宣布 ~ 下的3个相同。 . 同样的命令
    // 手 -written 数组有; 项目保持 cwd -relative。 已建成 -ins
    // 仍然会战胜任何同名用户代理( 强制于 加载 )),该机型为: round below.
    // 每个根都带有其范围源,所以任务卡可以浮出a "project agent"
    // 标签(a) 仓库-shipped 人性永远是无形的。
    std::vector<std::pair<fs::path, AgentOrigin>> roots;
    {
        scope::Env env;
        env.home             = home;
        env.user_native_base = ::agentty::util::user_root();
        env.project_root     = fs::path{"."};
        env.project_writable = true;
        const scope::Layout& layout = ::agentty::config::kAgentsLayout;
        for (const scope::Source& src : scope::plan(layout, env)) {
            const AgentOrigin org = src.locus == scope::Locus::User
                ? AgentOrigin::User : AgentOrigin::Project;
            roots.emplace_back(src.base / layout.leaf, org);
        }
    }
    for (const auto& [r, org] : roots) if (!r.empty()) scan_root(r);

    auto cur = user_agent_gen().current();
    if (cur && cur->sig == sig) return cur;
    auto next = std::make_shared<UserAgentStore>();
    next->sig  = sig;
    UserAgentStore& store = *next;
    // std::deque 永远不要移动已有元素 push_back ,所以,
    // 请 string_views 已纳入 owned.back (a) 即使在下列情况
    // 下仍有效: SSO-small 字符串( 向量会移动内线) 字节 资源重新分配。
    constexpr std::size_t kMaxUserAgents = 32;
    constexpr std::size_t kMaxAgentBytes = 32 * 1024;
    static const char* kBuiltins[] = {"explorer","reviewer","tester",
                                      "coder","general"};
    for (const auto& [r, org] : roots) {
        if (r.empty()) continue;
        std::error_code ec;
        if (!fs::is_directory(r, ec) || ec) continue;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(r, ec), end; !ec && it != end;
             it.increment(ec))
            if (it->path().extension() == ".md") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            if (store.types.size() >= kMaxUserAgents) break;
            const std::string slug = p.stem().string();
            if (slug.empty() || slug[0] == '.') continue;
            bool taken = false;
            for (auto* b : kBuiltins) if (slug == b) { taken = true; break; }
            for (const auto& t : store.types)
                if (t->name == slug) { taken = true; break; }
            if (taken) continue;
            std::error_code sec;
            auto sz = fs::file_size(p, sec);
            if (sec || sz == 0 || sz > kMaxAgentBytes) continue;
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            std::string raw(static_cast<std::size_t>(sz), '\0');
            f.read(raw.data(), static_cast<std::streamsize>(sz));
            raw.resize(static_cast<std::size_t>(f.gcount()));
            parse_user_agent(raw, slug, org, store);
        }
        if (store.types.size() >= kMaxUserAgents) break;
    }
    std::shared_ptr<const UserAgentStore> out = std::move(next);
    // 输了 竞态 以进行另一个扫描:他们的文件看到了相同的文件,使用它。
    if (!user_agent_gen().publish_if(cur, out))
        if (auto won = user_agent_gen().current()) return won;
    return out;
}

// 已发现用户代理的名称(用于任务工具的枚举).
std::vector<std::string> user_agent_names() {
    const auto store = user_agents();
    std::vector<std::string> out;
    out.reserve(store->types.size());
    for (const auto& t : store->types) out.emplace_back(t->name);
    return out;
}

} // 命名空间( 匿名)

std::vector<std::string> project_agent_names() {
    std::vector<std::string> out;
    for (const auto& t : user_agents()->types)
        if (t->origin == AgentOrigin::Project) out.emplace_back(t->name);
    return out;
}

namespace {

// 一个代理类型和它能维持生存的事物:一个build-in永远的存活,
// 一个用户类型生活在它的一代,而这个功能的控件持有的时间与要求它
// 运行的运行时间一样长.
struct ResolvedAgent {
    std::shared_ptr<const UserAgentStore> store;   // 已建的-in 无效
    const AgentType*                      type;
    const AgentType& operator*()  const noexcept { return *type; }
    const AgentType* operator->() const noexcept { return type; }
};

ResolvedAgent resolve_agent_type(std::string_view t) {
    using MR = smart::ModelRole;
    static const std::vector<AgentType> kTypes = {
        {"explorer", true,
         "Your role: EXPLORER. Map and explain the codebase region the task "
         "names. Read widely, trace call sites and definitions, and return a "
         "precise map: the key files, the functions/types involved, how they "
         "connect, and any gotchas. Cite exact file paths and line numbers. "
         "You are READ-ONLY \xe2\x80\x94 never modify anything.",
         {"read", "grep", "glob", "list_dir", "find_definition", "repo_map",
          "web_search", "web_fetch"}, MR::Utility},
        {"reviewer", true,
         "Your role: REVIEWER. Critically review the code or change the task "
         "names. Look for bugs, edge cases, race conditions, security issues, "
         "and deviations from the surrounding conventions. Return findings as "
         "a prioritised list (blocker / major / minor / nit), each with the "
         "exact file:line and a concrete fix suggestion. You are READ-ONLY.",
         {"read", "grep", "glob", "list_dir", "find_definition", "repo_map",
          "git_diff", "git_log", "git_status"}, MR::Strategic},
        {"tester", false,
         "Your role: TESTER. Reproduce, run, and diagnose. Build/run the "
         "relevant tests or commands the task names, read the failures, and "
         "report the root cause with the exact failing assertion and the "
         "file:line that produced it. Prefer running over guessing. Do NOT "
         "rewrite production code \xe2\x80\x94 only run, read, and diagnose.",
         {"read", "grep", "glob", "list_dir", "find_definition", "repo_map",
          "shell", "diagnostics", "git_diff", "git_status"}, MR::Implementation},
        {"coder", false,
         "Your role: CODER. Implement the change the task names end-to-end: "
         "read the relevant code first, make the edits, and verify they build/"
         "compile if a build command is obvious. Follow the surrounding "
         "conventions exactly. Report what you changed (files + a one-line "
         "summary each) and whether it built.",
         {}, MR::Implementation},
        {"general", false,
         "Your role: GENERAL. Complete the delegated task end-to-end using "
         "whatever tools fit, then report the outcome.",
         {}, MR::Implementation},
    };
    for (const auto& a : kTypes)
        if (a.name == t) return {nullptr, &a};
    // User-defined代理(.agentty/agents/*.md):在
    // 建置-ins之后咨询,所以用户文件永远不能阴影`general`等.
    auto gen = user_agents();
    for (const auto& ua : gen->types)
        if (ua->name == t) return {gen, ua.get()};
    return {nullptr, &kTypes.back()};
}

// 副特工的OWN报告是否称失败了?.
//
// 退出之路并不总是能分辨出来,一个会消耗其转弯预算或
// 决定中途 -way 无法继续, 往往仍然返回一个井
// -formed 最后的信息——简言之,这一信息说它
// 没有做工作。 报告说,成功比无用更糟糕:召唤者(
// 一个模型或一个人类滑动)对从未发生的结果采取行动。
// 这一点已经观察到一次,而且足够:一个编码子剂报告了
// "Task NOT completed … made zero edits",UI使DONE。
//
// 故意的 CONSERVATIVE ——假阳性降级 良好跑到警告, 所以我们只是
// 匹配 毫不含糊的自我 -declarations ,在报告开头处锁定,
// 判决的根据是 "the previous approach did not work, so I fixed it"
// 不得 绊倒这段,这就是为什么没有赤裸的 "did not work"
// / "failed" 模式。
[[nodiscard]] bool subagent_self_reported_failure(const std::string& report) {
    // 报告开篇时只检查一下:判决在前面(第
    // "## OUTCOME" 头文件号,第1行),
    // 扫描整个第正文号,将符合后来确定的中间故障的叙述。
    constexpr std::size_t kVerdictWindow = 400;
    std::string head = report.substr(0, std::min(report.size(), kVerdictWindow));
    for (auto& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // 将标记下的重点从这里删除 "**Task NOT completed**" 火柴也一样。
    std::string flat;
    flat.reserve(head.size());
    for (char c : head)
        if (c != '*' && c != '_' && c != '`') flat.push_back(c);

    static constexpr std::string_view kVerdicts[] = {
        "task not completed",
        "task was not completed",
        "not completed",
        "task incomplete",
        "i was unable to complete",
        "unable to complete the task",
        "i could not complete",
        "could not complete the task",
        "no files were created or modified",
        "made zero edits",
        "zero edits",
        "i made no changes",
        "no changes were made",
        "i did not make any changes",
    };
    for (auto v : kVerdicts)
        if (flat.find(v) != std::string_view::npos) return true;
    return false;
}

std::string subagent_system_prompt(Io io, const AgentType& type) {
    // 本地模型得到同样的紧凑 提示词 (以千美元计)
    // 完成工作，不要因为任务较大就拒绝。
    // 主转弯给它。 提示词 左边 `agentty run` 和每个副剂
    // 一个小的地方模型 没有他们, 这是91。
    const auto sel = provider::active();
    std::string base = provider::uses_local_prompt(sel)
        ? provider::local_system_prompt_for(sel)
        : provider::default_system_prompt(io, /*倾斜=*/true);
    base += "\n\n<subagent>\n";
    base += std::string{type.role};
    base +=
        "\n\nYou are a SUBAGENT spawned to complete ONE delegated task in "
        "isolation. You do NOT see the parent conversation and cannot ask it "
        "questions \xe2\x80\x94 work fully autonomously from the task prompt alone. "
        "Use your tools to investigate and act, then STOP calling tools and "
        "write your final report as plain text.\n\n"
        "DECIDE, DON'T STALL: if a detail is ambiguous, make the most "
        "reasonable assumption, proceed, and NOTE the assumption in your report "
        "\xe2\x80\x94 do not abort a whole delegation over a minor gap. Only stop early if "
        "the task is genuinely impossible or self-contradictory.\n\n"
        "Your final message is the ONLY thing the parent receives \xe2\x80\x94 not your "
        "transcript, not your tool output. So the report must stand alone and be "
        "TIGHT \xe2\x80\x94 the parent is paying context for every line. No preamble, no "
        "recap of what you were asked, no narration of your steps. Structure it "
        "as:\n"
        "  \xe2\x80\xa2 A one-line OUTCOME (what you found / did).\n"
        "  \xe2\x80\xa2 Only the details the parent needs to ACT, with exact file:line "
        "references where relevant \xe2\x80\x94 evidence, not prose.\n"
        "  \xe2\x80\xa2 Any assumption you made, and anything you could NOT determine, "
        "stated plainly.\n"
        "Cite evidence (paths, line numbers, command output); never invent a "
        "path or line you didn't see. Do not pad or hedge. If the task is "
        "impossible or underspecified beyond a reasonable assumption, say so and "
        "explain exactly what's missing rather than guessing.";
    if (type.read_only)
        base += "\n\nYou are READ-ONLY: you have no tools that modify files, "
                "run commands, or reach the network. Investigate and report only.";
    base += "\n</subagent>";
    return base;
}

std::string summarize_call(const ToolUse& tc) {
    std::string s = tc.name.value;
    if (tc.args.is_object()) {
        for (const char* key : {"path", "file_path", "pattern", "command",
                                "url", "query", "symbol", "prompt"}) {
            auto it = tc.args.find(key);
            if (it != tc.args.end() && it->is_string()) {
                std::string v = it->get<std::string>();
                if (v.size() > 80) { v.resize(77); v += "..."; }
                for (auto& ch : v) if (ch == '\n' || ch == '\r') ch = ' ';
                s += "  ";
                s += v;
                break;
            }
        }
    }
    return s;
}

// 跑的什么,因为 `agentty run`。任务工具没有通过。
struct RunStats {
    std::string model;          // 上次请求的传输 id
    int  max_turns = 0;         // 大于 0 时覆盖角色的预算
    std::optional<std::chrono::seconds> wall;   // 覆盖墙壁钟
    bool read_only  = false;    // 丢弃 write/exec/net 工具, 如果调用, 则拒绝
    bool no_network = false;    // 放下净工具, 拒绝工具, 如果调用
    std::optional<Effort> effort;   // --effort; 未设置保存子代理默认值( 关闭)
    bool events = false;        // 数据流 reasoning/text 以 --events jsonl 线条
    // 继续先前的对话(第3段) --resume 新的( ) 提示词 现附上。
    std::vector<Message> history;
    // 整个对话 当运行结束, 到保存。
    Thread final_thread;
    int  turns = 0;
    int  tool_calls = 0;
    int  tool_errors = 0;
    long long input_tokens = 0;
    long long output_tokens = 0;
    long long last_input_tokens = 0;   // 提示词上次请求的大小
    long long context_window = 0;      // 请求的大小
    bool context_full = false;         // 窗户跑出来了
    bool cancelled = false;
    bool deadline_hit = false;
    bool budget_exhausted = false;
    bool doomed = false;
    std::string last_error;     // provider/stream 错误, 如果运行结束在一个
    std::string answer;         // 模特儿的最后文本,没有横幅
};

// 运行是否使用工具。 模式在列表中既看到, 也看到 调用 one
// 时再次使用, 因此未列出的工具无论如何都无法运行。
bool subagent_tool_allowed(const tools::ToolDef& t, const AgentType& type,
                           const RunStats* stats) {
    if (t.name.value == "task") return false;
    if (!type.allow.empty()) {
        bool listed = false;
        for (auto n : type.allow)
            if (n == t.name.value) { listed = true; break; }
        if (!listed) return false;
    }
    const bool read_only  = type.read_only || (stats && stats->read_only);
    const bool no_network = stats && stats->no_network;
    if (read_only || no_network) {
        tools::EffectSet eff = t.effects;
        if (const auto* sp = tools::spec::lookup(t.name.value)) eff = sp->effects;
        using tools::Effect;
        if (read_only && (eff.has(Effect::WriteFs) || eff.has(Effect::Exec)))
            return false;
        if ((read_only || no_network) && eff.has(Effect::Net)) return false;
    }
    return true;
}

provider::StreamResult run_one_completion(Thread& thread,
                              const subagent::Config& cfg,
                              const AgentType& type,
                              std::string& log,
                              const CallContext& ctx,
                              const subagent::RunRegistration* run_reg = nullptr,
                              RunStats* stats = nullptr) {
    // Provider-agnostic 请求 - 通用形状 传输层 接受 接受这个建议吧
    // fresh_auth_header 刷新该 ANTHROPIC 磁盘上的 OAuth 令牌;
    // 在任何其他后端上它都会 CLOBBER 提供者的密钥,
    // 带有 Anthropic 凭据 ,所以打开它 活跃的提供者类型。
    provider::Request req;
    // Model 路由: 已读 -only 角色( 4) explorer/reviewer
    // 做点心工作 - read/grep/map/summarise 小型的
    // 模型手柄和旗舰 成本的一小部分,所以引导它们到更便宜的模型 活动 供应商提议。
    // Write-capable 角色( 4) coder/tester/general
    // ) 保留父模式,因为其编辑必须符合母模式的质量。路由器从不运行 UP ,
    // 所以一个 -model 或 Opus 键 -only 提供者不见
    // 变化。
    //
    // 现在可以走路了 ACROSS 提供者,但仅用于一个插槽,用户被固定在另一个端点上,
    // 并且只使用 cross_provider_roles 上—
    // —见 RoleConfig 关闭(默认)它的行为与它一贯一样。
    //
    // 第3层b:当智能模式子代理路由启动时,将每个子代理解决 工作线程
    // 以它为模式 ROLE (探索者-效用、审查者-战略、
    // coder/tester/ 普通 – 执行) 通过共享解析器 –
    // 遵守用户的固定槽。 关闭 QQ 准确的 现有级自动 -router.
    //
    // 此代表团的提供者向 不 发送一个活动方。
    // 空是正常的。 由解决下面角色的分支设置,
    // 然后在两者之后验证一次。
    std::string route_provider;
    if (cfg.smart.subagent_routing()) {
        // resolve_subagent_role 难 -clamps 父母(父母)
        // 的努力 Effort::None 努力不要太过紧张 工作线程 从来没有
        // 比它产生的转弯更难思考。 Model 路线不变:探险家_用户,审查者_战略,
        // coder/tester/general ——实施.
        smart::RoleProfile role_prof =
            smart::resolve_subagent_role(type.model_role, cfg.model,
                                         Effort::None, cfg.candidates,
                                         cfg.smart, cfg.provider);
        req.model  = role_prof.model;
        // provider::Request . 功劳是: . 传输 字符串;
        // 通过解析模型的能力转换被夹住的“努力”(无 →) "", i.e.
        // ——自此,今天的情况就是如此。 parent_effort 则无)。
        req.effort = std::string(effort_wire_for(
            role_prof.effort,
            role_prof.provider.empty() ? resolved_caps(role_prof.model)
                                       : resolved_caps(role_prof.model, role_prof.provider)));
        route_provider = role_prof.provider;
    } else {
        // 读取 -only 角色为路由 DOWN 从父母,
        // 但只到中层, 不是最便宜的东西在提供者。
        //
        // 地板上曾经是便宜的,在 Anthropic
        // 意思是每个探险家在父母是Opus时 跑到海库上
        // —— 第1级负责调查第3级 代码库 非机械性 :
        // 它决定40 grep 中哪一个是重要的,
        // 一个较弱的模型会用转弯时间读错文件,
        // 然后自信地总结它们。 Zed 根本不降级;
        // 中位是中间位置。
        //
        // 通关 role_target ,所以钉 --model 也很荣幸
        // 这个分支曾经 调用 cheapest_capable_model 直接,
        // 意思是 变换智能模式 OFF 并没有停止线路的运行—
        // —它掉到这个路由器上(#70).
        if (type.read_only) {
            auto t = smart::role_target(cfg.model, cfg.candidates, cfg.smart,
                                        ModelCapabilities::Tier::Mid,
                                        &cfg.smart.utility, cfg.provider);
            req.model      = std::move(t.model);
            route_provider = std::move(t.provider);
        } else {
            req.model = cfg.model;
        }
    }

    // 一个 位置决定是否十字 -provider 目标可以达到。
    //
    // 上面的两家分公司都可以给外国供应商起名 并且可以给
    // 一个解答者起名 "run on Groq" 只有这里
    // 的东西能真正做到 分派 —— 否则,它就是一个从未听说
    // 过的模范ID, 在每个代表团上都是404。在两个分支之后,
    // 检查一次,就是为什么两个部门不能不同意。
    if (!route_provider.empty() && !cfg.stream_to) {
        AGT_LOG(Smart, Debug, "route.cross_provider_unavailable",
                "provider={} model={} falling back to active",
                route_provider, req.model);
        req.model = agentty::cheapest_capable_model(
            agentty::wire_model_id(cfg.model), cfg.candidates,
            ModelCapabilities::Tier::Mid);
        route_provider.clear();
    }
    // 副代理商 绝不 需要的帮助 1M/2M 延长 -context 窗口:
    // 它执行一个有界限的破裂(8k输出,工具结果封顶到8) KiB ,
    // 最多24个转弯),可舒适地适应200K基数窗口。 选择面板-only
    // `[1m]`/`[2m]` 标记在这里,会使 传输层 发送应享待遇
    // -gated `context-1m-2025-08-07` β,
    // 400s与(或)
    // "long context beta is not yet available for this ..."（当前订阅尚未开放长上下文测试功能。）
    // 或当旗舰母舰更便宜的副剂型号不是1M时 -eligible )——杀死整个扇子.
    // -out。无条件剥离标记:坚固(从未穿过β)和节约(水手支付他们实际使用的窗户)。
    // cheapest_capable_model 找到更便宜的模型时,
    // 已经返回一个干净的 ID。 这也覆盖了 "kept the parent"
    // 回退 写作 -role (cfg.model 路径。
    req.model         = agentty::wire_model_id(req.model);
    // `agentty run --effort` :用户要求此运行思考。
    // 粘贴到路由模型的用法; 可见于此 --events 能够显示它。
    if (stats && stats->effort) {
        const auto caps = resolved_caps(req.model);
        req.effort = std::string(effort_wire_for(*stats->effort, caps));
        req.show_reasoning = !req.effort.empty();
        req.no_reasoning   = *stats->effort == Effort::None;
        req.reasoning_off  = caps.reasoning_off;
    }
    // --events 想要一个模型产生的推理,
    // 即使没有 --effort (深处) -r1 朋友总是会想的,
    // 不是在 Anthropic : 在那里,它只意味着任何努力,上面处理。
    if (stats && stats->events && ctx.depth == 0
        && provider::active().kind != provider::Kind::Anthropic)
        req.show_reasoning = true;
    // 模型的真实背景窗口,从 相同 目录 模型
    // 被运出。
    //
    // 之后 wire_model_id (),故意的。 目录 标识符
    // 选择面板-only 标记( 4) `[1m]`/`[2m]` ,
    // 该 传输 id条,所以在条形错失了准确的有标记的行之前进行匹配——
    // 在后进行匹配意味着两侧有一个id形状.
    //
    // 从来没有在这里设定过,所以每个副剂转弯(和每个
    // `agentty run` ,这个循环是相同的)
    // context_window =0: 数字 Ollama
    // 传输层 无法大小 options.num_ctx 回到
    // 守护神的身边 ~2k/4k 默认, 切换到正中存在的长行无头。
    // 主 状态更新函数 填满它 cmd_factory.cpp :954;
    // 两个循环,两个构建请求都必须同时填充,这是下面的评论
    // 警告过的不对称.
    for (const auto& mi : cfg.candidates) {
        if (agentty::wire_model_id(mi.id.value) == req.model) {
            req.context_window = mi.context_window;
            break;
        }
    }
    // A 目录 MISS 不是,是"我们",不是"我们" "unknown window" ,
    // 这是 "this id is not in the list we happen to hold"
    // ——这很普通:现场直播. 目录 移动(a) `-m claude-opus-4-5`
    // 被绑定在设置中, 超过其行, 自定义主机可能服务 ids no 目录 列表,
    // 离线运行中只有捆绑的地板。 WRONG 回答 下游 (无)
    // Ollama 传输层 读作 0 "use my tiny default" ),
    // 所以回到模型自己的已知窗口而不是洞.
    if (req.context_window <= 0)
        req.context_window = agentty::catalog_context_window_for(req.model);
    req.system_prompt = subagent_system_prompt(IoAccess::grant(), type);   // 子代理运行于工具工作线程
    // 聪明 -channel 遥测,代表团的半数追踪。
    // 没有这个调试日志 仅 战略转动:副剂 分派 时间:
    // 工作线程 线程 以相同方式 传输层 ,因此他们的请求
    // (如果有的话)以匿名身份出现 传输 没有任何角色、
    // 父母和任何迹象表明他们是代表团。 工作线程 启动、
    // 命名角色、路由器实际选择的模型,以及Layle 3b或
    // 阶梯自动 -router 选择了它。
    AGT_LOG(Smart, Debug, "route.subagent",
            "agent={} role_routing={} pinned={} read_only={} candidates={} "
            "parent_model={} model={} route_provider={}",
            type.name,
            cfg.smart.subagent_routing() ? 1 : 0,
            cfg.smart.pinned_model ? 1 : 0,
            type.read_only ? 1 : 0,
            cfg.candidates.size(),
            cfg.model,
            req.model,
            route_provider.empty() ? "active" : route_provider);
    // 解析认证 LIVE 从 活动 通过中央提供商 凭据 层——与
    // launch_stream's auth_snapshot().
    // cfg.auth 是一个 快照 上一个
    // update_auth/switch_provider ;
    // 背景 OAuth 刷新或输入 -picker 在那之前和这副特工
    // 发射之间 开关会传送一个 过期 (或 减 减 WRONG 供应商)
    // 凭据 ——主转弯路径上固定的准确的401级. cfg.auth 现为 回
    // 退 费用 oauth_native/local 供应者, 传输层 拥有
    // 自己的符号( 在那里解析返回空)。
    //
    // SKIPPED 何时 route_provider
    // 设置: 活动 供应商 凭据 代表团去别的地方
    // 是错的 stream_to 解决目标本身
    // 的认证——一 凭据 由知道目的地的人进行搜索
    if (route_provider.empty()) {
        const auto sel = provider::active();
        const std::string pid =
            sel.kind == provider::Kind::OpenAI
                ? sel.openai_endpoint.label
                : std::string{provider::default_provider_id()};
        auth::AuthHeader live = provider::credentials::resolve(IoAccess::grant(), pid);   // 子代理, 在一个工具工作线程上
        const bool live_real = !auth::bearer_token(live).empty()
            || std::holds_alternative<auth::BearerHeader>(live);
        req.auth = live_real
                 ? (sel.kind == provider::Kind::Anthropic
                        ? auth::fresh_auth_header(IoAccess::grant(), ctx.capability(), live)
                        : std::move(live))
                 : (sel.kind == provider::Kind::Anthropic
                        ? auth::fresh_auth_header(IoAccess::grant(), ctx.capability(), cfg.auth)
                        : cfg.auth);
    }
    // 副特工的任务是调查并归还 CONCISE 独立报告—
    // —不发射32k -token 散文。 父的默认值为 16k;
    // 8k 足够报告, 但上限为每页。 -turn
    // 扇形的输出成本 -out 平行亚剂(每面转弯,
    // 在全天花板上计费)。 -up 已经逼迫了一份严谨的最后报告
    req.max_tokens    = 8192;
    // 一个小的服务窗口(vLLM 4096) 400s 一个请求,
    // 单是max_tokens就能够填补它;与主循环相同的盖.
    if (req.context_window > 0)
        req.max_tokens = std::min(req.max_tokens,
                                  std::max(256, req.context_window / 4));
    req.messages      = thread.messages;
    // 同样的飞行前主循环运行。 子代理组装自己的请求 —
    // 自己的请求 线程 自己的工具广告,自己的修剪,
    // 所以这里的缺陷是看不见的 cmd_factory 审计
    //
    // 不对称是这一类 代码库 继续学习:推理 切换
    // 3人获奖 传输层 4个 `args={}` 由主
    // 种子栽培 状态更新函数 而不是子代理循环。两个循环,
    // 两个循环, 都必须同时构建请求, 或者没有人看
    // 的循环是错误的 载荷 来者.
    {
        const auto audit = app::cmd::audit_wire(req.messages);
        AGT_LOG(Model, Debug, "wire.shape",
                "loop=subagent messages={} tool_calls={} tool_results={} "
                "images={} last_role={} defects={}",
                audit.message_count, audit.tool_call_count,
                audit.tool_result_count, audit.image_part_count,
                audit.last_role == Role::User ? "user" : "assistant",
                audit.defects.size());
        for (const auto& d : audit.defects) {
            AGT_LOG(Model, Warn, "wire.defect",
                    "loop=subagent kind={} message_index={} detail={}",
                    app::cmd::describe(d.kind), d.message_index,
                    d.detail.empty() ? "-" : d.detail);
        }
    }
    req.cancel        = std::make_shared<http::CancelToken>();
    // 稳定人数 -subagent 对话身份,
    // 所以共享前缀( 重度系统) 提示词 + 工具 模式定义
    // + 累积工具结果)是 PROMPT-CACHED
    // 超越了这副特工的整个运行 而不是重新 -encoded 从头开始,
    // 每次转弯。按键在代理角色上 + 任务 提示词 所以每个
    // 产卵亚体都有自己的稳定 缓存 车道和风扇 -out 没有这个,
    // 最大的潜质成本是 -sending 每个转弯都有一个大
    // 前缀——每个转弯都要支付全额价格。
    {
        std::string key = "subagent:";
        key += type.name;
        key += ':';
        // 任务简短的稳定摘要 提示词 (FNV -1a)
        // 保持按授权任务划分的钥匙。
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (unsigned char c : thread.messages.empty()
                                   ? std::string_view{}
                                   : std::string_view{thread.messages.front().text}) {
            h ^= c; h *= 0x00000100000001B3ULL;
        }
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<unsigned long long>(h));
        key += buf;
        req.session_key = std::move(key);
    }
    // 网关粘滞遵循PARENTXXXX 线程,所以一个新鲜
    // 的副剂直接到后端,即已经解决的对话(#118).
    req.affinity_key = ctx.conversation;

    auto allowed = [&](const tools::ToolDef& t) -> bool {
        return subagent_tool_allowed(t, type, stats);
    };
    {
        // 与主循环相同的计划(#98):每个允许的工具可调用,
        // 计划决定骑什么 传输, tool_search 找到剩下的,
        // 跑步的历史已经说了些什么 已加载.
        const tools::Catalog cat = tools::wire_tools();
        tools::tool_loading::Request tr;
        for (const auto& t : *cat)
            if (!tools::withheld_by_policy(t) && allowed(t)) tr.pool.push_back(&t);
        tr.loaded = tools::plan::loaded_in(req.messages);
        tr.limits = tools::tool_loading::limits_for(
            req.selection ? *req.selection : provider::active(),
            static_cast<std::size_t>(req.context_window > 0 ? req.context_window : 0), req.model);
        tr.server_instructions = mcp::mcp_server_instructions();
        tr.mode   = tools::tool_loading::current();
        (void)tools::tool_loading::plan_into(tr, req.tools);
    }

    Message asst;
    asst.role = Role::Assistant;
    StopReason stop = StopReason::Unspecified;
    // 用法可以超过一次地到达每个请求(开始+三角洲),
    // 每个累积一次,所以保留最大,并在结尾加一次.
    long long req_in = 0, req_out = 0;
    if (stats) stats->model = req.model;
    std::unordered_map<std::string, std::string> tool_json;
    // A StreamError 到达的 MID-STREAM ,在200个和一些内容之后。
    //
    // 电话 传输层-level 错误路径( 非-2xx 状态,
    // 连接失败) 由 StreamResult ,但提供者也可以回答
    // 200 OK, 数据流 几瓶三角洲,
    // 然后发射 `{"error": {...}}` 替换a finish_reason。
    // 解码器处理正确形状——这个循环 StreamError 手臂是,
    // 我们 空 ,所以电文被解码并丢弃。然后转弯返回到任何部分文本,
    // 好像它正常完成一样。
    //
    // 被这里抓获,折成 last_error 之后 数据流 ,
    // 所以副剂报告失败而不是短暂的成功.
    std::string stream_error;

    auto find_tool = [&](const ToolCallId& id) -> ToolUse* {
        auto it = std::find_if(asst.tool_calls.begin(), asst.tool_calls.end(),
            [&](const ToolUse& tc) { return tc.id == id; });
        return it == asst.tool_calls.end() ? nullptr : &*it;
    };

    // 螺旋式饲料泵:快速型号 数据流 每秒数百个文本三角洲,
    // 每秒 progress::emit 横跨 a 线程 边界为
    // ToolExecProgress Msg. ~12 FPS 牌上是
    // 无法区分的,并保持平行的扇形 -out 水淹的亚剂 UI 队列。
    auto last_pump = std::chrono::steady_clock::now()
                   - std::chrono::milliseconds(100);
    auto pump = [&](bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && now - last_pump < std::chrono::milliseconds(80)) return;
        last_pump = now;
        // 只有 末尾 曾经显示,和 `log` 整个运行时间 :
        // 复制最多为每勾选一个窗口的最后一个窗口,而不是全部。
        constexpr std::size_t kKeep = util::kProgressKeep;
        const std::size_t sep = asst.text.empty() ? 0 : 6;
        const std::size_t want_log =
            asst.text.size() + sep >= kKeep ? 0 : kKeep - asst.text.size() - sep;
        std::string snap{util::progress_tail(log, want_log)};
        if (!asst.text.empty()) {
            snap += "\n  \xe2\x96\xb8 ";
            snap += util::progress_tail(asst.text);
        }
        ctx.emit(snap);
    };

    // 说明理由和答复文本如下: --events 线条; 顶端 -level 运行。
    TextEvents text_events(stats && stats->events && ctx.depth == 0,
                           stats ? stats->turns + 1 : 0);
    auto sink = [&](Msg m) {
            auto* sm = std::get_if<msg::StreamMsg>(&m);
            if (!sm) return;
            std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::same_as<T, StreamTextDelta>) {
                    asst.text += e.text;
                    text_events.text(e.text);
                    pump();
                } else if constexpr (std::same_as<T, StreamThinkingDelta>) {
                    text_events.reasoning(e.text);
                } else if constexpr (std::same_as<T, StreamToolUseStart>) {
                    ToolUse tc;
                    tc.id     = e.id;
                    tc.name   = e.name;
                    // 种子 `{}` ,与主机相同 状态更新函数 .
                    // 一种没有必要参数的工具( `ls`,
                    // `git_status`) 数据流 每张零参数三角形
                    // OpenAI-compatible 传输 ,所以,
                    // 我说 `args` 无效,而且 分派器 改为:
                    // "args failed to parse" - 一个工作 调
                    // 用 参数是空的;只有 FAILED 解析 可能使参数无效。
                    tc.args   = json::object();
                    tc.status = ToolUse::Pending{std::chrono::steady_clock::now()};
                    asst.tool_calls.push_back(std::move(tc));
                    tool_json[e.id.value].clear();
                } else if constexpr (std::same_as<T, StreamToolUseDelta>) {
                    tool_json[e.id.value] += e.partial_json;
                } else if constexpr (std::same_as<T, StreamToolUseSnapshot>) {
                    // ACP每次发送整个转子;替换,不要附加.
                    tool_json[e.id.value] = e.json;
                } else if constexpr (std::same_as<T, StreamObservedToolResult>) {
                    // 一个外部特工已经运行了这个调用。 解决它,
                    // 这样下面的循环就不会第二次运行它。
                    if (ToolUse* tc = find_tool(e.id)) {
                        const auto now = std::chrono::steady_clock::now();
                        std::string out = std::move(e.output);
                        if (out.empty())
                            out = e.failed ? "External agent tool failed."
                                           : "Executed by external agent.";
                        if (e.failed) tc->settle_failed(now, std::move(out));
                        else          tc->settle_done(now, std::move(out));
                    }
                } else if constexpr (std::same_as<T, StreamToolUseEnd>) {
                    ToolUse* tc = find_tool(e.id);
                    auto json_it = tool_json.find(e.id.value);
                    const std::string partial = json_it == tool_json.end()
                                              ? std::string{} : json_it->second;
                    // 空或白空间 -only 缓冲区 意思是模型完全没有
                    // 发出任何论据—— `args` 保留 `{}`
                    // 种子于 Start。 供应商还发送一个字节 "" 这样做;
                    // 对待两者的相同方式而不是 解析 "" 失败
                    const bool blank =
                        partial.find_first_not_of(" \t\r\n") == std::string::npos;
                    if (tc && !blank) {
                        try {
                            tc->args = json::parse(partial);
                        } catch (...) {
                            // Truncated/unbalanced 参数
                            // JSON (数据流 切,弱模型。
                            // 通过合成缺失的近距离——但是 绝不 当切片落在
                            // 字符串内时 值 : 修复的 JSON 将 解析 细
                            // 细无声地运行一个工具 半 -written 正文.
                            if (!util::ended_inside_string(partial)) {
                                try {
                                    tc->args = json::parse(
                                        util::close_partial_json(partial));
                                    ::agentty::util::dbglog("subagent.tool_args.repaired",
                                                 partial);
                                } catch (...) {
                                    ::agentty::util::dbglog("subagent.tool_args.parse",
                                                 partial);
                                }
                            } else {
                                ::agentty::util::dbglog("subagent.tool_args.mid_string",
                                             partial);
                            }
                        }
                        // `cmd` → `command` 等,用于副剂的 分派
                        // 器 也一样,它会漏斗穿过同样的 ArgReader ,
                        // 但 权限 纸牌和输出面板读取犬形键。 (Laguna
                        // S 2.1) `cmd` 寻找。 )
                        if (tc) (void)::agentty::app::detail::canonify_tool_args(
                                   tc->name.value, tc->args);
                    }
                    tool_json.erase(e.id.value);
                    if (tc) {
                        log += "\n  ⚙ ";
                        log += summarize_call(*tc);
                        pump(/*力=*/true);
                    }
                } else if constexpr (std::same_as<T, StreamFinished>) {
                    stop = e.stop_reason;
                } else if constexpr (std::same_as<T, StreamUsage>) {
                    const long long in = static_cast<long long>(e.input_tokens)
                        + e.cache_read_input_tokens + e.cache_creation_input_tokens;
                    req_in  = std::max(req_in, in);
                    req_out = std::max(req_out, static_cast<long long>(e.output_tokens));
                } else if constexpr (std::same_as<T, StreamError>) {
                    // StreamResult 具有权威性 TRANSPORT
                    // 失败(non-2xx, 已丢弃的连接) ——
                    // 但是它不能看到提供者的错误。 数据流 内部 a
                    // 200美元代替a finish_reason。
                    // 用于在此降落和被丢弃的形状,所以转弯返回了它的
                    // 部分文字,好像它已经完成了一样。第一个错误胜
                    // 出:一个在解开时发出数个字的提供者最好先由任何
                    // 断开的描述来描述。
                    if (stream_error.empty()) stream_error = e.message;
                }
            }, *sm);
        };

    // 穿过该公路的路线 相同 提供者 分派 母用途(启动时安装);
    // 返回 Anthropic 传输层 无时 接口边界
    // 被传输(只安装认证+模型的测试)。
    provider::StreamResult result;
    auto cancel = req.cancel;
    // 每一个可以阻止这一切的来源 数据流 ,
    // 作为 回调 零点 线程 零醒,每一次点火 源头问,
    // 而不是下一次投票时。
    //
    //   run_reg 请求此具体运行
    //             subagent::shutdown_running (),然后是 WAITS 给它。
    //             关闭坠机窗口 而不是缩小它: 没有等待的东西,
    //             main() 可以在此时解风 线程
    //             还在里面 cfg.stream (),
    //             它通过引用来持有提供者对象.
    //   父程序为用户按下逃逸键, 或应用程序正在关闭,
    //             不论附加工具已发布。
    //
    // 这曾经是一次Jthread投票,每20米。
    // 探测器只能通过投票;工具层发布其 取消 以
    // stop_tokens 如果一个接线员安装了无任何
    // 标志的光线探测器,那么这个来源就完全没有被监视在
    // 中间 -stream 转弯圈仍然在转弯之间检查它
    using StopCb = std::stop_callback<std::function<void()>>;
    std::vector<std::unique_ptr<StopCb>> stop_cbs;
    const auto trip = std::function<void()>{[cancel] { cancel->cancel(); }};
    if (run_reg) stop_cbs.push_back(std::make_unique<StopCb>(run_reg->token(), trip));
    for (const auto& t : ctx.cancel)
        stop_cbs.push_back(std::make_unique<StopCb>(t, trip));
    if (ctx.cancelled()
        || (run_reg && run_reg->cancelled())) cancel->cancel();
    if (!route_provider.empty() && cfg.stream_to) {
        // 一个角色被另一个终点所左右。 stream_to 解决
        // 提供者自己的问题 凭据 发送前 —— 参见上面的认证块,
        // 以说明为什么此路径跳过活动区 -provider 寻觅.
        result = cfg.stream_to(ctx.capability(), route_provider, std::move(req), sink);
    } else if (cfg.stream) {
        result = cfg.stream(ctx.capability(), std::move(req), sink);
    } else {
        provider::anthropic::AnthropicProvider p;
        result = p.stream(ctx.capability(), std::move(req), sink);
    }
    // 德利格斯特在一切运行前: 回调 抓取 `cancel`
    // 晚停时不能绊倒它 数据流 已经解决了。
    // stop_callback 破坏器块直到 回调 已经飞到另
    // 一个 线程 已经返回,所以这是 竞态-free 标准。
    stop_cbs.clear();
    pump(/*力=*/true);   // 冲刷节流的 末尾
    text_events.flush();
    if (stats) {
        stats->input_tokens  += req_in;
        stats->output_tokens += req_out;
        if (req_in > 0) stats->last_input_tokens = req_in;
        stats->context_window = req.context_window;
    }

    // 一个回答200,流出一些三角洲,然后排放 `{"error": …}`
    // 而不是一个 finish_reason 叶 `result`
    // OK - 电话 传输层 看见了干净的200个,一个关闭的 数据流。
    // 只有解码器 事件 更清楚,所以在这里宣传, 与另一个职位 -stream
    // 更正如下。
    //
    // 如果没有这个结果, 转回其部分文本为成功
    // 答案 : 用户看到一个短写的答复,
    // 但没有显示出任何错误, 这是最糟糕的读取失败请求。
    if (result.ok() && !stream_error.empty())
        result = provider::StreamResult::failed(stream_error);

    // 空的成功接近是短暂的 传输层 失败。最大值 -token 转弯
    // 也不完整:部分 prose/tool JSON 报告不是最后报告。
    if (result.ok() && asst.text.empty() && asst.tool_calls.empty())
        result = provider::StreamResult::failed(
            "provider returned an empty completion");
    if (result.ok() && (result.stop == StopReason::MaxTokens
                        || stop == StopReason::MaxTokens)) {
        // 切断,要么回复中弹 max_tokens ,或 ,或 提示词 + 回复 运行
        // 到上下文窗口( Q) llama.cpp 报告均为 "length").
        const long long used = req_in + req_out;
        const bool window_full = req.context_window > 0
            && req_out < req.max_tokens
            && used >= static_cast<long long>(req.context_window) * 95 / 100;
        if (window_full && stats) stats->context_full = true;
        result = provider::StreamResult::failed(
            window_full
                ? "context window full: " + std::to_string(used) + " of "
                      + std::to_string(req.context_window) + " tokens used, reply cut off"
                : "reply cut off at the " + std::to_string(req.max_tokens)
                      + "-token output limit");
        result.stop = StopReason::MaxTokens;
    }

    thread.messages.push_back(std::move(asst));
    return result;
}

class AgenttySubagentRunner final : public mt::SubagentRunner {
public:
    std::vector<std::string> extra_agent_types() const override {
        // 用户 -authored .agentty/agents/*.md ——在任务
        // 工具中可发现. agent_type 枚举。仅列出名称; 正文 加载 以决心时间。
        return user_agent_names();
    }

    std::string unavailable_reason(const mt::Call& call) const override {
        const auto& ctx = context_of(call);
        auto cfg = subagent::current();
        if (!cfg.installed)
            return "subagent runtime was not installed; restart agentty with the current executable";
        if (cfg.model.empty())
            return "no model is selected for the active provider";
        // 运行时 分派 谓 凭据-aware 本身: ChatGPT
        // 解决每个请求的本地 OAuth 商店, 而本地提供者则不需要
        // 任何认证。 -Anthropic 回退 需要非 -empty
        // 头文件 给
        if (!cfg.stream && auth::is_empty(cfg.auth))
            return "no provider stream or fallback Anthropic credential is configured";
        if (ctx.depth >= subagent::kMaxDepth)
            return "subagent nesting depth limit reached (maximum "
                 + std::to_string(subagent::kMaxDepth) + ")";
        if (ctx.cancelled())
            return "cancelled before the subagent started";
        return {};
    }

    std::string run(const mt::Call& call, const mt::SubagentRequest& sreq,
                    bool& is_error) override {
        return run_with_stats(call, sreq, is_error, nullptr);
    }

    std::string run_with_stats(const mt::Call& call, const mt::SubagentRequest& sreq,
                               bool& is_error, RunStats* stats) {
        is_error = false;
        // 所附内容 调用 上下文:其进度卡, 取消 编辑
        // depth/deadline 周围的副剂。
        const CallContext& ctx = context_of(call);
        auto cfg = subagent::current();
        if (!cfg.installed || cfg.model.empty()
            || (!cfg.stream && auth::is_empty(cfg.auth))) {
            is_error = true;
            return "subagents are unavailable in this context (no model/stream wired)";
        }
        if (ctx.depth >= subagent::kMaxDepth) {
            is_error = true;
            return "subagent depth limit reached — a subagent cannot spawn "
                   "further subagents at this nesting level";
        }


        // 被保存了整个运行:重新扫描代理文件无法解开.
        const ResolvedAgent resolved = resolve_agent_type(sreq.agent_type);
        const AgentType& type = *resolved;

        Thread thread;
        if (stats) thread.messages = stats->history;
        {
            Message user;
            user.role = Role::User;
            user.text = sreq.prompt;
            thread.messages.push_back(std::move(user));
        }

        int turns = 0;
        std::string log = "\xe2\x97\x86 " + std::string{type.name} + " agent";
        std::string last_error;
        // 给这副代理 自身 读 -dedup 整个运行的背景。
        //
        // `read` 回答相同的重复(路径、
        // 范围) "refer to the earlier tool_result"
        // ——只针对收到这些请求的背景 字节。该主题 内存 由读取器键入,
        // 所以没有此范围,子代理继承 PARENT 输入和被拒绝的文件,
        // 它从未见过。 这不是理论的: 一个编码器子代理 花了全部23
        // 个转折 -requesting 一份,每次都有监控,
        // 并且没有编辑 RAII-restored 所以筑巢不能泄露身份证明
        //
        // ID是一个 PROCESS-WIDE COUNTER ,
        // 没有 `&thread`。堆栈地址看起来独特,
        // 而不是: 被隔离的子代理运行 工作线程 线程 ,
        // 并连续运行相同角色的重用相同堆栈,所以 `&thread`
        // 出来 字节-identical 每次都(按先后顺序衡量:6
        // 次) 线程 因此,两个连续的探险家分享了上下文,
        // 第二个被拒绝了第一个文件所读的每个文件——对于顺序案例,
        // 重新提出这个范围存在的准确错误来防止。
        static std::atomic<std::uint64_t> subagent_run_seq{0};
        const std::string run_reader = "subagent:" + std::string{type.name} + ":"
            + std::to_string(subagent_run_seq.fetch_add(1, std::memory_order_relaxed));
        // 瞬变 数据流 失败( 4) 429/529 棕色 -out,
        // TLS 重设, 传输层 ⁇ )是( ⁇ ).
        // RETRIED 与 退避 而不是中止整个副剂——
        // "task fails a lot" 循环计数器:
        // 任何成功的完成都重置它。
        constexpr int kMaxStreamRetries = 3;
        int stream_failures = 0;
        // 墙 -clock 最高比率 整个 运行,不靠转弯。
        //
        // kMaxTurns 定义了子剂需要完成多少个;
        // 它没有定义每个子剂需要多长时间。
        // 每名 -stream 预算共计30分钟,
        // 闲置90秒 超时 并且闲置的只是对真实的沉默的火——
        // 一个每60秒就把守着的通道,而且从未结束 "healthy"
        // 将梯子堆上,循环圈的真天花板是80转x3 重试 x 30分钟,
        // i.e 天数。用户看到的是 `task`
        // 永远不回来的卡片 父母会把牌子插在后面
        //
        // 15分钟是选在工作上,而不是选在 传输层 :
        // 单 -role 预算假设一个探险家在几个转弯中汇合,
        // 一个编码员运行几个转弯 build/fix
        // 15分钟的潜质不是彻底的,而是卡住了-
        // 同样的参数 kMaxTurnsReadly 仅
        // 让转弯40。 CEILING 而不是配额:20
        // 岁时完成的跑步 毫无代价,只有跑步才能支付
        //
        // 过期时循环 STOPS 并报告它所拥有的,
        // 正像转弯盖一样——部分发现击败了绞刑,
        // 以及 is_error 标志 下方别乱动 渲染 以绿色成功为代表
        //
        // 无法逾越 AGENTTY_SUBAGENT_MAX_SECONDS
        // 所以行为是 TESTABLE. A 15-minute
        // 无法用测试套件来进行恒定,未经测试的停止路径是静静地
        // 停止工作的路径——这就是阅读的方式。 -context 范围退缩。
        // 每运行一次读入内存 var;未设置或无法解析表示默认值。
        const auto max_wall_clock = [stats] {
            if (stats && stats->wall) return *stats->wall;
            if (const char* s = std::getenv("AGENTTY_SUBAGENT_MAX_SECONDS")) {
                char* end = nullptr;
                const long v = std::strtol(s, &end, 10);
                if (end != s && v > 0 && v < 86'400)
                    return std::chrono::seconds(v);
            }
            return std::chrono::seconds(15 * 60);
        }();
        // NESTED 运行继承了后面的最后期限,而不是开始一个新的期限——见
        // subagent::DeadlineScope 为什么(千马克深度是2,
        // 所以每秒) -run 时钟可以使真正的约束深度 x 成为广告的深度。
        // 线程-local 自己生活着 subagent.cpp ,
        // 这是 TU 允许它保持。
        const auto own_deadline =
            subagent::RunClock::now() + max_wall_clock;
        const auto run_deadline =
            ctx.deadline ? std::min(*ctx.deadline, own_deadline) : own_deadline;
        // 发布此运行, 以找到它。 如果没有此操作, 关闭将询问 TOOL
        // 线程 停止( 编号: run_tool 探测器),但没有什么等待
        // 提供者 数据流 向风向移动,以及 main() 可以在循环还
        // 在内部时返回 cfg.stream () —— 将提供者的物体
        // 通过参考来保存在主堆栈之外。 "done" ,
        // 这就是什么 shutdown_running (等待)见
        // subagent::RunRegistration.
        const subagent::RunRegistration run_reg;
        // 此运行自己的工具的背景 调用 得到: 一层深,
        // 自己的阅读器, 更紧的最后期限, 已取消 当父母中任何
        // 一个 调用 或这次跑步是。
        CallContext child = ctx;
        // 复制上下文会故意放弃其能力( 复制会超过或超过) -travel
        // 编号 调用). `child` 也不是,它靠这个生活 帧 ,
        // 在其中的内侧 调用 证明能力,在相同的 线程 把它直接背过来,
        // 没有这个 provider/auth 回调 投出时
        // "tool callback outside a dispatched call"（工具回调发生在已分派调用范围之外。）
        // 副代理商永远无法运行。
        child.cap.set(ctx.cap.get());
        child.depth    = ctx.depth + 1;
        child.reader   = run_reader;
        child.deadline = run_deadline;
        child.active_skills.clear();
        child.cancel.push_back(run_reg.token());
        bool deadline_hit = false;
        // 重复 -failure 断路器( 与父母的末日 相同 )
        // -loop 断开器: 相同的工具 调用 3×失败意味着循环被
        // 卡住了——停止燃烧转弯,并报告我们所拥有的情况.
        std::unordered_map<std::string, int> failed_calls;
        // 镜子 failed_calls : 保存的工具
        // SUCCEEDING 与 字节-identical 参数也循环着,
        // 而这个案件根本没有上限。用一个私人网关背后的本地模型来衡量:
        // 80个相同成功 `read` 调用 22GB的
        // 一位居民, GPU 重新花费的时间
        // -deriving 已经是答案了
        //
        // 阈值高于失败的阈值: 重复 WORKS 较弱的
        // 证据(关于 -reading 编辑后的文件是正常的,
        // 但连续六次没有文本答案不是策略。
        std::unordered_map<std::string, int> repeated_ok_calls;
        bool doomed = false;
        // 等我们告诉模型 预算用完了 就得写最后报告 当前。
        // 防止一个彻底的代理人直接探索到转弯顶部,
        // 而永远不报告(下面的环路会拯救一个
        // 过期 早期的描述线,而不是真实的答案).
        bool wrapup_nudged = false;

        // 转预算为 此 角色。读作 -only 扫描迅速汇合;
        // 执行循环(编辑) 建 ——读错误 ——修正——关于 -run )
        // 在结构上更长,饥饿产生 "ran out of turns, made zero edits"
        // 而不是报告工作。
        const int max_turns = (stats && stats->max_turns > 0)
                                  ? stats->max_turns
                                  : subagent::max_turns_for(type.read_only);

        while (turns < max_turns && !doomed) {
            // 墙 -clock 停止,检查每个转弯的顶端。 while()
            // 条件如此 `deadline_hit` 设置在循环退出前,
            // 下面的报告可以说明 对应的 天花板结束了运行...
            // "out of turns" 财务报告和已审计财务报表
            // "out of time" 调用 与诵读它的人所修的律例,
            if (std::chrono::steady_clock::now() >= run_deadline) {
                deadline_hit = true;
                break;
            }
            // 关闭要求我们停止。 数据流 (通过
            // 取消 ——而不是握着 数据流 完全没有,
            // 仍然迅速离开, 而不是开始
            // 一个完成 拆卸,然后等待。
            if (run_reg.cancelled()) break;
            ++turns;
            if (stats) {
                stats->turns = turns - 1;   // 迄今已完成; 下一个是转弯
                if (stats->events && ctx.depth == 0)
                    emit_event({{"ev", "turn"}, {"turn", turns}});
            }

            // FINAL-TURN NUDGE : 当剩余预算接近使用时,
            // 注入一个合成用户信息, 命令该模型停止运行工具并
            // 撰写报告。 如果没有此信息, 代理将最后一次完成
            // 时间花在另一个工具上 调用 ,
            // 击中顶端 -investigation ,则完全不返回最后文本。
            //
            // 预算的准备时间尺度:一个转弯的警告足以进行短读扫荡,
            // 但编辑-建设-固定周期中一个深层的写角色需要
            // 空间来登陆它开始的内容(完成当前编辑,
            // 再修改) -run 废物预算太早;产生废物的太晚。
            // "ran out of turns with nothing to show"（轮次数已耗尽，却没有可展示的结果。）
            // 报告此信息是为了防止。
            //
            // 同样的一击也点燃了 TIME 副代理商可能处于
            // 80年的6年,但预算仍未到位——只需要几
            // 座缓慢的建筑或一个铺设的网关——没有这一点,
            // 它就会在最后期限的中期达到。 -tool-call
            // 并且完全不返回报告, 这正是失败的转弯
            // -based 阻力。两分钟是相同的铅形状:足够
            // 降落目前的步骤和总结,不足以启动新的东西。
            const bool near_deadline =
                std::chrono::steady_clock::now() + std::chrono::minutes(2)
                    >= run_deadline;
            const int wrapup_lead = max_turns >= 48 ? 3 : 1;
            if (!wrapup_nudged
                && (turns >= max_turns - wrapup_lead || near_deadline)
                && !thread.messages.empty()
                && thread.messages.back().role != Role::User) {
                Message nudge;
                nudge.role = Role::User;
                nudge.text =
                    "You are almost out of turn budget. Finish or abandon the "
                    "step you are on — do not start anything new — then write "
                    "your FINAL report as a plain text message: a complete, "
                    "self-contained answer to the task using everything you "
                    "have gathered so far. If you did not complete the task, "
                    "say so plainly and state exactly what remains.";
                thread.messages.push_back(std::move(nudge));
                wrapup_nudged = true;
            }

            provider::StreamResult stream_result =
                run_one_completion(thread, cfg, type, log, ctx, &run_reg, stats);

            if (!stream_result.ok()) {
                const std::string err = stream_result.error.value_or("stream failed");
                last_error = err;
                if (stats && provider::is_context_overflow(err)) stats->context_full = true;
                const auto error_class = stream_result.cancelled()
                    ? provider::ErrorClass::Cancelled
                    : provider::classify_stream_error(err,
                                                       stream_result.http_status);
                const bool empty_completion =
                    err == "provider returned an empty completion";
                const bool retryable = !stream_result.non_replayable
                    && (empty_completion
                        || error_class == provider::ErrorClass::Transient
                        || error_class == provider::ErrorClass::RateLimit);
                // 部分助理消息可以包含未发件的工具 调
                // 用 永远不能重现 重试 或报告为完整。
                if (!thread.messages.empty()
                    && thread.messages.back().role == Role::Assistant)
                    thread.messages.pop_back();
                if (!retryable || error_class == provider::ErrorClass::Cancelled
                    || ctx.cancelled()) {
                    is_error = true;
                    if (error_class == provider::ErrorClass::Cancelled
                        || ctx.cancelled())
                        last_error = "subagent cancelled: " + err;
                    break;
                }
                ++stream_failures;
                if (stream_failures > kMaxStreamRetries) {
                    log += "\n  ⚠ stream failed "
                         + std::to_string(stream_failures) + "× — giving up: "
                         + err;
                    ctx.emit(log);
                    break;
                }
                // A RETRY-AFTER MEASURED
                // IN HOURS IS A QUOTA,
                // 不 A BURST LIMIT。爆炸极限数以秒计,
                // 重试 计划配额在重新发布之前是无法明确的,
                // 首次 答复已经进行。
                //
                // 计量: Yolo -Auto 免费的等级答案
                // `retry-after: 29410` (8小时,
                // 00:00) UTC ),类型= insufficient_quota
                // 将它压到30岁 "you are out until midnight"
                // 进入三个毫无意义的等待和95秒的墙钟,然后放弃同样的文字.
                // 与互动相同的推理 状态更新函数 —— 见 kRetry After
                // provider/error_class.hpp 中的终止状态相关说明。
                if (stream_result.retry_after
                    && stream_result.retry_after->count()
                           > provider::kRetryAfterTerminalSeconds) {
                    log += "\n  \xe2\x9a\xa0 " + err;
                    ctx.emit(log);
                    break;
                }
                // 尊重服务器的指引,但封顶它如此 hostile/mistaken
                // 重试-After 无法定点任务 工作线程 无限期地
                constexpr auto kMaxRetryAfter = std::chrono::seconds{30};
                auto wait = stream_result.retry_after
                    ? std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::min(*stream_result.retry_after, kMaxRetryAfter))
                    : provider::backoff_with_jitter(error_class,
                                                    stream_failures - 1);
                log += "\n  ↻ retry " + std::to_string(stream_failures)
                     + "/" + std::to_string(kMaxStreamRetries)
                     + " in " + std::to_string(wait.count()) + "ms (" + err + ")";
                ctx.emit(log);
                if (ctx.wait(wait, run_reg.token())) {
                    is_error = true;
                    if (stats) {
                        stats->cancelled  = true;
                        stats->turns      = turns;
                        stats->last_error = err;
                    }
                    return "subagent cancelled while waiting to retry: " + err;
                }
                --turns;
                continue;
            }
            stream_failures = 0;
            last_error.clear();

            Message& asst = thread.messages.back();
            bool ran_a_tool = false;
            if (!asst.tool_calls.empty()) {
                const auto now = std::chrono::steady_clock::now();
                for (auto& tc : asst.tool_calls) {
                    if (child.cancelled()) {
                        is_error = true;
                        last_error = "subagent cancelled before local tool execution";
                        if (stats) stats->cancelled = true;
                        doomed = true;
                        break;
                    }
                    // 已经由外部代理人解决(StreamObservedToolResult)。
                    if (tc.is_terminal()) continue;
                    if (tc.args.is_null()) {
                        if (stats) { ++stats->tool_calls; ++stats->tool_errors; }
                        tc.status = ToolUse::Failed{now, now,
                            "tool args failed to parse \xe2\x80\x94 re-emit the call "
                            "with complete, valid JSON arguments"};
                        log += "\n    \xe2\x9c\x97 " + tc.name.value + ": bad args";
                        ctx.emit(log);
                        // A 解析 失败仍为工具 ROUND-TRIP:
                        // 模型查看错误 tool_result 并且
                        // 可以重新 -emit 没有这个,第一个坏消息
                        // 就爆发了 -args 调用 整个任务都死了
                        ran_a_tool = true;
                        continue;
                    }
                    // 只有此运行提供的工具才能运行。 一个模型仍然可以命名它没有
                    // 给它的工具( 读)。 -only, --no-network).
                    if (const auto def = tools::find(tc.name.value);
                        def && !subagent_tool_allowed(*def, type, stats)) {
                        if (stats) { ++stats->tool_calls; ++stats->tool_errors; }
                        tc.status = ToolUse::Failed{now, now,
                            "tool '" + tc.name.value + "' is not available in this run"};
                        log += "\n    \xe2\x9c\x97 " + tc.name.value + ": not allowed";
                        ctx.emit(log);
                        emit_tool_event(tc.name.value, 0, false, "not allowed in this run",
                                        tc.args.dump());
                        ran_a_tool = true;
                        continue;
                    }
                    ran_a_tool = true;
                    const auto t_start = std::chrono::steady_clock::now();
                    // 子代理自身背景:更深,自己的读者和
                    // 截止日期,父母的现场输出卡(如前).
                    CallContext tool_ctx = child;
                    tool_ctx.active_skills = skills::active_in(::agentty::visible_text(thread));
                    auto res = tool::DynamicDispatch::execute(child.capability(), tc.name.value, tc.args, tool_ctx);
                    // 一样 tool.exec 记录主循环的释放,
                    // 所以没有头 `agentty run`
                    // 并且副剂转动与交互转动一样可以诊断。
                    // 一个失败的工具是警告(因此在坠机记录器中),
                    // 其原理是导致它——在副驾驶工具中缺失的证据。
                    // 调用 空空的飞过来了
                    //
                    // 此处没有下下文,与 状态更新函数 副本 :
                    // exec_seq 由 cmd::next_tool_exec_seq
                    // () 让 状态更新函数 倒置结果的替换转弯,
                    // 而此路径没有 状态更新函数 而不转而转而转之,
                    // 它各奔走, 调用 以顺序完成。匹配名称+args即可。
                    const auto t_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t_start).count();
                    const std::string err_text =
                        res ? std::string{"-"}
                            : std::string{tools::to_string(res.error().kind)}
                                  + ": " + res.error().detail;
                    // 弧筁 logx::body (,将它们限制在4)
                    // KB 头部,除非 AGENTTY_LOG_BODIES
                    // 设定——与 传输 频道和 状态更新函数's
                    // tool.dispatch/tool.exec 双向给用户数据。
                    // 它是 SIZE 绑定,而不是修改: 短的参数blob
                    // 仍然完整出现, 这是您在诊断工具时想要的 调用.
                    // 它能防止的是 一个编辑工具 调用 带2
                    // MB 载荷 成为 2 MB 这条线用来
                    // 直接穿过垃圾场,完全没有束缚。
                    AGT_LOGL(Tool, res ? ::agentty::logx::Level::Debug
                                       : ::agentty::logx::Level::Warn,
                             "tool.exec", "name={} ms={} ok={} err={} args={}",
                             tc.name.value, t_ms, res ? 1 : 0, err_text,
                             ::agentty::logx::body(tc.args.dump()));
                    // 和上面的对数线一样 但都是在Stderr和机器上
                    // -readable 沙盒式的绳索从不看日志文件。
                    // emit_tool_event 为何要用大麻来做
                    emit_tool_event(tc.name.value, t_ms, static_cast<bool>(res),
                                    res ? std::string_view{}
                                        : std::string_view{err_text},
                                    tc.args.dump());
                    if (stats) {
                        ++stats->tool_calls;
                        if (!res) ++stats->tool_errors;
                    }
                    if (res) {
                        // ECONOMY : 子剂是一个焦点,
                        // 工具 -heavy 爆炸( 爆炸时)
                        // read/grep/repo_map 产出
                        // 为: KiB )这些结果累积在子剂的体内。
                        // 自身 线程 重放时 每个 之后转弯,
                        // 所以成本是四倍的转弯——写角色现在得到一个以几十
                        // 转弯计算的预算,这恰恰是咬伤时。
                        // KiB 最新数据 -result 预算是用来进行长时间
                        // 的交互聊天的;对于一个子代理,预算是主导成本.
                        // 盖上每个头部都有一个紧身的 末尾 我们
                        // 一储存起来, 工作就设置了模型 理由
                        // 保持精细,而不失去 内容 任何结果。
                        // (传输层 老年仍然适用于旧的。 )
                        constexpr std::size_t kSubagentToolBudget = 8u * 1024u;
                        std::string capped = provider::wire::cap_tool_result(
                            res->text, kSubagentToolBudget);
                        tc.status = ToolUse::Done{now, now, std::move(capped)};
                        log += "\n    \xe2\x9c\x93 " + summarize_call(tc);
                        // 同文SUCCEEDING 调用 6××××~也卡住了.
                        std::string ok_key = tc.name.value + '\0'
                            + (tc.args.is_null() ? std::string{} : tc.args.dump());
                        if (++repeated_ok_calls[ok_key] >= 6) {
                            doomed = true;
                            log += "\n  \xe2\x9a\xa0 same call repeated 6\xc3\x97 "
                                   "with identical args \xe2\x80\x94 stopping";
                        }
                    } else {
                        tc.status = ToolUse::Failed{now, now, res.error().render()};
                        log += "\n    \xe2\x9c\x97 " + summarize_call(tc) + "  \xe2\x80\x94 "
                             + res.error().render();
                        // 相同失败的调用 3x = 循环卡住了。
                        std::string key = tc.name.value + '\0'
                            + (tc.args.is_null() ? std::string{} : tc.args.dump());
                        if (++failed_calls[key] >= 3) {
                            doomed = true;
                            log += "\n  \xe2\x9a\xa0 same call failed 3\xc3\x97 \xe2\x80\x94 stopping";
                        }
                    }
                    ctx.emit(log);
                }
            }

            if (!ran_a_tool) break;   // 最后文本答案(或无所作为)
        }

        // 提取报告。 AUTHORITATIVE 报告为
        // FINAL 助理转身——模型在工具工作完成后最后写下的信息。
        // 不 向后走到第一个非 -empty 文本( E) :
        // 一种直接探索到转弯盖的代理器 其最后的几个转弯是纯工具 调
        // 用 (空的.文本),和天真的反转 -walk 将恢复其轮值
        // -1的描述(第2段), "I'll start by mapping..."
        // ) 并把它当作一个完成报告。那是真正的错误:
        // 24 -turn 运行返回它的开头句而不是答案。
        std::string report;
        bool salvaged_stale = false;
        if (ctx.on_subagent_done) {
            try { ctx.on_subagent_done(thread); } catch (...) {}
        }
        if (!thread.messages.empty()
            && thread.messages.back().role == Role::Assistant
            && !thread.messages.back().text.empty()) {
            // 清末:模特最后的行为是写作散文.
            report = thread.messages.back().text;
        } else {
            // 没有最后案文(预算未包括) -tool ,
            // 或者最后一个回合是工具 -only
            // ) 救护最新 EARLIER 助理文本
            // 作为部分,但标记它 过期 无法进入
            // 历史: 答案属于更早的运行。
            const std::size_t first_new = stats ? stats->history.size() : 0;
            for (std::size_t i = thread.messages.size(); i-- > first_new;) {
                const auto& msg = thread.messages[i];
                if (msg.role == Role::Assistant && !msg.text.empty()) {
                    report = msg.text;
                    salvaged_stale = true;
                    break;
                }
            }
        }

        // 预算耗尽是FAILUREXX MODE,即使最后转弯时恰好是生产
        // 干净的传言。 这里的旧大门是`report.empty() || salvaged_stale`,
        // 所以一个每转弯就花一圈时间写一个干净的
        // "## OUTCOME — Task NOT completed, I made zero edits"（任务未完成，没有进行任何修改。）
        // 的代理商掉到它上:没有横幅,is_error从未设置,
        // UI在-declared失败时就制造了绿色的DONE。
        // 完全照着这个,把击球盖当作
        // 一个可报告的条件
        const bool budget_exhausted = turns >= max_turns || deadline_hit;
        if (stats) {
            const bool clean_finish = !report.empty() && !salvaged_stale;
            stats->turns            = turns;
            stats->deadline_hit     = deadline_hit && !clean_finish;
            stats->budget_exhausted = turns >= max_turns && !clean_finish;
            stats->doomed           = doomed;
            stats->last_error       = last_error;
            if (ctx.cancelled() || run_reg.cancelled()) stats->cancelled = true;
            stats->answer = report;
            stats->final_thread = thread;
        }

        if (report.empty() || salvaged_stale) {
            std::string why;
            if (!last_error.empty())
                why = "[subagent failed: " + last_error + "]";
            else if (doomed)
                why = "[subagent stopped: the same tool call failed 3\xc3\x97 "
                      "in a row without converging]";
            else if (deadline_hit)
                // 与旋转盖分开命名:
                // "out of time" 财务报告
                // 和已审计财务报表 "out of turns"
                // 读者只能按不同的原因行事。
                why = "[subagent hit its wall-clock limit without producing a "
                      "final report \xe2\x80\x94 the summary below is incomplete]";
            else if (budget_exhausted)
                why = "[subagent hit its turn budget without producing a final "
                      "report \xe2\x80\x94 the summary below is incomplete]";
            else
                why = "[subagent finished without a final text report]";
            if (salvaged_stale) {
                // 我们从早先的转折中 部分地传播出来: 浮出标志,
                // 然后是被打碎的文字,然后是活动日志。
                report = why + "\n\nLast text the subagent produced (may be an "
                             "early, incomplete note):\n" + report
                       + (log.empty() ? std::string{}
                                      : "\n\nActivity:\n" + log);
            } else {
                report = log.empty() ? why : (why + "\n\nActivity:\n" + log);
            }
            // 赤裸的错误( 无可修复的报告) 被传播为错误, 所以 shell 标记
            // tool_result is_error.
            if (!last_error.empty()) is_error = true;
            // 也没有厄运 -loop 中止或预算耗尽都产生了
            // 要求完成的工作。 两者都是失败的,说吧。
            if (doomed || budget_exhausted) is_error = true;
        } else if (budget_exhausted) {
            // 预算没有这笔钱,但签了字,这段传言值得保留——
            // 这是经纪人自己对它取得多大进展的叙述——但是,
            // 它绝不能被打电话者或传呼者误认为成功。 UI.
            //
            // 上面空的-report分支同样也因此提出WHICH预算。
            // 当它恰好写了用来降落的传言并贴上
            // "turn budget"的标签时,
            // 运行被时钟停止, 指向读错的人。
            report = (deadline_hit
                          ? "[subagent hit its wall-clock limit \xe2\x80\x94 the task "
                            "may be incomplete; verify before relying on it]"
                          : "[subagent hit its turn budget \xe2\x80\x94 the task may "
                            "be incomplete; verify before relying on it]")
                   + std::string{"\n\n"} + report;
            is_error = true;
        }

        // 潜质 NARRATES 模型是它是否完成了工作的权威,
        // 当它说它没有完成时,相信文本的退出路径是怎样的。
        // "zero edits made" 报告
        // 最后作为“...”提交 DONE.
        if (!is_error && subagent_self_reported_failure(report)) {
            is_error = true;
        }

        std::ostringstream out;
        out << "Subagent report (" << type.name << ", " << turns << " turn"
            << (turns == 1 ? "" : "s") << "):\n\n" << report;
        return out.str();
    }
};

} // 命名空间结束。

void install_host_backends(::mcp::tools::HostServices& svc) {
    svc.memory    = std::make_shared<AgenttyMemoryStore>();
    svc.skills    = std::make_shared<AgenttySkillResolver>();
    svc.retriever = std::make_shared<AgenttyDocRetriever>();
    svc.code_retriever = std::make_shared<AgenttyCodeRetriever>();
    svc.subagent  = std::make_shared<AgenttySubagentRunner>();
    // svc.todo 故意留空: mcp 待办事宜 shell 渲染
    // 与本地工具完全相同,不需要东道国,以及 agentty's TUI
    // 解析 文本中已显示的文本 —— 没有结构化的槽可以输入。
}

bool is_known_agent_type(std::string_view name) {
    // resolve_agent_type (a) 答复
    // `general` 任何它不识别,所以,
    // "did it match" 需要由下列人士提出: IDENTITY ,
    // 而不是通过结果是否回来。将返回的名称与请求的名称进行比较
    // 正是这个问题,而且它保持正确如构建的 -ins 用户代理来回
    // 因为它重用同一解析器 而不是清单的第二副本。
    if (name.empty()) return false;
    return resolve_agent_type(name)->name == name;
}

std::string known_agent_types() {
    std::string out;
    for (std::string_view n :
         {"general", "explorer", "reviewer", "tester", "coder"}) {
        if (!out.empty()) out += ", ";
        out.append(n);
    }
    // 用户 -defined 角色是真实的答案 --agent ,因此,
    // 一个写一个的用户应该看到它被列出,而不是被告知自己的代理无效
    // -looking.
    try {
        for (const auto& ua : user_agents()->types) {
            out += ", ";
            out += ua->name;
        }
    } catch (...) {
        // 列出用户代理是一件好事; 永远不要失败错误消息。
    }
    return out;
}

std::string_view status_name(OneShotResult::Status s) noexcept {
    using S = OneShotResult::Status;
    switch (s) {
        case S::Ok:            return "ok";
        case S::Failed:        return "failed";
        case S::TurnLimit:     return "turn_limit";
        case S::Timeout:       return "timeout";
        case S::ProviderError: return "provider_error";
        case S::Cancelled:     return "cancelled";
        case S::Unavailable:   return "unavailable";
        case S::NotFound:      return "not_found";
        case S::ContextFull:   return "context_full";
    }
    return "failed";
}

OneShotResult run_one_shot(const maya::unlocked& cap, const OneShotOptions& opts) {
    // 与任务工具相同的环路,驱动自深度0的main().
    using S = OneShotResult::Status;
    const auto t0 = std::chrono::steady_clock::now();
    OneShotResult out;
    out.agent = opts.agent_type.empty() ? "general" : opts.agent_type;

    CallContext ctx;
    // `agentty run` 驱动子代理环路从 main() ,而不是通过
    // DynamicDispatch 没有其他能力,没有它 provider/auth 回
    // 调 掷出 "tool callback outside a dispatched call" ,
    // 并在第一次请求前中止进程。 `ctx` 活在这 帧 ,主机上,
    // 请检查date=中的日期值 (帮助) 线程 ,在内部 调用 证明 `cap`.
    ctx.cap.set(std::addressof(cap));
    if (opts.cancel.stop_possible()) ctx.cancel.push_back(opts.cancel);
    mt::Call top{};
    const HostCall host{&ctx, nullptr};
    top.host = &host;

    AgenttySubagentRunner runner;
    if (auto why = runner.unavailable_reason(top); !why.empty()) {
        out.status = S::Unavailable;
        out.error  = std::move(why);
        return out;
    }
    RunStats st;
    st.max_turns  = opts.max_turns;
    if (opts.timeout.count() > 0) st.wall = opts.timeout;
    st.read_only  = opts.read_only;
    st.no_network = opts.no_network;
    st.events     = opts.events;
    if (!opts.effort.empty()) st.effort = effort_from_wire(opts.effort);

    // --resume : 继续a 已保存 线程. "last" 是最新鲜的
    Thread prior;
    if (!opts.resume.empty()) {
        const Io io = IoAccess::grant();
        std::optional<Thread> got;
        if (opts.resume == "last") {
            auto all = persistence::load_all_threads(io, cap);
            auto newest = std::max_element(all.begin(), all.end(),
                [](const Thread& a, const Thread& b) { return a.updated_at < b.updated_at; });
            if (newest != all.end()) got = persistence::load_thread_by_id(io, newest->id);
        } else {
            got = persistence::load_thread_by_id(io, ThreadId{opts.resume});
        }
        if (!got) {
            out.status = S::NotFound;
            out.error  = opts.resume == "last" ? "no saved thread to resume"
                                                : "no thread with id " + opts.resume;
            return out;
        }
        prior = std::move(*got);
        st.history = prior.messages;
        std::vector<std::string> transcripts;
        for (const auto& msg : prior.messages)
            if (msg.fork_note && !msg.fork_transcript.empty())
                transcripts.push_back(msg.fork_transcript);
        if (!transcripts.empty())
            ::agentty::tools::util::allow_fork_transcripts(
                IoAccess::grant(), persistence::threads_dir(), transcripts);
    }

    // 运行时的1 id: 网关会话 id (# 118) 和 id
    // the 线程 谓 已保存 恢复运行后,将保持 线程's.
    const bool fresh = prior.id.value.empty();
    if (fresh) prior.id = persistence::new_id();
    ctx.conversation = prior.id.value;

    mt::SubagentRequest sreq;
    sreq.prompt     = opts.prompt;
    sreq.agent_type = out.agent;
    bool is_error = false;
    std::string report = runner.run_with_stats(top, sreq, is_error, &st);

    // 保存 发生什么了,所以可以恢复,或者在 TUI
    // 从未到达模型的跑步没有新的东西可保留
    if ((opts.save || !opts.resume.empty())
        && st.final_thread.messages.size() > st.history.size()) {
        Thread t = std::move(prior);
        if (fresh) {
            t.title      = persistence::title_from_first_message(opts.prompt);
            t.created_at = std::chrono::system_clock::now();
        }
        t.messages   = std::move(st.final_thread.messages);
        t.updated_at = std::chrono::system_clock::now();
        persistence::save_thread(IoAccess::grant(), cap, t);
        out.thread_id = t.id.value;
    }

    out.model         = st.model;
    out.turns         = st.turns;
    out.tool_calls    = st.tool_calls;
    out.tool_errors   = st.tool_errors;
    out.input_tokens  = st.input_tokens;
    out.output_tokens = st.output_tokens;
    out.last_input_tokens = st.last_input_tokens;
    out.context_window    = st.context_window;
    out.text          = st.answer;
    out.duration_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();

    // 最具体的原因首先是:取消也显示为错误.
    if (st.cancelled || opts.cancel.stop_requested()) {
        out.status = S::Cancelled;
        out.error  = "cancelled";
    } else if (st.deadline_hit) {
        out.status = S::Timeout;
        out.error  = "hit the time limit before finishing";
    } else if (st.context_full) {
        out.status = S::ContextFull;
        out.error  = "context window full";
        if (st.context_window > 0)
            out.error += " (" + std::to_string(st.context_window) + " tokens"
                       + (st.last_input_tokens > 0
                              ? ", last prompt " + std::to_string(st.last_input_tokens)
                              : std::string{})
                       + ")";
        if (!st.last_error.empty()
            && st.last_error.rfind("context window full", 0) != 0)
            out.error += ": " + st.last_error;
        out.error += ". give the model a bigger context, or a smaller task";
    } else if (!st.last_error.empty()) {
        out.status = S::ProviderError;
        out.error  = st.last_error;
    } else if (st.budget_exhausted) {
        out.status = S::TurnLimit;
        out.error  = "hit the turn limit (" + std::to_string(st.turns) + ") before finishing";
    } else if (st.doomed) {
        out.status = S::Failed;
        out.error  = "the same tool call failed 3 times in a row";
    } else if (is_error) {
        out.status = S::Failed;
        out.error  = "the agent said it did not finish the task (after "
                   + std::to_string(st.turns) + " turns, "
                   + std::to_string(st.tool_errors) + " of "
                   + std::to_string(st.tool_calls) + " tool calls failed); its last "
                     "message is on stdout";
    } else if (out.text.empty()) {
        // 跑者的规则不是错误,而是剧本什么也得不到.
        out.status = S::Failed;
        out.error  = "finished without a text answer";
    }
    // 从未进入循环的运行( 坏配置) 仅有报告。
    if (!out.ok() && out.error.empty()) out.error = report;
    return out;
}

// ── 主动检索(SOTA 活动-RAG /FLARE / Self-RAG) 互联网档案馆的存檔,存档日期2013-09-02. ────────────
namespace {

// TRUE 当 内存 记录在后面 `mem_path` ("memory://<scope>/<id>"
// )是(这是) ALREADY 系统中的 提示词's <learned-memory>
// 块状——再次通过 <retrieved-context> 将使用上下文符号 字
// 节 模型已经可以看见。 传输层 '准确的选中( E) load_recent_
// * → select_for_prompt 和。 和 简 况 缓存 编号集,
// 由相同的(大小,时间) stat = RAG 时代用法.
bool memory_fact_in_prompt_(const std::string& mem_path) {
    auto slash = mem_path.rfind('/');
    if (slash == std::string::npos || slash + 1 >= mem_path.size()) return false;
    const std::string id = mem_path.substr(slash + 1);

    struct PromptIds { std::uint64_t stamp = ~0ULL; std::unordered_set<std::string> ids; };
    // 当内存文件更改时替换了整个:一个快照, no 锁.
    static maya::snapshot<PromptIds> cache;

    std::uint64_t now_stamp = 1469598103934665603ULL;
    auto mix = [&now_stamp](std::uint64_t v) {
        now_stamp = (now_stamp ^ v) * 1099511628211ULL;
    };
    for (auto scope : {memory::Scope::User, memory::Scope::Project}) {
        std::error_code ec;
        auto p = memory::path_for(scope);
        if (p.empty()) continue;
        mix(static_cast<std::uint64_t>(
            fs::exists(p, ec) ? fs::file_size(p, ec) : 0));
        auto t = fs::last_write_time(p, ec);
        mix(ec ? 0ULL
               : static_cast<std::uint64_t>(t.time_since_epoch().count()));
    }

    // 新鲜:来自 缓存. 过期 : 重新装入到 锁.
    if (const auto c = cache.load(); c->stamp == now_stamp) return c->ids.count(id) > 0;

    std::unordered_set<std::string> ids;
    for (auto load : {&memory::load_recent_user, &memory::load_recent_project}) {
        auto picked = memory::select_for_prompt(load());
        for (const auto& r : picked.records) ids.insert(r.id);
    }
    const bool in = ids.count(id) > 0;
    cache.store(PromptIds{now_stamp, std::move(ids)});
    return in;
}

// 十字记 -turn 数据 -duplication 费用 PROACTIVE 注射,
// 没有这个,稳定高 -confidence 实体 -injects
// 编号 相同 段落进入 <retrieved-context>
// 每一转弯 线程 - 纯背景 -window 用于零新信息(模型
// 已经看到它们最后转弯)。 我们记得最近键( 来源: path:
// line) -injected 限定的段落 FIFO 跳过我们之前浮出
// 水面的 主动注射只是 花在了标语上 模型还没有显示
//
// 被绑了这么久 线程 无法无限制地生长它; 一旦一个关键年龄从
// 窗口里长出来,它就会变长 MAY 改为 -injected ,
// 这是正确的行为(它又相关又长 -since 卷出模型的注意力).
//
// SPLIT 进入到窥视( E) proactive_seen_ ) 并作出承诺( proactive_mark_injected_):
// proactive_retrieve 将候选块建在 WORKER
// 打电话者可以 ABANDON 如果漏斗同时检查 和 已记录的密钥,
// 已废弃 工作线程 将标志着段落 "injected" 用户
// 从未看到 —— 永久压制它们。 因此, 漏斗只限 PEEKS ,
// 而呼叫者 将幸存的钥匙 仅 当它真的返回击球 传输。
// 双方共享一个 mutex/FIFO.
namespace {
struct ProactiveDedup {
    std::unordered_set<std::string> seen;
    std::deque<std::string>         fifo;
};
// A 快照 :每一次偷看候选人的通道,一次就按几把钥匙。
// 锁-free ,所以弃置 -worker 偷看永远挡不住一个承诺
maya::snapshot<ProactiveDedup>& proactive_dedup_() {
    static maya::snapshot<ProactiveDedup> d;
    return d;
}
constexpr std::size_t kProactiveDedupMax = 256;
}

// PEEK : 真实,如果 `key` 已在此会话中注入。 不 记录下来
bool proactive_seen_(const std::string& key) {
    return proactive_dedup_().load()->seen.count(key) > 0;
}

// COMMIT : 记录 `key` 输入( 输入) FIFO 驱逐)。 幂等.
void proactive_mark_injected_(const std::string& key) {
    (void)proactive_dedup_().update([](const ProactiveDedup& cur, const std::string& k) {
        if (cur.seen.count(k)) return cur;
        ProactiveDedup d = cur;
        d.seen.insert(k);
        d.fifo.push_back(k);
        while (d.fifo.size() > kProactiveDedupMax) {
            d.seen.erase(d.fifo.front());
            d.fifo.pop_front();
        }
        return d;
    }, key);
}

} // 命名空间结束。

// 运行search_docs工具使用的SAME AgenttyDocRetriever,
// 但出局时——在模型看到转弯前——只有信心清除HIGH栏时(
// 高于工具的LOW楼:我们花费用户的上下文-window令牌没有速率,
// 所以击打必须读出UNPROMPTED注射的置信栏,
// 工具的LOW楼是0.25;我们只注射远高于它.
// Tunble通过AGENTTY_RAG_PROACTIVE_MIN.
namespace {
double proactive_min_conf_() {
    // 信心已经到来 CRAG's CALIBRATED 相关性
    // 级别(一个真正的模型) -free 评估员,不是老生化分数。
    // CRAG 级数为真正的 -relevant 检索到~0.3–0.6带,
    // 所以未发速的注射棒默认为0.35 — 高度足够跳过噪音,
    // 低到足够固态 -relevant 击中模型。
    //
    // 真相来源于寻人者 LIVE 配置( 哪个) 归并 坚持到底 RAG
    // 选择面板 设置。一个明确 AGENTTY_RAG_PROACTIVE_MIN
    // 仍然赢,所以贝壳的重置被尊重 -off 运行。
    double min_conf = 0.35;
    try { min_conf = shared_retriever().snapshot_config().proactive_min_conf; }
    catch (...) { /* 保持默认 */ }
    if (const char* mc = std::getenv("AGENTTY_RAG_PROACTIVE_MIN"); mc && mc[0]) {
        // 任何非 -negative 值被尊重; 超过 1.0 条是
        // 合法的 "never inject" 切换(信心被夹到[0,1]).
        try { double v = std::stod(mc); if (v >= 0) min_conf = v; }
        catch (...) { /* 保留 config/default */ }
    }
    return min_conf;
}

// 主动检索漏斗 正文 :运行回收机, 门在信心, 建造围栏
// <retrieved-context> 块, PEEK
// 十字架 -turn 脱下并收集击球手的遗存钥匙。
// 不 输入解开的密钥—— 只有在实际发生时才被调用
// returns/injects 被废弃或丢弃的跑步 永远无法压制从未显示的通道
// 永远不要扔(最好) -effort).
std::optional<ProactiveHit>
run_proactive_funnel_(const std::string& query, int k, double min_conf) {
  try {
    AgenttyDocRetriever r;
    mt::DocQuery q;
    q.query = query;
    q.k     = k > 0 ? k : 3;
    std::string mode, err;
    // 主动检索是明确的选择 -in 已经运行在孤立的 工作线程 ,
    // 所以使用完整索引一次。旧的寒冷 -index 快捷键只能
    // 默默搜索 skills/memory 并错过引发知识的文档
    // -shaped 查询。
    auto passages = r.retrieve(q, mode, err);
    if (!err.empty() || passages.empty()) return std::nullopt;

    // 恢复管道计算出的信心(模式载运)
    // ", confidence 0.NN" ) 如果我们不能 解析 保守一点,
    // 别注射
    double conf = -1.0;
    if (auto p = mode.find("confidence "); p != std::string::npos) {
        try { conf = std::stod(mode.substr(p + 11)); } catch (...) {}
    }
    if (conf < min_conf) return std::nullopt;

    // 构建 传输 栅栏+出处 -labelled
    // 所以模型把它当作检索到的参考文献,而不是用户的词。
    std::string block =
        "<retrieved-context>\n"
        "The following passages were auto-retrieved from the user's "
        "knowledge base (docs/skills/memory) because they look relevant "
        "to the request. Ground your answer in them where they apply; "
        "ignore any that don't. Cite the source path when you use one.\n\n";
    int n = 0;
    std::vector<std::string> keys;
    for (const auto& p : passages) {
        // CONTEXT-ECONOMY: 内存 系统中已经提供的事实
        // 提示词's <learned-memory> 块会纯双倍 -spend
        // - 模型可以看见他们。 select_for_prompt
        // () 决定 渲染 ; 镜像在这里, 并丢弃任何 内存 记录的
        // 段落 提示词 切开。 ( Docs/skills/MCP 系统
        // 里从来没有过段落 提示词 ——始终保留. )
        if (p.source == "memory" && memory_fact_in_prompt_(p.path))
            continue;

        // CROSS-TURN DEDUP :
        // 别重来 -inject 用于零新信号的上下文。
        // 同一文件的源:path:line的大小
        // 差异将分别处理。 PEEK 只有这里——
        // 呼叫者在实际注入区块时,会将幸存的钥匙投入.
        std::string key = (p.source.empty() ? std::string{"docs"} : p.source)
                        + ":" + p.path + ":" + std::to_string(p.line_start);
        if (proactive_seen_(key))
            continue;
        keys.push_back(key);

        block += "[" + (p.source.empty() ? std::string{"docs"} : p.source)
               + ":" + p.path;
        if (p.line_start > 0)
            block += ":" + std::to_string(p.line_start);
        block += "]\n";
        block += p.text;
        if (!p.text.empty() && p.text.back() != '\n') block += '\n';
        block += '\n';
        ++n;
    }
    block += "</retrieved-context>";

    if (n == 0) return std::nullopt;   // 都脱光了

    // 独立上限 UNPROMPTED。用户没有要求此块, 因此即使检索
    // 器返回丰富的段落, 也必须保持便宜。 将整块块抓上;
    // 默认为 ~6KiB (~1.5k tok), 大约一半的 -demand
    // 工具预算。
    // AGENTTY_RAG_PROACTIVE_BYTES.
    {
        std::size_t cap = 6 * 1024;
        try {
            int cfg_bytes = shared_retriever().snapshot_config().proactive_bytes;
            if (cfg_bytes > 0)
                cap = std::clamp<std::size_t>(static_cast<std::size_t>(cfg_bytes),
                                              1024, 32 * 1024);
        } catch (...) {}
        if (const char* v = std::getenv("AGENTTY_RAG_PROACTIVE_BYTES"); v && v[0]) {
            try { cap = std::clamp<std::size_t>(std::stoull(v), 1024, 32 * 1024); }
            catch (...) {}
        }
        if (block.size() > cap) {
            std::size_t cut = cap;
            // 调整到一个 UTF -8号边界,然后是 -close 围栏干净。
            while (cut > 0 && (static_cast<unsigned char>(block[cut]) & 0xc0) == 0x80)
                --cut;
            block.resize(cut);
            block += "\n…\n</retrieved-context>";
        }
    }
    return ProactiveHit{std::move(block), conf, n, std::move(keys)};
  } catch (...) {
    return std::nullopt;   // 主动检索是最佳的-effort, 绝不致命
  }
}
} // 命名空间结束。

// 兼容性入口。 主动检索现在已选择 -in 提交
// 路径总是拥有一个孤立的 工作线程 因此没有理由启动
// 第二个脱落的套期。执行一次漏斗。
std::optional<ProactiveHit> proactive_retrieve(const std::string& query, int k) {
    return proactive_retrieve_blocking(query, k);
}

// 应用程序拥有的孤立的 工作线程 的完整漏斗。 只有当块被
// 实际返回注入时, 才会提交解开键。
std::optional<ProactiveHit>
proactive_retrieve_blocking(const std::string& query, int k) {
    auto hit = run_proactive_funnel_(query, k, proactive_min_conf_());
    if (hit)
        for (const auto& key : hit->dedup_keys)
            proactive_mark_injected_(key);
    return hit;
}

namespace subagent {
// 任务卡的验证查询( 声明于 subagent.hpp。通过 相同 查找
// 运行器所用的, 然后将其来源映射到一个稳定的标签。 未知 ~
// "builtin" (没有显示标签)。 定义在结尾 -of-TU 所以它
// 不会遮蔽 subagent::current() 调用 用于文件的较早部分。
std::string_view agent_origin(Io, std::string_view name) noexcept {
    const ResolvedAgent r = resolve_agent_type(name);
    switch (r->origin) {
        case AgentOrigin::User:    return "user";
        case AgentOrigin::Project: return "project";
        case AgentOrigin::Builtin: break;
    }
    return "builtin";
}
}  // namespace subagent

} // namespace agentty::tools
