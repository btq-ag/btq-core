#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Dilithium opcode activation height on regtest (BTQ-AUDIT-017)."""

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

    def sign_dilithium_spend(self, node, *, utxo, dest, amount):
        prevtxs = [{
            'txid': utxo['txid'],
            'vout': utxo['vout'],
            'scriptPubKey': utxo['scriptPubKey'],
            'amount': utxo['amount'],
        }]
        raw = node.createrawtransaction(
            [{'txid': utxo['txid'], 'vout': utxo['vout']}],
            [{dest: amount}],
        )
        signed = node.signtransactionwithdilithium(raw, prevtxs)
        assert signed['complete'], signed.get('errors')
        return signed['hex']

    def run_test(self):
        node = self.nodes[0]

        self.log.info('Dilithium inactive at genesis')
        self.test_dilithium_info(is_active=False)

        self.log.info('Mine to two blocks before Dilithium activation')
        self.generate(node, DILITHIUM_HEIGHT - 2)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT - 2)
        self.test_dilithium_info(is_active=False)

        dil_addr = node.getnewdilithiumaddress()

        self.log.info('Creating Dilithium outputs before activation is allowed')
        txid = node.sendtoaddress(dil_addr, Decimal('1.0'))
        assert txid
        self.generate(node, 1)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT - 1)
        self.test_dilithium_info(is_active=True)

        dil_utxos = node.listunspent(minconf=1, addresses=[dil_addr])
        assert_equal(len(dil_utxos), 1)
        dest = node.getnewaddress()

        self.log.info('Spending Dilithium before activation must be rejected')
        spend_hex = self.sign_dilithium_spend(
            node,
            utxo=dil_utxos[0],
            dest=dest,
            amount=Decimal('0.49'),
        )
        result = node.testmempoolaccept([spend_hex])[0]
        assert_equal(result['allowed'], False)
        assert 'Public key version reserved for soft-fork upgrades' in result['reject-reason']

        self.log.info('Mine past activation; Dilithium receive must work')
        self.generate(node, 2)
        assert_equal(node.getblockcount(), DILITHIUM_HEIGHT + 1)
        self.test_dilithium_info(is_active=True)

        txid = node.sendtoaddress(dil_addr, Decimal('0.5'))
        assert txid
        self.generate(node, 1)
        assert_equal(node.getreceivedbyaddress(dil_addr), Decimal('1.5'))

        self.log.info('Spending Dilithium after activation must succeed')
        dil_utxos = node.listunspent(minconf=1, addresses=[dil_addr])
        assert len(dil_utxos) >= 2
        # Use the 0.5 BTQ output confirmed after activation (not the pre-activation 1.0).
        post_act_utxo = next(u for u in dil_utxos if u['amount'] == Decimal('0.5'))
        spend_hex = self.sign_dilithium_spend(
            node,
            utxo=post_act_utxo,
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
