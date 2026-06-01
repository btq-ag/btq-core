#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Signet WIF prefix must not collide with P2SH (BTQ-AUDIT-026)."""

from test_framework.test_framework import BTQTestFramework


class SignetWifPrefixTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.chain = 'signet'
        self.num_nodes = 1
        self.extra_args = [['-signetchallenge=51']]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info('Mine signet blocks and fund wallet')
        self.generate(node, 101)

        addr = node.getnewaddress()
        wif = node.dumpprivkey(addr)

        self.log.info('WIF must not use the P2SH version byte (196 / leading 2)')
        assert not wif.startswith('2'), f'WIF collides with P2SH prefix: {wif}'
        assert wif[0] in 'c9', f'Unexpected signet WIF prefix: {wif}'


if __name__ == '__main__':
    SignetWifPrefixTest().main()
