#!/usr/bin/env bash
#
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Fast audit-remediation test loop: targeted rebuild + subset of unit/functional tests.
#
# Usage:
#   ./scripts/quick-test.sh                 # build (if needed) + unit + functional
#   ./scripts/quick-test.sh --unit-only       # unit tests only (builds test_btq)
#   ./scripts/quick-test.sh --functional-only # functional tests only (builds btqd)
#   ./scripts/quick-test.sh --no-build        # skip make, run tests only
#   ./scripts/quick-test.sh --build-only      # build only, no tests
#   ./scripts/quick-test.sh --background      # start build in background, exit immediately
#   ./scripts/quick-test.sh --wait-build      # wait for background build, then run tests
#
# Environment:
#   JOBS=2          parallel make jobs (default 2; safer on WSL than -j$(nproc))
#   BUILD_LOG=...   log file for --background (default: .quick-test-build.log)

set -euo pipefail

export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRCDIR="$ROOT/src"
FUNCTIONAL_DIR="$ROOT/test/functional"
TEST_BTQ="$SRCDIR/test/test_btq"
BTQD="$SRCDIR/btqd"
BTQ_CLI="$SRCDIR/btq-cli"
JOBS="${JOBS:-2}"
BUILD_LOG="${BUILD_LOG:-$ROOT/.quick-test-build.log}"
PIDFILE="${PIDFILE:-$ROOT/.quick-test-build.pid}"

# Audit-related Boost test suites (colon-separated for --run_test).
UNIT_SUITES=(
    pow_lwma_tests
    chainparams_genesis_tests
    dilithium_basic_tests
    dilithium_network_policy_tests
    dilithium_key_tests
    dilithium_wallet_tests
    scriptpubkeyman_tests
    sigopcount_tests
    descriptor_p2sh_segwit_tests
)

# Audit-related functional tests (exact test_runner.py entries).
FUNCTIONAL_TESTS=(
    "feature_lwma_activation.py"
    "feature_dilithium_activation.py --descriptors"
    "feature_dilithium_sigops.py"
    "feature_taproot_bech32m_spend.py --descriptors"
    "wallet_signet_wif_prefix.py --legacy-wallet"
    "wallet_dilithium_encrypted_restart.py --legacy-wallet"
    "wallet_dilithium_encrypted_restart_descriptors.py --descriptors"
    "wallet_p2sh_segwit_spend.py --descriptors"
    "wallet_all_types_simulation.py --descriptors"
)

DO_BUILD=1
DO_UNIT=1
DO_FUNC=1
BACKGROUND=0
WAIT_BUILD=0

print_usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Options:
  --unit-only        Run audit unit tests only (builds test/test_btq)
  --functional-only  Run audit functional tests only (builds btqd + btq-cli)
  --no-build         Skip make; assume binaries are up to date
  --build-only       Build required targets only; do not run tests
  --background       Start build in background and exit (see BUILD_LOG)
  --wait-build       Wait for background build to finish, then run tests
  -h, --help         Show this help

Examples:
  $0 --background && ... edit code ... && $0 --wait-build --no-build
  JOBS=2 $0 --unit-only --no-build
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --unit-only) DO_FUNC=0 ;;
        --functional-only) DO_UNIT=0 ;;
        --no-build) DO_BUILD=0 ;;
        --build-only) DO_UNIT=0; DO_FUNC=0 ;;
        --background) BACKGROUND=1 ;;
        --wait-build) WAIT_BUILD=1; DO_BUILD=0 ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; print_usage; exit 1 ;;
    esac
    shift
done

unit_filter() {
    local IFS=':'
    echo "${UNIT_SUITES[*]}"
}

build_test_btq() {
    echo "==> Building test/test_btq (-j${JOBS})"
    make -C "$SRCDIR" -j"$JOBS" test/test_btq
}

build_btqd() {
    echo "==> Building btqd + btq-cli (-j${JOBS})"
    make -C "$SRCDIR" -j"$JOBS" btqd btq-cli
}

run_build() {
    if [[ "$DO_UNIT" -eq 1 || "$DO_FUNC" -eq 0 ]]; then
        build_test_btq
    fi
    if [[ "$DO_FUNC" -eq 1 ]]; then
        build_btqd
    fi
}

start_background_build() {
    echo "==> Starting background build (log: $BUILD_LOG)"
    (
        if [[ "$DO_UNIT" -eq 1 || "$DO_FUNC" -eq 0 ]]; then build_test_btq; fi
        if [[ "$DO_FUNC" -eq 1 ]]; then build_btqd; fi
    ) >"$BUILD_LOG" 2>&1 &
    echo $! >"$PIDFILE"
    echo "Build pid $(cat "$PIDFILE"). Tail with: tail -f $BUILD_LOG"
    echo "When done, run: $0 --wait-build --no-build"
}

wait_for_background_build() {
    if [[ ! -f "$PIDFILE" ]]; then
        echo "No background build pid file ($PIDFILE). Nothing to wait for." >&2
        return 1
    fi
    local pid
    pid="$(cat "$PIDFILE")"
    if kill -0 "$pid" 2>/dev/null; then
        echo "==> Waiting for background build (pid $pid)..."
        wait "$pid"
    fi
    rm -f "$PIDFILE"
    if [[ -f "$BUILD_LOG" ]]; then
        echo "==> Build log tail:"
        tail -5 "$BUILD_LOG"
    fi
}

run_unit_tests() {
    [[ -x "$TEST_BTQ" ]] || { echo "Missing $TEST_BTQ (run without --no-build)" >&2; exit 1; }
    local suite
    for suite in "${UNIT_SUITES[@]}"; do
        echo "==> Unit test suite: $suite"
        "$TEST_BTQ" --run_test="$suite"
    done
}

run_functional_tests() {
    [[ -x "$BTQD" ]] || { echo "Missing $BTQD (run without --no-build)" >&2; exit 1; }
    [[ -x "$BTQ_CLI" ]] || { echo "Missing $BTQ_CLI (run without --no-build)" >&2; exit 1; }
    cd "$FUNCTIONAL_DIR"
    local entry args
    for entry in "${FUNCTIONAL_TESTS[@]}"; do
        # shellcheck disable=SC2086
        set -- $entry
        local script="$1"
        shift
        echo "==> Functional: $script $*"
        python3 "$script" "$@" || exit 1
    done
}

main() {
    if [[ "$BACKGROUND" -eq 1 ]]; then
        start_background_build
        exit 0
    fi

    if [[ "$WAIT_BUILD" -eq 1 ]]; then
        wait_for_background_build
    fi

    if [[ "$DO_BUILD" -eq 1 ]]; then
        run_build
    fi

    if [[ "$DO_UNIT" -eq 0 && "$DO_FUNC" -eq 0 ]]; then
        echo "==> Build complete."
        exit 0
    fi

    if [[ "$DO_UNIT" -eq 1 ]]; then
        run_unit_tests
    fi

    if [[ "$DO_FUNC" -eq 1 ]]; then
        run_functional_tests
    fi

    echo "==> quick-test: all selected tests passed."
}

main
