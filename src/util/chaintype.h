// Copyright (c) 2023 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_UTIL_CHAINTYPE_H
#define BTQ_UTIL_CHAINTYPE_H

#include <optional>
#include <string>

enum class ChainType {
    BTQMAIN,
    BTQTEST,
    BTQSIGNET,
    BTQREGTEST,
};

std::string ChainTypeToString(ChainType chain);

std::optional<ChainType> ChainTypeFromString(std::string_view chain);

#endif // BTQ_UTIL_CHAINTYPE_H
