#!/usr/bin/env bash
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Operator wrapper around the Dilithium P2MR PSBT RPCs. Every command is a
# thin call to btq-cli; nothing here signs except walletprocesspsbt.
#
# A 2-of-3 PSBT is ~16 KB. Keep it in a file, not a QR code.
#
# Usage:
#   dilithium-psbt.sh --cli="<btq-cli invocation>" <command> [args]
#   e.g. dilithium-psbt.sh --cli="src/btq-cli -testnet" pubkey alice
#
# Commands print JSON or a PSBT/hex payload on stdout. Diagnostics go to stderr.
# See doc/psbt.md ("Dilithium P2MR") for the equivalent raw btq-cli recipe.

set -euo pipefail

usage() {
    cat <<'EOF'
usage: dilithium-psbt.sh --cli="<btq-cli invocation>" <command> [args]

Commands (each is one btq-cli RPC, or a short sequence of them):

  pubkey   <wallet> [label]
      New Dilithium key in <wallet>. Prints address, p2mr_id, pubkey.
      RPCs: getnewdilithiumaddress, getdilithiumpubkey

  register <wallet> <m> <pubkey> [<pubkey>...]
      Register the same m-of-n P2MR destination in <wallet>.
      Every co-signer must pass the same m and the same pubkeys in the same order.
      RPC: createdilithiummultisig

  create   <wallet> <txid:vout> <address> <amount>
      Unsigned funded PSBT spending that outpoint to <address>.
      RPC: walletcreatefundedpsbt

  sign     <wallet> <psbt>
      Add this wallet's Dilithium signature(s). <psbt> is a file or base64.
      RPC: walletprocesspsbt

  combine  <psbt> <psbt> [<psbt>...]
      Merge independently signed copies of the same unsigned PSBT.
      RPC: combinepsbt

  status   <psbt>
      Per-input Dilithium signing progress (use this, not analyzepsbt).
      RPC: decodepsbt

  finalize <psbt>
      Build the witness. Prints the raw hex when complete.
      RPC: finalizepsbt

  broadcast <hex>
      RPC: sendrawtransaction

Pass a PSBT as a file path (preferred) or as a base64 string.
EOF
}

CLI=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --cli=*)
            # shellcheck disable=SC2206
            CLI=(${1#--cli=})
            shift
            ;;
        --cli)
            # shellcheck disable=SC2206
            CLI=($2)
            shift 2
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "unknown option: $1 (btq-cli flags belong inside --cli)" >&2
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

if [[ ${#CLI[@]} -eq 0 ]]; then
    if [[ -n "${BTQ_CLI:-}" ]]; then
        # shellcheck disable=SC2206
        CLI=($BTQ_CLI)
    else
        CLI=(btq-cli)
    fi
fi

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

need_python() {
    command -v python3 >/dev/null 2>&1 || {
        echo "python3 is required to parse RPC JSON" >&2
        exit 1
    }
}

# If btq-cli printed a JSON value, unwrap a top-level string so stdout is
# the raw PSBT / txid / hex rather than a quoted JSON string.
unwrap() {
    need_python
    python3 -c '
import json, sys
raw = sys.stdin.read()
try:
    value = json.loads(raw)
except json.JSONDecodeError:
    sys.stdout.write(raw)
    raise SystemExit(0)
if isinstance(value, str):
    sys.stdout.write(value)
    if not value.endswith("\n"):
        sys.stdout.write("\n")
else:
    json.dump(value, sys.stdout)
    sys.stdout.write("\n")
'
}

# Read a dotted path out of JSON on stdin. Numeric path segments index arrays.
jget() {
    need_python
    python3 -c '
import json, sys
data = json.load(sys.stdin)
cur = data
for part in sys.argv[1].split("."):
    if part == "":
        continue
    if isinstance(cur, list):
        cur = cur[int(part)]
    else:
        cur = cur[part]
if isinstance(cur, (dict, list)):
    json.dump(cur, sys.stdout)
    sys.stdout.write("\n")
elif isinstance(cur, bool):
    print("true" if cur else "false")
else:
    print(cur)
' "$1"
}

btq() {
    "${CLI[@]}" "$@"
}

wallet() {
    local name="$1"
    shift
    "${CLI[@]}" -rpcwallet="$name" "$@"
}

read_psbt() {
    local arg="$1"
    if [[ -f "$arg" ]]; then
        tr -d '\n' < "$arg"
    else
        printf '%s' "$arg"
    fi
}

cmd_pubkey() {
    local name="${1:?wallet name}"
    local label="${2:-}"
    local created pk
    if [[ -n "$label" ]]; then
        created=$(wallet "$name" getnewdilithiumaddress "$label")
    else
        created=$(wallet "$name" getnewdilithiumaddress)
    fi
    local id address
    id=$(printf '%s' "$created" | jget p2mr_id)
    address=$(printf '%s' "$created" | jget address)
    pk=$(wallet "$name" getdilithiumpubkey "$id")
    need_python
    python3 -c '
import json, sys
created = json.loads(sys.argv[1])
info = json.loads(sys.argv[2])
pubkeys = info["pubkeys"]
if len(pubkeys) != 1:
    sys.stderr.write("expected a single-key Dilithium address, got %d pubkeys\n" % len(pubkeys))
    sys.exit(1)
out = {
    "wallet": sys.argv[3],
    "address": created["address"],
    "p2mr_id": created["p2mr_id"],
    "pubkey": pubkeys[0]["pubkey"],
    "ismine": pubkeys[0]["ismine"],
}
json.dump(out, sys.stdout, indent=2)
sys.stdout.write("\n")
' "$created" "$pk" "$name"
}

cmd_register() {
    local name="${1:?wallet name}"
    local m="${2:?m (signatures required)}"
    shift 2
    if [[ $# -lt 1 ]]; then
        echo "register: need at least one pubkey" >&2
        exit 1
    fi
    need_python
    local pubkeys_json
    pubkeys_json=$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1:]))' "$@")
    wallet "$name" createdilithiummultisig "$m" "$pubkeys_json"
}

cmd_create() {
    local name="${1:?wallet name}"
    local outpoint="${2:?txid:vout}"
    local dest="${3:?destination address}"
    local amount="${4:?amount}"
    local txid="${outpoint%:*}"
    local vout="${outpoint##*:}"
    if [[ "$txid" == "$outpoint" || -z "$vout" ]]; then
        echo "create: outpoint must be txid:vout" >&2
        exit 1
    fi
    need_python
    local inputs outputs funded
    inputs=$(python3 -c 'import json,sys; print(json.dumps([{"txid":sys.argv[1],"vout":int(sys.argv[2])}]))' "$txid" "$vout")
    outputs=$(python3 -c 'import json,sys; print(json.dumps([{sys.argv[1]: sys.argv[2]}]))' "$dest" "$amount")
    funded=$(wallet "$name" walletcreatefundedpsbt "$inputs" "$outputs")
    printf '%s' "$funded" | jget psbt
}

cmd_sign() {
    local name="${1:?wallet name}"
    local psbt
    psbt=$(read_psbt "${2:?psbt file or base64}")
    local result
    result=$(wallet "$name" walletprocesspsbt "$psbt")
    printf '%s' "$result" | jget psbt
    {
        echo "complete: $(printf '%s' "$result" | jget complete)"
        # Progress is on stderr so stdout stays a clean PSBT for piping.
        local decoded
        decoded=$(btq decodepsbt "$(printf '%s' "$result" | jget psbt)")
        printf '%s' "$decoded" | python3 -c '
import json, sys
decoded = json.load(sys.stdin)
for i, inp in enumerate(decoded.get("inputs", [])):
    progress = inp.get("p2mr_dilithium")
    if progress is None:
        print("input %d: not a Dilithium P2MR input" % i)
    else:
        print("input %d: %s  %s/%s signatures" % (
            i, progress.get("status"), progress.get("signatures"), progress.get("required")))
'
    } >&2
}

cmd_combine() {
    if [[ $# -lt 2 ]]; then
        echo "combine: need at least two PSBTs" >&2
        exit 1
    fi
    need_python
    local arr='['
    local first=1
    local p
    for p in "$@"; do
        if [[ $first -eq 1 ]]; then
            first=0
        else
            arr+=','
        fi
        arr+=$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$(read_psbt "$p")")
    done
    arr+=']'
    btq combinepsbt "$arr" | unwrap
}

cmd_status() {
    local psbt
    psbt=$(read_psbt "${1:?psbt file or base64}")
    local decoded
    decoded=$(btq decodepsbt "$psbt")
    need_python
    printf '%s' "$decoded" | python3 -c '
import json, sys
decoded = json.load(sys.stdin)
out = []
for i, inp in enumerate(decoded.get("inputs", [])):
    row = {"index": i}
    if "p2mr_dilithium" in inp:
        row.update(inp["p2mr_dilithium"])
    else:
        row["status"] = "not_p2mr"
    if "p2mr_scripts" in inp and inp["p2mr_scripts"]:
        leaf = inp["p2mr_scripts"][0]
        row["dilithium_policy"] = leaf.get("dilithium_policy")
        row["dilithium_required"] = leaf.get("dilithium_required")
        row["dilithium_total"] = leaf.get("dilithium_total")
    out.append(row)
json.dump(out if len(out) != 1 else out[0], sys.stdout, indent=2)
sys.stdout.write("\n")
'
}

cmd_finalize() {
    local psbt
    psbt=$(read_psbt "${1:?psbt file or base64}")
    local result
    result=$(btq finalizepsbt "$psbt")
    local complete
    complete=$(printf '%s' "$result" | jget complete)
    if [[ "$complete" == "true" ]]; then
        printf '%s' "$result" | jget hex
    else
        echo "not complete; need more signatures" >&2
        printf '%s' "$result" | jget psbt >&2 || true
        exit 2
    fi
}

cmd_broadcast() {
    local hex="$1"
    if [[ -f "$hex" ]]; then
        hex=$(tr -d '\n' < "$hex")
    fi
    btq sendrawtransaction "$hex" | unwrap
}

cmd="${1}"
shift || true

case "$cmd" in
    pubkey)    cmd_pubkey "$@" ;;
    register)  cmd_register "$@" ;;
    create)    cmd_create "$@" ;;
    sign)      cmd_sign "$@" ;;
    combine)   cmd_combine "$@" ;;
    status)    cmd_status "$@" ;;
    finalize)  cmd_finalize "$@" ;;
    broadcast) cmd_broadcast "$@" ;;
    help)      usage ;;
    *)
        echo "unknown command: $cmd" >&2
        usage >&2
        exit 1
        ;;
esac
