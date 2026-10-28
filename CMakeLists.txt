cmake_minimum_required(VERSION 3.16)
project(transaction_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # __int128_t is supported by GCC and Clang.

option(TRANSACTION_ENGINE_BUILD_BENCHMARK "Build the synthetic performance benchmark" OFF)

add_library(transaction_engine src/transaction_engine.cpp)
target_include_directories(transaction_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(transaction_engine PRIVATE -Wall -Wextra -Wconversion)

add_executable(transaction_engine_cli src/main.cpp)
target_link_libraries(transaction_engine_cli PRIVATE transaction_engine)
target_compile_options(transaction_engine_cli PRIVATE -Wall -Wextra -Wconversion)

include(CTest)
if(BUILD_TESTING)
    add_executable(transaction_engine_test tests/transaction_engine_test.cpp)
    target_link_libraries(transaction_engine_test PRIVATE transaction_engine)
    target_compile_options(transaction_engine_test PRIVATE -Wall -Wextra -Wconversion)
    add_test(NAME transaction_engine_test COMMAND transaction_engine_test)
endif()

if(TRANSACTION_ENGINE_BUILD_BENCHMARK)
    add_executable(transaction_engine_benchmark benchmarks/transaction_engine_benchmark.cpp)
    target_link_libraries(transaction_engine_benchmark PRIVATE transaction_engine)
    target_compile_options(transaction_engine_benchmark PRIVATE -Wall -Wextra -Wconversion)
endif()
