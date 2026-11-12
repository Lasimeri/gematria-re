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
  src/stats/cluster.cpp
  src/stats/els.cpp
  src/stats/order.cpp
  src/stats/capi.cpp)
target_link_libraries(gemstats PUBLIC gem OpenSSL::Crypto)
target_compile_options(gemstats PRIVATE ${GEM_WARN})
target_compile_definitions(gemstats PRIVATE GEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(gem-pairs-test tests/pairs_test.cpp)
target_compile_options(gem-pairs-test PRIVATE ${GEM_WARN})

# Host build of the card A/B kernel program (the same source card/build.sh
# compiles for the 7120A); -march=native for the strongest host opponent.
add_executable(els-card-host card/els_card.c)
target_compile_options(els-card-host PRIVATE ${GEM_WARN} -march=native)
target_link_libraries(els-card-host PRIVATE Threads::Threads)

add_executable(gem-els src/tools/gem_els.cpp)
target_link_libraries(gem-els PRIVATE gemstats)
target_compile_options(gem-els PRIVATE ${GEM_WARN})

add_executable(gem-stat src/tools/gem_stat.cpp)
target_link_libraries(gem-stat PRIVATE gemstats)
target_compile_options(gem-stat PRIVATE ${GEM_WARN})

# Stage K (pure C++, no link to the C core): hardcoded letter tables, UTF-8
# corpora, value indexes. gem-export writes the GEMC witness as the UTF-8
# text gem-kab loads.
add_library(gemkab STATIC
  src/kab/corpus.cpp
  src/kab/vectors.cpp
  src/kab/graph.cpp)
target_include_directories(gemkab PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(gemkab PRIVATE ${GEM_WARN})

add_executable(gem-kab
  src/tools/gem_kab.cpp
  src/tools/kab_index.cpp
  src/tools/kab_vectors.cpp
  src/tools/kab_graph.cpp
  src/tools/kab_dump.cpp
  src/tools/kab_bench.cpp)
target_link_libraries(gem-kab PRIVATE gemkab)
target_compile_options(gem-kab PRIVATE ${GEM_WARN})

add_executable(gem-export src/tools/gem_export.c)
target_link_libraries(gem-export PRIVATE gem)
target_compile_options(gem-export PRIVATE ${GEM_WARN})

# The C core as the oracle of the C++ tables and loader.
add_executable(gem-kab-test tests/kab_test.cpp)
target_link_libraries(gem-kab-test PRIVATE gemkab gem)
target_compile_options(gem-kab-test PRIVATE ${GEM_WARN})
