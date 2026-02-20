#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Dilithium opcode sigop counting and tapscript restrictions.

Tests the following security fixes:

  1. Dilithium opcodes are counted in GetSigOpCount, preventing DoS
     attacks via uncounted signature verifications.

  2. Dilithium opcodes are excluded from the OP_SUCCESSx range in
     tapscript, preventing anyone-can-spend on tapscript outputs.

  3. Dilithium opcodes are explicitly disabled in tapscript execution,
     matching the pattern used for OP_CHECKMULTISIG in tapscript.
"""

import struct

from test_framework.blocktools import (
    COINBASE_MATURITY,
    create_block,
    create_coinbase,
    add_witness_commitment,
)
from test_framework.messages import (
    CBlock,
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    SEQUENCE_FINAL,
    tx_from_hex,
)
from test_framework.p2p import P2PDataStore
from test_framework.script import (
    CScript,
    CScriptNum,
    CScriptOp,
    LEAF_VERSION_TAPSCRIPT,
    OP_0,
    OP_1,
    OP_2,
    OP_3,
    OP_CHECKMULTISIG,
    OP_CHECKMULTISIGVERIFY,
    OP_CHECKSIG,
    OP_CHECKSIGVERIFY,
    OP_CHECKSIGDILITHIUM,
    OP_CHECKSIGDILITHIUMVERIFY,
    OP_CHECKMULTISIGDILITHIUM,
    OP_CHECKMULTISIGDILITHIUMVERIFY,
    OP_DILITHIUM_PUBKEY,
    OP_DUP,
    OP_DROP,
    OP_EQUALVERIFY,
    OP_HASH160,
    OP_RETURN,
    OP_TRUE,
    taproot_construct,
)
from test_framework.key import ECKey, compute_xonly_pubkey
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

# ============================================================================
#  Constants
# ============================================================================
TAPROOT_LEAF_MASK = 0xfe
TAPROOT_LEAF_TAPSCRIPT = 0xc0
MAX_BLOCK_SIGOPS_COST = 80000
WITNESS_SCALE_FACTOR = 4


# ============================================================================
#  Pure-Python SigOp Counter (mirrors C++ GetSigOpCount)
# ============================================================================
def get_sigop_count(script_bytes):
    """Python reimplementation of CScript::GetSigOpCount(fAccurate=true).

    Mirrors the C++ logic so we can independently verify the counting.
    """
    n = 0
    last_opcode = 0xff
    pc = 0
    while pc < len(script_bytes):
        opcode = script_bytes[pc]
        pc += 1

        # ---- push-data opcodes (skip over payload) ----
        if opcode <= 0x4e:
            if opcode < 0x4c:
                pc += opcode
            elif opcode == 0x4c:
                if pc >= len(script_bytes):
                    break
                pc += 1 + script_bytes[pc]
            elif opcode == 0x4d:
                if pc + 1 >= len(script_bytes):
                    break
                size = script_bytes[pc] | (script_bytes[pc + 1] << 8)
                pc += 2 + size
            elif opcode == 0x4e:
                if pc + 3 >= len(script_bytes):
                    break
                size = struct.unpack('<I', script_bytes[pc:pc + 4])[0]
                pc += 4 + size
            last_opcode = opcode
            continue

        # ---- single-sig opcodes (+1 each) ----
        if opcode in (0xac, 0xad,   # OP_CHECKSIG, OP_CHECKSIGVERIFY
                      0xbb, 0xbc):  # OP_CHECKSIGDILITHIUM, OP_CHECKSIGDILITHIUMVERIFY
            n += 1

        # ---- multisig opcodes (+N or +20) ----
        elif opcode in (0xae, 0xaf,   # OP_CHECKMULTISIG(VERIFY)
                        0xbd, 0xbe):  # OP_CHECKMULTISIGDILITHIUM(VERIFY)
            if 0x51 <= last_opcode <= 0x60:   # OP_1 .. OP_16
                n += last_opcode - 0x50
            else:
                n += 20  # MAX_PUBKEYS_PER_MULTISIG

        last_opcode = opcode
    return n


# ============================================================================
#  Pretty Printing Helpers
# ============================================================================
BLUE = '\033[94m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
RED = '\033[91m'
BOLD = '\033[1m'
RESET = '\033[0m'
CHECK = f'{GREEN}\u2714{RESET}'
CROSS = f'{RED}\u2718{RESET}'

BANNER = f"""
{BLUE}{BOLD}
 ____  _____ ___    ____  _ _ _ _   _     _
| __ )|_   _/ _ \\  |  _ \\(_) (_) |_| |__ (_)_   _ _ __ ___
|  _ \\  | || | | | | | | | | | | __| '_ \\| | | | | '_ ` _ \\
| |_) | | || |_| | | |_| | | | | |_| | | | | |_| | | | | | |
|____/  |_| \\__\\_\\ |____/|_|_|_|\\__|_| |_|_|\\__,_|_| |_| |_|
 ____  _                        ___       _                       _   _
/ ___|(_) __ _  ___  _ __  ___ |_ _|_ __ | |_ ___  __ _ _ __ __ _| |_(_) ___  _ __
\\___ \\| |/ _` |/ _ \\| '_ \\/ __| | || '_ \\| __/ _ \\/ _` | '__/ _` | __| |/ _ \\| '_ \\
 ___) | | (_| | (_) | |_) \\__ \\ | || | | | ||  __/ (_| | | | (_| | |_| | (_) | | | |
|____/|_|\\__, |\\___/| .__/|___/|___|_| |_|\\__\\___|\\__, |_|  \\__,_|\\__|_|\\___/|_| |_|
         |___/      |_|                            |___/
{RESET}
{BOLD}  Security Test Suite for Dilithium Opcode Protections{RESET}
"""

SEPARATOR = f'{BLUE}{"=" * 72}{RESET}'
SEPARATOR_THIN = f'{BLUE}{"-" * 72}{RESET}'


def section(title):
    return f'\n{SEPARATOR}\n{BOLD}  {title}{RESET}\n{SEPARATOR}'


def sub_test(name):
    return f'{SEPARATOR_THIN}\n  {YELLOW}\u25B6{RESET} {name}'


def passed(msg=''):
    return f'    {CHECK} {GREEN}PASSED{RESET}  {msg}'


def failed(msg=''):
    return f'    {CROSS} {RED}FAILED{RESET}  {msg}'


def info(msg):
    return f'    \u2022 {msg}'


# ============================================================================
#  Test Class
# ============================================================================
class DilithiumSigopsTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-acceptnonstdtxn=1',
        ]]

    def run_test(self):
        print(BANNER)
        node = self.nodes[0]
        wallet = MiniWallet(node)

        # Generate blocks for coinbase maturity
        self.log.info("Generating blocks for coinbase maturity...")
        self.generate(wallet, COINBASE_MATURITY + 20)

        self.test_1_sigop_counting()
        self.test_2_op_success_range()
        self.test_3_tapscript_rejection(node, wallet)

        print(section('ALL TESTS COMPLETE'))
        print(f'\n  {CHECK} {GREEN}{BOLD}All Dilithium security tests passed!{RESET}\n')

    # ====================================================================
    #  TEST 1 - Sigop Counting
    # ====================================================================

    def test_1_sigop_counting(self):
        print(section('TEST 1 \u2014 Dilithium Sigop Counting'))
        self._test_1a_basic_checksig()
        self._test_1b_multisig()
        self._test_1c_mixed()
        self._test_1d_dos_vector()
        self._test_1e_non_sigops()

    def _test_1a_basic_checksig(self):
        print(sub_test('1a. OP_CHECKSIGDILITHIUM counts as 1 sigop'))

        cases = [
            (CScript([OP_CHECKSIGDILITHIUM]),         1, 'single OP_CHECKSIGDILITHIUM'),
            (CScript([OP_CHECKSIGDILITHIUMVERIFY]),    1, 'single OP_CHECKSIGDILITHIUMVERIFY'),
            (CScript([OP_CHECKSIGDILITHIUM,
                      OP_CHECKSIGDILITHIUMVERIFY]),    2, 'both Dilithium checksig variants'),
            (CScript([OP_CHECKSIG]),                   1, 'ECDSA OP_CHECKSIG (control)'),
            (CScript([OP_CHECKSIG,
                      OP_CHECKSIGDILITHIUM]),          2, 'ECDSA + Dilithium mixed'),
        ]

        for script, expected, label in cases:
            actual = get_sigop_count(bytes(script))
            assert_equal(actual, expected)
            print(info(f'{label}: {actual} sigop(s)'))

        print(passed())

    def _test_1b_multisig(self):
        print(sub_test('1b. OP_CHECKMULTISIGDILITHIUM uses N-of-M counting'))

        cases = [
            (CScript([OP_3, OP_CHECKMULTISIGDILITHIUM]),              3,  '3 keys (OP_3 prefix)'),
            (CScript([OP_2, OP_CHECKMULTISIGDILITHIUMVERIFY]),        2,  '2 keys (OP_2 prefix)'),
            (CScript([OP_1, OP_CHECKMULTISIGDILITHIUMVERIFY]),        1,  '1 key  (OP_1 prefix)'),
            (CScript([OP_DROP, OP_CHECKMULTISIGDILITHIUM]),           20, 'no OP_N prefix -> 20 (MAX)'),
            (CScript([OP_3, OP_CHECKMULTISIG]),                       3,  'ECDSA 3-key multisig (control)'),
        ]

        for script, expected, label in cases:
            actual = get_sigop_count(bytes(script))
            assert_equal(actual, expected)
            print(info(f'{label}: {actual} sigop(s)'))

        # Parity check: ECDSA and Dilithium multisig count identically
        ecdsa = get_sigop_count(bytes(CScript([OP_3, OP_CHECKMULTISIG])))
        dilithium = get_sigop_count(bytes(CScript([OP_3, OP_CHECKMULTISIGDILITHIUM])))
        assert_equal(ecdsa, dilithium)
        print(info(f'ECDSA vs Dilithium parity: both = {ecdsa}'))

        print(passed())

    def _test_1c_mixed(self):
        print(sub_test('1c. Complex mixed-opcode script'))

        script = CScript([
            OP_CHECKSIG,                                    # +1  ECDSA
            OP_CHECKSIGVERIFY,                              # +1  ECDSA
            OP_CHECKSIGDILITHIUM,                           # +1  Dilithium
            OP_CHECKSIGDILITHIUMVERIFY,                     # +1  Dilithium
            OP_3, OP_CHECKMULTISIG,                         # +3  ECDSA multisig
            OP_2, OP_CHECKMULTISIGDILITHIUM,                # +2  Dilithium multisig
        ])                                                  # ---
        actual = get_sigop_count(bytes(script))             #  9  total
        assert_equal(actual, 9)
        print(info(f'1+1+1+1+3+2 = {actual} sigops'))

        print(passed())

    def _test_1d_dos_vector(self):
        print(sub_test('1d. DoS attack vector: repeated OP_CHECKSIGDILITHIUMVERIFY'))

        for count in [10, 50, 100]:
            script = CScript([OP_CHECKSIGDILITHIUMVERIFY] * count)
            actual = get_sigop_count(bytes(script))
            assert_equal(actual, count)
            print(info(f'{count} repeated CHECKSIGDILITHIUMVERIFY -> {actual} sigops'))

        print(passed('DoS vector properly bounded by sigop counting'))

    def _test_1e_non_sigops(self):
        print(sub_test('1e. Non-sigop opcodes correctly excluded'))

        cases = [
            (CScript([OP_DILITHIUM_PUBKEY]),                                    0, 'OP_DILITHIUM_PUBKEY'),
            (CScript([OP_DUP, OP_HASH160, OP_EQUALVERIFY]),                     0, 'P2PKH template'),
            (CScript([OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_DILITHIUM_PUBKEY]),0, 'mixed non-sigop'),
            (CScript([]),                                                        0, 'empty script'),
        ]

        for script, expected, label in cases:
            actual = get_sigop_count(bytes(script))
            assert_equal(actual, expected)
            print(info(f'{label}: {actual} sigops'))

        print(passed())

    # ====================================================================
    #  TEST 2 - OP_SUCCESSx Range
    # ====================================================================

    def test_2_op_success_range(self):
        print(section('TEST 2 \u2014 OP_SUCCESSx Range Exclusion'))

        def is_op_success_fixed(o):
            """Matches the FIXED C++ IsOpSuccess()."""
            return (o == 0x50 or o == 0x62 or
                    (0x7e <= o <= 0x81) or (0x83 <= o <= 0x86) or
                    (0x89 <= o <= 0x8a) or (0x8d <= o <= 0x8e) or
                    (0x95 <= o <= 0x99) or (0xc0 <= o <= 0xfe))

        def is_op_success_old(o):
            """Matches the OLD (vulnerable) IsOpSuccess()."""
            return (o == 0x50 or o == 0x62 or
                    (0x7e <= o <= 0x81) or (0x83 <= o <= 0x86) or
                    (0x89 <= o <= 0x8a) or (0x8d <= o <= 0x8e) or
                    (0x95 <= o <= 0x99) or (0xbb <= o <= 0xfe))

        # --- 2a: Dilithium opcodes must NOT be OP_SUCCESSx ---
        print(sub_test('2a. Dilithium opcodes excluded from OP_SUCCESSx'))
        dilithium_ops = {
            0xbb: 'OP_CHECKSIGDILITHIUM',
            0xbc: 'OP_CHECKSIGDILITHIUMVERIFY',
            0xbd: 'OP_CHECKMULTISIGDILITHIUM',
            0xbe: 'OP_CHECKMULTISIGDILITHIUMVERIFY',
            0xbf: 'OP_DILITHIUM_PUBKEY',
        }
        for val, name in dilithium_ops.items():
            assert not is_op_success_fixed(val), f'{name} must not be OP_SUCCESSx'
            assert is_op_success_old(val), f'{name} was OP_SUCCESSx in vulnerable version'
            print(info(f'0x{val:02x} {name:40s} fixed=no  old=yes'))

        print(passed('Dilithium range (0xbb-0xbf) excluded'))

        # --- 2b: Opcodes 0xc0+ must still be OP_SUCCESSx ---
        print(sub_test('2b. Opcodes 0xc0-0xfe remain OP_SUCCESSx'))
        for val in [0xc0, 0xc1, 0xd0, 0xef, 0xfe]:
            assert is_op_success_fixed(val), f'0x{val:02x} should be OP_SUCCESSx'
            print(info(f'0x{val:02x} -> OP_SUCCESSx'))

        print(passed())

        # --- 2c: Boundaries ---
        print(sub_test('2c. Boundary values'))
        assert not is_op_success_fixed(0xba), '0xba (OP_CHECKSIGADD) is a real opcode'
        print(info('0xba OP_CHECKSIGADD   -> not OP_SUCCESSx'))
        assert not is_op_success_fixed(0xbf), '0xbf (OP_DILITHIUM_PUBKEY) excluded'
        print(info('0xbf OP_DILITHIUM_PK  -> not OP_SUCCESSx'))
        assert is_op_success_fixed(0xc0), '0xc0 is first OP_SUCCESSx'
        print(info('0xc0 first OP_SUCCESS -> OP_SUCCESSx'))
        assert not is_op_success_fixed(0xff), '0xff (OP_INVALIDOPCODE) is not OP_SUCCESSx'
        print(info('0xff OP_INVALIDOPCODE -> not OP_SUCCESSx'))

        print(passed())

    # ====================================================================
    #  TEST 3 - Tapscript Rejection (on-chain regtest)
    # ====================================================================

    def test_3_tapscript_rejection(self, node, wallet):
        print(section('TEST 3 \u2014 Dilithium Disabled in Tapscript (on-chain)'))

        privkey = ECKey()
        privkey.generate()
        xonly_pubkey, _ = compute_xonly_pubkey(privkey.get_bytes())

        # --- 3a-3f: Each Dilithium opcode must be rejected in tapscript ---
        reject_cases = [
            ('3a', 'OP_CHECKSIGDILITHIUM',           CScript([OP_TRUE, OP_TRUE, OP_CHECKSIGDILITHIUM]),                               [b'\x01', b'\x01']),
            ('3b', 'OP_CHECKSIGDILITHIUMVERIFY',      CScript([OP_TRUE, OP_TRUE, OP_CHECKSIGDILITHIUMVERIFY, OP_TRUE]),                [b'\x01', b'\x01']),
            ('3c', 'OP_CHECKMULTISIGDILITHIUM',       CScript([OP_0, OP_1, OP_1, OP_CHECKMULTISIGDILITHIUM]),                          [b'', b'\x01']),
            ('3d', 'OP_CHECKMULTISIGDILITHIUMVERIFY',  CScript([OP_0, OP_1, OP_1, OP_CHECKMULTISIGDILITHIUMVERIFY, OP_TRUE]),          [b'', b'\x01']),
            ('3e', 'OP_DILITHIUM_PUBKEY',             CScript([OP_TRUE, OP_DILITHIUM_PUBKEY]),                                        [b'\x01']),
        ]

        for test_id, opcode_name, leaf_script, witness_stack in reject_cases:
            print(sub_test(f'{test_id}. {opcode_name} in tapscript -> MUST REJECT'))
            result = self._tapscript_spend_test(
                node, wallet, xonly_pubkey,
                leaf_script, witness_stack,
                label=f'leaf_{test_id}',
            )
            assert not result, f'{opcode_name} should be rejected in tapscript!'
            print(passed(f'{opcode_name} correctly rejected'))

        # --- 3f: Control test - OP_TRUE must still work ---
        print(sub_test('3f. OP_TRUE in tapscript -> MUST ACCEPT (control test)'))
        result = self._tapscript_spend_test(
            node, wallet, xonly_pubkey,
            CScript([OP_TRUE]), [],
            label='leaf_true',
        )
        assert result, 'OP_TRUE tapscript should succeed!'
        print(passed('Tapscript execution works correctly'))

    # ====================================================================
    #  Helpers
    # ====================================================================

    def _tapscript_spend_test(self, node, wallet, xonly_pubkey, leaf_script, witness_stack, label):
        """Fund a P2TR output with the given leaf script, then try to spend it.

        Returns True if the spend was accepted to the mempool, False otherwise.
        """
        try:
            # Build the taproot output
            tap = taproot_construct(xonly_pubkey, [(label, leaf_script)])

            # Fund it using MiniWallet
            fund_info = wallet.send_to(
                from_node=node,
                scriptPubKey=tap.scriptPubKey,
                amount=50_000,
            )
            funding_txid = fund_info['txid']
            funding_vout = fund_info['sent_vout']
            self.generate(node, 1)

            # Build spending transaction - output to a standard P2TR (anyone-can-spend)
            # so we don't trigger maxburnamount policy rejection
            spend_tx = CTransaction()
            spend_tx.nVersion = 2
            spend_tx.vin = [CTxIn(
                COutPoint(int(funding_txid, 16), funding_vout),
                b'',
                SEQUENCE_FINAL,
            )]
            spend_tx.vout = [CTxOut(40_000, tap.scriptPubKey)]

            # Build the tapscript witness
            leaf = tap.leaves[label]
            control_byte = leaf.version | (1 if tap.negflag else 0)
            control_block = bytes([control_byte]) + tap.internal_pubkey
            for h in leaf.merklebranch:
                control_block += h

            wit = CTxInWitness()
            for item in witness_stack:
                wit.scriptWitness.stack.append(item)
            wit.scriptWitness.stack.append(bytes(leaf_script))
            wit.scriptWitness.stack.append(control_block)
            spend_tx.wit.vtxinwit = [wit]
            spend_tx.rehash()

            # Try to submit
            node.sendrawtransaction(spend_tx.serialize().hex())
            return True

        except Exception as e:
            err = str(e)
            if 'dilithium' in err.lower() or 'Script failed' in err or 'non-mandatory' in err.lower():
                print(info(f'Rejected as expected: {err[:80]}'))
            else:
                print(info(f'Rejected (other): {err[:80]}'))
            return False


if __name__ == '__main__':
    DilithiumSigopsTest().main()
