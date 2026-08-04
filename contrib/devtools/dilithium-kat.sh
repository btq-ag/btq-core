#!/usr/bin/env bash
#
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Regenerate the pq-crystals reference test vectors from BTQ's vendored
# Dilithium sources and check them against src/crypto/dilithium/SHA256SUMS.
#
# This is the supply-chain half of BTQ's Dilithium known-answer testing: it
# proves the vendored tree still reproduces the upstream reference output
# bit-for-bit, across all three security levels, over 10000 iterations that
# exercise key generation, signing, verification, matrix expansion, every
# pack/unpack routine, decompose, power2round, hint generation and challenge
# sampling. The standards-conformance half lives in the dilithium_kat_tests
# unit suite, which runs NIST ACVP ML-DSA-44 vectors against the same code.
#
# Upstream's own runtests.sh cannot be used directly: it drives per-directory
# Makefiles that BTQ does not vendor, and it writes ~4.5 GB of vectors to disk.
# This script compiles the same harness against our tree and streams the output
# through sha256sum instead.
#
# Takes about a minute. Run it after touching anything under
# src/crypto/dilithium/ref/, and before a release.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF_DIR="${REPO_ROOT}/src/crypto/dilithium/ref"
MANIFEST="${REPO_ROOT}/src/crypto/dilithium/SHA256SUMS"
CC_BIN="${CC:-cc}"
CFLAGS_EXTRA="${CFLAGS:--O2}"

for path in "${REF_DIR}/sign.c" "${REF_DIR}/test/test_vectors.c" "${MANIFEST}"; do
    if [[ ! -f "${path}" ]]; then
        echo "error: expected file not found: ${path}" >&2
        exit 1
    fi
done

WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT

# test_vectors.c supplies its own deterministic randombytes(), so randombytes.c
# must be left out of the link to avoid a duplicate definition.
SOURCES=(
    "${REF_DIR}/sign.c"
    "${REF_DIR}/packing.c"
    "${REF_DIR}/polyvec.c"
    "${REF_DIR}/poly.c"
    "${REF_DIR}/ntt.c"
    "${REF_DIR}/reduce.c"
    "${REF_DIR}/rounding.c"
    "${REF_DIR}/fips202.c"
    "${REF_DIR}/symmetric-shake.c"
    "${REF_DIR}/test/test_vectors.c"
)

echo "Dilithium reference known-answer test"
echo "  sources:  ${REF_DIR}"
echo "  manifest: ${MANIFEST}"
echo "  compiler: ${CC_BIN}"
echo

status=0
for mode in 2 3 5; do
    printf 'mode %s: building... ' "${mode}"
    # shellcheck disable=SC2086
    "${CC_BIN}" ${CFLAGS_EXTRA} -DDILITHIUM_MODE="${mode}" \
        -o "${WORKDIR}/test_vectors${mode}" "${SOURCES[@]}"

    printf 'generating... '
    actual="$("${WORKDIR}/test_vectors${mode}" | sha256sum | cut -d' ' -f1)"

    expected="$(awk -v f="tvecs${mode}" '$2 == f { print $1 }' "${MANIFEST}")"
    if [[ -z "${expected}" ]]; then
        echo "NO MANIFEST ENTRY for tvecs${mode}"
        status=1
    elif [[ "${actual}" == "${expected}" ]]; then
        echo "OK"
    else
        echo "MISMATCH"
        echo "    expected ${expected}"
        echo "    actual   ${actual}"
        status=1
    fi
done

echo
if [[ "${status}" -eq 0 ]]; then
    echo "All reference vectors match. See src/crypto/dilithium/PROVENANCE.md"
    echo "for what these digests are pinned to."
else
    echo "Reference vectors DO NOT match."
    echo
    echo "Either the vendored implementation changed behaviour, or the signing"
    echo "configuration changed. Note that enabling DILITHIUM_RANDOMIZED_SIGNING"
    echo "in ref/config.h changes every digest: the manifest is pinned to the"
    echo "deterministic variant BTQ ships. See src/crypto/dilithium/PROVENANCE.md."
fi
exit "${status}"
