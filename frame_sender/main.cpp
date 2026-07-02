#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

#include <opencv2/opencv.hpp>

#include "../RN_AI_cpp/capture/duplication_api_capture.h"
#include "../RN_AI_cpp/capture/udp_wire_protocol.h"
#include "../codec/gpu_jpeg_codec.h"

static std::atomic<bool> g_running{true};
std::atomic<bool> capture_method_changed{false};

static void signalHandler(int) {
    g_running.store(false);
}

struct Stats {
    uint64_t frames_sent = 0;
    double total_encode_ms = 0.0;
    double total_send_ms = 0.0;
};

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    std::string host = "127.0.0.1";
    int port = 12345;
    int monitor = 0;
    int crop_size = 640;
    int jpeg_quality = 80;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--monitor" && i + 1 < argc) {
            monitor = std::atoi(argv[++i]);
        } else if (arg == "--crop-size" && i + 1 < argc) {
            crop_size = std::atoi(argv[++i]);
        } else if (arg == "--jpeg-quality" && i + 1 < argc) {
            jpeg_quality = std::atoi(argv[++i]);
        }
    }

    printf("[frame_sender] host=%s port=%d monitor=%d crop_size=%d jpeg_quality=%d\n",
           host.c_str(), port, monitor, crop_size, jpeg_quality);

    // Register signal handler
    std::signal(SIGINT, signalHandler);

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "[frame_sender] WSAStartup failed\n");
        return 1;
    }

    // Create UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "[frame_sender] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Resolve destination address
    struct sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr) != 1) {
        fprintf(stderr, "[frame_sender] invalid host: %s\n", host.c_str());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Initialize GPUJPEG codec
    GpuJpegCodec codec;
    if (!codec.initialize()) {
        fprintf(stderr, "[frame_sender] GpuJpegCodec::initialize() failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Create DXGI screen capture
    auto capturer = std::make_unique<DuplicationAPIScreenCapture>(crop_size, crop_size, monitor);

    Stats stats;
    uint64_t frame_seq = 0;
    uint16_t frame_id = 0;
    auto fps_start = std::chrono::high_resolution_clock::now();
    uint64_t fps_counter = 0;

    printf("[frame_sender] starting main loop (Ctrl+C to stop)...\n");

    while (g_running.load()) {
        // Capture frame
        cv::Mat frame = capturer->GetNextFrameCpu();
        if (frame.empty()) {
            continue;
        }

        // Center crop to square
        cv::Mat cropped;
        int fw = frame.cols;
        int fh = frame.rows;
        if (fw >= crop_size && fh >= crop_size) {
            int x = (fw - crop_size) / 2;
            int y = (fh - crop_size) / 2;
            cropped = frame(cv::Rect(x, y, crop_size, crop_size)).clone();
        } else {
            // Frame smaller than crop_size, use as-is
            cropped = frame;
        }

        int out_w = cropped.cols;
        int out_h = cropped.rows;

        // Encode to JPEG
        auto t_enc_start = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> jpeg_data = codec.encode(cropped, jpeg_quality);
        auto t_enc_end = std::chrono::high_resolution_clock::now();
        double enc_ms = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();
        stats.total_encode_ms += enc_ms;

        if (jpeg_data.empty()) {
            fprintf(stderr, "[frame_sender] JPEG encode failed for frame %llu\n",
                    static_cast<unsigned long long>(frame_seq));
            ++frame_seq;
            ++frame_id;
            continue;
        }

        // Build frame header
        FrameHeader header;
        header.width = static_cast<uint32_t>(out_w);
        header.height = static_cast<uint32_t>(out_h);
        header.frame_seq = frame_seq;
        header.capture_timestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        header.format = FORMAT_JPEG;
        header.jpeg_size = static_cast<uint32_t>(jpeg_data.size());

        // Encode as length-prefixed frame
        std::vector<uint8_t> frame_buf;
        encode_length_prefixed(header, jpeg_data.data(), jpeg_data.size(), frame_buf);

        // Fragment into datagrams
        std::vector<std::vector<uint8_t>> datagrams;
        FragmentEncoder::encode(frame_id, frame_buf.data(), frame_buf.size(), datagrams);

        // Send each fragment
        auto t_send_start = std::chrono::high_resolution_clock::now();
        for (const auto& dg : datagrams) {
            int ret = sendto(sock, reinterpret_cast<const char*>(dg.data()),
                             static_cast<int>(dg.size()), 0,
                             reinterpret_cast<const sockaddr*>(&destAddr),
                             static_cast<int>(sizeof(destAddr)));
            if (ret < 0) {
                fprintf(stderr, "[frame_sender] sendto failed: %d\n", WSAGetLastError());
            }
        }
        auto t_send_end = std::chrono::high_resolution_clock::now();
        double send_ms = std::chrono::duration<double, std::milli>(t_send_end - t_send_start).count();
        stats.total_send_ms += send_ms;

        ++stats.frames_sent;
        ++frame_seq;
        ++frame_id;
        ++fps_counter;

        // Print FPS every 100 frames
        if (fps_counter % 100 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - fps_start).count();
            double fps = 100.0 / elapsed;
            printf("[frame_sender] frame=%llu fps=%.1f enc=%.2fms send=%.2fms\n",
                   static_cast<unsigned long long>(stats.frames_sent),
                   fps, enc_ms, send_ms);
            fps_start = now;
        }

        // 固定 10ms 间隔 = 100 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Print final statistics
    printf("\n[frame_sender] shutting down...\n");
    printf("[frame_sender] total frames sent: %llu\n",
           static_cast<unsigned long long>(stats.frames_sent));
    if (stats.frames_sent > 0) {
        double avg_enc = stats.total_encode_ms / static_cast<double>(stats.frames_sent);
        double avg_send = stats.total_send_ms / static_cast<double>(stats.frames_sent);
        printf("[frame_sender] avg encode: %.2f ms\n", avg_enc);
        printf("[frame_sender] avg send:   %.2f ms\n", avg_send);
    }

    // Cleanup
    closesocket(sock);
    WSACleanup();

    return 0;
}
