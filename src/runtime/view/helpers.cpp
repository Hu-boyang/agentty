#include <cstdlib>
#include <limits>
#include "agentty/runtime/view/helpers.hpp"

#include <concepts>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <variant>
#include <vector>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/model_name.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui {

maya::Color profile_color(Profile p) noexcept {
    // 每个剖面图都有一个 独特的,饱和的特性色调。 Minimal
    // 曾经是 渲染 输入 `muted` 留下了一种没有状态的
    // 感觉 而不是一个刻意的选择 bright_cyan 声称自己的身份,
    // 而不与 Write (马格塔语)或 Ask (蓝色)
    // 所有三个简介 标签 现在带一个真正的颜色。
    switch (p) {
        case Profile::Write:   return role_brand;   // 红金色
        case Profile::Ask:     return role_info;    // 蓝色
        case Profile::Minimal: return code_path;    // bright_cyan
    }
    return fg;
}

std::string_view phase_glyph(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> std::string_view {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, phase::Idle>)               return "●";
        else if constexpr (std::same_as<T, phase::Streaming>)          return "◐";
        else if constexpr (std::same_as<T, phase::AwaitingPermission>) return "⚠";
        else                                                           return "▶";
    }, p);
}

std::string_view phase_verb(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> std::string_view {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, phase::Idle>)               return "Ready";
        else if constexpr (std::same_as<T, phase::Streaming>)          return "Streaming";
        else if constexpr (std::same_as<T, phase::AwaitingPermission>) return "Awaiting";
        else                                                           return "Running";
    }, p);
}

maya::Color phase_color(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> maya::Color {
        using T = std::decay_t<decltype(v)>;
        // 使用亮度( L) ANSI 变体 用于活动阶段,从而进行脉冲 加
        // 载动画 状态中 -bar 标签 读作 "alive" 在每个调色板上,
        // 不只是高 -contrast 暗主题。
        //
        // 这些都是明亮的,因为 `highlight` 财务报告和已
        // 审计财务报表 `success` 降落在几个光主题的脱饱和端.
        // 符号是现在的主题,所以主题本身就回答:——
        // 这里的字面意思是,在别人的调色板上,
        // 它总是一样的赌注,只是朝着另一个方向。
        if      constexpr (std::same_as<T, phase::Idle>)               return muted;
        else if constexpr (std::same_as<T, phase::Streaming>)          return status_info;
        else if constexpr (std::same_as<T, phase::AwaitingPermission>) return status_warn;
        else                                                           return status_ok;
    }, p);
}

std::string small_caps(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        out.push_back(static_cast<char>(
            (c >= 'a' && c <= 'z') ? (c - 32) : c));
        if (i + 1 < s.size()) out.push_back(' ');
    }
    return out;
}

std::string tabular_int(int n, int width) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%*d", width, n);
    return buf;
}

std::string format_elapsed_5(float secs) {
    // 每个 分支必须生产 恰好 5 显示列 –
    // 此标签位于每个宽度变化的状态栏中。
    // 帧 预算按数量分列:
    //   <   10 s  → " 3.4s"   ( 1 + 3 + 1 = 5 )
    //   <  100 s  → "12.3s"   ( 4 + 1     = 5 )
    //   <  600 s  → " 234s"   ( 4 + 1     = 5 )
    //   < 600 m   → "59m9s" 需要我们 "mm'ss" 样式
    //                 —— 选择 " 9m5s" / "59m5s" (总是5个字符,
    //                 秒作为单位数四舍五入,1–2位数).
    //   其它的 —— —— (中文) " >1hr"
    char buf[16];
    if (secs < 0.0f) secs = 0.0f;
    if      (secs <   10.0f)  std::snprintf(buf, sizeof(buf), " %.1fs", static_cast<double>(secs));
    else if (secs <  100.0f)  std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(secs));
    else if (secs <  600.0f)  std::snprintf(buf, sizeof(buf), "%4ds", static_cast<int>(secs));
    else if (secs < 3600.0f) {
        int m = static_cast<int>(secs) / 60;
        int s = (static_cast<int>(secs) / 10) % 6;   // 10秒, 0–5 秒( 秒)
        // "%2dm%ds" → e.g. " 9m3s" 或 或 时 间 "59m4s" - 总是5科拉。
        std::snprintf(buf, sizeof(buf), "%2dm%ds", m, s);
    }
    else                      std::snprintf(buf, sizeof(buf), " >1hr");
    return buf;
}

std::string format_duration_compact(float secs) {
    char buf[24];
    if      (secs < 1.0f)
        std::snprintf(buf, sizeof(buf), "%.0fms", static_cast<double>(secs) * 1000.0);
    else if (secs < 60.0f)
        std::snprintf(buf, sizeof(buf), "%.1fs",  static_cast<double>(secs));
    else {
        int   mins = static_cast<int>(secs) / 60;
        float rest = secs - static_cast<float>(mins * 60);
        std::snprintf(buf, sizeof(buf), "%dm%.0fs", mins, static_cast<double>(rest));
    }
    return buf;
}

std::string pretty_model_label(std::string_view id) {
    // 瘦代表,以前住在这里的每条规则(家族分类学,版本提取, 快照-date
    // 财务报告和已审计财务报表 `:latest` 脱衣舞 Ollama
    // 标记处理,缩写外壳)现在生活在域中 SSOT 轮到你们了 头文件 ,
    // 作曲家(英语: 标签 ,欢迎屏幕和 选择面板 不可异见。
    // domain/model_name.hpp.
    return model_name::decode(id).full();
}

std::string model_display_label(std::string_view id,
                                std::string_view display_name) {
    // 瘦代表——解码器拥有id-vs-server-name对调.
    return model_name::decode(id, display_name).full();
}

std::string model_display_label_qualified(std::string_view id,
                                          std::string_view display_name) {
    // 相同的标签, ID 带有一个时, 与路由命名空间的前缀。
    // 对于。 选择面板 仅碰撞大小写: 由 : LANE (
    // `dgpu/x` 离散时 GPU,
    // `utils/x` 打开 CPU ),这些解码到同一标签。
    return model_name::decode(id, display_name).qualified();
}

std::string timestamp_hh_mm(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

std::string timestamp_full(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    // "Jan 14 09:15" - 12个可见的圆锥形,已固定 -width
    // 因为我们手握手,所有地方 -format 从小表格中而不是字符串%b(
    // 属于本地语区) 中选择月份 -dependent 并且可以生产3或
    // 4 -character 在一些地方的缩写).
    static constexpr const char* kMonth[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"};
    const int m = (tm.tm_mon >= 0 && tm.tm_mon < 12) ? tm.tm_mon : 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s %2d %02d:%02d",
                  kMonth[m], tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

std::string utf8_encode(char32_t cp) {
    std::string out;
    auto u = static_cast<uint32_t>(cp);
    if (u < 0x80) {
        out.push_back(static_cast<char>(u));
    } else if (u < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (u >> 6)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    } else if (u < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (u >> 12)));
        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (u >> 18)));
        out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    }
    return out;
}

std::string context_window_label(int tokens) {
    if (tokens <= 0) return "auto";
    if (tokens >= 1'000'000) {
        std::string s = std::to_string(tokens / 1'000'000) + "M";
        if (tokens % 1'000'000 != 0) s += "+";
        return s;
    }
    if (tokens >= 1000) return std::to_string(tokens / 1000) + "k";
    return std::to_string(tokens);
}

std::string context_override_key(std::string_view provider_id,
                                std::string_view model_id) {
    // Tab-separated ,匹配最近 -models MRU。
    // 一个模型 ID 中可以包含 '/' (开路器)和 ':' 所以
    // 一个标签是没有ID的分隔符
    std::string k;
    k.reserve(provider_id.size() + model_id.size() + 1);
    k.append(provider_id);
    k.push_back('\t');
    k.append(model_id);
    return k;
}

int max_context_tokens_from_env() noexcept {
    const char* env = std::getenv("AGENTTY_MAX_CONTEXT_TOKENS");   // launch-env: 仅由 read_launch_env 在启动时读取一次
    if (!env || !*env) return 0;
    char* end = nullptr;
    const long v = std::strtol(env, &end, 10);
    if (end && *end == '\0' && v > 0 && v <= std::numeric_limits<int>::max())
        return static_cast<int>(v);
    return 0;
}

int resolve_context_window(std::string_view provider_id,
                           std::string_view model_id,
                           int advertised,
                           const store::Settings& settings,
                           int env_max) noexcept {
    // FIVE 命令是整个设计,所以这里是说明的,
    // 而不是从下面的代码推断出来的。
    //
    //   1. 用户覆盖每-model, 他们配置了网关
    //   2. 现场广告 API 谓 TRUTH 只有400块能溢出
    //   3. models.dev 7824型号的实名声明
    //   4.id推断 Claude/GPT 家庭,[1m]后缀
    //   5. env / 200k 默认无任何信息
    //
    // 跑步队2 -4是 AUTOMATIC 占总金额的95% agentty
    // 船舶:与活体相比 目录 ,打开路由器宣布窗口 368/368 模型,
    // 谷歌 39/39 人类 14/14 打开 43/48 ,
    // xai 语句 12/12. 手动挂载符存在,用于大小写否
    // 目录 ——服务于无人公布的id的私人网关——而且应该非常罕见,
    // 以至于大多数用户从未遇到过。

    // 1. 用户的超度会直接获胜,它们配置了网关;
    //    我们无法推断出什么比这更好的证据。
    try {
        const auto it =
            settings.context_overrides.find(context_override_key(provider_id, model_id));
        if (it != settings.context_overrides.end() && it->second > 0)
            return it->second;
    } catch (...) {
        // 检查绝不能掉头;只能推论。
    }

    // 2. 终点告诉我们的,从探测器或v1/models
    //    列。 这比每个静态源都高, 并且总是会:
    //    两个网关后面的相同模式名可以用两个不同的
    //    窗口来服务, 只有网关知道哪些适用于我们
    //    即将发送的请求, 只有网门可以拒绝我们溢出。
    //    第三, -party 描述模型的元数据
    //    不能推翻描述主机 此 部署。
    if (advertised > 0) return advertised;

    // 3. models.dev 声明。 -Claude 模型
    //    诚实的。
    //
    //    from_id ()以下回答已知家庭的200k,
    //    另回答0-正确: Claude 也就是它解码
    //    的家庭 几乎错失了其他一切 快照 :7824型
    //    中的2698型有1M或以上的真窗口,
    //    每个都以0达到这个功能,而以200k默认值离开.
    //    格米尼-2.5-pro(1M),gpt-4.1(1M) qwen3
    //    -coder-plus(1M)、ellama-4-scout(10M)——都少写,
    //    都比需要的要快几次压缩.
    //
    //    由提供者先进行范围检查,因为不同的主机以不同大小真正
    //    提供同样的赤色ID;一个十字架 -provider
    //    分歧毒害共享密钥 "no declaration"
    //    而不是让一个宿主的身影流到另一个宿主身上
    //
    //    下方 现场广告和 上方 猜想,
    //    就是这个: 比猜想好,比真相差。
    if (const int w = catalog_context_window_for(model_id, provider_id); w > 0)
        return w;
    //    终点 -origin 声明: CUSTOM-HOST
    //    接口边界。一个会话的提供者 id 是结束点的光谱本身,
    //    上面的搜索范围永远无法解决——但是 models.dev
    //    声明条目 API URL ,因此同一声明可以按出处键。
    //    端口是一个用户定义,而不是身份(loopback
    //    条目在用户选择的端口中直播);第二个限定符在几个相同的
    //    端口之间 -host 条目是模型 id 本身,
    //    不同意 ^ 没有权利要求。 仅在被瞄准后在这里查阅,
    //    因为预设的自身身份比它高。 URL's.
    {
        std::uint16_t port = 0;
        const std::string host =
            context_endpoint_origin(provider_id, port);
        if (!host.empty())
            if (const int w = endpoint_context_window_for(model_id, host,
                                                          port);
                w > 0)
                return w;
    }
    if (int w = catalog_context_window_for(model_id); w > 0)
        return w;

    // 4. 调查编号。 Claude/GPT 家族是众所周知的, `[1m]` 迫使广大的
    //    窗口,未知家庭报告 0。
    if (const int w = ModelCapabilities::from_id(model_id).context_window(); w > 0)
        return w;

    // 5. 明确阐述不可知案件,然后是保守派
    //    默认。
    //
    //    AGENTTY_MAX_CONTEXT_TOKENS 是
    //    一个私门的逃生舱门: id no 目录 每一个严肃的客户都
    //    保留其中之一,因为发现可能无法覆盖这个案例——Zed要求
    //    max_tokens 输入 settings.json 费用
    //    OpenAI-compatible 终点, Claude 代码
    //    CLAUDE_CODE_MAX_CONTEXT_TOKENS。
    //    此处的拼写刻意反射 Claude 密码是,它可以被任何人 谁曾经打过
    //    这个。
    //
    //    最后 ,而不是首先:它是全球性的,因此,
    //    如果一个会话能够达到几个模型,那么所有模型都有一个数字。
    //    它只有在没有更好的结果时才会说话。
    //    发射时读一次( 3) Model::Env::max_context_tokens ; 输入错误
    //    解析 改为 0, i.e . 未设.
    if (env_max > 0) return env_max;

    // 保持保守: 结束 -estimate 打开失败 传输 下,
    // 一个下 -estimate 只能用比它更早的紧凑
    return kDefaultContextWindow;
}

void bake_context_window(ModelInfo& row,
                         std::string_view provider_id,
                         const store::Settings& settings,
                         int env_max) noexcept {
    // 广告的目前值为IS(0=无报告),
    // 所以输入为rung 2,并随阶梯返回。
    row.context_window =
        resolve_context_window(provider_id, row.id.value,
                               row.context_window, settings, env_max);
}

int context_max_for_model(std::string_view model_id) noexcept {
    // 编号 -only 路径,用于 调用 不存在的站点 目录 或手头的设置。
    // resolve_context_window (a) 两者均备有之处。
    const int w = ModelCapabilities::from_id(model_id).context_window();
    return w > 0 ? w : kDefaultContextWindow;
}

int utf8_prev(std::string_view s, int byte_pos) noexcept {
    if (byte_pos <= 0) return 0;
    int p = byte_pos - 1;
    while (p > 0 && (static_cast<uint8_t>(s[p]) & 0xC0) == 0x80) --p;
    return p;
}

int utf8_next(std::string_view s, int byte_pos) noexcept {
    int n = static_cast<int>(s.size());
    if (byte_pos >= n) return n;
    int p = byte_pos + 1;
    while (p < n && (static_cast<uint8_t>(s[p]) & 0xC0) == 0x80) ++p;
    return p;
}

int chip_prev(std::string_view s, int byte_pos) noexcept {
    if (byte_pos <= 0) return 0;
    // 占位符的关闭监视器 byte_pos - 跳过整个标志一步。
    // attachment::placeholder_len_ending_at
    // 返回 0,当 字节 不可形成有效的占位符,所以, 回退 改为
    // utf8_prev 保持不为 -placeholder 案件不变。
    if (auto len = attachment::placeholder_len_ending_at(
            s, static_cast<std::size_t>(byte_pos)); len > 0) {
        return byte_pos - static_cast<int>(len);
    }
    return utf8_prev(s, byte_pos);
}

int chip_next(std::string_view s, int byte_pos) noexcept {
    int n = static_cast<int>(s.size());
    if (byte_pos >= n) return n;
    if (auto len = attachment::placeholder_len_at(
            s, static_cast<std::size_t>(byte_pos)); len > 0) {
        return byte_pos + static_cast<int>(len);
    }
    return utf8_next(s, byte_pos);
}

namespace {
// 自然三号 -way 比较两个标签:将每个标签分为交替非标签 -digit
// / 数字块和比较块 -by-chunk 数字块为 INTEGERS
// (这样) "10" > "2" ),无 -digit 块大小写
// -folded 逻辑上,领先的无 -digit 块是家族; 第一个数块是世代等。
// 返回 <0 / 0 / >0.
[[nodiscard]] int natural_cmp(std::string_view a, std::string_view b) noexcept {
    auto lower = [](char c) -> char {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i]));
        const bool db = std::isdigit(static_cast<unsigned char>(b[j]));
        if (da && db) {
            // 比较数字运行为整数:跳过领先的零,
            // 然后更长的运行胜;等长 = 字形胜于位数.
            std::size_t ia = i, jb = j;
            while (ia < a.size() && a[ia] == '0') ++ia;
            while (jb < b.size() && b[jb] == '0') ++jb;
            std::size_t ea = ia; while (ea < a.size() && std::isdigit((unsigned char)a[ea])) ++ea;
            std::size_t eb = jb; while (eb < b.size() && std::isdigit((unsigned char)b[eb])) ++eb;
            const std::size_t la = ea - ia, lb = eb - jb;
            if (la != lb) return la < lb ? -1 : 1;
            for (std::size_t k = 0; k < la; ++k)
                if (a[ia + k] != b[jb + k])
                    return a[ia + k] < b[jb + k] ? -1 : 1;
            // 提前通过FULL位数运行(包括零)
            i = ea; j = eb;
            while (i < a.size() && std::isdigit((unsigned char)a[i])) ++i;   // ea 之后的第-op号
            while (j < b.size() && std::isdigit((unsigned char)b[j])) ++j;
        } else if (da != db) {
            // 在同一位置字母之前的数类 "gpt-4" 财务报告和已审计
            // 财务报表 "gpt-4o" 组前 "gpt-image".
            return da ? -1 : 1;
        } else {
            const char ca = lower(a[i]), cb = lower(b[j]);
            if (ca != cb) return ca < cb ? -1 : 1;
            ++i; ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// 领先的非 -digit 运行, 大小写 -folded - 电话 "family" ("claude sonnet",
// "gpt", "gemini" 拖曳分离器被修剪过 "gpt-" == "gpt".
[[nodiscard]] std::string family_of(std::string_view s) noexcept {
    std::string out;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) break;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '-'
                            || out.back() == '_' || out.back() == '.'))
        out.pop_back();
    return out;
}
}  // 命名空间结束。

bool model_order_less(std::string_view a, std::string_view b) noexcept {
    // 1. 家庭ASCENDING(所有索内图,字母顺序为横
    //    ——分类外观.
    const std::string fa = family_of(a), fb = family_of(b);
    if (fa != fb) return fa < fb;
    // 2. 在家庭中, NEWEST 首次 自然比较 DESCENDING ,所以,我说
    //    更高的版本数浮动到顶部。
    const int c = natural_cmp(a, b);
    if (c != 0) return c > 0;
    // 3. 在自然比较下完全平等( e.g. "latest" 日期为
    //    与同型号别名)——为稳定结果而生的字节顺序.
    return a < b;
}

} // namespace agentty::ui
