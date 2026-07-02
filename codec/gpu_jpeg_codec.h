#pragma once

#include <vector>
#include <cstdint>
#include <opencv2/opencv.hpp>

struct gpujpeg_encoder;
struct gpujpeg_decoder;

class GpuJpegCodec {
public:
    GpuJpegCodec();
    ~GpuJpegCodec();

    GpuJpegCodec(const GpuJpegCodec&) = delete;
    GpuJpegCodec& operator=(const GpuJpegCodec&) = delete;

    bool initialize(int device_id = 0);
    void shutdown();

    std::vector<uint8_t> encode(const cv::Mat& bgra_image, int quality = 90);
    cv::Mat decode(const std::vector<uint8_t>& jpeg_data);

    bool isInitialized() const { return m_initialized; }

private:
    gpujpeg_encoder* m_encoder = nullptr;
    gpujpeg_decoder* m_decoder = nullptr;
    bool m_initialized = false;
};
