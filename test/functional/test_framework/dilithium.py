#!/usr/bin/env python3
# Copyright (c) 2026 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Minimal CRYSTALS-Dilithium2 signing for the functional test framework.

There is no pure-Python Dilithium implementation available, so this module
compiles the *same* pq-crystals reference sources that BTQ-Core links
(`src/crypto/dilithium/ref/`) into a small shared library and drives it via
ctypes. Because signing is deterministic (BTQ builds with randomized signing
disabled) the signatures produced here are byte-identical to those the node's
consensus code accepts.

Usage:
    from test_framework.dilithium import dilithium_available, DilithiumKey
    if not dilithium_available():
        # skip the test
    k = DilithiumKey()            # random key (or DilithiumKey(seed=32 bytes))
    pub = k.pubkey                # 1312-byte public key
    sig = k.sign(sighash32)       # 2420-byte detached signature
"""

import ctypes
import os
import subprocess
import tempfile
import threading

PUBLICKEYBYTES = 1312
SECRETKEYBYTES = 2560
SIGNATUREBYTES = 2420
SEEDBYTES = 32

_NS = "pqcrystals_dilithium2_ref_"
_SOURCES = [
    "fips202.c", "ntt.c", "packing.c", "poly.c", "polyvec.c",
    "randombytes.c", "reduce.c", "rounding.c", "sign.c", "symmetric-shake.c",
]

_REF_DIR = os.path.normpath(os.path.join(
    os.path.dirname(__file__), "..", "..", "..",
    "src", "crypto", "dilithium", "ref"))

_lib = None
_lib_lock = threading.Lock()
_build_error = None


def _build_shared_lib():
    """Compile the reference sources into a cached shared library, return path."""
    src_paths = [os.path.join(_REF_DIR, s) for s in _SOURCES]
    for p in src_paths:
        if not os.path.exists(p):
            raise FileNotFoundError(f"Dilithium reference source missing: {p}")

    cache_dir = os.path.join(tempfile.gettempdir(), "btq_dilithium_ctypes")
    os.makedirs(cache_dir, exist_ok=True)
    out = os.path.join(cache_dir, "libbtqdilithium2_ref.so")

    newest_src = max(os.path.getmtime(p) for p in src_paths)
    if os.path.exists(out) and os.path.getmtime(out) >= newest_src:
        return out

    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O2", "-fPIC", "-shared", "-DDILITHIUM_MODE=2",
           "-I", _REF_DIR, *src_paths, "-o", out]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return out


def _load():
    global _lib, _build_error
    with _lib_lock:
        if _lib is not None:
            return _lib
        if _build_error is not None:
            raise _build_error
        try:
            path = _build_shared_lib()
            lib = ctypes.CDLL(path)
            lib[_NS + "keypair_from_seed"].restype = ctypes.c_int
            lib[_NS + "keypair_from_seed"].argtypes = [
                ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
            lib[_NS + "signature"].restype = ctypes.c_int
            lib[_NS + "signature"].argtypes = [
                ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_char_p, ctypes.c_size_t,
                ctypes.c_char_p, ctypes.c_size_t,
                ctypes.c_char_p]
            lib[_NS + "verify"].restype = ctypes.c_int
            lib[_NS + "verify"].argtypes = [
                ctypes.c_char_p, ctypes.c_size_t,
                ctypes.c_char_p, ctypes.c_size_t,
                ctypes.c_char_p, ctypes.c_size_t,
                ctypes.c_char_p]
            _lib = lib
            return _lib
        except Exception as e:  # noqa: BLE001
            _build_error = e
            raise


def dilithium_available():
    """Return True if the reference library can be built and loaded."""
    try:
        _load()
        return True
    except Exception:  # noqa: BLE001
        return False


class DilithiumKey:
    """A Dilithium2 keypair, deterministically derived from a 32-byte seed."""

    def __init__(self, seed=None):
        lib = _load()
        if seed is None:
            seed = os.urandom(SEEDBYTES)
        if len(seed) != SEEDBYTES:
            raise ValueError("seed must be 32 bytes")
        pk = ctypes.create_string_buffer(PUBLICKEYBYTES)
        sk = ctypes.create_string_buffer(SECRETKEYBYTES)
        rc = lib[_NS + "keypair_from_seed"](pk, sk, seed)
        if rc != 0:
            raise RuntimeError("Dilithium keypair generation failed")
        self.pubkey = pk.raw[:PUBLICKEYBYTES]
        self._sk = sk.raw[:SECRETKEYBYTES]

    def sign(self, message):
        """Return the 2420-byte detached signature over `message` (no context).

        For a P2MR tapscript spend, `message` is the 32-byte BIP341 sighash
        (TaprootSignatureHash), not the legacy pre-segwit serializer.
        """
        lib = _load()
        sig = ctypes.create_string_buffer(SIGNATUREBYTES)
        siglen = ctypes.c_size_t(0)
        rc = lib[_NS + "signature"](
            sig, ctypes.byref(siglen),
            message, len(message),
            None, 0,
            self._sk)
        if rc != 0:
            raise RuntimeError("Dilithium signing failed")
        return sig.raw[:siglen.value]

    def verify(self, message, signature):
        lib = _load()
        rc = lib[_NS + "verify"](
            signature, len(signature),
            message, len(message),
            None, 0,
            self.pubkey)
        return rc == 0
