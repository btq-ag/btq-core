# BTQ UTXO Consolidation Optimization

**Implementation Date:** January 2026  
**Purpose:** Improve transaction throughput and UTXO consolidation efficiency for post-quantum Dilithium signatures

---

## Overview

BTQ uses CRYSTALS-Dilithium post-quantum signatures which are significantly larger than traditional ECDSA signatures. This creates challenges for UTXO consolidation operations, particularly for mining pools that need to consolidate many small coinbase outputs into larger UTXOs for payouts.

### The Problem

Each Dilithium transaction input requires approximately **3,772 bytes**:
- Base input data: ~40 bytes
- Dilithium signature: 2,420 bytes
- Dilithium public key: 1,312 bytes

This means a consolidation transaction combining 100 UTXOs would be ~377 KB, compared to ~10 KB with ECDSA signatures.

### The Solution

We've implemented optimizations that increase consolidation throughput by **10x** through:
1. **Faster block times** - More block space per unit time
2. **Larger transaction limits** - More UTXOs per consolidation transaction
3. **Larger block capacity** - More room for consolidation alongside user transactions

---

## Implemented Changes

### 1. Block Timing (10x More Throughput)

| Parameter | Before | After |
|-----------|--------|-------|
| `nPowTargetSpacing` | 600 seconds (10 min) | **60 seconds (1 min)** |
| `nPowTargetTimespan` | 2 weeks | **1 week** |
| `nMinerConfirmationWindow` | 2,016 blocks | **10,080 blocks** |
| `nRuleChangeActivationThreshold` | 1,815 (90%) | **9,072 (90%)** |

**Files Modified:** `src/kernel/chainparams.cpp`

**Impact:**
- 10x more blocks per day (144 → 1,440)
- 10x more block space available for consolidation transactions
- Faster confirmations for all transactions
- Difficulty still adjusts smoothly (every ~1 week instead of ~2 weeks)

### 2. Maximum Standard Transaction Weight (4x Larger Consolidations)

| Parameter | Before | After |
|-----------|--------|-------|
| `MAX_STANDARD_TX_WEIGHT` | 1,000,000 (1 MB) | **4,000,000 (4 MB)** |

**File Modified:** `src/policy/policy.h`

**Impact:**
- Each consolidation transaction can now include ~1,052 inputs (vs ~263 before)
- 4x fewer consolidation transactions needed for the same number of UTXOs
- Directly addresses the large Dilithium signature overhead

### 3. Default Block Max Weight (62% More Capacity)

| Parameter | Before | After |
|-----------|--------|-------|
| `DEFAULT_BLOCK_MAX_WEIGHT` | 32,000,000 (32 MB) | **52,000,000 (52 MB)** |

**File Modified:** `src/policy/policy.h`

**Impact:**
- Miners can create larger blocks by default
- More room for consolidation transactions alongside user transactions
- Still well under the 64 MB hard cap for safety margin

### 4. Ancestor/Descendant Limits (2x More Flexibility)

| Parameter | Before | After |
|-----------|--------|-------|
| `DEFAULT_ANCESTOR_LIMIT` | 25 | **50** |
| `DEFAULT_ANCESTOR_SIZE_LIMIT_KVB` | 5,000 KB | **10,000 KB** |
| `DEFAULT_DESCENDANT_LIMIT` | 25 | **50** |
| `DEFAULT_DESCENDANT_SIZE_LIMIT_KVB` | 5,000 KB | **10,000 KB** |

**File Modified:** `src/policy/policy.h`

**Impact:**
- More flexible transaction chains in mempool
- Better support for CPFP (Child Pays For Parent)
- Accommodates larger Dilithium transaction sizes

---

## Performance Comparison

### Before Optimization

| Metric | Value |
|--------|-------|
| Block Time | 10 minutes |
| Blocks per Day | 144 |
| Effective TPS (simple tx) | ~14 |
| Max Inputs per Consolidation | ~263 |
| Block Space per Day | 4.6 GB |

### After Optimization

| Metric | Value | Improvement |
|--------|-------|-------------|
| Block Time | 1 minute | 10x faster |
| Blocks per Day | 1,440 | 10x more |
| Effective TPS (simple tx) | ~139 | 10x higher |
| Max Inputs per Consolidation | ~1,052 | 4x more |
| Block Space per Day | 74.9 GB | 16x more |

### Mining Pool Consolidation Example

**Scenario:** Pool needs to consolidate 10,000 UTXOs

| Metric | Before | After |
|--------|--------|-------|
| Consolidation Txs Needed | 38 txs | **10 txs** |
| Block Space Required | 38 MB | **40 MB** |
| Blocks Required | 2 blocks | **1 block** |
| Time to Complete | ~20 min | **~1 min** |

---

## Networks Affected

All networks use consistent 1-minute block times:

| Network | Block Time | Confirmation Window | Notes |
|---------|-----------|---------------------|-------|
| **Mainnet** | 1 minute | 10,080 blocks (~1 week) | Production network |
| **Testnet** | 1 minute | 10,080 blocks (~1 week) | Public test network |
| **Signet** | 1 minute | 10,080 blocks (~1 week) | Signed test network |
| **Regtest** | 1 minute | 144 blocks (fast testing) | Local development |

*Note: Regtest keeps a smaller confirmation window (144 blocks) for faster test cycles, but uses the same block timing for consistency.*

---

