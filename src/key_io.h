// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_KEY_IO_H
#define BTQ_KEY_IO_H

#include <addresstype.h>
#include <chainparams.h>
#include <key.h>
#include <pubkey.h>
#include <crypto/dilithium_key.h>

#include <string>

CKey DecodeSecret(const std::string& str);
std::string EncodeSecret(const CKey& key);

// Dilithium key WIF support
CDilithiumKey DecodeDilithiumSecret(const std::string& str);
std::string EncodeDilithiumSecret(const CDilithiumKey& key);

CExtKey DecodeExtKey(const std::string& str);
std::string EncodeExtKey(const CExtKey& extkey);
CExtPubKey DecodeExtPubKey(const std::string& str);
std::string EncodeExtPubKey(const CExtPubKey& extpubkey);

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
CTxDestination DecodeDestination(const std::string& str, std::string& error_msg, std::vector<int>* error_locations = nullptr);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

#endif // BTQ_KEY_IO_H
