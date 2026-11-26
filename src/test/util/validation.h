// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_TEST_UTIL_VALIDATION_H
#define BTQ_TEST_UTIL_VALIDATION_H

#include <validation.h>

class CValidationInterface;

/** Reset the IBD cache to its initial state. */
void ResetIbd(ChainstateManager& chainman);
/** Set mock time so the active tip is stale, then reset the IBD cache. */
void ResetIbdWithMockTime(ChainstateManager& chainman);
/** Toggle IsInitialBlockDownload from true to false. */
void JumpOutOfIbd(ChainstateManager& chainman);

class ValidationInterfaceTest
{
public:
    static void BlockConnected(
        ChainstateRole role,
        CValidationInterface& obj,
        const std::shared_ptr<const CBlock>& block,
        const CBlockIndex* pindex);
};

#endif // BTQ_TEST_UTIL_VALIDATION_H
