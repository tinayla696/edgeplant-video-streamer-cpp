#pragma once

#include <string>

struct PipelinePreset {
    std::string platform = "auto";
    std::string codec = "H264";
    bool use_test_source = true;
    std::string device = "/dev/video0";
    std::string target_ip = "127.0.0.1";
    int target_port = 5004;
    int width = 1280;
    int height = 720;
    int framerate = 30;
};

class PipelineFactory {
public:
    static std::string Build(const PipelinePreset& preset);
    static bool IsJetsonEncoderAvailable(const std::string& codec);
};
