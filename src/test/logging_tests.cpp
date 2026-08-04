// Copyright (c) 2019-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <init/common.h>
#include <logging.h>
#include <logging/timer.h>
#include <test/util/setup_common.h>
#include <util/fs_helpers.h>
#include <util/string.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(logging_tests, BasicTestingSetup)

static void ResetLogger()
{
    LogInstance().SetLogLevel(BCLog::DEFAULT_LOG_LEVEL);
    LogInstance().SetCategoryLogLevel({});
}

struct LogSetup : public BasicTestingSetup {
    fs::path prev_log_path;
    fs::path tmp_log_path;
    bool prev_reopen_file;
    bool prev_print_to_file;
    bool prev_log_timestamps;
    bool prev_log_threadnames;
    bool prev_log_sourcelocations;
    std::unordered_map<BCLog::LogFlags, BCLog::Level> prev_category_levels;
    BCLog::Level prev_log_level;

    LogSetup() : prev_log_path{LogInstance().m_file_path},
                 tmp_log_path{m_args.GetDataDirBase() / "tmp_debug.log"},
                 prev_reopen_file{LogInstance().m_reopen_file},
                 prev_print_to_file{LogInstance().m_print_to_file},
                 prev_log_timestamps{LogInstance().m_log_timestamps},
                 prev_log_threadnames{LogInstance().m_log_threadnames},
                 prev_log_sourcelocations{LogInstance().m_log_sourcelocations},
                 prev_category_levels{LogInstance().CategoryLevels()},
                 prev_log_level{LogInstance().LogLevel()}
    {
        LogInstance().m_file_path = tmp_log_path;
        LogInstance().m_reopen_file = true;
        LogInstance().m_print_to_file = true;
        LogInstance().m_log_timestamps = false;
        LogInstance().m_log_threadnames = false;

        // Prevent tests from failing when the line number of the logs changes.
        LogInstance().m_log_sourcelocations = false;

        LogInstance().SetLogLevel(BCLog::Level::Debug);
        LogInstance().SetCategoryLogLevel({});
    }

    ~LogSetup()
    {
        LogInstance().m_file_path = prev_log_path;
        LogPrintf("Sentinel log to reopen log file\n");
        LogInstance().m_print_to_file = prev_print_to_file;
        LogInstance().m_reopen_file = prev_reopen_file;
        LogInstance().m_log_timestamps = prev_log_timestamps;
        LogInstance().m_log_threadnames = prev_log_threadnames;
        LogInstance().m_log_sourcelocations = prev_log_sourcelocations;
        LogInstance().SetLogLevel(prev_log_level);
        LogInstance().SetCategoryLogLevel(prev_category_levels);
    }
};

BOOST_AUTO_TEST_CASE(logging_timer)
{
    auto micro_timer = BCLog::Timer<std::chrono::microseconds>("tests", "end_msg");
    const std::string_view result_prefix{"tests: msg ("};
    BOOST_CHECK_EQUAL(micro_timer.LogMsg("msg").substr(0, result_prefix.size()), result_prefix);
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintf_, LogSetup)
{
    LogInstance().m_log_sourcelocations = true;
    LogPrintf_("fn1", "src1", 1, BCLog::LogFlags::NET, BCLog::Level::Debug, BCLog::RateLimit::No, "foo1: %s\n", "bar1");
    LogPrintf_("fn2", "src2", 2, BCLog::LogFlags::NET, BCLog::Level::None, BCLog::RateLimit::No, "foo2: %s\n", "bar2");
    LogPrintf_("fn3", "src3", 3, BCLog::LogFlags::NONE, BCLog::Level::Debug, BCLog::RateLimit::No, "foo3: %s\n", "bar3");
    LogPrintf_("fn4", "src4", 4, BCLog::LogFlags::NONE, BCLog::Level::None, BCLog::RateLimit::No, "foo4: %s\n", "bar4");
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    std::vector<std::string> expected = {
        "[src1:1] [fn1] [net:debug] foo1: bar1",
        "[src2:2] [fn2] [net] foo2: bar2",
        "[src3:3] [fn3] [debug] foo3: bar3",
        "[src4:4] [fn4] foo4: bar4",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintMacros, LogSetup)
{
    LogPrintf("foo5: %s\n", "bar5");
    LogPrint(BCLog::NET, "foo6: %s\n", "bar6");
    LogPrintLevel(BCLog::NET, BCLog::Level::Debug, "foo7: %s\n", "bar7");
    LogPrintLevel(BCLog::NET, BCLog::Level::Info, "foo8: %s\n", "bar8");
    LogPrintLevel(BCLog::NET, BCLog::Level::Warning, "foo9: %s\n", "bar9");
    LogPrintLevel(BCLog::NET, BCLog::Level::Error, "foo10: %s\n", "bar10");
    LogPrintfCategory(BCLog::VALIDATION, "foo11: %s\n", "bar11");
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    std::vector<std::string> expected = {
        "foo5: bar5",
        "[net] foo6: bar6",
        "[net:debug] foo7: bar7",
        "[net:info] foo8: bar8",
        "[net:warning] foo9: bar9",
        "[net:error] foo10: bar10",
        "[validation] foo11: bar11",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_LogPrintMacros_CategoryName, LogSetup)
{
    LogInstance().EnableCategory(BCLog::LogFlags::ALL);
    const auto concatenated_category_names = LogInstance().LogCategoriesString();
    std::vector<std::pair<BCLog::LogFlags, std::string>> expected_category_names;
    const auto category_names = SplitString(concatenated_category_names, ',');
    for (const auto& category_name : category_names) {
        BCLog::LogFlags category;
        const auto trimmed_category_name = TrimString(category_name);
        BOOST_REQUIRE(GetLogCategory(category, trimmed_category_name));
        expected_category_names.emplace_back(category, trimmed_category_name);
    }

    std::vector<std::string> expected;
    for (const auto& [category, name] : expected_category_names) {
        LogPrint(category, "foo: %s\n", "bar");
        std::string expected_log = "[";
        expected_log += name;
        expected_log += "] foo: bar";
        expected.push_back(expected_log);
    }

    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_SeverityLevels, LogSetup)
{
    LogInstance().EnableCategory(BCLog::LogFlags::ALL);

    LogInstance().SetLogLevel(BCLog::Level::Debug);
    LogInstance().SetCategoryLogLevel(/*category_str=*/"net", /*level_str=*/"info");

    // Global log level
    LogPrintLevel(BCLog::HTTP, BCLog::Level::Info, "foo1: %s\n", "bar1");
    LogPrintLevel(BCLog::MEMPOOL, BCLog::Level::Trace, "foo2: %s. This log level is lower than the global one.\n", "bar2");
    LogPrintLevel(BCLog::VALIDATION, BCLog::Level::Warning, "foo3: %s\n", "bar3");
    LogPrintLevel(BCLog::RPC, BCLog::Level::Error, "foo4: %s\n", "bar4");

    // Category-specific log level
    LogPrintLevel(BCLog::NET, BCLog::Level::Warning, "foo5: %s\n", "bar5");
    LogPrintLevel(BCLog::NET, BCLog::Level::Debug, "foo6: %s. This log level is the same as the global one but lower than the category-specific one, which takes precedence. \n", "bar6");
    LogPrintLevel(BCLog::NET, BCLog::Level::Error, "foo7: %s\n", "bar7");

    std::vector<std::string> expected = {
        "[http:info] foo1: bar1",
        "[validation:warning] foo3: bar3",
        "[rpc:error] foo4: bar4",
        "[net:warning] foo5: bar5",
        "[net:error] foo7: bar7",
    };
    std::ifstream file{tmp_log_path};
    std::vector<std::string> log_lines;
    for (std::string log; std::getline(file, log);) {
        log_lines.push_back(log);
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(log_lines.begin(), log_lines.end(), expected.begin(), expected.end());
}

BOOST_FIXTURE_TEST_CASE(logging_Conf, LogSetup)
{
    // Set global log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"btqd", "-loglevel=debug"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(2, argv_test, err));

        auto result = init::SetLoggingLevel(args);
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::Level::Debug);
    }

    // Set category-specific log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"btqd", "-loglevel=net:trace"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(2, argv_test, err));

        auto result = init::SetLoggingLevel(args);
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::DEFAULT_LOG_LEVEL);

        const auto& category_levels{LogInstance().CategoryLevels()};
        const auto net_it{category_levels.find(BCLog::LogFlags::NET)};
        BOOST_REQUIRE(net_it != category_levels.end());
        BOOST_CHECK_EQUAL(net_it->second, BCLog::Level::Trace);
    }

    // Set both global log level and category-specific log level
    {
        ResetLogger();
        ArgsManager args;
        args.AddArg("-loglevel", "...", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        const char* argv_test[] = {"btqd", "-loglevel=debug", "-loglevel=net:trace", "-loglevel=http:info"};
        std::string err;
        BOOST_REQUIRE(args.ParseParameters(4, argv_test, err));

        auto result = init::SetLoggingLevel(args);
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(LogInstance().LogLevel(), BCLog::Level::Debug);

        const auto& category_levels{LogInstance().CategoryLevels()};
        BOOST_CHECK_EQUAL(category_levels.size(), 2);

        const auto net_it{category_levels.find(BCLog::LogFlags::NET)};
        BOOST_CHECK(net_it != category_levels.end());
        BOOST_CHECK_EQUAL(net_it->second, BCLog::Level::Trace);

        const auto http_it{category_levels.find(BCLog::LogFlags::HTTP)};
        BOOST_CHECK(http_it != category_levels.end());
        BOOST_CHECK_EQUAL(http_it->second, BCLog::Level::Info);
    }
}

//! Budget arithmetic for a single source location.
BOOST_AUTO_TEST_CASE(logging_log_limit_stats)
{
    BCLog::LogLimitStats counter{500};
    BOOST_CHECK_EQUAL(counter.GetAvailableBytes(), 500ull);
    BOOST_CHECK_EQUAL(counter.GetDroppedBytes(), 0ull);

    BOOST_CHECK(counter.Consume(200));
    BOOST_CHECK_EQUAL(counter.GetAvailableBytes(), 300ull);

    // Exactly exhausting the budget still counts as a success: the bytes fit.
    BOOST_CHECK(counter.Consume(300));
    BOOST_CHECK_EQUAL(counter.GetAvailableBytes(), 0ull);
    BOOST_CHECK_EQUAL(counter.GetDroppedBytes(), 0ull);

    // One byte past, and everything after it, is accounted as dropped rather
    // than silently discarded -- the reset message reports the total.
    BOOST_CHECK(!counter.Consume(1));
    BOOST_CHECK_EQUAL(counter.GetDroppedBytes(), 1ull);
    BOOST_CHECK(!counter.Consume(500));
    BOOST_CHECK_EQUAL(counter.GetDroppedBytes(), 501ull);
}

namespace {
//! Each case is a distinct source location, so each gets its own budget.
//! Cases 0 and 1 are deliberately identical statements in different places.
void LogFromLocation(int location, const std::string& message)
{
    switch (location) {
    case 0:
        LogPrintf("%s\n", message);
        break;
    case 1:
        LogPrintf("%s\n", message);
        break;
    case 2:
        LogPrint(BCLog::NET, "%s\n", message);
        break;
    }
}

struct RateLimitSetup : public LogSetup {
    //! Small enough to keep the test quick, large enough that a line fits.
    static constexpr uint64_t BUDGET{16 * 1024};
    //! Lines of exactly 1 KiB, so the budget is a line count.
    static constexpr int LINES_IN_BUDGET{static_cast<int>(BUDGET / 1024)};

    BCLog::LogRateLimiter* limiter{nullptr};

    RateLimitSetup()
    {
        // No scheduler: the test calls Reset() itself rather than waiting an
        // hour or mocking time.
        auto owned{std::make_unique<BCLog::LogRateLimiter>(
            [](std::function<void()>, std::chrono::milliseconds) {},
            BUDGET, std::chrono::seconds{3600})};
        limiter = owned.get();
        LogInstance().SetRateLimiting(std::move(owned));
        LogInstance().EnableCategory(BCLog::NET);
    }

    ~RateLimitSetup()
    {
        LogInstance().DisableCategory(BCLog::NET);
        // Must happen before ~LogSetup, which logs a sentinel line.
        LogInstance().SetRateLimiting(nullptr);
        limiter = nullptr;
    }

    std::streamsize LogFileSize() const
    {
        return static_cast<std::streamsize>(GetFileSize(fs::PathToString(tmp_log_path).c_str()));
    }

    bool LogContains(const std::string& needle) const
    {
        std::ifstream file{tmp_log_path};
        std::string line;
        while (std::getline(file, line)) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};
} // namespace

//! A single log location that a peer can drive must not be able to fill the
//! disk, and must not take the rest of the log down with it.
//!
//! This is what closes CVE-2025-54604 (a spoofed self-connection reaching the
//! unconditional "connected to self" LogPrintf) and CVE-2025-54605 (repeated
//! invalid blocks reaching the unconditional failure logs). Both are the same
//! defect -- an attacker-reachable log site with no ceiling -- so the fix is
//! per-location rather than per-message, and covers the next such site too.
BOOST_FIXTURE_TEST_CASE(logging_rate_limit, RateLimitSetup)
{
    // 1023 characters plus the newline the format string adds.
    const std::string line(1023, 'a');

    // A full budget's worth gets through untouched.
    std::streamsize size{LogFileSize()};
    for (int i = 0; i < LINES_IN_BUDGET; ++i) LogFromLocation(0, line);
    BOOST_CHECK_MESSAGE(LogFileSize() > size, "a location within its budget must reach disk");

    // The line that exceeds it is still written, and says why it is the last.
    // Announcing the gap matters: a silently truncated log is worse to debug
    // than a noisy one.
    LogFromLocation(0, line);
    BOOST_CHECK_MESSAGE(LogContains("Excessive logging detected"),
                        "hitting the limit must be announced in the log");

    // After which the location writes nothing further. This is the property
    // the CVEs need.
    size = LogFileSize();
    for (int i = 0; i < LINES_IN_BUDGET * 2; ++i) LogFromLocation(0, line);
    BOOST_CHECK_MESSAGE(LogFileSize() == size, "a suppressed location must stop reaching disk");

    // An unrelated location keeps its own budget. Without this a single
    // attacker-driven site would blind the operator to everything else.
    LogFromLocation(1, line);
    BOOST_CHECK_MESSAGE(LogFileSize() > size, "an unrelated location must keep logging");

    // Debug logging is exempt: reaching it requires -debug, and an operator
    // who asked for that has accepted the volume.
    size = LogFileSize();
    for (int i = 0; i < LINES_IN_BUDGET * 2; ++i) LogFromLocation(2, line);
    BOOST_CHECK_MESSAGE(LogFileSize() > size, "-debug output must not be rate limited");

    // Resetting the window restores the location and accounts for what was
    // lost, so the gap in the log is quantified rather than merely implied.
    limiter->Reset();
    BOOST_CHECK_MESSAGE(LogContains("Restarting logging"), "the reset must be announced");
    BOOST_CHECK_MESSAGE(LogContains("bytes were dropped"), "the reset must report the loss");

    size = LogFileSize();
    LogFromLocation(0, line);
    BOOST_CHECK_MESSAGE(LogFileSize() > size, "the location must log again after a reset");
}

BOOST_AUTO_TEST_SUITE_END()
