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

static RPCHelpMan getnewdilithiumaddress()
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
            CDilithiumKey dilithium_key;
            dilithium_key.MakeNewKey();
            
            if (!dilithium_key.IsValid()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to generate Dilithium key");
            }

            // Get the public key
            CDilithiumPubKey dilithium_pubkey = dilithium_key.GetPubKey();
            if (!dilithium_pubkey.IsValid()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get Dilithium public key");
            }

            // Create address based on output type
            std::string address;
            switch (output_type) {
                case OutputType::LEGACY: {
                    DilithiumPKHash dilithium_dest(dilithium_pubkey.GetID());
                    address = EncodeDestination(dilithium_dest);
                    break;
                }
                case OutputType::P2SH_SEGWIT: {
                    DilithiumPKHash dilithium_dest(dilithium_pubkey.GetID());
                    DilithiumScriptHash script_hash(GetScriptForDestination(dilithium_dest));
                    address = EncodeDestination(script_hash);
                    break;
                }
                case OutputType::BECH32: {
                    DilithiumWitnessV0KeyHash witness_dest(dilithium_pubkey);
                    address = EncodeDestination(witness_dest);
                    break;
                }
                default:
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unsupported address type for Dilithium");
            }

            // Set the label
            if (!label.empty()) {
                wallet->SetAddressBook(DecodeDestination(address), label, AddressPurpose::RECEIVE);
            }

            return address;
        },
    };
}

static RPCHelpMan importdilithiumkey()
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

            // TODO: Add the key to the wallet's key store
            // This would require extending the wallet's key management system

            if (fRescan) {
                // TODO: Trigger rescan of the blockchain
                // This would require extending the wallet's rescan functionality
            }

            return UniValue::VOBJ;
        },
    };
}

static RPCHelpMan signmessagewithdilithium()
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
            if (std::holds_alternative<DilithiumPKHash>(dest) || 
                std::holds_alternative<DilithiumScriptHash>(dest) ||
                std::holds_alternative<DilithiumWitnessV0KeyHash>(dest) ||
                std::holds_alternative<DilithiumWitnessV0ScriptHash>(dest)) {
                
                // TODO: Get the Dilithium private key from the wallet
                // This would require extending the wallet's key management system
                
                // For now, return a placeholder
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium key management not yet implemented");
            } else {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address is not a Dilithium address");
            }
        },
    };
}

} // namespace wallet

void RegisterDilithiumRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[] =
    {
        // Dilithium-specific RPCs
        {"wallet", &wallet::getnewdilithiumaddress},
        {"wallet", &wallet::importdilithiumkey},
        {"wallet", &wallet::signmessagewithdilithium},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
