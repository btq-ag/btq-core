#!/usr/bin/env python3
"""Measure ConnectBlock, mempool, storage, and P2P bytes for one btqd build.

This talks to real processes. It does not import the bandwidth model.
Pass the btqd and btq-cli you want measured. Results go to RAW_RESULTS.
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import time
from pathlib import Path

TICKS = os.sysconf("SC_CLK_TCK")


def rpc_load(raw):
    raw = raw.strip()
    if not raw:
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return raw


class Node:
    def __init__(self, name, datadir, btqd, btq_cli, port, rpcport, extra):
        self.name = name
        self.datadir = Path(datadir)
        self.btqd = btqd
        self.btq_cli = btq_cli
        self.port = port
        self.rpcport = rpcport
        self.extra = extra
        self.proc = None

    def start(self):
        self.datadir.mkdir(parents=True, exist_ok=True)
        cmd = [
            self.btqd,
            "-regtest",
            f"-datadir={self.datadir}",
            f"-port={self.port}",
            f"-rpcport={self.rpcport}",
            "-rpcbind=127.0.0.1",
            "-rpcallowip=127.0.0.1",
            "-dnsseed=0",
            "-fixedseeds=0",
            "-server=1",
            "-fallbackfee=0.0001",
            "-debug=bench",
            "-par=4",
        ] + self.extra
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.wait_rpc()

    def wait_rpc(self, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.cli("getblockcount")
                return
            except subprocess.CalledProcessError:
                time.sleep(0.2)
        raise RuntimeError(f"{self.name} RPC did not come up")

    def cli(self, *args, wallet=None):
        cmd = [self.btq_cli, "-regtest", f"-datadir={self.datadir}", f"-rpcport={self.rpcport}"]
        if wallet:
            cmd.append(f"-rpcwallet={wallet}")
        cmd.extend(args)
        raw = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True)
        return rpc_load(raw)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.cli("stop")
            except subprocess.CalledProcessError:
                self.proc.terminate()
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def rss_kb(self):
        if not self.proc:
            return None
        text = Path(f"/proc/{self.proc.pid}/status").read_text()
        out = {}
        for line in text.splitlines():
            if line.startswith("VmRSS:") or line.startswith("VmHWM:"):
                key, val = line.split()[:2]
                out[key[:-1]] = int(val)
        return out

    def cpu_ticks(self):
        fields = Path(f"/proc/{self.proc.pid}/stat").read_text().rsplit(")", 1)[1].split()
        # utime is field 14, stime field 15, 0-based after the comm split: index 11 and 12
        return int(fields[11]) + int(fields[12])

    def log_size(self):
        log = self.datadir / "regtest" / "debug.log"
        return log.stat().st_size if log.exists() else 0

    def bench_since(self, offset):
        log = self.datadir / "regtest" / "debug.log"
        data = log.read_text(errors="replace")[offset:]
        rows = []
        current = {}
        for line in data.splitlines():
            if "Connect total:" in line:
                current["connect_total_ms"] = float(line.split("Connect total:")[1].split("ms")[0])
                rows.append(current)
                current = {}
            elif "Connect " in line and "transactions:" in line:
                current["connect_tx_ms"] = float(line.split("transactions:")[1].split("ms")[0])
            elif "Verify " in line and "txins:" in line:
                current["verify_ms"] = float(line.split("txins:")[1].split("ms")[0])
            elif "Disconnect block:" in line:
                current["disconnect_ms"] = float(line.split("Disconnect block:")[1].split("ms")[0])
        return rows

    def peer_bytes(self):
        info = self.cli("getpeerinfo")
        sent = {}
        recv = {}
        for peer in info:
            for bucket, src in ((sent, peer.get("bytessent_per_msg", {})), (recv, peer.get("bytesrecv_per_msg", {}))):
                for key, val in src.items():
                    bucket[key] = bucket.get(key, 0) + val
        return {
            "peers": len(info),
            "bytessent": sum(p.get("bytessent", 0) for p in info),
            "bytesrecv": sum(p.get("bytesrecv", 0) for p in info),
            "sent": sent,
            "recv": recv,
        }


def sha256_file(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def dir_bytes(path):
    root = Path(path)
    if not root.exists():
        return 0
    total = 0
    for p in root.rglob("*"):
        if p.is_file():
            total += p.stat().st_size
    return total


def wait_height(node, height, timeout=120):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if int(node.cli("getblockcount")) >= height:
            return
        time.sleep(0.25)
    raise RuntimeError(f"{node.name} stuck below {height}")


def fund_outputs(node, n):
    addrs = []
    for _ in range(n):
        addrs.append(node.cli("getnewdilithiumaddress", wallet="dil")["address"])
    # Unique addresses, so one sendmany can make independent UTXOs.
    for start in range(0, n, 100):
        chunk = addrs[start:start + 100]
        amounts = {addr: 0.05 for addr in chunk}
        node.cli("sendmany", "", json.dumps(amounts), wallet="funding")
        node.cli("generatetoaddress", "1", node.cli("getnewaddress", wallet="funding"), wallet="funding")
    utxos = node.cli("listunspent", "1", "9999999", wallet="dil")
    utxos = [u for u in utxos if abs(u["amount"] - 0.05) < 1e-8]
    if len(utxos) < n:
        raise RuntimeError(f"expected {n} dilithium utxos, got {len(utxos)}")
    return utxos[:n]


def spend_many(node, utxos, dest):
    txids = []
    for u in utxos:
        raw = node.cli(
            "createrawtransaction",
            json.dumps([{"txid": u["txid"], "vout": u["vout"]}]),
            json.dumps({dest: 0.049}),
            wallet="dil",
        )
        signed = node.cli("signrawtransactionwithwallet", raw, wallet="dil")
        if not signed.get("complete"):
            raise RuntimeError(f"sign failed: {signed}")
        txids.append(node.cli("sendrawtransaction", signed["hex"]))
    return txids


def block_stats(node, blockhash):
    block = node.cli("getblock", blockhash)
    return {
        "hash": blockhash,
        "height": block["height"],
        "nTx": block["nTx"],
        "size": block["size"],
        "strippedsize": block.get("strippedsize"),
        "weight": block["weight"],
    }


def measure_block(node, utxos, dest):
    offset = node.log_size()
    cpu0 = node.cpu_ticks()
    rss0 = node.rss_kb()
    t0 = time.perf_counter()
    txids = spend_many(node, utxos, dest)
    t_send = time.perf_counter()
    miner = node.cli("getnewaddress", wallet="funding")
    tip = node.cli("generatetoaddress", "1", miner, wallet="funding")[0]
    t_mine = time.perf_counter()
    cpu1 = node.cpu_ticks()
    rss1 = node.rss_kb()
    benches = node.bench_since(offset)
    # Drop the coinbase-only noise if a previous line was in the slice. Keep the last connect.
    connect = [b for b in benches if "connect_total_ms" in b]
    return {
        "spends": len(txids),
        "send_s": round(t_send - t0, 3),
        "mine_wall_s": round(t_mine - t_send, 3),
        "cpu_s": round((cpu1 - cpu0) / TICKS, 3),
        "rss_before": rss0,
        "rss_after": rss1,
        "block": block_stats(node, tip),
        "bench": connect[-1] if connect else None,
        "bench_lines": connect,
    }


def diff_bytes(after, before):
    out = {}
    keys = set(after) | set(before)
    for key in sorted(keys):
        delta = after.get(key, 0) - before.get(key, 0)
        if delta:
            out[key] = delta
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--btqd", required=True)
    parser.add_argument("--btq-cli", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--workdir", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--stress-seconds", type=int, default=120)
    args = parser.parse_args()

    work = Path(args.workdir)
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    nodes = []
    result = {
        "label": args.label,
        "btqd_sha256": sha256_file(args.btqd),
        "stress_seconds": args.stress_seconds,
        "host": {
            "cpu": Path("/proc/cpuinfo").read_text().split("model name")[1].split(":", 1)[1].splitlines()[0].strip(),
            "nproc": os.cpu_count(),
            "meminfo_kib": {
                line.split()[0][:-1]: int(line.split()[1])
                for line in Path("/proc/meminfo").read_text().splitlines()
                if line.startswith(("MemTotal", "MemAvailable", "SwapTotal"))
            },
        },
    }

    def add(name, port, rpc, extra):
        node = Node(name, work / name, args.btqd, args.btq_cli, port, rpc, extra)
        nodes.append(node)
        return node

    try:
        origin = add("origin", 19744, 18744, ["-listen=1"])
        origin.start()
        origin.cli("-named", "createwallet", "wallet_name=funding", "descriptors=true")
        origin.cli("-named", "createwallet", "wallet_name=dil", "descriptors=true")
        fund_addr = origin.cli("getnewaddress", wallet="funding")
        origin.cli("generatetoaddress", "110", fund_addr, wallet="funding")
        dest = origin.cli("getnewdilithiumaddress", wallet="dil")["address"]

        need = 3 * 25 + 50 + 100 + 200 + 40
        utxos = fund_outputs(origin, need)
        cursor = 0

        def take(n):
            nonlocal cursor
            chunk = utxos[cursor:cursor + n]
            cursor += n
            if len(chunk) != n:
                raise RuntimeError("ran out of utxos")
            return chunk

        blocks = []
        for _ in range(3):
            blocks.append(measure_block(origin, take(25), dest))
        for n in (50, 100, 200):
            blocks.append(measure_block(origin, take(n), dest))
        result["blocks"] = blocks

        # Second connect of the 200-tx block, after the script cache has seen it.
        big = blocks[-1]["block"]["hash"]
        offset = origin.log_size()
        t0 = time.perf_counter()
        origin.cli("invalidateblock", big)
        t1 = time.perf_counter()
        origin.cli("reconsiderblock", big)
        t2 = time.perf_counter()
        result["reconnect_200"] = {
            "invalidate_wall_s": round(t1 - t0, 3),
            "reconsider_wall_s": round(t2 - t1, 3),
            "bench": origin.bench_since(offset),
        }
        wait_height(origin, blocks[-1]["block"]["height"])

        mem_utxos = take(40)
        mem_before = origin.cli("getmempoolinfo")
        spend_many(origin, mem_utxos, dest)
        mem_after = origin.cli("getmempoolinfo")
        result["mempool"] = {
            "before": mem_before,
            "after": mem_after,
            "txs_added": 40,
            "rss": origin.rss_kb(),
        }

        # Peers. origin already has the chain. Others IBD it.
        public = add("public", 19745, 18745, ["-listen=1", "-connect=127.0.0.1:19744"])
        private = add("private", 19746, 18746, ["-listen=0", "-connect=127.0.0.1:19744"])
        blocksonly = add("blocksonly", 19747, 18747, ["-listen=0", "-blocksonly=1", "-connect=127.0.0.1:19744"])
        extra = add("extra", 19748, 18748, ["-listen=1", "-connect=127.0.0.1:19744"])
        for node in (public, private, blocksonly, extra):
            node.start()
        tip_height = int(origin.cli("getblockcount"))
        for node in (public, private, blocksonly, extra):
            wait_height(node, tip_height, timeout=180)
        result["ibd_bytes"] = {
            "origin": origin.peer_bytes(),
            "public": public.peer_bytes(),
            "private": private.peer_bytes(),
            "blocksonly": blocksonly.peer_bytes(),
            "extra": extra.peer_bytes(),
        }

        snap = {n.name: n.peer_bytes() for n in nodes}
        live_utxos_funded = fund_outputs(origin, 30)
        # fund_outputs mines. Snapshot again so the tx wave is only the spends.
        snap = {n.name: n.peer_bytes() for n in nodes}
        spend_many(origin, live_utxos_funded, dest)
        time.sleep(2)
        after_tx = {n.name: n.peer_bytes() for n in nodes}
        miner = origin.cli("getnewaddress", wallet="funding")
        origin.cli("generatetoaddress", "1", miner, wallet="funding")
        time.sleep(2)
        after_block = {n.name: n.peer_bytes() for n in nodes}
        result["relay"] = {}
        for name in snap:
            result["relay"][name] = {
                "tx_phase_sent": diff_bytes(after_tx[name]["sent"], snap[name]["sent"]),
                "tx_phase_recv": diff_bytes(after_tx[name]["recv"], snap[name]["recv"]),
                "block_phase_sent": diff_bytes(after_block[name]["sent"], after_tx[name]["sent"]),
                "block_phase_recv": diff_bytes(after_block[name]["recv"], after_tx[name]["recv"]),
            }

        # Node that never saw the transactions. It should pull the block body.
        late = add("late", 19749, 18749, ["-listen=0", "-connect=127.0.0.1:19744"])
        before_late = origin.peer_bytes()
        late.start()
        wait_height(late, int(origin.cli("getblockcount")), timeout=120)
        time.sleep(1)
        after_late_origin = origin.peer_bytes()
        result["late_joiner"] = {
            "late_recv": late.peer_bytes()["recv"],
            "origin_sent_delta": diff_bytes(after_late_origin["sent"], before_late["sent"]),
        }

        # Short sustained run. Not 24 hours.
        stress_start = time.time()
        samples = []
        stress_txs = 0
        tips = []
        while time.time() - stress_start < args.stress_seconds:
            batch = fund_outputs(origin, 5)
            spend_many(origin, batch, dest)
            stress_txs += 5
            origin.cli("generatetoaddress", "1", origin.cli("getnewaddress", wallet="funding"), wallet="funding")
            height = int(origin.cli("getblockcount"))
            for node in nodes:
                if node is origin or node.proc.poll() is not None:
                    continue
                wait_height(node, height, timeout=60)
            row = {"t": round(time.time() - stress_start, 1), "height": height, "rss": {}}
            ok = True
            for node in nodes:
                if node.proc.poll() is not None:
                    ok = False
                    row["dead"] = node.name
                    continue
                got = int(node.cli("getblockcount"))
                row["rss"][node.name] = node.rss_kb()
                if got != height:
                    ok = False
            row["converged"] = ok
            samples.append(row)
            tips.append(ok)
        result["stress"] = {
            "seconds": round(time.time() - stress_start, 1),
            "spend_batches": stress_txs,
            "samples": samples,
            "all_samples_converged": all(tips) if tips else False,
        }

        # Restart the private node and make sure it catches blocks it missed.
        private.stop()
        origin.cli("generatetoaddress", "3", origin.cli("getnewaddress", wallet="funding"), wallet="funding")
        private.start()
        wait_height(private, int(origin.cli("getblockcount")), timeout=120)
        result["restart"] = {
            "origin_tip": origin.cli("getbestblockhash"),
            "private_tip": private.cli("getbestblockhash"),
            "public_tip": public.cli("getbestblockhash"),
            "blocksonly_tip": blocksonly.cli("getbestblockhash"),
        }

        reg = origin.datadir / "regtest"
        result["storage"] = {
            "blocks_bytes": dir_bytes(reg / "blocks"),
            "chainstate_bytes": dir_bytes(reg / "chainstate"),
            "height": int(origin.cli("getblockcount")),
            "txindex": False,
        }
        result["final_rss"] = {n.name: n.rss_kb() for n in nodes if n.proc and n.proc.poll() is None}
    finally:
        for node in nodes:
            node.stop()

    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
