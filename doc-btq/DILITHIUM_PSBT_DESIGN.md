# Partially Signed BTQ Transactions (Dilithium PSBT)

**Status:** v1 implemented in btq-core and proven on the public testnet (§16 maps the spec to
the code and lists what was deferred; §17 records the live 2-of-3 spends)  
**Authors:** BTQ Core wallet/protocol working group  
**Date:** 2026-08-06  
**Related:** BIP174, BIP370, BIP341, BIP342, BIP360 (P2MR), `btq-multisig` `.btqms` format  
**Target implementation:** btq-core wallet + `btq-multisig` convergence

---

## Abstract

This document specifies **BTQ-PSBT**, an extension to BIP174 Partially Signed Bitcoin Transaction (PSBT) format enabling multi-party signing workflows for **Dilithium** signatures on **P2MR (witness v2)** outputs. The extension is strictly an **off-chain interchange format**. It requires **no consensus change, no hardfork, and no activation height**.

Consensus already validates Dilithium P2MR spends (`SigVersion::P2MR_TAPSCRIPT`, `OP_CHECKSIGDILITHIUM`, `OP_CHECKMULTISIGDILITHIUM`). Today, btq-core's PSBT stack round-trips only ECDSA `partial_sigs` and BIP341 Taproot fields. Dilithium material lives in `SignatureData` (`dilithium_signatures`, `p2mr_dilithium_script_sigs`, `p2mr_spenddata`) but is **dropped** at PSBT serialization boundaries. The interim `.btqms` JSON format in `btq-multisig` exists solely because of this gap.

BTQ-PSBT closes the gap with first-class typed fields, normative finalization for production multisig (accumulator leaves), fail-closed parsing/merge/verification, wallet/RPC integration, and a migration path from `.btqms`.

**Implementer note:** Two independent teams MUST be able to produce byte-compatible PSBTs using **only this document** (plus BIP174/BIP341/BIP360 references cited herein). No btq-core source inspection is required.

---

## 1. Problem statement

### 1.1 Current state

| Capability | On-chain (consensus) | In-wallet signing (`SignatureData`) | PSBT serialization | `.btqms` |
|------------|---------------------|-------------------------------------|-------------------|----------|
| ECDSA P2PKH/P2WPKH/P2TR | Yes | Yes | Yes (BIP174/371) | N/A |
| Dilithium P2MR single-key | Yes | Yes (RPC) | **No** | Partial (single policy) |
| Dilithium P2MR m-of-n multisig | Yes | Yes (`btq-multisig`) | **No** | Yes |
| P2MR leaf/control-block metadata | Yes | Yes (`P2MRSpendData`) | **No** | Yes (embedded in multisig ctx) |
| Hardware / air-gapped co-signing | N/A | N/A | **Blocked** | JSON workaround |
| Mixed-input txs (ECDSA + Dilithium) | Yes | Yes | **Partial** (ECDSA only in PSBT) | No |

### 1.2 Requirements

**Functional (MUST):**

1. Serialize and deserialize Dilithium partial signatures without loss.
2. Serialize P2MR script-path spend metadata (leaf script, leaf version, control block, merkle root).
3. Support multi-signer workflows: creator → signer A → signer B → finalizer → broadcaster.
4. Support transactions with **multiple inputs**, each independently Dilithium or ECDSA.
5. Verify each imported partial signature against the BIP341-style sighash for `SigVersion::P2MR_TAPSCRIPT` before accepting it.
6. Integrate with existing RPCs: `walletcreatefundedpsbt`, `walletprocesspsbt`, `utxoupdatepsbt`, `finalizepsbt`, `decodepsbt`.
7. Remain backward-compatible with unextended BIP174 parsers (unknown fields preserved per BIP174 §1.1.5).
8. Finalize production `btq-multisig` **accumulator** leaves (not only `OP_CHECKMULTISIGDILITHIUM`).

**Non-functional (MUST):**

1. Fail-closed parsing: malformed keys/values reject the entire PSBT load (audit item W-05).
2. Bounded resource use: explicit maxima on signature/pubkey sizes and per-input field counts.
3. Deterministic merge semantics when combining PSBTs from multiple signers.
4. No new trusted third parties; no new on-chain data.

**Explicit non-goals (WILL NOT in v1):**

1. Dilithium in legacy P2PKH/P2SH/P2WPKH script types (consensus-forbidden for new receives).
2. Falcon/SPHINCS+ PSBT fields (future extension).
3. QR-based co-signing for multisig PSBTs (size makes this unsafe; see §13).
4. In-place fee-bump that preserves existing partial signatures (requires re-sign; see §12).
5. HWI / hardware wallet transport (Phase 5 backlog).
6. Proprietary `"btq.org"` PSBT fields in production (see §3).

---

## 2. Background

### 2.1 BTQ Dilithium on-chain model

- **Algorithm:** CRYSTALS-Dilithium2 (default `PUBLIC_KEY_SIZE=1312`, `SIGNATURE_SIZE=2420` bytes).
- **Home for Dilithium scripts:** P2MR (SegWit v2, `WitnessV2P2MR`), BIP360-style Merkle tree of tapscript leaves (`leaf_version` typically `0xc0`).
- **Signature version:** `SigVersion::P2MR_TAPSCRIPT` (distinct from BIP342 `TAPSCRIPT`; Dilithium opcodes are rejected outside P2MR).
- **Sighash:** BIP341 `SignatureHashSchnorr` transcript with `execdata.m_tapleaf_hash` set. Full normative algorithm: **§2.5**.
- **On-wire witness layout (script path):** `[stack items…] | leaf_script | control_block`.

Partial Dilithium signatures in wallet memory are keyed by:

```cpp
// script/sign.h (abridged)
std::map<std::pair<DilithiumPKHash, uint256>,
         std::pair<CDilithiumPubKey, std::vector<unsigned char>>> p2mr_dilithium_script_sigs;
P2MRSpendData p2mr_spenddata; // merkle_root + scripts map
```

P2MR script-path spends **must** use `p2mr_dilithium_script_sigs` (pubkey hash + leaf hash → sig), not bare `dilithium_signatures`.

**Wire vs internal key encoding:** PSBT keys carry the full 1312-byte `CDilithiumPubKey`. Internal maps use `DilithiumPKHash` (20-byte `Hash160` of the pubkey). Conversion: `DilithiumPKHash keyid = DilithiumPKHash(pubkey.GetID())` where `GetID()` returns `Hash160(pubkey_bytes)`. See §4.2 and §5.1.

### 2.2 Production multisig: accumulator leaves (critical)

`btq-multisig` deliberately does **not** use `OP_CHECKMULTISIGDILITHIUM` for m-of-n. It uses a **threshold accumulator leaf** so unsigned keys contribute empty signature slots (BIP342 `OP_CHECKSIGADD` semantics):

```
OP_0                                    # acc = 0 (alt stack)
for each pubkey pk[k] in script order:
  OP_TOALTSTACK <pk[k]> OP_CHECKSIGDILITHIUM OP_FROMALTSTACK OP_ADD
<m> OP_GREATERTHANOREQUAL
```

Source: `btq-multisig/src/multisig.cpp` (`GetScriptForDilithiumThreshold`).

**Witness stack (normative, from `finalize.cpp`):**

```
[sig[n-1], …, sig[1], sig[0], leaf_script, control_block]
```

where `sig[k]` is either:
- `2421` bytes (`2420` sig + `1` sighash byte) if key `k` signed, or
- empty vector if key `k` did not sign.

Key `0` is processed first by the script, so its slot is **on top** of the stack → slots are pushed in **reverse key order**.

`ProduceSignature` / miniscript **does not** finalize accumulator scripts today. BTQ-PSBT finalization MUST implement explicit template handlers (§6.5).

### 2.3 Why BIP174 `PSBT_IN_PARTIAL_SIG` is insufficient

| | BIP174 partial sig | Dilithium partial sig |
|--|-------------------|----------------------|
| Key | `0x02 \|\| secp256k1_pubkey` (33/65 B) | `dilithium_pubkey (1312 B) \|\| leaf_hash (32 B)` |
| Value | ECDSA/DER (~72 B) | `2420 B sig + 1 B sighash_type` |

Reusing `PSBT_IN_PARTIAL_SIG` breaks parser key-length validation and conflates algorithms. BTQ-PSBT follows the BIP371 precedent (`PSBT_IN_TAP_SCRIPT_SIG`).

### 2.4 Interim `.btqms` format

JSON container with global `MultisigContext`, `unsigned_tx`, per-input `partial_sigs`. Limitations: single global policy, no merge tooling, hex bloat. BTQ-PSBT subsumes it (§8).

### 2.5 Normative Dilithium P2MR sighash (MUST)

This section is normative. Implementers MUST produce identical sighash digests without reading btq-core source.

#### 2.5.1 Overview

Dilithium P2MR script-path signatures use `SignatureHashSchnorr` with `SigVersion::P2MR_TAPSCRIPT`. The transcript is BIP341 TapSighash with BIP342 extension fields (`tapleaf_hash`, `key_version`, `codeseparator_pos`). There is **no** separate Dilithium sighash function.

#### 2.5.2 PrecomputedTransactionData (MUST)

Before calling `SignatureHashSchnorr`, build `PrecomputedTransactionData txdata` as follows:

```
BuildPrecomputedData(unsigned_tx, spent_outputs):
  REQUIRE spent_outputs.size() == unsigned_tx.vin.size()
  REQUIRE every spent_outputs[i].nValue > 0   // amount mandatory
  txdata.Init(unsigned_tx, spent_outputs, force=true)
  REQUIRE txdata.m_spent_outputs_ready == true
  REQUIRE txdata.m_bip341_taproot_ready == true
```

**`spent_outputs` source (in priority order):**

1. `witness_utxo` for each input index (preferred).
2. `non_witness_utxo` prevout at `unsigned_tx.vin[i].prevout.n`.

If any input amount is unknown, sighash computation MUST fail (`MissingDataBehavior::FAIL`). PSBT creators MUST supply amounts for all inputs before signing.

**`force=true`:** Required for unsigned PSBTs (empty witnesses). Forces BIP341 precomputation even when input witnesses are null.

#### 2.5.3 ScriptExecutionData (MUST)

For P2MR script-path Dilithium signing/verification of a partial signature:

```
execdata.m_annex_init           = true
execdata.m_annex_present        = false
execdata.m_codeseparator_pos_init = true
execdata.m_codeseparator_pos    = 0xFFFFFFFF
execdata.m_tapleaf_hash_init    = true
execdata.m_tapleaf_hash         = ComputeTapleafHash(leaf_version, leaf_script)
```

**Annex:** PSBT v1 does not support annexes. `m_annex_present` MUST be `false`.

**Codeseparator:** No `OP_CODESEPARATOR` in production v1 leaves. Position MUST be `0xFFFFFFFF` (BIP342 "none executed").

**Leaf hash:** MUST match the leaf identified by the spend path's `PSBT_IN_P2MR_LEAF_SCRIPT` entry.

#### 2.5.4 SignatureHashSchnorr call (MUST)

```
// Input: partial sig value OR PSBT_IN_SIGHASH
hashtype = sighash_type_byte from partial sig value (last byte)
         OR PSBT_IN_SIGHASH if no per-sig byte yet
REJECT if hashtype != 0x01

// Call:
SignatureHashSchnorr(sighash, execdata, tx, input_index,
                     hashtype, SigVersion::P2MR_TAPSCRIPT, txdata,
                     MissingDataBehavior::FAIL)
```

**`SigVersion::P2MR_TAPSCRIPT` transcript flags:**

| Field | Value |
|-------|-------|
| `ext_flag` | `1` |
| `key_version` | `0` |
| `spend_type` | `(ext_flag << 1) + annex_present` = `2` (no annex) |
| Additional fields appended | `m_tapleaf_hash`, `key_version`, `m_codeseparator_pos` |

The hash uses tagged SHA256 with tag `"TapSighash"` (BIP341). Full field order matches BIP341 §Common signature message + BIP342 §Common signature message extension.

**Unsupported hash types:** Reject `hashtype` not in `{0x01}`. See §4.6.

#### 2.5.5 sig_raw vs sig_with_hashtype (MUST)

| Artifact | Length | Use |
|----------|--------|-----|
| `sig_raw` | 2420 bytes | First 2420 bytes of partial sig value. Passed to `CDilithiumPubKey::Verify(sighash, sig_raw)`. |
| `sig_with_hashtype` | 2421 bytes | `sig_raw \|\| sighash_type_byte`. Stored as PSBT value; appended to witness stack on finalize. |

**Verification steps:**

```
sig_raw   = partial_sig_value[0:2420]
hashtype  = partial_sig_value[2420]
REJECT if hashtype != 0x01
sighash   = SignatureHashSchnorr(..., hashtype, ...)
ACCEPT iff pubkey.Verify(sighash, sig_raw) == true
```

**On-chain note:** Consensus `CheckDilithiumSignature` for `P2MR_TAPSCRIPT` **rejects** `SIGHASH_DEFAULT` (`0x00`) in the witness sighash byte, so a partial signature carrying `0x00` could never be spent. Parsers therefore reject `0x00` when the PSBT is read, as §4.6 states, rather than normalizing it to `0x01` and hiding the mistake until broadcast.

#### 2.5.6 ComputeTapleafHash (MUST)

```
leaf_hash = TaggedHash("TapLeaf", leaf_version || compact_size(script) || script)
```

where `leaf_version` is the single-byte leaf version (typically `div` 0xc0) and `script` is the raw leaf script bytes (not including the version byte).

---

## 3. Design decision

**Option B — first-class typed fields** (BIP-BTQ-PSBT), mirroring BIP371.

### 3.1 Proprietary `"btq.org"` fields — OUT OF v1 production scope

Early prototypes used `PSBT_*_PROPRIETARY` with identifier `"btq.org"` as an alternate encoding. **This is deprecated and excluded from v1 production scope.**

| Rule | v1 production behavior |
|------|------------------------|
| Producers | MUST NOT emit `"btq.org"` proprietary Dilithium fields |
| Readers | MAY reject PSBTs containing **only** proprietary Dilithium fields (no typed `0x19`–`0x1D`) |
| Mixed encoding | MUST reject PSBTs with both typed (`0x19`–`0x1D`) and proprietary Dilithium fields on the same input |
| Migration | Use `btqconvertpsbt` (§8) to convert legacy proprietary PSBTs to typed fields |

Typed fields (`0x19`–`0x1D`) are the **sole** normative v1 encoding. Independent implementers MUST NOT depend on proprietary fields.

---

## 4. Wire format specification (BTQ-PSBT v1)

### 4.1 Global extensions

| Type | Name | Key | Value |
|------|------|-----|-------|
| `0xFC` | `PSBT_GLOBAL_PROPRIETARY` | `identifier \|\| subtype` | opaque |

Optional capability advertisement (non-normative): identifier `"btq.org"`, subtype `0x01` = reader supports BTQ-PSBT v1 typed fields. PSBT version remains BIP174/BIP370 (`0` or `2`).

**Magic bytes:** unchanged (`psbt\xff`).

### 4.2 Input type bytes

| Type | Name | Key | Value | v1 |
|------|------|-----|-------|----|
| `0x19` | `PSBT_IN_P2MR_LEAF_SCRIPT` | `control_block` (variable) | `leaf_script \|\| leaf_version` (`CScript` + `uint8`) | Implemented |
| `0x1A` | `PSBT_IN_P2MR_MERKLE_ROOT` | *(empty)* | `uint256` | Implemented |
| `0x1B` | `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` | `dilithium_pubkey (1312) \|\| leaf_hash (32)` | `dilithium_sig (2420) \|\| sighash_type (1)` | Implemented |
| `0x1C` | `PSBT_IN_P2MR_DILITHIUM_BIP32_DERIVATION` | `dilithium_pubkey (1312)` | BIP174 derivation format | Deferred (§16.3) |
| `0x1D` | `PSBT_IN_P2MR_DILITHIUM_PUBKEY` | `dilithium_pubkey (1312)` | `leaf_hash_set \|\| KeyOriginInfo` (BIP371-style; §4.2.1) | Deferred (§16.3) |

The three implemented type bytes are sufficient for the full creator → signer → finalizer
workflow. `0x1C` and `0x1D` only carry derivation hints; a co-signer that already knows
its own keys does not need them, and reserving the bytes now keeps the door open. `0x1C`
and `0x1D` are therefore currently parsed as unknown fields and preserved verbatim per
BIP174 §1.1.5.

#### 4.2.1 `PSBT_IN_P2MR_DILITHIUM_PUBKEY` value layout (MUST)

Mirrors BIP371 `PSBT_IN_TAP_BIP32_DERIVATION` value encoding (`m_tap_bip32_paths` in btq-core `psbt.h`):

```
value = Serialize(leaf_hash_set) || SerializeKeyOrigin(origin)

Serialize(leaf_hash_set):
  compact_size(count) || for each h in set (sorted ascending): h (32 bytes)

SerializeKeyOrigin(origin):
  origin.fingerprint (4 bytes, little-endian uint32)
  for each index in origin.path: index (4 bytes, little-endian uint32)
```

**Deserialize:**

```
value_len = ReadCompactSize(stream)
before = stream.remaining()
leaf_hash_set = DeserializeSet<uint256>(stream)   // BIP174 set encoding
hashes_len = before - stream.remaining()
origin_len = value_len - hashes_len
origin = DeserializeKeyOrigin(stream, origin_len)
REQUIRE stream consumed exactly value_len bytes
```

**KeyOriginInfo path length:** `(origin_len - 4)` MUST be divisible by 4; otherwise reject.

#### 4.2.2 `PSBT_IN_P2MR_LEAF_SCRIPT` wire rules (MUST)

**Wire encoding:**

| Component | Content |
|-----------|---------|
| PSBT key | `[type=0x19] \|\| control_block` (raw bytes, `P2MR_CONTROL_BASE_SIZE`–`P2MR_CONTROL_MAX_SIZE`, i.e. 1–4097 bytes) |
| PSBT value | `leaf_script_bytes \|\| leaf_version` (version is **last** byte of value) |

**v1 cardinality:** Exactly **one** `(leaf_script, leaf_version)` pair per distinct `control_block` key. If the same `control_block` key appears twice (duplicate PSBT key) → **REJECT** at parse.

**Multiple leaves:** Multiple `PSBT_IN_P2MR_LEAF_SCRIPT` entries are allowed when `control_block` keys differ (different Merkle paths to different leaves).

#### 4.2.3 Leaf hash tie-break (MUST)

After parsing all `PSBT_IN_P2MR_LEAF_SCRIPT` entries, compute `leaf_hash = ComputeTapleafHash(leaf_version, leaf_script)` for each.

| Situation | Result |
|-----------|--------|
| Two entries yield same `leaf_hash` with **different** `control_block` OR **different** `(leaf_script, leaf_version)` | **REJECT** at parse (`PSBT_P2MR_METADATA_MISMATCH`) |
| Two entries yield same `leaf_hash` with **identical** `(control_block, leaf_script, leaf_version)` | **REJECT** at parse (duplicate key rule) |
| One `leaf_hash`, one entry | OK |

v1 partial signatures on an input MUST all reference this unique `leaf_hash` (§5.3.6).

#### 4.2.4 Partial signature key encoding (MUST)

**Wire (PSBT key for `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG`):**

```
key = [type=0x1B] || dilithium_pubkey (1312 bytes) || leaf_hash (32 bytes)
```

Total key data length (excluding compact-size prefix): `1 + 1312 + 32 = 1345` bytes.

**Internal map (`SignatureData::p2mr_dilithium_script_sigs`, `PSBTInput::m_p2mr_dilithium_script_sigs`):**

```
map_key = (DilithiumPKHash, leaf_hash)
map_value = (CDilithiumPubKey, sig_with_hashtype)
```

**Conversion (MUST, both directions):**

```
// Deserialize wire → internal:
pubkey = CDilithiumPubKey(key[1:1313])
leaf_hash = key[1313:1345]
keyid = DilithiumPKHash(pubkey.GetID())   // GetID() = Hash160(pubkey_bytes)
map[keyid, leaf_hash] = (pubkey, value)

// Serialize internal → wire:
pubkey = map_value.first
key = [0x1B] || pubkey (1312) || leaf_hash (32)
value = sig_with_hashtype (2421 bytes)
```

Implementations MUST NOT use `DilithiumPKHash` on the wire; MUST NOT use full pubkey as internal map key.

#### 4.2.5 Field rules (numbered)

1. `PSBT_IN_P2MR_LEAF_SCRIPT` key is the witness **control block** bytes. Value is the revealed `(leaf_script, leaf_version)`. See §4.2.2–§4.2.3.
2. `leaf_hash` in `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` MUST equal `ComputeTapleafHash(leaf_version, leaf_script)` for the leaf identified by a `PSBT_IN_P2MR_LEAF_SCRIPT` entry with matching spend path.
3. Duplicate `(pubkey, leaf_hash)` keys with **different** sig bytes → parse failure (`PSBT_DILITHIUM_SIG_INVALID`).
4. Duplicate `(pubkey, leaf_hash)` keys with **identical** sig bytes → parse failure at deserialize time. Exception: idempotent re-insert during **merge** only (§5.3.3).
5. All partial sigs on one input MUST share one `leaf_hash` (§5.3.6).
6. Dilithium2-only in v1: pubkey MUST be exactly 1312 bytes; partial sig value MUST be exactly 2421 bytes. Dilithium5 requires a future capability bit (§15 Q1).

### 4.3 Output type bytes

| Type | Name | Key | Value | v1 |
|------|------|-----|-------|----|
| `0x08` | `PSBT_OUT_P2MR_TREE` | *(empty)* | `tree_encoding` | Deferred (§16.3) |

**`tree_encoding`:** Same structure as BIP371 `PSBT_OUT_TAP_TREE`: DFS tuples `(depth, leaf_version, script)`.

**Parse-time validation (`PSBT_OUT_P2MR_TREE`):**

1. Each script length ≤ `MAX_P2MR_LEAF_SCRIPT_SIZE`.
2. Depths strictly increase per BIP371 DFS rules; reject malformed encodings.
3. Recompute Merkle root from tuples via `ComputeTapleafHash` / `ComputeTapbranchHash`.
4. If output `scriptPubKey` is P2MR (`OP_2 <32-byte root>`), recomputed root MUST equal embedded program. Mismatch → parse failure.

P2MR has **no internal key**; validation is root equality only.

### 4.4 Field presence rules

For input `i` spending P2MR via script path:

| Field | Signer | Finalizer |
|-------|--------|-----------|
| `witness_utxo` (preferred) or `non_witness_utxo` | Required | Required |
| `PSBT_IN_P2MR_LEAF_SCRIPT` | Required | Required |
| `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` | Optional | ≥ `m` valid partials (policy-dependent) |
| `PSBT_IN_SIGHASH` | Optional | Optional |
| `final_script_witness` | Must be absent | Required when complete |

**Mixed transactions:** Non-P2MR inputs MUST NOT carry `0x19`–`0x1D` or `PSBT_IN_TAP_*` fields. P2MR inputs MUST NOT carry Taproot fields.

### 4.5 Size limits (fail-closed)

| Constant | Value | Meaning |
|----------|-------|---------|
| `MAX_DILITHIUM_PUBKEY_SIZE` | 1312 | Raw pubkey bytes |
| `MAX_DILITHIUM_SIG_RAW_SIZE` | 2420 | Raw signature bytes (`sig_raw`) |
| `MAX_DILITHIUM_PARTIAL_SIG_VALUE_SIZE` | 2421 | PSBT value: `sig_raw + sighash_type` |
| `MAX_DILITHIUM_PARTIAL_SIGS_PER_INPUT` | 20 | Count of `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` entries |
| `MAX_DILITHIUM_PARTIAL_SIG_PAYLOAD_PER_INPUT` | 1_500_000 bytes (~1.5 MB) | Sum of partial sig value sizes |
| `MAX_P2MR_LEAF_SCRIPT_SIZE` | 100000 (`MAX_SCRIPT_SIZE`) | Leaf script bytes |
| `P2MR_CONTROL_MAX_SIZE` | 4097 | Control block bytes; the consensus limit, reused verbatim |
| `MAX_PSBT_SIZE` | 100 MB | Entire PSBT blob |

Per-input partial-sig payload is the sum over all `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` value sizes on that input. Exceeding any limit → reject entire PSBT load.

**The control block cap MUST be the consensus constant itself, never an independent value.** A PSBT parse cap below `P2MR_CONTROL_MAX_SIZE` would reject control blocks for leaves at depth 17 and beyond, which are consensus-valid and reachable through `createp2mr`. That would make an on-chain-spendable output unspendable through any conformant implementation, and would split interop against implementations that use the consensus bound. Derive this limit from `P2MR_CONTROL_BASE_SIZE`, `P2MR_CONTROL_NODE_SIZE` and `P2MR_CONTROL_MAX_NODE_COUNT` so the two cannot drift apart.

### 4.6 Sighash type matrix (v1)

| `sighash_type` byte | v1 support | Notes |
|---------------------|------------|-------|
| `0x00` (`SIGHASH_DEFAULT`) | **Rejected** | Consensus rejects a zero-length-flag Dilithium signature under `SigVersion::P2MR_TAPSCRIPT`, so a `0x00` partial sig could never be spent. Rejecting it at parse keeps the failure loud and early rather than at broadcast. |
| `0x01` (`SIGHASH_ALL`) | **Accepted** | The only supported type. Default for `.btqms` and v1 tooling. |
| `0x02` (`SIGHASH_NONE`) | **Rejected** | At parse if present on partial sig or `PSBT_IN_SIGHASH` |
| `0x03` (`SIGHASH_SINGLE`) | **Rejected** | |
| `0x81`/`0x82`/`0x83` (ANYONECANPAY variants) | **Rejected** | |

**Consistency rules:**

1. If `PSBT_IN_SIGHASH` is set on an input carrying Dilithium P2MR fields, it MUST be `0x01`.
2. Mixed-input txs MAY use any ECDSA sighash type on ECDSA inputs independently of the Dilithium inputs.
3. v2 of this spec MAY extend the matrix; v1 implementations MUST fail-closed on unsupported types.

### 4.7 Parse-time validation checklist

After deserializing a PSBT, and again after every `Merge`, implementations MUST run `ValidatePSBTInput(i)` for each input with P2MR fields:

```
ValidatePSBTInput(i):
  1. Resolve utxo = witness_utxo or non_witness_utxo prevout output
  2. Assert utxo.scriptPubKey classifies as WITNESS_V2_P2MR; extract program (32-byte root)
  3. For each PSBT_IN_P2MR_LEAF_SCRIPT (control_block → leaf_script, leaf_version):
       0. A leaf with no control block MUST be rejected. The wire cannot encode
          that state; Merge and in-process construction still can.
       a. control_block size ∈ [P2MR_CONTROL_BASE_SIZE, P2MR_CONTROL_MAX_SIZE]
       b. (control_block.size - P2MR_CONTROL_BASE_SIZE) % P2MR_CONTROL_NODE_SIZE == 0
       c. (control_block[0] & 1) == 1   // parity bit MUST be set (BIP360)
       d. leaf_script size ≤ MAX_P2MR_LEAF_SCRIPT_SIZE
       e. leaf_hash = ComputeTapleafHash(leaf_version, leaf_script)
       f. VerifyP2MRCommitment(control_block, program, leaf_hash) MUST be true
  4. Apply leaf_hash tie-break rules (§4.2.3)
  5. If PSBT_IN_P2MR_MERKLE_ROOT present: MUST equal program
  6. For each PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG:
       a. Pubkey MUST be fully valid Dilithium2 key (1312 bytes, not all-zero)
       b. sig value length MUST be 2421; sig_raw = first 2420 bytes
       c. sighash_type byte MUST be supported per §4.6
       d. leaf_hash MUST match validated leaf from step 3
       e. Pubkey MUST be authorized by parsed leaf policy (§5.5)
       f. Cryptographic verify REQUIRED (§2.5.5, §5.4):
            recompute sighash; reject if !pubkey.Verify(sighash, sig_raw)
  7. If PSBT_IN_SIGHASH present: MUST be supported per §4.6
  8. If both witness_utxo and non_witness_utxo: prevout MUST match unsigned tx input i
  9. Single leaf_hash rule (§5.3.6)
```

Failure at any step → reject with error code from §14. **Cryptographic verification is mandatory at parse**; there is no optional-only-at-parse mode.

### 4.8 Canonical encoding

1. Producers SHOULD serialize fields in ascending type-byte order.
2. Signatures stored as raw Dilithium bytes (not hex/DER).
3. On finalize: set `final_script_witness`; clear partial Dilithium fields for that input.

### 4.9 VerifyP2MRCommitment (normative)

Proves that `(leaf_script, leaf_version)` commits to the P2MR `program` (32-byte witness program) via `control_block`.

#### 4.9.1 Constants (MUST)

```
P2MR_CONTROL_BASE_SIZE       = 1
P2MR_CONTROL_NODE_SIZE       = 32
P2MR_CONTROL_MAX_NODE_COUNT  = 128
P2MR_CONTROL_MAX_SIZE        = 1 + 32 * 128 = 4097   // consensus maximum, and the PSBT parse cap (§4.5)
TAPROOT_LEAF_MASK            = 0xFE
TAPROOT_LEAF_TAPSCRIPT       = 0xC0
```

Control block layout:

```
control[0]           = leaf_version_with_parity   // (leaf_version & 0xFE) | 0x01
control[1:33]          = merkle_path_node[0]        // only if path_len >= 1
control[33:65]         = merkle_path_node[1]
...
control[1 + 32*i : 1 + 32*(i+1)] = merkle_path_node[i]
```

`path_len = (control.size() - P2MR_CONTROL_BASE_SIZE) / P2MR_CONTROL_NODE_SIZE`

#### 4.9.2 Algorithm (MUST)

```
VerifyP2MRCommitment(control, program, tapleaf_hash):
  REQUIRE control.size() >= P2MR_CONTROL_BASE_SIZE
  REQUIRE program.size() >= 32
  path_len = (control.size() - P2MR_CONTROL_BASE_SIZE) / P2MR_CONTROL_NODE_SIZE
  k = tapleaf_hash
  for i in 0 .. path_len-1:
    node = control[P2MR_CONTROL_BASE_SIZE + P2MR_CONTROL_NODE_SIZE*i :
                   P2MR_CONTROL_BASE_SIZE + P2MR_CONTROL_NODE_SIZE*(i+1)]
    k = ComputeTapbranchHash(k, node)
  return k == uint256(program[0:32])
```

**ComputeTapbranchHash:**

```
ComputeTapbranchHash(a, b) = TaggedHash("TapBranch", a || b)
```

where `a` and `b` are 32-byte branch values sorted lexicographically before hashing (BIP341).

**Control block validation (before commitment check):**

1. `control.size()` in `[1, P2MR_CONTROL_MAX_SIZE]` and `(control.size() - 1) % 32 == 0`.
2. `(control[0] & 1) == 1` (parity bit set).
3. `tapleaf_hash == ComputeTapleafHash(control[0] & TAPROOT_LEAF_MASK, leaf_script)`.

---

## 5. Semantic layer

### 5.1 C++ data model and wire round-trip (MUST)

#### 5.1.1 Internal structures

```cpp
struct PSBTInput {
    // ... existing BIP174/371 fields ...

    // BTQ-PSBT v1
    // Wire key = control_block; value = leaf_script || leaf_version
    // Internal: one (script, version) per control_block (v1)
    std::map<std::vector<unsigned char>, std::pair<std::vector<unsigned char>, int>>
        m_p2mr_leaf_scripts;   // control_block → (leaf_script, leaf_version)

    uint256 m_p2mr_merkle_root;

    // Wire key = full pubkey || leaf_hash; internal key uses DilithiumPKHash
    std::map<std::pair<DilithiumPKHash, uint256>,
             std::pair<CDilithiumPubKey, std::vector<unsigned char>>>
        m_p2mr_dilithium_script_sigs;

    std::map<CDilithiumPubKey, KeyOriginInfo> m_p2mr_dilithium_hd_keypaths;
    std::map<CDilithiumPubKey, std::pair<std::set<uint256>, KeyOriginInfo>>
        m_p2mr_dilithium_misc_pubkeys;
};
```

**Note on Taproot analogy:** BIP371 `m_tap_scripts` maps `(script, version) → set<control_block>`. BTQ-PSBT v1 inverts the wire key/value relationship: **wire key = control_block**, **wire value = script + version**, with exactly one script per control block. Do not copy the Taproot internal map structure literally.

#### 5.1.2 Serialize `PSBT_IN_P2MR_LEAF_SCRIPT`

```
for each (control_block, (script, leaf_ver)) in m_p2mr_leaf_scripts:
  key = SerializeToVector(PSBT_IN_P2MR_LEAF_SCRIPT, control_block)
  value = script_bytes || uint8(leaf_ver)
  emit key, value
```

#### 5.1.3 Deserialize `PSBT_IN_P2MR_LEAF_SCRIPT`

```
on entry (type == 0x19):
  if key already in key_lookup: REJECT ("duplicate key")
  control_block = key[1:]   // strip type byte
  read value_v from stream
  if value_v.empty(): REJECT
  leaf_ver = value_v.back()
  script = value_v[0 : len(value_v)-1]
  if control_block in m_p2mr_leaf_scripts: REJECT ("duplicate control_block")
  m_p2mr_leaf_scripts[control_block] = (script, leaf_ver)
  append post-parse tie-break check (§4.2.3)
```

#### 5.1.4 Serialize `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG`

```
for each ((keyid, leaf_hash), (pubkey, sig_with_hashtype)) in m_p2mr_dilithium_script_sigs:
  key = SerializeToVector(PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG, pubkey_bytes, leaf_hash)
  value = sig_with_hashtype   // 2421 bytes
  emit key, value
```

#### 5.1.5 Deserialize `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG`

```
on entry (type == 0x1B):
  if key already in key_lookup: REJECT
  REQUIRE key.size() == 1 + 1312 + 32
  pubkey = CDilithiumPubKey(key[1:1313])
  leaf_hash = uint256(key[1313:1345])
  read sig_with_hashtype from stream
  REQUIRE sig_with_hashtype.size() == 2421
  keyid = DilithiumPKHash(pubkey.GetID())
  m_p2mr_dilithium_script_sigs[(keyid, leaf_hash)] = (pubkey, sig_with_hashtype)
```

#### 5.1.6 HD / misc pubkey mapping

| PSBT field | C++ field | Round-trip |
|------------|-----------|------------|
| `PSBT_IN_P2MR_DILITHIUM_BIP32_DERIVATION` | `m_p2mr_dilithium_hd_keypaths` | Direct |
| `PSBT_IN_P2MR_DILITHIUM_PUBKEY` | `m_p2mr_dilithium_misc_pubkeys[pk] = ({leaf_hashes}, origin)` | Value layout §4.2.1 |
| Partial sigs | `m_p2mr_dilithium_script_sigs` | Key: `(DilithiumPKHash, leaf_hash)`; wire §4.2.4 |

`FillSignatureData` copies HD paths into `dilithium_misc_pubkeys` with empty leaf sets; `FromSignatureData` splits by presence of derivation path.

### 5.2 `FillSignatureData` / `FromSignatureData`

**Fill (PSBT → SignatureData):**

```
m_p2mr_leaf_scripts               → sigdata.p2mr_spenddata.scripts
                                    // invert to (script,version) → {control_block}
m_p2mr_merkle_root                → sigdata.p2mr_spenddata.merkle_root
m_p2mr_dilithium_script_sigs      → sigdata.p2mr_dilithium_script_sigs
m_p2mr_dilithium_hd_keypaths      → sigdata.dilithium_misc_pubkeys (with origin)
m_p2mr_dilithium_misc_pubkeys     → sigdata.dilithium_misc_pubkeys (merge leaf sets)
```

**From (SignatureData → PSBT):** Reverse when `!sigdata.complete`. When complete, write `final_script_witness` only.

**Invariant:** `SD → PSBT → SD'` must be signing-equivalent for all Dilithium/P2MR fields.

### 5.3 Merge semantics (normative)

Applies to `PSBTInput::Merge`, `CombinePSBTs`, and `PartiallySignedTransaction::Merge`.

#### 5.3.1 UTXO fields

| Situation | Result |
|-----------|--------|
| `witness_utxo` null ← non-null | Take non-null |
| Both non-null, same `nValue` and `scriptPubKey` | OK |
| Both non-null, differ in value or script | **REJECT** (`PSBT_P2MR_METADATA_MISMATCH`) |
| `non_witness_utxo` both present, different txid for same prevout | **REJECT** |

#### 5.3.2 P2MR metadata

| Field | Rule |
|-------|------|
| `PSBT_IN_P2MR_LEAF_SCRIPT` | Merge by `control_block` key. Same key + different `(script, version)` → **REJECT**. New keys → union. Re-run tie-break (§4.2.3). |
| `PSBT_IN_P2MR_MERKLE_ROOT` | Both present and differ → **REJECT**. One present → take it. |
| `PSBT_IN_SIGHASH` | Both present and differ → **REJECT**. Either value that is not `0x01` is also **REJECT**. |

After merge, run `ValidatePSBTInput(i)` (§4.7).

#### 5.3.3 Partial signatures

| Situation | Result |
|-----------|--------|
| New `(pubkey, leaf_hash)` | Insert after cryptographic verify (§5.4) |
| Same key, same sig bytes | Idempotent (keep existing). **Exception** to §4.2.5 rule 4 (parse-time duplicate rejection). |
| Same key, different sig bytes | **REJECT** (`PSBT_DILITHIUM_SIG_INVALID`) |

Imported partial sigs MUST pass `VerifyPartialSig` (§6.4) before merge accepts them.

#### 5.3.4 HD keypaths and misc pubkeys

| Field | Rule |
|-------|--------|
| `PSBT_IN_P2MR_DILITHIUM_BIP32_DERIVATION` | Merge by pubkey key. Same pubkey + different derivation path → **REJECT**. New pubkeys → union. |
| `PSBT_IN_P2MR_DILITHIUM_PUBKEY` | Merge by pubkey key. Union `leaf_hash` sets. Conflicting `KeyOriginInfo` for same pubkey → **REJECT**. |

Mirror BIP371 Taproot merge semantics (`m_tap_bip32_paths`, `m_tap_tree`-adjacent pubkey hints).

#### 5.3.5 Proprietary vs typed

If either side has typed Dilithium fields and the other has `"btq.org"` Dilithium proprietary fields on the same input → **REJECT**.

#### 5.3.6 Single leaf hash per input (v1)

All `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` entries on a given input **MUST** share the same `leaf_hash`. Finalization selects the unique `PSBT_IN_P2MR_LEAF_SCRIPT` entry whose `(leaf_script, leaf_version)` yields that hash. `ClassifyP2MRInput` evaluates policy for that leaf only.

Multi-leaf choice (signer picks among several valid leaves on one input) is **out of scope for v1**.

#### 5.3.7 `combinepsbt` vs `joinpsbts`

| Operation | Semantics | Dilithium rule |
|-----------|-----------|----------------|
| **`combinepsbt`** | Same unsigned tx; merge inputs at same index from co-signers | Apply §5.3 per input index |
| **`joinpsbts`** | Concatenate distinct inputs/outputs from different txs | Each appended input brings its own P2MR metadata; no index collision. Run §4.7 on full result. |

**Test coverage (§10):** separate cases for combine (co-signers, same tx) and join (disjoint inputs).

### 5.4 Valid partial signature (normative)

A partial signature entry is **valid** iff ALL of the following hold:

1. Pubkey is valid Dilithium2 (1312 bytes, passes `IsFullyValid()`).
2. Value length is exactly 2421 bytes.
3. Sighash type byte is supported per §4.6 (`0x01` only).
4. `leaf_hash` matches a validated `PSBT_IN_P2MR_LEAF_SCRIPT` entry.
5. Pubkey is authorized by the leaf policy (`IsAuthorizedPartialSig`, §5.5).
6. **Cryptographic verify:** `pubkey.Verify(sighash, sig_raw) == true` where `sighash` is computed per §2.5.

**Usage:**

| Context | Requirement |
|---------|-------------|
| PSBT deserialize / `ValidatePSBTInput` | MUST reject invalid partial sigs |
| `Merge` / `combinepsbt` / external import | MUST reject invalid partial sigs |
| `ClassifyP2MRInput` → `FINALIZABLE` | Count only **valid** partial sigs |
| `walletprocesspsbt` sign path | MUST verify before inserting own sigs |

Invalid partial sigs MUST NOT count toward threshold `m` and MUST NOT be silently dropped.

### 5.5 Leaf script parser (normative)

Implement `ParseP2MRDilithiumLeaf(const CScript& script)` returning:

```cpp
struct P2MRDilithiumLeafPolicy {
    P2MRLeafTemplate type;
    int m;                                    // threshold; 1 for single-key
    int n;                                    // total keys
    std::vector<CDilithiumPubKey> pubkeys;    // script order (k=0..n-1)
};
```

#### Template A — Single key

```
<pk (1312 bytes)> OP_CHECKSIGDILITHIUM
```

→ `type=SINGLE`, `m=1`, `n=1`, `pubkeys=[pk]`.

#### Template B — `OP_CHECKMULTISIGDILITHIUM`

```
<m> <pk1> … <pkn> <n> OP_CHECKMULTISIGDILITHIUM
```

→ `type=CHECKMULTISIGDILITHIUM`, `m`, `n`, pubkeys in listed order.

#### Template C — Threshold accumulator (production multisig)

```
OP_0
(OP_TOALTSTACK <pk> OP_CHECKSIGDILITHIUM OP_FROMALTSTACK OP_ADD) × n
<m> OP_GREATERTHANOREQUAL
```

Parsing algorithm:

1. Consume leading `OP_0`.
2. Repeat `n` times: `OP_TOALTSTACK`, push exactly 1312 bytes, `OP_CHECKSIGDILITHIUM`, `OP_FROMALTSTACK`, `OP_ADD`.
3. Consume threshold push `m`, then `OP_GREATERTHANOREQUAL`; no trailing ops.
4. → `type=THRESHOLD_ACCUMULATOR`, pubkeys in loop order.

**Threshold `m` push encoding (MUST):** When **generating** Template C scripts, push `m` using CScript minimal push semantics (`CScript::push_int64`):

| `m` value | Script bytes |
|-----------|--------------|
| `0` | `OP_0` |
| `1`–`16` | `OP_1` … `OP_16` (i.e. opcode `OP_1 + (m-1)`) |
| other | minimal signed-magnitude push (`CScriptNum::serialize`) |

When **parsing**, accept any valid script push encoding of `m` (minimal or non-minimal); decode with standard `CScriptNum` rules.

Any other pattern → `UNKNOWN`.

#### Authorization check

`IsAuthorizedPartialSig(policy, pubkey)`:

- SINGLE / CHECKMULTISIG / ACCUMULATOR: pubkey must appear in `policy.pubkeys`.
- UNKNOWN: reject partial sig (fail-closed).

### 5.6 Input completion classification

```cpp
// script/dilithium_leaf.h
enum class P2MRLeafTemplate {
    SINGLE_CHECKSIGDILITHIUM,
    CHECKMULTISIGDILITHIUM,
    THRESHOLD_ACCUMULATOR,  // production btq-multisig
    UNKNOWN,
};

// psbt_dilithium.h
enum class P2MRInputStatus {
    NOT_P2MR,            // input has no P2MR fields and does not spend a P2MR output
    FINALIZED,           // final_script_witness set
    UNKNOWN_LEAF,        // P2MR input whose spend path cannot be pinned down
    UNSIGNED,
    PARTIALLY_SIGNED,
    FINALIZABLE,         // enough valid partials, not yet finalized
};

// Also reports the leaf being spent, its policy, and the signature counts.
P2MRInputInfo InspectP2MRInput(const PartiallySignedTransaction& psbt, unsigned int index);
```

`NOT_P2MR` and `UNKNOWN_LEAF` are additions to the draft classification. `UNKNOWN_LEAF`
covers the two cases where the spend path is ambiguous — several advertised leaves with no
partial signature to pin one down, or partial signatures pointing at more than one leaf —
and is deliberately distinct from `UNSIGNED` so `decodepsbt` can say which it is.

**`FINALIZABLE` thresholds:** Count only **valid** partial sigs (§5.4).

| Template | Required valid partial sigs |
|----------|----------------------------|
| `SINGLE_CHECKSIGDILITHIUM` | 1 (authorized pubkey) |
| `CHECKMULTISIGDILITHIUM` | `m` from script |
| `THRESHOLD_ACCUMULATOR` | `m` from script (≥ `m` distinct authorized keys) |
| `UNKNOWN` | Only `FINALIZED` if `final_script_witness` present |

---

## 6. Signing, verification, and finalization

### 6.1 Workflow roles

Unchanged from BIP174: Creator → Updater → Signer(s) → Finalizer → Extractor.

### 6.2 Creator

MUST include `witness_utxo`, `PSBT_IN_P2MR_LEAF_SCRIPT`, and SHOULD include `PSBT_IN_P2MR_DILITHIUM_PUBKEY` for cosigners. SHOULD set `PSBT_IN_SIGHASH=1` (avoid `0x00` in finalized witnesses; §2.5.5).

### 6.3 Updater

When P2MR input lacks leaf fields, fill from wallet `GetP2MRSpendData`. Never guess leaf content.

### 6.4 Signer algorithm

For P2MR input `i`:

```
1. ValidatePSBTInput(i) — fail-closed (includes crypto verify of existing partials)
2. Parse leaf policy from PSBT_IN_P2MR_LEAF_SCRIPT
3. leaf_hash = ComputeTapleafHash(leaf_version, leaf_script)
4. Build PrecomputedTransactionData per §2.5.2 (all input amounts required)
5. Setup ScriptExecutionData per §2.5.3
6. sighash = SignatureHashSchnorr(..., SigVersion::P2MR_TAPSCRIPT, §2.5.4)
7. For each wallet key K authorized by policy:
     sig_raw = Sign(K, sighash)   // 2420 bytes
     assert K.Verify(sighash, sig_raw)
     sighash_byte = PSBT_IN_SIGHASH or 0x01; reject if present and not 0x01
     sig_with_hashtype = sig_raw || sighash_byte
     insert PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG (wire key uses full pubkey)
8. Do not set final_script_witness unless explicitly finalizing AND FINALIZABLE
```

**Import verification (mandatory on merge/combine/external sign AND at parse):**

```
VerifyPartialSig(input, i, pubkey, leaf_hash, sig_with_hashtype):
  sig_raw = sig_with_hashtype[0:2420]
  hashtype = sig_with_hashtype[2420]
  reject if hashtype unsupported (§4.6)
  reject if hashtype is not 0x01 or conflicts with PSBT_IN_SIGHASH
  recompute sighash per §2.5 (PrecomputedTransactionData + ScriptExecutionData)
  reject if !pubkey.Verify(sighash, sig_raw)
  reject if !IsAuthorizedPartialSig(policy, pubkey)
```

### 6.5 Finalizer (normative witness construction)

Miniscript does not model BTQ's Dilithium opcodes, so `ProduceSignature` alone cannot
finalize any of these leaves. Rather than add a second, parallel finalizer, the
implementation teaches the existing signing path about Dilithium leaves and lets
`FinalizePSBT` reach them through the ordinary `SignPSBTInput` → `ProduceSignature` →
`SignP2MR` → `SignTaprootScript` chain:

- `SignTaprootScript` falls back, under `SigVersion::P2MR_TAPSCRIPT` only, to the Solver
  for the single-key and `OP_CHECKMULTISIGDILITHIUM` templates, and to
  `SignDilithiumAccumulatorLeaf` for the accumulator template that Solver cannot classify.
- Signing and finalizing are then the same operation with a different set of available
  keys: a signer with one key produces one partial and the step reports incomplete; a
  finalizer with no keys at all reuses the partials the PSBT already carries and the step
  reports complete. `ProduceSignature` verifies the assembled witness against the
  transaction before declaring success, so a finalized input is always spendable.

The resulting witness stacks are exactly as specified below.

```
FinalizeP2MRInput(input, tx, index, utxo):
  if final_script_witness already set: return OK
  policy = ParseP2MRDilithiumLeaf(leaf_script)
  leaf_hash = ComputeTapleafHash(...)
  collect VALID partial sigs for leaf_hash into map key_index → sig_with_hashtype

  switch (policy.type):

    SINGLE_CHECKSIGDILITHIUM:
      sig = partial for policy.pubkeys[0]
      witness.stack = [ sig_with_hashtype, leaf_script, control_block ]

    CHECKMULTISIGDILITHIUM:
      witness.stack = build per §6.5.1

    THRESHOLD_ACCUMULATOR:
      slots[k] = valid partial for pubkey[k] or empty vector
      witness.stack = [ slots[n-1], ..., slots[0], leaf_script, control_block ]

    UNKNOWN:
      return INVALID_PSBT

  Reject any stack sighash byte that is not 0x01 (do not rewrite 0x00)
  VerifyScript(..., STANDARD_SCRIPT_VERIFY_FLAGS) MUST succeed
  set final_script_witness; clear partial Dilithium fields
```

#### 6.5.1 CHECKMULTISIGDILITHIUM witness stack (normative)

Matches `SignStep` / `TxoutType::DILITHIUM_MULTISIG` in `sign.cpp`:

Script template: `<m> <pk1> … <pkn> <n> OP_CHECKMULTISIGDILITHIUM`

**Stack layout (bottom → top, before `leaf_script` and `control_block`):**

```
stack[0] = empty vector (0 bytes)           // CHECKMULTISIG dummy element (off-by-one)
stack[1] = sig_with_hashtype for pk1        // vSolutions[1]
stack[2] = sig_with_hashtype for pk2        // vSolutions[2]
...
stack[n] = sig_with_hashtype for pkn        // vSolutions[n]
stack[n+1] = leaf_script bytes
stack[n+2] = control_block bytes
```

**Signature ordering:** Signatures appear in **pubkey script order** (`pk1`, `pk2`, …, `pkn`), **not** reverse order. This differs from the accumulator template (§2.2).

**Partial sig mapping:** For each `k` in `1..n`, if a valid partial sig exists for `policy.pubkeys[k-1]`, include its `sig_with_hashtype`; otherwise signing fails (CHECKMULTISIG requires `m` valid sigs on finalize, not empty slots).

**Empty dummy:** `stack[0]` MUST be an empty byte vector (not `OP_0` as a one-byte push in the witness — the witness element itself is zero-length).

This matches `btq-multisig/src/finalize.cpp` for `THRESHOLD_ACCUMULATOR`. CHECKMULTISIGDILITHIUM follows `sign.cpp` as above.

### 6.6 Extraction

`finalizepsbt` → `extractpsbt`. Extracted tx MUST pass `testmempoolaccept`.

---

## 7. Wallet, descriptor, and RPC integration

### 7.1 RPC changes

| RPC | Change |
|-----|--------|
| `decodepsbt` | Adds `p2mr_scripts` (with the recognised leaf policy), `p2mr_merkle_root`, `p2mr_dilithium_script_path_sigs`, and a `p2mr_dilithium` progress object from `InspectP2MRInput` |
| `walletcreatefundedpsbt` | Attaches P2MR leaf, control block and merkle root for wallet inputs |
| `walletprocesspsbt` | Dilithium signing; contributes every key it holds in one pass |
| `finalizepsbt` | §6.5 finalizer |
| `combinepsbt` / `joinpsbts` / `utxoupdatepsbt` | Work unchanged: validation happens on decode, so every PSBT these RPCs read has already been checked |

Every RPC that reads a PSBT gets validation for free because `DecodeRawPSBT` and
`DecodeBase64PSBT` run §4.7 before returning. There is no way to hand an unvalidated
Dilithium PSBT to any caller.

**New wallet RPCs:**

| RPC | Purpose |
|-----|---------|
| `createdilithiummultisig <nrequired> <pubkeys> [label]` | Register an m-of-n threshold accumulator P2MR destination. Co-signers passing the same arguments derive the same address. |
| `getdilithiumpubkey <p2mr_id>` | Report the Dilithium pubkeys a wallet destination names, and which of them the wallet can sign for. This is how a co-signer publishes its key. |

**CLI recipe:** `doc/psbt.md` (section “Dilithium P2MR”) is the copy-paste
`btq-cli` flow. `contrib/btq/dilithium-psbt.sh` wraps those same RPCs so the
~16 KB PSBT does not have to be pasted as a shell argument.

**Deferred:** `btqconvertpsbt <btqms-file>` (§16.3).

### 7.2 Descriptor alignment

Illustrative:

```
p2mr({{leaf_v:0xc0,script:dilithium_threshold(2,pk1,pk2,pk3)}})
```

Descriptors carry policy; PSBT carries spend path.

---

## 8. Migration from `.btqms`

### 8.1 Field mapping

| `.btqms` | BTQ-PSBT |
|----------|----------|
| `unsigned_tx` | `PSBT_GLOBAL_UNSIGNED_TX` |
| `multisig.leaf_script`, `leaf_version`, `control_block` | Per-input `PSBT_IN_P2MR_LEAF_SCRIPT` |
| `multisig.merkle_root` | Per-input `PSBT_IN_P2MR_MERKLE_ROOT` |
| `inputs[i].partial_sigs` | Per-input `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` |
| `inputs[i].amount` | `witness_utxo.nValue` |
| `multisig.pubkeys` | `PSBT_IN_P2MR_DILITHIUM_PUBKEY` |

### 8.2 Multi-input conversion rules

```
convert_btqms_to_psbt(btqms):
  1. Decode unsigned_tx
  2. For each input index i:
       a. If prevout script is NOT P2MR → copy input fields only (no P2MR metadata)
       b. If P2MR:
            - If global multisig.script_pubkey matches prevout → attach leaf/control_block/metadata to input i
            - Else if wallet knows different P2MRSpendData for prevout → use wallet data
            - Else → FAIL (cannot infer policy)
  3. If multiple P2MR inputs require DIFFERENT multisig contexts → FAIL with
     "btqms global multisig incompatible with multi-policy transaction"
  4. Convert partial_sigs using leaf_hash from attached leaf
  5. Run ValidatePSBT on result
```

**Lossless case:** All P2MR inputs spend the same `MultisigContext` (typical `.btqms` use). **General case:** Requires per-input descriptors in PSBT from the start (no global multisig blob).

### 8.3 Deprecation

1. Ship BTQ-PSBT; `btq-multisig --format psbt` default.
2. Warn on `.btqms` write.
3. `.btqms` read-only import via converter.

---

## 9. Security analysis

| Threat | Mitigation |
|--------|------------|
| DoS (oversized fields) | §4.5 per-field and per-input caps |
| Wrong sighash | §2.5 normative sighash + §4.6 matrix + mandatory import verify |
| Unauthorized partial sig | §5.5 parser + authorization |
| Leaf / root substitution | §4.9 `VerifyP2MRCommitment` at parse/merge |
| Fee attack | Signers inspect fee before signing (UI warning) |
| Algorithm confusion | Separate type bytes; strict pubkey/sig lengths |
| Dual proprietary/typed encoding | §3 rejection rule |
| Forged partial sigs | §5.4 cryptographic verify at parse/merge |

**No hardfork.** PSBT never enters consensus.

---

## 10. Test strategy

### 10.1 Unit tests

`src/test/psbt_dilithium_tests.cpp`:

| Test | Covers |
|------|--------|
| `threshold_leaf_roundtrips_through_the_parser` | §2.2 builder ↔ §5.5 parser, key ordering |
| `single_key_leaf_is_recognised` | §5.5 single-key template |
| `unknown_leaves_are_not_misparsed` | §5.5 `UNKNOWN`; a truncated accumulator is not accepted |
| `threshold_witness_puts_the_first_key_on_top` | §6.5 accumulator stack order and empty slots; below-threshold refusal |
| `single_key_psbt_survives_serialization` | §4/§5.2 round-trip; a key-less provider finalizes what the PSBT carries |
| `threshold_psbt_accumulates_signatures_across_providers` | §5.3 merge of two independently produced partials, then `VerifyScript` |
| `finalized_input_drops_the_bulky_signing_material` | §6.5 field clearing on finalize |
| `a_forged_signature_is_rejected_on_decode` | §5.4 cryptographic verify at parse |
| `a_leaf_not_committed_to_by_the_output_is_rejected` | §4.9 commitment check |
| `a_merkle_root_disagreeing_with_the_output_is_rejected` | §4.7 step 5 |
| `duplicate_wire_keys_are_rejected` | §4.2 duplicate-key rule |

### 10.2 Functional tests

`test/functional/wallet_dilithium_psbt_multisig.py` — three separate wallets, each holding
one key, communicating only in base64 PSBTs:

1. All three derive the same 2-of-3 address from the same arguments.
2. An unsigned PSBT already advertises the leaf, its policy and the merkle root.
3. Alice signs; the serialized PSBT carries exactly one 2421-byte signature.
4. Carol signs the same unsigned PSBT independently; `combinepsbt` merges the two.
5. `finalizepsbt` → `testmempoolaccept` → broadcast → mined.
6. Sequential signing (alice, then bob on alice's output) also completes.
7. Re-signing with a key that already signed does not inflate the count, and a PSBT one
   signature short does not finalize.
8. A PSBT with one byte flipped inside a signature is rejected by both `decodepsbt` and
   `finalizepsbt`.

`test/functional/wallet_dilithium_psbt.py` (pre-existing) covers the single-key and
mixed ECDSA + Dilithium cases within one wallet.

**Not yet covered:** `joinpsbts` with a Dilithium input in each transaction, and the
`.btqms` migration equivalence check (§16.3).

### 10.3 Fuzz

`fuzz/psbt_dilithium` with structured seeds from valid templates. Not yet written; the
existing `psbt` fuzz target does exercise the new parse paths through
`PartiallySignedTransaction::Unserialize`.

---

## 11. Transport guidance

| Method | Multisig PSBT | Single-key PSBT |
|--------|---------------|-----------------|
| File (binary/base64) | **Required** | Recommended |
| QR / fragmented QR | **Forbidden** (a 2-of-3 is ~16 KB base64; see Appendix A) | Impractical |
| Clipboard | Discouraged (size) | OK with care |

Co-signers MUST verify `decodepsbt` output (fee, outputs, addresses) before signing.

---

## 12. Fee bumping / RBF (v1 scope)

**Supported:** `bumpfee` / `psbtbumpfee` creates a **new** PSBT with replaced inputs/outputs. All prior Dilithium partial signatures are **invalid** (sighash changes). Signers MUST re-sign from scratch.

**Not supported in v1:** Preserving partial sigs across fee bumps (impossible without identical sighash). Document explicitly in RPC help text.

Wallet weight estimation for P2MR inputs already accounts for Dilithium witness size in `feebumper.cpp`; PSBT creation MUST use the same estimator.

---

## 13. Implementation plan

| Phase | Deliverables | Status |
|-------|--------------|--------|
| 0 — Spec freeze | Frozen type bytes `0x19`/`0x1A`/`0x1B` | Done |
| 1 — Serialization | §4 fields; §5.2 round-trip; parse validation | Done |
| 2 — Parser + finalizer | §5.5 parser; §6.5 finalizer; import verify | Done |
| 3 — Wallet RPC | §7 RPC surface; unit + functional tests; live-chain proof | Done |
| 4 — btq-multisig + migration | `--format psbt`; `btqconvertpsbt` | Deferred (§16.3) |
| 5 — HWI prep | Protocol draft | Backlog |

See §16 for the spec-to-code map.

---

## 14. Error taxonomy

Extend `TransactionError` (or map to `INVALID_PSBT` sub-messages):

| Code | When |
|------|------|
| `INVALID_PSBT` | Generic parse failure; malformed key/value; oversize fields |
| `PSBT_MISMATCH` | Unsigned tx mismatch on combine |
| `PSBT_P2MR_METADATA_MISMATCH` | UTXO/leaf/root conflict on merge; leaf_hash tie-break violation (§4.2.3); merkle root mismatch; control block / program mismatch |
| `PSBT_DILITHIUM_SIG_INVALID` | Failed cryptographic verify; duplicate conflicting sig; wrong sig length |
| `PSBT_DILITHIUM_SIG_UNAUTHORIZED` | Pubkey not in leaf policy |
| `PSBT_DILITHIUM_SIGHASH_UNSUPPORTED` | §4.6 violation |
| `SIGHASH_MISMATCH` | Per-input sighash conflict between partial sigs or vs `PSBT_IN_SIGHASH` |

### 14.1 ValidatePSBTInput step → error code mapping

| ValidatePSBTInput step (§4.7) | Error code |
|--------------------------------|------------|
| Step 1–2: missing/bad UTXO; not P2MR | `INVALID_PSBT` |
| Step 3a–3b: control block size invalid | `INVALID_PSBT` |
| Step 3c: parity bit not set | `PSBT_P2MR_METADATA_MISMATCH` |
| Step 3d: leaf script oversize | `INVALID_PSBT` |
| Step 3f: `VerifyP2MRCommitment` false | `PSBT_P2MR_METADATA_MISMATCH` |
| Step 4: leaf_hash tie-break | `PSBT_P2MR_METADATA_MISMATCH` |
| Step 5: merkle root mismatch | `PSBT_P2MR_METADATA_MISMATCH` |
| Step 6a: invalid pubkey | `INVALID_PSBT` |
| Step 6b: bad sig length | `PSBT_DILITHIUM_SIG_INVALID` |
| Step 6c: unsupported sighash | `PSBT_DILITHIUM_SIGHASH_UNSUPPORTED` |
| Step 6d: leaf_hash not found | `PSBT_P2MR_METADATA_MISMATCH` |
| Step 6e: unauthorized pubkey | `PSBT_DILITHIUM_SIG_UNAUTHORIZED` |
| Step 6f: crypto verify fails | `PSBT_DILITHIUM_SIG_INVALID` |
| Step 7: bad `PSBT_IN_SIGHASH` | `PSBT_DILITHIUM_SIGHASH_UNSUPPORTED` |
| Step 8: witness/non-witness prevout mismatch | `PSBT_MISMATCH` |
| Step 9: multiple leaf_hashes on input | `PSBT_P2MR_METADATA_MISMATCH` |
| Duplicate key at deserialize | `INVALID_PSBT` |

RPC layer maps to `RPC_DESERIALIZATION_ERROR` (parse) or `RPC_INVALID_PARAMETER` (merge/policy) with full diagnostic string. Wallets MUST NOT mutate PSBT state on error.

---

## 15. Resolved open questions

| ID | Decision |
|----|----------|
| Q1 Dilithium5 | v2; v1 rejects non-1312/2420/2421 sizes |
| Q2 non_witness_utxo | Allowed; witness_utxo preferred |
| Q3 PSBT v2 | Support v0 and v2 |
| Q4 BIP registration | `doc-btq/bips/bip-btq-psbt.mediawiki` before public release |
| Q5 Single-key tuple | Always `(pubkey, leaf_hash)` for uniformity; wire uses full pubkey, internal uses `DilithiumPKHash` |
| Q6 Accumulator vs CHECKMULTISIG | Spec normative for **accumulator**; CHECKMULTISIG supported but not used by `btq-multisig` |
| Q7 Proprietary + typed | MUST NOT co-exist on same input; proprietary OUT OF v1 production (§3) |
| Q8 Crypto verify at parse | **Required** (§4.7, §5.4); no optional-only mode |

---

## 16. Implementation status

### 16.1 Spec-to-code map

| Spec section | Code |
|--------------|------|
| §4.2 type bytes, wire encoding, size limits | `src/psbt.h` — `PSBT_IN_P2MR_LEAF_SCRIPT`, `PSBT_IN_P2MR_MERKLE_ROOT`, `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG`; `PSBTInput::Serialize` / `Unserialize` |
| §4.7 parse-time validation | `src/psbt_dilithium.cpp` — `ValidateP2MRDilithiumInput`, called from `DecodeRawPSBT` |
| §4.9 P2MR commitment check | `ComputeP2MRMerkleRoot`, exported from `src/script/interpreter.cpp` (previously private to `VerifyP2MRCommitment`) |
| §5.2 `FillSignatureData` / `FromSignatureData` | `src/psbt.cpp` |
| §5.3 merge | `PSBTInput::Merge` in `src/psbt.cpp` |
| §5.5 leaf parser, §2.2 accumulator builder | `src/script/dilithium_leaf.{h,cpp}` — `ParseP2MRDilithiumLeaf`, `GetScriptForDilithiumThreshold`, `BuildDilithiumLeafWitness` |
| §5.6 classification | `InspectP2MRInput` in `src/psbt_dilithium.cpp` |
| §6.4 signer, §6.5 finalizer | `SignDilithiumAccumulatorLeaf` and the `SigVersion::P2MR_TAPSCRIPT` fallback in `SignTaprootScript`, `src/script/sign.cpp` |
| §7.1 RPC | `src/rpc/rawtransaction.cpp` (`decodepsbt`), `src/wallet/rpc/dilithium.cpp` (`createdilithiummultisig`, `getdilithiumpubkey`) |
| Wallet key discovery for accumulator leaves | `GetP2MRDilithiumKeyIDs` in `src/wallet/p2mr.cpp` |
| §10.1 unit tests | `src/test/psbt_dilithium_tests.cpp` |
| §10.2 functional tests | `test/functional/wallet_dilithium_psbt_multisig.py` |
| Live-chain proof | `contrib/btq/testnet_dilithium_psbt_proof.sh` |
| Operator CLI wrapper | `contrib/btq/dilithium-psbt.sh`; recipe in `doc/psbt.md` |

### 16.2 Deviations from the draft

| Draft | Implemented | Why |
|-------|-------------|-----|
| §4.6 accepts `SIGHASH_DEFAULT` (`0x00`), normalized to `0x01` | Only `SIGHASH_ALL` (`0x01`) accepted | Consensus rejects a zero-length sighash flag for Dilithium under `SigVersion::P2MR_TAPSCRIPT`, so a `0x00` partial could never be spent. Accepting then silently rewriting it would hide the mistake until broadcast. |
| §6.5 a dedicated `FinalizeP2MRInput` separate from `ProduceSignature` | The existing `ProduceSignature` chain is taught about Dilithium leaves | One code path means signing and finalizing cannot disagree, and `ProduceSignature` already verifies the assembled witness against the transaction before reporting success. |
| §5.6 `PSBTInputStatus` with 4 values | `P2MRInputStatus` with 6, adding `NOT_P2MR` and `UNKNOWN_LEAF` | An ambiguous spend path is a distinct, reportable state rather than something to conflate with `UNSIGNED`. |
| §4.5 separate `MAX_DILITHIUM_PARTIAL_SIG_PAYLOAD_PER_INPUT` of 1.5 MB | Implied by the per-input count limit | 20 signatures × 2421 bytes caps the payload at ≈ 48 KB, well under the draft's figure, so a second check would be dead code. |

### 16.3 Deferred

None of these block the creator → signer → finalizer workflow:

- `PSBT_IN_P2MR_DILITHIUM_BIP32_DERIVATION` (`0x1C`) and `PSBT_IN_P2MR_DILITHIUM_PUBKEY`
  (`0x1D`). Derivation hints only. The type bytes stay reserved.
- `PSBT_OUT_P2MR_TREE` (`0x08`). Needed to *receive* into a P2MR tree described by the
  PSBT rather than by wallet metadata.
- `btqconvertpsbt` and `btq-multisig --format psbt` (§8 migration). `.btqms` files still
  need to be converted by hand.
- HWI / hardware transport (§13 phase 5).

### 16.4 Live-chain proof

Recorded in §17.

---

## 17. Live-chain proof (public testnet)

Run with `contrib/btq/testnet_dilithium_psbt_proof.sh` against a node on the public BTQ
testnet (the v0.4.x parameter set, genesis `000000ffba1eed…`), starting at height 215447.
Coins came from CPU-mining 60 blocks to a fourth, unrelated funding wallet and waiting out
coinbase maturity.

### 17.1 The 2-of-3

Three wallets in the same node, each holding exactly one 1312-byte Dilithium key and
never seeing the others' private material. Given the same three pubkeys and threshold,
each derived the same address independently:

```
address:      tbtq1zv5frdx000hfzyljdrcdwltj2z3gy36jqucf8y53pju0g5vsn76esg6s0s2
scriptPubKey: 5220 65123699ef7dd2227e4d1e1aefae4a145048ea40e612725221971e8a3213f6b3
leaf script:  3960 bytes
```

Each reported `signers_available: 1`, i.e. no wallet could reach the threshold alone.

### 17.2 The spend

| Step | Result |
|------|--------|
| Funding | `a0a8ec548a9d370ce5a7b3397ce3fb673ae975b60e611306e2a0bffe6e5e94f1` vout 0, 0.001 BTQ |
| Unsigned PSBT (`decodepsbt`) | `dilithium_threshold` 2-of-3, merkle root matches the output, status `unsigned` |
| Alice signs | status `partially_signed`, 1 of 2 signatures |
| Base64 PSBT handed to bob | bob signs it → `complete: true`, status `finalized` |
| `finalizepsbt` | `complete: true` |
| `testmempoolaccept` | `allowed: true`, `reject-reason: null` |
| Broadcast | txid `6fc71b6259d6f79a041e592e848e2e125940e55ad9e642e1fcdba80a2e1e0bc9` |
| Confirmed | block `0000000c9999f0c670794c843ae2716cb4a71304046c4ba9e8ee4aed23f9821e`, height 215455 |

Witness stack on the confirmed input, byte lengths in push order:

```
[0, 2421, 2421, 3960, 1]
 │   │     │     │     └─ control block, single-leaf tree, the byte 0xc1
 │   │     │     └─────── leaf script
 │   │     └───────────── alice's signature (key 0)
 │   └─────────────────── bob's signature  (key 1)
 └─────────────────────── empty slot for carol (key 2, did not sign)
```

Cross-checked against the public block explorer, which shows the same five items in the same
order and both signatures ending in the `0x01` `SIGHASH_ALL` byte. Its reported serialized
size of 8 930 bytes reconciles exactly, leaving nothing unaccounted for in the witness:

| Part | Bytes |
|------|-------|
| Base transaction (version, 1 input, 2 outputs, locktime) | 113 |
| Segwit marker and flag | 2 |
| Witness: stack count, 2×(3 + 2421) sigs, 3 + 3960 leaf, 1 + 1 control, 1 empty | 8 815 |
| **Total** | **8 930** |

The explorer also reports 665 vB and a fee of 816 satoshis, so the witness is being discounted
well below the BIP141 rate. That discount is pre-existing BTQ policy, not something this change
introduces, but it is the reason a 2-of-3 post-quantum spend is affordable at all.

This matches Appendix A: slots are pushed in reverse key order, so key 0 ends up on top,
and a non-signing key contributes an empty push rather than being omitted.

### 17.3 Second spend, via `combinepsbt`

The first spend went alice → bob sequentially. A second UTXO on the same address was spent
by the other route: alice and carol each signed the *same* unsigned PSBT without seeing
each other's work, and `combinepsbt` merged the two independent partials.

| Step | Result |
|------|--------|
| Alice's PSBT | `partially_signed`, 1 of 2 |
| Carol's PSBT (independent) | `partially_signed`, 1 of 2 |
| `combinepsbt` of the two | `finalizable`, 2 of 2 |
| Confirmed | txid `8d31a6cf96ed98967f38debccfe04bc11c5b75e1f0e4ec66b975c37175632ff5`, block `0000000ee33d81c83360dd0fc44dd727f13e72d274790474fb26a54e23209b70`, height 215469 |

Witness lengths `[2421, 0, 2421, 3960, 1]` — carol (key 2) at the bottom, the empty slot in
the middle for bob, alice (key 0) on top. The empty slot moved to match which key signed.

### 17.4 Negative cases

Run against alice's real partially signed PSBT for the second UTXO, before it was spent:

| Attempt | Result |
|---------|--------|
| `finalizepsbt` one signature short | `complete: false`, no `hex` returned |
| One byte flipped inside the signature, then `decodepsbt` | `-22 … Input 0: Dilithium partial signature does not verify` |
| Same tampered PSBT, `finalizepsbt` | same error — rejected at parse, before any signing logic runs |

The forgery is caught when the PSBT is read, not when the transaction is broadcast, so a
co-signer never spends time reviewing or signing a PSBT carrying a bad signature.

### 17.5 Accepted by the rest of the network

Both spends were mined into blocks this node produced, so on its own that only shows the
node accepted its own work. What settles it is what happened next: a different miner
(coinbase `tbtq1zlvr4nym…qse07ya`) built 215470, 215471 and 215473–215475 on top, and three
`/BTQ:0.4.2/` peers report `synced_blocks: 215475` — the same tip.

```
215469  ours    <- second spend (combinepsbt)
215470  other miner
215471  other miner
215472  ours
215473  other miner
215474  other miner
215475  other miner  <- tip, matched by 3 peers
```

The first spend sits at height 215455, 21 blocks deep. Independent nodes validated both
transactions and extended the chain containing them; had either witness been invalid, they
would have rejected the block instead.

### 17.6 What this establishes

The two signatures on each spend were produced in different wallets that shared nothing but
a base64 string. Before this change that string carried no leaf script, no control block and
no Dilithium signature, so the second wallet had nothing to sign against and the finalizer
had nothing to assemble.

No consensus code was touched. The same `VerifyP2MRCommitment` and `CHECKSIGDILITHIUM`
that already governed P2MR spends accepted these witnesses, on the public chain, as judged
by nodes that were not running this branch. The change is confined to how signing material
travels between wallets before broadcast.

**Reproducing:** `contrib/btq/testnet_dilithium_psbt_proof.sh <btq-cli invocation…>`. It
needs a synced testnet node and a `psbtproof_funder` wallet with spendable coins; the
funder here was filled by CPU-mining 60 blocks and waiting out the 100-block coinbase
maturity.

---

## Appendix A — Worked example: 2-of-3 accumulator multisig

**Leaf script (conceptual):** `OP_0` + 3×(`OP_TOALTSTACK pk OP_CHECKSIGDILITHIUM OP_FROMALTSTACK OP_ADD`) + `OP_2 OP_GREATERTHANOREQUAL`

**Before finalize (input 0):**

```
PSBT_IN_WITNESS_UTXO = { value, scriptPubKey: OP_2 <merkle_root> }
PSBT_IN_P2MR_LEAF_SCRIPT = { key: control_block, value: leaf_script || 0xc0 }
PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG:
  (pk_A, leaf_hash) → sig_A (2421 bytes)
  (pk_B, leaf_hash) → sig_B (2421 bytes)
```

**After finalize — witness stack (n=3; key 0=A, key 1=B signed; key 2=C unsigned):**

Slots are pushed in reverse key order (key 0 on top). With C unsigned:

```
stack[0] = empty   (key 2 — unsigned)
stack[1] = sig_B   (key 1)
stack[2] = sig_A   (key 0)
stack[3] = leaf_script bytes
stack[4] = control_block
```

**Size (single input, 2-of-3, 2 partial sigs) — measured:**

| Component | Bytes |
|-----------|-------|
| 2 × `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` (key 1345 + value 2421 + overhead) | ≈ 7 540 |
| `PSBT_IN_P2MR_LEAF_SCRIPT` (3 × 1312-byte pubkey + opcodes = 3960, + 1-byte control block key) | ≈ 3 990 |
| `PSBT_IN_P2MR_MERKLE_ROOT`, UTXO, unsigned tx, outputs | ≈ 250 |
| **Binary total** | **≈ 11 800** |
| **Base64** | **≈ 15 700** |

Roughly 12 KB binary, 16 KB base64 — comfortable for a file, an email attachment or a
paste, but far too large for a QR code (§13 non-goal 3).

A P2MR control block has no internal key, so for a single-leaf tree it is one byte and each
additional merkle path node adds 32 bytes. That byte is the leaf version with BIP360's
parity bit set, which is always 1 in the absence of an internal key: `0xc0 | 1 = 0xc1`
(`P2MRBuilder::GetSpendData` in `src/script/signingprovider.cpp`). Note the asymmetry — the
`PSBT_IN_P2MR_LEAF_SCRIPT` *value* ends with the bare leaf version `0xc0`, while its *key*
is the control block beginning `0xc1`.

---

## Appendix B — Test vector specification

Fixtures live in `src/test/data/dilithium_psbt/`. Each vector set includes:

| File | Content |
|------|---------|
| `README.md` | Generator command (`btq-multisig test/e2e_regtest.sh` + export) |
| `accumulator_2of3_unsigned.psbt.base64` | Creator output, 0 partial sigs |
| `accumulator_2of3_signed_a.psbt.base64` | After signer A |
| `accumulator_2of3_signed_ab.psbt.base64` | After combinepsbt with signer B |
| `accumulator_2of3_finalized.psbt.base64` | After finalize |
| `accumulator_2of3_final_tx.hex` | Extracted raw tx |
| `metadata.json` | `{ leaf_hash, merkle_root, pubkeys[], m, n, txid, vout, amounts[] }` |

**Positive vector invariants (CI asserts):**

1. `metadata.json` leaf_hash matches `ComputeTapleafHash` of embedded leaf.
2. `VerifyP2MRCommitment` passes for control block + program.
3. Final tx hex matches mining `testmempoolaccept` on regtest.
4. `accumulator_2of3_signed_ab` classifies as `FINALIZABLE`; finalized classifies as `FINALIZED`.
5. Byte length of each `PSBT_IN_P2MR_DILITHIUM_SCRIPT_SIG` value == 2421.

### Appendix B.1 — Negative test vectors (REQUIRED for Phase 0 freeze)

Each negative fixture is a `.psbt.base64` file that MUST fail `ValidatePSBTInput` (or deserialize) with the indicated error code. CI MUST assert the specific error.

| File | Mutation | Expected error |
|------|----------|----------------|
| `neg_duplicate_leaf_script_key.psbt.base64` | Same `control_block` key twice | `INVALID_PSBT` |
| `neg_duplicate_partial_sig_key.psbt.base64` | Identical `(pubkey, leaf_hash)` key twice | `INVALID_PSBT` |
| `neg_conflicting_partial_sig.psbt.base64` | Same `(pubkey, leaf_hash)`, different sig bytes | `PSBT_DILITHIUM_SIG_INVALID` |
| `neg_wrong_leaf_hash.psbt.base64` | Partial sig `leaf_hash` does not match any leaf | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_leaf_hash_tiebreak.psbt.base64` | Two leaves, same `leaf_hash`, different scripts | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_bad_commitment.psbt.base64` | Valid-looking leaf, wrong control block / Merkle path | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_bad_parity_bit.psbt.base64` | Control byte parity bit cleared | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_unsupported_sighash.psbt.base64` | Partial sig sighash byte `0x02` (NONE) | `PSBT_DILITHIUM_SIGHASH_UNSUPPORTED` |
| `neg_invalid_signature.psbt.base64` | Valid structure, sig fails `Verify` | `PSBT_DILITHIUM_SIG_INVALID` |
| `neg_unauthorized_pubkey.psbt.base64` | Sig from key not in leaf policy | `PSBT_DILITHIUM_SIG_UNAUTHORIZED` |
| `neg_metadata_mismatch.psbt.base64` | `PSBT_IN_P2MR_MERKLE_ROOT` ≠ program | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_multi_leaf_hash.psbt.base64` | Partial sigs reference two distinct leaf_hashes on one input | `PSBT_P2MR_METADATA_MISMATCH` |
| `neg_proprietary_only.psbt.base64` | Dilithium data in `"btq.org"` proprietary fields only | `INVALID_PSBT` (production reader) |

**Phase 0 gate:** Spec freeze is blocked until all positive **and** negative vectors above exist and pass CI.

Vectors regenerated when type bytes or regtest chainparams change; CI checksums `metadata.json`.

---

## Appendix C — Glossary

| Term | Meaning |
|------|---------|
| BTQ-PSBT | BIP174 + §4 Dilithium/P2MR extensions |
| Accumulator leaf | Production m-of-n script using repeated `OP_CHECKSIGDILITHIUM` + add |
| Leaf hash | `TaggedHash("TapLeaf", leaf_version \|\| compact_size(script) \|\| script)` |
| FINALIZABLE | Enough **valid** partial sigs (§5.4) to run §6.5 finalizer |
| Control block | BIP360 P2MR witness suffix proving leaf inclusion (§4.9) |
| `sig_raw` | 2420-byte Dilithium signature without sighash byte |
| `sig_with_hashtype` | 2421-byte PSBT value: `sig_raw \|\| sighash_type` |
| `DilithiumPKHash` | 20-byte `Hash160` of full 1312-byte pubkey; internal map key only |
| `PrecomputedTransactionData` | BIP341 sighash cache; requires all input amounts (§2.5.2) |
| `ScriptExecutionData` | Per-input sighash context: leaf hash, codeseparator, annex (§2.5.3) |
| `VerifyP2MRCommitment` | Merkle path verification algorithm (§4.9) |
| Valid partial sig | Passes all §5.4 checks including cryptographic verify |

---

## References

1. BIP174 — PSBT  
2. BIP370 — PSBT Version 2  
3. BIP341/342 — Taproot / Tapscript  
4. BIP371 — Taproot PSBT fields  
5. BIP360 — P2MR (`VerifyP2MRCommitment`, §4.9)  
6. `btq-multisig/src/multisig.cpp`, `finalize.cpp`, `sighash.cpp`  
7. `btq-core/MAINNET_AUDIT_MATRIX_2026-06-03.md` — C28  
8. `btq-core/doc-btq/INTERNAL_SECURITY_AUDIT_PLAN.md` — W-05
