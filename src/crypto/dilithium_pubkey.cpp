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
    // BTQ-AUDIT-022: structural validation of an ML-DSA-44 public key.
    //
    // Layout is rho(32) || t1(1280 bytes). Unlike a secret key, there is NO
    // coefficient-range check to perform: t1 is stored as a 10-bit packed field
    // (POLYT1_PACKEDBYTES), so every correctly-sized byte string unpacks to
    // coefficients already in [0, 2^10) and unpack_pk cannot read out of bounds.
    // Structural validity therefore reduces to (a) correct length and (b)
    // rejection of degenerate keys that key generation never produces. We
    // deliberately do NOT call the verifier on a dummy signature as a "does it
    // crash" probe: it validates nothing (a well-formed key returns "invalid"
    // just like a malformed one) and only adds a heavy NTT to every check.
    if (!IsValid()) {
        return false;   // wrong length or all-zero blob
    }
    // Reject an all-zero rho (the public seed): never emitted by keygen, and a
    // cheap DoS/confusion input.
    bool rho_nonzero = false;
    for (size_t i = 0; i < 32; ++i) {
        if (vch[i] != 0) { rho_nonzero = true; break; }
    }
    if (!rho_nonzero) return false;
    // Reject an all-zero t1: a real keypair's t1 = Power2Round(A*s1 + s2) is
    // never identically zero, so this catches structurally degenerate keys.
    for (size_t i = 32; i < SIZE; ++i) {
        if (vch[i] != 0) return true;
    }
    return false;
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
