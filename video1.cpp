// Created by IT-JIM
// VIDEO1 : Send video to appsink, display with cv::imshow()

#include <iostream>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>


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
    GstElement *pipeline = nullptr;
    GstElement *sinkVideo = nullptr;
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
/// Appsink process thread
void codeThreadProcessV(GoblinData &data) {
    using namespace std;
    for (;;) {
        // Exit on EOS
        if (gst_app_sink_is_eos(GST_APP_SINK(data.sinkVideo))) {
            cout << "EOS !" << endl;
            break;
        }

        // Pull the sample (synchronous, wait)
        GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(data.sinkVideo));
        if (sample == nullptr) {
            cout << "NO sample !" << endl;
            break;
        }

        // Get width and height from sample caps (NOT element caps)
        GstCaps *caps = gst_sample_get_caps(sample);
        MY_ASSERT(caps != nullptr);
        GstStructure *s = gst_caps_get_structure(caps, 0);
        int imW, imH;
        MY_ASSERT(gst_structure_get_int(s, "width", &imW));
        MY_ASSERT(gst_structure_get_int(s, "height", &imH));
        cout << "Sample: W = " << imW << ", H = " << imH << endl;

//        cout << "sample !" << endl;
        // Process the sample
        // "buffer" and "map" are used to access raw data in the sample
        // "buffer" is a single data chunk, for raw video it's 1 frame
        // "buffer" is NOT a queue !
        // "Map" is the helper to access raw data in the buffer
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo m;
        MY_ASSERT(gst_buffer_map(buffer, &m, GST_MAP_READ));
        MY_ASSERT(m.size == imW * imH * 3);
//        cout << "size = " << map.size << " ==? " << imW*imH*3 << endl;

        // Wrap the raw data in OpenCV frame and show on screen
        cv::Mat frame(imH, imW, CV_8UC3, (void *) m.data);
        cv::imshow("frame", frame);
        int key = cv::waitKey(1);

        // Don't forget to unmap the buffer and unref the sample
        gst_buffer_unmap(buffer, &m);
        gst_sample_unref(sample);
        if (27 == key)
            exit(0);
    }
}

//======================================================================================================================
int main(int argc, char **argv) {
    using namespace std;
    cout << "VIDEO1 : Send video to appsink, display with cv::imshow()" << endl;

    // Init gstreamer
    gst_init(&argc, &argv);

    if (argc != 2) {
        cout << "Usage:\nvideo1 <video_file>" << endl;
        return 0;
    }
    string fileName(argv[1]);
    cout << "Playing file : " << fileName << endl;

    // Our global data
    GoblinData data;

    // Set up the pipeline
    // Caps in appsink are important
    // max-buffers=2 to limit the queue and RAM usage
    // sync=1 for real-time playback, try sync=0 for fun !
    string pipeStr = "filesrc location=" + fileName +
                     " ! decodebin ! videoconvert ! appsink name=mysink max-buffers=2 sync=1 caps=video/x-raw,format=BGR";
    GError *err = nullptr;
    data.pipeline = gst_parse_launch(pipeStr.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.pipeline);
    // Find our appsink by name
    data.sinkVideo = gst_bin_get_by_name(GST_BIN (data.pipeline), "mysink");
    MY_ASSERT(data.sinkVideo);

    // Play the pipeline
    MY_ASSERT(gst_element_set_state(data.pipeline, GST_STATE_PLAYING));

    // Start the bus thread
    thread threadBus([&data]() -> void {
        codeThreadBus(data.pipeline, data, "GOBLIN");
    });

    // Start the appsink process thread
    thread threadProcess([&data]() -> void {
        codeThreadProcessV(data);
    });

    // Wait for threads
    threadBus.join();
    threadProcess.join();

    // Destroy the pipeline
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);

    return 0;
}