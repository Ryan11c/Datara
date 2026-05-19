#include "index.h"
#include "parser.h"
#include "query.h"
#include "storage.h"

#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string argValue(int argc, char** argv, const std::string& name, const std::string& fallback = "") {
    for (int i = 1; i < argc - 1; ++i) {
        if (argv[i] == name) {
            return argv[i + 1];
        }
    }

    return fallback;
}

bool hasArg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }

    return false;
}

std::string jsonEscape(const std::string& input) {
    std::string output;
    for (char ch : input) {
        if (ch == '"') {
            output += "\\\"";
        } else if (ch == '\\') {
            output += "\\\\";
        } else {
            output += ch;
        }
    }
    return output;
}

void printRecord(const LogRecord& record) {
    std::cout << "{";
    std::cout << "\"id\":" << record.id << ",";
    std::cout << "\"ts\":" << record.ts << ",";
    std::cout << "\"service\":\"" << jsonEscape(record.service) << "\",";
    std::cout << "\"level\":\"" << jsonEscape(record.level) << "\",";
    std::cout << "\"status\":" << record.status << ",";
    std::cout << "\"latency\":" << record.latency << ",";
    std::cout << "\"message\":\"" << jsonEscape(record.message) << "\"";
    std::cout << "}";
}

void printJson(const QueryResult& result, const std::string& mode) {
    std::cout << "{";
    std::cout << "\"mode\":\"" << mode << "\",";
    std::cout << "\"count\":" << result.total << ",";
    std::cout << "\"page\":" << result.page << ",";
    std::cout << "\"limit\":" << result.limit << ",";
    std::cout << "\"tookMs\":" << result.tookMs << ",";
    if (!result.aggregate.empty()) {
        std::cout << "\"aggregate\":\"" << jsonEscape(result.aggregate) << "\",";
        std::cout << "\"groups\":[";
        for (size_t i = 0; i < result.groups.size(); ++i) {
            if (i > 0) {
                std::cout << ",";
            }
            std::cout << "{\"key\":\"" << jsonEscape(result.groups[i].first) << "\",\"value\":" << result.groups[i].second << "}";
        }
        std::cout << "],";
    }
    std::cout << "\"results\":[";

    for (size_t i = 0; i < result.records.size(); ++i) {
        if (i > 0) {
            std::cout << ",";
        }
        printRecord(result.records[i]);
    }

    std::cout << "]}" << std::endl;
}

std::vector<LogRecord> generateLogs(int count) {
    const std::vector<std::string> services = {"auth", "payments", "search", "billing", "checkout", "profile"};
    const std::vector<std::string> levels = {"INFO", "WARN", "ERROR"};
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> serviceDist(0, static_cast<int>(services.size() - 1));
    std::uniform_int_distribution<int> latencyDist(10, 1200);
    std::uniform_int_distribution<int> statusDist(0, 99);

    std::vector<LogRecord> records;
    records.reserve(static_cast<size_t>(count));
    for (int i = 1; i <= count; ++i) {
        const int statusRoll = statusDist(rng);
        const std::string level = statusRoll < 8 ? "ERROR" : (statusRoll < 20 ? "WARN" : "INFO");
        const int status = level == "ERROR" ? 500 : (level == "WARN" ? 429 : 200);
        records.push_back(LogRecord{
            i,
            1713900000LL + i,
            services[serviceDist(rng)],
            level,
            status,
            latencyDist(rng),
            level == "ERROR" ? "request failed" : "request completed",
        });
    }

    return records;
}

void loadRecords(LogStorage& storage, const std::vector<LogRecord>& records) {
    for (const auto& record : records) {
        storage.add(record);
    }
}

void printBenchmark(int count, const std::string& query) {
    const auto records = generateLogs(count);
    LogStorage storage;

    const auto ingestStarted = std::chrono::steady_clock::now();
    loadRecords(storage, records);
    const auto ingestEnded = std::chrono::steady_clock::now();

    const auto indexStarted = std::chrono::steady_clock::now();
    LogIndex index;
    index.build(storage);
    const auto indexEnded = std::chrono::steady_clock::now();

    const auto conditions = parseQuery(query);
    QueryOptions options;
    options.limit = 10;
    const auto naive = executeNaive(storage, conditions, options);
    QueryOptions singleThreadedOptions = options;
    singleThreadedOptions.useThreads = false;
    QueryOptions threadedOptions = options;
    threadedOptions.useThreads = true;
    const auto indexedSingle = executeIndexed(storage, index, conditions, singleThreadedOptions);
    const auto indexedThreaded = executeIndexed(storage, index, conditions, threadedOptions);
    const double singleSpeedup = naive.tookMs == 0 ? 0.0 : (1.0 - (static_cast<double>(indexedSingle.tookMs) / naive.tookMs)) * 100.0;
    const double threadedSpeedup = naive.tookMs == 0 ? 0.0 : (1.0 - (static_cast<double>(indexedThreaded.tookMs) / naive.tookMs)) * 100.0;
    const double threadingDelta = indexedSingle.tookMs == 0 ? 0.0 : (1.0 - (static_cast<double>(indexedThreaded.tookMs) / indexedSingle.tookMs)) * 100.0;

    std::cout << "{";
    std::cout << "\"records\":" << count << ",";
    std::cout << "\"query\":\"" << jsonEscape(query) << "\",";
    std::cout << "\"ingestMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(ingestEnded - ingestStarted).count() << ",";
    std::cout << "\"indexBuildMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(indexEnded - indexStarted).count() << ",";
    std::cout << "\"naiveMs\":" << naive.tookMs << ",";
    std::cout << "\"indexedSingleThreadMs\":" << indexedSingle.tookMs << ",";
    std::cout << "\"indexedThreadedMs\":" << indexedThreaded.tookMs << ",";
    std::cout << "\"singleThreadSpeedupPercent\":" << singleSpeedup << ",";
    std::cout << "\"threadedSpeedupPercent\":" << threadedSpeedup << ",";
    std::cout << "\"threadingDeltaPercent\":" << threadingDelta << ",";
    std::cout << "\"matches\":" << indexedThreaded.total;
    std::cout << "}" << std::endl;
}
}

int main(int argc, char** argv) {
    try {
        if (hasArg(argc, argv, "--benchmark")) {
            const int count = std::stoi(argValue(argc, argv, "--count", "1000000"));
            const std::string query = argValue(argc, argv, "--query", "level = ERROR AND latency > 1190");
            printBenchmark(count, query);
            return 0;
        }

        const std::string file = argValue(argc, argv, "--file");
        const std::string query = argValue(argc, argv, "--query");
        const std::string mode = argValue(argc, argv, "--mode", "indexed");
        QueryOptions options;
        options.page = std::stoi(argValue(argc, argv, "--page", "1"));
        options.limit = std::stoi(argValue(argc, argv, "--limit", "100"));
        options.aggregate = argValue(argc, argv, "--aggregate", "");
        options.useThreads = mode != "indexed_single";

        if (file.empty()) {
            throw std::runtime_error("Usage: log_engine --file data/logs.json --query \"service = auth AND latency > 100\" [--mode indexed|naive] [--page 1 --limit 50] [--aggregate count_by_service]");
        }

        LogStorage storage;
        for (const auto& record : parseLogsFile(file)) {
            storage.add(record);
        }

        const auto conditions = parseQuery(query);
        if (mode == "naive") {
            printJson(executeNaive(storage, conditions, options), mode);
            return 0;
        }

        LogIndex index;
        index.build(storage);
        printJson(executeIndexed(storage, index, conditions, options), options.useThreads ? "indexed_threaded" : "indexed_single");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
