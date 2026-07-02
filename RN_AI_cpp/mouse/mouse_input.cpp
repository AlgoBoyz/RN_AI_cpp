#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <vector>
#include "mouse_input.h"
#include "async_logger.h"

// RAWINPUTHEADER is 24 bytes on x64, but GetRawInputBuffer uses
// sizeof(RAWINPUTHEADER) for stride — keep this aligned.
#define RAW_HDR_SIZE sizeof(RAWINPUTHEADER)

MouseInputGatherer::MouseInputGatherer() = default;
MouseInputGatherer::~MouseInputGatherer() { stop(); }

bool MouseInputGatherer::start() {
    HINSTANCE hinst = GetModuleHandleA(nullptr);

    // Register a simple message-only window class
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        return DefWindowProcA(hwnd, msg, wp, lp);
    };
    wc.hInstance = hinst;
    wc.lpszClassName = "RN_MouseInput_Window";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, hinst, nullptr);
    if (!hwnd) {
        ALOG("mouse_input: FAIL create message window");
        std::cerr << "[MouseInput] Failed to create message window" << std::endl;
        return false;
    }

    // Register raw mouse input (RIDEV_INPUTSINK to capture even when not foreground)
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        ALOG("mouse_input: FAIL register raw input err=%lu", GetLastError());
        std::cerr << "[MouseInput] Failed to register raw input: " << GetLastError() << std::endl;
        DestroyWindow(hwnd);
        return false;
    }
    raw_input_registered_ = true;

    ALOG("mouse_input: started ok");
    std::cout << "[MouseInput] Started" << std::endl;

    // Buffer for GetRawInputBuffer (4 KB initial, grows if needed)
    std::vector<uint8_t> raw_buf(4096);

    should_stop_ = false;
    thread_ = std::thread([this, hwnd, raw_buf{std::move(raw_buf)}]() mutable {
        MSG msg = {};

        while (!should_stop_.load()) {
            // 1. Dispatch all messages EXCEPT WM_INPUT (which GetRawInputBuffer consumes)
            while (PeekMessageA(&msg, nullptr, 0, WM_INPUT - 1, PM_REMOVE) ||
                   PeekMessageA(&msg, nullptr, WM_INPUT + 1, UINT_MAX, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                if (msg.message == WM_QUIT) break;
            }

            // 2. Batch-read raw input
            UINT cb_size = (UINT)raw_buf.size();
            UINT count = GetRawInputBuffer((PRAWINPUT)raw_buf.data(), &cb_size, RAW_HDR_SIZE);

            if (count == 0) {
                // Buffer might be too small — cb_size now holds required size
                if (cb_size > raw_buf.size()) {
                    raw_buf.resize(cb_size);
                }
            } else if (count != UINT_MAX) {
                // 3. Process all raw input events
                std::lock_guard<std::mutex> lock(mtx_);
                PRAWINPUT raw_iter = (PRAWINPUT)raw_buf.data();
                for (UINT i = 0; i < count; i++) {
                    if (raw_iter->header.dwType == RIM_TYPEMOUSE) {
                        if (!(raw_iter->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                            accum_.dx += raw_iter->data.mouse.lLastX;
                            accum_.dy += raw_iter->data.mouse.lLastY;
                        }
                    }
                    // Advance to next RAWINPUT block (manual NEXTRAWINPUTBLOCK)
                    raw_iter = (PRAWINPUT)((PBYTE)raw_iter + raw_iter->header.dwSize);
                }

                // 4. Tell the system we consumed the data
                PRAWINPUT raw_ptr = (PRAWINPUT)raw_buf.data();
                DefRawInputProc(&raw_ptr, (int)count, RAW_HDR_SIZE);
            }

            // 5. Wait efficiently for next input (100 ms timeout as stop-check fallback)
            MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        if (raw_input_registered_) {
            DestroyWindow(hwnd);
        }
    });

    return true;
}

void MouseInputGatherer::stop() {
    should_stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

RawMouseAccum MouseInputGatherer::drain() {
    RawMouseAccum result;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        result.dx = accum_.dx;
        result.dy = accum_.dy;
        result.buttons = accum_.buttons;
        accum_.dx = 0;
        accum_.dy = 0;
        accum_.buttons = 0;
    }
    if (result.dx || result.dy) ALOG("mouse_input: dx=%d dy=%d", result.dx, result.dy);
    return result;
}
