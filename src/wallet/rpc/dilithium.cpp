// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/dilithium.h>

#include <outputtype.h>
#include <wallet/p2mr.h>
#include <wallet/rpc/util.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <crypto/dilithium_key.h>
#include <key_io.h>
#include <script/dilithium_leaf.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/sign.h>
#include <script/interpreter.h>
#include <core_io.h>
#include <rpc/rawtransaction_util.h>
#include <util/message.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <univalue.h>

namespace wallet {

RPCHelpMan getnewdilithiumaddress()
{
    return RPCHelpMan{"getnewdilithiumaddress",
        "\nReturns a new Dilithium-capable P2MR (witness v2) address for receiving payments.\n"
        "Dilithium opcodes are consensus-valid only inside P2MR tapscript leaves; legacy\n"
        "Dilithium P2PKH / witness-v0 destinations are no longer created.\n"
        "If 'label' is specified, it is assigned to the address.\n",
        {
            {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The label name for the address to be linked to. If set to the empty string \"\", it represents the default label."},
            {"address_type", RPCArg::Type::STR, RPCArg::Default{"p2mr"}, "Must be \"p2mr\". Legacy Dilithium address types are disabled."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The new P2MR Dilithium receive address"},
                {RPCResult::Type::STR, "p2mr_id", "Wallet-local P2MR metadata id"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
            }
        },
        RPCExamples{
            HelpExampleCli("getnewdilithiumaddress", "")
            + HelpExampleCli("getnewdilithiumaddress", "\"receiving\"")
            + HelpExampleRpc("getnewdilithiumaddress", "\"receiving\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);

            std::string label;
            if (!request.params[0].isNull())
                label = LabelFromValue(request.params[0]);

            if (!request.params[1].isNull()) {
                const std::string address_type = request.params[1].get_str();
                if (address_type != "p2mr") {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                        strprintf("Unsupported Dilithium address type '%s'. Dilithium receives must use P2MR (\"p2mr\").", address_type));
                }
            }

            auto created = CreateDilithiumP2MRReceive(*wallet, label);
            if (!created) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", created->address);
            result.pushKV("p2mr_id", created->id);
            result.pushKV("scriptPubKey", HexStr(created->script_pub_key));
            result.pushKV("merkle_root", HexStr(created->merkle_root));
            return result;
        },
    };
}

RPCHelpMan importdilithiumkey()
{
    return RPCHelpMan{"importdilithiumkey",
        "\nAdds a Dilithium private key (as returned by dumpprivkey) to your wallet and\n"
        "creates a matching single-leaf P2MR receive destination for it.\n"
        "If 'label' is specified, it is assigned to the new address.\n",
        {
            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The Dilithium private key (see dumpprivkey)"},
            {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "An optional label"},
            {"rescan", RPCArg::Type::BOOL, RPCArg::Default{true}, "Rescan the wallet for transactions"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The P2MR Dilithium address if import was successful"},
                {RPCResult::Type::STR, "p2mr_id", "Wallet-local P2MR metadata id"},
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

            const std::string strSecret = request.params[0].get_str();
            std::string strLabel;
            if (!request.params[1].isNull())
                strLabel = request.params[1].get_str();

            bool fRescan = true;
            if (!request.params[2].isNull())
                fRescan = request.params[2].get_bool();

            CDilithiumKey dilithium_key = DecodeDilithiumSecret(strSecret);
            if (!dilithium_key.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Dilithium private key");
            }

            auto created = ImportDilithiumKeyAsP2MR(*wallet, dilithium_key, strLabel);
            if (!created) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
            }

            if (fRescan) {
                // TODO: Trigger rescan of the blockchain
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", created->address);
            result.pushKV("p2mr_id", created->id);
            return result;
        },
    };
}

RPCHelpMan signmessagewithdilithium()
{
    return RPCHelpMan{"signmessagewithdilithium",
        "\nSign a message with a Dilithium private key.\n"
        "\nThe signature covers a domain-separated hash of the message, not the message\n"
        "bytes, so it can never be a valid transaction signature for the same key.\n"
        "Signatures produced before this separation existed no longer verify.\n",
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

            EnsureWalletIsUnlocked(*wallet);

            std::string strAddress = request.params[0].get_str();
            std::string strMessage = request.params[1].get_str();

            CTxDestination dest = DecodeDestination(strAddress);
            // Prefer P2MR Dilithium receive addresses; historical DilithiumPKHash
            // destinations remain decodable for message signing even though they
            // are no longer valid payment destinations.
            CKeyID keyID;
            if (auto p2mr_key = GetSingleDilithiumKeyIDForP2MR(*wallet, dest)) {
                keyID = *p2mr_key;
            } else if (std::holds_alternative<DilithiumPKHash>(dest)) {
                DilithiumPKHash dilithium_dest = std::get<DilithiumPKHash>(dest);
                keyID = CKeyID(static_cast<uint160>(dilithium_dest));
            } else if (std::holds_alternative<DilithiumWitnessV0KeyHash>(dest)) {
                // For witness addresses, we need to get the underlying key hash
                DilithiumWitnessV0KeyHash witness_dest = std::get<DilithiumWitnessV0KeyHash>(dest);
                keyID = CKeyID(static_cast<uint160>(witness_dest));
            } else {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address is not a Dilithium key address (use a Dilithium P2MR receive address, or a historical DilithiumPKHash)");
            }
            
            // Get the Dilithium private key from the wallet
            CDilithiumKey dilithium_key;
            
            bool key_found = false;
    auto spk_mans = wallet->GetAllScriptPubKeyMans();
    for (auto& spk_man : spk_mans) {
        DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
        if (desc_spk_man) {
            if (desc_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                key_found = true;
                break;
            }
        }
        LegacyScriptPubKeyMan* legacy_spk_man = dynamic_cast<LegacyScriptPubKeyMan*>(spk_man);
        if (legacy_spk_man) {
            if (legacy_spk_man->GetDilithiumKey(keyID, dilithium_key)) {
                key_found = true;
                break;
            }
        }
    }
            
            if (!key_found) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium key not found in wallet");
            }
            
            // Domain-separated: signing a message must not be able to produce a
            // signature that spends the same key's coins. See util/message.h.
            std::string signature;
            if (!DilithiumMessageSign(dilithium_key, strMessage, signature)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign message");
            }

            return signature;
        },
    };
}

/**
 * Shared body for the Dilithium message-verification RPCs.
 *
 * verifymessagewithdilithium takes (address, signature, message), mirroring the
 * inherited verifymessage. The deprecated verifydilithiumsignature takes
 * (message, address, signature). `usage` names the caller's own argument order
 * so a mis-ordered call gets told how to fix itself rather than just "not a
 * Dilithium address".
 */
static UniValue DilithiumVerifyMessage(const JSONRPCRequest& request,
                                       const std::string& strAddress,
                                       const std::string& strSignature,
                                       const std::string& strMessage,
                                       const char* usage)
{
    // Get the wallet to look up the Dilithium key
    std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    // Decode the address to get the key ID
    CTxDestination dest = DecodeDestination(strAddress);
    // Prefer P2MR Dilithium receive addresses; historical DilithiumPKHash
    // destinations remain decodable for message verification even though
    // they are no longer valid payment destinations.
    CKeyID keyID;
    if (auto p2mr_key = GetSingleDilithiumKeyIDForP2MR(*pwallet, dest)) {
        keyID = *p2mr_key;
    } else if (std::holds_alternative<DilithiumPKHash>(dest)) {
        DilithiumPKHash dilithium_dest = std::get<DilithiumPKHash>(dest);
        keyID = CKeyID(static_cast<uint160>(dilithium_dest));
    } else if (std::holds_alternative<DilithiumWitnessV0KeyHash>(dest)) {
        DilithiumWitnessV0KeyHash witness_dest = std::get<DilithiumWitnessV0KeyHash>(dest);
        keyID = CKeyID(static_cast<uint160>(witness_dest));
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
            strprintf("Address is not a Dilithium key address (use a Dilithium P2MR receive "
                      "address, or a historical DilithiumPKHash). Expected argument order: %s", usage));
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

    if (!DecodeBase64(strSignature)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid signature encoding");
    }

    // Paired with DilithiumMessageSign, so the domain the signature is
    // checked against is by construction the one it was made in.
    return DilithiumMessageVerify(dilithium_pubkey, strMessage, strSignature);
}

RPCHelpMan verifydilithiumsignature()
{
    return RPCHelpMan{"verifydilithiumsignature",
        "\nVerify a Dilithium signature produced by signmessagewithdilithium.\n"
        "\nOnly message signatures verify here: a transaction signature made with the\n"
        "same key belongs to a different domain and is rejected.\n"
        "\nDEPRECATED: use verifymessagewithdilithium, whose argument order mirrors\n"
        "verifymessage (address, signature, message). This form takes them in a\n"
        "different order and will be removed in a future release.\n",
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
            return DilithiumVerifyMessage(request,
                                          /*strAddress=*/request.params[1].get_str(),
                                          /*strSignature=*/request.params[2].get_str(),
                                          /*strMessage=*/request.params[0].get_str(),
                                          "verifydilithiumsignature \"message\" \"address\" \"signature\"");
        },
    };
}

RPCHelpMan verifymessagewithdilithium()
{
    return RPCHelpMan{"verifymessagewithdilithium",
        "\nVerify a message signed with a Dilithium private key.\n"
        "Argument order mirrors verifymessage. Replaces verifydilithiumsignature,\n"
        "which took the same arguments in a different order and is deprecated.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Dilithium address that signed the message."},
            {"signature", RPCArg::Type::STR, RPCArg::Optional::NO, "The signature provided by the signer in base 64 encoding (see signmessagewithdilithium)."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message that was signed."},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "If the signature is verified or not."
        },
        RPCExamples{
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessagewithdilithium", "\"myaddress\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessagewithdilithium", "\"myaddress\" \"signature\" \"my message\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("verifymessagewithdilithium", "\"myaddress\", \"signature\", \"my message\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            return DilithiumVerifyMessage(request,
                                          /*strAddress=*/request.params[0].get_str(),
                                          /*strSignature=*/request.params[1].get_str(),
                                          /*strMessage=*/request.params[2].get_str(),
                                          "verifymessagewithdilithium \"address\" \"signature\" \"message\"");
        },
    };
}

RPCHelpMan getdilithiumpubkey()
{
    return RPCHelpMan{"getdilithiumpubkey",
        "\nReturn the Dilithium public keys named by a wallet P2MR destination.\n"
        "Share the pubkey of a single-key destination with your co-signers to build\n"
        "a multisig with createdilithiummultisig.\n",
        {
            {"p2mr_id", RPCArg::Type::STR, RPCArg::Optional::NO, "P2MR metadata id, as returned by getnewdilithiumaddress"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The P2MR address"},
                {RPCResult::Type::ARR, "pubkeys", "Dilithium public keys in leaf order",
                {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "pubkey", "The Dilithium public key"},
                        {RPCResult::Type::BOOL, "ismine", "Whether this wallet holds the private key"},
                    }},
                }},
            }
        },
        RPCExamples{HelpExampleCli("getdilithiumpubkey", "\"abcd1234\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);
            auto entry = GetP2MR(*wallet, request.params[0].get_str());
            if (!entry) throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown p2mr_id");

            UniValue pubkeys(UniValue::VARR);
            for (const P2MRTreeLeaf& leaf : entry->tree) {
                const CScript script{leaf.script.begin(), leaf.script.end()};
                const P2MRDilithiumLeafPolicy policy = ParseP2MRDilithiumLeaf(script);
                for (const CDilithiumPubKey& pubkey : policy.pubkeys) {
                    CKeyID keyid;
                    const uint160 id = pubkey.GetID();
                    std::copy(id.begin(), id.end(), keyid.begin());
                    UniValue obj(UniValue::VOBJ);
                    obj.pushKV("pubkey", HexStr(pubkey));
                    obj.pushKV("ismine", WalletHaveDilithiumKey(*wallet, keyid));
                    pubkeys.push_back(std::move(obj));
                }
            }
            if (pubkeys.empty()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "destination names no Dilithium public keys");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", entry->address);
            result.pushKV("pubkeys", std::move(pubkeys));
            return result;
        },
    };
}

RPCHelpMan createdilithiummultisig()
{
    return RPCHelpMan{"createdilithiummultisig",
        "\nRegister an m-of-n Dilithium multisig P2MR destination in this wallet.\n"
        "\nEvery co-signer must call this with the same m and the same pubkey list in the\n"
        "same order; they then all derive the same address. Funds sent to it are spent by\n"
        "passing a PSBT between the co-signers with walletprocesspsbt until m signatures\n"
        "have accumulated.\n",
        {
            {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "Number of signatures required"},
            {"pubkeys", RPCArg::Type::ARR, RPCArg::Optional::NO, "Dilithium public keys, hex encoded",
                {
                    {"pubkey", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "A Dilithium public key"},
                },
            },
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The multisig P2MR address"},
                {RPCResult::Type::STR, "p2mr_id", "Wallet-local P2MR metadata id"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
                {RPCResult::Type::STR_HEX, "leaf_script", "The Dilithium threshold leaf script"},
                {RPCResult::Type::NUM, "signers_available", "How many of the n keys this wallet can sign with"},
            }
        },
        RPCExamples{
            HelpExampleCli("createdilithiummultisig", "2 '[\"pubkeyhex1\",\"pubkeyhex2\",\"pubkeyhex3\"]'")
            + HelpExampleRpc("createdilithiummultisig", "2, [\"pubkeyhex1\",\"pubkeyhex2\",\"pubkeyhex3\"]")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            const int required = request.params[0].getInt<int>();
            const UniValue& keys = request.params[1].get_array();
            const std::string label = request.params[2].isNull() ? "" : LabelFromValue(request.params[2]);

            if (keys.empty()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "pubkeys must not be empty");
            }
            if (keys.size() > MAX_PUBKEYS_PER_MULTISIG) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("Number of keys involved in the multisignature address creation > %d", MAX_PUBKEYS_PER_MULTISIG));
            }
            if (required < 1 || required > (int)keys.size()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("wrong number of keys, nrequired must be between 1 and %d", (int)keys.size()));
            }

            std::vector<CDilithiumPubKey> pubkeys;
            for (size_t i = 0; i < keys.size(); ++i) {
                const auto bytes = TryParseHex<unsigned char>(keys[i].get_str());
                if (!bytes || bytes->size() != CDilithiumPubKey::SIZE) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                        strprintf("pubkey %d is not a %d-byte hex Dilithium public key", (int)i, (int)CDilithiumPubKey::SIZE));
                }
                const CDilithiumPubKey pubkey{*bytes};
                if (!pubkey.IsFullyValid()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("pubkey %d is not a valid Dilithium public key", (int)i));
                }
                // Duplicates would silently lower the effective threshold, since
                // one private key could then fill several accumulator slots.
                if (std::find(pubkeys.begin(), pubkeys.end(), pubkey) != pubkeys.end()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("pubkey %d is a duplicate", (int)i));
                }
                pubkeys.push_back(pubkey);
            }

            const CScript leaf_script = GetScriptForDilithiumThreshold(required, pubkeys);
            P2MRTreeLeaf leaf;
            leaf.depth = 0;
            leaf.leaf_version = TAPROOT_LEAF_TAPSCRIPT;
            leaf.script.assign(leaf_script.begin(), leaf_script.end());

            LOCK(wallet->cs_wallet);
            auto created = CreateP2MR(*wallet, {leaf}, label);
            if (!created) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
            }

            int available = 0;
            for (const CDilithiumPubKey& pubkey : pubkeys) {
                CKeyID keyid;
                const uint160 id = pubkey.GetID();
                std::copy(id.begin(), id.end(), keyid.begin());
                if (WalletHaveDilithiumKey(*wallet, keyid)) ++available;
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", created->address);
            result.pushKV("p2mr_id", created->id);
            result.pushKV("scriptPubKey", HexStr(created->script_pub_key));
            result.pushKV("merkle_root", HexStr(created->merkle_root));
            result.pushKV("leaf_script", HexStr(leaf_script));
            result.pushKV("signers_available", available);
            return result;
        },
    };
}

RPCHelpMan signtransactionwithdilithium()
{
    return RPCHelpMan{"signtransactionwithdilithium",
        "\nDEPRECATED: legacy Dilithium BASE/witness-v0 signing is consensus-invalid.\n"
        "Use signp2mrtransaction for P2MR Dilithium spends.\n",
        {
            {"hexstring", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction hex string"},
            {"prevtxs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED, "Ignored",
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
            {"sighashtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Ignored"},
            {"force_dilithium", RPCArg::Type::BOOL, RPCArg::Default{true}, "Ignored"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hex", "Unused"},
                {RPCResult::Type::BOOL, "complete", "Unused"},
            }
        },
        RPCExamples{
            HelpExampleCli("signp2mrtransaction", "\"rawhex\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            throw JSONRPCError(RPC_METHOD_DEPRECATED,
                "signtransactionwithdilithium is disabled: Dilithium opcodes are consensus-valid only in P2MR tapscript. Use signp2mrtransaction.");
        },
    };
}

} // namespace wallet
