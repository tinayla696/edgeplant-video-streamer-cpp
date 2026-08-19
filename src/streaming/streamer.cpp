#include "streaming/streamer.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

Streamer::Streamer()
    : pipeline_(nullptr),
      bus_thread_running_(false),
      running_(false) {
    gst_init(nullptr, nullptr);
}

Streamer::~Streamer() {
    Stop();
}

bool Streamer::StartSimple(bool use_test_source,
                           const std::string& platform,
                           const std::string& device,
                           const std::string& codec,
                           const std::string& target_ip,
                           int target_port,
                           int width,
                           int height,
                           int framerate,
                           std::string* error_message) {
    const std::string pipeline = PipelineFactory::Build({
        platform, codec, use_test_source, device, target_ip, target_port,
        width, height, framerate});
    return StartAdvanced(pipeline, error_message);
}

bool Streamer::StartAdvanced(const std::string& custom_pipeline,
                             std::string* error_message) {
    Stop();

    std::lock_guard<std::mutex> lock(mutex_);

    if (custom_pipeline.empty()) {
        if (error_message) {
            *error_message = "custom pipeline is empty";
        }
        return false;
    }

    GError* parse_error = nullptr;
    pipeline_ = gst_parse_launch(custom_pipeline.c_str(), &parse_error);
    if (!pipeline_) {
        if (error_message) {
            *error_message = parse_error ? parse_error->message : "failed to create pipeline";
        }
        if (parse_error) {
            g_error_free(parse_error);
        }
        return false;
    }
    if (parse_error) {
        if (error_message) {
            *error_message = parse_error->message;
        }
        g_error_free(parse_error);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    const GstStateChangeReturn state_ret =
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (state_ret == GST_STATE_CHANGE_FAILURE) {
        if (error_message) {
            *error_message = "failed to transition pipeline to PLAYING";
        }
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    running_ = true;
    bus_thread_running_ = true;
    bus_thread_ = std::thread(&Streamer::BusWatchLoop, this);
    std::cout << "[streamer] pipeline started" << std::endl;
    return true;
}

void Streamer::Stop() {
    GstElement* pipeline_to_unref = nullptr;
    bool should_join_bus_thread = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        bus_thread_running_ = false;

        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            pipeline_to_unref = pipeline_;
            pipeline_ = nullptr;
        }

        should_join_bus_thread = bus_thread_.joinable();
    }

    if (should_join_bus_thread) {
        bus_thread_.join();
    }

    if (pipeline_to_unref) {
        gst_object_unref(pipeline_to_unref);
        std::cout << "[streamer] pipeline stopped" << std::endl;
    }
}

bool Streamer::IsRunning() const {
    return running_;
}

void Streamer::BusWatchLoop() {
    GstBus* bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pipeline_) {
            return;
        }
        bus = gst_element_get_bus(pipeline_);
    }

    if (!bus) {
        return;
    }

    while (bus_thread_running_) {
        GstMessage* message = gst_bus_timed_pop_filtered(
            bus,
            200 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING));

        if (!message) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(message, &err, &debug);
                std::cerr << "[streamer][error] "
                          << (err ? err->message : "unknown")
                          << " debug=" << (debug ? debug : "") << std::endl;
                if (err) {
                    g_error_free(err);
                }
                if (debug) {
                    g_free(debug);
                }
                running_ = false;
                bus_thread_running_ = false;
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_warning(message, &err, &debug);
                std::cerr << "[streamer][warning] "
                          << (err ? err->message : "unknown")
                          << " debug=" << (debug ? debug : "") << std::endl;
                if (err) {
                    g_error_free(err);
                }
                if (debug) {
                    g_free(debug);
                }
                break;
            }
            case GST_MESSAGE_EOS:
                std::cerr << "[streamer] EOS received" << std::endl;
                running_ = false;
                bus_thread_running_ = false;
                break;
            default:
                break;
        }

        gst_message_unref(message);
    }

    gst_object_unref(bus);
}
