#pragma once
// agentty::util::dbglog-吞咽-error路径的opt-in诊断日志
// (SECURITY_AUDIT-adjacent：静默`catch(...) {}`
// 站点隐藏损坏并使现场错误不可重现)。
//
// agentty拥有终端，因此裸露的fprintf(stderr，...)
// 会在TUI上乱写。相反，它会转发到 logx（通用通道），
// 它将带时间戳的行附加到诊断日志中。当该通道被过滤掉时（每次
// 正常运行的默认设置），调用 是几个便宜的 加载 并返回：没有
// 文件打开，没有格式化，零行为变化。 AGENTTY_DEBUG_LOG
// 不见了；捕获为 AGENTTY_LOG，
// 目标为 `--log-file`。
//
// 在先前-silent捕获地点的使用：
//     } catch (const std::exception&e) {
//         util::dbglog("persistence.save", e.what());
//     } 抓住 (...) {
//         util::dbglog("persistence.save", "non-std exception");
//     }
//
// 要点是可诊断性，NOT改变控制流：每个站点保持其现有的最佳
// -effort恢复。我们不再扔掉
// "what happened"。

#include <exception>
#include <string_view>

namespace agentty::util {

// 正确，当且仅当底层 logx 接收器将保留 General-channel 行。
// 在第一个调用之后被缓存。让热门捕获站点在日志记录
// 关闭时跳过构建消息字符串：
//   `if (dbglog_enabled()) dbglog(where, expensive())`。
bool dbglog_enabled() noexcept;

// 将 "<ISO-ish timestamp> [where] msg\n" 附加到诊断日志。
// 当禁用日志记录时，No-op(并且不触及文件系统)。
// 线程-safe:由内部互斥量序列化。
void dbglog(std::string_view where, std::string_view msg) noexcept;

// catch块的便利性：在`where`下记录`e.what()`。与非 -std 臂的尾随
// `catch (...) { dbglog(where, "non-std exception"); }`
// 配对。
inline void dbglog_ex(std::string_view where, const std::exception& e) noexcept {
    dbglog(where, e.what());
}

} // namespace agentty::util
