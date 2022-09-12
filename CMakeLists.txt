cmake_minimum_required(VERSION 3.1)
project(gst_app_tutorial)
set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)

# GStreamer
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-audio-1.0 gstreamer-video-1.0  gstreamer-app-1.0)
message("GST_INCLUDE_DIRS = ${GST_INCLUDE_DIRS}")
message("GST_LIBRARY_DIRS = ${GST_LIBRARY_DIRS}")
message("GST_LIBRARIES = ${GST_LIBRARIES}")

link_directories( ${GST_LIBRARY_DIRS}  )
include_directories( ${GST_INCLUDE_DIRS}  )


# OpenCV ${OpenCV_LIBS}, used for some examples
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(fun1 fun1.cpp)
target_link_libraries(fun1 ${GST_LIBRARIES})

add_executable(fun2 fun2.cpp)
target_link_libraries(fun2 ${GST_LIBRARIES})

add_executable(capinfo capinfo.cpp)
target_link_libraries(capinfo ${GST_LIBRARIES})

add_executable(video1 video1.cpp)
target_link_libraries(video1 ${GST_LIBRARIES} ${OpenCV_LIBS})

add_executable(video2 video2.cpp)
target_link_libraries(video2 ${GST_LIBRARIES} ${OpenCV_LIBS})

add_executable(video3 video3.cpp)
target_link_libraries(video3 ${GST_LIBRARIES} ${OpenCV_LIBS})

add_executable(audio1 audio1.cpp)
target_link_libraries(audio1 ${GST_LIBRARIES})

add_executable(av1 av1.cpp)
target_link_libraries(av1 ${GST_LIBRARIES} ${OpenCV_LIBS})
