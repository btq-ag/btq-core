// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_CONSENSUS_PARAMS_H
#define BTQ_CONSENSUS_PARAMS_H

#include <uint256.h>

#include <chrono>
#include <limits>
#include <map>
#include <vector>

namespace Consensus {

/**
 * Enum defining which signature algorithm system is used for transactions.
 * Initially set to NONE to maintain backward compatibility while we prepare
 * for post-quantum signature integration.
 */
enum class SignatureAlgorithm {
    NONE,      // No specific algorithm enforced (legacy mode)
    DILITHIUM, // CRYSTALS-Dilithium post-quantum signature scheme
    FALCON,    // Falcon post-quantum signature scheme (reserved, not implemented)
    SPHINCS    // SPHINCS+ post-quantum signature scheme (reserved, not implemented)
};

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
    DEPLOYMENT_LWMA,
    DEPLOYMENT_DILITHIUM,
    DEPLOYMENT_DILITHIUM_P2MR,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_DILITHIUM_P2MR; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * Reserved. Never read.
     *
     * Every network sets this to NONE (kernel/chainparams.cpp) and nothing
     * consumes it, so changing it has no effect on validation. No Falcon or
     * SPHINCS+ implementation exists in the tree either -- the enum values are
     * the only occurrences outside vendored directories.
     *
     * Dilithium is selected by script opcode and activation height
     * (nDilithiumHeight / nDilithiumP2MRHeight), not by this field. See #116.
     */
    SignatureAlgorithm signature_algorithm{SignatureAlgorithm::NONE};
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, uint32_t> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    /**
     * Minimum blocks including miner confirmation of the total of nMinerConfirmationWindow blocks
     * in a retargeting period (nPowTargetTimespan / nPowTargetSpacing), also used for BIP9 deployments.
     * BTQ uses 20160 blocks (14 days / 1-minute blocks). Examples: 18144 for 90%, 15120 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;

    /** LWMA difficulty adjustment activation height (hard fork) */
    int nLWMAHeight{std::numeric_limits<int>::max()};
    /** Height at which Dilithium script opcodes become consensus-mandatory */
    int nDilithiumHeight{1};
    /** Height at which Dilithium opcodes become P2MR-only (BTQ-AUDIT-048).
     *  Blocks at or above this height reject Dilithium opcodes outside P2MR
     *  tapscript and disable the legacy witness-v0 Dilithium keyhash routing. */
    int nDilithiumP2MRHeight{1};
    /** LWMA averaging window, in blocks.
     *
     *  Consensus-critical: changing it on a live chain is a hard fork.
     *
     *  Inherited as 45 from chains with ~10x longer block spacing, where it
     *  covers 7.5 hours of history. At BTQ's 60-second spacing the same number
     *  covers 45 minutes, which is short enough that a half-hour hashrate burst
     *  dominates the weighted window (weights 1..N sum to N(N+1)/2, so the most
     *  recent 30 blocks carry 88% of it at N=45 against 20% at N=288).
     *
     *  A field rather than a constant so the value can be swept in tests and
     *  overridden on regtest; see issue #110 and pow_lwma_tests.cpp. Every
     *  network currently sets 45, so this change is behaviour-preserving. */
    int nLWMAWindow{45};
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a BTQ Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        case DEPLOYMENT_LWMA:
            return nLWMAHeight;
        case DEPLOYMENT_DILITHIUM:
            return nDilithiumHeight;
        case DEPLOYMENT_DILITHIUM_P2MR:
            return nDilithiumP2MRHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BTQ_CONSENSUS_PARAMS_H
