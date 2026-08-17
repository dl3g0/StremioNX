#include "logger.hpp"
#include "file_paths.hpp"

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    FilePaths::ensureDataDir();
    log_file.open(FilePaths::kLogFile, std::ios::out | std::ios::app);
    if (!log_file.is_open()) {
        log_file.open("stremionx_log.txt", std::ios::out | std::ios::app);
    }
    log("=========================================");
    log("=== STREMIONX STARTED ===");
}

Logger::~Logger() {
    if (log_file.is_open()) {
        log("=== STREMIONX CLOSED ===");
        log_file.close();
    }
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file.is_open()) {
        log_file << message << std::endl;
        log_file.flush();
    }
}
