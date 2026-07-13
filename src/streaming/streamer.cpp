#include "streaming/streamer.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

namespace {

std::string ToUpperAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return value;
}

}  // namespace

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
                           const std::string& device,
                           const std::string& codec,
                           const std::string& target_ip,
                           int target_port,
                           std::string* error_message) {
    const std::string pipeline = BuildSimplePipeline(
        use_test_source, device, codec, target_ip, target_port);
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

std::string Streamer::BuildSimplePipeline(bool use_test_source,
                                          const std::string& device,
                                          const std::string& codec,
                                          const std::string& target_ip,
                                          int target_port) const {
    const std::string codec_upper = ToUpperAscii(codec);
    const bool use_h265 = (codec_upper == "H265" || codec_upper == "HEVC");
    const bool use_jetson_encoder = IsJetsonEncoderAvailable(use_h265 ? "H265" : "H264");

    std::ostringstream ss;

    if (use_test_source) {
        ss << "videotestsrc is-live=true pattern=ball "
           << "! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ";
    } else {
          // decodebin allows both raw and MJPEG-capable USB cameras to negotiate.
        ss << "v4l2src device=" << device << " do-timestamp=true "
              << "! decodebin ";
    }

    if (use_jetson_encoder) {
        ss << "! nvvidconv "
           << "! video/x-raw(memory:NVMM),format=NV12,width=1280,height=720,framerate=30/1 ";

        if (use_h265) {
            ss << "! nvv4l2h265enc bitrate=4000000 insert-sps-pps=true iframeinterval=30 "
               << "! h265parse config-interval=1 "
               << "! rtph265pay pt=96 ";
        } else {
            ss << "! nvv4l2h264enc bitrate=4000000 insert-sps-pps=true iframeinterval=30 "
               << "! h264parse config-interval=1 "
               << "! rtph264pay pt=96 ";
        }
    } else {
        ss << "! videoconvert "
           << "! videoscale "
           << "! videorate "
           << "! video/x-raw,width=1280,height=720,framerate=30/1 ";
        if (use_h265) {
            ss << "! x265enc tune=zerolatency bitrate=4000 key-int-max=30 "
               << "! h265parse config-interval=1 "
               << "! rtph265pay pt=96 ";
        } else {
            ss << "! x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 "
               << "! h264parse config-interval=1 "
               << "! rtph264pay pt=96 ";
        }
    }

    ss << "! udpsink host=" << target_ip << " port=" << target_port
       << " sync=false async=false";

    return ss.str();
}

bool Streamer::IsJetsonEncoderAvailable(const std::string& codec) const {
    const std::string codec_upper = ToUpperAscii(codec);
    const char* factory_name = (codec_upper == "H265" || codec_upper == "HEVC")
                                   ? "nvv4l2h265enc"
                                   : "nvv4l2h264enc";

    GstElementFactory* factory = gst_element_factory_find(factory_name);
    if (factory) {
        gst_object_unref(factory);
        return true;
    }
    return false;
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
