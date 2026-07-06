#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include "rn_ai_cpp.h"
#include "overlay.h"

void draw_buttons()
{
    ImGui::Text("Targeting Buttons");

    for (size_t i = 0; i < config.button_targeting.size(); )
    {
        std::string& current_key_name = config.button_targeting[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Targeting Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择目标锁定/自瞄的激活按键。按住此键时AI会自动瞄准检测到的目标。默认None, 推荐右键或侧键");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_targeting" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_targeting.size() <= 1)
            {
                config.button_targeting[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_targeting.erase(config.button_targeting.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此瞄准按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##targeting"))
    {
        config.button_targeting.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的瞄准按键绑定");

    ImGui::Separator();

    ImGui::Text("Shoot Buttons");

    for (size_t i = 0; i < config.button_shoot.size(); )
    {
        std::string& current_key_name = config.button_shoot[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Shoot Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择自动射击的激活按键。按住此键时AI会自动开火。默认None, 推荐左键");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_shoot" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_shoot.size() <= 1)
            {
                config.button_shoot[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_shoot.erase(config.button_shoot.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此射击按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##shoot"))
    {
        config.button_shoot.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的射击按键绑定");

    ImGui::Separator();

    ImGui::Text("Aim Hold Buttons");

    for (size_t i = 0; i < config.button_aim_hold.size(); )
    {
        std::string& current_key_name = config.button_aim_hold[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Aim Hold Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择纯瞄准(不射击)的激活按键。用于aim_trigger_mode=3的按键保持模式。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_aim_hold" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_aim_hold.size() <= 1)
            {
                config.button_aim_hold[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_aim_hold.erase(config.button_aim_hold.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此瞄准保持按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##aim_hold"))
    {
        config.button_aim_hold.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的瞄准保持按键绑定");

    ImGui::Separator();

    ImGui::Text("Triggerbot Buttons");

    for (size_t i = 0; i < config.button_triggerbot.size(); )
    {
        std::string& current_key_name = config.button_triggerbot[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Triggerbot Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择触发机器人的激活按键。按住此键时准星在目标上会自动开火。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_triggerbot" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_triggerbot.size() <= 1)
            {
                config.button_triggerbot[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_triggerbot.erase(config.button_triggerbot.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此触发机器人按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##triggerbot"))
    {
        config.button_triggerbot.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的触发机器人按键绑定");

    ImGui::Separator();

    ImGui::Text("Disable Headshot Buttons");

    for (size_t i = 0; i < config.button_disable_headshot.size(); )
    {
        std::string& current_key_name = config.button_disable_headshot[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Disable Headshot Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择临时禁用爆头瞄准的按键。按住此键时AI瞄准身体而非头部。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_disable_headshot" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_disable_headshot.size() <= 1)
            {
                config.button_disable_headshot[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_disable_headshot.erase(config.button_disable_headshot.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此禁用爆头按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##disable_headshot"))
    {
        config.button_disable_headshot.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的禁用爆头按键绑定");

    ImGui::Separator();

    ImGui::Text("Pause Buttons");

    for (size_t i = 0; i < config.button_pause.size(); )
    {
        std::string& current_key_name = config.button_pause[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Pause Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择暂停/恢复AI的按键。按下切换AI的启用状态。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_pause" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_pause.size() <= 1)
            {
                config.button_pause[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_pause.erase(config.button_pause.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此暂停按键绑定");
        ++i;
    }

    if (ImGui::Button("Add button##pause"))
    {
        config.button_pause.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的暂停按键绑定");

    ImGui::Separator();

    ImGui::Text("Reload config Buttons");

    for (size_t i = 0; i < config.button_reload_config.size(); )
    {
        std::string& current_key_name = config.button_reload_config[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Reload config Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择热重载配置文件的按键。按下时无需重启即可重新加载config.ini。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_reload_config" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            if (config.button_reload_config.size() <= 1)
            {
                config.button_reload_config[0] = std::string("None");
                config.saveConfig();
                continue;
            }
            else
            {
                config.button_reload_config.erase(config.button_reload_config.begin() + i);
                config.saveConfig();
                continue;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此重载配置按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##reload_config"))
    {
        config.button_reload_config.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的重载配置按键绑定");

    ImGui::Separator();

    ImGui::Text("Overlay Buttons");

    for (size_t i = 0; i < config.button_open_overlay.size(); )
    {
        std::string& current_key_name = config.button_open_overlay[i];

        int current_index = -1;
        for (size_t k = 0; k < key_names.size(); ++k)
        {
            if (key_names[k] == current_key_name)
            {
                current_index = static_cast<int>(k);
                break;
            }
        }

        if (current_index == -1)
        {
            current_index = 0;
        }

        std::string combo_label = "Overlay Button " + std::to_string(i);

        if (ImGui::Combo(combo_label.c_str(), &current_index, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            current_key_name = key_names[current_index];
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择显示/隐藏覆盖层UI的按键。按下切换覆盖层可见性。默认None");

        ImGui::SameLine();
        std::string remove_button_label = "Remove##button_open_overlay" + std::to_string(i);
        if (ImGui::Button(remove_button_label.c_str()))
        {
            config.button_open_overlay.erase(config.button_open_overlay.begin() + i);
            config.saveConfig();
            continue;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 删除此覆盖层按键绑定");

        ++i;
    }

    if (ImGui::Button("Add button##overlay"))
    {
        config.button_open_overlay.push_back("None");
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 添加一个新的覆盖层按键绑定");

    ImGui::Separator();

    if (ImGui::Checkbox("Enable arrows keys options", &config.enable_arrows_settings))
    {
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用后可用方向键在覆盖层中调整滑块数值。默认false");
}
