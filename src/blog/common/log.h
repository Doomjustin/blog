#ifndef BLOG_COMMON_LOG_H
#define BLOG_COMMON_LOG_H

#include <cstdint>
#include <format>
#include <memory>
#include <string_view>

#include "format.h"

enum class LogLevel: std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};


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


struct log {
    log() = delete;

    static void set_level(const LogLevel level) { logger().set_level(level); }

    static auto level() noexcept -> LogLevel { return logger().level(); }

    static void set_pattern(const std::string_view pattern)
    {
        logger().set_pattern(pattern);
    }

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