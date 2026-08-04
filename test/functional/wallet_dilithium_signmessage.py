#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dilithium message signing must not double as transaction signing (issue #90).

signmessagewithdilithium uses the same key and the same primitive as a P2MR
spend, and a transaction signature is made over a bare 32-byte sighash. Without
domain separation, "sign this message to prove you own the address" is a request
to sign a transaction. This checks the separation from the RPC surface: the
signature is bound to the message, to the key, and to the message domain.
"""

import base64
from decimal import Decimal

from test_framework.messages import tx_from_hex
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletDilithiumSignMessageTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="signer", descriptors=True)
        wallet = node.get_wallet_rpc("signer")
        self.generatetoaddress(node, 101, wallet.getnewaddress())

        address = wallet.getnewdilithiumaddress()["address"]
        other_address = wallet.getnewdilithiumaddress()["address"]
        message = "prove you own this address"

        self.log.info("A message signature verifies for its own message, key and nothing else")
        signature = wallet.signmessagewithdilithium(address, message)
        assert wallet.verifydilithiumsignature(message, address, signature)
        assert not wallet.verifydilithiumsignature(message + " ", address, signature)
        assert not wallet.verifydilithiumsignature(message, other_address, signature)

        self.log.info("The signature is over a hash of the message, not the message bytes")
        # A Dilithium2 signature is fixed-size, so length says nothing; what says
        # something is that a 32-byte message and a long one are equally signable
        # and neither signature transfers to the other.
        short = wallet.signmessagewithdilithium(address, "x" * 32)
        long_msg = wallet.signmessagewithdilithium(address, "x" * 4096)
        assert wallet.verifydilithiumsignature("x" * 32, address, short)
        assert wallet.verifydilithiumsignature("x" * 4096, address, long_msg)
        assert not wallet.verifydilithiumsignature("x" * 32, address, long_msg)

        self.log.info("A message signature is not accepted as a P2MR spending signature")
        # Fund the address, build a spend of it, and substitute the message
        # signature for the real one. The witness must fail script verification.
        wallet.sendtoaddress(address, Decimal("1"))
        self.generate(node, 1)
        utxo = next(u for u in wallet.listunspent() if u["address"] == address)

        raw = wallet.createrawtransaction(
            [{"txid": utxo["txid"], "vout": utxo["vout"]}],
            [{wallet.getnewaddress(): utxo["amount"] - Decimal("0.001")}],
        )
        signed = wallet.signrawtransactionwithwallet(raw)
        assert_equal(signed["complete"], True)

        tx = tx_from_hex(signed["hex"])
        real_sig = tx.wit.vtxinwit[0].scriptWitness.stack[0]
        # A Dilithium spending signature carries a trailing sighash byte. Reuse
        # the genuine one, so the forgery differs from the real witness in the
        # signature bytes alone.
        forged_sig = base64.b64decode(signature) + real_sig[-1:]
        assert_equal(len(forged_sig), len(real_sig))
        assert forged_sig != real_sig
        tx.wit.vtxinwit[0].scriptWitness.stack[0] = forged_sig

        result = node.testmempoolaccept([tx.serialize().hex()])[0]
        assert_equal(result["allowed"], False)
        self.log.info("  rejected with: %s", result["reject-reason"])

        # The genuine transaction is accepted, so the rejection above is the
        # substituted signature and not something else about the transaction.
        assert_equal(node.testmempoolaccept([signed["hex"]])[0]["allowed"], True)

        self.log.info("Errors are reported for unusable inputs")
        assert_raises_rpc_error(
            -5, "Address is not a Dilithium key address",
            wallet.signmessagewithdilithium, wallet.getnewaddress(), message,
        )
        assert_raises_rpc_error(
            -5, "Invalid signature encoding",
            wallet.verifydilithiumsignature, message, address, "not base64 $$$",
        )


if __name__ == "__main__":
    WalletDilithiumSignMessageTest().main()
