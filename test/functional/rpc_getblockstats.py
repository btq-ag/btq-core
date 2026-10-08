#!/usr/bin/env python3
# Copyright (c) 2017-2021 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test getblockstats rpc call
#

from test_framework.blocktools import COINBASE_MATURITY, TIME_GENESIS_BLOCK
from test_framework.test_framework import BTQTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)
import json
import os

TESTSDIR = os.path.dirname(os.path.realpath(__file__))

class GetblockstatsTest(BTQTestFramework):

    start_height = 101
    max_stat_pos = 2

    def add_options(self, parser):
        parser.add_argument('--gen-test-data', dest='gen_test_data',
                            default=False, action='store_true',
                            help='Generate test data')
        parser.add_argument('--test-data', dest='test_data',
                            default='data/rpc_getblockstats.json',
                            action='store', metavar='FILE',
                            help='Test data file')

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.supports_cli = False
        # Fixture replay does not need a wallet; generation imports a fixed key.
        # BTQ's legacy wallet defaults to P2PKH change; use P2WPKH so the
        # fixture has SegWit spends.
        if self.options.gen_test_data:
            self.extra_args = [['-disablewallet=0', '-changetype=bech32']]

    def get_stats(self):
        return [self.nodes[0].getblockstats(hash_or_height=self.start_height + i) for i in range(self.max_stat_pos+1)]

    def generate_test_data(self, filename):
        mocktime = TIME_GENESIS_BLOCK + 1
        self.nodes[0].setmocktime(mocktime)
        self.nodes[0].createwallet(wallet_name='test', descriptors=False)
        privkey = self.nodes[0].get_deterministic_priv_key().key
        self.nodes[0].importprivkey(privkey)

        self.generate(self.nodes[0], COINBASE_MATURITY + 1)

        address = self.nodes[0].get_deterministic_priv_key().address
        self.nodes[0].sendtoaddress(address=address, amount=1, subtractfeefromamount=True)
        self.generate(self.nodes[0], 1)

        # Lock the P2PKH coins so the next block's transactions spend P2WPKH
        # change and cover the swtxs/swtotal_* statistics.
        legacy_coins = [{"txid": u["txid"], "vout": u["vout"]} for u in self.nodes[0].listunspent()
                        if not u["scriptPubKey"].startswith("0014")]
        self.nodes[0].lockunspent(False, legacy_coins)
        self.nodes[0].sendtoaddress(address=address, amount=1, subtractfeefromamount=True)
        self.nodes[0].sendtoaddress(address=address, amount=1, subtractfeefromamount=False)
        self.nodes[0].settxfee(amount=0.003)
        self.nodes[0].sendtoaddress(address=address, amount=0.1, subtractfeefromamount=True)
        # Send to OP_RETURN output to test its exclusion from statistics
        self.nodes[0].send(outputs={"data": "21"})
        self.sync_all()
        self.generate(self.nodes[0], 1)

        self.expected_stats = self.get_stats()

        blocks = []
        tip = self.nodes[0].getbestblockhash()
        blockhash = None
        height = 0
        while tip != blockhash:
            blockhash = self.nodes[0].getblockhash(height)
            blocks.append(self.nodes[0].getblock(blockhash, 0))
            height += 1

        to_dump = {
            'blocks': blocks,
            'mocktime': int(mocktime),
            'stats': self.expected_stats,
        }
        with open(filename, 'w', encoding="utf8") as f:
            json.dump(to_dump, f, sort_keys=True, indent=2)

    def load_test_data(self, filename):
        with open(filename, 'r', encoding="utf8") as f:
            d = json.load(f)
            blocks = d['blocks']
            mocktime = d['mocktime']
            self.expected_stats = d['stats']

        # Set the timestamps from the file so that the nodes can get out of Initial Block Download
        self.nodes[0].setmocktime(mocktime)
        self.sync_all()

        assert_equal(self.nodes[0].getblock(self.nodes[0].getblockhash(0), 0), blocks[0])
        for b in blocks[1:]:
            assert_equal(self.nodes[0].submitblock(b), None)


    def run_test(self):
        test_data = os.path.join(TESTSDIR, self.options.test_data)
        if self.options.gen_test_data:
            self.generate_test_data(test_data)
        else:
            self.load_test_data(test_data)

        self.sync_all()
        stats = self.get_stats()

        # Make sure all valid statistics are included but nothing else is
        expected_keys = self.expected_stats[0].keys()
        assert_equal(set(stats[0].keys()), set(expected_keys))

        assert_equal(stats[0]['height'], self.start_height)
        assert_equal(stats[self.max_stat_pos]['height'], self.start_height + self.max_stat_pos)
        # The last block must cover the SegWit statistics.
        assert_greater_than(stats[self.max_stat_pos]['swtxs'], 0)

        for i in range(self.max_stat_pos+1):
            self.log.info('Checking block %d\n' % (i))
            assert_equal(stats[i], self.expected_stats[i])

            # Check selecting block by hash too
            blockhash = self.expected_stats[i]['blockhash']
            stats_by_hash = self.nodes[0].getblockstats(hash_or_height=blockhash)
            assert_equal(stats_by_hash, self.expected_stats[i])

        # Make sure each stat can be queried on its own
        for stat in expected_keys:
            for i in range(self.max_stat_pos+1):
                result = self.nodes[0].getblockstats(hash_or_height=self.start_height + i, stats=[stat])
                assert_equal(list(result.keys()), [stat])
                if result[stat] != self.expected_stats[i][stat]:
                    self.log.info('result[%s] (%d) failed, %r != %r' % (
                        stat, i, result[stat], self.expected_stats[i][stat]))
                assert_equal(result[stat], self.expected_stats[i][stat])

        # Make sure only the selected statistics are included (more than one)
        some_stats = {'minfee', 'maxfee'}
        stats = self.nodes[0].getblockstats(hash_or_height=1, stats=list(some_stats))
        assert_equal(set(stats.keys()), some_stats)

        # Test invalid parameters raise the proper json exceptions
        tip = self.start_height + self.max_stat_pos
        assert_raises_rpc_error(-8, 'Target block height %d after current tip %d' % (tip+1, tip),
                                self.nodes[0].getblockstats, hash_or_height=tip+1)
        assert_raises_rpc_error(-8, 'Target block height %d is negative' % (-1),
                                self.nodes[0].getblockstats, hash_or_height=-1)

        # Make sure not valid stats aren't allowed
        inv_sel_stat = 'asdfghjkl'
        inv_stats = [
            [inv_sel_stat],
            ['minfee', inv_sel_stat],
            [inv_sel_stat, 'minfee'],
            ['minfee', inv_sel_stat, 'maxfee'],
        ]
        for inv_stat in inv_stats:
            assert_raises_rpc_error(-8, f"Invalid selected statistic '{inv_sel_stat}'",
                                    self.nodes[0].getblockstats, hash_or_height=1, stats=inv_stat)

        # Make sure we aren't always returning inv_sel_stat as the culprit stat
        assert_raises_rpc_error(-8, f"Invalid selected statistic 'aaa{inv_sel_stat}'",
                                self.nodes[0].getblockstats, hash_or_height=1, stats=['minfee', f'aaa{inv_sel_stat}'])
        # Mainchain's genesis block shouldn't be found on regtest
        assert_raises_rpc_error(-5, 'Block not found', self.nodes[0].getblockstats,
                                hash_or_height='000003194a90d8d8eff8b39a7ad4e2490729b97a6772b7f4c4cb8887dffd1ae4')

        # Invalid number of args
        assert_raises_rpc_error(-1, 'getblockstats hash_or_height ( stats )', self.nodes[0].getblockstats, '00', 1, 2)
        assert_raises_rpc_error(-1, 'getblockstats hash_or_height ( stats )', self.nodes[0].getblockstats)

        self.log.info('Test block height 0')
        genesis_stats = self.nodes[0].getblockstats(0)
        assert_equal(genesis_stats["blockhash"], "5a6c309a7e9bb2fa314e63630520ca3c598c86a91dd2c6737e160cfadfc50f38")
        assert_equal(genesis_stats["utxo_increase"], 1)
        assert_equal(genesis_stats["utxo_size_inc"], 117)
        assert_equal(genesis_stats["utxo_increase_actual"], 0)
        assert_equal(genesis_stats["utxo_size_inc_actual"], 0)

        self.log.info('Test tip including OP_RETURN')
        tip_stats = self.nodes[0].getblockstats(tip)
        assert_equal(tip_stats["utxo_increase"], 6)
        assert_equal(tip_stats["utxo_size_inc"], 438)
        assert_equal(tip_stats["utxo_increase_actual"], 4)
        assert_equal(tip_stats["utxo_size_inc_actual"], 297)

if __name__ == '__main__':
    GetblockstatsTest().main()
