// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_wrapper.h>
#include <crypto/sha256.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <crypto/dilithium/ref/api.h>
#include <crypto/dilithium/ref/params.h>
}

namespace {

constexpr char NIST_KAT_RSP_SHA256[] =
    "14f92c48abc0d63ea263cce3c83183c8360c6ede7cbd5b65bd7c6f31e38f0ea5";

#ifndef DILITHIUM_KAT_RSP_PATH
#define DILITHIUM_KAT_RSP_PATH "crypto/dilithium/ref/nistkat/PQCsignKAT_Dilithium2.rsp"
#endif

fs::path DilithiumKatRspPath()
{
    const std::array<fs::path, 4> candidates{
        fs::path{DILITHIUM_KAT_RSP_PATH},
        fs::path{"src"} / DILITHIUM_KAT_RSP_PATH,
        fs::path{".."} / "src" / DILITHIUM_KAT_RSP_PATH,
        fs::path{"../.."} / "src" / DILITHIUM_KAT_RSP_PATH,
    };
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return fs::path{DILITHIUM_KAT_RSP_PATH};
}

std::string ReadLine(std::ifstream& in)
{
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

bool ReadHexField(std::ifstream& in, const std::string& label, std::vector<unsigned char>& out)
{
    const std::string line = ReadLine(in);
    if (line.rfind(label, 0) != 0) {
        return false;
    }
    out = ParseHex(line.substr(label.size()));
    return true;
}

struct NistKatVector
{
    int count{-1};
    std::vector<unsigned char> msg;
    std::vector<unsigned char> pk;
    std::vector<unsigned char> sk;
    std::vector<unsigned char> sm;
};

bool ReadNistKatVector(std::ifstream& in, NistKatVector& vec)
{
    const std::string count_line = ReadLine(in);
    if (count_line.empty()) {
        return false;
    }
    if (count_line.rfind("count = ", 0) != 0) {
        return false;
    }
    vec.count = std::stoi(count_line.substr(std::string{"count = "}.size()));

    std::string seed_line = ReadLine(in);
    if (seed_line.rfind("seed = ", 0) != 0) {
        return false;
    }

    std::string mlen_line = ReadLine(in);
    if (mlen_line.rfind("mlen = ", 0) != 0) {
        return false;
    }

    if (!ReadHexField(in, "msg = ", vec.msg)) {
        return false;
    }
    if (!ReadHexField(in, "pk = ", vec.pk)) {
        return false;
    }
    if (!ReadHexField(in, "sk = ", vec.sk)) {
        return false;
    }

    std::string smlen_line = ReadLine(in);
    if (smlen_line.rfind("smlen = ", 0) != 0) {
        return false;
    }
    if (!ReadHexField(in, "sm = ", vec.sm)) {
        return false;
    }

    ReadLine(in); // trailing blank line
    return true;
}

std::string Sha256HexFile(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    BOOST_REQUIRE(in.good());

    CSHA256 hasher;
    std::array<unsigned char, 4096> buf{};
    while (in.good()) {
        in.read(reinterpret_cast<char*>(buf.data()), buf.size());
        const std::streamsize n = in.gcount();
        if (n > 0) {
            hasher.Write(buf.data(), n);
        }
    }

    unsigned char digest[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(digest);
    return HexStr(Span{digest, CSHA256::OUTPUT_SIZE});
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(dilithium_kat_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(nist_kat_dilithium2_rsp_checksum)
{
    const fs::path kat_path = DilithiumKatRspPath();
    BOOST_REQUIRE_MESSAGE(fs::exists(kat_path), "missing NIST KAT file at " << fs::PathToString(kat_path));
    BOOST_CHECK_EQUAL(Sha256HexFile(kat_path), NIST_KAT_RSP_SHA256);
}

BOOST_AUTO_TEST_CASE(nist_kat_dilithium2_open_and_btq_verify)
{
    const fs::path kat_path = DilithiumKatRspPath();
    BOOST_REQUIRE(fs::exists(kat_path));

    std::ifstream in{kat_path};
    BOOST_REQUIRE(in.good());

    std::string header = ReadLine(in);
    BOOST_REQUIRE_EQUAL(header, "# Dilithium2");
    ReadLine(in); // blank

    int vectors_checked = 0;
    while (in.peek() != EOF) {
        NistKatVector vec;
        BOOST_REQUIRE(ReadNistKatVector(in, vec));

        BOOST_CHECK_EQUAL(vec.pk.size(), pqcrystals_dilithium2_ref_PUBLICKEYBYTES);
        BOOST_CHECK_EQUAL(vec.sk.size(), pqcrystals_dilithium2_ref_SECRETKEYBYTES);
        BOOST_CHECK(vec.sm.size() > pqcrystals_dilithium2_ref_BYTES);

        const size_t mlen = vec.sm.size() - pqcrystals_dilithium2_ref_BYTES;
        BOOST_CHECK_EQUAL(mlen, vec.msg.size());

        std::vector<unsigned char> opened(mlen);
        size_t opened_len = 0;
        BOOST_CHECK_EQUAL(
            pqcrystals_dilithium2_ref_open(opened.data(), &opened_len, vec.sm.data(), vec.sm.size(), nullptr, 0, vec.pk.data()),
            0);
        BOOST_CHECK_EQUAL(opened_len, mlen);
        BOOST_CHECK_EQUAL_COLLECTIONS(opened.begin(), opened.end(), vec.msg.begin(), vec.msg.end());

        BOOST_CHECK_EQUAL(
            btq_dilithium_verify(
                vec.sm.data(),
                pqcrystals_dilithium2_ref_BYTES,
                vec.msg.data(),
                vec.msg.size(),
                nullptr,
                0,
                vec.pk.data()),
            0);

        ++vectors_checked;
    }

    BOOST_CHECK_EQUAL(vectors_checked, 100);
}

BOOST_AUTO_TEST_SUITE_END()
