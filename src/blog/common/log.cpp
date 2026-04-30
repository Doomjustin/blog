#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

class Spdlog : public Logger {
public:
    Spdlog()
    {
        const auto logger = spdlog::stdout_color_mt("xin");
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] %^[%l]%$ %v");
        spdlog::set_default_logger(logger);

        spdlog::set_level(spdlog::level::info);
    }

    virtual ~Spdlog() = default;

private:
    void log(const LogLevel level, const std::string_view message) override
    {
        switch (level) {
        case LogLevel::Trace:
            spdlog::trace(message);
            break;
        case LogLevel::Debug:
            spdlog::debug(message);
            break;
        case LogLevel::Info:
            spdlog::info(message);
            break;
        case LogLevel::Warning:
            spdlog::warn(message);
            break;
        case LogLevel::Error:
            spdlog::error(message);
            break;
        case LogLevel::Critical:
            spdlog::critical(message);
            break;
        }
    }

    void set_level_impl(const LogLevel level) noexcept override
    {
        switch (level) {
        case LogLevel::Trace:
            spdlog::set_level(spdlog::level::trace);
            break;
        case LogLevel::Debug:
            spdlog::set_level(spdlog::level::debug);
            break;
        case LogLevel::Info:
            spdlog::set_level(spdlog::level::info);
            break;
        case LogLevel::Warning:
            spdlog::set_level(spdlog::level::warn);
            break;
        case LogLevel::Error:
            spdlog::set_level(spdlog::level::err);
            break;
        case LogLevel::Critical:
            spdlog::set_level(spdlog::level::critical);
            break;
        }
    }

    void set_pattern_impl(const std::string_view pattern) override
    {
        spdlog::set_pattern(std::string{ pattern });
    }
};

} // namespace


std::unique_ptr<Logger> log::default_logger = std::make_unique<Spdlog>();