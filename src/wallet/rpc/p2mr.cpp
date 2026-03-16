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
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <wallet/rpc/util.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

namespace wallet {
static constexpr const char* P2MR_STATE_CREATED{"created"};

using P2MRTreeTuple = std::tuple<uint8_t, uint8_t, std::vector<unsigned char>>;

std::vector<P2MRTreeTuple> ParseP2MRTree(const UniValue& tree)
{
    if (!tree.isArray() || tree.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "tree must be a non-empty array");
    }

    std::vector<P2MRTreeTuple> tuples;
    tuples.reserve(tree.size());
    for (size_t i = 0; i < tree.size(); ++i) {
        const UniValue& leaf = tree[i];
        if (!leaf.isObject() || !leaf.exists("depth") || !leaf.exists("leaf_version") || !leaf.exists("script")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "each tree entry must contain depth, leaf_version, script");
        }

        const int depth = leaf["depth"].getInt<int>();
        const int leaf_version = leaf["leaf_version"].getInt<int>();
        if (depth < 0 || depth > 128) throw JSONRPCError(RPC_INVALID_PARAMETER, "depth out of range");
        if (leaf_version < 0 || leaf_version > 255) throw JSONRPCError(RPC_INVALID_PARAMETER, "leaf_version out of range");

        auto script = TryParseHex<unsigned char>(leaf["script"].get_str());
        if (!script) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "script must be valid hex");
        }
        tuples.emplace_back(static_cast<uint8_t>(depth), static_cast<uint8_t>(leaf_version), std::move(*script));
    }
    return tuples;
}

P2MRBuilder BuildP2MRTree(const std::vector<P2MRTreeTuple>& tuples)
{
    P2MRBuilder builder;
    for (const auto& [depth, leaf_version, script] : tuples) {
        builder.Add(depth, script, leaf_version);
    }
    builder.Finalize();
    if (!builder.IsValid() || !builder.IsComplete()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid P2MR tree, verify DFS order and depths");
    }
    return builder;
}

UniValue TreeToJSON(const std::vector<P2MRTreeTuple>& tuples)
{
    UniValue tree(UniValue::VARR);
    for (const auto& [depth, leaf_version, script] : tuples) {
        UniValue leaf(UniValue::VOBJ);
        leaf.pushKV("depth", depth);
        leaf.pushKV("leaf_version", leaf_version);
        leaf.pushKV("script", HexStr(script));
        tree.push_back(leaf);
    }
    return tree;
}

UniValue BuildMetadata(const std::string& id, const std::string& address, const CScript& script_pub_key, const std::string& merkle_root_hex, const std::string& label, const std::vector<P2MRTreeTuple>& tuples)
{
    UniValue meta(UniValue::VOBJ);
    meta.pushKV("id", id);
    meta.pushKV("address", address);
    meta.pushKV("scriptPubKey", HexStr(script_pub_key));
    meta.pushKV("merkle_root", merkle_root_hex);
    meta.pushKV("created_at", GetTime());
    meta.pushKV("label", label);
    meta.pushKV("state", P2MR_STATE_CREATED);
    meta.pushKV("tree", TreeToJSON(tuples));
    return meta;
}

bool DecodeMetadata(const std::string& raw, UniValue& out)
{
    return out.read(raw) && out.isObject();
}

std::string NewP2MRId()
{
    return GetRandHash().GetHex().substr(0, 16);
}

std::optional<std::pair<CTxDestination, UniValue>> GetMetadataById(const CWallet& wallet, const std::string& id)
{
    for (const auto& [dest, rid, raw] : wallet.ListP2MRMetadata()) {
        if (rid != id) continue;
        UniValue meta;
        if (!DecodeMetadata(raw, meta)) continue;
        return std::make_pair(dest, std::move(meta));
    }
    return std::nullopt;
}

std::vector<std::pair<CTxDestination, UniValue>> GetAllMetadata(const CWallet& wallet)
{
    std::vector<std::pair<CTxDestination, UniValue>> out;
    for (const auto& [dest, rid, raw] : wallet.ListP2MRMetadata()) {
        UniValue meta;
        if (!DecodeMetadata(raw, meta)) continue;
        if (!meta.exists("id")) meta.pushKV("id", rid);
        out.emplace_back(dest, std::move(meta));
    }
    return out;
}

FlatSigningProvider BuildP2MRProviderFromMetadata(const CWallet& wallet, const std::optional<std::string>& only_id)
{
    FlatSigningProvider provider;
    for (const auto& [dest, meta] : GetAllMetadata(wallet)) {
        if (only_id && meta["id"].get_str() != *only_id) continue;

        if (!std::holds_alternative<WitnessV2P2MR>(dest)) continue;
        const UniValue& tree = meta["tree"];
        if (!tree.isArray()) continue;
        std::vector<P2MRTreeTuple> tuples = ParseP2MRTree(tree);
        P2MRBuilder builder = BuildP2MRTree(tuples);
        provider.p2mr_trees[std::get<WitnessV2P2MR>(dest)] = std::move(builder);
    }
    return provider;
}

UniValue CreateAndStoreP2MR(CWallet& wallet, const std::vector<P2MRTreeTuple>& tuples, const std::string& label)
{
    P2MRBuilder builder = BuildP2MRTree(tuples);
    const WitnessV2P2MR dest = builder.GetOutput();
    const CScript script_pub_key = GetScriptForDestination(dest);
    const std::string address = EncodeDestination(dest);
    const std::string id = NewP2MRId();
    const std::string merkle_root_hex = HexStr(Span<const unsigned char>{dest.begin(), WitnessV2P2MR::SIZE});
    const UniValue meta = BuildMetadata(id, address, script_pub_key, merkle_root_hex, label, tuples);

    WalletBatch batch(wallet.GetDatabase(), /*fFlushOnClose=*/false);
    if (!wallet.SetAddressBook(dest, label, AddressPurpose::RECEIVE)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "failed to set P2MR address book entry");
    }
    if (!wallet.SetP2MRMetadata(batch, dest, id, meta.write())) {
        throw JSONRPCError(RPC_WALLET_ERROR, "failed to persist P2MR metadata");
    }

    UniValue out(UniValue::VOBJ);
    out.pushKV("address", address);
    out.pushKV("p2mr_id", id);
    out.pushKV("scriptPubKey", HexStr(script_pub_key));
    out.pushKV("merkle_root", merkle_root_hex);
    return out;
}

RPCHelpMan getnewp2mraddress()
{
    return RPCHelpMan{
        "getnewp2mraddress",
        "\nCreate and store a new wallet-managed P2MR destination.\n",
        {
            {"tree", RPCArg::Type::ARR, RPCArg::Optional::NO, "P2MR tree leaves in DFS order", std::vector<RPCArg>{}, RPCArgOptions{}},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "Generated P2MR address"},
                {RPCResult::Type::STR, "p2mr_id", "Wallet-local metadata id"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "P2MR scriptPubKey"},
                {RPCResult::Type::STR_HEX, "merkle_root", "P2MR merkle root"},
            }},
        RPCExamples{HelpExampleCli("getnewp2mraddress", "'[{\"depth\":0,\"leaf_version\":192,\"script\":\"51\"}]'")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const auto tuples = ParseP2MRTree(request.params[0]);
            const std::string label = request.params[1].isNull() ? "" : LabelFromValue(request.params[1]);

            LOCK(pwallet->cs_wallet);
            return CreateAndStoreP2MR(*pwallet, tuples, label);
        },
    };
}

RPCHelpMan sendtop2mr()
{
    return RPCHelpMan{
        "sendtop2mr",
        "\nCreate a wallet-tracked P2MR destination and send funds to it.\n",
        {
            {"tree", RPCArg::Type::ARR, RPCArg::Optional::NO, "P2MR tree leaves in DFS order", std::vector<RPCArg>{}, RPCArgOptions{}},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send"},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label"},
            {"comment", RPCArg::Type::STR, RPCArg::Default{""}, "Wallet comment"},
            {"comment_to", RPCArg::Type::STR, RPCArg::Default{""}, "Wallet comment-to"},
            {"subtractfeefromamount", RPCArg::Type::BOOL, RPCArg::Default{false}, "Subtract fee from amount"},
        },
        RPCResult{RPCResult::Type::OBJ, "", "", {
            {RPCResult::Type::STR_HEX, "txid", "Funding transaction id"},
            {RPCResult::Type::STR, "address", "P2MR destination"},
            {RPCResult::Type::STR, "p2mr_id", "Stored metadata id"},
        }},
        RPCExamples{HelpExampleCli("sendtop2mr", "'[{\"depth\":0,\"leaf_version\":192,\"script\":\"51\"}]' 1.0")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const auto tuples = ParseP2MRTree(request.params[0]);
            const CAmount amount = AmountFromValue(request.params[1]);
            const std::string label = request.params[2].isNull() ? "" : LabelFromValue(request.params[2]);
            const bool subtract_fee = request.params[5].isNull() ? false : request.params[5].get_bool();

            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);
            UniValue created = CreateAndStoreP2MR(*pwallet, tuples, label);
            const CTxDestination dest = DecodeDestination(created["address"].get_str());

            std::vector<CRecipient> recipients{{dest, amount, subtract_fee}};
            CCoinControl coin_control;
            auto res = CreateTransaction(*pwallet, recipients, /*change_pos=*/-1, coin_control, /*sign=*/true);
            if (!res) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, util::ErrorString(res).original);
            }

            mapValue_t map_value;
            if (!request.params[3].isNull() && !request.params[3].get_str().empty()) map_value["comment"] = request.params[3].get_str();
            if (!request.params[4].isNull() && !request.params[4].get_str().empty()) map_value["to"] = request.params[4].get_str();
            pwallet->CommitTransaction(res->tx, std::move(map_value), /*orderForm=*/{});

            UniValue out(UniValue::VOBJ);
            out.pushKV("txid", res->tx->GetHash().GetHex());
            out.pushKV("address", created["address"].get_str());
            out.pushKV("p2mr_id", created["p2mr_id"].get_str());
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
            for (auto& [dest, meta] : GetAllMetadata(*pwallet)) {
                meta.pushKV("wallet_address", EncodeDestination(dest));
                out.push_back(meta);
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
            auto entry = GetMetadataById(*pwallet, request.params[0].get_str());
            if (!entry) throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown p2mr_id");
            entry->second.pushKV("wallet_address", EncodeDestination(entry->first));
            return entry->second;
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
        }},
        RPCExamples{HelpExampleCli("createp2mrspend", "\"abcd1234\" \"" + EXAMPLE_ADDRESS[0] + "\" 0.5")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            const std::string p2mr_id = request.params[0].get_str();
            const CTxDestination to_dest = DecodeDestination(request.params[1].get_str());
            if (!IsValidDestination(to_dest)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "invalid destination address");
            const CAmount send_amount = AmountFromValue(request.params[2]);
            const CAmount fee = request.params[3].isNull() ? AmountFromValue(UniValue("0.00001")) : AmountFromValue(request.params[3]);

            LOCK(pwallet->cs_wallet);
            auto entry = GetMetadataById(*pwallet, p2mr_id);
            if (!entry) throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown p2mr_id");
            const CScript target_spk = GetScriptForDestination(entry->first);
            std::optional<COutPoint> selected_outpoint;
            CAmount input_amount{0};
            for (const auto& [txid, wtx] : pwallet->mapWallet) {
                if (!wtx.tx) continue;
                if (pwallet->GetTxDepthInMainChain(wtx) <= 0) continue;
                for (uint32_t n = 0; n < wtx.tx->vout.size(); ++n) {
                    const CTxOut& txout = wtx.tx->vout[n];
                    if (txout.scriptPubKey != target_spk) continue;
                    const COutPoint outpoint{txid, n};
                    if (pwallet->IsSpent(outpoint)) continue;
                    selected_outpoint = outpoint;
                    input_amount = txout.nValue;
                    break;
                }
                if (selected_outpoint) break;
            }
            if (!selected_outpoint) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "no spendable P2MR UTXO found");
            }

            const uint256 prev_txid = selected_outpoint->hash;
            const uint32_t vout = selected_outpoint->n;

            const CAmount change = input_amount - send_amount - fee;
            if (change < 0) throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "insufficient P2MR UTXO amount");

            CMutableTransaction mtx;
            mtx.vin.emplace_back(COutPoint(prev_txid, vout));
            mtx.vout.emplace_back(send_amount, GetScriptForDestination(to_dest));

            if (change > 546) {
                auto change_dest = pwallet->GetNewChangeDestination(OutputType::BECH32);
                if (!change_dest) throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(change_dest).original);
                mtx.vout.emplace_back(change, GetScriptForDestination(*change_dest));
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("hex", EncodeHexTx(CTransaction(mtx)));
            out.pushKV("txid", CTransaction(mtx).GetHash().GetHex());
            out.pushKV("p2mr_id", p2mr_id);
            out.pushKV("input_txid", prev_txid.GetHex());
            out.pushKV("input_vout", vout);
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
            {"witness_args", RPCArg::Type::ARR, RPCArg::Optional::OMITTED, "Optional witness stack args (hex)", std::vector<RPCArg>{}, RPCArgOptions{}},
            {"leaf_script", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Optional explicit leaf script"},
            {"control_block", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Optional explicit control block"},
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

            std::map<COutPoint, Coin> coins;
            for (const CTxIn& txin : mtx.vin) coins[txin.prevout];
            pwallet->chain().findCoins(coins);

            LOCK(pwallet->cs_wallet);
            const std::optional<std::string> p2mr_id = request.params[1].isNull() ? std::nullopt : std::optional<std::string>(request.params[1].get_str());
            FlatSigningProvider p2mr_provider = BuildP2MRProviderFromMetadata(*pwallet, p2mr_id);

            // First let wallet sign non-P2MR paths it already supports.
            std::map<int, bilingual_str> ignored_errors;
            pwallet->SignTransaction(mtx, coins, SIGHASH_DEFAULT, ignored_errors);

            bool complete = true;
            const bool explicit_witness = !request.params[3].isNull() && !request.params[4].isNull();
            std::vector<std::vector<unsigned char>> witness_args;
            if (!request.params[2].isNull()) {
                for (const UniValue& arg : request.params[2].get_array().getValues()) {
                    auto data = TryParseHex<unsigned char>(arg.get_str());
                    if (!data) throw JSONRPCError(RPC_INVALID_PARAMETER, "witness_args entries must be hex");
                    witness_args.push_back(std::move(*data));
                }
            }
            std::vector<unsigned char> leaf_script;
            std::vector<unsigned char> control_block;
            if (explicit_witness) {
                auto script = TryParseHex<unsigned char>(request.params[3].get_str());
                auto control = TryParseHex<unsigned char>(request.params[4].get_str());
                if (!script || !control) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "leaf_script/control_block must be hex");
                }
                leaf_script = std::move(*script);
                control_block = std::move(*control);
            }

            for (unsigned int i = 0; i < mtx.vin.size(); ++i) {
                auto it = coins.find(mtx.vin[i].prevout);
                if (it == coins.end() || it->second.IsSpent()) continue;
                std::vector<std::vector<unsigned char>> solutions;
                if (Solver(it->second.out.scriptPubKey, solutions) != TxoutType::WITNESS_V2_P2MR) continue;

                if (explicit_witness) {
                    mtx.vin[i].scriptSig.clear();
                    mtx.vin[i].scriptWitness.stack = witness_args;
                    mtx.vin[i].scriptWitness.stack.push_back(leaf_script);
                    mtx.vin[i].scriptWitness.stack.push_back(control_block);
                    continue;
                }

                SignatureData sigdata = DataFromTransaction(mtx, i, it->second.out);
                if (!SignSignature(p2mr_provider, it->second.out.scriptPubKey, mtx, i, it->second.out.nValue, SIGHASH_DEFAULT, sigdata)) {
                    bool fallback_complete = false;
                    if (!solutions.empty() && solutions[0].size() == WitnessV2P2MR::SIZE) {
                        const WitnessV2P2MR p2mr_output{solutions[0]};
                        P2MRSpendData spenddata;
                        if (p2mr_provider.GetP2MRSpendData(p2mr_output, spenddata) && !spenddata.scripts.empty()) {
                            const auto& [script_key, control_blocks] = *spenddata.scripts.begin();
                            const auto& [script, leaf_version] = script_key;
                            if (leaf_version == TAPROOT_LEAF_TAPSCRIPT && !control_blocks.empty()) {
                                mtx.vin[i].scriptSig.clear();
                                mtx.vin[i].scriptWitness.stack.clear();
                                mtx.vin[i].scriptWitness.stack.emplace_back(script.begin(), script.end());
                                mtx.vin[i].scriptWitness.stack.push_back(*control_blocks.begin());
                                fallback_complete = true;
                            }
                        }
                    }
                    if (!fallback_complete) complete = false;
                }
            }

            UniValue out(UniValue::VOBJ);
            out.pushKV("hex", EncodeHexTx(CTransaction(mtx)));
            out.pushKV("complete", complete);
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
            CTransactionRef tx = MakeTransactionRef(mtx);
            std::string err_string;
            const bool allowed = pwallet->chain().broadcastTransaction(tx, /*max_tx_fee=*/MAX_MONEY, /*relay=*/false, err_string);

            UniValue result(UniValue::VARR);
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("txid", tx->GetHash().GetHex());
            entry.pushKV("allowed", allowed);
            if (!allowed) entry.pushKV("reject-reason", err_string);
            result.push_back(entry);
            return result;
        },
    };
}

} // namespace wallet
