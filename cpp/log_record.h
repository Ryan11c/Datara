#pragma once

#include <string>

struct LogRecord {
    int id = 0;
    long long ts = 0;
    std::string service;
    std::string level;
    int status = 0;
    int latency = 0;
    std::string message;
};
