#include "vynx_http/logger.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace vynx_http {

namespace {

tm safe_localtime(const time_t& time) {
    tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

}  // namespace

// Console sink implementation
void console_sink::log(const log_message& msg) {
    auto time = std::chrono::system_clock::to_time_t(msg.timestamp);
    auto tm = safe_localtime(time);

    std::scoped_lock lock(mutex_);
    std::cout << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
              << "[" << to_string(msg.level) << "] "
              << "[" << msg.component << "] " << msg.message << '\n';
}

// File sink implementation
struct file_sink::impl {
    std::ofstream file;
    std::mutex mutex;
};

file_sink::file_sink(std::string_view filename) : impl_(std::make_unique<impl>()) {
    impl_->file.open(std::string(filename), std::ios::app);
}

file_sink::~file_sink() = default;

void file_sink::log(const log_message& msg) {
    if (!impl_->file.is_open()) {
        return;
    }

    auto time = std::chrono::system_clock::to_time_t(msg.timestamp);
    auto tm = safe_localtime(time);

    std::scoped_lock lock(impl_->mutex);
    impl_->file << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                << "[" << to_string(msg.level) << "] "
                << "[" << msg.component << "] " << msg.message << '\n';
}

// Logger implementation
logger& logger::instance() {
    static logger instance;
    return instance;
}

logger::logger() {
    // Add default console sink
    add_sink(std::make_shared<console_sink>());
}

logger::~logger() = default;

void logger::set_level(log_level level) noexcept {
    level_ = level;
}

log_level logger::level() const noexcept {
    return level_;
}

void logger::add_sink(std::shared_ptr<log_sink> sink) {
    sinks_.push_back(std::move(sink));
}

void logger::clear_sinks() {
    sinks_.clear();
}

void logger::log(log_level level, std::string_view component, std::string_view message) {
    if (level < level_) {
        return;
    }

    const log_message msg{.level = level,
                          .component = component,
                          .message = message,
                          .timestamp = std::chrono::system_clock::now()};

    for (auto& sink : sinks_) {
        sink->log(msg);
    }
}

void logger::trace(std::string_view component, std::string_view message) {
    log(log_level::trace, component, message);
}

void logger::debug(std::string_view component, std::string_view message) {
    log(log_level::debug, component, message);
}

void logger::info(std::string_view component, std::string_view message) {
    log(log_level::info, component, message);
}

void logger::warn(std::string_view component, std::string_view message) {
    log(log_level::warn, component, message);
}

void logger::error(std::string_view component, std::string_view message) {
    log(log_level::error, component, message);
}

void logger::fatal(std::string_view component, std::string_view message) {
    log(log_level::fatal, component, message);
}

}  // namespace vynx_http
