#pragma once

#include "index.h"
#include "storage.h"

#include <string>
#include <vector>

enum class Operator {
    Equals,
    GreaterThan,
    LessThan,
};

struct Condition {
    std::string field;
    Operator op;
    std::string value;
};

struct QueryResult {
    std::vector<LogRecord> records;
    std::vector<std::pair<std::string, double>> groups;
    std::string aggregate;
    size_t total = 0;
    int page = 1;
    int limit = 100;
    long long tookMs = 0;
};

struct QueryOptions {
    int page = 1;
    int limit = 100;
    std::string aggregate;
    bool useThreads = true;
};

std::vector<Condition> parseQuery(const std::string& query);
QueryResult executeNaive(const LogStorage& storage, const std::vector<Condition>& conditions, const QueryOptions& options = {});
QueryResult executeIndexed(const LogStorage& storage, const LogIndex& index, const std::vector<Condition>& conditions, const QueryOptions& options = {});
