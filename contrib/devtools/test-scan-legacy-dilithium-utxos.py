#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests for scan-legacy-dilithium-utxos.py.

The scan reports a figure that an activation decision rests on, so the parts
that decide what counts are tested directly here: script classification, the
opcode walk that has to tell an executable OP_CHECKSIGDILITHIUM from the same
byte sitting inside a public key, and the spend bookkeeping that promotes a
hash-committed coin to a confirmed one.
"""

import importlib.util
import os
import unittest

_SPEC = importlib.util.spec_from_file_location(
    "scanner", os.path.join(os.path.dirname(os.path.realpath(__file__)),
                            "scan-legacy-dilithium-utxos.py"))
scanner = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(scanner)


OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_EQUAL = 0x76, 0xA9, 0x88, 0x87
OP_CHECKSIG = 0xAC
OP_0 = 0x00

H20 = bytes(range(20))
H32 = bytes(range(32))


def push(data):
    """Encode a minimal data push, as the script writer would."""
    n = len(data)
    if n < 0x4C:
        return bytes([n]) + data
    if n <= 0xFF:
        return bytes([scanner.OP_PUSHDATA1, n]) + data
    if n <= 0xFFFF:
        return bytes([scanner.OP_PUSHDATA2]) + n.to_bytes(2, "little") + data
    return bytes([scanner.OP_PUSHDATA4]) + n.to_bytes(4, "little") + data


def dilithium_pubkey():
    """1312 bytes that contain the Dilithium opcodes as data, not as opcodes.

    A real key is effectively random, so every one-byte value shows up in it
    around five times. Any scan that greps for 0xbb rather than walking the
    script will call a plain ECDSA output Dilithium.
    """
    key = bytearray(bytes(range(256)) * 6)[:scanner.DILITHIUM_PUBKEY_SIZE]
    assert len(key) == scanner.DILITHIUM_PUBKEY_SIZE
    assert bytes([scanner.OP_CHECKSIGDILITHIUM]) in key
    return bytes(key)


# Script shapes, written out rather than built by a helper so that a test
# failure points at a literal the reader can compare against the interpreter.
DILITHIUM_P2PKH = bytes([OP_DUP, OP_HASH160]) + push(H20) + bytes([OP_EQUALVERIFY, scanner.OP_CHECKSIGDILITHIUM])
ECDSA_P2PKH = bytes([OP_DUP, OP_HASH160]) + push(H20) + bytes([OP_EQUALVERIFY, OP_CHECKSIG])
DILITHIUM_BARE = push(dilithium_pubkey()) + bytes([scanner.OP_CHECKSIGDILITHIUM])
ECDSA_BARE_LOOKALIKE = push(dilithium_pubkey()) + bytes([OP_CHECKSIG])
P2WPKH = bytes([OP_0]) + push(H20)
P2WSH = bytes([OP_0]) + push(H32)
P2SH = bytes([OP_HASH160]) + push(H20) + bytes([OP_EQUAL])
P2TR = bytes([0x51]) + push(H32)


class TestClassify(unittest.TestCase):
    def test_dilithium_opcode_in_the_clear(self):
        for script, expected in ((DILITHIUM_P2PKH, "p2pkh"), (DILITHIUM_BARE, "bare")):
            kind, commitment = scanner.classify(script)
            self.assertEqual(kind, expected)
            self.assertIsNone(commitment, "a visible script commits to nothing")

    def test_hash_committed_shapes(self):
        self.assertEqual(scanner.classify(P2WPKH), ("p2wpkh", H20.hex()))
        self.assertEqual(scanner.classify(P2WSH), ("p2wsh", H32.hex()))
        self.assertEqual(scanner.classify(P2SH), ("p2sh", H20.hex()))

    def test_ordinary_scripts_are_ignored(self):
        for script in (ECDSA_P2PKH, P2TR, b"", bytes([0x6A]) + push(b"hello")):
            self.assertEqual(scanner.classify(script), (None, None))

    def test_opcode_byte_inside_a_pushed_key_is_not_an_opcode(self):
        """The false positive that would inflate the reported total."""
        self.assertEqual(scanner.classify(ECDSA_BARE_LOOKALIKE), (None, None))
        self.assertTrue(scanner.has_dilithium_opcode(DILITHIUM_BARE))
        self.assertFalse(scanner.has_dilithium_opcode(ECDSA_BARE_LOOKALIKE))

    def test_a_dilithium_p2wpkh_is_indistinguishable_from_an_ecdsa_one(self):
        """Why the scan needs spend evidence at all.

        Both destinations serialise to the same 22 bytes, so nothing in the
        UTXO set separates them. If this ever stops holding, the scan can be
        made exact and the residual bucket can go.
        """
        dilithium_v0 = bytes([OP_0]) + push(H20)
        ecdsa_v0 = bytes([OP_0]) + push(H20)
        self.assertEqual(dilithium_v0, ecdsa_v0)

    def test_all_four_dilithium_opcodes_count(self):
        for op in (scanner.OP_CHECKSIGDILITHIUM, scanner.OP_CHECKSIGDILITHIUMVERIFY,
                   scanner.OP_CHECKMULTISIGDILITHIUM, scanner.OP_CHECKMULTISIGDILITHIUMVERIFY):
            self.assertTrue(scanner.has_dilithium_opcode(push(H20) + bytes([op])))

    def test_truncated_push_does_not_hang_or_throw(self):
        for script in (bytes([0x4C]), bytes([0x4D, 0x01]), bytes([0x4E, 0x01, 0x00, 0x00]),
                       bytes([0x20]) + b"\x00" * 5):
            self.assertIsInstance(scanner.classify(script), tuple)


class TestScanBookkeeping(unittest.TestCase):
    def setUp(self):
        self.scan = scanner.Scan()

    def test_visible_output_is_reported_without_any_spend(self):
        self.scan.add_output("aa" * 32, 0, DILITHIUM_P2PKH, 5.0, 100)
        visible, hidden, residual = self.scan.findings()
        self.assertEqual(len(visible), 1)
        self.assertEqual(visible[0]["sats"], 500_000_000)
        self.assertEqual((hidden, residual), ([], {}))

    def test_unproved_p2wpkh_stays_in_the_residual_bucket(self):
        self.scan.add_output("bb" * 32, 0, P2WPKH, 1.5, 100)
        visible, hidden, residual = self.scan.findings()
        self.assertEqual((visible, hidden), ([], []))
        self.assertEqual(residual, {"p2wpkh": [1, 150_000_000]})

    def test_a_dilithium_sized_witness_key_proves_the_hash(self):
        # Two coins on one reused address. Spending the first reveals the key,
        # which is what lets the second be reported.
        self.scan.add_output("cc" * 32, 0, P2WPKH, 1.0, 100)
        self.scan.add_output("dd" * 32, 0, P2WPKH, 2.0, 101)
        self.scan.spend("cc" * 32, 0, [b"\x00" * 2420, dilithium_pubkey()], b"", "spender")

        visible, hidden, residual = self.scan.findings()
        self.assertEqual(visible, [])
        self.assertEqual(len(hidden), 1, "the spent coin is gone, the sibling is reported")
        self.assertEqual(hidden[0]["outpoint"], "dd" * 32 + ":0")
        self.assertEqual(hidden[0]["proved_by"], "spender")
        self.assertEqual(residual, {}, "nothing is left undetermined once the key is out")

    def test_an_ecdsa_witness_key_proves_nothing(self):
        self.scan.add_output("ee" * 32, 0, P2WPKH, 1.0, 100)
        self.scan.add_output("ff" * 32, 0, P2WPKH, 2.0, 101)
        self.scan.spend("ee" * 32, 0, [b"\x00" * 72, b"\x02" + b"\x11" * 32], b"", "spender")

        visible, hidden, residual = self.scan.findings()
        self.assertEqual((visible, hidden), ([], []))
        self.assertEqual(residual, {"p2wpkh": [1, 200_000_000]})

    def test_a_revealed_witness_script_proves_a_p2wsh_hash(self):
        self.scan.add_output("11" * 32, 0, P2WSH, 3.0, 100)
        self.scan.add_output("22" * 32, 0, P2WSH, 4.0, 101)
        self.scan.spend("11" * 32, 0, [b"", DILITHIUM_P2PKH], b"", "spender")

        _, hidden, residual = self.scan.findings()
        self.assertEqual([item["outpoint"] for item in hidden], ["22" * 32 + ":0"])
        self.assertEqual(residual, {})

    def test_a_revealed_redeem_script_proves_a_p2sh_hash(self):
        self.scan.add_output("33" * 32, 0, P2SH, 6.0, 100)
        self.scan.add_output("44" * 32, 0, P2SH, 7.0, 101)
        self.scan.spend("33" * 32, 0, [], push(b"\x00" * 64) + push(DILITHIUM_P2PKH), "spender")

        _, hidden, residual = self.scan.findings()
        self.assertEqual([item["outpoint"] for item in hidden], ["44" * 32 + ":0"])
        self.assertEqual(residual, {})

    def test_spending_an_untracked_or_unknown_output_is_harmless(self):
        self.scan.spend("99" * 32, 7, [b"", b""], b"", "spender")
        self.assertEqual(self.scan.findings(), ([], [], {}))

    def test_ordinary_outputs_never_enter_the_live_set(self):
        self.scan.add_output("55" * 32, 0, ECDSA_P2PKH, 9.0, 100)
        self.scan.add_output("55" * 32, 1, P2TR, 9.0, 100)
        self.assertEqual(self.scan.live, {})


class TestOpcodeWalk(unittest.TestCase):
    def test_pushdata_lengths_are_skipped_correctly(self):
        for size in (1, 0x4B, 0x4C, 0xFF, 0x100, scanner.DILITHIUM_PUBKEY_SIZE):
            script = push(bytes([scanner.OP_CHECKSIGDILITHIUM]) * size) + bytes([OP_CHECKSIG])
            self.assertEqual(list(scanner.iter_opcodes(script)), [OP_CHECKSIG],
                             f"push of {size} bytes was not skipped cleanly")

    def test_iter_pushes_recovers_the_last_stack_item(self):
        script = push(b"\x01" * 70) + push(DILITHIUM_P2PKH)
        self.assertEqual(list(scanner.iter_pushes(script))[-1], DILITHIUM_P2PKH)


if __name__ == "__main__":
    unittest.main()
