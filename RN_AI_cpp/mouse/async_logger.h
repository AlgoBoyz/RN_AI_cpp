#pragma once
#include <cstdio>
#include <cstdarg>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <Windows.h>

class AsyncLogger {
public:
    static AsyncLogger& instance() {
        static AsyncLogger a;
        return a;
    }

    void init() {
        const char* p = getenv("USERPROFILE");
        if (!p) { inited_ = false; printf("[AsyncLogger] FAIL no USERPROFILE\n"); return; }
        char path[MAX_PATH];
        sprintf_s(path, "%s\\rn_ai", p);
        CreateDirectoryA(path, nullptr);
        sprintf_s(path, "%s\\rn_ai\\fusion.log", p);
        fopen_s(&fp_, path, "w");
        if (!fp_) { inited_ = false; printf("[AsyncLogger] FAIL fopen %s\n", path); return; }
        printf("[AsyncLogger] init ok -> %s\n", path);
        inited_ = true; running_ = true;
        worker_ = std::thread([this]() {
            while (running_ || !q_.empty()) {
                std::string s;
                { std::unique_lock l(m_);
                  cv_.wait_for(l, std::chrono::milliseconds(200),
                    [this]{ return !q_.empty() || !running_; });
                  if (q_.empty()) continue;
                  s = std::move(q_.front()); q_.pop(); }
                if (fp_) { fputs(s.c_str(), fp_); fputc('\n', fp_); fflush(fp_); }
            }
            if (fp_) fclose(fp_);
        });
    }

    void log(const char* fmt, ...) {
        if (!inited_) return;
        char buf[1024];
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        struct tm tb; localtime_s(&tb, &now);
        int off = sprintf_s(buf, "%02d:%02d:%02d ", tb.tm_hour, tb.tm_min, tb.tm_sec);
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf + off, sizeof(buf) - off - 1, fmt, ap);
        va_end(ap);
        { std::lock_guard l(m_);
          if (q_.size() >= 512) q_.pop();
          q_.emplace(buf); }
        cv_.notify_one();
    }

    ~AsyncLogger() { running_ = false; cv_.notify_one(); if (worker_.joinable()) worker_.join(); }

private:
    AsyncLogger() = default;
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    FILE* fp_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false}, inited_{false};
    std::queue<std::string> q_;
    std::mutex m_;
    std::condition_variable cv_;
};

#define ALOG(fmt, ...) AsyncLogger::instance().log(fmt, ##__VA_ARGS__)
