// agtest::cap ()——套房单人 锁-order 能力根,从 agtest.hpp
// 这么说吧 STANDALONE 测试( 原主 / 输入) check() ,
// 折叠成 agentty_standalone_tests )可以不用拉进去就使用它
// doctest 编辑 CHECK/check 测试测试继续包括 agtest.hpp ,
// 包括这个。 agtest.hpp 用于规则。
#ifndef AGENTTY_TESTS_AGTEST_CAP_HPP
#define AGENTTY_TESTS_AGTEST_CAP_HPP

#include <maya/runtime.hpp>

namespace agtest {
inline const maya::unlocked& cap() { return maya::test_root(); }
}  // namespace agtest

#endif
