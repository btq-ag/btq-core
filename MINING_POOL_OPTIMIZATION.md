# BTQ Mining Pool UTXO Consolidation Optimization

**Implementation Date:** January 2026  
**Target Audience:** Mining pool operators, node operators, developers

---

## The Problem: Large Post-Quantum Signatures

BTQ uses CRYSTALS-Dilithium post-quantum signatures to protect against future quantum computing attacks. However, these signatures are significantly larger than traditional Bitcoin ECDSA signatures:

| Component | Bitcoin (ECDSA) | BTQ (Dilithium) | Size Increase |
|-----------|-----------------|-----------------|---------------|
| Signature | ~71 bytes | 2,420 bytes | **34x larger** |
| Public Key | 33 bytes | 1,312 bytes | **40x larger** |
| **Per Input Total** | ~104 bytes | ~3,772 bytes | **36x larger** |

### Why This Matters for Mining Pools

Mining pools face a unique challenge: they accumulate many small coinbase UTXOs (one per block mined) that must be consolidated before paying out miners. With Dilithium signatures:

**Example: Consolidating 1,000 UTXOs**
- Each input requires ~3,772 bytes (signature + public key + base data)
- Total transaction size: **~3.77 MB** for a single consolidation transaction
- Compare to Bitcoin: ~104 KB for the same consolidation

Without optimization, a mining pool would face:
- Transactions too large to broadcast (exceeded old 1 MB limit)
- Slow consolidation competing with user transactions for limited block space
- Delayed miner payouts while waiting for consolidation

---

## The Solution: Chain Parameter Optimization

We implemented a coordinated set of optimizations that work together to solve the UTXO consolidation bottleneck.

### 1. Faster Block Times (10x More Throughput)

**Change:** Block time reduced from 10 minutes to 1 minute

| Parameter | Before | After |
|-----------|--------|-------|
| `nPowTargetSpacing` | 600 seconds | **60 seconds** |
| `nPowTargetTimespan` | 2 weeks | **1 week** |
| `nMinerConfirmationWindow` | 2,016 blocks | **10,080 blocks** |

**Why This Helps Mining Pools:**

1. **10x more block space per day** — Instead of 144 blocks/day, we now have 1,440 blocks/day. This means 10x more opportunities to include consolidation transactions without competing as heavily with user transactions.

2. **Spread consolidation across more blocks** — A pool can include one consolidation transaction per block rather than trying to fit multiple large consolidations into fewer blocks.

3. **Faster confirmations** — Miner payouts confirm in ~6 minutes (6 blocks) instead of ~60 minutes.

4. **Reduced payout variance** — More frequent blocks mean more predictable income for miners.

**Block Space Comparison:**
```
Before: 144 blocks/day × 32 MB = 4.6 GB/day
After:  1,440 blocks/day × 52 MB = 74.9 GB/day (16x more capacity)
```

---

### 2. Larger Transaction Limit (4x More Inputs Per Consolidation)

**Change:** Maximum standard transaction weight increased from 1 MB to 4 MB

| Parameter | Before | After |
|-----------|--------|-------|
| `MAX_STANDARD_TX_WEIGHT` | 1,000,000 bytes | **4,000,000 bytes** |

**Why This Helps Mining Pools:**

1. **More UTXOs per consolidation** — Each consolidation transaction can now combine ~1,052 inputs instead of ~263 inputs.

2. **Fewer transactions needed** — To consolidate 10,000 UTXOs:
   - Before: 38 separate transactions
   - After: **10 transactions** (4x fewer)

3. **Lower total fees** — Fewer transactions means less overhead bytes (version, locktime, outputs), reducing total fees paid.

**Consolidation Capacity:**
```
Before: 1,000,000 bytes ÷ 3,772 bytes/input ≈ 263 inputs max
After:  4,000,000 bytes ÷ 3,772 bytes/input ≈ 1,052 inputs max
```

---

### 3. Larger Default Block Size (62% More Capacity)

**Change:** Default block weight increased from 32 MB to 52 MB

| Parameter | Before | After |
|-----------|--------|-------|
| `DEFAULT_BLOCK_MAX_WEIGHT` | 32,000,000 bytes | **52,000,000 bytes** |

**Why This Helps Mining Pools:**

1. **More room for consolidation** — A 4 MB consolidation transaction now uses only 7.7% of block space instead of 12.5%.

2. **Consolidation alongside user transactions** — Pools can include consolidation transactions without significantly impacting user transaction capacity.

3. **Headroom for growth** — Still well under the 64 MB hard cap, leaving room for future adjustments.

**Block Utilization:**
```
4 MB consolidation tx in 32 MB block = 12.5% of block space
4 MB consolidation tx in 52 MB block = 7.7% of block space
```

---

### 4. Increased Mempool Flexibility (2x Chain Depth)

**Change:** Ancestor and descendant limits doubled

| Parameter | Before | After |
|-----------|--------|-------|
| `DEFAULT_ANCESTOR_LIMIT` | 25 | **50** |
| `DEFAULT_ANCESTOR_SIZE_LIMIT_KVB` | 5,000 KB | **10,000 KB** |
| `DEFAULT_DESCENDANT_LIMIT` | 25 | **50** |
| `DEFAULT_DESCENDANT_SIZE_LIMIT_KVB` | 5,000 KB | **10,000 KB** |

**Why This Helps Mining Pools:**

1. **CPFP support for large transactions** — Child-Pays-For-Parent works better with larger Dilithium transactions that may need fee bumping.

2. **More complex payout structures** — Pools can create more sophisticated transaction chains for batched payouts.

3. **Better mempool acceptance** — Large consolidation transactions with multiple dependencies are less likely to be rejected.

---

## Combined Impact

### Before Optimization

| Metric | Value |
|--------|-------|
| Block Time | 10 minutes |
| Blocks per Day | 144 |
| Max Inputs per Consolidation | ~263 |
| Block Space per Day | 4.6 GB |
| Time to Consolidate 10,000 UTXOs | ~20 minutes (2 blocks minimum) |

### After Optimization

| Metric | Value | Improvement |
|--------|-------|-------------|
| Block Time | 1 minute | **10x faster** |
| Blocks per Day | 1,440 | **10x more** |
| Max Inputs per Consolidation | ~1,052 | **4x more** |
| Block Space per Day | 74.9 GB | **16x more** |
| Time to Consolidate 10,000 UTXOs | ~1 minute (1 block) | **20x faster** |

---

## Real-World Mining Pool Scenario

**Scenario:** A pool mines 100 blocks per day and needs to consolidate coinbase outputs for weekly payouts.

### Before Optimization
- 100 UTXOs accumulated per day × 7 days = 700 UTXOs to consolidate
- Each consolidation tx handles 263 inputs
- Need 3 consolidation transactions
- Each tx competes for space in 10-minute blocks
- **Total consolidation time: ~30 minutes**

### After Optimization
- Same 700 UTXOs to consolidate
- Single consolidation tx handles all 700 inputs (< 1,052 limit)
- Fits easily in one 52 MB block
- **Total consolidation time: ~1 minute**

---

## Network Configuration Summary

All BTQ networks now use optimized parameters:

| Network | Block Time | Confirmation Window | Use Case |
|---------|-----------|---------------------|----------|
| **Mainnet** | 1 minute | 10,080 blocks (~1 week) | Production |
| **Testnet** | 1 minute | 10,080 blocks (~1 week) | Public testing |
| **Signet** | 1 minute | 10,080 blocks (~1 week) | Signed testing |
| **Regtest** | 1 minute | 144 blocks (~2.4 hours) | Local development |

---

## What We Did NOT Change

| Parameter | Value | Reason |
|-----------|-------|--------|
| `COINBASE_MATURITY` | 100 blocks | Security: prevents spending reorganized coinbase outputs |
| `MAX_BLOCK_WEIGHT` | 64 MB | Hard cap provides safety margin |
| `MAX_BLOCK_SIGOPS_COST` | 80,000 | Prevents signature operation abuse |
| Dilithium key/signature sizes | 1,312/2,420 bytes | Cryptographic constants (cannot change) |

**Note on Coinbase Maturity:** With 1-minute blocks, the 100-block maturity period now represents ~100 minutes (~1.67 hours) instead of ~16.7 hours. This is a natural consequence of faster blocks and provides a good balance between security and usability.

---

## Files Modified

```
src/kernel/chainparams.cpp
├── CMainParams:     1-minute blocks, 1-week difficulty window
├── CTestNetParams:  1-minute blocks, 1-week difficulty window
├── SigNetParams:    1-minute blocks, 1-week difficulty window
└── CRegTestParams:  1-minute blocks, 144-block window (fast testing)

src/policy/policy.h
├── DEFAULT_BLOCK_MAX_WEIGHT:        32 MB → 52 MB
├── MAX_STANDARD_TX_WEIGHT:          1 MB → 4 MB
├── DEFAULT_ANCESTOR_LIMIT:          25 → 50
├── DEFAULT_ANCESTOR_SIZE_LIMIT_KVB: 5 MB → 10 MB
├── DEFAULT_DESCENDANT_LIMIT:        25 → 50
└── DEFAULT_DESCENDANT_SIZE_LIMIT_KVB: 5 MB → 10 MB
```

---

## For Mining Pool Operators

These optimizations are built into BTQ from genesis. No special configuration is needed:

```bash
# Start your node normally
./btqd -daemon

# Consolidation transactions up to 4 MB are now standard
# Blocks arrive every ~1 minute with 52 MB capacity
# Your pool software can consolidate ~1,052 UTXOs per transaction
```

### Recommended Pool Strategy

1. **Consolidate frequently** — With 1-minute blocks, there's no need to batch consolidations. Consolidate daily or even more often.

2. **Use large consolidation transactions** — Take advantage of the 4 MB limit to consolidate up to ~1,052 UTXOs at once.

3. **Include consolidation in your own blocks** — As a pool operator, you can include your consolidation transactions in blocks you mine, avoiding fee competition.

4. **Monitor confirmation times** — 6 confirmations now takes ~6 minutes instead of ~60 minutes.

---

*These optimizations enable BTQ to maintain post-quantum security through Dilithium signatures while providing practical transaction throughput for mining pool operations.*

