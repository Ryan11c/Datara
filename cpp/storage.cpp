#include "storage.h"

#include <utility>

void LogStorage::add(LogRecord record) {
    idToOffset_[record.id] = records_.size();
    records_.push_back(std::move(record));
}

const std::vector<LogRecord>& LogStorage::all() const {
    return records_;
}

std::optional<LogRecord> LogStorage::byId(int id) const {
    const auto found = idToOffset_.find(id);
    if (found == idToOffset_.end()) {
        return std::nullopt;
    }

    return records_[found->second];
}
