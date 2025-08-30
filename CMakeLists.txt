cmake_minimum_required(VERSION 3.16)
project(HSP004A_push)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)# sudo apt install pkg-config
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)
find_package(OpenCV REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(HSP004A_push
        main.cpp
)

target_include_directories(HSP004A_push PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(HSP004A_push
        ${OpenCV_LIBS}
        ${GSTREAMER_LIBRARIES}
        Threads::Threads
)
