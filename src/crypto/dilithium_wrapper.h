// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_CRYPTO_DILITHIUM_WRAPPER_H
#define BTQ_CRYPTO_DILITHIUM_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size constants for Dilithium2 (default mode) */
#define BTQ_DILITHIUM_PUBLIC_KEY_SIZE 1312
#define BTQ_DILITHIUM_SECRET_KEY_SIZE 2560
#define BTQ_DILITHIUM_SIGNATURE_SIZE 2420

/** Seed size for deterministic key generation (matches Dilithium SEEDBYTES). */
#define BTQ_DILITHIUM_SEED_SIZE 32

/**
 * Generate a new Dilithium key pair using fresh randomness.
 * @param pk Output buffer for public key (must be BTQ_DILITHIUM_PUBLIC_KEY_SIZE bytes)
 * @param sk Output buffer for secret key (must be BTQ_DILITHIUM_SECRET_KEY_SIZE bytes)
 * @return 0 on success, non-zero on failure
 */
int btq_dilithium_keypair(uint8_t *pk, uint8_t *sk);

/**
 * Generate a Dilithium key pair deterministically from a caller-supplied
 * 32-byte seed. Same seed always produces the same (pk, sk). This is the
 * building block for HD wallet derivation, where each child-key seed is
 * derived from the parent via HMAC-SHA512.
 *
 * @param pk Output buffer for public key (must be BTQ_DILITHIUM_PUBLIC_KEY_SIZE bytes)
 * @param sk Output buffer for secret key (must be BTQ_DILITHIUM_SECRET_KEY_SIZE bytes)
 * @param seed Input seed (must be BTQ_DILITHIUM_SEED_SIZE bytes)
 * @return 0 on success, non-zero on failure
 */
int btq_dilithium_keypair_from_seed(uint8_t *pk, uint8_t *sk, const uint8_t *seed);

/**
 * Create a Dilithium signature.
 * @param sig Output buffer for signature (must be at least BTQ_DILITHIUM_SIGNATURE_SIZE bytes)
 * @param siglen Output for actual signature length
 * @param m Message to sign
 * @param mlen Length of message
 * @param ctx Context string (can be NULL if ctxlen is 0)
 * @param ctxlen Length of context string
 * @param sk Secret key (must be BTQ_DILITHIUM_SECRET_KEY_SIZE bytes)
 * @return 0 on success, non-zero on failure
 */
int btq_dilithium_sign(uint8_t *sig, size_t *siglen,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *ctx, size_t ctxlen,
                       const uint8_t *sk);

/**
 * Verify a Dilithium signature.
 * @param sig Signature to verify
 * @param siglen Length of signature
 * @param m Original message
 * @param mlen Length of message
 * @param ctx Context string used during signing (can be NULL if ctxlen is 0)
 * @param ctxlen Length of context string
 * @param pk Public key (must be BTQ_DILITHIUM_PUBLIC_KEY_SIZE bytes)
 * @return 0 if signature is valid, non-zero if invalid
 */
int btq_dilithium_verify(const uint8_t *sig, size_t siglen,
                         const uint8_t *m, size_t mlen,
                         const uint8_t *ctx, size_t ctxlen,
                         const uint8_t *pk);

/**
 * Extract public key from secret key.
 * @param pk Output buffer for public key (must be BTQ_DILITHIUM_PUBLIC_KEY_SIZE bytes)
 * @param sk Secret key (must be BTQ_DILITHIUM_SECRET_KEY_SIZE bytes)
 * @return 0 on success, non-zero on failure
 */
int btq_dilithium_sk_to_pk(uint8_t *pk, const uint8_t *sk);

#ifdef __cplusplus
}
#endif

#endif // BTQ_CRYPTO_DILITHIUM_WRAPPER_H
