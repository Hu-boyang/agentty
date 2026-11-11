#include "agentty/util/dbglog.hpp"

#include "agentty/util/logx.hpp"

namespace agentty::util {

// dbglog是一款COMPAT SHIM 超越logx(结构化的log).
//
// 每一个 调用 网站标记本身带有点缀的站点名称( Name "persistence.save",
// "anthropic.list_models.parse",
// "mcp.bridge.connect"。该前缀已经命名了子系统,因此,
// 它不是平整50+历史 调用 站点上线 General/Error 让他们无法过滤,
// 埋了一条 传输 设置之间的错误 -parse 噪音——通过前缀将每个通道通向其真实的通道.
//
// 这是对字符串的检索,它是 编译-time 字数 调用 站点,
// 它只在已经正在记录的路径上运行( i.e ——一个
// 实际发生的错误),所以在普通情况下不花费任何费用.
//
// 新代码应该 调用 AGT_LOG 直接使用明确的通道和级别;
// 存在此特性, 因此现有的吞咽 -exception 网站获得频道的路由,
// 没有50个可以每个输入一个字的机械编辑。
namespace {

[[nodiscard]] logx::Channel channel_for(std::string_view site) noexcept {
    struct Route { std::string_view prefix; logx::Channel ch; };
    // 秩序事项仅针对脱节;下面的前缀是独一无二的.
    static constexpr Route kRoutes[] = {
        {"anthropic.",   logx::Channel::Wire},
        {"openai.",      logx::Channel::Wire},
        {"ollama.",      logx::Channel::Wire},
        {"responses.",   logx::Channel::Wire},
        {"copilot.",     logx::Channel::Wire},
        {"kimi.",        logx::Channel::Wire},
        {"chatgpt.",     logx::Channel::Wire},
        {"modelsdev.",   logx::Channel::Wire},
        {"provider.",    logx::Channel::Wire},
        {"auth.",        logx::Channel::Auth},
        {"oauth.",       logx::Channel::Auth},
        {"keystore.",    logx::Channel::Auth},
        {"accounts.",    logx::Channel::Auth},
        {"persistence.", logx::Channel::Persist},
        {"settings.",    logx::Channel::Persist},
        {"thread.",      logx::Channel::Persist},
        {"memory.",      logx::Channel::Persist},
        {"checkpoint.",  logx::Channel::Persist},
        {"tool.",        logx::Channel::Tool},
        {"tools.",       logx::Channel::Tool},
        {"sandbox.",     logx::Channel::Tool},
        {"hooks.",       logx::Channel::Tool},
        {"skills.",      logx::Channel::Tool},
        {"plugin.",      logx::Channel::Mcp},
        {"mcp.",         logx::Channel::Mcp},
        {"acp.",         logx::Channel::Acp},
        {"rag.",         logx::Channel::Rag},
        {"http.",        logx::Channel::Net},
        {"tls.",         logx::Channel::Net},
        {"net.",         logx::Channel::Net},
        {"smart.",       logx::Channel::Smart},
        {"caps.",        logx::Channel::Model},
        {"salvage.",     logx::Channel::Model},
        {"dispatch.",    logx::Channel::Model},
        {"view.",        logx::Channel::Ui},
        {"render.",      logx::Channel::Ui},
        {"composer.",    logx::Channel::Ui},
        {"clipboard.",   logx::Channel::Ui},
    };
    for (const auto& r : kRoutes)
        if (site.size() >= r.prefix.size()
            && site.compare(0, r.prefix.size(), r.prefix) == 0)
            return r.ch;
    return logx::Channel::General;
}

} // 命名空间结束。

bool dbglog_enabled() noexcept {
    return logx::enabled(logx::Channel::General, logx::Level::Error);
}

void dbglog(std::string_view where, std::string_view msg) noexcept {
    // 错误级别: 每个站点都是一个吞入的例外或失败 解析
    // 程序从中恢复出来的真正的错误。 Warn+ 也进入
    // 崩溃 -time 飞行记录器,所以这些显示在一个坠
    // 机堆放 即便文件记录关闭。
    logx::emit(channel_for(where), logx::Level::Error, where, msg);
}

} // namespace agentty::util
