/**
 * test_udp_receiver_multi_region.cpp
 *
 * 从真实 frame_sender 接收多区域 UDP 帧，在弹药区域(region 1)中识别数字，
 * 将识别结果用白底黑字绘制在图像上并保存到 debug 目录。
 *
 * 弹药数字区域在原图中的裁剪坐标:
 *   x=305  y=7  w=85  h=54
 * （相对于 region 1 图像的左上角）
 *
 * 编译:
 *   cl /std:c++17 /EHsc /utf-8 /I. /Ivendor\opencv\install\include
 *       test\test_udp_receiver_multi_region.cpp
 *       RN_AI_cpp\capture\udp_wire_protocol.cpp
 *       ws2_32.lib vendor\opencv\install\x64\vc17\lib\opencv_world4100.lib
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

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#include "RN_AI_cpp/capture/udp_wire_protocol.h"

// ── 弹药数字裁剪参数（相对于 region 1 图像） ──────────────────────────────
constexpr int AMMO_DIGIT_X = 305;
constexpr int AMMO_DIGIT_Y = 7;
constexpr int AMMO_DIGIT_W = 85;
constexpr int AMMO_DIGIT_H = 54;

// ── 模板匹配数字识别 ─────────────────────────────────────────────────────
// 模板为 1.png~50.png，每张是对应弹药数的完整数字区域截图。
// 直接用整图 TM_SQDIFF_NORMED 匹配，不做分割。
static int detect_number(const cv::Mat& gray_roi) {
    if (gray_roi.empty()) return -1;

    static bool templates_loaded = false;
    static std::vector<cv::Mat> templates;
    static int tmpl_w = 0, tmpl_h = 0;
    static int template_offset = 1; // 模板从 1.png 开始

    if (!templates_loaded) {
        const char* profile = getenv("USERPROFILE");
        char path[MAX_PATH];
        templates.reserve(51);
        for (int v = 1; v <= 50; v++) {
            sprintf_s(path, "%s\\rn_ai\\%d.png", profile, v);
            cv::Mat tmpl = cv::imread(path, cv::IMREAD_GRAYSCALE);
            if (!tmpl.empty()) {
                if (tmpl_w == 0) { tmpl_w = tmpl.cols; tmpl_h = tmpl.rows; }
                templates.push_back(tmpl);
            }
        }
        template_offset = 1;
        // 尝试加载 0.png（空弹夹），若有则偏移为 0
        sprintf_s(path, "%s\\rn_ai\\0.png", profile);
        cv::Mat tmpl0 = cv::imread(path, cv::IMREAD_GRAYSCALE);
        if (!tmpl0.empty()) {
            templates.insert(templates.begin(), tmpl0);
            template_offset = 0;
        }

        templates_loaded = true;
        printf("[templates] Loaded %zu templates (%dx%d), base_offset=%d\n",
               templates.size(), tmpl_w, tmpl_h, template_offset);
    }

    // 整图缩放到模板大小
    cv::Mat input;
    cv::resize(gray_roi, input, cv::Size(tmpl_w, tmpl_h));

    int best_val = -1;
    double best_score = DBL_MAX;

    // 收集所有匹配分数
    std::vector<std::pair<double,int>> scores;
    for (size_t i = 0; i < templates.size(); i++) {
        cv::Mat r;
        cv::matchTemplate(input, templates[i], r, cv::TM_SQDIFF_NORMED);
        double mv; cv::minMaxLoc(r, &mv);
        scores.push_back({mv, (int)(i + template_offset)});
    }
    std::sort(scores.begin(), scores.end());

    best_score = scores[0].first;
    best_val = scores[0].second;

    // 每帧输出 top3 方便调试
    printf(" [top3:");
    for (int k = 0; k < 3 && k < (int)scores.size(); k++)
        printf(" %d=%.3f", scores[k].second, scores[k].first);
    printf("]");

    return best_val;
}

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
        char cmd[MAX_PATH * 2];
        sprintf_s(cmd, "rmdir /s /q \"%s\" 2>nul", debug_dir);
        system(cmd);
        CreateDirectoryA(debug_dir, nullptr);
        printf("[debug] Region images with ammo labels will be saved to %s\n", debug_dir);
    }

    std::vector<uint8_t> buffer(MAX_DATAGRAM_SIZE);
    std::unique_ptr<FragmentAssembler> assembler;
    auto last_stat = std::chrono::steady_clock::now();
    int total_frames = 0;

    printf("[receiver] Waiting for frames...\n");

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

                // 区域1：裁剪弹药数字子区域并识别
                int ammo_number = -1;
                if (e.id == 1 && img.cols > AMMO_DIGIT_X + AMMO_DIGIT_W &&
                                 img.rows > AMMO_DIGIT_Y + AMMO_DIGIT_H) {
                    cv::Rect roi(AMMO_DIGIT_X, AMMO_DIGIT_Y, AMMO_DIGIT_W, AMMO_DIGIT_H);
                    cv::Mat digit_area = img(roi).clone();

                    cv::Mat gray;
                    cv::cvtColor(digit_area, gray, cv::COLOR_BGR2GRAY);
                    ammo_number = detect_number(gray);
                    printf(" ammo=%d", ammo_number);
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
