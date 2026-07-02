#include "gpu_jpeg_codec.h"

#include <libgpujpeg/gpujpeg.h>
#include <libgpujpeg/gpujpeg_encoder.h>
#include <libgpujpeg/gpujpeg_decoder.h>
#include <libgpujpeg/gpujpeg_common.h>

#include <stdexcept>
#include <cstring>

GpuJpegCodec::GpuJpegCodec() = default;

GpuJpegCodec::~GpuJpegCodec() {
    shutdown();
}

bool GpuJpegCodec::initialize(int device_id) {
    if (m_initialized) {
        return true;
    }

    if (gpujpeg_init_device(device_id, 0) != 0) {
        return false;
    }

    m_encoder = gpujpeg_encoder_create(nullptr);
    if (!m_encoder) {
        return false;
    }

    m_decoder = gpujpeg_decoder_create(nullptr);
    if (!m_decoder) {
        gpujpeg_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void GpuJpegCodec::shutdown() {
    if (m_decoder) {
        gpujpeg_decoder_destroy(m_decoder);
        m_decoder = nullptr;
    }
    if (m_encoder) {
        gpujpeg_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }
    m_initialized = false;
}

std::vector<uint8_t> GpuJpegCodec::encode(const cv::Mat& bgra_image, int quality) {
    if (!m_initialized || !m_encoder) {
        throw std::runtime_error("GpuJpegCodec not initialized");
    }

    if (bgra_image.empty() || bgra_image.type() != CV_8UC4) {
        throw std::runtime_error("Invalid input image - must be non-empty BGRA CV_8UC4");
    }

    cv::Mat bgr_image;
    cv::cvtColor(bgra_image, bgr_image, cv::COLOR_BGRA2BGR);

    cv::Mat rgb_image;
    cv::cvtColor(bgr_image, rgb_image, cv::COLOR_BGR2RGB);

    struct gpujpeg_parameters param;
    gpujpeg_set_default_parameters(&param);
    param.quality = quality;

    struct gpujpeg_image_parameters param_image;
    gpujpeg_image_set_default_parameters(&param_image);
    param_image.width = rgb_image.cols;
    param_image.height = rgb_image.rows;
    param_image.color_space = GPUJPEG_RGB;
    param_image.pixel_format = GPUJPEG_444_U8_P012;

    gpujpeg_parameters_chroma_subsampling(&param, GPUJPEG_SUBSAMPLING_444);

    struct gpujpeg_encoder_input encoder_input;
    gpujpeg_encoder_input_set_image(&encoder_input, rgb_image.data);

    uint8_t* compressed_data = nullptr;
    size_t compressed_size = 0;

    if (gpujpeg_encoder_encode(m_encoder, &param, &param_image, &encoder_input,
                               &compressed_data, &compressed_size) != 0) {
        throw std::runtime_error("GPUJPEG encoding failed");
    }

    return std::vector<uint8_t>(compressed_data, compressed_data + compressed_size);
}

cv::Mat GpuJpegCodec::decode(const std::vector<uint8_t>& jpeg_data) {
    if (!m_initialized || !m_decoder) {
        throw std::runtime_error("GpuJpegCodec not initialized");
    }

    if (jpeg_data.empty()) {
        throw std::runtime_error("Empty JPEG data");
    }

    struct gpujpeg_decoder_output dec_output;
    gpujpeg_decoder_output_set_default(&dec_output);

    std::vector<uint8_t> jpeg_copy = jpeg_data;

    if (gpujpeg_decoder_decode(m_decoder, jpeg_copy.data(), jpeg_copy.size(), &dec_output) != 0) {
        throw std::runtime_error("GPUJPEG decoding failed");
    }

    int width = dec_output.param_image.width;
    int height = dec_output.param_image.height;

    cv::Mat decoded_image;

    if (dec_output.param_image.color_space == GPUJPEG_RGB &&
        dec_output.param_image.pixel_format == GPUJPEG_444_U8_P012) {
        decoded_image = cv::Mat(height, width, CV_8UC3, dec_output.data);
    } else {
        throw std::runtime_error("Unsupported decoded pixel format");
    }

    cv::Mat bgr_image;
    cv::cvtColor(decoded_image, bgr_image, cv::COLOR_RGB2BGR);

    cv::Mat bgra_image;
    cv::cvtColor(bgr_image, bgra_image, cv::COLOR_BGR2BGRA);

    return bgra_image.clone();
}
