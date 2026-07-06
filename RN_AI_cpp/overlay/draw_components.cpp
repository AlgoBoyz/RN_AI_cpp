#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <array>
#include <cstring>

#include "imgui/imgui.h"

#include "ui_sections.h"
#include "ui_theme.h"
#include "ui_runtime.h"

void draw_components()
{
    ImGuiStyle& style = ImGui::GetStyle();
    bool changed = false;
    static std::array<char, 512> bg_path_buf{};
    static bool bg_path_init = false;
    if (!bg_path_init)
    {
        const std::string p = OverlayUI::g_menu_bg_path.empty() ? "ui_bg.png" : OverlayUI::g_menu_bg_path;
        const size_t n = (std::min)(p.size(), bg_path_buf.size() - 1);
        memcpy(bg_path_buf.data(), p.data(), n);
        bg_path_buf[n] = '\0';
        bg_path_init = true;
    }

    if (OverlayUI::BeginCard("DESIGN TOKENS", "components_tokens"))
    {
        changed |= ImGui::Checkbox("Show descriptions", &OverlayUI::g_show_descriptions);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 显示/隐藏各控件下方的小字功能描述。默认开启。");

        if (ImGui::BeginTable("components_layout", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Button width", &OverlayUI::g_row_button_width, 18.0f, 40.0f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置行内+/-按钮宽度(像素)。调大: 按钮更宽。调小: 按钮更窄。默认22, 推荐18-30");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Input width", &OverlayUI::g_row_input_width, 46.0f, 120.0f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置行内数值输入框宽度(像素)。调大: 输入框更宽。调小: 输入框更窄。默认54, 推荐46-80");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Nav width", &OverlayUI::g_nav_width, 120.0f, 360.0f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置左侧导航栏宽度(像素)。调大: 导航栏更宽。调小: 导航栏更窄。默认172, 推荐140-240");
            changed |= ImGui::Checkbox("Enable menu background", &OverlayUI::g_menu_bg_enabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用/禁用设置面板背景图片。需搭配BG image path指定图片文件。默认关闭。");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("BG opacity", &OverlayUI::g_menu_bg_opacity, 0.02f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置菜单背景图片不透明度。调大: 背景更明显。调小: 背景更透明。默认0.22, 推荐0.1-0.5");
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::InputText("BG image path", bg_path_buf.data(), bg_path_buf.size()))
            {
                OverlayUI::g_menu_bg_path = bg_path_buf.data();
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置菜单背景图片文件路径。支持PNG等格式。默认ui_bg.png");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Item Spacing X", &style.ItemSpacing.x, 2.0f, 24.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置控件之间水平间距(像素)。调大: 间距更大。调小: 间距更小。ImGui默认8, 推荐4-16");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Item Spacing Y", &style.ItemSpacing.y, 2.0f, 24.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置控件之间垂直间距(像素)。调大: 间距更大。调小: 间距更小。ImGui默认4, 推荐2-12");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Frame Padding X", &style.FramePadding.x, 2.0f, 24.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置控件内水平内边距(像素)。调大: 输入框等更宽。调小: 输入框等更窄。ImGui默认4, 推荐2-12");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Frame Padding Y", &style.FramePadding.y, 1.0f, 20.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置控件内垂直内边距(像素)。调大: 输入框等更高。调小: 输入框等更矮。ImGui默认3, 推荐1-8");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 8.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置控件边框圆角半径(像素)。调大: 更圆润。调小: 更方正。ImGui默认0, 推荐0-6");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 8.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置窗口边框圆角半径(像素)。调大: 窗口更圆润。调小: 窗口更方正。ImGui默认0, 推荐0-6");
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("Alpha", &style.Alpha, 0.2f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置全局UI透明度。调大: 更不透明。调小: 更透明。ImGui默认1.0, 推荐0.5-1.0");

            ImGui::TableNextColumn();
            changed |= ImGui::ColorEdit4("Text", (float*)&style.Colors[ImGuiCol_Text], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置全局文本颜色。");
            changed |= ImGui::ColorEdit4("Window Bg", (float*)&style.Colors[ImGuiCol_WindowBg], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置窗口背景颜色。");
            changed |= ImGui::ColorEdit4("Frame Bg", (float*)&style.Colors[ImGuiCol_FrameBg], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置输入框/滑块等控件背景颜色。");
            changed |= ImGui::ColorEdit4("Button", (float*)&style.Colors[ImGuiCol_Button], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置按钮颜色。");
            changed |= ImGui::ColorEdit4("Border", (float*)&style.Colors[ImGuiCol_Border], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置边框/分割线颜色。");
            changed |= ImGui::ColorEdit4("Tab", (float*)&style.Colors[ImGuiCol_Tab], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置未选中标签页颜色。");
            changed |= ImGui::ColorEdit4("Tab Active", (float*)&style.Colors[ImGuiCol_TabActive], ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置选中标签页颜色。");
            ImGui::EndTable();
        }

        if (ImGui::Button("Reload ui_theme.ini"))
            changed |= OverlayTheme_Reload("ui_theme.ini");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 从ui_theme.ini文件重新加载主题配置并覆盖当前设置。");

    }
    OverlayUI::EndCard();

    if (OverlayUI::BeginCard("ADVANCED", "components_advanced"))
    {
        ImGui::SetNextItemWidth(220.0f);
        changed |= ImGui::SliderFloat("Overlay FPS text size", &OverlayUI::g_overlay_fps_text_size, 10.0f, 48.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置游戏覆盖层FPS文字大小。调大: 文字更大。调小: 文字更小。默认19, 推荐14-28");
        ImGui::SetNextItemWidth(220.0f);
        changed |= ImGui::SliderFloat("Overlay Latency text size", &OverlayUI::g_overlay_latency_text_size, 10.0f, 48.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 设置游戏覆盖层延迟文字大小。调大: 文字更大。调小: 文字更小。默认18, 推荐14-24");
    }
    OverlayUI::EndCard();

    if (changed)
        OverlayTheme_Save("ui_theme.ini");
}
