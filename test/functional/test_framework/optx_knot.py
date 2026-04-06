#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Copyright (c) 2026 OPTX / Jett Optx (jettoptics.ai)
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/MIT.
#
# Python bindings for OPTX Phase 7: OP_OPTX_KNOT and TKDF
# GENSYS OPTX btQ v1.0 — Topological Key Derivation Function
#
# Implements:
#   - KnotWitnessData serialization/deserialization
#   - TKDF computation (SHAKE-256 based)
#   - OP_OPTX_KNOT script construction
#   - Knot invariant verification helpers

import struct
import hashlib
from typing import Optional, Tuple, List

# === Constants (matching C++ src/script/script.h) ===
OP_OPTX_KNOT = 0xc0
OP_OPTX_KNOT_VERIFY = 0xc1

TKDF_ALPHA_SIZE = 16     # IEEE 754 complex128
TKDF_NU_SIZE = 16        # IEEE 754 complex128
TKDF_WRITHE_SIZE = 4     # int32 big-endian
TKDF_SEED_SIZE = 32      # 256-bit entropy
TKDF_OUTPUT_SIZE = 64    # SHAKE-256 512-bit output
TKDF_INVARIANT_SIZE = TKDF_ALPHA_SIZE + TKDF_NU_SIZE + TKDF_WRITHE_SIZE  # 36 bytes

MAX_DT_CODE_SIZE = 64
MAX_OPTX_CROSSINGS = 16
MAX_OPTX_STRANDS = 7
MAX_OPTX_WORD_LENGTH = 128
MAX_OPTX_KNOT_DATA_SIZE = 512
MAX_OPTX_SPATIAL_SIG_SIZE = 4096

# Domain separator
TKDF_DOMAIN_TAG = b"OPTX-TKDF-v1.0\x00"


class KnotWitnessData:
    """
    Parsed knot data from OP_OPTX_KNOT witness stack.

    Wire format (big-endian):
      [dt_code_len:2] [dt_code:var] [alpha_K:16] [nu_K:16] [writhe:4]
    """

    def __init__(self):
        self.dt_code: bytes = b""
        self.alpha_k: bytes = b"\x00" * TKDF_ALPHA_SIZE
        self.nu_k: bytes = b"\x00" * TKDF_NU_SIZE
        self.writhe: int = 0

    def from_bytes(self, data: bytes) -> bool:
        """Deserialize from witness stack element."""
        if len(data) < 40:
            return False

        offset = 0

        # Read DT code length (2 bytes BE)
        dt_len = struct.unpack_from(">H", data, offset)[0]
        offset += 2

        if dt_len == 0 or dt_len > MAX_DT_CODE_SIZE:
            return False
        if offset + dt_len + TKDF_INVARIANT_SIZE > len(data):
            return False

        # Read DT code
        self.dt_code = data[offset:offset + dt_len]
        offset += dt_len

        # Read alpha_K (16 bytes)
        self.alpha_k = data[offset:offset + TKDF_ALPHA_SIZE]
        offset += TKDF_ALPHA_SIZE

        # Read nu_K (16 bytes)
        self.nu_k = data[offset:offset + TKDF_NU_SIZE]
        offset += TKDF_NU_SIZE

        # Read writhe (4 bytes BE signed)
        self.writhe = struct.unpack_from(">i", data, offset)[0]
        offset += TKDF_WRITHE_SIZE

        return True

    def to_bytes(self) -> bytes:
        """Serialize to witness stack element."""
        result = struct.pack(">H", len(self.dt_code))
        result += self.dt_code
        result += self.alpha_k
        result += self.nu_k
        result += struct.pack(">i", self.writhe)
        return result

    def size(self) -> int:
        return 2 + len(self.dt_code) + TKDF_INVARIANT_SIZE

    @classmethod
    def from_knot(cls, dt_code: bytes, alpha_k: bytes, nu_k: bytes, writhe: int) -> "KnotWitnessData":
        """Create from components."""
        kwd = cls()
        kwd.dt_code = dt_code
        kwd.alpha_k = alpha_k
        kwd.nu_k = nu_k
        kwd.writhe = writhe
        return kwd


def tkdf_derive(alpha_k: bytes, nu_k: bytes, writhe: int, seed: bytes) -> bytes:
    """
    Topological Key Derivation Function — GENSYS OPTX btQ v1.0, eq. (4)

    Computes: SHAKE-256(domain_tag || α_K || ν_K || ω_K || seed) → 512 bits

    Args:
        alpha_k: Alexander polynomial evaluation (16 bytes, IEEE 754 complex128 BE)
        nu_k:    Jones polynomial evaluation (16 bytes, IEEE 754 complex128 BE)
        writhe:  Writhe number w(K) (int32)
        seed:    Entropy seed (32 bytes)

    Returns:
        64 bytes of TKDF output for ML-DSA KeyGen seed
    """
    assert len(alpha_k) == TKDF_ALPHA_SIZE, f"alpha_k must be {TKDF_ALPHA_SIZE} bytes"
    assert len(nu_k) == TKDF_NU_SIZE, f"nu_k must be {TKDF_NU_SIZE} bytes"
    assert len(seed) == TKDF_SEED_SIZE, f"seed must be {TKDF_SEED_SIZE} bytes"

    # Build preimage: domain_tag ∥ α_K ∥ ν_K ∥ ω_K ∥ seed
    preimage = TKDF_DOMAIN_TAG
    preimage += alpha_k
    preimage += nu_k
    preimage += struct.pack(">i", writhe)
    preimage += seed

    # H = SHAKE-256 with 512-bit output
    shake = hashlib.shake_256(preimage)
    return shake.digest(TKDF_OUTPUT_SIZE)


def tkdf_derive_from_witness(knot: KnotWitnessData, seed: bytes) -> bytes:
    """Derive TKDF output from KnotWitnessData + seed."""
    return tkdf_derive(knot.alpha_k, knot.nu_k, knot.writhe, seed)


def check_knot_data_bounds(data: bytes) -> bool:
    """
    Validate knot witness data bounds (DoS protection).
    Matches C++ CheckKnotDataBounds().
    """
    if len(data) > MAX_OPTX_KNOT_DATA_SIZE:
        return False
    if len(data) < 40:
        return False

    dt_len = struct.unpack_from(">H", data, 0)[0]
    if dt_len == 0 or dt_len > MAX_DT_CODE_SIZE:
        return False

    num_crossings = dt_len // 2
    if num_crossings > MAX_OPTX_CROSSINGS:
        return False
    if num_crossings < 3:
        return False

    return True


def build_optx_knot_witness(spatial_sig: bytes, knot_data: bytes, pubkey: bytes,
                             verify: bool = False) -> list:
    """
    Build OP_OPTX_KNOT witness stack items.

    Stack layout: [spatial_sig, knot_data, pubkey, opcode_byte]
    Use with CScript when the full test_framework is available.
    """
    opcode = OP_OPTX_KNOT_VERIFY if verify else OP_OPTX_KNOT
    return [spatial_sig, knot_data, pubkey, bytes([opcode])]


# === Trefoil knot test vector ===
# Trefoil: 3_1, DT code = [4, 6, 2], writhe = -3
# These are reference values for testing

TREFOIL_DT_CODE = bytes([4, 6, 2, 0, 0, 0])  # Padded to even length

def trefoil_test_vector() -> KnotWitnessData:
    """Generate trefoil (3_1) test vector for OP_OPTX_KNOT testing."""
    # Alexander polynomial of trefoil at e^{2πi/2} = -1:
    # Δ_{3_1}(t) = t^{-1} - 1 + t → Δ(-1) = -1 - 1 + (-1) = -3
    # Encoded as IEEE 754 complex128 (real=-3.0, imag=0.0)
    alpha_k = struct.pack(">dd", -3.0, 0.0)

    # Jones polynomial of trefoil at e^{2πi/2} = -1:
    # V_{3_1}(t) = -t^{-4} + t^{-3} + t^{-1} → V(-1) = -1 - 1 - 1 = -3
    # Encoded as IEEE 754 complex128 (real=-3.0, imag=0.0)
    nu_k = struct.pack(">dd", -3.0, 0.0)

    writhe = -3

    return KnotWitnessData.from_knot(TREFOIL_DT_CODE, alpha_k, nu_k, writhe)


def figure_eight_test_vector() -> KnotWitnessData:
    """Generate figure-eight (4_1) test vector for OP_OPTX_KNOT testing."""
    # DT code for figure-eight: [4, 6, 8, 2]
    dt_code = bytes([4, 6, 8, 2, 0, 0])

    # Alexander polynomial: Δ_{4_1}(t) = -t^{-1} + 3 - t → Δ(-1) = 1 + 3 + 1 = 5
    alpha_k = struct.pack(">dd", 5.0, 0.0)

    # Jones: V_{4_1}(-1) = specific value
    nu_k = struct.pack(">dd", -1.0, 0.0)

    writhe = 0  # Figure-eight is amphicheiral

    return KnotWitnessData.from_knot(dt_code, alpha_k, nu_k, writhe)
