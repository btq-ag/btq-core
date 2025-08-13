// Copyright (c) 2023 BTQ Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logprintf.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

class BTQModule final : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& CheckFactories) override
    {
        CheckFactories.registerCheck<btq::LogPrintfCheck>("btq-unterminated-logprintf");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<BTQModule>
    X("btq-module", "Adds btq checks.");

volatile int BTQModuleAnchorSource = 0;
