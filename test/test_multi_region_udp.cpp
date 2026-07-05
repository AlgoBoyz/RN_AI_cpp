/**
 * test_multi_region_udp.cpp — 多区域 UDP 协议可视化测试
 *
 * 功能:
 *   1. 生成合成图像: 区域0 (640x640 彩色棋盘格) + 区域1 (弹药数字, 如 "25")
 *   2. 编码为 V2 多区域帧格式, 通过本地 UDP 回环发送
 *   3. 接收并解码多区域帧
 *   4. 在区域1图像上用白底黑字绘制识别到的数字
 *   5. 将区域0 和标注后的区域1 保存到 debug 目录
 *
 * 编译:
 *   cl /std:c++17 /EHsc /I. /Ivendor\opencv\install\include
 *       test\test_multi_region_udp.cpp
 *       RN_AI_cpp\capture\udp_wire_protocol.cpp
 *       ws2_32.lib vendor\opencv\install\x64\vc17\lib\opencv_world4100.lib
 *       /Fe:test_multi_region_udp.exe
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/opencv.hpp>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

#include "RN_AI_cpp/capture/udp_wire_protocol.h"

// ── Helpers ─────────────────────────────────────────────────────────────

static void log(const char* msg) {
    printf("[test] %s\n", msg);
}

// Create a colourful checkerboard image for region 0 (AI source).
static cv::Mat create_region0(int w, int h) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(30, 30, 30));
    int tile = 32;
    for (int y = 0; y < h; y += tile) {
        for (int x = 0; x < w; x += tile) {
            bool even = ((x / tile) + (y / tile)) % 2 == 0;
            cv::Scalar col = even ? cv::Scalar(60, 70, 200) : cv::Scalar(40, 180, 90);
            cv::rectangle(img, cv::Rect(x, y, tile, tile), col, cv::FILLED);
        }
    }
    // Draw a crosshair at center
    cv::circle(img, cv::Point(w / 2, h / 2), 20, cv::Scalar(0, 0, 255), 2);
    cv::line(img, cv::Point(w / 2 - 30, h / 2), cv::Point(w / 2 + 30, h / 2),
             cv::Scalar(0, 0, 255), 1);
    cv::line(img, cv::Point(w / 2, h / 2 - 30), cv::Point(w / 2, h / 2 + 30),
             cv::Scalar(0, 0, 255), 1);
    return img;
}

// Create a synthetic ammo counter image.
static cv::Mat create_region1_ammo(int number, int w, int h) {
    // White background
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(255, 255, 255));
    // Draw the number in black
    std::string text = std::to_string(number);
    int font = cv::FONT_HERSHEY_SIMPLEX;
    double scale = 1.0;
    int thickness = 2;
    int baseline = 0;
    cv::Size txt_size = cv::getTextSize(text, font, scale, thickness, &baseline);
    cv::Point org((w - txt_size.width) / 2, (h + txt_size.height) / 2);
    cv::putText(img, text, org, font, scale, cv::Scalar(0, 0, 0), thickness);
    // Add a thin border
    cv::rectangle(img, cv::Rect(0, 0, w - 1, h - 1), cv::Scalar(180, 180, 180), 1);
    return img;
}

// Simple template matching: detect digits in the ammo region.
// Returns the recognized number, or -1 on failure.
static int detect_ammo_number(const cv::Mat& ammo_region,
                               const std::vector<cv::Mat>& digit_templates) {
    if (ammo_region.empty() || digit_templates.empty()) return -1;

    cv::Mat gray;
    if (ammo_region.channels() == 3)
        cv::cvtColor(ammo_region, gray, cv::COLOR_BGR2GRAY);
    else
        gray = ammo_region.clone();

    // Threshold to binary (white background, black digits)
    cv::Mat binary;
    cv::threshold(gray, binary, 200, 255, cv::THRESH_BINARY_INV);

    // Find contours to isolate digits
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Filter and sort contours left-to-right
    std::vector<cv::Rect> digit_rects;
    for (const auto& c : contours) {
        cv::Rect r = cv::boundingRect(c);
        if (r.width > 4 && r.height > 8 && r.width < gray.cols * 0.6) {
            digit_rects.push_back(r);
        }
    }
    std::sort(digit_rects.begin(), digit_rects.end(),
              [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });

    if (digit_rects.empty()) return -1;

    // Match each digit against templates
    int result = 0;
    for (const auto& rect : digit_rects) {
        cv::Mat digit_roi = gray(rect);
        cv::resize(digit_roi, digit_roi, digit_templates[0].size());

        double best_val = -1.0;
        int best_digit = -1;
        for (int d = 0; d < 10; ++d) {
            cv::Mat match_result;
            cv::matchTemplate(digit_roi, digit_templates[d], match_result, cv::TM_CCOEFF_NORMED);
            double minV, maxV;
            cv::Point minP, maxP;
            cv::minMaxLoc(match_result, &minV, &maxV, &minP, &maxP);
            if (maxV > best_val) {
                best_val = maxV;
                best_digit = d;
            }
        }
        if (best_digit >= 0) {
            result = result * 10 + best_digit;
        }
    }
    return result;
}

// Generate synthetic digit templates (0-9) for the test.
static std::vector<cv::Mat> generate_digit_templates(int size) {
    std::vector<cv::Mat> templates(10);
    for (int d = 0; d < 10; ++d) {
        cv::Mat img(size, size / 2, CV_8UC1, cv::Scalar(255));
        std::string text = std::to_string(d);
        int font = cv::FONT_HERSHEY_SIMPLEX;
        double scale = 0.7;
        int thickness = 2;
        int baseline = 0;
        cv::Size txt_size = cv::getTextSize(text, font, scale, thickness, &baseline);
        cv::Point org((img.cols - txt_size.width) / 2, (img.rows + txt_size.height) / 2);
        cv::putText(img, text, org, font, scale, cv::Scalar(0), thickness);
        templates[d] = img.clone();
    }
    return templates;
}

// Save annotated images to debug folder
static bool save_debug_images(const cv::Mat& region0, const cv::Mat& annotated_ammo,
                               const std::string& suffix) {
    const char* user_profile = getenv("USERPROFILE");
    if (!user_profile) {
        log("WARN: %USERPROFILE% not set, cannot save debug images");
        return false;
    }

    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\rn_ai\\debug", user_profile);
    CreateDirectoryA(dir, nullptr);

    char path_r0[MAX_PATH];
    sprintf_s(path_r0, "%s\\region0_%s.png", dir, suffix.c_str());
    cv::imwrite(path_r0, region0);
    printf("[test] Saved: %s\n", path_r0);

    char path_r1[MAX_PATH];
    sprintf_s(path_r1, "%s\\region1_ammo_%s.png", dir, suffix.c_str());
    cv::imwrite(path_r1, annotated_ammo);
    printf("[test] Saved: %s\n", path_r1);

    return true;
}

// ── Main ────────────────────────────────────────────────────────────────

int main() {
    log("=== Multi-Region UDP Protocol Visual Test ===");
    printf("[test] OpenCV: %s\n", CV_VERSION);

    const int PORT = 0;      // 0 = OS-assigned (auto)
    const int TIMEOUT_MS = 3000;

    // ── Generate synthetic test data ──
    log("Generating synthetic test images...");
    cv::Mat r0_img = create_region0(640, 640);
    int fake_ammo = 25;
    cv::Mat r1_img = create_region1_ammo(fake_ammo, 120, 40);
    log("  Region 0: 640x640 checkerboard with crosshair");
    log("  Region 1: 120x40 ammo counter with number \"25\"");

    // Generate digit templates (for verification)
    auto digit_templates = generate_digit_templates(40);

    // ── Encode to JPEG ──
    log("Encoding regions to JPEG...");
    std::vector<int> jpeg_params = { cv::IMWRITE_JPEG_QUALITY, 85 };

    std::vector<uint8_t> jpeg0;
    cv::imencode(".jpg", r0_img, jpeg0, jpeg_params);
    printf("[test]  Region 0 JPEG: %zu bytes\n", jpeg0.size());

    std::vector<uint8_t> jpeg1;
    cv::imencode(".jpg", r1_img, jpeg1, jpeg_params);
    printf("[test]  Region 1 JPEG: %zu bytes\n", jpeg1.size());

    // ── Build multi-region frame ──
    log("Building V2 multi-region frame...");
    FrameHeader header;
    header.version = PROTOCOL_VERSION_V2;
    header.width = 2560;  // simulated full frame width
    header.height = 1440; // simulated full frame height
    header.frame_seq = 1;
    header.capture_timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    header.format = FORMAT_MULTI_REGION;

    MultiRegionHeader mr_header;
    mr_header.num_regions = 2;

    // Region 0: main AI crop (center)
    mr_header.regions[0].id = 0;
    mr_header.regions[0].src_x = (2560 - 640) / 2;
    mr_header.regions[0].src_y = (1440 - 640) / 2;
    mr_header.regions[0].width = 640;
    mr_header.regions[0].height = 640;

    // Region 1: ammo counter (simulated top-right)
    mr_header.regions[1].id = 1;
    mr_header.regions[1].src_x = 1800;
    mr_header.regions[1].src_y = 20;
    mr_header.regions[1].width = 120;
    mr_header.regions[1].height = 40;

    std::vector<std::vector<uint8_t>> region_jpegs = { jpeg0, jpeg1 };

    std::vector<uint8_t> frame_data;
    encode_multi_region(header, mr_header, region_jpegs, frame_data);
    printf("[test] Encoded frame: %zu bytes\n", frame_data.size());

    // ── Verify decode (in-process) ──
    log("In-process decode verification...");
    auto decoded_opt = decode_multi_region(frame_data.data(), frame_data.size());
    if (!decoded_opt) {
        log("FAIL: decode_multi_region returned nullopt");
        return 1;
    }
    auto& dec = *decoded_opt;
    printf("[test]  Decoded %u regions, format=%d, seq=%llu\n",
           dec.mr_header.num_regions, dec.header.format,
           (unsigned long long)dec.header.frame_seq);

    if (dec.mr_header.num_regions != 2) {
        printf("FAIL: expected 2 regions, got %u\n", dec.mr_header.num_regions);
        return 1;
    }

    for (uint32_t i = 0; i < dec.mr_header.num_regions; ++i) {
        auto& e = dec.mr_header.regions[i];
        printf("[test]  Region %u: id=%d (%dx%d) jpeg=%u bytes\n",
               i, e.id, e.width, e.height, e.jpeg_size);
        if (e.jpeg_size == 0 || dec.region_data[i] == nullptr) {
            printf("FAIL: region %u has no data\n", i);
            return 1;
        }
    }

    // Decode JPEGs back to cv::Mat
    std::vector<cv::Mat> decoded_imgs(2);
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> jpeg_buf(
            dec.region_data[i],
            dec.region_data[i] + dec.region_size[i]);
        decoded_imgs[i] = cv::imdecode(jpeg_buf, cv::IMREAD_COLOR);
        if (decoded_imgs[i].empty()) {
            printf("FAIL: imdecode failed for region %d\n", i);
            return 1;
        }
    }

    // ── UDP loopback test ──
    log("UDP loopback test...");
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log("FAIL: WSAStartup");
        return 1;
    }

    // Receiver socket — bind to port 0 for OS-assigned port
    SOCKET recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_sock == INVALID_SOCKET) {
        log("FAIL: receiver socket");
        WSACleanup();
        return 1;
    }
    sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = INADDR_ANY;  // bind to any local address
    recv_addr.sin_port = htons(0);  // OS assigns
    if (bind(recv_sock, (sockaddr*)&recv_addr, sizeof(recv_addr)) == SOCKET_ERROR) {
        printf("FAIL: bind failed %d\n", WSAGetLastError());
        closesocket(recv_sock);
        WSACleanup();
        return 1;
    }

    // Retrieve the actual port assigned by OS
    sockaddr_in bound_addr{};
    int bound_len = sizeof(bound_addr);
    getsockname(recv_sock, (sockaddr*)&bound_addr, &bound_len);
    int actual_port = ntohs(bound_addr.sin_port);
    printf("[test] Receiver bound to port %d\n", actual_port);

    // Set receive timeout so recvfrom doesn't block forever
    DWORD rcv_timeout = TIMEOUT_MS;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_timeout, sizeof(rcv_timeout));

    // Sender socket
    SOCKET send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_sock == INVALID_SOCKET) {
        log("FAIL: sender socket");
        closesocket(recv_sock);
        WSACleanup();
        return 1;
    }
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_port = htons(actual_port);

    // Fragment and send the frame via UDP
    std::vector<std::vector<uint8_t>> datagrams;
    FragmentEncoder::encode(0, frame_data.data(), frame_data.size(), datagrams);
    printf("[test] Fragmented into %zu datagrams\n", datagrams.size());

    for (const auto& dg : datagrams) {
        int sent = sendto(send_sock, (const char*)dg.data(), (int)dg.size(), 0,
                          (sockaddr*)&dest_addr, sizeof(dest_addr));
        if (sent == SOCKET_ERROR) {
            printf("FAIL: sendto error %d\n", WSAGetLastError());
        }
    }

    // Receive on loopback
    std::vector<uint8_t> recv_buf(MAX_DATAGRAM_SIZE);
    std::unique_ptr<FragmentAssembler> assembler;
    bool received = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_MS);

    while (std::chrono::steady_clock::now() < deadline && !received) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        int bytes = recvfrom(recv_sock, (char*)recv_buf.data(), (int)recv_buf.size(),
                             0, (sockaddr*)&from, &from_len);
        if (bytes <= 0) continue;

        auto frag_info = FragmentEncoder::decode_datagram(recv_buf.data(), bytes);
        if (!frag_info) continue;

        if (!assembler || assembler->frame_id() != frag_info->frame_id) {
            assembler = std::make_unique<FragmentAssembler>(
                frag_info->frame_id, frag_info->frag_count);
        }
        assembler->add_fragment(frag_info->frag_idx, frag_info->data, frag_info->data_size);

        if (assembler->is_complete()) {
            received = true;
        }
    }

    closesocket(recv_sock);
    closesocket(send_sock);
    WSACleanup();

    if (!received) {
        log("FAIL: timeout waiting for UDP frames");
        return 1;
    }

    // ── Decode received frame ──
    log("Decoding received multi-region frame...");
    auto assembled = assembler->assemble();
    auto rx_opt = decode_multi_region(assembled.data(), assembled.size());
    if (!rx_opt) {
        log("FAIL: decode received frame");
        return 1;
    }
    auto& rx = *rx_opt;
    printf("[test] Received %u regions via UDP\n", rx.mr_header.num_regions);

    std::vector<cv::Mat> rx_imgs(2);
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> jpeg_buf(
            rx.region_data[i], rx.region_data[i] + rx.region_size[i]);
        rx_imgs[i] = cv::imdecode(jpeg_buf, cv::IMREAD_COLOR);
        if (rx_imgs[i].empty()) {
            printf("FAIL: imdecode region %d after UDP\n", i);
            return 1;
        }
    }

    // ── Detect ammo number and annotate ──
    log("Detecting ammo number from received region 1...");
    int detected = detect_ammo_number(rx_imgs[1], digit_templates);
    printf("[test] Detected ammo: %d (expected: %d)\n", detected, fake_ammo);

    // Create annotated image: white background + drawn text
    cv::Mat annotated;
    if (rx_imgs[1].channels() == 3)
        rx_imgs[1].copyTo(annotated);
    else
        cv::cvtColor(rx_imgs[1], annotated, cv::COLOR_GRAY2BGR);

    // Draw detection result as overlay text
    std::string det_text = "Detected: " + std::to_string(detected);
    if (detected == fake_ammo) {
        cv::putText(annotated, det_text, cv::Point(5, annotated.rows - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 200, 0), 2);
        log("PASS: detected number matches");
    } else {
        cv::putText(annotated, det_text + " (MISMATCH!)", cv::Point(5, annotated.rows - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 200), 2);
        log("WARN: detected number does NOT match expected");
    }

    // ── Save debug images ──
    log("Saving debug images...");
    bool saved = save_debug_images(rx_imgs[0], annotated, "test_result");
    if (!saved) {
        log("WARN: could not save debug images");
    }

    // ── Summary ──
    printf("\n=== Results ===\n");
    printf("  Region 0 decode:       %s (%dx%d)\n",
           rx_imgs[0].empty() ? "FAIL" : "OK",
           rx_imgs[0].cols, rx_imgs[0].rows);
    printf("  Region 1 decode:       %s (%dx%d)\n",
           rx_imgs[1].empty() ? "FAIL" : "OK",
           rx_imgs[1].cols, rx_imgs[1].rows);
    printf("  Ammo detection:        %d\n", detected);
    printf("  Expected:              %d\n", fake_ammo);
    printf("  Debug images:          %%USERPROFILE%%\\rn_ai\\debug\\\n");

    int exit_code = (detected == fake_ammo) ? 0 : 1;
    printf("\n%s\n", exit_code == 0 ? "ALL PASS" : "SOME FAILURES");
    return exit_code;
}
