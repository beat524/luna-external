#include "features.hpp"

#include "core/process.hpp"
#include "game/offsets.hpp"
#include "game/roblox.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ext::features
{
    settings g_settings;

    namespace
    {
        // ---------------------------------------------------------------- helpers

        ImU32 to_color( const float color[ 4 ], const float alpha_mul = 1.0f )
        {
            return ImGui::ColorConvertFloat4ToU32(
                ImVec4( color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] * alpha_mul ) );
        }

        // Reading a name is one remote read, and the ESP needs the same part
        // names every frame, so they are memoised. The cache is dropped
        // periodically so reused instance addresses cannot go stale forever.
        const std::string& cached_name( const std::uintptr_t instance )
        {
            static std::unordered_map< std::uintptr_t, std::string > cache;
            static ULONGLONG last_clear = 0;

            const auto now = GetTickCount64();
            if ( now - last_clear > 10000 )
            {
                cache.clear();
                last_clear = now;
            }

            const auto [ slot, inserted ] = cache.try_emplace( instance );
            if ( inserted )
                slot->second = game::name( instance );
            return slot->second;
        }

        void draw_label( ImDrawList* draw_list, const ImVec2 pos, const ImU32 color, const char* text )
        {
            ImFont* font = ImGui::GetFont();
            const float size = ImGui::GetFontSize() * g_settings.text_scale;

            // Cheap 1px shadow so the text stays readable over bright sky.
            draw_list->AddText( font, size, ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, 200 ), text );
            draw_list->AddText( font, size, pos, color, text );
        }

        ImVec2 label_size( const char* text )
        {
            const ImVec2 size = ImGui::CalcTextSize( text );
            return ImVec2( size.x * g_settings.text_scale, size.y * g_settings.text_scale );
        }

        bool same_team( const std::uintptr_t a, const std::uintptr_t b )
        {
            const auto& o = game::get_offsets();
            if ( !o.player_team )
                return false;

            const auto team_a = g_process.read< std::uintptr_t >( a + o.player_team );
            const auto team_b = g_process.read< std::uintptr_t >( b + o.player_team );
            return team_a && team_b && team_a == team_b;
        }

        // One instance probe per frame instead of one per projection: the view
        // matrix is a 128-byte remote read and the skeleton needs ~20 points.
        struct projector
        {
            Mat4 view{};
            Vec2 dims{};
            bool ready = false;

            projector()
            {
                ready = game::view_matrix( view );
                dims = game::dimensions();
                if ( dims.x <= 0.0f || dims.y <= 0.0f )
                    ready = false;
            }

            bool operator()( const Vec3& world, Vec2& out ) const
            {
                return ready && ext::world_to_screen( view, world, dims, out );
            }
        };

        // ------------------------------------------------------------- skeleton

        struct bone_pair
        {
            const char* from;
            const char* to;
        };

        // Both R15 and R6 names are listed; R6 characters simply never resolve
        // the R15 names, and vice versa.
        constexpr bone_pair k_bones[] = {
            { "Head", "UpperTorso" },
            { "UpperTorso", "LowerTorso" },
            { "LowerTorso", "HumanoidRootPart" },
            { "UpperTorso", "RightUpperArm" },
            { "RightUpperArm", "RightLowerArm" },
            { "RightLowerArm", "RightHand" },
            { "UpperTorso", "LeftUpperArm" },
            { "LeftUpperArm", "LeftLowerArm" },
            { "LeftLowerArm", "LeftHand" },
            { "LowerTorso", "RightUpperLeg" },
            { "RightUpperLeg", "RightLowerLeg" },
            { "RightLowerLeg", "RightFoot" },
            { "LowerTorso", "LeftUpperLeg" },
            { "LeftUpperLeg", "LeftLowerLeg" },
            { "LeftLowerLeg", "LeftFoot" },
            { "Head", "Torso" },
            { "Torso", "HumanoidRootPart" },
            { "Torso", "Right Arm" },
            { "Torso", "Left Arm" },
            { "Torso", "Right Leg" },
            { "Torso", "Left Leg" },
        };

        constexpr const char* k_skeleton_names[] = {
            "Head",      "UpperTorso",      "LowerTorso",     "HumanoidRootPart", "Torso",
            "LeftUpperArm",  "LeftLowerArm",  "LeftHand",   "RightUpperArm",    "RightLowerArm",
            "RightHand",     "LeftUpperLeg",  "LeftLowerLeg", "LeftFoot",        "RightUpperLeg",
            "RightLowerLeg", "RightFoot",     "Left Arm",   "Right Arm",        "Left Leg",
            "Right Leg",
        };

        bool is_skeleton_part( const std::string& name )
        {
            for ( const auto* candidate : k_skeleton_names )
                if ( name == candidate )
                    return true;
            return false;
        }

        struct part_snapshot
        {
            std::string name;
            Vec3 position{};
            Vec2 screen{};
            bool projected = false;
        };

        bool part_screen( const std::vector< part_snapshot >& parts, const char* name, Vec2& out )
        {
            for ( const auto& part : parts )
            {
                if ( part.projected && part.name == name )
                {
                    out = part.screen;
                    return true;
                }
            }
            return false;
        }

        void draw_skeleton( ImDrawList* draw_list, const std::vector< part_snapshot >& parts, const ImU32 color )
        {
            const float thickness = std::max( g_settings.box_thickness * 0.75f, 1.0f );

            for ( const auto& bone : k_bones )
            {
                Vec2 from;
                Vec2 to;
                if ( !part_screen( parts, bone.from, from ) || !part_screen( parts, bone.to, to ) )
                    continue;
                draw_list->AddLine( ImVec2( from.x, from.y ), ImVec2( to.x, to.y ), IM_COL32( 0, 0, 0, 90 ),
                                    thickness + 1.0f );
                draw_list->AddLine( ImVec2( from.x, from.y ), ImVec2( to.x, to.y ), color, thickness );
            }
        }

        // ------------------------------------------------------------ box styles

        void draw_corner_box( ImDrawList* draw_list, const ImVec2 top_left, const ImVec2 bottom_right,
                              const ImU32 color, const float thickness )
        {
            const float full_w = bottom_right.x - top_left.x;
            const float full_h = bottom_right.y - top_left.y;
            const float arm = std::clamp( std::min( full_w, full_h ) * 0.28f, 4.0f, 24.0f );

            const ImVec2 tl = top_left;
            const ImVec2 br = bottom_right;
            const ImVec2 tr( br.x, tl.y );
            const ImVec2 bl( tl.x, br.y );

            draw_list->AddLine( tl, ImVec2( tl.x + arm, tl.y ), color, thickness );
            draw_list->AddLine( tl, ImVec2( tl.x, tl.y + arm ), color, thickness );

            draw_list->AddLine( tr, ImVec2( tr.x - arm, tr.y ), color, thickness );
            draw_list->AddLine( tr, ImVec2( tr.x, tr.y + arm ), color, thickness );

            draw_list->AddLine( bl, ImVec2( bl.x + arm, bl.y ), color, thickness );
            draw_list->AddLine( bl, ImVec2( bl.x, bl.y - arm ), color, thickness );

            draw_list->AddLine( br, ImVec2( br.x - arm, br.y ), color, thickness );
            draw_list->AddLine( br, ImVec2( br.x, br.y - arm ), color, thickness );
        }

        void draw_health_bar( ImDrawList* draw_list, const float left, const float top, const float bottom,
                              const float fraction )
        {
            const float height = bottom - top;
            if ( !( height > 2.0f ) || !std::isfinite( left ) || !std::isfinite( top ) )
                return;

            const float clamped = std::clamp( fraction, 0.0f, 1.0f );
            const ImU32 fill = IM_COL32( static_cast< int >( 255.0f * ( 1.0f - clamped ) ),
                                         static_cast< int >( 80.0f + 175.0f * clamped ), 60, 235 );

            // No rounded corners here on purpose. A 2 px wide bar leaves a
            // sub-pixel rounding radius behind (float cancellation on
            // left-3 - (left-5)), and that used to reach ImGui's arc
            // tessellator and divide by zero. Square corners avoid the path
            // entirely.
            draw_list->AddRectFilled( ImVec2( left - 6.0f, top - 1.0f ), ImVec2( left - 2.0f, bottom + 1.0f ),
                                      IM_COL32( 0, 0, 0, 160 ) );
            draw_list->AddRectFilled( ImVec2( left - 5.0f, bottom - height * clamped ),
                                      ImVec2( left - 3.0f, bottom ), fill );
        }
    }  // namespace

    void run_esp()
    {
        if ( !g_settings.esp )
            return;

        auto* draw_list = ImGui::GetBackgroundDrawList();
        if ( !draw_list )
            return;

        const auto data_model = game::data_model();
        if ( !data_model )
            return;

        const auto players = game::service( "Players" );
        if ( !players )
            return;

        const projector project;

        const auto local = game::local_player();
        const auto local_character = game::player_character( local );
        std::uintptr_t local_root = 0;
        if ( local_character )
        {
            for ( const auto child : game::children( local_character ) )
            {
                if ( cached_name( child ) == "HumanoidRootPart" )
                {
                    local_root = child;
                    break;
                }
            }
        }
        const Vec3 local_position = local_root ? game::part_position( local_root ) : Vec3{};

        const ImU32 box_color = to_color( g_settings.box_color );
        const ImU32 name_color = to_color( g_settings.name_color );
        const ImU32 dist_color = to_color( g_settings.dist_color );
        const ImU32 skeleton_color = to_color( g_settings.skeleton_color );
        const ImU32 fill_color = to_color( g_settings.box_color, 0.20f );

        const float thickness = std::max( g_settings.box_thickness, 1.0f );

        for ( const auto player : game::children( players ) )
        {
            if ( !player || player == local )
                continue;

            if ( g_settings.team_check && local && same_team( player, local ) )
                continue;

            const auto character = game::player_character( player );
            if ( !character )
                continue;

            std::uintptr_t head = 0;
            std::uintptr_t root = 0;
            std::uintptr_t humanoid = 0;
            std::vector< part_snapshot > parts;

            for ( const auto child : game::children( character ) )
            {
                if ( !child )
                    continue;

                const auto& child_name = cached_name( child );
                if ( child_name.empty() )
                    continue;

                if ( child_name == "Humanoid" )
                {
                    humanoid = child;
                    continue;
                }
                if ( child_name == "Head" )
                    head = child;
                else if ( child_name == "HumanoidRootPart" )
                    root = child;

                if ( !is_skeleton_part( child_name ) )
                    continue;

                part_snapshot part;
                part.name = child_name;
                part.position = game::part_position( child );
                part.projected = project( part.position, part.screen );
                parts.push_back( std::move( part ) );
            }

            if ( !head || !root )
                continue;

            const Vec3 head_position = game::part_position( head );
            const Vec3 root_position = game::part_position( root );

            const float distance = local_root ? ( root_position - local_position ).length() : 0.0f;
            if ( local_root && distance > g_settings.max_distance )
                continue;

            Vec2 head_screen;
            Vec2 root_screen;
            if ( !project( head_position + Vec3{ 0.0f, 0.5f, 0.0f }, head_screen ) )
                continue;
            if ( !project( root_position, root_screen ) )
                continue;

            const float height = std::fabs( head_screen.y - root_screen.y ) * 1.15f;
            if ( height < 4.0f )
                continue;

            const float width = height * 0.55f;
            const float top = head_screen.y - height * 0.15f;
            const float bottom = top + height;
            const float left = head_screen.x - width * 0.5f;
            const float right = head_screen.x + width * 0.5f;
            const ImVec2 top_left( left, top );
            const ImVec2 bottom_right( right, bottom );

            switch ( g_settings.box )
            {
            case box_style::full:
                draw_list->AddRect( top_left, bottom_right, box_color, 0.0f, 0, thickness );
                break;
            case box_style::corners:
                draw_corner_box( draw_list, top_left, bottom_right, box_color, thickness );
                break;
            case box_style::filled:
                draw_list->AddRectFilled( top_left, bottom_right, fill_color, 0.0f );
                draw_list->AddRect( top_left, bottom_right, box_color, 0.0f, 0, thickness );
                break;
            }

            if ( g_settings.skeleton )
                draw_skeleton( draw_list, parts, skeleton_color );

            if ( g_settings.health_bar && humanoid )
            {
                const float max_health = game::humanoid_max_health( humanoid );
                if ( max_health > 0.0f )
                {
                    const float fraction =
                        std::clamp( game::humanoid_health( humanoid ) / max_health, 0.0f, 1.0f );
                    draw_health_bar( draw_list, left, top, bottom, fraction );
                }
            }

            if ( g_settings.names )
            {
                const std::string label = cached_name( player );
                if ( !label.empty() )
                {
                    const ImVec2 size = label_size( label.c_str() );
                    draw_label( draw_list, ImVec2( head_screen.x - size.x * 0.5f, top - size.y - 2.0f ), name_color,
                                label.c_str() );
                }
            }

            if ( g_settings.distance )
            {
                const std::string label = std::to_string( static_cast< int >( distance ) ) + "m";
                const ImVec2 size = label_size( label.c_str() );
                draw_label( draw_list, ImVec2( head_screen.x - size.x * 0.5f, bottom + 2.0f ), dist_color,
                            label.c_str() );
            }

            if ( g_settings.snaplines && project.dims.x > 0.0f )
                draw_list->AddLine( ImVec2( project.dims.x * 0.5f, project.dims.y ),
                                    ImVec2( head_screen.x, head_screen.y ), box_color, thickness );
        }
    }

    void draw_fov_indicator()
    {
        if ( !g_settings.fov_circle || !g_settings.aimbot )
            return;

        auto* draw_list = ImGui::GetBackgroundDrawList();
        if ( !draw_list )
            return;

        const auto dims = game::dimensions();
        if ( !( dims.x > 0.0f ) || !( dims.y > 0.0f ) || !std::isfinite( dims.x ) || !std::isfinite( dims.y ) )
            return;

        const ImVec2 centre( dims.x * 0.5f, dims.y * 0.5f );

        // The circle radius is also the aimbot's search radius, so keep the
        // value sane even if the slider is ever given garbage.
        const float configured = std::isfinite( g_settings.aim_fov ) ? g_settings.aim_fov : 120.0f;
        const float radius = std::clamp( configured, 2.0f, 2000.0f );
        const ImU32 color = to_color( g_settings.fov_color );

        draw_list->AddCircle( centre, radius, color, 64, 1.5f );
        draw_list->AddCircle( centre, radius + 1.0f, IM_COL32( 0, 0, 0, 70 ), 64, 1.0f );
        draw_list->AddCircleFilled( centre, 3.0f, color, 12 );
    }

    namespace
    {
        // Which bone to aim at, in preference order. The torso entries are only
        // reached when the head is unavailable or off screen.
        struct aim_part
        {
            const char* name;
            float y_offset;
        };

        bool find_part( const std::uintptr_t character, const char* name, std::uintptr_t& out )
        {
            for ( const auto child : game::children( character ) )
            {
                if ( cached_name( child ) == name )
                {
                    out = child;
                    return true;
                }
            }
            return false;
        }

        // Projects the best available aim point for a character. Aiming at the
        // head and silently falling back to the torso means the aimbot still
        // works when a character is only half visible or above the screen edge.
        bool aim_screen_point( const std::uintptr_t character, const projector& project, Vec2& out )
        {
            const aim_part head{ "Head", 0.0f };
            const aim_part upper_torso{ "UpperTorso", 0.35f };
            const aim_part torso{ "Torso", 0.60f };

            const aim_part* order[ 3 ]{};
            int count = 0;
            if ( g_settings.aim_head )
                order[ count++ ] = &head;
            if ( !g_settings.aim_head || g_settings.aim_fallback )
            {
                order[ count++ ] = &upper_torso;
                order[ count++ ] = &torso;
            }

            for ( int i = 0; i < count; ++i )
            {
                std::uintptr_t part = 0;
                if ( !find_part( character, order[ i ]->name, part ) )
                    continue;

                Vec3 point = game::part_position( part );
                if ( !std::isfinite( point.x ) || !std::isfinite( point.y ) || !std::isfinite( point.z ) )
                    continue;
                point.y += order[ i ]->y_offset;

                if ( project( point, out ) )
                    return true;
            }
            return false;
        }

        // Target lock: while the locked player stays inside the FOV the aimbot
        // keeps aiming at them, so two players next to the crosshair cannot
        // make the target flip every frame.
        std::uintptr_t g_locked_player = 0;
        ULONGLONG g_locked_until = 0;

        // Mouse deltas are whole pixels. Without a carry the aimbot rounds every
        // small correction to zero and stalls just short of the target.
        float g_carry_x = 0.0f;
        float g_carry_y = 0.0f;

        void reset_aim_state()
        {
            g_locked_player = 0;
            g_locked_until = 0;
            g_carry_x = 0.0f;
            g_carry_y = 0.0f;
        }
    }  // namespace

    void run_aimbot()
    {
        if ( !g_settings.aimbot )
            return;

        if ( !( GetAsyncKeyState( g_settings.aim_key ) & 0x8000 ) )
        {
            reset_aim_state();
            return;
        }

        if ( !game::is_foreground() )
            return;

        const auto viewport = game::dimensions();
        if ( !( viewport.x > 0.0f ) || !( viewport.y > 0.0f ) || !std::isfinite( viewport.x ) ||
             !std::isfinite( viewport.y ) )
            return;

        const Vec2 center{ viewport.x * 0.5f, viewport.y * 0.5f };
        const float fov = std::clamp( g_settings.aim_fov, 1.0f, 2000.0f );

        const auto players = game::service( "Players" );
        if ( !players )
            return;

        const projector project;
        const auto local = game::local_player();
        const ULONGLONG now = GetTickCount64();

        // Local root position is only needed as a tie-breaker, and reading it
        // once keeps the per-player loop cheap.
        std::uintptr_t local_root = 0;
        if ( local )
            find_part( game::player_character( local ), "HumanoidRootPart", local_root );
        const Vec3 local_position = local_root ? game::part_position( local_root ) : Vec3{};

        Vec2 target_screen{};
        bool found = false;

        // 1) Keep the locked target while it is still inside the FOV.
        if ( g_settings.aim_sticky && g_locked_player && now < g_locked_until )
        {
            const auto character = game::player_character( g_locked_player );
            if ( character )
            {
                Vec2 screen;
                if ( aim_screen_point( character, project, screen ) && screen_distance( screen, center ) <= fov )
                {
                    target_screen = screen;
                    found = true;
                }
            }
        }

        // 2) Otherwise pick the player closest to the crosshair. Screen distance
        //    is the primary score; world distance only breaks near-ties so the
        //    aimbot prefers the closer of two overlapping targets.
        if ( !found )
        {
            float best_score = fov;
            std::uintptr_t best_player = 0;

            for ( const auto player : game::children( players ) )
            {
                if ( !player || player == local )
                    continue;

                if ( g_settings.team_check && local && same_team( player, local ) )
                    continue;

                const auto character = game::player_character( player );
                if ( !character )
                    continue;

                Vec2 screen;
                if ( !aim_screen_point( character, project, screen ) )
                    continue;

                const float distance = screen_distance( screen, center );
                if ( distance > fov )
                    continue;

                // 0.02 px of score per metre: a real tie-break, not a bias.
                float score = distance;
                if ( local_root )
                {
                    std::uintptr_t root = 0;
                    if ( find_part( character, "HumanoidRootPart", root ) )
                    {
                        const float world_distance = ( game::part_position( root ) - local_position ).length();
                        if ( std::isfinite( world_distance ) )
                            score += world_distance * 0.02f;
                    }
                }

                if ( !found || score < best_score )
                {
                    best_score = score;
                    best_player = player;
                    target_screen = screen;
                    found = true;
                }
            }

            if ( found )
            {
                g_locked_player = best_player;
                g_locked_until = now + 350;
            }
        }

        if ( !found )
        {
            g_carry_x = 0.0f;
            g_carry_y = 0.0f;
            return;
        }

        const float dx = target_screen.x - center.x;
        const float dy = target_screen.y - center.y;
        const float distance = std::sqrt( dx * dx + dy * dy );

        const float deadzone = std::clamp( g_settings.aim_deadzone, 0.0f, 20.0f );
        if ( distance <= deadzone )
        {
            g_carry_x = 0.0f;
            g_carry_y = 0.0f;
            return;
        }

        const float smooth = std::max( g_settings.aim_smooth, 1.0f );
        g_carry_x = std::clamp( g_carry_x + dx / smooth, -400.0f, 400.0f );
        g_carry_y = std::clamp( g_carry_y + dy / smooth, -400.0f, 400.0f );

        const auto step_x = static_cast< LONG >( g_carry_x );
        const auto step_y = static_cast< LONG >( g_carry_y );
        if ( step_x == 0 && step_y == 0 )
            return;

        g_carry_x -= static_cast< float >( step_x );
        g_carry_y -= static_cast< float >( step_y );

        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = step_x;
        input.mi.dy = step_y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput( 1, &input, sizeof( INPUT ) );
    }

    void draw_debug_hud()
    {
        if ( !g_settings.debug_hud )
            return;

        auto* draw_list = ImGui::GetBackgroundDrawList();
        if ( !draw_list )
            return;

        const auto viewport = game::dimensions();

        const auto data_model = game::data_model();
        const auto players = game::service( "Players" );
        const auto local = game::local_player();
        const auto character = game::player_character( local );
        const auto dm_class = game::class_name( data_model );
        const auto dm_kids = game::children( data_model );

        int player_count = 0;
        int character_count = 0;
        int projected_count = 0;
        bool any_transposed = false;

        if ( players )
        {
            const auto list = game::children( players );
            player_count = static_cast< int >( list.size() );

            for ( const auto player : list )
            {
                const auto chr = game::player_character( player );
                if ( !chr )
                    continue;
                ++character_count;

                const auto root = game::find_child( chr, "HumanoidRootPart" );
                if ( !root )
                    continue;

                Vec2 screen;
                bool transposed = false;
                if ( game::world_to_screen( game::part_position( root ), screen, &transposed ) )
                {
                    ++projected_count;
                    any_transposed = any_transposed || transposed;
                }
            }
        }

        Mat4 view{};
        const bool have_view = game::view_matrix( view );

        char text[ 1400 ];
        std::snprintf( text, sizeof( text ),
                       "external debug\n"
                       "attached      : %s   pid %u\n"
                       "base          : %p\n"
                       "datamodel     : %p\n"
                       "dm class      : '%s'   kids=%llu\n"
                       "players svc   : %p   (%d players)\n"
                       "local player  : %p   \"%s\"\n"
                       "character     : %p   (%d characters)\n"
                       "projected     : %d   matrix=%s\n"
                       "viewport      : %.0f x %.0f\n"
                       "view row3     : %.3f %.3f %.3f %.3f\n",
                       g_process.attached() ? "yes" : "no", g_process.pid(),
                       reinterpret_cast< void* >( g_process.base() ), reinterpret_cast< void* >( data_model ),
                       dm_class.c_str(), static_cast< unsigned long long >( dm_kids.size() ),
                       reinterpret_cast< void* >( players ), player_count, reinterpret_cast< void* >( local ),
                       game::name( local ).c_str(), reinterpret_cast< void* >( character ), character_count,
                       projected_count, !have_view ? "unreadable" : ( any_transposed ? "transposed" : "primary" ),
                       viewport.x, viewport.y, view.m[ 3 ][ 0 ], view.m[ 3 ][ 1 ], view.m[ 3 ][ 2 ], view.m[ 3 ][ 3 ] );

        const ImVec2 text_size = ImGui::CalcTextSize( text );
        draw_list->AddRectFilled( ImVec2( 6.0f, 6.0f ),
                                  ImVec2( 6.0f + text_size.x + 12.0f, 6.0f + text_size.y + 12.0f ),
                                  IM_COL32( 0, 0, 0, 190 ), 4.0f );
        draw_list->AddText( ImVec2( 12.0f, 12.0f ), IM_COL32( 0, 255, 0, 255 ), text );
    }
}  // namespace ext::features
