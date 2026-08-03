#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mine across a DEPLOYMENT_DILITHIUM_P2MR activation.

Mainnet ships nDilithiumP2MRHeight = 1, so P2MR-only is active from the first
block and no mainnet node ever crosses the boundary. Testnet leaves it
unscheduled, so no live chain has ever crossed it either. Issue #102 is that
the rule mainnet launches with has therefore never been observed switching on.

A chain reset is what actually closes that, and the scan in issue #111 shows why
it cannot be closed by activating on the running testnet: about three quarters
of that chain's value sits on legacy Dilithium outputs that the activation would
freeze. What can be done here is to exercise the transition itself, which no
existing test does -- coverage today either has P2MR-only on from height 1
(regtest, feature_p2mr.py) or off forever (tool_scan_legacy_dilithium.py), and
the unit tests in dilithium_p2mr_only_tests.cpp toggle the flag directly without
a chain.

What is checked here:

  - a legacy witness-v0 Dilithium spend is accepted in a block below the height
  - the same spend is rejected in a block at the height
  - activation is not retroactive: the pre-activation block survives a reindex
  - a reorg cannot launder a legacy spend across the boundary
  - P2MR spends work on both sides, so the restriction is narrow

Legacy spends go in via submitblock rather than the mempool. P2MR-only sits in
STANDARD_SCRIPT_VERIFY_FLAGS unconditionally, so the mempool refuses these
spends on every chain regardless of activation; that is deliberate (see
policy.h) and it means only block validation can show the consensus rule
changing.
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
    OP_CHECKSIGDILITHIUM,
    OP_DUP,
    OP_EQUALVERIFY,
    OP_HASH160,
    SIGHASH_ALL,
    SegwitV0SignatureHash,
    TaprootSignatureHash,
    hash160,
    p2mr_construct,
)
from test_framework.dilithium import DilithiumKey, dilithium_available
from test_framework.test_framework import BTQTestFramework, SkipTest
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

ACTIVATION_HEIGHT = 250
FUND_SATOSHIS = 200_000
SPEND_SATOSHIS = 180_000


class DilithiumP2MRActivationTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            f"-testactivationheight=dilithium_p2mr@{ACTIVATION_HEIGHT}",
            "-acceptnonstdtxn=1",
        ]]

    def skip_test_if_missing_module(self):
        if not dilithium_available():
            raise SkipTest("could not build the Dilithium reference library (needs a C compiler)")

    # ------------------------------------------------------------------ helpers
    def fund(self, script_pubkey):
        funded = self.wallet.send_to(from_node=self.node, scriptPubKey=script_pubkey,
                                     amount=FUND_SATOSHIS)
        self.generate(self.node, 1)
        return COutPoint(int(funded["txid"], 16), funded["sent_vout"])

    def legacy_witness_v0_spend(self, outpoint):
        """A witness-v0 keyhash spend satisfied by a Dilithium key.

        This is the shape the activation removes. Pre-activation the
        interpreter routes a 1312-byte witness key to OP_CHECKSIGDILITHIUM;
        afterwards the same witness falls through to the ECDSA path, where a
        1312-byte pubkey is not valid.
        """
        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [CTxIn(outpoint, b"", SEQUENCE_FINAL)]
        tx.vout = [CTxOut(SPEND_SATOSHIS, self.wallet.get_scriptPubKey())]

        # The scriptCode the interpreter builds for a v0 keyhash program held
        # by a Dilithium key, mirroring VerifyWitnessProgram.
        script_code = CScript([OP_DUP, OP_HASH160, self.program,
                               OP_EQUALVERIFY, OP_CHECKSIGDILITHIUM])
        sighash = SegwitV0SignatureHash(script_code, tx, 0, SIGHASH_ALL, FUND_SATOSHIS)

        witness = CTxInWitness()
        witness.scriptWitness.stack = [
            self.key.sign(sighash) + bytes([SIGHASH_ALL]),
            self.key.pubkey,
        ]
        tx.wit.vtxinwit = [witness]
        tx.rehash()
        return tx

    def p2mr_spend(self, outpoint):
        """A Dilithium spend through P2MR, which activation is meant to keep."""
        tx = CTransaction()
        tx.nVersion = 2
        tx.vin = [CTxIn(outpoint, b"", SEQUENCE_FINAL)]
        tx.vout = [CTxOut(SPEND_SATOSHIS, self.wallet.get_scriptPubKey())]

        leaf = self.p2mr.leaves["only"]
        sighash = TaprootSignatureHash(
            tx, [CTxOut(FUND_SATOSHIS, self.p2mr.scriptPubKey)], SIGHASH_ALL,
            input_index=0, scriptpath=True, script=bytes(leaf.script),
            leaf_ver=leaf.version)

        witness = CTxInWitness()
        witness.scriptWitness.stack = [
            self.key.sign(sighash) + bytes([SIGHASH_ALL]),
            bytes(leaf.script),
            bytes([leaf.version | 1]) + leaf.merklebranch,
        ]
        tx.wit.vtxinwit = [witness]
        tx.rehash()
        return tx

    def submit_block_with(self, tx):
        """Mine a block containing tx and return (accepted, height_attempted)."""
        tip_hash = self.node.getbestblockhash()
        height = self.node.getblockcount() + 1
        block = create_block(int(tip_hash, 16), create_coinbase(height),
                             self.node.getblock(tip_hash)["time"] + 1, txlist=[tx])
        add_witness_commitment(block)
        block.solve()

        self.node.submitblock(block.serialize().hex())
        accepted = self.node.getbestblockhash() != tip_hash
        return accepted, height

    def mine_to(self, height):
        missing = height - self.node.getblockcount()
        if missing > 0:
            self.generate(self.wallet, missing)
        assert_equal(self.node.getblockcount(), height)

    def assert_unspent(self, outpoint, spent):
        txout = self.node.gettxout(f"{outpoint.hash:064x}", outpoint.n)
        if spent:
            assert txout is None, "output should have been spent"
        else:
            assert txout is not None, "output should still be unspent"

    # --------------------------------------------------------------------- test
    def run_test(self):
        self.node = self.nodes[0]
        self.wallet = MiniWallet(self.node)
        self.generate(self.wallet, COINBASE_MATURITY + 10)

        self.key = DilithiumKey(seed=b"\x07" * 32)
        self.program = hash160(self.key.pubkey)
        legacy_spk = CScript([OP_0, self.program])
        self.p2mr = p2mr_construct([("only", CScript([self.key.pubkey, OP_CHECKSIGDILITHIUM]))])

        assert self.node.getblockcount() < ACTIVATION_HEIGHT - 5, \
            "setup already crossed the activation height"

        self.log.info("Deployment reports inactive before the height")
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_p2mr"]["active"], False)

        self.log.info("A legacy witness-v0 Dilithium spend is valid below the height")
        legacy_a = self.fund(legacy_spk)
        legacy_b = self.fund(legacy_spk)
        p2mr_early = self.fund(self.p2mr.scriptPubKey)

        accepted, height = self.submit_block_with(self.legacy_witness_v0_spend(legacy_a))
        assert accepted, f"legacy Dilithium spend rejected at height {height}, below activation"
        assert height < ACTIVATION_HEIGHT
        self.assert_unspent(legacy_a, spent=True)
        legacy_spend_block = self.node.getbestblockhash()

        self.log.info("P2MR works below the height too")
        accepted, _ = self.submit_block_with(self.p2mr_spend(p2mr_early))
        assert accepted, "P2MR spend rejected before activation"

        self.log.info("The mempool refuses the legacy spend on both sides, by policy")
        # P2MR-only is unconditionally standard, so this is not what changes at
        # the boundary. Asserting it keeps the block-level result below from
        # being misread as a policy effect.
        rejected = self.node.testmempoolaccept([self.legacy_witness_v0_spend(legacy_b).serialize().hex()])
        assert_equal(rejected[0]["allowed"], False)

        self.log.info("Mining to one block below the activation height")
        # getdeploymentinfo answers for the block that would come next, so it
        # flips one height early relative to the height that enforces the rule.
        # Worth pinning down: it is the field an operator would watch when
        # scheduling, and reading it as "already enforced" is off by one.
        self.mine_to(ACTIVATION_HEIGHT - 2)
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_p2mr"]["active"], False)
        self.mine_to(ACTIVATION_HEIGHT - 1)
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_p2mr"]["active"], True)

        self.log.info("The same spend is rejected in the activation block")
        accepted, height = self.submit_block_with(self.legacy_witness_v0_spend(legacy_b))
        assert_equal(height, ACTIVATION_HEIGHT)
        assert not accepted, "legacy Dilithium spend accepted at the activation height"
        self.assert_unspent(legacy_b, spent=False)

        self.log.info("The coin is frozen, not merely unrelayable")
        # Same input, a different output amount, so it is a different
        # transaction rather than a duplicate being rejected.
        variant = self.legacy_witness_v0_spend(legacy_b)
        variant.vout[0].nValue = SPEND_SATOSHIS - 1_000
        variant.rehash()
        accepted, _ = self.submit_block_with(variant)
        assert not accepted, "a reworded legacy spend still should not confirm"

        self.log.info("An ordinary block at the same height is fine")
        # Proves the rejection is about the spend and not about the height.
        self.generate(self.wallet, 1)
        assert_equal(self.node.getblockcount(), ACTIVATION_HEIGHT)
        assert_equal(self.node.getdeploymentinfo()["deployments"]["dilithium_p2mr"]["active"], True)

        self.log.info("P2MR still works above the height")
        p2mr_late = self.fund(self.p2mr.scriptPubKey)
        accepted, _ = self.submit_block_with(self.p2mr_spend(p2mr_late))
        assert accepted, "P2MR spend rejected after activation"

        self.log.info("Activation is not retroactive: the old block survives a reindex")
        tip_before = self.node.getbestblockhash()
        height_before = self.node.getblockcount()
        self.restart_node(0, self.extra_args[0] + ["-reindex"])
        assert_equal(self.node.getbestblockhash(), tip_before)
        assert_equal(self.node.getblockcount(), height_before)
        assert_equal(self.node.getblock(legacy_spend_block)["confirmations"] > 0, True)
        self.assert_unspent(legacy_a, spent=True)

        self.log.info("A reorg cannot carry a legacy spend across the boundary")
        # Rewinding to before activation and rebuilding past it must not leave
        # the frozen coin spent, however the blocks are re-ordered.
        self.node.invalidateblock(self.node.getblockhash(ACTIVATION_HEIGHT - 1))
        assert_equal(self.node.getblockcount(), ACTIVATION_HEIGHT - 2)
        self.assert_unspent(legacy_b, spent=False)
        self.generate(self.wallet, 5)
        assert self.node.getblockcount() > ACTIVATION_HEIGHT
        self.assert_unspent(legacy_b, spent=False)
        accepted, _ = self.submit_block_with(self.legacy_witness_v0_spend(legacy_b))
        assert not accepted, "reorg past the boundary let a frozen coin be spent"

        self.log.info("The pre-activation spend is still spent after all of that")
        self.assert_unspent(legacy_a, spent=True)


if __name__ == "__main__":
    DilithiumP2MRActivationTest().main()
