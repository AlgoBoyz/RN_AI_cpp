#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <atomic>
#include <chrono>
#include "mouse_input.h"
#include "async_logger.h"

std::atomic<int> MouseInputGatherer::s_accum_dx{0};
std::atomic<int> MouseInputGatherer::s_accum_dy{0};
std::atomic<int> MouseInputGatherer::s_accum_buttons{0};
std::atomic<int> MouseInputGatherer::s_accum_wheel{0};

// Raw WM_INPUT event counter for diagnosing capture rate vs drain rate.
static std::atomic<uint64_t> g_raw_input_events{0};
static std::atomic<uint64_t> g_raw_move_events{0};
static auto g_raw_stats_last = std::chrono::steady_clock::now();

MouseInputGatherer::MouseInputGatherer() = default;
MouseInputGatherer::~MouseInputGatherer() { stop(); }

LRESULT CALLBACK MouseInputGatherer::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        g_raw_input_events.fetch_add(1, std::memory_order_relaxed);
        RAWINPUT raw = {};
        HRAWINPUT hri = (HRAWINPUT)lp;
        UINT sz = sizeof(raw);
        if (GetRawInputData(hri, RID_INPUT, &raw, &sz, sizeof(RAWINPUTHEADER)) == sz) {
            if (raw.header.dwType == RIM_TYPEMOUSE) {
                // Relative movement
                if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                    s_accum_dx += raw.data.mouse.lLastX;
                    s_accum_dy += raw.data.mouse.lLastY;
                    g_raw_move_events.fetch_add(1, std::memory_order_relaxed);
                }
                // Buttons
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
                    s_accum_buttons |= 0x01;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
                    s_accum_buttons &= ~0x01;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
                    s_accum_buttons |= 0x02;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
                    s_accum_buttons &= ~0x02;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
                    s_accum_buttons |= 0x04;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
                    s_accum_buttons &= ~0x04;
                // Side buttons (X1/X2) -> bit 3 / bit 4 (matches MAKCU button_mask_)
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)
                    s_accum_buttons |= 0x08;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)
                    s_accum_buttons &= ~0x08;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)
                    s_accum_buttons |= 0x10;
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)
                    s_accum_buttons &= ~0x10;
                // Wheel
                if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
                    s_accum_wheel += (int16_t)raw.data.mouse.usButtonData / WHEEL_DELTA;
            }
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

bool MouseInputGatherer::start() {
    running_ = true;
    thread_ = std::thread([this]() {
        HINSTANCE hinst = GetModuleHandleA(nullptr);

        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hinst;
        wc.lpszClassName = "RN_MouseInput_Window";
        RegisterClassExA(&wc);

        hwnd_ = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            wc.lpszClassName, "", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
        if (!hwnd_) {
            std::cerr << "[MouseInput] CreateWindow failed: " << GetLastError() << std::endl;
            return;
        }

        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

        if (!registerRawInput()) {
            if (hwnd_) DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            return;
        }

        ALOG("mouse_input: started ok");
        std::cout << "[MouseInput] Started (RIDEV_INPUTSINK)" << std::endl;

        // Message pump on this thread — dispatches WM_INPUT to WndProc
        MSG msg = {};
        while (running_ && GetMessageA(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    });

    // Wait briefly for the window to be created
    Sleep(50);
    return hwnd_ != nullptr;
}

bool MouseInputGatherer::registerRawInput() {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd_;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        std::cerr << "[MouseInput] RegisterRawInputDevices failed: " << GetLastError() << std::endl;
        return false;
    }
    raw_registered_ = true;
    return true;
}

void MouseInputGatherer::suspend() {
    if (raw_registered_) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = RIDEV_REMOVE;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        raw_registered_ = false;
        std::cout << "[MouseInput] Suspended (raw input unregistered)" << std::endl;
    }
}

void MouseInputGatherer::resume() {
    if (!raw_registered_ && hwnd_) {
        registerRawInput();
        std::cout << "[MouseInput] Resumed" << std::endl;
    }
}

void MouseInputGatherer::stop() {
    running_ = false;
    if (hwnd_) PostMessageA(hwnd_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

RawMouseAccum MouseInputGatherer::drain() {
    RawMouseAccum result;
    result.dx = s_accum_dx.exchange(0);
    result.dy = s_accum_dy.exchange(0);
    result.buttons = static_cast<uint8_t>(s_accum_buttons.load());
    result.wheel = s_accum_wheel.exchange(0);

    // Diagnostics: compare raw WM_INPUT rate vs drain rate (once per second).
    static std::atomic<uint64_t> drain_count{0};
    drain_count.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now();
    if (now - g_raw_stats_last >= std::chrono::seconds(1)) {
        g_raw_stats_last = now;
        uint64_t ev = g_raw_input_events.exchange(0, std::memory_order_relaxed);
        uint64_t mv = g_raw_move_events.exchange(0, std::memory_order_relaxed);
        uint64_t dr = drain_count.exchange(0, std::memory_order_relaxed);
        ALOG("mouse_rate: raw_events=%llu move_events=%llu drains=%llu (raw_hz=%llu drain_hz=%llu)",
             (unsigned long long)ev, (unsigned long long)mv, (unsigned long long)dr,
             (unsigned long long)ev, (unsigned long long)dr);
    }

    if (result.dx || result.dy || result.wheel)
        ALOG("mouse_input: dx=%d dy=%d buttons=0x%02X wheel=%d", result.dx, result.dy, result.buttons, result.wheel);
    return result;
}
