cmake_minimum_required(VERSION 3.28)
project(gematria_re LANGUAGES C CXX)

# Gematria reverse-engineering backend: C core and ingest, C++ statistics.
# Every target builds warning-free under -Wall -Wextra -Werror; the
# compile_commands.json symlink at the repo root feeds clangd the same flags.

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)  # no C++ modules; keeps clangd's flags plain
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(GEM_WARN -Wall -Wextra -Werror -Wpedantic -Wshadow)

find_package(Threads REQUIRED)

add_library(gem STATIC
  src/core/hebrew.c
  src/core/corpus.c
  src/core/methods.c
  src/ingest/osis.c)
target_include_directories(gem PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(gem PRIVATE ${GEM_WARN})
target_link_libraries(gem PUBLIC Threads::Threads)

add_executable(gem-ingest src/tools/gem_ingest.c)
target_link_libraries(gem-ingest PRIVATE gem)
target_compile_options(gem-ingest PRIVATE ${GEM_WARN})

add_executable(gem-selftest tests/selftest.c)
target_link_libraries(gem-selftest PRIVATE gem)
target_compile_options(gem-selftest PRIVATE ${GEM_WARN})

# C++ statistics (PREREG-001): lexicon, claim auditor, R1, R2.
find_package(OpenSSL REQUIRED)

add_library(gemstats STATIC
  src/stats/lexicon.cpp
  src/stats/replicate.cpp
  src/stats/frozen.cpp
  src/stats/capi.cpp)
target_link_libraries(gemstats PUBLIC gem OpenSSL::Crypto)
target_compile_options(gemstats PRIVATE ${GEM_WARN})
target_compile_definitions(gemstats PRIVATE GEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(gem-stat src/tools/gem_stat.cpp)
target_link_libraries(gem-stat PRIVATE gemstats)
target_compile_options(gem-stat PRIVATE ${GEM_WARN})
