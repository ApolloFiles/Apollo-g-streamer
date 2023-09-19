#include "GstProcessor.h"
#include "ApolloCommunicator.h"

#include <gst/gst.h>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

void printStatus(const std::string& message) {
    std::cerr << message << std::endl;
}

namespace GstProcessor {
    /*constexpr*/ std::string TARGET_SEGMENT_DURATION = "2";

    /*constexpr*/ std::string HLS_TARGET_LOCATION = "./gst_v/";
    /*const*/ std::string MANIFEST_TARGET_LOCATION = HLS_TARGET_LOCATION + "manifest.m3u8";

    gint64 startSeekPosition = 0;

    long lastPipelineStatusSent = 0;
    bool manifestReady = false;

    gint64 pipelineDuration = 0;

    long lastSpeedMultiplerCalculationPosition = 0;
    long lastSpeedMultiplierCalculationMillis = 0;
    double currentProcessingSpeedMultiplier = 0;

    static std::string concatPipelineParts(const std::string& srcString, const std::string& sinkString,
                                           const std::vector<std::string>& video,
                                           const std::vector<std::string>& audio);

    static void dumpPipelineDebugGraph(const GstElement* pipeline, GstState& new_state);


    void setPipelineStateAndWait(GstElement* pipeline, GstState state) {
        GstStateChangeReturn stateChangeReturn;

        stateChangeReturn = gst_element_set_state(pipeline, state);

        if (stateChangeReturn == GST_STATE_CHANGE_ASYNC) {
            std::cout << "Waiting for pipeline to " << gst_element_state_get_name(state) << std::endl;
            stateChangeReturn = gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
            if (stateChangeReturn == GST_STATE_CHANGE_FAILURE) {
                std::cerr << "Failed to " << gst_element_state_get_name(state) << " pipeline" << std::endl;
            }
            std::cout << "pipeline is now " << gst_element_state_get_name(state) << std::endl;
        } else if (stateChangeReturn == GST_STATE_CHANGE_SUCCESS) {
            std::cout << "Pipeline is now " << gst_element_state_get_name(state) << std::endl;
        } else {
            std::cerr << "Failed to " << gst_element_state_get_name(state) << " pipeline" << std::endl;
        }
    }

    void resetOwnState() {
        std::filesystem::create_directories(HLS_TARGET_LOCATION);
        std::filesystem::remove(MANIFEST_TARGET_LOCATION);

        lastPipelineStatusSent = 0;
        manifestReady = false;

        pipelineDuration = 0;

        lastSpeedMultiplerCalculationPosition = 0;
        lastSpeedMultiplierCalculationMillis = 0;
        currentProcessingSpeedMultiplier = 0;
    }

    void resetOwnStateAndPipeline(GstElement* pipeline) {
        setPipelineStateAndWait(pipeline, GST_STATE_NULL);

        resetOwnState();
    }

    static long currentEpochSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    static long currentEpochMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    gint64 getPosition(GstElement* pipeline) {
        gint64 pos;
        if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos)) {
            return pos;
        }

        return 0;
    }

    long getPositionOfSrcElement(GstElement* pipeline) {
        auto srcElement = gst_bin_get_by_name(GST_BIN(pipeline), "src");
        auto srcPosition = getPosition(srcElement);
        gst_object_unref(srcElement);
        return srcPosition;
    }

    std::string getPipelineStateName(GstElement* pipeline) {
        GstState state;
        GstState pending;
        gst_element_get_state(pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
        return gst_element_state_get_name(state);
    }

    void tickProcessingSpeedMultiplierCalculation(GstElement* pipeline) {
        GstState state;
        gst_element_get_state(pipeline, &state, nullptr, GST_CLOCK_TIME_NONE);
        if (state != GST_STATE_PLAYING) {
            lastSpeedMultiplerCalculationPosition = 0;
            lastSpeedMultiplierCalculationMillis = 0;
            currentProcessingSpeedMultiplier = 0;
            return;
        }

        auto position = getPositionOfSrcElement(pipeline);
        auto duration = pipelineDuration;
        if (position <= 0 || duration <= 0) {
            return;
        }

        auto nowMillis = currentEpochMillis();
        if (lastSpeedMultiplierCalculationMillis == 0) {
            lastSpeedMultiplierCalculationMillis = nowMillis;
            lastSpeedMultiplerCalculationPosition = position;
            return;
        }

        auto timeDiff = nowMillis - lastSpeedMultiplierCalculationMillis;
        if (timeDiff <= 500) {
            return;
        }

        auto positionDiff = position - lastSpeedMultiplerCalculationPosition;
        currentProcessingSpeedMultiplier = (double) positionDiff / (double) timeDiff;
    }

    void increaseElementRank(const gchar* elementName) {
        auto elementFactory = gst_element_factory_find(elementName);
        if (elementFactory) {
            gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(elementFactory), GST_RANK_PRIMARY + 50);
        }
    }

    std::string generateVideoPipelinePart(const std::string& targetFpsString, bool forceSwEncoding) {
        if (false && !forceSwEncoding && gst_element_factory_find("nvh264enc")) {
            printStatus("Detected NVIDIA's hardware-accelerated NVENC encoder API...");

            if (false && gst_element_factory_find("cudaconvert")) {
                printStatus("Detected CUDA API support for colorspace conversion...");

//                return "videorate ! cudaupload ! cudaconvert"
//                       " ! video/x-raw(memory:CUDAMemory),format=I420,framerate=" + targetFpsString + "/1 ! " +
//                       "nvh264enc strict-gop=1 gop-size=" + targetFpsString + " ! h264parse";
                return "videorate ! cudaupload ! cudaconvert"
                       " ! video/x-raw(memory:CUDAMemory),format=I420,framerate=" + targetFpsString + "/1 ! " +
                       "nvh264enc strict-gop=1 gop-size=" + targetFpsString + " ! h264parse ! avdec_h264";
            }

            if (false && gst_element_factory_find("glcolorconvert")) {
                printStatus("Detected OpenGL support for colorspace conversion...");

                return "videorate ! glupload ! glcolorconvert"
                       " ! video/x-raw(memory:GLMemory),format=I420,framerate=" + targetFpsString + "/1 ! " +
                       "nvh264enc strict-gop=1 gop-size=" + targetFpsString + " ! h264parse";
            }

            return "videoconvert ! videorate"
                   " ! video/x-raw,format=I420,framerate=" + targetFpsString + "/1 ! " +
                   "nvh264enc strict-gop=1 gop-size=" + targetFpsString + " ! h264parse";
        }

        printStatus("Using software encoders...");
        return "videoconvert ! videorate"
               " ! video/x-raw,format=I420,framerate=" + targetFpsString + "/1 ! " +
               "x264enc bitrate=8192 key-int-max=" + targetFpsString + " ! h264parse";

//        return "videorate ! videoconvert ! video/x-raw,format=I420,framerate=" + targetFpsString + "/1";
    }

    std::string generatePipelineDescription(const std::string& fileUri, bool forceSwDecoding = false, bool forceSwEncoding = false) {
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

//        std::string sinkDescription = "autovideosink name=sink";
        std::string sinkDescription =
                "hlssink2 name=sink max-files=0 playlist-length=0 target-duration=" + TARGET_SEGMENT_DURATION + " " +
                "location=" + HLS_TARGET_LOCATION + "%d.ts " +
                "playlist-location=" + MANIFEST_TARGET_LOCATION;

        // TODO: Auto detect streams; Have them provided/controlled by the parent process
        // TODO: proper bitrate detection (currently hardcoded to 8192kbps)
        std::string videoBinDescription = generateVideoPipelinePart(targetFpsString, forceSwEncoding);
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
    bool checkAndNotifyIfManifestReady() {
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
        if (pipelineDuration > 0) {
            ApolloCommunicator::sendSimple(
                    ApolloCommunicator::OUT_MANIFEST_READY,
                    {MANIFEST_TARGET_LOCATION, std::to_string(pipelineDuration / GST_SECOND)}
            );
            manifestReady = true;
        }

        return true;
    }

    void sendPipelineStatusToCommunicator(GstElement* pipeline) {
        auto pipelineState = getPipelineStateName(pipeline);
        auto srcPosition = getPositionOfSrcElement(pipeline);

        ApolloCommunicator::sendSimple(
                ApolloCommunicator::OUT_PIPELINE_STATUS,
                {
                        pipelineState,
                        std::to_string(srcPosition / GST_SECOND),
                        std::to_string(pipelineDuration / GST_SECOND),

                        std::to_string(currentProcessingSpeedMultiplier)
                }
        );

        lastPipelineStatusSent = currentEpochSeconds();
    }

    bool seek_to_time(GstElement* pipeline, gint64 time_nanoseconds) {
        if (!gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME,
                              GST_SEEK_FLAG_FLUSH /* GST_SEEK_FLAG_FLUSH */ /* Eig. brauchen wir flush und queues/buffers zu leeren... Aber macht Probleme ,_, */,
                              GST_SEEK_TYPE_SET, time_nanoseconds,
                              GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
            return false;
        }

        return true;
    }

    int pipelineLoop(GstElement* pipeline, GstBus* pipelineBus) {
        bool initialPipelineSeekDone = startSeekPosition <= 0;

        GstBus* bus = pipelineBus;
        while (true) {
            if (!checkAndNotifyIfManifestReady()) {
                return -20;
            }

//            std::cout << "1" << std::endl;
//            tickProcessingSpeedMultiplierCalculation(pipeline);

            auto optionalCommand = ApolloCommunicator::apollo_commands.pop_non_blocking();
            if (optionalCommand.has_value()) {
                auto command = optionalCommand.value();
                if (std::get_if<ApolloCommunicator::pause>(&command)) {
                    gst_element_set_state(pipeline, GST_STATE_PAUSED);
                } else if (std::get_if<ApolloCommunicator::play>(&command)) {
                    gst_element_set_state(pipeline, GST_STATE_PLAYING);
                } else if (std::get_if<ApolloCommunicator::seek>(&command)) {
                    auto seek = std::get<ApolloCommunicator::seek>(command);
                    std::cout << "Starting seek to " << std::get<ApolloCommunicator::seek>(command).time << std::endl;
                    startSeekPosition = seek.time * GST_SECOND;

                    return -100;
                } else {
                    std::cout << "Unknown command received in pipelineLoop" << std::endl;
                }
            }

            if (currentEpochSeconds() > lastPipelineStatusSent + 2) {
                sendPipelineStatusToCommunicator(pipeline);
            }

            auto messageTypes = GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_DURATION_CHANGED;
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 1 * GST_SECOND, static_cast<GstMessageType>(messageTypes));
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

                    if (!checkAndNotifyIfManifestReady()) {
                        return 1;
                    }

                    return 0;
                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC(msg) != GST_OBJECT(pipeline)) {
                        break;
                    }

                    GstState oldState, newState, pendingState;
                    gst_message_parse_state_changed(msg, &oldState, &newState, &pendingState);

                    std::cout << "Pipeline state changed from "
                              << gst_element_state_get_name(oldState)
                              << " to "
                              << gst_element_state_get_name(newState)
                              << " (pending: "
                              << gst_element_state_get_name(pendingState)
                              << ")"
                              << '\n';
                    dumpPipelineDebugGraph(pipeline, newState);

//                    if (!initialPipelineSeekDone && newState == GST_STATE_PLAYING) {
//                        if (startSeekPosition > 0 && pipelineDuration > 0) {
//                            bool seekResult = seek_to_time(pipeline, startSeekPosition);
////                            auto seekFlags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST);
////                            bool seekResult = gst_element_seek_simple(pipeline, GST_FORMAT_TIME, seekFlags, startSeekPosition);
//                            if (!seekResult) {
//                                std::cerr << "Seeking to " << startSeekPosition << " failed." << std::endl;
//                                return 1;
//                            }
//                        }
//
//                        std::cout << "Initial seek done." << std::endl;
//                        std::cout << "manifestReady: " << manifestReady << std::endl;
//                        initialPipelineSeekDone = true;
//                    }
                    sendPipelineStatusToCommunicator(pipeline);

                    break;
                case GST_MESSAGE_DURATION_CHANGED:
                    gint64 newDuration;
                    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &newDuration)
                        && newDuration != pipelineDuration) {
                        pipelineDuration = newDuration;
                        sendPipelineStatusToCommunicator(pipeline);
                    }
                    break;
                default:
                    std::cerr << "Received an unexpected message on the bus." << std::endl;
                    break;
            }
            gst_message_unref(msg);
        }
    }

    void destroyPipelineAndResetOwnState(GstElement* pipeline) {
        resetOwnStateAndPipeline(pipeline);
        gst_object_unref(pipeline);
    }

    int startPipeline(const std::string& pipelineDescription, bool waitForApolloStartCommand) {
        GError* err = nullptr;
        GstElement* pipeline = gst_parse_launch(pipelineDescription.c_str(), &err);
        if (!pipeline || err != nullptr) {
            g_printerr("Failed to create pipeline %s", err->message);
            return 2;
        }

        /* Start playing */
        GstStateChangeReturn stateChangeRet = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        if (stateChangeRet == GST_STATE_CHANGE_ASYNC) {
            stateChangeRet = gst_element_get_state(pipeline, nullptr, nullptr, 10 * GST_SECOND);
        }
        if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Unable to set the pipeline to the READY state.\n");
            destroyPipelineAndResetOwnState(pipeline);
            return 1;
        }

        if (waitForApolloStartCommand) {
            std::cout << "Pipeline ready – waiting for play command..." << std::endl;
        }
        while (waitForApolloStartCommand) {
            auto command = ApolloCommunicator::apollo_commands.pop_blocking(30000);
            if (std::get_if<ApolloCommunicator::seek>(&command)) {
                startSeekPosition = std::get<ApolloCommunicator::seek>(command).time * GST_SECOND;

                if (startSeekPosition > 0) {
                    std::cout << "Seeking to " << startSeekPosition << std::endl;
                    seek_to_time(pipeline, startSeekPosition);
                }
                continue;
            } else if (std::get_if<ApolloCommunicator::play>(&command)) {
                break;
            }

            std::cerr << "unexpected command or timeout – aborting" << std::endl;
            destroyPipelineAndResetOwnState(pipeline);
            return 1;
        }

        g_print("Starting pipeline\n");
        GstBus* pipelineBus = gst_element_get_bus(pipeline);

//        setPipelineStateAndWait(pipeline, GST_STATE_PAUSED);
//        if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
//            g_printerr("Unable to set the pipeline to the PAUSED state.\n");
//            destroyPipelineAndResetOwnState(pipeline);
//            return 1;
//        }

//        if (startSeekPosition > 0) {
//            auto seekFlags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH /*| GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST*/);
//            bool seekResult = gst_element_seek_simple(pipeline, GST_FORMAT_TIME, seekFlags, startSeekPosition);
//            if (!seekResult) {
//                std::cerr << "Seeking to " << startSeekPosition << " failed." << std::endl;
//                return 1;
//            } else {
//                std::cout << "Seeking to " << startSeekPosition << " succeeded." << std::endl;
//            }
//        }

        /* Start playing */
//        setPipelineStateAndWait(pipeline, GST_STATE_PLAYING);
        stateChangeRet = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (stateChangeRet == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Unable to set the pipeline to the PLAYING state.\n");
            destroyPipelineAndResetOwnState(pipeline);
            return 1;
        }

        int pipelineExitCode = pipelineLoop(pipeline, pipelineBus);
        gst_object_unref(pipelineBus);
        destroyPipelineAndResetOwnState(pipeline);

        return pipelineExitCode;
    }

    // TODO: Detect if pipeline never reaches PLAYING state and abort after a timeout
    int init(int argc, char* argv[], const std::string& fileUri) {
        gst_init(&argc, &argv);

        resetOwnState();

        increaseElementRank("nvh265dec");

        auto pipelineDescription = generatePipelineDescription(fileUri);
        std::cerr << '\n' << pipelineDescription << '\n' << std::endl;

        int pipelineExitCode = 0;
        do {
            pipelineExitCode = startPipeline(pipelineDescription, pipelineExitCode == 0);
        } while (pipelineExitCode == -100);

        if (pipelineExitCode != -20 && pipelineExitCode != 2) {
            return pipelineExitCode;
        }
        std::cerr << "Pipeline failed to play with hardware encoding/decoding, trying software encoding/decoding..." << std::endl;

        std::filesystem::remove(MANIFEST_TARGET_LOCATION);
        pipelineDescription = generatePipelineDescription(fileUri, true, true);

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
            result += " src. ! " + conv + " ! sink. ";
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
