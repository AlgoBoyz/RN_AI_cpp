#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>

#include "imgui/imgui.h"

#include "rn_ai_cpp.h"
#include "include/other_tools.h"
#include "overlay.h"
#ifdef USE_CUDA
#include "trt_monitor.h"
#endif

std::string prev_backend = config.backend;
float prev_confidence_threshold = config.confidence_threshold;
float prev_nms_threshold = config.nms_threshold;
bool prev_adaptive_nms = config.adaptive_nms;
int prev_max_detections = config.max_detections;

static bool wasExporting = false;

void draw_ai()
{
#ifdef USE_CUDA
    if (gIsTrtExporting)
    {
        ImGui::OpenPopup("TensorRT Export Progress");
    }

    if (ImGui::BeginPopupModal("TensorRT Export Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        std::lock_guard<std::mutex> lock(gProgressMutex);
        if (!gProgressPhases.empty())
        {
            for (auto& [name, phase] : gProgressPhases)
            {
                float percent = phase.max > 0 ? phase.current / float(phase.max) : 0.0f;
                ImGui::Text("%s: %d/%d", name.c_str(), phase.current, phase.max);
                ImGui::ProgressBar(percent, ImVec2(300, 0));
            }
        }
        else
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::Text("Engine export in progress, please wait...");
        ImGui::EndPopup();
    }
#endif
    std::vector<std::string> availableModels = getAvailableModels();
    if (availableModels.empty())
    {
        ImGui::Text("No models available in the 'models' folder.");
    }
    else
    {
        int currentModelIndex = 0;
        auto it = std::find(availableModels.begin(), availableModels.end(), config.ai_model);

        if (it != availableModels.end())
        {
            currentModelIndex = static_cast<int>(std::distance(availableModels.begin(), it));
        }

        std::vector<const char*> modelsItems;
        modelsItems.reserve(availableModels.size());

        for (const auto& modelName : availableModels)
        {
            modelsItems.push_back(modelName.c_str());
        }

        if (ImGui::Combo("Model", &currentModelIndex, modelsItems.data(), static_cast<int>(modelsItems.size())))
        {
            if (config.ai_model != availableModels[currentModelIndex])
            {
                config.ai_model = availableModels[currentModelIndex];
                config.saveConfig();
                detector_model_changed.store(true);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择 AI 检测模型文件 (.onnx/.engine)，从 models/ 目录加载。推荐使用当前版本训练的最优模型。");
    }

    ImGui::Separator();

#ifdef USE_CUDA
    std::vector<std::string> backendOptions = { "TRT", "DML", "COLOR" };
    std::vector<const char*> backendItems = { "TensorRT (CUDA)", "DirectML (CPU/GPU)" , "Color Detection (HSV)" };

    int currentBackendIndex = 0;
    if (config.backend == "DML") currentBackendIndex = 1;
    else if (config.backend == "COLOR") currentBackendIndex = 2;

    if (ImGui::Combo("Backend", &currentBackendIndex, backendItems.data(), static_cast<int>(backendItems.size())))
    {
        std::string newBackend = backendOptions[currentBackendIndex];
        if (config.backend != newBackend)
        {
            config.backend = newBackend;
            config.saveConfig();
            detector_model_changed.store(true);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择推理后端。TensorRT 性能最高但需 NVIDIA GPU；DirectML 通用兼容；COLOR 为 HSV 颜色检测模式。默认 TRT。");

    ImGui::Separator();
#endif

    std::vector<std::string> postprocessOptions = { "yolo8", "yolo9", "yolo10", "yolo11", "yolo12" };
    std::vector<const char*> postprocessItems;
    for (const auto& option : postprocessOptions)
    {
        postprocessItems.push_back(option.c_str());
    }

    int currentPostprocessIndex = 0;
    for (size_t i = 0; i < postprocessOptions.size(); ++i)
    {
        if (postprocessOptions[i] == config.postprocess)
        {
            currentPostprocessIndex = static_cast<int>(i);
            break;
        }
    }

    if (ImGui::Combo("Postprocess", &currentPostprocessIndex, postprocessItems.data(), static_cast<int>(postprocessItems.size())))
    {
        config.postprocess = postprocessOptions[currentPostprocessIndex];
        config.saveConfig();
        detector_model_changed.store(true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择 YOLO 后处理版本，需与模型导出时使用的版本一致。默认 yolo10。");
    if (ImGui::SliderInt("Batch Size", &config.batch_size, 1, 8))
    {
        config.saveConfig();
        detector_model_changed.store(true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 推理批处理大小。调大: 吞吐量提升但延迟增加，仅在多目标场景有意义。调小: 降低延迟。默认 1, 推荐 1。");

    ImGui::Separator();
    ImGui::SliderFloat("Confidence Threshold", &config.confidence_threshold, 0.01f, 1.00f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测置信度阈值，低于此值的检测结果将被丢弃。调大: 减少误检但可能漏检。调小: 提高召回率但误检增多。默认 0.15, 推荐 0.10~0.25。");
    ImGui::SliderFloat("IOU Threshold (NMS)", &config.nms_threshold, 0.01f, 1.00f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 非极大抑制 (NMS) 的交并比阈值，用于去除重叠框。调大: 保留更多检测框，重叠目标不易丢失。调小: 更激进地去重。默认 0.50, 推荐 0.45~0.65。");
    ImGui::Checkbox("Adaptive NMS", &config.adaptive_nms);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用自适应 NMS，根据目标密度动态调整 NMS 阈值。密集场景自动降低阈值去重，稀疏场景自动提高阈值保留目标。默认开启, 推荐开启。");
    static int max_detections_cache = 20;
    if (config.max_detections > 0)
        max_detections_cache = config.max_detections;
    bool limit_max = config.max_detections > 0;
    if (ImGui::Checkbox("Limit Max Detections", &limit_max))
    {
        if (limit_max)
            config.max_detections = std::max(1, max_detections_cache);
        else
            config.max_detections = 0;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 限制最大检测数量。开启后仅保留置信度最高的 N 个目标。关闭则不限制。默认开启, 推荐开启以控制性能。");
    if (limit_max)
        ImGui::SliderInt("Max Detections", &config.max_detections, 1, 100);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 最大保留检测数量。调大: 可检测更多目标但排序开销增加。调小: 性能更好但可能遗漏远处目标。默认 20, 推荐 10~50。");

    if (ImGui::Checkbox("Fixed model size", &config.fixed_input_size))
    {
        capture_method_changed.store(true);
        config.saveConfig();
        detector_model_changed.store(true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 固定模型输入尺寸，将输入分辨率锁定为模型原生尺寸而非动态适配。通常可提高检测精度但可能影响性能。默认关闭, 推荐关闭。");

    if (prev_confidence_threshold != config.confidence_threshold ||
        prev_nms_threshold != config.nms_threshold ||
        prev_adaptive_nms != config.adaptive_nms ||
        prev_max_detections != config.max_detections)
    {
        prev_nms_threshold = config.nms_threshold;
        prev_confidence_threshold = config.confidence_threshold;
        prev_adaptive_nms = config.adaptive_nms;
        prev_max_detections = config.max_detections;
        config.saveConfig();
    }

    if (prev_backend != config.backend)
    {
        prev_backend = config.backend;
        detector_model_changed.store(true);
        config.saveConfig();
    }

    bool small_changed = false;
    ImGui::Separator();
    ImGui::Text("Small Target Enhancement");
    if (ImGui::Checkbox("Enable Small Target Enhancement", &config.small_target_enhancement_enabled))
        small_changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用小目标增强，对面积较小的检测框自动加权提分。解决远距离/小目标置信度偏低的问题。默认开启, 推荐开启。");
    if (config.small_target_enhancement_enabled)
    {
        if (ImGui::SliderFloat("Small Target Boost", &config.small_target_boost_factor, 0.0f, 5.0f, "%.2f"))
            small_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 小目标置信度放大系数。调大: 小目标更容易被锁定。调小: 减弱加成，减少误锁。默认 0.1, 推荐 0.05~0.5。");
        if (ImGui::SliderFloat("Small Target Threshold", &config.small_target_threshold, 0.0f, 0.20f, "%.3f"))
            small_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 小目标尺寸阈值（归一化面积），低于此值触发完整 Boost 加成。默认 0.02, 推荐 0.01~0.05。");
        if (ImGui::SliderFloat("Medium Target Threshold", &config.small_target_medium_threshold, 0.0f, 0.20f, "%.3f"))
            small_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 中等目标尺寸阈值上限，介于小和中等之间的目标获得线性递减的加成。默认 0.05, 推荐 0.03~0.10。");
        if (ImGui::SliderFloat("Medium Target Boost", &config.small_target_medium_boost, 0.0f, 5.0f, "%.2f"))
            small_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 中等目标置信度放大系数（加成力度小于小目标）。默认 1.5, 推荐 1.0~2.0。");
        if (ImGui::Checkbox("Small Target Smoothing", &config.small_target_smooth_enabled))
            small_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用小目标置信度时序平滑，在多帧间取指数移动平均以减少抖动。默认开启, 推荐开启。");
        if (config.small_target_smooth_enabled)
        {
            if (ImGui::SliderInt("Smooth Frames", &config.small_target_smooth_frames, 1, 10))
                small_changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 平滑窗口帧数。调大: 结果更稳定但响应变慢。调小: 响应更快但抖动增加。默认 2, 推荐 2~5。");
        }
    }
    if (small_changed)
        config.saveConfig();

    if (config.backend == "COLOR")
    {
        ImGui::Separator();
        ImGui::Text("Color Detection (HSV)");
        ImGui::Separator();

        // Color target selection
        std::vector<const char*> colorItems;
        for (const auto& cr : config.color_ranges) {
            colorItems.push_back(cr.name.c_str());
        }

        static int currentColorIndex = 0;
        for (size_t i = 0; i < config.color_ranges.size(); ++i) {
            if (config.color_ranges[i].name == config.color_target) {
                currentColorIndex = static_cast<int>(i);
                break;
            }
        }

        if (!colorItems.empty()) {
            if (ImGui::Combo("Target Color", &currentColorIndex, colorItems.data(), (int)colorItems.size())) {
                config.color_target = config.color_ranges[currentColorIndex].name;
                config.saveConfig();
                detector_model_changed.store(true);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择要检测的目标颜色（HSV 色域），可在 config.ini 的 [Colors] 节自定义。默认 Yellow。");

        if (ImGui::SliderInt("Erode Iterations", &config.color_erode_iter, 0, 5)) {
            config.saveConfig();
            detector_model_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 形态学腐蚀迭代次数，用于去除 HSV 掩码中的噪点。调大: 滤波更强但目标轮廓缩小。默认 1, 推荐 0~2。");
        if (ImGui::SliderInt("Dilate Iterations", &config.color_dilate_iter, 0, 5)) {
            config.saveConfig();
            detector_model_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 形态学膨胀迭代次数，用于填补 HSV 掩码中的空洞。调大: 轮廓更完整但可能粘连邻近目标。默认 2, 推荐 1~3。");
        if (ImGui::SliderInt("Min Area", &config.color_min_area, 10, 1000)) {
            config.saveConfig();
            detector_model_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 最小轮廓面积（像素），小于此值的目标被过滤。调大: 减少小噪点误检。调小: 保留更小的有效区域。默认 50, 推荐 30~200。");
    }
}
