cmake_minimum_required(VERSION 3.14)
project(TradingGame CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 CONFIG REQUIRED)

set(TRADER_SOURCES cpp_engine/order_book.cpp cpp_engine/traders.cpp)

add_executable(TradingEngine 
    cpp_engine/main.cpp 
    cpp_engine/order_book.cpp
    cpp_engine/traders.cpp)

target_include_directories(TradingEngine PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(TradingEngine PRIVATE 
    Threads::Threads 
    pybind11::embed 
    ${Python3_LIBRARIES}
)

pybind11_add_module(engine_backend cpp_engine/bindings.cpp)


add_executable(OrderBookTests cpp_engine/test_engine.cpp cpp_engine/order_book.cpp)
target_link_libraries(OrderBookTests PRIVATE Threads::Threads)

add_executable(TraderTests cpp_engine/test_traders.cpp ${TRADER_SOURCES})
target_link_libraries(TraderTests PRIVATE 
    Threads::Threads 
    pybind11::embed 
    ${Python3_LIBRARIES}
)

add_executable(OrderBookBenchmark cpp_engine/benchmark.cpp cpp_engine/order_book.cpp)
target_link_libraries(OrderBookBenchmark PRIVATE Threads::Threads)
# Benchmarks are meaningless unoptimised, so force -O3 regardless of CMAKE_BUILD_TYPE
if(NOT MSVC)
    target_compile_options(OrderBookBenchmark PRIVATE -O3)
endif()
