#include "query.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <future>
#include <thread>
#include <unordered_map>
#include <stdexcept>

namespace {
std::string trim(const std::string& input) {
    const auto first = std::find_if_not(input.begin(), input.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    const auto last = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();

    return first >= last ? "" : std::string(first, last);
}

std::vector<std::string> splitAnd(const std::string& query) {
    std::vector<std::string> parts;
    std::string remaining = query;
    const std::string delimiter = " AND ";

    while (true) {
        const auto pos = remaining.find(delimiter);
        if (pos == std::string::npos) {
            parts.push_back(trim(remaining));
            break;
        }

        parts.push_back(trim(remaining.substr(0, pos)));
        remaining = remaining.substr(pos + delimiter.size());
    }

    return parts;
}

bool matches(const LogRecord& record, const Condition& condition) {
    if (condition.field == "service") {
        return condition.op == Operator::Equals && record.service == condition.value;
    }

    if (condition.field == "level") {
        return condition.op == Operator::Equals && record.level == condition.value;
    }

    const long long expected = std::stoll(condition.value);
    long long actual = 0;
    if (condition.field == "status") {
        actual = record.status;
    } else if (condition.field == "latency") {
        actual = record.latency;
    } else if (condition.field == "ts") {
        actual = record.ts;
    } else {
        throw std::runtime_error("Unknown query field: " + condition.field);
    }

    if (condition.op == Operator::Equals) {
        return actual == expected;
    }
    if (condition.op == Operator::GreaterThan) {
        return actual > expected;
    }
    return actual < expected;
}

bool matchesAll(const LogRecord& record, const std::vector<Condition>& conditions) {
    for (const auto& condition : conditions) {
        if (!matches(record, condition)) {
            return false;
        }
    }

    return true;
}

std::vector<int> intersectSorted(const std::vector<int>& left, const std::vector<int>& right) {
    std::vector<int> output;
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(output));
    return output;
}

QueryResult materializeResult(
    const std::vector<LogRecord>& matches,
    const QueryOptions& options,
    long long tookMs
) {
    QueryResult result;
    result.total = matches.size();
    result.page = std::max(1, options.page);
    result.limit = std::max(1, options.limit);
    result.aggregate = options.aggregate;
    result.tookMs = tookMs;

    if (options.aggregate == "count") {
        result.groups.push_back({"count", static_cast<double>(matches.size())});
        return result;
    }

    if (options.aggregate == "count_by_service") {
        if (!options.useThreads || matches.size() < 10000) {
            std::unordered_map<std::string, double> counts;
            for (const auto& record : matches) {
                counts[record.service] += 1.0;
            }
            for (const auto& entry : counts) {
                result.groups.push_back(entry);
            }
            std::sort(result.groups.begin(), result.groups.end());
            return result;
        }

        const unsigned int workerCount = std::max(1u, std::thread::hardware_concurrency());
        const size_t chunkSize = std::max<size_t>(1, matches.size() / workerCount);
        std::vector<std::future<std::unordered_map<std::string, double>>> futures;

        for (size_t start = 0; start < matches.size(); start += chunkSize) {
            const size_t end = std::min(matches.size(), start + chunkSize);
            futures.push_back(std::async(std::launch::async, [start, end, &matches]() {
                std::unordered_map<std::string, double> localCounts;
                for (size_t i = start; i < end; ++i) {
                    localCounts[matches[i].service] += 1.0;
                }
                return localCounts;
            }));
        }

        std::unordered_map<std::string, double> counts;
        for (auto& future : futures) {
            const auto partial = future.get();
            for (const auto& entry : partial) {
                counts[entry.first] += entry.second;
            }
        }

        for (const auto& entry : counts) {
            result.groups.push_back(entry);
        }
        std::sort(result.groups.begin(), result.groups.end());
        return result;
    }

    if (options.aggregate == "avg_latency_by_service") {
        struct ServiceStats {
            std::unordered_map<std::string, double> sums;
            std::unordered_map<std::string, double> counts;
        };

        if (!options.useThreads || matches.size() < 10000) {
            ServiceStats stats;
            for (const auto& record : matches) {
                stats.sums[record.service] += record.latency;
                stats.counts[record.service] += 1.0;
            }
            for (const auto& entry : stats.sums) {
                result.groups.push_back({entry.first, entry.second / stats.counts[entry.first]});
            }
            std::sort(result.groups.begin(), result.groups.end());
            return result;
        }

        const unsigned int workerCount = std::max(1u, std::thread::hardware_concurrency());
        const size_t chunkSize = std::max<size_t>(1, matches.size() / workerCount);
        std::vector<std::future<ServiceStats>> futures;

        for (size_t start = 0; start < matches.size(); start += chunkSize) {
            const size_t end = std::min(matches.size(), start + chunkSize);
            futures.push_back(std::async(std::launch::async, [start, end, &matches]() {
                ServiceStats local;
                for (size_t i = start; i < end; ++i) {
                    local.sums[matches[i].service] += matches[i].latency;
                    local.counts[matches[i].service] += 1.0;
                }
                return local;
            }));
        }

        std::unordered_map<std::string, double> sums;
        std::unordered_map<std::string, double> counts;
        for (auto& future : futures) {
            const auto partial = future.get();
            for (const auto& entry : partial.sums) {
                sums[entry.first] += entry.second;
            }
            for (const auto& entry : partial.counts) {
                counts[entry.first] += entry.second;
            }
        }

        for (const auto& entry : sums) {
            result.groups.push_back({entry.first, entry.second / counts[entry.first]});
        }
        std::sort(result.groups.begin(), result.groups.end());
        return result;
    }

    const size_t start = static_cast<size_t>(result.page - 1) * static_cast<size_t>(result.limit);
    if (start >= matches.size()) {
        return result;
    }

    const size_t end = std::min(matches.size(), start + static_cast<size_t>(result.limit));
    result.records.assign(matches.begin() + start, matches.begin() + end);
    return result;
}

std::vector<int> indexedIdsForCondition(const LogIndex& index, const Condition& condition, bool& canUseIndex) {
    canUseIndex = true;
    if (condition.field == "service" && condition.op == Operator::Equals) {
        return index.serviceEquals(condition.value);
    }
    if (condition.field == "level" && condition.op == Operator::Equals) {
        return index.levelEquals(condition.value);
    }
    if (condition.field == "latency" && condition.op == Operator::GreaterThan) {
        return index.latencyGreaterThan(std::stoi(condition.value));
    }
    if (condition.field == "latency" && condition.op == Operator::LessThan) {
        return index.latencyLessThan(std::stoi(condition.value));
    }
    if (condition.field == "ts" && condition.op == Operator::GreaterThan) {
        return index.tsGreaterThan(std::stoll(condition.value));
    }
    if (condition.field == "ts" && condition.op == Operator::LessThan) {
        return index.tsLessThan(std::stoll(condition.value));
    }

    canUseIndex = false;
    return {};
}
}

std::vector<Condition> parseQuery(const std::string& query) {
    if (trim(query).empty()) {
        return {};
    }

    std::vector<Condition> conditions;
    for (const auto& part : splitAnd(query)) {
        const auto equalsPos = part.find('=');
        const auto greaterPos = part.find('>');
        const auto lessPos = part.find('<');

        size_t opPos = std::string::npos;
        Operator op = Operator::Equals;
        if (equalsPos != std::string::npos) {
            opPos = equalsPos;
            op = Operator::Equals;
        } else if (greaterPos != std::string::npos) {
            opPos = greaterPos;
            op = Operator::GreaterThan;
        } else if (lessPos != std::string::npos) {
            opPos = lessPos;
            op = Operator::LessThan;
        }

        if (opPos == std::string::npos) {
            throw std::runtime_error("Invalid condition: " + part);
        }

        conditions.push_back(Condition{
            trim(part.substr(0, opPos)),
            op,
            trim(part.substr(opPos + 1)),
        });
    }

    return conditions;
}

QueryResult executeNaive(const LogStorage& storage, const std::vector<Condition>& conditions, const QueryOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    std::vector<LogRecord> matches;

    for (const auto& record : storage.all()) {
        if (matchesAll(record, conditions)) {
            matches.push_back(record);
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    return materializeResult(matches, options, std::chrono::duration_cast<std::chrono::milliseconds>(ended - started).count());
}

QueryResult executeIndexed(const LogStorage& storage, const LogIndex& index, const std::vector<Condition>& conditions, const QueryOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::vector<int>> indexedCandidates;

    if (options.useThreads && conditions.size() > 1) {
        std::vector<std::future<std::pair<bool, std::vector<int>>>> futures;
        for (const auto& condition : conditions) {
            futures.push_back(std::async(std::launch::async, [&index, condition]() {
                bool canUseIndex = false;
                auto ids = indexedIdsForCondition(index, condition, canUseIndex);
                return std::make_pair(canUseIndex, std::move(ids));
            }));
        }

        for (auto& future : futures) {
            auto resolved = future.get();
            if (resolved.first) {
                indexedCandidates.push_back(std::move(resolved.second));
            }
        }
    } else {
        for (const auto& condition : conditions) {
            bool canUseIndex = false;
            auto ids = indexedIdsForCondition(index, condition, canUseIndex);
            if (canUseIndex) {
                indexedCandidates.push_back(std::move(ids));
            }
        }
    }

    if (indexedCandidates.empty()) {
        return executeNaive(storage, conditions, options);
    }

    std::sort(indexedCandidates.begin(), indexedCandidates.end(), [](const auto& left, const auto& right) {
        return left.size() < right.size();
    });

    std::vector<int> candidateIds = indexedCandidates.front();
    for (size_t i = 1; i < indexedCandidates.size(); ++i) {
        candidateIds = intersectSorted(candidateIds, indexedCandidates[i]);
    }

    std::vector<LogRecord> matches;
    for (int id : candidateIds) {
        const auto record = storage.byId(id);
        if (record.has_value() && matchesAll(*record, conditions)) {
            matches.push_back(*record);
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    return materializeResult(matches, options, std::chrono::duration_cast<std::chrono::milliseconds>(ended - started).count());
}
