#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MakcuConnection.h"
#include "async_logger.h"
#include "config.h"
#include "rn_ai_cpp.h"
#ifdef makcu
#undef makcu
#endif

namespace {
std::mutex g_makcu_state_log_mutex;

void logMakcuButtons(const char* source, const MakcuConnection& connection, int raw_value = -1)
{
    std::lock_guard<std::mutex> lock(g_makcu_state_log_mutex);
    std::cout << "[Makcu][" << source << "]";
    if (raw_value >= 0) {
        std::cout << " raw=" << raw_value;
    }
    std::cout
        << " L=" << (connection.left_active ? 1 : 0)
        << " R=" << (connection.right_active ? 1 : 0)
        << " M=" << (connection.middle_active ? 1 : 0)
        << " S1=" << (connection.side1_active ? 1 : 0)
        << " S2=" << (connection.side2_active ? 1 : 0)
        << " shoot=" << (connection.shooting_active ? 1 : 0)
        << " aim=" << (connection.aiming_active ? 1 : 0)
        << " zoom=" << (connection.zooming_active ? 1 : 0)
        << " trig=" << (connection.triggerbot_active ? 1 : 0)
        << std::endl;
}
} // namespace

#if RN_MAKCU_SDK_AVAILABLE

MakcuConnection::MakcuConnection(const std::string& port, unsigned int baud_rate)
    : is_open_(false)
    , sdk_listening_(false)
    , aiming_active(false)
    , shooting_active(false)
    , zooming_active(false)
    , triggerbot_active(false)
    , side1_active(false)
    , side2_active(false)
    , left_active(false)
    , right_active(false)
    , middle_active(false)
{
    try
    {
        device_.setMouseButtonCallback([this](makcu::MouseButton button, bool pressed) {
            onButtonCallback(button, pressed);
        });

        device_.enableButtonMonitoring(true);

        if (device_.connect(port))
        {
            if (baud_rate > 0)
            {
                if (!device_.setBaudRate(baud_rate, true))
                {
                    ALOG("makcu: FAIL set baudrate %u", baud_rate);
                    std::cerr << "[Makcu] Failed to set baud rate to " << baud_rate
                              << ", continuing with current baud rate." << std::endl;
                }
            }

            is_open_ = true;
            ALOG("makcu: connected port=%s baud=%u", port.c_str(), baud_rate);
            std::cout << "[Makcu] Connected! PORT: " << port << std::endl;
            startSdkPolling();
        }
        else
        {
            ALOG("makcu: FAIL connect port=%s", port.c_str());
            std::cerr << "[Makcu] Unable to connect to the port: " << port << std::endl;
        }
    }
    catch (const makcu::MakcuException& e)
    {
        ALOG("makcu: FAIL exception %s", e.what());
        std::cerr << "[Makcu] Error: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        ALOG("makcu: FAIL exception %s", e.what());
        std::cerr << "[Makcu] Error: " << e.what() << std::endl;
    }
}

MakcuConnection::~MakcuConnection()
{
    stopSdkPolling();
    try
    {
        device_.enableButtonMonitoring(false);
        device_.disconnect();
    }
    catch (...)
    {
    }
    is_open_ = false;
}

bool MakcuConnection::isOpen() const
{
    return is_open_ && device_.isConnected();
}

void MakcuConnection::write(const std::string&)
{
}

std::string MakcuConnection::read()
{
    return std::string();
}

void MakcuConnection::move(int x, int y, uint8_t buttons, int8_t wheel, int8_t hwheel)
{
    if (!isOpen()) {
        ALOG("makcu: FAIL send dx=%d dy=%d not open", x, y);
        return;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    try
    {
        device_.mouseMove(x, y);
        ALOG("makcu: send dx=%d dy=%d btns=0x%02X whl=%d", x, y, buttons, wheel);
    }
    catch (const std::exception& e)
    {
        ALOG("makcu: FAIL send dx=%d dy=%d err=%s", x, y, e.what());
        is_open_ = false;
    }
    catch (...)
    {
        ALOG("makcu: FAIL send dx=%d dy=%d unknown err", x, y);
        is_open_ = false;
    }
}

makcu::MouseButton MakcuConnection::toMouseButton(int button) const
{
    switch (button)
    {
    case 1: return makcu::MouseButton::RIGHT;
    case 2: return makcu::MouseButton::MIDDLE;
    case 3: return makcu::MouseButton::SIDE1;
    case 4: return makcu::MouseButton::SIDE2;
    case 0:
    default:
        return makcu::MouseButton::LEFT;
    }
}

void MakcuConnection::click(int button)
{
    if (!isOpen())
        return;

    std::lock_guard<std::mutex> lock(write_mutex_);
    try
    {
        device_.click(toMouseButton(button));
    }
    catch (...)
    {
        is_open_ = false;
    }
}

void MakcuConnection::press(int button)
{
    if (!isOpen())
        return;

    std::lock_guard<std::mutex> lock(write_mutex_);
    try
    {
        device_.mouseDown(toMouseButton(button));
    }
    catch (...)
    {
        is_open_ = false;
    }
}

void MakcuConnection::release(int button)
{
    if (!isOpen())
        return;

    std::lock_guard<std::mutex> lock(write_mutex_);
    try
    {
        device_.mouseUp(toMouseButton(button));
    }
    catch (...)
    {
        is_open_ = false;
    }
}

void MakcuConnection::start_boot()
{
}

void MakcuConnection::reboot()
{
}

void MakcuConnection::send_stop()
{
}

void MakcuConnection::onButtonCallback(makcu::MouseButton button, bool pressed)
{
    switch (button)
    {
    case makcu::MouseButton::LEFT:
        left_active = pressed;
        shooting_active = pressed;
        shooting.store(pressed);
        break;

    case makcu::MouseButton::RIGHT:
        right_active = pressed;
        zooming_active = pressed;
        zooming.store(pressed);
        break;

    case makcu::MouseButton::MIDDLE:
        middle_active = pressed;
        break;

    case makcu::MouseButton::SIDE1:
        side1_active = pressed;
        triggerbot_active = pressed;
        triggerbot_button.store(pressed);
        break;

    case makcu::MouseButton::SIDE2:
        side2_active = pressed;
        aiming_active = pressed;
        aiming.store(pressed);
        break;

    default:
        break;
    }

    logMakcuButtons("callback", *this, static_cast<int>(button));
}

void MakcuConnection::applyButtonMask(uint8_t mask, const char* source)
{
    left_active = (mask & 0x01) != 0;
    right_active = (mask & 0x02) != 0;
    middle_active = (mask & 0x04) != 0;
    side1_active = (mask & 0x08) != 0;
    side2_active = (mask & 0x10) != 0;

    shooting_active = left_active;
    zooming_active = right_active;
    triggerbot_active = side1_active;
    aiming_active = side2_active;

    shooting.store(shooting_active);
    zooming.store(zooming_active);
    triggerbot_button.store(triggerbot_active);
    aiming.store(aiming_active);

    logMakcuButtons(source, *this, static_cast<int>(mask));
}

void MakcuConnection::startSdkPolling()
{
    sdk_listening_ = true;
    if (sdk_listening_thread_.joinable())
        sdk_listening_thread_.join();
    sdk_listening_thread_ = std::thread(&MakcuConnection::sdkPollingThreadFunc, this);
}

void MakcuConnection::stopSdkPolling()
{
    sdk_listening_ = false;
    if (sdk_listening_thread_.joinable())
        sdk_listening_thread_.join();
}

void MakcuConnection::sdkPollingThreadFunc()
{
    uint8_t last_mask = 0xFF;
    while (sdk_listening_ && isOpen())
    {
        try
        {
            const uint8_t mask = device_.getButtonMask();
            if (mask != last_mask)
            {
                applyButtonMask(mask, "sdk-mask");
                last_mask = mask;
            }
        }
        catch (...)
        {
            is_open_ = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

#else

/* ---------- STM32 G302 binary protocol constants ------------------- */
static const uint32_t SERIAL_BAUD = 921600;
static const uint8_t  FRAME_SOF   = 0xAA;
static const uint8_t  FRAME_OP    = 0x01;
static const uint8_t  RESP_SOF    = 0xBB;

/* ── Builder ──────────────────────────────────────────────────────── */
MakcuConnection::MakcuConnection(const std::string& port, unsigned int /*baud_rate*/)
    : is_open_(false)
    , listening_(false)
    , button_mask_(0)
    , aiming_active(false)
    , shooting_active(false)
    , zooming_active(false)
    , triggerbot_active(false)
    , side1_active(false)
    , side2_active(false)
    , left_active(false)
    , right_active(false)
    , middle_active(false)
{
    std::cerr << "[Makcu] SDK header not found. Using STM32 G302 serial protocol." << std::endl;

    try {
        serial_.setPort(port);
        serial_.setBaudrate(SERIAL_BAUD);
        serial_.open();
        if (!serial_.isOpen())
            throw std::runtime_error("open failed");

        is_open_ = true;
        std::cout << "[Makcu] Connected @" << SERIAL_BAUD << " on " << port
                  << " (STM32 G302 protocol)\n";

        startListening();
    }
    catch (const std::exception& e) {
        std::cerr << "[Makcu] Error: " << e.what() << '\n';
    }
}

/* ── Destructor ───────────────────────────────────────────────────── */
MakcuConnection::~MakcuConnection()
{
    listening_ = false;
    if (serial_.isOpen()) {
        try { serial_.close(); } catch (...) {}
    }
    if (listening_thread_.joinable())
        listening_thread_.join();
    is_open_ = false;
}

/* ── isOpen ───────────────────────────────────────────────────────── */
bool MakcuConnection::isOpen() const { return is_open_; }

/* ── write / read (unused in binary protocol) ─────────────────────── */
void MakcuConnection::write(const std::string&) {}
std::string MakcuConnection::read() { return {}; }

/* ── Button helpers ───────────────────────────────────────────────── */
uint8_t MakcuConnection::buttonToBit(int button) const
{
    if (button < 0 || button > 4) return 0;
    return static_cast<uint8_t>(1U << button);
}

void MakcuConnection::updateTrackingFields()
{
    left_active   = (button_mask_ & 0x01) != 0;
    right_active  = (button_mask_ & 0x02) != 0;
    middle_active = (button_mask_ & 0x04) != 0;
    side1_active  = (button_mask_ & 0x08) != 0;
    side2_active  = (button_mask_ & 0x10) != 0;

    shooting_active   = left_active;
    zooming_active    = right_active;
    triggerbot_active = side1_active;
    aiming_active     = side2_active;

    shooting.store(shooting_active);
    zooming.store(zooming_active);
    triggerbot_button.store(triggerbot_active);
    aiming.store(aiming_active);

    logMakcuButtons("tracking", *this, static_cast<int>(button_mask_));
}

/* ── Binary frame sender ──────────────────────────────────────────── */
void MakcuConnection::sendMouseFrame(int16_t dx, int16_t dy,
                                     uint8_t buttons, int8_t wheel, int8_t hwheel)
{
    if (!is_open_) return;

    uint8_t frame[10];
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

    {
        static auto lastLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(1)) {
            lastLog = now;
            printf("[Makcu::sendFrame] SOF=%02X OP=%02X dx=%d dy=%d btns=%02X whl=%d hwhl=%d cs=%02X\n",
                   frame[0], frame[1], dx, dy, buttons, wheel, hwheel, csum);
        }
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    try {
        serial_.write(frame, 10);
    }
    catch (...) {
        is_open_ = false;
    }
}

/* ── High-level mouse operations ──────────────────────────────────── */
void MakcuConnection::move(int x, int y, uint8_t buttons, int8_t wheel, int8_t hwheel)
{
    uint8_t merged_btns = buttons | button_mask_;
    {
        static auto lastLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(1)) {
            lastLog = now;
            printf("[Makcu::move] dx=%d dy=%d btn_gui=0x%02X btn_human=0x%02X whl=%d\n",
                   x, y, button_mask_, buttons, wheel);
        }
    }
    sendMouseFrame(static_cast<int16_t>(x), static_cast<int16_t>(y), merged_btns, wheel, hwheel);
}

void MakcuConnection::press(int button)
{
    if (!is_open_) return;
    button_mask_ |= buttonToBit(button);
    updateTrackingFields();
    sendMouseFrame(0, 0, button_mask_);
}

void MakcuConnection::release(int button)
{
    if (!is_open_) return;
    button_mask_ &= ~buttonToBit(button);
    updateTrackingFields();
    sendMouseFrame(0, 0, button_mask_);
}

void MakcuConnection::click(int button)
{
    press(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    release(button);
}

/* ── Boot / reboot (no-op for STM32) ──────────────────────────────── */
void MakcuConnection::start_boot() {}
void MakcuConnection::reboot()     {}
void MakcuConnection::send_stop()  {}

/* ── Status response listener ─────────────────────────────────────── */
void MakcuConnection::startListening()
{
    listening_ = true;
    if (listening_thread_.joinable())
        listening_thread_.join();

    listening_thread_ = std::thread(&MakcuConnection::listeningThreadFunc, this);
}

void MakcuConnection::listeningThreadFunc()
{
    // Parse 3-byte status frames: [0xBB][status][0xBB ^ status]
    enum class State { WaitSOF, WaitStatus, WaitCS };
    State state = State::WaitSOF;
    uint8_t rx_status = 0;

    while (listening_ && is_open_) {
        try {
            if (!serial_.available()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            uint8_t b = 0;
            serial_.read(&b, 1);

            switch (state) {
            case State::WaitSOF:
                if (b == RESP_SOF)
                    state = State::WaitStatus;
                break;

            case State::WaitStatus:
                rx_status = b;
                state = State::WaitCS;
                break;

            case State::WaitCS:
                if (b == (RESP_SOF ^ rx_status)) {
                    // Valid firmware status — bits:
                    //   0 = frame XOR ok,  1 = buf pushed, 2 = buf dropped
                    //   3 = USB send ok,   4 = USB busy,   5 = USB fail
                    //   6 = XOR error,     7 = UART overrun
                    {
                        ALOG("makcu: status=0x%02X ok=%d pushed=%d dropped=%d usb_ok=%d busy=%d fail=%d xor_err=%d overrun=%d",
                             rx_status,
                             (rx_status & 0x01) != 0, (rx_status & 0x02) != 0, (rx_status & 0x04) != 0,
                             (rx_status & 0x08) != 0, (rx_status & 0x10) != 0, (rx_status & 0x20) != 0,
                             (rx_status & 0x40) != 0, (rx_status & 0x80) != 0);
                        static auto lastLog = std::chrono::steady_clock::now();
                        auto now = std::chrono::steady_clock::now();
                        if (now - lastLog >= std::chrono::seconds(1)) {
                            lastLog = now;
                            printf("[Makcu::response] status=0x%02X"
                                   " (XOR_ok=%d buf_pushed=%d buf_dropped=%d"
                                   " USB_send_ok=%d USB_busy=%d USB_fail=%d"
                                   " XOR_err=%d UART_overrun=%d)\n",
                                   rx_status,
                                   (rx_status & 0x01) ? 1 : 0,
                                   (rx_status & 0x02) ? 1 : 0,
                                   (rx_status & 0x04) ? 1 : 0,
                                   (rx_status & 0x08) ? 1 : 0,
                                   (rx_status & 0x10) ? 1 : 0,
                                   (rx_status & 0x20) ? 1 : 0,
                                   (rx_status & 0x40) ? 1 : 0,
                                   (rx_status & 0x80) ? 1 : 0);
                        }
                    }
                    if (rx_status & 0xC0) {
                        std::cerr << "[Makcu] FW error: 0x"
                                  << std::hex << static_cast<int>(rx_status)
                                  << std::dec << '\n';
                    }
                }
                state = State::WaitSOF;
                break;
            }
        }
        catch (...) {
            is_open_ = false;
            break;
        }
    }
}

#endif
