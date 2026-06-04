#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test LWMA activation boundary behavior."""

from test_framework.blocktools import NORMAL_GBT_REQUEST_PARAMS
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class LWMAActivationTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-testactivationheight=lwma@2"]]

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        self.log.info("getblocktemplate does not advertise LWMA before the next block activates it")
        assert_equal(node.getblockcount(), 0)
        assert "!lwma" not in node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)["rules"]

        self.log.info("getblocktemplate advertises LWMA for the first active block")
        self.generate(wallet, 1)
        assert_equal(node.getblockcount(), 1)
        assert "!lwma" in node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)["rules"]


if __name__ == "__main__":
    LWMAActivationTest().main()
