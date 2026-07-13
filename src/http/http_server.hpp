#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class HttpServer {
public:
    using GetStatusHandler = std::function<std::string()>;
    using UpdateConfigHandler = std::function<std::pair<int, std::string>(const std::string&)>;

    HttpServer();
    ~HttpServer();

    bool Start(int port,
               GetStatusHandler get_status_handler,
               UpdateConfigHandler update_config_handler,
               std::string* error_message = nullptr);

    void Stop();

private:
    void AcceptLoop();
    std::string BuildHttpResponse(int status_code,
                                  const std::string& status_text,
                                  const std::string& body,
                                  const std::string& content_type = "application/json") const;

    int ReadRequest(int client_fd,
                    std::string* method,
                    std::string* path,
                    std::string* body) const;

    int server_fd_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    GetStatusHandler get_status_handler_;
    UpdateConfigHandler update_config_handler_;
};
