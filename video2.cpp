//
// Created by IT-JIM 
// VIDEO2: Decode a video file with opencv and send to a gstreamer pipeline via appsrc

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <sstream>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <opencv2/opencv.hpp>

//======================================================================================================================
/// A simple assertion function + macro
inline void myAssert(bool b, const std::string &s = "MYASSERT ERROR !") {
    if (!b)
        throw std::runtime_error(s);
}

#define MY_ASSERT(x) myAssert(x, "MYASSERT ERROR :" #x)

//======================================================================================================================
/// Check GStreamer error, exit on error
inline void checkErr(GError *err) {
    if (err) {
        std::cerr << "checkErr : " << err->message << std::endl;
        exit(0);
    }
}

//======================================================================================================================
/// Our global data, serious gstreamer apps should always have this !
struct GoblinData {
    /// pipeline
    GstElement *pipeline = nullptr;
    /// appsrc
    GstElement *srcVideo = nullptr;
    /// Video file name
    std::string fileName;
    /// Appsrc flag: when it's true, send the frames
    std::atomic_bool flagRunV{false};

};

//======================================================================================================================
/// Process a single bus message, log messages, exit on error, return false on eof
static bool busProcessMsg(GstElement *pipeline, GstMessage *msg, const std::string &prefix) {
    using namespace std;

    GstMessageType mType = GST_MESSAGE_TYPE(msg);
    cout << "[" << prefix << "] : mType = " << mType << " ";
    switch (mType) {
        case (GST_MESSAGE_ERROR):
            // Parse error and exit program, hard exit
            GError *err;
            gchar *dbg;
            gst_message_parse_error(msg, &err, &dbg);
            cout << "ERR = " << err->message << " FROM " << GST_OBJECT_NAME(msg->src) << endl;
            cout << "DBG = " << dbg << endl;
            g_clear_error(&err);
            g_free(dbg);
            exit(1);
        case (GST_MESSAGE_EOS) :
            // Soft exit on EOS
            cout << " EOS !" << endl;
            return false;
        case (GST_MESSAGE_STATE_CHANGED):
            // Parse state change, print extra info for pipeline only
            cout << "State changed !" << endl;
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                GstState sOld, sNew, sPenging;
                gst_message_parse_state_changed(msg, &sOld, &sNew, &sPenging);
                cout << "Pipeline changed from " << gst_element_state_get_name(sOld) << " to " <<
                     gst_element_state_get_name(sNew) << endl;
            }
            break;
        case (GST_MESSAGE_STEP_START):
            cout << "STEP START !" << endl;
            break;
        case (GST_MESSAGE_STREAM_STATUS):
            cout << "STREAM STATUS !" << endl;
            break;
        case (GST_MESSAGE_ELEMENT):
            cout << "MESSAGE ELEMENT !" << endl;
            break;

            // You can add more stuff here if you want

        default:
            cout << endl;
    }
    return true;
}

//======================================================================================================================
/// Run the message loop for one bus
void codeThreadBus(GstElement *pipeline, GoblinData &data, const std::string &prefix) {
    using namespace std;
    GstBus *bus = gst_element_get_bus(pipeline);

    int res;
    while (true) {
        GstMessage *msg = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
        MY_ASSERT(msg);
        res = busProcessMsg(pipeline, msg, prefix);
        gst_message_unref(msg);
        if (!res)
            break;
    }
    gst_object_unref(bus);
    cout << "BUS THREAD FINISHED : " << prefix << endl;
}

//======================================================================================================================
/// Read a video file with opencv and send data to appsrc
void codeThreadSrcV(GoblinData &data) {
    using namespace std;
    using namespace cv;

    // Open the video file with opencv
    VideoCapture video(data.fileName);
    MY_ASSERT(video.isOpened());

    // Find width, height, FPS
    int imW = (int) video.get(CAP_PROP_FRAME_WIDTH);
    int imH = (int) video.get(CAP_PROP_FRAME_HEIGHT);
    double fps = video.get(CAP_PROP_FPS);
    MY_ASSERT(imW > 0 && imH > 0 && fps > 0);

    // Now, we must give our apprsc proper caps (with width+height) and re-negotiate !
    // Otherwise, the pipeline will not work !
    ostringstream oss;
    oss << "video/x-raw,format=BGR,width=" << imW << ",height=" << imH << ",framerate=" << int(lround(fps)) << "/1";
    cout << "CAPS=" << oss.str() << endl;
    GstCaps *capsVideo = gst_caps_from_string(oss.str().c_str());
    g_object_set(data.srcVideo, "caps", capsVideo, nullptr);
    gst_caps_unref(capsVideo);

    // Play the pipeline AFTER we have set up the final caps
    MY_ASSERT(gst_element_set_state(data.pipeline, GST_STATE_PLAYING));

    // Frame loop
    int frameCount = 0;
    Mat frame;
    for (;;) {
        // If the flag is false, go idle and wait, the pipeline does not want data for now
        if (!data.flagRunV) {
            cout << "(wait)" << endl;
            this_thread::sleep_for(chrono::milliseconds(10));
            continue;
        }

        // Read a frame from the video
        video.read(frame);
        if (frame.empty())
            break;

        // Create a GStreamer buffer and copy data to it via a map
        int bufferSize = frame.cols * frame.rows * 3;
        GstBuffer *buffer = gst_buffer_new_and_alloc(bufferSize);
        GstMapInfo m;
        gst_buffer_map(buffer, &m, GST_MAP_WRITE);
        memcpy(m.data, frame.data, bufferSize);
        gst_buffer_unmap(buffer, &m);

        // Set up timestamp
        // This is not strictly required, but you need it for the correct 1x playback with sync=1 !
        buffer->pts = uint64_t(frameCount  / fps * GST_SECOND);

        // Send buffer to gstreamer
        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(data.srcVideo), buffer);

        ++frameCount;
    }

    // Signal EOF to the pipeline
    gst_app_src_end_of_stream(GST_APP_SRC(data.srcVideo));
}

//======================================================================================================================
/// Callback called when the pipeline wants more data
static void startFeed(GstElement *source, guint size, GoblinData *data) {
    using namespace std;
    if (!data->flagRunV) {
        cout << "startFeed !" << endl;
        data->flagRunV = true;
    }
}

//======================================================================================================================
/// Callback called when the pipeline wants no more data for now
static void stopFeed(GstElement *source, GoblinData *data) {
    using namespace std;
    if (data->flagRunV) {
        cout << "stopFeed !" << endl;
        data->flagRunV = false;
    }
}

//======================================================================================================================
int main(int argc, char **argv) {
    using namespace std;
    cout << "VIDEO2 : Decode a video file with opencv and send to a gstreamer pipeline via appsrc" << endl;

    // Init gstreamer
    gst_init(&argc, &argv);

    if (argc != 2) {
        cout << "Usage:\nvideo2 <video_file>" << endl;
        return 0;
    }

    // Our global data
    GoblinData data;
    data.fileName = argv[1];
    cout << "Playing file : " << data.fileName << endl;

    // Create GSTreamer pipeline
    // Note: we don't know image size or framerate yet !
    // We'll give preliminary caps only which we will replace later
    // format=time is not really needed for video, but audio appsrc will not work without it !
    string pipeStr = "appsrc name=mysrc format=time caps=video/x-raw,format=BGR ! videoconvert ! autovideosink sync=1";
    GError *err = nullptr;
    data.pipeline = gst_parse_launch(pipeStr.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.pipeline);
    // Find our appsrc by name
    data.srcVideo = gst_bin_get_by_name(GST_BIN (data.pipeline), "mysrc");
    MY_ASSERT(data.srcVideo);

    // Important ! We don't want to abuse the appsrc queue
    // Thus let the pipeline itself signal us when it wants data
    // This is based on GLib signals
    g_signal_connect(data.srcVideo, "need-data", G_CALLBACK(startFeed), &data);
    g_signal_connect(data.srcVideo, "enough-data", G_CALLBACK(stopFeed), &data);

    // Start the bus thread
    thread threadBus([&data]() -> void {
        codeThreadBus(data.pipeline, data, "ELF");
    });

    // Start the appsrc process thread
    thread threadSrcV([&data]() -> void {
        codeThreadSrcV(data);
    });

    // Wait for threads
    threadBus.join();
    threadSrcV.join();

    // Destroy the pipeline
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);

    return 0;
}
