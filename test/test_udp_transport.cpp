/**
 * UDP 帧传输测试程序
 * 测试本地回环环境下的 UDP 帧发送和接收
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

// 日志级别
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

// 全局日志级别
LogLevel g_log_level = LOG_INFO;

// 日志输出
void log_message(LogLevel level, const std::string& component, const std::string& message) {
    if (level < g_log_level) return;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    const char* level_str = "INFO";
    switch (level) {
        case LOG_DEBUG: level_str = "DEBUG"; break;
        case LOG_INFO: level_str = "INFO"; break;
        case LOG_WARN: level_str = "WARN"; break;
        case LOG_ERROR: level_str = "ERROR"; break;
    }
    
    std::cout << "[" << ss.str() << "] [" << level_str << "] [" << component << "] " << message << std::endl;
}

#define LOG_DEBUG(comp, msg) log_message(LOG_DEBUG, comp, msg)
#define LOG_INFO(comp, msg) log_message(LOG_INFO, comp, msg)
#define LOG_WARN(comp, msg) log_message(LOG_WARN, comp, msg)
#define LOG_ERROR(comp, msg) log_message(LOG_ERROR, comp, msg)

// 简化的帧头结构（22字节）
struct FrameHeader {
    uint8_t version;
    uint32_t width;
    uint32_t height;
    uint64_t frame_seq;
    uint8_t format;
    uint32_t jpeg_size;
    
    static const size_t SIZE = 22;
    
    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> data(SIZE);
        data[0] = version;
        
        // width (u32 LE)
        data[1] = width & 0xFF;
        data[2] = (width >> 8) & 0xFF;
        data[3] = (width >> 16) & 0xFF;
        data[4] = (width >> 24) & 0xFF;
        
        // height (u32 LE)
        data[5] = height & 0xFF;
        data[6] = (height >> 8) & 0xFF;
        data[7] = (height >> 16) & 0xFF;
        data[8] = (height >> 24) & 0xFF;
        
        // frame_seq (u64 LE)
        for (int i = 0; i < 8; i++) {
            data[9 + i] = (frame_seq >> (i * 8)) & 0xFF;
        }
        
        data[17] = format;
        
        // jpeg_size (u32 LE)
        data[18] = jpeg_size & 0xFF;
        data[19] = (jpeg_size >> 8) & 0xFF;
        data[20] = (jpeg_size >> 16) & 0xFF;
        data[21] = (jpeg_size >> 24) & 0xFF;
        
        return data;
    }
    
    static bool decode(const std::vector<uint8_t>& data, FrameHeader& header) {
        if (data.size() < SIZE) return false;
        
        header.version = data[0];
        
        header.width = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
        header.height = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
        
        header.frame_seq = 0;
        for (int i = 0; i < 8; i++) {
            header.frame_seq |= ((uint64_t)data[9 + i]) << (i * 8);
        }
        
        header.format = data[17];
        header.jpeg_size = data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24);
        
        return true;
    }
};

// 长度前缀帧编码
std::vector<uint8_t> encode_length_prefixed(const FrameHeader& header, const std::vector<uint8_t>& jpeg_data) {
    auto header_data = header.encode();
    uint32_t payload_len = header_data.size() + jpeg_data.size();
    
    std::vector<uint8_t> frame;
    frame.reserve(4 + payload_len);
    
    // payload_len (u32 LE)
    frame.push_back(payload_len & 0xFF);
    frame.push_back((payload_len >> 8) & 0xFF);
    frame.push_back((payload_len >> 16) & 0xFF);
    frame.push_back((payload_len >> 24) & 0xFF);
    
    // header
    frame.insert(frame.end(), header_data.begin(), header_data.end());
    
    // jpeg data
    frame.insert(frame.end(), jpeg_data.begin(), jpeg_data.end());
    
    return frame;
}

// 分片头结构（10字节）
struct FragmentHeader {
    static const size_t SIZE = 10;
    static const uint32_t MAGIC = 0x4654575