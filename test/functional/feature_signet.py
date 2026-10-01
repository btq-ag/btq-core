#!/usr/bin/env python3
# Copyright (c) 2019-2022 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test basic signet functionality"""

from decimal import Decimal
import struct
import subprocess

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.key import ECKey
from test_framework.messages import (
    CBlockHeader, COutPoint, CTransaction, CTxIn, CTxOut, from_hex, ser_string, ser_uint256,
)
from test_framework.script import CScript, LegacySignatureHash, OP_0, OP_RETURN, OP_TRUE, SIGHASH_ALL
from test_framework.script_util import keys_to_multisig_script

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal

SIGNET_HEADER = bytes.fromhex("ecc7daa2")
DEFAULT_CHALLENGE = (
    "522103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430"
    "210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae"
)


class SignetBasicTest(BTQTestFramework):
    def set_test_params(self):
        self.chain = "signet"
        self.num_nodes = 8
        self.setup_clean_chain = True
        shared_args1 = ["-signetchallenge=51"]  # OP_TRUE
        self.signers = []
        for secret in (1, 2):
            key = ECKey()
            key.set(secret.to_bytes(32, "big"), compressed=True)
            self.signers.append(key)
        pubkeys = [key.get_pubkey().get_bytes() for key in self.signers]
        shared_args2 = ["-signetchallenge=" + keys_to_multisig_script(pubkeys, k=1).hex()]
        shared_args3 = ["-signetchallenge=" + keys_to_multisig_script(pubkeys, k=2).hex()]
        self.extra_args = [
            shared_args1, shared_args1,
            shared_args2, shared_args2,
            shared_args3, shared_args3,
            [], [],  # BTQ's default challenge, with no known private keys
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_btq_util()

    def signed_block(self, node, signers):
        """Construct the BIP325 virtual transactions and sign a BTQ block template."""
        tmpl = node.getblocktemplate({"rules": ["signet", "segwit"]})
        challenge = CScript(bytes.fromhex(tmpl["signet_challenge"]))
        coinbase = create_coinbase(tmpl["height"], script_pubkey=CScript([OP_TRUE]))
        coinbase.vout[0].nValue = tmpl["coinbasevalue"]
        block = create_block(coinbase=coinbase, tmpl=tmpl)
        add_witness_commitment(block)
        witness_script = bytes(block.vtx[0].vout[-1].scriptPubKey)
        # The signature commits to a merkle root with only the signet header,
        # not the solution itself (see src/signet.cpp and contrib/signet/miner).
        block.vtx[0].vout[-1].scriptPubKey = witness_script + bytes(CScript([SIGNET_HEADER]))
        block.vtx[0].rehash()
        block_data = (struct.pack("<i", block.nVersion) + ser_uint256(block.hashPrevBlock)
                      + ser_uint256(block.calc_merkle_root()) + struct.pack("<I", block.nTime))
        to_spend = CTransaction()
        to_spend.nVersion = 0
        to_spend.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript([OP_0, block_data]), 0)]
        to_spend.vout = [CTxOut(0, challenge)]
        to_spend.rehash()
        to_sign = CTransaction()
        to_sign.nVersion = 0
        to_sign.vin = [CTxIn(COutPoint(to_spend.sha256, 0), b"", 0)]
        to_sign.vout = [CTxOut(0, CScript([OP_RETURN]))]
        sighash, error = LegacySignatureHash(challenge, to_sign, 0, SIGHASH_ALL)
        assert_equal(error, None)
        signatures = [key.sign_ecdsa(sighash, rfc6979=True) + bytes([SIGHASH_ALL])
                      for key in signers]
        solution = ser_string(CScript([OP_0] + signatures)) + b"\x00"  # empty witness
        block.vtx[0].vout[-1].scriptPubKey = witness_script + bytes(CScript([SIGNET_HEADER + solution]))
        block.vtx[0].rehash()
        block.hashMerkleRoot = block.calc_merkle_root()
        # Signet uses real proof of work; grind in C++ rather than Python.
        header = subprocess.run([self.options.btqutil, "grind", CBlockHeader.serialize(block).hex()],
                                capture_output=True, text=True, check=True, timeout=60).stdout.strip()
        block.nNonce = from_hex(CBlockHeader(), header).nNonce
        block.rehash()
        return block.serialize().hex()

    def setup_network(self):
        self.setup_nodes()

        # Each pair uses its own signing challenge.
        self.connect_nodes(0, 1)
        self.connect_nodes(2, 3)
        self.connect_nodes(4, 5)
        self.connect_nodes(6, 7)

    def run_test(self):
        self.log.info("basic tests using OP_TRUE challenge")

        self.log.info('getmininginfo')
        mining_info = self.nodes[0].getmininginfo()
        assert_equal(mining_info['blocks'], 0)
        assert_equal(mining_info['chain'], 'signet')
        assert 'currentblocktx' not in mining_info
        assert 'currentblockweight' not in mining_info
        assert_equal(mining_info['networkhashps'], Decimal('0'))
        assert_equal(mining_info['pooledtx'], 0)

        self.generate(self.nodes[0], 1, sync_fun=self.no_op)

        self.log.info("signed blocks using a 1-of-2 challenge")
        first_block = None
        for height in range(1, 11):
            block = self.signed_block(self.nodes[2], self.signers[:1])
            if first_block is None:
                first_block = block
            assert_equal(self.nodes[2].submitblock(block), None)
            self.sync_blocks(self.nodes[2:4])
            assert_equal(self.nodes[3].getblockcount(), height)

        self.log.info("reject a solution for another challenge")
        assert_equal(self.nodes[4].submitblock(first_block), "bad-signet-blksig")
        default_template = self.nodes[6].getblocktemplate({"rules": ["signet", "segwit"]})
        assert_equal(default_template["signet_challenge"], DEFAULT_CHALLENGE)
        assert_equal(self.nodes[6].submitblock(first_block), "bad-signet-blksig")
        assert_equal(self.nodes[6].getblockcount(), 0)

        self.log.info("2-of-2 challenge requires both signatures")
        incomplete = self.signed_block(self.nodes[4], self.signers[:1])
        assert_equal(self.nodes[4].submitblock(incomplete), "bad-signet-blksig")
        assert_equal(self.nodes[4].getblockcount(), 0)
        complete = self.signed_block(self.nodes[4], self.signers)
        assert_equal(self.nodes[4].submitblock(complete), None)
        self.sync_blocks(self.nodes[4:6])
        assert_equal(self.nodes[5].getblockcount(), 1)

        self.log.info("test that signet logs the network magic on node start")
        with self.nodes[0].assert_debug_log(["Signet derived magic (message start)"]):
            self.restart_node(0)
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(extra_args=["-signetchallenge=abc"], expected_msg="Error: -signetchallenge must be hex, not 'abc'.")
        self.nodes[0].assert_start_raises_init_error(extra_args=["-signetchallenge=abc"] * 2, expected_msg="Error: -signetchallenge cannot be multiple values.")


if __name__ == '__main__':
    SignetBasicTest().main()
