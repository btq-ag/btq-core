#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Find coins that DEPLOYMENT_DILITHIUM_P2MR would freeze on this chain.

Once P2MR-only activates, a Dilithium opcode is consensus-valid only inside a
P2MR (witness v2) tapscript leaf, and the witness-v0 routing that sent a
1312-byte key in a P2WPKH-shaped spend to OP_CHECKSIGDILITHIUM is switched off.
Every coin that can only be spent through one of the old paths becomes
permanently unspendable at the flag day. Not policy-rejected: consensus-invalid,
with no recovery afterwards. See issue #111.

This exists to answer, with evidence, what such an activation would cost on a
given chain. Run it before choosing an activation height, and publish the
result: "we scanned and found none" is a materially different launch story from
not having looked.


WHAT CAN AND CANNOT BE FOUND

Some of these coins announce themselves and some are indistinguishable from
ordinary ones. The difference is not a limitation of this tool, it is a property
of the chain, and it determines how much assurance a clean scan is worth.

Visible in the output script, so found exactly:

  bare      <pubkey> OP_CHECKSIGDILITHIUM
  p2pkh     OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIGDILITHIUM

Committed behind a hash, so found only once something reveals the preimage:

  p2wpkh    OP_0 <20-byte hash>        -- byte-identical to an ECDSA P2WPKH
  p2sh      OP_HASH160 <hash> OP_EQUAL
  p2wsh     OP_0 <32-byte hash>

For the hash-committed kinds the scan works backwards from spends. A witness-v0
keyhash spend carrying a 1312-byte public key, or a redeem/witness script
containing a Dilithium opcode, proves what the hash it satisfied stood for. Any
coin still sitting on a hash proved that way is reported. This finds reused
addresses, which in practice is where a pool payout or treasury balance lives.

What it cannot find is a hash-committed coin whose preimage has never appeared
on chain -- a fresh Dilithium P2WPKH address paid once and never spent from is,
by construction, not distinguishable from an ECDSA one. The residual is reported
as a bounded unknown rather than folded into a total, because the honest reading
of a clean run is "no evidence of exposure", not "no exposure". Closing that gap
needs wallet-side enumeration by whoever holds the keys, which is why issue #111
asks for pool and treasury addresses to be checked directly.


USAGE

  scan-legacy-dilithium-utxos.py --datadir ~/.btq-testnet --chain test
  scan-legacy-dilithium-utxos.py --rpcuser u --rpcpassword p --rpcport 18332
  scan-legacy-dilithium-utxos.py ... --json report.json

Exit status is 0 when the scan completes and finds nothing frozen, 1 when it
finds something, and 2 on error, so it can gate a release checklist.
"""

import argparse
import base64
import http.client
import json
import os
import sys
import time
from collections import defaultdict

# A Dilithium2 public key. The witness-v0 routing keys on this exact length.
DILITHIUM_PUBKEY_SIZE = 1312

OP_CHECKSIGDILITHIUM = 0xBB
OP_CHECKSIGDILITHIUMVERIFY = 0xBC
OP_CHECKMULTISIGDILITHIUM = 0xBD
OP_CHECKMULTISIGDILITHIUMVERIFY = 0xBE
DILITHIUM_OPCODES = frozenset({
    OP_CHECKSIGDILITHIUM,
    OP_CHECKSIGDILITHIUMVERIFY,
    OP_CHECKMULTISIGDILITHIUM,
    OP_CHECKMULTISIGDILITHIUMVERIFY,
})

OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4 = 0x4C, 0x4D, 0x4E

# Kinds that carry a Dilithium opcode in the clear.
VISIBLE_KINDS = ("bare", "p2pkh")
# Kinds that commit to one behind a hash.
HASHED_KINDS = ("p2wpkh", "p2sh", "p2wsh")


class RpcError(Exception):
    pass


class Rpc:
    """Minimal batching JSON-RPC client over one keep-alive connection."""

    def __init__(self, host, port, auth, timeout=600):
        self._conn = http.client.HTTPConnection(host, port, timeout=timeout)
        self._headers = {
            "Content-Type": "application/json",
            "Authorization": "Basic " + base64.b64encode(auth.encode()).decode(),
        }
        self._id = 0

    def call(self, method, *params):
        return self.batch([(method, list(params))])[0]

    def batch(self, calls):
        payload = []
        for method, params in calls:
            self._id += 1
            payload.append({"jsonrpc": "2.0", "id": self._id, "method": method, "params": params})

        self._conn.request("POST", "/", json.dumps(payload), self._headers)
        response = self._conn.getresponse()
        body = response.read()
        if response.status not in (200, 500):
            raise RpcError(f"HTTP {response.status} {response.reason}: {body[:200]!r}")

        decoded = json.loads(body)
        if isinstance(decoded, dict):
            decoded = [decoded]
        by_id = {item["id"]: item for item in decoded}
        results = []
        for item_id in sorted(by_id):
            item = by_id[item_id]
            if item.get("error"):
                raise RpcError(f"{item['error']}")
            results.append(item["result"])
        return results


def iter_opcodes(script):
    """Yield each opcode in a script, skipping over pushed data.

    Walking the script rather than searching the bytes matters: a Dilithium
    public key is 1312 bytes of arbitrary data and will contain 0xbb about five
    times over. Only an opcode in an executable position counts.
    """
    i, n = 0, len(script)
    while i < n:
        op = script[i]
        i += 1
        if op < OP_PUSHDATA1:
            i += op
        elif op == OP_PUSHDATA1:
            if i >= n:
                return
            i += 1 + script[i]
        elif op == OP_PUSHDATA2:
            if i + 1 >= n:
                return
            i += 2 + int.from_bytes(script[i:i + 2], "little")
        elif op == OP_PUSHDATA4:
            if i + 3 >= n:
                return
            i += 4 + int.from_bytes(script[i:i + 4], "little")
        else:
            yield op
        if i > n:
            return


def has_dilithium_opcode(script):
    return any(op in DILITHIUM_OPCODES for op in iter_opcodes(script))


def classify(script):
    """Return (kind, commitment_hex) for a script this scan cares about.

    commitment_hex is the hash a coin is parked behind, or None when the script
    says outright what it is.
    """
    n = len(script)
    if n == 22 and script[0] == 0x00 and script[1] == 0x14:
        return "p2wpkh", script[2:].hex()
    if n == 34 and script[0] == 0x00 and script[1] == 0x20:
        return "p2wsh", script[2:].hex()
    if n == 23 and script[0] == 0xA9 and script[1] == 0x14 and script[22] == 0x87:
        return "p2sh", script[2:22].hex()
    if not has_dilithium_opcode(script):
        return None, None
    if n == 25 and script[0] == 0x76 and script[1] == 0xA9 and script[2] == 0x14 and script[23] == 0x88:
        return "p2pkh", None
    return "bare", None


def to_sats(value):
    return int(round(float(value) * 100_000_000))


def resolve_auth(args):
    if args.rpcuser is not None:
        return f"{args.rpcuser}:{args.rpcpassword or ''}"

    cookie = args.rpccookiefile
    if cookie is None:
        if args.datadir is None:
            raise RpcError("need --rpcuser/--rpcpassword, or --datadir (or --rpccookiefile) for cookie auth")
        # These are BTQ's data subdirectories, which differ from upstream's:
        # testnet lives in "test", not "testnet3". See CreateBaseChainParams.
        subdir = {"main": "", "test": "test", "signet": "signet", "regtest": "regtest"}[args.chain]
        cookie = os.path.join(os.path.expanduser(args.datadir), subdir, ".cookie")
    try:
        with open(cookie, "r", encoding="utf-8") as handle:
            return handle.read().strip()
    except OSError as exc:
        raise RpcError(f"cannot read cookie file {cookie}: {exc}") from exc


class Scan:
    def __init__(self):
        # outpoint -> [kind, commitment, sats, height]. Entries are dropped as
        # their coins are spent, so what survives the walk is the live set.
        self.live = {}
        # A commitment proved to stand for a Dilithium key or script, and the
        # spend that proved it.
        self.proved = {}
        self.blocks = 0
        self.spends_seen = 0

    def add_output(self, txid, vout, script, value, height):
        kind, commitment = classify(script)
        if kind is None:
            return
        self.live[f"{txid}:{vout}"] = [kind, commitment, to_sats(value), height]

    def spend(self, txid, vout, witness, script_sig, spender):
        entry = self.live.pop(f"{txid}:{vout}", None)
        if entry is None:
            return
        kind, commitment, _, _ = entry
        if commitment is None or commitment in self.proved:
            return

        if kind == "p2wpkh":
            # The routing this activation removes: two witness items, the second
            # a Dilithium-sized key. Nothing else about the spend distinguishes
            # it from an ECDSA P2WPKH.
            if len(witness) == 2 and len(witness[1]) == DILITHIUM_PUBKEY_SIZE:
                self.proved[commitment] = spender
            return

        # P2SH and P2WSH reveal the script they committed to at spend time.
        revealed = list(witness)
        if script_sig:
            revealed.extend(push for push in iter_pushes(script_sig))
        for item in revealed:
            if has_dilithium_opcode(item):
                self.proved[commitment] = spender
                return

    def findings(self):
        visible, hidden, residual = [], [], defaultdict(lambda: [0, 0])
        for outpoint, (kind, commitment, sats, height) in self.live.items():
            record = {"outpoint": outpoint, "kind": kind, "sats": sats, "height": height}
            if commitment is None:
                visible.append(record)
            elif commitment in self.proved:
                record["commitment"] = commitment
                record["proved_by"] = self.proved[commitment]
                hidden.append(record)
            else:
                bucket = residual[kind]
                bucket[0] += 1
                bucket[1] += sats
        return visible, hidden, dict(residual)


def iter_pushes(script):
    """Yield the data pushed by a script. Used to read a P2SH scriptSig."""
    i, n = 0, len(script)
    while i < n:
        op = script[i]
        i += 1
        size = None
        if op < OP_PUSHDATA1:
            size = op
        elif op == OP_PUSHDATA1 and i < n:
            size, i = script[i], i + 1
        elif op == OP_PUSHDATA2 and i + 1 < n:
            size, i = int.from_bytes(script[i:i + 2], "little"), i + 2
        elif op == OP_PUSHDATA4 and i + 3 < n:
            size, i = int.from_bytes(script[i:i + 4], "little"), i + 4
        if size is None:
            continue
        if i + size > n:
            return
        yield script[i:i + size]
        i += size


def run_scan(rpc, start_height, stop_height, batch_size, progress):
    scan = Scan()
    height = start_height
    started = time.time()

    while height <= stop_height:
        heights = list(range(height, min(height + batch_size, stop_height + 1)))
        hashes = rpc.batch([("getblockhash", [h]) for h in heights])
        blocks = rpc.batch([("getblock", [h, 2]) for h in hashes])

        for block in blocks:
            block_height = block["height"]
            for tx in block["tx"]:
                txid = tx["txid"]
                for vin in tx["vin"]:
                    if "coinbase" in vin:
                        continue
                    scan.spends_seen += 1
                    witness = [bytes.fromhex(item) for item in vin.get("txinwitness", [])]
                    script_sig = bytes.fromhex(vin.get("scriptSig", {}).get("hex", ""))
                    scan.spend(vin["txid"], vin["vout"], witness, script_sig,
                               f"{txid} in block {block_height}")
                for vout in tx["vout"]:
                    script = bytes.fromhex(vout["scriptPubKey"]["hex"])
                    scan.add_output(txid, vout["n"], script, vout["value"], block_height)
            scan.blocks += 1

        height = heights[-1] + 1
        if progress and scan.blocks % (batch_size * 20) < batch_size:
            rate = scan.blocks / max(time.time() - started, 1e-9)
            print(f"  ...{height - 1}/{stop_height} ({rate:.0f} blocks/s)", file=sys.stderr)

    return scan


def btq(sats):
    return f"{sats / 100_000_000:.8f}"


def report(scan, chain, tip, visible, hidden, residual, out):
    total = sum(item["sats"] for item in visible + hidden)

    print(f"\nChain {chain}, scanned {scan.blocks} blocks to height {tip}.", file=out)
    print(f"{len(scan.live)} live outputs of a relevant kind, {scan.spends_seen} spends inspected.\n", file=out)

    print("Coins that P2MR-only activation would freeze", file=out)
    print("--------------------------------------------", file=out)
    if not visible and not hidden:
        print("  none found", file=out)
    for label, group in (("visible in the output script", visible),
                         ("proved by an earlier spend", hidden)):
        if not group:
            continue
        group_total = sum(item["sats"] for item in group)
        print(f"  {label}: {len(group)} outputs, {btq(group_total)} BTQ", file=out)
        for item in sorted(group, key=lambda i: -i["sats"])[:20]:
            print(f"    {item['outpoint']}  {item['kind']:7} {btq(item['sats']):>18} BTQ  height {item['height']}", file=out)
        if len(group) > 20:
            print(f"    ... and {len(group) - 20} more", file=out)
    print(f"\n  TOTAL AT RISK: {btq(total)} BTQ across {len(visible) + len(hidden)} outputs\n", file=out)

    print("Not determinable from chain data", file=out)
    print("--------------------------------", file=out)
    print("  These commit to a hash whose preimage has never been revealed on", file=out)
    print("  chain. Almost all are ordinary ECDSA coins. Any Dilithium ones", file=out)
    print("  among them can only be identified by whoever holds the keys.", file=out)
    if not residual:
        print("  none", file=out)
    for kind in HASHED_KINDS:
        if kind in residual:
            count, sats = residual[kind]
            print(f"  {kind:7} {count:>8} outputs  {btq(sats):>18} BTQ", file=out)
    print(file=out)

    return total


def main():
    parser = argparse.ArgumentParser(
        description="Find coins that P2MR-only activation would freeze (issue #111).",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rpcconnect", default="127.0.0.1")
    parser.add_argument("--rpcport", type=int)
    parser.add_argument("--rpcuser")
    parser.add_argument("--rpcpassword")
    parser.add_argument("--rpccookiefile")
    parser.add_argument("--datadir", help="for cookie auth, when no rpcuser is given")
    parser.add_argument("--chain", default="main", choices=("main", "test", "signet", "regtest"))
    parser.add_argument("--start-height", type=int, default=0)
    parser.add_argument("--stop-height", type=int, help="defaults to the chain tip")
    parser.add_argument("--batch-size", type=int, default=100)
    parser.add_argument("--json", metavar="PATH", help="also write the findings as JSON")
    parser.add_argument("--quiet", action="store_true", help="no progress on stderr")
    args = parser.parse_args()

    default_port = {"main": 8332, "test": 18332, "signet": 38332, "regtest": 18443}[args.chain]
    port = args.rpcport or default_port

    try:
        rpc = Rpc(args.rpcconnect, port, resolve_auth(args))
        info = rpc.call("getblockchaininfo")
        tip = args.stop_height if args.stop_height is not None else info["blocks"]
        if not args.quiet:
            print(f"Scanning {info['chain']} blocks {args.start_height}..{tip}", file=sys.stderr)
        scan = run_scan(rpc, args.start_height, tip, args.batch_size, not args.quiet)
    except (RpcError, OSError, KeyError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    visible, hidden, residual = scan.findings()
    total = report(scan, info["chain"], tip, visible, hidden, residual, sys.stdout)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({
                "chain": info["chain"],
                "scanned_to_height": tip,
                "blocks_scanned": scan.blocks,
                "frozen_total_sats": total,
                "frozen_visible": visible,
                "frozen_proved": hidden,
                "undeterminable": {k: {"outputs": v[0], "sats": v[1]} for k, v in residual.items()},
            }, handle, indent=2)

    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
