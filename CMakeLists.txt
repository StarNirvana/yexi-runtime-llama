cmake_minimum_required(VERSION 4.2)
project(YxAgent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# llama.cpp: build it from the vendored source tree and link the `llama` target.
# LLAMA_STANDALONE is OFF when added via add_subdirectory, so the extra
# artifacts (tests/tools/examples/server/app) are disabled by default; set
# them explicitly to keep only the library.
set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP      OFF CACHE BOOL "" FORCE)
add_subdirectory(library/llama.cpp-b11526)

add_executable(YxAgent main.cpp
        src/llama_llm.cpp
        include/llama_llm.h
)
target_link_libraries(YxAgent PRIVATE llama)

# MSVC 默认按系统代码页(中文环境为 GBK)编码窄字符串字面量：
# 源码里的 UTF-8 中文 prompt 会被写成 GBK 字节，传给 llama.cpp(要求 UTF-8)
# 就会变成乱码。/utf-8 让源文件与字面量都按 UTF-8 处理。
if (MSVC)
    target_compile_options(YxAgent PRIVATE /utf-8)
endif()