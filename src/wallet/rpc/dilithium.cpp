// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/dilithium.h>

#include <wallet/rpc/util.h>
#include <wallet/wallet.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/sign.h>
#include <script/interpreter.h>
#include <core_io.h>
#include <rpc/rawtransaction_util.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <univalue.h>

namespace wallet {

// Helper function to check if a script is a Dilithium script
static bool IsDilithiumScript(const CScript& script)
{
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(script, solutions);
    
    return (type == TxoutType::DILITHIUM_PUBKEY ||
            type == TxoutType::DILITHIUM_PUBKEYHASH ||
            type == TxoutType::DILITHIUM_SCRIPTHASH ||
            type == TxoutType::DILITHIUM_MULTISIG ||
            type == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH ||
            type == TxoutType::DILITHIUM_WITNESS_V0_SCRIPTHASH);
}

// Helper function to extract Dilithium key ID from script
static bool ExtractDilithiumKeyID(const CScript& script, CKeyID& keyID)
{
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(script, solutions);
    
    if (type == TxoutType::DILITHIUM_PUBKEY && !solutions.empty()) {
        // For DILITHIUM_PUBKEY, the solution contains the public key
        CDilithiumPubKey pubkey(solutions[0]);
        keyID = CKeyID(pubkey.GetID());
        return true;
    } else if (type == TxoutType::DILITHIUM_PUBKEYHASH && !solutions.empty()) {
        // For DILITHIUM_PUBKEYHASH, the solution contains the key hash
        keyID = CKeyID(uint160(solutions[0]));
        return true;
    } else if ((type == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH ||
                type == TxoutType::WITNESS_V0_KEYHASH) && !solutions.empty()) {
        // For witness v0 keyhash, the solution is the 20-byte key hash.
        // Treat it as a Dilithium key hash; wallet Dilithium key lookup will
        // succeed only if the corresponding Dilithium key exists.
        keyID = CKeyID(uint160(solutions[0]));
        return true;
    }
    
    return false;
}

// Helper function to sign transactions with Dilithium keys
static bool SignTransactionWithDilithium(const CWallet& wallet, CMutableTransaction& tx, 
                                        const std::map<COutPoint, Coin>& coins, 
                                        int sighash, std::map<int, bilingual_str>& input_errors,
                                        bool force_dilithium = true)
{
    bool complete = true;
    
    // Iterate through all inputs
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        const Coin& coin = coins.at(txin.prevout);
        
        // Check if this is a Dilithium output or if we're forcing Dilithium signing
        if (force_dilithium || IsDilithiumScript(coin.out.scriptPubKey)) {
            // Get the Dilithium key for this input
            CDilithiumKey dilithium_key;
            bool key_found = false;
            
            // Try to get the Dilithium key from all script pub key managers
            auto spk_mans = wallet.GetAllScriptPubKeyMans();
            for (auto& spk_man : spk_mans) {
                // Try descriptor wallet first
                DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
                if (desc_spk_man) {
                    // Extract key ID from the script
                    CKeyID keyID;
                    if (ExtractDilithiumKeyID(coin.out.scriptPubKey, keyID)) {
                        if (desc_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                            key_found = true;
                            break;
                        }
                    }
                }
                // Try legacy wallet
                LegacyScriptPubKeyMan* legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man);
                if (legacy_spk_man) {
                    CKeyID keyID;
                    if (ExtractDilithiumKeyID(coin.out.scriptPubKey, keyID)) {
                        if (legacy_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                            key_found = true;
                            break;
                        }
                    }
                }
            }
            
            if (!key_found) {
                input_errors[i] = _("Dilithium key not found in wallet for this input");
                complete = false;
                continue;
            }
            
            // Create the signature hash for this input
            uint256 sighash_hash = SignatureHash(coin.out.scriptPubKey, tx, i, sighash, coin.out.nValue, SigVersion::BASE);
            
            // Sign with Dilithium key
            std::vector<unsigned char> vchSig;
            if (!dilithium_key.Sign(sighash_hash, vchSig)) {
                input_errors[i] = _("Failed to sign with Dilithium key");
                complete = false;
                continue;
            }
            
            // For witness scripts, put signature in witness field; for non-witness, use scriptSig
            std::vector<std::vector<unsigned char>> solutions;
            TxoutType script_type = Solver(coin.out.scriptPubKey, solutions);
            
            if (script_type == TxoutType::WITNESS_V0_KEYHASH || 
                script_type == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH) {
                // For witness scripts, signature goes in witness field
                tx.vin[i].scriptWitness.stack.clear();
                tx.vin[i].scriptWitness.stack.push_back(vchSig);
                // scriptSig must be empty for witness transactions
                tx.vin[i].scriptSig = CScript();
            } else {
                // For non-witness scripts, signature goes in scriptSig
                CScript scriptSig;
                scriptSig << vchSig;
                tx.vin[i].scriptSig = scriptSig;
            }
        }
    }
    
    return complete;
}

RPCHelpMan getnewdilithiumaddress()
{
    return RPCHelpMan{"getnewdilithiumaddress",
        "\nReturns a new Dilithium address for receiving payments.\n"
        "If 'label' is specified, it is assigned to the default address.\n"
        "The keypool will be refilled (one address for each key in the keypool).\n"
        "You may need to call keypoolrefill first.\n",
        {
            {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label. The label does not need to exist, it will be created if there is no label by the given name."},
            {"address_type", RPCArg::Type::STR, RPCArg::Default{"bech32"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\"."},
        },
        RPCResult{
            RPCResult::Type::STR, "address", "The new dilithium address"
        },
        RPCExamples{
            HelpExampleCli("getnewdilithiumaddress", "")
            + HelpExampleCli("getnewdilithiumaddress", "\"\"")
            + HelpExampleCli("getnewdilithiumaddress", "\"myaccount\"")
            + HelpExampleRpc("getnewdilithiumaddress", "\"myaccount\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);

            // Parse the label first so we don't generate a key if there's an error
            std::string label;
            if (!request.params[0].isNull())
                label = LabelFromValue(request.params[0]);

            OutputType output_type = OutputType::BECH32;
            if (!request.params[1].isNull()) {
                std::optional<OutputType> parsed = ParseOutputType(request.params[1].get_str());
                if (!parsed) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[1].get_str()));
                }
                output_type = *parsed;
            }

            if (!wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium addresses are only available with descriptor wallets");
            }

            // Generate new Dilithium key
            LogPrintf("DEBUG: getnewdilithiumaddress - Starting key generation\n");
            CDilithiumKey dilithium_key;
            LogPrintf("DEBUG: getnewdilithiumaddress - Calling MakeNewKey()\n");
            dilithium_key.MakeNewKey();
            LogPrintf("DEBUG: getnewdilithiumaddress - MakeNewKey() completed\n");
            
            if (!dilithium_key.IsValid()) {
                LogPrintf("DEBUG: getnewdilithiumaddress - Dilithium key is not valid\n");
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to generate Dilithium key");
            }
            LogPrintf("DEBUG: getnewdilithiumaddress - Dilithium key is valid\n");

            // Get the public key
            LogPrintf("DEBUG: getnewdilithiumaddress - Getting public key\n");
            CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
            LogPrintf("DEBUG: getnewdilithiumaddress - Got public key\n");
            if (!dilithium_pubkey.IsValid()) {
                LogPrintf("DEBUG: getnewdilithiumaddress - Dilithium public key is not valid\n");
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get Dilithium public key");
            }
            LogPrintf("DEBUG: getnewdilithiumaddress - Dilithium public key is valid\n");

            // Create address based on output type
            LogPrintf("DEBUG: getnewdilithiumaddress - Creating address for output type: %d\n", static_cast<int>(output_type));
            std::string address;
            switch (output_type) {
                case OutputType::LEGACY: {
                    LogPrintf("DEBUG: getnewdilithiumaddress - Creating legacy address\n");
                    DilithiumPKHash dilithium_dest(dilithium_pubkey.GetID());
                    address = EncodeDestination(dilithium_dest);
                    LogPrintf("DEBUG: getnewdilithiumaddress - Legacy address created: %s\n", address.c_str());
                    break;
                }
                case OutputType::P2SH_SEGWIT: {
                    LogPrintf("DEBUG: getnewdilithiumaddress - Creating P2SH-SegWit address\n");
                    DilithiumPKHash dilithium_dest(dilithium_pubkey.GetID());
                    DilithiumScriptHash script_hash(GetScriptForDestination(dilithium_dest));
                    address = EncodeDestination(script_hash);
                    LogPrintf("DEBUG: getnewdilithiumaddress - P2SH-SegWit address created: %s\n", address.c_str());
                    break;
                }
                case OutputType::BECH32: {
                    LogPrintf("DEBUG: getnewdilithiumaddress - Creating Bech32 address\n");
                    DilithiumWitnessV0KeyHash witness_dest(dilithium_pubkey);
                    address = EncodeDestination(witness_dest);
                    LogPrintf("DEBUG: getnewdilithiumaddress - Bech32 address created: %s\n", address.c_str());
                    break;
                }
                default:
                    LogPrintf("DEBUG: getnewdilithiumaddress - Unsupported output type: %d\n", static_cast<int>(output_type));
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unsupported address type for Dilithium");
            }

            // Store the Dilithium key in the wallet
            LogPrintf("DEBUG: getnewdilithiumaddress - Starting key storage\n");
            if (wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS)) {
                LogPrintf("DEBUG: getnewdilithiumaddress - Storing in descriptor wallet\n");
                // Store in descriptor wallet
                bool stored = false;
                auto spk_mans = wallet->GetAllScriptPubKeyMans();
                LogPrintf("DEBUG: getnewdilithiumaddress - Found %zu script pub key managers\n", spk_mans.size());
                for (auto& spk_man : spk_mans) {
                    DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
                    if (desc_spk_man) {
                        LogPrintf("DEBUG: getnewdilithiumaddress - Found descriptor script pub key manager\n");
                        // Create a dummy CPubKey for compatibility (Dilithium keys don't use CPubKey)
                        CPubKey dummy_pubkey;
                        LogPrintf("DEBUG: getnewdilithiumaddress - Calling AddDilithiumKeyPubKey\n");
                        if (desc_spk_man->AddDilithiumKeyPubKey(dilithium_key, dummy_pubkey)) {
                            LogPrintf("DEBUG: getnewdilithiumaddress - Successfully stored Dilithium key\n");
                            stored = true;
                            break;
                        } else {
                            LogPrintf("DEBUG: getnewdilithiumaddress - Failed to store Dilithium key in descriptor wallet\n");
                        }
                    }
                }
                if (!stored) {
                    LogPrintf("DEBUG: getnewdilithiumaddress - No descriptor script pub key manager could store the key\n");
                    throw JSONRPCError(RPC_WALLET_ERROR, "Failed to store Dilithium key in descriptor wallet");
                }
            } else {
                LogPrintf("DEBUG: getnewdilithiumaddress - Storing in legacy wallet\n");
                // Store in legacy wallet
                LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
                if (!spk_man) {
                    LogPrintf("DEBUG: getnewdilithiumaddress - Legacy wallet not available\n");
                    throw JSONRPCError(RPC_WALLET_ERROR, "Legacy wallet not available");
                }
                LogPrintf("DEBUG: getnewdilithiumaddress - Calling AddDilithiumKeyPubKey on legacy wallet\n");
                if (!spk_man->AddDilithiumKeyPubKey(dilithium_key, CPubKey())) {
                    LogPrintf("DEBUG: getnewdilithiumaddress - Failed to store Dilithium key in legacy wallet\n");
                    throw JSONRPCError(RPC_WALLET_ERROR, "Failed to store Dilithium key in legacy wallet");
                }
                LogPrintf("DEBUG: getnewdilithiumaddress - Successfully stored Dilithium key in legacy wallet\n");
            }

            // Set the label
            if (!label.empty()) {
                LogPrintf("DEBUG: getnewdilithiumaddress - Setting address book label\n");
                wallet->SetAddressBook(DecodeDestination(address), label, AddressPurpose::RECEIVE);
            }

            LogPrintf("DEBUG: getnewdilithiumaddress - Returning address: %s\n", address.c_str());
            return address;
        },
    };
}

RPCHelpMan importdilithiumkey()
{
    return RPCHelpMan{"importdilithiumkey",
        "\nAdds a Dilithium private key (as returned by dumpprivkey) to your wallet.\n"
        "This creates a new Dilithium address for receiving payments.\n"
        "If 'label' is specified, it is assigned to the new address.\n",
        {
            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The Dilithium private key (see dumpprivkey)"},
            {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "An optional label"},
            {"rescan", RPCArg::Type::BOOL, RPCArg::Default{true}, "Rescan the wallet for transactions"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The Dilithium address if import was successful"},
            }
        },
        RPCExamples{
            HelpExampleCli("importdilithiumkey", "\"mykey\"")
            + HelpExampleCli("importdilithiumkey", "\"mykey\" \"testing\" false")
            + HelpExampleRpc("importdilithiumkey", "\"mykey\", \"testing\", false")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);

            if (!wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium keys are only available with descriptor wallets");
            }

            std::string strSecret = request.params[0].get_str();
            std::string strLabel = "";
            if (!request.params[1].isNull())
                strLabel = request.params[1].get_str();

            // Whether to perform rescan after import
            bool fRescan = true;
            if (!request.params[2].isNull())
                fRescan = request.params[2].get_bool();

            CDilithiumKey dilithium_key = DecodeDilithiumSecret(strSecret);
            if (!dilithium_key.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Dilithium private key");
            }

            // Get the public key and create address
            CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
            DilithiumPKHash dilithium_dest(dilithium_pubkey.GetID());
            std::string address = EncodeDestination(dilithium_dest);

            // Set the label
            if (!strLabel.empty()) {
                wallet->SetAddressBook(DecodeDestination(address), strLabel, AddressPurpose::RECEIVE);
            }

            // Add the Dilithium key to the wallet's key store
            // Convert Dilithium public key to CPubKey for compatibility
            CPubKey pubkey(dilithium_pubkey.begin(), dilithium_pubkey.end());
            
            // Get the ScriptPubKeyMan for key management
            LegacyScriptPubKeyMan* spk_man = wallet->GetLegacyScriptPubKeyMan();
            if (!spk_man) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Legacy wallet not available");
            }
            
            // Add the Dilithium key to the wallet
            if (!spk_man->AddDilithiumKeyPubKey(dilithium_key, pubkey)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to add Dilithium key to wallet");
            }

            if (fRescan) {
                // TODO: Trigger rescan of the blockchain
                // This would require extending the wallet's rescan functionality
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", address);
            return result;
        },
    };
}

RPCHelpMan signmessagewithdilithium()
{
    return RPCHelpMan{"signmessagewithdilithium",
        "\nSign a message with a Dilithium private key.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Dilithium address to use for signing."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message to create a signature of."},
        },
        RPCResult{
            RPCResult::Type::STR, "signature", "The signature of the message encoded in base 64"
        },
        RPCExamples{
            HelpExampleCli("signmessagewithdilithium", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"")
            + HelpExampleRpc("signmessagewithdilithium", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"my message\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);

            std::string strAddress = request.params[0].get_str();
            std::string strMessage = request.params[1].get_str();

            CTxDestination dest = DecodeDestination(strAddress);
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }

            // Check if this is a Dilithium address
            CKeyID keyID;
            if (std::holds_alternative<DilithiumPKHash>(dest)) {
                DilithiumPKHash dilithium_dest = std::get<DilithiumPKHash>(dest);
                keyID = CKeyID(static_cast<uint160>(dilithium_dest));
            } else if (std::holds_alternative<DilithiumWitnessV0KeyHash>(dest)) {
                // For witness addresses, we need to get the underlying key hash
                DilithiumWitnessV0KeyHash witness_dest = std::get<DilithiumWitnessV0KeyHash>(dest);
                keyID = CKeyID(static_cast<uint160>(witness_dest));
            } else {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address is not a Dilithium address");
            }
            
            // Get the Dilithium private key from the wallet
            CDilithiumKey dilithium_key;
            
            bool key_found = false;
    // Try all script pub key managers (both legacy and descriptor)
    auto spk_mans = wallet->GetAllScriptPubKeyMans();
    LogPrintf("DEBUG: Message signing - Found %zu script pub key managers\n", spk_mans.size());
    LogPrintf("DEBUG: Message signing - Looking for keyID: %s\n", keyID.ToString());
    for (auto& spk_man : spk_mans) {
        // Try descriptor wallet first
        DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
        if (desc_spk_man) {
            LogPrintf("DEBUG: Message signing - Checking descriptor script pub key manager\n");
            if (desc_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                LogPrintf("DEBUG: Message signing - Found Dilithium key in descriptor wallet\n");
                key_found = true;
                break;
            } else {
                LogPrintf("DEBUG: Message signing - Dilithium key not found in descriptor wallet\n");
            }
        }
        // Try legacy wallet
        LegacyScriptPubKeyMan* legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man);
        if (legacy_spk_man) {
            LogPrintf("DEBUG: Message signing - Checking legacy script pub key manager\n");
            if (legacy_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                LogPrintf("DEBUG: Message signing - Found Dilithium key in legacy wallet\n");
                key_found = true;
                break;
            } else {
                LogPrintf("DEBUG: Message signing - Dilithium key not found in legacy wallet\n");
            }
        }
    }
            
            if (!key_found) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium key not found in wallet");
            }
            
            // Sign the message
            std::vector<unsigned char> vchSig;
            std::vector<unsigned char> messageBytes(strMessage.begin(), strMessage.end());
            if (!dilithium_key.SignMessage(messageBytes, vchSig)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign message");
            }
            
            return EncodeBase64(vchSig);
        },
    };
}

RPCHelpMan verifydilithiumsignature()
{
    return RPCHelpMan{"verifydilithiumsignature",
        "\nVerify a Dilithium signature.\n",
        {
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message that was signed."},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Dilithium address that signed the message."},
            {"signature", RPCArg::Type::STR, RPCArg::Optional::NO, "The signature to verify (base64 encoded)."},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "If the signature is verified or not."
        },
        RPCExamples{
            HelpExampleCli("verifydilithiumsignature", "\"hello world\" \"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\"")
            + HelpExampleRpc("verifydilithiumsignature", "\"hello world\", \"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\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::string strMessage = request.params[0].get_str();
            std::string strAddress = request.params[1].get_str();
            std::string strSignature = request.params[2].get_str();

            // Get the wallet to look up the Dilithium key
            std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            LOCK(pwallet->cs_wallet);

            // Decode the address to get the key ID
            CTxDestination dest = DecodeDestination(strAddress);
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }

            // Get the key ID from the address
            CKeyID keyID;
            if (std::holds_alternative<DilithiumPKHash>(dest)) {
                DilithiumPKHash dilithium_dest = std::get<DilithiumPKHash>(dest);
                keyID = CKeyID(static_cast<uint160>(dilithium_dest));
            } else if (std::holds_alternative<DilithiumWitnessV0KeyHash>(dest)) {
                DilithiumWitnessV0KeyHash witness_dest = std::get<DilithiumWitnessV0KeyHash>(dest);
                keyID = CKeyID(static_cast<uint160>(witness_dest));
            } else {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address is not a Dilithium address");
            }

            // Look up the Dilithium key in the wallet
            CDilithiumKey dilithium_key;
            bool key_found = false;
            
            // Try all script pub key managers (both legacy and descriptor)
            auto spk_mans = pwallet->GetAllScriptPubKeyMans();
            for (auto& spk_man : spk_mans) {
                // Try descriptor wallet first
                DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
                if (desc_spk_man) {
                    if (desc_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                        key_found = true;
                        break;
                    }
                }
                // Try legacy wallet
                LegacyScriptPubKeyMan* legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man);
                if (legacy_spk_man) {
                    if (legacy_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                        key_found = true;
                        break;
                    }
                }
            }
            
            if (!key_found) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium key not found in wallet for this address");
            }

            // Get the public key from the Dilithium key
            CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
            if (!dilithium_pubkey.IsValid()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Invalid Dilithium public key");
            }

            // Decode the signature
            auto vchSig_opt = DecodeBase64(strSignature);
            if (!vchSig_opt) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid signature encoding");
            }
            std::vector<unsigned char> vchSig = *vchSig_opt;

            // Convert message to bytes (same as signing)
            std::vector<unsigned char> messageBytes(strMessage.begin(), strMessage.end());

            // Verify the signature using the Dilithium verification function
            bool result = dilithium_pubkey.VerifyMessage(messageBytes, vchSig);
            
            return result;
        },
    };
}

RPCHelpMan signtransactionwithdilithium()
{
    return RPCHelpMan{"signtransactionwithdilithium",
        "\nSign inputs for raw transaction using Dilithium keys (serialized, hex-encoded).\n"
        "The second optional argument (may be null) is an array of previous transaction outputs that\n"
        "this transaction depends on but may not yet be in the block chain." +
        HELP_REQUIRING_PASSPHRASE,
        {
            {"hexstring", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction hex string"},
            {"prevtxs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED, "The previous dependent transaction outputs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                            {"scriptPubKey", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The script key"},
                            {"redeemScript", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "(required for P2SH) The redeem script"},
                            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount spent"},
                        },
                    },
                },
            },
            {"sighashtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The signature hash type. Must be one of\n"
                "       \"ALL\"\n"
                "       \"NONE\"\n"
                "       \"SINGLE\"\n"
                "       \"ALL|ANYONECANPAY\"\n"
                "       \"NONE|ANYONECANPAY\"\n"
                "       \"SINGLE|ANYONECANPAY\"\n"
                "If not specified, defaults to ALL"},
            {"force_dilithium", RPCArg::Type::BOOL, RPCArg::Default{true}, "Force all inputs to be treated as Dilithium scripts"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hex", "The hex-encoded raw transaction with signature(s)"},
                {RPCResult::Type::BOOL, "complete", "If the transaction has a complete set of signatures"},
                {RPCResult::Type::ARR, "errors", "Script verification errors (if there are any)",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "txid", "The hash of the referenced, previous transaction"},
                                {RPCResult::Type::NUM, "vout", "The index of the output to spent and used as input"},
                                {RPCResult::Type::STR_HEX, "scriptSig", "The hex-encoded signature script"},
                                {RPCResult::Type::NUM, "sequence", "Script sequence number"},
                                {RPCResult::Type::STR, "error", "Verification or signing error related to the input"},
                            },
                        },
                    },
                },
            },
        },
        RPCExamples{
            HelpExampleCli("signtransactionwithdilithium", "\"myhex\"")
            + HelpExampleRpc("signtransactionwithdilithium", "\"myhex\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str())) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed. Make sure the tx has at least one input.");
            }

            // Sign the transaction
            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);

            // Fetch previous transactions (inputs):
            std::map<COutPoint, Coin> coins;
            for (const CTxIn& txin : mtx.vin) {
                coins[txin.prevout]; // Create empty map entry keyed by prevout.
            }
            pwallet->chain().findCoins(coins);

            // Parse the prevtxs array
            ParsePrevouts(request.params[1], nullptr, coins);

            int nHashType = ParseSighashString(request.params[2]);
            bool force_dilithium = request.params[3].isNull() ? true : request.params[3].get_bool();

            // Script verification errors
            std::map<int, bilingual_str> input_errors;

            // For Dilithium transactions, we need to use a custom signing method
            // that specifically handles Dilithium keys
            bool complete = SignTransactionWithDilithium(*pwallet, mtx, coins, nHashType, input_errors, force_dilithium);
            UniValue result(UniValue::VOBJ);
            SignTransactionResultToJSON(mtx, complete, coins, input_errors, result);
            return result;
        },
    };
}

} // namespace wallet

