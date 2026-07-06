#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <string.h>
#include <algorithm>
#include <cstdlib>

#include <imgui/imgui.h>
#include "imgui/imgui_internal.h"

#include "config.h"
#include "rn_ai_cpp.h"
#include "capture.h"
#include "other_tools.h"
#include "virtual_camera.h"
#include "draw_settings.h"
#include "overlay.h"
#include "ui_sections.h"
#include "config_dirty.h"
#include "ui_controls.h"
#include "ui_runtime.h"

bool disable_winrt_futures = checkwin1903();
int monitors = get_active_monitors();

static std::vector<std::string> virtual_cameras;
static char virtual_camera_filter_buf[128] = "";
static char obs_ip_buf[64] = "";
static bool obs_buf_inited = false;

void ensureVirtualCamerasLoaded() {
    if (virtual_cameras.empty()) {
        virtual_cameras = VirtualCameraCapture::GetAvailableVirtualCameras();
    }
}

void draw_capture_settings()
{
    static const int allowed_resolutions[] = { 220, 320, 640 };
    static int winrt_source_mode = 0; // 0 = monitor, 1 = window title (preview UI)
    static char winrt_window_title_buf[128] = "";
    if (OverlayUI::BeginCard("GENERAL CAPTURE SETTINGS", "capture_core_card"))
    {
        std::vector<std::string> captureMethodOptions = { "duplication_api", "winrt", "virtual_camera", "udp", "obs" };
        std::vector<const char*> captureMethodItems;
        captureMethodItems.push_back("duplication_api");
        captureMethodItems.push_back("winrt");
        captureMethodItems.push_back("virtual_camera");
        captureMethodItems.push_back("udp");
        captureMethodItems.push_back("obs");

        int currentcaptureMethodIndex = 0;
        for (size_t i = 0; i < captureMethodOptions.size(); ++i)
        {
            if (captureMethodOptions[i] == config.capture_method)
            {
                currentcaptureMethodIndex = static_cast<int>(i);
                break;
            }
        }

        int current_resolution_idx = 1;
        for (int i = 0; i < 3; ++i)
            if (config.detection_resolution == allowed_resolutions[i])
                current_resolution_idx = i;
        OverlayUI::AdaptiveItemWidth(1.0f, 220.0f, 560.0f);
        if (ImGui::Combo("Detection Resolution", &current_resolution_idx, "220\0""320\0""640\0"))
        {
            const int chosen = allowed_resolutions[current_resolution_idx];
            if (chosen != config.detection_resolution)
            {
                config.detection_resolution = chosen;
                detection_resolution_changed.store(true);
                detector_model_changed.store(true);

                globalMouseThread->updateConfig(
                    config.detection_resolution,
                    config.fovX,
                    config.fovY,
                    config.minSpeedMultiplier,
                    config.maxSpeedMultiplier,
                    config.predictionInterval,
                    config.auto_shoot,
                    config.bScope_multiplier,
                    config.triggerbot_bScope_multiplier);
                OverlayConfig_MarkDirty();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: AI检测输入帧边长(像素)。调大=更精细但更吃性能, 调小=更快但可能漏检。默认320, 推荐640");
        if (OverlayUI::g_show_descriptions)
            ImGui::TextDisabled("Input size for AI model - higher = more accurate but slower");

        ImGui::TextUnformatted("Capture Method");
        OverlayUI::AdaptiveItemWidth(1.0f, 220.0f, 560.0f);
        if (ImGui::Combo("##capture_method_combo", &currentcaptureMethodIndex, captureMethodItems.data(), static_cast<int>(captureMethodItems.size())))
        {
            config.capture_method = captureMethodOptions[currentcaptureMethodIndex];
            capture_method_changed.store(true);
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 画面捕获方式。duplication_api=DXGI桌面复制(最低延迟), winrt=Windows通用捕获, virtual_camera=虚拟摄像头, udp=网络流, obs=OBS插件。默认udp");
        if (OverlayUI::g_show_descriptions)
            ImGui::TextDisabled("duplication_api / winrt / virtual_camera / udp / obs");

        if (OverlayUI::IntControlRow(
            "Capture FPS",
            &config.capture_fps,
            0,
            400,
            1,
            "FPS",
            config.capture_fps == 0 ? "Capture thread disabled" : nullptr))
        {
            capture_fps_changed.store(true);
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 捕获帧率上限。调大=画面更流畅但更吃CPU, 调小=省性能。0=关闭捕获线程。默认0, 推荐60-120");
        if (OverlayUI::g_show_descriptions)
            ImGui::TextDisabled("Current: %d FPS", config.capture_fps);

        if (config.capture_fps == 0 || config.capture_fps >= 61)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Warning: high FPS may reduce performance.");
        }

        if (ImGui::Checkbox("Enable Circle Mask", &config.circle_mask))
        {
            capture_method_changed.store(true);
            OverlayConfig_MarkDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 仅检测圆形FOV区域内的目标。开启=减少边缘误检, 关闭=检测全图。默认true");
        if (OverlayUI::g_show_descriptions)
            ImGui::TextDisabled("Only detect targets in circular FOV (reduces false positives)");

#ifdef USE_CUDA
        if (config.backend == "TRT")
        {
            const bool cudaCaptureAvailable = (config.capture_method == "duplication_api");
            if (!cudaCaptureAvailable)
                ImGui::BeginDisabled();

            if (ImGui::Checkbox("CUDA Direct Capture", &config.capture_use_cuda))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: GPU零拷贝捕获, 省去CPU-GPU拷贝延迟。仅NVIDIA显卡+duplication_api可用。默认true");
            if (OverlayUI::g_show_descriptions)
                ImGui::TextDisabled("Zero-copy GPU capture for lowest latency (requires NVIDIA GPU)");

            if (!cudaCaptureAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Available only with duplication_api.");
            }
        }
#endif

        if (config.capture_method == "duplication_api" || config.capture_method == "winrt" || config.capture_method == "obs")
        {
            std::vector<std::string> monitorNames;
            if (monitors == -1)
            {
                monitorNames.push_back("Monitor 1");
            }
            else
            {
                for (int i = -1; i < monitors; ++i)
                {
                    monitorNames.push_back("Monitor " + std::to_string(i + 1));
                }
            }

            std::vector<const char*> monitorItems;
            for (const auto& name : monitorNames)
            {
                monitorItems.push_back(name.c_str());
            }

            OverlayUI::AdaptiveItemWidth();
            if (ImGui::Combo("Capture monitor", &config.monitor_idx, monitorItems.data(), static_cast<int>(monitorItems.size())))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择要捕获的显示器编号。默认Monitor 1");
        }
    }
    OverlayUI::EndCard();

    if (config.capture_method == "winrt")
    {
        if (OverlayUI::BeginCard("WINRT SETTINGS", "capture_winrt"))
        {
            OverlayUI::AdaptiveItemWidth();
            ImGui::Combo("Source mode", &winrt_source_mode, "Monitor\0Window title (preview)\0");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WinRT捕获源类型。Monitor=捕获整个显示器, Window title=按窗口标题捕获。默认Monitor");
            if (winrt_source_mode == 1)
            {
                OverlayUI::AdaptiveItemWidth();
                ImGui::InputText("Window title", winrt_window_title_buf, IM_ARRAYSIZE(winrt_window_title_buf));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WinRT窗口捕获的目标窗口标题。需先切Source mode为Window title");
                ImGui::TextDisabled("Current backend uses monitor capture only.");
            }

            if (disable_winrt_futures)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }

            if (ImGui::Checkbox("Capture Borders", &config.capture_borders))
            {
                capture_borders_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WinRT捕获时是否包含窗口边框。默认true");

            if (ImGui::Checkbox("Capture Cursor", &config.capture_cursor))
            {
                capture_cursor_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 捕获画面中是否绘制鼠标光标。默认true");

            if (disable_winrt_futures)
            {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
        }
        OverlayUI::EndCard();
    }

    if (config.capture_method == "virtual_camera")
    {
        if (OverlayUI::BeginCard("VIRTUAL CAMERA", "capture_virtual_camera"))
        {
            ensureVirtualCamerasLoaded();
            ImGui::Text("Select virtual camera:");

            ImGui::Text("Filter:");
            OverlayUI::AdaptiveItemWidth();
            ImGui::InputText("##VCFilter", virtual_camera_filter_buf, IM_ARRAYSIZE(virtual_camera_filter_buf));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 按名称关键字过滤虚拟摄像头列表");

            std::string filter_lower = virtual_camera_filter_buf;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

            std::vector<int> filtered_indices;
            for (int i = 0; i < static_cast<int>(virtual_cameras.size()); ++i)
            {
                std::string name_lower = virtual_cameras[i];
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                if (filter_lower.empty() || name_lower.find(filter_lower) != std::string::npos)
                {
                    filtered_indices.push_back(i);
                }
            }

            if (!filtered_indices.empty())
            {
                int currentIndex = 0;
                for (int fi = 0; fi < static_cast<int>(filtered_indices.size()); ++fi)
                {
                    if (virtual_cameras[filtered_indices[fi]] == config.virtual_camera_name)
                    {
                        currentIndex = fi;
                        break;
                    }
                }

                std::vector<const char*> items;
                items.reserve(filtered_indices.size());
                for (int idx : filtered_indices)
                {
                    items.push_back(virtual_cameras[idx].c_str());
                }

                OverlayUI::AdaptiveItemWidth();
                if (ImGui::Combo("##virtual_camera_combo", &currentIndex, items.data(), static_cast<int>(items.size())))
                {
                    config.virtual_camera_name = virtual_cameras[filtered_indices[currentIndex]];
                    capture_method_changed.store(true);
                    OverlayConfig_MarkDirty();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择要捕获的虚拟摄像头设备");
            }
            else
            {
                ImGui::TextDisabled("No matching virtual cameras");
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
            {
                VirtualCameraCapture::ClearCachedCameraList();
                virtual_cameras = VirtualCameraCapture::GetAvailableVirtualCameras(true);
                virtual_camera_filter_buf[0] = '\0';
            }

            if (OverlayUI::IntControlRow("Virtual camera width", &config.virtual_camera_width, 128, 3840, 8, "px"))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 虚拟摄像头捕获宽度(像素)。默认1920, 推荐1920");

            if (OverlayUI::IntControlRow("Virtual camera heigth", &config.virtual_camera_heigth, 128, 2160, 8, "px"))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 虚拟摄像头捕获高度(像素)。默认1080, 推荐1080");
        }
        OverlayUI::EndCard();
    }

    if (config.capture_method == "udp")
    {
        if (OverlayUI::BeginCard("UDP RECEIVER", "capture_udp"))
        {
            if (OverlayUI::IntControlRow("Receive port", &config.udp_recv_port, 1024, 65535, 1, ""))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: UDP接收端口, 需与发送端端口一致。范围1024-65535。默认12345");
            if (OverlayUI::IntControlRow("JPEG quality", &config.udp_jpeg_quality, 1, 100, 1, ""))
            {
                capture_method_changed.store(true);
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: UDP接收的JPEG解码质量百分比。调大=画质好但带宽高, 调小=省带宽但画质差。默认80");
            if (ImGui::Checkbox("Save received frames", &config.udp_save_frames))
            {
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 将接收到的UDP帧保存到 %%USERPROFILE%%\\rn_ai\\ 目录用于调试。默认false");
            if (config.udp_save_frames)
            {
                ImGui::TextDisabled("Saving to %%USERPROFILE%%\\rn_ai\\");
            }
        }
        OverlayUI::EndCard();
    }

    if (config.capture_method == "obs")
    {
        if (OverlayUI::BeginCard("OBS", "capture_obs"))
        {
            if (ImGui::Checkbox("Enable OBS", &config.is_obs))
            {
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用OBS WebSocket连接获取游戏画面。需先在OBS中安装WebSocket插件。默认false");
            if (!obs_buf_inited)
            {
                strncpy_s(obs_ip_buf, sizeof(obs_ip_buf), config.obs_ip.c_str(), _TRUNCATE);
                obs_buf_inited = true;
            }
            OverlayUI::AdaptiveItemWidth();
            if (ImGui::InputText("OBS IP", obs_ip_buf, IM_ARRAYSIZE(obs_ip_buf)))
            {
                config.obs_ip = obs_ip_buf;
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: OBS WebSocket服务器IP地址, 通常为本机127.0.0.1");
            OverlayUI::AdaptiveItemWidth();
            if (ImGui::InputInt("OBS Port", &config.obs_port))
            {
                if (config.obs_port < 0)
                    config.obs_port = 0;
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: OBS WebSocket服务器端口, 需与OBS中设置一致。默认4455");
            OverlayUI::AdaptiveItemWidth();
            if (ImGui::InputInt("OBS FPS", &config.obs_fps))
            {
                if (config.obs_fps < 0)
                    config.obs_fps = 0;
                OverlayConfig_MarkDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: OBS捕获帧率上限。默认240");
        }
        OverlayUI::EndCard();
    }
}
