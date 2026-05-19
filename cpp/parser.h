#pragma once

#include "log_record.h"

#include <string>
#include <vector>

std::vector<LogRecord> parseLogsFile(const std::string& path);
