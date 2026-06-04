// Copyright (c) 2021-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/fees.h>
#include <script/descriptor.h>
#include <script/solver.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

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

BOOST_FIXTURE_TEST_SUITE(spend_tests, WalletTestingSetup)

BOOST_FIXTURE_TEST_CASE(SubtractFee, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    // Check that a subtract-from-recipient transaction slightly less than the
    // coinbase input amount does not create a change output (because it would
    // be uneconomical to add and spend the output), and make sure it pays the
    // leftover input amount which would have been change to the recipient
    // instead of the miner.
    auto check_tx = [&wallet](CAmount leftover_input_amount) {
        CRecipient recipient{PubKeyDestination({}), 50 * COIN - leftover_input_amount, /*subtract_fee=*/true};
        constexpr int RANDOM_CHANGE_POSITION = -1;
        CCoinControl coin_control;
        coin_control.m_feerate.emplace(10000);
        coin_control.fOverrideFeeRate = true;
        // We need to use a change type with high cost of change so that the leftover amount will be dropped to fee instead of added as a change output
        coin_control.m_change_type = OutputType::LEGACY;
        auto res = CreateTransaction(*wallet, {recipient}, RANDOM_CHANGE_POSITION, coin_control);
        BOOST_CHECK(res);
        const auto& txr = *res;
        BOOST_CHECK_EQUAL(txr.tx->vout.size(), 1);
        BOOST_CHECK_EQUAL(txr.tx->vout[0].nValue, recipient.nAmount + leftover_input_amount - txr.fee);
        BOOST_CHECK_GT(txr.fee, 0);
        return txr.fee;
    };

    // Send full input amount to recipient, check that only nonzero fee is
    // subtracted (to_reduce == fee).
    const CAmount fee{check_tx(0)};

    // Send slightly less than full input amount to recipient, check leftover
    // input amount is paid to recipient not the miner (to_reduce == fee - 123)
    BOOST_CHECK_EQUAL(fee, check_tx(123));

    // Send full input minus fee amount to recipient, check leftover input
    // amount is paid to recipient not the miner (to_reduce == 0)
    BOOST_CHECK_EQUAL(fee, check_tx(fee));

    // Send full input minus more than the fee amount to recipient, check
    // leftover input amount is paid to recipient not the miner (to_reduce ==
    // -123). This overpays the recipient instead of overpaying the miner more
    // than double the necessary fee.
    BOOST_CHECK_EQUAL(fee, check_tx(fee + 123));
}

BOOST_FIXTURE_TEST_CASE(wallet_duplicated_preset_inputs_test, TestChain100Setup)
{
    // Verify that the wallet's Coin Selection process does not include pre-selected inputs twice in a transaction.

    // Add 4 spendable UTXO, 50 BTQ each, to the wallet (total balance 200 BTQ)
    for (int i = 0; i < 4; i++) CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    LOCK(wallet->cs_wallet);
    auto available_coins = AvailableCoins(*wallet);
    std::vector<COutput> coins = available_coins.All();
    // Preselect the first 3 UTXO (150 BTQ total)
    std::set<COutPoint> preset_inputs = {coins[0].outpoint, coins[1].outpoint, coins[2].outpoint};

    // Try to create a tx that spends more than what preset inputs + wallet selected inputs are covering for.
    // The wallet can cover up to 200 BTQ, and the tx target is 299 BTQ.
    std::vector<CRecipient> recipients{{*Assert(wallet->GetNewDestination(OutputType::BECH32, "dummy")),
                                           /*nAmount=*/299 * COIN, /*fSubtractFeeFromAmount=*/true}};
    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = true;
    for (const auto& outpoint : preset_inputs) {
        coin_control.Select(outpoint);
    }

    // Attempt to send 299 BTQ from a wallet that only has 200 BTQ. The wallet should exclude
    // the preset inputs from the pool of available coins, realize that there is not enough
    // money to fund the 299 BTQ payment, and fail with "Insufficient funds".
    //
    // Even with SFFO, the wallet can only afford to send 200 BTQ.
    // If the wallet does not properly exclude preset inputs from the pool of available coins
    // prior to coin selection, it may create a transaction that does not fund the full payment
    // amount or, through SFFO, incorrectly reduce the recipient's amount by the difference
    // between the original target and the wrongly counted inputs (in this case 99 BTQ)
    // so that the recipient's amount is no longer equal to the user's selected target of 299 BTQ.

    // First case, use 'subtract_fee_from_outputs=true'
    util::Result<CreatedTransactionResult> res_tx = CreateTransaction(*wallet, recipients, /*change_pos*/-1, coin_control);
    BOOST_CHECK(!res_tx.has_value());

    // Second case, don't use 'subtract_fee_from_outputs'.
    recipients[0].fSubtractFeeFromAmount = false;
    res_tx = CreateTransaction(*wallet, recipients, /*change_pos*/-1, coin_control);
    BOOST_CHECK(!res_tx.has_value());
}

BOOST_AUTO_TEST_SUITE_END()

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

    // Dilithium shares LEGACY/BECH32 spk managers (see rpc/dilithium.cpp).
    for (const auto& [spkm_type, dil_type, label] : {
             std::make_tuple(OutputType::LEGACY, OutputType::DILITHIUM_LEGACY, "dilithium-legacy"),
             std::make_tuple(OutputType::BECH32, OutputType::DILITHIUM_BECH32, "dilithium-bech32"),
         }) {
        ScriptPubKeyMan* spk_man = wallet->GetScriptPubKeyMan(spkm_type, /*internal=*/false);
        BOOST_REQUIRE(spk_man != nullptr);
        const CTxDestination dest = *Assert(spk_man->GetNewDestination(dil_type));
        const CScript script = GetScriptForDestination(dest);
        const CTxOut out(COIN, script);
        const int input_vsize = CalculateMaximumSignedInputSize(out, wallet.get(), /*coin_control=*/nullptr);
        BOOST_CHECK_MESSAGE(input_vsize > 0, strprintf("fee estimate failed for %s", label));
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
