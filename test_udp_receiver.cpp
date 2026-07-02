#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <queue>
#include <winsock2.h>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include "RN_AI_cpp/capture/udp_wire_protocol.h"

class SimpleUdpReceiver {
private:
    SOCKET sock_;
    int port_;
    std::map<uint32_t, std::unique_ptr<FragmentAssembler>> assemblers_;
    std::mutex mutex_;
    std::queue<cv::Mat> frame_queue_;
    bool should_stop_;
    
public:
    SimpleUdpReceiver(int port) : port_(port), sock_(INVALID_SOCKET), should_stop_(false) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[Receiver] WSAStartup failed" << std::endl;
            return;
        }
        
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
            std::cerr << "[Receiver] Socket creation failed: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return;
        }

        // Set receive timeout so recvfrom unblocks periodically to check should_stop_
        DWORD timeout_ms = 500;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        
        sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;
        local_addr.sin_port = htons(port_);
        
        if (bind(sock_, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
            std::cerr << "[Receiver] Bind failed: " << WSAGetLastError() << std::endl;
            closesocket(sock_);
            WSACleanup();
            return;
        }
        
        std::cout << "[Receiver] Listening on port " << port_ << std::endl;
    }
    
    ~SimpleUdpReceiver() {
        should_stop_ = true;
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
        }
        WSACleanup();
    }
    
    void receiveLoop() {
        std::vector<uint8_t> buffer(MAX_DATAGRAM_SIZE);
        int fragment_count = 0;
        int frame_count = 0;
        double total_latency_ms = 0.0;
        double min_latency_ms = 999999.0;
        double max_latency_ms = 0.0;

        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (!should_stop_) {
            sockaddr_in from_addr;
            int from_len = sizeof(from_addr);
            
            int bytes = recvfrom(sock_, (char*)buffer.data(), buffer.size(), 0,
                                (sockaddr*)&from_addr, &from_len);
            
            if (bytes == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
                    continue;
                }
                std::cerr << "[Receiver] Recv error: " << err << std::endl;
                break;
            }
            
            if (bytes <= 0) continue;
            
            fragment_count++;
            
            // 解析分片
            auto frag_info = FragmentEncoder::decode_datagram(buffer.data(), bytes);
            if (!frag_info) {
                std::cerr << "[Receiver] Failed to parse fragment" << std::endl;
                continue;
            }
            
            // 获取或创建 assembler
            std::lock_guard<std::mutex> lock(mutex_);
            auto& assembler = assemblers_[frag_info->frame_id];
            if (!assembler) {
                assembler = std::make_unique<FragmentAssembler>(frag_info->frame_id, frag_info->frag_count);
            }
            
            // 添加分片
            if (!assembler->add_fragment(frag_info->frag_idx, frag_info->data, frag_info->data_size)) {
                std::cerr << "[Receiver] Failed to add fragment" << std::endl;
                continue;
            }
            
            // 检查是否完整
            if (assembler->is_complete()) {
                std::vector<uint8_t> frame_data = assembler->assemble();
                assemblers_.erase(frag_info->frame_id);
                
                // 解码长度前缀
                auto decoded = decode_length_prefixed(frame_data.data(), frame_data.size());
                if (!decoded) {
                    std::cerr << "[Receiver] Failed to decode length prefix" << std::endl;
                    continue;
                }
                
                // CPU 解码 JPEG
                std::vector<uint8_t> jpeg_data(decoded->jpeg_data, 
                                               decoded->jpeg_data + decoded->jpeg_size);
                cv::Mat frame = cv::imdecode(jpeg_data, cv::IMREAD_COLOR);
                
                if (frame.empty()) {
                    std::cerr << "[Receiver] Failed to decode JPEG" << std::endl;
                    continue;
                }
                
                frame_count++;

                // 端到端延迟
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                double latency = static_cast<double>(now_ms - decoded->header.capture_timestamp_ms);
                total_latency_ms += latency;
                if (latency < min_latency_ms) min_latency_ms = latency;
                if (latency > max_latency_ms) max_latency_ms = latency;

                if (frame_count % 100 == 0) {
                    std::cout << "[Receiver] frame=" << frame_count << " latency=" << latency << "ms" << std::endl;
                }
                
                frame_queue_.push(frame.clone());
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "\n=== Receive Summary ===" << std::endl;
        std::cout << "Total fragments: " << fragment_count << std::endl;
        std::cout << "Total frames: " << frame_count << std::endl;
        std::cout << "Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "FPS: " << (frame_count * 1000.0 / duration.count()) << std::endl;
        if (frame_count > 0) {
            std::cout << "Latency (min/avg/max): " << min_latency_ms << "/"
                      << (total_latency_ms / frame_count) << "/" << max_latency_ms << " ms" << std::endl;
        }
    }
    
    cv::Mat getNextFrame() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frame_queue_.empty()) {
            return cv::Mat();
        }
        cv::Mat frame = frame_queue_.front();
        frame_queue_.pop();
        return frame;
    }
    
    void stop() { should_stop_ = true; }
};

int main() {
    std::cout << "=== UDP Frame Receiver Test ===" << std::endl;
    
    SimpleUdpReceiver receiver(5000);
    
    // 在后台线程接收
    std::thread receive_thread([&receiver]() {
        receiver.receiveLoop();
    });
    
    // 主线程等待并显示帧
    std::cout << "Waiting for frames... (Press ESC to exit)" << std::endl;
    
    while (true) {
        cv::Mat frame = receiver.getNextFrame();
        if (!frame.empty()) {
            cv::imshow("Received Frame", frame);
        }
        
        int key = cv::waitKey(30);
        if (key == 27) { // ESC
            break;
        }
    }
    
    receiver.stop();
    receive_thread.join();
    
    std::cout << "Test completed." << std::endl;
    return 0;
}
