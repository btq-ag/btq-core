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
    const char* pszTimestamp = "BTQ Genesis Block - Quantum Resistant BTQ Fork 2024"; //TODO think of a better timestamp
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
        consensus.nSubsidyHalvingInterval = 210000;
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: No script flag exceptions for clean start
        
        // BTQ: Enable all features from height 1 for clean activation
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // CLTV (BIP65) at height 1
        consensus.BIP66Height = 1; // DERSIG (BIP66) at height 1
        consensus.CSVHeight = 1;   // CSV at height 1
        
        // BTQ: Disable SegWit - set to INT_MAX to effectively disable
        consensus.SegwitHeight = INT_MAX;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        
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
        genesis = CreateGenesisBlock(1704067200, 52692, 0x1f00ffff, 1, 50 * COIN); //TODO change timestamp to actual deployment time 
        //MineGenesisBlock(genesis);
        consensus.hashGenesisBlock = genesis.GetHash();
        // BTQ: Genesis block assertions with mined values
        assert(consensus.hashGenesisBlock == uint256S("0x00004e49ccbf1f195f34f5fe088d8edb2c7d074fadcd575b46a6d445d20942a1"));
        assert(genesis.hashMerkleRoot == uint256S("0xf9fea3db1bf427a4ebed47fd727c06fc4fa172cd095f300be88f8c44824dcc16"));

        // BTQ: Add BTQ seed nodes (replace with actual DNS seeds)
        vSeeds.emplace_back("seed1.btq.com");
        //vSeeds.emplace_back("TODO: MORE SEED NODES");

        // BTQ: Custom Base58 prefixes for BTQ addresses
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,75);  // BTQ: B... addresses (25 = 'B')
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,135);  // BTQ: Q... script addresses (85 = 'Q')
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,235); // BTQ: Private keys
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1F}; // BTQ: Extended public keys
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE5}; // BTQ: Extended private keys

        bech32_hrp = "qbtc";

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
        consensus.nSubsidyHalvingInterval = 210000;
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: No script flag exceptions for clean start
        
        // BTQ: Enable all features from height 1 for clean activation
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // CLTV (BIP65) at height 1
        consensus.BIP66Height = 1; // DERSIG (BIP66) at height 1
        consensus.CSVHeight = 1;   // CSV at height 1
        
        // BTQ: Disable SegWit - set to INT_MAX to effectively disable
        consensus.SegwitHeight = INT_MAX;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016;
        
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
        const char* pszTimestamp = "BTQ Testnet Genesis Block - Quantum Resistant BTQ Fork 2024";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1704067200, 0, 0x207fffff, 1, 50 * COIN);
        //MineGenesisBlock(genesis);  // Already mined - nonce=0
        consensus.hashGenesisBlock = genesis.GetHash();
        // BTQ: Testnet genesis block assertions with mined values
        assert(consensus.hashGenesisBlock == uint256S("0x212f4cabe852aab559e15fda37836d6f61b99cecd5f198394433ea3343279f0c"));
        assert(genesis.hashMerkleRoot == uint256S("0x52a953f737c03be1abd87b4d6cfb00cc38c899bd71c402497203423d2ed2aa86"));

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

        bech32_hrp = "tbtq"; // BTQ: tbtq for testnet Bech32 addresses

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
        consensus.nSubsidyHalvingInterval = 210000;
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: Keep BIP34/66/CSV at height 1 (already optimal)
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        
        // BTQ: Disable SegWit - set to INT_MAX to effectively disable
        consensus.SegwitHeight = INT_MAX;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
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

        // BTQ: Create BTQ SigNet genesis block (around line 364)
        const char* pszTimestamp = "BTQ SigNet Genesis Block - Quantum Resistant BTQ Fork 2024";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1704067200, 0, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // BTQ: Update these assertions after mining the genesis block
        // assert(consensus.hashGenesisBlock == uint256S("0x[BTQ_SIGNET_GENESIS_HASH]"));
        // assert(genesis.hashMerkleRoot == uint256S("0x[BTQ_SIGNET_MERKLE_ROOT]"));

        vFixedSeeds.clear();

        m_assumeutxo_data = {
            {
                .height = 160'000,
                .hash_serialized = AssumeutxoHash{uint256S("0xfe0a44309b74d6b5883d246cb419c6221bcccf0b308c9b59b7d70783dbdf928a")},
                .nChainTx = 2289496,
                .blockhash = uint256S("0x0000003ca3c99aff040f2563c2ad8f8ec88bd0fd6b8f0895cfaf1ef90353a62c")
            }
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "qtb"; // BTQ: qtb for signet (same as testnet)

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
        consensus.nSubsidyHalvingInterval = 150;
        
        // BTQ: Set signature algorithm to NONE initially (stub implementation)
        consensus.signature_algorithm = Consensus::SignatureAlgorithm::NONE;
        
        // BTQ: Keep BIP34/66/CSV at height 1 (already optimal)
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        
        // BTQ: Disable SegWit - set to INT_MAX to effectively disable
        consensus.SegwitHeight = INT_MAX; // Disabled unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

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

        // BTQ: Create BTQ regtest genesis block
        const char* pszTimestamp = "BTQ Regtest Genesis Block - Quantum Resistant BTQ Fork 2024";
        const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1704067200, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // BTQ: These assertions will need to be updated once you mine the actual genesis block
        // assert(consensus.hashGenesisBlock == uint256S("0x[REGTEST_GENESIS_HASH]"));
        // assert(genesis.hashMerkleRoot == uint256S("0x[REGTEST_MERKLE_ROOT]"));

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
            {
                // BTQ: Add regtest assumeutxo data for testing when we have it
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256S("0x6657b736d4fe4db0cbc796789e812d5dba7f5c143764b1b6905612f1830609d1")},
                .nChainTx = 111,
                .blockhash = uint256S("0x696e92821f65549c7ee134edceeeeaaa4105647a3c4fd9f298c0aec0ab50425c")
            },
            {
                // For use by test/functional/feature_assumeutxo.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256S("0x61d9c2b29a2571a5fe285fe2d8554f91f93309666fc9b8223ee96338de25ff53")},
                .nChainTx = 300,
                .blockhash = uint256S("0x7e0517ef3ea6ecbed9117858e42eedc8eb39e8698a38dcbd1b3962a283233f4c")
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

        bech32_hrp = "qcrt";
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
