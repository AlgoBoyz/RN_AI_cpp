#pragma once

#include "capture.h"
#include "udp_wire_protocol.h"
#ifndef _DML_BUILD
#include "../codec/gpu_jpeg_codec.h"
#endif
#include <opencv2/opencv.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

class UdpFrameReceiver : public IScreenCapture {
public:
    explicit UdpFrameReceiver(int port);
    ~UdpFrameReceiver();
    
    cv::Mat GetNextFrameCpu() override;
    
private:
    void ReceiveThread();
    
    int port_;
    SOCKET socket_;
    
    std::atomic<bool> should_stop_;
    std::thread receive_thread_;
    
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    std::queue<cv::Mat> frame_queue_;
    
    std::unique_ptr<GpuJpegCodec> gpu_codec_;
    
    static const int MAX_QUEUE_SIZE = 5;
    static const int FRAGMENT_TIMEOUT_MS = 5;
};
