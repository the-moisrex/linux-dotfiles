#include "pch.h"

using namespace spdlog;

int main() {
  info("Welcome to spdlog!");
  error("Some error message with arg: {}", 1);

  warn("Easy padding in numbers like {:08d}", 12);
  critical("Support for int: {0:d};  hex: {0:x};  oct: {0:o}; bin: {0:b}", 42);
  info("Support for floats {:03.2f}", 1.23456);
  info("Positional args are {1} {0}..", "too", "supported");
  info("{:<30}", "left aligned");

  set_level(level::debug); // Set global log level to debug
  debug("This message should be displayed..");

  // change log pattern
  set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");

  // Compile time log levels
  // define SPDLOG_ACTIVE_LEVEL to desired level
  SPDLOG_TRACE("Some trace message with param {}", 42);
  SPDLOG_DEBUG("Some debug message");

  // Set the default logger to file logger
  auto file_logger = basic_logger_mt("basic_logger", "logs/basic.txt");
  set_default_logger(file_logger);
}
