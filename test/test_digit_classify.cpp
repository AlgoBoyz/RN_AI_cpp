/**
 * test_digit_classify.cpp — 离线数字识别，按预测结果分文件夹
 *
 * 读取 debug 目录中的弹药区域图像，裁剪十分位/个分位，
 * 用 ONNX CNN 模型分类，按预测的数字存入对应文件夹。
 * 人工浏览各文件夹即可验证模型准确率。
 *
 * 输出结构:
 *   test_result/
 *     0/  帧001_ammo_32_tens.png  ← CNN 判为 0
 *     1/  帧001_ammo_32_ones.png  ← CNN 判为 1
 *     ...
 *     9/
 *
 * 编译:
 *   cl /std:c++17 /EHsc /utf-8 /I. /Ivendor\opencv\install\include
 *       /Ipackages\Microsoft.ML.OnnxRuntime.DirectML.1.22.0\build\native\include
 *       test\test_digit_classify.cpp
 *       ws2_32.lib vendor\opencv\install\x64\vc17\lib\opencv_world4100.lib
 *       packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.0\runtimes\win-x64\native\onnxruntime.lib
 *       /Fe:test_digit_classify.exe
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ── ROI 坐标（相对于 region 1 图像） ────────────────────────────────────
constexpr int TENS_X = 312, TENS_Y = 13, TENS_W = 37, TENS_H = 47;
constexpr int ONES_X = 351, ONES_Y = 13, ONES_W = 34, ONES_H = 48;
constexpr int CNN_SIZE = 28;

// ── ONNX 分类器 ─────────────────────────────────────────────────────────
class DigitClassifier {
public:
    DigitClassifier() {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(env_, L"models/digit_classifier.onnx", opts);

        Ort::AllocatorWithDefaultOptions alloc;
        input_name_ = session_->GetInputNameAllocated(0, alloc).get();
        output_name_ = session_->GetOutputNameAllocated(0, alloc).get();
        printf("[CNN] Loaded, input='%s' output='%s'\n", input_name_.c_str(), output_name_.c_str());
    }

    int classify(const cv::Mat& gray_roi) {
        if (gray_roi.empty()) return -1;

        cv::Mat resized;
        cv::resize(gray_roi, resized, cv::Size(CNN_SIZE, CNN_SIZE), 0, 0, cv::INTER_AREA);
        resized.convertTo(resized, CV_32F, 1.0 / 127.5, -1.0);

        std::vector<float> input_data(CNN_SIZE * CNN_SIZE);
        memcpy(input_data.data(), resized.ptr<float>(0), input_data.size() * sizeof(float));

        std::vector<int64_t> shape = { 1, 1, CNN_SIZE, CNN_SIZE };
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(mem, input_data.data(),
                         input_data.size(), shape.data(), shape.size());

        const char* in = input_name_.c_str();
        const char* out = output_name_.c_str();
        auto outputs = session_->Run(Ort::RunOptions{nullptr}, &in, &tensor, 1, &out, 1);
        float* probs = outputs[0].GetTensorMutableData<float>();

        int best = 0;
        for (int i = 1; i < 10; i++)
            if (probs[i] > probs[best]) best = i;
        return best;
    }

private:
    Ort::Env env_{ ORT_LOGGING_LEVEL_WARNING, "test" };
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
};

// ── 主函数 ───────────────────────────────────────────────────────────────
int main() {
    const char* profile = getenv("USERPROFILE");
    if (!profile) { fprintf(stderr, "USERPROFILE not set\n"); return 1; }

    char input_dir[MAX_PATH];
    sprintf_s(input_dir, "%s\\rn_ai\\debug", profile);
    if (!fs::is_directory(input_dir)) {
        fprintf(stderr, "Input dir not found: %s\n", input_dir);
        return 1;
    }

    const std::string out_dir = "test_result";
    std::error_code ec;
    fs::remove_all(out_dir, ec);
    // 预建 0~9 文件夹
    for (int d = 0; d < 10; d++)
        fs::create_directories(fs::path(out_dir) / std::to_string(d));

    DigitClassifier cnn;

    // 收集所有 _ammo_ 图片
    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator(input_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find("_ammo_") != std::string::npos && entry.path().extension() == ".png")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    printf("\n%zu images to process\n\n", files.size());

    int saved_tens = 0, saved_ones = 0, skipped = 0;

    for (const auto& f : files) {
        cv::Mat img = cv::imread(f.string(), cv::IMREAD_COLOR);
        if (img.empty() || img.cols < ONES_X + ONES_W || img.rows < ONES_Y + ONES_H) {
            skipped++; continue;
        }

        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        std::string src_stem = f.stem().string();  // "frame_00005_ammo_25"

        // ── 个分位 ──
        cv::Mat ones_roi = gray(cv::Rect(ONES_X, ONES_Y, ONES_W, ONES_H));
        int pred_ones = cnn.classify(ones_roi);
        cv::Mat ones_28;
        cv::resize(ones_roi, ones_28, cv::Size(CNN_SIZE, CNN_SIZE), 0, 0, cv::INTER_AREA);
        cv::imwrite((fs::path(out_dir) / std::to_string(pred_ones) / (src_stem + "_ones.png")).string(), ones_28);
        saved_ones++;

        // ── 十分位 ──
        if (img.cols >= TENS_X + TENS_W && img.rows >= TENS_Y + TENS_H) {
            cv::Mat tens_roi = gray(cv::Rect(TENS_X, TENS_Y, TENS_W, TENS_H));
            // 空白检测：方差太低说明没有数字
            cv::Scalar mean, stddev;
            cv::meanStdDev(tens_roi, mean, stddev);
            if (stddev[0] > 10.0) {
                int pred_tens = cnn.classify(tens_roi);
                cv::Mat tens_28;
                cv::resize(tens_roi, tens_28, cv::Size(CNN_SIZE, CNN_SIZE), 0, 0, cv::INTER_AREA);
                cv::imwrite((fs::path(out_dir) / std::to_string(pred_tens) / (src_stem + "_tens.png")).string(), tens_28);
                saved_tens++;
            }
        }
    }

    // ── 统计每个文件夹数量 ──
    printf("Done. %d skipped, %d ones saved, %d tens saved.\n\n", skipped, saved_ones, saved_tens);
    for (int d = 0; d < 10; d++) {
        auto dir = fs::path(out_dir) / std::to_string(d);
        int n = 0;
        for (auto& e : fs::directory_iterator(dir))
            if (e.is_regular_file()) n++;
        printf("  [%d] %4d 张\n", d, n);
    }
    printf("\n打开 test_result/ 各文件夹人工验证即可。\n");

    return 0;
}
