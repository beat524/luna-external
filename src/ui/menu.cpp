#include "menu.hpp"

#include "features/features.hpp"
#include "imgui.h"

#include <cstdio>

namespace ext::ui
{
    namespace
    {
        bounds_t g_bounds;

        const ImVec4 k_accent( 0.49f, 0.36f, 1.00f, 1.00f );
        const ImU32 k_accent_u32 = IM_COL32( 125, 92, 255, 255 );
        const ImU32 k_track_off = IM_COL32( 44, 47, 58, 255 );
        const ImU32 k_knob = IM_COL32( 244, 245, 250, 255 );

        void apply_style()
        {
            ImGuiStyle& style = ImGui::GetStyle();

            style.WindowRounding = 10.0f;
            style.ChildRounding = 8.0f;
            style.FrameRounding = 6.0f;
            style.GrabRounding = 6.0f;
            style.PopupRounding = 8.0f;
            style.TabRounding = 6.0f;
            style.ScrollbarRounding = 8.0f;
            style.WindowPadding = ImVec2( 14.0f, 12.0f );
            style.FramePadding = ImVec2( 8.0f, 5.0f );
            style.ItemSpacing = ImVec2( 10.0f, 8.0f );
            style.ItemInnerSpacing = ImVec2( 8.0f, 6.0f );
            style.WindowBorderSize = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.ScrollbarSize = 10.0f;
            style.WindowTitleAlign = ImVec2( 0.5f, 0.5f );

            ImVec4* c = style.Colors;
            c[ ImGuiCol_WindowBg ] = ImVec4( 0.052f, 0.056f, 0.070f, 0.97f );
            c[ ImGuiCol_PopupBg ] = ImVec4( 0.070f, 0.074f, 0.090f, 0.98f );
            c[ ImGuiCol_TitleBg ] = ImVec4( 0.052f, 0.056f, 0.070f, 1.00f );
            c[ ImGuiCol_TitleBgActive ] = ImVec4( 0.078f, 0.082f, 0.104f, 1.00f );
            c[ ImGuiCol_TitleBgCollapsed ] = ImVec4( 0.052f, 0.056f, 0.070f, 1.00f );
            c[ ImGuiCol_MenuBarBg ] = ImVec4( 0.060f, 0.064f, 0.080f, 1.00f );

            c[ ImGuiCol_Border ] = ImVec4( 1.00f, 1.00f, 1.00f, 0.06f );
            c[ ImGuiCol_BorderShadow ] = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );

            c[ ImGuiCol_FrameBg ] = ImVec4( 0.105f, 0.112f, 0.140f, 1.00f );
            c[ ImGuiCol_FrameBgHovered ] = ImVec4( 0.145f, 0.152f, 0.190f, 1.00f );
            c[ ImGuiCol_FrameBgActive ] = ImVec4( 0.175f, 0.180f, 0.230f, 1.00f );

            c[ ImGuiCol_Button ] = ImVec4( 0.130f, 0.135f, 0.170f, 1.00f );
            c[ ImGuiCol_ButtonHovered ] = ImVec4( 0.230f, 0.190f, 0.380f, 1.00f );
            c[ ImGuiCol_ButtonActive ] = ImVec4( 0.300f, 0.240f, 0.520f, 1.00f );

            c[ ImGuiCol_CheckMark ] = k_accent;
            c[ ImGuiCol_SliderGrab ] = k_accent;
            c[ ImGuiCol_SliderGrabActive ] = ImVec4( 0.62f, 0.50f, 1.00f, 1.00f );

            c[ ImGuiCol_Text ] = ImVec4( 0.905f, 0.915f, 0.945f, 1.00f );
            c[ ImGuiCol_TextDisabled ] = ImVec4( 0.420f, 0.440f, 0.500f, 1.00f );
            c[ ImGuiCol_Separator ] = ImVec4( 1.00f, 1.00f, 1.00f, 0.07f );
            c[ ImGuiCol_SeparatorHovered ] = k_accent;
            c[ ImGuiCol_SeparatorActive ] = k_accent;

            c[ ImGuiCol_Tab ] = ImVec4( 0.085f, 0.090f, 0.110f, 1.00f );
            c[ ImGuiCol_TabHovered ] = ImVec4( 0.180f, 0.160f, 0.280f, 1.00f );
            c[ ImGuiCol_TabActive ] = ImVec4( 0.200f, 0.160f, 0.340f, 1.00f );
            c[ ImGuiCol_TabUnfocused ] = ImVec4( 0.070f, 0.074f, 0.090f, 1.00f );
            c[ ImGuiCol_TabUnfocusedActive ] = ImVec4( 0.120f, 0.105f, 0.180f, 1.00f );

            c[ ImGuiCol_Header ] = ImVec4( 0.150f, 0.130f, 0.240f, 1.00f );
            c[ ImGuiCol_HeaderHovered ] = ImVec4( 0.220f, 0.190f, 0.360f, 1.00f );
            c[ ImGuiCol_HeaderActive ] = ImVec4( 0.280f, 0.230f, 0.460f, 1.00f );

            c[ ImGuiCol_ScrollbarBg ] = ImVec4( 0.00f, 0.00f, 0.00f, 0.20f );
            c[ ImGuiCol_ScrollbarGrab ] = ImVec4( 0.20f, 0.20f, 0.26f, 1.00f );
            c[ ImGuiCol_ScrollbarGrabHovered ] = ImVec4( 0.28f, 0.27f, 0.36f, 1.00f );
            c[ ImGuiCol_ScrollbarGrabActive ] = k_accent;
        }

        // Pill-style switch. The whole row is clickable, which makes the menu a
        // lot less fiddly than 13px checkboxes.
        void toggle( const char* label, bool* value )
        {
            ImGui::PushID( label );

            const float height = ImGui::GetFrameHeight();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;

            ImGui::InvisibleButton( "##row", ImVec2( width, height ) );
            const bool hovered = ImGui::IsItemHovered();
            if ( ImGui::IsItemClicked() )
                *value = !*value;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            const float text_y = start.y + ( height - ImGui::GetFontSize() ) * 0.5f;
            draw_list->AddText( ImVec2( start.x, text_y ), ImGui::GetColorU32( ImGuiCol_Text ), label );

            const float track_w = height * 1.85f;
            const float track_h = height * 0.70f;
            const ImVec2 track_min( start.x + width - track_w, start.y + ( height - track_h ) * 0.5f );
            const ImVec2 track_max( track_min.x + track_w, track_min.y + track_h );
            const float rounding = track_h * 0.5f;

            const ImU32 track = *value ? ( hovered ? IM_COL32( 141, 112, 255, 255 ) : k_accent_u32 )
                                       : ( hovered ? IM_COL32( 58, 62, 76, 255 ) : k_track_off );
            draw_list->AddRectFilled( track_min, track_max, track, rounding );

            const float knob_r = rounding - 2.0f;
            const float knob_x = *value ? track_max.x - rounding : track_min.x + rounding;
            draw_list->AddCircleFilled( ImVec2( knob_x, track_min.y + rounding ), knob_r, k_knob, 24 );

            ImGui::PopID();
        }

        void color_row( const char* label, float color[ 4 ] )
        {
            ImGui::TextUnformatted( label );
            ImGui::SameLine();

            const float swatch = 120.0f;
            ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - swatch );
            ImGui::SetNextItemWidth( swatch );
            ImGui::ColorEdit4( "##color", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview );
        }

        void separator_label( const char* label )
        {
            ImGui::Spacing();
            ImGui::TextDisabled( "%s", label );
            ImGui::Separator();
            ImGui::Spacing();
        }

        void draw_esp_tab( features::settings& s )
        {
            toggle( "Enable ESP", &s.esp );
            toggle( "Names", &s.names );
            toggle( "Distance", &s.distance );
            toggle( "Health bar", &s.health_bar );
            toggle( "Skeleton", &s.skeleton );
            toggle( "Snaplines", &s.snaplines );
            toggle( "Team check", &s.team_check );

            separator_label( "STYLE" );

            int box_mode = static_cast< int >( s.box );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::Combo( "Box style", &box_mode, "Full\0Corners\0Filled\0" ) )
                s.box = static_cast< features::box_style >( box_mode );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "Thickness", &s.box_thickness, 1.0f, 4.0f, "%.1f px" );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "Text scale", &s.text_scale, 0.7f, 1.8f, "%.2fx" );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "Max distance", &s.max_distance, 100.0f, 5000.0f, "%.0f m" );

            separator_label( "COLOURS" );

            color_row( "Box", s.box_color );
            color_row( "Name", s.name_color );
            color_row( "Distance", s.dist_color );
            color_row( "Skeleton", s.skeleton_color );
        }

        void draw_aimbot_tab( features::settings& s )
        {
            toggle( "Enable aimbot", &s.aimbot );
            toggle( "Aim at head", &s.aim_head );
            toggle( "Torso fallback", &s.aim_fallback );
            toggle( "Sticky target", &s.aim_sticky );
            toggle( "FOV indicator", &s.fov_circle );

            separator_label( "TARGETING" );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "FOV", &s.aim_fov, 10.0f, 600.0f, "%.0f px" );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "Smoothing", &s.aim_smooth, 1.0f, 20.0f, "%.1fx" );

            ImGui::SetNextItemWidth( -1.0f );
            ImGui::SliderFloat( "Deadzone", &s.aim_deadzone, 0.0f, 10.0f, "%.1f px" );

            static const char* keys[] = { "Mouse 5", "Mouse 4", "Left Alt", "Left Shift", "Left Ctrl" };
            static const int key_codes[] = { 0x06, 0x05, 0xA4, 0xA0, 0xA2 };

            int key_index = 0;
            for ( int i = 0; i < IM_ARRAYSIZE( key_codes ); ++i )
                if ( key_codes[ i ] == s.aim_key )
                    key_index = i;

            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::Combo( "Aim key", &key_index, keys, IM_ARRAYSIZE( keys ) ) )
                s.aim_key = key_codes[ key_index ];

            separator_label( "INDICATOR" );

            color_row( "FOV circle", s.fov_color );

            ImGui::Spacing();
            ImGui::TextDisabled( "Hold the aim key to snap onto the nearest\nplayer inside the FOV circle." );
            ImGui::TextDisabled( "Sticky target keeps the same player locked\nwhile they stay in the circle." );
        }

        void draw_misc_tab( features::settings& s )
        {
            toggle( "Debug HUD", &s.debug_hud );

            separator_label( "INFO" );

            ImGui::TextDisabled( "Menu is clickable only inside this box." );
            ImGui::TextDisabled( "Everything else is passed to Roblox." );
            ImGui::Spacing();
            ImGui::TextDisabled( "Insert / F1  -  hide or show this menu" );
            ImGui::TextDisabled( "Aimbot       -  hold the aim key" );
        }
    }  // namespace

    bounds_t bounds()
    {
        return g_bounds;
    }

    void draw_menu()
    {
        static bool styled = false;
        if ( !styled )
        {
            apply_style();
            styled = true;
        }

        auto& s = features::g_settings;

        ImGui::SetNextWindowSize( ImVec2( 430.0f, 600.0f ), ImGuiCond_FirstUseEver );
        ImGui::Begin( "luna", nullptr, ImGuiWindowFlags_NoCollapse );

        // Remember the window rectangle so the overlay's hit-test can let the
        // mouse through to the game everywhere outside it.
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        g_bounds = { pos.x, pos.y, size.x, size.y };

        ImGui::TextColored( k_accent, "luna" );
        ImGui::SameLine();
        ImGui::TextDisabled( "esp + aim" );
        ImGui::SameLine();

        char fps[ 32 ];
        std::snprintf( fps, sizeof( fps ), "%.0f fps", ImGui::GetIO().Framerate );
        ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize( fps ).x );
        ImGui::TextDisabled( "%s", fps );
        ImGui::Spacing();

        if ( ImGui::BeginTabBar( "##tabs", ImGuiTabBarFlags_None ) )
        {
            if ( ImGui::BeginTabItem( "ESP" ) )
            {
                draw_esp_tab( s );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Aimbot" ) )
            {
                draw_aimbot_tab( s );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Misc" ) )
            {
                draw_misc_tab( s );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}  // namespace ext::ui
