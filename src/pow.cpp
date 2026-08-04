// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    // After LWMA hard fork: per-block difficulty adjustment
    if (pindexLast->nHeight + 1 >= params.nLWMAHeight) {
        return LwmaGetNextWorkRequired(pindexLast, pblock, params);
    }

    // --- Legacy difficulty adjustment (pre-fork) ---

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int LwmaGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const int64_t T = params.nPowTargetSpacing;
    const int N = params.nLWMAWindow;
    const int64_t k = static_cast<int64_t>(N) * (N + 1) * T / 2;
    const int height = pindexLast->nHeight;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    if (height < N) {
        return powLimit.GetCompact();
    }

    arith_uint256 targetQuotientSum;
    arith_uint256 targetRemainderSum;
    int64_t weightedSolvetimes = 0;
    int64_t previousTimestamp = pindexLast->GetAncestor(height - N)->GetBlockTime();

    for (int i = 1; i <= N; i++) {
        const CBlockIndex* block = pindexLast->GetAncestor(height - N + i);
        int64_t thisTimestamp = block->GetBlockTime();

        int64_t solvetime = thisTimestamp - previousTimestamp;
        solvetime = std::min(solvetime, 6 * T);
        solvetime = std::max(solvetime, -6 * T);

        weightedSolvetimes += solvetime * i;

        arith_uint256 target;
        target.SetCompact(block->nBits);
        const arith_uint256 targetQuotient = target / N;
        targetQuotientSum += targetQuotient;
        targetRemainderSum += target - targetQuotient * static_cast<uint32_t>(N);

        previousTimestamp = thisTimestamp;
    }

    if (weightedSolvetimes < 1) {
        weightedSolvetimes = 1;
    }

    const arith_uint256 averageTarget = targetQuotientSum + targetRemainderSum / N;
    const arith_uint256 k_uint{static_cast<uint64_t>(k)};
    const uint32_t weighted_solvetimes_u32{static_cast<uint32_t>(weightedSolvetimes)};
    const arith_uint256 quotient = averageTarget / k_uint;
    const arith_uint256 remainder = averageTarget - quotient * static_cast<uint32_t>(k);

    arith_uint256 nextTarget;
    if (quotient > powLimit / weighted_solvetimes_u32) {
        nextTarget = powLimit;
    } else {
        nextTarget = quotient * weighted_solvetimes_u32;
        nextTarget += (remainder * weighted_solvetimes_u32) / k_uint;
    }

    if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }

    return nextTarget.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    // LWMA adjusts difficulty every block
    if (height >= params.nLWMAHeight) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
