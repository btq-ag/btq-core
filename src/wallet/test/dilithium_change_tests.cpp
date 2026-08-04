// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <consensus/amount.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <outputtype.h>
#include <script/solver.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/p2mr.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
namespace {
std::unique_ptr<CWallet> CreateDescriptorWallet(interfaces::Chain* chain)
{
    auto wallet = std::make_unique<CWallet>(chain, "", CreateMockableWalletDatabase());
    wallet->LoadWallet();
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetupDescriptorScriptPubKeyMans();
    return wallet;
}

CRecipient Pay(const CTxDestination& dest)
{
    return CRecipient{dest, COIN, /*subtract_fee=*/false};
}

//! A P2MR destination that does not belong to any wallet, so nothing about the
//! change decision can depend on the recipient being ours.
CTxDestination ForeignP2MR()
{
    return WitnessV2P2MR{std::vector<unsigned char>(WitnessV2P2MR::SIZE, 0x11)};
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(dilithium_change_tests, WalletTestingSetup)

/** p2mr is a first-class output type: nameable, parseable, and derivable from a destination. */
BOOST_AUTO_TEST_CASE(p2mr_output_type_round_trips)
{
    BOOST_CHECK(ParseOutputType("p2mr") == OutputType::P2MR);
    BOOST_CHECK_EQUAL(FormatOutputType(OutputType::P2MR), "p2mr");
    BOOST_CHECK(ParseOutputType(FormatOutputType(OutputType::P2MR)) == OutputType::P2MR);

    // P2MR used to report as BECH32M, which is what grouped it with Taproot for
    // coin selection and reporting.
    BOOST_CHECK(OutputTypeFromDestination(ForeignP2MR()) == OutputType::P2MR);
    BOOST_CHECK(OutputTypeFromDestination(WitnessV1Taproot{XOnlyPubKey(uint256::ONE)}) == OutputType::BECH32M);

    BOOST_CHECK(IsQuantumSafeOutputType(OutputType::P2MR));
    BOOST_CHECK(!IsQuantumSafeOutputType(OutputType::BECH32M));
}

/**
 * The defect in #76: a send to a Dilithium destination matched none of the
 * recipient cases, so every flag stayed false and the bech32m fallback returned
 * Taproot change. Deterministically, and without telling the user.
 */
BOOST_AUTO_TEST_CASE(dilithium_recipients_get_quantum_safe_change)
{
    auto wallet = CreateDescriptorWallet(m_node.chain.get());
    LOCK(wallet->cs_wallet);

    const CTxDestination p2mr_dest{ForeignP2MR()};
    BOOST_CHECK(wallet->TransactionChangeType(/*change_type=*/std::nullopt, {Pay(p2mr_dest)}) == OutputType::P2MR);

    CDilithiumKey dilithium_key;
    BOOST_REQUIRE(dilithium_key.MakeNewKey());
    const CTxDestination dilithium_legacy_dest{DilithiumPKHash(dilithium_key.GetPubKey())};
    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(dilithium_legacy_dest)}) == OutputType::P2MR);

    // One quantum-safe recipient among classical ones is enough: the change of a
    // mixed send is still change from a Dilithium spend.
    const CTxDestination taproot_dest{WitnessV1Taproot{XOnlyPubKey(uint256::ONE)}};
    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(taproot_dest), Pay(p2mr_dest)}) == OutputType::P2MR);
}

/** Classical sends must be unaffected: this is upstream behaviour and stays. */
BOOST_AUTO_TEST_CASE(classical_recipients_keep_classical_change)
{
    auto wallet = CreateDescriptorWallet(m_node.chain.get());
    LOCK(wallet->cs_wallet);

    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(WitnessV1Taproot{XOnlyPubKey(uint256::ONE)})}) == OutputType::BECH32M);
    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(WitnessV0KeyHash(key.GetPubKey()))}) == OutputType::BECH32);
    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(PKHash(key.GetPubKey()))}) == OutputType::LEGACY);
}

/** An explicit -changetype / coin control choice is still honoured above everything else. */
BOOST_AUTO_TEST_CASE(explicit_change_type_overrides_the_dilithium_rule)
{
    auto wallet = CreateDescriptorWallet(m_node.chain.get());
    LOCK(wallet->cs_wallet);

    const CTxDestination p2mr_dest{ForeignP2MR()};
    BOOST_CHECK(wallet->TransactionChangeType(OutputType::BECH32M, {Pay(p2mr_dest)}) == OutputType::BECH32M);
    BOOST_CHECK(wallet->TransactionChangeType(OutputType::LEGACY, {Pay(p2mr_dest)}) == OutputType::LEGACY);
}

/**
 * A wallet with no private keys cannot derive the Dilithium key a P2MR change
 * output needs. Returning P2MR there would turn "your change is Taproot" into
 * "you cannot fund a transaction at all", so it keeps the classical answer.
 */
BOOST_AUTO_TEST_CASE(watch_only_wallet_keeps_classical_change)
{
    auto wallet = CreateDescriptorWallet(m_node.chain.get());
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);

    BOOST_CHECK(wallet->TransactionChangeType(std::nullopt, {Pay(ForeignP2MR())}) == OutputType::BECH32M);
}

/**
 * P2MR change is minted rather than drawn from a keypool, so check the pieces
 * the rest of the wallet relies on: it is a witness v2 output, the wallet tracks
 * its script tree (without which it could never be spent), and it is not filed
 * as a receive address.
 */
BOOST_AUTO_TEST_CASE(p2mr_change_destination_is_tracked_and_not_a_receive_address)
{
    auto wallet = CreateDescriptorWallet(m_node.chain.get());
    LOCK(wallet->cs_wallet);

    const auto change_dest = wallet->GetNewChangeDestination(OutputType::P2MR);
    BOOST_REQUIRE_MESSAGE(change_dest, util::ErrorString(change_dest).original);
    BOOST_CHECK(std::holds_alternative<WitnessV2P2MR>(*change_dest));
    BOOST_CHECK(IsValidDestination(*change_dest));

    const CScript change_script = GetScriptForDestination(*change_dest);
    BOOST_CHECK(IsTrackedP2MRScript(*wallet, change_script));
    BOOST_CHECK(wallet->IsMine(change_script) == ISMINE_SPENDABLE);
    BOOST_CHECK(wallet->FindAddressBookEntry(*change_dest) == nullptr);

    // A receive destination of the same type is filed, so the difference above
    // is the change flag rather than P2MR never reaching the address book.
    const auto receive_dest = wallet->GetNewDestination(OutputType::P2MR, "savings");
    BOOST_REQUIRE(receive_dest);
    BOOST_CHECK(wallet->FindAddressBookEntry(*receive_dest) != nullptr);

    // Successive change destinations must not repeat.
    const auto second = wallet->GetNewChangeDestination(OutputType::P2MR);
    BOOST_REQUIRE(second);
    BOOST_CHECK(GetScriptForDestination(*second) != change_script);
}

/** End to end: fund a wallet, pay a P2MR address, and read the change output off the wire. */
BOOST_FIXTURE_TEST_CASE(dilithium_send_returns_change_to_p2mr, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    const CAmount balance{WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet).GetTotalAmount())};
    BOOST_REQUIRE(balance > 0);

    const CTxDestination recipient_dest{ForeignP2MR()};
    const CScript recipient_script = GetScriptForDestination(recipient_dest);

    CCoinControl coin_control;
    auto res = CreateTransaction(*wallet, {CRecipient{recipient_dest, balance / 2, /*subtract_fee=*/false}},
                                 /*change_pos=*/-1, coin_control);
    BOOST_REQUIRE_MESSAGE(res, util::ErrorString(res).original);
    BOOST_REQUIRE(res->change_pos >= 0);

    const CTxOut& change_out = res->tx->vout.at(res->change_pos);
    BOOST_CHECK(change_out.scriptPubKey != recipient_script);

    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType change_type = Solver(change_out.scriptPubKey, solutions);
    BOOST_CHECK_MESSAGE(change_type == TxoutType::WITNESS_V2_P2MR,
                        strprintf("change output is %s, expected witness_v2_p2mr", GetTxnOutputType(change_type)));

    // ...and the wallet can actually spend it, which is the half that makes the
    // change quantum-safe rather than merely quantum-shaped.
    BOOST_CHECK(WITH_LOCK(wallet->cs_wallet, return IsTrackedP2MRScript(*wallet, change_out.scriptPubKey)));
    BOOST_CHECK(WITH_LOCK(wallet->cs_wallet, return wallet->IsMine(change_out.scriptPubKey)) == ISMINE_SPENDABLE);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
