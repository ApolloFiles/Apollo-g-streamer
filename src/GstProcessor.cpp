#include "GstProcessor.h"

#include <gst/gst.h>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include "ApolloCommunicator.h"

namespace GstProcessor {
    /*constexpr*/ std::string TARGET_SEGMENT_DURATION = "2";

    /*constexpr*/ std::string HLS_TARGET_LOCATION = "./gst_v/";
    /*const*/ std::string MANIFEST_TARGET_LOCATION = HLS_TARGET_LOCATION + "manifest.m3u8";

    bool manifestReady = false;

    static std::string concatPipelineParts(const std::string& srcString, const std::string& sinkString,
                                           const std::vector<std::string>& video,
                                           const std::vector<std::string>& audio);


    static void dumpPipelineDebugGraph(const GstElement* pipeline, GstState& new_state);

    gint64 getPosition(GstElement* pipeline) {
        gint64 pos;
        if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos)) {
            return pos;
        }

        return 0;
    }

    gint64 getDuration(GstElement* pipeline) {
        gint64 len;
        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &len)) {
            return len;
        }

        return 0;
    }

    std::string generatePipelineDescription(const std::string& fileUri, bool forceSwDecoding = false) {
        auto targetFps = 30;
        if (const char* env_p = std::getenv("APOLLO_GST_TARGET_FPS")) {
            targetFps = std::stoi(env_p);
        } else {
            std::cout << "APOLLO_GST_TARGET_FPS env not set, using default value of " << targetFps << std::endl;
        }
        const auto targetFpsString = std::to_string(targetFps);

        std::string srcDescription = "uridecodebin name=src uri=\"" + fileUri + "\"";
        if (forceSwDecoding) {
            srcDescription += " force-sw-decoders=1";
        }

        std::string sinkDescription =
                "hlssink2 name=sink max-files=0 playlist-length=0 target-duration=" + TARGET_SEGMENT_DURATION + " " +
                "location=" + HLS_TARGET_LOCATION + "%d.ts " +
                "playlist-location=" + MANIFEST_TARGET_LOCATION;

        // TODO: Auto detect streams; Have them provided/controlled by the parent process
        // TODO: proper bitrate detection (currently hardcoded to 8192kbps)
        std::string videoBinDescription =
                "videoconvert ! videorate ! video/x-raw,format=I420,framerate=" + targetFpsString +
                "/1 ! x264enc bitrate=8192 key-int-max=" + targetFpsString;
        std::string audioBinDescription = "audioconvert ! audiorate ! audio/x-raw,channels=2 ! avenc_aac maxrate=48000";

        return concatPipelineParts(srcDescription, sinkDescription,
                                   {videoBinDescription}, {audioBinDescription});
    }

    bool doesHlsManifestExist() {
        return std::filesystem::exists(MANIFEST_TARGET_LOCATION);
    }

    bool doesHlsManifestLookBroken() {
        std::ifstream manifest(MANIFEST_TARGET_LOCATION);
        std::string line;
        while (std::getline(manifest, line)) {
            if (line == ("#EXT-X-TARGETDURATION:" + TARGET_SEGMENT_DURATION)) {
                return false;
            }
        }

        return true;
    }

    /**
     * @return false if the manifest is broken (e.g. due to a bug in the NVIDIA VP9 decoder)
     */
    bool checkAndNotifyIfManifestReady(GstElement* pipeline) {
        if (manifestReady) {
            return true;
        }
        if (!doesHlsManifestExist()) {
            return true;
        }

        if (doesHlsManifestLookBroken()) {
            std::cerr << "\nBroken manifest detected "
                      << "(unexpected value for '#EXT-X-TARGETDURATION'; "
                      << "This can for example happen in combination with 'nvvp9dec' [NVIDIA VP9 decoder] "
                      << "https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/1753)\n"
                      << std::endl;
            return false;
        }

        // TODO: have a duration_changed messsage instead of sending it with the manifest?
        auto duration = getDuration(pipeline);
        if (duration > 0) {
            ApolloCommunicator::sendSimple(
                    ApolloCommunicator::OUT_MANIFEST_READY,
                    {MANIFEST_TARGET_LOCATION, std::to_string(duration / GST_SECOND)}
            );
            manifestReady = true;
        }

        return true;
    }

    int pipelineLoop(GstElement* pipeline) {
        GstBus* bus = gst_element_get_bus(pipeline);
        while (true) {
            if (!checkAndNotifyIfManifestReady(pipeline)) {
                return -20;
            }

            auto messageTypes = GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED;
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 1000 * 1000 * 1000, static_cast<GstMessageType>(messageTypes));
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

                    return 1;
                case GST_MESSAGE_EOS:
                    std::cout << "End-Of-Stream reached." << std::endl;
                    // TODO: maybe pause the pipeline instead of stopping it and timeout if no further commands are received?

                    if (!checkAndNotifyIfManifestReady(pipeline)) {
                        return 1;
                    }

                    return 0;
                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC(msg) != GST_OBJECT(pipeline)) {
                        break;
                    }

                    GstState oldState, newState;
                    gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);

                    std::cout << "Pipeline state changed from "
                              << gst_element_state_get_name(oldState)
                              << " to "
                              << gst_element_state_get_name(newState)
                              << '\n';
                    dumpPipelineDebugGraph(pipeline, newState);

                    break;
                default:
                    std::cerr << "Received an unexpected message on the bus." << std::endl;
                    break;
            }

            gst_message_unref(msg);
        }
    }

    void destroyPipeline(GstElement* pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }

    int startPipeline(const std::string& pipelineDescription, bool waitForApolloStartCommand) {
        GError* err = nullptr;
        GstElement* pipeline = gst_parse_launch(pipelineDescription.c_str(), &err);
        if (!pipeline || err != nullptr) {
            g_printerr("Failed to create pipeline %s", err->message);
            return 1;
        }

        /* Start playing */
        GstStateChangeReturn stateChangeRet = gst_element_set_state(pipeline, GST_STATE_READY);
        if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Unable to set the pipeline to the READY state.\n");
            destroyPipeline(pipeline);
            return 1;
        }

        if (waitForApolloStartCommand) {
            std::cout << "Pipeline ready – waiting for start command..." << std::endl;
            auto res = ApolloCommunicator::apollo_commands.pop_blocking(30000);
            if (!std::get_if<ApolloCommunicator::start>(&res)) {
                std::cerr << "unexpected command or timeout – aborting" << std::endl;
                destroyPipeline(pipeline);
                return 1;
            }
        }

        g_print("Starting pipeline\n");

        /* Start playing */
        stateChangeRet = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Unable to set the pipeline to the PLAYING state.\n");
            destroyPipeline(pipeline);
            return 1;
        }

        int pipelineExitCode = pipelineLoop(pipeline);
        destroyPipeline(pipeline);
        return pipelineExitCode;
    }

    int init(int argc, char* argv[], const std::string& fileUri) {
        gst_init(&argc, &argv);

        std::filesystem::create_directories(HLS_TARGET_LOCATION);
        std::filesystem::remove(MANIFEST_TARGET_LOCATION);

        auto pipelineDescription = generatePipelineDescription(fileUri);

        int pipelineExitCode = startPipeline(pipelineDescription, true);
        if (pipelineExitCode != -20) {
            return pipelineExitCode;
        }

        std::filesystem::remove(MANIFEST_TARGET_LOCATION);
        pipelineDescription = generatePipelineDescription(fileUri, true);

        pipelineExitCode = startPipeline(pipelineDescription, false);
        if (pipelineExitCode == -20) {
            std::cerr << "Pipeline has been restarted with software decoding, but still failed..." << std::endl;
        }
        return pipelineExitCode;
    }

    static std::string concatPipelineParts(const std::string& srcString, const std::string& sinkString,
                                           const std::vector<std::string>& video,
                                           const std::vector<std::string>& audio) {
        std::string result = srcString + " " + sinkString;

        for (const auto& conv: video) {
            result += " src. ! queue ! " + conv + " ! sink.video ";
        }
        for (const auto& conv: audio) {
            result += " src. ! queue ! " + conv + " ! sink.audio ";
        }

        return result;
    }

    static void dumpPipelineDebugGraph(const GstElement* pipeline, GstState& new_state) {
        std::string newPipelineStateName = gst_element_state_get_name(new_state);
        auto systemMillis = std::chrono::system_clock::now().time_since_epoch();
        std::string fileName = "pipeline-state-" + newPipelineStateName + "-" + std::to_string(systemMillis.count());
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, fileName.c_str());
    }
}
