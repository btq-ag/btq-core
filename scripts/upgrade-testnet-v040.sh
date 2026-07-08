#!/usr/bin/env bash
# Upgrade a BTQ testnet node to v0.4.0 (new genesis). Run as root on the server.
set -euo pipefail

WIPE_DATA="${WIPE_DATA:-1}"          # 1 = remove blocks+chainstate for fresh v0.4 chain
KEEP_REINDEX="${KEEP_REINDEX:-0}"    # 1 = start with -reindex (slow; avoid if syncing from peers)
BTQ_ROOT="${BTQ_ROOT:-/root/BTQ-Core}"
DATA_DIR="${DATA_DIR:-/root/.btq/test}"

INFRA_PEERS=(
  "45.76.141.238:19333"
  "45.63.97.41:19333"
  "65.20.111.197:19333"
  "208.85.16.175:19333"
)

echo "=== Stop btqd ==="
"$BTQ_ROOT/src/btq-cli" -testnet stop 2>/dev/null || true
sleep 5
pkill -x btqd 2>/dev/null || true
sleep 2

echo "=== Build v0.4.0-testnet ==="
cd "$BTQ_ROOT"
git fetch --tags origin 2>/dev/null || git fetch --tags 2>/dev/null || true
git checkout -f v0.4.0-testnet
make distclean 2>/dev/null || rm -f config.status Makefile
./autogen.sh >/dev/null 2>&1
mkdir -p build-v040
cd build-v040
../configure --disable-tests --disable-bench --with-gui=no >/dev/null
make -j"$(nproc)" >/dev/null
install -m 755 src/btqd src/btq-cli "$BTQ_ROOT/src/"
"$BTQ_ROOT/src/btqd" -version | head -1

if [[ "$WIPE_DATA" == "1" ]]; then
  ts=$(date +%Y%m%d%H%M%S)
  echo "=== Archive datadir ==="
  for d in blocks chainstate indexes; do
    if [[ -d "$DATA_DIR/$d" ]]; then
      mv "$DATA_DIR/$d" "$DATA_DIR/${d}.pre-v040-$ts"
    fi
  done
  mkdir -p "$DATA_DIR/blocks"
fi

echo "=== Update btq.conf peers ==="
CONF="${BTQ_CONF:-/root/.btq/btq.conf}"
if [[ -f "$CONF" ]]; then
  grep -v '^addnode=' "$CONF" | grep -v '^connect=' > "${CONF}.tmp" || true
  mv "${CONF}.tmp" "$CONF"
  for p in "${INFRA_PEERS[@]}"; do
    echo "addnode=$p" >> "$CONF"
  done
fi

EXTRA_FLAGS="-testnet -server -daemon"
grep -q txindex "$CONF" 2>/dev/null && EXTRA_FLAGS="-testnet -txindex -server -daemon"
[[ "$KEEP_REINDEX" == "1" ]] && EXTRA_FLAGS="$EXTRA_FLAGS -reindex"

echo "=== Start btqd: $EXTRA_FLAGS ==="
cd "$BTQ_ROOT/src"
# shellcheck disable=SC2086
./btqd $EXTRA_FLAGS
sleep 8
"$BTQ_ROOT/src/btq-cli" -testnet getblockchaininfo | head -14
