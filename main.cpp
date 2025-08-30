#include <gst/gst.h>
#include <gst/app/gstappsrc.h>// 向GStreamer管道推流
#include <opencv2/opencv.hpp>
#include <string>
#include <iostream>
#include <thread>

void poll_bus(GstBus *bus) {
    while (true) {
        GstMessage *msg = gst_bus_pop(bus); // 非阻塞
        if (!msg) break; // 没有消息时退出循环

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError *e = nullptr;
                gchar *dbg = nullptr;
                gst_message_parse_error(msg, &e, &dbg);
                std::cerr << "[GST ERROR] " << (e ? e->message : "") << " dbg:" << (dbg ? dbg : "") << std::endl;
                if (e) g_error_free(e);
                if (dbg) g_free(dbg);
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError *e = nullptr;
                gchar *dbg = nullptr;
                gst_message_parse_warning(msg, &e, &dbg);
                std::cerr << "[GST WARN] " << (e ? e->message : "") << " dbg:" << (dbg ? dbg : "") << std::endl;
                if (e) g_error_free(e);
                if (dbg) g_free(dbg);
                break;
            }
            case GST_MESSAGE_EOS: {
                std::cerr << "[GST] EOS - End of Stream" << std::endl;
                return; // 接收到 EOS 消息，退出循环
            }
            case GST_MESSAGE_STATE_CHANGED: {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                std::cout << "[GST] STATE_CHANGED: " << gst_element_state_get_name(new_state) << std::endl;
                break;
            }
            default: break; // 其他消息不处理
        }
        gst_message_unref(msg);
    }
}

int main() {
    const int WIDTH = 1280;
    const int HEIGHT = 720;
    const int FPS = 30;
    cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) <<
                            619.97674, 0.0, 586.32027,
                            0.0, 625.27679, 339.90312,
                            0.0, 0.0, 1.0);
    cv::Mat distCoeffs = (cv::Mat_<double>(1, 5) << -0.291149, 0.057760, -0.006811, 0.001601, 0.0);
    cv::Mat map1, map2; //去畸变映射表
    cv::initUndistortRectifyMap(cameraMatrix, distCoeffs, cv::Mat(),
                                cameraMatrix, cv::Size(WIDTH, HEIGHT),
                                CV_16SC2, map1, map2); //只计算一次

    cv::VideoCapture cap = cv::VideoCapture(0, cv::CAP_V4L2);
    if (!cap.isOpened())
        return -2;
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, HEIGHT);
    cap.set(cv::CAP_PROP_FPS, FPS);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); //给硬件时间

    gst_init(nullptr, nullptr); // 初始化 GStreamer
    //is-live=true 会让 GStreamer 把数据源当作实时流来处理，避免缓冲过长；format=time 和 do-timestamp=true 保证帧有时间戳，
    //否则接收端可能会卡顿；block=false 避免下游堵塞时卡死推帧循环
    std::string pipeline_str =
            " appsrc name=mysrc is-live=true format=time do-timestamp=true block=false "
            " caps=video/x-raw,format=BGR,width=1280,height=720,framerate=30/1 " //明确声明输入帧格式和分辨率
            "! queue max-size-buffers=5 max-size-time=0 max-size-bytes=0 leaky=downstream "
            "! videoconvert ! video/x-raw,format=I420 " //在videoconvert后将 BGR 转换为 I420（YUV420）
            "! queue max-size-buffers=5 max-size-time=0 max-size-bytes=0 leaky=downstream "
            "! x265enc bitrate=1800 speed-preset=ultrafast tune=zerolatency key-int-max=30 " //tune=zerolatency 禁用B帧
            "! queue max-size-buffers=5 max-size-time=0 max-size-bytes=0 leaky=downstream "
            "! h265parse config-interval=1 " //每隔 1 个关键帧就插入一次 SPS/PPS（编码参数）
            "! queue max-size-buffers=20 max-size-time=0 max-size-bytes=0 leaky=downstream "
            "! rtspclientsink location=rtsp://127.0.0.1:8554/video1 latency=10 ";
    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &err);
    if (err) {
        std::cerr << "Pipeline 错误: " << err->message << std::endl;
        g_error_free(err);
        return -3;
    }
    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysrc");
    //从 GStreamer 管道 pipeline 中，按名称 "mysrc" 获取名为 appsrc 的元素指针
    // 设置 pipeline 状态
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstBus *bus = gst_element_get_bus(pipeline);
    std::thread bus_thread(poll_bus, bus); // 启动轮询线程

    cv::Mat frame, undistorted;
    for (guint64 frame_id = 0; true; frame_id++) {
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "cap.read(frame) error\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        cv::remap(frame, undistorted, map1, map2, cv::INTER_LINEAR); // 去畸变cv::INTER_LINEAR 够用

        // 用 gst_buffer_new_wrapped 零拷贝封装
        size_t size = undistorted.total() * undistorted.elemSize();
        GstBuffer *buffer = gst_buffer_new_wrapped(g_memdup(undistorted.data, size), size);
        // 时间戳
        GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(frame_id, GST_SECOND, FPS);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, FPS);
        if (gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer) != GST_FLOW_OK) {
            //通过 appsrc 把处理好的帧推送到 GStreamer 流水线里
            std::cerr << "Push buffer error!" << std::endl;
            break;
        }
        // std::cout << "Pushed frame " << frame_id << std::endl;
        // cv::imshow("undistorted", undistorted);
        // if (cv::waitKey(1) == 27) break; // ESC退出
    }

    // 发送 EOS 并清理
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    cap.release();
    cv::destroyAllWindows();
    bus_thread.join();
    return 0;
}

/*

*/
