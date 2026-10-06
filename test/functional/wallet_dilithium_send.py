#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression tests for wallet sends/funding with Dilithium P2MR UTXOs."""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletDilithiumSendTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def dilithium_address(self, wallet, label=""):
        created = wallet.getnewdilithiumaddress(label)
        assert isinstance(created, dict), created
        return created["address"]

    def run_test(self):
        node = self.nodes[0]

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        node.createwallet(wallet_name="repro", descriptors=True)
        repro = node.get_wallet_rpc("repro")

        self.log.info("Descriptor Dilithium P2MR keys remain usable after wallet encryption")
        dilithium_address = self.dilithium_address(repro)
        msg = "descriptor encrypted dilithium signing"
        sig = repro.signmessagewithdilithium(dilithium_address, msg)
        assert repro.verifydilithiumsignature(msg, dilithium_address, sig)
        # verifymessagewithdilithium takes (address, signature, message), mirroring
        # verifymessage; verifydilithiumsignature is the deprecated reordered form.
        assert repro.verifymessagewithdilithium(dilithium_address, sig, msg)
        assert not repro.verifymessagewithdilithium(dilithium_address, sig, msg + "!")
        # Passing the deprecated order to the new RPC must fail loudly, never
        # silently verify: the message is rejected where an address is expected.
        assert_raises_rpc_error(
            -5,
            "Address is not a Dilithium key address",
            repro.verifymessagewithdilithium,
            msg,
            dilithium_address,
            sig,
        )
        # F2.1: with an explicit pubkey, verification needs neither the private
        # key nor the signer's wallet metadata.
        created = repro.getnewdilithiumaddress()
        pubkey_hex = repro.getdilithiumpubkey(created["p2mr_id"])["pubkeys"][0]["pubkey"]
        pk_sig = repro.signmessagewithdilithium(created["address"], msg)
        assert funding.verifymessagewithdilithium(created["address"], pk_sig, msg, pubkey_hex)
        assert not funding.verifymessagewithdilithium(created["address"], pk_sig, msg + "!", pubkey_hex)
        # A pubkey that does not belong to the address is rejected, not verified.
        other = repro.getnewdilithiumaddress()
        other_pubkey = repro.getdilithiumpubkey(other["p2mr_id"])["pubkeys"][0]["pubkey"]
        assert_raises_rpc_error(
            -5,
            "Dilithium public key does not match the address",
            funding.verifymessagewithdilithium,
            created["address"],
            pk_sig,
            msg,
            other_pubkey,
        )

        repro.encryptwallet("pass")
        assert_raises_rpc_error(
            -13,
            "Please enter the wallet passphrase with walletpassphrase first",
            repro.signmessagewithdilithium,
            dilithium_address,
            msg,
        )
        repro.walletpassphrase("pass", 100000)
        sig = repro.signmessagewithdilithium(dilithium_address, msg)
        assert repro.verifydilithiumsignature(msg, dilithium_address, sig)

        self.log.info("Fund repro wallet with confirmed Dilithium P2MR and bech32m UTXOs")
        taproot_address = repro.getnewaddress(address_type="bech32m")
        # A second Dilithium UTXO so sendmany can be exercised independently.
        dilithium_address2 = self.dilithium_address(repro)
        funding.sendtoaddress(dilithium_address, Decimal("10"))
        funding.sendtoaddress(dilithium_address2, Decimal("10"))
        funding.sendtoaddress(taproot_address, Decimal("10"))
        self.generate(node, 1)

        utxos = repro.listunspent()
        assert_equal(len(utxos), 3)

        dilithium_utxo = next(utxo for utxo in utxos if utxo["address"] == dilithium_address)
        dilithium_utxo2 = next(utxo for utxo in utxos if utxo["address"] == dilithium_address2)
        taproot_utxo = next(utxo for utxo in utxos if utxo["address"] == taproot_address)

        info = repro.getaddressinfo(dilithium_address)
        assert info["solvable"]
        assert info["isdilithium"]
        assert info["ismine"]

        raw = repro.createrawtransaction(
            [{"txid": dilithium_utxo["txid"], "vout": dilithium_utxo["vout"]}],
            [{repro.getnewaddress(): Decimal("0.01")}],
        )
        funded = repro.fundrawtransaction(raw, {"add_inputs": False})
        assert funded["hex"]
        assert funded["fee"] > 0

        self.log.info("sendtoaddress must return a valid, broadcastable tx spending the Dilithium P2MR UTXO")
        # Lock every other input so coin selection is forced to spend the Dilithium UTXO,
        # exercising the OP_CHECKSIGDILITHIUM P2MR signing path (regression for issue #41).
        repro.lockunspent(False, [
            {"txid": taproot_utxo["txid"], "vout": taproot_utxo["vout"]},
            {"txid": dilithium_utxo2["txid"], "vout": dilithium_utxo2["vout"]},
        ])
        destination = repro.getnewaddress()
        txid = repro.sendtoaddress(destination, Decimal("0.01"))
        assert txid
        assert txid in node.getrawmempool()

        # The Dilithium UTXO must actually be the input that was spent.
        spent_outpoints = {(vin["txid"], vin["vout"]) for vin in node.getrawtransaction(txid, True)["vin"]}
        assert (dilithium_utxo["txid"], dilithium_utxo["vout"]) in spent_outpoints

        # Crux of issue #41: the RPC reported success AND the tx is consensus-valid.
        # Mining it proves the Dilithium signature verifies, not merely that it relayed.
        self.generate(node, 1)
        assert_equal(repro.gettransaction(txid)["confirmations"], 1)
        unspent_after = {(u["txid"], u["vout"]) for u in repro.listunspent(0)}
        assert (dilithium_utxo["txid"], dilithium_utxo["vout"]) not in unspent_after

        self.log.info("sendmany must also spend a Dilithium P2MR UTXO and broadcast a valid tx")
        repro.lockunspent(True)
        # Leave only the second Dilithium UTXO spendable.
        others = [
            {"txid": u["txid"], "vout": u["vout"]}
            for u in repro.listunspent(0)
            if (u["txid"], u["vout"]) != (dilithium_utxo2["txid"], dilithium_utxo2["vout"])
        ]
        repro.lockunspent(False, others)
        many_txid = repro.sendmany("", {
            repro.getnewaddress(): Decimal("0.2"),
            self.dilithium_address(repro): Decimal("0.3"),
        })
        assert many_txid in node.getrawmempool()
        many_spent = {(vin["txid"], vin["vout"]) for vin in node.getrawtransaction(many_txid, True)["vin"]}
        assert (dilithium_utxo2["txid"], dilithium_utxo2["vout"]) in many_spent
        self.generate(node, 1)
        assert_equal(repro.gettransaction(many_txid)["confirmations"], 1)

        # F2.5: the Dilithium sequence has its own counter, so ECDSA address
        # generation must not create gaps in it, and a scan of indexes 0..N
        # must find every Dilithium key the wallet ever derived.
        self.log.info("recoverdilithiumkeys scans the dedicated Dilithium sequence")
        node.createwallet(wallet_name="seq", descriptors=True)
        seq = node.get_wallet_rpc("seq")
        # Dilithium derivation hangs off the LEGACY descriptor, so advance
        # that one: with the old shared counter these calls created the gaps.
        for _ in range(5):
            seq.getnewaddress(address_type="legacy")
        first = seq.getnewdilithiumaddress()["address"]
        for _ in range(5):
            seq.getnewaddress(address_type="legacy")
        second = seq.getnewdilithiumaddress()["address"]

        scan = seq.recoverdilithiumkeys(0, 9)
        assert_equal(len(scan), 10)
        # Indexes 0 and 1 are the two keys above; the ECDSA calls in between
        # left no holes.
        assert_equal(scan[0]["address"], first)
        assert_equal(scan[1]["address"], second)
        assert_equal(scan[0]["recovered"], False)
        assert_equal(scan[1]["recovered"], False)
        assert all(entry["recovered"] for entry in scan[2:])
        # Idempotent: a second scan recovers nothing new.
        rescan = seq.recoverdilithiumkeys(0, 9)
        assert not any(entry["recovered"] for entry in rescan)
        # Scanned-ahead keys are already materialized, so the next new address
        # skips past them rather than reusing one.
        assert seq.getnewdilithiumaddress()["address"] not in {e["address"] for e in scan}
        # The internal (change) sequence is distinct from the external one.
        internal_scan = seq.recoverdilithiumkeys(0, 9, True)
        assert not (
            {entry["address"] for entry in internal_scan}
            & {entry["address"] for entry in scan}
        )
        assert_raises_rpc_error(-8, "Invalid index range", seq.recoverdilithiumkeys, 5, 4)
        assert_raises_rpc_error(-8, "Index range too large", seq.recoverdilithiumkeys, 0, 20000)
        # A recovered address is spendable: fund it and send the coins onward.
        self.generate(node, 1)
        fund_txid = repro.sendtoaddress(scan[3]["address"], Decimal("1.0"))
        self.generate(node, 1)
        assert fund_txid in {u["txid"] for u in seq.listunspent()}
        spend_txid = seq.sendtoaddress(repro.getnewaddress(), Decimal("0.5"))
        assert spend_txid in node.getrawmempool()
        self.generate(node, 1)
        assert_equal(seq.gettransaction(spend_txid)["confirmations"], 1)

        # F2.4: change follows the inputs. A preselected P2MR input paying a
        # classical recipient must return its change to a P2MR script.
        self.log.info("P2MR inputs force P2MR change")
        change_addr = self.dilithium_address(repro)
        funding.sendtoaddress(change_addr, Decimal("5"))
        self.generate(node, 1)
        p2mr_utxo = next(u for u in repro.listunspent() if u["address"] == change_addr)
        raw = repro.createrawtransaction(
            [{"txid": p2mr_utxo["txid"], "vout": p2mr_utxo["vout"]}],
            [{repro.getnewaddress(): Decimal("0.5")}],
        )
        funded = repro.fundrawtransaction(raw, {"add_inputs": False})
        decoded = repro.decoderawtransaction(funded["hex"])
        change_out = decoded["vout"][funded["changepos"]]
        assert_equal(change_out["scriptPubKey"]["type"], "witness_v2_p2mr")
        # An explicit changetype still wins.
        funded = repro.fundrawtransaction(raw, {"add_inputs": False, "change_type": "bech32m"})
        decoded = repro.decoderawtransaction(funded["hex"])
        change_out = decoded["vout"][funded["changepos"]]
        assert_equal(change_out["scriptPubKey"]["type"], "witness_v1_taproot")

        # Automatically selected P2MR inputs force P2MR change too. The seq
        # wallet holds only P2MR coins, so sendtoaddress must auto-select one.
        auto_txid = seq.sendtoaddress(repro.getnewaddress(), Decimal("0.1"))
        auto_decoded = node.getrawtransaction(auto_txid, True)
        auto_types = [v["scriptPubKey"]["type"] for v in auto_decoded["vout"]]
        assert_equal(sorted(auto_types), ["witness_v0_keyhash", "witness_v2_p2mr"])
        self.generate(node, 1)

        # F2.4: a quantum_only wallet refuses to mint ECDSA destinations.
        self.log.info("quantum_only wallets refuse ECDSA addresses")
        node.createwallet(wallet_name="quantum", descriptors=True)
        quantum = node.get_wallet_rpc("quantum")
        assert_equal(quantum.setwalletflag("quantum_only")["flag_state"], True)
        assert_raises_rpc_error(-12, "quantum-only", quantum.getnewaddress)
        assert_raises_rpc_error(-12, "quantum-only", quantum.getrawchangeaddress)
        assert quantum.getnewdilithiumaddress()["address"]
        assert quantum.getnewaddress(address_type="p2mr")
        # Unsetting the flag restores classical minting.
        quantum.setwalletflag("quantum_only", False)
        assert quantum.getnewaddress()

        # After a reload every Dilithium key record lands in an arbitrary
        # manager, so generation must check the whole wallet before storing:
        # a plaintext wallet otherwise errors on every already-materialized
        # index, an encrypted one silently re-issues an address.
        self.log.info("Dilithium sequence skips materialized keys after restart")
        issued = {e["address"] for e in seq.recoverdilithiumkeys(0, 14)}
        issued |= {e["address"] for e in seq.recoverdilithiumkeys(0, 14, True)}
        self.restart_node(0)
        node = self.nodes[0]
        node.loadwallet("seq")
        seq = node.get_wallet_rpc("seq")
        assert seq.getnewdilithiumaddress()["address"] not in issued
        # Both sequences stay intact: rescans recover nothing new and the
        # next index extends the scan.
        assert not any(e["recovered"] for e in seq.recoverdilithiumkeys(0, 14))
        assert not any(e["recovered"] for e in seq.recoverdilithiumkeys(0, 14, True))
        assert_equal(seq.recoverdilithiumkeys(15, 15, True)[0]["recovered"], True)

        self.log.info("Dilithium sequence skips materialized keys after encryption")
        node.createwallet(wallet_name="enc", descriptors=True)
        enc = node.get_wallet_rpc("enc")
        enc_first = enc.getnewdilithiumaddress()["address"]
        enc.encryptwallet("pass")
        enc.walletpassphrase("pass", 600)
        # Encrypting rotates the active descriptors to a new seed, so the
        # active Dilithium sequence starts over. The pre-encryption key is
        # already materialized in the wallet and needs no recovery.
        enc_scan = enc.recoverdilithiumkeys(0, 4)
        assert all(e["recovered"] for e in enc_scan)
        assert enc_first not in {e["address"] for e in enc_scan}
        assert enc.getaddressinfo(enc_first)["ismine"]
        # Reload, then generate: the encrypted store path overwrites instead
        # of failing, so a missed skip would hand out a duplicate address.
        node.unloadwallet("enc")
        node.loadwallet("enc")
        enc = node.get_wallet_rpc("enc")
        enc.walletpassphrase("pass", 600)
        assert enc.getnewdilithiumaddress()["address"] not in {e["address"] for e in enc_scan}

        # Recovered P2MR scripts belong to no descriptor; the block-filter
        # fast rescan must still see them or rescanblockchain silently
        # misses the blocks that pay them.
        self.log.info("Recovered P2MR scripts are found by block-filter rescans")
        self.restart_node(0, ["-blockfilterindex=1"])
        node = self.nodes[0]
        self.wait_until(lambda: node.getindexinfo()["basic block filter index"]["synced"])
        node.loadwallet("seq")
        node.loadwallet("funding")
        seq = node.get_wallet_rpc("seq")
        funding = node.get_wallet_rpc("funding")
        target = seq.recoverdilithiumkeys(20, 20)[0]["address"]
        node.unloadwallet("seq")
        filter_txid = funding.sendtoaddress(target, Decimal("2.0"))
        self.generate(node, 1)
        # The rescan consults the filter only for blocks the index has
        # covered; wait so the funding block is filtered, not fallback-read.
        self.wait_until(lambda: node.getindexinfo()["basic block filter index"]["synced"])
        node.loadwallet("seq")
        seq = node.get_wallet_rpc("seq")
        seq.rescanblockchain(node.getblockcount() - 2)
        assert filter_txid in {u["txid"] for u in seq.listunspent()}


if __name__ == "__main__":
    WalletDilithiumSendTest().main()
