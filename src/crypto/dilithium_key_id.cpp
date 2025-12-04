// Copyright (c) 2024 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/dilithium_key_id.h>
#include <crypto/dilithium_key.h>
#include <hash.h>
#include <util/strencodings.h>
#include <uint256.h>

DilithiumKeyID::DilithiumKeyID(const CDilithiumPubKey& pubkey)
{
    // Use SHA256 for full Dilithium key ID (256 bits)
    CSHA256().Write(pubkey.data(), pubkey.size()).Finalize(begin());
}

std::string DilithiumKeyID::ToString() const
{
    return HexStr(*this);
}

DilithiumKeyID DilithiumKeyID::FromString(const std::string& str)
{
    uint256 hash;
    if (str.length() == 64 && IsHex(str)) {
        hash = uint256S(str);
    }
    return DilithiumKeyID(hash);
}

DilithiumLegacyKeyID::DilithiumLegacyKeyID(const CDilithiumPubKey& pubkey)
{
    // Use Hash160 for legacy compatibility (160 bits)
    uint160 hash = Hash160(Span<const unsigned char>(pubkey.data(), pubkey.size()));
    std::copy(hash.begin(), hash.end(), begin());
}

std::string DilithiumLegacyKeyID::ToString() const
{
    return HexStr(*this);
}

DilithiumLegacyKeyID DilithiumLegacyKeyID::FromString(const std::string& str)
{
    uint160 hash;
    if (str.length() == 40 && IsHex(str)) {
        hash = uint160(ParseHex(str));
    }
    return DilithiumLegacyKeyID(hash);
}
