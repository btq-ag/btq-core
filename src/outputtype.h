// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_OUTPUTTYPE_H
#define BTQ_OUTPUTTYPE_H

#include <addresstype.h>
#include <script/signingprovider.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

enum class OutputType {
    LEGACY,
    P2SH_SEGWIT,
    BECH32,
    BECH32M,
    DILITHIUM_LEGACY,
    DILITHIUM_BECH32,
    //! BIP360 pay-to-merkle-root (witness v2). The only consensus-valid
    //! Dilithium spend path once DEPLOYMENT_DILITHIUM_P2MR is active, and the
    //! only quantum-safe type the wallet can hand out. Unlike the types above
    //! it is not descriptor-backed: destinations come from wallet/p2mr.cpp,
    //! which stores the script tree as wallet metadata.
    P2MR,
    UNKNOWN,
};

static constexpr auto OUTPUT_TYPES = std::array{
    OutputType::LEGACY,
    OutputType::P2SH_SEGWIT,
    OutputType::BECH32,
    OutputType::BECH32M,
    OutputType::DILITHIUM_LEGACY,
    OutputType::DILITHIUM_BECH32,
    OutputType::P2MR,
};

/** Output types whose spends are authorised by a Dilithium key rather than ECDSA/Schnorr. */
constexpr bool IsQuantumSafeOutputType(OutputType type)
{
    return type == OutputType::P2MR || type == OutputType::DILITHIUM_LEGACY || type == OutputType::DILITHIUM_BECH32;
}

std::optional<OutputType> ParseOutputType(const std::string& str);
const std::string& FormatOutputType(OutputType type);

/**
 * Get a destination of the requested type (if possible) to the specified key.
 * The caller must make sure LearnRelatedScripts has been called beforehand.
 */
CTxDestination GetDestinationForKey(const CPubKey& key, OutputType);

/** Get all destinations (potentially) supported by the wallet for the given key. */
std::vector<CTxDestination> GetAllDestinationsForKey(const CPubKey& key);

/**
 * Get a destination of the requested type (if possible) to the specified script.
 * This function will automatically add the script (and any other
 * necessary scripts) to the keystore.
 */
CTxDestination AddAndGetDestinationForScript(FillableSigningProvider& keystore, const CScript& script, OutputType);

/** Get the OutputType for a CTxDestination */
std::optional<OutputType> OutputTypeFromDestination(const CTxDestination& dest);

#endif // BTQ_OUTPUTTYPE_H
