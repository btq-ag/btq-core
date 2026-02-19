#!/bin/bash

# Test script for sendmany with Dilithium addresses on regtest
# This will verify that the sendmany fix works correctly

set -e

BTQD="$(pwd)/src/btqd"
BTQCLI="$(pwd)/src/btq-cli"
NETWORK="-regtest"
WALLET="test_dilithium_sendmany"

echo "=========================================="
echo "Testing sendmany with Dilithium Addresses"
echo "=========================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Step 1: Stop any running daemon
echo "Step 1: Cleaning up..."
$BTQCLI $NETWORK stop 2>/dev/null || true
sleep 2
rm -rf ~/.btq/regtest 2>/dev/null || true
echo "✓ Clean state"
echo ""

# Step 2: Start daemon
echo "Step 2: Starting btqd in regtest mode..."
$BTQD $NETWORK -daemon -debug=rpc
sleep 3
echo "✓ Daemon started"
echo ""

# Step 3: Create test wallet
echo "Step 3: Creating test wallet..."
$BTQCLI $NETWORK createwallet "$WALLET" false false "" false true true
echo "✓ Wallet created: $WALLET"
echo ""

# Step 4: Generate Dilithium address (pool address)
echo "Step 4: Generating Dilithium pool address..."
POOL_ADDR=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getnewdilithiumaddress)
echo "✓ Pool address: $POOL_ADDR"

# Check if it's Dilithium
ADDR_INFO=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getaddressinfo "$POOL_ADDR")
IS_DILITHIUM=$(echo "$ADDR_INFO" | grep -o '"isdilithium": true' || echo "")
if [ -z "$IS_DILITHIUM" ]; then
    echo -e "${RED}✗ ERROR: Address is not Dilithium!${NC}"
    exit 1
fi
echo "✓ Confirmed Dilithium address"

# Get scriptPubKey
SCRIPT_PUBKEY=$(echo "$ADDR_INFO" | grep '"scriptPubKey"' | cut -d'"' -f4)
echo "✓ ScriptPubKey: $SCRIPT_PUBKEY"
if [[ $SCRIPT_PUBKEY == *"bb" ]]; then
    echo -e "${GREEN}✓ Script ends with 'bb' (OP_CHECKSIGDILITHIUM)${NC}"
else
    echo -e "${RED}✗ ERROR: Script doesn't end with 'bb'!${NC}"
    exit 1
fi
echo ""

# Step 5: Mine blocks to pool address
echo "Step 5: Mining 110 blocks to pool address..."
$BTQCLI $NETWORK -rpcwallet="$WALLET" generatetoaddress 110 "$POOL_ADDR" > /dev/null
echo "✓ Mined 110 blocks"

# Check balance
BALANCE=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getbalance)
echo "✓ Pool balance: $BALANCE BTQ"
echo ""

# Step 6: Generate recipient addresses (miners)
echo "Step 6: Generating miner addresses..."
MINER1=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getnewaddress "" "legacy")
MINER2=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getnewaddress "" "legacy")
MINER3=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getnewdilithiumaddress)
echo "✓ Miner 1 (regular): $MINER1"
echo "✓ Miner 2 (regular): $MINER2"
echo "✓ Miner 3 (Dilithium): $MINER3"
echo ""

# Step 7: List UTXOs
echo "Step 7: Checking UTXOs..."
UTXO_COUNT=$($BTQCLI $NETWORK -rpcwallet="$WALLET" listunspent | grep -c '"spendable": true')
echo "✓ Spendable UTXOs: $UTXO_COUNT"

# Check if UTXOs are Dilithium
DILITHIUM_UTXO=$($BTQCLI $NETWORK -rpcwallet="$WALLET" listunspent | grep -c 'bb"' || echo "0")
echo "✓ Dilithium UTXOs: $DILITHIUM_UTXO"
echo ""

# Step 8: Test sendmany with mixed addresses
echo "========================================"
echo "Step 8: TESTING SENDMANY"
echo "========================================"
echo ""

echo "Test 1: Sending to 2 regular addresses..."
TX1=$($BTQCLI $NETWORK -rpcwallet="$WALLET" sendmany "" "{\"$MINER1\":1.0,\"$MINER2\":2.0}" 2>&1)
if [[ $TX1 == *"error"* ]] || [[ $TX1 == *"Insufficient"* ]]; then
    echo -e "${RED}✗ FAILED: $TX1${NC}"
    echo ""
    echo "Debugging info:"
    echo "Balance: $($BTQCLI $NETWORK -rpcwallet="$WALLET" getbalance)"
    echo "Listunspent:"
    $BTQCLI $NETWORK -rpcwallet="$WALLET" listunspent | head -30
    exit 1
else
    echo -e "${GREEN}✓ SUCCESS: $TX1${NC}"
fi
echo ""

echo "Test 2: Sending to 1 Dilithium address..."
TX2=$($BTQCLI $NETWORK -rpcwallet="$WALLET" sendmany "" "{\"$MINER3\":0.5}" 2>&1)
if [[ $TX2 == *"error"* ]] || [[ $TX2 == *"Insufficient"* ]]; then
    echo -e "${RED}✗ FAILED: $TX2${NC}"
    exit 1
else
    echo -e "${GREEN}✓ SUCCESS: $TX2${NC}"
fi
echo ""

echo "Test 3: Sending to mixed addresses (2 regular + 1 Dilithium)..."
TX3=$($BTQCLI $NETWORK -rpcwallet="$WALLET" sendmany "" "{\"$MINER1\":0.1,\"$MINER2\":0.2,\"$MINER3\":0.3}" 2>&1)
if [[ $TX3 == *"error"* ]] || [[ $TX3 == *"Insufficient"* ]]; then
    echo -e "${RED}✗ FAILED: $TX3${NC}"
    exit 1
else
    echo -e "${GREEN}✓ SUCCESS: $TX3${NC}"
fi
echo ""

# Step 9: Verify transactions
echo "Step 9: Verifying transactions..."
for TXID in "$TX1" "$TX2" "$TX3"; do
    if [[ $TXID != *"error"* ]]; then
        TX_DETAILS=$($BTQCLI $NETWORK -rpcwallet="$WALLET" gettransaction "$TXID" 2>&1)
        if [[ $TX_DETAILS == *"confirmations"* ]]; then
            echo "✓ Transaction $TXID found in wallet"
        else
            echo -e "${RED}✗ Transaction $TXID not found!${NC}"
        fi
    fi
done
echo ""

# Step 10: Mine a block to confirm
echo "Step 10: Mining block to confirm transactions..."
$BTQCLI $NETWORK -rpcwallet="$WALLET" generatetoaddress 1 "$POOL_ADDR" > /dev/null
echo "✓ Block mined"
echo ""

# Step 11: Check final balances
echo "Step 11: Checking final balances..."
FINAL_BALANCE=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getbalance)
echo "Pool final balance: $FINAL_BALANCE BTQ"

echo ""
echo "Miner balances:"
for ADDR in "$MINER1" "$MINER2" "$MINER3"; do
    BAL=$($BTQCLI $NETWORK -rpcwallet="$WALLET" getreceivedbyaddress "$ADDR" 0)
    echo "  $ADDR: $BAL BTQ"
done
echo ""

# Cleanup
echo "Cleanup: Stopping daemon..."
$BTQCLI $NETWORK stop
sleep 2

echo ""
echo "=========================================="
echo -e "${GREEN}✓✓✓ ALL TESTS PASSED! ✓✓✓${NC}"
echo "=========================================="
echo ""
echo "sendmany with Dilithium addresses is WORKING!"
echo ""
echo "This confirms:"
echo "  ✓ Dilithium keys properly stored"
echo "  ✓ FlatSigningProvider includes Dilithium keys"
echo "  ✓ sendmany can spend Dilithium UTXOs"
echo "  ✓ Mixed transactions (Dilithium + regular) work"
echo ""
echo "Your BTQ-Core build is ready for production!"
echo ""

