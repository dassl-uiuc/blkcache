#pragma once

#include "fmt/core.h"
#include "magic_enum.hpp"
#include "spdlog/spdlog.h"

using namespace fmt::literals;

template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args &&...args) {
  const auto message = fmt::format(fmt, std::forward<Args>(args)...);
  spdlog::info(message);
}

template <typename... Args>
void panic(fmt::format_string<Args...> fmt, Args &&...args) {
  const auto message = fmt::format(fmt, std::forward<Args>(args)...);
  spdlog::error(message);
  exit(-1);
}