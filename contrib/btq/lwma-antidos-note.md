# LWMA and the 144-block anti-DoS work threshold

Characterization note for [GitHub issue #168](https://github.com/btq-ag/btq-core/issues/168).
No consensus or net_processing change is proposed here.

## What `GetAntiDoSWorkThreshold` does

`PeerManagerImpl::GetAntiDoSWorkThreshold` in `src/net_processing.cpp`
computes:

```
near = tip.nChainWork - min(144 * GetBlockProof(tip), tip.nChainWork)
threshold = max(near, MinimumChainWork())
```

Incoming headers (and compact-block / block paths that consult the same
helper) with total chain work below that threshold are treated as a
low-work DoS candidate (`TryLowWorkHeadersSync` and the compact-block
guard). The 144-block buffer is so near-tip forks are still accepted.

## Why Bitcoin is fine

Bitcoin retargets every 2016 blocks. Across a 144-block window difficulty
barely moves, so two honest forks of similar length stay near equal
per-block work (`r ≈ 1`). For any fork length `H > 144`,
`1 > (H - 144) / H`, so same-length peers stay above the threshold.

## Why BTQ is not

BTQ uses per-block LWMA with `nPowTargetSpacing = 60s`. Two branches can
diverge by orders of magnitude. A public-testnet observation was roughly
difficulty ~40 vs ~7800 (`r ≈ 0.005`). Activation heights
(`nLWMAHeight` in `src/kernel/chainparams.cpp`): mainnet = 1;
testnet/regtest = 300000.

## The ignore inequality

A same-length fork is ignored when

```
D2 / D1 < (H1 - 144) / H2
```

Worked numbers for `H1 = H2 = 450`:

- crossover: `r < 306 / 450 ≈ 0.68`
- observed testnet shape: `r ≈ 40 / 7800 ≈ 0.005` (far below)

Each side's tip-relative threshold can then treat the other as low-work.
Unit coverage of this arithmetic lives in
`src/test/pow_lwma_tests.cpp` (`antidos_work_threshold_lwma_divergence`).

## Mainnet genesis implications

Mainnet launches with `nLWMAHeight = 1` (LWMA from the first block) and
`powLimit` `00000377ae…`. `nMinimumChainWork` is nonzero
(`0x49d414` = `GetBlockProof(genesis)` at `nBits 0x1e0377ae`). On
testnet/regtest `nMinimumChainWork` is zero, so `max()` does not provide
a floor and the 144-block tip-relative term dominates once the chain is
taller than 144 blocks. Early on mainnet (`height < 144`)
`near_chaintip_work` is 0 and the floor is one genesis-block of work —
still not a defense against a long low-diff fork after height 144.

## Regtest limitation

`CRegTestParams` sets `fPowNoRetargeting = true` and
`nLWMAHeight = 300000`. Regtest cannot reproduce live LWMA difficulty
swings. Characterization is the formula unit test above, not a P2P
functional test.

## Next step

Measure before changing the 144-block buffer. This note does not
recommend a specific new constant or any consensus change.
