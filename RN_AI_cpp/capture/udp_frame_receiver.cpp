#include "udp_frame_receiver.h"
#include "../config/config.h"
#include "../mouse/async_logger.h"
#include <iostream>
#include <mutex>

// Log file for UDP receive performance
static FILE* g_udp_log = nullptr;
static std::once_flag g_udp_log_init;

static void init_udp_log() {
    const char* profile = getenv("USERPROFILE");
    if (!profile) return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\rn_ai", profile);
    CreateDirectoryA(path, nullptr);
    sprintf_s(path, "%s\\rn_ai\\udp_receiver.csv", profile);
    fopen_s(&g_udp_log, path, "w");
    if (g_udp_log) {
        fprintf(g_udp_log, "time,frame_id,frag_count,assembly_ms,decode_ms,width,height,capture_ts_ms,frame_seq\n");
        fflush(g_udp_log);
    }
}

static void log_frame(uint16_t frame_id, uint8_t frag_count,
    double assembly_ms, double decode_ms, int w, int h,
    uint64_t capture_ts_ms, uint64_t frame_seq) {
    if (!g_udp_log) return;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    fprintf(g_udp_log, "%02d:%02d:%02d,%u,%u,%.2f,%.2f,%d,%d,%llu,%llu\n",
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        frame_id, frag_count, assembly_ms, decode_ms, w, h,
        (unsigned long long)capture_ts_ms, (unsigned long long)frame_seq);
    fflush(g_udp_log);
}

UdpFrameReceiver::UdpFrameReceiver(int port)
    : port_(port), socket_(INVALID_SOCKET), should_stop_(false) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[UdpFrameReceiver] WSAStartup failed" << std::endl;
        return;
    }
    
    // Create UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "[UdpFrameReceiver] Failed to create socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }
    
    // Bind to port
    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(port_);
    
    if (bind(socket_, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        std::cerr << "[UdpFrameReceiver] Failed to bind socket: " << WSAGetLastError() << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    // Set receive timeout so recvfrom unblocks periodically to check should_stop_
    DWORD timeout_ms = 500;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    // Increase receive buffer to hold hundreds of fragments (default 8KB is too small)
    int rcvbuf = 256 * 1024; // 256KB
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

#ifdef USE_CUDA
    // Initialize GPU codec
    gpu_codec_ = std::make_unique<GpuJpegCodec>();
    if (!gpu_codec_->initialize()) {
        std::cerr << "[UdpFrameReceiver] Failed to initialize GPU JPEG codec" << std::endl;
    }
#endif
    
    // Start receive thread
    should_stop_ = false;
    receive_thread_ = std::thread(&UdpFrameReceiver::ReceiveThread, this);
    
    std::cout << "[UdpFrameReceiver] Initialized on port " << port_ << std::endl;
}

UdpFrameReceiver::~UdpFrameReceiver() {
    should_stop_ = true;
    frame_cv_.notify_one();  // wake up GetNextFrameCpu

    // Wake up receive thread by closing the socket (recvfrom will return error)
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

#ifdef USE_CUDA
    if (gpu_codec_) {
        gpu_codec_->shutdown();
    }
#endif

    WSACleanup();
}

void UdpFrameReceiver::ReceiveThread() {
    std::call_once(g_udp_log_init, init_udp_log);

    std::vector<uint8_t> buffer(MAX_DATAGRAM_SIZE);
    std::unique_ptr<FragmentAssembler> current_assembler;
    std::chrono::steady_clock::time_point assembler_start_time;
    uint16_t current_frame_id = 0;
    
    while (!should_stop_) {
        sockaddr_in from_addr;
        int from_len = sizeof(from_addr);
        
        int bytes_received = recvfrom(socket_, (char*)buffer.data(), buffer.size(), 0,
                                      (sockaddr*)&from_addr, &from_len);
        
        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAETIMEDOUT) {
                continue;
            }
            // During shutdown the socket is closed, causing an error - that's expected
            if (should_stop_) break;
            std::cerr << "[UdpFrameReceiver] Receive error: " << error << std::endl;
            break;
        }

        if (bytes_received <= 0) {
            continue;
        }
        
        // Parse fragment header
        auto frag_info = FragmentEncoder::decode_datagram(buffer.data(), bytes_received);
        if (!frag_info) {
            continue;
        }
        
        // Check if we need a new assembler (frame_id changed)
        if (!current_assembler || current_assembler->frame_id() != frag_info->frame_id) {
            if (current_assembler && g_udp_log) {
                // Log how many fragments the PREVIOUS frame got before being replaced
                fprintf(g_udp_log, "DISCARD,%u,%u,%u\n",
                    current_assembler->frame_id(),
                    current_assembler->frag_count(),
                    current_assembler->received_count());
            }
            current_frame_id = frag_info->frame_id;
            current_assembler = std::make_unique<FragmentAssembler>(frag_info->frame_id, frag_info->frag_count);
            assembler_start_time = std::chrono::steady_clock::now();
            if (g_udp_log) {
                fprintf(g_udp_log, "ARRIVE,%u,%u,,,\n", frag_info->frame_id, frag_info->frag_count);
            }
        }
        
        // Add fragment to assembler
        if (!current_assembler->add_fragment(frag_info->frag_idx, frag_info->data, frag_info->data_size)) {
            continue;
        }
        
        // Check if frame is complete
        if (current_assembler->is_complete()) {
            auto t_assemble_end = std::chrono::steady_clock::now();
            double assembly_ms = std::chrono::duration<double, std::milli>(t_assemble_end - assembler_start_time).count();
            uint8_t frag_count = current_assembler->frag_count();

            std::vector<uint8_t> frame_data = current_assembler->assemble();
            current_assembler.reset();

            // Parse multi-region header (also handles V1 fallback)
            auto decoded = decode_multi_region(frame_data.data(), frame_data.size());
            if (!decoded) {
                if (g_udp_log) fprintf(g_udp_log, "FAIL,%u,%u,%.2f,,header_decode_fail\n", current_frame_id, frag_count, assembly_ms);
                continue;
            }

            // Push raw assembled frame to queue (decoding done in GetNextFrameCpu)
            AssembledFrame af;
            af.data = std::move(frame_data);
            af.frame_seq = decoded->header.frame_seq;
            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                while (frame_queue_.size() >= MAX_QUEUE_SIZE)
                    frame_queue_.pop();
                frame_queue_.push(std::move(af));
            }
            frame_cv_.notify_one();

            // Log using region 0 info (backward compat)
            auto& r0 = decoded->mr_header.regions[0];
            log_frame(current_frame_id, frag_count, assembly_ms, -1.0,
                      static_cast<uint32_t>(r0.width), static_cast<uint32_t>(r0.height),
                      decoded->header.capture_timestamp_ms, decoded->header.frame_seq);
        }
    }
}

cv::Mat UdpFrameReceiver::GetNextFrameCpu() {
    std::unique_lock<std::mutex> lock(frame_mutex_);
    frame_cv_.wait(lock, [this] { return !frame_queue_.empty() || should_stop_; });

    if (should_stop_ && frame_queue_.empty()) {
        return cv::Mat();
    }

    // Keep the latest frame, drop stale ones
    AssembledFrame af;
    while (!frame_queue_.empty()) {
        af = std::move(frame_queue_.front());
        frame_queue_.pop();
    }
    lock.unlock();

    // Decode multi-region frame
    auto decoded = decode_multi_region(af.data.data(), af.data.size());
    if (!decoded) return cv::Mat();

    // Cache decoded frame for callers that need other regions
    last_mr_frame_storage_ = std::move(af.data);
    last_mr_frame_ = *decoded;

    // Re-point region_data pointers into our storage
    for (uint32_t i = 0; i < last_mr_frame_.mr_header.num_regions; ++i) {
        auto& e = last_mr_frame_.mr_header.regions[i];
        last_mr_frame_.region_data[i] = last_mr_frame_storage_.data() + 4 + FRAME_HEADER_SIZE + e.jpeg_offset;
        last_mr_frame_.region_size[i] = e.jpeg_size;
    }

    // Return region 0 (main AI frame) — backward compatible
    if (last_mr_frame_.mr_header.num_regions == 0 || last_mr_frame_.region_size[0] == 0)
        return cv::Mat();

    std::vector<uint8_t> jpeg0(last_mr_frame_.region_data[0],
                                last_mr_frame_.region_data[0] + last_mr_frame_.region_size[0]);

    // Decode JPEG
    cv::Mat frame;
#ifdef USE_CUDA
    try {
        if (gpu_codec_ && gpu_codec_->isInitialized()) {
            frame = gpu_codec_->decode(jpeg0);
        } else {
            frame = cv::imdecode(jpeg0, cv::IMREAD_COLOR);
        }
    } catch (const std::exception& e) {
        std::cerr << "[UdpFrameReceiver] GPU decode failed: " << e.what() << ", falling back to CPU" << std::endl;
        frame = cv::imdecode(jpeg0, cv::IMREAD_COLOR);
    }
#else
    frame = cv::imdecode(jpeg0, cv::IMREAD_COLOR);
#endif

    // ── Ammo digit recognition (region 1) ──
    extern Config config;
    static int ammo_log_counter = 0;
    if (config.ammo_enabled) {
        if (!digit_classifier_init_attempted_) {
            InitDigitClassifier();
            digit_classifier_init_attempted_ = true;
        }
        if (!digit_classifier_ || !digit_classifier_->isLoaded()) {
            if (ammo_log_counter++ % 600 == 0)
                ALOG("[Ammo] Classifier not loaded or init failed");
        } else {
            ammo_classify_counter_++;
            if (ammo_classify_counter_ >= config.ammo_classify_every_n) {
                ammo_classify_counter_ = 0;
                if (last_mr_frame_.mr_header.num_regions <= 1) {
                    if (ammo_log_counter++ % 600 == 0)
                        ALOG("[Ammo] No region 1 in frame (num_regions=%u)",
                               last_mr_frame_.mr_header.num_regions);
                } else if (last_mr_frame_.region_size[1] == 0) {
                    if (ammo_log_counter++ % 600 == 0)
                        ALOG("[Ammo] Region 1 has zero size");
                } else {
                    std::vector<uint8_t> jpeg1(
                        last_mr_frame_.region_data[1],
                        last_mr_frame_.region_data[1] + last_mr_frame_.region_size[1]);
                    cv::Mat region1 = cv::imdecode(jpeg1, cv::IMREAD_COLOR);
                    if (region1.empty()) {
                        if (ammo_log_counter++ % 600 == 0)
                            ALOG("[Ammo] Region 1 JPEG decode failed (size=%u)",
                                   last_mr_frame_.region_size[1]);
                    } else {
                        int ammo = digit_classifier_->detect(region1,
                            config.ammo_tens_x, config.ammo_tens_y, config.ammo_tens_w, config.ammo_tens_h,
                            config.ammo_ones_x, config.ammo_ones_y, config.ammo_ones_w, config.ammo_ones_h,
                            config.ammo_blank_stddev_threshold);
                        latest_ammo_count_.store(ammo, std::memory_order_relaxed);
                        extern std::atomic<int> g_ammo_count;
                        extern std::atomic<uint64_t> g_ammo_capture_ts;
                        g_ammo_count.store(ammo, std::memory_order_relaxed);
                        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        g_ammo_capture_ts.store(now_ms, std::memory_order_relaxed);
                        if (ammo_log_counter++ % 300 == 0)
                            ALOG("[Ammo] Detected=%d (region1=%dx%d)", ammo, region1.cols, region1.rows);
                        static auto last_fps_log = std::chrono::steady_clock::now();
                        static int fps_count = 0;
                        fps_count++;
                        auto now = std::chrono::steady_clock::now();
                        if (now - last_fps_log > std::chrono::seconds(5)) {
                            ALOG("[Ammo] classify FPS: %.1f", fps_count / 5.0f);
                            fps_count = 0;
                            last_fps_log = now;
                        }
                    }
                }
            }
        }
    }

    return frame;
}

void UdpFrameReceiver::InitDigitClassifier() {
    try {
        digit_classifier_ = std::make_unique<DigitClassifier>(L"models/digit_classifier.onnx");
    } catch (const std::exception& e) {
        fprintf(stderr, "[UdpFrameReceiver] Digit classifier init failed: %s\n", e.what());
    }
}
