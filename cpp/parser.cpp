#include "parser.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
std::string readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open logs file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

int readInt(const std::string& object, const std::string& field) {
    const std::regex pattern("\"" + field + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        throw std::runtime_error("Missing numeric field: " + field);
    }
    return std::stoi(match[1].str());
}

long long readLongLong(const std::string& object, const std::string& field, long long fallback) {
    const std::regex pattern("\"" + field + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return fallback;
    }
    return std::stoll(match[1].str());
}

std::string readString(const std::string& object, const std::string& field) {
    const std::regex pattern("\"" + field + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        throw std::runtime_error("Missing string field: " + field);
    }
    return match[1].str();
}

std::string readString(const std::string& object, const std::string& field, const std::string& fallback) {
    const std::regex pattern("\"" + field + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return fallback;
    }
    return match[1].str();
}
}

std::vector<LogRecord> parseLogsFile(const std::string& path) {
    const std::string contents = readFile(path);
    const std::regex objectPattern("\\{[^\\}]*\\}");
    std::sregex_iterator current(contents.begin(), contents.end(), objectPattern);
    std::sregex_iterator end;
    std::vector<LogRecord> records;

    for (; current != end; ++current) {
        const std::string object = current->str();
        records.push_back(LogRecord{
            readInt(object, "id"),
            readLongLong(object, "ts", 0),
            readString(object, "service"),
            readString(object, "level"),
            readInt(object, "status"),
            readInt(object, "latency"),
            readString(object, "message", ""),
        });
    }

    return records;
}
