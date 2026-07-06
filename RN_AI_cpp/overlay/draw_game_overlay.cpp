#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <commdlg.h>
#include <string>
#include <cstring>
#include <algorithm>

#include "imgui/imgui.h"
#include "config.h"
#include "rn_ai_cpp.h"
#include "capture.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"

std::string g_iconLastError;

void draw_game_overlay_settings()
{
    if (OverlayUI::BeginSection("General", "game_overlay_section_general"))
    {
        if (ImGui::Checkbox("Enable", &config.game_overlay_enabled))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开启/关闭游戏画面叠加层窗口, 关闭后不渲染任何叠加内容。默认关闭, 推荐开启以查看检测框和预测信息。");

        ImGui::SliderInt("Overlay Max FPS (0 = uncapped)", &config.game_overlay_max_fps, 0, 256);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 限制叠加层窗口的最大渲染帧率, 0表示不限制。调大叠加层更流畅但GPU占用更高, 调小可降低GPU占用但有卡顿感。默认0(不限制), 推荐60(流畅且省资源)。");

        if (ImGui::Checkbox("Draw Detection Boxes", &config.game_overlay_draw_boxes))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在叠加层上绘制目标检测包围框。默认开启, 推荐开启以查看检测结果。");

        if (ImGui::Checkbox("Draw Future Positions", &config.game_overlay_draw_future))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在叠加层上绘制预测的未来位置点(基于速度外推)。默认开启, 推荐开启以观察轨迹预测效果。");

        if (ImGui::Checkbox("Draw Wind Debug Tail", &config.game_overlay_draw_wind_tail))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 绘制Wind鼠标算法的调试轨迹尾巴, 用于观察鼠标移动路径。默认开启, 推荐调试瞄准算法时开启。");

        if (ImGui::Checkbox("Show Target Correction", &config.game_overlay_show_target_correction))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 显示瞄准修正信息(瞄准点偏移方向和距离)。默认开启, 推荐调试瞄准偏移时开启。");

        if (ImGui::Checkbox("Show FPS Counter", &config.game_overlay_show_fps_counter))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在叠加层上显示当前FPS帧率。默认开启, 推荐开启以监控性能。");

        if (ImGui::Checkbox("Show Latency", &config.game_overlay_show_latency))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在叠加层上显示端到端延迟信息(截图->推理->渲染)。默认开启, 推荐开启以监控延迟。");

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Box Color", "game_overlay_section_box_color"))
    {
        bool colorChanged = false;

        ImGui::SliderInt("A##go_box_a", &config.game_overlay_box_a, 0, 255);
        colorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测框颜色的透明度(Alpha通道), 0=完全透明, 255=完全不透明。调大检测框更不透明更显眼, 调小检测框更透明不遮挡目标。默认255, 推荐200-255。");

        ImGui::SliderInt("R##go_box_r", &config.game_overlay_box_r, 0, 255);
        colorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测框颜色的红色分量。调大偏红, 调小偏青。默认0(纯绿), 推荐0。");

        ImGui::SliderInt("G##go_box_g", &config.game_overlay_box_g, 0, 255);
        colorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测框颜色的绿色分量。调大偏绿, 调小偏品红。默认255(纯绿), 推荐200-255(亮绿色醒目)。");

        ImGui::SliderInt("B##go_box_b", &config.game_overlay_box_b, 0, 255);
        colorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测框颜色的蓝色分量。调大偏蓝, 调小偏黄。默认0(纯绿无蓝), 推荐0-50。");

        ImGui::SliderFloat("Box Thickness", &config.game_overlay_box_thickness, 0.5f, 10.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 检测框线条粗细(像素)。调大框线更粗更显眼但可能遮挡目标, 调小框线更精细不遮挡画面。默认2.0, 推荐1.5-3.0。");

        if (colorChanged)
            config.clampGameOverlayColor();

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Capture Frame", "game_overlay_section_capture_frame"))
    {
        if (ImGui::Checkbox("Draw Capture Frame", &config.game_overlay_draw_frame))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在叠加层上绘制截图/捕获区域的边框, 用于确认截图范围是否正确。默认开启, 推荐调试截图区域时开启。");

        bool frameColorChanged = false;

        ImGui::SliderInt("A##go_frame_a", &config.game_overlay_frame_a, 0, 255);
        frameColorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 截图区域边框的透明度(Alpha通道)。调大边框更不透明, 调小边框更透明。默认180, 推荐150-220(半透明不遮挡画面)。");

        ImGui::SliderInt("R##go_frame_r", &config.game_overlay_frame_r, 0, 255);
        frameColorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 截图区域边框的红色分量。调大偏红, 调小偏青。默认255(白色偏暖), 推荐255。");

        ImGui::SliderInt("G##go_frame_g", &config.game_overlay_frame_g, 0, 255);
        frameColorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 截图区域边框的绿色分量。调大偏绿, 调小偏品红。默认255(白色), 推荐255。");

        ImGui::SliderInt("B##go_frame_b", &config.game_overlay_frame_b, 0, 255);
        frameColorChanged |= ImGui::IsItemEdited();
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 截图区域边框的蓝色分量。调大偏蓝, 调小偏黄。默认255(白色), 推荐255。");

        ImGui::SliderFloat("Frame Thickness", &config.game_overlay_frame_thickness, 0.5f, 10.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 截图区域边框线条粗细(像素)。调大边框更醒目, 调小边框更精细。默认1.5, 推荐1.0-3.0。");

        if (frameColorChanged)
            config.clampGameOverlayColor();

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Future Point Style", "game_overlay_section_future_style"))
    {
        ImGui::SliderFloat("Point Radius", &config.game_overlay_future_point_radius, 1.0f, 20.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 未来位置预测点的半径大小(像素)。调大点更大更醒目, 调小点更精细不遮挡画面。默认5.0, 推荐3.0-8.0。");

        ImGui::SliderFloat("Point Step Alpha Falloff", &config.game_overlay_future_alpha_falloff, 0.1f, 5.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 每个预测步长透明度的衰减系数, 控制远处预测点的透明度。调大远处点更快消失(只看到近处), 调小所有预测步长透明度更均匀。默认1.0, 推荐0.5-2.0。");

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Icon Overlay", "game_overlay_section_icon"))
    {
        if (ImGui::Checkbox("Enable Icon Overlay", &config.game_overlay_icon_enabled))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开启/关闭图标叠加层, 可在检测到的目标上显示自定义图标图片。默认关闭, 推荐需要自定义标记时开启。");

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::BeginDisabled();
        }

        static bool pathInit = false;
        static char iconPathBuf[512];

        if (!pathInit)
        {
            pathInit = true;
            memset(iconPathBuf, 0, sizeof(iconPathBuf));
            std::string p = config.game_overlay_icon_path;
            if (p.size() >= sizeof(iconPathBuf)) p = p.substr(0, sizeof(iconPathBuf) - 1);
            memcpy(iconPathBuf, p.c_str(), p.size());
        }

        if (ImGui::InputText("Icon Path", iconPathBuf, IM_ARRAYSIZE(iconPathBuf)))
        {
            config.game_overlay_icon_path = iconPathBuf;
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标图片文件的路径, 支持PNG/JPG/BMP/ICO格式。默认为icon.png, 推荐使用带透明通道的PNG图片。");

        ImGui::SameLine();
        if (ImGui::Button("Browse##icon_path"))
        {
            char filePath[MAX_PATH] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = sizeof(filePath);
            ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.ico\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn))
            {
                strncpy_s(iconPathBuf, filePath, sizeof(iconPathBuf) - 1);
                config.game_overlay_icon_path = iconPathBuf;
                OverlayConfig_MarkDirty();
            }
        }

        ImGui::SliderInt("Icon Width", &config.game_overlay_icon_width, 4, 512);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标显示宽度(像素)。调大图标更大更显眼, 调小图标更小不遮挡目标。默认64, 推荐32-128。");

        ImGui::SliderInt("Icon Height", &config.game_overlay_icon_height, 4, 512);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标显示高度(像素)。调大图标更大更显眼, 调小图标更小不遮挡目标。默认64, 推荐32-128。");

        ImGui::SliderFloat("Icon Offset X", &config.game_overlay_icon_offset_x, -500.0f, 500.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标相对目标中心的水平偏移(像素), 正数右移负数左移。默认0, 推荐-20到20微调对齐。");

        ImGui::SliderFloat("Icon Offset Y", &config.game_overlay_icon_offset_y, -500.0f, 500.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标相对目标中心的垂直偏移(像素), 正数下移负数上移(向上偏移通常更自然)。默认0, 推荐-50到0使图标显示在目标上方。");

        if (ImGui::InputInt("Icon Class (-1 = all)", &config.game_overlay_icon_class))
        {
            if (config.game_overlay_icon_class < -1) config.game_overlay_icon_class = -1;
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标显示的目标类别过滤, -1表示所有类别都显示图标。设置为特定类别编号则只对该类别目标显示图标。默认-1(全部), 推荐按需设置(如7=仅头部)。");

        const char* anchors[] = { "center", "top", "bottom", "head" };
        int currentAnchor = 0;
        for (int i = 0; i < (int)(sizeof(anchors) / sizeof(anchors[0])); ++i)
        {
            if (config.game_overlay_icon_anchor == anchors[i])
            {
                currentAnchor = i;
                break;
            }
        }

        if (ImGui::Combo("Icon Anchor", &currentAnchor, anchors, IM_ARRAYSIZE(anchors)))
        {
            config.game_overlay_icon_anchor = anchors[currentAnchor];
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 图标锚点位置, 决定图标相对目标包围框的对齐方式。center=居中, top=顶部, bottom=底部, head=头部(目标上方)。默认center, 推荐head使图标显示在头部上方不遮挡目标。");

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Enable Icon Overlay to edit settings.");
        }

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Aim Simulation", "game_overlay_section_aim_sim"))
    {
        if (ImGui::Checkbox("Enable Aim Simulation Window", &config.aim_sim_enabled))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开启/关闭瞄准模拟窗口, 用于在独立窗口中模拟目标移动和瞄准效果, 方便离线调试瞄准参数。默认关闭, 推荐调试瞄准参数时开启。");

        if (!config.aim_sim_enabled)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SliderInt("Sim X", &config.aim_sim_x, -3000, 3000);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟窗口的屏幕X坐标(像素)。默认24, 推荐根据屏幕布局调整(如放在不遮挡游戏的角落)。");

        ImGui::SliderInt("Sim Y", &config.aim_sim_y, -3000, 3000);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟窗口的屏幕Y坐标(像素)。默认24, 推荐根据屏幕布局调整。");

        ImGui::SliderInt("Sim Width", &config.aim_sim_width, 220, 1600);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟窗口宽度(像素), 代表模拟的屏幕区域。调大模拟区域更大, 调小窗口更紧凑。默认560, 推荐400-800。");

        ImGui::SliderInt("Sim Height", &config.aim_sim_height, 180, 1000);
        if (ImGui::IsItemDeactivatedAfterEdit())
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟窗口高度(像素), 代表模拟的屏幕区域。调大模拟区域更大, 调小窗口更紧凑。默认360, 推荐300-600。");

        if (ImGui::SliderInt("Sim FPS Min", &config.aim_sim_fps_min, 15, 360))
        {
            if (config.aim_sim_fps_min > config.aim_sim_fps_max)
                config.aim_sim_fps_max = config.aim_sim_fps_min;
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟的最小帧率, 目标随机在min和max之间切换FPS来模拟真实帧率波动。调大模拟更流畅, 调小可测试低帧率场景。默认90, 推荐30-90。");

        if (ImGui::SliderInt("Sim FPS Max", &config.aim_sim_fps_max, 15, 360))
        {
            if (config.aim_sim_fps_max < config.aim_sim_fps_min)
                config.aim_sim_fps_min = config.aim_sim_fps_max;
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟的最大帧率。调大模拟更流畅, 调小更接近真实游戏帧率。默认120, 推荐60-144。");

        if (ImGui::SliderFloat("FPS Jitter", &config.aim_sim_fps_jitter, 0.0f, 0.8f, "%.3f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: FPS抖动幅度(0-1比例), 模拟实际帧率不稳定时的波动程度。调大波动更剧烈更真实, 0表示帧率完全稳定。默认0.15, 推荐0.05-0.2。");

        if (ImGui::SliderFloat("Capture Delay (ms)", &config.aim_sim_capture_delay_ms, 0.0f, 80.0f, "%.1f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟截图延迟(毫秒), 从截图完成到检测结果就绪的时间差。调大模拟延迟更大, 调小延迟更小。默认6.0, 推荐5-17(对应60-200FPS截图间隔)。");

        static bool delayedSnapshotPending = false;
        static double delayedSnapshotApplyAt = 0.0;
        const auto apply_snapshot_metrics = []()
        {
            float current_preprocess = 0.0f;
            float current_inference = 0.0f;
            float current_copy = 0.0f;
            float current_post = 0.0f;
            float current_nms = 0.0f;
            bool hasTimingMetrics = false;

            if (config.backend == "DML" && dml_detector)
            {
                current_preprocess = static_cast<float>(dml_detector->lastPreprocessTimeDML.count());
                current_inference = static_cast<float>(dml_detector->lastInferenceTimeDML.count());
                current_copy = static_cast<float>(dml_detector->lastCopyTimeDML.count());
                current_post = static_cast<float>(dml_detector->lastPostprocessTimeDML.count());
                current_nms = static_cast<float>(dml_detector->lastNmsTimeDML.count());
                hasTimingMetrics = true;
            }
#ifdef USE_CUDA
            else
            {
                current_preprocess = static_cast<float>(trt_detector.lastPreprocessTime.count());
                current_inference = static_cast<float>(trt_detector.lastInferenceTime.count());
                current_copy = static_cast<float>(trt_detector.lastCopyTime.count());
                current_post = static_cast<float>(trt_detector.lastPostprocessTime.count());
                current_nms = static_cast<float>(trt_detector.lastNmsTime.count());
                hasTimingMetrics = true;
            }
#endif

            const auto clampf = [](float v, float lo, float hi) -> float
            {
                if (v < lo) return lo;
                if (v > hi) return hi;
                return v;
            };

            const float fpsNow = static_cast<float>(captureFps.load());
            if (fpsNow > 1.0f)
            {
                const float captureDelayMs = 1000.0f / fpsNow;
                config.aim_sim_capture_delay_ms = clampf(captureDelayMs, 0.0f, 80.0f);
            }

            if (hasTimingMetrics && current_inference > 0.0f)
                config.aim_sim_inference_delay_ms = clampf(current_inference, 0.0f, 120.0f);

            if (hasTimingMetrics)
            {
                const float extraDelayMs = current_preprocess + current_copy + current_post + current_nms;
                config.aim_sim_extra_delay_ms = clampf(extraDelayMs, 0.0f, 60.0f);
            }

            OverlayConfig_MarkDirty();
        };

        if (delayedSnapshotPending && ImGui::GetTime() >= delayedSnapshotApplyAt)
        {
            apply_snapshot_metrics();
            delayedSnapshotPending = false;
        }

        if (ImGui::Checkbox("Use Live Inference Delay", &config.aim_sim_use_live_inference))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 使用实时推理耗时自动设置Inference/Extra延迟参数, 开启后点击Snapshot从当前后端获取真实延迟数据。默认开启, 推荐开启以获取准确延迟模拟。");
        ImGui::SameLine();
        if (ImGui::Button("Snapshot Metrics"))
            apply_snapshot_metrics();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Capture Delay <- 1000/FPS\n"
                "Inference Delay <- current backend inference\n"
                "Extra Delay <- preprocess + copy + postprocess + NMS"
            );
        }
        ImGui::SameLine();
        if (ImGui::Button("Snapshot in 4s"))
        {
            delayedSnapshotPending = true;
            delayedSnapshotApplyAt = ImGui::GetTime() + 4.0;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"Start 4-second timer, then snapshot metrics automatically.");

        if (delayedSnapshotPending)
        {
            const double remaining = std::max(0.0, delayedSnapshotApplyAt - ImGui::GetTime());
            ImGui::SameLine();
            ImGui::TextDisabled("Auto in %.1fs", static_cast<float>(remaining));
        }

        if (ImGui::SliderFloat("Inference Delay (ms)", &config.aim_sim_inference_delay_ms, 0.0f, 120.0f, "%.1f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟AI模型推理耗时(毫秒), 即从图像输入到检测结果输出的时间。调大模拟更慢的推理, 调小模拟更快的推理。默认12.0, 推荐8-25(取决于模型和后端)。");

        if (ImGui::SliderFloat("Input Delay (ms)", &config.aim_sim_input_delay_ms, 0.0f, 60.0f, "%.1f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟输入延迟(毫秒), 即鼠标指令发送到游戏实际响应的时间。调大模拟更大的输入延迟, 调小响应更快。默认2.0, 推荐1-10。");

        if (ImGui::SliderFloat("Extra Delay (ms)", &config.aim_sim_extra_delay_ms, 0.0f, 60.0f, "%.1f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟额外处理延迟(毫秒), 包括预处理/图像拷贝/后处理/NMS等非推理耗时之和。调大总延迟更大, 调小总延迟更小。默认2.0, 推荐2-15。");

        if (ImGui::SliderFloat("Target Max Speed", &config.aim_sim_target_max_speed, 20.0f, 2500.0f, "%.0f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟目标最大移动速度(像素/秒)。调大目标移动更快更难瞄准, 调小目标移动更慢更容易跟踪。默认560, 推荐300-1000(模拟真实玩家移动速度)。");

        if (ImGui::SliderFloat("Target Accel", &config.aim_sim_target_accel, 20.0f, 10000.0f, "%.0f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟目标加速度(像素/秒^2), 控制目标改变方向时的加速度。调大目标变向更突然(更难预测), 调小目标移动更平滑(更易预测)。默认1850, 推荐1000-5000。");

        if (ImGui::SliderFloat("Target Stop Chance", &config.aim_sim_target_stop_chance, 0.0f, 0.95f, "%.2f"))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 模拟目标随机停止的概率(每帧), 用于模拟玩家停顿行为。调大目标更频繁停顿(更容易瞄准), 0表示永不停顿。默认0.25, 推荐0.1-0.4。");

        if (ImGui::Checkbox("Show Delayed Observation", &config.aim_sim_show_observed))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在模拟窗口中显示延迟后的观测位置(红色), 与真实模拟位置(绿色)对比, 直观展示延迟对瞄准的影响。默认开启, 推荐开启以观察延迟效果。");

        if (ImGui::Checkbox("Show Trajectory History", &config.aim_sim_show_history))
            OverlayConfig_MarkDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在模拟窗口中显示目标历史轨迹线, 便于观察移动路径和预测准确性。默认开启, 推荐开启。");

        if (!config.aim_sim_enabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Enable Aim Simulation Window to edit settings.");
        }

        OverlayUI::EndSection();
    }

    // ── Ammo Counter ──
    if (OverlayUI::BeginSection("Ammo Counter", "game_overlay_section_ammo"))
    {
        bool ammo_changed = false;
        if (ImGui::Checkbox("Enable Ammo Display", &config.ammo_enabled))
            ammo_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开启/关闭弹药数字识别与叠加显示, 通过OCR识别游戏中的弹药数字并绘制到叠加层。默认关闭, 推荐有弹药显示需求的游戏开启。");

        ImGui::SliderInt("Display X", &config.ammo_display_x, 0, 3000);
        if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药数字显示位置的屏幕X坐标(像素)。默认20, 推荐根据游戏UI布局调整到不遮挡关键信息的位置。");

        ImGui::SliderInt("Display Y", &config.ammo_display_y, 0, 2000);
        if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药数字显示位置的屏幕Y坐标(像素)。默认60, 推荐根据游戏UI布局调整。");

        ImGui::SliderFloat("Text Size", &config.ammo_display_text_size, 8.0f, 72.0f, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药数字显示的文字大小。调大文字更大更易读, 调小文字更不遮挡游戏画面。默认24, 推荐18-36。");

        if (ImGui::CollapsingHeader("Text Color"))
        {
            ImGui::SliderInt("R##ammo_r", &config.ammo_display_color_r, 0, 255);
            if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药文字颜色的红色分量。默认255, 推荐255(亮黄色醒目)。");

            ImGui::SliderInt("G##ammo_g", &config.ammo_display_color_g, 0, 255);
            if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药文字颜色的绿色分量。默认255(黄绿色), 推荐200-255。");

            ImGui::SliderInt("B##ammo_b", &config.ammo_display_color_b, 0, 255);
            if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药文字颜色的蓝色分量。调大偏蓝, 调小偏黄。默认0(纯黄无蓝), 推荐0-50。");

            ImGui::SliderInt("A##ammo_a", &config.ammo_display_color_a, 0, 255);
            if (ImGui::IsItemDeactivatedAfterEdit()) ammo_changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 弹药文字颜色的透明度(Alpha), 0=完全透明, 255=完全不透明。调大文字更不透明更清晰, 调小文字更透明不遮挡画面。默认255, 推荐200-255。");
        }

        if (ammo_changed) OverlayConfig_MarkDirty();
        OverlayUI::EndSection();
    }

    // ── Auto Reload ──
    if (OverlayUI::BeginSection("Auto Reload", "game_overlay_section_auto_reload"))
    {
        bool reload_changed = false;
        if (ImGui::Checkbox("Enable Auto Reload", &config.auto_reload))
            reload_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开启/关闭自动换弹功能, 当弹药数低于阈值时自动触发换弹按键。默认关闭, 推荐需要自动换弹的游戏开启。");

        ImGui::SliderInt("Threshold", &config.auto_reload_threshold, 1, 30);
        if (ImGui::IsItemDeactivatedAfterEdit()) reload_changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"Reload when ammo <= this value");

        ImGui::SliderInt("Cooldown (ms)", &config.auto_reload_cooldown_ms, 50, 3000);
        if (ImGui::IsItemDeactivatedAfterEdit()) reload_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 两次自动换弹之间的冷却时间(毫秒), 防止短时间内连续触发换弹。调大换弹间隔更长更安全, 调小响应更快但可能误触发。默认500, 推荐300-1000。");

        ImGui::SliderInt("Lead (ms)", &config.auto_reload_lead_ms, 1, 200);
        if (ImGui::IsItemDeactivatedAfterEdit()) reload_changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"Trigger reload this many ms before ammo hits 0 (predicted)");

        ImGui::Combo("Side Button", &config.auto_reload_button,
                     "XButton1 (Back)\0XButton2 (Forward)\0\0");
        if (ImGui::IsItemDeactivatedAfterEdit()) reload_changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 触发换弹的鼠标侧键。XButton1=浏览器后退键(通常是侧键下方), XButton2=浏览器前进键(通常是侧键上方)。默认XButton2(Forward), 推荐按个人鼠标习惯选择。");

        if (reload_changed) OverlayConfig_MarkDirty();
        OverlayUI::EndSection();
    }

    if (!g_iconLastError.empty())
    {
        if (OverlayUI::BeginSection("Errors", "game_overlay_section_errors"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
            ImGui::TextWrapped("%s", g_iconLastError.c_str());
            ImGui::PopStyleColor();
            OverlayUI::EndSection();
        }
    }

}
