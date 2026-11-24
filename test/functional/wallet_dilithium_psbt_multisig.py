#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the BTQ-PSBT Dilithium extension.

Before this extension a PSBT could not carry Dilithium material at all: the P2MR
leaf script, its control block and any Dilithium signature were dropped on
serialization, so a partially signed transaction could not be handed to another
wallet. The single-wallet cases in wallet_dilithium_psbt.py passed only because
signing and finalizing happened inside one process.

Here each co-signer is a separate wallet that only ever sees base64 PSBTs.
"""

import base64
from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error


class WalletDilithiumPSBTMultisigTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def make_signer(self, name):
        """A wallet holding exactly one Dilithium key, plus that key's pubkey."""
        self.nodes[0].createwallet(wallet_name=name, descriptors=True)
        wallet = self.nodes[0].get_wallet_rpc(name)
        created = wallet.getnewdilithiumaddress()
        keys = wallet.getdilithiumpubkey(created["p2mr_id"])["pubkeys"]
        assert_equal(len(keys), 1)
        assert_equal(keys[0]["ismine"], True)
        return wallet, keys[0]["pubkey"]

    def input_status(self, wallet, psbt):
        decoded = wallet.decodepsbt(psbt)
        return decoded["inputs"][0]["p2mr_dilithium"]

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        alice, alice_pk = self.make_signer("alice")
        bob, bob_pk = self.make_signer("bob")
        carol, carol_pk = self.make_signer("carol")
        pubkeys = [alice_pk, bob_pk, carol_pk]

        self.log.info("all three co-signers derive the same 2-of-3 address")
        registered = [w.createdilithiummultisig(2, pubkeys, "shared") for w in (alice, bob, carol)]
        address = registered[0]["address"]
        for reg in registered:
            assert_equal(reg["address"], address)
            assert_equal(reg["scriptPubKey"], registered[0]["scriptPubKey"])
            assert_equal(reg["leaf_script"], registered[0]["leaf_script"])
            # Each co-signer holds exactly one of the three keys.
            assert_equal(reg["signers_available"], 1)
        assert registered[0]["scriptPubKey"].startswith("5220"), registered[0]["scriptPubKey"]

        self.log.info("fund the multisig")
        funding.sendtoaddress(address, Decimal("10"))
        self.generate(node, 1)
        utxo = next(u for u in alice.listunspent() if u["address"] == address)

        destination = funding.getnewaddress()
        psbt = alice.walletcreatefundedpsbt(
            [{"txid": utxo["txid"], "vout": utxo["vout"]}],
            [{destination: Decimal("4")}],
        )["psbt"]

        self.log.info("an unsigned PSBT already advertises the leaf and its policy")
        decoded = alice.decodepsbt(psbt)
        leaf = decoded["inputs"][0]["p2mr_scripts"][0]
        assert_equal(leaf["script"], registered[0]["leaf_script"])
        assert_equal(leaf["leaf_ver"], 192)
        assert_equal(leaf["dilithium_policy"], "dilithium_threshold")
        assert_equal(leaf["dilithium_required"], 2)
        assert_equal(leaf["dilithium_total"], 3)
        assert_equal(decoded["inputs"][0]["p2mr_merkle_root"], registered[0]["merkle_root"])
        assert_equal(self.input_status(alice, psbt)["status"], "unsigned")

        self.log.info("alice signs; the PSBT survives serialization with one signature")
        half = alice.walletprocesspsbt(psbt)
        assert_equal(half["complete"], False)
        status = self.input_status(bob, half["psbt"])
        assert_equal(status["status"], "partially_signed")
        assert_equal(status["signatures"], 1)
        assert_equal(status["required"], 2)
        sigs = bob.decodepsbt(half["psbt"])["inputs"][0]["p2mr_dilithium_script_path_sigs"]
        assert_equal([s["pubkey"] for s in sigs], [alice_pk])
        # 2420-byte Dilithium signature plus the sighash byte.
        assert_equal(len(sigs[0]["sig"]), 2 * 2421)

        self.log.info("carol signs the same PSBT independently of bob")
        carol_half = carol.walletprocesspsbt(psbt)
        assert_equal(carol_half["complete"], False)
        assert_equal(self.input_status(carol, carol_half["psbt"])["signatures"], 1)

        self.log.info("combinepsbt merges two independent partial signatures")
        combined = node.combinepsbt([half["psbt"], carol_half["psbt"]])
        status = self.input_status(alice, combined)
        assert_equal(status["signatures"], 2)
        assert_equal(status["status"], "finalizable")

        self.log.info("finalizepsbt turns the collected signatures into a witness")
        final = node.finalizepsbt(combined)
        assert_equal(final["complete"], True)
        assert_equal(node.testmempoolaccept([final["hex"]])[0]["allowed"], True)

        txid = node.sendrawtransaction(final["hex"])
        self.generate(node, 1)
        assert_equal(alice.gettransaction(txid)["confirmations"], 1)

        self.log.info("sequential signing (alice then bob) also completes")
        funding.sendtoaddress(address, Decimal("6"))
        self.generate(node, 1)
        utxo2 = next(u for u in alice.listunspent() if u["address"] == address and u["amount"] == Decimal("6"))
        psbt2 = alice.walletcreatefundedpsbt(
            [{"txid": utxo2["txid"], "vout": utxo2["vout"]}],
            [{destination: Decimal("2")}],
        )["psbt"]
        step1 = alice.walletprocesspsbt(psbt2)
        assert_equal(step1["complete"], False)
        step2 = bob.walletprocesspsbt(step1["psbt"])
        assert_equal(step2["complete"], True)
        assert_equal(self.input_status(bob, step2["psbt"])["status"], "finalized")
        final2 = node.finalizepsbt(step2["psbt"])
        txid2 = node.sendrawtransaction(final2["hex"])
        self.generate(node, 1)
        assert_equal(alice.gettransaction(txid2)["confirmations"], 1)

        self.log.info("re-signing with a key that already signed does not add a signature")
        funding.sendtoaddress(address, Decimal("3"))
        self.generate(node, 1)
        utxo3 = next(u for u in alice.listunspent() if u["address"] == address and u["amount"] == Decimal("3"))
        psbt3 = alice.walletcreatefundedpsbt(
            [{"txid": utxo3["txid"], "vout": utxo3["vout"]}],
            [{destination: Decimal("1")}],
        )["psbt"]
        once = alice.walletprocesspsbt(psbt3)["psbt"]
        twice = alice.walletprocesspsbt(once)["psbt"]
        assert_equal(self.input_status(alice, twice)["signatures"], 1)
        # One signature short of the threshold must not finalize.
        under_threshold = node.finalizepsbt(twice, False)
        assert_equal(under_threshold["complete"], False)
        assert "hex" not in under_threshold

        self.log.info("a forged signature is rejected when the PSBT is decoded")
        # `once` still carries alice's signature; a completed PSBT would have
        # replaced it with a final witness.
        assert_greater_than(len(once), 0)
        tampered = self.corrupt_first_signature(node, once)
        assert_raises_rpc_error(-22, "signature", node.decodepsbt, tampered)
        assert_raises_rpc_error(-22, "signature", node.finalizepsbt, tampered)

    def corrupt_first_signature(self, node, psbt_b64):
        """Flip a byte inside the first Dilithium signature value."""
        raw = bytearray(base64.b64decode(psbt_b64))
        sig = bytes.fromhex(node.decodepsbt(psbt_b64)["inputs"][0]["p2mr_dilithium_script_path_sigs"][0]["sig"])
        offset = raw.find(sig)
        assert offset != -1, "signature not found in PSBT bytes"
        raw[offset + 100] ^= 0xFF
        return base64.b64encode(bytes(raw)).decode()


if __name__ == "__main__":
    WalletDilithiumPSBTMultisigTest().main()
