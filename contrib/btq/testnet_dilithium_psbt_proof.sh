#!/usr/bin/env bash
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Proves the BTQ-PSBT Dilithium extension end to end against a live chain.
#
# Three wallets each hold one Dilithium key, register the same 2-of-3 P2MR
# multisig, and then spend from it by passing base64 PSBTs between themselves.
# Nothing but the PSBT string moves between wallets, so the spend can only
# succeed if the PSBT itself carries the leaf script, control block and
# Dilithium partial signatures.
#
# Usage: testnet_dilithium_psbt_proof.sh <btq-cli invocation...>
#   e.g. testnet_dilithium_psbt_proof.sh src/btq-cli -testnet -rpcport=18355 ...

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <btq-cli invocation...>" >&2
    exit 1
fi

CLI=("$@")
btq() { "${CLI[@]}" "$@"; }
wallet() { local name="$1"; shift; "${CLI[@]}" "-rpcwallet=$name" "$@"; }

FUNDER=psbtproof_funder
SIGNERS=(psbtproof_alice psbtproof_bob psbtproof_carol)
REQUIRED=2

log() { printf '\n=== %s\n' "$*"; }

ensure_wallet() {
    local name="$1"
    if btq listwallets | grep -q "\"$name\""; then return; fi
    btq loadwallet "$name" >/dev/null 2>&1 || btq createwallet "$name" false false "" false true true >/dev/null
}

log "chain status"
btq getblockchaininfo | grep -E '"chain"|"blocks"|"difficulty"'

log "wallets"
for name in "$FUNDER" "${SIGNERS[@]}"; do
    ensure_wallet "$name"
    echo "  $name"
done

log "each co-signer publishes one Dilithium pubkey"
PUBKEYS=()
for name in "${SIGNERS[@]}"; do
    id=$(wallet "$name" getnewdilithiumaddress "psbt-proof" | jq -r .p2mr_id)
    pk=$(wallet "$name" getdilithiumpubkey "$id" | jq -r '.pubkeys[0].pubkey')
    PUBKEYS+=("$pk")
    echo "  $name ${pk:0:32}... (${#pk} hex chars)"
done

log "every co-signer registers the same ${REQUIRED}-of-${#SIGNERS[@]} multisig"
PUBKEY_JSON=$(printf '%s\n' "${PUBKEYS[@]}" | jq -R . | jq -sc .)
ADDRESS=""
for name in "${SIGNERS[@]}"; do
    out=$(wallet "$name" createdilithiummultisig "$REQUIRED" "$PUBKEY_JSON" "psbt-proof")
    addr=$(echo "$out" | jq -r .address)
    echo "  $name -> $addr (can sign with $(echo "$out" | jq -r .signers_available) key)"
    if [ -z "$ADDRESS" ]; then
        ADDRESS="$addr"
        LEAF_SCRIPT=$(echo "$out" | jq -r .leaf_script)
        SCRIPT_PUBKEY=$(echo "$out" | jq -r .scriptPubKey)
    elif [ "$addr" != "$ADDRESS" ]; then
        echo "co-signers derived different addresses" >&2
        exit 1
    fi
done
echo "  address:      $ADDRESS"
echo "  scriptPubKey: $SCRIPT_PUBKEY"
echo "  leaf script:  ${#LEAF_SCRIPT} hex chars"

log "funding the multisig"
BALANCE=$(wallet "$FUNDER" getbalance)
echo "  funder balance: $BALANCE"
if [ "$(echo "$BALANCE < 0.001" | bc -l)" = "1" ]; then
    echo "funder has no spendable coins; mine to $(wallet "$FUNDER" getnewaddress) first" >&2
    exit 1
fi
FUND_TXID=$(wallet "$FUNDER" sendtoaddress "$ADDRESS" 0.001)
echo "  funding txid: $FUND_TXID"

log "waiting for the funding transaction to confirm"
until [ "$(wallet "$FUNDER" gettransaction "$FUND_TXID" | jq -r .confirmations)" -ge 1 ]; do
    sleep 15
done
VOUT=$(wallet "$FUNDER" gettransaction "$FUND_TXID" true true |
       jq --arg a "$ADDRESS" '.decoded.vout[] | select(.scriptPubKey.address == $a) | .n')
echo "  confirmed at ${FUND_TXID}:${VOUT}"

log "alice builds the unsigned PSBT"
DEST=$(wallet "$FUNDER" getnewaddress)
PSBT=$(wallet "${SIGNERS[0]}" walletcreatefundedpsbt \
        "[{\"txid\":\"$FUND_TXID\",\"vout\":$VOUT}]" "[{\"$DEST\":0.0005}]" | jq -r .psbt)
btq decodepsbt "$PSBT" | jq '.inputs[0] | {p2mr_merkle_root, leaf: .p2mr_scripts[0] | {leaf_ver, dilithium_policy, dilithium_required, dilithium_total}, p2mr_dilithium}'

log "alice signs (below threshold)"
PSBT_A=$(wallet "${SIGNERS[0]}" walletprocesspsbt "$PSBT" | jq -r .psbt)
btq decodepsbt "$PSBT_A" | jq '.inputs[0].p2mr_dilithium'

log "bob signs the base64 PSBT alice handed over"
RESULT=$(wallet "${SIGNERS[1]}" walletprocesspsbt "$PSBT_A")
PSBT_AB=$(echo "$RESULT" | jq -r .psbt)
echo "  complete: $(echo "$RESULT" | jq -r .complete)"
btq decodepsbt "$PSBT_AB" | jq '.inputs[0].p2mr_dilithium'

log "finalize and broadcast"
FINAL=$(btq finalizepsbt "$PSBT_AB")
echo "  complete: $(echo "$FINAL" | jq -r .complete)"
RAW=$(echo "$FINAL" | jq -r .hex)
btq testmempoolaccept "[\"$RAW\"]" | jq -c '.[0] | {txid, allowed, "reject-reason"}'
SPEND_TXID=$(btq sendrawtransaction "$RAW")
echo "  spend txid: $SPEND_TXID"

log "waiting for the spend to confirm"
until [ "$(wallet "${SIGNERS[0]}" gettransaction "$SPEND_TXID" | jq -r .confirmations)" -ge 1 ]; do
    sleep 15
done
CONF=$(wallet "${SIGNERS[0]}" gettransaction "$SPEND_TXID" true true)
echo "  confirmations: $(echo "$CONF" | jq -r .confirmations)"
echo "  blockhash:     $(echo "$CONF" | jq -r .blockhash)"
echo "  witness items: $(echo "$CONF" | jq -r '.decoded.vin[0].txinwitness | length')"
echo "  witness sizes: $(echo "$CONF" | jq -c '[.decoded.vin[0].txinwitness[] | (length / 2)]')"

log "PROVEN: 2-of-3 Dilithium multisig spent via PSBT interchange"
echo "  funding: $FUND_TXID"
echo "  spend:   $SPEND_TXID"
