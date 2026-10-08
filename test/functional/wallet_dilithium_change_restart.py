#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Recovered internal Dilithium keys must not be reissued after wallet reload."""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class DilithiumChangeRestartTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)
        parser.add_argument("--encrypted", action="store_true")

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("sequence", descriptors=True)
        wallet = node.get_wallet_rpc("sequence")
        issued = set()
        if self.options.encrypted:
            issued.add(wallet.getnewdilithiumaddress()["address"])
            wallet.encryptwallet("test-only-change-sequence")
            wallet.walletpassphrase("test-only-change-sequence", 600)

        external = wallet.recoverdilithiumkeys(0, 14)
        internal = wallet.recoverdilithiumkeys(0, 14, True)
        external_addresses = {entry["address"] for entry in external}
        internal_addresses = {entry["address"] for entry in internal}
        assert_equal(len(external_addresses), 15)
        assert_equal(len(internal_addresses), 15)
        assert external_addresses.isdisjoint(internal_addresses)
        issued |= external_addresses | internal_addresses

        # No internal address has been issued through normal generation yet:
        # its counter must skip all fifteen already-materialized keys.
        if self.options.encrypted:
            node.unloadwallet("sequence")
        else:
            self.restart_node(0)
        node.loadwallet("sequence")
        wallet = node.get_wallet_rpc("sequence")
        if self.options.encrypted:
            wallet.walletpassphrase("test-only-change-sequence", 600)

        for index in range(15, 18):
            address = wallet.getrawchangeaddress("p2mr")
            assert address not in issued, "reissued a materialized Dilithium change address"
            issued.add(address)
            assert_equal(wallet.getaddressinfo(address)["ismine"], True)
            recovered = wallet.recoverdilithiumkeys(index, index, True)[0]
            assert_equal(recovered["address"], address)
            assert_equal(recovered["recovered"], False)

        # Both original sequences remain materialized and disjoint.
        assert not any(entry["recovered"] for entry in wallet.recoverdilithiumkeys(0, 14))
        assert not any(entry["recovered"] for entry in wallet.recoverdilithiumkeys(0, 14, True))


if __name__ == '__main__':
    DilithiumChangeRestartTest().main()
