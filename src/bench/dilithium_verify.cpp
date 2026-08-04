// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <bench/bench.h>
#include <crypto/dilithium_key.h>
#include <key.h>
#include <policy/policy.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <test/util/transaction_utils.h>
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

// The two benchmarks above measure the signature primitives in isolation. That
// is not the unit the sigop budget charges.
//
// DILITHIUM_SIGOP_COST is applied per signature *opcode inside a script*, by
// CScript::GetSigOpCount, reached for P2MR through WitnessSigOps ->
// CountWitnessSigOps -> GetTransactionSigOpCost, and enforced against
// MAX_BLOCK_SIGOPS_COST in ConnectBlock. So what a node actually pays per
// charged sigop is a whole input verification: witness parsing, control-block
// and Merkle-path checking, sighash computation over the transaction, script
// execution, and only then the signature check.
//
// That overhead is substantial and largely algorithm-independent, so it dilutes
// the primitive ratio. Measuring only the primitive overstates how much dearer a
// Dilithium input is than an ECDSA one. The two benchmarks below verify complete
// inputs through VerifyScript, which is the comparison the constant should be
// derived from:
//
//     ./src/bench/bench_btq -filter='(P2MRDilithium|P2WPKHECDSA)Input'
//
// Both are built with the same crediting/spending transaction helpers and run
// through the same VerifyScript entry point, so they differ only in output type
// and signature algorithm.

static void P2MRDilithiumInput(benchmark::Bench& bench)
{
    ECC_Start();

    CDilithiumKey key;
    const bool made{key.MakeNewKey()};
    assert(made);
    const CDilithiumPubKey pubkey{key.GetPubKey()};

    // Single-leaf P2MR tree: <pubkey> OP_CHECKSIGDILITHIUM.
    const CScript leaf_script{CScript() << ToByteVector(pubkey) << OP_CHECKSIGDILITHIUM};
    const std::vector<unsigned char> leaf_bytes{leaf_script.begin(), leaf_script.end()};

    P2MRBuilder builder;
    builder.Add(/*depth=*/0, leaf_bytes, TAPROOT_LEAF_TAPSCRIPT);
    builder.Finalize();
    assert(builder.IsComplete());
    const WitnessV2P2MR output{builder.GetOutput()};
    const P2MRSpendData spenddata{builder.GetSpendData()};

    const CScript script_pubkey{GetScriptForDestination(output)};
    const CAmount amount{1};

    const CMutableTransaction tx_credit{BuildCreditingTransaction(script_pubkey, amount)};
    CMutableTransaction tx_spend{BuildSpendingTransaction(CScript(), CScriptWitness(), CTransaction(tx_credit))};

    const auto script_it{spenddata.scripts.find({leaf_bytes, TAPROOT_LEAF_TAPSCRIPT})};
    assert(script_it != spenddata.scripts.end());
    assert(!script_it->second.empty());
    const std::vector<unsigned char> control{*script_it->second.begin()};

    PrecomputedTransactionData txdata;
    // force=true: Init otherwise decides which precomputations to build by
    // inspecting the witness, which is not populated until the signature below
    // exists -- and the signature needs the sighash this cache feeds.
    txdata.Init(tx_spend, {CTxOut{tx_credit.vout[0]}}, /*force=*/true);

    ScriptExecutionData execdata;
    execdata.m_tapleaf_hash_init = true;
    execdata.m_tapleaf_hash = ComputeTapleafHash(TAPROOT_LEAF_TAPSCRIPT, leaf_bytes);
    execdata.m_codeseparator_pos_init = true;
    execdata.m_codeseparator_pos = 0xFFFFFFFF;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false;

    uint256 sighash;
    const bool hashed{SignatureHashSchnorr(sighash, execdata, tx_spend, 0, SIGHASH_ALL,
                                           SigVersion::P2MR_TAPSCRIPT, txdata,
                                           MissingDataBehavior::ASSERT_FAIL)};
    assert(hashed);

    std::vector<unsigned char> sig;
    const bool signed_ok{key.Sign(sighash, sig)};
    assert(signed_ok);
    sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));

    // P2MR witness: [args...] [script] [control block]
    CScriptWitness& witness{tx_spend.vin[0].scriptWitness};
    witness.stack.clear();
    witness.stack.push_back(sig);
    witness.stack.push_back(leaf_bytes);
    witness.stack.push_back(control);

    const CTransaction ctx{tx_spend};
    const uint32_t flags{STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM | SCRIPT_VERIFY_P2MR};

    // Same guard as the primitive benchmarks: a spend that fails to verify can
    // bail out long before the Dilithium check and would time a rejection path.
    {
        ScriptError err{SCRIPT_ERR_OK};
        const bool ok{VerifyScript(CScript(), script_pubkey, &ctx.vin[0].scriptWitness, flags,
                                   TransactionSignatureChecker(&ctx, 0, amount, txdata,
                                                               MissingDataBehavior::ASSERT_FAIL),
                                   &err)};
        assert(err == SCRIPT_ERR_OK);
        assert(ok);
    }

    bench.run([&] {
        ScriptError err{SCRIPT_ERR_OK};
        const bool ok{VerifyScript(CScript(), script_pubkey, &ctx.vin[0].scriptWitness, flags,
                                   TransactionSignatureChecker(&ctx, 0, amount, txdata,
                                                               MissingDataBehavior::ASSERT_FAIL),
                                   &err)};
        assert(ok);
    });

    ECC_Stop();
}

static void P2WPKHECDSAInput(benchmark::Bench& bench)
{
    ECC_Start();

    CKey key;
    key.MakeNewKey(/*fCompressedIn=*/true);
    assert(key.IsValid());
    const CPubKey pubkey{key.GetPubKey()};

    uint160 pubkey_hash;
    CHash160().Write(pubkey).Finalize(pubkey_hash);

    const CScript script_pubkey{CScript() << OP_0 << ToByteVector(pubkey_hash)};
    const CScript exec_script{CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkey_hash)
                                        << OP_EQUALVERIFY << OP_CHECKSIG};
    const CAmount amount{1};

    const CMutableTransaction tx_credit{BuildCreditingTransaction(script_pubkey, amount)};
    CMutableTransaction tx_spend{BuildSpendingTransaction(CScript(), CScriptWitness(), CTransaction(tx_credit))};

    PrecomputedTransactionData txdata;
    // force=true: Init otherwise decides which precomputations to build by
    // inspecting the witness, which is not populated until the signature below
    // exists -- and the signature needs the sighash this cache feeds.
    txdata.Init(tx_spend, {CTxOut{tx_credit.vout[0]}}, /*force=*/true);

    std::vector<unsigned char> sig;
    const bool signed_ok{key.Sign(SignatureHash(exec_script, tx_spend, 0, SIGHASH_ALL, amount,
                                                SigVersion::WITNESS_V0),
                                  sig)};
    assert(signed_ok);
    sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));

    CScriptWitness& witness{tx_spend.vin[0].scriptWitness};
    witness.stack.clear();
    witness.stack.push_back(sig);
    witness.stack.push_back(ToByteVector(pubkey));

    const CTransaction ctx{tx_spend};
    const uint32_t flags{STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DILITHIUM | SCRIPT_VERIFY_P2MR};

    {
        ScriptError err{SCRIPT_ERR_OK};
        const bool ok{VerifyScript(CScript(), script_pubkey, &ctx.vin[0].scriptWitness, flags,
                                   TransactionSignatureChecker(&ctx, 0, amount, txdata,
                                                               MissingDataBehavior::ASSERT_FAIL),
                                   &err)};
        assert(err == SCRIPT_ERR_OK);
        assert(ok);
    }

    bench.run([&] {
        ScriptError err{SCRIPT_ERR_OK};
        const bool ok{VerifyScript(CScript(), script_pubkey, &ctx.vin[0].scriptWitness, flags,
                                   TransactionSignatureChecker(&ctx, 0, amount, txdata,
                                                               MissingDataBehavior::ASSERT_FAIL),
                                   &err)};
        assert(ok);
    });

    ECC_Stop();
}

BENCHMARK(DilithiumVerify, benchmark::PriorityLevel::HIGH);
BENCHMARK(ECDSAVerify, benchmark::PriorityLevel::HIGH);
BENCHMARK(P2MRDilithiumInput, benchmark::PriorityLevel::HIGH);
BENCHMARK(P2WPKHECDSAInput, benchmark::PriorityLevel::HIGH);
