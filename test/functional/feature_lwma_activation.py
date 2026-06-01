#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test LWMA difficulty adjustment activation on regtest (BTQ-AUDIT-015)."""

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

LWMA_HEIGHT = 150


class LwmaActivationTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [[
            f'-testactivationheight=lwma@{LWMA_HEIGHT}',
            '-whitelist=noban@127.0.0.1',
        ]]
        self.setup_clean_chain = True

    def test_lwma_info(self, *, is_active):
        assert_equal(
            self.nodes[0].getdeploymentinfo()['deployments']['lwma'],
            {
                'active': is_active,
                'height': LWMA_HEIGHT,
                'type': 'buried',
            },
        )

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        self.log.info('LWMA inactive at genesis')
        self.test_lwma_info(is_active=False)

        self.log.info('Mine to two blocks before LWMA activation')
        self.generate(wallet, LWMA_HEIGHT - 2)
        assert_equal(node.getblockcount(), LWMA_HEIGHT - 2)
        self.test_lwma_info(is_active=False)

        self.log.info('Mine to one block before activation (RPC reports active for next block)')
        self.generate(wallet, 1)
        assert_equal(node.getblockcount(), LWMA_HEIGHT - 1)
        self.test_lwma_info(is_active=True)
        pre_bits = node.getblockheader(node.getbestblockhash())['bits']

        self.log.info('Mine past LWMA activation with accelerated block times')
        base_time = node.getblockheader(node.getbestblockhash())['time']
        for i in range(5):
            node.setmocktime(base_time + i)
            self.generate(wallet, 1)
        post_bits = node.getblockheader(node.getbestblockhash())['bits']
        assert_equal(node.getblockcount(), LWMA_HEIGHT - 1 + 5)
        self.test_lwma_info(is_active=True)

        self.log.info('LWMA path should adjust difficulty after activation')
        # Lower compact nBits value means higher difficulty after fast blocks.
        assert post_bits != pre_bits
        assert post_bits < pre_bits, f'expected harder difficulty, got {post_bits:#08x} from {pre_bits:#08x}'


if __name__ == '__main__':
    LwmaActivationTest().main()
