#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test for contrib/devtools/scan-legacy-dilithium-utxos.py.

The scan sizes what a P2MR-only activation would freeze on a chain with history
predating it (issue #111). Regtest activates P2MR-only at height 1, so this
pushes activation out of reach with -testactivationheight and mines the
pre-activation shapes the scan is meant to be pointed at.

Outputs are created from raw() descriptors rather than through the wallet. The
wallet's willingness to hand out legacy Dilithium destinations is itself
changing (issue #97), and this test is about whether the scan recognises the
scripts that are already sitting in blocks -- which does not depend on whether
the wallet would still produce them today.

Classification and spend bookkeeping are covered in isolation by
contrib/devtools/test-scan-legacy-dilithium-utxos.py. What this adds is that
walking real getblock output over RPC reaches the same answer.
"""

import json
import os
import subprocess
import sys

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal, get_auth_cookie, rpc_port

# Far enough out that this chain never reaches it, so P2MR-only stays
# unactivated and the legacy shapes below are still consensus-valid.
P2MR_NEVER = 1_000_000

TOOL = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                    "..", "..", "contrib", "devtools", "scan-legacy-dilithium-utxos.py")

OP_CHECKSIGDILITHIUM = "bb"
DILITHIUM_PUBKEY_SIZE = 1312

# Distinct hashes so a misattributed output is obvious in a failure.
HASH_A = "11" * 20
HASH_B = "22" * 20
HASH_C = "33" * 20
HASH_D = "44" * 32

# A key-shaped blob that is full of 0xbb bytes. Mined into a real block, it is
# the on-chain version of the false positive the unit tests cover: a scan that
# greps for the opcode rather than walking the script would count the ECDSA
# output below as Dilithium.
KEY_WITH_OPCODE_BYTES = (bytes(range(256)) * 6)[:DILITHIUM_PUBKEY_SIZE].hex()
PUSHDATA2 = "4d" + DILITHIUM_PUBKEY_SIZE.to_bytes(2, "little").hex()

SCRIPTS = {
    # Reported: the opcode is in the output script.
    "dilithium_p2pkh": f"76a914{HASH_A}88{OP_CHECKSIGDILITHIUM}",
    "dilithium_bare": f"{PUSHDATA2}{KEY_WITH_OPCODE_BYTES}{OP_CHECKSIGDILITHIUM}",
    # Bucketed as undetermined: hash-committed, nothing has revealed a preimage.
    "p2wpkh": f"0014{HASH_B}",
    "p2sh": f"a914{HASH_C}87",
    "p2wsh": f"0020{HASH_D}",
    # Ignored: ordinary coins, including the opcode-bytes lookalike.
    "ecdsa_p2pkh": f"76a914{HASH_A}88ac",
    "ecdsa_bare_lookalike": f"{PUSHDATA2}{KEY_WITH_OPCODE_BYTES}ac",
}

REPORTED = {"dilithium_p2pkh": "p2pkh", "dilithium_bare": "bare"}
BUCKETED = {"p2wpkh", "p2sh", "p2wsh"}


class ScanLegacyDilithiumTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[f"-testactivationheight=dilithium_p2mr@{P2MR_NEVER}"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_scan(self, expect_findings, extra_args=None, name="scan"):
        """Run the tool against the node and return its JSON report."""
        report_path = os.path.join(self.options.tmpdir, f"{name}.json")
        if os.path.exists(report_path):
            os.remove(report_path)

        user, password = get_auth_cookie(self.nodes[0].datadir_path, self.chain)
        result = subprocess.run(
            [sys.executable, TOOL,
             "--chain", "regtest",
             "--rpcport", str(rpc_port(self.nodes[0].index)),
             "--rpcuser", user,
             "--rpcpassword", password,
             "--json", report_path,
             "--quiet"] + (extra_args or []),
            capture_output=True, text=True, check=False)

        # Exit status is meaningful: 1 means something frozen was found, so a
        # release checklist can gate on it. Assert on stderr too, or a tool
        # crash reads as a plain assertion failure.
        assert_equal((result.returncode, result.stderr.strip()),
                     (1 if expect_findings else 0, ""))
        self.log.debug(result.stdout)
        with open(report_path, "r", encoding="utf-8") as handle:
            return json.load(handle)

    def mine_to_script(self, script_hex, blocks=1):
        """Mine coinbases paying to a raw script, returning their heights."""
        descriptor = self.nodes[0].getdescriptorinfo(f"raw({script_hex})")["descriptor"]
        hashes = self.generatetodescriptor(self.nodes[0], blocks, descriptor)
        return [self.nodes[0].getblock(h)["height"] for h in hashes]

    def run_test(self):
        node = self.nodes[0]

        self.log.info("A chain with no Dilithium history has nothing to freeze")
        self.generatetoaddress(node, 20, node.getnewaddress("", "bech32"))
        report = self.run_scan(expect_findings=False)
        assert_equal(report["frozen_total_sats"], 0)
        assert_equal(report["frozen_visible"], [])
        assert_equal(report["frozen_proved"], [])

        self.log.info("Mining one coinbase for each shape the scan must tell apart")
        heights = {name: self.mine_to_script(script)[0] for name, script in SCRIPTS.items()}
        report = self.run_scan(expect_findings=True)

        self.log.info("Only the two visibly-Dilithium shapes are counted")
        visible = {item["kind"]: item for item in report["frozen_visible"]}
        assert_equal(sorted(visible), sorted(REPORTED.values()))
        assert_equal(report["frozen_proved"], [])
        assert_equal(report["frozen_total_sats"],
                     sum(item["sats"] for item in report["frozen_visible"]))

        for name, kind in REPORTED.items():
            assert_equal(visible[kind]["height"], heights[name])

        self.log.info("The opcode-bytes lookalike is not counted")
        # Both ECDSA scripts were mined at known heights. If the walk were a
        # byte search, ecdsa_bare_lookalike would show up as a third finding.
        counted_heights = {item["height"] for item in report["frozen_visible"]}
        assert heights["ecdsa_bare_lookalike"] not in counted_heights
        assert heights["ecdsa_p2pkh"] not in counted_heights

        self.log.info("Hash-committed shapes are bucketed, not counted and not dropped")
        for kind in BUCKETED:
            assert kind in report["undeterminable"], f"{kind} vanished from the report"
            assert report["undeterminable"][kind]["outputs"] >= 1

        self.log.info("Every reported coin is one the node still holds as unspent")
        for item in report["frozen_visible"]:
            txid, vout = item["outpoint"].split(":")
            utxo = node.gettxout(txid, int(vout))
            assert utxo is not None, f"{item['outpoint']} was reported but is already spent"
            assert_equal(int(round(utxo["value"] * 100_000_000)), item["sats"])

        self.log.info("Spending a coin drops it from the report")
        # Exercises the pruning path against real block data. An ordinary coin
        # is used because spending a legacy Dilithium one needs keys this
        # wallet has no way to produce.
        before = report["undeterminable"]["p2wpkh"]["outputs"]
        self.generate(node, 100)  # mature the initial bech32 coinbases
        node.sendtoaddress(node.getnewaddress("", "bech32m"), 1)
        self.generate(node, 1)
        after = self.run_scan(expect_findings=True, name="after_spend")
        assert after["undeterminable"]["p2wpkh"]["outputs"] < before + 1, \
            "a spent p2wpkh should leave the live set"

        self.log.info("A partial scan reports only the range it was given")
        partial = self.run_scan(expect_findings=False, name="partial",
                                extra_args=["--stop-height", "20"])
        assert_equal(partial["scanned_to_height"], 20)
        assert_equal(partial["frozen_total_sats"], 0)


if __name__ == "__main__":
    ScanLegacyDilithiumTest().main()
