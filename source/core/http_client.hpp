#pragma once
#include <string>
#include <vector>

class HttpClient {
public:
    static HttpClient& getInstance();

    // Inicia curl globalmente
    bool init();
    void cleanup();

    // Obtener texto (ej. JSON)
    bool get(const std::string& url, std::string& response);

    // Obtener datos binarios (ej. Imagenes)
    bool getBinary(const std::string& url, std::vector<unsigned char>& buffer);

private:
    HttpClient() = default;
    ~HttpClient() = default;
};
