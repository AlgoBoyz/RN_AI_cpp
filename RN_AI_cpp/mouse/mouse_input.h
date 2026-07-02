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
    RawMouseAccum drain();

private:
    void threadFunc();

    std::atomic<bool> running_{false};
    std::thread thread_;
    HWND hwnd_ = nullptr;

    static int s_accum_dx;
    static int s_accum_dy;
    static int s_accum_buttons;
    static int s_accum_wheel;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
