// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/descriptor.h>
#include <script/solver.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/check.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>

#include <memory>
#include <set>
#include <variant>

#include <boost/test/unit_test.hpp>

namespace wallet {
namespace {
std::unique_ptr<CWallet> CreateDescriptorOnlyWallet(interfaces::Chain* chain)
{
    auto wallet = std::make_unique<CWallet>(chain, "", CreateMockableWalletDatabase());
    wallet->LoadWallet();
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetupDescriptorScriptPubKeyMans();
    return wallet;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(descriptor_p2sh_segwit_tests, BasicTestingSetup)

/** BTQ-AUDIT-006: descriptor wallets must estimate fees when spending p2sh-segwit UTXOs. */
BOOST_AUTO_TEST_CASE(fee_estimation)
{
    auto wallet = CreateDescriptorOnlyWallet(m_node.chain.get());

    CScript p2sh_script;
    CTxDestination legacy_dest;
    {
        LOCK(wallet->cs_wallet);
        const CTxDestination p2sh_dest = *Assert(wallet->GetNewDestination(OutputType::P2SH_SEGWIT, "p2sh-segwit"));
        p2sh_script = GetScriptForDestination(p2sh_dest);
        legacy_dest = *Assert(wallet->GetNewDestination(OutputType::LEGACY, "legacy"));
    }

    const CTxOut p2sh_out(COIN, p2sh_script);

    LOCK(wallet->cs_wallet);
    const std::set<ScriptPubKeyMan*> spk_mans = wallet->GetScriptPubKeyMans(p2sh_script);
    BOOST_CHECK(!spk_mans.empty());
    for (const ScriptPubKeyMan* spk_man : spk_mans) {
        const std::unique_ptr<SigningProvider> provider = spk_man->GetSolvingProvider(p2sh_script);
        BOOST_CHECK(provider != nullptr);
        BOOST_CHECK(InferDescriptor(p2sh_script, *provider) != nullptr);
    }

    const int input_vsize = CalculateMaximumSignedInputSize(p2sh_out, wallet.get(), /*coin_control=*/nullptr);
    BOOST_CHECK_MESSAGE(input_vsize > 0, "CalculateMaximumSignedInputSize must succeed for p2sh-segwit");

    // CreateTransaction uses CalculateMaximumSignedTxSize with explicit txouts (the -6 regression path).
    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint(uint256::ONE, 0), CScript{});
    mtx.vout.emplace_back(COIN / 2, GetScriptForDestination(legacy_dest));
    const TxSize tx_size = CalculateMaximumSignedTxSize(CTransaction{mtx}, wallet.get(), {p2sh_out}, /*coin_control=*/nullptr);
    BOOST_CHECK_MESSAGE(tx_size.vsize > 0, "CalculateMaximumSignedTxSize must succeed for p2sh-segwit input");
}

/** Every output type in a mixed descriptor wallet must be fee-estimable (coin selection picks any). */
BOOST_AUTO_TEST_CASE(mixed_wallet_input_fee_estimation)
{
    auto wallet = CreateDescriptorOnlyWallet(m_node.chain.get());

    struct Case {
        OutputType type;
        const char* label;
    };
    const Case cases[] = {
        {OutputType::LEGACY, "legacy"},
        {OutputType::P2SH_SEGWIT, "p2sh-segwit"},
        {OutputType::BECH32, "bech32"},
        {OutputType::BECH32M, "bech32m"},
    };

    LOCK(wallet->cs_wallet);
    for (const auto& c : cases) {
        const CTxDestination dest = *Assert(wallet->GetNewDestination(c.type, c.label));
        const CScript script = GetScriptForDestination(dest);
        const CTxOut out(COIN, script);
        const int input_vsize = CalculateMaximumSignedInputSize(out, wallet.get(), /*coin_control=*/nullptr);
        BOOST_CHECK_MESSAGE(input_vsize > 0, strprintf("fee estimate failed for %s", c.label));
    }

    // P2MR is the wallet's quantum-safe output type and the only Dilithium
    // destination it will issue on a P2MR-only chain. It must be fee-estimable
    // for the same reason as the four above: coin selection can pick it, and
    // TransactionChangeType now returns it for Dilithium sends.
    {
        const auto dest = wallet->GetNewDestination(OutputType::P2MR, "p2mr");
        BOOST_REQUIRE_MESSAGE(dest, strprintf("GetNewDestination failed for p2mr: %s",
                                              util::ErrorString(dest).original));
        BOOST_CHECK(std::holds_alternative<WitnessV2P2MR>(*dest));
        BOOST_CHECK(IsValidDestination(*dest));

        const CTxOut out(COIN, GetScriptForDestination(*dest));
        const int input_vsize = CalculateMaximumSignedInputSize(out, wallet.get(), /*coin_control=*/nullptr);
        BOOST_CHECK_MESSAGE(input_vsize > 0, "fee estimate failed for p2mr");
    }

    // This case used to record the #97 defect instead of the property it is
    // named for: the wallet issued a dilithium-legacy destination on a chain
    // where such an output is neither a valid payment destination nor sizeable,
    // and the two halves were asserted so the defect stayed visible. Both are
    // now unreachable, because generation is refused. What remains is the
    // refusal, in both spk manager kinds.
    //
    // This chain sets nDilithiumP2MRHeight, so Dilithium is P2MR-only here.
    BOOST_REQUIRE(!LegacyDilithiumBase58PaymentsAllowed());
    {
        ScriptPubKeyMan* spk_man = wallet->GetScriptPubKeyMan(OutputType::LEGACY, /*internal=*/false);
        BOOST_REQUIRE(spk_man != nullptr);
        BOOST_CHECK(!spk_man->GetNewDestination(OutputType::DILITHIUM_LEGACY));
        BOOST_CHECK(!wallet->GetNewDestination(OutputType::DILITHIUM_LEGACY, "dilithium-legacy"));
    }

    // dilithium-bech32 is refused by design: a witness v0 keyhash program is
    // indistinguishable from ECDSA P2WPKH and would not be spendable with a
    // Dilithium key, so handing one out would create unspendable funds. Both
    // LegacyScriptPubKeyMan::GetNewDestination and
    // DescriptorScriptPubKeyMan::GetNewDestination refuse it. This wallet is
    // descriptor-only, so the descriptor one is what gets asserted here. This
    // case previously asked for the address and so contradicted the decision
    // rather than covering it.
    {
        ScriptPubKeyMan* spk_man = wallet->GetScriptPubKeyMan(OutputType::BECH32, /*internal=*/false);
        BOOST_REQUIRE(spk_man != nullptr);
        BOOST_CHECK(!spk_man->GetNewDestination(OutputType::DILITHIUM_BECH32));
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
