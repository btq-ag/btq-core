#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for CVE-2024-52911 (use-after-free in the script interpreter).

ConnectBlock dispatches script checks to background threads as CScriptCheck
functors. Each holds a *pointer* into a PrecomputedTransactionData, so that data
must outlive the check queue.

Local objects are destroyed in reverse order of construction, so the queue
control has to be constructed *after* the data it points at. When the order is
inverted, an early return between queueing the checks and the explicit
control.Wait() frees the precomputed data while it is still needed:
~CCheckQueueControl calls Wait(), and Wait() runs the outstanding checks.

Three conditions all have to hold for the dangling pointer to actually be
dereferenced, which is why this block is shaped so specifically.

1.  The queue must be deep at the instant of the return. One transaction with
    many inputs queues a large batch in a single control.Add(), and the *next*
    transaction is rejected, so the main thread does almost no work in between.
    Padding the block with ordinary transactions instead lets the worker drain
    the queue while the main thread grinds through the rest of the loop.

2.  The inputs must use a segwit sighash. BIP143 reads the cached midstate out
    of PrecomputedTransactionData; legacy P2PK and P2PKH sighashes never touch
    it, so a block full of those leaves the pointer dangling but unread and
    nothing is observable.

3.  The transaction must not be in the mempool. ConnectBlock consults the
    script execution cache first, and a hit means no checks are queued at all --
    hence signing here rather than broadcasting.

Under a normal build the freed read is silent and this only confirms the node
rejects the block and stays up. Observing the defect itself needs an
AddressSanitizer build:

    ./configure --with-sanitizers=address && make
    ASAN_OPTIONS=detect_leaks=0 test/functional/feature_cve_2024_52911.py

Against the unfixed tree that reports a heap-use-after-free in SignatureHash(),
read by a b-scriptch worker and freed by the thread running ConnectBlock.
"""

from test_framework.address import key_to_p2wpkh
from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.key import ECKey
from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
)
from test_framework.script import (
    CScript,
    OP_RETURN,
    SIGHASH_ALL,
    SegwitV0SignatureHash,
    hash160,
)
from test_framework.script_util import key_to_p2wpkh_script, keyhash_to_p2pkh_script
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal

# Inputs on the single large transaction, each one a queued BIP143
# verification. Only a handful need to still be outstanding when the return
# fires, so this is comfortably more than enough.
NUM_INPUTS = 400


class Cve202452911Test(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # -par counts the main thread, so this is exactly one background
        # worker. At -par=1 there are zero workers, no checks are queued at
        # all, and the defect cannot appear.
        self.extra_args = [["-par=2"]]

    def run_test(self):
        node = self.nodes[0]

        key = ECKey()
        key.set((11).to_bytes(32, "big"), True)
        pubkey = key.get_pubkey().get_bytes()
        # P2WPKH so spending uses the BIP143 sighash, which is what reads the
        # precomputed data. No wallet is involved anywhere.
        script_code = keyhash_to_p2pkh_script(hash160(pubkey))

        self.log.info(f"Mining {NUM_INPUTS + 150} blocks to a P2WPKH address")
        self.generatetoaddress(node, NUM_INPUTS + 150, key_to_p2wpkh(pubkey))

        self.log.info(f"Collecting {NUM_INPUTS} mature coinbase outputs")
        utxos = []
        for height in range(1, NUM_INPUTS + 1):
            coinbase = node.getblock(node.getblockhash(height), 2)["tx"][0]
            utxos.append((coinbase["txid"], int(coinbase["vout"][0]["value"] * COIN)))

        self.log.info(f"Building and signing one transaction with {NUM_INPUTS} inputs")
        big_tx = CTransaction()
        for txid, _ in utxos:
            big_tx.vin.append(CTxIn(COutPoint(int(txid, 16), 0)))
        big_tx.vout.append(CTxOut(sum(v for _, v in utxos) - 10000,
                                  key_to_p2wpkh_script(pubkey)))
        big_tx.wit.vtxinwit = [CTxInWitness() for _ in utxos]
        for i, (_, value) in enumerate(utxos):
            sighash = SegwitV0SignatureHash(script_code, big_tx, i, SIGHASH_ALL, value)
            signature = key.sign_ecdsa(sighash) + bytes([SIGHASH_ALL])
            big_tx.wit.vtxinwit[i].scriptWitness.stack = [signature, pubkey]
        big_tx.rehash()

        # Spends an outpoint that has never existed, so CheckTxInputs rejects it
        # and ConnectBlock returns while the queue behind it is still full.
        missing = CTransaction()
        missing.vin.append(CTxIn(COutPoint(0xdead_beef, 0)))
        missing.vout.append(CTxOut(0, CScript([OP_RETURN])))
        missing.calc_sha256()

        height = node.getblockcount() + 1
        block = create_block(
            int(node.getbestblockhash(), 16),
            create_coinbase(height),
            txlist=[big_tx, missing],
        )
        add_witness_commitment(block)
        block.solve()

        self.log.info("Submitting; ConnectBlock must reject it and stay alive")
        assert_equal(node.submitblock(block.serialize().hex()),
                     "bad-txns-inputs-missingorspent")

        # If the node died the next call raises rather than returning.
        assert_equal(node.getblockcount(), height - 1)
        self.log.info("Node survived and the chain is unchanged")


if __name__ == '__main__':
    Cve202452911Test().main()
