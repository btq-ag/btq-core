#!/usr/bin/env python3
# Copyright (c) 2019-2022 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that we reject low difficulty headers to prevent our block tree from filling up with useless bloat"""

from test_framework.test_framework import BTQTestFramework

from test_framework.p2p import (
    P2PInterface,
)

from test_framework.messages import (
    CBlockHeader,
    from_hex,
    msg_headers,
)

from test_framework.blocktools import (
    NORMAL_GBT_REQUEST_PARAMS,
    create_block,
)

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

NODE1_BLOCKS_REQUIRED = 15
NODE2_BLOCKS_REQUIRED = 2047


class RejectLowDifficultyHeadersTest(BTQTestFramework):
    def set_test_params(self):
        self.rpc_timeout *= 4  # To avoid timeout when generating BLOCKS_TO_MINE
        self.setup_clean_chain = True
        self.num_nodes = 4
        # Node0 has no required chainwork; node1 requires 15 blocks on top of the genesis block; node2 requires 2047
        self.extra_args = [["-minimumchainwork=0x0", "-checkblockindex=0"], ["-minimumchainwork=0x20", "-checkblockindex=0"], ["-minimumchainwork=0x1000", "-checkblockindex=0"], ["-minimumchainwork=0x1000", "-checkblockindex=0", "-whitelist=noban@127.0.0.1"]]

    def setup_network(self):
        self.setup_nodes()
        self.reconnect_all()
        self.sync_all()

    def disconnect_all(self):
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(0, 2)
        self.disconnect_nodes(0, 3)

    def reconnect_all(self):
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.connect_nodes(0, 3)

    def test_chains_sync_when_long_enough(self):
        self.log.info("Generate blocks on the node with no required chainwork, and verify nodes 1 and 2 have no new headers in their headers tree")
        with self.nodes[1].assert_debug_log(expected_msgs=["[net] Ignoring low-work chain (height=14)"]), self.nodes[2].assert_debug_log(expected_msgs=["[net] Ignoring low-work chain (height=14)"]), self.nodes[3].assert_debug_log(expected_msgs=["Synchronizing blockheaders, height: 14"]):
            self.generate(self.nodes[0], NODE1_BLOCKS_REQUIRED-1, sync_fun=self.no_op)
        # Regtest contributes two units of work per block. Genesis plus 14
        # blocks is therefore exactly two units below node1's 0x20 floor.
        assert_equal(int(self.nodes[0].getblockchaininfo()['chainwork'], 16), 0x1e)

        # Node3 should always allow headers due to noban permissions
        self.log.info("Check that node3 will sync headers (due to noban permissions)")

        def check_node3_chaintips(num_tips, tip_hash, height):
            node3_chaintips = self.nodes[3].getchaintips()
            assert len(node3_chaintips) == num_tips
            assert {
                'height': height,
                'hash': tip_hash,
                'branchlen': height,
                'status': 'headers-only',
            } in node3_chaintips

        check_node3_chaintips(2, self.nodes[0].getbestblockhash(), NODE1_BLOCKS_REQUIRED-1)

        genesis_hash = self.nodes[0].getblockhash(0)
        for node in self.nodes[1:3]:
            chaintips = node.getchaintips()
            assert len(chaintips) == 1
            assert {
                'height': 0,
                'hash': genesis_hash,
                'branchlen': 0,
                'status': 'active',
            } in chaintips

        self.log.info("Generate more blocks to satisfy node1's minchainwork requirement, and verify node2 still has no new headers in headers tree")
        with self.nodes[2].assert_debug_log(expected_msgs=["[net] Ignoring low-work chain (height=15)"]), self.nodes[3].assert_debug_log(expected_msgs=["Synchronizing blockheaders, height: 15"]):
            self.generate(self.nodes[0], NODE1_BLOCKS_REQUIRED - self.nodes[0].getblockcount(), sync_fun=self.no_op)
        self.sync_blocks(self.nodes[0:2]) # node3 will sync headers (noban permissions) but not blocks (due to minchainwork)
        # Equality with the floor is accepted; the production check rejects
        # only chains whose total work is strictly lower.
        assert_equal(int(self.nodes[0].getblockchaininfo()['chainwork'], 16), 0x20)
        assert_equal(self.nodes[1].getbestblockhash(), self.nodes[0].getbestblockhash())

        assert {
            'height': 0,
            'hash': genesis_hash,
            'branchlen': 0,
            'status': 'active',
        } in self.nodes[2].getchaintips()

        assert len(self.nodes[2].getchaintips()) == 1

        self.log.info("Check that node3 accepted these headers as well")
        check_node3_chaintips(2, self.nodes[0].getbestblockhash(), NODE1_BLOCKS_REQUIRED)

        self.log.info("Generate long chain for node0/node1/node3")
        self.generate(self.nodes[0], NODE2_BLOCKS_REQUIRED-self.nodes[0].getblockcount(), sync_fun=self.no_op)

        self.log.info("Verify that node2 and node3 will sync the chain when it gets long enough")
        self.sync_blocks()

    @staticmethod
    def headers_for_hashes(node, hashes):
        return [from_hex(CBlockHeader(), node.getblockheader(block_hash, False)) for block_hash in hashes]

    def test_tip_relative_threshold_directionality(self):
        self.log.info("Test exact tip-relative threshold boundaries and directionality")
        self.sync_all()
        self.disconnect_all()

        common_height = self.nodes[0].getblockcount()
        assert_equal(self.nodes[1].getblockcount(), common_height)

        # Build two valid, isolated constant-work forks from the common tip.
        # The high branch is 200 blocks longer than the low branch, while the
        # inherited near-tip work buffer is 144 tip-equivalent blocks.
        high_hashes = self.generate(self.nodes[0], 450, sync_fun=self.no_op)
        low_hashes = self.generate(self.nodes[1], 250, sync_fun=self.no_op)
        assert high_hashes[0] != low_hashes[0]

        high_headers = self.headers_for_hashes(self.nodes[0], high_hashes)
        low_headers = self.headers_for_hashes(self.nodes[1], low_hashes)

        # The high-work node filters the lower-work fork, but does not
        # disconnect the peer that announced it.
        peer_to_high = self.nodes[0].add_p2p_connection(P2PInterface())
        with self.nodes[0].assert_debug_log(expected_msgs=[f"[net] Ignoring low-work chain (height={common_height + len(low_headers)})"]):
            peer_to_high.send_and_ping(msg_headers(headers=low_headers))
        assert peer_to_high.is_connected
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getblockheader, low_hashes[-1])

        # In the opposite direction, the higher-work fork necessarily clears
        # the lower-work node's threshold and is stored as headers.
        peer_to_low = self.nodes[1].add_p2p_connection(P2PInterface())
        peer_to_low.send_and_ping(msg_headers(headers=high_headers))
        assert peer_to_low.is_connected
        assert_equal(
            self.nodes[1].getblockheader(high_hashes[-1])['height'],
            common_height + len(high_headers),
        )

        # With two work units per regtest block, a header building on the
        # ancestor 145 blocks behind the tip has total chainwork exactly equal
        # to the dynamic threshold. Moving its parent back one more block is
        # exactly two work units below the threshold.
        active_tip_height = self.nodes[0].getblockcount()
        equal_parent_hash = self.nodes[0].getblockhash(active_tip_height - 145)
        below_parent_hash = self.nodes[0].getblockhash(active_tip_height - 146)

        equal_header = create_block(
            hashprev=int(equal_parent_hash, 16),
            tmpl=self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS),
        )
        equal_header.solve()
        below_header = create_block(
            hashprev=int(below_parent_hash, 16),
            tmpl=self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS),
        )
        below_header.solve()

        with self.nodes[0].assert_debug_log(expected_msgs=["[net] Ignoring low-work chain"]):
            peer_to_high.send_and_ping(msg_headers(headers=[below_header]))
        assert peer_to_high.is_connected
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getblockheader, below_header.hash)

        peer_to_high.send_and_ping(msg_headers(headers=[equal_header]))
        assert peer_to_high.is_connected
        equal_header_info = self.nodes[0].getblockheader(equal_header.hash)
        tip_chainwork = int(self.nodes[0].getblockheader(high_hashes[-1])['chainwork'], 16)
        assert_equal(int(equal_header_info['chainwork'], 16), tip_chainwork - 144 * 2)

        # Clear outstanding block requests to the synthetic peers, reconnect
        # the real nodes, and converge before the remaining reorg tests.
        self.nodes[0].disconnect_p2ps()
        self.nodes[1].disconnect_p2ps()
        self.reconnect_all()
        self.sync_blocks(timeout=120)

    def test_peerinfo_includes_headers_presync_height(self):
        self.log.info("Test that getpeerinfo() includes headers presync height")

        # Disconnect network, so that we can find our own peer connection more
        # easily
        self.disconnect_all()

        p2p = self.nodes[0].add_p2p_connection(P2PInterface())
        node = self.nodes[0]

        # Ensure we have a long chain already
        current_height = self.nodes[0].getblockcount()
        if (current_height < 3000):
            self.generate(node, 3000-current_height, sync_fun=self.no_op)

        # Send a group of 2000 headers, forking from genesis.
        new_blocks = []
        hashPrevBlock = int(node.getblockhash(0), 16)
        for i in range(2000):
            block = create_block(hashprev = hashPrevBlock, tmpl=node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS))
            block.solve()
            new_blocks.append(block)
            hashPrevBlock = block.sha256

        headers_message = msg_headers(headers=new_blocks)
        p2p.send_and_ping(headers_message)

        # getpeerinfo should show a sync in progress
        assert_equal(node.getpeerinfo()[0]['presynced_headers'], 2000)

    def test_large_reorgs_can_succeed(self):
        self.log.info("Test that a 2000+ block reorg, starting from a point that is more than 2000 blocks before a locator entry, can succeed")

        self.sync_all() # Ensure all nodes are synced.
        # BTQ's future-timestamp window is shorter than Bitcoin's. Re-anchor
        # mock time at the shared tip before rapidly mining both long forks.
        tip_time = self.nodes[0].getblockheader(self.nodes[0].getbestblockhash())['time']
        for node in self.nodes:
            node.setmocktime(tip_time)
        self.disconnect_all()

        # locator(block at height T) will have heights:
        # [T, T-1, ..., T-10, T-12, T-16, T-24, T-40, T-72, T-136, T-264,
        #  T-520, T-1032, T-2056, T-4104, ...]
        # So mine a number of blocks > 4104 to ensure that the first window of
        # received headers during a sync are fully between locator entries.
        BLOCKS_TO_MINE = 4110

        self.generate(self.nodes[0], BLOCKS_TO_MINE, sync_fun=self.no_op)
        self.generate(self.nodes[1], BLOCKS_TO_MINE+2, sync_fun=self.no_op)

        self.reconnect_all()

        self.sync_blocks(timeout=300) # Ensure tips eventually agree


    def run_test(self):
        self.test_chains_sync_when_long_enough()

        self.test_tip_relative_threshold_directionality()

        self.test_large_reorgs_can_succeed()

        self.test_peerinfo_includes_headers_presync_height()



if __name__ == '__main__':
    RejectLowDifficultyHeadersTest().main()
