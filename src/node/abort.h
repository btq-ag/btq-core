// Copyright (c) 2023 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_NODE_ABORT_H
#define BTQ_NODE_ABORT_H

#include <util/translation.h>

#include <atomic>
#include <string>

namespace node {
void AbortNode(std::atomic<int>& exit_status, const std::string& debug_message, const bilingual_str& user_message = {}, bool shutdown = true);
} // namespace node

#endif // BTQ_NODE_ABORT_H
