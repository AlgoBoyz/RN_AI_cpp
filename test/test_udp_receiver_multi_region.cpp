/**
 * test_udp_receiver_multi_region.cpp
 *
 * 从真实 frame_sender 接收多区域 UDP 帧，在弹药区域(region 1)中用 CNN 识别数字。
 * 将原始区域图像保存到 debug 目录（文件名含识别结果）。
 *
 * 数字识别: ONNX CNN 模型 (models/digit_classifier.onnx)
 *   十分位: x=312  y=13  w=37  h=47  (相对 region 1)
 *   个分位: x=351  y=13  w=34  h=48
 *
 * 编译:
 *   cl /std:c++17 /EHsc /utf-8 /I. /Ivendor\opencv\install\include
 *       /Ipackages\Microsoft.ML.OnnxRuntime.DirectML.1.22.0\build\native\include
 *       test\test_udp_receiver_multi_region.cpp
 *       RN_AI_cpp\capture\udp_wire_protocol.cpp
 *       ws2_32.lib vendor\opencv\install\x64\vc17\lib\opencv_world4100.lib
 *       packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.0\runtimes\win-x64\native\onnxruntime.lib
 *       /Fe:test_udp_receiver_multi_region.exe
 *
 * 运行:
 *   test_udp_receiver_multi_region [端口号]
 *   默认端口 12345
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

#include "RN_AI_cpp/capture/udp_wire_protocol.h"

// ── 数字位 ROI（相对于 region 1 图像，由 roi_selector.py 框选） ──────────
constexpr int TENS_X = 312, TENS_Y = 13, TENS_W = 37, TENS_H = 47;
constexpr int ONES_X = 351, ONES_Y = 13, ONES_W = 34, ONES_H = 48;
#include "../RN_AI_cpp/digit/digit_classifier.h"

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 12345;

    printf("[receiver] Listening on port %d for multi-region UDP frames...\n", port);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { fprintf(stderr, "socket failed\n"); WSACleanup(); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed\n"); closesocket(sock); WSACleanup(); return 1;
    }

    DWORD timeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int rcvbuf = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    const char* profile = getenv("USERPROFILE");
    char debug_dir[MAX_PATH];
    if (profile) {
        sprintf_s(debug_dir, "%s\\rn_ai\\debug", profile);
        // 清空 debug 目录，重新积累
        std::error_code ec;
        std::filesystem::remove_all(debug_dir, ec);
        std::filesystem::create_directories(debug_dir, ec);
        printf("[debug] Cleared and ready: %s\n", debug_dir);
    }

    std::vector<uint8_t> buffer(MAX_DATAGRAM_SIZE);
    std::unique_ptr<FragmentAssembler> assembler;
    auto last_stat = std::chrono::steady_clock::now();
    int total_frames = 0;

    printf("[receiver] Waiting for frames...\n");

    // 初始化 CNN 分类器
    DigitClassifier classifier(L"models/digit_classifier.onnx");

    while (true) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        int bytes = recvfrom(sock, (char*)buffer.data(), (int)buffer.size(),
                             0, (sockaddr*)&from, &from_len);

        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_stat > std::chrono::seconds(2)) {
                    printf("[receiver] %d frames received so far...\n", total_frames);
                    last_stat = now;
                }
                continue;
            }
            break;
        }

        auto frag = FragmentEncoder::decode_datagram(buffer.data(), bytes);
        if (!frag) continue;

        if (!assembler || assembler->frame_id() != frag->frame_id) {
            assembler = std::make_unique<FragmentAssembler>(
                frag->frame_id, frag->frag_count);
        }
        assembler->add_fragment(frag->frag_idx, frag->data, frag->data_size);

        if (assembler->is_complete()) {
            auto assembled = assembler->assemble();
            assembler.reset();
            total_frames++;

            auto decoded = decode_multi_region(assembled.data(), assembled.size());
            if (!decoded) { printf("[receiver] Frame %d: decode FAILED\n", total_frames); continue; }

            auto& mr = decoded->mr_header;
            printf("[receiver] Frame %d: %u regions", total_frames, mr.num_regions);

            for (uint32_t i = 0; i < mr.num_regions; ++i) {
                auto& e = mr.regions[i];
                if (decoded->region_size[i] == 0) continue;

                std::vector<uint8_t> jpeg(
                    decoded->region_data[i],
                    decoded->region_data[i] + decoded->region_size[i]);

                cv::Mat img = cv::imdecode(jpeg, cv::IMREAD_COLOR);
                if (img.empty()) { printf(" [%u:FAIL]", e.id); continue; }
                printf(" [%u:%dx%d]", e.id, img.cols, img.rows);

                // 区域1：CNN 数字识别 + 绘制覆盖层
                int ammo_number = -1;
                if (e.id == 1) {
                    ammo_number = classifier.detect(img,
                        TENS_X, TENS_Y, TENS_W, TENS_H,
                        ONES_X, ONES_Y, ONES_W, ONES_H, 15.0f);
                    printf(" ammo=%d", ammo_number);

                    // 绘制 ROI 框（十分位=绿，个分位=蓝）
                    cv::rectangle(img, cv::Rect(TENS_X, TENS_Y, TENS_W, TENS_H),
                                  cv::Scalar(0, 255, 0), 1);
                    cv::rectangle(img, cv::Rect(ONES_X, ONES_Y, ONES_W, ONES_H),
                                  cv::Scalar(255, 128, 0), 1);

                    // 绘制识别数字（白底黑字，左上角）
                    char num_text[16];
                    sprintf_s(num_text, "%d", ammo_number);
                    int font = cv::FONT_HERSHEY_SIMPLEX;
                    double font_scale = 1.0;
                    int thickness = 2;
                    int baseline = 0;
                    cv::Size ts = cv::getTextSize(num_text, font, font_scale, thickness, &baseline);
                    // 白底
                    cv::rectangle(img, cv::Point(2, 2),
                                  cv::Point(ts.width + 8, ts.height + 8),
                                  cv::Scalar(255, 255, 255), cv::FILLED);
                    // 黑字
                    cv::putText(img, num_text, cv::Point(5, ts.height + 4),
                                font, font_scale, cv::Scalar(0, 0, 0), thickness);
                }

                if (profile) {
                    char path[MAX_PATH];
                    if (e.id == 1 && ammo_number >= 0)
                        sprintf_s(path, "%s\\frame_%05d_ammo_%d.png", debug_dir, total_frames, ammo_number);
                    else
                        sprintf_s(path, "%s\\frame_%05d_region_%d.png", debug_dir, total_frames, e.id);
                    cv::imwrite(path, img);
                }
            }
            printf("\n");
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
