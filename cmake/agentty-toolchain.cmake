# Dependencies extracted from Ubuntu packages keep their multiarch layout.
# Unlike /usr/include, this user-local include root is not searched by Clang.
set(_agentty_deps_prefix "$ENV{HOME}/.local/agentty-deps/usr")

set(CMAKE_C_COMPILER "${_agentty_deps_prefix}/bin/clang-22")
set(CMAKE_CXX_COMPILER "${_agentty_deps_prefix}/bin/clang++-22")

list(APPEND CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${_agentty_deps_prefix}/include/x86_64-linux-gnu")
list(APPEND CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    "${_agentty_deps_prefix}/include/x86_64-linux-gnu")
list(REMOVE_DUPLICATES CMAKE_C_STANDARD_INCLUDE_DIRECTORIES)
list(REMOVE_DUPLICATES CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES)
