# Protocol map: where each consensus rule lives

Each row names a protocol rule, the value or mechanism that realises it, and the area of the tree
where it is defined. Use it to check a stated rule against the code.

| Protocol rule | Value or mechanism | Where it lives |
|---|---|---|
| Signature scheme | ML-DSA-44 / Dilithium2; 1,312-byte public key, 2,560-byte secret key, 2,420-byte raw signature | `src/crypto/dilithium_wrapper.h`; `src/crypto/dilithium/` |
| Block capacity and witness scale | 8 MB serialized block cap; 8 MW block-weight cap; witness scale factor 16 | `src/consensus/consensus.h` |
| Monetary policy and timing | 60-second target spacing; 5 BTQ initial subsidy; 2,100,000-block halving interval; 21 million BTQ cap; 100-block coinbase maturity | `src/kernel/chainparams.cpp` |
| Difficulty adjustment | Bitcoin retarget before activation; LWMA after the height-gated activation, with a 144-block averaging window. Activation at genesis in the mainnet parameter set; block 300,000 on the public testnet | `src/pow.cpp`; `src/consensus/params.h`; `src/kernel/chainparams.cpp` |
| Script element and transaction limits | 15,000-byte maximum script element; 100,000-byte maximum script; 400,000-weight-unit standard transaction limit | `src/script/script.h`; policy limit definitions |
| Sigops accounting | 80,000 maximum block sigops cost; Dilithium checksig cost 50, counted for witness v2 P2MR leaf scripts; tapscript validation weight 500 per passing Dilithium check against 50 for ECDSA, with a 50-byte budget offset | `src/consensus/consensus.h`; `src/script/script.h`; `src/script/script.cpp`; `src/script/interpreter.cpp` |
| Address namespaces | Distinct Dilithium legacy prefixes; separate classical and Dilithium Bech32 HRPs | `src/kernel/chainparams.cpp`; `src/key_io.cpp` |
| P2MR support | Witness version 2; 32-byte Merkle-root output; consensus commitment verification | `src/addresstype.h`; `src/script/interpreter.cpp`; `src/validation.cpp` |
| Dilithium opcodes | `0xbb`–`0xbf`: Dilithium checksig, multisig, and public-key validation opcodes | `src/script/script.h`; `src/script/interpreter.cpp` |
| Sighash and dispatch | Explicit Dilithium opcode dispatch; pre-P2MR witness-v0 keyhash dispatch by public-key size, disabled under the P2MR-only rule; signer appends sighash byte | `src/script/sign.cpp`; `src/script/interpreter.cpp` |
| Dilithium activation | Two height-activated buried deployments: one gating Dilithium verification, one confining the Dilithium opcodes to P2MR tapscript and disabling witness-v0 Dilithium routing. Both at genesis in the mainnet parameter set; P2MR enabled unconditionally | `src/consensus/params.h`; `src/validation.cpp`; `src/kernel/chainparams.cpp` |
| Key derivation | Hardened-only Dilithium HD derivation. External keys `m/0'/0'/n'`, change keys `m/0'/1'/n'`. Non-hardened indices are refused: ML-DSA public keys admit no homomorphic tweak, so xpub-style derivation would produce an unrestorable keypair | `src/crypto/dilithium_key.h`; `src/crypto/dilithium_key.cpp`; `src/wallet/scriptpubkeyman.cpp` |
| Wallet RPC surface | `getnewdilithiumaddress`, `signmessagewithdilithium`, `verifydilithiumsignature`, `signtransactionwithdilithium`, `importdilithiumkey`. No `dumpdilithiumkey`. P2MR flow: `getnewp2mraddress`, `sendtop2mr`, `listp2mr`, `getp2mrinfo`, `createp2mrspend`, `signp2mrtransaction`, `testp2mrtransaction` | `src/wallet/rpc/dilithium.cpp` |

## Keeping it true

Paths drift. When a change moves one of the areas above, update the row in the same pull request.
A row that points at a file which no longer holds the rule is worse than no row at all.
