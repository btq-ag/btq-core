#!/usr/bin/env python3
#
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
"""Validate the pinned testnet LWMA activation corpus and its two targets.

This is deliberately an offline, standard-library oracle. Python integers are
arbitrary precision, so the target arithmetic does not share the uint256
overflow constraints or implementation structure of src/pow.cpp.
"""

import hashlib
import json
from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = REPO_ROOT / "src/test/data/testnet_lwma_activation.json"
UINT256_MAX = (1 << 256) - 1
KNOWN_ACTIVATION_HASH = "0000000002960cc1e8b64b778958b5398e5165ff2fb26767872a902846f1d436"
KNOWN_RESULTS = {
    45: (4423, 0x1C04675D),
    144: (54294, 0x1C055BF4),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def compact_to_target(bits):
    size = bits >> 24
    word = bits & 0x007FFFFF
    negative = word != 0 and bits & 0x00800000
    overflow = word != 0 and (
        size > 34
        or (word > 0xFF and size > 33)
        or (word > 0xFFFF and size > 32)
    )
    require(not negative, f"negative compact target: {bits:08x}")
    require(not overflow, f"overflowing compact target: {bits:08x}")

    if size <= 3:
        target = word >> (8 * (3 - size))
    else:
        target = word << (8 * (size - 3))
    require(target > 0, f"zero compact target: {bits:08x}")
    return target


def target_to_compact(target):
    require(0 < target <= UINT256_MAX, f"target outside uint256: {target}")
    size = (target.bit_length() + 7) // 8
    if size <= 3:
        compact = target << (8 * (3 - size))
    else:
        compact = target >> (8 * (size - 3))

    if compact & 0x00800000:
        compact >>= 8
        size += 1
    return compact | (size << 24)


def block_proof(target):
    return (UINT256_MAX - target) // (target + 1) + 1


def parse_header(row, index, pow_limit):
    height = row["height"]
    require(isinstance(height, int), f"row {index}: height is not an integer")

    raw_hex = row["header_hex"]
    require(isinstance(raw_hex, str), f"height {height}: header_hex is not a string")
    raw = bytes.fromhex(raw_hex)
    require(len(raw) == 80, f"height {height}: header is {len(raw)} bytes, expected 80")
    require(raw.hex() == raw_hex, f"height {height}: header_hex must be lowercase canonical hex")

    hash_hex = hashlib.sha256(hashlib.sha256(raw).digest()).digest()[::-1].hex()
    require(hash_hex == row["hash"], f"height {height}: advertised hash does not match raw header")

    prev_hash = raw[4:36][::-1].hex()
    timestamp = int.from_bytes(raw[68:72], "little")
    bits = int.from_bytes(raw[72:76], "little")
    target = compact_to_target(bits)
    require(target <= pow_limit, f"height {height}: target exceeds public-testnet pow limit")
    require(target_to_compact(target) == bits, f"height {height}: non-canonical nBits {bits:08x}")
    require(int(hash_hex, 16) <= target, f"height {height}: proof of work does not meet nBits {bits:08x}")

    chainwork_hex = row["chainwork"]
    require(isinstance(chainwork_hex, str) and len(chainwork_hex) == 64,
            f"height {height}: chainwork must be 32-byte hex")
    chainwork = int(chainwork_hex, 16)
    require(f"{chainwork:064x}" == chainwork_hex, f"height {height}: chainwork must be lowercase canonical hex")

    return {
        "height": height,
        "hash": hash_hex,
        "prev_hash": prev_hash,
        "timestamp": timestamp,
        "bits": bits,
        "target": target,
        "chainwork": chainwork,
    }


def lwma_oracle(headers, window, target_spacing, pow_limit):
    last_index = len(headers) - 2
    require(last_index >= window, f"N={window}: insufficient history")

    previous = headers[last_index - window]
    weighted_solvetime = 0
    targets = []
    for weight, block in enumerate(headers[last_index - window + 1:last_index + 1], start=1):
        solvetime = block["timestamp"] - previous["timestamp"]
        solvetime = min(max(solvetime, -6 * target_spacing), 6 * target_spacing)
        weighted_solvetime += solvetime * weight
        targets.append(block["target"])
        previous = block

    # Direct arbitrary-precision form of the LWMA equation. This intentionally
    # does not reproduce the quotient/remainder overflow-avoidance in C++.
    average_target = sum(targets) // window
    denominator = window * (window + 1) * target_spacing // 2
    next_target = average_target * max(weighted_solvetime, 1) // denominator
    next_target = min(next_target, pow_limit)
    return weighted_solvetime, target_to_compact(next_target)


def validate():
    with FIXTURE.open(encoding="utf8") as fixture_file:
        root = json.load(fixture_file, object_pairs_hook=reject_duplicate_keys)

    require(root["schema_version"] == 1, "unsupported fixture schema")
    require(root["network"] == "BTQ public testnet", "fixture network mismatch")
    provenance = root["provenance"]
    require(provenance["source"] == "https://explorer.bitcoinquantum.com/api/v1/block/{height}",
            "fixture source endpoint mismatch")
    require(provenance["per_height_endpoint_template"] ==
            "https://explorer.bitcoinquantum.com/api/v1/block/{height}",
            "fixture per-height endpoint mismatch")
    require(provenance["retrieval_method"] ==
            "GET every integer height 299855..300000; require canonical=true; serialize "
            "uint32le(version) || reverse32(prev_hash) || reverse32(merkle_root) || "
            "uint32le(unix(timestamp)) || uint32le(hex(bits)) || uint32le(nonce); retain "
            "advertised hash and chainwork.",
            "fixture retrieval method mismatch")
    require(provenance["retrieved_at_utc"] == "2026-08-18", "fixture retrieval date mismatch")

    block_range = root["range"]
    require(block_range == {
        "start_height": 299855,
        "end_height": 300000,
        "lwma_activation_height": 300000,
    }, "fixture range mismatch")

    context = root["consensus_context"]
    require(context["target_spacing_seconds"] == 60, "target spacing mismatch")
    require(context["pow_limit_bits"] == "1e0377ae", "pow-limit compact mismatch")
    target_spacing = context["target_spacing_seconds"]
    pow_limit = compact_to_target(int(context["pow_limit_bits"], 16))

    rows = root["headers"]
    require(isinstance(rows, list) and len(rows) == 146, "fixture must contain exactly 146 headers")
    headers = [parse_header(row, index, pow_limit) for index, row in enumerate(rows)]

    for index, block in enumerate(headers):
        expected_height = block_range["start_height"] + index
        require(block["height"] == expected_height,
                f"row {index}: got height {block['height']}, expected {expected_height}")
        if index == 0:
            continue
        previous = headers[index - 1]
        require(block["prev_hash"] == previous["hash"], f"height {block['height']}: broken header linkage")
        expected_chainwork = previous["chainwork"] + block_proof(block["target"])
        require(block["chainwork"] == expected_chainwork,
                f"height {block['height']}: invalid chainwork increment")

    expected = root["expected"]
    activation = headers[-1]
    require(expected["activation_hash"] == KNOWN_ACTIVATION_HASH,
            "fixture activation known-answer hash drift")
    require(activation["hash"] == expected["activation_hash"], "activation hash mismatch")
    require(f"{activation['bits']:08x}" == expected["activation_bits"], "activation nBits mismatch")

    for window, key in ((45, "n45"), (144, "n144")):
        known_weighted_solvetime, known_next_bits = KNOWN_RESULTS[window]
        require(expected[key]["weighted_solvetime"] == known_weighted_solvetime,
                f"N={window}: fixture weighted-solvetime known answer drift")
        require(int(expected[key]["next_bits"], 16) == known_next_bits,
                f"N={window}: fixture nBits known answer drift")
        weighted_solvetime, next_bits = lwma_oracle(headers, window, target_spacing, pow_limit)
        require(weighted_solvetime == known_weighted_solvetime,
                f"N={window}: weighted solvetime mismatch")
        require(next_bits == known_next_bits,
                f"N={window}: next nBits mismatch")

    require(expected["n45"]["next_bits"] == expected["activation_bits"],
            "observed activation header does not match N=45")
    require(expected["n45"]["next_bits"] != expected["n144"]["next_bits"],
            "fixture does not demonstrate a window-version divergence")


def main():
    try:
        validate()
    except (KeyError, OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"testnet LWMA activation fixture validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
