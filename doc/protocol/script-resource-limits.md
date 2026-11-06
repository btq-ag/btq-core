# Script and block resource limits

The limits below are the ones in this tree. Comments next to a constant are the
only place a Bitcoin value is treated as confirmed. Where a comment does not
state the old value, the Bitcoin figure is issue #94's comparison, and the BTQ
figure is from the source named in the table. Comments say "Bitcoin" or
"upstream". They do not name a Bitcoin Core release.

This page records the envelope. It does not change a constant.

Witness scale factor and block weight are covered separately. This branch has
no page for issue #216. The protocol map has the short row:
[`protocol-map.md`](protocol-map.md).

## Current values

| Constant | This tree | Bitcoin comparison |
|---|---|---|
| `MAX_SCRIPT_ELEMENT_SIZE` | 15000, `src/script/script.h` | 520. `src/script/descriptor.cpp` and `src/test/descriptor_tests.cpp` call that Bitcoin's and upstream's element limit. |
| `MAX_SCRIPT_SIZE` | 100000, `src/script/script.h` | The comment says the limit was increased to 100000 and does not state the previous value. Issue #94 lists Bitcoin's 10000. |
| `MAX_OPS_PER_SCRIPT` | 201, `src/script/script.h` | Issue #94 lists Bitcoin's 201, unchanged. The definition comment does not state Bitcoin's value. |
| `MAX_STACK_SIZE` | 1000, `src/script/script.h` | Issue #94 lists Bitcoin's 1000, unchanged. No comment states why it was left there. |
| `MAX_BLOCK_SERIALIZED_SIZE` | 8000000, `src/consensus/consensus.h` | Issue #94 lists Bitcoin's 4000000. The comment says 8 MB and does not state the previous value. |
| `MAX_BLOCK_WEIGHT` | 8000000, `src/consensus/consensus.h` | Issue #94 lists Bitcoin's 4000000. The comment says 8 MW and does not state the previous value. |
| `WITNESS_SCALE_FACTOR` | 16, `src/consensus/consensus.h` | Issue #94 lists Bitcoin's 4. `src/consensus/validation.h` says the witness discount was restored at 16 and does not state the previous factor. |
| `MAX_BLOCK_SIGOPS_COST` | 80000, `src/consensus/consensus.h` | Issue #94 lists Bitcoin's 80000, unchanged. |
| `MAX_BLOCK_SIGOPS_COST_DILITHIUM_LIMITS` | 160000, `src/consensus/consensus.h` | Not in issue #94's table. |
| `MAX_OPCODE` | `OP_NOP10` (0xb9), `src/script/script.h` | Issue #94 lists a move to `OP_DILITHIUM_PUBKEY` (0xbf). See below. |
| `MAX_STANDARD_P2WSH_STACK_ITEMS` | 100, `src/policy/policy.h` | Issue #94 lists Bitcoin's 100, unchanged. The definition comment does not state Bitcoin's value. |
| `MAX_STANDARD_P2WSH_STACK_ITEM_SIZE` | `MAX_SCRIPT_ELEMENT_SIZE` (15000), `src/policy/policy.h` | Issue #94 lists Bitcoin's 80. The definition does not state 80. |
| `MAX_STANDARD_TAPSCRIPT_STACK_ITEM_SIZE` | `MAX_SCRIPT_ELEMENT_SIZE` (15000), `src/policy/policy.h` | Issue #94 lists Bitcoin's 80. The definition does not state 80. |
| `MAX_STANDARD_P2WSH_SCRIPT_SIZE` | 65536, `src/policy/policy.h` | 3600. `src/test/miniscript_tests.cpp` calls that Bitcoin's ceiling for a standard witnessScript, and 65536 BTQ's. |
| `MAX_STANDARD_SCRIPTSIG_SIZE` | `MAX_SCRIPT_ELEMENT_SIZE` (15000), `src/policy/policy.h` | Issue #94 lists Bitcoin's 1650. The code comment does not state 1650. |
| `MAX_STANDARD_TX_WEIGHT` | 400000, `src/policy/policy.h` | Issue #94 lists Bitcoin's 400000, unchanged. The comment discusses Dilithium input weight and does not state Bitcoin's number. |
| `MAX_P2SH_SIGOPS` | 15, `src/policy/policy.h` | Issue #94 says this is still 15. |

`MAX_SCRIPT_ELEMENT_SIZE`'s comment says 15000 is so post-quantum signatures and
keys fit. It names Dilithium2 as the active set: signature 2420 bytes, public
key 1312 bytes, plus headroom for higher Dilithium levels and script overhead.

`MAX_SCRIPT_SIZE`'s comment says 100000 is for complex post-quantum multisig
scripts: multiple Dilithium2 signatures and keys, plus overhead.

`Consensus::Params::MaxBlockSigOpsCost` returns
`MAX_BLOCK_SIGOPS_COST_DILITHIUM_LIMITS` once the block height is at or above
`nDilithiumLimitsHeight`, and `MAX_BLOCK_SIGOPS_COST` below it. That height is
1 on mainnet, testnet, signet, and regtest (`src/kernel/chainparams.cpp`).
Regtest can move it with `-testactivationheight`.

## Stack depth

`MAX_STACK_SIZE` is 1000. `EvalScript` fails when `stack.size() + altstack.size()`
goes above that (`src/script/interpreter.cpp`). A tapscript or P2MR witness
also rejects an initial stack deeper than 1000, before the script runs.

Worst-case stack memory for one evaluation is bounded by element size times
depth: an element is at most `MAX_SCRIPT_ELEMENT_SIZE` (15000) and the combined
stack is at most 1000. It is also bounded by `MAX_OPS_PER_SCRIPT` and by
`MAX_BLOCK_SERIALIZED_SIZE`. On legacy and witness v0 the interpreter counts
opcodes above `OP_16` and fails past 201, and the script being executed cannot
exceed `MAX_SCRIPT_SIZE` (100000). A block cannot exceed
`MAX_BLOCK_SERIALIZED_SIZE` (8000000).

A P2MR leaf does not take that per-opcode count, and it does not take
`MAX_SCRIPT_SIZE`. `OP_CHECKMULTISIGDILITHIUM` still adds its key count to
`nOpCount` and fails the count above 201. Element size and stack depth still
apply.

The reason `MAX_STACK_SIZE` was left at 1000 is not recorded.

## P2SH redeem scripts

A P2SH redeem script is the top stack item after the scriptSig runs, and
`VerifyScript` then executes it (`src/script/interpreter.cpp`). A push larger
than `MAX_SCRIPT_ELEMENT_SIZE` fails inside `EvalScript`, so the redeem script
can be up to that size (15000 bytes). The executed script is also subject to
`MAX_SCRIPT_SIZE` (100000). The push limit is the smaller one.

`MAX_P2SH_SIGOPS` is 15. `AreInputsStandard` rejects a P2SH input whose redeem
script sigop count is above it (`src/policy/policy.cpp`). That check is
standardness.

`PubkeyProvider` in `src/script/descriptor.cpp` has three implementations:
`OriginPubkeyProvider`, `ConstPubkeyProvider`, and `BIP32PubkeyProvider`. Each
one returns a `CPubKey`. There is no Dilithium `PubkeyProvider`. The `multi()`
parser comment for the P2SH size check says the only providers are 33-byte and
65-byte ECDSA keys, so 20 keys top out at `20 * (65 + 1) = 1320` bytes against
the 15000-byte element limit. The same comment says the check used to bite at
Bitcoin's 520, which capped a P2SH multisig at 15 compressed pubkeys. The same
file has `AddressDescriptor` cases for Dilithium hash destinations. Those
cases are separate from `PubkeyProvider`.

## Standard stack items

`MAX_STANDARD_P2WSH_STACK_ITEM_SIZE` and
`MAX_STANDARD_TAPSCRIPT_STACK_ITEM_SIZE` are defined as
`MAX_SCRIPT_ELEMENT_SIZE`. `IsWitnessStandard` compares P2WSH witness items to
the first, and tapscript items (Taproot leaf version `0xc0`, and the same check
on a P2MR witness) to the second (`src/policy/policy.cpp`). A later edit of the
consensus constant moves both policy limits with it. That coupling is what the
code does. The defines stay aliases of `MAX_SCRIPT_ELEMENT_SIZE`.

`MAX_STANDARD_SCRIPTSIG_SIZE` is the same alias. The `IsStandardTx` comment
says the scriptSig cap stays aligned with the per-element cap so a Dilithium
signature or pubkey can be relayed, and so a non-witness push is not accepted
up to the transaction weight limit (`src/policy/policy.cpp`).

## `MAX_OPCODE`

`MAX_OPCODE` is `OP_NOP10` (0xb9). The comment above it calls that the
pre-Taproot ceiling for `HasValidOps` and the asm parser. `OP_CHECKSIGADD`
(0xba) and the Dilithium opcodes, through the reserved `OP_DILITHIUM_PUBKEY`
(0xbf), sit above it.

`CScript::HasValidOps` returns false when an opcode is greater than
`MAX_OPCODE` (`src/script/script.cpp`). `script_HasValidOps` expects both
`0xba` and `0xbf` to fail (`src/test/script_tests.cpp`). `ParseScript` rejects
the name `OP_CHECKSIGADD` as an unknown opcode (`src/test/script_parse_tests.cpp`).

`EvalScript` rejects `OP_CHECKSIGADD` for `SigVersion::BASE` (legacy) and for
`SigVersion::WITNESS_V0`, with `SCRIPT_ERR_BAD_OPCODE`
(`src/script/interpreter.cpp`). In tapscript and P2MR tapscript the opcode is
implemented. `src/test/script_tests.cpp` runs a P2MR leaf that is only
`OP_CHECKSIGADD` and expects `SCRIPT_ERR_INVALID_STACK_OPERATION`.

`OP_DILITHIUM_PUBKEY` is reserved. Its comment says execution fails and that
0xbf must not be reused.
