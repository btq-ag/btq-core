// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_LOGGING_H
#define BTQ_LOGGING_H

#include <crypto/siphash.h>
#include <threadsafety.h>
#include <tinyformat.h>
#include <util/fs.h>
#include <util/string.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS        = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
static const bool DEFAULT_LOGTHREADNAMES = false;
static const bool DEFAULT_LOGSOURCELOCATIONS = false;
extern const char * const DEFAULT_DEBUGLOGFILE;

extern bool fLogIPs;

struct LogCategory {
    std::string category;
    bool active;
};

/**
 * Identifies the place in the source a log line came from.
 *
 * Upstream keys its rate limiter on std::source_location, which is C++20; this
 * tree still builds as C++17 by default (--enable-c++20 is off). It does not
 * need it: LogPrintf_ already receives __FILE__ and __LINE__ as arguments, so
 * the same identity is available without the newer type. File and line alone
 * are enough -- the function name adds nothing a line number does not already
 * distinguish.
 */
struct LogSource {
    std::string file;
    int line;

    bool operator==(const LogSource& other) const noexcept
    {
        return line == other.line && file == other.file;
    }
};

struct LogSourceHasher {
    size_t operator()(const LogSource& s) const noexcept
    {
        // CSipHasher(0, 0) purely as a cheap way to get a uniform spread.
        return static_cast<size_t>(CSipHasher(0, 0)
                                       .Write(std::hash<std::string>{}(s.file))
                                       .Write(static_cast<uint64_t>(s.line))
                                       .Finalize());
    }
};

namespace BCLog {
    enum LogFlags : uint32_t {
        NONE        = 0,
        NET         = (1 <<  0),
        TOR         = (1 <<  1),
        MEMPOOL     = (1 <<  2),
        HTTP        = (1 <<  3),
        BENCH       = (1 <<  4),
        ZMQ         = (1 <<  5),
        WALLETDB    = (1 <<  6),
        RPC         = (1 <<  7),
        ESTIMATEFEE = (1 <<  8),
        ADDRMAN     = (1 <<  9),
        SELECTCOINS = (1 << 10),
        REINDEX     = (1 << 11),
        CMPCTBLOCK  = (1 << 12),
        RAND        = (1 << 13),
        PRUNE       = (1 << 14),
        PROXY       = (1 << 15),
        MEMPOOLREJ  = (1 << 16),
        LIBEVENT    = (1 << 17),
        COINDB      = (1 << 18),
        QT          = (1 << 19),
        LEVELDB     = (1 << 20),
        VALIDATION  = (1 << 21),
        I2P         = (1 << 22),
        IPC         = (1 << 23),
#ifdef DEBUG_LOCKCONTENTION
        LOCK        = (1 << 24),
#endif
        UTIL        = (1 << 25),
        BLOCKSTORAGE = (1 << 26),
        TXRECONCILIATION = (1 << 27),
        SCAN        = (1 << 28),
        TXPACKAGES  = (1 << 29),
        ALL         = ~(uint32_t)0,
    };
    enum class Level {
        Trace = 0, // High-volume or detailed logging for development/debugging
        Debug,     // Reasonably noisy logging, but still usable in production
        Info,      // Default
        Warning,
        Error,
        None, // Internal use only
    };
    constexpr auto DEFAULT_LOG_LEVEL{Level::Debug};

    //! Whether a log statement is subject to rate limiting.
    //!
    //! Scoped, rather than a bool, because this argument sits immediately
    //! before the format string and a `const char*` converts to bool without
    //! complaint. A caller who omits it would otherwise compile cleanly and
    //! log nonsense -- which is exactly what happened to logging_LogPrintf_
    //! while this was being written, with no diagnostic from the compiler.
    enum class RateLimit {
        No,
        Yes,
    };

    //! Bytes a single source location may write to disk within one window.
    constexpr uint64_t RATELIMIT_MAX_BYTES{1024 * 1024};
    //! How often the per-location budgets are refilled.
    constexpr auto RATELIMIT_WINDOW{std::chrono::hours{1}};

    //! Budget for one source location within the current window.
    class LogLimitStats
    {
    private:
        //! Bytes still available in this window.
        uint64_t m_available_bytes;
        //! Bytes this location tried to log after running out.
        uint64_t m_dropped_bytes{0};

    public:
        explicit LogLimitStats(uint64_t max_bytes) : m_available_bytes{max_bytes} {}

        //! Deduct `bytes` if the budget covers it. Returns whether it did.
        bool Consume(uint64_t bytes);

        uint64_t GetAvailableBytes() const { return m_available_bytes; }
        uint64_t GetDroppedBytes() const { return m_dropped_bytes; }
    };

    /**
     * Fixed-window rate limiter for logging, keyed by source location.
     *
     * A peer that can make the node log unconditionally can otherwise fill the
     * operator's disk: a spoofed self-connection (CVE-2025-54604) or a stream
     * of invalid blocks (CVE-2025-54605) each reach a LogPrintf that has no
     * cap on how often it fires. Limiting per location rather than per message
     * is what makes this general -- it covers the next such log site too,
     * without anyone having to notice it is one.
     *
     * Only writes to disk are suppressed. Console output and log callbacks are
     * left alone, since neither consumes the resource under attack.
     */
    class LogRateLimiter
    {
    private:
        mutable StdMutex m_mutex;

        //! Per-location budgets for the current window.
        std::unordered_map<LogSource, LogLimitStats, LogSourceHasher> m_source_locations GUARDED_BY(m_mutex);
        //! Whether anything is currently suppressed. A cached view of
        //! m_source_locations, so the common path need not take the lock.
        std::atomic<bool> m_suppression_active{false};

    public:
        using SchedulerFunction = std::function<void(std::function<void()>, std::chrono::milliseconds)>;

        /**
         * @param scheduler_func Used to schedule the periodic window reset.
         * @param max_bytes      Budget per source location per window.
         * @param reset_window   How long a window lasts.
         */
        LogRateLimiter(SchedulerFunction scheduler_func, uint64_t max_bytes, std::chrono::seconds reset_window);

        const uint64_t m_max_bytes;
        const std::chrono::seconds m_reset_window;

        enum class Status {
            UNSUPPRESSED,     //!< within budget
            NEWLY_SUPPRESSED, //!< this message is the one that exhausted it
            STILL_SUPPRESSED, //!< already exhausted earlier in this window
        };

        //! Charge `str` against `source`'s budget and report the result.
        [[nodiscard]] Status Consume(const LogSource& source, const std::string& str)
            EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

        //! Refill every budget. Called on the scheduler once per window.
        void Reset() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

        bool SuppressionsActive() const { return m_suppression_active; }
    };

    class Logger
    {
    private:
        mutable StdMutex m_cs; // Can not use Mutex from sync.h because in debug mode it would cause a deadlock when a potential deadlock was detected

        FILE* m_fileout GUARDED_BY(m_cs) = nullptr;
        std::list<std::string> m_msgs_before_open GUARDED_BY(m_cs);
        bool m_buffering GUARDED_BY(m_cs) = true; //!< Buffer messages before logging can be started.

        /**
         * m_started_new_line is a state variable that will suppress printing of
         * the timestamp when multiple calls are made that don't end in a
         * newline.
         */
        std::atomic_bool m_started_new_line{true};

        //! Rate limiter for unconditional log locations. Null until AppInitMain
        //! installs one, so early startup logging is never suppressed.
        std::unique_ptr<LogRateLimiter> m_limiter GUARDED_BY(m_cs);

        //! Category-specific log level. Overrides `m_log_level`.
        std::unordered_map<LogFlags, Level> m_category_log_levels GUARDED_BY(m_cs);

        //! If there is no category-specific log level, all logs with a severity
        //! level lower than `m_log_level` will be ignored.
        std::atomic<Level> m_log_level{DEFAULT_LOG_LEVEL};

        /** Log categories bitfield. */
        std::atomic<uint32_t> m_categories{0};

        std::string LogTimestampStr(const std::string& str);

        /** Slots that connect to the print signal */
        std::list<std::function<void(const std::string&)>> m_print_callbacks GUARDED_BY(m_cs) {};

        /** Send a string to the log output, with m_cs already held. */
        void LogPrintStr_(const std::string& str, const std::string& logging_function, const std::string& source_file, int source_line, BCLog::LogFlags category, BCLog::Level level, bool should_ratelimit) EXCLUSIVE_LOCKS_REQUIRED(m_cs);

    public:
        bool m_print_to_console = false;
        bool m_print_to_file = false;

        bool m_log_timestamps = DEFAULT_LOGTIMESTAMPS;
        bool m_log_time_micros = DEFAULT_LOGTIMEMICROS;
        bool m_log_threadnames = DEFAULT_LOGTHREADNAMES;
        bool m_log_sourcelocations = DEFAULT_LOGSOURCELOCATIONS;

        fs::path m_file_path;
        std::atomic<bool> m_reopen_file{false};

        /** Send a string to the log output */
        void LogPrintStr(const std::string& str, const std::string& logging_function, const std::string& source_file, int source_line, BCLog::LogFlags category, BCLog::Level level, bool should_ratelimit);

        /** Install the rate limiter. Passing nullptr disables rate limiting. */
        void SetRateLimiting(std::unique_ptr<LogRateLimiter>&& limiter)
        {
            StdLockGuard scoped_lock(m_cs);
            m_limiter = std::move(limiter);
        }

        /** Returns whether logs will be written to any output */
        bool Enabled() const
        {
            StdLockGuard scoped_lock(m_cs);
            return m_buffering || m_print_to_console || m_print_to_file || !m_print_callbacks.empty();
        }

        /** Connect a slot to the print signal and return the connection */
        std::list<std::function<void(const std::string&)>>::iterator PushBackCallback(std::function<void(const std::string&)> fun)
        {
            StdLockGuard scoped_lock(m_cs);
            m_print_callbacks.push_back(std::move(fun));
            return --m_print_callbacks.end();
        }

        /** Delete a connection */
        void DeleteCallback(std::list<std::function<void(const std::string&)>>::iterator it)
        {
            StdLockGuard scoped_lock(m_cs);
            m_print_callbacks.erase(it);
        }

        /** Start logging (and flush all buffered messages) */
        bool StartLogging();
        /** Only for testing */
        void DisconnectTestLogger();

        void ShrinkDebugFile();

        std::unordered_map<LogFlags, Level> CategoryLevels() const
        {
            StdLockGuard scoped_lock(m_cs);
            return m_category_log_levels;
        }
        void SetCategoryLogLevel(const std::unordered_map<LogFlags, Level>& levels)
        {
            StdLockGuard scoped_lock(m_cs);
            m_category_log_levels = levels;
        }
        bool SetCategoryLogLevel(const std::string& category_str, const std::string& level_str);

        Level LogLevel() const { return m_log_level.load(); }
        void SetLogLevel(Level level) { m_log_level = level; }
        bool SetLogLevel(const std::string& level);

        uint32_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);
        bool EnableCategory(const std::string& str);
        void DisableCategory(LogFlags flag);
        bool DisableCategory(const std::string& str);

        bool WillLogCategory(LogFlags category) const;
        bool WillLogCategoryLevel(LogFlags category, Level level) const;

        /** Returns a vector of the log categories in alphabetical order. */
        std::vector<LogCategory> LogCategoriesList() const;
        /** Returns a string with the log categories in alphabetical order. */
        std::string LogCategoriesString() const
        {
            return Join(LogCategoriesList(), ", ", [&](const LogCategory& i) { return i.category; });
        };

        //! Returns a string with all user-selectable log levels.
        std::string LogLevelsString() const;

        //! Returns the string representation of a log level.
        std::string LogLevelToStr(BCLog::Level level) const;

        bool DefaultShrinkDebugFile() const;
    };

} // namespace BCLog

BCLog::Logger& LogInstance();

/** Return true if log accepts specified category, at the specified level. */
static inline bool LogAcceptCategory(BCLog::LogFlags category, BCLog::Level level)
{
    return LogInstance().WillLogCategoryLevel(category, level);
}

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

// Be conservative when using LogPrintf/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

template <typename... Args>
static inline void LogPrintf_(const std::string& logging_function, const std::string& source_file, const int source_line, const BCLog::LogFlags flag, const BCLog::Level level, const BCLog::RateLimit rate_limit, const char* fmt, const Args&... args)
{
    if (LogInstance().Enabled()) {
        std::string log_msg;
        try {
            log_msg = tfm::format(fmt, args...);
        } catch (tinyformat::format_error& fmterr) {
            /* Original format string will have newline so don't add one here */
            log_msg = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + fmt;
        }
        LogInstance().LogPrintStr(log_msg, logging_function, source_file, source_line, flag, level, rate_limit == BCLog::RateLimit::Yes);
    }
}

#define LogPrintLevel_(category, level, rate_limit, ...) LogPrintf_(__func__, __FILE__, __LINE__, category, level, rate_limit, __VA_ARGS__)

// Log unconditionally. Rate-limited per source location, because a caller here
// cannot know whether a peer can drive it (see BCLog::LogRateLimiter).
#define LogPrintf(...) LogPrintLevel_(BCLog::LogFlags::NONE, BCLog::Level::None, BCLog::RateLimit::Yes, __VA_ARGS__)

// Log unconditionally, prefixing the output with the passed category name.
#define LogPrintfCategory(category, ...) LogPrintLevel_(category, BCLog::Level::None, BCLog::RateLimit::Yes, __VA_ARGS__)

// Use a macro instead of a function for conditional logging to prevent
// evaluating arguments when logging for the category is not enabled.

// Log conditionally, prefixing the output with the passed category name.
// Not rate-limited: reaching this requires -debug, and an operator who asked
// for debug output has accepted the volume that comes with it.
#define LogPrint(category, ...)                                                              \
    do {                                                                                     \
        if (LogAcceptCategory((category), BCLog::Level::Debug)) {                            \
            LogPrintLevel_(category, BCLog::Level::None, BCLog::RateLimit::No, __VA_ARGS__); \
        }                                                                                    \
    } while (0)

// Log conditionally, prefixing the output with the passed category name and severity level.
// Info and above pass the default log level without -debug, so they are
// effectively unconditional and are rate-limited on the same reasoning as
// LogPrintf. Below Info requires -debug and is not.
#define LogPrintLevel(category, level, ...)                                       \
    do {                                                                          \
        if (LogAcceptCategory((category), (level))) {                             \
            const auto rate_limit{(level) >= BCLog::Level::Info                   \
                                      ? BCLog::RateLimit::Yes                     \
                                      : BCLog::RateLimit::No};                    \
            LogPrintLevel_(category, level, rate_limit, __VA_ARGS__);             \
        }                                                                         \
    } while (0)

template <typename... Args>
bool error(const char* fmt, const Args&... args)
{
    LogPrintf("ERROR: %s\n", tfm::format(fmt, args...));
    return false;
}

#endif // BTQ_LOGGING_H
