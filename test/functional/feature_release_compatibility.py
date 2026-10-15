#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Round-trip actual BTQ releases through current chainstate, txindex and wallets.

Uses only locally generated regtest blocks, classical bech32 wallet outputs,
and pinned, hash-checked source-build manifests. It makes no claim that old
releases support newer Dilithium PSBT features or pre-BTQ wallet migrations.
"""

from decimal import Decimal

from test_framework.previous_releases import release_binaries
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class ReleaseCompatibilityTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.wallet_names = []
        self.extra_args = [["-txindex=1", "-walletbroadcast=0"]] * 2
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.releases = release_binaries()

    def setup_network(self):
        self.add_nodes(2, extra_args=self.extra_args,
                       binary=[release["btqd"] for release in self.releases],
                       binary_cli=[release["btq-cli"] for release in self.releases])
        for node in self.nodes:
            self.start_node(node.index)

    def start_node(self, i, *args, **kwargs):
        # Old RPC calls must not count toward current-binary RPC coverage or
        # initialize its reference command list from an older release.
        coverage_dir = self.options.coveragedir
        if self.nodes[i].binary != self.options.btqd:
            self.options.coveragedir = None
        self.nodes[i].coverage_dir = self.options.coveragedir
        try:
            super().start_node(i, *args, **kwargs)
        finally:
            self.options.coveragedir = coverage_dir

    def switch_binary(self, node, binary, *, extra_args=None):
        self.stop_node(node.index)
        node.binary = binary
        node.args[0] = binary
        self.start_node(node.index, extra_args=extra_args)

    def check_index(self, node, txid):
        self.wait_until(lambda: node.getindexinfo()["txindex"]["synced"])
        assert_equal(node.getrawtransaction(txid, True)["txid"], txid)

    def run_test(self):
        for node, release in zip(self.nodes, self.releases):
            self.log.info("Testing %s -> current -> %s", release["tag"], release["tag"])
            assert_equal(node.getnetworkinfo()["version"], release["version"])
            assert_equal(node.getblockhash(0), "5a6c309a7e9bb2fa314e63630520ca3c598c86a91dd2c6737e160cfadfc50f38")
            node.createwallet("compat", descriptors=self.options.descriptors, load_on_startup=True)
            wallet = node.get_wallet_rpc("compat")
            address = wallet.getnewaddress("historical", "bech32")
            blocks = self.generatetoaddress(node, 102, address, sync_fun=self.no_op)
            coinbase = node.getblock(blocks[0])["tx"][0]
            self.check_index(node, coinbase)
            txid = wallet.sendtoaddress(wallet.getnewaddress("old send", "bech32"), Decimal("0.1"))
            node.sendrawtransaction(wallet.gettransaction(txid)["hex"])
            node.prioritisetransaction(txid, 0, 1000)
            entry = node.getmempoolentry(txid)
            node.savemempool()
            balance = wallet.getbalances()
            state = node.gettxoutsetinfo()["hash_serialized_3"]

            self.switch_binary(node, self.options.btqd)
            assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], state)
            self.check_index(node, coinbase)
            assert_equal(node.getrawmempool(), [txid])
            assert_equal(node.getmempoolentry(txid)["fees"], entry["fees"])
            wallet = node.get_wallet_rpc("compat")
            assert_equal(wallet.getbalances(), balance)
            assert_equal(wallet.getaddressinfo(address)["ismine"], True)
            assert_equal(wallet.getaddressinfo(address)["labels"], ["historical"])
            assert_equal(wallet.getwalletinfo()["walletversion"], 169900)
            assert_equal(wallet.upgradewallet()["current_version"], 169900)

            # Current writes an encrypted wallet and another mempool transaction.
            self.generatetoaddress(node, 1, address, sync_fun=self.no_op)
            wallet.encryptwallet("test-only-compatibility")
            wallet.walletpassphrase("test-only-compatibility", 60)
            current_tx = wallet.sendtoaddress(wallet.getnewaddress("current send", "bech32"), Decimal("0.1"))
            node.sendrawtransaction(wallet.gettransaction(current_tx)["hex"])
            node.prioritisetransaction(current_tx, 0, 2000)
            current_entry = node.getmempoolentry(current_tx)
            node.savemempool()
            balance = wallet.getbalances()
            state = node.gettxoutsetinfo()["hash_serialized_3"]
            tip = node.getbestblockhash()

            self.switch_binary(node, release["btqd"])
            assert_equal(node.getnetworkinfo()["version"], release["version"])
            assert_equal(node.getbestblockhash(), tip)
            assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], state)
            assert_equal(node.getrawmempool(), [current_tx])
            assert_equal(node.getmempoolentry(current_tx)["fees"], current_entry["fees"])
            self.check_index(node, coinbase)
            wallet = node.get_wallet_rpc("compat")
            assert_equal(wallet.getbalances(), balance)
            wallet.walletpassphrase("test-only-compatibility", 60)
            assert_equal(wallet.getaddressinfo(address)["ismine"], True)
            assert_equal(wallet.gettransaction(txid)["confirmations"], 1)
            self.check_index(node, txid)
            rollback_tx = wallet.sendtoaddress(wallet.getnewaddress("rollback send", "bech32"), Decimal("0.1"))
            assert_equal(node.testmempoolaccept([wallet.gettransaction(rollback_tx)["hex"]])[0]["allowed"], True)
            # Recovery reconstructs the same UTXOs from both producers' blocks.
            self.restart_node(node.index, extra_args=self.extra_args[node.index] + ["-reindex-chainstate"])
            assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], state)
            self.switch_binary(node, self.options.btqd,
                               extra_args=self.extra_args[node.index] + ["-reindex"])
            assert_equal(node.gettxoutsetinfo()["hash_serialized_3"], state)
            self.check_index(node, coinbase)


if __name__ == '__main__':
    ReleaseCompatibilityTest().main()
