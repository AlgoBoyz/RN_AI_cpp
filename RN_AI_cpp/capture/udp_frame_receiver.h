#pragma once

#include "capture.h"
#include "udp_wire_protocol.h"
#ifdef USE_CUDA
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
#include <vector>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib")

// Stores a fully assembled multi-region frame (raw encoded data).
struct AssembledFrame {
    std::vector<uint8_t> data;  // complete length-prefixed frame blob
    uint64_t frame_seq = 0;
};

class UdpFrameReceiver : public IScreenCapture {
public:
    explicit UdpFrameReceiver(int port);
    ~UdpFrameReceiver();

    // Returns region 0 as cv::Mat (backward compatible, used by AI pipeline).
    cv::Mat GetNextFrameCpu() override;

    // Get the full multi-region frame after the last GetNextFrameCpu() call.
    // Returns nullptr if no frame has been received yet.
    const DecodedMultiRegion* GetLastMultiRegion() const { return &last_mr_frame_; }

private:
    void ReceiveThread();

    int port_;
    SOCKET socket_;

    std::atomic<bool> should_stop_;
    std::thread receive_thread_;

    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    std::queue<AssembledFrame> frame_queue_;

    // Storage for the last fully decoded multi-region frame (for callers
    // that need regions other than 0).
    DecodedMultiRegion last_mr_frame_;
    std::vector<uint8_t> last_mr_frame_storage_;  // keeps the buffer alive

#ifdef USE_CUDA
    std::unique_ptr<GpuJpegCodec> gpu_codec_;
#endif

    static const int MAX_QUEUE_SIZE = 5;
    static const int FRAGMENT_TIMEOUT_MS = 200;
};
