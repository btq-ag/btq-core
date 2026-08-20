# Historical validation paths

The current protocol design reaches Dilithium through explicit opcodes in P2MR leaves. Older
testnet data can exercise rules that predate that design. This note records those rules next to
the code that still validates them. It is maintenance documentation, not a statement that the
historical testnet is the model for a new chain.

New networks should use the P2MR-only rules from genesis. New wallet and protocol designs should
not copy the paths below.

## Pre-P2MR Dilithium

The older script templates are listed in
[`pre-p2mr-script-templates.md`](pre-p2mr-script-templates.md). In that regime:

- Dilithium opcodes were accepted in legacy, P2SH, and witness-v0 scripts.
- A witness-v0 keyhash output did not identify a signature scheme. The interpreter selected
  Dilithium when the spender supplied a 1,312-byte public key; otherwise it reconstructed the
  ordinary ECDSA program.
- The P2MR-only deployment disables that size-based dispatch and confines the Dilithium opcodes
  to `SigVersion::P2MR_TAPSCRIPT`.

The dispatch and sigop paths are in `src/script/interpreter.cpp`. The activation parameters and
mandatory flags are in `src/kernel/chainparams.cpp` and `src/validation.cpp`.

## Witness-v0 sighash

A pre-P2MR witness-v0 Dilithium spend uses the ordinary witness-v0 signature-hash construction:
the ten-field BIP-143 serialization, the amount of the spent output, and
`SHA256d(serialized_preimage)`. The `scriptCode` is the Dilithium witness script. The signer
appends the one-byte sighash type to the 2,420-byte raw signature.

This differs from a P2MR Dilithium spend, which uses the BIP-341 script-path sighash and a P2MR
tapleaf commitment. The two routes are selected by script version, not by the signature itself.

The shared witness-v0 construction is implemented by `SignatureHash` in
`src/script/interpreter.cpp`; signing dispatch is in `src/script/sign.cpp`.

## Wallet recognition

Wallets serving a chain with pre-P2MR outputs may need to recognize raw Dilithium public-key,
Dilithium script-hash, witness-v0 keyhash, and witness-v0 script-hash destinations. Recognition
does not make those types suitable for new receives. Current wallet generation uses P2MR for
Dilithium destinations.

The recognition paths are in `src/wallet/scriptpubkeyman.cpp`, `src/wallet/p2mr.cpp`, and
`src/key_io.cpp`.

## LWMA activation boundary

The testnet parameter set selects Bitcoin's legacy retarget below height 300,000 and LWMA from
height 300,000. `GetNextWorkRequired` performs that selection from `nLWMAHeight`.

Released clients have not all used the same LWMA averaging window: v0.4.2 used 45 blocks and
v0.4.4 uses 144. The branch followed by a given historical node therefore depends on its exact
binary and rule set. Issue
[#174](https://github.com/btq-ag/btq-core/issues/174) tracks reconstruction of that version split
and the supporting header corpus. Until that work is complete, this note makes no claim that one
observed branch is the canonical testnet history.

The activation height is in `src/kernel/chainparams.cpp`; the current averaging window and
difficulty calculation are in `src/pow.cpp` and `src/consensus/params.h`.

## Scope

These paths exist because old data must be interpreted under the rules that produced it. They do
not belong in the protocol whitepaper, whose purpose is to explain the current authorization
architecture rather than specify synchronization with a historical testnet.
