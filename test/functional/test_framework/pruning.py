# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Large, consensus-valid witness blocks for pruning tests."""

from .address import address_to_scriptpubkey
from .blocktools import COINBASE_MATURITY, add_witness_commitment, create_block, create_coinbase
from .messages import COIN, MAX_BLOCK_WEIGHT, COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut, tx_from_hex
from .script import CScript, OP_NOP, OP_RETURN, taproot_construct
from .util import assert_equal


class LargeBlockBuilder:
    def __init__(self):
        # An unknown Taproot leaf version permits a large uninterpreted witness.
        # A 950k non-witness coinbase would exceed BTQ's block weight limit.
        self.script = CScript([OP_RETURN] + [OP_NOP] * 950000)
        self.tap = taproot_construct(bytes.fromhex("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"), [("large", self.script, 0xc2)])
        self.utxos = {}

    def build(self, node, *, height, ntime, previousblockhash):
        key = node.get_deterministic_priv_key()
        utxo = self.utxos.get(node.index)
        if utxo is None or node.gettxout(utxo["txid"], utxo["vout"]) is None:
            candidates = node.scantxoutset("start", [f"addr({key.address})"])["unspents"]
            utxo = next(u for u in candidates if u["height"] <= height - COINBASE_MATURITY)
        funding = CTransaction()
        funding.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        funding.vout = [CTxOut(0, self.tap.scriptPubKey), CTxOut(int(utxo["amount"] * COIN), address_to_scriptpubkey(key.address))]
        signed = node.signrawtransactionwithkey(funding.serialize().hex(), [key.key])
        assert_equal(signed["complete"], True)
        funding = tx_from_hex(signed["hex"])
        funding.rehash()
        spend = CTransaction()
        spend.vin = [CTxIn(COutPoint(funding.sha256, 0))]
        spend.vout = [CTxOut(0, CScript([OP_RETURN]))]
        spend.wit.vtxinwit = [CTxInWitness()]
        spend.wit.vtxinwit[0].scriptWitness.stack = [self.script, bytes([0xc2 | self.tap.negflag]) + self.tap.internal_pubkey]
        block = create_block(previousblockhash, create_coinbase(height, script_pubkey=CScript([OP_RETURN])), ntime, txlist=[funding, spend])
        add_witness_commitment(block)
        assert block.get_weight() <= MAX_BLOCK_WEIGHT
        assert len(block.serialize()) > 950000
        self.utxos[node.index] = {"txid": funding.hash, "vout": 1, "amount": utxo["amount"]}
        return block
