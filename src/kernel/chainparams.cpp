// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <arith_uint256.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <climits>

void MineGenesisBlock(CBlock &genesis);

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "BTQ mainnet genesis 22/Oct/2026";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * BTQ Quantum Main network - replaces BTQ mainnet
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::BTQMAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 2100000; // BTQ: 10x Bitcoin for 1-min blocks, same ~4yr halving
        
        // BTQ: Enable all features from height 1 for clean activation
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        // BTQ: Enable SegWit at height 1 for Dilithium witness transactions
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks (legacy, pre-LWMA)
        consensus.nPowTargetSpacing = 1 * 60;
        // LWMA from block 1. The running testnet stays on master.
        consensus.nLWMAHeight = 1;
        consensus.nDilithiumHeight = 1;
        // Mainnet is pre-launch: Dilithium is P2MR-only from genesis.
        consensus.nDilithiumP2MRHeight = 1;
        // Also pre-launch: the raised sigop limit and Dilithium validation
        // weight apply from the first block that can carry a spend.
        consensus.nDilithiumLimitsHeight = 1;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 18144; // 90% of 20160
        consensus.nMinerConfirmationWindow = 20160; // nPowTargetTimespan / nPowTargetSpacing (14 days / 1 min)
        
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        // Pre-launch floor: tied to the remined genesis block (see assert below).
        // Rotate both values at each tagged release.
        // 0x49d414 is GetBlockProof(genesis) for nBits 0x1e0377ae; the previous
        // 0x10002 predated the genesis remine and was below one block's work.
        consensus.nMinimumChainWork = uint256S("000000000000000000000000000000000000000000000000000000000049d414");
        consensus.defaultAssumeValid = uint256S("0x000002e4f1ad9a3f2321555ef6168d48295af74ec0921b69a3a76cec17b0e4f2");

        pchMessageStart[0] = 0xf1;
        pchMessageStart[1] = 0xb2;
        pchMessageStart[2] = 0xa3;
        pchMessageStart[3] = 0xd4;
        nDefaultPort = 9333;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1792627200, 505727, 0x1e0377ae, 1, 5 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000002e4f1ad9a3f2321555ef6168d48295af74ec0921b69a3a76cec17b0e4f2"));
        assert(genesis.hashMerkleRoot == uint256S("0x0bbd7929e39fd86a74157ebda9f3ecf8c018216951d7aacd0c0102ba061ef9d3"));

        vSeeds.emplace_back("seed1.btq.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,75);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,135);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,235);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1F};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE5};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,76);
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,136);

        bech32_hrp = "qbtc";
        dilithium_bech32_hrp = "dbtc";

        // Currently empty: no fixed seeds have been provisioned for mainnet
        // (issue #114). The assignment is kept so that populating
        // chainparams_seed_main is the only step needed.
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, genesis.GetHash()},
            }
        };

        m_assumeutxo_data = {
        };

        chainTxData = ChainTxData{
            .nTime    = 1792627200,
            .nTxCount = 1,
            .dTxRate  = 0.0,
        };
    }
};

/**
 * BTQ Quantum Test network
 */
class CTestNetParams : public CChainParams {
public:
    explicit CTestNetParams(const TestNetOptions& opts) {
        m_chain_type = ChainType::BTQTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 2100000;
        
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks (legacy, pre-LWMA)
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.nLWMAHeight = 1;
        consensus.nDilithiumHeight = 1;
        // Same rules as mainnet. The running testnet stays on master.
        // -testnetdilithiump2mrheight can still override the height.
        consensus.nDilithiumP2MRHeight = opts.dilithium_p2mr_height.value_or(1);
        // Same rules as mainnet. This testnet is the new chain, so the raised
        // sigop limit and Dilithium validation weight start at height 1.
        consensus.nDilithiumLimitsHeight = 1;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 15120; // 75% of 20160 for testchains
        consensus.nMinerConfirmationWindow = 20160;
        
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        // One genesis proof, same bits as mainnet. 0x49d414 is GetBlockProof for 0x1e0377ae.
        consensus.nMinimumChainWork = uint256S("000000000000000000000000000000000000000000000000000000000049d414");
        consensus.defaultAssumeValid = uint256S("0x0000022f03b97f577d6dcd5b776f93bb69535c3c84f3e1d4d66c4fa81f45a133");

        // 0c120a08 stays on master, with the testnet that is running today.
        pchMessageStart[0] = 0x71;
        pchMessageStart[1] = 0x74;
        pchMessageStart[2] = 0x6e;
        pchMessageStart[3] = 0x74;
        nDefaultPort = 19333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        const char* pszTimestamp = "BTQ testnet genesis 22/Oct/2026";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1792627260, 452194, 0x1e0377ae, 1, 5 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0000022f03b97f577d6dcd5b776f93bb69535c3c84f3e1d4d66c4fa81f45a133"));
        assert(genesis.hashMerkleRoot == uint256S("0x190d305f7e3f0e4d3b1a5a7d8a99b57dc05b107b54feadebfeda6320f7521d6d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // DNS names are not chosen. Do not list testnet-seed1 or testnet-seed2.

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);

        bech32_hrp = "tbtq";
        dilithium_bech32_hrp = "tdbt";

        vFixedSeeds.clear();
        
        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, genesis.GetHash()},
            }
        };

        m_assumeutxo_data = {
        };

        chainTxData = ChainTxData{
            .nTime    = 1792627260,
            .nTxCount = 1,
            .dTxRate  = 0.0,
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!options.challenge) {
            bin = ParseHex("522103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            if (bin.empty()) {
                throw std::runtime_error("Default signet challenge is invalid");
            }
            
            vSeeds.emplace_back("signet-seed1.btq.com");
            vSeeds.emplace_back("signet-seed2.btq.com");
            
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                1771804800,
                1,
                0.0,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::BTQSIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 2100000;
        
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks (legacy, pre-LWMA)
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.nLWMAHeight = 300000;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 18144; // 90% of 20160
        consensus.nMinerConfirmationWindow = 20160;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
        consensus.nDilithiumHeight = 1;
        consensus.nDilithiumP2MRHeight = 1;
        consensus.nDilithiumLimitsHeight = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "BTQ genesis remine: quantum-safe launch baseline, 26/Feb/2026";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1771804800, 2666531, 0x1e0377ae, 1, 5 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000120a12ac337785653cdff1f23b4891d3ffeb492a011cc95b165e86a4b15"));
        assert(genesis.hashMerkleRoot == uint256S("0xec88310bd306cf5f9554cc257db16b81147e4bd0efda75f11b38467a5d918db1"));

        vFixedSeeds.clear();

        m_assumeutxo_data = {
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);

        bech32_hrp = "qtb";
        dilithium_bech32_hrp = "sdbt";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::BTQREGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 1500;
        
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks (legacy, pre-LWMA)
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.nLWMAHeight = 300000;
        consensus.nDilithiumHeight = 1;
        consensus.nDilithiumP2MRHeight = 1;
        consensus.nDilithiumLimitsHeight = 1;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0;

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 19444;
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_LWMA:
                consensus.nLWMAHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DILITHIUM:
                consensus.nDilithiumHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DILITHIUM_P2MR:
                consensus.nDilithiumP2MRHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DILITHIUM_LIMITS:
                consensus.nDilithiumLimitsHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        const char* pszTimestamp = "BTQ genesis remine: quantum-safe launch baseline, 26/Feb/2026";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1771804800, 3, 0x207fffff, 1, 5 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x5a6c309a7e9bb2fa314e63630520ca3c598c86a91dd2c6737e160cfadfc50f38"));
        assert(genesis.hashMerkleRoot == uint256S("0xec88310bd306cf5f9554cc257db16b81147e4bd0efda75f11b38467a5d918db1"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, genesis.GetHash()},
            }
        };

        if (opts.test_checkpoint) {
            checkpointData.mapCheckpoints.emplace(*opts.test_checkpoint);
        }

        // Scaffolding for the assumeutxo tests, which mine to this height and
        // then snapshot. Every chain shipped an empty table, so ActivateSnapshot
        // rejected every snapshot as "height in snapshot metadata not
        // recognized" and the whole feature went untested. These values are
        // BTQ's own: the base block hash and the serialized UTXO hash both
        // depend on the subsidy and chain parameters, so upstream's do not
        // carry over. Regenerate with the dumptxoutset output logged by
        // CreateAndActivateUTXOSnapshot if the test chain ever changes.
        m_assumeutxo_data = {
            {
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256S("0x5d86a7f67e8bb0e146c206164dcc984c2af8b3449845a42dd72ef76f082e690a")},
                .nChainTx = 111,
                .blockhash = uint256S("0x100831e245415bda8a1b889280fd766c9a1e8a805e2c89c85ae4bc582b4f3efb"),
            },
            { // test/functional/feature_assumeutxo.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256S("0x67d34d66e24f805fceee2fab4d9667a9efae69502a62e72ed405e1f9732475f0")},
                .nChainTx = 300,
                .blockhash = uint256S("0x7abfb358694c66f37e421544f2394904f7d845c450be94e37358f0a590850e20"),
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);

        bech32_hrp = "qcrt";
        dilithium_bech32_hrp = "rdbt";
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet(const TestNetOptions& options)
{
    return std::make_unique<const CTestNetParams>(options);
}

void MineGenesisBlock(CBlock &genesis)
{
    arith_uint256 best = arith_uint256();
    int n = 0;
    
    arith_uint256 hashTarget = arith_uint256().SetCompact(genesis.nBits);
    while (UintToArith256(genesis.GetHash()) > hashTarget) {
        
        arith_uint256 c = UintToArith256(genesis.GetHash());
        
        if (c < best || n == 0) {
            best = c;
            n = 1;
            
            printf("%s %s %s nonce=%u\n", genesis.GetHash().GetHex().c_str(), hashTarget.GetHex().c_str(),
                   best.GetHex().c_str(), genesis.nNonce); 
        }
        
        ++genesis.nNonce;
        if (genesis.nNonce == 0) { 
            ++genesis.nTime; 
            printf("Nonce wrapped, incremented time to %u\n", genesis.nTime);
        }
    }
    
    printf("\n*** FOUND GENESIS BLOCK ***\n");
    printf("Nonce: %u\n", genesis.nNonce);
    printf("Time: %u\n", genesis.nTime);
    printf("Hash: %s\n", genesis.GetHash().GetHex().c_str());
    printf("Merkle Root: %s\n", genesis.hashMerkleRoot.GetHex().c_str());
    printf("Converting genesis hash to string: %s\n", genesis.ToString().c_str()); 
}
