// Copyright (c) 2019 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_TEST_UTIL_BLOCKFILTER_H
#define BTQ_TEST_UTIL_BLOCKFILTER_H

#include <blockfilter.h>

class CBlockIndex;
namespace node {
class BlockManager;
}

bool ComputeFilter(BlockFilterType filter_type, const CBlockIndex& block_index, BlockFilter& filter, const node::BlockManager& blockman);

#endif // BTQ_TEST_UTIL_BLOCKFILTER_H
