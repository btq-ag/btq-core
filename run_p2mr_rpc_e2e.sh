#!/usr/bin/env bash
set -euo pipefail

# End-to-end validation for wallet P2MR RPCs.
# Usage:
#   ./run_p2mr_rpc_e2e.sh
# Optional env overrides:
#   DATADIR=/tmp/p2mr-live RPC_USER=btq RPC_PASS=btqpass ./run_p2mr_rpc_e2e.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATADIR="${DATADIR:-/tmp/p2mr-live-e2e-gdb}"
RPC_USER="${RPC_USER:-btq}"
RPC_PASS="${RPC_PASS:-btqpass}"
RPC_PORT="${RPC_PORT:-28543}"
P2P_PORT="${P2P_PORT:-$((RPC_PORT + 1))}"
WALLET_NAME="${WALLET_NAME:-p2mr}"
TREE='[{"depth":0,"leaf_version":192,"script":"51"}]'
MODE="${MODE:-attach}" # attach|fresh

BTQD="$SCRIPT_DIR/src/btqd"
BTQCLI="$SCRIPT_DIR/src/btq-cli"

if [[ ! -x "$BTQD" || ! -x "$BTQCLI" ]]; then
  echo "[fail] Missing btqd/btq-cli binaries under $SCRIPT_DIR/src"
  echo "       Build first: make -j\"\$(nproc)\""
  exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
  echo "[fail] Missing required dependency: jq"
  echo "       Install jq (e.g.: sudo apt-get install -y jq)"
  exit 1
fi

CLI=( "$BTQCLI" -regtest -datadir="$DATADIR" -rpcuser="$RPC_USER" -rpcpassword="$RPC_PASS" -rpcport="$RPC_PORT" )
WCLI=( "${CLI[@]}" -rpcwallet="$WALLET_NAME" )

blue=$'\033[94m'
green=$'\033[92m'
red=$'\033[91m'
yellow=$'\033[93m'
cyan=$'\033[96m'
purple=$'\033[95m'
bold=$'\033[1m'
dim=$'\033[2m'
reset=$'\033[0m'

STEP_NO=0
START_TS="$(date +%s)"

hr()    { printf '%s\n' "${dim}-----------------------------------------------------------------------${reset}"; }
step()  { STEP_NO=$((STEP_NO + 1)); echo; hr; echo "${purple}${bold}[STEP $STEP_NO]${reset} ${cyan}$*${reset}"; }
ok()    { echo "${green}[ok]${reset} $*"; }
warn()  { echo "${yellow}[warn]${reset} $*"; }
info()  { echo "${blue}[info]${reset} $*"; }
detail(){ echo "       ${dim}- $*${reset}"; }
fail()  { echo "${red}[fail]${reset} $*"; exit 1; }
kv()    { printf "       %-16s %s\n" "$1" "$2"; }

json_get() {
  local json="$1"
  local key="$2"
  local path='.'
  local part
  IFS='.' read -r -a parts <<< "$key"
  for part in "${parts[@]}"; do
    if [[ "$part" =~ ^[0-9]+$ ]]; then
      path+="[$part]"
    else
      path+="[\"$part\"]"
    fi
  done
  jq -r "$path" <<<"$json"
}

json_has_id() {
  local json="$1"
  local p2mr_id="$2"
  jq -e --arg id "$p2mr_id" '.[] | select(.id == $id)' <<<"$json" >/dev/null && echo "true" || echo "false"
}

start_node() {
  "$BTQD" -regtest -datadir="$DATADIR" -server=1 -daemonwait -listen=0 -fallbackfee=0.0001 -rpcuser="$RPC_USER" -rpcpassword="$RPC_PASS" -rpcport="$RPC_PORT" -port="$P2P_PORT"
}

wait_rpc() {
  local attempts=30
  local i
  for ((i=1; i<=attempts; i++)); do
    if "${CLI[@]}" getblockcount >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  return 1
}

wallet_loaded() {
  local wallets
  wallets="$("${CLI[@]}" listwallets 2>/dev/null || echo "[]")"
  jq -e --arg w "$WALLET_NAME" 'index($w) != null' <<<"$wallets" >/dev/null && echo "true" || echo "false"
}

ensure_wallet_has_keys() {
  set +e
  local out rc
  out="$("${WCLI[@]}" getnewaddress 2>&1)"
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "$out"
    return 0
  fi
  if [[ "$out" == *"This wallet has no available keys"* ]]; then
    echo "[warn] Wallet '$WALLET_NAME' has no keys; recreating it for E2E flow" >&2
    set +e
    "${CLI[@]}" unloadwallet "$WALLET_NAME" >/dev/null 2>&1 || true
    rm -rf "$DATADIR/regtest/wallets/$WALLET_NAME"
    set -e
    "${CLI[@]}" createwallet "$WALLET_NAME" >/dev/null
    "${WCLI[@]}" getnewaddress
    return 0
  fi
  echo "$out" >&2
  return $rc
}

restart_node() {
  set +e
  "${CLI[@]}" stop >/dev/null 2>&1 || true
  pkill -f "btqd .* -regtest .* -datadir=$DATADIR" >/dev/null 2>&1 || true
  set -e
  start_node
  wait_rpc || fail "RPC did not come back after restart (see $DATADIR/regtest/debug.log)"
}

cleanup() {
  [[ "${NODE_STARTED_BY_SCRIPT:-0}" == "1" ]] || return 0
  set +e
  "${CLI[@]}" stop >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo
hr
echo "${bold}${cyan}P2MR RPC End-to-End Validator${reset}"
echo "${dim}Validates create/fund/spend/sign/broadcast workflow on regtest.${reset}"
hr
kv "Mode" "$MODE"
kv "Datadir" "$DATADIR"
kv "RPC endpoint" "127.0.0.1:$RPC_PORT"
kv "Wallet" "$WALLET_NAME"
kv "Tree" "$TREE"
hr

if [[ "$MODE" == "fresh" ]]; then
  step "Preparing clean regtest datadir: $DATADIR"
  detail "This removes previous chain and wallet data under the target datadir."
  rm -rf "$DATADIR"
  mkdir -p "$DATADIR"

  step "Starting btqd"
  kv "rpcport" "$RPC_PORT"
  kv "p2pport" "$P2P_PORT"
  detail "Launching isolated regtest node with deterministic RPC credentials."
  start_node
  NODE_STARTED_BY_SCRIPT=1
  wait_rpc || fail "Initial RPC connection failed (see $DATADIR/regtest/debug.log)"
  ok "Node RPC reachable"
else
  step "Using existing btqd (mode=$MODE)"
  detail "Verifying RPC connectivity before proceeding."
  wait_rpc || fail "RPC not reachable at datadir=$DATADIR rpcport=$RPC_PORT. Start btqd first or run MODE=fresh."
  ok "Connected to running node"
fi

step "Creating/loading wallet"
detail "Ensures the wallet exists and is available for RPC operations."
if [[ "$(wallet_loaded)" == "true" ]]; then
  ok "Wallet '$WALLET_NAME' already loaded"
elif "${CLI[@]}" loadwallet "$WALLET_NAME" >/dev/null 2>&1; then
  ok "Wallet '$WALLET_NAME' loaded"
else
  if "${CLI[@]}" createwallet "$WALLET_NAME" >/dev/null 2>&1; then
    ok "Wallet '$WALLET_NAME' created"
  else
    warn "createwallet failed; attempting recovery (node restart + loadwallet/createwallet)"
    if ! wait_rpc; then
      warn "RPC dropped after createwallet; restarting node"
      restart_node
    fi
    if [[ "$(wallet_loaded)" == "true" ]]; then
      ok "Wallet '$WALLET_NAME' already loaded after recovery"
    elif "${CLI[@]}" loadwallet "$WALLET_NAME" >/dev/null 2>&1; then
      ok "Wallet '$WALLET_NAME' loaded after recovery"
    elif "${CLI[@]}" createwallet "$WALLET_NAME" >/dev/null 2>&1; then
      ok "Wallet '$WALLET_NAME' created after recovery"
    elif [[ "$(wallet_loaded)" == "true" ]]; then
      ok "Wallet '$WALLET_NAME' became loaded after create retry"
    else
      fail "Unable to create or load wallet '$WALLET_NAME' (see $DATADIR/regtest/debug.log)"
    fi
  fi
fi

step "Mining spendable balance"
detail "Mines 110 blocks so coinbase outputs are mature and spendable."
MINER_ADDR="$(ensure_wallet_has_keys)"
"${WCLI[@]}" generatetoaddress 110 "$MINER_ADDR" >/dev/null
BAL="$("${WCLI[@]}" getbalance)"
ok "Wallet funded (balance=$BAL)"
kv "miner address" "$MINER_ADDR"

step "Configuring fixed wallet fee for deterministic funding"
detail "Avoids dependence on fee estimation/fallbackfee behavior."
"${WCLI[@]}" settxfee 0.00010000 >/dev/null
ok "Wallet fee set to 0.00010000"

step "Ensuring P2MR RPC is available"
detail "Checks wallet RPC command registration."
"${WCLI[@]}" help getnewp2mraddress >/dev/null
ok "getnewp2mraddress is registered"

step "Creating P2MR destination"
detail "Creates and stores metadata-backed P2MR destination."
CREATED="$("${WCLI[@]}" getnewp2mraddress "$TREE")"
P2MR_ID="$(json_get "$CREATED" "p2mr_id")"
P2MR_ADDR="$(json_get "$CREATED" "address")"
ok "Created P2MR address=$P2MR_ADDR id=$P2MR_ID"

LISTED="$("${WCLI[@]}" listp2mr)"
HAS_ID="$(json_has_id "$LISTED" "$P2MR_ID")"
[[ "$HAS_ID" == "true" ]] || fail "listp2mr does not contain id=$P2MR_ID"
ok "listp2mr includes created id"

step "Funding P2MR via sendtop2mr"
detail "Sends 1.0 BTQ to a newly generated metadata-tracked P2MR output."
FUNDED="$("${WCLI[@]}" sendtop2mr "$TREE" 1.0 "demo-p2mr")"
FUND_TXID="$(json_get "$FUNDED" "txid")"
FUND_ID="$(json_get "$FUNDED" "p2mr_id")"
"${WCLI[@]}" generatetoaddress 1 "$MINER_ADDR" >/dev/null
ok "Funded P2MR txid=$FUND_TXID id=$FUND_ID"

step "Reading stored P2MR metadata"
INFO="$("${WCLI[@]}" getp2mrinfo "$FUND_ID")"
INFO_ID="$(json_get "$INFO" "id")"
[[ "$INFO_ID" == "$FUND_ID" ]] || fail "getp2mrinfo returned mismatched id ($INFO_ID != $FUND_ID)"
ok "getp2mrinfo works"

step "Creating unsigned spend from P2MR"
detail "Builds unsigned tx spending funded P2MR UTXO to a fresh wallet address."
DEST="$("${WCLI[@]}" getnewaddress)"
UNSIGNED="$("${WCLI[@]}" createp2mrspend "$FUND_ID" "$DEST" 0.5)"
UNSIGNED_HEX="$(json_get "$UNSIGNED" "hex")"
[[ -n "$UNSIGNED_HEX" ]] || fail "createp2mrspend returned empty hex"
ok "Unsigned spend created"
kv "destination" "$DEST"

step "Signing P2MR spend"
detail "Finalizes witness for P2MR input."
SIGNED="$("${WCLI[@]}" signp2mrtransaction "$UNSIGNED_HEX" "$FUND_ID")"
SIGNED_HEX="$(json_get "$SIGNED" "hex")"
COMPLETE="$(json_get "$SIGNED" "complete")"
[[ "$COMPLETE" == "true" ]] || fail "signp2mrtransaction did not complete"
ok "P2MR spend signed"

step "Dry-run mempool accept"
TEST_RES="$("${WCLI[@]}" testp2mrtransaction "$SIGNED_HEX")"
ALLOWED="$(json_get "$TEST_RES" "0.allowed")"
[[ "$ALLOWED" == "true" ]] || fail "testp2mrtransaction rejected tx"
ok "testp2mrtransaction allowed=true"

step "Broadcasting and mining signed spend"
detail "Broadcasts signed tx and mines one block to confirm it."
SPEND_TXID="$("${CLI[@]}" sendrawtransaction "$SIGNED_HEX")"
"${WCLI[@]}" generatetoaddress 1 "$MINER_ADDR" >/dev/null
TXV="$("${WCLI[@]}" gettransaction "$SPEND_TXID" true)"
CONF="$(json_get "$TXV" "confirmations")"
[[ "${CONF:-0}" -gt 0 ]] || fail "Spend tx not confirmed"
ok "Spend tx confirmed (txid=$SPEND_TXID, confirmations=$CONF)"

step "Negative checks"
set +e
"${WCLI[@]}" getp2mrinfo "does-not-exist" >/dev/null 2>&1
rc_missing=$?
"${WCLI[@]}" getnewp2mraddress '[{"depth":999,"leaf_version":192,"script":"51"}]' >/dev/null 2>&1
rc_tree=$?
set -e
[[ $rc_missing -ne 0 ]] || fail "Expected getp2mrinfo unknown-id failure"
[[ $rc_tree -ne 0 ]] || fail "Expected invalid-tree rejection"
ok "Negative checks passed"

echo
hr
DURATION="$(( $(date +%s) - START_TS ))"
ok "All P2MR RPC E2E checks passed."
info "You can inspect logs under: $DATADIR/regtest/debug.log"
info "Tip: use MODE=fresh to run on a clean temporary chain."
kv "Elapsed" "${DURATION}s"
hr
