#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Header-work filter on the real P2P path.

A headers chain whose work equals the node's threshold is stored. One proof
less is not, and that peer stays connected. The side with more work can
reject a lighter branch while the lighter side stores the heavier one.
"""

from test_framework.blocktools import TIME_GENESIS_BLOCK, create_block, create_coinbase
from test_framework.messages import CBlockHeader, from_hex, msg_headers
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# Regtest proof is 2 (nBits 0x207fffff, no retarget). Genesis work is one
# proof, so 0x6 is exactly two headers on top of genesis.
MIN_WORK_HEX = "0x6"
# Same depth AntiDoSWorkThreshold subtracts. Used only to check the mined
# tip is far enough that a two-header fork sits under it.
FORK_DEPTH = 144
HIGH_CHAIN_LEN = 160


class LowWorkHeadersTest(BTQTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        # Node 0 grows a chain. Node 1's floor is the work of genesis plus
        # two headers, so one header is under the filter and two is on it.
        self.extra_args = [
            ["-minimumchainwork=0x0"],
            [f"-minimumchainwork={MIN_WORK_HEX}"],
        ]

    def setup_network(self):
        self.setup_nodes()

    def chainwork(self, node, height):
        blockhash = node.getblockhash(height)
        return int(node.getblockheader(blockhash)["chainwork"], 16)

    def make_headers(self, count):
        headers = []
        previous = int(self.nodes[0].getblockhash(0), 16)
        for height in range(1, count + 1):
            block = create_block(
                previous,
                create_coinbase(height),
                TIME_GENESIS_BLOCK + height * 60,
            )
            block.solve()
            headers.append(CBlockHeader(block))
            previous = block.sha256
        return headers

    def assert_peer_stays(self, node, peer, peer_id):
        peer.sync_with_ping()
        assert peer.is_connected
        assert_equal([p["id"] for p in node.getpeerinfo()], [peer_id])

    def assert_headers_only(self, node, header, height):
        assert {
            "height": height,
            "hash": header.hash,
            "branchlen": height,
            "status": "headers-only",
        } in node.getchaintips()
        assert_equal(node.getblockheader(header.hash)["height"], height)

    def run_test(self):
        genesis = self.nodes[0].getblockhash(0)
        assert_equal(genesis, self.nodes[1].getblockhash(0))
        assert_equal(self.chainwork(self.nodes[0], 0), 2)

        self.boundary_on_low_node()
        self.direction_against_high_node()

    def boundary_on_low_node(self):
        self.log.info("Work equal to the threshold is stored; one proof less is not")
        node = self.nodes[1]
        peer = node.add_p2p_connection(P2PInterface())
        peer_id = node.getpeerinfo()[0]["id"]
        chain = self.make_headers(2)

        with node.assert_debug_log(["[net] Ignoring low-work chain (height=1) from peer="]):
            peer.send_and_ping(msg_headers(chain[:1]))
        self.assert_peer_stays(node, peer, peer_id)
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, chain[0].hash)

        peer.send_and_ping(msg_headers(chain))
        self.assert_peer_stays(node, peer, peer_id)
        self.assert_headers_only(node, chain[-1], 2)
        self.short_chain = chain
        self.low_peer = peer
        self.low_peer_id = peer_id

    def direction_against_high_node(self):
        self.log.info("Higher-work side rejects the lighter branch; lower-work side stores the heavier one")
        high = self.nodes[0]
        low = self.nodes[1]
        self.generate(high, HIGH_CHAIN_LEN, sync_fun=self.no_op)
        assert_equal(high.getblockcount(), HIGH_CHAIN_LEN)

        tip_work = self.chainwork(high, HIGH_CHAIN_LEN)
        proof = tip_work - self.chainwork(high, HIGH_CHAIN_LEN - 1)
        assert_equal(proof, 2)
        # Two headers on genesis: work 6. The mined tip's buffer must sit above that.
        assert self.chainwork(high, 0) + 2 * proof < tip_work - FORK_DEPTH * proof

        high_best = high.getbestblockhash()
        peer = high.add_p2p_connection(P2PInterface())
        peer_id = high.getpeerinfo()[0]["id"]
        with high.assert_debug_log(["[net] Ignoring low-work chain (height=2) from peer="]):
            peer.send_and_ping(msg_headers(self.short_chain))
        self.assert_peer_stays(high, peer, peer_id)
        assert_equal(high.getbestblockhash(), high_best)
        assert_raises_rpc_error(-5, "Block not found", high.getblockheader, self.short_chain[-1].hash)

        high_headers = [
            from_hex(CBlockHeader(), high.getblockheader(high.getblockhash(height), False))
            for height in range(1, HIGH_CHAIN_LEN + 1)
        ]
        self.low_peer.send_and_ping(msg_headers(high_headers))
        self.assert_peer_stays(low, self.low_peer, self.low_peer_id)
        assert {
            "height": HIGH_CHAIN_LEN,
            "hash": high_best,
            "branchlen": HIGH_CHAIN_LEN,
            "status": "headers-only",
        } in low.getchaintips()
        assert_equal(low.getblockheader(high_best)["height"], HIGH_CHAIN_LEN)


if __name__ == '__main__':
    LowWorkHeadersTest().main()
