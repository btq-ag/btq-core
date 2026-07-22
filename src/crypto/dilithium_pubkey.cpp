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
    // BTQ-AUDIT-022: cheap structural checks for an ML-DSA-44 / Dilithium2
    // public key (rho(32) || t1(1280) == SIZE).
    //
    // t1 is 10-bit packed, so any SIZE-byte string unpacks in-bounds; there is
    // no coefficient-range check analogous to secp256k1 curve membership.
    // Random non-degenerate blobs still pass — this rejects only empty /
    // all-zero and the all-zero-rho / all-zero-t1 encodings that keygen does
    // not produce. We deliberately do NOT probe with a dummy verify() call.
    if (!IsValid()) {
        return false;   // all-zero blob (wrong-length Set() also clears to this)
    }
    // Reject all-zero rho (public seed): not produced by keygen.
    bool rho_nonzero = false;
    for (size_t i = 0; i < 32; ++i) {
        if (vch[i] != 0) { rho_nonzero = true; break; }
    }
    if (!rho_nonzero) return false;
    // Reject all-zero t1: Power2Round(A*s1+s2) is not expected to be identically
    // zero for a keygen output; treat that encoding as degenerate.
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
