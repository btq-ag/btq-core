// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_UTIL_MESSAGE_H
#define BTQ_UTIL_MESSAGE_H

#include <uint256.h>

#include <string>
#include <vector>

class CKey;
class CDilithiumKey;
class CDilithiumPubKey;

extern const std::string MESSAGE_MAGIC;
extern const std::string DILITHIUM_MESSAGE_MAGIC;

/** The result of a signed message verification.
 * Message verification takes as an input:
 * - address (with whose private key the message is supposed to have been signed)
 * - signature
 * - message
 */
enum class MessageVerificationResult {
    //! The provided address is invalid.
    ERR_INVALID_ADDRESS,

    //! The provided address is valid but does not refer to a public key.
    ERR_ADDRESS_NO_KEY,

    //! The provided signature couldn't be parsed (maybe invalid base64).
    ERR_MALFORMED_SIGNATURE,

    //! A public key could not be recovered from the provided signature and message.
    ERR_PUBKEY_NOT_RECOVERED,

    //! The message was not signed with the private key of the provided address.
    ERR_NOT_SIGNED,

    //! The message verification was successful.
    OK
};

enum class SigningResult {
    OK, //!< No error
    PRIVATE_KEY_NOT_AVAILABLE,
    SIGNING_FAILED,
};

/** Verify a signed message.
 * @param[in] address Signer's btq address, it must refer to a public key.
 * @param[in] signature The signature in base64 format.
 * @param[in] message The message that was signed.
 * @return result code */
MessageVerificationResult MessageVerify(
    const std::string& address,
    const std::string& signature,
    const std::string& message);

/** Sign a message.
 * @param[in] privkey Private key to sign with.
 * @param[in] message The message to sign.
 * @param[out] signature Signature, base64 encoded, only set if true is returned.
 * @return true if signing was successful. */
bool MessageSign(
    const CKey& privkey,
    const std::string& message,
    std::string& signature);

/**
 * Hashes a message for signing and verification in a manner that prevents
 * inadvertently signing a transaction.
 */
uint256 MessageHash(const std::string& message);

/**
 * Dilithium context string for message signing.
 *
 * Dilithium message signing and transaction signing use the same key and the
 * same primitive, and a transaction signature is made over a bare 32-byte
 * sighash under the empty context. Without separation, signing a message is
 * signing a transaction, and anything offering "sign this to prove you own the
 * address" becomes a spending oracle for the key behind a P2MR output.
 *
 * FIPS 204 prepends (0, len(ctx), ctx) to the message representative, so a
 * signature made under this context cannot verify under the empty one whatever
 * the payload -- including a payload an attacker ground to equal a sighash.
 * The version is part of the string because changing it invalidates every
 * signature previously made under it.
 */
const std::vector<unsigned char>& DilithiumMessageContext();

/**
 * Hashes a message for Dilithium signing and verification, committing to
 * DILITHIUM_MESSAGE_MAGIC. Independent of, and redundant with, the context
 * separation above: neither has to be trusted alone.
 */
uint256 DilithiumMessageHash(const std::string& message);

/** Sign a message with a Dilithium key.
 * @param[in] privkey Dilithium private key to sign with.
 * @param[in] message The message to sign.
 * @param[out] signature Signature, base64 encoded, only set if true is returned.
 * @return true if signing was successful. */
bool DilithiumMessageSign(
    const CDilithiumKey& privkey,
    const std::string& message,
    std::string& signature);

/** Verify a Dilithium signed message against a known public key.
 * Paired with DilithiumMessageSign so the two cannot drift apart.
 * @param[in] pubkey Signer's Dilithium public key.
 * @param[in] message The message that was signed.
 * @param[in] signature The signature in base64 format.
 * @return true if the signature is valid for this key and message. */
bool DilithiumMessageVerify(
    const CDilithiumPubKey& pubkey,
    const std::string& message,
    const std::string& signature);

std::string SigningResultString(const SigningResult res);

#endif // BTQ_UTIL_MESSAGE_H
