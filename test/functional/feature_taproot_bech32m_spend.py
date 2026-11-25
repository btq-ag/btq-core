#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression for BTQ-AUDIT-2026-028: Taproot (bech32m) must be consensus-validated.

When SCRIPT_VERIFY_TAPROOT is active, witness v1 outputs cannot be spent with an
empty or invalid witness. This test funds a descriptor-wallet bech32m address,
rejects a crafted empty-witness spend, and confirms a normal wallet spend works.
"""

from decimal import Decimal

from test_framework.address import address_to_scriptpubkey
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import (
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    SEQUENCE_FINAL,
)
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_greater_than


class TaprootBech32mSpendTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True)

    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [
            ['-whitelist=noban@127.0.0.1'],
            ['-whitelist=noban@127.0.0.1'],
        ]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.sync_all()

    def run_test(self):
        miner = self.nodes[0]
        self.generate(miner, COINBASE_MATURITY + 1)

        self.log.info('Taproot deployment must be active on regtest')
        dep = miner.getdeploymentinfo()['deployments']['taproot']
        assert_equal(dep['type'], 'bip9')
        assert dep['active'], dep

        self.log.info('Create descriptor wallet and fund bech32m (P2TR) address')
        self.nodes[1].createwallet('taproot_spend', descriptors=True)
        wallet = self.nodes[1].get_wallet_rpc('taproot_spend')

        bech32m_addr = wallet.getnewaddress(address_type='bech32m')
        info = wallet.getaddressinfo(bech32m_addr)
        assert info['iswitness']
        assert_equal(info['witness_version'], 1)

        txid = miner.sendtoaddress(bech32m_addr, Decimal('1.0'))
        assert txid
        self.generate(miner, 1)
        self.sync_all()

        utxos = wallet.listunspent(minconf=1, addresses=[bech32m_addr])
        assert_equal(len(utxos), 1)
        utxo = utxos[0]

        self.log.info('Empty-witness spend of bech32m UTXO must be rejected')
        spend_tx = CTransaction()
        spend_tx.nVersion = 2
        spend_tx.vin = [CTxIn(
            COutPoint(int(utxo['txid'], 16), utxo['vout']),
            b'',
            SEQUENCE_FINAL,
        )]
        change_spk = address_to_scriptpubkey(wallet.getrawchangeaddress())
        spend_tx.vout = [CTxOut(int(0.5 * 1e8), change_spk)]
        spend_tx.wit.vtxinwit = [CTxInWitness()]
        spend_tx.rehash()

        result = miner.testmempoolaccept([spend_tx.serialize().hex()])[0]
        assert not result['allowed'], result
        reason = result.get('reject-reason', '')
        assert (
            'witness' in reason.lower()
            or 'Script failed' in reason
            or 'empty' in reason.lower()
        ), reason

        self.log.info('Bech32m UTXO must remain unspent after invalid witness attempt')
        utxos_after = wallet.listunspent(minconf=1, addresses=[bech32m_addr])
        assert_equal(len(utxos_after), 1)
        assert_equal(utxos_after[0]['txid'], utxo['txid'])
        assert_greater_than(utxos_after[0]['amount'], Decimal('0.9'))


if __name__ == '__main__':
    TaprootBech32mSpendTest().main()
