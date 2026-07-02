#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <winsock2.h>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include "RN_AI_cpp/capture/udp_wire_protocol.h"

// 模拟发送端：创建测试帧并发送
int main() {
    std::cout << "=== UDP Frame Sender Test ===" << std::endl;
    
    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
    
    // 创建 UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }
    
    // 目标地址（本地回环）
    sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_port = htons(5000);
    
    // 创建测试图像（640x480 BGR）
    cv::Mat test_frame(480, 640, CV_8UC3, cv::Scalar(100, 150, 200));
    cv::putText(test_frame, "UDP Test Frame", cv::Point(150, 240), 
                cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(255, 255, 255), 2);
    
    std::cout << "Created test frame: " << test_frame.cols << "x" << test_frame.rows << std::endl;
    
    // 编码为 JPEG
    std::vector<uchar> jpeg_buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
    cv::imencode(".jpg", test_frame, jpeg_buffer, params);
    
    std::cout << "JPEG encoded size: " << jpeg_buffer.size() << " bytes" << std::endl;

    // 使用协议库的 encode_length_prefixed 格式：[4B payload_len][22B FrameHeader][JPEG]
    FrameHeader frame_hdr;
    frame_hdr.version = PROTOCOL_VERSION;
    frame_hdr.width = test_frame.cols;
    frame_hdr.height = test_frame.rows;
    frame_hdr.frame_seq = 1;
    frame_hdr.capture_timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    frame_hdr.format = FORMAT_JPEG;
    frame_hdr.jpeg_size = static_cast<uint32_t>(jpeg_buffer.size());

    std::vector<uint8_t> frame_data;
    encode_length_prefixed(frame_hdr, jpeg_buffer.data(), jpeg_buffer.size(), frame_data);

    std::cout << "Total frame size (with header): " << frame_data.size() << " bytes" << std::endl;
    
    // 分片并发送
    uint32_t frame_id = 1;
    std::vector<std::vector<uint8_t>> fragments;
    FragmentEncoder::encode(static_cast<uint16_t>(frame_id), frame_data.data(), frame_data.size(), fragments);
    
    std::cout << "Split into " << fragments.size() << " fragments" << std::endl;
    
    int total_sent = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < fragments.size(); i++) {
        int sent = sendto(sock, (const char*)fragments[i].data(), fragments[i].size(), 0,
                         (sockaddr*)&dest_addr, sizeof(dest_addr));
        if (sent == SOCKET_ERROR) {
            std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
            break;
        }
        total_sent += sent;
        std::cout << "Sent fragment " << (i + 1) << "/" << fragments.size() 
                  << " (" << sent << " bytes)" << std::endl;
        
        // 稍微延迟，避免丢包
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "\n=== Send Summary ===" << std::endl;
    std::cout << "Total sent: " << total_sent << " bytes" << std::endl;
    std::cout << "Fragments: " << fragments.size() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << (total_sent * 1000.0 / duration.count() / 1024 / 1024) << " MB/s" << std::endl;
    
    closesocket(sock);
    WSACleanup();
    
    std::cout << "\nTest completed. Check receiver logs." << std::endl;
    return 0;
}
