// Copyright (c) 2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_NODE_MEMPOOL_ARGS_H
#define BTQ_NODE_MEMPOOL_ARGS_H

#include <util/result.h>

#include <cstdint>

class ArgsManager;
class CChainParams;
struct bilingual_str;
namespace kernel {
struct MemPoolOptions;
};

//! Largest -maxmempool, in MB, accepted on a 32-bit build.
static constexpr int64_t MAX_32BIT_MEMPOOL_MB{500};

/**
 * Reject a -maxmempool that a 32-bit build cannot safely carry.
 *
 * The whole address space is 4 GiB there. A mempool large enough to
 * reconstruct a compact block of over 1 GB overflows the size arithmetic done
 * before the block is written to disk, crashing the node (CVE-2025-46597).
 * Capping the mempool removes the precondition, which is how upstream chose to
 * close it.
 *
 * Taking `is_32bit` as an argument rather than testing sizeof(void*) inline is
 * what makes this testable: on a 64-bit host the interesting branch would
 * otherwise be eliminated at compile time and never run.
 */
[[nodiscard]] util::Result<void> CheckMaxMempoolSize(int64_t mb, bool is_32bit);

/**
 * Overlay the options set in \p argsman on top of corresponding members in \p mempool_opts.
 * Returns an error if one was encountered.
 *
 * @param[in]  argsman The ArgsManager in which to check set options.
 * @param[in,out] mempool_opts The MemPoolOptions to modify according to \p argsman.
 */
[[nodiscard]] util::Result<void> ApplyArgsManOptions(const ArgsManager& argsman, const CChainParams& chainparams, kernel::MemPoolOptions& mempool_opts);


#endif // BTQ_NODE_MEMPOOL_ARGS_H
