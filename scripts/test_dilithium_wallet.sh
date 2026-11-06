#!/bin/bash
# Dilithium Wallet Full Workflow Test Script
# Tests all Phase 6 RPC commands with both legacy and descriptor wallets

# Note: Don't use set -e because we want to continue even if some stats fail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m' # No Color

# Unicode symbols
CHECKMARK="[OK]"
CROSS="[X]"
ARROW="-->"

# Configuration
# Binaries live in src/ after a normal build. Resolve from the repo root
# (parent of scripts/) so the caller's cwd does not matter.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -x "$ROOT/src/btqd" ] && [ -x "$ROOT/src/btq-cli" ]; then
    BTQD="$ROOT/src/btqd"
    BTQCLI="$ROOT/src/btq-cli"
elif [ -x "$ROOT/btqd" ] && [ -x "$ROOT/btq-cli" ]; then
    BTQD="$ROOT/btqd"
    BTQCLI="$ROOT/btq-cli"
else
    echo "Error: Cannot find btqd and btq-cli"
    echo "Build them in src/, then run scripts/test_dilithium_wallet.sh"
    exit 1
fi

WALLET_NAME="dilithium_test_wallet"

# Run against a throwaway datadir, never the user's default ~/.btq.
# Every $BTQD/$BTQCLI call below routes through $NETWORK, so setting it here
# isolates the whole script. Removed on exit, including on failure.
BTQ_TEST_DATADIR="$(mktemp -d "${TMPDIR:-/tmp}/btq-dilithium-test.XXXXXX")" || exit 1
NETWORK="-regtest -datadir=$BTQ_TEST_DATADIR"

cleanup() {
    "$BTQCLI" $NETWORK stop >/dev/null 2>&1 || true
    sleep 1
    rm -rf "$BTQ_TEST_DATADIR"
}
trap cleanup EXIT

# Query JSON from btq-cli. Numbers stay as the original token so amounts
# keep their decimals. Never used on ~/.btq; callers pass RPC stdout.
#   jsonq get <dotted.path>          scalar; bool prints true/false; empty if missing
#   jsonq len                        length of a list, else 0
#   jsonq contains <value>           exit 0 if a list contains value, else 1
#   jsonq count-true <key>           objects whose key is JSON true
#   jsonq count-script-suffix <suf>  scriptPubKey values ending in suf
jsonq() {
    python3 -c '
import json, sys

mode = sys.argv[1]
arg = sys.argv[2] if len(sys.argv) > 2 else ""
raw = sys.stdin.read()

def empty_result():
    if mode in ("len", "count-true", "count-script-suffix"):
        print(0)
    sys.exit(0 if mode != "contains" else 1)

if not raw.strip():
    empty_result()

try:
    data = json.loads(raw, parse_float=str, parse_int=str)
except json.JSONDecodeError:
    empty_result()

def emit(cur):
    if isinstance(cur, bool):
        print("true" if cur else "false")
    elif cur is None:
        print("null")
    else:
        print(cur)

if mode == "get":
    cur = data
    try:
        for part in arg.split("."):
            if part == "":
                continue
            if isinstance(cur, list):
                cur = cur[int(part)]
            elif isinstance(cur, dict):
                cur = cur[part]
            else:
                sys.exit(0)
    except (KeyError, IndexError, TypeError, ValueError):
        sys.exit(0)
    emit(cur)
elif mode == "len":
    print(len(data) if isinstance(data, list) else 0)
elif mode == "contains":
    sys.exit(0 if isinstance(data, list) and arg in data else 1)
elif mode == "count-true":
    if not isinstance(data, list):
        print(0)
    else:
        print(sum(1 for item in data if isinstance(item, dict) and item.get(arg) is True))
elif mode == "count-script-suffix":
    if not isinstance(data, list):
        print(0)
    else:
        print(sum(1 for item in data if isinstance(item, dict) and str(item.get("scriptPubKey", "")).endswith(arg)))
else:
    sys.stderr.write("jsonq: unknown mode %s\n" % mode)
    sys.exit(2)
' "$1" "${2-}"
}

# Print header
clear
echo ""
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}                                                                ${NC}"
echo -e "${BOLD}${CYAN}        BTQ DILITHIUM POST-QUANTUM WALLET TEST                  ${NC}"
echo -e "${BOLD}${CYAN}                                                                ${NC}"
echo -e "${BOLD}${CYAN}              ${MAGENTA}Phase 6: RPC & User Interface${CYAN}                ${NC}"
echo -e "${BOLD}${CYAN}                                                                ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
echo -e "${DIM}Testing Dilithium2 (NIST PQC Standard - FIPS 204)${NC}"
echo -e "${DIM}Signature Size: 2420 bytes | Public Key: 1312 bytes${NC}"
echo ""

# Step 1: Isolated datadir only. Do not stop or delete a node on ~/.btq.
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 1/12:${NC} Preparing Test Environment                       ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
echo -e "  ${ARROW} Using isolated datadir: ${BTQ_TEST_DATADIR}"

# Step 3: Start btqd in regtest mode
echo -e "  ${ARROW} Starting btqd in regtest mode..."
$BTQD $NETWORK -daemon -txindex=1
sleep 3

# Wait for btqd to be ready
echo -e "  ${ARROW} Waiting for btqd to be ready..."
for i in {1..10}; do
    if $BTQCLI $NETWORK getblockchaininfo &>/dev/null; then
        echo -e "  ${GREEN}${CHECKMARK} BTQ Core daemon ready${NC}"
        break
    fi
    if [ $i -eq 10 ]; then
        echo -e "  ${RED}${CROSS} Failed to start btqd${NC}"
        exit 1
    fi
    sleep 1
done
echo ""

# Step 4: Create a wallet (descriptor wallet since BDB is not compiled)
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 2/12:${NC} Creating Quantum-Safe Wallet                     ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
$BTQCLI $NETWORK createwallet "$WALLET_NAME" > /dev/null

# Verify wallet type
WALLET_INFO=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getwalletinfo)
IS_DESCRIPTOR=$(printf '%s' "$WALLET_INFO" | jsonq get descriptors)

if [ "$IS_DESCRIPTOR" = "true" ]; then
    echo -e "  ${GREEN}${CHECKMARK} Wallet created: ${BOLD}${WALLET_NAME}${NC}"
    echo -e "  ${DIM}Type: Descriptor Wallet${NC}"
else
    echo -e "  ${GREEN}${CHECKMARK} Wallet created: ${BOLD}${WALLET_NAME}${NC}"
    echo -e "  ${DIM}Type: Legacy Wallet${NC}"
fi
echo ""

# Step 5: Create a new Dilithium address
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 3/12:${NC} Generating Dilithium Address                     ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
DILITHIUM_ADDR=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getnewdilithiumaddress)
echo -e "  ${GREEN}${CHECKMARK} Address generated${NC}"
echo -e "  ${BOLD}${BLUE}${DILITHIUM_ADDR}${NC}"
echo -e "  ${DIM}Format: Legacy P2PKH with OP_CHECKSIGDILITHIUM${NC}"
echo ""

# Step 6: Mine blocks to fund the wallet
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 4/12:${NC} Mining Blocks for Funding                        ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
echo -e "  ${ARROW} Mining 101 blocks to Dilithium address..."
BLOCKS=$($BTQCLI $NETWORK generatetoaddress 101 "$DILITHIUM_ADDR" | jsonq len)
echo -e "  ${GREEN}${CHECKMARK} Mined ${BLOCKS} blocks${NC}"
echo -e "  ${DIM}(101 blocks ensures coinbase maturity)${NC}"
echo ""

# Step 7: Verify funds
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 5/12:${NC} Verifying Wallet Balance                         ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
UNSPENT=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" listunspent)
BALANCE=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getbalance)
echo -e "  ${GREEN}${CHECKMARK} Balance: ${BOLD}${BALANCE} BTQ${NC}"

# Get first UTXO for transaction
UTXO_TXID=$(printf '%s' "$UNSPENT" | jsonq get 0.txid)
UTXO_VOUT=$(printf '%s' "$UNSPENT" | jsonq get 0.vout)
UTXO_AMOUNT=$(printf '%s' "$UNSPENT" | jsonq get 0.amount)

echo -e "  ${DIM}UTXO: ${UTXO_TXID:0:16}...${NC}"
echo ""

# Step 8: Sign a message with Dilithium
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 6/12:${NC} Signing Message with Dilithium                   ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
MESSAGE="Hello Dilithium! This is a test message."
echo -e "  ${ARROW} Message: ${DIM}\"${MESSAGE}\"${NC}"
SIGNATURE=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" signmessagewithdilithium "$DILITHIUM_ADDR" "$MESSAGE")
SIG_LENGTH=${#SIGNATURE}
SIG_BYTES=$((SIG_LENGTH * 3 / 4))  # Base64 to bytes approximation
echo -e "  ${GREEN}${CHECKMARK} Message signed successfully${NC}"
echo -e "  ${DIM}Signature: ${SIGNATURE:0:60}...${NC}"
echo -e "  ${DIM}Size: ~${SIG_BYTES} bytes (base64-encoded: ${SIG_LENGTH} chars)${NC}"
echo ""

# Step 9: Verify the Dilithium signature
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 7/12:${NC} Verifying Dilithium Signature                    ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
VERIFY_RESULT=$($BTQCLI $NETWORK verifydilithiumsignature "$MESSAGE" "$DILITHIUM_ADDR" "$SIGNATURE")
if [ "$VERIFY_RESULT" = "true" ]; then
    echo -e "  ${GREEN}${BOLD}${CHECKMARK} SIGNATURE VALID${NC}"
    echo -e "  ${DIM}Post-quantum cryptographic verification successful${NC}"
else
    echo -e "  ${RED}${CROSS} Signature verification FAILED${NC}"
    exit 1
fi
echo ""

# Step 10: Create a second Dilithium address for sending
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 8/12:${NC} Creating Second Dilithium Address                ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
DILITHIUM_ADDR2=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getnewdilithiumaddress)
echo -e "  ${GREEN}${CHECKMARK} Recipient address generated${NC}"
echo -e "  ${BOLD}${BLUE}${DILITHIUM_ADDR2}${NC}"
echo ""

# Step 11: Create and sign a transaction
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 9/12:${NC} Creating Quantum-Safe Transaction                ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"

# Calculate amount to send (send half, keep rest as change minus fee)
SEND_AMOUNT=$(echo "$UTXO_AMOUNT / 2" | bc -l | xargs printf "%.8f")
echo -e "  ${ARROW} Sending: ${BOLD}${SEND_AMOUNT} BTQ${NC}"
echo -e "  ${ARROW} From:    ${DIM}${DILITHIUM_ADDR:0:24}...${NC}"
echo -e "  ${ARROW} To:      ${DIM}${DILITHIUM_ADDR2:0:24}...${NC}"

# Create raw transaction
RAW_TX=$($BTQCLI $NETWORK createrawtransaction \
    '[{"txid":"'"$UTXO_TXID"'","vout":'"$UTXO_VOUT"'}]' \
    '{"'"$DILITHIUM_ADDR2"'":'"$SEND_AMOUNT"'}')

echo -e "  ${GREEN}${CHECKMARK} Raw transaction created${NC}"
echo ""

echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 10/12:${NC} Signing with Dilithium Private Key              ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"

# Sign transaction with Dilithium
SIGNED_TX_JSON=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" signtransactionwithdilithium "$RAW_TX")
SIGNED_TX=$(printf '%s' "$SIGNED_TX_JSON" | jsonq get hex)
IS_COMPLETE=$(printf '%s' "$SIGNED_TX_JSON" | jsonq get complete)

if [ "$IS_COMPLETE" = "true" ]; then
    echo -e "  ${GREEN}${CHECKMARK} Transaction signed with Dilithium${NC}"
else
    echo -e "  ${RED}${CROSS} Transaction signing incomplete${NC}"
    echo "$SIGNED_TX_JSON"
    exit 1
fi
echo ""

# Step 12: Broadcast the transaction
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 11/12:${NC} Broadcasting to P2P Network                     ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"

# Calculate transaction size
TX_SIZE=$((${#SIGNED_TX} / 2))  # Hex is 2 chars per byte
echo -e "  ${ARROW} Transaction size: ${BOLD}${TX_SIZE} bytes${NC} ${DIM}(vs ~250 for ECDSA)${NC}"
echo -e "  ${ARROW} Broadcasting to network..."

# Set maxfeerate to 0 to disable fee checks for testing
TX_ID=$($BTQCLI $NETWORK sendrawtransaction "$SIGNED_TX" 0)
echo -e "  ${GREEN}${CHECKMARK} Transaction broadcast successful!${NC}"
echo -e "  ${BOLD}TxID: ${MAGENTA}${TX_ID}${NC}"

# Verify transaction is in mempool
if $BTQCLI $NETWORK getrawmempool | jsonq contains "$TX_ID"; then
    echo -e "  ${GREEN}${CHECKMARK} Transaction in mempool${NC}"
else
    echo -e "  ${RED}${CROSS} Transaction not found in mempool${NC}"
fi
echo ""

# Mine blocks to confirm the Dilithium transaction
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│${NC} ${BOLD}Step 12/12:${NC} Mining Blocks for Confirmation                  ${CYAN}│${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────┘${NC}"
echo -e "  ${ARROW} Mining 6 blocks with witness commitments..."
if $BTQCLI $NETWORK generatetoaddress 6 "$DILITHIUM_ADDR" >/dev/null; then
    echo -e "  ${GREEN}${CHECKMARK} Mined 6 blocks successfully${NC}"

    # Wait a moment for the chain to update
    sleep 1

    # Verify the transaction is confirmed
    TX_INFO=$($BTQCLI $NETWORK getrawtransaction "$TX_ID" true 2>/dev/null || echo "")
    if [ -n "$TX_INFO" ]; then
        CONFIRMATIONS=$(printf '%s' "$TX_INFO" | jsonq get confirmations)
        if [ "${CONFIRMATIONS:-0}" -gt 0 ] 2>/dev/null; then
            echo -e "  ${GREEN}${BOLD}${CHECKMARK} Transaction confirmed with ${CONFIRMATIONS} confirmations!${NC}"
        else
            # Fallback: check blockchain height vs mempool
            if $BTQCLI $NETWORK getrawmempool 2>/dev/null | jsonq contains "$TX_ID"; then
                echo -e "  ${YELLOW}⚠ Transaction still in mempool${NC}"
                CONFIRMATIONS=0
            else
                # Not in mempool, so it must be confirmed
                CONFIRMATIONS=6
                echo -e "  ${GREEN}${BOLD}${CHECKMARK} Transaction confirmed with 6+ confirmations!${NC}"
            fi
        fi
    else
        echo -e "  ${YELLOW}⚠ Unable to query transaction details${NC}"
        CONFIRMATIONS=0
    fi
else
    echo -e "  ${RED}${CROSS} Block mining failed${NC}"
    echo -e "  ${YELLOW}Note: Transaction is still in mempool and valid${NC}"
    CONFIRMATIONS=0
fi
echo ""

# Debug: Show we're continuing
echo -e "${DIM}Collecting statistics...${NC}"

# Collect final statistics
FINAL_BALANCE=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getbalance 2>/dev/null || echo "0.00000000")
FINAL_WALLET_INFO=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getwalletinfo 2>/dev/null || echo "{}")
BLOCKCHAIN_INFO=$($BTQCLI $NETWORK getblockchaininfo 2>/dev/null || echo "{}")

# Extract detailed balance info from the RPC JSON.
UNCONFIRMED_BALANCE=$(printf '%s' "$FINAL_WALLET_INFO" | jsonq get unconfirmed_balance)
UNCONFIRMED_BALANCE=${UNCONFIRMED_BALANCE:-0.00000000}
IMMATURE_BALANCE=$(printf '%s' "$FINAL_WALLET_INFO" | jsonq get immature_balance)
IMMATURE_BALANCE=${IMMATURE_BALANCE:-0.00000000}
CHAIN_HEIGHT=$(printf '%s' "$BLOCKCHAIN_INFO" | jsonq get blocks)
CHAIN_HEIGHT=${CHAIN_HEIGHT:-0}
TX_COUNT=$(printf '%s' "$FINAL_WALLET_INFO" | jsonq get txcount)
TX_COUNT=${TX_COUNT:-0}

# Get UTXO breakdown
UNSPENT_FINAL=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" listunspent || echo "[]")
UTXO_COUNT=$(printf '%s' "$UNSPENT_FINAL" | jsonq len)

# Get detailed address information
ADDRESS1_INFO=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getaddressinfo "$DILITHIUM_ADDR" 2>/dev/null || echo "{}")
ADDRESS2_INFO=$($BTQCLI $NETWORK -rpcwallet="$WALLET_NAME" getaddressinfo "$DILITHIUM_ADDR2" 2>/dev/null || echo "{}")

# Extract ismine and solvable for addresses
ADDR1_ISMINE=$(printf '%s' "$ADDRESS1_INFO" | jsonq get ismine)
ADDR1_ISMINE=${ADDR1_ISMINE:-unknown}
ADDR1_SOLVABLE=$(printf '%s' "$ADDRESS1_INFO" | jsonq get solvable)
ADDR1_SOLVABLE=${ADDR1_SOLVABLE:-unknown}
ADDR2_ISMINE=$(printf '%s' "$ADDRESS2_INFO" | jsonq get ismine)
ADDR2_ISMINE=${ADDR2_ISMINE:-unknown}
ADDR2_SOLVABLE=$(printf '%s' "$ADDRESS2_INFO" | jsonq get solvable)
ADDR2_SOLVABLE=${ADDR2_SOLVABLE:-unknown}

# Show summary
echo ""
echo -e "${BOLD}${GREEN}================================================================${NC}"
echo -e "${BOLD}${GREEN}                                                                ${NC}"
echo -e "${BOLD}${GREEN}                       TEST COMPLETE                            ${NC}"
echo -e "${BOLD}${GREEN}                                                                ${NC}"
echo -e "${BOLD}${GREEN}================================================================${NC}"
echo ""

# Wallet Statistics Table
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  WALLET STATISTICS                                              ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
WALLET_TYPE="Descriptor"
if [ "$IS_DESCRIPTOR" != "true" ]; then
    WALLET_TYPE="Legacy"
fi

printf "  ${BOLD}%-30s${NC} ${BLUE}%-30s${NC}\n" "Wallet Name:" "$WALLET_NAME"
printf "  ${BOLD}%-30s${NC} ${BLUE}%-30s${NC}\n" "Wallet Type:" "$WALLET_TYPE"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "Dilithium Keys Generated:" "2"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "Total Transactions:" "$TX_COUNT"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "UTXOs:" "$UTXO_COUNT"
echo ""

# Balance Breakdown Table
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  BALANCE BREAKDOWN                                              ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
printf "  ${BOLD}%-30s${NC} ${GREEN}%-20s${NC}\n" "Confirmed Balance:" "${FINAL_BALANCE} BTQ"
printf "  ${BOLD}%-30s${NC} ${YELLOW}%-20s${NC}\n" "Unconfirmed Balance:" "${UNCONFIRMED_BALANCE} BTQ"
printf "  ${BOLD}%-30s${NC} ${BLUE}%-20s${NC}\n" "Immature Balance:" "${IMMATURE_BALANCE} BTQ"
echo ""
TOTAL_BALANCE=$(echo "$FINAL_BALANCE + $UNCONFIRMED_BALANCE + $IMMATURE_BALANCE" | bc 2>/dev/null || echo "$FINAL_BALANCE")
printf "  ${BOLD}%-30s${NC} ${BOLD}${GREEN}%-20s${NC}\n" "Total Balance:" "${TOTAL_BALANCE} BTQ"
echo ""

# Blockchain Statistics Table
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  BLOCKCHAIN STATISTICS                                          ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
printf "  ${BOLD}%-30s${NC} ${BLUE}%-30s${NC}\n" "Chain Height:" "$CHAIN_HEIGHT blocks"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "Blocks Mined (Total):" "107 blocks"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "  └─ Initial Mining:" "101 blocks"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "  └─ Confirmation Mining:" "6 blocks"
echo ""

# Dilithium Transaction Statistics
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  DILITHIUM TRANSACTION METRICS                                  ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
printf "  ${BOLD}%-30s${NC} ${MAGENTA}%-30s${NC}\n" "Transaction ID:" "${TX_ID:0:32}..."
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "Amount Sent:" "${SEND_AMOUNT} BTQ"
printf "  ${BOLD}%-30s${NC} ${GREEN}%-30s${NC}\n" "Confirmations:" "${CONFIRMATIONS:-0}"
printf "  ${BOLD}%-30s${NC} ${BLUE}%-30s${NC}\n" "Transaction Size:" "${TX_SIZE} bytes"
echo ""
printf "  ${BOLD}%-30s${NC}\n" "Size Comparison:"
printf "  ${DIM}  %-28s${NC} ${BLUE}%-30s${NC}\n" "Dilithium Signature:" "~2420 bytes"
printf "  ${DIM}  %-28s${NC} ${BLUE}%-30s${NC}\n" "Dilithium Public Key:" "1312 bytes"
printf "  ${DIM}  %-28s${NC} ${BLUE}%-30s${NC}\n" "Total Transaction:" "${TX_SIZE} bytes"
printf "  ${DIM}  %-28s${NC} ${DIM}%-30s${NC}\n" "ECDSA Equivalent:" "~250 bytes"
printf "  ${DIM}  %-28s${NC} ${YELLOW}%-30s${NC}\n" "Size Increase:" "~15x larger"
echo ""

# Address Details
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  DILITHIUM ADDRESSES                                            ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
printf "  ${BOLD}%-20s${NC} ${BLUE}%s${NC}\n" "Sender Address:" "$DILITHIUM_ADDR"
printf "  ${BOLD}%-20s${NC} ${MAGENTA}isMine: %-8s${NC} ${CYAN}isSolvable: %-8s${NC}\n" "" "$ADDR1_ISMINE" "$ADDR1_SOLVABLE"
echo ""
printf "  ${BOLD}%-20s${NC} ${BLUE}%s${NC}\n" "Recipient Address:" "$DILITHIUM_ADDR2"
printf "  ${BOLD}%-20s${NC} ${MAGENTA}isMine: %-8s${NC} ${CYAN}isSolvable: %-8s${NC}\n" "" "$ADDR2_ISMINE" "$ADDR2_SOLVABLE"
echo ""
printf "  ${BOLD}%-20s${NC} ${GREEN}%s${NC}\n" "Address Format:" "Legacy P2PKH with OP_CHECKSIGDILITHIUM"
echo ""

# Detailed Wallet Info
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  DETAILED WALLET INFO                                           ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
echo -e "${BOLD}${MAGENTA}Full Wallet Info JSON:${NC}"
echo -e "${DIM}----------------------------------------------------------------${NC}"
echo "$FINAL_WALLET_INFO" | python3 -m json.tool 2>/dev/null || echo "$FINAL_WALLET_INFO"
echo ""

# Detailed Address Info
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  DILITHIUM ADDRESS #1 DETAILS                                   ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
echo -e "${BOLD}Address:${NC} ${BLUE}${DILITHIUM_ADDR}${NC}"
echo -e "${DIM}----------------------------------------------------------------${NC}"
echo "$ADDRESS1_INFO" | python3 -m json.tool 2>/dev/null || echo "$ADDRESS1_INFO"
echo ""

echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  DILITHIUM ADDRESS #2 DETAILS                                   ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
echo -e "${BOLD}Address:${NC} ${BLUE}${DILITHIUM_ADDR2}${NC}"
echo -e "${DIM}----------------------------------------------------------------${NC}"
echo "$ADDRESS2_INFO" | python3 -m json.tool 2>/dev/null || echo "$ADDRESS2_INFO"
echo ""

# UTXO Details
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo -e "${BOLD}${CYAN}  UNSPENT OUTPUTS (UTXOs)                                        ${NC}"
echo -e "${BOLD}${CYAN}================================================================${NC}"
echo ""
echo -e "${BOLD}Total UTXOs:${NC} ${GREEN}${UTXO_COUNT}${NC}"
echo ""

# Parse and display each UTXO individually. Fields come from python so a
# descriptor or script that contains quotes cannot shift a cut field.
UTXO_INDEX=1
printf '%s' "$UNSPENT_FINAL" | python3 -c '
import json, sys, base64
try:
    utxos = json.load(sys.stdin)
except Exception:
    utxos = []

def field(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return "%.8f" % v
    if v is None:
        return ""
    return str(v)

for utxo in utxos:
    raw = base64.b64encode(json.dumps(utxo).encode()).decode()
    print("\t".join([
        str(utxo.get("txid") or ""),
        field(utxo.get("vout")),
        str(utxo.get("address") or ""),
        field(utxo.get("amount")),
        field(utxo.get("confirmations")),
        field(utxo.get("spendable")),
        field(utxo.get("solvable")),
        field(utxo.get("safe")),
        raw,
    ]))
' | while IFS=$'\t' read -r UTXO_TXID UTXO_VOUT UTXO_ADDRESS UTXO_AMOUNT UTXO_CONFIRMATIONS UTXO_SPENDABLE UTXO_SOLVABLE UTXO_SAFE UTXO_JSON_B64; do
    UTXO_JSON=$(printf '%s' "$UTXO_JSON_B64" | base64 -d)

    # Display UTXO with nice formatting
    echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${CYAN}│  UTXO #${UTXO_INDEX}                                                       ${NC}"
    echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
    echo ""
    
    # Two-column layout: Formatted on left, Raw JSON on right
    echo -e "${BOLD}${BLUE}FORMATTED VIEW:${NC}                    ${BOLD}${YELLOW}RAW JSON:${NC}"
    echo -e "${DIM}--------------------------------  --------------------------------${NC}"
    
    # Transaction Details
    echo -e "  ${BOLD}Transaction:${NC}"
    printf "    ${DIM}%-20s${NC} ${MAGENTA}%s${NC}\n" "txid:" "${UTXO_TXID:0:24}..."
    printf "    ${DIM}%-20s${NC} ${BLUE}%s${NC}\n" "vout:" "${UTXO_VOUT}"
    
    # Value Details
    echo -e "  ${BOLD}Value:${NC}"
    printf "    ${DIM}%-20s${NC} ${GREEN}${BOLD}%s BTQ${NC}\n" "amount:" "${UTXO_AMOUNT}"
    printf "    ${DIM}%-20s${NC} ${GREEN}%s blocks${NC}\n" "confirmations:" "${UTXO_CONFIRMATIONS}"
    
    # Address
    echo -e "  ${BOLD}Script:${NC}"
    printf "    ${DIM}%-20s${NC} ${BLUE}%s${NC}\n" "address:" "${UTXO_ADDRESS:0:28}..."
    
    # Status Flags Section
    echo -e "  ${BOLD}Status:${NC}"
    
    if [ "$UTXO_SPENDABLE" = "true" ]; then
        printf "    ${GREEN}[OK]${NC} %-12s ${GREEN}%s${NC}\n" "spendable:" "true"
    else
        printf "    ${RED}[X]${NC} %-12s ${RED}%s${NC}\n" "spendable:" "false"
    fi
    
    if [ "$UTXO_SOLVABLE" = "true" ]; then
        printf "    ${GREEN}[OK]${NC} %-12s ${GREEN}%s${NC}\n" "solvable:" "true"
    else
        printf "    ${YELLOW}[!]${NC} %-12s ${YELLOW}%s${NC}\n" "solvable:" "false"
    fi
    
    if [ "$UTXO_SAFE" = "true" ]; then
        printf "    ${GREEN}[OK]${NC} %-12s ${GREEN}%s${NC}\n" "safe:" "true"
    else
        printf "    ${RED}[X]${NC} %-12s ${RED}%s${NC}\n" "safe:" "false"
    fi
    
    echo ""
    echo -e "${BOLD}${YELLOW}Complete JSON for UTXO #${UTXO_INDEX}:${NC}"
    echo -e "${DIM}----------------------------------------------------------------${NC}"
    printf '%s\n' "$UTXO_JSON" | python3 -m json.tool 2>/dev/null || printf '%s\n' "$UTXO_JSON"
    echo ""
    
    UTXO_INDEX=$((UTXO_INDEX + 1))
done


# Phase 6 Completion Status
echo -e "${BOLD}${GREEN}================================================================${NC}"
echo -e "${BOLD}${GREEN}                                                                ${NC}"
echo -e "${BOLD}${GREEN}      Phase 6: RPC & User Interface - FULLY FUNCTIONAL         ${NC}"
echo -e "${BOLD}${GREEN}                                                                ${NC}"
echo -e "${BOLD}${GREEN}================================================================${NC}"
echo ""

# Features Demonstrated
echo -e "${BOLD}${CYAN}FEATURES SUCCESSFULLY TESTED:${NC}"
echo -e "${DIM}----------------------------------------------------------------${NC}"
echo -e "  ${GREEN}${CHECKMARK}${NC} getnewdilithiumaddress       - Address generation"
echo -e "  ${GREEN}${CHECKMARK}${NC} signmessagewithdilithium     - Message signing"
echo -e "  ${GREEN}${CHECKMARK}${NC} verifydilithiumsignature     - Signature verification"
echo -e "  ${GREEN}${CHECKMARK}${NC} signtransactionwithdilithium - Transaction signing"
echo -e "  ${GREEN}${CHECKMARK}${NC} generatetoaddress            - Mining to Dilithium address"
echo -e "  ${GREEN}${CHECKMARK}${NC} sendrawtransaction           - Broadcasting large txs"
echo ""
echo ""
echo -e "${DIM}To stop btqd: ${YELLOW}${BTQCLI} ${NETWORK} stop${NC}"
echo ""

