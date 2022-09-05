/// FUN 2 : Creating pipeline by hand, message processing

#include <iostream>
#include <string>

#include <gst/gst.h>

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
int main(int argc, char **argv) {
    using namespace std;
    cout << "GST FUN 2 : Creating pipeline by hand, message processing" << endl;

    // Init gstreamer
    cout << "argc before = " << argc << endl;
    gst_init(&argc, &argv);
    cout << "argc after = " << argc << endl;

    // Create a pipeline by hand, don't forget error checks !
    // Important: gst_parse_launch() tends to give a nonzero output even on error!
//    string pipelineStr = "videotestsrc pattern=0 ! videoconvert ! autovideosink";
    // First, create the elements
    GstElement *src = gst_element_factory_make("videotestsrc", "goblin_src");
    GstElement *conv = gst_element_factory_make("videoconvert", "goblin_conv");
    GstElement *sink = gst_element_factory_make("autovideosink", "goblin_sink");
    GstElement *pipeline = gst_pipeline_new("goblin_pipeline");
    MY_ASSERT(src && conv && sink && pipeline);

    // Set up parameters if needed
    g_object_set(src, "pattern", 18, nullptr);

    // Add and link elements
    gst_bin_add_many(GST_BIN(pipeline), src, conv, sink, nullptr);
    MY_ASSERT(gst_element_link_many(src, conv, sink, nullptr));

    // Note: In this tutorial we don't cover linking dynamic and request pads
    // See the official GStreamer tutorial !

    // Play the pipeline
    MY_ASSERT(gst_element_set_state(pipeline, GST_STATE_PLAYING));

    // Message - processing loop
    GstBus *bus = gst_element_get_bus(pipeline);
    for (;;) {
        // Wait for message, no filtering, any message goes
        GstMessage *msg = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
        bool res = busProcessMsg(pipeline, msg, "GOBLIN");
        gst_message_unref(msg);
        if (!res)
            break;
    }
    gst_object_unref(bus);

    // Stop and free the pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
//======================================================================================================================

