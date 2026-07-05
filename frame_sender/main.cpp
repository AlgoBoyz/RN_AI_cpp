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
#include <sstream>

#include <opencv2/opencv.hpp>

#include "../RN_AI_cpp/capture/duplication_api_capture.h"
#include "../RN_AI_cpp/capture/udp_wire_protocol.h"
#include "../codec/gpu_jpeg_codec.h"

static std::atomic<bool> g_running{true};
std::atomic<bool> capture_method_changed{false};

static void signalHandler(int) { g_running.store(false); }

struct Stats {
    uint64_t frames_sent = 0;
    double total_encode_ms = 0.0;
    double total_send_ms = 0.0;
};

// ── Crop region descriptor ──────────────────────────────────────────────
struct CropRegion {
    uint8_t id;
    int x, y, w, h;      // source coordinates on full frame; -1 = centered
};

static std::vector<CropRegion> parse_crop_regions(int argc, char* argv[]) {
    std::vector<CropRegion> regions;
    // Default: single centered 640x640 region (backward compatible)
    regions.push_back({0, -1, -1, 640, 640});

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--crop-region" && i + 1 < argc) {
            std::string val = argv[++i];
            // Parse "id,x,y,w,h"  or  "id,center,center,w,h"
            int id, x, y, w, h;
            char cx_buf[32], cy_buf[32];
            if (sscanf_s(val.c_str(), "%d,%d,%d,%d,%d", &id, &x, &y, &w, &h) == 5) {
                // numeric coordinates
            } else if (sscanf_s(val.c_str(), "%d,%31[^,],%31[^,],%d,%d", &id, cx_buf, (unsigned)sizeof(cx_buf), cy_buf, (unsigned)sizeof(cy_buf), &w, &h) == 5) {
                x = (_stricmp(cx_buf, "center") == 0) ? -1 : std::atoi(cx_buf);
                y = (_stricmp(cy_buf, "center") == 0) ? -1 : std::atoi(cy_buf);
            } else {
                fprintf(stderr, "[frame_sender] invalid --crop-region format: %s\n", val.c_str());
                continue;
            }
            // If this is the first --crop-region on cmd line, replace default
            if (i == 1 || (i > 1 && std::string(argv[i-1]) == "--crop-region" && regions.size() == 1 && regions[0].id == 0 && regions[0].x == -1)) {
                regions.clear();
            }
            // Remove duplicate ID: replace or append
            bool found = false;
            for (auto& r : regions) {
                if (r.id == id) { r = {static_cast<uint8_t>(id), x, y, w, h}; found = true; break; }
            }
            if (!found) regions.push_back({static_cast<uint8_t>(id), x, y, w, h});
        }
        // Keep old --crop-size for backward compat (maps to region 0)
        if (arg == "--crop-size" && i + 1 < argc) {
            int s = std::atoi(argv[++i]);
            for (auto& r : regions) {
                if (r.id == 0) { r.w = s; r.h = s; break; }
            }
        }
    }
    return regions;
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    std::string host = "192.168.137.2";
    int port = 12345;
    int monitor = 0;
    int jpeg_quality = 80;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--monitor" && i + 1 < argc) monitor = std::atoi(argv[++i]);
        else if (arg == "--jpeg-quality" && i + 1 < argc) jpeg_quality = std::atoi(argv[++i]);
    }

    auto regions = parse_crop_regions(argc, argv);
    printf("[frame_sender] host=%s port=%d monitor=%d jpeg_quality=%d regions=%zu\n",
           host.c_str(), port, monitor, jpeg_quality, regions.size());
    for (auto& r : regions)
        printf("  region %u: (%d,%d) %dx%d\n", r.id, r.x, r.y, r.w, r.h);

    std::signal(SIGINT, signalHandler);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { fprintf(stderr, "[frame_sender] WSAStartup failed\n"); return 1; }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { fprintf(stderr, "[frame_sender] socket() failed: %d\n", WSAGetLastError()); WSACleanup(); return 1; }

    struct sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr) != 1) {
        fprintf(stderr, "[frame_sender] invalid host: %s\n", host.c_str());
        closesocket(sock); WSACleanup(); return 1;
    }

    GpuJpegCodec codec;
    if (!codec.initialize()) { fprintf(stderr, "[frame_sender] GpuJpegCodec::initialize() failed\n"); closesocket(sock); WSACleanup(); return 1; }

    // ── Query full monitor resolution ──
    int full_w = GetSystemMetrics(SM_CXSCREEN);
    int full_h = GetSystemMetrics(SM_CYSCREEN);
    printf("[frame_sender] full screen: %dx%d\n", full_w, full_h);

    // Create DXGI screen capture at full resolution
    auto capturer = std::make_unique<DuplicationAPIScreenCapture>(full_w, full_h, monitor);

    Stats stats;
    uint64_t frame_seq = 0;
    uint16_t frame_id = 0;
    auto fps_start = std::chrono::high_resolution_clock::now();
    uint64_t fps_counter = 0;

    printf("[frame_sender] starting main loop (Ctrl+C to stop)...\n");

    while (g_running.load()) {
        cv::Mat full_frame = capturer->GetNextFrameCpu();
        if (full_frame.empty()) continue;

        int fw = full_frame.cols, fh = full_frame.rows;

        // ── Crop each region and encode as JPEG ──
        auto t_enc_start = std::chrono::high_resolution_clock::now();

        std::vector<std::vector<uint8_t>> region_jpegs(regions.size());
        MultiRegionHeader mr_header;
        mr_header.num_regions = static_cast<uint32_t>(regions.size());

        for (size_t i = 0; i < regions.size(); ++i) {
            auto& cr = regions[i];

            // Resolve centered coordinates
            int rx = cr.x, ry = cr.y;
            if (rx < 0) rx = (fw - cr.w) / 2;
            if (ry < 0) ry = (fh - cr.h) / 2;
            rx = std::max(0, std::min(rx, fw - 1));
            ry = std::max(0, std::min(ry, fh - 1));
            int rw = std::min(cr.w, fw - rx);
            int rh = std::min(cr.h, fh - ry);

            cv::Mat cropped = full_frame(cv::Rect(rx, ry, rw, rh)).clone();
            region_jpegs[i] = codec.encode(cropped, jpeg_quality);

            auto& entry = mr_header.regions[i];
            entry.id = cr.id;
            entry.src_x = rx;
            entry.src_y = ry;
            entry.width = rw;
            entry.height = rh;
            entry.jpeg_offset = 0;  // filled by encode_multi_region
            entry.jpeg_size = static_cast<uint32_t>(region_jpegs[i].size());

            if (region_jpegs[i].empty()) {
                fprintf(stderr, "[frame_sender] JPEG encode failed for region %u\n", cr.id);
            }
        }

        auto t_enc_end = std::chrono::high_resolution_clock::now();
        double enc_ms = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();
        stats.total_encode_ms += enc_ms;

        // ── Build multi-region frame ──
        FrameHeader header;
        header.version = PROTOCOL_VERSION_V2;
        header.width = static_cast<uint32_t>(fw);
        header.height = static_cast<uint32_t>(fh);
        header.frame_seq = frame_seq;
        header.capture_timestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        header.format = FORMAT_MULTI_REGION;  // V2

        std::vector<uint8_t> frame_buf;
        encode_multi_region(header, mr_header, region_jpegs, frame_buf);

        // ── Fragment and send ──
        std::vector<std::vector<uint8_t>> datagrams;
        FragmentEncoder::encode(frame_id, frame_buf.data(), frame_buf.size(), datagrams);

        auto t_send_start = std::chrono::high_resolution_clock::now();
        for (const auto& dg : datagrams) {
            sendto(sock, reinterpret_cast<const char*>(dg.data()),
                   static_cast<int>(dg.size()), 0,
                   reinterpret_cast<const sockaddr*>(&destAddr),
                   static_cast<int>(sizeof(destAddr)));
        }
        auto t_send_end = std::chrono::high_resolution_clock::now();
        double send_ms = std::chrono::duration<double, std::milli>(t_send_end - t_send_start).count();
        stats.total_send_ms += send_ms;

        ++stats.frames_sent;
        ++frame_seq;
        ++frame_id;
        ++fps_counter;

        if (fps_counter % 100 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - fps_start).count();
            double fps = 100.0 / elapsed;
            printf("[frame_sender] frame=%llu fps=%.1f enc=%.2fms send=%.2fms regions=%zu\n",
                   static_cast<unsigned long long>(stats.frames_sent),
                   fps, enc_ms, send_ms, regions.size());
            fps_start = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("\n[frame_sender] shutting down...\n");
    printf("[frame_sender] total frames sent: %llu\n", static_cast<unsigned long long>(stats.frames_sent));
    if (stats.frames_sent > 0) {
        printf("[frame_sender] avg encode: %.2f ms\n", stats.total_encode_ms / static_cast<double>(stats.frames_sent));
        printf("[frame_sender] avg send:   %.2f ms\n", stats.total_send_ms / static_cast<double>(stats.frames_sent));
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
