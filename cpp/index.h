#pragma once

#include "storage.h"

#include <string>
#include <unordered_map>
#include <vector>

class LogIndex {
public:
    void build(const LogStorage& storage);

    std::vector<int> serviceEquals(const std::string& value) const;
    std::vector<int> levelEquals(const std::string& value) const;
    std::vector<int> latencyGreaterThan(int value) const;
    std::vector<int> latencyLessThan(int value) const;
    std::vector<int> tsGreaterThan(long long value) const;
    std::vector<int> tsLessThan(long long value) const;

private:
    std::unordered_map<std::string, std::vector<int>> serviceIndex_;
    std::unordered_map<std::string, std::vector<int>> levelIndex_;
    std::vector<std::pair<int, int>> latencyIndex_;
    std::vector<std::pair<long long, int>> tsIndex_;
};
