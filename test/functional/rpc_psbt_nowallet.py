#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise wallet-independent PSBT and raw signature combination end to end."""

from decimal import Decimal

from test_framework.descriptors import descsum_create
from test_framework.psbt import PSBT, PSBT_GLOBAL_UNSIGNED_TX
from test_framework.test_framework import BTQTestFramework
from test_framework.test_node import TestNode
from test_framework.util import assert_equal, assert_raises_rpc_error


class PSBTNoWalletTest(BTQTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.wallet_names = []
        self.extra_args = [["-disablewallet"]]

    def run_test(self):
        node = self.nodes[0]
        keys = [key.key for key in TestNode.PRIV_KEYS[:2]]
        descriptors = [descsum_create(f"wpkh({key})") for key in keys]
        addresses = [node.deriveaddresses(desc)[0] for desc in descriptors]
        first = self.generatetoaddress(node, 101, addresses[0])[0]
        second = self.generatetoaddress(node, 1, addresses[1])[0]
        self.generatetoaddress(node, 100, addresses[0])
        fee = Decimal("0.001")
        psbts = []
        for block, address, descriptor in zip((first, second), addresses, descriptors):
            coinbase = node.getblock(block, 2)["tx"][0]
            output = coinbase["vout"][0]
            psbt = node.createpsbt([{"txid": coinbase["txid"], "vout": 0}],
                                  {address: output["value"] - fee})
            decoded = node.decodepsbt(psbt)
            assert_equal(decoded["tx"]["vin"][0]["txid"], coinbase["txid"])
            assert_equal(decoded["tx"]["vout"][0]["value"], output["value"] - fee)
            updated = node.utxoupdatepsbt(psbt, [descriptor])
            assert_equal(node.decodepsbt(updated)["inputs"][0]["witness_utxo"]["amount"], output["value"])
            combined = node.combinepsbt([psbt, updated])
            assert_equal(node.decodepsbt(combined), node.decodepsbt(updated))
            psbts.append(updated)

        joined = node.joinpsbts(psbts)
        decoded = node.decodepsbt(joined)
        assert_equal(len(decoded["tx"]["vin"]), 2)
        assert_equal(len(decoded["tx"]["vout"]), 2)
        analysis = node.analyzepsbt(joined)
        assert_equal(analysis["fee"], 2 * fee)
        assert_equal(analysis["next"], "signer")

        # Neither key alone can authorize both inputs; merge their contributions.
        partials = [node.descriptorprocesspsbt(joined, [desc], finalize=False) for desc in descriptors]
        for partial in partials:
            assert_equal(partial["complete"], False)
            assert_equal(node.finalizepsbt(partial["psbt"])["complete"], False)
        signed = node.combinepsbt([partial["psbt"] for partial in partials])
        final = node.finalizepsbt(signed)
        assert_equal(final["complete"], True)
        assert_equal(node.testmempoolaccept([final["hex"]])[0]["allowed"], True)

        # Conversion explicitly strips signature data and retains the transaction.
        converted = node.converttopsbt(final["hex"], permitsigdata=True)
        unsigned = node.decodepsbt(converted)
        assert_equal(unsigned["tx"], decoded["tx"])
        assert_equal(unsigned["inputs"], [{}, {}])
        assert_raises_rpc_error(-22, "Inputs must not have scriptSigs and scriptWitnesses", node.converttopsbt, final["hex"])

        # The raw-transaction combiner must also preserve both distinct signatures.
        unsigned_hex = PSBT.from_base64(converted).g.map[PSBT_GLOBAL_UNSIGNED_TX].hex()
        raw_partials = [node.signrawtransactionwithkey(unsigned_hex, [key]) for key in keys]
        for partial in raw_partials:
            assert_equal(partial["complete"], False)
            assert_equal(node.testmempoolaccept([partial["hex"]])[0]["allowed"], False)
        combined_raw = node.combinerawtransaction([partial["hex"] for partial in raw_partials])
        assert_equal(node.decoderawtransaction(combined_raw)["txid"], decoded["tx"]["txid"])
        assert_equal(node.testmempoolaccept([combined_raw])[0]["allowed"], True)


if __name__ == '__main__':
    PSBTNoWalletTest().main()
