// 编译-fail 个案 agentty::Io。
// 案例0必须 编译 (证明有把握,包括罚款);其他案件不得.
#include "agentty/util/io.hpp"

namespace demo {
// 任何东西都得站出来 IO 函数:需要 I/O.
inline int read_disk(agentty::Io) { return 1; }
struct Model { int n = 0; };
}  // namespace demo

#if IO_CASE == 0
// 所交I/O的副作用XX 正文可能为调用.
int effect(agentty::Io io) { return demo::read_disk(io); }
int main() { return 0; }
#elif IO_CASE == 1
// A 状态更新函数 没有 I/O ,所以它不能做到这一点 调用 a 数据为: IO 函数。
void update(demo::Model& m) { m.n = demo::read_disk(); }
#elif IO_CASE == 2
// 也不做一个:建筑者是私人的。
void update(demo::Model& m) { m.n = demo::read_disk(agentty::Io{}); }
#else
#error "no case selected"
#endif
