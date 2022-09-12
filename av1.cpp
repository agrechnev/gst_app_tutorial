//
// Created by IT-JIM 
// AV1: Two pipelines, with both audio and video (video3 + audio1 combined !)

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
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
/// Our global data
struct GoblinData {
    // The two pipelines
    // Now we have two appsrcS and two appsinkS, for audio and video respectively
    GstElement *goblinPipeline = nullptr;
    GstElement *goblinSinkV = nullptr;
    GstElement *goblinSinkA = nullptr;
    GstElement *elfPipeline = nullptr;
    GstElement *elfSrcV = nullptr;
    GstElement *elfSrcA = nullptr;

    /// Appsrc video flag: when it's true, send the video frames, otherwise wait
    std::atomic_bool flagRunV{false};
    /// Appsrc audio flag: when it's true, send the audio frames, otherwise wait
    std::atomic_bool flagRunA{false};

    // Now we have a more sophisticated initialization, we can start ELF only after BOTH audio and video are initialized !
    /// Has ELF started ?
    std::atomic_bool flagElfStarted{false};
    /// Is audio initialized ?
    std::atomic_bool flagInitA{false};
    /// Is video initialized ?
    std::atomic_bool flagInitV{false};

    /// A mutext to protect starting of ELF
    std::mutex mutexElfStart;
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
/// Start the elf pipeline, thread-safe to avoid double start
void playElf(GoblinData &data) {
    using namespace std;
    lock_guard<mutex> lock(data.mutexElfStart);
    // We check again under mutex, the start code runs only once strictly !
    if (!data.flagElfStarted) {
        cout << "PLAYELF !!!! PLAYELF !!!! PLAYELF !!!! " << endl;
        GstStateChangeReturn ret = gst_element_set_state(data.elfPipeline, GST_STATE_PLAYING);
        MY_ASSERT(ret != GST_STATE_CHANGE_FAILURE);
        data.flagElfStarted = true;
    }
}
//======================================================================================================================
/// Process video frames
void codeThreadProcessV(GoblinData &data) {
    using namespace std;
    using namespace cv;

    for (;;) {
        // We wait until ELF wants data, but only if initialized
        while (data.flagInitV && !data.flagRunV) {
            cout << "V : (wait)" << endl;
            this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Check for Goblin EOS
        if (gst_app_sink_is_eos(GST_APP_SINK(data.goblinSinkV))) {
            cout << "V : GOBLIN EOS !" << endl;
            break;
        }

        // Pull the sample from Goblin appsink
        GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(data.goblinSinkV));
        if (sample == nullptr) {
            cout << "V : NO sample !" << endl;
            break;
        }

        // Get width and height from sample caps
        GstCaps *caps = gst_sample_get_caps(sample);
        myAssert(caps != nullptr);
//        printCaps(caps, "");

        GstStructure *s = gst_caps_get_structure(caps, 0);
        int imW, imH;
        MY_ASSERT(gst_structure_get_int(s, "width", &imW));
        MY_ASSERT(gst_structure_get_int(s, "height", &imH));
        int f1, f2;
        MY_ASSERT(gst_structure_get_fraction(s, "framerate", &f1, &f2));
//        cout << "V : Sample: W = " << imW << ", H = " << imH << ", framerate = " << f1 << " / " << f2 << endl;

        // Initialization is now a bit more tricky, we want to play ELF
        // only after BOTH A and V are initialized !
        if (!data.flagInitV) {
            // Use sample caps verbatim to ELF appsrc and re-negotiate
            // Make a copy to be safe (probably not needed)
            GstCaps *capsElf = gst_caps_copy(caps);
            g_object_set(data.elfSrcV, "caps", capsElf, nullptr);
            gst_caps_unref(capsElf);
            data.flagInitV = true;

            // Now we can play the ELF pipeline if needed
            if (!data.flagElfStarted && data.flagInitA && data.flagInitV)
                playElf(data);
        }

        // Copy data from the sample to cv::Mat()
        GstBuffer *bufferIn = gst_sample_get_buffer(sample);
        GstMapInfo mapIn;
        myAssert(gst_buffer_map(bufferIn, &mapIn, GST_MAP_READ));
        myAssert(mapIn.size == imW * imH * 3);
        // Don't forget the Timestamp
        uint64_t pts = bufferIn->pts;
        // Clone to be safe, we don't want to modify the input buffer
        Mat frame = Mat(imH, imW, CV_8UC3, (void *) mapIn.data).clone();
        gst_buffer_unmap(bufferIn, &mapIn);

        // Modify the frame: apply photo negative to the middle 1/9 of the image
        Mat frameMid(frame, Rect2i(imW/3, imH/3, imW/3, imH/3));
        bitwise_not(frameMid, frameMid);
        // Create the output bufer and send it to elfSrc
        int bufferSize = frame.cols * frame.rows * 3;
        GstBuffer *bufferOut = gst_buffer_new_and_alloc(bufferSize);
        GstMapInfo mapOut;
        gst_buffer_map(bufferOut, &mapOut, GST_MAP_WRITE);
        memcpy(mapOut.data, frame.data, bufferSize);
        gst_buffer_unmap(bufferOut, &mapOut);
        // Copy the input packet timestamp
        bufferOut->pts = pts;
        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(data.elfSrcV), bufferOut);
    }
    // Send EOS to ELF
    gst_app_src_end_of_stream(GST_APP_SRC(data.elfSrcV));
}
//======================================================================================================================
/// Process audio
void codeThreadProcessA(GoblinData &data) {
    using namespace std;
    for(;;) {
        // We wait until ELF wants data, but only if ELF is already started
        while (data.flagInitA && !data.flagRunA) {
            cout << "A : (wait)" << endl;
            this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Check for Goblin EOS
        if (gst_app_sink_is_eos(GST_APP_SINK(data.goblinSinkA))) {
            cout << "A : GOBLIN EOS !" << endl;
            break;
        }

        // Pull the sample from Goblin appsink
        GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(data.goblinSinkA));
        if (sample == nullptr) {
            cout << "A : NO sample !" << endl;
            break;
        }

        // Initialization is now a bit more tricky, we want to play ELF
        // only after BOTH A and V are initialized !
        if (!data.flagInitA) {
            // Use sample caps verbatim to ELF appsrc and re-negotiate
            //            // Make a copy to be safe (probably not needed)
            GstCaps *caps = gst_sample_get_caps(sample);
            MY_ASSERT(caps != nullptr);
            GstCaps *capsElf = gst_caps_copy(caps);

            g_object_set(data.elfSrcA, "caps", capsElf, nullptr);
            gst_caps_unref(capsElf);
            data.flagInitA = true;

            // Now we can play the ELF pipeline if needed
            if (!data.flagElfStarted && data.flagInitA && data.flagInitV)
                playElf(data);
        }

        // Process sample
        GstBuffer *bufferIn = gst_sample_get_buffer(sample);
        GstMapInfo mapIn;
        MY_ASSERT(gst_buffer_map(bufferIn, &mapIn, GST_MAP_READ));

        // Create the output bufer and send it to elfSrc
        // Here we simply copy the input buffer to the output
        // If needed, some sound processing on the raw audio waveform can be put in the middle
        int bufferSize = mapIn.size;
//        cout << "A : SAMPLE: bufferSize = " << mapIn.size << endl;
        GstBuffer *bufferOut = gst_buffer_new_and_alloc(bufferSize);
        GstMapInfo mapOut;
        gst_buffer_map(bufferOut, &mapOut, GST_MAP_WRITE);
        memcpy(mapOut.data, mapIn.data, bufferSize);
        gst_buffer_unmap(bufferIn, &mapIn);
        gst_buffer_unmap(bufferOut, &mapOut);
        // Copy the input packet timestamp and duration
        bufferOut->pts = bufferIn->pts;
        bufferOut->duration = bufferIn->duration;
        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(data.elfSrcA), bufferOut);

        gst_sample_unref(sample);
    }
    // Send EOS to ELF
    gst_app_src_end_of_stream(GST_APP_SRC(data.elfSrcA));
}
//======================================================================================================================
/// Callback called when the pipeline wants more data
/// A more tricky version to run with both audio and video
static void startFeed(GstElement *source, guint size, GoblinData *data) {
    using namespace std;
    bool isV = false;
    if (source == data->elfSrcV)
        isV = true;
    else
        myAssert(source == data->elfSrcA);

    atomic_bool *f = isV ? &data->flagRunV : &data->flagRunA;
    if (!(*f)) {
        string prefix = isV ? "V : " : "A : ";
        cout << prefix << "startFeed !" << endl;
        (*f) = true;
    }
}
//======================================================================================================================
/// Callback called when the pipeline has enough data
/// A more tricky version to run with both audio and video
static void stopFeed(GstElement *source, GoblinData *data) {
    using namespace std;
    bool isV = false;
    if (source == data->elfSrcV)
        isV = true;
    else
        myAssert(source == data->elfSrcA);

    atomic_bool *f = isV ? &data->flagRunV : &data->flagRunA;
    if (*f) {
        string prefix = isV ? "V : " : "A : ";
        cout << prefix << "stopFeed !" << endl;
        (*f) = false;
    }
}
//======================================================================================================================
int main(int argc, char **argv){
    using namespace std;
    cout << "AV1: Two pipelines, with both audio and video (video3 + audio1 combined !)" << endl;

    // Init gstreamer
    gst_init(&argc, &argv);

    if (argc != 2) {
        cout << "Usage:\nav1 <video_file>" << endl;
        return 0;
    }
    string fileName(argv[1]);
    cout << "Playing file : " << fileName << endl;

    // Our global data
    GoblinData data;

    // Set up GOBLIN (input) pipeline
    // Now we have a branched pipeline with two appsinks, for audio and video
    // queues are important !!!
    string pipeStrGoblin = "filesrc location=" + fileName +
                     " ! decodebin name=d ! queue ! videoconvert ! appsink sync=false name=goblin_sink_v caps=video/x-raw,format=BGR " +
                     "d. ! queue ! audioconvert ! appsink sync=false name=goblin_sink_a caps=audio/x-raw,format=S16LE,layout=interleaved";
    GError *err = nullptr;
    data.goblinPipeline = gst_parse_launch(pipeStrGoblin.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.goblinPipeline);
    data.goblinSinkV = gst_bin_get_by_name(GST_BIN (data.goblinPipeline), "goblin_sink_v");
    MY_ASSERT(data.goblinSinkV);
    data.goblinSinkA = gst_bin_get_by_name(GST_BIN (data.goblinPipeline), "goblin_sink_a");
    MY_ASSERT(data.goblinSinkA);

    // Set up ELF (output pipeline)
    // Note that appsrcs do not have full caps yet as usual
    // Note that there is no ! sign after autovideosink
    // Here we have two unlinked branches in one pipeline, but it's OK
    string pipeStrElf = string("appsrc name=elf_src_v format=time caps=video/x-raw,format=BGR ! queue ! videoconvert ! autovideosink ") +
                        "appsrc name=elf_src_a format=time caps=audio/x-raw,format=S16LE,layout=interleaved ! queue ! audioconvert ! audioresample ! autoaudiosink";
    data.elfPipeline = gst_parse_launch(pipeStrElf.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.elfPipeline);
    data.elfSrcV = gst_bin_get_by_name(GST_BIN (data.elfPipeline), "elf_src_v");
    MY_ASSERT(data.elfSrcV);
    data.elfSrcA = gst_bin_get_by_name(GST_BIN (data.elfPipeline), "elf_src_a");
    MY_ASSERT(data.elfSrcA);
    // Add callbacks for both sources, we use same function for both sinks
    g_signal_connect(data.elfSrcV, "need-data", G_CALLBACK(startFeed), &data);
    g_signal_connect(data.elfSrcV, "enough-data", G_CALLBACK(stopFeed), &data);
    g_signal_connect(data.elfSrcA, "need-data", G_CALLBACK(startFeed), &data);
    g_signal_connect(data.elfSrcA, "enough-data", G_CALLBACK(stopFeed), &data);

    // Play the Goblin pipeline only (Elf will start a bit later)
    MY_ASSERT(gst_element_set_state(data.goblinPipeline, GST_STATE_PLAYING));

    // Video processing thread (from goblin appsink to elf appsrc)
    thread threadProcessV([&data]{
        codeThreadProcessV(data);
    });
    // Audio processing thread
    thread threadProcessA([&data]{
        codeThreadProcessA(data);
    });
    // Now we need two bus threads: one for each pipeline !
    thread threadBusGoblin([&data]{
        codeThreadBus(data.goblinPipeline, data, "GOBLIN");
    });
    thread threadBusElf([&data]{
        codeThreadBus(data.elfPipeline, data, "ELF");
    });

    // Wait for the threads
    threadProcessV.join();
    threadProcessA.join();
    threadBusGoblin.join();
    threadBusElf.join();

    // Destroy the two pipelines
    gst_element_set_state(data.goblinPipeline, GST_STATE_NULL);
    gst_object_unref(data.goblinPipeline);
    gst_element_set_state(data.elfPipeline, GST_STATE_NULL);
    gst_object_unref(data.elfPipeline);

    return 0;
}
//======================================================================================================================