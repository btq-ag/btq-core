#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Real m-of-n Dilithium multisig spends inside BIP360 P2MR.

BTQ has no Dilithium key aggregation, so m-of-n must be script-level. This test
exercises *actual* multisig spends (real Dilithium signatures, real mining
confirmation), which the existing feature_p2mr.py suite does not.

It covers two leaf constructions:

  1. A threshold accumulator built from OP_CHECKSIGDILITHIUM (emulating BIP342
     OP_CHECKSIGADD):

         OP_0
         (per key)  OP_TOALTSTACK <pk> OP_CHECKSIGDILITHIUM OP_FROMALTSTACK OP_ADD
         <m> OP_GREATERTHANOREQUAL

     Non-signers contribute an empty signature (which OP_CHECKSIGDILITHIUM scores
     as 0 without failing). This yields correct m-of-n semantics for *any* subset
     of signers, and is what a robust wallet should use.

  2. The native OP_CHECKMULTISIGDILITHIUM opcode (standard Bitcoin multisig script
     layout). After the interpreter fix, any m-of-n signer subset is accepted.
"""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    SEQUENCE_FINAL,
)
from test_framework.script import (
    CScript,
    OP_0,
    OP_ADD,
    OP_CHECKMULTISIGDILITHIUM,
    OP_CHECKSIGDILITHIUM,
    OP_FROMALTSTACK,
    OP_GREATERTHANOREQUAL,
    OP_TOALTSTACK,
    SIGHASH_ALL,
    TaprootSignatureHash,
    p2mr_construct,
)
from test_framework.dilithium import DilithiumKey, dilithium_available
from test_framework.test_framework import BTQTestFramework, SkipTest
from test_framework.wallet import MiniWallet


def threshold_leaf(m, pubkeys):
    """OP_CHECKSIGDILITHIUM accumulator: correct m-of-n for any signer subset."""
    ops = [OP_0]
    for pk in pubkeys:
        ops += [OP_TOALTSTACK, pk, OP_CHECKSIGDILITHIUM, OP_FROMALTSTACK, OP_ADD]
    ops += [m, OP_GREATERTHANOREQUAL]
    return CScript(ops)


def checkmultisig_leaf(m, pubkeys):
    """<m> pk.. <n> OP_CHECKMULTISIGDILITHIUM (standard Bitcoin multisig layout)."""
    return CScript([m, *pubkeys, len(pubkeys), OP_CHECKMULTISIGDILITHIUM])


class P2MRDilithiumMultisigTest(BTQTestFramework):
    FUND_SATOSHIS = 200_000

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-acceptnonstdtxn=1']]

    def skip_test_if_missing_module(self):
        if not dilithium_available():
            raise SkipTest("could not build the Dilithium reference library (needs a C compiler)")

    def run_test(self):
        self.node = self.nodes[0]
        self.wallet = MiniWallet(self.node)
        self.generate(self.wallet, COINBASE_MATURITY + 30)

        # Deterministic participant keys.
        self.keys = [DilithiumKey(seed=bytes([i + 1]) * 32) for i in range(3)]
        self.pubkeys = [k.pubkey for k in self.keys]

        self.test_threshold_all_subsets()
        self.test_threshold_under_threshold_rejected()
        self.test_n_of_n()
        self.test_checkmultisigdilithium_all_subsets()

        self.log.info("All Dilithium P2MR multisig tests passed.")

    # ------------------------------------------------------------------ helpers
    def fund(self, spk):
        f = self.wallet.send_to(from_node=self.node, scriptPubKey=spk, amount=self.FUND_SATOSHIS)
        self.generate(self.node, 1)
        return f

    def _build_spend_tx(self, fund, p2mr):
        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [CTxIn(COutPoint(int(fund['txid'], 16), fund['sent_vout']), b'', SEQUENCE_FINAL)]
        tx.vout = [CTxOut(180_000, p2mr.scriptPubKey)]
        return tx

    def _finish_and_send(self, tx, p2mr, leaf_name, exec_items):
        leaf = p2mr.leaves[leaf_name]
        cb = bytes([leaf.version | 1]) + leaf.merklebranch
        wit = CTxInWitness()
        wit.scriptWitness.stack = list(exec_items) + [bytes(leaf.script), cb]
        tx.wit.vtxinwit = [wit]
        tx.rehash()
        try:
            txid = self.node.sendrawtransaction(hexstring=tx.serialize().hex(), maxfeerate=0)
            return {'accepted': True, 'txid': txid, 'error': ''}
        except Exception as e:  # noqa: BLE001
            return {'accepted': False, 'txid': '', 'error': str(e)}

    def _sighash(self, tx, leaf, fund_spk):
        """BIP341 tapscript sighash for P2MR (required since v0.4.0)."""
        spent = [CTxOut(self.FUND_SATOSHIS, fund_spk)]
        return TaprootSignatureHash(
            tx, spent, SIGHASH_ALL,
            input_index=0,
            scriptpath=True,
            script=bytes(leaf.script),
            leaf_ver=leaf.version,
        )

    def spend_threshold(self, fund, p2mr, leaf_name, signer_indices):
        """Spend an accumulator leaf using the given subset of signer indices."""
        tx = self._build_spend_tx(fund, p2mr)
        leaf = p2mr.leaves[leaf_name]
        sighash = self._sighash(tx, leaf, p2mr.scriptPubKey)
        sigs = {i: self.keys[i].sign(sighash) + bytes([SIGHASH_ALL]) for i in signer_indices}
        # One slot per key, empty if that key did not sign; key[0] processed
        # first, so slots are pushed in reverse key order.
        exec_items = [sigs.get(k, b'') for k in reversed(range(len(self.pubkeys)))]
        return self._finish_and_send(tx, p2mr, leaf_name, exec_items)

    def spend_checkmultisig(self, fund, p2mr, leaf_name, signer_indices):
        """Spend a CHECKMULTISIGDILITHIUM leaf; sigs ordered by ascending key index."""
        tx = self._build_spend_tx(fund, p2mr)
        leaf = p2mr.leaves[leaf_name]
        sighash = self._sighash(tx, leaf, p2mr.scriptPubKey)
        ordered = sorted(signer_indices)
        exec_items = [b'']  # CHECKMULTISIG NULLDUMMY element
        exec_items += [self.keys[i].sign(sighash) + bytes([SIGHASH_ALL]) for i in ordered]
        return self._finish_and_send(tx, p2mr, leaf_name, exec_items)

    def mine_and_verify(self, txid):
        block_hash = self.generate(self.node, 1)[0]
        assert txid in self.node.getblock(block_hash)['tx'], "spend not mined"

    # ------------------------------------------------------------------- tests
    def test_threshold_all_subsets(self):
        self.log.info("2-of-3 accumulator: every 2- and 3-signer subset spends and confirms")
        leaf = threshold_leaf(2, self.pubkeys)
        for subset in [(0, 1), (0, 2), (1, 2), (0, 1, 2)]:
            p2mr = p2mr_construct([("ms", leaf)])
            fund = self.fund(p2mr.scriptPubKey)
            r = self.spend_threshold(fund, p2mr, "ms", subset)
            assert r['accepted'], f"subset {subset} should spend: {r['error']}"
            self.mine_and_verify(r['txid'])
            self.log.info(f"  subset {subset}: accepted and mined")

    def test_threshold_under_threshold_rejected(self):
        self.log.info("2-of-3 accumulator: a single signature is rejected")
        leaf = threshold_leaf(2, self.pubkeys)
        p2mr = p2mr_construct([("ms", leaf)])
        fund = self.fund(p2mr.scriptPubKey)
        r = self.spend_threshold(fund, p2mr, "ms", (1,))
        assert not r['accepted'], "under-threshold spend must be rejected"
        self.log.info(f"  rejected as expected: {r['error'][:60]}")

        # Empty signatures for everyone -> accumulator = 0 < 2 -> reject.
        r0 = self.spend_threshold(fund, p2mr, "ms", ())
        assert not r0['accepted'], "zero-signature spend must be rejected"

    def test_n_of_n(self):
        self.log.info("3-of-3 accumulator: all three required")
        leaf = threshold_leaf(3, self.pubkeys)
        p2mr = p2mr_construct([("ms", leaf)])
        fund = self.fund(p2mr.scriptPubKey)
        r_bad = self.spend_threshold(fund, p2mr, "ms", (0, 2))
        assert not r_bad['accepted'], "2 of 3 must fail for a 3-of-3"
        r_ok = self.spend_threshold(fund, p2mr, "ms", (0, 1, 2))
        assert r_ok['accepted'], f"3-of-3 with all signers should pass: {r_ok['error']}"
        self.mine_and_verify(r_ok['txid'])
        self.log.info("  3-of-3 confirmed; 2 signatures correctly rejected")

    def test_checkmultisigdilithium_all_subsets(self):
        self.log.info("OP_CHECKMULTISIGDILITHIUM: every 2-signer subset spends and confirms")
        leaf = checkmultisig_leaf(2, self.pubkeys)
        for subset in [(0, 1), (0, 2), (1, 2)]:
            p2mr = p2mr_construct([("ms", leaf)])
            fund = self.fund(p2mr.scriptPubKey)
            r = self.spend_checkmultisig(fund, p2mr, "ms", subset)
            assert r['accepted'], f"subset {subset} should spend: {r['error']}"
            self.mine_and_verify(r['txid'])
            self.log.info(f"  subset {subset}: accepted and mined")

        self.log.info("OP_CHECKMULTISIGDILITHIUM: under-threshold rejected")
        p2mr = p2mr_construct([("ms", leaf)])
        fund = self.fund(p2mr.scriptPubKey)
        r = self.spend_checkmultisig(fund, p2mr, "ms", (0,))
        assert not r['accepted'], "single signature must fail for 2-of-3"
        self.log.info(f"  rejected as expected: {r['error'][:60]}")


if __name__ == '__main__':
    P2MRDilithiumMultisigTest().main()
