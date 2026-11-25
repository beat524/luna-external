#include "overlay.hpp"

#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <GL/gl.h>

#include <cstddef>
#include <cstdio>

#include "features/features.hpp"
#include "game/roblox.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ui/menu.hpp"

namespace ext::overlay
{
    namespace
    {
        GLFWwindow* g_window = nullptr;
        HWND g_overlay = nullptr;
        HWND g_roblox = nullptr;
        bool g_menu_open = true;
        WNDPROC g_original_wndproc = nullptr;

        // Framebuffer / window ratio. WM_NCHITTEST reports physical pixels while
        // ImGui works in logical ones, so points are divided by this.
        float g_display_scale = 1.0f;

        // Whether WS_EX_TRANSPARENT is currently applied (i.e. clicks fall
        // through to the game).
        bool g_click_through = false;

        // True while a click that started inside the menu is still held, so a
        // slider keeps tracking even if the cursor leaves the menu rectangle.
        bool g_menu_capture = false;

        int g_last_x = -1;
        int g_last_y = -1;
        int g_last_w = -1;
        int g_last_h = -1;
        ULONGLONG g_last_log = 0;

        bool menu_contains_client_point( const POINT client )
        {
            const float scale = g_display_scale > 0.0f ? g_display_scale : 1.0f;
            const float x = static_cast< float >( client.x ) / scale;
            const float y = static_cast< float >( client.y ) / scale;

            const auto box = ui::bounds();
            return g_menu_open && box.valid() && box.contains( x, y );
        }

        POINT point_from_lparam( const LPARAM lparam )
        {
            return POINT{ static_cast< LONG >( static_cast< short >( LOWORD( lparam ) ) ),
                          static_cast< LONG >( static_cast< short >( HIWORD( lparam ) ) ) };
        }

        // lParam of WM_NCHITTEST carries screen coordinates.
        bool menu_contains_screen_point( HWND hwnd, const LPARAM lparam )
        {
            POINT pt = point_from_lparam( lparam );
            if ( !ScreenToClient( hwnd, &pt ) )
                return false;
            return menu_contains_client_point( pt );
        }

        // lParam of WM_*BUTTONDOWN/UP already carries client coordinates.
        bool menu_contains_client_lparam( const LPARAM lparam )
        {
            return menu_contains_client_point( point_from_lparam( lparam ) );
        }

        // Is the pointer over the menu? Polled every frame, because once the
        // window is click-through it never receives mouse messages and so
        // cannot notice the pointer arriving.
        bool cursor_over_menu()
        {
            if ( !g_menu_open )
                return false;

            const auto box = ui::bounds();
            if ( !box.valid() )
                return false;

            POINT cursor{};
            if ( !GetCursorPos( &cursor ) || !ScreenToClient( g_overlay, &cursor ) )
                return false;

            const float scale = g_display_scale > 0.0f ? g_display_scale : 1.0f;
            const float x = static_cast< float >( cursor.x ) / scale;
            const float y = static_cast< float >( cursor.y ) / scale;

            // Slack of a few pixels so the window is already interactive by the
            // time the pointer actually reaches a widget.
            constexpr float pad = 6.0f;
            return x >= box.x - pad && x < box.x + box.width + pad && y >= box.y - pad &&
                   y < box.y + box.height + pad;
        }

        // HTTRANSPARENT only forwards clicks to windows on the same thread, so it
        // cannot hand input to another process. WS_EX_TRANSPARENT on a layered
        // window does work cross-process: the window is skipped by hit-testing
        // entirely and the message goes to whatever is underneath.
        void apply_click_through( const bool click_through )
        {
            if ( click_through == g_click_through )
                return;
            g_click_through = click_through;

            LONG_PTR style = GetWindowLongPtrW( g_overlay, GWL_EXSTYLE );
            if ( click_through )
                style |= WS_EX_TRANSPARENT;
            else
                style &= ~WS_EX_TRANSPARENT;
            SetWindowLongPtrW( g_overlay, GWL_EXSTYLE, style );

            // Without SWP_FRAMECHANGED Windows keeps hit-testing with the stale
            // style, so the first click after a toggle reaches the wrong window.
            SetWindowPos( g_overlay, nullptr, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
        }

        void update_interactivity()
        {
            const bool interactive = cursor_over_menu() || g_menu_capture;
            apply_click_through( !interactive );
        }

        // Style flips decide where input goes; WM_NCHITTEST is kept as a
        // second line of defence for the frames around a toggle.
        LRESULT CALLBACK overlay_wndproc( HWND hwnd, const UINT msg, const WPARAM wparam, const LPARAM lparam )
        {
            switch ( msg )
            {
            case WM_MOUSEACTIVATE:
                // Clicking the menu must not pull keyboard focus off the game,
                // otherwise WASD stops working while the overlay is visible.
                return MA_NOACTIVATE;

            case WM_NCHITTEST:
                if ( g_menu_capture )
                    return HTCLIENT;
                return menu_contains_screen_point( hwnd, lparam ) ? HTCLIENT : HTTRANSPARENT;

            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                if ( menu_contains_client_lparam( lparam ) )
                    g_menu_capture = true;
                break;

            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_CAPTURECHANGED:
                g_menu_capture = false;
                break;

            default:
                break;
            }

            return CallWindowProcW( g_original_wndproc, hwnd, msg, wparam, lparam );
        }

        void update_bounds()
        {
            if ( !g_roblox || !IsWindow( g_roblox ) )
            {
                g_roblox = reinterpret_cast< HWND >( game::roblox_window() );
                if ( !g_roblox )
                    return;
            }

            RECT client{};
            if ( !GetClientRect( g_roblox, &client ) )
                return;

            POINT top_left{ 0, 0 };
            ClientToScreen( g_roblox, &top_left );

            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            if ( top_left.x == g_last_x && top_left.y == g_last_y && width == g_last_w && height == g_last_h )
                return;

            g_last_x = top_left.x;
            g_last_y = top_left.y;
            g_last_w = width;
            g_last_h = height;

            glfwSetWindowPos( g_window, top_left.x, top_left.y );
            glfwSetWindowSize( g_window, width, height );
        }

        void on_glfw_error( const int code, const char* description )
        {
            std::printf( "[glfw] %d: %s\n", code, description );
        }

        // Printed to the console every couple of seconds. Works even if the
        // overlay renders nothing, so it is the "dump without --dump" path.
        void log_status()
        {
            const auto data_model = game::data_model();
            const auto players = game::service( "Players" );
            const auto local = game::local_player();
            const auto character = game::player_character( local );
            const auto viewport = game::dimensions();
            const auto kids = game::children( data_model );

            std::printf( "[status] datamodel=%p class='%s' children=%llu\n", reinterpret_cast< void* >( data_model ),
                         game::class_name( data_model ).c_str(),
                         static_cast< unsigned long long >( kids.size() ) );

            for ( std::size_t i = 0; i < kids.size() && i < 24; ++i )
                std::printf( "   [%llu] %p class='%s' name='%s'\n", static_cast< unsigned long long >( i ),
                             reinterpret_cast< void* >( kids[ i ] ), game::class_name( kids[ i ] ).c_str(),
                             game::name( kids[ i ] ).c_str() );

            std::printf( "[status] players=%p local=%p \"%s\" character=%p viewport=%.0fx%.0f\n",
                         reinterpret_cast< void* >( players ), reinterpret_cast< void* >( local ),
                         game::name( local ).c_str(), reinterpret_cast< void* >( character ), viewport.x, viewport.y );
            std::fflush( stdout );
        }
    }  // namespace

    bool init()
    {
        glfwSetErrorCallback( on_glfw_error );
        if ( !glfwInit() )
            return false;

        glfwWindowHint( GLFW_DECORATED, GLFW_FALSE );
        glfwWindowHint( GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE );
        glfwWindowHint( GLFW_FLOATING, GLFW_TRUE );
        glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );
        glfwWindowHint( GLFW_FOCUS_ON_SHOW, GLFW_FALSE );

        g_window = glfwCreateWindow( 800, 600, "luna", nullptr, nullptr );
        if ( !g_window )
        {
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent( g_window );
        glfwSwapInterval( 1 );

        g_overlay = glfwGetWin32Window( g_window );
        LONG_PTR style = GetWindowLongPtrW( g_overlay, GWL_EXSTYLE );
        // WS_EX_NOACTIVATE: the overlay can never become the active window, so
        // the game keeps keyboard focus permanently.
        // WS_EX_LAYERED: required for WS_EX_TRANSPARENT to skip hit-testing.
        // LWA_ALPHA at 255 keeps the OpenGL per-pixel alpha intact.
        style |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
        SetWindowLongPtrW( g_overlay, GWL_EXSTYLE, style );
        SetLayeredWindowAttributes( g_overlay, 0, 255, LWA_ALPHA );
        SetWindowPos( g_overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );

        // Hook the window procedure so hit-testing can be made transparent.
        g_original_wndproc = reinterpret_cast< WNDPROC >(
            SetWindowLongPtrW( g_overlay, GWLP_WNDPROC, reinterpret_cast< LONG_PTR >( overlay_wndproc ) ) );

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL( g_window, true );
        ImGui_ImplOpenGL3_Init( "#version 130" );

        g_roblox = reinterpret_cast< HWND >( game::roblox_window() );
        update_bounds();
        glfwShowWindow( g_window );

        // The overlay must never hold focus: the game is playable the whole
        // time, menu open or not.
        if ( g_roblox && IsWindow( g_roblox ) )
            SetForegroundWindow( g_roblox );
        return true;
    }

    void run()
    {
        game::probe();

        while ( !glfwWindowShouldClose( g_window ) )
        {
            glfwPollEvents();

            // Physical (framebuffer) pixels per logical pixel, for hit-testing.
            {
                int window_w = 0;
                int window_h = 0;
                int buffer_w = 0;
                int buffer_h = 0;
                glfwGetWindowSize( g_window, &window_w, &window_h );
                glfwGetFramebufferSize( g_window, &buffer_w, &buffer_h );
                if ( window_w > 0 && buffer_w > 0 )
                    g_display_scale = static_cast< float >( buffer_w ) / static_cast< float >( window_w );
            }

            // Safety net: never leave the overlay swallowing clicks because a
            // mouse-up was missed.
            if ( g_menu_capture && !( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) &&
                 !( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) && !( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) )
                g_menu_capture = false;

            const bool toggle_menu =
                ( GetAsyncKeyState( VK_INSERT ) & 0x0001 ) || ( GetAsyncKeyState( VK_F1 ) & 0x0001 );
            if ( toggle_menu )
            {
                g_menu_open = !g_menu_open;
                g_menu_capture = false;
                if ( g_roblox )
                    SetForegroundWindow( g_roblox );
            }

            update_interactivity();

            // A mouse-up delivered to the game instead of the overlay would
            // leave ImGui believing the button is still held, which makes the
            // next click drag the wrong widget. Release it as soon as no
            // physical button is down.
            if ( !( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) && !( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) &&
                 !( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) && !( GetAsyncKeyState( VK_XBUTTON1 ) & 0x8000 ) &&
                 !( GetAsyncKeyState( VK_XBUTTON2 ) & 0x8000 ) )
            {
                ImGuiIO& io = ImGui::GetIO();
                for ( int button = 0; button < 5; ++button )
                    if ( io.MouseDown[ button ] )
                        io.AddMouseButtonEvent( button, false );
            }

            update_bounds();

            if ( !g_roblox || !IsWindow( g_roblox ) )
                break;

            features::run_aimbot();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if ( g_menu_open )
                ui::draw_menu();

            features::run_esp();
            features::draw_fov_indicator();
            features::draw_debug_hud();

            const auto now = GetTickCount64();
            if ( now - g_last_log >= 5000 )
            {
                g_last_log = now;
                log_status();
            }

            ImGui::Render();

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize( g_window, &width, &height );
            glViewport( 0, 0, width, height );
            glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
            glClear( GL_COLOR_BUFFER_BIT );
            ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
            glfwSwapBuffers( g_window );
        }
    }

    void shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if ( g_window )
            glfwDestroyWindow( g_window );
        glfwTerminate();
    }
}  // namespace ext::overlay
