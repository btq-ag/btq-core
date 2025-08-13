// Copyright (c) 2021 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_NODE_MINISKETCHWRAPPER_H
#define BTQ_NODE_MINISKETCHWRAPPER_H

#include <minisketch.h>

#include <cstddef>
#include <cstdint>

namespace node {
/** Wrapper around Minisketch::Minisketch(32, implementation, capacity). */
Minisketch MakeMinisketch32(size_t capacity);
/** Wrapper around Minisketch::CreateFP. */
Minisketch MakeMinisketch32FP(size_t max_elements, uint32_t fpbits);
} // namespace node

#endif // BTQ_NODE_MINISKETCHWRAPPER_H
