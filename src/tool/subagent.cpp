#include "agentty/tool/subagent.hpp"
#include "agentty/util/teardown.hpp"

#include "agentty/util/sendable.hpp"   // maya::guarded + 选择 -ins 生活需要

#include <algorithm>
#include <mutex>   // std::once_flag
#include <optional>
#include <stop_token>
#include <vector>

namespace agentty::tools::subagent {

namespace {
// 已安装的配置。 整个配置由 current() 改为:
// install() ; 设置器在用户切换时一次更改一个字段。
// 复制 -on-write 在一个已公布的插槽中,而不是 锁:
// set_auth 是从 副作用 主机的 Auth 交换和 current()
// 从每个子剂启动时, 都不需要一个被替换的值的能力,
// 也从来不会变异。 每个定点器都是一个比较器。 -and-swap.
// (Config持有可调用设备,所以它不能是一个 jaal::snapshot - 这些需要证明
// -immutable T; 已出版 <const Config> 持有 不可变 ) 存档副本.
maya::published<const Config>& cfg() {
    static maya::published<const Config> c;
    return c;
}

// 读 -modify-write: f( 合并) , 仅以无任何文本的形式互换
// else swapped first; retried otherwise. f must have no side effects.
template <class F>
void edit(F f) {
    for (;;) {
        auto cur  = cfg().current();
        auto next = std::make_shared<Config>(cur ? *cur : Config{});
        f(*next);
        if (cfg().publish_if(cur, std::shared_ptr<const Config>(std::move(next)))) return;
    }
}

} // 命名空间结束。

void install(Config c) {
    c.installed = true;
    cfg().publish(std::make_shared<const Config>(std::move(c)));
}

Config current() {
    auto c = cfg().current();
    return c ? *c : Config{};
}

void set_auth(Io, auth::AuthHeader auth) {
    edit([&](Config& c) { if (c.installed) c.auth = auth; });
}

void set_model(Io, std::string model) {
    if (model.empty()) return;
    // 只有一个配置存在时才有意义; 使 `installed` 不受影响。
    edit([&](Config& c) { c.model = model; });
}

void set_candidates(Io, std::vector<ModelInfo> candidates) {
    edit([&](Config& c) { if (c.installed) c.candidates = candidates; });
}

void set_smart(Io, smart::RoleConfig smart) {
    edit([&](Config& c) { if (c.installed) c.smart = smart; });
}

void set_provider(Io, std::string provider) {
    edit([&](Config& c) { if (c.installed) c.provider = provider; });
}



// ── 运行-run 注册表 ─────────────────────────────────────────
// 见 头文件 为何存在它? maya::stop_group:
// 碞琌硂 注册表 此文件用于手递 -roll (a 互斥量 ,
// 条件变量,矢量为 { 已取消 ,完成)对和一个等待,
// 重新扫描每个条目在每个醒悟,写在一次 jaal.
//
// 或 jaal 类型在删除代码后添加 :
//   * ADMISSION CLOSES 关闭。 运行从中点开始。 -teardown 曾经是
//     注册后 shutdown_running 已经拿走了 快照 所以它
//     从未被要求停下来 也从未等待过 join() 现在拒绝它。
//   * 报告内容 取消 是个真实的 std::stop_token ,所以,请 数据流's 取消 可以是: 一种
//     stop_callback 而不是一个 线程 投票a 标志 每20毫秒 时间轴:

struct RunRegistration::State {
    std::optional<maya::stop_group::member> member;
};

namespace {

maya::stop_group& runs() {
    static maya::stop_group g;
    return g;
}

// 从第一次开始就把脱落的钩子登记一次
// 这是 注册表 自己的规则( util/teardown.hpp : 加入与
// 创建 线程 ,而不是在手边 -maintained 列表中 main().
void ensure_teardown_registered() {
    static std::once_flag once;
    maya::once(once, [] {
        util::teardown::on_shutdown("subagent.running", [](const maya::unlocked& cap) {
            (void)shutdown_running(cap);
        });
    });
}

} // 命名空间结束。

RunRegistration::RunRegistration()
    : state_{std::make_shared<State>()} {
    ensure_teardown_registered();
    state_->member = runs().join();
}

// 成员离开时会受到破坏,这唤醒等待关闭的状态.
RunRegistration::~RunRegistration() = default;

bool RunRegistration::cancelled() const noexcept {
    // 一次拒绝录取(shutdown already running)
    // 读作已取消,所以它停止了第一次检查,而不是做没有人等待的工作.
    return !state_->member || state_->member->stop_requested();
}

std::stop_token RunRegistration::token() const noexcept {
    if (state_->member) return state_->member->token();
    // 拒绝: 将一个已经 -stopped 的代号还回去, 所以呼叫者不需要分机。
    std::stop_source dead;
    dead.request_stop();
    return dead.get_token();
}

std::size_t shutdown_running(const maya::unlocked& cap, std::chrono::milliseconds grace) noexcept {
    // 已设界而非屏障:一个楔形的线程不能打开进程。
    // 运行的触摸提供者对象会 main() 活过这个 调用 ,
    // 所以弃案是一个缓慢的退出,而不是一个令人费解的写作.
    try {
        return runs().stop_and_wait(cap, grace);
    } catch (...) {
        return 0;   // a 拆解时的互斥量失败不值得终止
    }
}

} // namespace agentty::tools::subagent
