#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Copyright (c) 2026 OPTX / Jett Optx (jettoptics.ai)
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/MIT.
#
# Functional tests for OPTX Phase 7: OP_OPTX_KNOT and TKDF
# GENSYS OPTX btQ v1.0 — Topological Witness Extension for ML-DSA
#
# Tests:
#   1. TKDF derivation determinism and consistency
#   2. KnotWitnessData serialization round-trip
#   3. DoS bounds enforcement
#   4. Trefoil and figure-eight test vectors
#   5. OP_OPTX_KNOT script construction

import struct
import os
import sys
import importlib.util

# Direct import of optx_knot module to avoid triggering the full test_framework
_spec = importlib.util.spec_from_file_location(
    "optx_knot",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_framework", "optx_knot.py")
)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

# Re-export needed symbols
KnotWitnessData = _mod.KnotWitnessData
tkdf_derive = _mod.tkdf_derive
tkdf_derive_from_witness = _mod.tkdf_derive_from_witness
check_knot_data_bounds = _mod.check_knot_data_bounds
trefoil_test_vector = _mod.trefoil_test_vector
figure_eight_test_vector = _mod.figure_eight_test_vector
TKDF_ALPHA_SIZE = _mod.TKDF_ALPHA_SIZE
TKDF_NU_SIZE = _mod.TKDF_NU_SIZE
TKDF_SEED_SIZE = _mod.TKDF_SEED_SIZE
TKDF_OUTPUT_SIZE = _mod.TKDF_OUTPUT_SIZE
MAX_OPTX_CROSSINGS = _mod.MAX_OPTX_CROSSINGS
MAX_DT_CODE_SIZE = _mod.MAX_DT_CODE_SIZE
MAX_OPTX_KNOT_DATA_SIZE = _mod.MAX_OPTX_KNOT_DATA_SIZE
OP_OPTX_KNOT = _mod.OP_OPTX_KNOT
OP_OPTX_KNOT_VERIFY = _mod.OP_OPTX_KNOT_VERIFY


def test_tkdf_determinism():
    """TKDF produces identical output for identical inputs."""
    alpha_k = b"\x00" * TKDF_ALPHA_SIZE
    nu_k = b"\x00" * TKDF_NU_SIZE
    seed = b"\x42" * TKDF_SEED_SIZE

    out1 = tkdf_derive(alpha_k, nu_k, writhe=3, seed=seed)
    out2 = tkdf_derive(alpha_k, nu_k, writhe=3, seed=seed)

    assert out1 == out2, "TKDF must be deterministic"
    assert len(out1) == TKDF_OUTPUT_SIZE, f"TKDF output must be {TKDF_OUTPUT_SIZE} bytes"
    print("  PASS: TKDF determinism")


def test_tkdf_sensitivity():
    """TKDF output changes when any input changes."""
    alpha_k = b"\x00" * TKDF_ALPHA_SIZE
    nu_k = b"\x00" * TKDF_NU_SIZE
    seed = b"\x42" * TKDF_SEED_SIZE

    baseline = tkdf_derive(alpha_k, nu_k, writhe=3, seed=seed)

    # Change writhe
    diff_writhe = tkdf_derive(alpha_k, nu_k, writhe=4, seed=seed)
    assert baseline != diff_writhe, "TKDF must be sensitive to writhe"

    # Change seed
    diff_seed = tkdf_derive(alpha_k, nu_k, writhe=3, seed=b"\x43" * TKDF_SEED_SIZE)
    assert baseline != diff_seed, "TKDF must be sensitive to seed"

    # Change alpha_K
    diff_alpha = tkdf_derive(b"\x01" + b"\x00" * 15, nu_k, writhe=3, seed=seed)
    assert baseline != diff_alpha, "TKDF must be sensitive to alpha_K"

    # Change nu_K
    diff_nu = tkdf_derive(alpha_k, b"\x01" + b"\x00" * 15, writhe=3, seed=seed)
    assert baseline != diff_nu, "TKDF must be sensitive to nu_K"

    print("  PASS: TKDF sensitivity")


def test_knot_witness_roundtrip():
    """KnotWitnessData serializes and deserializes correctly."""
    trefoil = trefoil_test_vector()

    # Serialize
    data = trefoil.to_bytes()
    assert len(data) == trefoil.size(), "Size must match serialized length"

    # Deserialize
    parsed = KnotWitnessData()
    assert parsed.from_bytes(data), "Deserialization must succeed"
    assert parsed.dt_code == trefoil.dt_code, "DT code must round-trip"
    assert parsed.alpha_k == trefoil.alpha_k, "alpha_K must round-trip"
    assert parsed.nu_k == trefoil.nu_k, "nu_K must round-trip"
    assert parsed.writhe == trefoil.writhe, "writhe must round-trip"

    # Re-serialize and compare
    data2 = parsed.to_bytes()
    assert data == data2, "Double round-trip must be identical"

    print("  PASS: KnotWitnessData round-trip")


def test_figure_eight_roundtrip():
    """Figure-eight (4_1) test vector round-trips correctly."""
    fig8 = figure_eight_test_vector()
    data = fig8.to_bytes()
    parsed = KnotWitnessData()
    assert parsed.from_bytes(data), "Figure-eight deserialization must succeed"
    assert parsed.writhe == 0, "Figure-eight writhe must be 0 (amphicheiral)"
    print("  PASS: Figure-eight round-trip")


def test_bounds_enforcement():
    """DoS bounds are enforced correctly."""
    # Valid trefoil data
    trefoil = trefoil_test_vector()
    valid_data = trefoil.to_bytes()
    assert check_knot_data_bounds(valid_data), "Valid trefoil must pass bounds"

    # Too small
    assert not check_knot_data_bounds(b"\x00" * 10), "Undersized data must fail"

    # Too large
    assert not check_knot_data_bounds(b"\x00" * (MAX_OPTX_KNOT_DATA_SIZE + 1)), "Oversized data must fail"

    # DT code length 0
    zero_dt = struct.pack(">H", 0) + b"\x00" * 38
    assert not check_knot_data_bounds(zero_dt), "Zero-length DT code must fail"

    # DT code too large (exceeds MAX_DT_CODE_SIZE)
    big_dt = struct.pack(">H", MAX_DT_CODE_SIZE + 1) + b"\x00" * (MAX_DT_CODE_SIZE + 1 + 36)
    assert not check_knot_data_bounds(big_dt), "Oversized DT code must fail"

    # Too many crossings (33 crossings = 66 bytes DT code)
    many_crossings_dt = struct.pack(">H", (MAX_OPTX_CROSSINGS + 1) * 2) + b"\x00" * ((MAX_OPTX_CROSSINGS + 1) * 2 + 36)
    assert not check_knot_data_bounds(many_crossings_dt), "Excess crossings must fail"

    # Too few crossings (2 crossings = 4 bytes, below minimum 3)
    few_crossings_dt = struct.pack(">H", 4) + b"\x00" * (4 + 36)
    assert not check_knot_data_bounds(few_crossings_dt), "Under-minimum crossings must fail"

    print("  PASS: DoS bounds enforcement")


def test_tkdf_from_witness():
    """TKDF derivation from KnotWitnessData matches direct derivation."""
    trefoil = trefoil_test_vector()
    seed = os.urandom(TKDF_SEED_SIZE)

    out_direct = tkdf_derive(trefoil.alpha_k, trefoil.nu_k, trefoil.writhe, seed)
    out_witness = tkdf_derive_from_witness(trefoil, seed)

    assert out_direct == out_witness, "Witness derivation must match direct"
    print("  PASS: TKDF from witness")


def test_opcode_values():
    """Opcode constants match C++ definitions."""
    assert OP_OPTX_KNOT == 0xc0, f"OP_OPTX_KNOT must be 0xc0, got 0x{OP_OPTX_KNOT:02x}"
    assert OP_OPTX_KNOT_VERIFY == 0xc1, f"OP_OPTX_KNOT_VERIFY must be 0xc1, got 0x{OP_OPTX_KNOT_VERIFY:02x}"
    print("  PASS: Opcode values")


def test_distinct_knots_distinct_tkdf():
    """Different knots produce different TKDF outputs (non-degeneracy)."""
    seed = b"\xAA" * TKDF_SEED_SIZE

    trefoil = trefoil_test_vector()
    fig8 = figure_eight_test_vector()

    out_trefoil = tkdf_derive_from_witness(trefoil, seed)
    out_fig8 = tkdf_derive_from_witness(fig8, seed)

    assert out_trefoil != out_fig8, "Distinct knots must produce distinct TKDF outputs"
    print("  PASS: Distinct knots -> distinct TKDF")


def main():
    print("=" * 60)
    print("OPTX Phase 7: OP_OPTX_KNOT + TKDF Functional Tests")
    print("GENSYS OPTX btQ v1.0 - Jett Optx / jettoptics.ai")
    print("=" * 60)
    print()

    test_tkdf_determinism()
    test_tkdf_sensitivity()
    test_knot_witness_roundtrip()
    test_figure_eight_roundtrip()
    test_bounds_enforcement()
    test_tkdf_from_witness()
    test_opcode_values()
    test_distinct_knots_distinct_tkdf()

    print()
    print("All tests PASSED.")
    print()
    print("Knot proofs verified. SpaceCowboys, AstroKnots, Guardians,")
    print("and Seekers secure Post-Q Bitcoin with Jett Optx - JOE.")


if __name__ == "__main__":
    main()
