// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2023 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_UTIL_EXCEPTION_H
#define BTQ_UTIL_EXCEPTION_H

#include <exception>
#include <string_view>

void PrintExceptionContinue(const std::exception* pex, std::string_view thread_name);

#endif // BTQ_UTIL_EXCEPTION_H
