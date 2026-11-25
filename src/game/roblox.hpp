#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math.hpp"

namespace ext::game
{
    // An "instance" is just a remote pointer; 0 means null.
    using instance = std::uintptr_t;

    std::uintptr_t data_model();
    std::string class_name( std::uintptr_t instance );
    std::string name( std::uintptr_t instance );
    std::vector< std::uintptr_t > children( std::uintptr_t instance );
    std::uintptr_t find_child( std::uintptr_t parent, const std::string& child_name );
    std::uintptr_t find_child_by_class( std::uintptr_t parent, const std::string& cls );

    std::uintptr_t service( const std::string& cls );
    std::uintptr_t local_player();
    std::uintptr_t player_character( std::uintptr_t player );

    Vec3 part_position( std::uintptr_t part );
    Vec3 part_size( std::uintptr_t part );
    float humanoid_health( std::uintptr_t humanoid );
    float humanoid_max_health( std::uintptr_t humanoid );

    bool view_matrix( Mat4& out );
    Vec2 dimensions();
    bool world_to_screen( const Vec3& world, Vec2& screen, bool* used_transposed = nullptr );

    std::uintptr_t roblox_window();
    bool is_foreground();

    // Prints resolved addresses / a shallow instance tree; useful for calibrating offsets.ini.
    void dump();

    // Tries every plausible FakeDataModel -> DataModel path and prints which one
    // yields a real DataModel. Used to fix the global path without guessing.
    void probe();
}  // namespace ext::game
