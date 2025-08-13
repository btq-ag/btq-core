// Copyright (c) 2013-2020 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_NOUI_H
#define BTQ_NOUI_H

#include <string>

struct bilingual_str;

/** Non-GUI handler, which logs and prints messages. */
bool noui_ThreadSafeMessageBox(const bilingual_str& message, const std::string& caption, unsigned int style);
/** Non-GUI handler, which logs and prints questions. */
bool noui_ThreadSafeQuestion(const bilingual_str& /* ignored interactive message */, const std::string& message, const std::string& caption, unsigned int style);
/** Non-GUI handler, which only logs a message. */
void noui_InitMessage(const std::string& message);

/** Connect all btqd signal handlers */
void noui_connect();

/** Redirect all btqd signal handlers to LogPrintf. Used to check or suppress output during test runs that produce expected errors */
void noui_test_redirect();

/** Reconnects the regular Non-GUI handlers after having used noui_test_redirect */
void noui_reconnect();

#endif // BTQ_NOUI_H
