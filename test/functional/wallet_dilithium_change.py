#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Change from a Dilithium send must stay quantum-safe (issues #76 and #97).

A wallet that pays a Dilithium destination and returns the remainder of the
input to a Taproot change output has moved most of the balance back under a
Schnorr key. The user asked for quantum safety by choosing the recipient; the
change is the same money and gets no such protection. This exercises the
end-to-end RPC behaviour, including the escape hatches that must keep working.
"""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error


class WalletDilithiumChangeTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Node 1 keeps DEPLOYMENT_DILITHIUM_P2MR unscheduled, which is testnet's
        # configuration today and the only one where base58 Dilithium payments
        # are still consensus-spendable.
        self.extra_args = [
            [],
            ["-testactivationheight=dilithium_p2mr@2147483647"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def change_output(self, node, wallet, txid):
        """The single output of txid that the wallet owns and treats as change."""
        tx = node.getrawtransaction(txid, True)
        changes = []
        for out in tx["vout"]:
            addresses = out["scriptPubKey"].get("address")
            if addresses is None:
                continue
            info = wallet.getaddressinfo(addresses)
            if info["ismine"] and info.get("ischange", False):
                changes.append(out)
        assert_equal(len(changes), 1)
        return changes[0]

    def run_test(self):
        node = self.nodes[0]
        self.disconnect_nodes(0, 1)

        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress(), sync_fun=self.no_op)

        node.createwallet(wallet_name="sender", descriptors=True)
        sender = node.get_wallet_rpc("sender")
        funding.sendtoaddress(sender.getnewaddress(address_type="bech32m"), Decimal("40"))
        self.generate(node, 1, sync_fun=self.no_op)

        self.log.info("p2mr is a first-class address type for getnewaddress and getrawchangeaddress")
        p2mr_address = sender.getnewaddress(address_type="p2mr")
        assert_equal(sender.getaddressinfo(p2mr_address)["ismine"], True)
        assert_equal(node.validateaddress(p2mr_address)["isvalid"], True)
        raw_change = sender.getrawchangeaddress("p2mr")
        assert_equal(sender.getaddressinfo(raw_change)["ischange"], True)
        assert raw_change != p2mr_address

        self.log.info("Paying a Dilithium destination returns the change to P2MR, not Taproot")
        recipient = sender.getnewdilithiumaddress()["address"]
        # The swap reserves a classical change key, then replaces the script.
        # That reservation has to go back to the pool. Keeping it burns an
        # internal index that never appears in the transaction.
        internal_before = sender.getwalletinfo()["keypoolsize_hd_internal"]
        txid = sender.sendtoaddress(recipient, Decimal("1"))
        assert_equal(sender.getwalletinfo()["keypoolsize_hd_internal"], internal_before)
        change = self.change_output(node, sender, txid)
        assert_equal(change["scriptPubKey"]["type"], "witness_v2_p2mr")
        # The point of the change being P2MR rather than merely witness v2: the
        # wallet must still be able to spend it.
        assert_equal(sender.getaddressinfo(change["scriptPubKey"]["address"])["solvable"], True)
        assert_greater_than(change["value"], Decimal("1"))
        self.generate(node, 1, sync_fun=self.no_op)

        self.log.info("The P2MR change is spendable, so the balance did not become stranded")
        sender.lockunspent(False, [
            {"txid": u["txid"], "vout": u["vout"]}
            for u in sender.listunspent()
            if u["address"] != change["scriptPubKey"]["address"]
        ])
        spend_txid = sender.sendtoaddress(funding.getnewaddress(), Decimal("0.5"))
        assert spend_txid in node.getrawmempool()
        self.generate(node, 1, sync_fun=self.no_op)
        assert_equal(sender.gettransaction(spend_txid)["confirmations"], 1)
        sender.lockunspent(True)

        self.log.info("An auto-selected P2MR input forces P2MR change too")
        # By now the sender owns only P2MR coins, so sendtoaddress has to
        # pick one; the change must not fall back to a classical script.
        auto_txid = sender.sendtoaddress(funding.getnewaddress(address_type="bech32m"), Decimal("1"))
        auto_change = self.change_output(node, sender, auto_txid)
        assert_equal(auto_change["scriptPubKey"]["type"], "witness_v2_p2mr")
        self.generate(node, 1, sync_fun=self.no_op)

        self.log.info("A classical send is unaffected")
        # The sender holds only P2MR coins, so give it a fresh classical one
        # and preselect it; only then does the input-following rule stay out.
        classical_addr = sender.getnewaddress(address_type="bech32m")
        funding.sendtoaddress(classical_addr, Decimal("3"))
        self.generate(node, 1, sync_fun=self.no_op)
        classical_utxo = next(u for u in sender.listunspent() if u["address"] == classical_addr)
        # Same reservation, no swap: the classical change key stays used.
        internal_before = sender.getwalletinfo()["keypoolsize_hd_internal"]
        raw = sender.createrawtransaction(
            [{"txid": classical_utxo["txid"], "vout": classical_utxo["vout"]}],
            [{funding.getnewaddress(address_type="bech32m"): Decimal("1")}],
        )
        funded = sender.fundrawtransaction(raw, {"add_inputs": False})
        classical_change = sender.decoderawtransaction(funded["hex"])["vout"][funded["changepos"]]
        assert classical_change["scriptPubKey"]["type"] != "witness_v2_p2mr"
        assert_equal(sender.getwalletinfo()["keypoolsize_hd_internal"], internal_before - 1)

        self.log.info("An explicit change_type still wins over the Dilithium rule")
        outputs = [{sender.getnewdilithiumaddress()["address"]: Decimal("1")}]
        funded = sender.walletcreatefundedpsbt(
            inputs=[], outputs=outputs, options={"change_type": "bech32m"}
        )
        decoded = node.decodepsbt(funded["psbt"])["tx"]
        change_vout = decoded["vout"][funded["changepos"]]
        assert_equal(change_vout["scriptPubKey"]["type"], "witness_v1_taproot")

        self.log.info("A wallet with no private keys keeps classical change instead of failing to fund")
        node.createwallet(wallet_name="watchonly", descriptors=True, disable_private_keys=True)
        watchonly = node.get_wallet_rpc("watchonly")
        watchonly.importdescriptors([
            {
                "desc": d["desc"],
                "timestamp": "now",
                "active": d["active"],
                "internal": d.get("internal", False),
            }
            for d in sender.listdescriptors()["descriptors"]
        ])
        funding.sendtoaddress(watchonly.getnewaddress(address_type="bech32m"), Decimal("5"))
        self.generate(node, 1, sync_fun=self.no_op)
        assert_greater_than(watchonly.getbalances()["mine"]["trusted"], Decimal("2"))

        psbt = watchonly.walletcreatefundedpsbt(
            inputs=[],
            outputs=[{sender.getnewdilithiumaddress()["address"]: Decimal("1")}],
        )
        watch_change = node.decodepsbt(psbt["psbt"])["tx"]["vout"][psbt["changepos"]]
        assert_equal(watch_change["scriptPubKey"]["type"], "witness_v1_taproot")

        self.log.info("A locked wallet keeps classical change instead of failing to fund")
        node.createwallet(wallet_name="locked", descriptors=True)
        locked = node.get_wallet_rpc("locked")
        locked_p2mr = locked.getnewaddress(address_type="p2mr")
        funding.sendtoaddress(locked_p2mr, Decimal("5"))
        self.generate(node, 1, sync_fun=self.no_op)
        locked.encryptwallet("pass")
        locked_utxo = next(u for u in locked.listunspent() if u["address"] == locked_p2mr)
        classical_out = [{funding.getnewaddress(address_type="bech32m"): Decimal("1")}]
        # While locked, deriving a Dilithium change key is impossible; the
        # funding must still work and the change stays classical.
        psbt = locked.walletcreatefundedpsbt(
            inputs=[{"txid": locked_utxo["txid"], "vout": locked_utxo["vout"]}],
            outputs=classical_out,
            options={"add_inputs": False},
        )
        locked_change = node.decodepsbt(psbt["psbt"])["tx"]["vout"][psbt["changepos"]]
        assert locked_change["scriptPubKey"]["type"] != "witness_v2_p2mr"
        # Paying a P2MR recipient while locked used to fail the funding: the
        # change type followed the recipient and minting it needs the seed.
        # Same fallback as the input case.
        p2mr_out = [{funding.getnewaddress(address_type="p2mr"): Decimal("1")}]
        psbt = locked.walletcreatefundedpsbt(
            inputs=[{"txid": locked_utxo["txid"], "vout": locked_utxo["vout"]}],
            outputs=p2mr_out,
            options={"add_inputs": False},
        )
        locked_p2mr_change = node.decodepsbt(psbt["psbt"])["tx"]["vout"][psbt["changepos"]]
        assert locked_p2mr_change["scriptPubKey"]["type"] != "witness_v2_p2mr"
        # Unlocked, the same funding returns quantum-safe change.
        locked.walletpassphrase("pass", 600)
        psbt = locked.walletcreatefundedpsbt(
            inputs=[{"txid": locked_utxo["txid"], "vout": locked_utxo["vout"]}],
            outputs=classical_out,
            options={"add_inputs": False},
        )
        unlocked_change = node.decodepsbt(psbt["psbt"])["tx"]["vout"][psbt["changepos"]]
        assert_equal(unlocked_change["scriptPubKey"]["type"], "witness_v2_p2mr")

        self.log.info("Swapped change is sized against P2MR viability, not classical")
        node.createwallet(wallet_name="boundary", descriptors=True)
        boundary = node.get_wallet_rpc("boundary")
        boundary_value = Decimal("1.0")
        funding.sendtoaddress(boundary.getnewaddress(address_type="p2mr"), boundary_value)
        self.generate(node, 1, sync_fun=self.no_op)
        classical_dest = funding.getnewaddress(address_type="bech32m")
        # Probe the fee for this shape of transaction, then aim the change at
        # chosen targets. Auto-selection must pick the only (P2MR) coin.
        probe = boundary.walletcreatefundedpsbt(
            inputs=[], outputs=[{classical_dest: Decimal("0.5")}], options={"fee_rate": 10},
        )
        for target_sat, must_drop in ((600, True), (20000, False)):
            amount = boundary_value - probe["fee"] - Decimal(target_sat) / Decimal(100_000_000)
            res = boundary.walletcreatefundedpsbt(
                inputs=[], outputs=[{classical_dest: amount}], options={"fee_rate": 10},
            )
            if must_drop:
                # Above classical viability but below the real cost of ever
                # spending a P2MR output: dropped to fees, never left as a
                # classical crumb.
                assert_equal(res["changepos"], -1)
            else:
                # Far above the viability floor: the change must exist, and
                # it must be quantum-safe. A conditional check here would
                # silently pass if the wallet wrongly dropped it to fees.
                assert res["changepos"] != -1
                change_out = node.decodepsbt(res["psbt"])["tx"]["vout"][res["changepos"]]
                assert_equal(change_out["scriptPubKey"]["type"], "witness_v2_p2mr")

        self.log.info("dilithium-legacy is refused where Dilithium spends must be P2MR")
        # Regtest activates DEPLOYMENT_DILITHIUM_P2MR at height 1, so a base58
        # Dilithium address is not a valid payment destination and the wallet
        # cannot spend one. Handing it out strands whatever is paid to it.
        assert_raises_rpc_error(
            -12, "dilithium-legacy is disabled on this network",
            sender.getnewaddress, "", "dilithium-legacy",
        )
        # getnewdilithiumaddress refuses every non-P2MR type outright, on every
        # chain, so a Dilithium receive is always the spendable kind.
        assert_raises_rpc_error(
            -5, "Dilithium receives must use P2MR",
            sender.getnewdilithiumaddress, "", "dilithium-legacy",
        )

        self.log.info("...and still allowed where P2MR is unscheduled, as on testnet")
        unscheduled = self.nodes[1]
        unscheduled.createwallet(wallet_name="legacy_ok", descriptors=True)
        legacy_ok = unscheduled.get_wallet_rpc("legacy_ok")
        legacy_address = legacy_ok.getnewaddress("", "dilithium-legacy")
        # Valid on this chain is the whole point of the distinction: the guard
        # keys on whether the destination can be paid at all, and here it can.
        assert_equal(unscheduled.validateaddress(legacy_address)["isvalid"], True)
        assert_equal(legacy_ok.getaddressinfo(legacy_address)["isdilithium"], True)

        self.log.info("-changetype wins over the input-following rule")
        self.restart_node(0, ["-changetype=bech32m"])
        node = self.nodes[0]
        node.loadwallet("funding")
        node.loadwallet("boundary")
        funding = node.get_wallet_rpc("funding")
        boundary = node.get_wallet_rpc("boundary")
        res = boundary.walletcreatefundedpsbt(
            inputs=[], outputs=[{funding.getnewaddress(address_type="bech32m"): Decimal("0.5")}],
        )
        pinned_change = node.decodepsbt(res["psbt"])["tx"]["vout"][res["changepos"]]
        assert_equal(pinned_change["scriptPubKey"]["type"], "witness_v1_taproot")


if __name__ == "__main__":
    WalletDilithiumChangeTest().main()
