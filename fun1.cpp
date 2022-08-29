/// FUN 1 : An (almost) minimal GStreamer C++ example

#include <iostream>
#include <string>

// Include GStreamer, minimal version
// Sometimes you need additional headers also
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
inline void checkErr(GError * err){
    if (err) {
        std::cerr << "checkErr : " << err->message << std::endl;
        exit(0);
    }
}
//======================================================================================================================
int main(int argc, char **argv) {
    using namespace std;
    cout << "GST FUN 1 : An (almost) minimal GStreamer C++ example" << endl;

    // Init gstreamer
    // First thing in GST project, always
    // It inits gstreamer, parses argc, argv, and REMOVES gst options from argv
    // Such as --gst-debug-level=2
    cout << "argc before = " << argc << endl;
    gst_init(&argc, &argv);
    cout << "argc after = " << argc << endl;

    // Create a pipeline from a string, don't forget error checks !
    // Important: gst_parse_launch() tends to give a nonzero output even on error!
    string pipelineStr = "videotestsrc pattern=0 ! videoconvert ! autovideosink";
    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(pipelineStr.c_str(), &err);
    checkErr(err);
    MY_ASSERT(pipeline);

    // Play the pipeline, assert success
    MY_ASSERT(gst_element_set_state(pipeline, GST_STATE_PLAYING));

    // Now we'll wait for either error or EOS from our pipeline
    // This is vital, otherwise the program will just finish not waiting for anything
    // Note how GStreamer does not have a main loop per se, we'll have to create one
    // Note also how Gstreamer is multi-thread and thread-friendly, pipeline runs in different thread(s) !

    // First get the bus of our pipeline
    GstBus *bus = gst_element_get_bus (pipeline);
    // Then wait for a message forever, filtered Error or EOF messages only
    GstMessage *msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    // Note that we don't print the error message yet, we'll do this in the next example
    // Unref msg and bus, freeing memory is super-vital in gstreamer, otherwise you'll get a memory leak !!!
    gst_message_unref(msg);
    gst_object_unref(bus);

    // The general unref rule is like this:
    // If you don't need myBanana anymore, write
    // gst_banana_unref(myBanana);
    // If no such function, try
    // gst_object_unref(myBanana);
    // If the code does not work, it means that you shouldn't unref myBanana

    // Stop and free the pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
//======================================================================================================================

