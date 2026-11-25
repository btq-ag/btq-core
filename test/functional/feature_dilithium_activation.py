#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Dilithium opcode activation height on regtest (BTQ-AUDIT-017).

Dilithium opcodes are consensus-valid only inside P2MR tapscript leaves.
Receives use getnewdilithiumaddress (P2MR), and spends use signp2mrtransaction.
"""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

DILITHIUM_HEIGHT = 150


class DilithiumActivationTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [[
            f'-testactivationheight=dilithium@{DILITHIUM_HEIGHT}',
            '-whitelist=noban@127.0.0.1',
        ]]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def test_dilithium_info(self, *, is_active):
        assert_equal(
            self.nodes[0].getdeploymentinfo()['deployments']['dilithium'],
            {
                'active': is_active,
                'height': DILITHIUM_HEIGHT,
                'type': 'buried',
            },
        )

    def dilithium_receive(self, node, label=""):
        created = node.getnewdilithiumaddress(label)
        assert isinstance(created, dict), created
        assert created["address"]
        assert created["p2mr_id"]
        return created

    def sign_dilithium_p2mr_spend(self, node, *, p2mr_id, dest, amount):
        spend = node.createp2mrspend(p2mr_id, dest, amount)
        signed = node.signp2mrtransaction(spend["hex"], p2mr_id)
        assert signed["complete"], signed
        return signed["hex"]

    def run_test(self):
        node = self.nodes[0]

        self.log.info('Dilithium inactive at genesis')
        self.test_dilithium_info(is_active=False)

        self.log.info('Mine to two blocks before Dilithium activation')
        self.generate(node, DILITHIUM_HEIGHT - 2)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT - 2)
        self.test_dilithium_info(is_active=False)

        created = self.dilithium_receive(node)
        dil_addr = created["address"]
        p2mr_id = created["p2mr_id"]

        self.log.info('Creating Dilithium P2MR outputs before activation is allowed')
        txid = node.sendtoaddress(dil_addr, Decimal('1.0'))
        assert txid
        self.generate(node, 1)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT - 1)
        self.test_dilithium_info(is_active=True)

        dil_utxos = node.listunspent(minconf=1, addresses=[dil_addr])
        assert_equal(len(dil_utxos), 1)
        dest = node.getnewaddress()

        self.log.info('Legacy signtransactionwithdilithium is disabled')
        assert_raises_rpc_error(
            -32,
            "signtransactionwithdilithium is disabled",
            node.signtransactionwithdilithium,
            "00",
        )

        self.log.info('Spending Dilithium P2MR before activation must be rejected')
        spend_hex = self.sign_dilithium_p2mr_spend(
            node,
            p2mr_id=p2mr_id,
            dest=dest,
            amount=Decimal('0.49'),
        )
        result = node.testmempoolaccept([spend_hex])[0]
        assert_equal(result['allowed'], False)
        assert 'Public key version reserved for soft-fork upgrades' in result['reject-reason']

        self.log.info('Mine past activation; Dilithium P2MR receive must work')
        self.generate(node, 2)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT + 1)
        self.test_dilithium_info(is_active=True)

        txid = node.sendtoaddress(dil_addr, Decimal('0.5'))
        assert txid
        self.generate(node, 1)
        assert_equal(node.getreceivedbyaddress(dil_addr), Decimal('1.5'))

        self.log.info('Spending Dilithium P2MR after activation must succeed')
        dil_utxos = node.listunspent(minconf=1, addresses=[dil_addr])
        assert len(dil_utxos) >= 2
        # Prefer the post-activation 0.5 BTQ output when selecting via createp2mrspend
        # by spending a small amount that fits either UTXO; createp2mrspend picks
        # confirmed UTXOs for this p2mr_id.
        spend_hex = self.sign_dilithium_p2mr_spend(
            node,
            p2mr_id=p2mr_id,
            dest=dest,
            amount=Decimal('0.25'),
        )
        result = node.testmempoolaccept([spend_hex])[0]
        if not result['allowed']:
            self.log.error('post-activation spend rejected: %s', result)
        assert_equal(result['allowed'], True)
        spend_txid = node.sendrawtransaction(spend_hex)
        assert spend_txid
        self.generate(node, 1)
        assert_equal(node.getreceivedbyaddress(dest), Decimal('0.25'))


if __name__ == '__main__':
    DilithiumActivationTest().main()
