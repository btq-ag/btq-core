#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the dilithium_limits block sigop limit across activation.

Before activation ConnectBlock caps a block at 80000 sigop cost, from the
activation height at 160000. getblocktemplate reports the limit for the
block it templates. The P2MR Dilithium validation weight that activates at
the same height is covered by the p2mr_dilithium_validation_weight unit test.

Witness sigops come from P2WSH inputs whose script runs 100 zero-key
OP_CHECKMULTISIGs. Each counts as 20 witness sigops, so an input costs 2000
and needs no signature. Legacy sigops come from bare OP_CHECKSIGs in a
coinbase output, at WITNESS_SCALE_FACTOR each.
"""

from test_framework.blocktools import (
    COINBASE_MATURITY,
    MAX_BLOCK_SIGOPS_WEIGHT,
    MAX_BLOCK_SIGOPS_WEIGHT_DILITHIUM_LIMITS,
    add_witness_commitment,
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    COIN,
    WITNESS_SCALE_FACTOR,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
)
from test_framework.script import (
    CScript,
    OP_0,
    OP_1,
    OP_CHECKMULTISIG,
    OP_CHECKSIG,
    OP_DROP,
)
from test_framework.script_util import script_to_p2wsh_script
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

ACTIVATION_HEIGHT = COINBASE_MATURITY + 20
SIGOPS_PER_INPUT = 2000
WITNESS_SCRIPT = CScript([OP_0, OP_0, OP_0, OP_CHECKMULTISIG, OP_DROP] * (SIGOPS_PER_INPUT // 20) + [OP_1])
# 41 inputs: just over the old limit. 81 inputs: just over the new one.
OVER_OLD = MAX_BLOCK_SIGOPS_WEIGHT // SIGOPS_PER_INPUT + 1
OVER_NEW = MAX_BLOCK_SIGOPS_WEIGHT_DILITHIUM_LIMITS // SIGOPS_PER_INPUT + 1
# Bare OP_CHECKSIGs in the coinbase, counted at WITNESS_SCALE_FACTOR each
LEGACY_OVER_OLD = MAX_BLOCK_SIGOPS_WEIGHT // WITNESS_SCALE_FACTOR + 1
LEGACY_OVER_NEW = MAX_BLOCK_SIGOPS_WEIGHT_DILITHIUM_LIMITS // WITNESS_SCALE_FACTOR + 1


class DilithiumLimitsTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[f"-testactivationheight=dilithium_limits@{ACTIVATION_HEIGHT}"]]

    def spend(self, fund_tx, count):
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(fund_tx.sha256, n)) for n in range(count)]
        tx.vout = [CTxOut(sum(o.nValue for o in fund_tx.vout[:count]) - 100000, CScript([OP_1]))]
        tx.wit.vtxinwit = [CTxInWitness() for _ in range(count)]
        for w in tx.wit.vtxinwit:
            w.scriptWitness.stack = [bytes(WITNESS_SCRIPT)]
        tx.rehash()
        return tx

    def submit(self, txs, legacy_sigops=0):
        tip = self.node.getbestblockhash()
        height = self.node.getblockcount() + 1
        coinbase = create_coinbase(height, extra_output_script=CScript([OP_CHECKSIG] * legacy_sigops) if legacy_sigops else None)
        block = create_block(int(tip, 16), coinbase, self.node.getblock(tip)["time"] + 1, txlist=txs)
        add_witness_commitment(block)
        block.solve()
        return self.node.submitblock(block.serialize().hex())

    def run_test(self):
        self.node = self.nodes[0]
        wallet = MiniWallet(self.node)
        self.generate(wallet, COINBASE_MATURITY + 1)

        self.log.info("Fund P2WSH outputs that cost 2000 sigops each to spend")
        utxo = wallet.get_utxo()
        fund_tx = CTransaction()
        fund_tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        per_output = (int(utxo["value"] * COIN) - 100000) // OVER_NEW
        fund_tx.vout = [CTxOut(per_output, script_to_p2wsh_script(WITNESS_SCRIPT)) for _ in range(OVER_NEW)]
        wallet.sign_tx(fund_tx)
        fund_tx.rehash()
        assert_equal(self.submit([fund_tx]), None)

        spend_over_old = self.spend(fund_tx, OVER_OLD)
        spend_over_new = self.spend(fund_tx, OVER_NEW)
        assert self.node.getblockcount() + 1 < ACTIVATION_HEIGHT

        self.log.info("Before activation: 80000 limit")
        assert_equal(self.node.getblocktemplate({"rules": ["segwit"]})["sigoplimit"], MAX_BLOCK_SIGOPS_WEIGHT)
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_limits"]["active"], False)
        assert_equal(self.submit([spend_over_old]), "bad-blk-sigops")
        assert_equal(self.submit([], legacy_sigops=LEGACY_OVER_OLD), "bad-blk-sigops")

        self.generate(self.node, ACTIVATION_HEIGHT - 2 - self.node.getblockcount())
        self.log.info("The block before activation still has the 80000 limit")
        assert_equal(self.submit([spend_over_old]), "bad-blk-sigops")
        self.generate(self.node, 1)

        self.log.info("From activation: 160000 limit")
        assert_equal(self.node.getblocktemplate({"rules": ["segwit"]})["sigoplimit"], MAX_BLOCK_SIGOPS_WEIGHT_DILITHIUM_LIMITS)
        assert_equal(self.submit([spend_over_new]), "bad-blk-sigops")
        assert_equal(self.submit([spend_over_old]), None)
        assert_equal(self.node.getblockcount(), ACTIVATION_HEIGHT)
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_limits"]["active"], True)

        self.log.info("Legacy sigops are checked against 160000 too")
        assert_equal(self.submit([], legacy_sigops=LEGACY_OVER_NEW), "bad-blk-sigops")
        assert_equal(self.submit([], legacy_sigops=LEGACY_OVER_NEW - 1), None)


if __name__ == '__main__':
    DilithiumLimitsTest().main()
