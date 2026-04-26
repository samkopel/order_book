cmake_minimum_required(VERSION 3.14)
project(OrderBooks)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json CONFIG REQUIRED)

if(DEFINED VCPKG_TARGET_TRIPLET)
    include_directories(${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include)
endif()

# --- Core library (shared by server, tests, and benchmarks) ---
add_library(order_book_lib STATIC src/order_book.cpp src/matcher.cpp)
target_include_directories(order_book_lib PUBLIC src)

if(MSVC)
    target_compile_options(order_book_lib PRIVATE /W4 /O2)
else()
    target_compile_options(order_book_lib PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

# --- Server executable ---
add_executable(order_books src/main.cpp)
target_link_libraries(order_books PRIVATE order_book_lib nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(order_books PRIVATE ws2_32)
    target_compile_definitions(order_books PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

if(MSVC)
    target_compile_options(order_books PRIVATE /W4)
else()
    target_compile_options(order_books PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Unit tests ---
find_package(GTest CONFIG REQUIRED)

enable_testing()

add_executable(order_book_tests tests/unit/test_order_book.cpp tests/unit/test_matcher.cpp)
target_link_libraries(order_book_tests PRIVATE order_book_lib GTest::gtest GTest::gtest_main)

if(MSVC)
    target_compile_options(order_book_tests PRIVATE /W4)
else()
    target_compile_options(order_book_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(order_book_tests)

# --- Benchmarks ---
find_package(benchmark CONFIG REQUIRED)

add_executable(order_book_bench tests/bench/bench_order_book.cpp tests/bench/bench_matchers.cpp)
target_link_libraries(order_book_bench PRIVATE order_book_lib benchmark::benchmark benchmark::benchmark_main)

if(MSVC)
    target_compile_options(order_book_bench PRIVATE /W4 /O2)
else()
    target_compile_options(order_book_bench PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()
