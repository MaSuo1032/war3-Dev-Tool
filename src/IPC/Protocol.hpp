#pragma once
#include <string>

namespace War3Dev {

// Named Pipe name
constexpr const char* WAR3_DEV_PIPE_NAME = "\\\\.\\pipe\\War3JassDevTool";

// Command prefixes
constexpr const char* CMD_ANTIOVER = "ANTIOVER:";
constexpr const char* CMD_PERCENT  = "PERCENT:";
constexpr const char* CMD_MALL     = "MALL:";
constexpr const char* CMD_KILL     = "KILL";
constexpr const char* CMD_SAVE     = "SAVE:";
constexpr const char* CMD_NATIVE   = "NATIVE:";

} // namespace War3Dev
