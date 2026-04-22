#include <iostream>
#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    httplib::Server server;

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"status", "ok"},
            {"service", "order-books"}
        };
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/api/orders", [](const httplib::Request&, httplib::Response& res) {
        
    });

    server.Post("/api/orders", [](const httplib::Request& req, httplib::Response& res) {
       
    });

    std::cout << "Server listening on http://localhost:8080" << std::endl;
    return server.listen("0.0.0.0", 8080) ? 0 : 1;
}