# P2MR End-to-End With `btq-cli` (No Python)

This guide shows how to create, fund, spend, sign, and confirm a P2MR transaction on regtest using `btq-cli` plus Bash and `jq`.

## Prerequisites

- Built binaries in `./src`:
  - `btqd`
  - `btq-cli`
- `jq` installed:
  - `sudo apt-get install -y jq`

## 1) Start `btqd` (regtest)

```bash
cd ~/BTQ-Core/src
./btqd -regtest -datadir=/tmp/p2mr-live-e2e-gdb -server=1 \
  -rpcport=28543 -port=28544 -rpcuser=btq -rpcpassword=btqpass -daemonwait
```

## 2) Define reusable CLI variables

```bash
cd ~/BTQ-Core/src
CLI="./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass"
WCLI="$CLI -rpcwallet=p2mr"
TREE='[{"depth":0,"leaf_version":192,"script":"51"}]'
```

## 3) Create/load wallet and mine mature funds

```bash
# Robust wallet init:
# 1) If already loaded, do nothing
# 2) Else try loadwallet
# 3) Else createwallet
if $CLI listwallets | jq -e '.[] | select(.=="p2mr")' >/dev/null; then
  echo "wallet p2mr already loaded"
elif $CLI loadwallet p2mr >/dev/null 2>&1; then
  echo "wallet p2mr loaded"
else
  $CLI createwallet p2mr >/dev/null
  echo "wallet p2mr created"
fi

MINER_ADDR=$($WCLI getnewaddress)
$WCLI generatetoaddress 110 "$MINER_ADDR" >/dev/null
$WCLI settxfee 0.00010000
$WCLI getbalance
```

## 4) Create a P2MR address

```bash
CREATED=$($WCLI getnewp2mraddress "$TREE")
echo "$CREATED" | jq .
P2MR_ADDR=$(echo "$CREATED" | jq -r '.address')
P2MR_ID=$(echo "$CREATED" | jq -r '.p2mr_id')
echo "P2MR_ADDR=$P2MR_ADDR"
echo "P2MR_ID=$P2MR_ID"
```

Optional metadata check:

```bash
$WCLI listp2mr | jq .
$WCLI getp2mrinfo "$P2MR_ID" | jq .
```

## 5) Fund P2MR output

```bash
FUNDED=$($WCLI sendtop2mr "$TREE" 1.0 "demo-p2mr")
echo "$FUNDED" | jq .
FUND_ID=$(echo "$FUNDED" | jq -r '.p2mr_id')
FUND_TXID=$(echo "$FUNDED" | jq -r '.txid')
$WCLI generatetoaddress 1 "$MINER_ADDR" >/dev/null
echo "FUND_ID=$FUND_ID"
echo "FUND_TXID=$FUND_TXID"
```

## 6) Create unsigned spend from P2MR

```bash
DEST=$($WCLI getnewaddress)
UNSIGNED=$($WCLI createp2mrspend "$FUND_ID" "$DEST" 0.5)
echo "$UNSIGNED" | jq .
UNSIGNED_HEX=$(echo "$UNSIGNED" | jq -r '.hex')
```

## 7) Sign P2MR transaction

```bash
SIGNED=$($WCLI signp2mrtransaction "$UNSIGNED_HEX" "$FUND_ID")
echo "$SIGNED" | jq .
SIGNED_HEX=$(echo "$SIGNED" | jq -r '.hex')
COMPLETE=$(echo "$SIGNED" | jq -r '.complete')
test "$COMPLETE" = "true"
```

## 8) Dry-run mempool acceptance

```bash
TEST=$($WCLI testp2mrtransaction "$SIGNED_HEX")
echo "$TEST" | jq .
echo "$TEST" | jq -e '.[0].allowed == true' >/dev/null
```

## 9) Broadcast and confirm

```bash
SPEND_TXID=$($CLI sendrawtransaction "$SIGNED_HEX")
$WCLI generatetoaddress 1 "$MINER_ADDR" >/dev/null
$WCLI gettransaction "$SPEND_TXID" true | jq '{txid, confirmations, blockhash}'
```

If `confirmations` is `>= 1`, your P2MR spend completed successfully.

## Manual CLI-only pattern (no helper vars)

If you prefer raw `btq-cli` invocation style, use this sequence directly.

### Interactive value checklist

During manual execution, copy these values from command outputs as you go:

- `MINER_ADDRESS`: from `getnewaddress` output before mining
- `FUND_ID`: from `sendtop2mr` JSON field `p2mr_id`
- `DEST_ADDRESS`: from a new `getnewaddress` output for recipient
- `UNSIGNED_HEX`: from `createp2mrspend` JSON field `hex`
- `SIGNED_HEX`: from `signp2mrtransaction` JSON field `hex`
- `SPEND_TXID`: from `sendrawtransaction` output

Quick sanity checks before broadcast:

- `signp2mrtransaction` returns `"complete": true`
- `testp2mrtransaction` returns `allowed: true`
- After mining 1 block, `gettransaction "<SPEND_TXID>" true` shows `confirmations >= 1`

1) Wallet load/create:

```bash
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass listwallets
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass loadwallet p2mr
```

If load fails and wallet does not exist:

```bash
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass createwallet p2mr
```

2) Get miner address and mine:

```bash
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr getnewaddress
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr generatetoaddress 110 "<MINER_ADDRESS>"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr settxfee 0.00010000
```

3) Create + fund P2MR:

```bash
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr getnewp2mraddress '[{"depth":0,"leaf_version":192,"script":"51"}]'
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr sendtop2mr '[{"depth":0,"leaf_version":192,"script":"51"}]' 1.0 "demo-p2mr"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr generatetoaddress 1 "<MINER_ADDRESS>"
```

4) Spend/sign/confirm:

```bash
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr createp2mrspend "<FUND_ID>" "<DEST_ADDRESS>" 0.5
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr signp2mrtransaction "<UNSIGNED_HEX>" "<FUND_ID>"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr testp2mrtransaction "<SIGNED_HEX>"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass sendrawtransaction "<SIGNED_HEX>"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr generatetoaddress 1 "<MINER_ADDRESS>"
./btq-cli -regtest -datadir=/tmp/p2mr-live-e2e-gdb -rpcport=28543 -rpcuser=btq -rpcpassword=btqpass -rpcwallet=p2mr gettransaction "<SPEND_TXID>" true
```

## 10) (Optional) Run the automated script

From repo root:

```bash
cd ~/BTQ-Core
./run_p2mr_rpc_e2e.sh
```

For a clean isolated run:

```bash
MODE=fresh ./run_p2mr_rpc_e2e.sh
```
