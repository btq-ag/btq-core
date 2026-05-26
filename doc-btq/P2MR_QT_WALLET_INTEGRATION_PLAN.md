# P2MR Qt Wallet Integration Plan

**Classification:** Internal engineering plan  
**Document type:** Implementation roadmap (not a specification of final UI copy)  
**Version:** 1.1  
**Status:** Phases 0 to 3 implemented (pending build verification)  
**Related:** BIP360 P2MR (Pay-to-Merkle-Root), `README_P2MR_CLI_E2E.md`, `src/wallet/rpc/p2mr.cpp`, `src/wallet/p2mr.{h,cpp}`, `src/qt/p2mr*.{h,cpp}`

---

## 0. Document control

| Field | Value |
|-------|-------|
| **Owner** | Wallet and GUI maintainers (Bitcoin Quantum) |
| **Depends on** | Wallet RPC and metadata layer in `p2mr.cpp` (landed) |
| **Consumers** | `src/qt/`, `src/interfaces/`, QA, technical writers |
| **Out of scope** | Consensus or interpreter changes (assumed complete per `feature_p2mr.py`) |

### 0.1 Implementation status snapshot

| Phase | Status | Notes |
|-------|--------|-------|
| P0 Foundations | Done | `wallet/p2mr.{h,cpp}` shared module, `interfaces::Wallet` API, balance helpers (no IsMine change to preserve coin selection safety), `WalletModel::getP2MRController()` |
| P1 Vault manager | Done | `P2MRVaultDialog`, `P2MRNewVaultDialog`, `P2MRSpendDialog`, `P2MRDetailsDialog`; Window menu entry; sign-test-broadcast flow with unlock and confirmation |
| P2 Receive integration | Done | "P2MR (BIP360, advanced)" sentinel item in Receive dropdown opens the vault dialog |
| P3 History annotations | Done | Transaction details popup shows "P2MR input"/"P2MR output" lines when scripts match tracked vaults |
| Tests | Done | `wallet/test/p2mr_tests.cpp` unit tests for tree parse and build; functional `feature_p2mr_rpc.py` unchanged and continues to exercise the RPC layer which now delegates to the shared module |
| P4 Advanced | Backlog | Tree visualizer, hardware wallet, PSBT, metadata export |

---

## 1. Executive summary

BTQ has implemented **BIP360 P2MR** at the consensus, script, and wallet-RPC layers. Users can complete the full receive, fund, spend, sign, and broadcast cycle today using **`btq-cli`** (documented in `README_P2MR_CLI_E2E.md`). The **Qt wallet (`btq-qt`)** does not yet expose P2MR: it generates addresses only through `OutputType` (legacy, P2SH-SegWit, Bech32, Bech32m/Taproot) and spends through the standard `WalletModel::prepareTransaction` / `CreateTransaction` path.

This plan describes how to integrate P2MR into the Qt wallet in phases, with explicit attention to:

1. **Architecture:** Qt uses `interfaces::Wallet`, not JSON-RPC, when embedded with `btqd`. P2MR logic must be lifted from RPC handlers into shared wallet APIs before the GUI can call it cleanly.
2. **Accounting gap:** `WitnessV2P2MR` outputs are not treated as `IsMine` in `LegacyScriptPubKeyMan` today. P2MR UTXOs are located by `scriptPubKey` equality against address-book destinations in `createp2mrspend`. The GUI balance and coin list may not reflect P2MR-held funds until wallet ownership semantics are extended.
3. **UX complexity:** P2MR requires a **script tree** (DFS-ordered leaves: `depth`, `leaf_version`, `script` hex), not a single key. The GUI must offer safe defaults and advanced editing, not raw JSON by default.

---

## 2. Current implementation inventory

### 2.1 Protocol and consensus (reference only)

| Component | Location | Role |
|-----------|----------|------|
| Output type | `TxoutType::WITNESS_V2_P2MR` | `solver.cpp`, witness v2 program 32 bytes |
| Destination | `WitnessV2P2MR` | `addresstype.h` |
| Script builder | `P2MRBuilder` | `signingprovider.h` / `signingprovider.cpp` |
| Validation | `interpreter.cpp` | BIP360 merkle root and script path rules |
| Dilithium in P2MR tapscript | `feature_p2mr.py` | Allowed in P2MR; blocked in P2TR |

**On-chain format:** `scriptPubKey` is 34 bytes: `OP_2` + 32-byte merkle root (same size as P2TR; different semantics, no internal key).

**Address encoding:** Bech32m with witness version **2** (`key_io.cpp`, comment notes `bc1z`-style prefix under BTQ HRP `qbtc` on mainnet).

### 2.2 Wallet backend (implemented)

| RPC | Purpose |
|-----|---------|
| `getnewp2mraddress` | Build tree, store metadata + address book, return `address` and `p2mr_id` |
| `sendtop2mr` | Same as above, plus fund with `CreateTransaction` |
| `listp2mr` | List all P2MR metadata entries |
| `getp2mrinfo` | Lookup by `p2mr_id` |
| `createp2mrspend` | Unsigned tx spending one P2MR UTXO (by metadata id) |
| `signp2mrtransaction` | Sign/finalize P2MR inputs using stored tree |
| `testp2mrtransaction` | Mempool accept dry-run (`broadcastTransaction` relay=false) |

**Metadata storage:** JSON blob in address receive-request slot with prefix `rrp2mr:` (`wallet.cpp`). Fields include `id`, `address`, `scriptPubKey`, `merkle_root`, `tree`, `label`, `state`, `created_at`.

**Signing:** `BuildP2MRProviderFromMetadata` populates `FlatSigningProvider::p2mr_trees` for `SignSignature` / `SignP2MR`.

**Tests:**

- `test/functional/feature_p2mr.py` (consensus-level, 15 test groups)
- `test/functional/feature_p2mr_rpc.py` (descriptor wallet RPC flow)
- `run_p2mr_rpc_e2e.sh`, `README_P2MR_CLI_E2E.md`

### 2.3 Qt wallet (not implemented)

| Area | Current behavior | P2MR gap |
|------|------------------|----------|
| Receive tab | `OutputType` dropdown only | No P2MR tree creation |
| Address table | `getNewDestination(OutputType)` | No P2MR entries with `p2mr_id` |
| Send tab | `CreateTransaction` to decoded address | Cannot sign spends from P2MR UTXOs automatically |
| Transaction list | Standard `WalletTx` display | P2MR receive outputs may not show as wallet balance |
| Coin control | `CCoinControl` | No P2MR-aware coin selection |
| `interfaces::Wallet` | No P2MR methods | GUI cannot call wallet layer without new API |

---

## 3. Goals and non-goals

### 3.1 Goals

| ID | Goal |
|----|------|
| G1 | Create and label P2MR receive addresses from the GUI with vetted templates |
| G2 | Fund P2MR outputs (optional shortcut: receive + manual send) |
| G3 | Spend from P2MR to arbitrary BTQ addresses with fee control |
| G4 | List, inspect, and manage P2MR vaults (`p2mr_id`, tree, merkle root, state) |
| G5 | Show P2MR-related transactions and balances accurately in the main window |
| G6 | Preserve parity with CLI RPC behavior for the same wallet file |

### 3.2 Non-goals (initial releases)

| ID | Non-goal |
|----|----------|
| NG1 | Visual merkle tree editor for arbitrary depth (phase 4+ optional) |
| NG2 | Hardware wallet signing for P2MR (until device support exists) |
| NG3 | Full descriptor-wallet `tr()` integration in Qt (descriptor logic may stay internal) |
| NG4 | Replacing Taproot with P2MR in the default receive dropdown |
| NG5 | Multi-party MuSig / FROST workflows in Qt |

---

## 4. Architecture decision: how Qt should call P2MR

### 4.1 Recommended approach: shared C++ wallet module + `interfaces::Wallet`

Bitcoin Qt does not use JSON-RPC against itself. Follow the same pattern:

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  btq-qt UI  │────▶│ interfaces::Wallet│────▶│ CWallet + p2mr  │
│  (dialogs)  │     │  (new methods)    │     │  helpers        │
└─────────────┘     └──────────────────┘     └─────────────────┘
                              ▲
                              │
                     ┌────────┴────────┐
                     │ wallet/rpc/     │
                     │ p2mr.cpp        │
                     └─────────────────┘
```

**Steps:**

1. Extract from `src/wallet/rpc/p2mr.cpp` into `src/wallet/p2mr.h` / `p2mr.cpp`:
   - `ParseP2MRTree` (from `UniValue` and from a Qt-friendly struct)
   - `BuildP2MRTree`, `CreateAndStoreP2MR`
   - `ListP2MR`, `GetP2MRById`
   - `CreateP2MRSpend`, `SignP2MRTransaction`, `TestP2MRTransaction`
2. Keep RPC handlers as thin wrappers around these functions.
3. Add `interfaces::Wallet` virtual methods and implement in `src/wallet/interfaces.cpp`.
4. Add `WalletModel` wrappers that emit Qt signals and translate errors to `QString`.

### 4.2 Alternative (not recommended): Qt calls JSON-RPC only

Possible for a remote-wallet mode, but inconsistent with embedded `btqd`, duplicates auth, and complicates multi-wallet UI. Use only if a hard requirement appears for GUI-without-node builds.

### 4.3 Prerequisite: wallet balance and `IsMine` for P2MR

**Problem:** `IsMineInner` returns `NO` for `WITNESS_V2_P2MR` (`scriptpubkeyman.cpp`). Address book entries exist, but `GetAddressBalances` and coin selection skip outputs where `!IsMine(output)`.

**Required wallet work (before or in parallel with Phase 1 UI):**

| Option | Description | Recommendation |
|--------|-------------|----------------|
| A | Treat address-book RECEIVE entries with `WitnessV2P2MR` dest as spendable for accounting | Preferred for GUI and `listunspent` parity |
| B | Separate `IsMineP2MR(scriptPubKey)` used only in balance and coin lists | More invasive, clearer separation |
| C | Leave as-is; GUI shows P2MR only in dedicated vault view | Insufficient for G5 |

**Acceptance test:** After `sendtop2mr`, main window balance includes locked P2MR funds (or a clearly labeled sub-balance), and `listunspent` reports the output when using the same wallet as Qt.

---

## 5. Data model for the GUI

### 5.1 `P2MRTreeLeaf` (C++ / Qt)

```cpp
struct P2MRTreeLeaf {
    uint8_t depth;
    uint8_t leaf_version;  // default: TAPROOT_LEAF_TAPSCRIPT (0xc0)
    std::vector<unsigned char> script;
};
```

### 5.2 `P2MRWalletEntry` (view model)

| Field | Source | UI use |
|-------|--------|--------|
| `p2mr_id` | metadata `id` | Internal key, spend dialog |
| `address` | metadata `address` | Display, QR, copy |
| `label` | metadata `label` | Address book |
| `merkle_root` | metadata | Advanced info, export |
| `tree` | metadata `tree` | Read-only inspect; editor in advanced mode |
| `state` | metadata `state` | Badge (created, funded, spent) |
| `created_at` | metadata | Sorting |
| `balance` | wallet scan for matching `scriptPubKey` | Vault list |
| `spendable` | confirmed, unspent UTXO exists | Enable spend button |

### 5.3 Tree templates (product-defined)

Ship built-in templates to avoid asking users for hex scripts on day one:

| Template ID | Tree | Use case |
|-------------|------|----------|
| `op_true` | depth 0, leaf_version 192, script `51` (OP_TRUE) | Testing, CLI parity, E2E docs |
| `single_dilithium` | One leaf with `OP_DILITHIUM_PUBKEY` + user key | PQ single-sig path (when keys in wallet) |
| `delay_nop` | OP_CHECKSEQUENCEVERIFY style leaf (future) | Time-locked vault |
| `custom` | Advanced editor | Power users |

Default for Receive flow: **`op_true`** with strong warning copy that it is for testing unless user selects PQ template.

---

## 6. UI/UX design (phased)

### 6.1 Phase 1: P2MR manager (MVP)

**New menu:** Wallet → **P2MR Vaults…** (or Receive tab sub-tab **P2MR**)

**List view columns:** Label, Address (truncated), Balance, State, Created

**Actions:**

| Action | Maps to |
|--------|---------|
| New vault… | `getnewp2mraddress` |
| Fund vault… | `sendtop2mr` |
| Spend from vault… | Wizard: `createp2mrspend` → `signp2mrtransaction` → `testp2mrtransaction` → `sendrawtransaction` |
| Copy address | Clipboard |
| Show details | Tree leaves, merkle root, `p2mr_id` |
| Export metadata | JSON file backup (user security responsibility) |

**New vault dialog:**

1. Label (optional)
2. Template dropdown (`OP_TRUE`, Custom…)
3. Custom: table editor (depth, version, script hex) with validation before OK
4. On success: show address + `p2mr_id` + optional QR (reuse `ReceiveRequestDialog` patterns)

**Spend wizard (modal):**

1. Select vault (or pre-filled from list)
2. Destination address (`validateAddress`)
3. Amount + fee (default fee from wallet; advanced: fixed fee like RPC default `0.00001`)
4. Preview unsigned txid / vsize
5. Sign (requires unlocked wallet)
6. Show `testp2mrtransaction` result; block broadcast if `allowed == false`
7. Confirm broadcast → `sendrawtransaction` via `interfaces::Wallet` or existing raw tx path

### 6.2 Phase 2: Receive tab integration

Add address type **P2MR (BIP360)** to `ReceiveCoinsDialog` dropdown (alongside Bech32m Taproot):

- Same template picker as Phase 1
- Generates address via shared API (not `OutputType::BECH32M`)
- Stores in recent requests with type flag so URI generation does not claim incompatible BIP21 fields

**Address table:** New row type or icon for P2MR receive entries; `AddressTableModel` may need a column or delegate for “Type = P2MR”.

### 6.3 Phase 3: Transaction and balance surfacing

| Surface | Change |
|---------|--------|
| Overview balance | Include P2MR subtotal or single total after IsMine fix |
| Transactions tab | Decode `WITNESS_V2_P2MR` outputs; link to vault by `scriptPubKey` |
| Transaction details | Show “P2MR vault” label and merkle root (truncated) |
| Coin control | Optional: filter “P2MR only” for manual consolidation (advanced) |

### 6.4 Phase 4: Advanced features (optional)

- Import/export P2MR metadata JSON for wallet migration
- Multi-leaf tree builder with validation preview (DFS order checker)
- Dilithium leaf helper: pick wallet Dilithium key, generate script hex
- PSBT export for external signers (if PSBT support extended for P2MR)

---

## 7. Qt code touchpoints (file-level plan)

| File / area | Changes |
|-------------|---------|
| **New** `src/wallet/p2mr.h`, `p2mr.cpp` | Shared logic extracted from RPC |
| `src/wallet/interfaces.h`, `interfaces.cpp` | Virtual methods: `listP2MR`, `createP2MR`, `fundP2MR`, `createP2MRSpend`, `signP2MR`, `testP2MR` |
| `src/wallet/wallet.cpp` | `IsMine` / balance for address-book P2MR (prerequisite) |
| `src/qt/walletmodel.h`, `walletmodel.cpp` | QObject API, error enums, unlock context |
| **New** `src/qt/p2mrvaultdialog.h/.cpp`, `.ui` | Manager + wizards |
| `src/qt/walletview.cpp` | Add action to open P2MR manager |
| `src/qt/bitcoingui.cpp` | Menu entry |
| `src/qt/receivecoinsdialog.cpp` | Optional P2MR address type (Phase 2) |
| `src/qt/addresstablemodel.cpp` | Display P2MR addresses from `listp2mr` merge |
| `src/qt/transactionrecord.cpp`, `transactiondesc.cpp` | Human-readable P2MR type |
| `src/qt/guiutil.cpp` | Address validation already uses `DecodeDestination` (supports v2) |
| `src/qt/locale/btq_en.ts` | All new strings |
| `src/Makefile.am`, `src/qt/btq-qt.pro` | Build new sources |

**Do not** call RPC from GUI threads: use `interfaces::Wallet` on wallet thread or `WalletModel` async patterns consistent with existing send flow.

---

## 8. Implementation phases and milestones

### Phase 0: Foundations (1 to 2 weeks)

| Task | Owner hint | Done when |
|------|------------|-----------|
| Extract `wallet/p2mr` module from RPC | Wallet | RPC tests still pass |
| Fix or document P2MR `IsMine` / balance | Wallet | `feature_p2mr_rpc.py` + manual balance check |
| Add `interfaces::Wallet` P2MR API | Wallet + GUI | Unit test via `test_btq` or interface mock |
| Define `P2MREntry` struct shared GUI/wallet | Wallet | Header stable |

**Milestone M0:** CLI and GUI share one code path; balance includes P2MR UTXOs.

### Phase 1: P2MR Vault manager MVP (2 to 3 weeks)

| Task | Done when |
|------|-----------|
| Vault list dialog wired to `listp2mr` | List matches `btq-cli listp2mr` |
| New / Fund / Spend wizards | E2E matches `README_P2MR_CLI_E2E.md` without CLI |
| Error handling: locked wallet, insufficient funds, unknown id | User-visible `QMessageBox` |
| Sign + test + broadcast pipeline | Spend confirms in regtest |

**Milestone M1:** Internal QA can demo full flow on regtest from Qt only.

### Phase 2: Receive tab + address book (1 to 2 weeks)

| Task | Done when |
|------|-----------|
| P2MR in receive dropdown | Address matches `getnewp2mraddress` |
| Recent requests + QR | URI policy documented |
| Address table shows P2MR rows | Label edit syncs to wallet |

**Milestone M2:** New users discover P2MR without opening Vault manager first.

### Phase 3: History and polish (1 to 2 weeks)

| Task | Done when |
|------|-----------|
| Transaction list annotations | P2MR sends/receives identifiable |
| Balance breakdown (optional) | Settings toggle |
| Help links to docs | `doc-btq` or user guide |

**Milestone M3:** Release notes ready for testnet.

### Phase 4: Advanced (backlog)

Templates for Dilithium, metadata export, tree editor, PSBT.

---

## 9. Testing plan

### 9.1 Automated

| Layer | Test | Notes |
|-------|------|-------|
| Unit | `wallet/p2mr` tree parse/build | Invalid DFS, bad hex, empty tree |
| Unit | `IsMine` / balance with P2MR dest | After prerequisite |
| Functional | Extend `feature_p2mr_rpc.py` | Optional: tag for GUI-driven RPC parity |
| Qt | `src/qt/test/wallettests.cpp` | Add P2MR regtest scenario if feasible |
| Lint | `test/lint` | Include new `.ui` and headers |

**Suggested new Qt test (pseudo-flow):**

1. Create wallet on regtest  
2. Mine coins  
3. Call `WalletModel::createP2MRVault(OP_TRUE template)`  
4. Fund, spend, assert confirmation count via model

If full Qt test is too heavy, maintain **`test/functional/qt_p2mr_stub.py`** that only documents manual steps until GUI test harness exists.

### 9.2 Manual QA checklist (release gate)

| # | Step | Expected |
|---|------|----------|
| 1 | Create OP_TRUE vault | Address decodes as `WitnessV2P2MR`, `listp2mr` entry |
| 2 | Fund 1 BTQ | Tx visible; balance updated per M0 |
| 3 | Spend 0.5 BTQ to Bech32 address | `complete: true`, mempool accept, 1 conf |
| 4 | Locked wallet spend attempt | Clear unlock prompt |
| 5 | Invalid tree in custom editor | OK disabled; RPC error not reachable |
| 6 | Wrong `p2mr_id` spend | Error message, no crash |
| 7 | Encrypted wallet restart | Metadata persists; spend still works |
| 8 | Compare CLI vs Qt same wallet file | Same `listp2mr` JSON |
| 9 | Mainnet disabled templates warning | Copy shown if policy requires testnet-only |
| 10 | Dilithium template (when added) | Spend path matches `feature_p2mr.py` expectations |

### 9.3 Security-focused tests

| Risk | Test |
|------|------|
| Broadcast without `testp2mrtransaction` | UI must not skip dry-run by default |
| Tree substitution | Spend uses metadata for selected `p2mr_id` only |
| Leak merkle root in logs | Debug log redaction review |
| User exports JSON metadata | Document physical backup risks |

---

## 10. Success criteria

Integration is **complete for a given release** when all of the following hold:

| ID | Criterion |
|----|-----------|
| SC-1 | All Phase 1 milestones met on **regtest** and **testnet** |
| SC-2 | No open **Critical** or **High** GUI bugs in P2MR flows (see severity table below) |
| SC-3 | Parity: operations available in CLI (`getnewp2mraddress` through broadcast) are available in Qt without RPC hacks |
| SC-4 | `feature_p2mr_rpc.py` and Qt manual checklist both pass on release candidate build |
| SC-5 | Documentation updated: user guide section + release notes |
| SC-6 | Strings translatable; no hard-coded English-only in production dialogs without `tr()` |
| SC-7 | Wallet file backward compatible: pre-P2MR wallets open; P2MR metadata survives restart |

**Performance:** Vault list refresh &lt; 500 ms for 100 entries on reference hardware; spend wizard sign step shows progress for txs &gt; 100 ms.

---

## 11. Severity definitions (GUI / integration defects)

Use these when triaging Qt P2MR bugs (aligned with `doc-btq/SECURITY.md` where applicable).

### 11.1 Critical

- Loss of funds (wrong recipient, silent fee drain, spend to unspendable script without warning)  
- Broadcast of invalid P2MR spend that should have been blocked  
- Wallet corruption or metadata loss on normal quit/restart  
- Crash on default path with no custom tree  

### 11.2 High

- Balance materially wrong (P2MR funds missing from total)  
- Spend appears to succeed but tx not in mempool and UI shows confirmed  
- Unable to unlock or sign valid vault with correct tree  
- P2MR receive address copied wrong from UI  

### 11.3 Medium

- Incorrect labels in transaction list (type misidentified)  
- Template validation error messages unclear  
- Vault list not refreshed after external CLI operation on same wallet  
- Minor visual defects on HiDPI  

### 11.4 Low

- Typos, tooltip wording, column width preferences  
- Missing keyboard shortcut  
- Non-blocking log spam  

### 11.5 Informational

- Feature requests (tree visualizer, hardware wallet)  
- Copy improvements  
- Additional templates  

---

## 12. Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| `IsMine` not fixed | Wrong balance, user confusion | Phase 0 blocker |
| Users build unspendable trees | Locked funds | Template-first UX; expert mode requires confirmation |
| Confusion between Taproot and P2MR | Wrong address type | Clear naming “P2MR (BIP360, no key path)” |
| Large witness txs | High fees | Show vsize/fee estimate in spend wizard |
| Descriptor vs legacy wallet | RPC tests use descriptors | Document supported wallet modes in Qt help |
| Concurrent CLI + Qt on same wallet | Race on metadata | Document “single writer”; optional wallet lock messaging |

---

## 13. Documentation deliverables

| Document | Action |
|----------|--------|
| `README_P2MR_CLI_E2E.md` | Add “Qt equivalent” section after Phase 1 |
| `doc-btq/testing/TESTING_GUIDE.md` | Add Qt P2MR manual checklist reference |
| `doc-btq/README.md` | Link to this plan and user guide |
| In-app Help | Short topic: what P2MR is, difference from Taproot |
| Release notes | Call out experimental/beta flag if needed |

---

## 14. Open questions (resolve in design review)

1. Should P2MR be **testnet/regtest-only** in Qt until Phase 3 mainnet sign-off?  
2. Default fee model: wallet `estimatesmartfee` vs RPC fixed fee in `createp2mrspend`?  
3. Merge vault list into main **Addresses** page or separate dialog only?  
4. Support **watch-only** P2MR metadata import?  
5. Minimum wallet type: descriptor-only (as RPC tests) or also legacy SQLite wallets?

---

## 15. Appendix A: RPC to GUI method mapping

| RPC | Proposed `interfaces::Wallet` method |
|-----|--------------------------------------|
| `getnewp2mraddress` | `util::Result<P2MRCreated> createP2MR(std::vector<P2MRTreeLeaf>, label)` |
| `sendtop2mr` | `util::Result<P2MRFunded> fundP2MR(..., amount, coin_control)` |
| `listp2mr` | `std::vector<P2MREntry> listP2MR()` |
| `getp2mrinfo` | `std::optional<P2MREntry> getP2MR(std::string id)` |
| `createp2mrspend` | `util::Result<P2MRSpend> createP2MRSpend(id, dest, amount, fee)` |
| `signp2mrtransaction` | `util::Result<SignedP2MR> signP2MR(hex, optional_id)` |
| `testp2mrtransaction` | `util::Result<MempoolAccept> testP2MR(hex)` |

Broadcast can reuse existing `broadcastTx` / `sendrawtransaction` wrapper.

---

## 16. Appendix B: Reference CLI sequence (parity target)

From `README_P2MR_CLI_E2E.md` (regtest):

```bash
TREE='[{"depth":0,"leaf_version":192,"script":"51"}]'
CREATED=$(./btq-cli ... getnewp2mraddress "$TREE")
FUNDED=$(./btq-cli ... sendtop2mr "$TREE" 1.0 "demo")
UNSIGNED=$(./btq-cli ... createp2mrspend "$FUND_ID" "$DEST" 0.5)
SIGNED=$(./btq-cli ... signp2mrtransaction "$UNSIGNED_HEX" "$FUND_ID")
./btq-cli ... testp2mrtransaction "$SIGNED_HEX"
./btq-cli ... sendrawtransaction "$SIGNED_HEX"
```

Qt Phase 1 must reproduce this sequence without requiring the user to paste hex into a debug console.

---

## 17. Document approval

| Role | Name | Date |
|------|------|------|
| Wallet lead | | |
| GUI lead | | |
| Protocol / BIP360 owner | | |
| QA lead | | |

---

*End of P2MR Qt Wallet Integration Plan*
