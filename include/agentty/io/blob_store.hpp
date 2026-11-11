#pragma once
// agentty::blobs —内容-addressed 载荷存储。
//
// 大载荷曾经是base64'd INTO 线程 JSON。
// 一张截图是~1 MB，base64将其膨胀到~1.3 MB，
// 然后它们中的每一个都是re-read和re-decoded在每个线程
// 加载上——永远。一个真实的线程达到了29个MB，其中17个MB是图像，
// 只有0.7个MB是实际的对话文本；切换到它会使 UI
// 停顿五分之一秒，然后才绘制一行。
//
// Blob从源头修复增长：字节被写入ONCE到threads/blobs/
// <hash>，并且消息保留引用。相同的 载荷
// （相同的屏幕截图粘贴两次，或重试一次）免费折叠为
// 一个文件，因为名称 IS 是内容哈希。
//
// 这家商店故意很小，并且对其所售商品一无所知：
// 它获取字节，返回名称，并返回字节。今天图像使用它；
// 工具输出和任意文件附件是同样的问题，
// 将不加改变地使用它。
//
// 从 persistence.cpp 中逐字提取——在
// 一个 86 KB 文件中积累了十个单独的关注点——因此
// 线程 日志可以共享它，而不依赖于整个 持久化 层。
// 行为相同；哈希函数和 on-disk 布局未更改，
// 因此之前代码写入的每个 blob 仍然可以解析。

#include <filesystem>
#include <string>

namespace agentty::blobs {

// threads/blobs/. 在首次使用时创建。
[[nodiscard]] std::filesystem::path dir();

// 用作blob的文件名的内容散列。 FNV-1a 超过 字节，十六进制。
// NOT加密——这命名本地缓存条目，
// 它不能防御已经可以写入该目录的对手。
[[nodiscard]] std::string name_for(std::string_view bytes);

// Write `bytes` 并返回其名称，或者失败时返回空字符串。
// Content-addressed，因此具有相同名称的现有斑点
// 通过构造为字节-identical并且被单独保留。
//
// 调用者MUST通过回退到内联载荷来处理空返回：
// 缓慢的线程击败丢失的图像。
[[nodiscard]] std::string put(const std::string& bytes);

// 按名称读取斑点。如果名称格式错误、文件丢失或无法读取，
// 则返回空 - 所有这些调用者都已将其视为 "this payload is gone"，
// 与 pre-blob 损坏的-base64 路径相同。
[[nodiscard]] std::string get(const std::string& name);

} // namespace agentty::blobs
