#pragma once

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <string>
#include <memory>
#include <cstdint>

/**
 * CNN digit classifier using ONNX Runtime.
 * Input: 28x28 grayscale normalized to [-1, 1]
 * Output: digit 0-9
 */
class DigitClassifier {
public:
    static constexpr int CNN_SIZE = 28;

    explicit DigitClassifier(const std::wstring& model_path);

    bool isLoaded() const { return loaded_; }

    // Classify a single grayscale digit ROI (any size, resized internally).
    // Returns 0-9, or -1 on failure.
    int classify(const cv::Mat& gray_roi);

    // Detect 2-digit ammo number from a BGR region image.
    // Crops tens and ones ROIs, classifies each, combines to integer.
    // blank_stddev_threshold: if tens ROI stddev is below this, treat as blank.
    int detect(const cv::Mat& region1_img,
               int tens_x, int tens_y, int tens_w, int tens_h,
               int ones_x, int ones_y, int ones_w, int ones_h,
               float blank_stddev_threshold = 10.0f);

private:
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
    bool loaded_ = false;
};
