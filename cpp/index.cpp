#include "index.h"

#include <algorithm>
#include <limits>

void LogIndex::build(const LogStorage& storage) {
    serviceIndex_.clear();
    levelIndex_.clear();
    latencyIndex_.clear();
    tsIndex_.clear();

    for (const auto& record : storage.all()) {
        serviceIndex_[record.service].push_back(record.id);
        levelIndex_[record.level].push_back(record.id);
        latencyIndex_.push_back({record.latency, record.id});
        tsIndex_.push_back({record.ts, record.id});
    }

    for (auto& entry : serviceIndex_) {
        std::sort(entry.second.begin(), entry.second.end());
    }

    for (auto& entry : levelIndex_) {
        std::sort(entry.second.begin(), entry.second.end());
    }

    std::sort(latencyIndex_.begin(), latencyIndex_.end());
    std::sort(tsIndex_.begin(), tsIndex_.end());
}

std::vector<int> LogIndex::serviceEquals(const std::string& value) const {
    const auto found = serviceIndex_.find(value);
    return found == serviceIndex_.end() ? std::vector<int>{} : found->second;
}

std::vector<int> LogIndex::levelEquals(const std::string& value) const {
    const auto found = levelIndex_.find(value);
    return found == levelIndex_.end() ? std::vector<int>{} : found->second;
}

std::vector<int> LogIndex::latencyGreaterThan(int value) const {
    const auto first = std::upper_bound(
        latencyIndex_.begin(),
        latencyIndex_.end(),
        std::pair<int, int>{value, std::numeric_limits<int>::max()});

    std::vector<int> ids;
    for (auto current = first; current != latencyIndex_.end(); ++current) {
        ids.push_back(current->second);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int> LogIndex::latencyLessThan(int value) const {
    const auto last = std::lower_bound(
        latencyIndex_.begin(),
        latencyIndex_.end(),
        std::pair<int, int>{value, std::numeric_limits<int>::min()});

    std::vector<int> ids;
    for (auto current = latencyIndex_.begin(); current != last; ++current) {
        ids.push_back(current->second);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int> LogIndex::tsGreaterThan(long long value) const {
    const auto first = std::upper_bound(
        tsIndex_.begin(),
        tsIndex_.end(),
        std::pair<long long, int>{value, std::numeric_limits<int>::max()});

    std::vector<int> ids;
    for (auto current = first; current != tsIndex_.end(); ++current) {
        ids.push_back(current->second);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int> LogIndex::tsLessThan(long long value) const {
    const auto last = std::lower_bound(
        tsIndex_.begin(),
        tsIndex_.end(),
        std::pair<long long, int>{value, std::numeric_limits<int>::min()});

    std::vector<int> ids;
    for (auto current = tsIndex_.begin(); current != last; ++current) {
        ids.push_back(current->second);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}
