// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/validation.h>

#include <sync.h>
#include <util/check.h>
#include <util/time.h>
#include <validation.h>
#include <validationinterface.h>

void ResetIbd(ChainstateManager& chainman)
{
    chainman.m_cached_finished_ibd = false;
    assert(chainman.IsInitialBlockDownload());
}

void ResetIbdWithMockTime(ChainstateManager& chainman)
{
    LOCK(chainman.GetMutex());
    const auto mock_time{Assert(chainman.ActiveTip())->Time() + chainman.m_options.max_tip_age + 1s};
    SetMockTime(mock_time.time_since_epoch());
    ResetIbd(chainman);
}

void JumpOutOfIbd(ChainstateManager& chainman)
{
    Assert(chainman.IsInitialBlockDownload());
    chainman.m_cached_finished_ibd = true;
    Assert(!chainman.IsInitialBlockDownload());
}

void ValidationInterfaceTest::BlockConnected(
        ChainstateRole role,
        CValidationInterface& obj,
        const std::shared_ptr<const CBlock>& block,
        const CBlockIndex* pindex)
{
    obj.BlockConnected(role, block, pindex);
}
