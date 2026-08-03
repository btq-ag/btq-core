#!/usr/bin/env bash
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Regenerate BTQ-config NIST Known Answer Tests for Dilithium2 (deterministic
# signing, ref/ only). Requires OpenSSL development libraries.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF="${ROOT}/src/crypto/dilithium/ref"
KAT_DIR="${REF}/nistkat"
RSP="${KAT_DIR}/PQCsignKAT_Dilithium2.rsp"
SHA256SUMS="${ROOT}/src/crypto/dilithium/SHA256SUMS"

if [[ ! -d "${REF}" ]]; then
  echo "missing dilithium ref tree at ${REF}" >&2
  exit 1
fi

TMP_MAKEFILE=""
cleanup() {
  if [[ -n "${TMP_MAKEFILE}" && -f "${REF}/Makefile" ]]; then
    rm -f "${REF}/Makefile"
  fi
}
trap cleanup EXIT

if [[ ! -f "${REF}/Makefile" ]]; then
  TMP_MAKEFILE=1
  curl -fsSL "https://raw.githubusercontent.com/pq-crystals/dilithium/master/ref/Makefile" \
    -o "${REF}/Makefile"
fi

make -C "${REF}" nistkat/PQCgenKAT_sign2
(
  cd "${KAT_DIR}"
  ./PQCgenKAT_sign2
)

HASH="$(sha256sum "${RSP}" | awk '{print $1}')"
cat > "${SHA256SUMS}" <<EOF
${HASH}  ref/nistkat/PQCsignKAT_Dilithium2.rsp
EOF

echo "Wrote ${RSP}"
echo "SHA256 ${HASH}"
echo "Updated ${SHA256SUMS}"
