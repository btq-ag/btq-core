// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The BTQ Core developers
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
    const char* pszTimestamp = "Quantum is here - calcalistech.com/rkb3zkze11e 31/12/25";
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
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: No script flag exceptions for clean start
        
        // BTQ: Enable all features from height 1 for clean activation
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // CLTV (BIP65) at height 1
        consensus.BIP66Height = 1; // DERSIG (BIP66) at height 1
        consensus.CSVHeight = 1;   // CSV at height 1
        
        // BTQ: Enable SegWit at height 1 for Dilithium witness transactions
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 18144; // 90% of 20160
        consensus.nMinerConfirmationWindow = 20160; // nPowTargetTimespan / nPowTargetSpacing (14 days / 1 min)
        
        // BTQ: Disable all version bits deployments
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // BTQ: Disable Taproot (BIPs 340-342) - mark as NEVER_ACTIVE
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * BTQ: Quantum network message start - unique identifier
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf1;
        pchMessageStart[1] = 0xb2;
        pchMessageStart[2] = 0xa3;
        pchMessageStart[3] = 0xd4;
        nDefaultPort = 9333;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        // BTQ: Create BTQ genesis block with custom timestamp
        genesis = CreateGenesisBlock(1704067200, 194445, 0x1f00ffff, 1, 5 * COIN); //TODO change timestamp to actual deployment time 
        //MineGenesisBlock(genesis);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0000630a5e65a4bdeb8ad46b1c659de7917c6b75a8c15a997cd10c0260e8f038"));
        assert(genesis.hashMerkleRoot == uint256S("0xc8d6a9eb714de74c9eff54ad5818da2a8afd11703a30202c227ac6974f728511"));

        // BTQ: Add BTQ seed nodes (replace with actual DNS seeds)
        vSeeds.emplace_back("seed1.btq.com");
        //vSeeds.emplace_back("TODO: MORE SEED NODES");

        // BTQ: Custom Base58 prefixes for BTQ addresses
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,75);  // BTQ: B... addresses (25 = 'B')
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,135);  // BTQ: Q... script addresses (85 = 'Q')
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,235); // BTQ: Private keys
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1F}; // BTQ: Extended public keys
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE5}; // BTQ: Extended private keys
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,76);  // BTQ: D... Dilithium addresses (76 = 'D')
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,136);  // BTQ: R... Dilithium script addresses (136 = 'R')

        bech32_hrp = "qbtc";
        dilithium_bech32_hrp = "dbtc"; // BTQ: dbtc for Dilithium Bech32 addresses

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                // BTQ: Genesis checkpoint
                {0, genesis.GetHash()},
            }
        };

        m_assumeutxo_data = {
            // BTQ: Add assumeutxo data in future updates
        };

        chainTxData = ChainTxData{
            // BTQ: Initial chain data - will be changed once we deploy mainnet
            .nTime    = 1704067200, // Jan 1, 2024
            .nTxCount = 1,          // Genesis transaction
            .dTxRate  = 0.0,        // No transactions yet
        };
    }
};

/**
 * BTQ Quantum Test network - replaces BTQ testnet
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::BTQTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 2100000; // BTQ: 10x Bitcoin for 1-min blocks, same ~4yr halving
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: No script flag exceptions for clean start
        
        // BTQ: Enable all features from height 1 for clean activation
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // CLTV (BIP65) at height 1
        consensus.BIP66Height = 1; // DERSIG (BIP66) at height 1
        consensus.CSVHeight = 1;   // CSV at height 1
        
        // BTQ: Enable SegWit at height 1 for Dilithium witness transactions
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 15120; // 75% of 20160 for testchains
        consensus.nMinerConfirmationWindow = 20160; // nPowTargetTimespan / nPowTargetSpacing (14 days / 1 min)
        
        // BTQ: Disable all version bits deployments
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // BTQ: Disable Taproot (BIPs 340-342) - mark as NEVER_ACTIVE
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * BTQ: Quantum testnet message start
         */
        pchMessageStart[0] = 0x0c;
        pchMessageStart[1] = 0x12;
        pchMessageStart[2] = 0x0a;
        pchMessageStart[3] = 0x08;
        nDefaultPort = 19333; // BTQ: Unique port for BTQ testnet
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        // BTQ: Create BTQ testnet genesis block with mined values
        const char* pszTimestamp = "Quantum is here - calcalistech.com/rkb3zkze11e 31/12/25";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1738540800, 0, 0x207fffff, 1, 5 * COIN);
        //MineGenesisBlock(genesis);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x1bac0760b85ab8c6e58d5e830c589b8203765e3f03cf886caec17cba5441fb4b"));
        assert(genesis.hashMerkleRoot == uint256S("0xc8d6a9eb714de74c9eff54ad5818da2a8afd11703a30202c227ac6974f728511"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // BTQ: Add BTQ testnet seed nodes
        vSeeds.emplace_back("testnet-seed1.btq.com");
        vSeeds.emplace_back("testnet-seed2.btq.com");

        // BTQ: Testnet uses same address prefixes as mainnet for simplicity
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111); // Standard testnet prefix
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196); // Standard testnet prefix
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // Standard testnet prefix
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);  // BTQ: Testnet Dilithium addresses
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);  // BTQ: Testnet Dilithium script addresses

        bech32_hrp = "tbtq"; // BTQ: tbtq for testnet Bech32 addresses
        dilithium_bech32_hrp = "tdbt"; // BTQ: tdbt for testnet Dilithium Bech32 addresses

        vFixedSeeds.clear(); // BTQ: No fixed seeds initially
        
        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                // BTQ: Add testnet checkpoints as the network grows
            }
        };

        m_assumeutxo_data = {
            // BTQ: Add testnet assumeutxo data in future updates
        };

        chainTxData = ChainTxData{
            // BTQ: Initial testnet chain data
            .nTime    = 1704067200, // Jan 1, 2024
            .nTxCount = 1,          // Genesis transaction
            .dTxRate  = 0.0,        // No transactions yet
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
            // BTQ: Use BTQ-specific SigNet challenge instead of BTQ's
            bin = ParseHex("512103[YOUR_BTQ_SIGNET_PUBKEY]210359[YOUR_BTQ_SIGNET_PUBKEY2]52ae"); //TODO: We need to set up a admin key for Signet at some point, this is invalid
            
            // BTQ: Use BTQ SigNet seeds
            vSeeds.emplace_back("signet-seed1.btq.com");
            vSeeds.emplace_back("signet-seed2.btq.com");
            
            // BTQ: Reset chain work and assume valid for new BTQ SigNet
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                1704067200, // Jan 1, 2024
                1,          // Genesis transaction
                0.0,        // No transactions yet
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
        consensus.nSubsidyHalvingInterval = 2100000; // BTQ: 10x Bitcoin for 1-min blocks, same ~4yr halving
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: Keep BIP34/66/CSV at height 1 (already optimal)
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        // BTQ: Enable SegWit at height 1 for Dilithium witness transactions
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 18144; // 90% of 20160
        consensus.nMinerConfirmationWindow = 20160; // nPowTargetTimespan / nPowTargetSpacing (14 days / 1 min)
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        
        // BTQ: Disable all version bits deployments
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // BTQ: Disable Taproot (BIPs 340-342) - mark as NEVER_ACTIVE
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;

        // BTQ: Create BTQ SigNet genesis block
        const char* pszTimestamp = "Quantum is here - calcalistech.com/rkb3zkze11e 31/12/25";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1704067200, 12871552, 0x1e0377ae, 1, 5 * COIN);
        //MineGenesisBlock(genesis);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0000023375058c22d1702928d6f02be61902cc6cd4b15ef5b39b7c4165745aba"));
        assert(genesis.hashMerkleRoot == uint256S("0xc8d6a9eb714de74c9eff54ad5818da2a8afd11703a30202c227ac6974f728511"));

        vFixedSeeds.clear();

        m_assumeutxo_data = {
            // BTQ: Add signet assumeutxo data once chain is running
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);  // BTQ: Signet Dilithium addresses
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);  // BTQ: Signet Dilithium script addresses

        bech32_hrp = "qtb"; // BTQ: qtb for signet (same as testnet)
        dilithium_bech32_hrp = "sdbt"; // BTQ: sdbt for signet Dilithium Bech32 addresses

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
        consensus.nSubsidyHalvingInterval = 1500; // BTQ: 10x Bitcoin for 1-min blocks
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: Keep BIP34/66/CSV at height 1 (already optimal)
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        
        // BTQ: Enable SegWit at height 1 for Dilithium witness transactions
        consensus.SegwitHeight = 1; // Disabled unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 20160)

        // BTQ: Disable all version bits deployments
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // BTQ: Disable Taproot (BIPs 340-342) - mark as NEVER_ACTIVE
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * BTQ: Quantum regtest message start
         */
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 19444; // BTQ: Unique port for BTQ regtest
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
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        // BTQ: Create BTQ regtest genesis block with mined values
        const char* pszTimestamp = "Quantum is here - calcalistech.com/rkb3zkze11e 31/12/25";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1738540800, 0, 0x207fffff, 1, 5 * COIN);
        //MineGenesisBlock(genesis);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x1bac0760b85ab8c6e58d5e830c589b8203765e3f03cf886caec17cba5441fb4b"));
        assert(genesis.hashMerkleRoot == uint256S("0xc8d6a9eb714de74c9eff54ad5818da2a8afd11703a30202c227ac6974f728511"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                // BTQ: Genesis checkpoint
                {0, genesis.GetHash()},
            }
        };

        m_assumeutxo_data = {
            // BTQ: Add regtest assumeutxo data once chain parameters are finalized
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
        base58Prefixes[DILITHIUM_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);  // BTQ: Regtest Dilithium addresses
        base58Prefixes[DILITHIUM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,197);  // BTQ: Regtest Dilithium script addresses

        bech32_hrp = "qcrt";
        dilithium_bech32_hrp = "rdbt"; // BTQ: rdbt for regtest Dilithium Bech32 addresses
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

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
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
