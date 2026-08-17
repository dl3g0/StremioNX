#include "web_server.hpp"
#include "addon_manager.hpp"
#include "logger.hpp"
#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

#include <cstring>
#include <vector>

WebServer& WebServer::getInstance() {
    static WebServer instance;
    return instance;
}

WebServer::~WebServer() {
    stop();
}

static std::string urldecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                int hex;
                sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex);
                result += static_cast<char>(hex);
                i += 2;
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

void WebServer::start() {
    if (is_running) return;
    is_running = true;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG("[WebServer] Failed to create socket");
        is_running = false;
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG("[WebServer] Bind failed");
        close(server_fd);
        is_running = false;
        return;
    }
    
    if (listen(server_fd, 5) < 0) {
        LOG("[WebServer] Listen failed");
        close(server_fd);
        is_running = false;
        return;
    }
    
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    LOG("[WebServer] Listening on 0.0.0.0:8080");
    
    pollServer();
}

void WebServer::pollServer() {
    if (!is_running) return;

    brls::async([this]() {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        
        if (client_socket >= 0) {
            // Handle each connection on a dedicated thread with a generous
            // stack. addAddon() performs blocking HTTP + JSON parsing, which
            // would overflow the small borealis task-loop thread stack.
            pthread_t thread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 512 * 1024);
            pthread_create(&thread, &attr, WebServer::handleConnection, (void*)(intptr_t)client_socket);
            pthread_detach(thread);
            pthread_attr_destroy(&attr);
            
            // Schedule the next poll immediately since we found a client
            brls::delay(10, [this]() { pollServer(); });
        } else {
            // No client, wait 200ms before polling again
            brls::delay(200, [this]() { pollServer(); });
        }
    });
}

void* WebServer::handleConnection(void* arg) {
    int client_socket = (int)(intptr_t)arg;
    
    char buffer[4096] = {0};
    int bytes_read = recv(client_socket, buffer, 4095, 0);
    if (bytes_read > 0) {
        std::string request(buffer);
        if (request.find("GET / ") == 0) {
            std::string html = R"(HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>StremioNX Addons</title>
    <style>
        body { background-color: #181227; color: #fff; font-family: sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; }
        h1 { margin-bottom: 20px; }
        form { display: flex; flex-direction: column; width: 300px; }
        input[type="text"] { padding: 10px; margin-bottom: 10px; border: 1px solid #7b5bf5; border-radius: 5px; background: #282237; color: white; }
        input[type="submit"] { padding: 10px; background-color: #7b5bf5; color: white; border: none; border-radius: 5px; cursor: pointer; }
        input[type="submit"]:hover { background-color: #9275f7; }
    </style>
</head>
<body>
    <h1>StremioNX Addons</h1>
    <form action="/add" method="POST">
        <input type="text" name="url" placeholder="https://.../manifest.json" required>
        <input type="submit" value="Añadir Addon">
    </form>
</body>
</html>)";
            send(client_socket, html.c_str(), html.length(), 0);
        } else if (request.find("POST /add") == 0) {
            size_t body_pos = request.find("\r\n\r\n");
            if (body_pos != std::string::npos && body_pos + 4 < request.length()) {
                std::string body = request.substr(body_pos + 4);
                if (body.find("url=") == 0) {
                    std::string url = body.substr(4);
                    // The form is the only field; drop anything past "&" just in case.
                    size_t amp = url.find('&');
                    if (amp != std::string::npos) url = url.substr(0, amp);
                    url = urldecode(url);
                    
                    // Defensive cleanup: some browsers append trailing junk
                    // (or the request was read together with leftover data).
                    // A manifest URL must end with "manifest.json".
                    size_t mj = url.find("manifest.json");
                    if (mj != std::string::npos) {
                        url = url.substr(0, mj) + "manifest.json";
                    } else {
                        // Fallback: cut at the first space/control char.
                        size_t sp = url.find_first_of(" \t\r\n<");
                        if (sp != std::string::npos) url = url.substr(0, sp);
                    }
                    
                    // Only accept well-formed URLs.
                    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
                        LOG("[WebServer] Adding addon: " + url);
                        AddonManager::getInstance().addAddon(url);
                        std::string html = R"(HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>StremioNX Addons</title>
    <style>
        body { background-color: #181227; color: #fff; font-family: sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; }
        a { color: #7b5bf5; text-decoration: none; margin-top: 20px; }
    </style>
</head>
<body>
    <h1>Addon Añadido con Éxito</h1>
    <a href="/">Volver</a>
</body>
</html>)";
                        send(client_socket, html.c_str(), html.length(), 0);
                    } else {
                        LOG("[WebServer] ERROR: invalid addon URL rejected: " + url);
                        std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<h1>URL inv\u00e1lida</h1>";
                        send(client_socket, resp.c_str(), resp.length(), 0);
                    }
                }
            }
        } else {
            std::string resp = "HTTP/1.1 404 Not Found\r\n\r\nNot Found";
            send(client_socket, resp.c_str(), resp.length(), 0);
        }
    }
    close(client_socket);
    return nullptr;
}

void WebServer::stop() {
    is_running = false;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

std::string WebServer::getLocalIP() {
#ifdef __SWITCH__
    u32 ip = 0;
    nifmGetCurrentIpAddress(&ip);
    struct in_addr addr;
    addr.s_addr = ip;
    return std::string(inet_ntoa(addr));
#else
    return "127.0.0.1";
#endif
}
