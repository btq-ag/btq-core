#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reject forks below a reached checkpoint, using only generated regtest headers."""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class RejectCheckpointForkTest(BTQTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.genesis = "5a6c309a7e9bb2fa314e63630520ca3c598c86a91dd2c6737e160cfadfc50f38"

        def headers(count, offset):
            result = []
            previous = int(self.genesis, 16)
            for height in range(1, count + 1):
                block = create_block(previous, create_coinbase(height), 1771804800 + height * 60 + offset)
                block.solve()
                result.append(CBlockHeader(block))
                previous = block.sha256
            return result

        self.headers = headers(10, 0)
        self.fork = headers(2, 1)
        self.checkpoint = self.headers[-1].hash
        self.extra_args = [["-minimumchainwork=0", "-prune=550", f"-testcheckpoint=10:{self.checkpoint}"]] * 2

    def setup_network(self):
        self.setup_nodes()

    def assert_tip(self, node, header, height):
        assert {"height": height, "hash": header.hash, "branchlen": height,
                "status": "headers-only"} in node.getchaintips()

    def run_test(self):
        assert_equal(self.nodes[0].getblockhash(0), self.genesis)
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_headers(self.headers))
        self.assert_tip(self.nodes[0], self.headers[-1], 10)
        with self.nodes[0].assert_debug_log(["bad-fork-prior-to-checkpoint"]):
            peer.send_message(msg_headers(self.fork))
            peer.wait_for_disconnect()
        assert self.fork[-1].hash not in [tip["hash"] for tip in self.nodes[0].getchaintips()]

        # Same headers become acceptable when checkpoints are explicitly disabled.
        self.restart_node(0, extra_args=self.extra_args[0] + ["-nocheckpoints"])
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_headers(self.fork))
        self.assert_tip(self.nodes[0], self.fork[-1], 2)

        # Merely configuring a checkpoint must not reject forks before it is reached.
        peer = self.nodes[1].add_p2p_connection(P2PInterface())
        peer.send_and_ping(msg_headers(self.fork))
        self.assert_tip(self.nodes[1], self.fork[-1], 2)


if __name__ == '__main__':
    RejectCheckpointForkTest().main()
