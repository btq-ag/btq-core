# BTQ Block Timing Implementation

**Implementation Date:** January 2026  
**Purpose:** Complete the 1-minute block timing across all networks for UTXO consolidation optimization

---

## Overview

This document describes the changes made to complete the block timing optimization described in `UTXO_CONSOLIDATION_OPTIMIZATION.md`. The optimization was partially implemented (Mainnet and Testnet), and these changes complete the implementation for Signet and Regtest networks.

---

## Changes Made

### 1. Signet Network (`SigNetParams`)

**File:** `src/kernel/chainparams.cpp` (lines ~355-365)

| Parameter | Before | After |
|-----------|--------|-------|
| `nPowTargetTimespan` | 14 days (2 weeks) | **7 days (1 week)** |
| `nPowTargetSpacing` | 600 seconds (10 min) | **60 seconds (1 min)** |
| `nMinerConfirmationWindow` | 2,016 blocks | **10,080 blocks** |
| `nRuleChangeActivationThreshold` | 1,815 (90% of 2016) | **9,072 (90% of 10080)** |

**Code change:**
```cpp
// BEFORE:
consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
consensus.nPowTargetSpacing = 10 * 60;
consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
consensus.nMinerConfirmationWindow = 2016;

// AFTER:
consensus.nPowTargetTimespan = 7 * 24 * 60 * 60; // one week (reduced for 1-min blocks)
consensus.nPowTargetSpacing = 1 * 60;            // 1 minute blocks (was 10 minutes)
consensus.nRuleChangeActivationThreshold = 9072; // 90% of 10080
consensus.nMinerConfirmationWindow = 10080;      // nPowTargetTimespan / nPowTargetSpacing
```

---

### 2. Regtest Network (`CRegTestParams`)

**File:** `src/kernel/chainparams.cpp` (lines ~450-460)

| Parameter | Before | After |
|-----------|--------|-------|
| `nPowTargetSpacing` | 600 seconds (10 min) | **60 seconds (1 min)** |
| `nMinerConfirmationWindow` | 144 blocks | 144 blocks (unchanged) |
| `nRuleChangeActivationThreshold` | 108 (75%) | 108 (unchanged) |

**Note:** Regtest keeps its smaller confirmation window (144 blocks) for faster test cycles, but now uses consistent 1-minute block timing.

**Code change:**
```cpp
// BEFORE:
consensus.nPowTargetSpacing = 10 * 60;
consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

// AFTER:
consensus.nPowTargetSpacing = 1 * 60;             // 1 minute blocks for consistency
consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 10080)
```

---

## Unchanged Parameters

The following parameters were intentionally NOT changed:

| Parameter | Value | Location | Reason |
|-----------|-------|----------|--------|
| `COINBASE_MATURITY` | 100 blocks | `src/consensus/consensus.h` | Remains at 100 blocks (~1.67 hours with 1-min blocks) |
| `nPowTargetTimespan` (Regtest) | 14 days | `chainparams.cpp` | Irrelevant since `fPowNoRetargeting = true` |

---

## Final Network Configuration

All networks now use consistent 1-minute block times:

| Network | Block Time | Confirmation Window | Difficulty Retarget |
|---------|-----------|---------------------|---------------------|
| **Mainnet** | 1 minute | 10,080 blocks (~1 week) | Every ~1 week |
| **Testnet** | 1 minute | 10,080 blocks (~1 week) | Every ~1 week |
| **Signet** | 1 minute | 10,080 blocks (~1 week) | Every ~1 week |
| **Regtest** | 1 minute | 144 blocks (~2.4 hours) | Never (fixed difficulty) |

---

## Impact

### Benefits
- **10x more blocks per day** (1,440 vs 144)
- **Faster confirmations** for all transaction types
- **Better UTXO consolidation** for mining pools with large Dilithium signatures
- **Consistent behavior** across all networks for easier testing

### Coinbase Maturity
With 1-minute blocks and 100-block maturity:
- Miners wait ~100 minutes (~1.67 hours) before spending coinbase rewards
- This is faster than Bitcoin's ~16.7 hours but appropriate for BTQ's use case

---

## Files Modified

```
src/kernel/chainparams.cpp
  - SigNetParams: Updated nPowTargetTimespan, nPowTargetSpacing, 
                  nRuleChangeActivationThreshold, nMinerConfirmationWindow
  - CRegTestParams: Updated nPowTargetSpacing
```

---

## Related Documentation

- `UTXO_CONSOLIDATION_OPTIMIZATION.md` - Full optimization design and rationale
- `src/policy/policy.h` - Transaction and block size limits (already implemented)

