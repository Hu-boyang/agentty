// util/storage_lock.cpp - 电话 "agentty is running" 锁。见 头文件.

#include "agentty/util/storage_lock.hpp"

#include "agentty/util/user_root.hpp"

namespace agentty::util {

namespace {
std::string target() {
    const auto r = user_root();
    return r.empty() ? std::string{} : (r / "agentty.running").string();
}
}  // 命名空间结束。

std::optional<maya::platform::native_file_lock> hold_store_shared() {
    const auto t = target();
    if (t.empty()) return std::nullopt;
    auto got = maya::platform::native_file_lock::try_acquire(t, maya::platform::lock_mode::shared);
    if (!got || !*got) return std::nullopt;   // a `config move` 中点 -flight ,或无 加锁
    return std::move(**got);
}

std::optional<maya::platform::native_file_lock> try_store_exclusive() {
    const auto t = target();
    if (t.empty()) return std::nullopt;
    auto got = maya::platform::native_file_lock::try_acquire(t, maya::platform::lock_mode::exclusive);
    if (!got || !*got) return std::nullopt;
    return std::move(**got);
}

}  // namespace agentty::util
