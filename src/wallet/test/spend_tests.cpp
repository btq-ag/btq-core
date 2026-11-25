// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/fees.h>
#include <script/solver.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
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
    // Every case below pays out the fixture's single mature coinbase less some
    // leftover, so read what that coinbase is actually worth. The literal 50 BTQ
    // this replaces is Bitcoin's subsidy; BTQ pays 5, so each case was asking
    // for ten times the money the wallet held and failing on insufficient funds
    // before it could test anything about subtracting the fee.
    const CAmount coinbase_value{WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet).GetTotalAmount())};

    auto check_tx = [&wallet, coinbase_value](CAmount leftover_input_amount) {
        CRecipient recipient{PubKeyDestination({}), coinbase_value - leftover_input_amount, /*subtract_fee=*/true};
        constexpr int RANDOM_CHANGE_POSITION = -1;
        CCoinControl coin_control;
        coin_control.m_feerate.emplace(10000);
        coin_control.fOverrideFeeRate = true;
        // We need to use a change type with high cost of change so that the leftover amount will be dropped to fee instead of added as a change output
        coin_control.m_change_type = OutputType::LEGACY;
        auto res = CreateTransaction(*wallet, {recipient}, RANDOM_CHANGE_POSITION, coin_control);
        // REQUIRE, not CHECK: *res aborts the process when transaction creation
        // failed, and an abort here leaves the wallet fixture standing so
        // CCheckQueue's destructor then trips on its worker threads and wedges
        // the binary rather than exiting.
        BOOST_REQUIRE_MESSAGE(res, "CreateTransaction failed: " << util::ErrorString(res).original);
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

    // Add 4 spendable UTXO, 5 BTQ each, to the wallet (total balance 20 BTQ).
    // Every amount below is upstream's divided by ten, because that is the
    // ratio between Bitcoin's 50 BTC subsidy and BTQ's 5. Keeping the ratio
    // matters: the case turns on the target exceeding the balance by roughly
    // half the balance again, so that a wallet double-counting its preset
    // inputs would appear able to fund it. Left at 299 BTQ against a 20 BTQ
    // balance the shortfall is so wide that no double-count could close it,
    // and the case would pass without exercising anything.
    for (int i = 0; i < 4; i++) CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    LOCK(wallet->cs_wallet);
    auto available_coins = AvailableCoins(*wallet);
    std::vector<COutput> coins = available_coins.All();
    // Preselect the first 3 UTXO (15 BTQ total)
    std::set<COutPoint> preset_inputs = {coins[0].outpoint, coins[1].outpoint, coins[2].outpoint};

    // Try to create a tx that spends more than what preset inputs + wallet selected inputs are covering for.
    // The wallet can cover up to 20 BTQ, and the tx target is 29.9 BTQ.
    std::vector<CRecipient> recipients{{*Assert(wallet->GetNewDestination(OutputType::BECH32, "dummy")),
                                           /*nAmount=*/2990 * CENT, /*fSubtractFeeFromAmount=*/true}};
    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = true;
    for (const auto& outpoint : preset_inputs) {
        coin_control.Select(outpoint);
    }

    // Attempt to send 29.9 BTQ from a wallet that only has 20 BTQ. The wallet should exclude
    // the preset inputs from the pool of available coins, realize that there is not enough
    // money to fund the 29.9 BTQ payment, and fail with "Insufficient funds".
    //
    // Even with SFFO, the wallet can only afford to send 20 BTQ.
    // If the wallet does not properly exclude preset inputs from the pool of available coins
    // prior to coin selection, it may create a transaction that does not fund the full payment
    // amount or, through SFFO, incorrectly reduce the recipient's amount by the difference
    // between the original target and the wrongly counted inputs (in this case 9.9 BTQ)
    // so that the recipient's amount is no longer equal to the user's selected target of 29.9 BTQ.

    // First case, use 'subtract_fee_from_outputs=true'
    util::Result<CreatedTransactionResult> res_tx = CreateTransaction(*wallet, recipients, /*change_pos*/-1, coin_control);
    BOOST_CHECK(!res_tx.has_value());

    // Second case, don't use 'subtract_fee_from_outputs'.
    recipients[0].fSubtractFeeFromAmount = false;
    res_tx = CreateTransaction(*wallet, recipients, /*change_pos*/-1, coin_control);
    BOOST_CHECK(!res_tx.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
