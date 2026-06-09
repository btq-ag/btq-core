// Copyright (c) 2026 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_wrapper.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>

#include <array>
#include <vector>

FUZZ_TARGET(dilithium)
{
    FuzzedDataProvider fuzzed_data_provider{buffer.data(), buffer.size()};

    std::array<uint8_t, BTQ_DILITHIUM_SEED_SIZE> seed{};
    fuzzed_data_provider.ConsumeData(seed.data(), seed.size());

    std::array<uint8_t, BTQ_DILITHIUM_PUBLIC_KEY_SIZE> pk{};
    std::array<uint8_t, BTQ_DILITHIUM_SECRET_KEY_SIZE> sk{};
    if (btq_dilithium_keypair_from_seed(pk.data(), sk.data(), seed.data()) != 0) {
        return;
    }

    const std::vector<uint8_t> message = ConsumeRandomLengthByteVector(fuzzed_data_provider, 256);
    std::array<uint8_t, BTQ_DILITHIUM_SIGNATURE_SIZE> sig{};
    size_t siglen = 0;
    if (btq_dilithium_sign(sig.data(), &siglen, message.data(), message.size(), nullptr, 0, sk.data()) != 0) {
        return;
    }

    (void)btq_dilithium_verify(sig.data(), siglen, message.data(), message.size(), nullptr, 0, pk.data());

    // Mutated signature or message must not verify as valid.
    if (!message.empty() && fuzzed_data_provider.ConsumeBool()) {
        std::vector<uint8_t> bad_msg = message;
        bad_msg[0] ^= 1;
        assert(btq_dilithium_verify(sig.data(), siglen, bad_msg.data(), bad_msg.size(), nullptr, 0, pk.data()) != 0);
    }
}
