#pragma once

#include "log_record.h"

#include <optional>
#include <unordered_map>
#include <vector>

class LogStorage {
public:
    void add(LogRecord record);
    const std::vector<LogRecord>& all() const;
    std::optional<LogRecord> byId(int id) const;

private:
    std::vector<LogRecord> records_;
    std::unordered_map<int, size_t> idToOffset_;
};
