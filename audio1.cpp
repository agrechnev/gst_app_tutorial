//
// Created by IT-JIM 
// AUDIO1: Two audio pipelines, with custom audio processing in the middle, no video
// This is very similar to VIDEO3, but for audio

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>


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
    GstElement *goblinPipeline = nullptr;
    GstElement *goblinSinkA = nullptr;
    GstElement *elfPipeline = nullptr;
    GstElement *elfSrcA = nullptr;

    /// Appsrc flag: when it's true, send the frames, otherwise wait
    std::atomic_bool flagRunA{false};
    /// True if the elf pipeline has initialized and started splaying
    std::atomic_bool flagElfStarted{false};
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
/// Callback called when the pipeline wants more data
static void startFeed(GstElement *source, guint size, GoblinData *data) {
    using namespace std;
    if (!data->flagRunA) {
        cout << "startFeed !" << endl;
        data->flagRunA = true;
    }
}

//======================================================================================================================
/// Callback called when the pipeline wants no more data for now
static void stopFeed(GstElement *source, GoblinData *data) {
    using namespace std;
    if (data->flagRunA) {
        cout << "stopFeed !" << endl;
        data->flagRunA = false;
    }
}
//======================================================================================================================
/// Process audio
void codeThreadProcessA(GoblinData &data) {
    using namespace std;
    for(;;) {
        // We wait until ELF wants data, but only if ELF is already started
        while (data.flagElfStarted && !data.flagRunA) {
            cout << "(wait)" << endl;
            this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Check for Goblin EOS
        if (gst_app_sink_is_eos(GST_APP_SINK(data.goblinSinkA))) {
            cout << "GOBLIN EOS !" << endl;
            break;
        }

        // Pull the sample from Goblin appsink
        GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(data.goblinSinkA));
        if (sample == nullptr) {
            cout << "NO sample !" << endl;
            break;
        }

        // Check if ELF is initialized
        if (!data.flagElfStarted) {
            // Use sample caps verbatim to ELF appsrc and re-negotiate
            //            // Make a copy to be safe (probably not needed)
            GstCaps *caps = gst_sample_get_caps(sample);
            MY_ASSERT(caps != nullptr);
            GstCaps *capsElf = gst_caps_copy(caps);

            g_object_set(data.elfSrcA, "caps", capsElf, nullptr);
            gst_caps_unref(capsElf);

            // Start the elf pipeline
            GstStateChangeReturn ret = gst_element_set_state(data.elfPipeline, GST_STATE_PLAYING);
            MY_ASSERT(ret != GST_STATE_CHANGE_FAILURE);
            data.flagElfStarted = true;
        }

        // Process sample
        GstBuffer *bufferIn = gst_sample_get_buffer(sample);
        GstMapInfo mapIn;
        MY_ASSERT(gst_buffer_map(bufferIn, &mapIn, GST_MAP_READ));

        // Create the output bufer and send it to elfSrc
        // Here we simply copy the input buffer to the output
        // If needed, some sound processing on the raw audio waveform can be put in the middle
        int bufferSize = mapIn.size;
        cout << "SAMPLE: bufferSize = " << mapIn.size << endl;
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
    gst_app_src_end_of_stream(GST_APP_SRC(data.elfSrcA));
}
//======================================================================================================================
int main(int argc, char **argv) {
    using namespace std;
    cout << "AUDIO1: Two audio pipelines, with custom audio processing in the middle, no video" << endl;

    // Init gstreamer
    gst_init(&argc, &argv);

    if (argc != 2) {
        cout << "Usage:\naudio1 <audio_file>" << endl;
        return 0;
    }
    string fileName(argv[1]);
    cout << "Playing file : " << fileName << endl;

    // Our global data
    GoblinData data;

    // Set up GOBLIN (input) pipeline
    // Here we force the int16 interleaved format, but do not specify the sample rate
    string pipeStrGoblin = "filesrc location=" + fileName +
                           " ! decodebin ! audioconvert ! appsink name=goblin_sink max-buffers=2 sync=1 caps=audio/x-raw,format=S16LE,layout=interleaved";
    GError *err = nullptr;
    data.goblinPipeline = gst_parse_launch(pipeStrGoblin.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.goblinPipeline);
    data.goblinSinkA = gst_bin_get_by_name(GST_BIN (data.goblinPipeline), "goblin_sink");
    MY_ASSERT(data.goblinSinkA);

    // Set up ELF (output pipeline)
    // Note that appsrc does not have full caps yet as usual
    // format=time is vital for audio for some reason
    string pipeStrElf = "appsrc name=elf_src format=time caps=audio/x-raw,format=S16LE,layout=interleaved ! audioconvert ! audioresample ! autoaudiosink sync=1";
    data.elfPipeline = gst_parse_launch(pipeStrElf.c_str(), &err);
    checkErr(err);
    MY_ASSERT(data.elfPipeline);
    data.elfSrcA = gst_bin_get_by_name(GST_BIN (data.elfPipeline), "elf_src");
    MY_ASSERT(data.elfSrcA);
    // Add calbacks like in video2
    g_signal_connect(data.elfSrcA, "need-data", G_CALLBACK(startFeed), &data);
    g_signal_connect(data.elfSrcA, "enough-data", G_CALLBACK(stopFeed), &data);

    // Play the Goblin pipeline only (Elf will start a bit later)
    MY_ASSERT(gst_element_set_state(data.goblinPipeline, GST_STATE_PLAYING));

    // Audio processing thread (from goblin appsink to elf appsrc)
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

    // Wait for threads
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
