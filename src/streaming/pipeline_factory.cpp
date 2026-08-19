#include "streaming/pipeline_factory.hpp"

#include <gst/gst.h>

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

bool IsH265(const std::string& codec) {
    const std::string codec_upper = ToUpperAscii(codec);
    return codec_upper == "H265" || codec_upper == "HEVC";
}

bool UseJetsonPipeline(const std::string& platform, const std::string& codec) {
    const std::string platform_upper = ToUpperAscii(platform);
    if (platform_upper == "JETSON") {
        return true;
    }
    if (platform_upper == "GENERIC") {
        return false;
    }
    return PipelineFactory::IsJetsonEncoderAvailable(codec);
}

}  // namespace

std::string PipelineFactory::Build(const PipelinePreset& preset) {
    const bool use_h265 = IsH265(preset.codec);
    const bool use_jetson = UseJetsonPipeline(preset.platform, preset.codec);

    std::ostringstream pipeline;
    if (preset.use_test_source) {
        pipeline << "videotestsrc is-live=true pattern=ball "
                 << "! video/x-raw,format=NV12,width=" << preset.width
                 << ",height=" << preset.height << ",framerate=" << preset.framerate << "/1 ";
    } else {
        pipeline << "v4l2src device=" << preset.device << " do-timestamp=true "
                 << "! video/x-raw,width=" << preset.width
                 << ",height=" << preset.height << ",framerate=" << preset.framerate << "/1 ";
    }

    if (use_jetson) {
        pipeline << "! nvvidconv "
                 << "! video/x-raw(memory:NVMM),format=NV12,width=" << preset.width
                 << ",height=" << preset.height << ",framerate=" << preset.framerate << "/1 ";
        if (use_h265) {
            pipeline << "! nvv4l2h265enc bitrate=4000000 insert-sps-pps=true iframeinterval=30 "
                     << "! h265parse config-interval=1 ! rtph265pay pt=96 ";
        } else {
            pipeline << "! nvv4l2h264enc bitrate=4000000 insert-sps-pps=true iframeinterval=30 "
                     << "! h264parse config-interval=1 ! rtph264pay pt=96 ";
        }
    } else {
        pipeline << "! videoconvert ! videoscale ! videorate "
                 << "! video/x-raw,width=" << preset.width
                 << ",height=" << preset.height << ",framerate=" << preset.framerate << "/1 ";
        if (use_h265) {
            pipeline << "! x265enc tune=zerolatency bitrate=4000 key-int-max=30 "
                     << "! h265parse config-interval=1 ! rtph265pay pt=96 ";
        } else {
            pipeline << "! x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 "
                     << "! h264parse config-interval=1 ! rtph264pay pt=96 ";
        }
    }

    pipeline << "! udpsink host=" << preset.target_ip
             << " port=" << preset.target_port << " sync=false async=false";
    return pipeline.str();
}

bool PipelineFactory::IsJetsonEncoderAvailable(const std::string& codec) {
    const char* factory_name = IsH265(codec) ? "nvv4l2h265enc" : "nvv4l2h264enc";
    GstElementFactory* factory = gst_element_factory_find(factory_name);
    if (!factory) {
        return false;
    }
    gst_object_unref(factory);
    return true;
}
