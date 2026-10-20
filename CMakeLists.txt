cmake_minimum_required(VERSION 3.20)

project(Aurora LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AURORA_ENABLE_LTO "Enable link-time optimization" OFF)
option(AURORA_MARCH_NATIVE "Enable -march=native (GCC/Clang)" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options("$<$<CONFIG:Release>:-O3;-DNDEBUG>")
elseif(MSVC)
  add_compile_options("$<$<CONFIG:Release>:/O2;/DNDEBUG>")
endif()

if(AURORA_MARCH_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-march=native)
endif()

if(AURORA_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT aurora_ipo_supported OUTPUT aurora_ipo_error)
  if(aurora_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "LTO requested but not supported: ${aurora_ipo_error}")
  endif()
endif()

find_package(Threads REQUIRED)

add_library(aurora_engine
  src/nnue.cpp
  src/params.cpp
  src/types.cpp
  src/bitboard.cpp
  src/attacks.cpp
  src/magic.cpp
  src/position.cpp
  src/movegen.cpp
  src/makemove.cpp
  src/zobrist.cpp
  src/evaluate.cpp
  src/transposition.cpp
  src/perft.cpp
  src/uci.cpp
  src/search.cpp
  src/play.cpp
)

target_include_directories(aurora_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(aurora_engine PUBLIC Threads::Threads)

add_executable(aurora_cli src/aurora_cli.cpp)
target_link_libraries(aurora_cli PRIVATE aurora_engine)

add_executable(perft src/perft_main.cpp)
target_link_libraries(perft PRIVATE aurora_engine)

add_executable(bench src/bench_main.cpp)
target_link_libraries(bench PRIVATE aurora_engine)

add_executable(uci src/uci_main.cpp)
target_link_libraries(uci PRIVATE aurora_engine)

add_executable(tuner tools/tuner.cpp)
target_link_libraries(tuner PRIVATE aurora_engine)

add_executable(nnue_sample tools/nnue/sample.cpp)
target_link_libraries(nnue_sample PRIVATE aurora_engine)

add_executable(nnue_train tools/nnue/train.cpp)
target_link_libraries(nnue_train PRIVATE aurora_engine)

add_executable(nnue_export tools/nnue/export.cpp)
target_link_libraries(nnue_export PRIVATE aurora_engine)

add_executable(nnue_validate tools/nnue/validate.cpp)
target_link_libraries(nnue_validate PRIVATE aurora_engine)

add_executable(aurora_tests tests/perft_tests.cpp)
target_link_libraries(aurora_tests PRIVATE aurora_engine)

add_executable(aurora_bb_tests tests/bitboard_tests.cpp)
target_link_libraries(aurora_bb_tests PRIVATE aurora_engine)

add_executable(aurora_attacks_tests tests/attacks_tests.cpp)
target_link_libraries(aurora_attacks_tests PRIVATE aurora_engine)

add_executable(aurora_position_tests tests/position_tests.cpp)
target_link_libraries(aurora_position_tests PRIVATE aurora_engine)

add_executable(aurora_move_tests tests/move_tests.cpp)
target_link_libraries(aurora_move_tests PRIVATE aurora_engine)

add_executable(aurora_magic_tests tests/magic_tests.cpp)
target_link_libraries(aurora_magic_tests PRIVATE aurora_engine)

add_executable(aurora_movegen_tests tests/movegen_tests.cpp)
target_link_libraries(aurora_movegen_tests PRIVATE aurora_engine)

add_executable(aurora_legal_tests tests/legal_move_tests.cpp)
target_link_libraries(aurora_legal_tests PRIVATE aurora_engine)

add_executable(aurora_zobrist_tests tests/zobrist_tests.cpp)
target_link_libraries(aurora_zobrist_tests PRIVATE aurora_engine)

add_executable(aurora_search_tests tests/search_tests.cpp)
target_link_libraries(aurora_search_tests PRIVATE aurora_engine)

enable_testing()
add_test(NAME aurora_tests COMMAND aurora_tests)
add_test(NAME aurora_bb_tests COMMAND aurora_bb_tests)
add_test(NAME aurora_attacks_tests COMMAND aurora_attacks_tests)
add_test(NAME aurora_position_tests COMMAND aurora_position_tests)
add_test(NAME aurora_move_tests COMMAND aurora_move_tests)
add_test(NAME aurora_magic_tests COMMAND aurora_magic_tests)
add_test(NAME aurora_movegen_tests COMMAND aurora_movegen_tests)
add_test(NAME aurora_legal_tests COMMAND aurora_legal_tests)
add_test(NAME aurora_zobrist_tests COMMAND aurora_zobrist_tests)
add_test(NAME aurora_search_tests COMMAND aurora_search_tests)