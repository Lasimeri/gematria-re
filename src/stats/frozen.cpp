// frozen.cpp - verify the corpus and the preregistration files against their
// frozen sha256 (OpenSSL EVP). Every tool that reports a statistic prints
// this status; a result is a PREREG result only when it reads VERIFIED.
#include "gem/stats.hpp"

#include <openssl/evp.h>

#include <cstdio>
#include <fstream>
#include <iterator>

#ifndef GEM_SOURCE_DIR
#error "GEM_SOURCE_DIR must name the repository root"
#endif

namespace gem {

namespace {

constexpr const char *kCorpusSha = "46ce61ff38c678231f7283b13085a5278bee631ea6b40db62ec2737eccf86575";
constexpr const char *kPreregs[] = {"PREREG-001", "PREREG-002"};

std::string sha256_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "unreadable";
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    if (!EVP_Digest(data.data(), data.size(), md, &n, EVP_sha256(), nullptr))
        return "digest-failed";
    std::string hex;
    char b[3];
    for (unsigned i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, "%02x", md[i]);
        hex += b;
    }
    return hex;
}

}  // namespace

std::string frozen_status(const std::string &corpus_path, bool *ok_out)
{
    std::string got_corpus = sha256_file(corpus_path);
    bool ok = got_corpus == kCorpusSha;
    std::string line = "corpus " + got_corpus.substr(0, 12) + (ok ? " ok" : " MISMATCH");
    for (const char *id : kPreregs) {
        const std::string base = std::string(GEM_SOURCE_DIR) + "/prereg/" + id;
        std::ifstream f(base + ".sha256");
        std::string want;
        f >> want;
        std::string got = sha256_file(base + ".md");
        ok &= got == want;
        line += std::string(", ") + id + " " + got.substr(0, 12) + (got == want ? " ok" : " MISMATCH");
    }
    line += ok ? " -> VERIFIED" : " -> NOT VERIFIED";
    if (ok_out)
        *ok_out = ok;
    return line;
}

}  // namespace gem
