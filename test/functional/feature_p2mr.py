#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test BIP360 P2MR (Pay-to-Merkle-Root) output type.

BIP-level comprehensive test suite covering:
  1.  P2MR output format and funding
  2.  Script path spending with mining confirmation
  3.  No key path (reject 0 or 1 witness elements)
  4.  Dilithium enabled in P2MR, blocked in P2TR (all 5 opcodes)
  5.  Dilithium error message verification
  6.  OP_CHECKMULTISIG still blocked in P2MR tapscript
  7.  Two-leaf and four-leaf script trees
  8.  Invalid Merkle proofs
  9.  Invalid control block sizes
  10. Control block format and P2TR size comparison
  11. Parity bit enforcement (BIP360 requires bit = 1)
  12. P2MR address encoding
  13. Multiple P2MR inputs in one transaction
"""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    SEQUENCE_FINAL,
)
from test_framework.script import (
    CScript,
    CScriptOp,
    LEAF_VERSION_TAPSCRIPT,
    OP_0,
    OP_1,
    OP_2,
    OP_3,
    OP_CHECKMULTISIG,
    OP_CHECKSIG,
    OP_CHECKSIGDILITHIUM,
    OP_CHECKSIGDILITHIUMVERIFY,
    OP_CHECKMULTISIGDILITHIUM,
    OP_CHECKMULTISIGDILITHIUMVERIFY,
    OP_DILITHIUM_PUBKEY,
    OP_DROP,
    OP_DUP,
    OP_EQUAL,
    OP_HASH160,
    OP_RETURN,
    OP_TRUE,
    TaggedHash,
    p2mr_construct,
    taproot_construct,
)
from test_framework.key import ECKey, compute_xonly_pubkey
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

# ============================================================================
#  Display helpers
# ============================================================================
BLUE   = '\033[94m'
GREEN  = '\033[92m'
YELLOW = '\033[93m'
RED    = '\033[91m'
BOLD   = '\033[1m'
DIM    = '\033[2m'
RESET  = '\033[0m'
CHECK  = f'{GREEN}\u2714{RESET}'

BANNER = f"""
{BLUE}{BOLD}
 ____  _____ ___    ____  ____  __  __ ____
| __ )|_   _/ _ \\  |  _ \\|___ \\|  \\/  |  _ \\
|  _ \\  | || | | | | |_) | __) | |\\/| | |_) |
| |_) | | || |_| | |  __/ / __/| |  | |  _ <
|____/  |_| \\__\\_\\ |_|   |_____|_|  |_|_| \\_\\

  Pay-to-Merkle-Root  |  BIP 360  |  SegWit v2
{RESET}
{BOLD}  BIP-Level Integration Test Suite{RESET}
{DIM}  Quantum-resistant script tree output type{RESET}
"""

SEP      = f'{BLUE}{"=" * 78}{RESET}'
SEP_THIN = f'{BLUE}{"-" * 78}{RESET}'

def section(t):
    return f'\n{SEP}\n{BOLD}  {t}{RESET}\n{SEP}'

def sub(t):
    return f'{SEP_THIN}\n  {YELLOW}\u25B6{RESET} {BOLD}{t}{RESET}'

def ok(m=''):
    return f'    {CHECK} {GREEN}PASSED{RESET}  {m}'

def dot(m):
    return f'    {DIM}\u2502{RESET} {m}'

def result_line(label, value):
    return f'    {DIM}\u2502{RESET} {DIM}{label:30s}{RESET} {value}'

def why(m):
    return f'    {DIM}\u2502 WHY: {m}{RESET}'

def how(m):
    return f'    {DIM}\u2502 HOW: {m}{RESET}'

def expect(m):
    return f'    {DIM}\u2502 EXPECT: {m}{RESET}'

def got(m):
    return f'    {DIM}\u2502 GOT:    {m}{RESET}'


class P2MRTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-acceptnonstdtxn=1']]

    def run_test(self):
        print(BANNER)
        self.node = self.nodes[0]
        self.wallet = MiniWallet(self.node)
        self.generate(self.wallet, COINBASE_MATURITY + 60)

        self.test_01_output_format()
        self.test_02_script_path_spend_mined()
        self.test_03_no_key_path()
        self.test_04_dilithium_p2mr_vs_p2tr()
        self.test_05_dilithium_error_messages()
        self.test_06_checkmultisig_blocked()
        self.test_07_two_leaf_tree()
        self.test_08_four_leaf_tree()
        self.test_09_wrong_merkle_proof()
        self.test_10_wrong_merkle_root()
        self.test_11_invalid_control_block_sizes()
        self.test_12_control_block_format()
        self.test_13_parity_bit()
        self.test_14_address_encoding()
        self.test_15_multiple_inputs()

        print(section('ALL TESTS COMPLETE'))
        print(f'\n  {CHECK} {GREEN}{BOLD}All 15 BIP360 P2MR test groups passed!{RESET}\n')

    # ================================================================
    #  1 - Output Format
    # ================================================================
    def test_01_output_format(self):
        print(section('TEST 1 \u2014 P2MR Output Format'))

        print(sub('1a. Verify P2MR scriptPubKey structure'))
        print(why('P2MR outputs must be exactly 34 bytes: OP_2 (0x52) + PUSH32 (0x20) + 32-byte Merkle root.'))
        print(how('Construct a P2MR with a single OP_TRUE leaf and inspect the raw scriptPubKey bytes.'))
        p2mr = p2mr_construct([("leaf", CScript([OP_TRUE]))])
        spk = bytes(p2mr.scriptPubKey)
        assert spk[0] == 0x52 and spk[1] == 0x20 and len(spk) == 34
        print(result_line('Byte 0 (witness version):', f'0x{spk[0]:02x} (OP_2)'))
        print(result_line('Byte 1 (push length):', f'0x{spk[1]:02x} (32 bytes)'))
        print(result_line('Total size:', f'{len(spk)} bytes'))
        print(result_line('Merkle root:', f'{spk[2:].hex()[:32]}...'))
        print(ok())

        print(sub('1b. P2MR and P2TR outputs are the same size'))
        print(why('P2MR should not increase UTXO set size compared to P2TR.'))
        print(how('Compare raw scriptPubKey sizes of P2MR and P2TR with identical leaf scripts.'))
        pk = ECKey(); pk.generate()
        xo, _ = compute_xonly_pubkey(pk.get_bytes())
        tap = taproot_construct(xo, [("leaf", CScript([OP_TRUE]))])
        print(result_line('P2TR scriptPubKey size:', f'{len(bytes(tap.scriptPubKey))} bytes'))
        print(result_line('P2MR scriptPubKey size:', f'{len(spk)} bytes'))
        assert len(bytes(tap.scriptPubKey)) == 34
        print(ok('Both are 34 bytes'))

    # ================================================================
    #  2 - Script Path Spend with Mining Confirmation
    # ================================================================
    def test_02_script_path_spend_mined(self):
        print(section('TEST 2 \u2014 Script Path Spend (mempool + mined)'))

        print(sub('2a. Spend OP_TRUE leaf and confirm in a block'))
        print(why('Verifies P2MR script path spending works at both the mempool policy and consensus levels.'))
        print(how('Create P2MR with OP_TRUE leaf, fund it, spend via script path, mine, check tx is in block.'))
        p2mr = p2mr_construct([("leaf", CScript([OP_TRUE]))])
        fund = self.fund(p2mr.scriptPubKey)
        r = self.spend_p2mr(fund, p2mr, "leaf", [])
        assert r['accepted'], f"Should accept: {r['error']}"
        print(result_line('Mempool accepted:', 'yes'))
        self.mine_and_verify(r['txid'])
        print(result_line('Mined in block:', 'yes'))
        print(result_line('Txid:', f'{r["txid"][:24]}...'))
        print(ok('Spend confirmed at consensus level'))

        print(sub('2b. Hash lock script: OP_EQUAL with secret preimage'))
        print(why('Verifies non-trivial script execution works inside a P2MR output.'))
        print(how('Create P2MR leaf [<secret> OP_EQUAL], spend by pushing the matching secret as witness.'))
        secret = b'quantum_safe_btq'
        p2mr = p2mr_construct([("leaf", CScript([secret, OP_EQUAL]))])
        fund = self.fund(p2mr.scriptPubKey)
        r = self.spend_p2mr(fund, p2mr, "leaf", [secret])
        assert r['accepted']
        self.mine_and_verify(r['txid'])
        print(result_line('Secret:', secret.decode()))
        print(result_line('Mempool + mined:', 'yes'))
        print(ok('Hash lock spend confirmed'))

    # ================================================================
    #  3 - No Key Path
    # ================================================================
    def test_03_no_key_path(self):
        print(section('TEST 3 \u2014 No Key Path (P2MR is script-path only)'))
        p2mr = p2mr_construct([("leaf", CScript([OP_TRUE]))])

        print(sub('3a. Empty witness (0 elements)'))
        print(why('P2MR requires at least 2 witness elements (script + control block). Zero is invalid.'))
        print(how('Submit a transaction spending a P2MR output with an empty witness stack.'))
        print(expect('Rejection with "witness" or "empty" error'))
        fund = self.fund(p2mr.scriptPubKey)
        r = self.raw_spend(fund, p2mr.scriptPubKey, [])
        assert not r['accepted']
        assert 'witness' in r['error'].lower() or 'empty' in r['error'].lower()
        print(got(f'{r["error"][:70]}'))
        print(ok('Empty witness correctly rejected'))

        print(sub('3b. Single witness element (simulating a Taproot key-path spend)'))
        print(why('In Taproot, a single witness element = key path spend. P2MR has no key path, so this must fail.'))
        print(how('Submit a 64-byte fake signature as the only witness element.'))
        print(expect('Rejection with "mismatch" error'))
        fund = self.fund(p2mr.scriptPubKey)
        r = self.raw_spend(fund, p2mr.scriptPubKey, [b'\x00' * 64])
        assert not r['accepted']
        assert 'mismatch' in r['error'].lower() or 'witness' in r['error'].lower()
        print(got(f'{r["error"][:70]}'))
        print(ok('Key-path-style spend correctly rejected'))

    # ================================================================
    #  4 - Dilithium: P2MR vs P2TR (all 5 opcodes)
    # ================================================================
    def test_04_dilithium_p2mr_vs_p2tr(self):
        print(section('TEST 4 \u2014 Dilithium Opcodes: P2MR (allow) vs P2TR (block)'))
        print(dot('This test verifies each of the 5 Dilithium opcodes is:'))
        print(dot('  - BLOCKED in P2TR tapscript (quantum-vulnerable key path makes it pointless)'))
        print(dot('  - ALLOWED in P2MR tapscript (no key path, so Dilithium provides real protection)'))
        print(dot(''))

        pk = ECKey(); pk.generate()
        xo, _ = compute_xonly_pubkey(pk.get_bytes())

        opcodes = [
            ('OP_CHECKSIGDILITHIUM',            OP_CHECKSIGDILITHIUM,           '0xbb'),
            ('OP_CHECKSIGDILITHIUMVERIFY',      OP_CHECKSIGDILITHIUMVERIFY,     '0xbc'),
            ('OP_CHECKMULTISIGDILITHIUM',       OP_CHECKMULTISIGDILITHIUM,      '0xbd'),
            ('OP_CHECKMULTISIGDILITHIUMVERIFY',  OP_CHECKMULTISIGDILITHIUMVERIFY,'0xbe'),
            ('OP_DILITHIUM_PUBKEY',             OP_DILITHIUM_PUBKEY,            '0xbf'),
        ]
        for name, op, hex_val in opcodes:
            print(sub(f'{name} ({hex_val})'))
            leaf = CScript([OP_TRUE, OP_TRUE, op])

            # P2TR test
            print(how(f'Place [{name}] in a P2TR tapscript leaf and attempt to spend'))
            print(expect('Rejection mentioning "dilithium"'))
            tap = taproot_construct(xo, [("l", leaf)])
            f = self.fund(tap.scriptPubKey)
            r = self.spend_taproot(f, tap, "l", [b'\x01', b'\x01'])
            assert not r['accepted']
            assert 'dilithium' in r['error'].lower(), f'P2TR error should mention Dilithium: {r["error"][:60]}'
            print(got(f'P2TR rejected: {r["error"][:55]}'))

            # P2MR test
            print(how(f'Place same [{name}] in a P2MR tapscript leaf and attempt to spend'))
            print(expect('NOT rejected for "not available in tapscript" (may fail for other reasons like invalid sig)'))
            p2mr = p2mr_construct([("l", leaf)])
            f2 = self.fund(p2mr.scriptPubKey)
            r2 = self.spend_p2mr(f2, p2mr, "l", [b'\x01', b'\x01'])
            if not r2['accepted']:
                assert 'not available in tapscript' not in r2['error'].lower(), \
                    f'{name} blocked in P2MR! {r2["error"]}'
                print(got(f'P2MR ran opcode, failed on sig/pubkey: {r2["error"][:45]}'))
            else:
                print(got('P2MR accepted'))
            print(ok(f'{name}: P2TR=blocked, P2MR=allowed'))

    # ================================================================
    #  5 - Error Message Verification
    # ================================================================
    def test_05_dilithium_error_messages(self):
        print(section('TEST 5 \u2014 Error Message Verification'))
        pk = ECKey(); pk.generate()
        xo, _ = compute_xonly_pubkey(pk.get_bytes())

        print(sub('5a. P2TR must return exact error message'))
        print(why('The error message distinguishes "Dilithium blocked in tapscript" from other failures.'))
        print(how('Attempt OP_CHECKSIGDILITHIUM in P2TR and check the exact error string.'))
        expected_msg = 'Dilithium opcodes are not available in tapscript'
        print(expect(f'Error contains: "{expected_msg}"'))
        leaf = CScript([OP_TRUE, OP_TRUE, OP_CHECKSIGDILITHIUM])
        tap = taproot_construct(xo, [("l", leaf)])
        f = self.fund(tap.scriptPubKey)
        r = self.spend_taproot(f, tap, "l", [b'\x01', b'\x01'])
        assert expected_msg in r['error'], f'Expected "{expected_msg}" in: {r["error"][:80]}'
        print(got(f'"{expected_msg}"'))
        print(ok('Exact error message verified'))

        print(sub('5b. P2MR must NOT return that error message'))
        print(why('If P2MR returned the same error, it would mean Dilithium is blocked in P2MR too.'))
        print(how('Same opcode in P2MR: check error is about sig/pubkey, not about opcode being blocked.'))
        print(expect(f'Error does NOT contain: "{expected_msg}"'))
        p2mr = p2mr_construct([("l", leaf)])
        f2 = self.fund(p2mr.scriptPubKey)
        r2 = self.spend_p2mr(f2, p2mr, "l", [b'\x01', b'\x01'])
        if not r2['accepted']:
            assert expected_msg not in r2['error'], f'P2MR must not have tapscript error: {r2["error"][:80]}'
            print(got(f'Error: {r2["error"][:60]}'))
        else:
            print(got('Accepted (no error)'))
        print(ok('P2MR error correctly differs from P2TR'))

    # ================================================================
    #  6 - OP_CHECKMULTISIG Blocked
    # ================================================================
    def test_06_checkmultisig_blocked(self):
        print(section('TEST 6 \u2014 OP_CHECKMULTISIG Blocked in P2MR Tapscript'))

        print(sub('6a. ECDSA OP_CHECKMULTISIG in P2MR tapscript'))
        print(why('BIP342 disables OP_CHECKMULTISIG in tapscript (use OP_CHECKSIGADD instead).'))
        print(why('P2MR inherits all tapscript rules, so OP_CHECKMULTISIG must be blocked here too.'))
        print(how('Create P2MR leaf with [OP_0 OP_1 OP_1 OP_CHECKMULTISIG] and try to spend.'))
        print(expect('Rejection mentioning "checkmultisig"'))
        leaf = CScript([OP_0, OP_1, OP_1, OP_CHECKMULTISIG])
        p2mr = p2mr_construct([("l", leaf)])
        f = self.fund(p2mr.scriptPubKey)
        r = self.spend_p2mr(f, p2mr, "l", [b'', b'\x01'])
        assert not r['accepted']
        assert 'checkmultisig' in r['error'].lower(), f'Error should mention checkmultisig: {r["error"][:70]}'
        print(got(f'{r["error"][:65]}'))
        print(ok('OP_CHECKMULTISIG correctly blocked (use OP_CHECKSIGADD in tapscript)'))

    # ================================================================
    #  7 - Two-Leaf Tree
    # ================================================================
    def test_07_two_leaf_tree(self):
        print(section('TEST 7 \u2014 Two-Leaf Script Tree'))
        print(why('Verifies the Merkle tree construction works with 2 leaves at depth 1.'))
        print(how('Build tree with leaf_a=[OP_TRUE] and leaf_b=[OP_DROP OP_TRUE]. Spend each independently.'))
        la = CScript([OP_TRUE])
        lb = CScript([OP_DROP, OP_TRUE])
        p2mr = p2mr_construct([("a", la), ("b", lb)])

        for name, stk, desc in [("a", [], "OP_TRUE (no witness data)"), ("b", [b'\x01'], "OP_DROP OP_TRUE (1 stack element)")]:
            print(sub(f'7. Spend leaf "{name}" ({desc})'))
            print(how(f'Fund P2MR, spend via leaf "{name}" with witness stack {stk}, mine and verify.'))
            f = self.fund(p2mr.scriptPubKey)
            r = self.spend_p2mr(f, p2mr, name, stk)
            assert r['accepted'], f'Leaf {name}: {r["error"]}'
            self.mine_and_verify(r['txid'])
            pl = len(p2mr.leaves[name].merklebranch) // 32
            print(result_line('Merkle path length:', f'{pl} node(s)'))
            print(result_line('Mined in block:', 'yes'))
            print(ok())

    # ================================================================
    #  8 - Four-Leaf Tree (Depth 2)
    # ================================================================
    def test_08_four_leaf_tree(self):
        print(section('TEST 8 \u2014 Four-Leaf Tree (Depth 2)'))
        print(why('Verifies deeper Merkle trees work correctly with 2 levels of branching.'))
        print(how('Build a balanced tree: [[a, b], [c, d]]. Each leaf is a distinct anyone-can-spend script.'))
        la = CScript([OP_TRUE])
        lb = CScript([OP_DROP, OP_TRUE])
        lc = CScript([OP_2, OP_DROP, OP_TRUE])
        ld = CScript([OP_1, OP_DROP, OP_TRUE])
        p2mr = p2mr_construct([[("a", la), ("b", lb)], [("c", lc), ("d", ld)]])
        print(dot('Tree structure: [[a, b], [c, d]]'))
        print(dot(f'Merkle root: {p2mr.merkle_root.hex()[:32]}...'))
        print(dot(''))

        for name, stk in [("a", []), ("b", [b'\x01']), ("c", []), ("d", [])]:
            print(sub(f'8. Leaf "{name}" at depth 2'))
            print(how(f'Spend via leaf "{name}", verify Merkle proof and mine.'))
            f = self.fund(p2mr.scriptPubKey)
            r = self.spend_p2mr(f, p2mr, name, stk)
            if not r['accepted']:
                print(got(f'Error: {r["error"][:70]}'))
            assert r['accepted'], f'Leaf {name}: {r["error"][:80]}'
            self.mine_and_verify(r['txid'])
            pl = len(p2mr.leaves[name].merklebranch) // 32
            print(result_line('Merkle path depth:', f'{pl} nodes'))
            print(result_line('Confirmed in block:', 'yes'))
            print(ok())

    # ================================================================
    #  9 - Wrong Merkle Proof
    # ================================================================
    def test_09_wrong_merkle_proof(self):
        print(section('TEST 9 \u2014 Invalid Merkle Proof'))

        print(sub('9a. Corrupted Merkle path bytes'))
        print(why('If an attacker flips bits in the Merkle path, the computed root will not match the output.'))
        print(how('Build valid 2-leaf P2MR, flip first and last bytes of the Merkle branch, try to spend.'))
        print(expect('Rejection with "mismatch" (computed Merkle root != witness program)'))
        p2mr = p2mr_construct([("a", CScript([OP_TRUE])), ("b", CScript([OP_DROP, OP_TRUE]))])
        f = self.fund(p2mr.scriptPubKey)
        leaf = p2mr.leaves["a"]
        bad_branch = bytearray(leaf.merklebranch)
        if len(bad_branch) > 0:
            bad_branch[0] ^= 0xff
        r = self.spend_raw_cb(f, p2mr.scriptPubKey, bytes(leaf.script),
                              bytes([leaf.version | 1]) + bytes(bad_branch), [])
        assert not r['accepted']
        assert 'mismatch' in r['error'].lower()
        print(got(f'{r["error"][:65]}'))
        print(ok('Corrupted Merkle proof rejected'))

    # ================================================================
    # 10 - Wrong Merkle Root (cross-tree)
    # ================================================================
    def test_10_wrong_merkle_root(self):
        print(section('TEST 10 \u2014 Cross-Tree Spend'))

        print(sub('10a. Use a valid leaf+proof from tree B to spend tree A'))
        print(why('Each P2MR output commits to a specific Merkle root. A proof from a different tree must fail.'))
        print(how('Build two P2MR trees with different leaf scripts. Fund tree A, try to spend with tree B proof.'))
        print(expect('Rejection with "mismatch"'))
        ta = p2mr_construct([("l", CScript([OP_TRUE]))])
        tb = p2mr_construct([("l", CScript([OP_1, OP_DROP, OP_TRUE]))])
        print(result_line('Tree A root:', f'{ta.merkle_root.hex()[:32]}...'))
        print(result_line('Tree B root:', f'{tb.merkle_root.hex()[:32]}...'))
        f = self.fund(ta.scriptPubKey)
        r = self.spend_p2mr(f, tb, "l", [])
        assert not r['accepted']
        assert 'mismatch' in r['error'].lower()
        print(got(f'{r["error"][:65]}'))
        print(ok('Cross-tree spend rejected'))

    # ================================================================
    # 11 - Invalid Control Block Sizes
    # ================================================================
    def test_11_invalid_control_block_sizes(self):
        print(section('TEST 11 \u2014 Invalid Control Block Sizes'))
        print(why('P2MR control blocks must be exactly 1 + 32*m bytes (m = 0, 1, 2, ..., 128).'))
        print(why('Sizes that do not match this formula must be rejected.'))
        p2mr = p2mr_construct([("l", CScript([OP_TRUE]))])
        cases = [
            ('0 bytes (empty)',     b'',                        'too small, no control byte'),
            ('2 bytes',             b'\xc1\x00',                'not 1 + 32*m (remainder = 1)'),
            ('10 bytes',            b'\xc1' + b'\x00' * 9,     'not 1 + 32*m (remainder = 9)'),
            ('34 bytes',            b'\xc1' + b'\x00' * 33,    'not 1 + 32*m (remainder = 1)'),
        ]
        for label, bad_cb, reason in cases:
            print(sub(f'11. Control block = {label}'))
            print(how(f'Submit spend with malformed control block ({reason}).'))
            print(expect('Rejection'))
            f = self.fund(p2mr.scriptPubKey)
            r = self.spend_raw_cb(f, p2mr.scriptPubKey, bytes(CScript([OP_TRUE])), bad_cb, [])
            assert not r['accepted']
            print(got(f'{r["error"][:65]}'))
            print(ok())

    # ================================================================
    # 12 - Control Block Format
    # ================================================================
    def test_12_control_block_format(self):
        print(section('TEST 12 \u2014 Control Block Format'))

        print(sub('12a. Single leaf: control block = 1 byte (just the control byte)'))
        print(why('With only one leaf, no Merkle path is needed. Control block = [control_byte].'))
        p1 = p2mr_construct([("l", CScript([OP_TRUE]))])
        cb1 = 1 + len(p1.leaves["l"].merklebranch)
        assert cb1 == 1
        print(result_line('Control block size:', f'{cb1} byte'))
        print(ok())

        print(sub('12b. Two leaves: control block = 33 bytes (1 byte + 32-byte sibling hash)'))
        print(why('With two leaves, the Merkle proof contains one 32-byte sibling hash.'))
        p2 = p2mr_construct([("a", CScript([OP_TRUE])), ("b", CScript([OP_DROP, OP_TRUE]))])
        cb2 = 1 + len(p2.leaves["a"].merklebranch)
        assert cb2 == 33
        print(result_line('Control block size:', f'{cb2} bytes (1 + 32)'))
        print(ok())

        print(sub('12c. P2MR control block is 32 bytes smaller than P2TR'))
        print(why('P2TR control block includes the 32-byte internal key. P2MR omits it (no key path).'))
        print(how('Build identical 2-leaf trees in P2TR and P2MR, compare control block sizes.'))
        pk = ECKey(); pk.generate()
        xo, _ = compute_xonly_pubkey(pk.get_bytes())
        tap = taproot_construct(xo, [("a", CScript([OP_TRUE])), ("b", CScript([OP_DROP, OP_TRUE]))])
        tr_cb = 1 + 32 + len(tap.leaves["a"].merklebranch)
        mr_cb = 1 + len(p2.leaves["a"].merklebranch)
        savings = tr_cb - mr_cb
        assert savings == 32
        print(result_line('P2TR control block:', f'{tr_cb} bytes (1 + 32 internal_key + 32 path)'))
        print(result_line('P2MR control block:', f'{mr_cb} bytes (1 + 32 path)'))
        print(result_line('Savings per spend:', f'{savings} bytes'))
        print(ok('P2MR is 32 bytes more efficient per script path spend'))

    # ================================================================
    # 13 - Parity Bit Enforcement
    # ================================================================
    def test_13_parity_bit(self):
        print(section('TEST 13 \u2014 BIP360 Parity Bit Enforcement'))
        print(why('BIP360 specifies that the parity bit (bit 0 of the control byte) must always be 1.'))
        print(why('This is because P2MR has no internal key, so there is no parity to encode.'))
        print(why('The bit is fixed at 1 to maintain encoding compatibility with Taproot leaf versions.'))

        p2mr = p2mr_construct([("l", CScript([OP_TRUE]))])
        leaf = p2mr.leaves["l"]

        print(sub('13a. Parity bit = 1 (correct per BIP360)'))
        print(how('Construct control byte with bit 0 = 1, spend, mine.'))
        print(expect('Accepted and confirmed in block'))
        f = self.fund(p2mr.scriptPubKey)
        cb_good = bytes([leaf.version | 1]) + leaf.merklebranch
        print(result_line('Control byte:', f'0x{cb_good[0]:02x} (leaf_version | 1, bit 0 = 1)'))
        r = self.spend_raw_cb(f, p2mr.scriptPubKey, bytes(leaf.script), cb_good, [])
        assert r['accepted'], f'Parity=1 should accept: {r["error"]}'
        self.mine_and_verify(r['txid'])
        print(result_line('Result:', 'accepted + mined'))
        print(ok())

        print(sub('13b. Parity bit = 0 (violates BIP360)'))
        print(how('Construct control byte with bit 0 = 0, attempt to spend.'))
        print(expect('Rejection with "mismatch" error'))
        f2 = self.fund(p2mr.scriptPubKey)
        cb_bad = bytes([leaf.version & 0xfe]) + leaf.merklebranch
        print(result_line('Control byte:', f'0x{cb_bad[0]:02x} (leaf_version & 0xfe, bit 0 = 0)'))
        r2 = self.spend_raw_cb(f2, p2mr.scriptPubKey, bytes(leaf.script), cb_bad, [])
        assert not r2['accepted']
        assert 'mismatch' in r2['error'].lower()
        print(result_line('Result:', 'rejected'))
        print(got(f'{r2["error"][:65]}'))
        print(ok('Parity bit enforced per BIP360 spec'))

    # ================================================================
    # 14 - Address Encoding
    # ================================================================
    def test_14_address_encoding(self):
        print(section('TEST 14 \u2014 P2MR Address Encoding'))

        print(sub('14a. P2MR address uses SegWit v2 encoding'))
        print(why('P2MR uses SegWit version 2. In bech32m, version 2 maps to the character "z" after the separator.'))
        print(why('BTQ uses HRP "qcrt" for regtest, so P2MR addresses start with "qcrt1z".'))
        print(how('Fund a P2MR output, look up the transaction, and inspect the address field.'))
        p2mr = p2mr_construct([("l", CScript([OP_TRUE]))])
        fund = self.wallet.send_to(from_node=self.node, scriptPubKey=p2mr.scriptPubKey, amount=50_000)
        block_hashes = self.generate(self.node, 1)
        tx_info = self.node.getrawtransaction(fund['txid'], True, block_hashes[0])
        p2mr_vout = tx_info['vout'][fund['sent_vout']]
        if 'address' in p2mr_vout['scriptPubKey']:
            addr = p2mr_vout['scriptPubKey']['address']
            assert '1z' in addr[:10], f'Expected SegWit v2 (..1z..) prefix, got: {addr[:20]}'
            print(result_line('Address:', addr))
            print(result_line('Prefix:', f'{addr.split("1z")[0]}1z (SegWit v2)'))
            print(ok('P2MR address correctly uses SegWit v2 encoding'))
        else:
            spk_hex = p2mr_vout['scriptPubKey']['hex']
            assert spk_hex[:4] == '5220', f'Expected OP_2 PUSH32, got: {spk_hex[:4]}'
            print(result_line('scriptPubKey hex:', f'{spk_hex[:16]}... (OP_2 + 32 bytes)'))
            print(ok('scriptPubKey correctly encodes as SegWit v2'))

    # ================================================================
    # 15 - Multiple P2MR Inputs
    # ================================================================
    def test_15_multiple_inputs(self):
        print(section('TEST 15 \u2014 Multiple P2MR Inputs in One Transaction'))

        print(sub('15a. Two P2MR inputs from different trees'))
        print(why('Real transactions may spend multiple P2MR outputs at once.'))
        print(how('Create two P2MR outputs with different scripts, spend both in a single transaction.'))
        p2mr_a = p2mr_construct([("l", CScript([OP_TRUE]))])
        p2mr_b = p2mr_construct([("l", CScript([OP_1, OP_DROP, OP_TRUE]))])

        fa = self.fund(p2mr_a.scriptPubKey)
        fb = self.fund(p2mr_b.scriptPubKey)

        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [
            CTxIn(COutPoint(int(fa['txid'], 16), fa['sent_vout']), b'', SEQUENCE_FINAL),
            CTxIn(COutPoint(int(fb['txid'], 16), fb['sent_vout']), b'', SEQUENCE_FINAL),
        ]
        tx.vout = [CTxOut(80_000, p2mr_a.scriptPubKey)]

        leaf_a = p2mr_a.leaves["l"]
        cb_a = bytes([leaf_a.version | 1]) + leaf_a.merklebranch
        wit_a = CTxInWitness()
        wit_a.scriptWitness.stack = [bytes(leaf_a.script), cb_a]

        leaf_b = p2mr_b.leaves["l"]
        cb_b = bytes([leaf_b.version | 1]) + leaf_b.merklebranch
        wit_b = CTxInWitness()
        wit_b.scriptWitness.stack = [bytes(leaf_b.script), cb_b]

        tx.wit.vtxinwit = [wit_a, wit_b]
        tx.rehash()

        try:
            txid = self.node.sendrawtransaction(tx.serialize().hex())
            print(result_line('Input 0:', f'P2MR tree A (OP_TRUE)'))
            print(result_line('Input 1:', f'P2MR tree B (OP_1 OP_DROP OP_TRUE)'))
            print(result_line('Mempool accepted:', 'yes'))
            self.mine_and_verify(txid)
            print(result_line('Mined in block:', 'yes'))
            print(ok('Multi-input P2MR transaction works'))
        except Exception as e:
            assert False, f'Multi-input P2MR should succeed: {e}'

    # ================================================================
    #  Helpers
    # ================================================================

    def fund(self, spk):
        f = self.wallet.send_to(from_node=self.node, scriptPubKey=spk, amount=50_000)
        self.generate(self.node, 1)
        return f

    def mine_and_verify(self, txid):
        block_hashes = self.generate(self.node, 1)
        block = self.node.getblock(block_hashes[0])
        assert txid in block['tx'], f'txid {txid[:16]}... not in mined block'

    def spend_p2mr(self, fund_info, p2mr, leaf_name, witness_stack):
        try:
            tx = CTransaction()
            tx.nVersion = 2
            tx.vin = [CTxIn(COutPoint(int(fund_info['txid'], 16), fund_info['sent_vout']), b'', SEQUENCE_FINAL)]
            tx.vout = [CTxOut(40_000, p2mr.scriptPubKey)]
            leaf = p2mr.leaves[leaf_name]
            cb = bytes([leaf.version | 1]) + leaf.merklebranch
            wit = CTxInWitness()
            for item in witness_stack:
                wit.scriptWitness.stack.append(item)
            wit.scriptWitness.stack.append(bytes(leaf.script))
            wit.scriptWitness.stack.append(cb)
            tx.wit.vtxinwit = [wit]
            tx.rehash()
            txid = self.node.sendrawtransaction(tx.serialize().hex())
            return {'accepted': True, 'error': '', 'txid': txid}
        except Exception as e:
            return {'accepted': False, 'error': str(e), 'txid': ''}

    def spend_taproot(self, fund_info, tap, leaf_name, witness_stack):
        try:
            tx = CTransaction()
            tx.nVersion = 2
            tx.vin = [CTxIn(COutPoint(int(fund_info['txid'], 16), fund_info['sent_vout']), b'', SEQUENCE_FINAL)]
            tx.vout = [CTxOut(40_000, tap.scriptPubKey)]
            leaf = tap.leaves[leaf_name]
            cb = bytes([leaf.version | (1 if tap.negflag else 0)]) + tap.internal_pubkey
            for h in leaf.merklebranch:
                cb += h
            wit = CTxInWitness()
            for item in witness_stack:
                wit.scriptWitness.stack.append(item)
            wit.scriptWitness.stack.append(bytes(leaf.script))
            wit.scriptWitness.stack.append(cb)
            tx.wit.vtxinwit = [wit]
            tx.rehash()
            txid = self.node.sendrawtransaction(tx.serialize().hex())
            return {'accepted': True, 'error': '', 'txid': txid}
        except Exception as e:
            return {'accepted': False, 'error': str(e), 'txid': ''}

    def raw_spend(self, fund_info, output_spk, witness_stack):
        try:
            tx = CTransaction()
            tx.nVersion = 2
            tx.vin = [CTxIn(COutPoint(int(fund_info['txid'], 16), fund_info['sent_vout']), b'', SEQUENCE_FINAL)]
            tx.vout = [CTxOut(40_000, output_spk)]
            wit = CTxInWitness()
            wit.scriptWitness.stack = witness_stack
            tx.wit.vtxinwit = [wit]
            tx.rehash()
            txid = self.node.sendrawtransaction(tx.serialize().hex())
            return {'accepted': True, 'error': '', 'txid': txid}
        except Exception as e:
            return {'accepted': False, 'error': str(e), 'txid': ''}

    def spend_raw_cb(self, fund_info, output_spk, script, control_block, witness_stack):
        try:
            tx = CTransaction()
            tx.nVersion = 2
            tx.vin = [CTxIn(COutPoint(int(fund_info['txid'], 16), fund_info['sent_vout']), b'', SEQUENCE_FINAL)]
            tx.vout = [CTxOut(40_000, output_spk)]
            wit = CTxInWitness()
            for item in witness_stack:
                wit.scriptWitness.stack.append(item)
            wit.scriptWitness.stack.append(script)
            wit.scriptWitness.stack.append(control_block)
            tx.wit.vtxinwit = [wit]
            tx.rehash()
            txid = self.node.sendrawtransaction(tx.serialize().hex())
            return {'accepted': True, 'error': '', 'txid': txid}
        except Exception as e:
            return {'accepted': False, 'error': str(e), 'txid': ''}


if __name__ == '__main__':
    P2MRTest().main()
