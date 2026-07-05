#include "digit_classifier.h"
#include "../mouse/async_logger.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

DigitClassifier::DigitClassifier(const std::wstring& model_path)
    : env_(ORT_LOGGING_LEVEL_WARNING, "DigitClassifier")
{
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        input_name_  = session_->GetInputNameAllocated(0, alloc).get();
        output_name_ = session_->GetOutputNameAllocated(0, alloc).get();
        loaded_ = true;
        printf("[DigitClassifier] Loaded, input='%s' output='%s'\n",
               input_name_.c_str(), output_name_.c_str());
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "[DigitClassifier] Failed to load model: %s\n", e.what());
        loaded_ = false;
    }
}

int DigitClassifier::classify(const cv::Mat& gray_roi)
{
    if (!loaded_ || gray_roi.empty()) return -1;

    // Resize to 28x28, normalize [0,255] to [-1,1]
    cv::Mat resized;
    cv::resize(gray_roi, resized, cv::Size(CNN_SIZE, CNN_SIZE), 0, 0, cv::INTER_AREA);
    resized.convertTo(resized, CV_32F, 1.0 / 127.5, -1.0);

    // Prepare tensor
    std::vector<float> input_data(CNN_SIZE * CNN_SIZE);
    memcpy(input_data.data(), resized.ptr<float>(0), input_data.size() * sizeof(float));

    std::vector<int64_t> shape = { 1, 1, CNN_SIZE, CNN_SIZE };
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(mem, input_data.data(),
                     input_data.size(), shape.data(), shape.size());

    // Inference
    const char* in  = input_name_.c_str();
    const char* out = output_name_.c_str();
    auto outputs = session_->Run(Ort::RunOptions{nullptr}, &in, &tensor, 1, &out, 1);

    // Argmax
    float* probs = outputs[0].GetTensorMutableData<float>();
    int best = 0;
    for (int i = 1; i < 10; i++)
        if (probs[i] > probs[best]) best = i;

    static int classify_log = 0;
    if (classify_log++ % 300 == 0) {
        int idx[10] = {0,1,2,3,4,5,6,7,8,9};
        std::sort(idx, idx+10, [&](int a, int b){ return probs[a] > probs[b]; });
        ALOG("[Classify] -> %d (top3: %d=%.2f %d=%.2f %d=%.2f)",
             best, idx[0], probs[idx[0]], idx[1], probs[idx[1]], idx[2], probs[idx[2]]);
    }
    return best;
}

int DigitClassifier::detect(const cv::Mat& region1_img,
                             int tens_x, int tens_y, int tens_w, int tens_h,
                             int ones_x, int ones_y, int ones_w, int ones_h,
                             float blank_stddev_threshold)
{
    if (!loaded_ || region1_img.empty()) return -1;
    if (region1_img.cols < ones_x + ones_w || region1_img.rows < ones_y + ones_h)
        return -1;

    cv::Mat gray;
    cv::cvtColor(region1_img, gray, cv::COLOR_BGR2GRAY);

    // Ones digit (always present)
    cv::Mat ones_roi = gray(cv::Rect(ones_x, ones_y, ones_w, ones_h));
    int ones = classify(ones_roi);
    if (ones < 0) return -1;

    // Tens digit (check if not blank)
    int tens = -1;
    if (region1_img.cols >= tens_x + tens_w && region1_img.rows >= tens_y + tens_h) {
        cv::Mat tens_roi = gray(cv::Rect(tens_x, tens_y, tens_w, tens_h));
        cv::Scalar mean, stddev;
        cv::meanStdDev(tens_roi, mean, stddev);
        if (stddev[0] > blank_stddev_threshold) {
            tens = classify(tens_roi);
        }
    }

    static int detect_log = 0;
    if (detect_log++ % 300 == 0)
        ALOG("[DigitDetect] ones=%d tens=%d -> %d (img=%dx%d)",
               ones, tens, (tens > 0 ? tens * 10 + ones : ones),
               region1_img.cols, region1_img.rows);

    if (tens > 0) return tens * 10 + ones;
    else          return ones;
}
