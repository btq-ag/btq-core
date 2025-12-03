# Dilithium Integration Roadmap – BTQ Repository

We’ve successfully integrated the **Dilithium cryptographic primitives** into the `src/crypto` module. Dilithium is now officially compiling and building in the BTQ repository 🎉

The next step is to extend support across the Bitcoin codebase so that Dilithium can be used for addresses, transactions, and eventually consensus/mining.

---

## Phase 1: Core Cryptography Wrappers
**Goal:** Provide clean C++ abstractions for Dilithium keys and signatures.

### Tasks
- Implement `CDilithiumKey` (private key wrapper):
  - Key generation via `crypto_sign_keypair()`
  - Transaction/message signing
  - Serialization & secure memory handling

- Implement `CDilithiumPubKey` (public key wrapper):
  - Signature verification
  - Public key validation (Dilithium2: 1312 bytes, Dilithium5: 2592 bytes)
  - Address derivation functions

**Deliverable:** Self-contained crypto wrappers with unit tests.

---

## Phase 2: Address & Script Layer Extensions
**Goal:** Add Dilithium to the Bitcoin address/script system.

### Tasks
- Extend `OutputType` enum with Dilithium variants
- New `CTxDestination` types (key hash, script hash, witness)
- Base58 & Bech32 support for Dilithium addresses
- Add script opcodes:
  - `OP_CHECKSIGDILITHIUM`
  - `OP_CHECKMULTISIGDILITHIUM`
  - `OP_DILITHIUM_PUBKEY`
- Update script interpreter for large signatures (~4.6KB)

**Deliverable:** Generate/validate Dilithium addresses & scripts.

---

## Phase 3: Wallet & Key Management
**Goal:** Enable wallets to hold and use Dilithium keys.

### Tasks
- Generate Dilithium keys in-wallet
- Encrypt/store keys in wallet DB
- Import/export keys (WIF support)
- HD wallet derivation (BIP32/44 pathing)
- Address book updates for Dilithium

**Deliverable:** Wallet users can transact with Dilithium addresses.

---

## Phase 4: Transaction Signing & Consensus Rules
**Goal:** Support transactions signed with Dilithium.

### Tasks
- Implement transaction input signing
- Extend signature caching
- Consensus rules for Dilithium signature validation
- Full `SIGHASH` support
- Mixed-mode transactions (ECDSA + Dilithium)

**Deliverable:** End-to-end Dilithium transaction support.

---

## Phase 5: Networking & Mempool Policy
**Goal:** Ensure the network supports Dilithium transactions.

### Tasks
- Update message size & `inv` message handling
- Block propagation with Dilithium signatures
- Mempool acceptance policy for larger transactions
- Update fee/weight calculations

**Deliverable:** Dilithium transactions flow across the P2P network.

---

## Phase 6: RPC & User Interface
**Goal:** Expose Dilithium functionality via RPC.

### Tasks
- New RPCs:
  - `generatedilithiumaddress`
  - `importdilithiumkey`
  - `dumpdilithiumkey`
  - `signtransactionwithdilithium`
  - `verifydilithiumsignature`
- Extend existing RPCs for Dilithium compatibility
- Message signing/verification with Dilithium

**Deliverable:** Users and developers can interact with Dilithium keys & transactions.

---

## Phase 7: Security, Testing, Deployment
**Goal:** Validate and safely roll out Dilithium integration.

### Tasks
- Unit tests for crypto operations
- Integration tests for transactions, blocks, and mempool
- Regression tests (ensure ECDSA unaffected)
- Performance benchmarks (Dilithium vs ECDSA)
- Fuzz testing of signature verification

**Deliverable:** Tested and deployable Dilithium support.

---
