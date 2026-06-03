// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dilithium_wrapper.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Include the actual Dilithium implementation
#include "dilithium/ref/api.h"

// Real Dilithium implementation using the reference library

int btq_dilithium_keypair(uint8_t *pk, uint8_t *sk)
{
    // Use the actual Dilithium2 keypair generation
    return pqcrystals_dilithium2_ref_keypair(pk, sk);
}

int btq_dilithium_keypair_from_seed(uint8_t *pk, uint8_t *sk, const uint8_t *seed)
{
    // Deterministic keypair generation from a 32-byte seed. This is the
    // primitive that fixes BTQ-AUDIT-019 / issue #53: the upstream reference
    // implementation's randomized keypair() overwrites its sk buffer with
    // randombytes(), discarding any caller-supplied entropy. The seeded
    // variant uses the supplied seed in place of randombytes() so HD wallet
    // derivation can be deterministic.
    return pqcrystals_dilithium2_ref_keypair_from_seed(pk, sk, seed);
}

int btq_dilithium_sign(uint8_t *sig, size_t *siglen,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *ctx, size_t ctxlen,
                       const uint8_t *sk)
{
    // Use the actual Dilithium2 signature generation
    // Handle empty context by passing NULL and 0
    const uint8_t *ctx_ptr = (ctxlen > 0) ? ctx : NULL;
    size_t ctx_len = (ctxlen > 0) ? ctxlen : 0;
    
    return pqcrystals_dilithium2_ref_signature(sig, siglen, m, mlen, ctx_ptr, ctx_len, sk);
}

int btq_dilithium_verify(const uint8_t *sig, size_t siglen,
                         const uint8_t *m, size_t mlen,
                         const uint8_t *ctx, size_t ctxlen,
                         const uint8_t *pk)
{
    // Use the actual Dilithium2 signature verification
    // Handle empty context by passing NULL and 0
    const uint8_t *ctx_ptr = (ctxlen > 0) ? ctx : NULL;
    size_t ctx_len = (ctxlen > 0) ? ctxlen : 0;
    
    return pqcrystals_dilithium2_ref_verify(sig, siglen, m, mlen, ctx_ptr, ctx_len, pk);
}

int btq_dilithium_sk_to_pk(uint8_t *pk, const uint8_t *sk)
{
    (void)sk;

    if (pk != NULL) {
        memset(pk, 0, pqcrystals_dilithium2_ref_PUBLICKEYBYTES);
    }

    // The packed Dilithium2 secret key does not contain the packed public key.
    // It stores rho, key, tr, s1, s2, and t0; the public key stores rho and
    // t1. Returning success here would hand callers bytes that cannot verify
    // signatures. BTQ key storage keeps sk || pk, so callers should use the
    // stored public key or the public key returned by key generation.
    return -1;
}
