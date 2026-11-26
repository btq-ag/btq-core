#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""ConnectBlock sigop boundary for Dilithium opcodes.

GetTransactionSigOpCost charges DILITHIUM_SIGOP_COST (50) per
OP_CHECKSIGDILITHIUM in the revealed witness script, and does not multiply
that witness count by WITNESS_SCALE_FACTOR. MAX_BLOCK_SIGOPS_COST is 80000,
so 1600 opcodes land on the cap and 1601 cross it.

The cap is applied in ConnectBlock before CheckInputScripts, so a witness
that cannot possibly verify still has to be counted. These spends are
submitted with submitblock. The 1600-opcode script is not a valid spend.
It must fail for some other reason. The 1601-opcode script must fail
specifically as bad-blk-sigops.
"""

from test_framework.blocktools import (
    COINBASE_MATURITY,
    add_witness_commitment,
    create_block,
    create_coinbase,
)
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
    OP_2,
    OP_CHECKSIGDILITHIUM,
    OP_TRUE,
)
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

SIGOP_COST = 50
SIGOP_CAP = 80_000
AT_CAP = SIGOP_CAP // SIGOP_COST
OVER_CAP = AT_CAP + 1
FUND_SAT = 1_000_000
SPEND_SAT = 900_000


def fat_leaf(n_ops):
    return CScript([OP_CHECKSIGDILITHIUM] * n_ops)


class Dilithium5SigopBoundaryTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-acceptnonstdtxn=1"]]

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        self.generate(wallet, COINBASE_MATURITY + 5)

        p2mr = CScript([OP_2, b"\x11" * 32])
        p2wsh = CScript([OP_0, b"\x22" * 32])
        funded = {}
        for name, spk in (("p2mr_at", p2mr), ("p2mr_over", p2mr), ("p2wsh_over", p2wsh)):
            sent = wallet.send_to(from_node=node, scriptPubKey=spk, amount=FUND_SAT)
            self.generate(node, 1)
            funded[name] = COutPoint(int(sent["txid"], 16), sent["sent_vout"])

        at = self.spend(funded["p2mr_at"], AT_CAP, p2mr=True)
        over = self.spend(funded["p2mr_over"], OVER_CAP, p2mr=True)
        wsh = self.spend(funded["p2wsh_over"], OVER_CAP, p2mr=False)

        at_reason = self.submit(at)
        over_reason = self.submit(over)
        wsh_reason = self.submit(wsh)

        self.log.info("1600 opcodes on P2MR: %s", at_reason)
        self.log.info("1601 opcodes on P2MR: %s", over_reason)
        self.log.info("1601 opcodes on P2WSH: %s", wsh_reason)

        assert at_reason != "bad-blk-sigops", at_reason
        assert_equal(over_reason, "bad-blk-sigops")
        assert_equal(wsh_reason, "bad-blk-sigops")
        # The at-cap block must not have moved the tip. The script is not a real spend.
        assert node.gettxout(f"{funded['p2mr_at'].hash:064x}", funded["p2mr_at"].n) is not None

    def spend(self, outpoint, n_ops, p2mr):
        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [CTxIn(outpoint, b"", SEQUENCE_FINAL)]
        tx.vout = [CTxOut(SPEND_SAT, CScript([OP_TRUE]))]
        leaf = fat_leaf(n_ops)
        witness = CTxInWitness()
        if p2mr:
            # [script] [control]. CountWitnessSigOps reads the script item.
            witness.scriptWitness.stack = [bytes(leaf), bytes([0xC0])]
        else:
            witness.scriptWitness.stack = [bytes(leaf)]
        tx.wit.vtxinwit = [witness]
        tx.rehash()
        return tx

    def submit(self, tx):
        tip = self.nodes[0].getbestblockhash()
        height = self.nodes[0].getblockcount() + 1
        block = create_block(
            int(tip, 16),
            create_coinbase(height),
            self.nodes[0].getblock(tip)["time"] + 1,
            txlist=[tx],
        )
        add_witness_commitment(block)
        block.solve()
        return self.nodes[0].submitblock(block.serialize().hex())


if __name__ == "__main__":
    Dilithium5SigopBoundaryTest().main()
