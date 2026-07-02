#ifndef MAKCU_CONNECTION_H
#define MAKCU_CONNECTION_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#if __has_include("../modules/makcu/include/makcu.h")
#include "../modules/makcu/include/makcu.h"
#define RN_MAKCU_SDK_AVAILABLE 1
#else
#define RN_MAKCU_SDK_AVAILABLE 0
#include <vector>
#include "serial/serial.h"
#endif

class MakcuConnection
{
public:
    MakcuConnection(const std::string& port, unsigned int baud_rate);
    ~MakcuConnection();

    bool isOpen() const;

    void write(const std::string& data);
    std::string read();

    void click(int button);
    void press(int button);
    void release(int button);
    void move(int x, int y, uint8_t buttons = 0, int8_t wheel = 0, int8_t hwheel = 0);

    void start_boot();
    void reboot();
    void send_stop();

    bool aiming_active;
    bool shooting_active;
    bool zooming_active;
    bool triggerbot_active;
    bool side1_active;
    bool side2_active;
    bool left_active;
    bool right_active;
    bool middle_active;

private:
#if RN_MAKCU_SDK_AVAILABLE
    void startSdkPolling();
    void stopSdkPolling();
    void sdkPollingThreadFunc();
    void applyButtonMask(uint8_t mask, const char* source);
    void onButtonCallback(makcu::MouseButton button, bool pressed);
    makcu::MouseButton toMouseButton(int button) const;
    makcu::Device device_;
    std::atomic<bool> sdk_listening_;
    std::thread sdk_listening_thread_;
#else
    void sendMouseFrame(int16_t dx, int16_t dy, uint8_t buttons, int8_t wheel = 0, int8_t hwheel = 0);
    void updateTrackingFields();
    uint8_t buttonToBit(int button) const;
    void startListening();
    void listeningThreadFunc();

    serial::Serial serial_;
    std::atomic<bool> listening_;
    std::thread listening_thread_;
    uint8_t button_mask_;
#endif

    std::atomic<bool> is_open_;
    std::mutex write_mutex_;
};

#endif // MAKCU_CONNECTION_H
