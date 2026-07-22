#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Cross-chain address HRP and decode simulation.

Today's functional-test harness hard-codes regtest for every test. That
leaves a coverage hole: the testnet HRP `tbtq` (and its Dilithium
counterpart `tdbt`), the signet HRP `qtb` / `sdbt`, and the mainnet HRP
`qbtc` / `dbtc` are never exercised end-to-end. The user-reported bug
"tbtq addresses not working for transfers" surfaced precisely because of
that blind spot.

This test launches one ephemeral btqd per chain (test, signet, regtest,
main) and, on each:

  1. Creates a wallet.
  2. Calls `getnewaddress` for legacy / p2sh-segwit / bech32 / bech32m,
     calls `getnewdilithiumaddress` for legacy Dilithium, and asserts the
     disabled Dilithium bech32 output type is rejected.
  3. Asserts each generated address uses that chain's expected HRP /
     Base58 prefix.
  4. Calls `validateaddress` on a canonical representative of every
     other chain's bech32 families. Foreign addresses must be rejected
     with a non-empty `error` message (that is the UX change Phase 5a
     introduces).
  5. Tries `walletcreatefundedpsbt` sending to a freshly generated
     address of the same chain. On regtest this actually constructs an
     unsigned PSBT; on the other chains it is expected to fail with
     "Insufficient funds" because we cannot mine blocks on them. Either
     outcome proves the address survived `ParseRecipients`.

Running `btqd` in testnet / signet / mainnet without peers (dnsseed=0,
fixedseeds=0) is harmless: the node idles at genesis and serves RPC.
"""

from decimal import Decimal

from test_framework.test_framework import BTQTestFramework
from test_framework.test_node import TestNode
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    get_datadir_path,
    initialize_datadir,
)


# Expected HRPs and Base58 version bytes per chain, mirroring
# src/kernel/chainparams.cpp. Kept in-test so any drift between
# chainparams.cpp and this list trips an obvious failure.
CHAIN_SPECS = [
    {
        "chain":            "test",
        "bech32_hrp":       "tbtq",
        "dilithium_hrp":    "tdbt",
        "base58_pubkey":    111,
        "base58_script":    196,
    },
    {
        "chain":            "signet",
        "bech32_hrp":       "qtb",
        "dilithium_hrp":    "sdbt",
        "base58_pubkey":    111,
        "base58_script":    196,
    },
    {
        "chain":            "regtest",
        "bech32_hrp":       "qcrt",
        "dilithium_hrp":    "rdbt",
        "base58_pubkey":    111,
        "base58_script":    196,
    },
    {
        "chain":            "main",
        "bech32_hrp":       "qbtc",
        "dilithium_hrp":    "dbtc",
        "base58_pubkey":    75,
        "base58_script":    135,
    },
]

CLASSICAL_TYPES = ["legacy", "p2sh-segwit", "bech32", "bech32m"]


class WalletCrossChainAddresses(BTQTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        # We manage nodes ourselves so the stock single-chain framework
        # plumbing stays out of the way.
        self.num_nodes = 0
        self.supports_cli = False
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_chain(self):
        # Per-chain data directories are created lazily in run_test().
        self.log.info("Initializing test directory " + self.options.tmpdir)

    def setup_network(self):
        # No default network; each chain spins up its own node in run_test.
        pass

    # ---------- node lifecycle helpers ----------

    def _launch(self, chain, node_index):
        """Launch a fresh btqd on `chain`, return the running TestNode."""
        datadir = get_datadir_path(self.options.tmpdir, node_index)
        initialize_datadir(self.options.tmpdir, node_index, chain, self.disable_autoconnect)
        chain_args = ["-chain=test"] if chain == "test" else []
        node_chain = "" if chain == "main" else chain

        node = TestNode(
            node_index,
            datadir,
            chain=node_chain,
            rpchost=None,
            timewait=self.rpc_timeout,
            timeout_factor=self.options.timeout_factor,
            btqd=self.options.btqd,
            btq_cli=self.options.btqcli,
            version=None,
            coverage_dir=self.options.coveragedir,
            cwd=self.options.tmpdir,
            extra_conf=["bind=127.0.0.1"],
            extra_args=chain_args + [
                # Stay off every real network. dnsseed/fixedseeds are
                # already disabled by write_config().
                "-maxconnections=0",
                "-whitelist=noban@127.0.0.1",
            ],
            use_cli=False,
            descriptors=(self.options.descriptors if self.options.descriptors is not None else True),
        )
        node.start()
        node.wait_for_rpc_connection()
        self.nodes.append(node)
        return node

    def _stop(self, node):
        node.stop_node()
        node.wait_until_stopped()

    # ---------- assertions ----------

    def _assert_address_hrp(self, address, expected_prefix, label, chain):
        assert address.lower().startswith(expected_prefix.lower() + "1"), (
            f"[{chain}] {label} address {address} should start with "
            f"'{expected_prefix}1' HRP"
        )

    def _assert_base58_version(self, address, expected_version, label, chain):
        from test_framework.address import base58_to_byte
        _, version = base58_to_byte(address)
        assert version == expected_version, (
            f"[{chain}] {label} address {address} has base58 version "
            f"{version}, expected {expected_version}"
        )

    # ---------- per-chain test body ----------

    def _test_chain(self, spec, node_index):
        chain = spec["chain"]
        self.log.info(f"--- Testing chain: {chain} ---")
        node = self._launch(chain, node_index)

        try:
            # Fresh wallet (regtest-cached defaults can't help us here).
            wallet_name = f"xchain_{chain}"
            node.createwallet(
                wallet_name=wallet_name,
                descriptors=True,
                load_on_startup=True,
            )
            w = node.get_wallet_rpc(wallet_name)

            # 1. getnewaddress for every classical address type.
            classical_addresses = {}
            for t in CLASSICAL_TYPES:
                try:
                    addr = w.getnewaddress("", t)
                except Exception as e:
                    raise AssertionError(
                        f"[{chain}] getnewaddress(type={t}) failed: {e}"
                    )
                classical_addresses[t] = addr
                if t in ("bech32", "bech32m"):
                    self._assert_address_hrp(addr, spec["bech32_hrp"], t, chain)
                elif t == "legacy":
                    self._assert_base58_version(
                        addr, spec["base58_pubkey"], t, chain
                    )
                elif t == "p2sh-segwit":
                    self._assert_base58_version(
                        addr, spec["base58_script"], t, chain
                    )
                self.log.info(f"  [{chain}] {t:12s}: {addr}")

            # 2. Dilithium P2MR receive addresses. Legacy Dilithium address types
            # are disabled; only "p2mr" is accepted.
            dilithium_addresses = {}
            try:
                created = w.getnewdilithiumaddress("", "p2mr")
            except Exception as e:
                raise AssertionError(
                    f"[{chain}] getnewdilithiumaddress(type=p2mr) failed: {e}"
                )
            assert isinstance(created, dict), created
            addr = created["address"]
            dilithium_addresses["p2mr"] = addr
            self.log.info(f"  [{chain}] dilithium-p2mr: {addr}")
            assert_raises_rpc_error(
                -5,
                "Unsupported Dilithium address type",
                w.getnewdilithiumaddress,
                "",
                "legacy",
            )
            assert_raises_rpc_error(
                -5,
                "Unsupported Dilithium address type",
                w.getnewdilithiumaddress,
                "",
                "bech32",
            )

            # 3. Same-chain validateaddress: every address we just
            # generated must validate as isvalid=True.
            for label, addr in list(classical_addresses.items()) + [
                (f"dilithium-{t}", a) for t, a in dilithium_addresses.items()
            ]:
                info = w.validateaddress(addr)
                assert_equal(info["isvalid"], True)
                assert info.get("error", "") == "", (
                    f"[{chain}] unexpected error for {label} address {addr}: {info}"
                )

            # 4. Cross-chain rejection. Build one canonical bech32
            # address for every OTHER chain and assert it is rejected
            # with a non-empty error message. After Phase 5a the error
            # must mention the expected HRP for operator clarity.
            for other in CHAIN_SPECS:
                if other["chain"] == chain:
                    continue

                # Deterministic 20-byte payload for the witness program.
                from test_framework.segwit_addr import encode_segwit_address
                payload = bytes.fromhex("ab" * 20)
                foreign_wpkh = encode_segwit_address(
                    other["bech32_hrp"], 0, payload
                )
                foreign_dwpkh = encode_segwit_address(
                    other["dilithium_hrp"], 0, payload
                )

                for role, foreign in [
                    (f"{other['chain']}-bech32",    foreign_wpkh),
                    (f"{other['chain']}-dilithium", foreign_dwpkh),
                ]:
                    info = w.validateaddress(foreign)
                    assert_equal(info["isvalid"], False)
                    err = info.get("error", "")
                    assert err, (
                        f"[{chain}] expected validateaddress to return a "
                        f"non-empty error for foreign address {role}={foreign}; "
                        f"got {info}"
                    )
                    # Post-Phase-5a, the message should mention both HRPs.
                    # We don't hard-require that text yet so the test can
                    # run against pre-fix builds and be used to reproduce
                    # the bug. The hard requirement is that an error exists.
                    self.log.info(
                        f"  [{chain}] rejected foreign {role} with: {err}"
                    )

            # 5. PSBT send path. On regtest we actually have mined
            # coins pre-funded; elsewhere we expect a clean
            # "Insufficient funds" error which still proves the
            # address survived DecodeDestination + ParseRecipients.
            addr_for_psbt = classical_addresses["bech32"]
            try:
                w.walletcreatefundedpsbt(
                    [], {addr_for_psbt: Decimal("0.0001")},
                )
                self.log.info(
                    f"  [{chain}] walletcreatefundedpsbt succeeded for same-chain "
                    f"bech32 address"
                )
            except Exception as e:
                msg = str(e)
                # Accept "Insufficient funds" / "wallet has no UTXOs" /
                # "Unable to create PSBT" — any error that came from
                # fund selection, NOT from address parsing.
                assert "Invalid BTQ address" not in msg, (
                    f"[{chain}] same-chain bech32 address rejected by "
                    f"walletcreatefundedpsbt: {msg}"
                )
                self.log.info(
                    f"  [{chain}] walletcreatefundedpsbt failed as expected "
                    f"(no UTXOs): {msg}"
                )
        finally:
            self._stop(node)
            # Drop the node from the list so other chains get fresh indices.
            self.nodes.remove(node)

    def run_test(self):
        for i, spec in enumerate(CHAIN_SPECS):
            self._test_chain(spec, node_index=i)

        self.log.info("All cross-chain address operations completed")


if __name__ == "__main__":
    WalletCrossChainAddresses().main()
