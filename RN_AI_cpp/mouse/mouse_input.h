#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>

struct RawMouseAccum {
    int dx = 0;
    int dy = 0;
    uint8_t buttons = 0;
};

class MouseInputGatherer {
public:
    MouseInputGatherer();
    ~MouseInputGatherer();

    bool start();
    void stop();
    RawMouseAccum drain();

private:
    void gatherThread();

    std::atomic<bool> should_stop_{false};
    std::thread thread_;

    std::mutex mtx_;
    RawMouseAccum accum_;
    bool raw_input_registered_ = false;
};
