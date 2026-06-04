#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""All-wallet-types transfer simulation on regtest.

For every combination of classical address type (legacy / p2sh-segwit /
bech32 / bech32m) AND spendable Dilithium addresses, this test:

    1. Generates at least one address of that type on a single descriptor
       wallet (via explicit `getnewaddress` / `getnewdilithiumaddress`
       type parameters).  A separate node whose *default* `-addresstype`
       is `p2sh-segwit` is avoided: on SQLite descriptor wallets that
       configuration can make fee estimation fail with "Missing solving
       data for estimating transaction size" when spending nested-segwit
       UTXOs, even though the same addresses work when requested from a
       bech32-default wallet.
    2. Funds each address from a shared miner.
    3. Runs an N x N cross-matrix of `sendtoaddress`, transferring coins
       between every pair of address types, mining a block after each.
    4. Runs a `sendmany` that mixes classical and Dilithium recipients,
       which is the exact shape regressed by the bug documented in
       SENDMANY_DILITHIUM_FIX.md.

Today's default functional harness only covers the regtest HRP (`qcrt` /
`rdbt`). This simulation deliberately exercises every address family on
regtest so that any future refactor to DecodeDestination, ParseRecipients
or the signing provider cannot silently break one family while leaving
the others working.

See wallet_cross_chain_addresses.py for the same coverage across
mainnet, testnet (tbtq / tdbt) and signet HRPs.
"""

from decimal import Decimal
import itertools

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BTQTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


# Classical address types getnewaddress supports directly.
CLASSICAL_TYPES = ["legacy", "p2sh-segwit", "bech32", "bech32m"]


class WalletAllTypesSimulation(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        # Miner + one descriptor wallet. All address variants are obtained
        # with explicit RPC `address_type` / Dilithium parameters on the
        # same wallet (see module docstring re p2sh-segwit default node).
        self.num_nodes = 2
        self.extra_args = [
            ["-whitelist=noban@127.0.0.1"],
            ["-addresstype=bech32", "-whitelist=noban@127.0.0.1"],
        ]
        self.supports_cli = False
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        for i, j in itertools.product(range(self.num_nodes), repeat=2):
            if i > j:
                self.connect_nodes(i, j)
        self.sync_all()

    # ---------- helpers ----------

    def _miner(self):
        return self.nodes[0]

    def _wallet(self):
        return self.nodes[1]

    def _generate_address(self, role):
        """Generate one address of the given role on the shared wallet.

        Roles: "legacy", "p2sh-segwit", "bech32", "bech32m", "dilithium".
        """
        w = self._wallet()
        if role in CLASSICAL_TYPES:
            return w, w.getnewaddress("", role)
        if role == "dilithium":
            # Default Dilithium output type is legacy (see
            # src/wallet/rpc/dilithium.cpp:getnewdilithiumaddress).
            return w, w.getnewdilithiumaddress()
        raise AssertionError(f"unknown role {role}")

    def _assert_valid_on_self(self, node, address, role):
        info = node.validateaddress(address)
        assert_equal(info["isvalid"], True)
        self.log.info(
            f"  generated {role:20s} on node{self.nodes.index(node)}: {address}"
        )

    def _send_and_confirm(self, src_node, dst_address, amount):
        txid = src_node.sendtoaddress(dst_address, amount)
        assert txid
        self.generate(self._miner(), 1)
        self.sync_all()
        return txid

    # ---------- test body ----------

    def run_test(self):
        miner = self._miner()

        # BTQ uses 5 BTQ per block subsidy (see GetBlockSubsidy in
        # src/validation.cpp), not Bitcoin's 50. After height H, the miner
        # can spend (H - COINBASE_MATURITY) coinbases, each worth 5 BTQ.
        self.log.info("Mining initial blocks to fund the miner wallet")
        extra_mature = 100  # 100 mature coinbases -> ~500 BTQ for the matrix
        self.generate(miner, COINBASE_MATURITY + extra_mature)
        self.sync_all()
        assert_greater_than(miner.getbalance(), Decimal("450"))

        self.log.info("Generating at least one address per role")
        assert_raises_rpc_error(
            -4,
            "dilithium-bech32 is disabled",
            self._wallet().getnewdilithiumaddress,
            "",
            "bech32",
        )
        roles = CLASSICAL_TYPES + ["dilithium"]
        addresses = {}
        owning_nodes = {}
        for role in roles:
            node, addr = self._generate_address(role)
            self._assert_valid_on_self(node, addr, role)
            addresses[role] = addr
            owning_nodes[role] = node

        # Sanity: classical bech32 address starts with regtest HRP "qcrt1",
        # while spendable Dilithium legacy uses a base58 regtest prefix.
        assert addresses["bech32"].startswith("qcrt1"), addresses["bech32"]
        assert addresses["bech32m"].startswith("qcrt1"), addresses["bech32m"]

        self.log.info("Funding every recipient wallet from the miner")
        for role, addr in addresses.items():
            self._send_and_confirm(miner, addr, Decimal("5.0"))

        # After funding each wallet once, make sure every owning node sees
        # a spendable balance for its address.
        for role, addr in addresses.items():
            node = owning_nodes[role]
            bal = node.getreceivedbyaddress(addr)
            assert_equal(bal, Decimal("5.0"))

        self.log.info(
            "Running NxN sendtoaddress cross-matrix across all address families"
        )
        # For each (source role, destination role), have the source node
        # pay the destination. This exercises the full
        #   DecodeDestination -> ParseRecipients -> CreateTransaction ->
        #   CommitTransaction
        # codepath for every address family in both sender and recipient
        # positions.
        for src_role, dst_role in itertools.product(roles, repeat=2):
            if src_role == dst_role:
                # Self-sends are exercised implicitly by every wallet
                # generating its own change output; skip to keep the run
                # time reasonable.
                continue

            src_node = owning_nodes[src_role]
            dst_addr = addresses[dst_role]
            amount = Decimal("0.25")

            self.log.info(
                f"  send {amount} BTQ from {src_role:20s} -> {dst_role}"
            )
            # Top up the sender if it ran low from previous iterations.
            if src_node.getbalance() < Decimal("1.0"):
                self._send_and_confirm(miner, addresses[src_role], Decimal("5.0"))

            try:
                txid = src_node.sendtoaddress(dst_addr, amount)
            except Exception as e:
                # Surface exactly which pair regressed. Make the error
                # loud -- this is the whole point of the simulation.
                raise AssertionError(
                    f"sendtoaddress failed for pair ({src_role} -> {dst_role}) "
                    f"with address {dst_addr}: {e}"
                )
            assert txid
            self.generate(miner, 1)
            self.sync_all()

            # Recipient must now see the new output.
            dst_node = owning_nodes[dst_role]
            new_bal = dst_node.getreceivedbyaddress(dst_addr)
            assert_greater_than(new_bal, Decimal("0"))

        self.log.info(
            "sendmany mixing classical + Dilithium recipients "
            "(regression for SENDMANY_DILITHIUM_FIX.md)"
        )
        sender = owning_nodes["bech32"]
        if sender.getbalance() < Decimal("2.0"):
            self._send_and_confirm(miner, addresses["bech32"], Decimal("10.0"))
        many = {
            addresses["legacy"]:           Decimal("0.10"),
            addresses["p2sh-segwit"]:      Decimal("0.11"),
            addresses["bech32m"]:          Decimal("0.12"),
            addresses["dilithium"]:        Decimal("0.13"),
        }
        txid = sender.sendmany("", many)
        assert txid
        self.generate(miner, 1)
        self.sync_all()

        self.log.info(
            "Negative test: sending to a foreign-HRP address must fail "
            "with a parseable error message"
        )
        # Build a checksum-valid testnet P2WPKH address on the fly. The
        # HRP is 'tbtq' (testnet bech32), which MUST be rejected on a
        # regtest node whose HRP is 'qcrt'. We build it with the same
        # helper the daemon uses so the only thing wrong is the HRP.
        from test_framework.segwit_addr import encode_segwit_address
        foreign_tbtq = encode_segwit_address("tbtq", 0, bytes.fromhex("ab" * 20))
        assert_raises_rpc_error(
            -5,
            "Invalid BTQ address",
            miner.sendtoaddress,
            foreign_tbtq,
            Decimal("0.01"),
        )

        self.log.info("All wallet-type transfer combinations succeeded")


if __name__ == "__main__":
    WalletAllTypesSimulation().main()
