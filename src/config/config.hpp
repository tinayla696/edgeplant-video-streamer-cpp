#pragma once

#include <string>

struct StreamerConfig {
    std::string status = "stopped";
    std::string mode = "simple";
    std::string platform = "auto";
    std::string device = "/dev/video0";
    std::string codec = "H264";
    int width = 1280;
    int height = 720;
    int framerate = 30;
    std::string target_ip = "127.0.0.1";
    int target_port = 5004;
    std::string custom_pipeline;

    // WSL and non-camera environments should default to a synthetic source.
    bool use_test_source = true;
};
