// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//! Known Answer Tests for BTQ's post-quantum signature scheme.
//!
//! BTQ compiles the vendored pq-crystals tree at DILITHIUM_MODE=2. That tree
//! carries FIPS 204 parameters (TRBYTES 64, RNDBYTES 32, per-level CTILDEBYTES)
//! and takes a context string, so what BTQ actually runs is ML-DSA-44 under
//! Dilithium naming. These vectors come from NIST's ACVP suite and hold us to
//! the standard rather than merely to ourselves: a round-trip sign/verify test
//! passes just as happily on an implementation that is self-consistent and
//! wrong, which is precisely the failure a KAT exists to catch.
//!
//! Only the external/pure interface appears here; that is what
//! btq_dilithium_sign and btq_dilithium_verify use, and it invokes ML-DSA's
//! internal interface underneath, so that path is covered transitively.

#include <crypto/dilithium_wrapper.h>
#include <test/data/mldsa44_acvp.json.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>
#include <univalue.h>

#include <cstdint>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(dilithium_kat_tests, BasicTestingSetup)

namespace {
//! FIPS 204 Table 2 sizes for ML-DSA-44.
constexpr size_t MLDSA44_PUBLIC_KEY_BYTES{1312};
constexpr size_t MLDSA44_SECRET_KEY_BYTES{2560};
constexpr size_t MLDSA44_SIGNATURE_BYTES{2420};
constexpr size_t MLDSA44_SEED_BYTES{32};

UniValue LoadVectors()
{
    UniValue root;
    BOOST_REQUIRE_MESSAGE(root.read(json_tests::mldsa44_acvp) && root.isObject(),
                          "could not parse mldsa44_acvp.json");
    BOOST_REQUIRE_EQUAL(root["parameterSet"].get_str(), "ML-DSA-44");
    return root;
}

std::vector<unsigned char> Hex(const UniValue& v)
{
    return ParseHex(v.get_str());
}
} // namespace

//! If someone retargets the vendored tree at another security level, every
//! vector below becomes meaningless. Fail here first, with a legible reason.
BOOST_AUTO_TEST_CASE(compiled_parameter_set_is_mldsa44)
{
    BOOST_CHECK_EQUAL(size_t{BTQ_DILITHIUM_PUBLIC_KEY_SIZE}, MLDSA44_PUBLIC_KEY_BYTES);
    BOOST_CHECK_EQUAL(size_t{BTQ_DILITHIUM_SECRET_KEY_SIZE}, MLDSA44_SECRET_KEY_BYTES);
    BOOST_CHECK_EQUAL(size_t{BTQ_DILITHIUM_SIGNATURE_SIZE}, MLDSA44_SIGNATURE_BYTES);
    BOOST_CHECK_EQUAL(size_t{BTQ_DILITHIUM_SEED_SIZE}, MLDSA44_SEED_BYTES);
}

//! ML-DSA.KeyGen. This is also BTQ's HD-wallet derivation entry point: every
//! wallet key is produced by expanding a 32-byte seed through this function, so
//! conformance here is what makes BTQ keys reproducible by other ML-DSA tooling.
BOOST_AUTO_TEST_CASE(mldsa44_acvp_keygen)
{
    const UniValue root{LoadVectors()};
    const UniValue& cases{root["keyGen"]};
    BOOST_REQUIRE(cases.isArray() && cases.size() > 0);

    for (size_t i = 0; i < cases.size(); ++i) {
        const UniValue& tc{cases[i]};
        const std::string label{"keyGen tcId " + tc["tcId"].getValStr()};

        const std::vector<unsigned char> seed{Hex(tc["seed"])};
        BOOST_REQUIRE_EQUAL(seed.size(), MLDSA44_SEED_BYTES);

        std::vector<unsigned char> pk(MLDSA44_PUBLIC_KEY_BYTES);
        std::vector<unsigned char> sk(MLDSA44_SECRET_KEY_BYTES);
        BOOST_REQUIRE_MESSAGE(
            btq_dilithium_keypair_from_seed(pk.data(), sk.data(), seed.data()) == 0,
            label + ": key generation failed");

        BOOST_CHECK_MESSAGE(HexStr(pk) == tc["pk"].get_str(), label + ": public key mismatch");
        BOOST_CHECK_MESSAGE(HexStr(sk) == tc["sk"].get_str(), label + ": secret key mismatch");
    }
}

//! ML-DSA.Sign with a context string, deterministic variant (rnd = 0^32).
BOOST_AUTO_TEST_CASE(mldsa44_acvp_siggen)
{
    const UniValue root{LoadVectors()};
    const UniValue& cases{root["sigGen"]};
    BOOST_REQUIRE(cases.isArray() && cases.size() > 0);

    for (size_t i = 0; i < cases.size(); ++i) {
        const UniValue& tc{cases[i]};
        const std::string label{"sigGen tcId " + tc["tcId"].getValStr()};

        const std::vector<unsigned char> sk{Hex(tc["sk"])};
        const std::vector<unsigned char> ctx{Hex(tc["context"])};
        const std::vector<unsigned char> msg{Hex(tc["message"])};
        BOOST_REQUIRE_EQUAL(sk.size(), MLDSA44_SECRET_KEY_BYTES);

        std::vector<unsigned char> sig(MLDSA44_SIGNATURE_BYTES);
        size_t siglen{0};
        BOOST_REQUIRE_MESSAGE(
            btq_dilithium_sign(sig.data(), &siglen, msg.data(), msg.size(),
                               ctx.data(), ctx.size(), sk.data()) == 0,
            label + ": signing failed");
        BOOST_REQUIRE_EQUAL(siglen, MLDSA44_SIGNATURE_BYTES);

        BOOST_CHECK_MESSAGE(HexStr(sig) == tc["signature"].get_str(),
                            label + ": signature does not match the NIST vector");
    }
}

//! ML-DSA.Verify. The negative cases matter more than the positive ones: they
//! pin down what the verifier must *reject*, which is the direction a consensus
//! rule can be attacked from. Each carries the ACVP rejection reason so a
//! failure names the specific malleability that got through.
BOOST_AUTO_TEST_CASE(mldsa44_acvp_sigver)
{
    const UniValue root{LoadVectors()};
    const UniValue& cases{root["sigVer"]};
    BOOST_REQUIRE(cases.isArray() && cases.size() > 0);

    size_t accepted_cases{0};
    size_t rejected_cases{0};

    for (size_t i = 0; i < cases.size(); ++i) {
        const UniValue& tc{cases[i]};
        const bool should_verify{tc["testPassed"].get_bool()};
        const std::string label{"sigVer tcId " + tc["tcId"].getValStr() +
                                " (" + tc["reason"].get_str() + ")"};

        const std::vector<unsigned char> pk{Hex(tc["pk"])};
        const std::vector<unsigned char> ctx{Hex(tc["context"])};
        const std::vector<unsigned char> msg{Hex(tc["message"])};
        const std::vector<unsigned char> sig{Hex(tc["signature"])};
        BOOST_REQUIRE_EQUAL(pk.size(), MLDSA44_PUBLIC_KEY_BYTES);

        const bool verified{btq_dilithium_verify(sig.data(), sig.size(),
                                                 msg.data(), msg.size(),
                                                 ctx.data(), ctx.size(),
                                                 pk.data()) == 0};

        BOOST_CHECK_MESSAGE(verified == should_verify,
                            label + (should_verify ? ": valid signature was rejected"
                                                   : ": invalid signature was accepted"));
        should_verify ? ++accepted_cases : ++rejected_cases;
    }

    // A fixture that lost its negative cases would still pass every assertion
    // above while testing almost nothing.
    BOOST_CHECK_MESSAGE(accepted_cases > 0, "no must-accept vectors present");
    BOOST_CHECK_MESSAGE(rejected_cases > 0, "no must-reject vectors present");
}

//! BTQ builds with DILITHIUM_RANDOMIZED_SIGNING off, so signing is the
//! deterministic ML-DSA variant. Upstream flipped its own default to hedged in
//! September 2024; we did not follow, and that should stay a decision rather
//! than a drift. Enabling hedged signing breaks this test and the sigGen
//! vectors above, which is the intended way to find out.
BOOST_AUTO_TEST_CASE(signing_is_deterministic)
{
    const UniValue root{LoadVectors()};
    const UniValue& tc{root["sigGen"][0]};

    const std::vector<unsigned char> sk{Hex(tc["sk"])};
    const std::vector<unsigned char> ctx{Hex(tc["context"])};
    const std::vector<unsigned char> msg{Hex(tc["message"])};

    std::vector<unsigned char> first(MLDSA44_SIGNATURE_BYTES);
    std::vector<unsigned char> second(MLDSA44_SIGNATURE_BYTES);
    size_t first_len{0}, second_len{0};

    BOOST_REQUIRE_EQUAL(btq_dilithium_sign(first.data(), &first_len, msg.data(), msg.size(),
                                           ctx.data(), ctx.size(), sk.data()), 0);
    BOOST_REQUIRE_EQUAL(btq_dilithium_sign(second.data(), &second_len, msg.data(), msg.size(),
                                           ctx.data(), ctx.size(), sk.data()), 0);

    BOOST_CHECK_EQUAL(first_len, second_len);
    BOOST_CHECK(first == second);
}

//! The context string is a FIPS 204 feature and is domain-separating: a
//! signature made under one context must not verify under another.
BOOST_AUTO_TEST_CASE(context_string_is_binding)
{
    const UniValue root{LoadVectors()};
    const std::vector<unsigned char> msg{Hex(root["sigGen"][0]["message"])};

    const std::vector<unsigned char> ctx_a{'B', 'T', 'Q', '-', 'A'};
    const std::vector<unsigned char> ctx_b{'B', 'T', 'Q', '-', 'B'};

    const std::vector<unsigned char> seed{Hex(root["keyGen"][0]["seed"])};
    std::vector<unsigned char> gen_pk(MLDSA44_PUBLIC_KEY_BYTES);
    std::vector<unsigned char> gen_sk(MLDSA44_SECRET_KEY_BYTES);
    BOOST_REQUIRE_EQUAL(btq_dilithium_keypair_from_seed(gen_pk.data(), gen_sk.data(), seed.data()), 0);

    std::vector<unsigned char> sig(MLDSA44_SIGNATURE_BYTES);
    size_t siglen{0};
    BOOST_REQUIRE_EQUAL(btq_dilithium_sign(sig.data(), &siglen, msg.data(), msg.size(),
                                           ctx_a.data(), ctx_a.size(), gen_sk.data()), 0);

    BOOST_CHECK_EQUAL(btq_dilithium_verify(sig.data(), siglen, msg.data(), msg.size(),
                                           ctx_a.data(), ctx_a.size(), gen_pk.data()), 0);
    BOOST_CHECK_NE(btq_dilithium_verify(sig.data(), siglen, msg.data(), msg.size(),
                                        ctx_b.data(), ctx_b.size(), gen_pk.data()), 0);
    BOOST_CHECK_NE(btq_dilithium_verify(sig.data(), siglen, msg.data(), msg.size(),
                                        nullptr, 0, gen_pk.data()), 0);
}

BOOST_AUTO_TEST_SUITE_END()
