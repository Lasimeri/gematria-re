// kab_cmd.hpp - the commands of gem-kab, one source file per stage. Each
// receives the loaded corpora and the arguments after the command name and
// returns the process exit code.
#pragma once

#include "gem/kab/corpus.hpp"

#include <span>

namespace kabtool {

using Args = std::span<char *const>;

// "1,234,567"
std::string commas(uint64_t n);
// Corpora a type occurs in, e.g. "WLC+SY".
std::string corpora_of(const gem::kab::Corpus &c, uint32_t t);
// Agreement mask as four characters, e.g. "H-KS" (- = the system differs).
std::string mask_name(unsigned mask);

int cmd_index(const gem::kab::Corpus &c, Args args);    // kab_index.cpp
int cmd_value(const gem::kab::Corpus &c, Args args);    // kab_index.cpp
int cmd_vectors(const gem::kab::Corpus &c, Args args);  // kab_vectors.cpp
int cmd_near(const gem::kab::Corpus &c, Args args);     // kab_vectors.cpp
int cmd_graph(const gem::kab::Corpus &c, Args args);    // kab_graph.cpp
int cmd_verify(const gem::kab::Corpus &c, Args args);   // kab_graph.cpp
int cmd_pair(const gem::kab::Corpus &c, Args args);     // kab_graph.cpp
int cmd_dump(const gem::kab::Corpus &c, Args args);     // kab_dump.cpp
int cmd_bench(const gem::kab::Corpus &c, Args args);    // kab_bench.cpp

}  // namespace kabtool
