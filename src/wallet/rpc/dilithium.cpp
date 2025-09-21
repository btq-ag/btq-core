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
#include <util/strencodings.h>
#include <util/string.h>

#include <univalue.h>

namespace wallet {

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

} // namespace wallet

