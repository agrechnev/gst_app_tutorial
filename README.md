An informal GStreamer C++ tutorial, focused on `appsrc` and `appsink`
===============================================

By Oleksiy Grechnyev, IT-JIM, 2022.

https://www.it-jim.com/

This does not replace, but complements the official GStreamer tutorials. Here we focus on using `appsrc` and `appsink`
for custom video (or audio) processing in the C++ code. In such situation, GStreamer is used mainly for encoding and
decoding of various audio and video formats. 

Note: Our examples are written in C++ and not C. We are NOT using any GLib stuff we don't really need. 
This includes the GLib "main loop". Our code is well-commented. For the best experience, follow the examples in the order 
specified below.

On Linux, the code builds fine with CMake.  
On other OS-es, you'll probably have to replace `pkg-config` with something else.  
OpenCV is used in some examples for custom image processing.  

The examples:

* `fun1` : An (almost) minimal GStreamer C++ example  
* `fun2` : Creating pipeline by hand, message processing  
* `capinfo` :  Information on pads, caps and elements, otherwise similar to `fun2`  
* `video1`: Send video to `appsink`, display with `cv::imshow()`   
* `video2` : Decode a video file with opencv and send to a gstreamer pipeline via `appsrc`  
* `video3` : Two pipelines, with custom video processing in the middle, no audio  
* `audio1` : Two audio pipelines, with custom audio processing in the middle, no video  
* `av1` : Two pipelines, with both audio and video (`video3` + `audio1` combined !)  
