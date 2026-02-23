// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CDilithiumPubKey implementation — consensus-safe (no LockedPoolManager dependency).
// CDilithiumKey and extended key implementations live in dilithium_key.cpp.

#include <crypto/dilithium_key.h>

#include <hash.h>

extern "C" {
#include "dilithium_wrapper.h"
}

uint256 CDilithiumPubKey::GetHash() const
{
    return Hash(Span{vch});
}

uint160 CDilithiumPubKey::GetID() const
{
    return Hash160(Span{vch});
}

bool CDilithiumPubKey::IsValid() const
{
    for (size_t i = 0; i < SIZE; ++i) {
        if (vch[i] != 0) {
            return true;
        }
    }
    return false;
}

bool CDilithiumPubKey::IsFullyValid() const
{
    return IsValid();
}

bool CDilithiumPubKey::Verify(const uint256& hash, const std::vector<unsigned char>& vchSig,
                             const std::vector<unsigned char>& context) const
{
    if (!IsValid() || vchSig.empty()) {
        return false;
    }

    return VerifyMessage(Span<const unsigned char>(hash.begin(), hash.size()), vchSig, context);
}

bool CDilithiumPubKey::VerifyMessage(Span<const unsigned char> message, const std::vector<unsigned char>& vchSig,
                                    const std::vector<unsigned char>& context) const
{
    if (!IsValid() || vchSig.empty()) {
        return false;
    }

    int result = btq_dilithium_verify(
        vchSig.data(), vchSig.size(),
        message.data(), message.size(),
        context.data(), context.size(),
        vch.data()
    );

    return result == 0;
}

std::vector<unsigned char> CDilithiumPubKey::GetAddress() const
{
    if (!IsValid()) {
        return {};
    }

    uint160 hash = GetID();
    return std::vector<unsigned char>(hash.begin(), hash.end());
}
