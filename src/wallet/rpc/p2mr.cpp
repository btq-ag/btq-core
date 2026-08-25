// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/script.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/p2mr.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

namespace wallet {

namespace {
UniValue EntryToJSON(const P2MREntry& entry)
{
    UniValue meta(UniValue::VOBJ);
    meta.pushKV("id", entry.id);
    meta.pushKV("address", entry.address);
    meta.pushKV("scriptPubKey", HexStr(entry.script_pub_key));
    meta.pushKV("merkle_root", HexStr(entry.merkle_root));
    meta.pushKV("created_at", entry.created_at);
    meta.pushKV("label", entry.label);
    meta.pushKV("state", entry.state);
    meta.pushKV("tree", P2MRTreeToUniValue(entry.tree));
    meta.pushKV("wallet_address", EncodeDestination(entry.dest));
    return meta;
}
} // namespace

RPCHelpMan getnewp2mraddress()
{
    return RPCHelpMan{
        "getnewp2mraddress",
        "\nCreate and store a new wallet-managed P2MR destination.\n"
        "The normal Dilithium receive path is getnewdilithiumaddress. This RPC is for custom trees.\n"
        "A leaf whose script is empty or OP_TRUE (hex 51) is anyone-can-spend. Those trees are\n"
        "rejected unless allow_trivial_leaves is true (regtest and tests only).\n",
        {
            {"tree", RPCArg::Type::ARR, RPCArg::Optional::NO, "P2MR tree leaves in DFS order", std::vector<RPCArg>{}, RPCArgOptions{}},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
            {"allow_trivial_leaves", RPCArg::Type::BOOL, RPCArg::Default{false}, "Allow empty or OP_TRUE leaves (anyone-can-spend). Tests only."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "Generated P2MR address"},
                {RPCResult::Type::STR, "p2mr_id", "Wallet-local metadata id"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
            }},
        RPCExamples{
            HelpExampleCli("getnewdilithiumaddress", "")
            + HelpExampleCli("getnewp2mraddress", "'[{\"depth\":0,\"leaf_version\":192,\"script\":\"51\"}]' \"\" true")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const auto leaves = ParseP2MRTreeFromUniValue(request.params[0]);
            const std::string label = request.params[1].isNull() ? "" : LabelFromValue(request.params[1]);
            const bool allow_trivial = !request.params[2].isNull() && request.params[2].get_bool();

            LOCK(pwallet->cs_wallet);
            auto created = CreateP2MR(*pwallet, leaves, label, /*add_to_address_book=*/true, allow_trivial);
            if (!created) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("address", created->address);
            out.pushKV("p2mr_id", created->id);
            out.pushKV("scriptPubKey", HexStr(created->script_pub_key));
            out.pushKV("merkle_root", HexStr(created->merkle_root));
            return out;
        },
    };
}

RPCHelpMan sendtop2mr()
{
    return RPCHelpMan{
        "sendtop2mr",
        "\nCreate a wallet-tracked P2MR destination and send funds to it.\n"
        "Trivial anyone-can-spend leaves (empty or OP_TRUE / hex 51) are rejected unless\n"
        "allow_trivial_leaves is true. Prefer getnewdilithiumaddress for ordinary receive.\n",
        {
            {"tree", RPCArg::Type::ARR, RPCArg::Optional::NO, "P2MR tree leaves in DFS order", std::vector<RPCArg>{}, RPCArgOptions{}},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send"},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
            {"comment", RPCArg::Type::STR, RPCArg::Default{""}, "Wallet comment"},
            {"comment_to", RPCArg::Type::STR, RPCArg::Default{""}, "Wallet comment-to"},
            {"subtractfeefromamount", RPCArg::Type::BOOL, RPCArg::Default{false}, "Subtract fee from amount"},
            {"allow_trivial_leaves", RPCArg::Type::BOOL, RPCArg::Default{false}, "Allow empty or OP_TRUE leaves (anyone-can-spend). Tests only."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::STR_HEX, "txid", "Funding transaction id"},
            {RPCResult::Type::STR, "address", "P2MR destination"},
            {RPCResult::Type::STR, "p2mr_id", "Stored metadata id"},
        }},
        RPCExamples{HelpExampleCli("sendtop2mr", "'[{\"depth\":0,\"leaf_version\":192,\"script\":\"51\"}]' 1.0 \"\" \"\" \"\" false true")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const auto leaves = ParseP2MRTreeFromUniValue(request.params[0]);
            const CAmount amount = AmountFromValue(request.params[1]);
            const std::string label = request.params[2].isNull() ? "" : LabelFromValue(request.params[2]);
            const bool subtract_fee = request.params[5].isNull() ? false : request.params[5].get_bool();
            const bool allow_trivial = !request.params[6].isNull() && request.params[6].get_bool();

            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);

            CCoinControl coin_control;
            auto funded = FundP2MR(*pwallet, leaves, amount, label, subtract_fee, coin_control, allow_trivial);
            if (!funded) {
                const std::string msg = util::ErrorString(funded).original;
                if (msg.find("trivial anyone-can-spend") != std::string::npos) {
                    throw JSONRPCError(RPC_WALLET_ERROR, msg);
                }
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, msg);
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("txid", funded->txid.GetHex());
            out.pushKV("address", funded->created.address);
            out.pushKV("p2mr_id", funded->created.id);
            return out;
        },
    };
}

RPCHelpMan listp2mr()
{
    return RPCHelpMan{
        "listp2mr",
        "\nList wallet P2MR metadata entries.\n",
        {},
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {{
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR, "id", "Wallet-local metadata id"},
                    {RPCResult::Type::STR, "address", "P2MR address"},
                    {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                    {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
                    {RPCResult::Type::NUM, "created_at", "Creation UNIX timestamp"},
                    {RPCResult::Type::STR, "label", "Address label"},
                    {RPCResult::Type::STR, "state", "Metadata state"},
                    {RPCResult::Type::ARR, "tree", "Tree leaves in DFS order", {
                        {RPCResult::Type::OBJ, "", "", {
                            {RPCResult::Type::NUM, "depth", "Leaf depth"},
                            {RPCResult::Type::NUM, "leaf_version", "Leaf version"},
                            {RPCResult::Type::STR_HEX, "script", "Leaf script hex"},
                        }},
                    }},
                    {RPCResult::Type::STR, "wallet_address", "Wallet destination string"},
                }
            }}
        },
        RPCExamples{HelpExampleCli("listp2mr", "")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            LOCK(pwallet->cs_wallet);
            UniValue out(UniValue::VARR);
            for (const auto& entry : ListP2MR(*pwallet)) {
                out.push_back(EntryToJSON(entry));
            }
            return out;
        },
    };
}

RPCHelpMan getp2mrinfo()
{
    return RPCHelpMan{
        "getp2mrinfo",
        "\nGet one P2MR metadata entry by id.\n",
        {{"p2mr_id", RPCArg::Type::STR, RPCArg::Optional::NO, "P2MR metadata id"}},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "id", "Wallet-local metadata id"},
                {RPCResult::Type::STR, "address", "P2MR address"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
                {RPCResult::Type::NUM, "created_at", "Creation UNIX timestamp"},
                {RPCResult::Type::STR, "label", "Address label"},
                {RPCResult::Type::STR, "state", "Metadata state"},
                {RPCResult::Type::ARR, "tree", "Tree leaves in DFS order", {
                    {RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "depth", "Leaf depth"},
                        {RPCResult::Type::NUM, "leaf_version", "Leaf version"},
                        {RPCResult::Type::STR_HEX, "script", "Leaf script hex"},
                    }},
                }},
                {RPCResult::Type::STR, "wallet_address", "Wallet destination string"},
            }
        },
        RPCExamples{HelpExampleCli("getp2mrinfo", "\"abcd1234\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            LOCK(pwallet->cs_wallet);
            auto entry = GetP2MR(*pwallet, request.params[0].get_str());
            if (!entry) throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown p2mr_id");
            return EntryToJSON(*entry);
        },
    };
}

RPCHelpMan createp2mrspend()
{
    return RPCHelpMan{
        "createp2mrspend",
        "\nCreate an unsigned transaction spending a wallet-tracked P2MR output.\n",
        {
            {"p2mr_id", RPCArg::Type::STR, RPCArg::Optional::NO, "P2MR metadata id"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Recipient address"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Recipient amount"},
            {"fee", RPCArg::Type::AMOUNT, RPCArg::Default{"0.00001"}, "Fixed fee amount"},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::STR_HEX, "hex", "Unsigned raw transaction"},
            {RPCResult::Type::STR_HEX, "txid", "Unsigned txid"},
            {RPCResult::Type::STR, "p2mr_id", "P2MR metadata id used"},
            {RPCResult::Type::STR_HEX, "input_txid", "Selected P2MR input txid"},
            {RPCResult::Type::NUM, "input_vout", "Selected P2MR input vout"},
            {RPCResult::Type::ARR, "inputs", "Selected P2MR inputs", {
                {RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::STR_HEX, "txid", "Input txid"},
                    {RPCResult::Type::NUM, "vout", "Input vout"},
                }},
            }},
            {RPCResult::Type::STR_AMOUNT, "input_amount", "Total selected P2MR input amount"},
            {RPCResult::Type::STR_AMOUNT, "effective_fee", "Actual transaction fee after change and dust handling"},
            {RPCResult::Type::STR_AMOUNT, "change_amount", "Change amount, or 0 when change is dust and added to fee"},
        }},
        RPCExamples{HelpExampleCli("createp2mrspend", "\"abcd1234\" \"" + EXAMPLE_ADDRESS[0] + "\" 0.5")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const std::string p2mr_id = request.params[0].get_str();
            const CTxDestination to_dest = DecodeDestination(request.params[1].get_str());
            const CAmount send_amount = AmountFromValue(request.params[2]);
            const CAmount fee = request.params[3].isNull() ? AmountFromValue(UniValue("0.00001"))
                                                            : AmountFromValue(request.params[3]);

            LOCK(pwallet->cs_wallet);
            auto spend = CreateP2MRSpend(*pwallet, p2mr_id, to_dest, send_amount, fee);
            if (!spend) {
                // Map common errors to specific RPC codes for backwards compatibility.
                const std::string msg = util::ErrorString(spend).original;
                if (msg.find("invalid destination") != std::string::npos) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, msg);
                }
                if (msg.find("unknown p2mr_id") != std::string::npos) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, msg);
                }
                if (msg.find("insufficient") != std::string::npos || msg.find("no spendable") != std::string::npos) {
                    throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, msg);
                }
                throw JSONRPCError(RPC_WALLET_ERROR, msg);
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("hex", EncodeHexTx(CTransaction(spend->tx)));
            out.pushKV("txid", CTransaction(spend->tx).GetHash().GetHex());
            out.pushKV("p2mr_id", spend->p2mr_id);
            out.pushKV("input_txid", spend->input.hash.GetHex());
            out.pushKV("input_vout", (uint64_t)spend->input.n);
            UniValue inputs(UniValue::VARR);
            for (const COutPoint& input : spend->inputs) {
                UniValue input_obj(UniValue::VOBJ);
                input_obj.pushKV("txid", input.hash.GetHex());
                input_obj.pushKV("vout", (uint64_t)input.n);
                inputs.push_back(std::move(input_obj));
            }
            out.pushKV("inputs", std::move(inputs));
            out.pushKV("input_amount", ValueFromAmount(spend->input_amount));
            out.pushKV("effective_fee", ValueFromAmount(spend->effective_fee));
            out.pushKV("change_amount", ValueFromAmount(spend->change_amount));
            return out;
        },
    };
}

RPCHelpMan signp2mrtransaction()
{
    return RPCHelpMan{
        "signp2mrtransaction",
        "\nSign/finalize P2MR inputs in a raw transaction using wallet metadata.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Raw tx hex"},
            {"p2mr_id", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional metadata id filter"},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::STR_HEX, "hex", "Signed transaction hex"},
            {RPCResult::Type::BOOL, "complete", "Whether all P2MR inputs are finalized"},
        }},
        RPCExamples{HelpExampleCli("signp2mrtransaction", "\"rawhex\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str(), /*try_no_witness=*/true, /*try_witness=*/true)) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }

            LOCK(pwallet->cs_wallet);
            const std::optional<std::string> only_id = request.params[1].isNull()
                ? std::nullopt
                : std::optional<std::string>(request.params[1].get_str());

            auto signed_res = SignP2MRTransaction(*pwallet, mtx, only_id);
            if (!signed_res) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(signed_res).original);
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("hex", EncodeHexTx(CTransaction(signed_res->tx)));
            out.pushKV("complete", signed_res->complete);
            return out;
        },
    };
}

RPCHelpMan importp2mr()
{
    return RPCHelpMan{
        "importp2mr",
        "\nImport P2MR metadata from listp2mr-shaped JSON so custom trees can be restored\n"
        "on descriptor wallets (dumpwallet is legacy-only). Keys must already be in the wallet\n"
        "if the tree needs them. Trivial anyone-can-spend leaves are allowed here because this\n"
        "is a restore, not a new destination.\n",
        {
            {"entries", RPCArg::Type::ARR, RPCArg::Optional::NO, "Array of P2MR entries (listp2mr output or objects with a tree field)",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"tree", RPCArg::Type::ARR, RPCArg::Optional::NO, "P2MR tree leaves in DFS order"},
                            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
                        }},
                }},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {{
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR, "address", "Restored P2MR address"},
                    {RPCResult::Type::STR, "p2mr_id", "Wallet-local metadata id"},
                }
            }}
        },
        RPCExamples{HelpExampleCli("importp2mr", "'[{\"tree\":[{\"depth\":0,\"leaf_version\":192,\"script\":\"7551\"}]}]'")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            if (!request.params[0].isArray()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "entries must be an array");
            }

            LOCK(pwallet->cs_wallet);
            UniValue out(UniValue::VARR);
            for (const UniValue& entry : request.params[0].getValues()) {
                if (!entry.isObject() || !entry.exists("tree")) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "each entry must be an object with a tree field");
                }
                const auto leaves = ParseP2MRTreeFromUniValue(entry["tree"]);
                const std::string label = entry.exists("label") && entry["label"].isStr() ? entry["label"].get_str() : "";
                auto created = CreateP2MR(*pwallet, leaves, label, /*add_to_address_book=*/true, /*allow_trivial_leaves=*/true);
                if (!created) {
                    throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
                }
                UniValue row(UniValue::VOBJ);
                row.pushKV("address", created->address);
                row.pushKV("p2mr_id", created->id);
                out.push_back(std::move(row));
            }
            return out;
        },
    };
}

RPCHelpMan testp2mrtransaction()
{
    return RPCHelpMan{
        "testp2mrtransaction",
        "\nRun a mempool-accept dry run for a raw transaction.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Raw tx hex"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {{
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR_HEX, "txid", "Tested transaction id"},
                    {RPCResult::Type::BOOL, "allowed", "Whether mempool would accept"},
                    {RPCResult::Type::STR, "reject-reason", /*optional=*/true, "Reject reason when not allowed"},
                }
            }}
        },
        RPCExamples{HelpExampleCli("testp2mrtransaction", "\"rawhex\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str(), /*try_no_witness=*/true, /*try_witness=*/true)) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }

            auto accept = TestP2MRTransaction(*pwallet, mtx);

            UniValue result(UniValue::VARR);
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("txid", accept.txid.GetHex());
            entry.pushKV("allowed", accept.allowed);
            if (!accept.allowed) entry.pushKV("reject-reason", accept.reject_reason);
            result.push_back(entry);
            return result;
        },
    };
}

} // namespace wallet
