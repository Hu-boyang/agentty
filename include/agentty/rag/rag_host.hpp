#pragma once
// agentty/rag/rag_host.hpp-rag-cpp需要从agentty得到什么，在一处。
//
// rag-cpp不拥有线程、套接字、进程或时钟。
// 其并行区域通过 Splitter 进行分割，其网络和桥接后端通过
// HostIo 进行通信，其文档加载器通过 RunFn 运行转换器。
// 这些构建在agentty的运行时上。

#include <rag/loaders/extract.hpp>
#include <rag/plugin/builder.hpp>
#include <rag/util/parallel.hpp>

namespace agentty::rag {

// 这里故意没有并行分离器：agentty拥有的每个rag-cpp引擎都在RAG索引锁内运行，
// 并且连接助手线程的分离器不能在锁下运行（#112）。
// 这些引擎使用内联 ::rag::util::Splitter{}。

// HTTP通过agentty的客户端（TLS、代理、SSRF防护：这些是
// 用户配置的端点）并通过util::ChildProcess进行处理。
[[nodiscard]] ::rag::plugin::HostIo host_io();

// 内置-in后端具有host_io()。
[[nodiscard]] const ::rag::plugin::Registries& registries();

// 转换器（pdftotext、antiword等）作为子进程运行，捕获标准输出。
[[nodiscard]] ::rag::loaders::RunFn converter_runner();

}  // namespace agentty::rag
