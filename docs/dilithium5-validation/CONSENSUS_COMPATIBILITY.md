# Consensus and compatibility

No consensus constant was changed in this pass. `DILITHIUM_SIGOP_COST` stays 50. See `SIGOP_VALIDATION.md`.

## Dilithium2 material does not verify here

`BTQ_DILITHIUM_PUBLIC_KEY_SIZE` is 2,592, the signature size is 4,627, and the secret key is 4,896. `EvalChecksigDilithium` rejects a non-empty signature whose length is not `BTQ_DILITHIUM_SIGNATURE_SIZE + 1` with `SCRIPT_ERR_SIG_DER`, before `CheckDilithiumSignature`. `IsValidDilithiumPubKey` rejects a public key that is not exactly `CDilithiumPubKey::SIZE`. A Dilithium2 key is 1,312 bytes and a signature is 2,420. Both fail those comparisons.

The wrapper calls `pqcrystals_dilithium5_ref_*` only. The mode 2 sources are still in the tree so `contrib/devtools/dilithium-kat.sh` can build them. They are not linked into `btqd`.

This is intentional on `dilithium-5-tests`. It is not a compatible upgrade. Wallets, PSBTs, and descriptors that store a mode-2 blob will not round-trip into a verifier. There is no dual-verify path and no activation that accepts both sizes. Adding one would be a consensus design of its own, and it was not started.

`origin/master` at the same base commit still calls `pqcrystals_dilithium2_ref_*`. The Dilithium2 binary used for the benchmarks is that tree, unmodified.

## What was checked on the Dilithium5 binary

- Sigop boundary in `ConnectBlock`, including a P2WSH witness, so a non-P2MR script is not a way past the counter. `feature_dilithium5_sigop_boundary.py`.
- `sigopcount_tests`: the runner reported 5 cases, no errors. Those cases cover single-sig, accurate multisig key counts, and the witness flag.
- Real 2-of-3 and 3-of-3 spends mined, and under-threshold spends rejected. `feature_p2mr_dilithium_multisig.py`.
- Five nodes, including one restarted after missing blocks, reached one tip. `MULTINODE_STRESS_TEST.md`.

Malformed signatures shorter or longer than 4,628 bytes take the `SCRIPT_ERR_SIG_DER` path in the interpreter. This pass did not add a new test for a mutated signature inside an otherwise valid P2MR spend. The length check is the one in `interpreter.cpp` around the `BTQ_DILITHIUM_SIGNATURE_SIZE + 1` comparison. A wrong signature of the right length still has to fail `CheckDilithiumSignature`. The multisig test covers a real signature from the wrong signer, which is rejected. It does not cover a flipped bit in a valid signature as its own case.

## Weight

The 200-input blocks measured 1,751,244 weight for Dilithium5 and 1,053,844 for Dilithium2, both under 8,000,000, and both were accepted. The unit test on this branch already checks a single spend at 8,211 weight and a 20-of-20 at 145,588. Those unit tests were not re-run in this pass. The functional boundary test and the mined blocks were.

## Nodes agreeing

On each binary, the nodes that were still up at the end of the harness returned the same best block hash. Dilithium2 and Dilithium5 were not pointed at each other. They would not agree, because the transaction bytes differ. That split is the compatibility finding, not a bug inside one build.
