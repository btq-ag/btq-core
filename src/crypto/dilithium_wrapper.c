// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dilithium_wrapper.h"

// Include Dilithium headers in isolation to avoid macro conflicts
#include "dilithium/ref/api.h"
#include "dilithium/ref/sign.h"
#include "dilithium/ref/packing.h"
#include "dilithium/ref/params.h"
#include "dilithium/ref/polyvec.h"

int btq_dilithium_keypair(uint8_t *pk, uint8_t *sk)
{
    return crypto_sign_keypair(pk, sk);
}

int btq_dilithium_sign(uint8_t *sig, size_t *siglen,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *ctx, size_t ctxlen,
                       const uint8_t *sk)
{
    return crypto_sign_signature(sig, siglen, m, mlen, ctx, ctxlen, sk);
}

int btq_dilithium_verify(const uint8_t *sig, size_t siglen,
                         const uint8_t *m, size_t mlen,
                         const uint8_t *ctx, size_t ctxlen,
                         const uint8_t *pk)
{
    return crypto_sign_verify(sig, siglen, m, mlen, ctx, ctxlen, pk);
}

int btq_dilithium_sk_to_pk(uint8_t *pk, const uint8_t *sk)
{
    // Unpack the secret key to extract rho and reconstruct public key
    uint8_t rho[SEEDBYTES];
    uint8_t tr[TRBYTES];
    uint8_t key[SEEDBYTES];
    polyveck t0;
    polyvecl s1;
    polyveck s2;
    
    // Unpack the secret key
    unpack_sk(rho, tr, key, &t0, &s1, &s2, sk);
    
    // The public key can be reconstructed from rho and by recomputing t1
    // However, the easiest approach is to use the tr value which is H(rho, t1)
    // and reconstruct the public key from rho
    
    // Expand matrix from rho
    polyvecl mat[K];
    polyvec_matrix_expand(mat, rho);
    
    // Compute t1 = A*s1 + s2 (mod q)
    polyvecl s1hat = s1;
    polyvecl_ntt(&s1hat);
    polyveck t1;
    polyvec_matrix_pointwise_montgomery(&t1, mat, &s1hat);
    polyveck_reduce(&t1);
    polyveck_invntt_tomont(&t1);
    polyveck_add(&t1, &t1, &s2);
    polyveck_caddq(&t1);
    
    // Split t1 into t1 and t0
    polyveck_power2round(&t1, &t0, &t1);
    
    // Pack the public key
    pack_pk(pk, rho, &t1);
    
    return 0;
}
