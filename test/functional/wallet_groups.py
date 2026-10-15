#!/usr/bin/env python3
# Copyright (c) 2018-2022 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test wallet group functionality."""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BTQTestFramework
from test_framework.messages import (
    tx_from_hex,
)
from test_framework.util import (
    assert_approx,
    assert_equal,
)

# The amounts this test moves around are a tenth of upstream's because BTQ's
# block subsidy is a tenth of Bitcoin's. The fee rate is unchanged, so the
# assert_approx spans (which only absorb the fee) are too.


class WalletGroupTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 5
        self.extra_args = [
            [],
            [],
            ["-avoidpartialspends"],
            # One sat below and exactly at the grouped - non-grouped fee
            # difference for tx5/tx6 (6220 - 4320 sats) further down.
            ["-maxapsfee=0.00001899"],
            ["-maxapsfee=0.00001900"],
        ]

        for args in self.extra_args:
            args.append("-whitelist=noban@127.0.0.1")   # whitelist peers to speed up tx relay / mempool sync
            args.append(f"-paytxfee={20 * 1e3 / 1e8}")  # apply feerate of 20 sats/vB across all nodes

        self.rpc_timeout = 480

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Setting up")
        # To take full use of immediate tx relay, all nodes need to be reachable
        # via inbound peers, i.e. connect first to last to close the circle
        # (the default test network topology looks like this:
        #  node0 <-- node1 <-- node2 <-- node3 <-- node4 <-- node5)
        self.connect_nodes(0, self.num_nodes - 1)
        # Mine some coins
        self.generate(self.nodes[0], COINBASE_MATURITY + 1)

        # Get some addresses from the two nodes
        addr1 = [self.nodes[1].getnewaddress() for _ in range(3)]
        addr2 = [self.nodes[2].getnewaddress() for _ in range(3)]
        addrs = addr1 + addr2

        # Send 0.1 + 0.05 coin to each address
        [self.nodes[0].sendtoaddress(addr, Decimal('0.1')) for addr in addrs]
        [self.nodes[0].sendtoaddress(addr, Decimal('0.05')) for addr in addrs]

        self.generate(self.nodes[0], 1)

        # For each node, send 0.02 coins back to 0;
        # - node[1] should pick one 0.05 UTXO and leave the rest
        # - node[2] should pick one (0.1 + 0.05) UTXO group corresponding to a
        #   given address, and leave the rest
        self.log.info("Test sending transactions picks one UTXO group and leaves the rest")
        txid1 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.02'))
        tx1 = self.nodes[1].getrawtransaction(txid1, True)
        # txid1 should have 1 input and 2 outputs
        assert_equal(1, len(tx1["vin"]))
        assert_equal(2, len(tx1["vout"]))
        # one output should be 0.02, the other should be ~0.03
        v = [vout["value"] for vout in tx1["vout"]]
        v.sort()
        assert_approx(v[0], vexp=0.02, vspan=0.0001)
        assert_approx(v[1], vexp=0.03, vspan=0.0001)

        txid2 = self.nodes[2].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.02'))
        tx2 = self.nodes[2].getrawtransaction(txid2, True)
        # txid2 should have 2 inputs and 2 outputs
        assert_equal(2, len(tx2["vin"]))
        assert_equal(2, len(tx2["vout"]))
        # one output should be 0.02, the other should be ~0.13
        v = [vout["value"] for vout in tx2["vout"]]
        v.sort()
        assert_approx(v[0], vexp=0.02, vspan=0.0001)
        assert_approx(v[1], vexp=0.13, vspan=0.0001)

        self.log.info("Test avoiding partial spends if warranted, even if avoidpartialspends is disabled")
        self.sync_all()
        self.generate(self.nodes[0], 1)
        # Nodes 1-2 now have confirmed UTXOs (letters denote destinations):
        # Node #1:      Node #2:
        # - A  0.1      - D0 0.1
        # - B0 0.1      - D1 0.05
        # - B1 0.05     - E0 0.1
        # - C0 0.1      - E1 0.05
        # - C1 0.05     - F  ~0.13
        # - D ~0.03
        assert_approx(self.nodes[1].getbalance(), vexp=0.43, vspan=0.0001)
        assert_approx(self.nodes[2].getbalance(), vexp=0.43, vspan=0.0001)
        # Sending 0.14 btc should pick one 0.1 + one more. For node #1,
        # this could be (A / B0 / C0) + (B1 / C1 / D). We ensure that it is
        # B0 + B1 or C0 + C1, because this avoids partial spends while not being
        # detrimental to transaction cost
        txid3 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.14'))
        tx3 = self.nodes[1].getrawtransaction(txid3, True)
        # tx3 should have 2 inputs and 2 outputs
        assert_equal(2, len(tx3["vin"]))
        assert_equal(2, len(tx3["vout"]))
        # the accumulated value should be 0.15, so the outputs should be
        # ~0.01 and 0.14 and should come from the same destination
        values = [vout["value"] for vout in tx3["vout"]]
        values.sort()
        assert_approx(values[0], vexp=0.01, vspan=0.0001)
        assert_approx(values[1], vexp=0.14, vspan=0.0001)

        input_txids = [vin["txid"] for vin in tx3["vin"]]
        input_addrs = [self.nodes[1].gettransaction(txid)['details'][0]['address'] for txid in input_txids]
        assert_equal(input_addrs[0], input_addrs[1])
        # Node 2 enforces avoidpartialspends so needs no checking here

        # Lower than upstream's because BTQ's witness scale factor is 16, not
        # 4, so the P2WPKH inputs these transactions spend weigh fewer vbytes.
        tx4_ungrouped_fee = 2400
        tx4_grouped_fee = 3360
        tx5_6_ungrouped_fee = 4320
        tx5_6_grouped_fee = 6220

        self.log.info("Test wallet option maxapsfee")
        addr_aps = self.nodes[3].getnewaddress()
        self.nodes[0].sendtoaddress(addr_aps, Decimal('0.1'))
        self.nodes[0].sendtoaddress(addr_aps, Decimal('0.1'))
        self.generate(self.nodes[0], 1)
        with self.nodes[3].assert_debug_log([f'Fee non-grouped = {tx4_ungrouped_fee}, grouped = {tx4_grouped_fee}, using grouped']):
            txid4 = self.nodes[3].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.01'))
        tx4 = self.nodes[3].getrawtransaction(txid4, True)
        # tx4 should have 2 inputs and 2 outputs although one output would
        # have been enough and the transaction caused higher fees
        assert_equal(2, len(tx4["vin"]))
        assert_equal(2, len(tx4["vout"]))

        addr_aps2 = self.nodes[3].getnewaddress()
        [self.nodes[0].sendtoaddress(addr_aps2, Decimal('0.1')) for _ in range(5)]
        self.generate(self.nodes[0], 1)
        with self.nodes[3].assert_debug_log([f'Fee non-grouped = {tx5_6_ungrouped_fee}, grouped = {tx5_6_grouped_fee}, using non-grouped']):
            txid5 = self.nodes[3].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.295'))
        tx5 = self.nodes[3].getrawtransaction(txid5, True)
        # tx5 should have 3 inputs (0.1, 0.1, 0.1) and 2 outputs
        assert_equal(3, len(tx5["vin"]))
        assert_equal(2, len(tx5["vout"]))

        # Test wallet option maxapsfee with node 4, which sets maxapsfee
        # 1 sat higher, crossing the threshold from non-grouped to grouped.
        self.log.info("Test wallet option maxapsfee threshold from non-grouped to grouped")
        addr_aps3 = self.nodes[4].getnewaddress()
        [self.nodes[0].sendtoaddress(addr_aps3, Decimal('0.1')) for _ in range(5)]
        self.generate(self.nodes[0], 1)
        with self.nodes[4].assert_debug_log([f'Fee non-grouped = {tx5_6_ungrouped_fee}, grouped = {tx5_6_grouped_fee}, using grouped']):
            txid6 = self.nodes[4].sendtoaddress(self.nodes[0].getnewaddress(), Decimal('0.295'))
        tx6 = self.nodes[4].getrawtransaction(txid6, True)
        # tx6 should have 5 inputs and 2 outputs
        assert_equal(5, len(tx6["vin"]))
        assert_equal(2, len(tx6["vout"]))

        # Empty out node2's wallet
        self.nodes[2].sendall(recipients=[self.nodes[0].getnewaddress()])
        self.sync_all()
        self.generate(self.nodes[0], 1)

        self.log.info("Fill a wallet with 10,000 outputs corresponding to the same scriptPubKey")
        # Upstream sends 5 transactions of 2000 outputs. At BTQ's witness
        # scale factor of 16 that many outputs weigh more than
        # MAX_STANDARD_TX_WEIGHT, so send 20 of 500, each the weight upstream's
        # transactions had.
        for _ in range(20):
            raw_tx = self.nodes[0].createrawtransaction([{"txid":"0"*64, "vout":0}], [{addr2[0]: Decimal('0.005')}])
            tx = tx_from_hex(raw_tx)
            tx.vin = []
            tx.vout = [tx.vout[0]] * 500
            funded_tx = self.nodes[0].fundrawtransaction(tx.serialize().hex())
            signed_tx = self.nodes[0].signrawtransactionwithwallet(funded_tx['hex'])
            self.nodes[0].sendrawtransaction(signed_tx['hex'])
            self.generate(self.nodes[0], 1)

        # Check that we can create a transaction that only requires ~100 of our
        # utxos, without pulling in all outputs and creating a transaction that
        # is way too big.
        self.log.info("Test creating txn that only requires ~100 of our UTXOs without pulling in all outputs")
        assert self.nodes[2].sendtoaddress(address=addr2[0], amount=Decimal('0.5'))


if __name__ == '__main__':
    WalletGroupTest().main()
