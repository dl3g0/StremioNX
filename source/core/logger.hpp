#pragma once
#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    static Logger& getInstance();
    void log(const std::string& message);
    
private:
    Logger();
    ~Logger();
    std::ofstream log_file;
    std::mutex log_mutex;
};

#include <borealis.hpp>
#define LOG(msg) brls::Logger::info("{}", msg)
