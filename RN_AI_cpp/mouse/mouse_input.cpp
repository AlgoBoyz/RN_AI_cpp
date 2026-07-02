#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include "mouse_input.h"
#include "async_logger.h"

#pragma comment(lib, "user32.lib")

MouseInputGatherer::MouseInputGatherer() = default;
MouseInputGatherer::~MouseInputGatherer() { stop(); }

bool MouseInputGatherer::start() {
    // Create a message-only window for raw input
    HINSTANCE hinst = GetModuleHandleA(nullptr);
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

    // Register for raw mouse input
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01; // HID usage page: Generic Desktop
    rid.usUsage = 0x02;     // HID usage: Mouse
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

    // Start gather thread
    should_stop_ = false;
    thread_ = std::thread([this, hwnd]() {
        // Message loop for raw input
        MSG msg;
        BOOL result;
        while (!should_stop_.load()) {
            result = PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE);
            if (!result) {
                // No messages, wait for raw input
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_INPUT | QS_RAWINPUT);
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageA(&msg);

            if (msg.message == WM_INPUT) {
                HRAWINPUT hRawInput = reinterpret_cast<HRAWINPUT>(msg.lParam);
                RAWINPUT raw = {};
                UINT size = sizeof(raw);
                if (GetRawInputData(hRawInput, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != size) {
                    continue;
                }

                if (raw.header.dwType == RIM_TYPEMOUSE) {
                    int dx = raw.data.mouse.lLastX;
                    int dy = raw.data.mouse.lLastY;
                    if (dx == 0 && dy == 0) continue;

                    std::lock_guard<std::mutex> lock(mtx_);
                    accum_.dx += dx;
                    accum_.dy += dy;
                }
            }
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
