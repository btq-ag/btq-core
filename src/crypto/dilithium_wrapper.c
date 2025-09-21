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
    // Extract public key from secret key
    // In Dilithium, the secret key contains the public key at the beginning
    // For Dilithium2: sk = (rho, K, tr, s1, s2, t0) and pk = (rho, K, tr)
    // The public key is the first 1312 bytes of the secret key
    memcpy(pk, sk, pqcrystals_dilithium2_ref_PUBLICKEYBYTES);
    return 0;
}
