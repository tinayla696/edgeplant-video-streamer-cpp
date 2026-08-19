#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <gst/gst.h>

#include "streaming/pipeline_factory.hpp"

class Streamer {
public:
    Streamer();
    ~Streamer();

    bool StartSimple(bool use_test_source,
                     const std::string& platform,
                     const std::string& device,
                     const std::string& codec,
                     const std::string& target_ip,
                     int target_port,
                     std::string* error_message = nullptr);

    bool StartAdvanced(const std::string& custom_pipeline,
                       std::string* error_message = nullptr);

    void Stop();
    bool IsRunning() const;

private:
    void BusWatchLoop();

    mutable std::mutex mutex_;
    GstElement* pipeline_;
    std::thread bus_thread_;
    std::atomic<bool> bus_thread_running_;
    std::atomic<bool> running_;
};
