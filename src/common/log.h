#ifndef BLOG_COMMON_LOG_H
#define BLOG_COMMON_LOG_H

#include <cstdint>
#include <format>
#include <memory>
#include <string_view>

#include <common/format.h>

enum class LogLevel: std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};


/**
 * @brief Abstract base for pluggable log sinks.
 *
 * Derived classes implement `log()`, `set_level_impl()`, and
 * `set_pattern_impl()` to target specific backends (e.g. spdlog, stderr).
 * The public API uses `std::format`-style formatting and filters by
 * the active log level before delegating to the virtual sink.
 */
class Logger {
public:
    Logger() = default;

    Logger(const Logger&) = delete;
    auto operator=(const Logger&) -> Logger& = delete;

    Logger(Logger&&) = default;
    auto operator=(Logger&&) -> Logger& = default;

    virtual ~Logger() = default;

    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Critical, std::format(fmt, std::forward<Args>(args)...));
    }

    void set_level(const LogLevel level) noexcept
    {
        level_ = level;
        set_level_impl(level);
    }

    [[nodiscard]]
    constexpr auto level() const noexcept -> LogLevel
    {
        return level_;
    }

    /**
     * @brief Set the output format pattern on the underlying logger backend.
     *
     * @param pattern Format string accepted by the underlying backend.
     */
    void set_pattern(const std::string_view pattern)
    {
        set_pattern_impl(pattern);
    }

private:
    LogLevel level_ = LogLevel::Info;

    virtual void log(LogLevel level, std::string_view message) = 0;

    virtual void set_level_impl(LogLevel level) = 0;

    virtual void set_pattern_impl(std::string_view pattern) = 0;
};


/**
 * @brief Static façade over the process-wide default `Logger` instance.
 *
 * All methods forward to the logger registered via `set_default_logger()`.
 * Logging is disabled (no-op) until a default logger is installed.
 */
struct log {
    log() = delete;

    /** @brief Set the minimum level below which messages are discarded. */
    static void set_level(const LogLevel level) { logger().set_level(level); }

    /** @brief Return the currently active log level. */
    static auto level() noexcept -> LogLevel { return logger().level(); }

    /** @brief Set the output format pattern on the default logger. */
    static void set_pattern(const std::string_view pattern)
    {
        logger().set_pattern(pattern);
    }

    /**
     * @brief Replace the global logger instance.
     *
     * Must be called before any logging occurs. Not thread-safe;
     * call once during program initialization.
     *
     * @param logger New logger implementation to install.
     */
    static void set_default_logger(std::unique_ptr<Logger> logger)
    {
        default_logger = std::move(logger);
    }

    template <typename... Args>
    static void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().warning(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void critical(std::format_string<Args...> fmt, Args&&... args)
    {
        logger().critical(fmt, std::forward<Args>(args)...);
    }

private:
    static std::unique_ptr<Logger> default_logger;

    static auto logger() -> Logger& { return *default_logger; }
};

#endif // BLOG_COMMON_LOG_H