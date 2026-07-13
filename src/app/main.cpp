#include "config/config.hpp"
#include "http/http_server.hpp"
#include "streaming/streamer.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void SignalHandler(int) {
    g_shutdown_requested = 1;
}

std::string ToUpperAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return value;
}

std::string JsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);

    for (char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

bool HasKey(const std::string& body, const std::string& key) {
    const std::regex re("\"" + key + "\"\\s*:");
    return std::regex_search(body, re);
}

std::optional<std::string> GetStringField(const std::string& body, const std::string& key) {
    const std::regex re("\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"");
    std::smatch match;
    if (std::regex_search(body, match, re)) {
        return match[1].str();
    }
    return std::nullopt;
}

std::optional<int> GetIntField(const std::string& body, const std::string& key) {
    const std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (std::regex_search(body, match, re)) {
        try {
            return std::stoi(match[1].str());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<bool> GetBoolField(const std::string& body, const std::string& key) {
    const std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(body, match, re)) {
        return match[1].str() == "true";
    }
    return std::nullopt;
}

std::string BuildStatusJson(const StreamerConfig& cfg, bool running) {
    std::ostringstream ss;
    ss << "{"
       << "\"status\":\"" << (running ? "running" : "stopped") << "\"," 
       << "\"mode\":\"" << JsonEscape(cfg.mode) << "\"," 
       << "\"device\":\"" << JsonEscape(cfg.device) << "\"," 
       << "\"codec\":\"" << JsonEscape(cfg.codec) << "\"," 
       << "\"target_ip\":\"" << JsonEscape(cfg.target_ip) << "\"," 
       << "\"target_port\":" << cfg.target_port << ","
       << "\"custom_pipeline\":\"" << JsonEscape(cfg.custom_pipeline) << "\"," 
       << "\"use_test_source\":" << (cfg.use_test_source ? "true" : "false")
       << "}";
    return ss.str();
}

std::string BuildErrorJson(const std::string& message) {
    return "{\"result\":\"error\",\"message\":\"" + JsonEscape(message) + "\"}";
}

std::string BuildSuccessJson(const std::string& message) {
    return "{\"result\":\"success\",\"message\":\"" + JsonEscape(message) + "\"}";
}

}  // namespace

int main() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    Streamer streamer;
    HttpServer http_server;

    StreamerConfig config;
    std::mutex app_mutex;

    {
        std::lock_guard<std::mutex> lock(app_mutex);
        std::string err;
        const bool started = streamer.StartSimple(
            config.use_test_source,
            config.device,
            config.codec,
            config.target_ip,
            config.target_port,
            &err);
        if (!started) {
            std::cerr << "[main] failed to start default pipeline: " << err << std::endl;
        }
    }

    auto get_status_handler = [&]() -> std::string {
        std::lock_guard<std::mutex> lock(app_mutex);
        return BuildStatusJson(config, streamer.IsRunning());
    };

    auto update_config_handler = [&](const std::string& body) -> std::pair<int, std::string> {
        std::lock_guard<std::mutex> lock(app_mutex);

        StreamerConfig new_cfg = config;
        StreamerConfig old_cfg = config;

        if (HasKey(body, "mode")) {
            const auto mode = GetStringField(body, "mode");
            if (!mode) {
                return {400, BuildErrorJson("invalid type for mode")};
            }
            new_cfg.mode = *mode;
        }

        if (HasKey(body, "device")) {
            const auto device = GetStringField(body, "device");
            if (!device) {
                return {400, BuildErrorJson("invalid type for device")};
            }
            new_cfg.device = *device;
        }

        if (HasKey(body, "codec")) {
            const auto codec = GetStringField(body, "codec");
            if (!codec) {
                return {400, BuildErrorJson("invalid type for codec")};
            }
            new_cfg.codec = ToUpperAscii(*codec);
            if (new_cfg.codec != "H264" && new_cfg.codec != "H265" && new_cfg.codec != "HEVC") {
                return {400, BuildErrorJson("codec must be H264 or H265")};
            }
            if (new_cfg.codec == "HEVC") {
                new_cfg.codec = "H265";
            }
        }

        if (HasKey(body, "target_ip")) {
            const auto ip = GetStringField(body, "target_ip");
            if (!ip) {
                return {400, BuildErrorJson("invalid type for target_ip")};
            }
            new_cfg.target_ip = *ip;
        }

        if (HasKey(body, "target_port")) {
            const auto port = GetIntField(body, "target_port");
            if (!port || *port <= 0 || *port > 65535) {
                return {400, BuildErrorJson("target_port must be in 1..65535")};
            }
            new_cfg.target_port = *port;
        }

        if (HasKey(body, "custom_pipeline")) {
            const auto custom = GetStringField(body, "custom_pipeline");
            if (!custom) {
                return {400, BuildErrorJson("invalid type for custom_pipeline")};
            }
            new_cfg.custom_pipeline = *custom;
        }

        if (HasKey(body, "use_test_source")) {
            const auto use_test = GetBoolField(body, "use_test_source");
            if (!use_test) {
                return {400, BuildErrorJson("invalid type for use_test_source")};
            }
            new_cfg.use_test_source = *use_test;
        }

        new_cfg.mode = ToUpperAscii(new_cfg.mode);
        if (new_cfg.mode == "SIMPLE") {
            new_cfg.mode = "simple";
        } else if (new_cfg.mode == "ADVANCED") {
            new_cfg.mode = "advanced";
        } else {
            return {400, BuildErrorJson("mode must be simple or advanced")};
        }

        std::string err;
        bool ok = false;
        if (new_cfg.mode == "advanced") {
            if (new_cfg.custom_pipeline.empty()) {
                return {400, BuildErrorJson("custom_pipeline is required in advanced mode")};
            }
            ok = streamer.StartAdvanced(new_cfg.custom_pipeline, &err);
        } else {
            ok = streamer.StartSimple(
                new_cfg.use_test_source,
                new_cfg.device,
                new_cfg.codec,
                new_cfg.target_ip,
                new_cfg.target_port,
                &err);
        }

        if (!ok) {
            std::cerr << "[main] restart failed, trying rollback: " << err << std::endl;
            std::string rollback_error;
            if (old_cfg.mode == "advanced") {
                streamer.StartAdvanced(old_cfg.custom_pipeline, &rollback_error);
            } else {
                streamer.StartSimple(
                    old_cfg.use_test_source,
                    old_cfg.device,
                    old_cfg.codec,
                    old_cfg.target_ip,
                    old_cfg.target_port,
                    &rollback_error);
            }
            return {500, BuildErrorJson("failed to reload pipeline: " + err)};
        }

        config = new_cfg;
        return {200, BuildSuccessJson("Pipeline reloaded successfully.")};
    };

    std::string http_error;
    if (!http_server.Start(8080, get_status_handler, update_config_handler, &http_error)) {
        std::cerr << "[main] failed to start HTTP server: " << http_error << std::endl;
        streamer.Stop();
        return 1;
    }

    std::cout << "[main] edgeplant-video-streamer-cpp is running on :8080" << std::endl;

    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "[main] shutting down..." << std::endl;
    http_server.Stop();
    streamer.Stop();

    return 0;
}
