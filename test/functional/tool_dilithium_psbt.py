#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Drive a 2-of-3 Dilithium P2MR spend through contrib/btq/dilithium-psbt.sh.

The Python functional tests already cover the RPCs. This file exists so the
documented CLI wrapper cannot drift from those RPCs unnoticed: it creates
three wallets, registers the same 2-of-3, and spends via the script's
create / sign / combine / finalize commands, never calling those RPCs itself.
"""

import json
import os
import subprocess

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal


class ToolDilithiumPSBTTest(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_cli()
        self.skip_if_no_wallet()

    def script_cmd(self, *args, stdin=None):
        script = os.path.join(self.config["environment"]["SRCDIR"], "contrib", "btq", "dilithium-psbt.sh")
        cli = f"{self.nodes[0].cli.binary} -datadir={self.nodes[0].cli.datadir}"
        cmd = [script, f"--cli={cli}", *args]
        self.log.debug(" ".join(cmd))
        return subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
            input=stdin,
        )

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="funding", descriptors=True)
        funding = node.get_wallet_rpc("funding")
        self.generatetoaddress(node, 110, funding.getnewaddress())

        for name in ("alice", "bob", "carol"):
            node.createwallet(wallet_name=name, descriptors=True)

        self.log.info("pubkey publishes one Dilithium key per wallet")
        keys = []
        for name in ("alice", "bob", "carol"):
            out = json.loads(self.script_cmd("pubkey", name).stdout)
            assert_equal(out["wallet"], name)
            assert_equal(out["ismine"], True)
            assert_equal(len(out["pubkey"]), 2624)  # 1312-byte key, hex
            keys.append(out["pubkey"])

        self.log.info("every co-signer registers the same 2-of-3")
        addresses = []
        for name in ("alice", "bob", "carol"):
            out = json.loads(self.script_cmd("register", name, "2", *keys).stdout)
            assert_equal(out["signers_available"], 1)
            addresses.append(out["address"])
        assert_equal(len(set(addresses)), 1)
        address = addresses[0]

        self.log.info("fund the multisig and build an unsigned PSBT")
        funding.sendtoaddress(address, Decimal("10"))
        self.generate(node, 1)
        utxo = next(u for u in node.get_wallet_rpc("alice").listunspent() if u["address"] == address)
        dest = funding.getnewaddress()
        unsigned = self.script_cmd(
            "create", "alice", f"{utxo['txid']}:{utxo['vout']}", dest, "4",
        ).stdout.strip()
        status = json.loads(self.script_cmd("status", unsigned).stdout)
        assert_equal(status["status"], "unsigned")
        assert_equal(status["signatures"], 0)
        assert_equal(status["required"], 2)

        self.log.info("alice and carol sign independently, then combine")
        alice_psbt = self.script_cmd("sign", "alice", unsigned).stdout.strip()
        carol_psbt = self.script_cmd("sign", "carol", unsigned).stdout.strip()
        assert_equal(json.loads(self.script_cmd("status", alice_psbt).stdout)["signatures"], 1)
        combined = self.script_cmd("combine", alice_psbt, carol_psbt).stdout.strip()
        combined_status = json.loads(self.script_cmd("status", combined).stdout)
        assert_equal(combined_status["status"], "finalizable")
        assert_equal(combined_status["signatures"], 2)

        self.log.info("finalize, broadcast, confirm")
        raw = self.script_cmd("finalize", combined).stdout.strip()
        txid = self.script_cmd("broadcast", raw).stdout.strip()
        self.generate(node, 1)
        assert_equal(node.get_wallet_rpc("alice").gettransaction(txid)["confirmations"], 1)

        self.log.info("one signature is not enough to finalize")
        funding.sendtoaddress(address, Decimal("6"))
        self.generate(node, 1)
        utxo2 = next(
            u for u in node.get_wallet_rpc("alice").listunspent()
            if u["address"] == address and u["amount"] == Decimal("6")
        )
        unsigned2 = self.script_cmd(
            "create", "alice", f"{utxo2['txid']}:{utxo2['vout']}", dest, "2",
        ).stdout.strip()
        once = self.script_cmd("sign", "alice", unsigned2).stdout.strip()
        proc = subprocess.run(
            [
                os.path.join(self.config["environment"]["SRCDIR"], "contrib", "btq", "dilithium-psbt.sh"),
                f"--cli={self.nodes[0].cli.binary} -datadir={self.nodes[0].cli.datadir}",
                "finalize",
                once,
            ],
            capture_output=True,
            text=True,
        )
        assert_equal(proc.returncode, 2)
        assert "not complete" in proc.stderr


if __name__ == "__main__":
    ToolDilithiumPSBTTest().main()
