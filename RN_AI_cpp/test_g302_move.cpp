/**
 * test_g302_move.cpp — 硬件通信测试
 *
 * 向 STM32 G302 固件连续 5 秒发送向左移动指令（dx = -5）。
 * 使用与 MakcuConnection 串口后备路径相同的 10 字节二进制帧协议。
 *
 * 构建:
 *   cl.exe /EHsc /I modules\serial\include test_g302_move.cpp
 *       /link modules\serial\visual_studio\x64\Release\serial.lib
 *
 * 运行:
 *   test_g302_move.exe COM14
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <string>

#include "serial/serial.h"

/* ── 协议常量 ─────────────────────────────────────────────────────── */
static const uint32_t SERIAL_BAUD = 921600;
static const uint8_t  FRAME_SOF   = 0xAA;
static const uint8_t  FRAME_OP    = 0x01;
static const uint8_t  RESP_SOF    = 0xBB;

/* ── 构建 10 字节帧 ───────────────────────────────────────────────── */
static void build_frame(uint8_t frame[10], int16_t dx, int16_t dy,
                        uint8_t buttons, int8_t wheel, int8_t hwheel)
{
    frame[0] = FRAME_SOF;
    frame[1] = FRAME_OP;
    frame[2] = static_cast<uint8_t>(dx & 0xFF);
    frame[3] = static_cast<uint8_t>((dx >> 8) & 0xFF);
    frame[4] = static_cast<uint8_t>(dy & 0xFF);
    frame[5] = static_cast<uint8_t>((dy >> 8) & 0xFF);
    frame[6] = buttons;
    frame[7] = static_cast<uint8_t>(wheel);
    frame[8] = static_cast<uint8_t>(hwheel);

    uint8_t csum = 0;
    for (int i = 0; i < 9; i++) csum ^= frame[i];
    frame[9] = csum;
}

/* ── 解析 3 字节状态回包 ──────────────────────────────────────────── */
static const char* fmt_status(uint8_t s)
{
    static char buf[64];
    buf[0] = '\0';

    if (s & 0x40) strcat(buf, " XR_ERR");
    if (s & 0x80) strcat(buf, " OVRRUN");
    if (buf[0] == '\0') {
        strcat(buf, " OK");
        if (s & 0x01) strcat(buf, "|FRAME_OK");
        if (s & 0x02) strcat(buf, "|PUSHED");
        if (s & 0x04) strcat(buf, "|DROPPED");
        if (s & 0x08) strcat(buf, "|USB_OK");
        if (s & 0x10) strcat(buf, "|USB_BUSY");
        if (s & 0x20) strcat(buf, "|USB_FAIL");
    }
    return buf;
}

/* ── 读取固件状态回包（非阻塞，超时 5ms）─────────────────────────── */
static int try_read_status(serial::Serial& port)
{
    enum class State { WaitSOF, WaitStatus, WaitCS };
    static State state = State::WaitSOF;
    static uint8_t rx_status = 0;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!port.available())
            break;

        uint8_t b = 0;
        port.read(&b, 1);

        switch (state) {
        case State::WaitSOF:
            if (b == RESP_SOF) state = State::WaitStatus;
            break;
        case State::WaitStatus:
            rx_status = b;
            state = State::WaitCS;
            break;
        case State::WaitCS:
            state = State::WaitSOF;
            if (b == (RESP_SOF ^ rx_status))
                return rx_status;
            return -1;   /* bad checksum */
        }
    }
    return -1;  /* timeout / incomplete */
}

/* ── 打印帧内容 ───────────────────────────────────────────────────── */
static void print_frame(const uint8_t frame[10])
{
    printf("TX  %02X %02X  dx=%+04d  dy=%+04d  btn=0x%02X  whl=%+03d  hwhl=%+03d  cs=%02X\n",
           frame[0], frame[1],
           (int16_t)(frame[2] | (frame[3] << 8)),
           (int16_t)(frame[4] | (frame[5] << 8)),
           frame[6], (int8_t)frame[7], (int8_t)frame[8], frame[9]);
    fflush(stdout);
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s <COM端口>\n", argv[0]);
        fprintf(stderr, "示例: %s COM14\n", argv[0]);
        return 1;
    }

    const std::string port_name = argv[1];
    const int DURATION_SEC = 5;
    const int16_t DX = -5;    /* 向左移动 */
    const int16_t DY = 0;
    const int FRAME_INTERVAL_MS = 5;  /* ~200 Hz */

    /* ── 打开串口 ──────────────────────────────────────────────────── */
    serial::Serial port;
    try {
        port.setPort(port_name);
        port.setBaudrate(SERIAL_BAUD);
        port.open();
        if (!port.isOpen()) {
            fprintf(stderr, "错误: 无法打开 %s\n", port_name.c_str());
            return 1;
        }
        printf("已连接 %s @ %u baud\n\n", port_name.c_str(), SERIAL_BAUD);
    }
    catch (const std::exception& e) {
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }

    /* ── 发送循环 ──────────────────────────────────────────────────── */
    uint8_t frame[10];
    int total_frames = 0;
    int ok_count = 0;
    int err_count = 0;

    auto start = std::chrono::steady_clock::now();
    auto next_tx = start;

    printf("%-6s  %-24s  %s\n", "t(s)", "Frame", "Status");
    printf("------------------------------------------------------------\n");

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed >= DURATION_SEC)
            break;

        /* 定时发送 */
        if (now >= next_tx) {
            build_frame(frame, DX, DY, 0, 0, 0);
            port.write(frame, 10);
            total_frames++;
            print_frame(frame);
            next_tx = now + std::chrono::milliseconds(FRAME_INTERVAL_MS);
        }

        /* 读回包 */
        int st = try_read_status(port);
        if (st >= 0) {
            if (st & 0xC0) {
                printf("  RX  status=0x%02X%s\n", st, fmt_status(static_cast<uint8_t>(st)));
                err_count++;
            } else {
                ok_count++;
            }
        }

        /* 不忙等 */
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    /* ── 统计 ──────────────────────────────────────────────────────── */
    printf("\n=== 测试完成 ===\n");
    printf("  发送帧数:  %d\n", total_frames);
    printf("  正常回包:  %d\n", ok_count);
    printf("  异常回包:  %d\n", err_count);
    printf("  平均帧率:  %.0f Hz\n", total_frames / double(DURATION_SEC));

    port.close();
    return err_count > 0 ? 1 : 0;
}
