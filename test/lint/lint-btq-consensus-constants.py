#!/usr/bin/env python3
#
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
"""Check that BTQ consensus constants mirrored in the functional test framework
stay synchronized with their C++ definitions."""

from ast import (
    Add,
    Assign,
    BinOp,
    Constant,
    Div,
    FloorDiv,
    FunctionDef,
    Module,
    Mult,
    Name,
    Sub,
    UnaryOp,
    USub,
    literal_eval,
    parse,
)
from pathlib import Path
import hashlib
import re
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]


def eval_int_expr(node, names):
    if isinstance(node, Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, Name):
        return names[node.id]
    if isinstance(node, UnaryOp) and isinstance(node.op, USub):
        return -eval_int_expr(node.operand, names)
    if isinstance(node, BinOp):
        lhs = eval_int_expr(node.left, names)
        rhs = eval_int_expr(node.right, names)
        if isinstance(node.op, Add):
            return lhs + rhs
        if isinstance(node.op, Sub):
            return lhs - rhs
        if isinstance(node.op, Mult):
            return lhs * rhs
        if isinstance(node.op, (Div, FloorDiv)):
            if lhs % rhs != 0:
                raise ValueError(f"non-integral division in expression: {lhs} / {rhs}")
            return lhs // rhs
    raise ValueError(f"unsupported integer expression: {node!r}")


def cpp_constant(path, name):
    text = (REPO_ROOT / path).read_text(encoding="utf8")
    match = re.search(
        rf"\b{name}\b\s*=\s*(?P<expr>[^;]+);",
        text,
    )
    if not match:
        raise ValueError(f"could not find {name} in {path}")
    expr = match.group("expr").split("//", 1)[0].strip()
    tree = parse(expr, mode="eval")
    return eval_int_expr(tree.body, {})


def cpp_regex_int(path, pattern):
    text = (REPO_ROOT / path).read_text(encoding="utf8")
    match = re.search(pattern, text, re.DOTALL)
    if not match:
        raise ValueError(f"could not match {pattern!r} in {path}")
    return int(match.group(1), 0)


def cpp_message_start(class_name):
    text = (REPO_ROOT / "src/kernel/chainparams.cpp").read_text(encoding="utf8")
    match = re.search(
        rf"class {class_name}[\s\S]*?"
        r"pchMessageStart\[0\]\s*=\s*(0x[0-9a-fA-F]+);[\s\S]*?"
        r"pchMessageStart\[1\]\s*=\s*(0x[0-9a-fA-F]+);[\s\S]*?"
        r"pchMessageStart\[2\]\s*=\s*(0x[0-9a-fA-F]+);[\s\S]*?"
        r"pchMessageStart\[3\]\s*=\s*(0x[0-9a-fA-F]+);",
        text,
    )
    if not match:
        raise ValueError(f"could not find {class_name} pchMessageStart")
    return bytes(int(group, 16) for group in match.groups())


def cpp_default_signet_message_start():
    text = (REPO_ROOT / "src/kernel/chainparams.cpp").read_text(encoding="utf8")
    match = re.search(r'bin = ParseHex\("([0-9a-fA-F]+)"\);', text)
    if not match:
        raise ValueError("could not find default signet challenge")
    challenge = bytes.fromhex(match.group(1))
    payload = bytes([len(challenge)]) + challenge
    return hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]


def python_constants(path):
    tree = parse((REPO_ROOT / path).read_text(encoding="utf8"))
    assert isinstance(tree, Module)
    names = {}
    for statement in tree.body:
        if not isinstance(statement, Assign):
            continue
        if len(statement.targets) != 1 or not isinstance(statement.targets[0], Name):
            continue
        try:
            names[statement.targets[0].id] = eval_int_expr(statement.value, names)
        except (KeyError, ValueError):
            continue
    return names


def python_function_kw_default(path, function_name, kwarg_name):
    tree = parse((REPO_ROOT / path).read_text(encoding="utf8"))
    for statement in tree.body:
        if not isinstance(statement, FunctionDef) or statement.name != function_name:
            continue
        for arg, default in zip(statement.args.kwonlyargs, statement.args.kw_defaults):
            if arg.arg != kwarg_name:
                continue
            return eval_int_expr(default, {})
    raise ValueError(f"could not find default for {function_name}({kwarg_name}) in {path}")


def python_regex_int(path, pattern):
    text = (REPO_ROOT / path).read_text(encoding="utf8")
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"could not match {pattern!r} in {path}")
    return int(match.group(1), 0)


def python_magic_bytes():
    text = (REPO_ROOT / "test/functional/test_framework/p2p.py").read_text(encoding="utf8")
    match = re.search(r"MAGIC_BYTES\s*=\s*\{(?P<body>.*?)\n\}", text, re.DOTALL)
    if not match:
        raise ValueError("could not find MAGIC_BYTES in p2p.py")
    values = {}
    for item in re.finditer(r'"(?P<network>[^"]+)":\s*(?P<magic>b"[^"]+")', match.group("body")):
        values[item.group("network")] = literal_eval(item.group("magic"))
    return values


MISSING = object()


def check_equal(errors, label, actual, expected):
    if actual is MISSING:
        errors.append(f"{label}: missing, expected {expected}")
        return
    if actual != expected:
        errors.append(f"{label}: got {actual}, expected {expected}")


def main():
    errors = []

    consensus = {
        "MAX_BLOCK_WEIGHT": cpp_constant("src/consensus/consensus.h", "MAX_BLOCK_WEIGHT"),
        "MAX_BLOCK_SIGOPS_COST": cpp_constant("src/consensus/consensus.h", "MAX_BLOCK_SIGOPS_COST"),
        "WITNESS_SCALE_FACTOR": cpp_constant("src/consensus/consensus.h", "WITNESS_SCALE_FACTOR"),
        "COINBASE_MATURITY": cpp_constant("src/consensus/consensus.h", "COINBASE_MATURITY"),
        "MAX_SCRIPT_ELEMENT_SIZE": cpp_constant("src/script/script.h", "MAX_SCRIPT_ELEMENT_SIZE"),
        "DILITHIUM_SIGOP_COST": cpp_constant("src/script/script.h", "DILITHIUM_SIGOP_COST"),
        "MAX_FUTURE_BLOCK_TIME": cpp_constant("src/chain.h", "MAX_FUTURE_BLOCK_TIME"),
        "MAX_PROTOCOL_MESSAGE_LENGTH": cpp_constant("src/net.h", "MAX_PROTOCOL_MESSAGE_LENGTH"),
        "INITIAL_SUBSIDY_BTQ": cpp_regex_int("src/validation.cpp", r"CAmount nSubsidy = (\d+) \* COIN;"),
        "REGTEST_HALVING_INTERVAL": cpp_regex_int("src/kernel/chainparams.cpp", r"class CRegTestParams[\s\S]*?consensus\.nSubsidyHalvingInterval = (\d+);"),
        "REGTEST_GENESIS_TIME": cpp_regex_int("src/kernel/chainparams.cpp", r"class CRegTestParams[\s\S]*?genesis = CreateGenesisBlock\([^;]*?,\s*(\d+),\s*3,\s*0x"),
    }

    messages = python_constants("test/functional/test_framework/messages.py")
    blocktools = python_constants("test/functional/test_framework/blocktools.py")
    script = python_constants("test/functional/test_framework/script.py")
    magic_bytes = python_magic_bytes()

    check_equal(errors, "messages.MAX_BLOCK_WEIGHT", messages.get("MAX_BLOCK_WEIGHT", MISSING), consensus["MAX_BLOCK_WEIGHT"])
    check_equal(
        errors,
        "messages.MAX_PROTOCOL_MESSAGE_LENGTH",
        messages.get("MAX_PROTOCOL_MESSAGE_LENGTH", MISSING),
        consensus["MAX_PROTOCOL_MESSAGE_LENGTH"],
    )
    check_equal(
        errors,
        "blocktools.WITNESS_SCALE_FACTOR",
        blocktools.get("WITNESS_SCALE_FACTOR", MISSING),
        consensus["WITNESS_SCALE_FACTOR"],
    )
    check_equal(
        errors,
        "blocktools.MAX_BLOCK_SIGOPS_WEIGHT",
        blocktools.get("MAX_BLOCK_SIGOPS_WEIGHT", MISSING),
        consensus["MAX_BLOCK_SIGOPS_COST"],
    )
    check_equal(
        errors,
        "blocktools.MAX_BLOCK_SIGOPS",
        blocktools.get("MAX_BLOCK_SIGOPS", MISSING),
        consensus["MAX_BLOCK_SIGOPS_COST"] // consensus["WITNESS_SCALE_FACTOR"],
    )
    check_equal(
        errors,
        "blocktools.MAX_FUTURE_BLOCK_TIME",
        blocktools.get("MAX_FUTURE_BLOCK_TIME", MISSING),
        consensus["MAX_FUTURE_BLOCK_TIME"],
    )
    check_equal(
        errors,
        "blocktools.COINBASE_MATURITY",
        blocktools.get("COINBASE_MATURITY", MISSING),
        consensus["COINBASE_MATURITY"],
    )
    check_equal(
        errors,
        "blocktools.TIME_GENESIS_BLOCK",
        blocktools.get("TIME_GENESIS_BLOCK", MISSING),
        consensus["REGTEST_GENESIS_TIME"],
    )
    check_equal(
        errors,
        "blocktools.create_coinbase nValue default",
        python_function_kw_default("test/functional/test_framework/blocktools.py", "create_coinbase", "nValue"),
        consensus["INITIAL_SUBSIDY_BTQ"],
    )
    check_equal(
        errors,
        "blocktools.create_coinbase halving interval",
        python_regex_int("test/functional/test_framework/blocktools.py", r"halvings = int\(height / (\d+)\)"),
        consensus["REGTEST_HALVING_INTERVAL"],
    )
    check_equal(
        errors,
        "script.MAX_SCRIPT_ELEMENT_SIZE",
        script.get("MAX_SCRIPT_ELEMENT_SIZE", MISSING),
        consensus["MAX_SCRIPT_ELEMENT_SIZE"],
    )
    check_equal(
        errors,
        "script.DILITHIUM_SIGOP_COST",
        script.get("DILITHIUM_SIGOP_COST", MISSING),
        consensus["DILITHIUM_SIGOP_COST"],
    )
    check_equal(errors, "p2p.MAGIC_BYTES mainnet", magic_bytes.get("mainnet", MISSING), cpp_message_start("CMainParams"))
    check_equal(errors, "p2p.MAGIC_BYTES testnet3", magic_bytes.get("testnet3", MISSING), cpp_message_start("CTestNetParams"))
    check_equal(errors, "p2p.MAGIC_BYTES regtest", magic_bytes.get("regtest", MISSING), cpp_message_start("CRegTestParams"))
    check_equal(errors, "p2p.MAGIC_BYTES signet", magic_bytes.get("signet", MISSING), cpp_default_signet_message_start())

    if errors:
        print("BTQ consensus constant drift detected:")
        for error in errors:
            print(f"- {error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
