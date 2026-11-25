#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Restore custom P2MR trees on descriptor wallets via listp2mr / importp2mr."""

import copy
from decimal import Decimal

from test_framework.script import (
    CScript,
    LEAF_VERSION_TAPSCRIPT,
    OP_CHECKSIG,
    OP_CHECKSIGDILITHIUM,
    OP_CHECKSIGDILITHIUMVERIFY,
)
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletP2MRBackupTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-acceptnonstdtxn=1", "-blockfilterindex=1", "-deprecatedrpc=create_bdb"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def test_plain_hybrid_multileaf(self, node, source):
        """Plain, hybrid, and multi-leaf trees survive importp2mr. A seed scan does not rebuild the last two."""
        help_text = node.help("recoverdilithiumkeys")
        assert "Seed-only recovery does not rebuild hybrid or multi-leaf trees." in help_text
        assert "Seed-only recovery does not rebuild hybrid or multi-leaf trees." in node.help("dumpwallet")
        assert "Seed-only recovery does not rebuild hybrid or multi-leaf trees." in node.help("importwallet")
        assert "Seed-only recovery does not rebuild hybrid or multi-leaf trees." in node.help("importp2mr")

        plain_a = source.getnewdilithiumaddress("plain-a")
        plain_b = source.getnewdilithiumaddress("plain-b")
        info = source.getaddressinfo(source.getnewaddress())
        pubkey = bytes.fromhex(info["pubkey"])
        assert pubkey[0] in (2, 3)
        xonly = pubkey[1:]

        def dilithium_pubkey(script_hex):
            items = list(CScript(bytes.fromhex(script_hex)))
            assert_equal(items[1], OP_CHECKSIGDILITHIUM)
            return items[0]

        def leaf(depth, script):
            raw = script if isinstance(script, str) else script.hex()
            return {"depth": depth, "leaf_version": LEAF_VERSION_TAPSCRIPT, "script": raw}

        plain_a_info = source.getp2mrinfo(plain_a["p2mr_id"])
        plain_b_info = source.getp2mrinfo(plain_b["p2mr_id"])
        key_a = dilithium_pubkey(plain_a_info["tree"][0]["script"])
        hybrid_script = CScript([key_a, OP_CHECKSIGDILITHIUMVERIFY, xonly, OP_CHECKSIG])
        hybrid = source.getnewp2mraddress([leaf(0, hybrid_script)], "hybrid-pair", True)
        multi = source.getnewp2mraddress([
            leaf(1, plain_a_info["tree"][0]["script"]),
            leaf(1, plain_b_info["tree"][0]["script"]),
        ], "multi-leaf")
        assert hybrid["address"] != plain_a["address"]
        assert multi["address"] != plain_a["address"]
        assert hybrid["address"] != multi["address"]

        source.sendtoaddress(plain_a["address"], Decimal("0.50"))
        source.sendtoaddress(hybrid["address"], Decimal("0.40"))
        source.sendtoaddress(multi["address"], Decimal("0.60"))
        self.generate(node, 1)

        wanted = {
            plain_a["address"]: plain_a,
            hybrid["address"]: hybrid,
            multi["address"]: multi,
        }
        exported = []
        for entry in source.listp2mr():
            if entry["address"] in wanted:
                exported.append(entry)
        assert_equal(len(exported), 3)

        self.log.info("importp2mr restores plain, hybrid, and multi-leaf addresses")
        node.createwallet(wallet_name="shapes_meta", descriptors=True)
        meta = node.get_wallet_rpc("shapes_meta")
        imported = meta.importp2mr(exported)
        assert_equal(len(imported), 3)
        for entry in exported:
            info = meta.getaddressinfo(entry["address"])
            assert_equal(info["ismine"], False)
            assert_equal(info["iswatchonly"], True)
            assert_equal(meta.getreceivedbyaddress(entry["address"]), {
                plain_a["address"]: Decimal("0.50"),
                hybrid["address"]: Decimal("0.40"),
                multi["address"]: Decimal("0.60"),
            }[entry["address"]])
            restored = meta.getp2mrinfo(next(row["p2mr_id"] for row in imported if row["address"] == entry["address"]))
            assert_equal(restored["merkle_root"], entry["merkle_root"])
            assert_equal(restored["scriptPubKey"], entry["scriptPubKey"])
            assert_equal(restored["tree"], entry["tree"])

        again = meta.importp2mr(exported)
        assert_equal(sorted(row["address"] for row in again), sorted(entry["address"] for entry in exported))
        assert_equal(len([e for e in meta.listp2mr() if e["address"] in wanted]), 3)

        node.unloadwallet("shapes_meta")
        node.loadwallet("shapes_meta")
        meta = node.get_wallet_rpc("shapes_meta")
        reloaded = {e["address"]: e for e in meta.listp2mr() if e["address"] in wanted}
        assert_equal(set(reloaded), set(wanted))
        for address, entry in reloaded.items():
            original = next(e for e in exported if e["address"] == address)
            assert_equal(entry["merkle_root"], original["merkle_root"])
            assert_equal(entry["tree"], original["tree"])

        self.log.info("Seed recovery rebuilds the single-leaf addresses and not the other trees")
        node.createwallet(wallet_name="shapes_seed", blank=True, descriptors=True)
        seed = node.get_wallet_rpc("shapes_seed")
        requests = []
        for desc in source.listdescriptors(True)["descriptors"]:
            req = {
                "desc": desc["desc"],
                "timestamp": "now",
                "internal": desc["internal"],
                "active": desc["active"],
            }
            # listdescriptors can report next_index one past the inclusive
            # range end once the keypool has been topped up. importdescriptors
            # rejects that pair, so extend the range to include the index.
            if "range" in desc:
                begin, end = desc["range"]
                next_index = desc.get("next_index", begin)
                if next_index > end:
                    end = next_index
                req["range"] = [begin, end]
                req["next_index"] = next_index
            requests.append(req)
        imported_desc = seed.importdescriptors(requests)
        for row in imported_desc:
            assert_equal(row.get("error"), None)
        scanned = {row["address"] for row in seed.recoverdilithiumkeys(0, 5)}
        assert plain_a["address"] in scanned
        assert plain_b["address"] in scanned
        assert hybrid["address"] not in scanned
        assert multi["address"] not in scanned
        seed_listed = {e["address"] for e in seed.listp2mr()}
        assert hybrid["address"] not in seed_listed
        assert multi["address"] not in seed_listed

        seeded = seed.importp2mr(exported)
        by_address = {row["address"]: row["p2mr_id"] for row in seeded}
        for address in (plain_a["address"], multi["address"]):
            info = seed.getaddressinfo(address)
            assert_equal(info["ismine"], True)
        hybrid_info = seed.getaddressinfo(hybrid["address"])
        assert_equal(hybrid_info["ismine"], False)
        assert_equal(hybrid_info["iswatchonly"], True)

        # A wallet created blank stays blank after importdescriptors, so it
        # cannot derive its own P2MR change output. Hand it one.
        change = source.getnewdilithiumaddress()["address"]
        for address, amount in ((plain_a["address"], Decimal("0.20")), (multi["address"], Decimal("0.20"))):
            spend = seed.createp2mrspend(by_address[address], source.getnewaddress(), amount, None, change)
            signed = seed.signp2mrtransaction(spend["hex"], by_address[address])
            assert signed["complete"]
            txid = seed.sendrawtransaction(signed["hex"])
            self.generate(node, 1)
            assert_equal(seed.gettransaction(txid, True)["confirmations"], 1)

        self.log.info("A bad tree in a batch writes nothing")
        node.createwallet(wallet_name="shapes_reject", descriptors=True)
        reject = node.get_wallet_rpc("shapes_reject")
        good = copy.deepcopy(next(e for e in exported if e["address"] == hybrid["address"]))
        bad = copy.deepcopy(good)
        bad["merkle_root"] = "00" * 32
        assert_raises_rpc_error(-8, "merkle_root does not match", reject.importp2mr, [good, bad])
        assert_equal(reject.listp2mr(), [])
        malformed = {"tree": [{"depth": 0, "leaf_version": LEAF_VERSION_TAPSCRIPT, "script": "zz"}]}
        assert_raises_rpc_error(-8, "valid hex", reject.importp2mr, [malformed])
        incomplete = {"tree": [{"depth": 1, "leaf_version": LEAF_VERSION_TAPSCRIPT, "script": "51"}]}
        assert_raises_rpc_error(-8, "invalid P2MR tree", reject.importp2mr, [incomplete])
        oversized = {"tree": [{"depth": 1, "leaf_version": LEAF_VERSION_TAPSCRIPT, "script": "51"} for _ in range(129)]}
        assert_raises_rpc_error(-8, "too many leaves", reject.importp2mr, [oversized])
        assert_equal(reject.listp2mr(), [])
        restored_good = reject.importp2mr([good])
        assert_equal(restored_good[0]["address"], hybrid["address"])

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="source", descriptors=True)
        source = node.get_wallet_rpc("source")
        self.generatetoaddress(node, 110, source.getnewaddress())
        self.test_plain_hybrid_multileaf(node, source)

        tree = [{
            "depth": 0,
            "leaf_version": LEAF_VERSION_TAPSCRIPT,
            "script": "51",
        }]
        funded = source.sendtop2mr(tree, Decimal("1.0"), "custom-vault", allow_trivial_leaves=True)
        self.generate(node, 1)
        exported = source.listp2mr()
        funded_entry = next(e for e in exported if e["id"] == funded["p2mr_id"])

        self.log.info("Restore the custom tree on a fresh descriptor wallet")
        node.createwallet(wallet_name="restored", descriptors=True)
        restored = node.get_wallet_rpc("restored")
        # Change for a P2MR send is another P2MR, so listp2mr is not just the funded tree.
        restore_entry = copy.deepcopy(funded_entry)
        del restore_entry["created_at"]
        later_entry = {
            "tree": [{
                "depth": 0,
                "leaf_version": LEAF_VERSION_TAPSCRIPT,
                "script": "52",
            }],
            "label": "later-entry",
            "created_at": node.getblockheader(node.getbestblockhash())["time"] + 1000,
        }
        imported = restored.importp2mr([restore_entry, later_entry])
        assert_equal(len(imported), 2)
        restored_funded = next(row for row in imported if row["address"] == funded["address"])

        spend = restored.createp2mrspend(restored_funded["p2mr_id"], source.getnewaddress(), Decimal("0.4"))
        signed = restored.signp2mrtransaction(spend["hex"], restored_funded["p2mr_id"])
        assert signed["complete"]
        txid = restored.sendrawtransaction(signed["hex"])
        self.generate(node, 1)
        assert_equal(restored.gettransaction(txid, True)["confirmations"], 1)

        self.log.info("Reject the whole batch when a later entry is invalid")
        node.createwallet(wallet_name="atomic", descriptors=True)
        atomic = node.get_wallet_rpc("atomic")
        bad_entry = copy.deepcopy(later_entry)
        bad_entry["merkle_root"] = 1
        assert_raises_rpc_error(-8, "merkle_root must be a string", atomic.importp2mr, [later_entry, bad_entry])
        assert_equal(atomic.listp2mr(), [])

        self.log.info("Preserve an explicit zero creation timestamp")
        node.createwallet(wallet_name="zero_time", descriptors=True)
        zero_time = node.get_wallet_rpc("zero_time")
        zero_entry = copy.deepcopy(later_entry)
        zero_entry["created_at"] = 0
        zero_imported = zero_time.importp2mr([zero_entry])
        zero_metadata = zero_time.getp2mrinfo(zero_imported[0]["p2mr_id"])
        assert_equal(zero_metadata["created_at"], 0)

        if self.is_bdb_compiled():
            self.log.info("importwallet rescans from genesis when created_at is 0")
            funded_zero = source.sendtop2mr(tree, Decimal("0.5"), "zero-birth", allow_trivial_leaves=True)
            self.generate(node, 1)
            # RescanFromTime looks back TIMESTAMP_WINDOW (15 min). Move the tip
            # past that so a rescan that starts at the tip cannot see this output.
            # Key birth times in a full dumpwallet would also widen the range, so
            # the imported file is p2mr records only.
            chain_time = node.getblockheader(node.getbestblockhash())["time"]
            node.setmocktime(chain_time + 16 * 60)
            self.generate(node, 1)

            node.createwallet(wallet_name="legacy_zero", descriptors=False)
            legacy_zero = node.get_wallet_rpc("legacy_zero")
            zero_dump_entries = [e for e in source.listp2mr() if e["id"] == funded_zero["p2mr_id"]]
            assert_equal(len(zero_dump_entries), 1)
            zero_dump_entries[0]["created_at"] = 0
            imported_zero = legacy_zero.importp2mr(zero_dump_entries)
            dump_path = node.datadir_path / "p2mr_zero.dump"
            legacy_zero.dumpwallet(str(dump_path))
            p2mr_only = node.datadir_path / "p2mr_zero_only.dump"
            with open(dump_path, encoding="utf-8") as src, open(p2mr_only, "w", encoding="utf-8") as dst:
                for line in src:
                    if line.startswith("p2mr ") or line.startswith("#"):
                        dst.write(line)
            node.createwallet(wallet_name="legacy_import", descriptors=False)
            legacy_import = node.get_wallet_rpc("legacy_import")
            legacy_import.importwallet(str(p2mr_only))
            utxos = legacy_import.listunspent(0, 9999999, [imported_zero[0]["address"]])
            assert_equal(len(utxos), 1)


if __name__ == "__main__":
    WalletP2MRBackupTest().main()
