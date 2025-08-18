// Copyright (c) 2023 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/chaintype.h>

#include <cassert>
#include <optional>
#include <string>

std::string ChainTypeToString(ChainType chain)
{
    switch (chain) {
    case ChainType::BTQMAIN:
        return "main";
    case ChainType::BTQTEST:
        return "test";
    case ChainType::BTQSIGNET:
        return "signet";
    case ChainType::BTQREGTEST:
        return "regtest";
    }
    assert(false);
}

std::optional<ChainType> ChainTypeFromString(std::string_view chain)
{
    if (chain == "main") {
        return ChainType::BTQMAIN;
    } else if (chain == "test") {
        return ChainType::BTQTEST;
    } else if (chain == "signet") {
        return ChainType::BTQSIGNET;
    } else if (chain == "regtest") {
        return ChainType::BTQREGTEST;
    } else {
        return std::nullopt;
    }
}