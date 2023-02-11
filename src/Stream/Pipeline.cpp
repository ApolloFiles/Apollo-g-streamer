//
// Created by nudelerde on 11.02.23.
//

#include "Stream/Pipeline.h"
#include "ApolloCommunicator.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

namespace gst {

constexpr auto HLS_TARGET_LOCATION = "./hls_out/";
constexpr auto HLS_MANIFEST_NAME = "manifest.m3u8";
constexpr auto MANIFEST_TARGET_LOCATION = "./hls_out/manifest.m3u8";

std::expected<void, error_t> Pipeline::prepare(bool forceSoftware) {
    GError* err = nullptr;
    auto description = buildPipelineDescription(forceSoftware);
    pipeline.reset(gst_parse_launch(description.c_str(), &err));
    if (!pipeline || err != nullptr) {
        g_printerr("Failed to create pipeline %s", err->message);
        return std::unexpected{1};
    }

    /* Start playing */
    auto stateChangeRet = gst_element_set_state(pipeline.get(), GST_STATE_READY);
    if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the READY state.\n");
        return std::unexpected{1};
    }
    return {};
}

std::expected<void, error_t> Pipeline::start() {
    g_print("Starting pipeline\n");

    /* Start playing */
    auto stateChangeRet = gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the PLAYING state.\n");
        return std::unexpected{1};
    }

    return {};
}
std::expected<void, error_t> Pipeline::loop() {
    GstBus* bus = gst_element_get_bus(pipeline.get());
    while (true) {
        if (!checkManifest()) {
            return std::unexpected{-20};
        }

        auto messageTypes = GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED;
        auto* msg = gst_bus_timed_pop_filtered(bus, 1000'000'000, static_cast<GstMessageType>(messageTypes));
        if (msg == nullptr) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                GError* err;
                gchar* debug_info;
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);

                return std::unexpected{1};
            case GST_MESSAGE_EOS:
                std::cerr << "End-Of-Stream reached." << std::endl;
                // TODO: maybe pause the pipeline instead of stopping it and timeout if no further commands are received?

                if (!checkManifest()) {
                    return std::unexpected{1};
                }
                return {};
            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(msg) != GST_OBJECT(pipeline.get())) {
                    break;
                }

                GstState oldState, newState;
                gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);

                std::cerr << "Pipeline state changed from "
                          << gst_element_state_get_name(oldState)
                          << " to "
                          << gst_element_state_get_name(newState)
                          << '\n';
                dumpPipelineDebugGraph(newState);

                break;
            default:
                std::cerr << "Received an unexpected message on the bus." << std::endl;
                break;
        }

        gst_message_unref(msg);
    }
}
bool Pipeline::checkManifest() {
    if (manifestReady) return true;
    if (!doesHlsManifestExist()) return true;

    if (doesHlsManifestLookBroken()) {
        std::cerr << "\nBroken manifest detected "
                  << "(unexpected value for '#EXT-X-TARGETDURATION'; "
                  << "This can for example happen in combination with 'nvvp9dec' [NVIDIA VP9 decoder] "
                  << "https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/1753)\n"
                  << std::endl;
        return false;
    }

    auto duration = getDuration();
    if (!duration) {
        std::cerr << "Failed to get duration of HLS stream" << std::endl;
        return false;
    }
    manifestReady = true;
    outputQueue.push(ApolloCommunicator::manifest_ready{MANIFEST_TARGET_LOCATION, *duration});
    return true;
}
Pipeline::Pipeline(std::string fileUri, SpScQueue<ApolloCommunicator::apollo_out_cmd_t>& output_queue) : fileUri{std::move(fileUri)},
                                                                                                         outputQueue{output_queue}, pipeline(nullptr) {
}
bool Pipeline::doesHlsManifestExist() {
    return std::filesystem::exists(MANIFEST_TARGET_LOCATION);
}
bool Pipeline::doesHlsManifestLookBroken() {
    std::ifstream manifest(MANIFEST_TARGET_LOCATION);
    std::string line;
    while (std::getline(manifest, line)) {
        if (line == "#EXT-X-TARGETDURATION:2") {
            return false;
        }
    }

    return true;
}
std::expected<gint64, error_t> Pipeline::getDuration() {
    gint64 len;
    if (gst_element_query_duration(pipeline.get(), GST_FORMAT_TIME, &len)) {
        return len;
    }

    return std::unexpected{1};
}
std::expected<gint64, error_t> Pipeline::getPosition() {
    gint64 pos;
    if (gst_element_query_position(pipeline.get(), GST_FORMAT_TIME, &pos)) {
        return pos;
    }

    return std::unexpected{1};
}
void Pipeline::dumpPipelineDebugGraph(GstState new_state) {
    std::string newPipelineStateName = gst_element_state_get_name(new_state);
    auto systemMillis = std::chrono::system_clock::now().time_since_epoch();
    std::string fileName = "pipeline-state-" + newPipelineStateName + "-" + std::to_string(systemMillis.count());
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, fileName.c_str());
}
std::string Pipeline::buildPipelineDescription(bool forceSoftware) {
    std::string srcDescription = "uridecodebin name=src uri=\"" + fileUri + "\"";
    if (forceSoftware) {
        srcDescription += " force-sw-decoders=1";
    }

    std::string sinkDescription = "hlssink2 name=sink max-files=0 playlist-length=0 target-duration=2 "
                                  "location=./hls_out/%d.ts "
                                  "playlist-location=";
    sinkDescription += MANIFEST_TARGET_LOCATION;
    std::string result = srcDescription + " " + sinkDescription;
    result += " src. ! queue ! ";
    result += "videoconvert ! videorate ! video/x-raw,format=I420,framerate=24/1 ! x264enc bitrate=8192 key-int-max=24";
    result += " ! sink.video ";
    audioConvertDescription = "audioconvert ! audiorate ! audio/x-raw,channels=2 ! avenc_aac maxrate=48000";
    return result;
}

void Pipeline::DeletePipeline::operator()(GstElement* pipe) {
    if (!pipe) return;
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}
}// namespace gst