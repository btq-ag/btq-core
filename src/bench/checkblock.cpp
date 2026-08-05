// Copyright (c) 2016-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/data.h>

#include <chainparams.h>
#include <common/args.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <streams.h>
#include <util/chaintype.h>
#include <validation.h>

// These are the two major time-sinks which happen after we have fully received
// a block off the wire, but before we can relay the block on to peers using
// compact block relay.

static void DeserializeBlockTest(benchmark::Bench& bench)
{
    CDataStream stream(benchmark::data::block413567, SER_NETWORK, PROTOCOL_VERSION);
    std::byte a{0};
    stream.write({&a, 1}); // Prevent compaction

    bench.unit("block").run([&] {
        CBlock block;
        stream >> block;
        bool rewound = stream.Rewind(benchmark::data::block413567.size());
        assert(rewound);
    });
}

// The bundled block is a 1 MB upstream Bitcoin block. BTQ scales witness data by
// 16 rather than 4, so under an 8 MW cap a block holds at most 500 kB of
// non-witness data and that block weighs 16 MW here. CheckBlock rejects it as
// bad-blk-length, and it does so before the per-transaction work this benchmark
// exists to measure. Drop transactions from the end until it fits, so the
// benchmark reaches that work again.
static CDataStream BlockThatFitsTheWeightLimit()
{
    CDataStream source(benchmark::data::block413567, SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    source >> block;

    // Track the running weight rather than re-measuring the block each time, so
    // this stays linear. Dropping a transaction also shrinks the count varint, so
    // the running figure is an upper bound and the result always clears the cap.
    int64_t weight = GetBlockWeight(block);
    while (block.vtx.size() > 1 && weight > MAX_BLOCK_WEIGHT) {
        weight -= GetTransactionWeight(*block.vtx.back());
        block.vtx.pop_back();
    }
    assert(GetBlockWeight(block) <= MAX_BLOCK_WEIGHT);
    block.hashMerkleRoot = BlockMerkleRoot(block);

    CDataStream trimmed(SER_NETWORK, PROTOCOL_VERSION);
    trimmed << block;
    return trimmed;
}

static void DeserializeAndCheckBlockTest(benchmark::Bench& bench)
{
    CDataStream stream = BlockThatFitsTheWeightLimit();
    const size_t block_size = stream.size();
    std::byte a{0};
    stream.write({&a, 1}); // Prevent compaction

    ArgsManager bench_args;
    const auto chainParams = CreateChainParams(bench_args, ChainType::BTQMAIN);

    bench.unit("block").run([&] {
        CBlock block; // Note that CBlock caches its checked state, so we need to recreate it here
        stream >> block;
        bool rewound = stream.Rewind(block_size);
        assert(rewound);

        BlockValidationState validationState;
        // Re-deriving the merkle root invalidated the header's proof of work.
        // Checking it is one hash either way, so nothing measurable is lost.
        bool checked = CheckBlock(block, validationState, chainParams->GetConsensus(),
                                  /*fCheckPOW=*/false);
        assert(checked);
    });
}

BENCHMARK(DeserializeBlockTest, benchmark::PriorityLevel::HIGH);
BENCHMARK(DeserializeAndCheckBlockTest, benchmark::PriorityLevel::HIGH);
