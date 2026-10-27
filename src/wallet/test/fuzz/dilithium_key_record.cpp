// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key.h>
#include <hash.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <cassert>
#include <optional>

namespace wallet {
namespace {
const TestingSetup* g_setup;
// Heap-allocated and never freed: a static holding secure memory would be
// destroyed after the locked pool it came from.
const CPrivKey* g_record;
CKeyID g_key_id;

void initialize_load_dilithium_key()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
    CDilithiumKey key;
    const bool made{key.MakeNewKey()};
    assert(made);
    g_record = new CPrivKey{key.begin(), key.end()};
    g_key_id = CKeyID{key.GetPubKey().GetID()};
}

bool HasKeyUnderId(CWallet& wallet, const CKeyID& id)
{
    CDilithiumKey key;
    LegacyScriptPubKeyMan* spk_man = wallet.GetLegacyScriptPubKeyMan();
    return spk_man && spk_man->GetDilithiumKey(id, key) && CKeyID{key.GetPubKey().GetID()} == id;
}
} // namespace

FUZZ_TARGET(wallet_load_dilithium_key, .init = initialize_load_dilithium_key)
{
    FuzzedDataProvider provider{buffer.data(), buffer.size()};
    CWallet wallet{g_setup->m_node.chain.get(), "", CreateMockableWalletDatabase()};
    {
        LOCK(wallet.cs_wallet);
        wallet.SetupLegacyScriptPubKeyMan();
    }
    std::string error;

    if (provider.ConsumeBool()) {
        // Arbitrary record bytes must never crash the loader.
        DataStream ss_key{ConsumeDataStream(provider)};
        DataStream ss_value{ConsumeDataStream(provider)};
        (void)LoadDilithiumKey(&wallet, ss_key, ss_value, error);
        return;
    }

    // Start from the genuine record and change parts of it.
    CKeyID key_id{g_key_id};
    if (provider.ConsumeBool()) key_id = CKeyID{ConsumeUInt160(provider)};
    CPrivKey record{*g_record};
    const int flips{provider.ConsumeIntegralInRange<int>(0, 4)};
    for (int i = 0; i < flips; ++i) {
        const size_t pos{provider.ConsumeIntegralInRange<size_t>(0, record.size() - 1)};
        record[pos] ^= provider.ConsumeIntegralInRange<uint8_t>(1, 255);
    }
    enum class Trailer { NONE, MATCHING, GENUINE, ARBITRARY };
    const Trailer trailer_kind{provider.PickValueInArray({Trailer::NONE, Trailer::MATCHING, Trailer::GENUINE, Trailer::ARBITRARY})};
    std::optional<uint256> trailer;
    switch (trailer_kind) {
    case Trailer::NONE: break;
    case Trailer::MATCHING: trailer = Hash(key_id, record); break;
    case Trailer::GENUINE: trailer = Hash(g_key_id, *g_record); break;
    case Trailer::ARBITRARY: trailer = ConsumeUInt256(provider); break;
    }
    const std::vector<unsigned char> extra{provider.ConsumeBytes<unsigned char>(provider.ConsumeIntegralInRange<size_t>(0, 40))};
    const bool key_suffix{provider.ConsumeBool()};

    DataStream ss_key;
    ss_key << key_id;
    if (key_suffix) ss_key << provider.ConsumeIntegral<uint8_t>();
    DataStream ss_value;
    ss_value << record;
    if (trailer) ss_value << *trailer;
    ss_value.write(MakeByteSpan(extra));

    const bool loaded{LoadDilithiumKey(&wallet, ss_key, ss_value, error)};
    const bool genuine{key_id == g_key_id && record == *g_record};

    if (loaded) {
        // Only a working key can load, and only under exactly its own id.
        assert(HasKeyUnderId(wallet, key_id));
        assert(key_id == g_key_id);
        assert(!key_suffix);
    }
    // The genuine record's checksum authenticates nothing else.
    if (trailer_kind == Trailer::GENUINE && !genuine) assert(!loaded);
    // A checksum the record does not match fails the load.
    if (trailer && !trailer->IsNull() && *trailer != Hash(key_id, record)) assert(!loaded);
    // The genuine record loads with its own trailer, or with none.
    if (genuine && extra.empty() && !key_suffix && (trailer_kind == Trailer::NONE || trailer_kind == Trailer::MATCHING || trailer_kind == Trailer::GENUINE)) {
        assert(loaded);
    }
}
} // namespace wallet
