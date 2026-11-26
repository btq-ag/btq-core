# Sigop accounting

`DILITHIUM_SIGOP_COST` is 50. It is a constant in `src/script/script.h`. Nothing in validation measures a signature and then picks a cost. The node charges 50 for each `OP_CHECKSIGDILITHIUM` or `OP_CHECKSIGDILITHIUMVERIFY` that `CScript::GetSigOpCount` sees in the revealed script.

## Where the charge is applied

`CScript::GetSigOpCount` (`src/script/script.cpp`) adds 50 for each Dilithium checksig opcode. For `OP_CHECKMULTISIGDILITHIUM` it multiplies 50 by the key count when the previous opcode is `OP_1` through `OP_16`, and by `MAX_PUBKEYS_PER_MULTISIG` (20) otherwise. That matches ECDSA multisig, which charges keys, not the threshold.

P2MR spends do not use that multisig opcode for the wallet's threshold leaf. `GetScriptForDilithiumThreshold` emits one `OP_CHECKSIGDILITHIUM` per key, then a count and `OP_GREATERTHANOREQUAL`. Twenty keys cost 20 times 50, which is 1,000. The same number falls out of the accurate multisig path at 20 keys.

`CountWitnessSigOps` (`src/script/interpreter.cpp`) pulls the leaf out of the witness and calls `GetSigOpCount`. It does not check the Merkle proof first. `GetTransactionSigOpCost` (`src/consensus/tx_verify.cpp`) adds that witness count as-is. Legacy and P2SH sigops are still multiplied by `WITNESS_SCALE_FACTOR` (16). Dilithium witness sigops are not.

`ConnectBlock` adds the cost and rejects the block with `bad-blk-sigops` when the running total passes `MAX_BLOCK_SIGOPS_COST` (80,000). That check runs before `CheckInputScripts`. A witness that cannot verify is still counted. The mempool uses the same cost against `MAX_STANDARD_TX_SIGOPS_COST` (16,000).

There is a second limit, `VALIDATION_WEIGHT_PER_DILITHIUM_SIGOP_PASSED` (500), subtracted inside `EvalChecksigDilithium` only when a non-empty signature is actually checked in P2MR tapscript. That is a per-script resource limit. It is not the block sigop budget.

After `DEPLOYMENT_DILITHIUM_P2MR`, Dilithium opcodes fail outside P2MR tapscript. A witness v0 script that merely contains the opcodes is still counted, which is the conservative direction. It cannot buy a free verify on the v0 path, because execution rejects the opcode.

## What 50 does to capacity

A normal one-input Dilithium5 spend weighs 8,211. At one signature per input the weight cap arrives first: 974 transactions, 48,700 sigops, under 80,000. Dilithium2 one-input spends still hit sigops first, at 1,600, because those inputs are smaller.

A script that is nothing but the opcode is one byte per 50 sigops. 1,600 opcodes weigh almost nothing, so the sigop cap binds and the weight cap does not. That is the case the constant still polices. Packing 1,600 real ML-DSA-87 signatures into one block is a different question. Each signature is 4,628 witness bytes. 1,600 of those is about 7.4 MB before the public keys and the non-witness bytes, so weight and sigops land close together. This run did not build that block.

## Tests that were run

`test/functional/feature_dilithium5_sigop_boundary.py`, 9 Nov 2026, against the Dilithium5 `btqd`:

- 1,600 `OP_CHECKSIGDILITHIUM` opcodes on a P2MR spend: `submitblock` returned `block-validation-failed`. Not `bad-blk-sigops`. The script is not a valid spend, and the tip did not move.
- 1,601 opcodes on P2MR: `bad-blk-sigops`.
- 1,601 opcodes on P2WSH: `bad-blk-sigops`.

`src/test/test_btq --run_test=sigopcount_tests` reported 5 cases and no errors.

`test/functional/feature_p2mr_dilithium_multisig.py` passed. Every 2-of-3 subset of the accumulator and of `OP_CHECKMULTISIGDILITHIUM` was mined. A single signature was rejected. A 3-of-3 leaf required all three.

## Recommendation

Leave the cost at 50. Dropping it would be a consensus change that lets a dense opcode script carry more checks, and the verify times in `CONNECTBLOCK_BENCHMARKS.md` do not show the current cap is too tight for wallet traffic. Raising it above 82 would start cutting ordinary one-input Dilithium5 blocks (974 times the new cost would exceed 80,000). That is also a consensus change, and weight already limits those blocks. Do not change the constant on this branch.
