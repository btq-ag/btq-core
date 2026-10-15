#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Initialize RPC coverage from the complete current binary interface."""

from test_framework.coverage import write_all_rpc_commands
from test_framework.test_framework import BTQTestFramework


class CreateRPCCoverage(BTQTestFramework):
    def add_options(self, parser):
        # Enables wallet RPC registration when a wallet backend is compiled.
        # With a wallet-disabled build, the reference contains only core RPCs.
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = []

    def run_test(self):
        assert self.options.coveragedir is not None
        write_all_rpc_commands(self.options.coveragedir, self.nodes[0].rpc, authoritative=True)


if __name__ == '__main__':
    CreateRPCCoverage().main()
