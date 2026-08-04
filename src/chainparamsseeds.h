#ifndef BTQ_CHAINPARAMSSEEDS_H
#define BTQ_CHAINPARAMSSEEDS_H

#include <array>
#include <cstdint>

/**
 * List of fixed seed nodes for the BTQ network
 *
 * Each entry is a BIP155 serialized (networkID, addr, port) tuple.
 *
 * These lists are empty: BTQ has not provisioned fixed seeds for any network
 * yet. See issue #114.
 *
 * They previously held a single entry of
 *
 *     0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
 *
 * described in a comment as an ignored placeholder. It was ignored, but not
 * harmlessly: it deserialises to 0.0.0.0:0, which is then discarded as
 * unroutable, so a node logged "Added 0 fixed seeds from reachable networks"
 * and read as though seeding had failed at runtime rather than never having
 * been configured at all. An empty list says the same thing without the
 * misdirection, and lets a test assert that every fixed seed we do ship is
 * routable.
 *
 * To populate these, run contrib/seeds/generate-seeds.py against a node with a
 * healthy peers.dat, then restore the vFixedSeeds assignment in
 * src/kernel/chainparams.cpp. Do this before mainnet launch: DNS seeding is the
 * only other bootstrap mechanism, and fixed seeds are precisely what covers the
 * case where it fails.
 */

static constexpr std::array<uint8_t, 0> chainparams_seed_main{};

static constexpr std::array<uint8_t, 0> chainparams_seed_test{};

#endif // BTQ_CHAINPARAMSSEEDS_H
