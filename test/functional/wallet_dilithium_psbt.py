#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for signing P2MR (Dilithium) inputs through the PSBT workflow.

Issue #79: walletprocesspsbt silently skipped P2MR inputs and returned
complete=false with no diagnostic, while signrawtransactionwithwallet signed the
same outpoint fine. P2MR scriptPubKeys are tracked in wallet metadata rather than
by a ScriptPubKeyMan, and CWallet::FillPSBT only consulted ScriptPubKeyMans.
"""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class WalletDilithiumPSBTTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def dilithium_address(self, wallet):
        created = wallet.getnewdilithiumaddress()
        assert isinstance(created, dict), created
        return created["address"]

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        node.createwallet(wallet_name="psbt_w", descriptors=True)
        wallet = node.get_wallet_rpc("psbt_w")

        p2mr_address = self.dilithium_address(wallet)
        ordinary_address = wallet.getnewaddress()
        funding.sendtoaddress(p2mr_address, Decimal("10"))
        funding.sendtoaddress(ordinary_address, Decimal("10"))
        self.generate(node, 1)

        utxos = wallet.listunspent()
        p2mr_utxo = next(u for u in utxos if u["address"] == p2mr_address)
        ordinary_utxo = next(u for u in utxos if u["address"] == ordinary_address)
        # Confirm the P2MR outpoint really is witness v2, so a regression that
        # changed the receive type could not make this test pass vacuously.
        assert p2mr_utxo["scriptPubKey"].startswith("5220"), p2mr_utxo["scriptPubKey"]

        destination = wallet.getnewaddress()

        self.log.info("walletprocesspsbt signs a P2MR-only PSBT and the result relays")
        psbt = wallet.walletcreatefundedpsbt(
            [{"txid": p2mr_utxo["txid"], "vout": p2mr_utxo["vout"]}],
            [{destination: Decimal("4")}],
        )["psbt"]

        # Baseline behaviour was complete=false with an empty errors list.
        processed = wallet.walletprocesspsbt(psbt)
        assert_equal(processed["complete"], True)

        final = wallet.finalizepsbt(processed["psbt"])
        assert_equal(final["complete"], True)
        accept = node.testmempoolaccept([final["hex"]])[0]
        assert_equal(accept["allowed"], True)

        # Mining it proves the Dilithium witness actually verifies, not merely
        # that the PSBT reported completion.
        txid = node.sendrawtransaction(final["hex"])
        self.generate(node, 1)
        confirmed = wallet.gettransaction(txid, True, True)
        assert_equal(confirmed["confirmations"], 1)
        spent = {(v["txid"], v["vout"]) for v in confirmed["decoded"]["vin"]}
        assert (p2mr_utxo["txid"], p2mr_utxo["vout"]) in spent

        self.log.info("a PSBT mixing a P2MR input with an ordinary input also completes")
        p2mr_address2 = self.dilithium_address(wallet)
        funding.sendtoaddress(p2mr_address2, Decimal("5"))
        self.generate(node, 1)
        p2mr_utxo2 = next(u for u in wallet.listunspent() if u["address"] == p2mr_address2)

        mixed = wallet.walletcreatefundedpsbt(
            [
                {"txid": p2mr_utxo2["txid"], "vout": p2mr_utxo2["vout"]},
                {"txid": ordinary_utxo["txid"], "vout": ordinary_utxo["vout"]},
            ],
            [{destination: Decimal("6")}],
        )["psbt"]
        processed_mixed = wallet.walletprocesspsbt(mixed)
        assert_equal(processed_mixed["complete"], True)
        final_mixed = wallet.finalizepsbt(processed_mixed["psbt"])
        assert_equal(node.testmempoolaccept([final_mixed["hex"]])[0]["allowed"], True)

        self.log.info("sign=false leaves the P2MR input unsigned without erroring")
        p2mr_address3 = self.dilithium_address(wallet)
        funding.sendtoaddress(p2mr_address3, Decimal("5"))
        self.generate(node, 1)
        p2mr_utxo3 = next(u for u in wallet.listunspent() if u["address"] == p2mr_address3)
        unsigned_psbt = wallet.walletcreatefundedpsbt(
            [{"txid": p2mr_utxo3["txid"], "vout": p2mr_utxo3["vout"]}],
            [{destination: Decimal("1")}],
        )["psbt"]
        assert_equal(wallet.walletprocesspsbt(unsigned_psbt, False)["complete"], False)
        # ...and signing the same PSBT afterwards still works.
        assert_equal(wallet.walletprocesspsbt(unsigned_psbt, True)["complete"], True)

        self.log.info("ordinary-only PSBTs are unaffected")
        plain = wallet.walletcreatefundedpsbt([], [{destination: Decimal("0.5")}])["psbt"]
        assert_equal(wallet.walletprocesspsbt(plain)["complete"], True)


if __name__ == "__main__":
    WalletDilithiumPSBTTest().main()
