// agentty::tools::skills ——执行。 skills.hpp 执行
// 《公约》的情况 -disclosure 理由和发现 -root 表格中显示的表格。

#include "agentty/util/user_root.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/util/sendable.hpp"
#include "agentty/util/home_dir.hpp"

#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // kskillsLayout——单一宣言
#include "agentty/util/capped_read.hpp"   // 原始的封顶-read
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/util/dbglog.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

namespace agentty::tools::skills {

namespace fs = std::filesystem;

namespace {

// 后面的解决柜台 debug_cap_resolutions (见A/C.5/56/L.10和Add.1和2) skills.hpp).
// 原子因为 all() 谓 互斥量-guarded 却从测试中读
// 出柜台 线程 没有那个 锁.
std::atomic<std::size_t>& cap_resolutions() noexcept {
    static std::atomic<std::size_t> n{0};
    return n;
}

// ── 目录 上限: 默认 + env 覆盖 ────────────────────────
//
// kMax 技能( ưμ㼯A 头文件 )是默认的一级 目录 盖;
// AGENTTY_MAX_SKILLS 覆盖它, 对于小 -context
// 型号(a 100) -skill 库 就会吃这个系统 提示词 或
// 减 减 . 库 合法地超过违约。
//
// 被挤到[8,4096]:低于8: 目录 失去促成
// 上限的交易(列名值其上下文),超过4096年,
// 不再保护该系统 提示词。一个不正确的值会保存默认值(匹配)
// bridge.cpp 内存 解析 ),带有dbglog
// 面包屑.
//
// RESOLVED 一次 PER DISCOVERY PASS ,然后通过线程
// ——不读于 调用 . 按重量顺序排列的两个原因:
//
//   1. 面包屑,在scan_root的行走中查阅一次。
//      百分比 DIRECTORY ENTRY 重新编辑
//      -reading 记录了畸形 -value 每个条目一次留言:
//      测量120行单次传球超过40次技能. all() 运行每个
//      转弯和 dbglog 发送到 ERROR 关卡,
//      它也进入崩溃 -time 飞行记录器——所以一个打字器会用重复的
//      信息淹没记录器,并取代了一个坠机场的诊断,以保存。
//
//   2. 稳定。 -read 洞穴可以观察两个
//      不同的盖子,用于自己的扫描 目录 如果环境在
//      它下面发生了变化,每一次通过一次分辨率就使得
//      这种状态无法反映,而不仅仅是不太可能。
//
// 还在 -read 百分比 PASS (未在函数中挂起)
// -local 静态: 副剂, a --workspace 切换,
// 或者一个测试在同一过程中以不同的值合法运行,以及一个进程
// -lifetime 锁紧会默默地为每个人提供第一个呼叫者的帽子
//
// 两者 约束的站点具有已解析的价值—— scan_root
// 候选人收藏和 目录 切入( 切入) all() 高高的盖子
// 可以抬起扫描 -side WORK 绑定; 封顶只封片会
// 切掉发现的大型 库 (错误) scan_root ”). .
[[nodiscard]] std::size_t resolve_catalog_cap() noexcept {
    cap_resolutions().fetch_add(1, std::memory_order_relaxed);
    if (const char* e = std::getenv("AGENTTY_MAX_SKILLS"); e && e[0]) {
        const char* end = e + std::strlen(e);
        std::size_t v   = 0;
        const auto [ptr, ec] = std::from_chars(e, end, v);
        if (ec == std::errc{} && ptr == end) {
            if (v < 8)    return std::size_t{8};
            if (v > 4096) return std::size_t{4096};
            return v;
        }
        agentty::util::dbglog("skills.catalog_cap.env",
                              "AGENTTY_MAX_SKILLS: not a positive integer — using default");
    }
    return kMaxSkills;
}

[[nodiscard]] fs::path home_dir() noexcept {
    return agentty::util::home_dir_or_empty();
}

// 双端的 Trim ASCII 白空。
[[nodiscard]] std::string trim(std::string s) {
    auto issp = [](char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'; };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back()))  s.pop_back();
    return s;
}

// NOTE : 当地 read_capped 已经走了,那个帮手住在这里,
// 在 commands.cpp 和在 hooks.cpp ,每份副本都返回 "" 两者
// "missing" 财务报告和已审计财务报表 "too big" - ——这就是一个超大
// SKILL.md 用来消失 `0 warning(s)`. util::capped_read
// 返回 a VARIANT ,所以两者不能被一个忘记关心的呼叫者混淆.

// 解析 `key: value` 调自 a YAML 头条线。
// LENIENT 设计(规格指导):仅限 首次 结肠分裂,所以一个描述像
// "Use when: handling PDFs" - 技术上
// 无效 YAML 说到其他客户 解析器 接受 -- -- 解析 如果
// 线条不是简单的平面图, 则返回错误。
[[nodiscard]] bool parse_kv(const std::string& line,
                            std::string& key, std::string& val) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return false;
    key = trim(line.substr(0, colon));
    val = trim(line.substr(colon + 1));
    // 将周围的引号在数值上脱落匹配。
    if (val.size() >= 2 &&
        ((val.front() == '"' && val.back() == '"') ||
         (val.front() == '\'' && val.back() == '\''))) {
        val = val.substr(1, val.size() - 2);
    }
    return !key.empty();
}

[[nodiscard]] bool is_truthy(const std::string& v) noexcept {
    return v == "true" || v == "1" || v == "yes";
}

// 领 领 -space 计数(前题嵌入深度探测器)。
[[nodiscard]] std::size_t indent_of(const std::string& line) noexcept {
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    return i;
}

// 拆分 a 单数 SKILL.md 输入前题字段+ 正文
// 前题是前两题之间的块 `---` 线条。 `slug`
// 是目录名称, 作为名称 回退 (借方:a name/dir
// 错配 加载 反正,前题名胜出).
//
// 迷你车 -parser 覆盖 YAML 子集实际技能使用 :
//   • 平面 `key: value` (第一届) -colon 分拆——未引用的结肠精细)
//   • 块状板 `key: |` / `key: >` (folded/literal 多个 -line
//     说明——常见于 Claude 代码 -authored 技术)
//   • 一个 -level 嵌入式绘图 `metadata:`
[[nodiscard]] Skill parse_skill(const std::string& raw, const std::string& slug,
                                const std::string& source) {
    Skill s;
    s.name = slug;   // 回退 ; 前题 `name:` 可能覆盖以下
    s.slug = slug;   // spec-derived 身份, 保存于 lint 中
    s.source = source;

    std::istringstream in(raw);
    std::string line;
    std::streampos body_start = 0;
    bool fm_done = false;
    std::string first;
    if (std::getline(in, first) && trim(first) == "---") {
        bool in_metadata = false;          // `metadata:` 嵌入式地图
        std::string* block_target = nullptr;  // 将块级的缩放收集到
        bool block_fold = false;           // `>` 归并 空间新线
        bool block_first = true;
        while (std::getline(in, line)) {
            if (trim(line) == "---") { fm_done = true; body_start = in.tellg(); break; }

            const std::size_t ind = indent_of(line);

            // 活动块的线条 : 任何缩进的无 -empty
            // 上边 -level (无缩)行止.
            if (block_target) {
                if (ind > 0 || trim(line).empty()) {
                    auto t = trim(line);
                    if (!t.empty()) {
                        if (!block_first) *block_target += block_fold ? " " : "\n";
                        *block_target += t;
                        block_first = false;
                    }
                    continue;
                }
                block_target = nullptr;   // 掉到: 解析 此行
            }

            // 下方的嵌线 `metadata:`.
            if (in_metadata && ind > 0) {
                std::string k, v;
                if (parse_kv(line, k, v)) s.metadata.emplace_back(k, v);
                continue;
            }
            in_metadata = false;

            std::string k, v;
            if (!parse_kv(line, k, v)) continue;
            if (k == "metadata" && v.empty()) { in_metadata = true; continue; }

            // 块-scalar 开口器:`description: |` / `description: >-` 等.
            std::string* field = nullptr;
            if      (k == "name")                      { if (!v.empty()) s.name = v; continue; }
            else if (k == "description")               field = &s.description;
            else if (k == "compatibility")             field = &s.compatibility;
            else if (k == "allowed-tools")             field = &s.allowed_tools;
            else if (k == "license")                   field = &s.license;
            else if (k == "source")                    field = &s.origin;
            else if (k == "effects")                   { s.effects = parse_effects(v); continue; }
            else if (k == "disable-model-invocation")  { s.user_only = is_truthy(v); continue; }
            else continue;   // 未知密钥: 容忍、 未使用

            if (v == "|" || v == "|-" || v == "|+" ||
                v == ">" || v == ">-" || v == ">+") {
                block_target = field;
                block_fold   = (v[0] == '>');
                block_first  = true;
                field->clear();
            } else {
                *field = v;
            }
        }
    }

    if (fm_done && body_start != std::streampos(-1)) {
        s.body = trim(raw.substr(static_cast<std::size_t>(body_start)));
    } else {
        // 没有前题——将整份文件作为正文处理.
        s.body = trim(raw);
    }
    // 列宁特 回退 (规格:说明对披露至关重要,但我们可以合成
    // 一个):第一,不 -blank 正文 线条。
    if (s.description.empty()) {
        std::istringstream b(s.body);
        std::string l;
        while (std::getline(b, l)) {
            auto t = trim(l);
            if (!t.empty()) { s.description = t; break; }
        }
    }
    return s;
}

// 从根下面的目录路径中衍生技能的光谱名称.
//
// `embedded/startup` -> `embedded-startup`。
// 该字符集是 `[a-z0-9-]` ,此函数是 一个 保证它的地方
// ——它返回的名称是什么 `skills::find()` 键和用户
// 类型 `/name` ,所以在此生存的字符是不能被引用的技巧。
// '/' 不够: 组文件夹命名 `My Group/Sub_Dir`
// 生成名称 "My Group-Sub_Dir" ,
// 它可以发现和列出,但永久无法达到,因为斜线 -command
// 解析器 (update/submit.cpp )在白空间上分割其符号。
//
// 规则,按下列顺序适用:
//   • ASCII 上 - > 下( 名称为大小写) -insensitive 实际中;
//     选择面板 财务报告和已审计财务报表 `find()` 准确比较一下,所以在这里折叠是 `Foo/`
//     财务报告和已审计财务报表 `foo/` 与技能相同,而不是两种。
//   • [-z0-9] 保存;其他一切(分离、空格、插头,
//     任何非-ASCII 字节)改为'-'.
//   • 运行情况 '-' 崩溃,和 leading/trailing '-' 被修剪,所以,我... ...
//     `a//b`, `a - b` 财务报告和已审计财务报表 `-a-` 切换到双倍或边缘连字符。
//
// 当无物幸存时返回空( 名为目录) `___` ),而打电话者视之
// 为 "not a skill" 而不是发明一个名字。
[[nodiscard]] std::string slug_from_path(std::string_view rel) {
    std::string out;
    out.reserve(rel.size());
    for (unsigned char c : rel) {
        char mapped;
        if (c >= 'A' && c <= 'Z')                      mapped = static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                                                       mapped = static_cast<char>(c);
        else                                           mapped = '-';
        // 折叠运行而不是排放双倍连字符.
        if (mapped == '-' && (out.empty() || out.back() == '-')) continue;
        out.push_back(mapped);
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > kMaxSlugLen) {
        out.resize(kMaxSlugLen);
        while (!out.empty() && out.back() == '-') out.pop_back();
    }
    return out;
}

// 假设在技能目录( 渐进披露的第 3 级) 下捆绑的
// 资源文件。 递归但有限制: 深度 3,
// 最多为 kMaxResources 条目, 跳过点文件及
// 每个 SKILL.md (技能的自有) 和 指导永远不是资源。
// 相
// 对于技能目录记录的路径, 带有前向斜线,
// 模型可以在绝对目录之后进行切片。
void enumerate_resources(const fs::path& dir, std::vector<std::string>& out) {
    std::error_code ec;
    auto opts = fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator it(dir, opts, ec), end;
    for (; !ec && it != end && out.size() < kMaxResources; it.increment(ec)) {
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        const auto& p = it->path();
        auto fname = p.filename().string();
        if (!fname.empty() && fname.front() == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        // 每一个 SKILL.md ——技能自有(深度0).
        // 和 巢型技能(深)——是指示,而不是捆绑的资源.
        if (fname == "SKILL.md") continue;
        auto rel = fs::relative(p, dir, ec);
        if (ec) continue;
        std::string r = rel.generic_string();   // 向上斜线
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end());
}

// 扫描一个根来表示技能条目——在根下方的ANY深度处扫描一个
// `<dir>/SKILL.md`(已存在) 附加到`out`(已
// 获取名称的) 早期根胜,参见头文件优先表) 内嵌组文件夹透明: 弹
// 片是技能目录相对于根的路径,其片段与`-`(
// `skills/embedded/startup/` ~
// `embedded-startup`)相配合,所以名称留在谱系的`a-z0-9-`套.
// 访问条目 SHALLOWEST-FIRST (见下文)—
// —关于联合的 -name 碰撞较浅的条目获胜,根 -level
// 技能保持他们的平面名称如此现成 库 加载 不变。
// 附加每个找到的 SKILL.md 时间进入 `sig` 这么一来 -place
// 编辑( 相同的 dir mtime) 仍然无效 缓存.
// `shadows`收集了这个传球隐藏在更早的根后面的技能.
void scan_root(Io io, const fs::path& root, const std::string& source,
               std::size_t cap, std::vector<Skill>& out, std::string& sig,
               std::vector<Shadowed>& shadows) {
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec) return;
    auto mt = fs::last_write_time(root, ec);
    // file_time_type 代表是,我们... __int128 打开 Android/bionic
    // -没有 std::to_string 超载匹配。 长长的保存年限为 ~ 292 年的
    // ns 范围; a 缓存-sig 包装无害( 只需 CHANGE 当时间发生变化时。
    if (!ec)
        sig += source + ":" +
               std::to_string(static_cast<long long>(
                   mt.time_since_epoch().count())) +
               ";";

    // 收集每个 SKILL.md 根下面。隐藏目录从不降
    // 为( `.git`, `.obsidian` ,.
    // 是存储,而不是技能领域),目录的共鸣—— 执行者
    // 没有遵循—— 被直接探测到如此的顶端 -level
    // 共链接技能目录继续工作(它在平面扫描中可以发现).
    //
    // 走路是... 走路是 走路是来着 BOUNDED。
    // 深度停止于 kMaxSkill Depth( 组文件夹组织一个) 库 ;
    // 它们不会任意筑巢),一旦有足够的候选人填补 kMaxSkills 时,
    // 收集就会停止。没有第二个约束,则对“顶点”的上限将自动关闭。
    // RESULT 但不是说 WORK :一个巧妙地包含一个
    // 大 源码树 被完全穿过每一次 缓存 老师
    std::vector<fs::path> mds;
    auto opts = fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator it(root, opts, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        // depth() 直接在根中输入为 0, 因此深处的技巧目录
        // d 有其 SKILL.md 以 d+ 1 键停止
        // DESCENDING 而不是阻止散步: 法律深度的兄弟姐妹们
        // 仍然需要探视。
        if (it.depth() >= static_cast<int>(kMaxSkillDepth))
            it.disable_recursion_pending();
        if (mds.size() >= cap) break;
        if (it->is_directory(ec)) {
            auto fname = it->path().filename().string();
            if (!fname.empty() && fname.front() == '.') {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_symlink(ec)) {
                fs::path md = it->path() / "SKILL.md";
                if (fs::is_regular_file(md, ec)) mds.push_back(std::move(md));
            }
            continue;
        }
        if (it->is_regular_file(ec) && it->path().filename() == "SKILL.md")
            mds.push_back(it->path());
    }
    // 访问 SHALLOWEST-FIRST ,然后在深度内用词法表示。
    //
    // 下方的影子规则是第一 -wins ,所以访问命令 IS 优先级。
    // 普通词典顺序不编码深度:'-'(0x2D)排序在'/'(0×2F)之前,
    // 所以`a/b/SKILL.md`排序为BEFORE
    // `a-b/SKILL.md`,即使后者比较浅。 两种流星到
    // `a-b`,因此,当一个不相关的`a/b/`群文件夹出现旁边时,
    // 一个已经无声地取代了现有的`a-b/`技能——这是库在约会前筑
    // 巢的回归,这正是"flat layouts keep their names unchanged"
    // 应该保证的.
    //
    // 比较(深入、路径)直接说明意图,
    // 而不是依赖 字节-order 深度的测量
    // 基于同样的原因(见下文): 共线技能目录
    // 属于其原深处 PUT 而不是它决心去的地方。
    auto depth_of = [&](const fs::path& md) {
        const auto rel = md.parent_path().lexically_relative(root);
        return static_cast<std::size_t>(
            std::distance(rel.begin(), rel.end()));
    };
    std::ranges::sort(mds, [&](const fs::path& a, const fs::path& b) {
        const auto da = depth_of(a), db = depth_of(b);
        if (da != db) return da < db;
        return a < b;
    });
    mds.erase(std::unique(mds.begin(), mds.end()), mds.end());

    for (const auto& md : mds) {
        if (out.size() >= cap) break;
        // 斜拉线=根部以下路径, 静态到 spec's charset.
        // A SKILL.md 直接坐在根部没有自己的目录—
        // —与平面扫描一样,它不是技能(技能是技能)
        // DIRECTORY 持有 SKILL.md。
        // 一个将无为化的路径(一个名为“组文件夹”的文件夹) `___`
        // )同样不是技能;为此而造名比跳过要糟糕。
        //
        // LEXICAL 相对,不 fs::relative : 后一词条,
        // 它解决了共鸣。一个技能的 dir 共鸣连接到 库 (
        // `skills/linked -> /elsewhere/realskill`
        // 之后从它的 TARGET ,从根中逃出,
        // 作为 `../../elsewhere/realskill` ,
        // 并用绝对文件系统路径命名技能,而不是用户在其中给出的名称 库。
        // 技能的名称必须来自它原处。 PUT ,而不是从哪里
        // 字节 恰好活着。
        const std::string rel =
            md.parent_path().lexically_relative(root).generic_string();
        if (rel.empty() || rel == "." || rel.starts_with("..")) continue;
        const std::string slug = slug_from_path(rel);
        if (slug.empty()) continue;
        std::error_code mec;
        auto fmt = fs::last_write_time(md, mec);
        if (!mec)
            sig += std::to_string(static_cast<long long>(
                       fmt.time_since_epoch().count())) +
                   ";";
        // 拜访,别偷看: 编译器 拒绝省略的羊肉套件 TooBig ,
        // 这就是为什么这是一个 变体.
        const auto got = ::agentty::util::capped_read(md, kMaxBodyBytes);
        std::string raw;
        bool skip = false;
        std::visit([&](const auto& v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, ::agentty::util::Content>) {
                raw = v.text;
            } else if constexpr (std::is_same_v<V, ::agentty::util::TooBig>) {
                // 保留一个 stub, 这样发现可以 REPORT。
                // 目录 忽略的磁盘上的文件正是阴影日志的存在
                // 来防止的; 大小上限应该得到同样的处理。
                Skill over;
                over.name   = slug;
                over.slug   = slug;
                over.source = source;
                over.dir    = md.parent_path();
                over.oversized_bytes = v.size;
                out.push_back(std::move(over));
                skip = true;
            } else {
                skip = true;   // 缺席/ 无法读取
            }
        }, got);
        if (skip || raw.empty()) continue;
        Skill s = parse_skill(raw, slug, source);
        if (s.name.empty()) continue;
        // 阴影:更早的根(项目在用户之前,原生在互通之前)——以及
        // 更早的访问命令(shallower before deep)
        // ——在名称碰撞上获胜.
        if (const auto winner = std::ranges::find_if(
                out, [&](const Skill& e){ return e.name == s.name; });
            winner != out.end()) {
            // 记录它而不是默默地扔下去。丢失的复制品是NOT 已
            // 加载——它无法到达模型——但是坐在磁盘上、
            // 隐形到`list`、`approve`的技巧,
            // 而面板恰恰是某物在另一个条目上戴着它的名字时隐藏的。
            // Cross-scope 阴影是打算的,在UI层保持安静;
            // 同样的-scope是浮出水面的东西。
            std::error_code sec;
            auto sdir = fs::weakly_canonical(md.parent_path(), sec);
            shadows.push_back(Shadowed{
                .name        = s.name,
                .dir         = (sec ? md.parent_path() : sdir).string(),
                .source      = source,
                .effects     = s.effects,
                .winner_dir  = winner->dir.string(),
            });
            continue;
        }
        std::error_code cec;
        auto abs = fs::weakly_canonical(md.parent_path(), cec);
        s.dir = cec ? fs::absolute(md.parent_path(), cec) : abs;
        enumerate_resources(s.dir, s.resources);
        // 三级访问: 允许清单 技能目录 READS 这样模型就可以被捆绑
        // scripts/references 住在外边的 工作区 (
        // ~/.agentty/skills/... )不绊倒边界。
        // 读取 -only - 电话 write/edit 门从来不会咨询它。
        util::allow_read_root(io, s.dir);
        out.push_back(std::move(s));
    }
}

// 最后一个发现是:技能、他们所跟踪的东西、 以及他们被扫描的签名。
// 不可变 值在已公布的槽中, 使得三人不能有异议: 每个读者都
// 看到一个完整的发现, 而重新扫描会发布一个全新的发现。 不,
// 无法 锁 ,因此没有能力 all() (从工具中调用) 正文 ,
// 则 RAG 索引和 提示词 建筑师一样。
struct Discovery {
    std::vector<Skill>    skills;
    std::vector<Shadowed> shadows;
    std::string           sig = "\x01uninit";
    std::uint64_t         generation = 0;   // 在每个重新扫描时都碰巧
};
maya::published<const Discovery>& discovery() {
    static maya::published<const Discovery> d;
    return d;
}

} // 命名空间结束。

std::size_t debug_cap_resolutions() noexcept {
    return cap_resolutions().load(std::memory_order_relaxed);
}

// 状态 -only 传: mtime/size 指纹 在一个根,
// 无需 读取或 解析 任何东西,故意的镜子 scan_root 横向
// ( 相同的目录顺序, 相同) SKILL.md 地点,同一个巢穴),
// 所以两人同意什么是 "changed" 意思是 如果他们漂流的话 缓存
// 要么永远不要打,要么永远不要失败 两者都比代价更糟糕 保存.
// 一个 元数据查询每个路径: 是否在其中, 类型、 mtime 大小。 每一个转弯
// 都会有此传出, 以及 fs::is_regular_file + last_write_time
// + file_size 是三个独立的统计 -- -- 运行于 Windows 每个
// 都打开并关闭文件控件, 将 40 个技能放在~ 10 ms/turn.
// GetFileAttributesExW 从目录条目中解答全部内容,而不打开文件。
struct StatSig { bool dir = false; bool file = false; long long mtime = 0; std::uintmax_t size = 0; };
[[nodiscard]] std::optional<StatSig> stat_sig(const fs::path& p) noexcept {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return std::nullopt;
    StatSig s;
    s.dir   = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    s.file  = !s.dir && (a.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0;
    s.mtime = static_cast<long long>((static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32)
                                     | a.ftLastWriteTime.dwLowDateTime);
    s.size  = (static_cast<std::uintmax_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return s;
#else
    struct ::stat st{};
    if (::stat(p.c_str(), &st) != 0) return std::nullopt;
    StatSig s;
    s.dir   = S_ISDIR(st.st_mode);
    s.file  = S_ISREG(st.st_mode);
#  if defined(__APPLE__)
    s.mtime = static_cast<long long>(st.st_mtimespec.tv_sec) * 1'000'000'000LL + st.st_mtimespec.tv_nsec;
#  else
    s.mtime = static_cast<long long>(st.st_mtim.tv_sec) * 1'000'000'000LL + st.st_mtim.tv_nsec;
#  endif
    s.size  = static_cast<std::uintmax_t>(st.st_size);
    return s;
#endif
}

void append_md_sig(const fs::path& dir, const StatSig& md, std::string& sig) {
    sig += dir.filename().string();
    sig += std::to_string(md.mtime);
    sig += ':';
    sig += std::to_string(md.size);
    sig += ';';
}

void scan_root_signature(const fs::path& root, std::string& sig) {
    std::error_code ec;
    const auto rs = stat_sig(root);
    if (!rs || !rs->dir) return;

    // 根的自定义 mtime 捕获到一个添加或删除的技能目录。
    sig += std::to_string(rs->mtime);
    sig += "|";

    std::vector<fs::path> dirs;
#if defined(_WIN32)
    // 一个 FindFirstFile 每一个条目都通过一个不设手柄的“每个孩子”名称+属性;
    // std::filesystem 重播器每个条目的重播统计 is_directory
    // () 占每笔捐款的多数 -turn 费用 Windows.
    auto list_subdirs = [](const fs::path& d, std::vector<fs::path>& out) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileExW((d / L"*").c_str(), FindExInfoBasic, &fd,
                                      FindExSearchNameMatch, nullptr,
                                      FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const std::wstring_view n{fd.cFileName};
            if (n == L"." || n == L"..") continue;
            out.push_back(d / fd.cFileName);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    };
    list_subdirs(root, dirs);
#else
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) dirs.push_back(it->path());
    }
#endif
    std::ranges::sort(dirs);   // 稳定顺序 → 稳定签名

    for (const auto& d : dirs) {
        // 技术就是 <dir>/SKILL.md ,或一个层次的群筑巢.
        if (const auto md = stat_sig(d / "SKILL.md"); md && md->file) {
            append_md_sig(d, *md, sig);
            continue;
        }
        std::vector<fs::path> subs;
#if defined(_WIN32)
        list_subdirs(d, subs);
#else
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) subs.push_back(it->path());
        }
#endif
        std::ranges::sort(subs);
        for (const auto& s : subs) {
            if (const auto smd = stat_sig(s / "SKILL.md"); smd && smd->file)
                append_md_sig(s, *smd, sig);
        }
    }
}

std::vector<Skill> all(Io io) {
    // 两张通行证都运行在锁之外;只有结果的互换被锁定.
    // 两个呼叫者竞速进行两次扫描,第二个相同的
    // 结果取代第一个:浪费的工作,永远不做错的工作.

    // 解开盖子 一次 每一个根的扫描带和每个根
    // 的扫描 目录 下方切片分享此值,
    // 因此环境无法改变答案部分 -way 并且
    // 一个畸形的值记录了它的一个面包屑
    const std::size_t cap = resolve_catalog_cap();

    // 优先秩序来自 scope::plan (乐库) -major 对话框 -minor):
    // . 项目。 agentty . 代理 . 宣布 . 用户 .
    // agentty . . .. . 克劳德 . -write. scan_root
    // 阴影( 更早的根胜) + mtime -sig + 第三级 允许清单 每一个根。
    // NOTE : 项目保持 cwd -relative 这里( 不同于 内存's
    // project_root ()夹)——技能一直解决 ".agentty/skills" 对抗Cwd,
    // 所以 env.project_root = "." 保留了这个
    scope::Env env;
    env.home             = home_dir();
    env.user_native_base = ::agentty::util::user_root();
    env.project_root     = fs::path{"."};
    env.project_writable = true;   // 发现读取所有方言; 这里没有使用
    const scope::Layout& layout = ::agentty::config::kSkillsLayout;
    const auto sources = scope::plan(layout, env);

    // PASS 1——仅签字. stat() 百分比 SKILL.md ,没有读取文件,没有 解析.
    //
    // 分头是重点,过去是 一个 传球 : scan_root 解析次数
    // SKILL.md 用于建设 `fresh` ,比较签名,
    // 然后扔出 解析 被击中了 catalog_block() 调用
    // all() 打开 每个 转过来,所以那是一个完整的R -parse
    // 整个技能 库 · 每项请求——以~3 mss(40个技能为4
    // 个)计量 KB ,坐在用户按下输入和请求外出之间的路径上。
    std::string sig;
    for (const scope::Source& src : sources)
        scan_root_signature(src.base / layout.leaf, sig);

    // 盖子是 INPUT 而不是仅仅为了逆向:它决定了有多少技能
    // 能够存活下来,以及 AGENTTY_MAX_SKILLS 可以
    // 在 调用 在同一进程内。仅按键文件就意味着更改上限返回上一个列表。
    // skills_engine_test ,它翻转了 env
    // var 并重新 -reads all().
    sig += "#cap=" + std::to_string(cap);

    if (const auto cur = discovery().current(); cur && cur->sig == sig) return cur->skills;

    // PASS 2 — 磁盘上有些变化; 做昂贵的读数+ 解析.
    Discovery fresh;
    std::string parse_sig;
    for (const scope::Source& src : sources) {
        scan_root(io, src.base / layout.leaf, std::string{scope::to_string(src.locus)},
                  cap, fresh.skills, parse_sig, fresh.shadows);
    }
    fresh.sig = sig;
    std::vector<Skill> out = fresh.skills;
    // 与下一代一起出版; 如果赛车扫描先进入, 将重新测试。
    for (;;) {
        auto prev = discovery().current();
        auto next = std::make_shared<Discovery>(fresh);
        next->generation = (prev ? prev->generation : 0) + 1;
        if (discovery().publish_if(prev, std::shared_ptr<const Discovery>(std::move(next)))) break;
    }
    return out;
}

std::vector<Shadowed> shadowed(Io io) {
    (void)all(io);           // 确保发现运行
    auto d = discovery().current();
    return d ? d->shadows : std::vector<Shadowed>{};
}

bool shadowed_within_scope(Io io, std::string_view name) {
    // 十字记 -scope 阴影是文件规则( project
    // beats user),所以它不是一个警告。
    // 在 相同 要求一个名字的范畴是错误或隐藏地点。
    const auto winner = find(io, name);
    if (!winner) return false;
    for (const auto& sh : shadowed(io))
        if (sh.name == name && sh.source == winner->source) return true;
    return false;
}

std::optional<Skill> find(Io io, std::string_view name) {
    for (auto& s : all(io))
        if (s.name == name) return std::move(s);
    return std::nullopt;
}

std::string catalog_block(Io io) {
    // CACHED。此操作开始 每个 转弯( E) provider::system_prompt_for
    // → default_system_prompt 解决信任问题使其代价高昂:
    // a JSON 读取批准商店加a SHA - 256 超越一切技能 -
    // 255 over every skill 正文 测量为3.4级 ms/turn
    // 4个技能 KB ——在请求路径上支付,以重算一个字符串,该字符串只有在
    // 技能文件或批准时才会改变.
    //
    // 关键是(技能-cache生成,批准文件mtime+大小).
    // all()已经重新扫描了自己的 mtime 签名,
    // 所以一个技能编辑土地 下转完全像以前一样; 当它写的文件
    // 更改时批准土地。
    struct Built {
        std::string    text;
        std::uintmax_t key = 0;
        std::uint64_t  generation = ~std::uint64_t{0};
    };
    // 替换了整段, 读取每个转弯: 一个已公布的插槽中的不可变值.
    static maya::published<const Built> built_cache;

    const auto skills = all(io);
    const auto disc = discovery().current();
    const std::uint64_t gen = disc ? disc->generation : 0;

    // 批准文件身份 - 一个 元数据查询,无 解析 (中文(简体) ).
    // stat_sig 发现签名用途; file_size + last_write_time
    // 打开两个把手 Windows).
    std::uintmax_t akey = 0;
    if (const auto st = stat_sig(agentty::util::user_root() / std::string{kApprovalsLeaf});
        st && st->file)
        akey = st->size ^ static_cast<std::uintmax_t>(st->mtime) * 1099511628211ull;
    // 发现者说技能有变 被认可
    // 指纹 说信任决定是的
    const std::uintmax_t key = akey;

    if (const auto b = built_cache.current(); b && b->key == key && b->generation == gen)
        return b->text;

    // 隐藏的节点列表 -but-blocked: user_only
    // 技能永远不会出现,所以模型不能浪费一个回合试图激活一个回合。
    // UNAPPROVED 有效的技能也隐藏在同样的原因—
    // —列出一些激活路径会拒绝的事物,只是烧掉一个转弯,
    // 读作故障. `agentty skill list`
    // 显示为 PENDING 模型根本就没有被告知。
    const auto approvals = load_approvals(io);
    auto eligible_for_model = [&](const Skill& s) {
        if (s.user_only) return false;
        // 超大个头 SKILL.md 生产了报告根,没有 正文 改
        // 为 加载 . 提供给模型将提供一个无法启动的技能 -
        // - -- 同样的浪费的转弯 user_only 避风头
        // `agentty skills` 警告是它出现的地方。
        if (s.oversized_bytes > 0) return false;
        return std::holds_alternative<scope::Trusted>(trust_of(s, approvals));
    };

    std::size_t eligible = 0;
    for (const auto& s : skills) if (eligible_for_model(s)) ++eligible;
    if (eligible == 0) {
        built_cache.publish(std::make_shared<const Built>(Built{{}, key, gen}));
        return {};
    }

    std::ostringstream m;
    m << "\n\n<skills>\n"
      << "On-demand skills are available. Each is a focused instruction "
         "doc you can load IN FULL with the `skill` tool when its task "
         "comes up — don't guess the contents, load it. Skills may "
         "bundle resource files (scripts/, references/, assets/); the "
         "activation result lists them — `read` the specific file "
         "when the instructions reference it, resolving relative paths "
         "against the skill's directory shown below. NEVER guess a skill "
         "path — skills live in several roots (.agentty/, .agents/, "
         ".claude/, project and user) and only the listed directory is "
         "readable. Listed: name — description (directory).\n";
    for (const auto& s : skills) {
        if (!eligible_for_model(s)) continue;
        // 作者 -controlled 文本, 离系统指令一行远
        // 提示词。 将它添加到 Sanitise ,
        // 这样描述不能将新线 + 假指令走私到 目录 块(确认
        // 的91%) -malicious 斯奈克审计中的技术 注射。
        m << "- " << sanitize_author_text(s.name, 64);
        if (!s.description.empty())
            m << " — " << sanitize_author_text(s.description, 300);
        if (!s.dir.empty()) m << " (" << s.dir.string() << ")";
        m << "\n";
    }
    m << "</skills>";
    auto built = m.str();
    built_cache.publish(std::make_shared<const Built>(Built{built, key, gen}));
    return built;
}

std::string activation_payload(Io io, const Skill& s) {
    return activation_payload(s, load_approvals(io));
}

std::string activation_payload(const Skill& s, const scope::Approvals& approvals) {
    // 执行点. catalog_block ()隐藏
    // 未经批准的技能,但模型可以直接命名(它可能在前
    // 一个会话中见过,或者用户可能提到过),
    // 所以拒绝必须活下来. 此处 而不是依赖名单 是唯一的门。
    //
    // 拒绝使用该模型可以操作的指令,而不是一个明显的错误:
    // 它应该告诉用户该运行什么,而不是重试或周围的路线.
    if (needs_trust_gate(s.effects)
        && !std::holds_alternative<scope::Trusted>(trust_of(s, approvals))) {
        std::ostringstream deny;
        deny << "<skill_blocked name=\"" << s.name << "\">\n"
             << "This skill declares effects (" << effects_to_frontmatter(s.effects)
             << ") and has not been approved on this machine, so its contents "
                "were not loaded.\n\n"
                "Do NOT attempt to read the skill file directly or guess what "
                "it says — that defeats the approval. Tell the user they can "
                "review and approve it with:\n"
             << "  agentty skill approve " << s.name << "\n"
             << "</skill_blocked>";
        return deny.str();
    }

    std::ostringstream out;
    out << "<skill_content name=\"" << s.name << "\">\n";
    if (!s.description.empty()) out << s.description << "\n\n";
    if (!s.compatibility.empty())
        out << "Compatibility: " << s.compatibility << "\n\n";
    if (!s.license.empty())
        out << "License: " << s.license << "\n\n";
    if (!s.allowed_tools.empty())
        out << "Allowed tools: " << s.allowed_tools
            << " \u2014 prefer these tools while following this skill.\n\n";
    out << s.body << "\n";
    if (!s.dir.empty()) {
        out << "\nSkill directory: " << s.dir.string() << "\n"
            << "Relative paths in this skill resolve against the skill "
               "directory \u2014 use absolute paths in tool calls.\n";
    }
    if (!s.resources.empty()) {
        out << "\n<skill_resources>\n";
        for (const auto& r : s.resources) out << "  " << r << "\n";
        if (s.resources.size() >= kMaxResources)
            out << "  (listing capped \u2014 there may be more files)\n";
        out << "</skill_resources>\n"
            << "Resources are NOT loaded \u2014 `read` the specific file "
               "when the instructions call for it.\n";
    }
    out << "</skill_content>";
    return out.str();
}

namespace {
// 标签 activation_payload (a) 发射环绕a 正文。
// 匹配确切的开口标记(与名称属性和关闭引号),是指其名称是他人(forward)前缀的技巧(
// federal name difference). "review" 数字 "review-pr"
// )不可误也.
std::string body_tag_for(std::string_view name) {
    std::string t = "<skill_content name=\"";
    t.append(name);
    t += "\">";
    return t;
}

} // 命名空间结束。

bool is_active_in(std::string_view name,
                  const std::vector<std::string_view>& visible) {
    const std::string tag = body_tag_for(name);
    for (auto text : visible)
        if (text.find(tag) != std::string_view::npos) return true;
    return false;
}

std::vector<std::string>
active_in(const std::vector<std::string_view>& visible) {
    // 扫描标记前缀并读出名称,而不是测试每个安装
    // 在每一个消息上的技巧:一旦每次会话拥有许多技能,
    // 记录片就是较小的一面.
    static constexpr std::string_view open = "<skill_content name=\"";
    std::vector<std::string> out;
    for (auto text : visible) {
        for (std::size_t at = text.find(open); at != std::string_view::npos;
             at = text.find(open, at + open.size())) {
            const std::size_t b = at + open.size();
            const std::size_t e = text.find('"', b);
            if (e == std::string_view::npos) break;
            std::string n{text.substr(b, e - b)};
            if (std::find(out.begin(), out.end(), n) == out.end())
                out.push_back(std::move(n));
        }
    }
    return out;
}


// ── 副作用 + 信托 ────────────────────────────────────────────────────────

// 其中一个 解析器 ,暴露在外。。 (笑声) `skill add` 需要先
// 描述文件然后安装; 给它自己的副本意味着在区域中用不同的解析符来
// 解析块scalars。 提示词 时值 加载 时间
Skill parse_skill_text(const std::string& raw, const std::string& slug,
                       const std::string& source) {
    return parse_skill(raw, slug, source);
}

EffectSet parse_effects(std::string_view value) noexcept {
    // 设计上是: `[exec, net]`, `exec net`,
    // `exec,net` 都意味着同样的事情, 一个未知的词被跳过而不是
    // 致命的 所以写给未来的技能 agentty 仍然 加载 硂初
    EffectSet out{};
    std::string tok;
    auto flush = [&] {
        if (tok.empty()) return;
        // 归并 以粗略小写字母: "write-fs"/"write_fs"/"WriteFs"
        // 全部崩溃到 "writefs" ,所以拼写永远不会花费一个用户一个
        // 混乱 "unknown effect" 他们看不见。
        std::string k;
        for (char c : tok) {
            if (c == '-' || c == '_') continue;
            k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        tok.clear();
        if      (k == "readfs"  || k == "read")  out |= EffectSet{Effect::ReadFs};
        else if (k == "writefs" || k == "write") out |= EffectSet{Effect::WriteFs};
        else if (k == "net"     || k == "network") out |= EffectSet{Effect::Net};
        else if (k == "exec"    || k == "execute") out |= EffectSet{Effect::Exec};
        // 其它: 未知 – 故意忽略(向前兼容).
    };
    for (char c : value) {
        if (c == '[' || c == ']' || c == ',' || c == ' ' ||
            c == '\t' || c == '"' || c == '\'') { flush(); continue; }
        tok += c;
    }
    flush();
    return out;
}

std::string effects_to_frontmatter(EffectSet e) {
    // 位顺序,所以 渲染 在一个给定的集合中稳定——安装 提示词 财务报告和已
    // 审计财务报表 `skill list` 运行之间不得重排。
    std::string out;
    auto add = [&](std::string_view w) {
        if (!out.empty()) out += ", ";
        out += w;
    };
    if (e.has(Effect::ReadFs))  add("read-fs");
    if (e.has(Effect::WriteFs)) add("write-fs");
    if (e.has(Effect::Net))     add("net");
    if (e.has(Effect::Exec))    add("exec");
    return out;
}

scope::Trust trust_of(const Skill& s, const scope::Approvals& approvals) noexcept {
    // Prose 语句 -only :绝大多数,以及必须坚持的道路 字节-identical
    // 准备 -effects agentty.
    if (!needs_trust_gate(s.effects)) return scope::Trusted{};

    // 内容 -bound ,从名称 -bound (麦克波森) 字节
    // 实际引导代理人的: 正文 加所申报 副作用 ,所以一个v2 保持它的传言,
    // 但添加 `exec` 内容 -gates.
    const std::string sha = scope::content_hash(
        s.body + "\n#effects:" + effects_to_frontmatter(s.effects));
    if (approvals.approved(sha)) return scope::Trusted{};

    // 从某处获取:用户没有写这些指令,
    // 所以他们一次批准,一次明确批准.
    if (!s.origin.empty()) return scope::Pending{sha};

    // 手 -authored. A USER-root 技能是人类自己的
    // 文件——可信,与范围处理用户相同 -locus 配置。
    // A PROJECT 技术到达与 仓库 ,所以克隆人不能为自己担保。
    if (s.source == "user") return scope::Trusted{};
    return scope::Pending{sha};
}

std::vector<std::string> lint(const Skill& s) {
    std::vector<std::string> out;
    // 太大了,不能动 加载 报告 仅 这条规则是 写着一个从未
    // 被解析过的地方 所以运行它们会埋葬一个事实 事关一切
    // "description is missing".
    if (s.oversized_bytes > 0) {
        out.push_back("SKILL.md is "
                      + std::to_string(s.oversized_bytes / 1024)
                      + " KB — over the "
                      + std::to_string(kMaxBodyBytes / 1024)
                      + " KB limit, so this skill is NOT loaded"
                        " (move detail into references/)");
        return out;
    }
    // 名称:1-64字符,小写为alnum + 连字符,无edge/double连字符.
    if (s.name.empty()) out.push_back("name is empty");
    if (s.name.size() > 64) out.push_back("name exceeds 64 characters");
    bool bad_char = false, prev_hyphen = false, dbl = false;
    for (char c : s.name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) bad_char = true;
        if (c == '-' && prev_hyphen) dbl = true;
        prev_hyphen = (c == '-');
    }
    if (bad_char)
        out.push_back("name has invalid characters (allowed: a-z, 0-9, hyphen)");
    if (!s.name.empty() && (s.name.front() == '-' || s.name.back() == '-'))
        out.push_back("name must not start or end with a hyphen");
    if (dbl) out.push_back("name contains consecutive hyphens");
    // name/dir : 前题名称必须与规格匹配
    // -derived 弹壳( 弹壳) '-'-joined。
    // 对于嵌入式技能,单叶目录就不是名称。
    if (!s.slug.empty() && s.slug != s.name)
        out.push_back("name does not match parent directory '"
                      + s.slug + "'");
    // 说明:要求,第1024页。
    if (s.description.empty()) out.push_back("description is missing");
    if (s.description.size() > 1024)
        out.push_back("description exceeds 1024 characters");
    // 兼容性:  ⁇  500 当存在。
    if (s.compatibility.size() > 500)
        out.push_back("compatibility exceeds 500 characters");
    // 正文 : spec建议 \ 500 行(将细节移动到 references/).
    std::size_t lines = 1 + static_cast<std::size_t>(
        std::count(s.body.begin(), s.body.end(), '\n'));
    if (lines > 500)
        out.push_back("body is " + std::to_string(lines)
                      + " lines (spec recommends ≤ 500 — move detail to "
                        "references/)");
    return out;
}

int cmd_skills(Io io) {
    const auto sk = all(io);
    if (sk.empty()) {
        std::printf("no skills installed.\n"
                    "add one: <project>/.agentty/skills/<name>/SKILL.md "
                    "(or ~/.agentty/skills/, .agents/, .claude/)\n");
        return 0;
    }
    int dirty = 0;
    for (const auto& s : sk) {
        std::printf("%-28s %-8s %s\n", s.name.c_str(), s.source.c_str(),
                    s.dir.string().c_str());
        if (!s.description.empty())
            std::printf("    %s\n", s.description.c_str());
        if (!s.resources.empty())
            std::printf("    resources: %zu file(s)\n", s.resources.size());
        if (s.user_only)
            std::printf("    [disable-model-invocation — hidden from catalog]\n");
        for (const auto& d : lint(s)) {
            std::printf("    warn: %s\n", d.c_str());
            ++dirty;
        }
    }
    std::printf("%zu skill(s), %d warning(s)\n", sk.size(), dirty);
    return dirty ? 1 : 0;
}

} // namespace agentty::tools::skills
