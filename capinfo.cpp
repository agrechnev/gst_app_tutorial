/// CAPINFO : Information on pads, caps and elements, otherwise similar to FUN2
/// See our funny diagnostics in the modified busProcessMsg()

#include <iostream>
#include <string>

#include <gst/gst.h>

//======================================================================================================================
/// A simple assertion function
/// Never use C++ assert statement, the whole line will be removed in Release builds !
inline void myAssert(bool b, const std::string &s = "MYASSERT ERROR !") {
    if (!b)
        throw std::runtime_error(s);
}

/// And the macro version, requires true or anything non-zero (converted to bool true) to pass
/// This is similar to CV_Assert() of OPenCV
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
void diagnose(GstElement *element);

/// Process a single bus message, log messages, exit on error, return false on eof
/// This is a MODIFIED version, we run diagnose in here !
/// Diagnose would not give much info before we actually start playing the pipeline (PAUSED is enough)
static bool busProcessMsg(GstElement *pipeline, GstMessage *msg, const std::string &prefix, GstElement *elemToDiagnose) {
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
                diagnose(elemToDiagnose);
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
// A few useful routines for diagnostics

static gboolean printField(GQuark field, const GValue *value, gpointer pfx) {
    using namespace std;
    gchar *str = gst_value_serialize(value);
    cout << (char *) pfx << " " << g_quark_to_string(field) << " " << str << endl;
    g_free(str);
    return TRUE;
}

void printCaps(const GstCaps *caps, const std::string &pfx) {
    using namespace std;
    if (caps == nullptr)
        return;
    if (gst_caps_is_any(caps))
        cout << pfx << "ANY" << endl;
    else if (gst_caps_is_empty(caps))
        cout << pfx << "EMPTY" << endl;
    for (int i = 0; i < gst_caps_get_size(caps); ++i) {
        GstStructure *s = gst_caps_get_structure(caps, i);
        cout << pfx << gst_structure_get_name(s) << endl;
        gst_structure_foreach(s, &printField, (gpointer) pfx.c_str());
    }
}


void printPadsCB(const GValue * item, gpointer userData) {
    using namespace std;
    GstElement *element = (GstElement *)userData;
    GstPad *pad = (GstPad *)g_value_get_object(item);
    myAssert(pad);
    cout << "PAD : " << gst_pad_get_name(pad) << endl;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    char * str = gst_caps_to_string(caps);
    cout << str << endl;
    free(str);
}

void printPads(GstElement *element) {
    using namespace std;
    GstIterator *pad_iter = gst_element_iterate_pads(element);
    gst_iterator_foreach(pad_iter, printPadsCB, element);
    gst_iterator_free(pad_iter);

}
void diagnose(GstElement *element) {
    using namespace std;
    cout << "=====================================" << endl;
    cout << "DIAGNOSE element : " << gst_element_get_name(element) << endl;
    printPads(element);
    cout << "=====================================" << endl;
}


//======================================================================================================================
int main(int argc, char **argv) {
    using namespace std;
    cout << "GST CAPINFO : Information on pads, caps and elements" << endl;

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

    // Play the pipeline
    MY_ASSERT(gst_element_set_state(pipeline, GST_STATE_PLAYING));

    // Message - processing loop
    GstBus *bus = gst_element_get_bus(pipeline);
    for (;;) {
        // Wait for message, no filtering, any message goes
        GstMessage *msg = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
        bool res = busProcessMsg(pipeline, msg, "GOBLIN", conv);
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

