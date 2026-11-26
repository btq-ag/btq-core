#!/usr/bin/env python3
"""The simulation cases the first harness did not reach.

Stages: relay-d2, miss, full, mempool, peers, soak.
Each writes one JSON file under RAW_RESULTS.
"""

import json
import os
import subprocess
import sys
import time
import urllib.request
from base64 import b64encode
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measure import Node, diff_bytes  # noqa: E402

ROOT = Path("/home/barney/btq/BTQ-Core-dilithium5")
D5 = ROOT / "src/btqd"
D5CLI = ROOT / "src/btq-cli"
D2 = Path("/home/barney/btq/BTQ-Core-dilithium2/src/btqd")
D2CLI = Path("/home/barney/btq/BTQ-Core-dilithium2/src/btq-cli")
OUT = ROOT / "docs/dilithium5-validation/RAW_RESULTS"
WORK = Path("/tmp/btq-finish")


def cookie(datadir):
    return (Path(datadir) / "regtest" / ".cookie").read_text().strip()


def batch(datadir, rpcport, calls, wallet=None):
    user, pw = cookie(datadir).split(":", 1)
    url = f"http://127.0.0.1:{rpcport}/"
    if wallet:
        url += f"wallet/{wallet}"
    body = json.dumps([
        {"jsonrpc": "1.0", "id": i, "method": method, "params": params}
        for i, (method, params) in enumerate(calls)
    ]).encode()
    req = urllib.request.Request(url, data=body)
    req.add_header("Authorization", "Basic " + b64encode(f"{user}:{pw}".encode()).decode())
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=300) as resp:
        payload = json.loads(resp.read().decode())
    out = []
    for item in payload:
        if item.get("error"):
            raise RuntimeError(item["error"])
        out.append(item["result"])
    return out


def boot(node):
    node.start()
    for name in ("funding", "dil"):
        try:
            node.cli("-named", "createwallet", f"wallet_name={name}", "descriptors=true")
        except subprocess.CalledProcessError as exc:
            if "already exists" not in (exc.output or ""):
                raise
            try:
                node.cli("loadwallet", name)
            except subprocess.CalledProcessError as load_exc:
                if "already loaded" not in (load_exc.output or ""):
                    raise
    addr = node.cli("getnewaddress", wallet="funding")
    node.cli("generatetoaddress", "110", addr, wallet="funding")
    return node.cli("getnewdilithiumaddress", wallet="dil")["address"]


def fund(node, n, dest_wallet="dil"):
    addrs = []
    while len(addrs) < n:
        chunk = min(100, n - len(addrs))
        made = batch(
            node.datadir, node.rpcport,
            [("getnewdilithiumaddress", [])] * chunk,
            wallet=dest_wallet,
        )
        addrs.extend(item["address"] for item in made)
        print(f"  addresses {len(addrs)}/{n}", flush=True)
    utxos = []
    for start in range(0, n, 80):
        chunk = addrs[start:start + 80]
        node.cli("sendmany", "", json.dumps({a: 0.05 for a in chunk}), wallet="funding")
        node.cli("generatetoaddress", "1", node.cli("getnewaddress", wallet="funding"), wallet="funding")
        print(f"  funded {start + len(chunk)}/{n}", flush=True)
    confirmed = node.cli("listunspent", "1", "9999999", wallet=dest_wallet)
    utxos = [u for u in confirmed if abs(u["amount"] - 0.05) < 1e-8]
    if len(utxos) < n:
        raise RuntimeError(f"short utxos {len(utxos)} < {n}")
    return utxos[:n]


def spend_hexes(node, utxos, dest):
    hexes = []
    for start in range(0, len(utxos), 25):
        chunk = utxos[start:start + 25]
        raws = batch(node.datadir, node.rpcport, [
            ("createrawtransaction", [
                [{"txid": u["txid"], "vout": u["vout"]}],
                {dest: 0.049},
            ]) for u in chunk
        ], wallet="dil")
        signed = batch(node.datadir, node.rpcport, [
            ("signrawtransactionwithwallet", [raw]) for raw in raws
        ], wallet="dil")
        for item in signed:
            if not item.get("complete"):
                raise RuntimeError(item)
            hexes.append(item["hex"])
        print(f"  signed {len(hexes)}/{len(utxos)}", flush=True)
    return hexes


def broadcast(node, hexes):
    ids = []
    for start in range(0, len(hexes), 25):
        chunk = hexes[start:start + 25]
        ids.extend(batch(node.datadir, node.rpcport, [
            ("sendrawtransaction", [hx]) for hx in chunk
        ]))
        print(f"  broadcast {len(ids)}/{len(hexes)}", flush=True)
    return ids


def cold_tip(node):
    tip = node.cli("getbestblockhash")
    info = node.cli("getblock", tip)
    node.stop()
    time.sleep(1)
    node.start()
    off = node.log_size()
    t0 = time.perf_counter()
    node.cli("invalidateblock", tip)
    t1 = time.perf_counter()
    node.cli("reconsiderblock", tip)
    t2 = time.perf_counter()
    text = (node.datadir / "regtest" / "debug.log").read_text(errors="replace")[off:]
    first = None
    for line in text.splitlines():
        if "Verify " in line and "txins:" in line:
            left = line.split("Verify ", 1)[1]
            first = {
                "txins": int(left.split(" txins")[0]),
                "verify_ms": float(left.split("txins:")[1].split("ms")[0]),
            }
            break
    return {
        "tip": tip,
        "block": {k: info[k] for k in ("height", "nTx", "size", "weight", "strippedsize")},
        "invalidate_s": round(t1 - t0, 3),
        "reconsider_s": round(t2 - t1, 3),
        "first_verify": first,
    }


def write(name, obj):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    path.write_text(json.dumps(obj, indent=2) + "\n")
    print(f"wrote {path}", flush=True)


def full_block(label, btqd, cli, n, port):
    work = WORK / label
    node = Node("origin", work, str(btqd), str(cli), port, port + 100, [
        "-listen=1", "-blockmaxweight=8000000",
    ])
    try:
        dest = boot(node)
        print(f"{label} funding {n}", flush=True)
        utxos = fund(node, n)
        second = node.cli("getnewdilithiumaddress", wallet="dil")["address"]
        print(f"{label} signing", flush=True)
        t0 = time.perf_counter()
        hexes = spend_hexes(node, utxos, second)
        t1 = time.perf_counter()
        broadcast(node, hexes)
        t2 = time.perf_counter()
        miner = node.cli("getnewaddress", wallet="funding")
        node.cli("generatetoaddress", "1", miner, wallet="funding")
        cold = cold_tip(node)
        cold["sign_s"] = round(t1 - t0, 3)
        cold["broadcast_s"] = round(t2 - t1, 3)
        cold["funded"] = n
        cold["mempool_after"] = node.cli("getmempoolinfo")
        write(f"{label}-full-block.json", cold)
    finally:
        node.stop()


def relay_d2():
    base = 20100
    origin = Node("origin", WORK / "d2relay" / "origin", str(D2), str(D2CLI), base, base + 100, ["-listen=1"])
    peers = []
    try:
        dest = boot(origin)
        specs = [
            ("public", ["-listen=1", f"-connect=127.0.0.1:{base}"]),
            ("private", ["-listen=0", f"-connect=127.0.0.1:{base}"]),
            ("blocksonly", ["-listen=0", "-blocksonly=1", f"-connect=127.0.0.1:{base}"]),
            ("extra", ["-listen=1", f"-connect=127.0.0.1:{base}"]),
        ]
        for i, (name, extra) in enumerate(specs):
            node = Node(name, WORK / "d2relay" / name, str(D2), str(D2CLI), base + 1 + i, base + 101 + i, extra)
            node.start()
            peers.append(node)
        height = int(origin.cli("getblockcount"))
        for node in peers:
            for _ in range(80):
                if int(node.cli("getblockcount")) >= height:
                    break
                time.sleep(0.25)
        utxos = fund(origin, 20)
        time.sleep(2)
        snap = {n.name: n.peer_bytes() for n in [origin, *peers]}
        second = origin.cli("getnewdilithiumaddress", wallet="dil")["address"]
        hexes = spend_hexes(origin, utxos, second)
        broadcast(origin, hexes)
        time.sleep(5)
        mid = {n.name: n.peer_bytes() for n in [origin, *peers]}
        origin.cli("generatetoaddress", "1", origin.cli("getnewaddress", wallet="funding"), wallet="funding")
        time.sleep(2)
        end = {n.name: n.peer_bytes() for n in [origin, *peers]}
        out = {}
        for name in snap:
            out[name] = {
                "tx_sent": diff_bytes(mid[name]["sent"], snap[name]["sent"]),
                "tx_recv": diff_bytes(mid[name]["recv"], snap[name]["recv"]),
                "block_sent": diff_bytes(end[name]["sent"], mid[name]["sent"]),
                "block_recv": diff_bytes(end[name]["recv"], mid[name]["recv"]),
            }
        write("dilithium2-relay-5s.json", out)
    finally:
        origin.stop()
        for node in peers:
            node.stop()


def miss_rates():
    base = 20200
    a = Node("a", WORK / "miss" / "a", str(D5), str(D5CLI), base, base + 100, ["-listen=1"])
    b = Node("b", WORK / "miss" / "b", str(D5), str(D5CLI), base + 1, base + 101, [
        "-listen=0", "-connect=0",
    ])
    try:
        boot(a)
        b.start()
        # B has no wallet. Sync the chain by a one-shot connection, then disconnect.
        b.stop()
        b.extra = ["-listen=0", f"-connect=127.0.0.1:{base}"]
        b.start()
        height = int(a.cli("getblockcount"))
        for _ in range(80):
            if int(b.cli("getblockcount")) >= height:
                break
            time.sleep(0.25)
        b.stop()
        dest = a.cli("getnewdilithiumaddress", wallet="dil")["address"]
        utxos = fund(a, 100)
        # B must see the funding blocks before it can accept the spends.
        b.extra = ["-listen=0", f"-connect=127.0.0.1:{base}"]
        b.start()
        height = int(a.cli("getblockcount"))
        for _ in range(80):
            if int(b.cli("getblockcount")) >= height:
                break
            time.sleep(0.25)
        b.stop()
        second = a.cli("getnewdilithiumaddress", wallet="dil")["address"]
        hexes = spend_hexes(a, utxos, second)
        broadcast(a, hexes)
        a.cli("generatetoaddress", "1", a.cli("getnewaddress", wallet="funding"), wallet="funding")
        tip = a.cli("getbestblockhash")
        block = a.cli("getblock", tip)
        results = {"block": {k: block[k] for k in ("nTx", "size", "weight")}}
        for label, keep in (("miss10", 90), ("miss1", 99)):
            # Drop B back to the parent by starting from a copy is hard.
            # Rebuild B's mempool against the pre-block chain: invalidate on A
            # is wrong because we need A to still have the block.
            # B is at the parent if we never let it see the last block.
            # It is currently stopped at the parent (we stopped it before the spends
            # were mined only if the last start was before mining). It was stopped
            # BEFORE spend creation, then we mined. B's tip is the funding height.
            b.extra = ["-listen=0", "-connect=0"]
            b.start()
            if int(b.cli("getblockcount")) != block["height"] - 1:
                raise RuntimeError(f"B height {b.cli('getblockcount')} parent {block['height'] - 1}")
            subset = hexes[:keep]
            for hx in subset:
                try:
                    b.cli("sendrawtransaction", hx)
                except Exception:
                    pass
            accepted = int(b.cli("getmempoolinfo")["size"])
            b.stop()
            time.sleep(0.5)
            before = a.peer_bytes()
            b.extra = ["-listen=0", f"-connect=127.0.0.1:{base}"]
            b.start()
            for _ in range(80):
                if int(b.cli("getblockcount")) >= block["height"]:
                    break
                time.sleep(0.25)
            time.sleep(1)
            after_a = a.peer_bytes()
            after_b = b.peer_bytes()
            results[label] = {
                "kept_in_mempool": accepted,
                "b_height": int(b.cli("getblockcount")),
                "a_sent": diff_bytes(after_a["sent"], before["sent"]),
                "b_recv": after_b["recv"],
            }
            b.stop()
            # Next ratio needs B at the parent again. The chain on B now includes
            # the block. Rewind B.
            b.extra = ["-listen=0", "-connect=0"]
            b.start()
            b.cli("invalidateblock", tip)
            b.stop()
        write("dilithium5-cmpct-miss.json", results)
    finally:
        a.stop()
        b.stop()


def mempool_fill():
    base = 20300
    node = Node("origin", WORK / "mempool", str(D5), str(D5CLI), base, base + 100, [
        "-listen=0", "-maxmempool=300", "-mempoolexpiry=999999",
    ])
    try:
        dest = boot(node)
        target = 42000
        print(f"mempool funding {target}", flush=True)
        utxos = fund(node, target)
        second = node.cli("getnewdilithiumaddress", wallet="dil")["address"]
        print("mempool signing", flush=True)
        hexes = spend_hexes(node, utxos, second)
        accepted = 0
        rejected = 0
        samples = []
        last = None
        for start in range(0, len(hexes), 50):
            chunk = hexes[start:start + 50]
            for hx in chunk:
                try:
                    node.cli("sendrawtransaction", hx)
                    accepted += 1
                except Exception as exc:  # noqa: BLE001
                    rejected += 1
                    last = str(exc)[:300]
                    if rejected > 20:
                        break
            info = node.cli("getmempoolinfo")
            samples.append({
                "accepted": accepted,
                "rejected": rejected,
                "size": info["size"],
                "bytes": info["bytes"],
                "usage": info["usage"],
                "mempoolminfee": info.get("mempoolminfee"),
            })
            print(f"  mempool size={info['size']} usage={info['usage']} rejected={rejected}", flush=True)
            if rejected > 20 or info["usage"] > 290_000_000:
                break
        write("dilithium5-mempool-fill.json", {
            "accepted": accepted,
            "rejected": rejected,
            "last_error": last,
            "final": node.cli("getmempoolinfo"),
            "rss": node.rss_kb(),
            "samples": samples[:: max(1, len(samples) // 20)] + samples[-1:],
        })
    finally:
        node.stop()


def peer_ceiling():
    """100 protocol peers that request every inv. Counts what btqd actually sends."""
    sys.path.insert(0, str(ROOT / "test/functional"))
    from test_framework.p2p import NetworkThread, P2PInterface

    base = 21500
    node = Node("origin", WORK / "peers-b", str(D5), str(D5CLI), base, base + 100, [
        "-listen=1", "-maxconnections=200",
    ])
    thread = None
    try:
        dest = boot(node)
        thread = NetworkThread()
        thread.start()
        while NetworkThread.network_event_loop is None:
            time.sleep(0.05)
        peers = []
        for n in range(100):
            peer = P2PInterface()
            peer.peer_connect(dstaddr="127.0.0.1", dstport=base, net="regtest", timeout_factor=1)()
            peers.append(peer)
            if n % 10 == 9:
                time.sleep(0.3)
        up = []
        for peer in peers:
            try:
                peer.wait_for_verack()
                up.append(peer)
            except Exception:
                pass
        print(f"peers up {len(up)} node sees {len(node.cli('getpeerinfo'))}", flush=True)
        if len(up) < 50:
            raise RuntimeError(f"only {len(up)} peers connected")
        utxos = fund(node, 10)
        time.sleep(1)
        before = node.peer_bytes()
        second = node.cli("getnewdilithiumaddress", wallet="dil")["address"]
        hexes = spend_hexes(node, utxos, second)
        broadcast(node, hexes)
        time.sleep(5)
        after = node.peer_bytes()
        write("dilithium5-peer-ceiling.json", {
            "connected": len(node.cli("getpeerinfo")),
            "tx_sent": diff_bytes(after["sent"], before["sent"]),
            "tx_recv": diff_bytes(after["recv"], before["recv"]),
        })
    finally:
        node.stop()
        if thread:
            thread.close()


def mesh_peers(n):
    base = 21200
    origin = Node("origin", WORK / "mesh" / "origin", str(D5), str(D5CLI), base, base + 1000, [
        "-listen=1", "-maxconnections=200", "-dbcache=4",
    ])
    nodes = []
    try:
        dest = boot(origin)
        for i in range(n):
            extra = [
                "-listen=1", "-disablewallet", "-dbcache=4", "-maxmempool=20",
                f"-connect=127.0.0.1:{base}",
                f"-port={base + 1 + i}",
                f"-rpcport={base + 2000 + i}",
            ]
            if i > 0:
                extra.append(f"-connect=127.0.0.1:{base + i}")
            node = Node(f"p{i}", WORK / "mesh" / f"p{i}", str(D5), str(D5CLI), base + 1 + i, base + 2000 + i, extra)
            node.start()
            nodes.append(node)
            if i % 5 == 4:
                print(f"  mesh nodes {i + 1} rss_origin {origin.rss_kb()}", flush=True)
        time.sleep(2)
        utxos = fund(origin, 10)
        time.sleep(1)
        before = origin.peer_bytes()
        second = origin.cli("getnewdilithiumaddress", wallet="dil")["address"]
        hexes = spend_hexes(origin, utxos, second)
        broadcast(origin, hexes)
        time.sleep(8)
        after = origin.peer_bytes()
        write("dilithium5-mesh.json", {
            "nodes": n,
            "origin_peers": len(origin.cli("getpeerinfo")),
            "tx_sent": diff_bytes(after["sent"], before["sent"]),
            "tx_recv": diff_bytes(after["recv"], before["recv"]),
            "origin_rss": origin.rss_kb(),
        })
    finally:
        origin.stop()
        for node in nodes:
            node.stop()


def soak(seconds):
    base = 21400
    origin = Node("origin", WORK / "soak" / "origin", str(D5), str(D5CLI), base, base + 100, ["-listen=1"])
    nodes = []
    samples = []
    try:
        dest = boot(origin)
        for i, extra in enumerate([
            ["-listen=1", f"-connect=127.0.0.1:{base}"],
            ["-listen=0", f"-connect=127.0.0.1:{base}"],
            ["-listen=0", "-blocksonly=1", f"-connect=127.0.0.1:{base}"],
            ["-listen=1", f"-connect=127.0.0.1:{base}"],
        ]):
            node = Node(f"n{i}", WORK / "soak" / f"n{i}", str(D5), str(D5CLI), base + 1 + i, base + 101 + i, extra)
            node.start()
            nodes.append(node)
        start = time.time()
        spends = 0
        while time.time() - start < seconds:
            utxos = fund(origin, 5)
            second = origin.cli("getnewdilithiumaddress", wallet="dil")["address"]
            broadcast(origin, spend_hexes(origin, utxos, second))
            spends += 5
            origin.cli("generatetoaddress", "1", origin.cli("getnewaddress", wallet="funding"), wallet="funding")
            height = int(origin.cli("getblockcount"))
            deadline = time.time() + 3
            while time.time() < deadline:
                if all(int(n.cli("getblockcount")) == height for n in nodes):
                    break
                time.sleep(0.2)
            row = {"t": round(time.time() - start, 1), "height": height, "spends": spends, "tips": {}, "rss": {}}
            ok = True
            for node in [origin, *nodes]:
                if node.proc.poll() is not None:
                    ok = False
                    row["dead"] = node.name
                    continue
                got = int(node.cli("getblockcount"))
                row["tips"][node.name] = got
                row["rss"][node.name] = node.rss_kb()
                if got != height:
                    ok = False
            row["converged"] = ok
            samples.append(row)
            write("dilithium5-soak.json", {
                "seconds": round(time.time() - start, 1),
                "spends": spends,
                "samples": samples,
                "all_converged": all(s["converged"] for s in samples),
            })
            print(f"  soak t={row['t']} height={height} ok={ok}", flush=True)
        # Restart one node after it has missed blocks.
        nodes[1].stop()
        origin.cli("generatetoaddress", "3", origin.cli("getnewaddress", wallet="funding"), wallet="funding")
        nodes[1].start()
        target = int(origin.cli("getblockcount"))
        for _ in range(80):
            if int(nodes[1].cli("getblockcount")) >= target:
                break
            time.sleep(0.25)
        tips = {n.name: n.cli("getbestblockhash") for n in [origin, *nodes]}
        write("dilithium5-soak.json", {
            "seconds": round(time.time() - start, 1),
            "spends": spends,
            "samples": samples,
            "all_converged": all(s["converged"] for s in samples),
            "restart_tips": tips,
            "restart_ok": len(set(tips.values())) == 1,
        })
    finally:
        origin.stop()
        for node in nodes:
            node.stop()


if __name__ == "__main__":
    stage = sys.argv[1]
    if stage == "relay-d2":
        relay_d2()
    elif stage == "miss":
        miss_rates()
    elif stage == "full-d5":
        full_block("dilithium5", D5, D5CLI, 1000, 20700)
    elif stage == "full-d2":
        full_block("dilithium2", D2, D2CLI, 1600, 20800)
    elif stage == "mempool":
        mempool_fill()
    elif stage == "peers":
        peer_ceiling()
    elif stage == "mesh":
        mesh_peers(int(sys.argv[2]) if len(sys.argv) > 2 else 12)
    elif stage == "soak":
        soak(int(sys.argv[2]) if len(sys.argv) > 2 else 900)
    else:
        raise SystemExit(f"unknown stage {stage}")
