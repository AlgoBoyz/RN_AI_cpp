#pragma once
#include <cstdint>
#include <windows.h>
#include <atomic>
#include <thread>

struct RawMouseAccum {
    int dx = 0;
    int dy = 0;
    uint8_t buttons = 0;
    int wheel = 0;
};

class MouseInputGatherer {
public:
    MouseInputGatherer();
    ~MouseInputGatherer();

    bool start();
    void stop();
    void suspend();   // unregister raw input (restore legacy messages for overlay)
    void resume();    // re-register with RIDEV_NOLEGACY
    RawMouseAccum drain();

private:
    void threadFunc();
    bool registerRawInput();

    std::atomic<bool> running_{false};
    std::thread thread_;
    HWND hwnd_ = nullptr;
    bool raw_registered_ = false;

    static std::atomic<int> s_accum_dx;
    static std::atomic<int> s_accum_dy;
    static std::atomic<int> s_accum_buttons;
    static std::atomic<int> s_accum_wheel;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
