#pragma once

#include "imgui.h"

namespace ext::features
{
    enum class box_style : int
    {
        full = 0,
        corners = 1,
        filled = 2,
    };

    struct settings
    {
        // ESP
        bool esp = true;
        box_style box = box_style::full;
        bool names = true;
        bool distance = true;
        bool health_bar = true;
        bool skeleton = true;
        bool snaplines = false;
        bool team_check = true;
        float max_distance = 1500.0f;

        float box_thickness = 1.5f;
        float text_scale = 1.0f;

        float box_color[ 4 ] = { 0.0f, 1.0f, 0.0f, 1.0f };
        float name_color[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float dist_color[ 4 ] = { 0.8f, 0.8f, 0.8f, 1.0f };
        float skeleton_color[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.9f };

        // Aimbot
        bool aimbot = false;
        int aim_key = 0x06;  // VK_XBUTTON2 (mouse button 5)
        bool aim_head = true;
        bool aim_fallback = true;   // aim at the torso when the head is unavailable
        bool aim_sticky = true;     // keep the current target while it stays inside the FOV
        float aim_fov = 120.0f;
        float aim_smooth = 4.0f;
        float aim_deadzone = 1.5f;

        bool fov_circle = true;
        float fov_color[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.75f };

        // Diagnostics
        bool debug_hud = false;
    };

    extern settings g_settings;

    void run_esp();             // must be called between ImGui NewFrame() and Render()
    void draw_fov_indicator();  // aimbot FOV circle, independent of the ESP toggle
    void run_aimbot();          // called once per overlay tick
    void draw_debug_hud();      // always-on readout: proves rendering + shows resolved values
}  // namespace ext::features
