cmake_minimum_required(VERSION 3.15)
project(JsonLib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 静态库：JsonCore
add_library(JsonCore STATIC
    JsonCore.cpp
    Number.cpp
    JsonYaml.cpp
)
target_include_directories(JsonCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_options(JsonCore PRIVATE -Wall -Wextra)

# 单元测试
enable_testing()
add_executable(JsonCoreTests
    tests/JsonCoreTests.cpp
)
target_link_libraries(JsonCoreTests PRIVATE JsonCore)
target_compile_options(JsonCoreTests PRIVATE -Wall -Wextra)

add_test(NAME JsonCoreTests COMMAND JsonCoreTests)
