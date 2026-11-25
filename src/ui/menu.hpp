#pragma once

namespace ext::ui
{
    // Screen-space rectangle the menu occupied on the last frame, in the same
    // (logical) pixel space ImGui uses. The overlay uses it for hit-testing so
    // only the menu is clickable and everything else falls through to Roblox.
    struct bounds_t
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool valid() const
        {
            return width > 0.0f && height > 0.0f;
        }

        bool contains( const float px, const float py ) const
        {
            return px >= x && px < x + width && py >= y && py < y + height;
        }
    };

    void draw_menu();
    bounds_t bounds();
}  // namespace ext::ui
