#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace vynx_http {

// Log levels
enum class log_level { trace, debug, info, warn, error, fatal };

// Convert log level to string
constexpr std::string_view to_string(log_level level) noexcept {
    switch (level) {
    case log_level::trace:
        return "TRACE";
    case log_level::debug:
        return "DEBUG";
    case log_level::info:
        return "INFO";
    case log_level::warn:
        return "WARN";
    case log_level::error:
        return "ERROR";
    case log_level::fatal:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

// Log message structure
struct log_message {
    log_level level;
    std::string_view component;
    std::string_view message;
    std::chrono::system_clock::time_point timestamp;
};

// Log sink interface
class log_sink {
   public:
    virtual ~log_sink() = default;
    virtual void log(const log_message& msg) = 0;
};

// Console log sink
class console_sink : public log_sink {
   public:
    void log(const log_message& msg) override;

   private:
    std::mutex mutex_;
};

// File log sink
class file_sink : public log_sink {
   public:
    explicit file_sink(std::string_view filename);
    ~file_sink() override;

    void log(const log_message& msg) override;

   private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// Logger class
class logger {
   public:
    // Get singleton instance
    static logger& instance();

    // Set log level
    void set_level(log_level level) noexcept;

    // Get current log level
    [[nodiscard]] log_level level() const noexcept;

    // Add a log sink
    void add_sink(std::shared_ptr<log_sink> sink);

    // Remove all sinks
    void clear_sinks();

    // Log a message
    void log(log_level level, std::string_view component, std::string_view message);

    // Convenience methods
    void trace(std::string_view component, std::string_view message);
    void debug(std::string_view component, std::string_view message);
    void info(std::string_view component, std::string_view message);
    void warn(std::string_view component, std::string_view message);
    void error(std::string_view component, std::string_view message);
    void fatal(std::string_view component, std::string_view message);

   private:
    logger();
    ~logger();

    log_level level_{log_level::info};
    std::vector<std::shared_ptr<log_sink>> sinks_;
};

// Convenience macros
#define VYNX_LOG_TRACE(component, message) vynx_http::logger::instance().trace(component, message)

#define VYNX_LOG_DEBUG(component, message) vynx_http::logger::instance().debug(component, message)

#define VYNX_LOG_INFO(component, message) vynx_http::logger::instance().info(component, message)

#define VYNX_LOG_WARN(component, message) vynx_http::logger::instance().warn(component, message)

#define VYNX_LOG_ERROR(component, message) vynx_http::logger::instance().error(component, message)

#define VYNX_LOG_FATAL(component, message) vynx_http::logger::instance().fatal(component, message)

}  // namespace vynx_http
