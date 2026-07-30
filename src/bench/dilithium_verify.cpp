// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <cassert>
#include <vector>

// Signature verification cost, Dilithium against ECDSA, measured on the same
// hardware over the same input.
//
// This exists because two consensus constants encode a ratio between the two
// that has never been measured. DILITHIUM_SIGOP_COST (src/script/script.h)
// charges a Dilithium check 50 sigops, asserting that one Dilithium
// verification costs about fifty ECDSA verifications. The reasoning recorded
// when that was chosen cited a figure closer to 10. Both cannot be right, and
// the constant bounds how many post-quantum signatures a block can carry:
// MAX_BLOCK_SIGOPS_COST / WITNESS_SCALE_FACTOR legacy sigops, divided by
// whatever a Dilithium op is charged.
//
// Run both and divide the reported ns/op:
//
//     ./src/bench/bench_btq -filter='(Dilithium|ECDSA)Verify'
//
// The ratio is what DILITHIUM_SIGOP_COST should approximate. Verification is
// the operation to measure rather than signing, because validation cost is what
// the sigop budget exists to bound -- every node verifies every signature in
// every block, while signing happens once on the spending wallet.
//
// Both benchmarks verify a *valid* signature and assert as much. An invalid one
// can be rejected early on a size or format check without the verification
// maths ever running, which would time a rejection path and understate the true
// cost by an arbitrary margin.

static void DilithiumVerify(benchmark::Bench& bench)
{
    CDilithiumKey key;
    const bool made{key.MakeNewKey()};
    assert(made);

    const CDilithiumPubKey pubkey{key.GetPubKey()};
    const uint256 hash{uint256S("0x1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f809")};

    std::vector<unsigned char> vchSig;
    const bool signed_ok{key.Sign(hash, vchSig)};
    assert(signed_ok);
    // Guard against timing a rejection path rather than a verification.
    assert(pubkey.Verify(hash, vchSig));

    bench.run([&] {
        const bool ok{pubkey.Verify(hash, vchSig)};
        assert(ok);
    });
}

static void ECDSAVerify(benchmark::Bench& bench)
{
    ECC_Start();

    CKey key;
    key.MakeNewKey(/*fCompressedIn=*/true);
    assert(key.IsValid());

    const CPubKey pubkey{key.GetPubKey()};
    const uint256 hash{uint256S("0x1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f809")};

    std::vector<unsigned char> vchSig;
    const bool signed_ok{key.Sign(hash, vchSig)};
    assert(signed_ok);
    // Same guard as above.
    assert(pubkey.Verify(hash, vchSig));

    bench.run([&] {
        const bool ok{pubkey.Verify(hash, vchSig)};
        assert(ok);
    });

    ECC_Stop();
}

BENCHMARK(DilithiumVerify, benchmark::PriorityLevel::HIGH);
BENCHMARK(ECDSAVerify, benchmark::PriorityLevel::HIGH);
