#pragma once
#include <string>

class WebServer {
public:
    static WebServer& getInstance();
    
    void start();
    void stop();
    std::string getLocalIP();

private:
    WebServer() = default;
    ~WebServer();

    void pollServer();
    static void* handleConnection(void* arg);

    int server_fd = -1;
    bool is_running = false;
};
