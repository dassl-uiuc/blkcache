#pragma once

#include "fmt/core.h"
#include "magic_enum.hpp"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

using namespace fmt::literals;

inline void initialize_logger() {
  static bool is_initialized = false;
  if (!is_initialized) {
    is_initialized = true;
    // auto console = spdlog::stdout_logger_mt("console");
    // spdlog::set_default_logger(console);
    // spdlog::set_pattern("[source %s] [function %!] [line %#] %v");
  }
}

template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args &&...args) {
  initialize_logger();
  const auto message = fmt::format(fmt, std::forward<Args>(args)...);
  spdlog::info(message);
}

template <typename... Args>
void debug(fmt::format_string<Args...> fmt, Args &&...args) {
  initialize_logger();
  const auto message = fmt::format(fmt, std::forward<Args>(args)...);
  spdlog::debug(message);
}

template <typename... Args>
void panic(fmt::format_string<Args...> fmt, Args &&...args) {
  initialize_logger();
  const auto message = fmt::format(fmt, std::forward<Args>(args)...);
  spdlog::error(message);
  exit(-1);
}