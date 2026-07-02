#include "udp_frame_receiver.h"
#include <iostream>

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
    
    // Initialize GPU codec
    gpu_codec_ = std::make_unique<GpuJpegCodec>();
    if (!gpu_codec_->initialize()) {
        std::cerr << "[UdpFrameReceiver] Failed to initialize GPU JPEG codec" << std::endl;
    }
    
    // Start receive thread
    should_stop_ = false;
    receive_thread_ = std::thread(&UdpFrameReceiver::ReceiveThread, this);
    
    std::cout << "[UdpFrameReceiver] Initialized on port " << port_ << std::endl;
}

UdpFrameReceiver::~UdpFrameReceiver() {
    should_stop_ = true;
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    if (gpu_codec_) {
        gpu_codec_->shutdown();
    }
    
    WSACleanup();
}

void UdpFrameReceiver::ReceiveThread() {
    std::vector<uint8_t> buffer(MAX_DATAGRAM_SIZE);
    std::unique_ptr<FragmentAssembler> current_assembler;
    std::chrono::steady_clock::time_point assembler_start_time;
    
    while (!should_stop_) {
        sockaddr_in from_addr;
        int from_len = sizeof(from_addr);
        
        int bytes_received = recvfrom(socket_, (char*)buffer.data(), buffer.size(), 0,
                                      (sockaddr*)&from_addr, &from_len);
        
        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            } else {
                std::cerr << "[UdpFrameReceiver] Receive error: " << error << std::endl;
                break;
            }
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
            current_assembler = std::make_unique<FragmentAssembler>(frag_info->frame_id, frag_info->frag_count);
            assembler_start_time = std::chrono::steady_clock::now();
        }
        
        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - assembler_start_time).count();
        if (elapsed > FRAGMENT_TIMEOUT_MS) {
            current_assembler.reset();
            continue;
        }
        
        // Add fragment to assembler
        if (!current_assembler->add_fragment(frag_info->frag_idx, frag_info->data, frag_info->data_size)) {
            continue;
        }
        
        // Check if frame is complete
        if (current_assembler->is_complete()) {
            std::vector<uint8_t> frame_data = current_assembler->assemble();
            current_assembler.reset();
            
            // Decode length-prefixed frame
            auto decoded = decode_length_prefixed(frame_data.data(), frame_data.size());
            if (!decoded) {
                continue;
            }
            
            // Decode JPEG
            std::vector<uint8_t> jpeg_data(decoded->jpeg_data, decoded->jpeg_data + decoded->jpeg_size);
            cv::Mat frame;
            
            if (gpu_codec_ && gpu_codec_->isInitialized()) {
                frame = gpu_codec_->decode(jpeg_data);
            } else {
                frame = cv::imdecode(jpeg_data, cv::IMREAD_COLOR);
            }
            
            if (frame.empty()) {
                continue;
            }
            
            // Add to frame queue
            std::lock_guard<std::mutex> lock(frame_mutex_);
            while (frame_queue_.size() >= MAX_QUEUE_SIZE) {
                frame_queue_.pop();
            }
            frame_queue_.push(frame.clone());
            frame_cv_.notify_one();
        }
    }
}

cv::Mat UdpFrameReceiver::GetNextFrameCpu() {
    std::unique_lock<std::mutex> lock(frame_mutex_);
    frame_cv_.wait(lock, [this] { return !frame_queue_.empty() || should_stop_; });
    
    if (should_stop_ && frame_queue_.empty()) {
        return cv::Mat();
    }
    
    cv::Mat frame = frame_queue_.front();
    frame_queue_.pop();
    return frame;
}
