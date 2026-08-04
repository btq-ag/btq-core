// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/message.h>
#include <util/strencodings.h>

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dilithium_message_tests, BasicTestingSetup)

namespace {
CDilithiumKey NewKey()
{
    CDilithiumKey key;
    BOOST_REQUIRE(key.MakeNewKey());
    return key;
}
} // namespace

BOOST_AUTO_TEST_CASE(sign_verify_round_trip)
{
    const CDilithiumKey key{NewKey()};
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    const std::string message{"proof that I control this address"};

    std::string signature;
    BOOST_REQUIRE(DilithiumMessageSign(key, message, signature));
    BOOST_CHECK(DilithiumMessageVerify(pubkey, message, signature));

    BOOST_CHECK(!DilithiumMessageVerify(pubkey, message + "!", signature));
    BOOST_CHECK(!DilithiumMessageVerify(NewKey().GetPubKey(), message, signature));
    BOOST_CHECK(!DilithiumMessageVerify(pubkey, message, "not base64 $$$"));
}

/**
 * The crux of #90. Message signing and transaction signing use the same key and
 * the same primitive; a transaction signature is made over a bare 32-byte
 * sighash. So the attack is not hypothetical arithmetic: it is asking the
 * victim to "sign a message" whose bytes are the sighash of a transaction that
 * spends their coins.
 *
 * The strongest form of the test hands the attacker the win condition for free.
 * Rather than grinding a transaction until its sighash matches something, take
 * the exact 32 bytes the message signature commits to and treat those as the
 * sighash. If the signature still does not verify as a transaction signature
 * over them, no amount of grinding helps.
 */
BOOST_AUTO_TEST_CASE(a_message_signature_cannot_spend)
{
    const CDilithiumKey key{NewKey()};
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    const std::string message{"harmless looking login challenge"};

    std::string signature;
    BOOST_REQUIRE(DilithiumMessageSign(key, message, signature));
    const auto sig_bytes = DecodeBase64(signature);
    BOOST_REQUIRE(sig_bytes);

    // Grant the attacker the sighash they would otherwise have to grind for.
    const uint256 sighash{DilithiumMessageHash(message)};

    // This is what the script interpreter and MutableTransactionSignatureCreator
    // do: verify over the sighash under the empty context.
    BOOST_CHECK(!pubkey.Verify(sighash, *sig_bytes));

    // And the same key signing that sighash as a transaction would produce a
    // signature that does verify, so the check above is not passing because the
    // key or the sighash is somehow unusable.
    std::vector<unsigned char> tx_sig;
    BOOST_REQUIRE(key.Sign(sighash, tx_sig));
    BOOST_CHECK(pubkey.Verify(sighash, tx_sig));

    // The construction that was replaced, kept as the reference the case above
    // is measured against: signing the caller's bytes under the empty context
    // is the same call as signing a transaction, so a 32-byte "message" is a
    // spending signature. Asking for this is what made the RPC an oracle.
    std::vector<unsigned char> undomained_sig;
    BOOST_REQUIRE(key.SignMessage(Span<const unsigned char>(sighash.begin(), sighash.size()), undomained_sig));
    BOOST_CHECK(pubkey.Verify(sighash, undomained_sig));
}

/** The other direction: a spending signature must not pass as an attestation. */
BOOST_AUTO_TEST_CASE(a_transaction_signature_is_not_a_message_signature)
{
    const CDilithiumKey key{NewKey()};
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    const std::string message{"I attest to owning these funds"};

    std::vector<unsigned char> tx_sig;
    BOOST_REQUIRE(key.Sign(DilithiumMessageHash(message), tx_sig));
    BOOST_CHECK(!DilithiumMessageVerify(pubkey, message, EncodeBase64(tx_sig)));
}

/**
 * Two separations are applied and either would suffice, so check them apart:
 * a fault that silently drops one must not go unnoticed behind the other.
 */
BOOST_AUTO_TEST_CASE(context_and_magic_separate_independently)
{
    const CDilithiumKey key{NewKey()};
    const CDilithiumPubKey pubkey{key.GetPubKey()};
    const std::string message{"payload"};
    const uint256 hash{DilithiumMessageHash(message)};
    const auto span = Span<const unsigned char>(hash.begin(), hash.size());

    // The context alone: same payload, empty context, does not verify under the
    // message context.
    std::vector<unsigned char> no_context_sig;
    BOOST_REQUIRE(key.SignMessage(span, no_context_sig));
    BOOST_CHECK(!pubkey.VerifyMessage(span, no_context_sig, DilithiumMessageContext()));

    // The magic alone: the hash commits to it, so hashing the raw message
    // without the prefix lands on a different payload entirely.
    HashWriter unprefixed{};
    unprefixed << message;
    BOOST_CHECK(unprefixed.GetHash() != hash);
    BOOST_CHECK(DilithiumMessageHash("a") != DilithiumMessageHash("b"));
}

BOOST_AUTO_TEST_SUITE_END()
