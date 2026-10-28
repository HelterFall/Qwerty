cmake_minimum_required(VERSION 3.16)
project(transaction_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # __int128_t is supported by GCC and Clang.

option(TRANSACTION_ENGINE_BUILD_BENCHMARK "Build synthetic performance benchmarks" OFF)

include(CTest)

add_library(transaction_engine src/transaction_engine.cpp)
target_include_directories(transaction_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(transaction_engine PRIVATE -Wall -Wextra -Wconversion)

add_executable(transaction_engine_cli src/main.cpp)
target_link_libraries(transaction_engine_cli PRIVATE transaction_engine)
target_compile_options(transaction_engine_cli PRIVATE -Wall -Wextra -Wconversion)

# Isolated research prototype for composing offset-aware subarray profiles.
# This target is deliberately separate from the production TransactionEngine.
add_executable(subarray_profile_merge_prototype
    experiments/subarray_profile_merge.cpp)
target_compile_options(subarray_profile_merge_prototype
    PRIVATE -Wall -Wextra -Wconversion)

# Isolated model for the exact affine action on prefix-sum points.
add_executable(prefix_sum_affine_model
    experiments/prefix_sum_affine_model.cpp)
target_compile_options(prefix_sum_affine_model
    PRIVATE -Wall -Wextra -Wconversion)

# Lazy affine tags for an implicit convex-chain representation.
add_executable(affine_hull_lazy_prototype
    experiments/affine_hull_lazy.cpp)
target_compile_options(affine_hull_lazy_prototype
    PRIVATE -Wall -Wextra -Wconversion)

if(BUILD_TESTING)
    add_executable(transaction_engine_test tests/transaction_engine_test.cpp)
    target_link_libraries(transaction_engine_test PRIVATE transaction_engine)
    target_compile_options(transaction_engine_test PRIVATE -Wall -Wextra -Wconversion)
    add_test(NAME transaction_engine_test COMMAND transaction_engine_test)
    add_test(NAME subarray_profile_merge_prototype
        COMMAND subarray_profile_merge_prototype)
    add_test(NAME prefix_sum_affine_model
        COMMAND prefix_sum_affine_model)
    add_test(NAME affine_hull_lazy_prototype
        COMMAND affine_hull_lazy_prototype)
endif()

# Compile multiple block-size variants so CI can compare speed and verify
# correctness without changing the default library's public API.
if(TRANSACTION_ENGINE_BUILD_BENCHMARK)
    foreach(BLOCK_SIZE IN ITEMS 16 32 64 128 256 1024)
        add_library(transaction_engine_${BLOCK_SIZE} STATIC src/transaction_engine.cpp)
        target_include_directories(transaction_engine_${BLOCK_SIZE} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_definitions(transaction_engine_${BLOCK_SIZE} PRIVATE
            TRANSACTION_ENGINE_BLOCK_SIZE=${BLOCK_SIZE})
        target_compile_options(transaction_engine_${BLOCK_SIZE} PRIVATE -Wall -Wextra -Wconversion)

        add_executable(transaction_engine_benchmark_${BLOCK_SIZE}
            benchmarks/transaction_engine_benchmark.cpp)
        target_link_libraries(transaction_engine_benchmark_${BLOCK_SIZE}
            PRIVATE transaction_engine_${BLOCK_SIZE})
        target_compile_options(transaction_engine_benchmark_${BLOCK_SIZE}
            PRIVATE -Wall -Wextra -Wconversion)

        if(BUILD_TESTING)
            add_executable(transaction_engine_test_${BLOCK_SIZE}
                tests/transaction_engine_test.cpp)
            target_link_libraries(transaction_engine_test_${BLOCK_SIZE}
                PRIVATE transaction_engine_${BLOCK_SIZE})
            target_compile_options(transaction_engine_test_${BLOCK_SIZE}
                PRIVATE -Wall -Wextra -Wconversion)
            add_test(NAME transaction_engine_test_${BLOCK_SIZE}
                COMMAND transaction_engine_test_${BLOCK_SIZE})
        endif()
    endforeach()

    add_executable(transaction_engine_benchmark benchmarks/transaction_engine_benchmark.cpp)
    target_link_libraries(transaction_engine_benchmark PRIVATE transaction_engine)
    target_compile_options(transaction_engine_benchmark PRIVATE -Wall -Wextra -Wconversion)
endif()
