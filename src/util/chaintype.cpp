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
        return "btqmain";
    case ChainType::BTQTEST:
        return "btqtest";
    case ChainType::BTQSIGNET:
        return "btqsignet";
    case ChainType::BTQREGTEST:
        return "btqregtest";
    }
    assert(false);
}

std::optional<ChainType> ChainTypeFromString(std::string_view chain)
{
    if (chain == "btqmain") {
        return ChainType::BTQMAIN;
    } else if (chain == "btqtest") {
        return ChainType::BTQTEST;
    } else if (chain == "btqsignet") {
        return ChainType::BTQSIGNET;
    } else if (chain == "btqregtest") {
        return ChainType::BTQREGTEST;
    } else {
        return std::nullopt;
    }
}